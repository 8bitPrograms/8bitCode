/*
 * This file is part of Notepad Next.
 * Copyright 2019 Justin Dailey
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Notepad Next is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Notepad Next.  If not, see <https://www.gnu.org/licenses/>.
 */


#include "MainWindow.h"
#include "NotepadNextApplication.h"
#include "DevOpsProfiles.h"
#include "MarkerAppDecorator.h"
#include "RecentFilesListManager.h"
#include "EditorManager.h"
#include "LuaExtension.h"
#include "DebugManager.h"
#include "SessionManager.h"
#include "TranslationManager.h"
#include "ApplicationSettings.h"
#include "LegacyProfileImport.h"
#include "ThemeManager.h"
#include "AppSmokeTest.h"
#include "FileAssociations.h"
#include <QStatusBar>

#include "LuaState.h"
#include "lua.hpp"
#include "EditorConfigAppDecorator.h"

#include "ILexer.h"
#include "Lexilla.h"

#include <QCommandLineParser>

#include <QDirIterator>

#ifdef Q_OS_WIN
#include <Windows.h>
#endif

const SingleApplication::Options opts = SingleApplication::User | SingleApplication::ExcludeAppPath | SingleApplication::ExcludeAppVersion;

void parseCommandLine(QCommandLineParser &parser, const QStringList &args)
{
    parser.setApplicationDescription("8bitCode - native text editor for Windows and Linux");
    parser.addHelpOption();
    parser.addVersionOption();

    parser.addPositionalArgument("files", "Files to open.");

    parser.addOptions({
        {"translation", "Overrides the system default translation.", "translation"},
        {"reset-settings", "Resets all application settings."},
        {"n", "Places the cursor on the line number for the first file specified", "line number"},
        {"workspace", "Opens the specified folder as a workspace", "workspace path"}
        ,{"profile", "Use an isolated settings and recovery directory", "directory"}
        ,{"smoke-test", "Run the application integration checks and save screenshots", "output directory"}
    });

    parser.process(args);
}

static QString toLocalFileName(const QString file)
{
    QUrl fileUrl(file);
    return fileUrl.isValid() && fileUrl.isLocalFile() ? fileUrl.toLocalFile() : file;
}

NotepadNextApplication::NotepadNextApplication(int &argc, char **argv)
    : SingleApplication(argc, argv, true, opts, 1000, qEnvironmentVariable("BIT8PAD_PROFILE"))
{
#ifdef Q_OS_WIN
    // Create a system-wide mutex so the installer can detect if it is running
    CreateMutex(NULL, false, L"BitPadMutex");
#endif
    parseCommandLine(parser, arguments());

    DebugManager::manageDebugOutput();
    DebugManager::pauseDebugOutput();
}

