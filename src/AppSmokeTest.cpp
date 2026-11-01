// SPDX-License-Identifier: GPL-3.0-or-later
#include "AppSmokeTest.h"
#include "NotepadNextApplication.h"
#include "MainWindow.h"
#include "ThemeManager.h"
#include "VimMode.h"
#include "SessionManager.h"
#include "DebugManager.h"
#include <QAction>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QIcon>
#include <QKeyEvent>
#include <QElapsedTimer>
#include <QSharedPointer>
#include <QDockWidget>
#include <QTreeWidget>
#include <QEventLoop>
#include <QTimer>
#include <memory>
#ifdef BIT8PAD_TESTING
#include "../tests/ThemeTest.h"
#include "../tests/RecoveryTest.h"
#include "../tests/WorkspaceUiTest.h"
#endif

void runAppSmokeTest(NotepadNextApplication *app, MainWindow *window, const QString &outputDirectory)
{
    QDir().mkpath(outputDirectory);
    static QSharedPointer<QFile> log;
    log = QSharedPointer<QFile>::create(QDir(outputDirectory).filePath("smoke-runtime.log"));
    if (log->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        DebugManager::addMessageHandler([](const QString &line) {
            log->write(line.toUtf8() + '\n');
            log->flush();
        });
    }
    QJsonArray checks;
    bool success = true;
    auto check = [&](QString name, bool passed) {
        checks.append(QJsonObject{{"name", name}, {"passed", passed}});
        success &= passed;
    };
    auto key = [](ScintillaNext *editor, int code, QString text = {}, Qt::KeyboardModifiers modifiers = {}) {
        QKeyEvent event(QEvent::KeyPress, code, modifiers, text);
        QCoreApplication::sendEvent(editor, &event);
        QCoreApplication::processEvents();
    };
    auto bytes = [](ScintillaNext *editor) {
        return QByteArray(reinterpret_cast<const char *>(editor->characterPointer()), editor->length());
    };
    check("window-visible", window->isVisible());
    check("editor-created", window->currentEditor() != nullptr);
#ifdef BIT8PAD_TESTING
    for (const auto &entry : runThemeChecks(app, window)) {
        checks.append(entry);
        success &= entry.toObject().value("passed").toBool();
    }
#endif
    ScintillaNext *editor = window->currentEditor();
    if (!editor) { app->exit(1); return; }
    check("theme-catalog", ThemeManager::catalog().size() >= 23);
    for (const auto &theme : ThemeManager::catalog())
        check("theme-" + theme.id, app->getThemeManager()->setTheme(theme.id));
    app->getThemeManager()->setTheme("ayu-mirage");
    const QByteArray sample = QString::fromUtf8(
        "// 8bitPad — Windows & Linux\n"
        "// Türkçe: ıİ ğĞ şŞ öÖ üÜ çÇ 🌍\n\n"
        "#include <iostream>\n#include <string>\n\n"
        "int main() {\n"
        "    const std::string editor = \"8bitPad\";\n"
        "    std::cout << \"Native. Fast. Yours.\" << std::endl;\n"
        "    // Ctrl+Alt+V: Vim mode | Ctrl+Shift+P: commands\n"
        "    return 0;\n}\n").toUtf8();
    editor->setText(sample.constData());
    app->setEditorLanguage(editor, "C++");
    QCoreApplication::processEvents();
    check("syntax-language", editor->languageName == "C++");
    auto *vimAction = window->findChild<QAction *>("actionVimMode");
    check("vim-action", vimAction != nullptr);
    if (vimAction) vimAction->setChecked(true);
    auto *vim = editor->findChild<VimMode *>();
    check("vim-attached", vim && vim->isEnabled());
    editor->setText("alpha beta\ngamma\n");
    editor->gotoPos(0);
    editor->emptyUndoBuffer();
    key(editor, Qt::Key_Escape);
    key(editor, Qt::Key_D, "d");
    key(editor, Qt::Key_W, "w");
    check("vim-delete-word", bytes(editor) == "beta\ngamma\n");
    key(editor, Qt::Key_U, "u");
    check("vim-undo", bytes(editor) == "alpha beta\ngamma\n");
    editor->gotoPos(0);
    key(editor, Qt::Key_I, "i");
    check("vim-insert-mode", vim && vim->modeName() == "INSERT");
    key(editor, Qt::Key_X, "X", Qt::ShiftModifier);
    key(editor, Qt::Key_Escape);
    check("vim-insert", bytes(editor).startsWith("Xalpha"));
    checks.append(QJsonObject{{"name", "vim-insert-content"}, {"passed", bytes(editor).startsWith("Xalpha")},
                              {"detail", QString::fromUtf8(bytes(editor))}});
    if (vimAction) vimAction->setChecked(false);
    editor->setText(sample.constData());
    editor->gotoPos(0);
    const QString savedPath = QDir(outputDirectory).filePath("smoke-unicode.cpp");
    check("atomic-save", window->saveFileAs(editor, savedPath));
    QFile saved(savedPath);
    check("saved-bytes-exact", saved.open(QIODevice::ReadOnly) && saved.readAll() == sample);
    saved.close();

    const QString benchmarkPath = QDir(outputDirectory).filePath("benchmark-24mib.txt");
    QByteArray fixture;
    fixture.reserve(24 * 1024 * 1024);
    const QByteArray fixtureLine = QByteArray("// 8bitPad large text ") + QByteArray(105, 'x') + '\n';
    while (fixture.size() < 24 * 1024 * 1024) fixture.append(fixtureLine);
    QFile benchmarkFile(benchmarkPath);
    const bool fixtureSaved = benchmarkFile.open(QIODevice::WriteOnly) && benchmarkFile.write(fixture) == fixture.size();
    benchmarkFile.close();
    QElapsedTimer openTimer;
    openTimer.start();
    std::unique_ptr<ScintillaNext> loaded(fixtureSaved ? ScintillaNext::fromFile(benchmarkPath) : nullptr);
    const double openMilliseconds = openTimer.nsecsElapsed() / 1000000.0;
    const bool opened = loaded && loaded->length() == fixture.size() && loaded->isSavedToDisk();
    check("performance-24mib-disk-open", opened);
    checks.append(QJsonObject{{"name", "performance-24mib-disk-open-timing"}, {"passed", opened},
                              {"bytes", fixture.size()}, {"elapsedMilliseconds", openMilliseconds}});
    loaded.reset();
    QFile::remove(benchmarkPath);

    auto *functions = window->findChild<QDockWidget *>("BitPadFunctionList");
    auto *map = window->findChild<QDockWidget *>("BitPadDocumentMap");
    check("navigation-panels", functions && map);
    if (functions && map) {
        functions->show();
        map->show();
        auto *tree = functions->findChild<QTreeWidget *>();
        QElapsedTimer timeout;
        timeout.start();
        while (tree && tree->topLevelItemCount() == 0 && timeout.elapsed() < 3000) {
            QEventLoop wait;
            QTimer::singleShot(50, &wait, &QEventLoop::quit);
            wait.exec();
        }
        check("function-list-main", tree && tree->topLevelItemCount() == 1 && tree->topLevelItem(0)->text(0).contains("main"));
        check("document-map-visible", map->findChild<QWidget *>("DocumentDensityMap")->isVisible());
    }

    window->resize(1280, 800);
    QCoreApplication::processEvents();
    check("application-icon", QIcon(":/icons/8bitcode.svg").pixmap(256, 256).save(QDir(outputDirectory).filePath("8bitcode-icon.png")));
    check("dark-screenshot", window->grab().save(QDir(outputDirectory).filePath("8bitcode-ayu.png")));
    app->getThemeManager()->setTheme("github-light");
    QCoreApplication::processEvents();
    check("light-screenshot", window->grab().save(QDir(outputDirectory).filePath("8bitcode-light.png")));
    app->getThemeManager()->setTheme("ayu-mirage");
#ifdef BIT8PAD_TESTING
    for (const auto &entry : runWorkspaceUiChecks(app, window, outputDirectory)) {
        checks.append(entry);
        success &= entry.toObject().value("passed").toBool();
    }
#endif
    app->getSettings()->setRestorePreviousSession(true);
    app->getSettings()->setRestoreTempFiles(true);
    window->newFile();
    window->currentEditor()->setText("Unsaved recovery: Türkçe 🌍\n");
    check("session-commit", app->getSessionManager()->saveSession(window));
    const QString manifest = QDir(ApplicationSettings::profileDirectory()).filePath("session/session.ini");
    check("session-manifest", QFile::exists(manifest));
    app->getSettings()->sync();
#ifdef BIT8PAD_TESTING
    for (const auto &entry : runRecoveryChecks(app, window)) {
        checks.append(entry);
        success &= entry.toObject().value("passed").toBool();
    }
#endif

    QJsonObject report{{"success", success}, {"checks", checks},
                       {"qt", QT_VERSION_STR}, {"platform", QGuiApplication::platformName()}};
    QFile reportFile(QDir(outputDirectory).filePath("smoke-results.json"));
    if (!reportFile.open(QIODevice::WriteOnly) || reportFile.write(QJsonDocument(report).toJson()) < 0) success = false;
    reportFile.close();
    app->exit(success ? 0 : 1);
}
