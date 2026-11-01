// SPDX-License-Identifier: GPL-3.0-or-later
#include "FileAssociations.h"
#include "FileTypeIcons.h"
#include "FileIconHandlerWindows.h"
#include "ShellIntegration.h"

#include <QDir>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

using namespace FileAssociations;

namespace {
QByteArray readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}

bool writeFile(const QString &path, const QByteArray &data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}

Services windows(QSettings &store)
{
    Services services;
    services.platform = Platform::Windows;
    services.executable = "C:/Program Files/8bitPad/8bitPad.exe";
    services.registry = &store;
    services.queryDefaultProgId = [&store](const QString &extension) {
        const QString chosen = store.value("Software/Microsoft/Windows/CurrentVersion/Explorer/FileExts/."
            + extension + "/UserChoice/ProgId").toString();
        return chosen.isEmpty() ? store.value("Software/Classes/." + extension + "/.").toString() : chosen;
    };
    return services;
}

Services linuxServices(const QTemporaryDir &temp)
{
    Services services;
    services.platform = Platform::Linux;
    services.executable = "/opt/8bitPad edition/8bitPad";
    services.dataHome = temp.filePath("xdg-data");
    services.configHome = temp.filePath("xdg-config");
    services.findExecutable = [](const QString &program) { return "/fake-tools/" + program; };
    services.run = [](const QString &, const QStringList &args) {
        return CommandResult{true, 0, args.value(0) == "query" ? QString("8bitcode.desktop\n") : QString(), {}};
    };
    return services;
}
}

class FileAssociationsTest : public QObject
{
    Q_OBJECT
private slots:
    void parsesCustomBatchAtomically();
    void catalogContainsOnlyUniqueTextTypes();
    void windowsBatchAssociatesAvailableAndReportsProtected();
    void windowsRegistrationOnlyDoesNotChangeDefaults();
    void windowsCleanupRestoresPreviousDefaults();
    void windowsExistingApplicationDefaultIsRecognized();
    void windowsIconsUseEmbeddedTypeResources();
    void windowsIconRefreshPreservesDefaultsAndOtherCopies();
    void windowsIconRefreshHandlesGenericOpenWithAndEmptyRegistry();
    void windowsNativeIconsCoverSharedApplicationAndSafeFallback();
    void windowsContextToggleDoesNotChangeAssociations();
    void windowsUpgradeAdoptsOnlyRemoved8bitPadEntries();
    void windowsCleanupIncludesCustomAndPreservesSettings();
    void windowsUnverifiedChangesAreRestored();
    void linuxDeduplicatesAndConfirmsMimeDefaults();
    void linuxFailuresNeverClaimDefaults();
    void linuxCustomRegistrationAndCleanupStayInXdg();
    void linuxContextToggleIsSeparate();
};

void FileAssociationsTest::parsesCustomBatchAtomically()
{
    const auto result = parseExtensions(".TXT, *.py;sql\njson txt");
    QVERIFY(result.valid());
    QCOMPARE(result.extensions, QStringList({"txt", "py", "sql", "json"}));
    for (const QString &invalid : {"txt ../evil", "*.exe", "txt *", "txt foo/bar", "txt .", "txt tar.gz", "txt bad\\name"}) {
        const auto parsed = parseExtensions(invalid);
        QVERIFY2(!parsed.valid(), qPrintable(invalid));
        QVERIFY(parsed.extensions.isEmpty());
    }
    QTemporaryDir temp;
    QSettings store(temp.filePath("registry.ini"), QSettings::IniFormat);
    QVERIFY(!Backend(windows(store)).apply({"txt", "../escape"}, false).ok());
    QVERIFY(store.allKeys().isEmpty());
}

void FileAssociationsTest::catalogContainsOnlyUniqueTextTypes()
{
    QSet<QString> seen;
    for (const auto &group : catalog()) {
        QVERIFY(!group.title.isEmpty());
        for (const QString &extension : group.extensions) {
            QVERIFY2(!seen.contains(extension), qPrintable(extension));
            seen.insert(extension);
            const auto parsed = parseExtensions(extension);
            QVERIFY(parsed.valid());
            QCOMPARE(parsed.extensions, QStringList{extension});
        }
    }
    QVERIFY(seen.size() > 100);
    QVERIFY(seen.contains("txt") && seen.contains("cpp") && seen.contains("py") && seen.contains("yaml"));
    QVERIFY(!seen.contains("exe") && !seen.contains("png") && !seen.contains("url"));
}