bool NotepadNextApplication::init()
{
    qInfo(Q_FUNC_INFO);

#ifndef Q_OS_MACOS
    setWindowIcon(QIcon(QStringLiteral(":/icons/8bitcode.svg")));
#endif

    // --profile, BIT8PAD_PROFILE and portable copies keep their folder; nothing to import there.
    if (qEnvironmentVariableIsEmpty("BIT8PAD_PROFILE")) {
        const QString profile = ApplicationSettings::profileDirectory();
        const auto imported = LegacyProfileImport::run(LegacyProfileImport::legacyDirectoryFor(profile), profile);
        if (imported.ran)
            qInfo("8bitPad profile import: %d copied, %d already present", imported.copied, int(imported.skipped.size()));
        for (const QString &name : imported.skipped) qInfo("8bitPad profile import kept existing %s", qUtf8Printable(name));
        for (const QString &error : imported.errors) qWarning("8bitPad profile import will retry: %s", qUtf8Printable(error));
    }
    settings = new ApplicationSettings(this);
    if (parser.isSet("smoke-test")) settings->setRestorePreviousSession(false);

    if (parser.isSet("reset-settings")) {
        QFileInfo original(settings->fileName());
        const QString backup = original.canonicalFilePath() + QStringLiteral(".backup");

        qInfo("Resetting application settings");
        qInfo("Backuping up %s to %s", qUtf8Printable(settings->fileName()), qUtf8Printable(backup));

        QFile::remove(backup);
        QFile::rename(settings->fileName(), backup);

        settings->clear();
    }

    // Translation files are stored as a qresource
    translationManager = new TranslationManager(this, QStringLiteral(":/i18n/"));

    // The command line overrides the settings
    if (!parser.value("translation").isEmpty()) {
        translationManager->loadTranslation(parser.value("translation"));
    }
    else if (!settings->translation().isEmpty()){
        translationManager->loadTranslation(settings->translation());
    }
    else {
        translationManager->loadSystemDefaultTranslation();
    }

    // This connection isn't needed since the application can not appropriately retranslate the UI at runtime
    //connect(settings, &ApplicationSettings::translationChanged, translationManager, &TranslationManager::loadTranslationByName);

    luaState = new LuaState();

    recentFilesListManager = new RecentFilesListManager(this);
    editorManager = new EditorManager(settings, this);
    sessionManager = new SessionManager(this);

    connect(editorManager, &EditorManager::editorCreated, recentFilesListManager, [this](ScintillaNext *editor) {
        if (editor->isFile()) {
            recentFilesListManager->removeFile(editor->getFilePath());
        }
    });

    connect(editorManager, &EditorManager::editorClosed, recentFilesListManager, [this](ScintillaNext *editor) {
        if (editor->isFile()) {
            recentFilesListManager->addFile(editor->getFilePath());
        }
    });

    loadSettings();

    connect(this, &NotepadNextApplication::aboutToQuit, this, &NotepadNextApplication::saveSettings);

    EditorConfigAppDecorator *ecad = new EditorConfigAppDecorator(this);
    ecad->setEnabled(true);
    MarkerAppDecorator *mad = new MarkerAppDecorator(this);
    mad->setEnabled(true);

    luaState->executeFile(":/scripts/init.lua");
    LuaExtension::Instance().Initialise(luaState->L, Q_NULLPTR);

    themeManager = new ThemeManager(settings, luaState, editorManager, this);

    createNewWindow();
    connect(editorManager, &EditorManager::editorCreated, window, &MainWindow::addEditor);

    // If the application is activated (e.g. user switching to another program and them back) the focus
    // needs to be reset on whatever object previously had focus (e.g. the find dialog)
    connect(this, &NotepadNextApplication::focusChanged, this, [&](QWidget *old, QWidget *now) {
        Q_UNUSED(old);
        if (now) {
            currentlyFocusedWidget = now;
        }
    });

    connect(this, &SingleApplication::instanceStarted, window, &MainWindow::bringWindowToForeground);
    connect(this, &SingleApplication::receivedMessage, this, &NotepadNextApplication::receiveInfoFromSecondaryInstance, Qt::QueuedConnection);

    connect(this, &NotepadNextApplication::applicationStateChanged, this, [&](Qt::ApplicationState state) {
        if (state == Qt::ApplicationActive) {

            // Make sure it is active...
            // The application can be active without the main window being show e.g. if there is a
            // message box that pops up before the main window
            if (window->isActiveWindow()) {
                window->focusIn();
            }

            if (!currentlyFocusedWidget.isNull()) {
                currentlyFocusedWidget->activateWindow();
            }
        }
    });

    if (settings->restorePreviousSession()) {
        qInfo("Restoring previous session");

        sessionManager->loadSession(window);
    }

    openFiles(parser.positionalArguments());

    if (parser.isSet("n") && parser.positionalArguments().length() > 0) {
        QString firstFile = parser.positionalArguments()[0];
        ScintillaNext *editor = editorManager->getEditorByFilePath(toLocalFileName(firstFile));

        if (editor) {
            int n = parser.value("n").toInt();
            editor->gotoLine(n - 1);
        }
    }

    // If the window does not have any editors (meaning the no files were
    // specified on the command line) then create a new empty file
    if (window->editorCount() == 0) {
        window->newFile();
    }

    // Everything should be ready at this point

    window->restoreWindowState();

    // Check this after restoring the state, as the state contains the previous visibility state of the FAW dock
    if (parser.isSet("workspace")) {
        const QString dir = parser.value("workspace");
        qInfo("Opening folder as workspace: %s", qUtf8Printable(dir));
        window->setFolderAsWorkspacePath(dir);
    }

    window->show();

#ifdef Q_OS_WIN
    // Normal desktop starts repair our existing icon records after an update.
    // Profile-based/offscreen verification must never write the real registry.
    if (!parser.isSet("smoke-test") && !parser.isSet("profile") &&
        qEnvironmentVariableIsEmpty("BIT8PAD_PROFILE") && platformName() == "windows") {
        QTimer::singleShot(0, this, [] {
            const auto result = FileAssociations::refreshOwnedIcons();
            if (!result.ok()) qWarning().noquote() << result.errors.join('\n');
        });
    }
#endif

    if (parser.isSet("smoke-test")) {
        const QString output = parser.value("smoke-test");
        QTimer::singleShot(200, this, [this, output] { runAppSmokeTest(this, window, output); });
    }

    DebugManager::resumeDebugOutput();

    return true;
}

