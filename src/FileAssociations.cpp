// SPDX-License-Identifier: GPL-3.0-or-later
#include "FileAssociations.h"
#include "FileTypeIcons.h"
#include "FileIconHandlerWindows.h"
#include "ShellIntegration.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeDatabase>
#include <QMimeType>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <memory>
#include <string>
#include <utility>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#endif

namespace {
const QString classes = QStringLiteral("Software/Classes/");
const QString capabilities = QStringLiteral("Software/8bitPad/Capabilities/");
const QString manifestKey = QStringLiteral("Software/8bitPad/ShellIntegration/Extensions");
const QString registeredAppKey = QStringLiteral("Software/RegisteredApplications/8bitPad");
const QString capabilitiesValue = QStringLiteral("Software\\8bitPad\\Capabilities");
const QString desktopId = QStringLiteral("8bitpad.desktop");
const QString folderDesktopId = QStringLiteral("8bitpad-folder.desktop");
const QString iconRevisionKey = QStringLiteral("Software/8bitPad/ShellIntegration/IconRevision");
const QString iconHandlerClass = QString::fromLatin1(FileIconHandlerWindows::ClassIdString);
const QString iconHandlerComKey = classes + "CLSID/" + iconHandlerClass;
const QString applicationIconHandlerKey = classes + "Applications/8bitPad.exe/shellex/IconHandler/.";

QString iconReference(const QString &executable, int resourceId)
{
    // Index zero is the application icon. -1 is special in some Shell APIs
    // (it counts icon groups), so do not use it to address resource ID 1.
    if (resourceId == FileTypeIcons::ApplicationResourceId)
        return '"' + QDir::toNativeSeparators(executable) + QStringLiteral("\",0");
    // Negative IDs address stable RT_GROUP_ICON resources, not their order.
    return '"' + QDir::toNativeSeparators(executable) + QStringLiteral("\",-%1").arg(resourceId);
}

bool validExecutable(const QString &executable)
{
    return !executable.isEmpty() && !executable.contains('"') &&
        !executable.contains('\n') && !executable.contains('\r');
}

QString expectedIconHandler(const FileAssociations::Services &services)
{
    return QDir(QFileInfo(QDir::fromNativeSeparators(services.executable)).path()).filePath(
        QString::fromLatin1(FileIconHandlerWindows::LibraryFileName));
}

QString nativeIconHandler(const FileAssociations::Services &services)
{
    const QString path = QDir::fromNativeSeparators(services.nativeIconHandler);
    return validExecutable(path) && path.compare(expectedIconHandler(services), Qt::CaseInsensitive) == 0
        ? QDir::toNativeSeparators(path) : QString();
}

QStringList unique(QStringList values)
{
    values.removeDuplicates();
    return values;
}

QString desktopProgram(const QString &path)
{
    const QString native = QDir::fromNativeSeparators(path);
    static const QRegularExpression plain(QStringLiteral("^[A-Za-z0-9_./-]+$"));
    if (plain.match(native).hasMatch()) return native;
    QString result = "\"";
    for (QChar ch : native) {
        if (ch == '\\') result += QString(4, '\\');
        else if (ch == '"' || ch == '$' || ch == '`') result += QString(2, '\\') + ch;
        else if (ch == '%') result += "%%";
        else if (ch == '\n') result += "\\n";
        else if (ch == '\r') result += "\\r";
        else result += ch;
    }
    return result + '"';
}

QString desktopEntry(const QString &executable, const QStringList &mimes, bool folders = false)
{
    return QStringLiteral("[Desktop Entry]\nVersion=1.0\nType=Application\nName=%1\nGenericName=Text Editor\n"
        "Comment=Open %2 in 8bitCode\nExec=%3 %F\nTerminal=false\nCategories=Development;TextEditor;\n"
        "MimeType=%4;\nStartupNotify=true\nX-8bitPad-Managed=true\n")
        .arg(folders ? QStringLiteral("8bitCode Folder") : QStringLiteral("8bitCode"),
             folders ? QStringLiteral("folders") : QStringLiteral("text and code files"), desktopProgram(executable), mimes.join(';'));
}

bool writeAtomic(const QString &path, const QByteArray &contents, QStringList &errors)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        errors << QObject::tr("Could not create the registration directory: %1").arg(QFileInfo(path).absolutePath());
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(contents) != contents.size() || !file.commit()) {
        errors << QObject::tr("Could not save %1: %2").arg(path, file.errorString());
        return false;
    }
    return true;
}

QString manifestPath(const FileAssociations::Services &s) { return QDir(s.dataHome).filePath("8bitpad/file-associations.json"); }
QString applicationsPath(const FileAssociations::Services &s) { return QDir(s.dataHome).filePath("applications"); }