void FileAssociationsTest::windowsBatchAssociatesAvailableAndReportsProtected()
{
    QTemporaryDir temp;
    QSettings store(temp.filePath("registry.ini"), QSettings::IniFormat);
    const QString protectedKey = "Software/Microsoft/Windows/CurrentVersion/Explorer/FileExts/.txt/UserChoice/";
    store.setValue(protectedKey + "ProgId", "Other.Text");
    store.setValue(protectedKey + "Hash", "unchanged");
    store.setValue("Software/Classes/.txt/.", "Other.Text");
    store.setValue("Software/Classes/.py/.", "Other.Python");
    int notifications = 0;
    auto services = windows(store);
    services.associationsChanged = [&] { ++notifications; };
    Backend backend(services);
    const auto result = backend.apply({"txt", "py", "TXT"}, true);
    QVERIFY(result.ok());
    QCOMPARE(result.registeredExtensions, QStringList({"txt", "py"}));
    QVERIFY(result.defaultMimeTypes.isEmpty());
    QCOMPARE(result.defaultExtensions, QStringList{"py"});
    QCOMPARE(result.pendingDefaultExtensions, QStringList{"txt"});
    QCOMPARE(notifications, 3); // Registration, direct mappings, failed-mapping rollback.
    QCOMPARE(store.value("Software/Classes/.py/.").toString(), QString("8bitPad.py"));
    QCOMPARE(store.value("Software/8bitPad/ShellIntegration/PreviousDefaults/py/Value").toString(), QString("Other.Python"));
    QVERIFY(!store.contains("Software/8bitPad/ShellIntegration/PreviousDefaults/txt/Present"));
    QCOMPARE(store.value(protectedKey + "ProgId").toString(), QString("Other.Text"));
    QCOMPARE(store.value(protectedKey + "Hash").toString(), QString("unchanged"));
    QCOMPARE(store.value("Software/Classes/.txt/.").toString(), QString("Other.Text"));
    QVERIFY(store.contains("Software/Classes/.py/OpenWithProgids/8bitPad.py"));
    QCOMPARE(store.value("Software/8bitPad/Capabilities/FileAssociations/.txt").toString(), QString("8bitPad.txt"));
    QVERIFY(!backend.contextMenuInstalled());
}

void FileAssociationsTest::windowsRegistrationOnlyDoesNotChangeDefaults()
{
    QTemporaryDir temp;
    QSettings store(temp.filePath("registry.ini"), QSettings::IniFormat);
    store.setValue("Software/Classes/.txt/.", "Other.Text");
    auto services = windows(store);
    int queries = 0;
    services.queryDefaultProgId = [&](const QString &) { ++queries; return QString(); };
    const auto result = Backend(services).apply({"txt", "py"}, false);
    QVERIFY(result.ok());
    QCOMPARE(queries, 0);
    QVERIFY(result.defaultExtensions.isEmpty());
    QVERIFY(result.pendingDefaultExtensions.isEmpty());
    QCOMPARE(store.value("Software/Classes/.txt/.").toString(), QString("Other.Text"));
    QVERIFY(!store.contains("Software/Classes/.py/."));
    QVERIFY(!store.contains("Software/8bitPad/ShellIntegration/PreviousDefaults/txt/Present"));
}