SessionManager *NotepadNextApplication::getSessionManager() const
{
    SessionManager::SessionFileTypes fileTypes;

    if (settings->restorePreviousSession()) {
        fileTypes |= SessionManager::SavedFile;
    }

    if (settings->restoreUnsavedFiles()) {
        fileTypes |= SessionManager::UnsavedFile;
    }

    if (settings->restoreTempFiles()) {
        fileTypes |= SessionManager::TempFile;
    }

    // Update the file types supported in case something has changed in the settings
    sessionManager->setSessionFileTypes(fileTypes);

    return sessionManager;
}

QString NotepadNextApplication::getFileDialogFilter() const
{
    return getLuaState()->executeAndReturn<QString>("return DialogFilters()");
}

QString NotepadNextApplication::getFileDialogFilterForLanguage(const QString &language) const
{
    getLuaState()->setVariable("langForFilter", language);
    return getLuaState()->executeAndReturn<QString>("return FilterForLanguage(langForFilter)");
}

QStringList NotepadNextApplication::getLanguages() const
{
    return getLuaState()->executeAndReturn<QStringList>(
                R"(
                local names = {}
                for k in pairs(languages) do table.insert(names, k) end
                table.sort(names, function (a, b) return string.lower(a) < string.lower(b) end)
                return names
                )");
}

void NotepadNextApplication::setEditorLanguage(ScintillaNext *editor, const QString &languageName) const
{
    LuaExtension::Instance().setEditor(editor);

    getLuaState()->setVariable("languageName", languageName);
    const QString lexer = getLuaState()->executeAndReturn<QString>("return languages[languageName].lexer");

    editor->languageName = languageName;
    editor->languageSingleLineComment = getLuaState()->executeAndReturn<QString>("return languages[languageName].singleLineComment or \"\"").toUtf8();

    auto lexerInstance = CreateLexer(lexer.toLatin1().constData());
    editor->setILexer((sptr_t) lexerInstance);
    editor->clearDocumentStyle(); // Remove all previous style information, setting the lexer does not guarantee styling information is cleared

    // Not ideal this has to be manually emitted but it works since setILexer() is not widely used
    emit editor->lexerChanged();

    // Dynamic properties can be used to skip part of the default initialization. The value in the
    // property doesn't currently matter, but may be used at a later point.
    bool skipTabs = editor->QObject::property("nn_skip_usetabs").isValid();
    bool skipTabWidth = editor->QObject::property("nn_skip_tabwidth").isValid();

    getLuaState()->setVariable("skip_tabs", skipTabs);
    getLuaState()->setVariable("skip_tabwidth", skipTabWidth);

    getLuaState()->execute("SetLanguage(languageName)");
}