bool syncRegistry(QSettings *registry, QStringList &errors)
{
    if (!registry) { errors << QObject::tr("The user registration store is unavailable."); return false; }
    registry->sync();
    if (registry->status() != QSettings::NoError) {
        errors << QObject::tr("Some registration changes could not be saved. Check permissions; earlier changes may already have been written.");
        return false;
    }
    return true;
}

bool runTool(const FileAssociations::Services &s, const QString &name, const QStringList &arguments,
             QStringList &errors, QString *output = nullptr)
{
    const QString program = s.findExecutable ? s.findExecutable(name) : QString();
    if (program.isEmpty() || !s.run) {
        errors << QObject::tr("%1 was not found; the requested operation was not completed.").arg(name);
        return false;
    }
    const auto result = s.run(program, arguments);
    if (!result.started || result.exitCode != 0) {
        errors << QObject::tr("%1 failed (exit %2): %3").arg(name).arg(result.exitCode).arg(result.error);
        return false;
    }
    if (output) *output = result.standardOutput.trimmed();
    return true;
}

void removeFile(const QString &path, QStringList &errors)
{
    if (!QFileInfo::exists(path)) return;
    QFile file(path);
    if (!file.remove()) errors << QObject::tr("Could not remove %1: %2").arg(path, file.errorString());
}

void removeDesktopReferences(const QString &path, const QSet<QString> &ids, QStringList &errors)
{
    QFile file(path);
    if (!file.exists()) return;
    if (!file.open(QIODevice::ReadOnly)) { errors << QObject::tr("Could not read %1: %2").arg(path, file.errorString()); return; }
    const QByteArray original = file.readAll();
    file.close();
    QStringList output;
    bool associationGroup = false;
    bool changed = false;
    for (const QString &line : QString::fromUtf8(original).split('\n')) {
        const QString trimmed = line.trimmed();
        if (trimmed.startsWith('[')) associationGroup = trimmed == "[Default Applications]" || trimmed == "[Added Associations]" || trimmed == "[Removed Associations]";
        const qsizetype equal = line.indexOf('=');
        if (associationGroup && equal > 0 && !trimmed.startsWith('#')) {
            QStringList applications = line.mid(equal + 1).trimmed().split(';', Qt::SkipEmptyParts);
            const qsizetype count = applications.size();
            applications.removeIf([&](const QString &application) { return ids.contains(application); });
            if (applications.size() != count) {
                changed = true;
                if (!applications.isEmpty()) output << line.left(equal + 1) + applications.join(';') + ';';
                continue;
            }
        }
        output << line;
    }
    if (changed) writeAtomic(path, output.join('\n').toUtf8(), errors);
}

FileAssociations::Services systemServices(QSettings *registry)
{
    FileAssociations::Services s;
#ifdef Q_OS_WIN
    s.platform = FileAssociations::Platform::Windows;
#else
    s.platform = FileAssociations::Platform::Linux;
#endif
    s.registry = registry;
    s.executable = QCoreApplication::applicationFilePath();
#ifdef Q_OS_WIN
    SYSTEM_INFO systemInfo{};
    GetNativeSystemInfo(&systemInfo);
#if defined(_M_ARM64) || defined(__aarch64__)
    constexpr WORD applicationArchitecture = PROCESSOR_ARCHITECTURE_ARM64;
#elif defined(_M_X64) || defined(__x86_64__)
    constexpr WORD applicationArchitecture = PROCESSOR_ARCHITECTURE_AMD64;
#else
    constexpr WORD applicationArchitecture = PROCESSOR_ARCHITECTURE_INTEL;
#endif
    const QString handler = expectedIconHandler(s);
    if (systemInfo.wProcessorArchitecture == applicationArchitecture && QFileInfo(handler).isFile())
        s.nativeIconHandler = handler;
#endif
    s.dataHome = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    s.configHome = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    s.findExecutable = [](const QString &name) { return QStandardPaths::findExecutable(name); };
#ifdef Q_OS_WIN
    s.queryDefaultProgId = [](const QString &extension) {
        const std::wstring suffix = ('.' + extension).toStdWString();
        DWORD size = 0;
        if (AssocQueryStringW(ASSOCF_NONE, ASSOCSTR_PROGID, suffix.c_str(), nullptr, nullptr, &size) != S_FALSE
            || size == 0 || size > 32768)
            return QString{};
        std::wstring buffer(size, L'\0');
        if (FAILED(AssocQueryStringW(ASSOCF_NONE, ASSOCSTR_PROGID, suffix.c_str(), nullptr, buffer.data(), &size)))
            return QString{};
        return QString::fromWCharArray(buffer.c_str());
    };
    s.associationsChanged = [] { SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr); };