void FileAssociationsTest::windowsCleanupRestoresPreviousDefaults()
{
    QTemporaryDir temp;
    QSettings store(temp.filePath("registry.ini"), QSettings::IniFormat);
    store.setValue("Software/Classes/.txt/.", "Other.Text");
    store.setValue("Software/Classes/.log/.", "");
    Backend backend(windows(store));
    const QStringList types{"txt", "log", "freshnote", "customnote"};
    QCOMPARE(backend.apply(types, true).defaultExtensions.size(), 4);
    // A repeated apply must not replace the original backup with our ProgID.
    QCOMPARE(backend.apply(types, true).defaultExtensions.size(), 4);
    QCOMPARE(store.value("Software/8bitPad/ShellIntegration/PreviousDefaults/txt/Value").toString(), QString("Other.Text"));
    store.setValue("Software/Classes/.customnote/.", "Later.Editor");
    QVERIFY(backend.removeOwnedRegistration().ok());
    QCOMPARE(store.value("Software/Classes/.txt/.").toString(), QString("Other.Text"));
    QVERIFY(store.contains("Software/Classes/.log/."));
    QVERIFY(store.value("Software/Classes/.log/.").toString().isEmpty());
    QVERIFY(!store.contains("Software/Classes/.freshnote/."));
    QCOMPARE(store.value("Software/Classes/.customnote/.").toString(), QString("Later.Editor"));
    QVERIFY(!store.contains("Software/8bitPad/ShellIntegration/PreviousDefaults/txt/Present"));
}

void FileAssociationsTest::windowsExistingApplicationDefaultIsRecognized()
{
    QTemporaryDir temp;
    QSettings store(temp.filePath("registry.ini"), QSettings::IniFormat);
    store.setValue("Software/Classes/.txt/.", "Other.Text");
    auto services = windows(store);
    services.queryDefaultProgId = [](const QString &) { return QStringLiteral("applications\\8bitcode.exe"); };
    const auto result = Backend(services).apply({"txt"}, true);
    QCOMPARE(result.defaultExtensions, QStringList{"txt"});
    QVERIFY(result.pendingDefaultExtensions.isEmpty());
    QCOMPARE(store.value("Software/Classes/.txt/.").toString(), QString("Other.Text"));
    QVERIFY(!store.contains("Software/8bitPad/ShellIntegration/PreviousDefaults/txt/Present"));
}

void FileAssociationsTest::windowsIconsUseEmbeddedTypeResources()
{
    QTemporaryDir temp;
    QSettings store(temp.filePath("registry.ini"), QSettings::IniFormat);
    auto services = windows(store);
    services.executable = QStringLiteral("C:/Users/Beri ö/8bitPad edition/8bitPad.exe");
    const QString prefix = '"' + QDir::toNativeSeparators(services.executable) + "\",-";
    const QStringList extensions{"txt", "log", "md", "sql", "pls", "json", "yaml", "py", "js", "ts", "cpp", "ps1", "sh", "customnote"};
    const auto result = Backend(services).apply(extensions, false);
    QVERIFY2(result.ok(), qPrintable(result.errors.join('\n')));
    QSet<QString> references;
    for (const auto &extension : extensions) {
        const QString icon = store.value("Software/Classes/8bitPad." + extension + "/DefaultIcon/.").toString();
        QCOMPARE(icon, prefix + QString::number(FileTypeIcons::forExtension(extension).resourceId));
        references.insert(icon);
        QVERIFY(!store.contains("Software/Classes/." + extension + "/DefaultIcon/."));
    }
    QVERIFY(references.size() >= 12);
    QCOMPARE(FileTypeIcons::forExtension("SQL").resourceId, FileTypeIcons::forExtension("sql").resourceId);
    QCOMPARE(FileTypeIcons::forExtension("unknown-custom").resourceId, FileTypeIcons::forExtension({}).resourceId);
    QCOMPARE(store.value("Software/Classes/Applications/8bitCode.exe/DefaultIcon/.").toString(),
        prefix + QString::number(FileTypeIcons::forExtension({}).resourceId));
    QCOMPARE(store.value("Software/8bitPad/Capabilities/ApplicationIcon").toString(),
        '"' + QDir::toNativeSeparators(services.executable) + QStringLiteral("\",0"));
}