QStringList NotepadNextApplication::getLanguageKeywords(const QString &languageName) const
{
    getLuaState()->setVariable("languageName", languageName);
    return getLuaState()->executeAndReturn<QStringList>("return GetLanguageKeywords(languageName)");
}

QString NotepadNextApplication::detectLanguage(ScintillaNext *editor) const
{
    qInfo(Q_FUNC_INFO);

    QString language_name = QStringLiteral("Text");

    if (editor->isFile()) {
        const auto profile = DevOpsProfiles::detect(editor->getFileInfo().absoluteFilePath(),
            QString::fromUtf8(editor->get_text_range(0, qMin<sptr_t>(editor->length(), 64 * 1024))));
        if (profile != DevOpsProfiles::Kind::None)
            return DevOpsProfiles::languageName(profile);
        language_name = detectLanguageFromExtension(editor->getFileInfo().suffix().toLower());
    }

    if (language_name == QStringLiteral("Text")) {
        language_name = detectLanguageFromContents(editor);
    }

    return language_name;
}

QString NotepadNextApplication::detectLanguageFromExtension(const QString &extension) const
{
    qInfo(Q_FUNC_INFO);

    getLuaState()->setVariable("ext", extension);

    return getLuaState()->executeAndReturn<QString>(R"(
    for name, L in pairs(languages) do
        if L.extensions then
            for _, v in ipairs(L.extensions) do
                if v == ext then
                    return name
                end
            end
        end
    end
    return "Text"
    )");
}

QString NotepadNextApplication::detectLanguageFromContents(ScintillaNext *editor) const
{
    qInfo(Q_FUNC_INFO);

    LuaExtension::Instance().setEditor(editor);

    return getLuaState()->executeAndReturn<QString>(R"(
    -- Grab a small chunk
    if editor.Length > 0 then
        editor:SetTargetRange(0, math.min(64, editor.Length))
        return DetectLanguageFromContents(editor.TargetText)
    end

    return "Text"
    )");
}

bool NotepadNextApplication::sendInfoToPrimaryInstance()
{
    qInfo(Q_FUNC_INFO);

    QStringList argsToSend{applicationFilePath()};
    for (const QString &file : parser.positionalArguments())
        argsToSend << QFileInfo(toLocalFileName(file)).absoluteFilePath();
    if (parser.isSet("workspace"))
        argsToSend << "--workspace" << QFileInfo(toLocalFileName(parser.value("workspace"))).absoluteFilePath();
    if (parser.isSet("n")) argsToSend << "-n" << parser.value("n");
    QByteArray buffer;
    QDataStream stream(&buffer, QIODevice::WriteOnly);
    stream << argsToSend;
    const bool success = sendMessage(buffer, 5000);
    if (!success) {
        qWarning("sendMessage() unsuccessful");
    }
    return success;
}

void NotepadNextApplication::receiveInfoFromSecondaryInstance(quint32 instanceId, QByteArray message)
{
    qInfo(Q_FUNC_INFO);

    Q_UNUSED(instanceId)

    QDataStream stream(&message, QIODevice::ReadOnly);
    QStringList args;
    stream >> args;
    if (stream.status() != QDataStream::Ok || args.isEmpty() || args.size() > 512) return;

    QCommandLineParser parser;
    // Configure without processing untrusted options through process(), which
    // would terminate the running editor on a malformed local request.
    parseCommandLine(parser, {applicationFilePath()});
    if (!parser.parse(args)) { qWarning() << "Invalid file-open command:" << parser.errorText(); return; }

    openFiles(parser.positionalArguments());
    if (parser.isSet("workspace"))
        window->setFolderAsWorkspacePath(parser.value("workspace"));
    window->bringWindowToForeground();
}