#endif
    s.run = [](const QString &program, const QStringList &arguments) {
        FileAssociations::CommandResult result;
        QProcess process;
        process.setProgram(program);
        process.setArguments(arguments);
        process.start();
        if (!process.waitForStarted(3000)) { result.error = process.errorString(); return result; }
        result.started = true;
        if (!process.waitForFinished(10000)) {
            process.kill(); process.waitForFinished(1000);
            result.error = QObject::tr("The command timed out.");
            return result;
        }
        result.exitCode = process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1;
        result.standardOutput = QString::fromUtf8(process.readAllStandardOutput());
        result.error = QString::fromUtf8(process.readAllStandardError()).trimmed();
        return result;
    };
    return s;
}

std::unique_ptr<QSettings> systemRegistry()
{
#ifdef Q_OS_WIN
    return std::make_unique<QSettings>(QStringLiteral("HKEY_CURRENT_USER"), QSettings::NativeFormat);
#else
    return {};
#endif
}
}

QString windowsOpenCommand(const QString &executable, const QString &argumentToken)
{
    return '"' + QDir::toNativeSeparators(executable) + QStringLiteral("\" \"") + argumentToken + '"';
}

QString mimeTypeForExtension(const QString &extension)
{
    const auto parsed = FileAssociations::parseExtensions(extension);
    if (!parsed.valid() || parsed.extensions.size() != 1) return {};
    const QString ext = parsed.extensions.first();
    static const QHash<QString, QString> known = {
        {"txt","text/plain"},{"log","text/plain"},{"ini","text/plain"},{"cfg","text/plain"},{"conf","text/plain"},
        {"md","text/markdown"},{"markdown","text/markdown"},{"mdown","text/markdown"},
        {"json","application/json"},{"jsonc","application/json"},{"xml","application/xml"},
        {"html","text/html"},{"htm","text/html"},{"xhtml","application/xhtml+xml"},{"css","text/css"},
        {"js","text/javascript"},{"mjs","text/javascript"},{"cjs","text/javascript"},{"jsx","text/jsx"},
        {"ts","text/typescript"},{"tsx","text/tsx"},{"cpp","text/x-c++src"},{"cc","text/x-c++src"},{"cxx","text/x-c++src"},
        {"h","text/x-c++hdr"},{"hh","text/x-c++hdr"},{"hpp","text/x-c++hdr"},{"hxx","text/x-c++hdr"},
        {"c","text/x-csrc"},{"py","text/x-python"},{"pyw","text/x-python"},{"lua","text/x-lua"},{"rs","text/rust"},
        {"go","text/x-go"},{"java","text/x-java"},{"cs","text/x-csharp"},{"sh","application/x-shellscript"},
        {"bash","application/x-shellscript"},{"bsh","application/x-shellscript"},{"zsh","application/x-shellscript"},
        {"sql","application/sql"},{"pls","application/sql"},{"pgsql","application/sql"},{"mssql","application/sql"},
        {"yml","application/yaml"},{"yaml","application/yaml"},{"toml","application/toml"},
        {"rb","application/x-ruby"},{"rbw","application/x-ruby"},{"pl","application/x-perl"},{"pm","application/x-perl"},
        {"php","application/x-php"},{"svg","image/svg+xml"},{"tex","text/x-tex"},{"diff","text/x-patch"},{"patch","text/x-patch"}
    };
    if (known.contains(ext)) return known.value(ext);
    const QMimeType detected = QMimeDatabase().mimeTypeForFile("file." + ext, QMimeDatabase::MatchExtension);
    if (detected.isValid() && !detected.isDefault() && detected.inherits("text/plain")) return detected.name();
    return "application/x-8bitpad-" + ext;
}

QString linuxDesktopEntry(const QString &executable)
{
    // Compatibility helper for the existing folder-aware desktop entry tests.
    // Batch file defaults use a separate entry without inode/directory.
    return desktopEntry(executable, {"text/plain", "text/markdown", "application/json", "application/xml", "inode/directory"});
}