void FileAssociationsTest::windowsIconRefreshPreservesDefaultsAndOtherCopies()
{
    QTemporaryDir temp;
    QSettings store(temp.filePath("registry.ini"), QSettings::IniFormat);
    auto services = windows(store);
    Backend setup(services);
    QVERIFY(setup.apply({"sql", "py", "customnote"}, false).ok());
    const QString userChoice = "Software/Microsoft/Windows/CurrentVersion/Explorer/FileExts/.sql/UserChoice/";
    store.setValue(userChoice + "ProgId", "Other.Editor");
    store.setValue(userChoice + "Hash", "must-not-change");
    store.setValue("Software/Classes/.sql/.", "Other.Editor");
    store.setValue("Software/8bitPad/ShellIntegration/PreviousDefaults/sql/Value", "Original.Editor");
    store.setValue("Software/Classes/8bitPad.sql/DefaultIcon/.", "old-app.exe,0");
    store.remove("Software/Classes/8bitPad.py/DefaultIcon");
    store.remove("Software/Classes/Applications/8bitCode.exe/DefaultIcon");
    store.setValue("Software/Classes/8bitPad.py/shell/open/command/.",
        windowsOpenCommand(services.executable.toUpper(), "%1"));
    // Known legacy registrations can predate the manifest.
    store.setValue("Software/Classes/8bitPad.md/shell/open/command/.", windowsOpenCommand(services.executable, "%1"));
    store.setValue("Software/Classes/8bitPad.customnote/shell/open/command/.", "\"D:\\Portable\\8bitPad.exe\" \"%1\"");
    store.setValue("Software/Classes/8bitPad.customnote/DefaultIcon/.", "portable-owned-icon");
    QMap<QString, QVariant> before;
    for (const auto &key : store.allKeys()) before.insert(key, store.value(key));
    const QSet<QString> allowed{
        "Software/Classes/8bitPad.sql/DefaultIcon/.", "Software/Classes/8bitPad.py/DefaultIcon/.",
        "Software/Classes/8bitPad.md/DefaultIcon/.", "Software/Classes/Applications/8bitCode.exe/DefaultIcon/.",
        "Software/8bitPad/Capabilities/ApplicationIcon", "Software/8bitPad/ShellIntegration/IconRevision"};
    int notifications = 0;
    services.associationsChanged = [&] { ++notifications; };
    Backend backend(services);
    const auto result = backend.refreshOwnedIcons();
    QVERIFY(result.ok());
    QCOMPARE(notifications, 1);
    QVERIFY(result.defaultExtensions.isEmpty() && result.pendingDefaultExtensions.isEmpty());
    QVERIFY(result.registeredExtensions.contains("md"));
    QVERIFY(!result.registeredExtensions.contains("customnote"));
    for (const auto &key : before.keys())
        if (!allowed.contains(key)) QCOMPARE(store.value(key), before.value(key));
    for (const auto &key : store.allKeys()) QVERIFY2(before.contains(key) || allowed.contains(key), qPrintable(key));
    QVERIFY(store.value("Software/Classes/8bitPad.sql/DefaultIcon/.").toString().endsWith(
        ",-" + QString::number(FileTypeIcons::forExtension("sql").resourceId)));
    QVERIFY(store.contains("Software/Classes/8bitPad.py/DefaultIcon/."));
    QVERIFY(store.contains("Software/Classes/Applications/8bitCode.exe/DefaultIcon/."));
    QVERIFY(backend.refreshOwnedIcons().ok());
    QCOMPARE(notifications, 1); // No cache flush on every launch.
    store.setValue("Software/8bitPad/ShellIntegration/IconRevision", "previous-artwork");
    QVERIFY(backend.refreshOwnedIcons().ok());
    QCOMPARE(notifications, 2); // Same IDs, new artwork still invalidates the cache.

    auto otherCopy = services;
    otherCopy.executable = "E:/Unregistered portable/8bitPad.exe";
    before.clear();
    for (const auto &key : store.allKeys()) before.insert(key, store.value(key));
    QVERIFY(Backend(otherCopy).refreshOwnedIcons().ok());
    QCOMPARE(notifications, 2);
    QCOMPARE(store.allKeys(), before.keys());
    for (const auto &key : before.keys()) QCOMPARE(store.value(key), before.value(key));
}