bool NotepadNextApplication::isRunningAsAdmin() const
{
    static bool initialized = false;
    static bool isAdmin = false;

    if (!initialized) {
        initialized = true;

#ifdef Q_OS_WIN
        BOOL isMember;
        SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
        PSID administratorsGroupSid = NULL;

        // Create a SID for the Administrators group
        if (AllocateAndInitializeSid(&ntAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &administratorsGroupSid)) {
            if (CheckTokenMembership(NULL, administratorsGroupSid, &isMember)) {
                isAdmin = isMember;
            }
            FreeSid(administratorsGroupSid);
        }
#endif
    }

    return isAdmin;
}

bool NotepadNextApplication::event(QEvent *event)
{
    // Handle the QFileOpenEvent to open files on MacOS X.
    if (event->type() == QEvent::FileOpen) {
        QFileOpenEvent *fileOpenEvent = static_cast<QFileOpenEvent*>(event);

        qInfo("QFileOpenEvent %s", qUtf8Printable(fileOpenEvent->file()));

        openFiles(QStringList() << fileOpenEvent->file());

        return true;
    }

    return SingleApplication::event(event);
}

void NotepadNextApplication::openFiles(const QStringList &files)
{
    qInfo(Q_FUNC_INFO);

    for (const QString &file : files) {
        window->openFile(toLocalFileName(file));
    }
}

void NotepadNextApplication::loadSettings()
{
    recentFilesListManager->setFileList(getSettings()->value("App/RecentFilesList").toStringList());
}

void NotepadNextApplication::saveSettings()
{
    getSettings()->setValue("App/RecentFilesList", recentFilesListManager->fileList());
}

void NotepadNextApplication::saveSession()
{
    // Iterate all the opened editors and add them to the recent file list
    for (const auto &editor : window->editors()) {
        if (editor->isFile()) {
            recentFilesListManager->addFile(editor->getFilePath());
        }
    }
    saveSettings();

    if (!getSessionManager()->saveSession(window))
        window->statusBar()->showMessage(tr("Recovery save failed. Save your documents to disk."));
}

MainWindow *NotepadNextApplication::createNewWindow()
{
    Q_ASSERT(window == Q_NULLPTR);

    window = new MainWindow(this);

    // Keep Lua's editor reference up to date
    connect(window, &MainWindow::editorActivated, this, [](ScintillaNext *editor) {
        LuaExtension::Instance().setEditor(editor);
    });

    // TODO: this shouldn't be dependent on a MainWindow closing, but this works for now
    // since the assumption is MainWindow::aboutToClose() infers the application is shutting
    // down but the editors are still active.
    // MainWindow commits recovery before allowing close and reports failures.
    connect(window, &MainWindow::aboutToClose, this, &NotepadNextApplication::saveSettings);

    // Timer to autosave the session
    connect(&autoSaveTimer, &QTimer::timeout, this, &NotepadNextApplication::saveSession);
    autoSaveTimer.start(60 * 1000);

    return window;
}

QStringList NotepadNextApplication::debugInfo() const
{
    QStringList info;

    info.append(QStringLiteral("%1 v%2 %3").arg(applicationDisplayName(), applicationVersion(), APP_DISTRIBUTION));
    info.append(QStringLiteral("Build Date/Time: %1 %2").arg(__DATE__, __TIME__));
    info.append(QStringLiteral("Qt: %1").arg(qVersion()));
    info.append(QStringLiteral("OS: %1").arg(QSysInfo::prettyProductName()));
    info.append(QStringLiteral("Locale: %1").arg(QLocale::system().name()));
    info.append(QStringLiteral("CPU: %1").arg(QSysInfo::currentCpuArchitecture()));
    info.append(QStringLiteral("File Path: %1").arg(applicationFilePath()));
    info.append(QStringLiteral("Arguments: %1").arg(arguments().join(' ')));
    info.append(QStringLiteral("Config File: %1").arg(ApplicationSettings().fileName()));

    return info;
}