namespace FileAssociations {
QList<Group> catalog()
{
    return {
        {"text", QObject::tr("Text, notes and configuration"), {"txt","log","md","markdown","mdown","ini","cfg","conf","properties","nfo","t2t","inf","wer"}},
        {"web", QObject::tr("Web and JavaScript"), {"html","htm","shtml","shtm","xhtml","xht","hta","vue","css","scss","less","js","mjs","cjs","jsm","jsx","ts","tsx","coffee","litcoffee","asp","as","mx"}},
        {"data", QObject::tr("Data, markup and automation"), {"json","jsonc","xml","yaml","yml","toml","xaml","xsl","xslt","xsd","xul","kml","svg","mxml","xsml","wsdl","xlf","xliff","xbl","sxbl","sitemap","gml","gpx","plist"}},
        {"systems", QObject::tr("C family and systems languages"), {"c","lex","cpp","cxx","cc","h","hh","hpp","hxx","ino","cs","rs","go","d","m","mm","swift","asm","mms","rc"}},
        {"scripts", QObject::tr("Python, Ruby and scripting"), {"py","pyw","rb","rbw","lua","pl","pm","plx","php","php3","php4","php5","phps","phpt","phtml","tcl","au3","kix","avs","avsi","osx","r2","r3","reb"}},
        {"shell", QObject::tr("Shell, build and DevOps"), {"sh","bash","bsh","csh","zsh","bash_profile","bashrc","profile","bat","cmd","nt","ps1","psm1","psd1","cmake","mak","mk","gradle","nix","nsi","nsh","iss","reg","diff","patch"}},
        {"jvm", QObject::tr("Java and JVM"), {"java","groovy","gvy","gy","gsh","kt","kts","scala"}},
        {"functional", QObject::tr("Functional and other programming languages"), {"hs","lhs","las","ml","mli","sml","thy","erl","hrl","lsp","lisp","scm","smd","ss","clj","cljs","nim","ada","ads","adb","pas","pp","p","inc","lpr","vb","vbs","bas","bi","bb","pb","st","forth","pro","cl","i","pack","ph","src","em","bc","cln"}},
        {"sql", QObject::tr("SQL and databases"), {"sql","pls","pgsql","mssql"}},
        {"science", QObject::tr("Scientific, hardware and document sources"), {"r","s","splus","f","for","f90","f95","f2k","f23","f77","tex","sty","ps","v","sv","vh","svh","vhd","vhdl","hex","mot","srec","tek","inp","mib","orc","sco","csd","scp","tab","spf","cbl","cbd","cdb","cdc","cob","cpy","copy","lst","vcproj","vcxproj","csproj","csxproj","vbproj","dbproj"}}
    };
}

ParsedExtensions parseExtensions(const QString &text)
{
    ParsedExtensions result;
    static const QRegularExpression separators(QStringLiteral("[,;\\s]+"));
    static const QRegularExpression valid(QStringLiteral("^[a-z0-9][a-z0-9_+-]{0,63}$"));
    static const QSet<QString> binary = {"exe","dll","com","msi","lnk","url","sys","so","dylib","bin","obj","o","a","lib",
        "png","jpg","jpeg","gif","webp","ico","bmp","mp3","mp4","wav","avi","zip","7z","rar","gz","pdf","docx","xlsx","pptx","db","sqlite"};
    for (QString extension : text.toLower().split(separators, Qt::SkipEmptyParts)) {
        if (extension.startsWith("*.")) extension.remove(0, 2);
        else if (extension.startsWith('.')) extension.remove(0, 1);
        if (!valid.match(extension).hasMatch() || binary.contains(extension)) {
            result.extensions.clear();
            result.error = QObject::tr("Use text or code extensions such as txt, .py or *.sql. Invalid or unsupported type: %1").arg(extension);
            return result;
        }
        if (!result.extensions.contains(extension)) result.extensions << extension;
        if (result.extensions.size() > 512) { result.extensions.clear(); result.error = QObject::tr("Choose no more than 512 file types at once."); return result; }
    }
    return result;
}

Backend::Backend(Services services) : services(std::move(services)) {}

QStringList Backend::registeredExtensions() const
{
    QStringList extensions;
    if (services.platform == Platform::Windows) {
        if (!services.registry) return {};
        extensions = services.registry->value(manifestKey).toStringList();
        services.registry->beginGroup(capabilities + "FileAssociations");
        for (const QString &key : services.registry->childKeys()) {
            const auto parsed = parseExtensions(key);
            if (parsed.valid() && parsed.extensions.size() == 1 && services.registry->value(key).toString() == "8bitPad." + parsed.extensions.first())
                extensions << parsed.extensions.first();
        }
        services.registry->endGroup();
    } else {
        QFile file(manifestPath(services));
        if (file.open(QIODevice::ReadOnly))
            for (const auto &item : QJsonDocument::fromJson(file.readAll()).object().value("extensions").toArray()) extensions << item.toString();
    }
    const auto parsed = parseExtensions(extensions.join(' '));
    return parsed.valid() ? parsed.extensions : QStringList{};
}

BatchResult Backend::apply(const QStringList &extensions, bool requestDefaults)
{
    BatchResult result;
    const auto parsed = parseExtensions(extensions.join(' '));
    if (!parsed.valid()) { result.errors << parsed.error; return result; }
    if (parsed.extensions.isEmpty()) { result.errors << QObject::tr("Select at least one file type."); return result; }
    if (!validExecutable(services.executable)) {
        result.errors << QObject::tr("The application executable path cannot be registered."); return result;
    }
    const QStringList combined = unique(registeredExtensions() + parsed.extensions);
    if (services.platform == Platform::Windows) {
        auto *registry = services.registry;
        if (!registry) { result.errors << QObject::tr("The user registration store is unavailable."); return result; }
        const QString command = windowsOpenCommand(services.executable, "%1");
        registry->setValue(capabilities + "ApplicationName", "8bitCode");
        registry->setValue(capabilities + "ApplicationDescription", "8bitCode text and code editor");
        registry->setValue(capabilities + "ApplicationIcon", iconReference(services.executable, FileTypeIcons::ApplicationResourceId));
        registry->setValue(registeredAppKey, capabilitiesValue);
        registry->setValue(classes + "Applications/8bitPad.exe/FriendlyAppName", "8bitCode");
        registry->setValue(classes + "Applications/8bitPad.exe/DefaultIcon/.",
            iconReference(services.executable, FileTypeIcons::forExtension({}).resourceId));
        const QString handler = nativeIconHandler(services);
        if (!handler.isEmpty()) {
            registry->setValue(iconHandlerComKey + "/.", "8bitCode file icons");
            registry->setValue(iconHandlerComKey + "/InprocServer32/.", handler);
            registry->setValue(iconHandlerComKey + "/InprocServer32/ThreadingModel", "Apartment");
            registry->setValue(applicationIconHandlerKey, iconHandlerClass);
            registry->setValue(classes + "Applications/8bitPad.exe/DefaultIcon/.", "%1");
        } else if (registry->value(applicationIconHandlerKey).toString().compare(iconHandlerClass, Qt::CaseInsensitive) == 0) {
            registry->remove(classes + "Applications/8bitPad.exe/shellex/IconHandler");
        }
        registry->setValue(classes + "Applications/8bitPad.exe/shell/open/command/.", command);
        for (const QString &extension : parsed.extensions) {
            const QString progId = "8bitPad." + extension;
            const auto &type = FileTypeIcons::forExtension(extension);
            registry->setValue(classes + progId + "/.", QStringLiteral("8bitCode %1 document").arg(type.label));
            registry->setValue(classes + progId + "/DefaultIcon/.", iconReference(services.executable, type.resourceId));
            registry->setValue(classes + progId + "/shell/open/command/.", command);
            registry->setValue(classes + '.' + extension + "/OpenWithProgids/" + progId, QString());
            registry->setValue(classes + "Applications/8bitPad.exe/SupportedTypes/." + extension, QString());
            registry->setValue(capabilities + "FileAssociations/." + extension, progId);
        }
        registry->setValue(manifestKey, combined);
        if (!syncRegistry(registry, result.errors)) return result;
        if (services.associationsChanged) services.associationsChanged();
        result.registeredExtensions = parsed.extensions;
        if (requestDefaults) {
            struct Change {
                QString extension;
                QVariant previous;
                bool present;
                bool backupCreated;
            };
            QList<Change> changes;
            const QString backups = QStringLiteral("Software/8bitPad/ShellIntegration/PreviousDefaults/");
            const auto isOurs = [](const QString &actual, const QString &extension) {
                return actual.compare("8bitPad." + extension, Qt::CaseInsensitive) == 0
                    || actual.compare(QStringLiteral("Applications\\8bitPad.exe"), Qt::CaseInsensitive) == 0;
            };
            for (const QString &extension : parsed.extensions) {
                if (services.queryDefaultProgId && isOurs(services.queryDefaultProgId(extension), extension)) {
                    result.defaultExtensions << extension;
                    continue;
                }
                const QString key = classes + '.' + extension + "/.";
                const bool present = registry->contains(key);
                const QVariant previous = registry->value(key);
                const QString backup = backups + extension + '/';
                const bool saveBackup = !registry->contains(backup + "Present")
                    && previous.toString().compare("8bitPad." + extension, Qt::CaseInsensitive) != 0;
                if (saveBackup) {
                    registry->setValue(backup + "Present", present);
                    if (present) registry->setValue(backup + "Value", previous);
                }
                changes.append({extension, previous, present, saveBackup});
                // Set the per-user file-type mapping. Protected Windows defaults
                // can override it; never write UserChoice, its hash or its ACL.
                registry->setValue(key, "8bitPad." + extension);
            }
            if (!changes.isEmpty()) {
                if (!syncRegistry(registry, result.errors)) return result;
                if (services.associationsChanged) services.associationsChanged();
            }
            bool restored = false;
            for (const Change &change : std::as_const(changes)) {
                const QString actual = services.queryDefaultProgId ? services.queryDefaultProgId(change.extension) : QString();
                if (isOurs(actual, change.extension)) {
                    result.defaultExtensions << change.extension;
                    continue;
                }
                result.pendingDefaultExtensions << change.extension;
                // Do not leave an ineffective takeover behind or report it as
                // success. Restore only a mapping that still belongs to us.
                const QString key = classes + '.' + change.extension + "/.";
                if (registry->value(key).toString() == "8bitPad." + change.extension) {
                    if (change.present) registry->setValue(key, change.previous);
                    else registry->remove(key);
                    restored = true;
                }
                if (change.backupCreated) registry->remove(backups + change.extension);
            }
            if (restored) {
                syncRegistry(registry, result.errors);
                if (services.associationsChanged) services.associationsChanged();
            }
        }
        return result;
    }

    if (!QDir::isAbsolutePath(services.dataHome) || !QDir::isAbsolutePath(services.configHome)) {
        result.errors << QObject::tr("The user's XDG data/configuration directories are unavailable."); return result;
    }
    QStringList mimes;
    for (const QString &extension : combined) {
        const QString mime = mimeTypeForExtension(extension);
        if (mime.isEmpty() || mime == "inode/directory") continue;
        mimes << mime;
    }
    mimes = unique(mimes);
    if (!writeAtomic(QDir(applicationsPath(services)).filePath(desktopId), desktopEntry(services.executable, mimes).toUtf8(), result.errors)) return result;
    {
        // A declared desktop MIME type alone does not associate a new suffix
        // (jsonc/jsx/tsx, for example) with that type. Publish the explicitly
        // selected text/code globs too. Existing recognized text types retain
        // their system MIME; ambiguous .ts/.tsx intentionally use code types.
        QString xml = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<!-- Generated by 8bitPad -->\n<mime-info xmlns=\"http://www.freedesktop.org/standards/shared-mime-info\">\n";
        for (const QString &extension : combined) {
            const QString mime = mimeTypeForExtension(extension);
            const bool custom = mime.startsWith("application/x-8bitpad-");
            xml += QString(" <mime-type type=\"%1\">%2<glob pattern=\"*.%3\" weight=\"60\"/></mime-type>\n")
                       .arg(mime, custom ? QString("<comment>8bitPad text document</comment><sub-class-of type=\"text/plain\"/>") : QString(), extension);
        }
        xml += "</mime-info>\n";
        if (!writeAtomic(QDir(services.dataHome).filePath("mime/packages/8bitpad-types.xml"), xml.toUtf8(), result.errors)) return result;
        if (!runTool(services, "update-mime-database", {QDir(services.dataHome).filePath("mime")}, result.errors)) return result;
    }
    if (!runTool(services, "update-desktop-database", {applicationsPath(services)}, result.errors)) return result;
    const QJsonObject manifest{{"owner", "8bitPad"}, {"extensions", QJsonArray::fromStringList(combined)}};
    if (!writeAtomic(manifestPath(services), QJsonDocument(manifest).toJson(), result.errors)) return result;
    result.registeredExtensions = parsed.extensions;
    if (requestDefaults) {
        QStringList requestedMimes;
        for (const QString &extension : parsed.extensions) requestedMimes << mimeTypeForExtension(extension);
        requestedMimes = unique(requestedMimes);
        QElapsedTimer budget; budget.start();
        for (const QString &mime : requestedMimes) {
            if (mime.isEmpty() || mime == "inode/directory") continue;
            if (budget.elapsed() > 30000) { result.errors << QObject::tr("Default-app changes reached the time limit; remaining types were not changed."); break; }
            if (!runTool(services, "xdg-mime", {"default", desktopId, mime}, result.errors)) continue;
            QString actual;
            if (!runTool(services, "xdg-mime", {"query", "default", mime}, result.errors, &actual)) continue;
            if (actual == desktopId) result.defaultMimeTypes << mime;
            else result.errors << QObject::tr("%1 was requested, but the desktop still reports %2 as its default.").arg(mime, actual.isEmpty() ? QObject::tr("no application") : actual);
        }
    }
    return result;
}

bool Backend::contextMenuInstalled() const
{
    if (services.platform == Platform::Windows)
        return services.registry && !services.registry->value(classes + "*/shell/8bitPad/command/.").toString().isEmpty();
    return QFileInfo::exists(QDir(applicationsPath(services)).filePath(folderDesktopId));
}

BatchResult Backend::setContextMenuEnabled(bool enabled)
{
    BatchResult result;
    if (services.platform == Platform::Windows) {
        if (!services.registry) { result.errors << QObject::tr("The user registration store is unavailable."); return result; }
        for (const QString &key : {QString("*/shell/8bitPad"), QString("Directory/shell/8bitPad"), QString("Directory/Background/shell/8bitPad")}) {
            if (!enabled) { services.registry->remove(classes + key); continue; }
            const bool files = key.startsWith('*');
            services.registry->setValue(classes + key + "/.", files ? "Open with 8bitCode" : "Open folder in 8bitCode");
            services.registry->setValue(classes + key + "/Icon", '"' + QDir::toNativeSeparators(services.executable) + "\",0");
            services.registry->setValue(classes + key + "/command/.", windowsOpenCommand(services.executable, key.contains("Background") ? "%V" : "%1"));
        }
        if (syncRegistry(services.registry, result.errors) && services.associationsChanged) services.associationsChanged();
    } else {
        const QString path = QDir(applicationsPath(services)).filePath(folderDesktopId);
        if (enabled) {
            if (!writeAtomic(path, desktopEntry(services.executable, {"inode/directory"}, true).toUtf8(), result.errors)) return result;
        } else removeFile(path, result.errors);
        runTool(services, "update-desktop-database", {applicationsPath(services)}, result.errors);
    }
    return result;
}

BatchResult Backend::refreshOwnedIcons()
{
    BatchResult result;
    if (services.platform != Platform::Windows) return result;
    auto *registry = services.registry;
    if (!registry || !validExecutable(services.executable)) {
        result.errors << QObject::tr("The application icon registration could not be refreshed.");
        return result;
    }

    // A portable or isolated copy must never redirect another copy's entries.
    // Only the exact, existing open command establishes ownership here.
    const QString command = QDir::fromNativeSeparators(windowsOpenCommand(services.executable, "%1"));
    const auto owned = [&](const QString &handler) {
        return QDir::fromNativeSeparators(registry->value(classes + handler + "/shell/open/command/.")
            .toString().trimmed()).compare(command, Qt::CaseInsensitive) == 0;
    };
    bool changed = false;
    bool hasOwnedHandler = false;
    const auto update = [&](const QString &key, const QString &value) {
        if (registry->value(key).toString() != value) {
            registry->setValue(key, value);
            changed = true;
        }
    };

    QStringList extensions = registeredExtensions();
    for (const auto &group : catalog()) extensions += group.extensions;
    for (const QString &extension : unique(extensions)) {
        const QString handler = "8bitPad." + extension;
        if (!owned(handler)) continue;
        hasOwnedHandler = true;
        update(classes + handler + "/DefaultIcon/.",
            iconReference(services.executable, FileTypeIcons::forExtension(extension).resourceId));
        result.registeredExtensions += extension;
    }
    // Windows may have created only the generic Open With application entry,
    // without our extension manifest. Give that route a real document icon too.
    if (owned("Applications/8bitPad.exe")) {
        hasOwnedHandler = true;
        const QString existing = registry->value(applicationIconHandlerKey).toString();
        if (existing.isEmpty() || existing.compare(iconHandlerClass, Qt::CaseInsensitive) == 0) {
            const QString handler = nativeIconHandler(services);
            if (!handler.isEmpty()) {
                update(iconHandlerComKey + "/.", "8bitCode file icons");
                update(iconHandlerComKey + "/InprocServer32/.", handler);
                update(iconHandlerComKey + "/InprocServer32/ThreadingModel", "Apartment");
                update(applicationIconHandlerKey, iconHandlerClass);
                update(classes + "Applications/8bitPad.exe/DefaultIcon/.", "%1");
            } else {
                update(classes + "Applications/8bitPad.exe/DefaultIcon/.",
                    iconReference(services.executable, FileTypeIcons::forExtension({}).resourceId));
                if (!existing.isEmpty()) {
                    registry->remove(classes + "Applications/8bitPad.exe/shellex/IconHandler");
                    changed = true;
                }
            }
        }
        if (registry->value(registeredAppKey).toString() == capabilitiesValue)
            update(capabilities + "ApplicationIcon", iconReference(services.executable, FileTypeIcons::ApplicationResourceId));
    }
    if (hasOwnedHandler) {
        // Resource IDs stay stable when artwork changes; invalidate Explorer's
        // cache once for each asset revision, not on every application launch.
        update(iconRevisionKey, QDir::fromNativeSeparators(services.executable) + '\n' + FileTypeIcons::revision());
    }
    if (changed && syncRegistry(registry, result.errors) && services.associationsChanged)
        services.associationsChanged();
    return result;
}

BatchResult Backend::removeOwnedRegistration()
{
    BatchResult result;
    const QStringList registered = registeredExtensions();
    if (services.platform == Platform::Windows) {
        if (!services.registry) { result.errors << QObject::tr("The user registration store is unavailable."); return result; }
        if (QDir::fromNativeSeparators(services.registry->value(iconHandlerComKey + "/InprocServer32/.").toString())
            .compare(expectedIconHandler(services), Qt::CaseInsensitive) == 0)
            services.registry->remove(iconHandlerComKey);
        QStringList legacy = registered;
        for (const auto &group : catalog()) legacy << group.extensions;
        for (const QString &extension : unique(legacy)) {
            const QString progId = "8bitPad." + extension;
            services.registry->remove(classes + progId);
            services.registry->remove(classes + '.' + extension + "/OpenWithProgids/" + progId);
            // Restore a previous per-user mapping only while ours still owns it.
            // A later choice by the user or another editor always takes precedence.
            const QString key = classes + '.' + extension + "/.";
            const QString backup = "Software/8bitPad/ShellIntegration/PreviousDefaults/" + extension + '/';
            if (services.registry->value(key).toString().compare(progId, Qt::CaseInsensitive) == 0) {
                if (services.registry->value(backup + "Present").toBool())
                    services.registry->setValue(key, services.registry->value(backup + "Value"));
                else services.registry->remove(key);
            }
        }
        services.registry->remove(classes + "Applications/8bitPad.exe");
        if (services.registry->value(registeredAppKey).toString() == capabilitiesValue) services.registry->remove(registeredAppKey);
        services.registry->remove("Software/8bitPad/Capabilities");
        services.registry->remove("Software/8bitPad/ShellIntegration");
        for (const QString &key : {QString("*/shell/8bitPad"), QString("Directory/shell/8bitPad"), QString("Directory/Background/shell/8bitPad")})
            services.registry->remove(classes + key);
        if (syncRegistry(services.registry, result.errors) && services.associationsChanged) services.associationsChanged();
    } else {
        removeFile(QDir(applicationsPath(services)).filePath(desktopId), result.errors);
        removeFile(QDir(applicationsPath(services)).filePath(folderDesktopId), result.errors);
        const QDir packages(QDir(services.dataHome).filePath("mime/packages"));
        for (const QFileInfo &entry : packages.entryInfoList({"8bitpad-*.xml"}, QDir::Files)) {
            const QString ext = entry.baseName().mid(QString("8bitpad-").size());
            if (ext == "types" || (parseExtensions(ext).valid() && parseExtensions(ext).extensions.size() == 1)) removeFile(entry.absoluteFilePath(), result.errors);
        }
        QStringList lists = {QDir(services.configHome).filePath("mimeapps.list"), QDir(applicationsPath(services)).filePath("mimeapps.list")};
        for (const QString &desktop : qEnvironmentVariable("XDG_CURRENT_DESKTOP").toLower().split(':', Qt::SkipEmptyParts)) {
            if (!QRegularExpression("^[a-z0-9_-]+$").match(desktop).hasMatch()) continue;
            lists << QDir(services.configHome).filePath(desktop + "-mimeapps.list") << QDir(applicationsPath(services)).filePath(desktop + "-mimeapps.list");
        }
        for (const QString &path : unique(lists)) removeDesktopReferences(path, {desktopId, folderDesktopId}, result.errors);
        removeFile(manifestPath(services), result.errors);
        runTool(services, "update-desktop-database", {applicationsPath(services)}, result.errors);
        if (packages.exists()) runTool(services, "update-mime-database", {QDir(services.dataHome).filePath("mime")}, result.errors);
    }
    return result;
}

QStringList registeredExtensions() { auto registry = systemRegistry(); return Backend(systemServices(registry.get())).registeredExtensions(); }
BatchResult apply(const QStringList &extensions, bool defaults) { auto registry = systemRegistry(); return Backend(systemServices(registry.get())).apply(extensions, defaults); }
BatchResult refreshOwnedIcons() { auto registry = systemRegistry(); return Backend(systemServices(registry.get())).refreshOwnedIcons(); }
BatchResult removeOwnedRegistration() { auto registry = systemRegistry(); return Backend(systemServices(registry.get())).removeOwnedRegistration(); }
bool contextMenuInstalled() { auto registry = systemRegistry(); return Backend(systemServices(registry.get())).contextMenuInstalled(); }
BatchResult setContextMenuEnabled(bool enabled) { auto registry = systemRegistry(); return Backend(systemServices(registry.get())).setContextMenuEnabled(enabled); }
}