void FileAssociationsTest::windowsIconRefreshHandlesGenericOpenWithAndEmptyRegistry()
{
    QTemporaryDir temp;
    QSettings store(temp.filePath("registry.ini"), QSettings::IniFormat);
    auto services = windows(store);
    int notifications = 0;
    services.associationsChanged = [&] { ++notifications; };
    Backend backend(services);
    QVERIFY(backend.refreshOwnedIcons().ok());
    QVERIFY(store.allKeys().isEmpty());
    QCOMPARE(notifications, 0);
    store.setValue("Software/Classes/Applications/8bitCode.exe/shell/open/command/.", windowsOpenCommand(services.executable, "%1"));
    QVERIFY(backend.refreshOwnedIcons().ok());
    QVERIFY(backend.registeredExtensions().isEmpty());
    QCOMPARE(store.allKeys().size(), 3); // Only the old command, icon and revision.
    QVERIFY(store.contains("Software/Classes/Applications/8bitCode.exe/DefaultIcon/."));
    QCOMPARE(notifications, 1);
}

void FileAssociationsTest::windowsNativeIconsCoverSharedApplicationAndSafeFallback()
{
    QTemporaryDir temp;
    QSettings store(temp.filePath("registry.ini"), QSettings::IniFormat);
    auto services = windows(store);
    services.nativeIconHandler = QFileInfo(services.executable).dir().filePath(
        QString::fromLatin1(FileIconHandlerWindows::LibraryFileName));
    const QString clsid = QString::fromLatin1(FileIconHandlerWindows::ClassIdString);
    const QString com = "Software/Classes/CLSID/" + clsid;
    const QString appIcon = "Software/Classes/Applications/8bitCode.exe/DefaultIcon/.";
    const QString handlerKey = "Software/Classes/Applications/8bitCode.exe/shellex/IconHandler/.";
    const QString protectedKey = "Software/Microsoft/Windows/CurrentVersion/Explorer/FileExts/.sql/UserChoice/";
    store.setValue(protectedKey + "ProgId", "Applications\\8bitCode.exe");
    store.setValue(protectedKey + "Hash", "preserve-windows-choice");
    Backend backend(services);
    QVERIFY(backend.apply({"sql", "py"}, true).ok());
    QCOMPARE(store.value(appIcon).toString(), QString("%1"));
    QCOMPARE(store.value(handlerKey).toString(), clsid);
    QCOMPARE(store.value(com + "/InprocServer32/.").toString(), QDir::toNativeSeparators(services.nativeIconHandler));
    QCOMPARE(store.value(com + "/InprocServer32/ThreadingModel").toString(), QString("Apartment"));
    QCOMPARE(store.value(protectedKey + "Hash").toString(), QString("preserve-windows-choice"));
    QVERIFY(store.value("Software/Classes/8bitPad.sql/DefaultIcon/.").toString().endsWith(
        ",-" + QString::number(FileTypeIcons::forExtension("sql").resourceId)));

    // A normal launch upgrades an old generic application record too.
    store.remove("Software/Classes/Applications/8bitCode.exe/shellex/IconHandler");
    store.setValue(appIcon, "old-app.exe,0");
    QVERIFY(backend.refreshOwnedIcons().ok());
    QCOMPARE(store.value(appIcon).toString(), QString("%1"));
    QCOMPARE(store.value(handlerKey).toString(), clsid);

    // Older portable inventories or a non-native CPU get a real static icon.
    auto noHandler = services;
    noHandler.nativeIconHandler.clear();
    QVERIFY(Backend(noHandler).refreshOwnedIcons().ok());
    QVERIFY(!store.contains(handlerKey));
    QVERIFY(store.value(appIcon).toString().endsWith(
        ",-" + QString::number(FileTypeIcons::forExtension({}).resourceId)));

    // Startup maintenance cannot replace a separately installed icon provider.
    store.setValue(handlerKey, "{OTHER-ICON-PROVIDER}");
    store.setValue(appIcon, "foreign-icon.ico");
    QVERIFY(backend.refreshOwnedIcons().ok());
    QCOMPARE(store.value(handlerKey).toString(), QString("{OTHER-ICON-PROVIDER}"));
    QCOMPARE(store.value(appIcon).toString(), QString("foreign-icon.ico"));
    store.setValue("Software/Classes/CLSID/{OTHER-ICON-PROVIDER}/InprocServer32/.", "keep.dll");
    QVERIFY(backend.removeOwnedRegistration().ok());
    QVERIFY(!store.contains(com + "/InprocServer32/."));
    QCOMPARE(store.value("Software/Classes/CLSID/{OTHER-ICON-PROVIDER}/InprocServer32/.").toString(), QString("keep.dll"));
    QCOMPARE(store.value(protectedKey + "Hash").toString(), QString("preserve-windows-choice"));
}

void FileAssociationsTest::windowsUpgradeAdoptsOnlyRemoved8bitPadEntries()
{
    QTemporaryDir temp;
    QSettings store(temp.filePath("registry.ini"), QSettings::IniFormat);
    auto portable = windows(store);
    portable.executable = "D:/Portable 8bitCode/8bitCode.exe";
    auto installed = windows(store);
    installed.executable = "C:/Fixture/AppData/Local/Programs/8bitCode/bin/8bitCode.exe";
    const QString removed = "C:/Fixture/AppData/Local/Programs/8bitPad/bin/8bitPad.exe";
    const QString classes = "Software/Classes/";
    store.setValue(classes + "8bitPad.py/shell/open/command/.", windowsOpenCommand(removed, "%1"));
    store.setValue(classes + "8bitPad.md/shell/open/command/.", windowsOpenCommand("D:/Portable/8bitPad.exe", "%1"));
    store.setValue(classes + "*/shell/8bitPad/command/.", windowsOpenCommand(removed, "%1"));
    store.setValue(classes + "Directory/Background/shell/8bitPad/command/.", windowsOpenCommand(removed, "%V"));

    QVERIFY(Backend(portable).refreshOwnedIcons().ok());
    QCOMPARE(store.value(classes + "8bitPad.py/shell/open/command/.").toString(), windowsOpenCommand(removed, "%1"));

    QVERIFY(Backend(installed).refreshOwnedIcons().ok());
    QCOMPARE(store.value(classes + "8bitPad.py/shell/open/command/.").toString(), windowsOpenCommand(installed.executable, "%1"));
    QVERIFY(store.value(classes + "8bitPad.py/DefaultIcon/.").toString().contains("8bitCode.exe"));
    QCOMPARE(store.value(classes + "8bitPad.md/shell/open/command/.").toString(), windowsOpenCommand("D:/Portable/8bitPad.exe", "%1"));
    QCOMPARE(store.value(classes + "*/shell/8bitPad/command/.").toString(), windowsOpenCommand(installed.executable, "%1"));
    QCOMPARE(store.value(classes + "Directory/Background/shell/8bitPad/command/.").toString(), windowsOpenCommand(installed.executable, "%V"));
    QVERIFY(store.value(classes + "*/shell/8bitPad/Icon").toString().contains("8bitCode.exe"));
    QVERIFY(!store.contains(classes + "Directory/shell/8bitPad/command/."));
}

void FileAssociationsTest::windowsContextToggleDoesNotChangeAssociations()
{
    QTemporaryDir temp;
    QSettings store(temp.filePath("registry.ini"), QSettings::IniFormat);
    Backend backend(windows(store));
    QVERIFY(backend.setContextMenuEnabled(true).ok());
    QVERIFY(backend.contextMenuInstalled());
    QVERIFY(backend.registeredExtensions().isEmpty());
    QVERIFY(!store.contains("Software/RegisteredApplications/8bitPad"));
    QVERIFY(backend.apply({"txt", "customnote"}).ok());
    QVERIFY(backend.setContextMenuEnabled(false).ok());
    QVERIFY(!backend.contextMenuInstalled());
    QCOMPARE(backend.registeredExtensions(), QStringList({"txt", "customnote"}));
    QVERIFY(store.contains("Software/Classes/.customnote/OpenWithProgids/8bitPad.customnote"));
}

void FileAssociationsTest::windowsCleanupIncludesCustomAndPreservesSettings()
{
    QTemporaryDir temp;
    QSettings store(temp.filePath("registry.ini"), QSettings::IniFormat);
    store.setValue("Software/8bitPad/Theme", "ayu");
    store.setValue("Software/Classes/.customnote/OpenWithProgids/Other.Editor", "");
    store.setValue("Software/Classes/.customnote/.", "Other.Editor");
    Backend backend(windows(store));
    QVERIFY(backend.apply({"txt", "customnote"}).ok());
    QVERIFY(backend.removeOwnedRegistration().ok());
    QCOMPARE(store.value("Software/8bitPad/Theme").toString(), QString("ayu"));
    QCOMPARE(store.value("Software/Classes/.customnote/.").toString(), QString("Other.Editor"));
    QVERIFY(store.contains("Software/Classes/.customnote/OpenWithProgids/Other.Editor"));
    QVERIFY(!store.contains("Software/Classes/.customnote/OpenWithProgids/8bitPad.customnote"));
    QVERIFY(!store.contains("Software/Classes/8bitPad.customnote/shell/open/command/."));
    QVERIFY(backend.registeredExtensions().isEmpty());
}

void FileAssociationsTest::windowsUnverifiedChangesAreRestored()
{
    QTemporaryDir temp;
    QSettings store(temp.filePath("registry.ini"), QSettings::IniFormat);
    store.setValue("Software/Classes/.txt/.", "Other.Text");
    auto services = windows(store);
    services.queryDefaultProgId = [](const QString &) { return QString(); };
    const auto result = Backend(services).apply({"txt", "py"}, true);
    QVERIFY(result.ok()); // Registration succeeded; defaults are explicitly pending.
    QCOMPARE(result.registeredExtensions.size(), 2);
    QVERIFY(result.defaultExtensions.isEmpty());
    QCOMPARE(result.pendingDefaultExtensions, QStringList({"txt", "py"}));
    QCOMPARE(store.value("Software/Classes/.txt/.").toString(), QString("Other.Text"));
    QVERIFY(!store.contains("Software/Classes/.py/."));
    QVERIFY(!store.contains("Software/8bitPad/ShellIntegration/PreviousDefaults/txt/Present"));
    QVERIFY(!store.contains("Software/8bitPad/ShellIntegration/PreviousDefaults/py/Present"));
}

void FileAssociationsTest::linuxDeduplicatesAndConfirmsMimeDefaults()
{
    QTemporaryDir temp;
    auto services = linuxServices(temp);
    QList<QStringList> defaults;
    services.run = [&](const QString &program, const QStringList &args) {
        if (program.endsWith("xdg-mime") && args.value(0) == "default") defaults << args;
        return CommandResult{true, 0, args.value(0) == "query" ? QString("8bitcode.desktop\n") : QString(), {}};
    };
    const auto result = Backend(services).apply({"txt", "log", "ini", "py"}, true);
    QVERIFY2(result.ok(), qPrintable(result.errors.join('\n')));
    QCOMPARE(defaults.size(), 2);
    QCOMPARE(result.defaultMimeTypes, QStringList({"text/plain", "text/x-python"}));
    const QByteArray entry = readFile(services.dataHome + "/applications/8bitcode.desktop");
    QVERIFY(entry.contains("text/plain"));
    QVERIFY(entry.contains("text/x-python"));
    QVERIFY(!entry.contains("inode/directory"));
    const QByteArray globs = readFile(services.dataHome + "/mime/packages/8bitpad-types.xml");
    QVERIFY(globs.contains("*.txt") && globs.contains("*.log") && globs.contains("*.py"));
    for (const auto &args : defaults) QVERIFY(!args.contains("inode/directory"));
}

void FileAssociationsTest::linuxFailuresNeverClaimDefaults()
{
    QTemporaryDir temp;
    auto services = linuxServices(temp);
    services.run = [](const QString &program, const QStringList &args) {
        if (program.endsWith("xdg-mime") && args.value(0) == "default") return CommandResult{true, 4, {}, "desktop refused"};
        return CommandResult{true, 0, {}, {}};
    };
    const auto refused = Backend(services).apply({"txt"}, true);
    QVERIFY(!refused.ok());
    QVERIFY(refused.defaultMimeTypes.isEmpty());
    QCOMPARE(refused.registeredExtensions, QStringList{"txt"});
    services.run = [](const QString &, const QStringList &args) {
        return CommandResult{true, 0, args.value(0) == "query" ? QString("other.desktop") : QString(), {}};
    };
    const auto unchanged = Backend(services).apply({"txt"}, true);
    QVERIFY(!unchanged.ok());
    QVERIFY(unchanged.defaultMimeTypes.isEmpty());
    services.findExecutable = [](const QString &) { return QString(); };
    const auto missing = Backend(services).apply({"txt"}, true);
    QVERIFY(!missing.ok());
    QVERIFY(missing.defaultMimeTypes.isEmpty());
}

void FileAssociationsTest::linuxCustomRegistrationAndCleanupStayInXdg()
{
    QTemporaryDir temp;
    auto services = linuxServices(temp);
    services.executable = "/opt/8bitPad % edition/8bitPad";
    Backend backend(services);
    const auto result = backend.apply({"customnote", "md", "jsonc", "tsx", "ts"});
    QVERIFY2(result.ok(), qPrintable(result.errors.join('\n')));
    const QByteArray entry = readFile(services.dataHome + "/applications/8bitcode.desktop");
    QVERIFY(entry.contains("application/x-8bitpad-customnote"));
    QVERIFY(entry.contains("8bitPad %% edition"));
    QVERIFY(QFileInfo::exists(services.dataHome + "/mime/packages/8bitpad-types.xml"));
    const QByteArray aliases = readFile(services.dataHome + "/mime/packages/8bitpad-types.xml");
    QVERIFY(aliases.contains("*.jsonc") && aliases.contains("*.tsx") && aliases.contains("*.ts"));
    const QString mimeapps = services.configHome + "/mimeapps.list";
    QVERIFY(writeFile(mimeapps, "# keep comment\n[Default Applications]\ntext/plain=8bitcode.desktop;other.desktop;\nimage/png=viewer.desktop;\n[Added Associations]\ntext/markdown=other.desktop;8bitcode.desktop;8bitpad.desktop;\n"));
    QVERIFY(writeFile(services.dataHome + "/mime/packages/other-app.xml", "keep"));
    QVERIFY(writeFile(services.dataHome + "/applications/8bitpad.desktop", "[Desktop Entry]\nX-8bitPad-Managed=true\n"));
    QVERIFY(writeFile(services.dataHome + "/applications/8bitpad-folder.desktop", "[Desktop Entry]\nName=Not ours\n"));
    QVERIFY(backend.removeOwnedRegistration().ok());
    const QByteArray after = readFile(mimeapps);
    QVERIFY(after.contains("text/plain=other.desktop;"));
    QVERIFY(after.contains("image/png=viewer.desktop;"));
    QVERIFY(after.contains("# keep comment"));
    QVERIFY(!after.contains("8bitcode.desktop") && !after.contains("8bitpad.desktop"));
    QVERIFY(!QFileInfo::exists(services.dataHome + "/applications/8bitpad.desktop"));
    QVERIFY(QFileInfo::exists(services.dataHome + "/applications/8bitpad-folder.desktop"));
    QCOMPARE(readFile(services.dataHome + "/mime/packages/other-app.xml"), QByteArray("keep"));
    QVERIFY(!QFileInfo::exists(services.dataHome + "/applications/8bitcode.desktop"));
}

void FileAssociationsTest::linuxContextToggleIsSeparate()
{
    QTemporaryDir temp;
    auto services = linuxServices(temp);
    Backend backend(services);
    QVERIFY(backend.setContextMenuEnabled(true).ok());
    QVERIFY(backend.contextMenuInstalled());
    QVERIFY(backend.registeredExtensions().isEmpty());
    const QByteArray folder = readFile(services.dataHome + "/applications/8bitcode-folder.desktop");
    QVERIFY(folder.contains("inode/directory"));
    QVERIFY(backend.apply({"txt"}).ok());
    const QByteArray association = readFile(services.dataHome + "/applications/8bitcode.desktop");
    QVERIFY(backend.setContextMenuEnabled(false).ok());
    QVERIFY(!backend.contextMenuInstalled());
    QCOMPARE(readFile(services.dataHome + "/applications/8bitcode.desktop"), association);
    QCOMPARE(backend.registeredExtensions(), QStringList{"txt"});
}

QTEST_GUILESS_MAIN(FileAssociationsTest)
#include "FileAssociationsTest.moc"
