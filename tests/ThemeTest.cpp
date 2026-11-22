// SPDX-License-Identifier: GPL-3.0-or-later
#include "ThemeTest.h"

#include "ApplicationSettings.h"
#include "EditorManager.h"
#include "LargeFilePolicy.h"
#include "LuaExtension.h"
#include "LuaState.h"
#include "MainWindow.h"
#include "NotepadNextApplication.h"
#include "ScintillaNext.h"
#include "Readability.h"
#include "ThemeManager.h"
#include "AutoCompletion.h"
#include "SmartHighlighter.h"
#include "URLFinder.h"
#include "VimMode.h"
#include "TabsQuickActionsBar.h"

#include <QApplication>
#include <QAction>
#include <QCoreApplication>
#include <QDialog>
#include <QElapsedTimer>
#include <QEvent>
#include <QFile>
#include <QImage>
#include <QJsonObject>
#include <QKeyEvent>
#include <QPointer>
#include <QSet>
#include <QTemporaryDir>
#include <QTimer>

namespace {
sptr_t sciColor(const QColor &c)
{
    return c.red() | (c.green() << 8) | (c.blue() << 16);
}

QByteArray bytes(ScintillaNext *editor)
{
    return QByteArray(reinterpret_cast<const char *>(editor->characterPointer()), editor->length());
}

void settle()
{
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
}
}

QJsonArray runThemeChecks(NotepadNextApplication *app, MainWindow *window)
{
    QJsonArray checks;
    const auto check = [&checks](const QString &name, bool passed, const QString &detail = QString()) {
        QJsonObject result{{QStringLiteral("name"), name}, {QStringLiteral("passed"), passed}};
        if (!detail.isEmpty())
            result.insert(QStringLiteral("detail"), detail);
        checks.append(result);
    };
    const auto metric = [&checks](const QString &name, qint64 elapsedNs, qint64 size, bool passed = true) {
        checks.append(QJsonObject{{QStringLiteral("name"), name}, {QStringLiteral("passed"), passed},
                                  {QStringLiteral("elapsedMilliseconds"), elapsedNs / 1000000.0},
                                  {QStringLiteral("bytes"), size}});
    };
    QPointer<ScintillaNext> original = window->currentEditor();
    auto *themes = app->getThemeManager();
    const QString previousTheme = themes->currentThemeId();
    // Isolated profiles exercise the real constructor, including persistence.
    QTemporaryDir themeProfile;
    check("theme-default-profile-fixture", themeProfile.isValid());
    if (themeProfile.isValid()) {
        const QByteArray savedProfile = qgetenv("BIT8PAD_PROFILE");
        const bool hadProfile = qEnvironmentVariableIsSet("BIT8PAD_PROFILE");
        qputenv("BIT8PAD_PROFILE", themeProfile.path().toUtf8());
        {
            ApplicationSettings isolated;
            {
                ThemeManager fresh(&isolated, app->getLuaState());
                check("theme-fresh-profile-is-classic-white", fresh.currentThemeId() == "classic-light" &&
                      !fresh.currentTheme().isDark && fresh.currentTheme().background == QColor(Qt::white));
                check("theme-bonus-locked-by-default", !fresh.bonusThemesUnlocked() && fresh.availableThemes().size() == ThemeManager::catalog().size() - 5);
                for (int tap = 0; tap < 6; ++tap) fresh.registerVersionTap();
                check("theme-six-taps-do-not-unlock", !fresh.bonusThemesUnlocked());
                QEventLoop pause;
                QTimer::singleShot(1600, &pause, &QEventLoop::quit);
                pause.exec();
                fresh.registerVersionTap();
                check("theme-slow-taps-reset-sequence", !fresh.bonusThemesUnlocked());
                for (int tap = 0; tap < 6; ++tap) fresh.registerVersionTap();
                check("theme-seven-taps-unlock", fresh.bonusThemesUnlocked() && fresh.availableThemes().size() == ThemeManager::catalog().size());
                check("theme-unlock-does-not-repeat", !fresh.registerVersionTap());
            }
            isolated.setValue("appearance/theme", "ayu-dark");
            {
                ThemeManager saved(&isolated, app->getLuaState());
                check("theme-explicit-saved-choice-preserved", saved.currentThemeId() == "ayu-dark");
                check("theme-bonus-unlock-persists", saved.bonusThemesUnlocked());
            }
            isolated.setValue("appearance/bonusThemesUnlocked", false);
            for (const auto &bonus : ThemeManager::catalog()) {
                if (!ThemeManager::isBonusTheme(bonus.id)) continue;
                isolated.setValue("appearance/theme", bonus.id);
                ThemeManager migrated(&isolated, app->getLuaState());
                bool hidden = true;
                for (const auto &available : migrated.availableThemes())
                    hidden &= !ThemeManager::isBonusTheme(available.id);
                check("theme-locked-saved-choice-stays-hidden-" + bonus.id, hidden &&
                    !migrated.bonusThemesUnlocked() && migrated.currentThemeId() == bonus.id);
            }
            isolated.setValue("appearance/theme", "missing-theme");
            {
                ThemeManager fallback(&isolated, app->getLuaState());
                check("theme-invalid-saved-choice-falls-back-white", fallback.currentThemeId() == "classic-light");
            }
        }
        if (hadProfile) qputenv("BIT8PAD_PROFILE", savedProfile); else qunsetenv("BIT8PAD_PROFILE");
        themes->setTheme("classic-light");
        themes->setTheme(previousTheme);
    }
    auto *first = app->getEditorManager()->createEditor(QStringLiteral("Theme checks.cpp"));
    auto *second = app->getEditorManager()->createEditor(QStringLiteral("Theme checks.py"));
    const QByteArray cpp = "// comment\nint main() { const char* s = \"text\"; return 42; }\n";
    const QByteArray python = "# comment\nclass Widget:\n    def method(self):\n        return \"text\", 42\n";
    first->setText(cpp.constData());
    first->emptyUndoBuffer();
    first->appendText(8, "// tail\n");
    app->setEditorLanguage(first, QStringLiteral("C++"));
    second->setText(python.constData());
    app->setEditorLanguage(second, QStringLiteral("Python"));
    settle();
    first->colourise(0, -1);
    second->colourise(0, -1);
    first->setSelection(9, 3);
    const QByteArray changedCpp = bytes(first);
    const QByteArray previousFont = first->styleFont(STYLE_DEFAULT);
    const sptr_t previousSize = first->styleSize(STYLE_DEFAULT);
    const sptr_t previousTabWidth = first->tabWidth();
    const bool previousUseTabs = first->useTabs();
    const auto tabBars = window->findChildren<TabsQuickActionsBar *>();
    check(QStringLiteral("theme-tab-quick-actions-present"), !tabBars.isEmpty());

    const auto token = [&check](ScintillaNext *editor, const QByteArray &source,
                                const char *needle, const QColor &expected, const QString &name) {
        const qsizetype position = source.indexOf(needle);
        const sptr_t style = position >= 0 ? editor->styleAt(position) : -1;
        const sptr_t actual = style >= 0 ? editor->styleFore(style) : -1;
        check(name, actual == sciColor(expected),
              QStringLiteral("style=%1 actual=%2 expected=%3")
                  .arg(style).arg(actual, 0, 16).arg(sciColor(expected), 0, 16));
    };

    for (const auto &theme : ThemeManager::catalog()) {
        const QString prefix = QStringLiteral("theme-colors-") + theme.id + QLatin1Char('-');
        if (theme.id.startsWith("cyberpunk-")) {
            QSet<QRgb> distinct;
            bool readable = true;
            for (const QColor &color : {theme.comment, theme.keyword, theme.string, theme.number,
                                       theme.function, theme.type, theme.error}) {
                distinct.insert(color.rgb());
                readable &= contrastRatio(color, theme.background) >= 4.5;
            }
            check(prefix + "distinct-syntax-roles", distinct.size() == 7);
            check(prefix + "readable-syntax-roles", readable);
        }
        const auto ink = [&theme](const QColor &color) { return liftContrast(color, theme.background, 4.5); };
        check(prefix + QStringLiteral("switch"), themes->setTheme(theme.id));
        token(first, cpp, "comment", ink(theme.comment), prefix + QStringLiteral("cpp-comment"));
        token(first, cpp, "int", ink(theme.type), prefix + QStringLiteral("cpp-type"));
        token(first, cpp, "return", ink(theme.keyword), prefix + QStringLiteral("cpp-keyword"));
        token(first, cpp, "text", ink(theme.string), prefix + QStringLiteral("cpp-string"));
        token(first, cpp, "42", ink(theme.number), prefix + QStringLiteral("cpp-number"));
        token(second, python, "comment", ink(theme.comment), prefix + QStringLiteral("python-comment"));
        token(second, python, "Widget", ink(theme.type), prefix + QStringLiteral("python-class"));
        token(second, python, "method", ink(theme.function), prefix + QStringLiteral("python-function"));
        token(second, python, "return", ink(theme.keyword), prefix + QStringLiteral("python-keyword"));
        token(second, python, "text", ink(theme.string), prefix + QStringLiteral("python-string"));
        token(second, python, "42", ink(theme.number), prefix + QStringLiteral("python-number"));
        check(prefix + QStringLiteral("preserves-document"),
              bytes(first) == changedCpp && first->canUndo() && first->currentPos() == 9 &&
              first->anchor() == 3 && first->styleFont(STYLE_DEFAULT) == previousFont &&
              first->styleSize(STYLE_DEFAULT) == previousSize && first->tabWidth() == previousTabWidth &&
              first->useTabs() == previousUseTabs);
        check(prefix + QStringLiteral("all-style-backgrounds"), [&] {
            for (int id = 0; id <= STYLE_MAX; ++id) {
                const bool special = id == STYLE_LINENUMBER || id == STYLE_BRACELIGHT ||
                                     id == STYLE_BRACEBAD || id == STYLE_CALLTIP;
                if (!special && first->styleBack(id) != sciColor(theme.background))
                    return false;
            }
            return true;
        }());
        check(prefix + QStringLiteral("tab-icons"), [&] {
            for (auto *bar : tabBars) {
                // The stylesheet may lift foreground colors for readability.
                // Icons must track the active widget palette and stay legible.
                const QColor expected = bar->palette().color(QPalette::Active, QPalette::ButtonText);
                if (contrastRatio(expected, bar->palette().color(QPalette::Window)) < 3.0)
                    return false;
                for (auto *action : bar->actions()) {
                    const auto icon = action->icon().pixmap(24, 24).toImage();
                    bool hasInk = false;
                    for (int y = 0; y < icon.height(); ++y) {
                        for (int x = 0; x < icon.width(); ++x) {
                            const auto pixel = icon.pixelColor(x, y);
                            if (pixel.alpha() > 240) {
                                hasInk = true;
                                if (pixel.rgb() != expected.rgb()) return false;
                            }
                        }
                    }
                    if (!hasInk) return false;
                }
            }
            return true;
        }());
    }
    first->undo();
    check(QStringLiteral("theme-change-preserves-undo-operation"), bytes(first) == cpp);

    themes->setTheme(QStringLiteral("ayu-mirage"));
    const auto &ayu = themes->currentTheme();
    first->setText("{\"label\": \"hello\", \"number\": 42, \"enabled\": true}\n");
    app->setEditorLanguage(first, QStringLiteral("JSON"));
    settle();
    first->colourise(0, -1);
    const auto json = bytes(first);
    token(first, json, "hello", ayu.string, QStringLiteral("theme-json-string"));
    token(first, json, "42", ayu.number, QStringLiteral("theme-json-number"));
    token(first, json, "true", ayu.keyword, QStringLiteral("theme-json-keyword"));
    check(QStringLiteral("theme-queued-language-reapply"), first->styleBack(2) == sciColor(ayu.background));

    const QByteArray html = "<html><script>var n = \"embedded\"; // script-comment\n</script></html>\n";
    first->setText(html.constData());
    app->setEditorLanguage(first, QStringLiteral("HTML"));
    settle();
    first->colourise(0, -1);
    token(first, html, "html", ayu.keyword, QStringLiteral("theme-html-tag"));
    token(first, html, "embedded", ayu.string, QStringLiteral("theme-html-embedded-string"));
    token(first, html, "script-comment", ayu.comment, QStringLiteral("theme-html-embedded-comment"));

    const QByteArray diff = "--- old\n+++ new\n@@ -1 +1 @@\n-removed\n+added\n";
    first->setText(diff.constData());
    app->setEditorLanguage(first, QStringLiteral("DIFF"));
    settle();
    first->colourise(0, -1);
    token(first, diff, "removed", ayu.error, QStringLiteral("theme-diff-removed"));
    token(first, diff, "added", ayu.string, QStringLiteral("theme-diff-added"));

    // Verify every supported Lua language leaves theme backgrounds intact after
    // its normal setup; do not require all grammars to classify the same sample.
    first->setText("sample = 42\n");
    for (const auto &language : app->getLanguages()) {
        app->setEditorLanguage(first, language);
        settle();
        check(QStringLiteral("theme-language-background-") + language,
              first->styleBack(0) == sciColor(ayu.background) &&
              first->styleBack(STYLE_DEFAULT) == sciColor(ayu.background));
    }

    lua_State *state = app->getLuaState()->L;
    const int top = lua_gettop(state);
    lua_pushliteral(state, "theme-test-stack-sentinel");
    themes->applyEditor(first);
    check(QStringLiteral("theme-preserves-lua-stack"), lua_gettop(state) == top + 1 &&
          QByteArray(lua_tostring(state, -1)) == "theme-test-stack-sentinel");
    lua_settop(state, top);
    first->setUpdatesEnabled(false);
    themes->applyEditor(first);
    check(QStringLiteral("theme-preserves-disabled-widget-updates"), !first->updatesEnabled());
    first->setUpdatesEnabled(true);
    // ADS temporarily reparents inactive dock pages to nullptr. Activate this
    // page so MainWindow is actually its ancestor for the inheritance check.
    window->switchToEditor(first);
    settle();
    const bool hasUpdateAncestor = window->isAncestorOf(first);
    window->setUpdatesEnabled(false);
    themes->applyEditor(first);
    const bool respectsParentSuppression = !first->updatesEnabled();
    window->setUpdatesEnabled(true);
    check(QStringLiteral("theme-inherited-update-suppression-resumes"),
          hasUpdateAncestor && respectsParentSuppression && first->updatesEnabled() && first->viewport()->updatesEnabled());
    check(QStringLiteral("theme-rejects-unknown-id"), !themes->setTheme(QStringLiteral("missing-theme")) &&
          themes->currentThemeId() == QStringLiteral("ayu-mirage"));
    themes->setTheme(QStringLiteral("GitHub Light"));
    app->getSettings()->sync();
    {
        ApplicationSettings readBack;
        check(QStringLiteral("theme-persists-id"), readBack.value(QStringLiteral("appearance/theme")).toString()
              == QStringLiteral("github-light"));
    }
    auto *transient = app->getEditorManager()->createEditor(QStringLiteral("Theme queued-close.cpp"));
    app->setEditorLanguage(transient, QStringLiteral("C++"));
    QPointer<ScintillaNext> deleted(transient);
    window->closeFile(transient, true);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    settle();
    check(QStringLiteral("theme-queued-closed-editor-safe"), deleted.isNull());

    // Exercise the real queued :q! route for a modified on-disk buffer whose
    // file disappeared. FileMissing stays dirty even at a Scintilla save point.
    // A watchdog rejects any regression prompt instead of hanging the suite.
    QTemporaryDir missingDirectory;
    const QString missingPath = missingDirectory.filePath(QStringLiteral("force-close.txt"));
    QFile fixture(missingPath);
    const bool fixtureReady = missingDirectory.isValid() && fixture.open(QIODevice::WriteOnly) &&
                              fixture.write("before\n") == 7;
    fixture.close();
    check(QStringLiteral("vim-force-close-fixture"), fixtureReady);
    if (fixtureReady) {
        const int countBefore = window->editorCount();
        QPointer<ScintillaNext> missing = app->getEditorManager()->createEditorFromFile(missingPath);
        check(QStringLiteral("vim-force-close-opened"), !missing.isNull());
        if (missing) {
            missing->appendText(6, "dirty\n");
            const bool removed = QFile::remove(missingPath);
            check(QStringLiteral("vim-force-close-missing-dirty"), removed &&
                  missing->checkFileForStateChange() == ScintillaNext::FileStateChange::Deleted &&
                  !missing->isSavedToDisk());
            bool prompted = false;
            QTimer modalWatchdog;
            modalWatchdog.setInterval(10);
            QObject::connect(&modalWatchdog, &QTimer::timeout, window, [&prompted] {
                if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) {
                    prompted = true;
                    dialog->reject();
                }
            });
            modalWatchdog.start();
            auto *vim = missing->findChild<VimMode *>();
            check(QStringLiteral("vim-force-close-controller"), vim != nullptr);
            if (vim) {
                vim->setEnabled(true);
                const auto key = [&missing](int code, const QString &text = QString()) {
                    QKeyEvent event(QEvent::KeyPress, code, Qt::NoModifier, text);
                    QCoreApplication::sendEvent(missing.data(), &event);
                };
                key(Qt::Key_Escape);
                key(Qt::Key_Colon, QStringLiteral(":"));
                key(Qt::Key_Q, QStringLiteral("q"));
                key(Qt::Key_Exclam, QStringLiteral("!"));
                key(Qt::Key_Return, QStringLiteral("\r"));
                settle();
                QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            }
            modalWatchdog.stop();
            check(QStringLiteral("vim-force-close-missing-no-prompt"), !prompted && missing.isNull() &&
                  window->editorCount() == countBefore && !QFile::exists(missingPath));
            if (missing) {
                window->closeFile(missing, true);
                QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            }
        }
    }

    // Real automatic-decorator checks across the dynamic threshold. Settings
    // are recorded before and after to prove the optimization is not a preference.
    window->switchToEditor(first);
    auto *highlighter = first->findChild<SmartHighlighter *>();
    auto *completion = first->findChild<AutoCompletion *>();
    auto *urls = first->findChild<URLFinder *>();
    check(QStringLiteral("performance-decorators-present"), highlighter && completion && urls);
    if (highlighter && completion && urls) {
        const bool autoPreference = app->getSettings()->autoCompletion();
        const bool urlPreference = app->getSettings()->urlHighlighting();
        const bool wrapPreference = app->getSettings()->wordWrap();
        const bool urlWasEnabled = urls->isEnabled();
        if (!urlWasEnabled) urls->setEnabled(true);
        app->setEditorLanguage(first, QStringLiteral("C++"));
        const QByteArray small = "berrycom\nberrycompletionword\nmarker marker\nhttps://example.com\nret\n";
        first->setText(small.constData());
        first->gotoPos(8);
        settle();
        completion->showAutoCompletion();
        check(QStringLiteral("performance-small-document-completion"), first->autoCActive());
        first->autoCCancel();
        const int indicator = first->allocateIndicator(QStringLiteral("smart_highlighter"));
        Scintilla::NotificationData notice{};
        notice.nmhdr.code = Scintilla::Notification::UpdateUI;
        notice.updated = Scintilla::Update::Selection;
        const int marker = static_cast<int>(small.indexOf("marker"));
        first->setSelection(marker + 6, marker);
        highlighter->notify(&notice);
        check(QStringLiteral("performance-small-document-highlighting"), first->indicatorValueAt(indicator, marker) != 0);
        first->gotoPos(0);
        QMetaObject::invokeMethod(urls, "findURLs", Qt::DirectConnection);
        check(QStringLiteral("performance-small-document-url"), urls->isURL(static_cast<int>(small.indexOf("https")) + 5));

        QByteArray large = small;
        const QByteArray row = QByteArray("// berrycompletionword marker ") + QByteArray(95, 'x') + '\n';
        const int targetSize = 24 * 1024 * 1024;
        large.reserve(targetSize + row.size());
        while (large.size() < targetSize)
            large.append(row);
        QElapsedTimer timer;
        timer.start();
        first->setText(large.constData());
        metric(QStringLiteral("performance-24mib-buffer-load"), timer.nsecsElapsed(), large.size());
        first->emptyUndoBuffer();
        first->setSelection(marker + 6, marker);
        timer.restart();
        highlighter->notify(&notice);
        metric(QStringLiteral("performance-24mib-selection-guard"), timer.nsecsElapsed(), first->length(),
               first->indicatorValueAt(indicator, marker) == 0);
        first->gotoPos(8);
        first->autoCCancel();
        timer.restart();
        completion->showAutoCompletion();
        metric(QStringLiteral("performance-24mib-completion-guard"), timer.nsecsElapsed(), first->length(), !first->autoCActive());
        const int returnPrefix = static_cast<int>(small.lastIndexOf("ret"));
        first->gotoPos(returnPrefix + 3);
        completion->showAutoCompletion();
        check(QStringLiteral("performance-large-language-keywords-retained"), first->autoCActive());
        first->autoCCancel();
        timer.restart();
        QMetaObject::invokeMethod(urls, "findURLs", Qt::DirectConnection);
        metric(QStringLiteral("performance-24mib-url-guard"), timer.nsecsElapsed(), first->length(),
               !urls->isURL(static_cast<int>(small.indexOf("https")) + 5));
        timer.restart();
        themes->setTheme(QStringLiteral("ayu-mirage"));
        themes->applyEditor(first);
        metric(QStringLiteral("performance-24mib-theme-switch"), timer.nsecsElapsed(), first->length());
        check(QStringLiteral("performance-guards-preserve-buffer"), first->length() == large.size() &&
              first->get_text_range(0, small.size()) == small && !first->canUndo());

        const QByteArray longLine = QByteArray(96 * 1024, 'a') + " https://example.com\n";
        first->setText(longLine.constData());
        first->gotoPos(8);
        first->autoCCancel();
        timer.restart();
        completion->showAutoCompletion();
        QMetaObject::invokeMethod(urls, "findURLs", Qt::DirectConnection);
        first->setSelection(96 * 1024, 0);
        highlighter->notify(&notice);
        metric(QStringLiteral("performance-long-line-guards"), timer.nsecsElapsed(), first->length(),
               !first->autoCActive() && !urls->isURL(static_cast<int>(longLine.indexOf("https")) + 5) &&
               first->indicatorValueAt(indicator, 0) == 0);
        first->setText(small.constData());
        first->gotoPos(8);
        completion->showAutoCompletion();
        const bool completionResumed = first->autoCActive();
        first->autoCCancel();
        first->setSelection(marker + 6, marker);
        highlighter->notify(&notice);
        check(QStringLiteral("performance-resumes-after-shrinking"), completionResumed &&
              first->indicatorValueAt(indicator, marker) != 0);
        check(QStringLiteral("performance-preserves-preferences"), autoPreference == app->getSettings()->autoCompletion() &&
              urlPreference == app->getSettings()->urlHighlighting() && wrapPreference == app->getSettings()->wordWrap());
        if (!urlWasEnabled) urls->setEnabled(false);
    }

    check(QStringLiteral("performance-threshold-boundaries"),
          !LargeFilePolicy::reduceAutomaticWork(LargeFilePolicy::documentThreshold - 1) &&
          LargeFilePolicy::reduceAutomaticWork(LargeFilePolicy::documentThreshold) &&
          !LargeFilePolicy::skipLineScan(LargeFilePolicy::longLineThreshold - 1) &&
          LargeFilePolicy::skipLineScan(LargeFilePolicy::longLineThreshold));
    window->closeFile(second, true);
    window->closeFile(first, true);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    themes->setTheme(previousTheme);
    if (original) {
        window->switchToEditor(original);
        LuaExtension::Instance().setEditor(original);
    }
    settle();
    check(QStringLiteral("theme-original-editor-restored"), original && window->currentEditor() == original &&
          original->isVisible() && original->updatesEnabled() && original->viewport()->isVisible() &&
          original->viewport()->width() > 10 && original->viewport()->height() > 10,
          original ? QStringLiteral("visible=%1 updates=%2 viewportVisible=%3 viewport=%4x%5 linesOnScreen=%6 font=%7 size=%8")
              .arg(original->isVisible()).arg(original->updatesEnabled()).arg(original->viewport()->isVisible())
              .arg(original->viewport()->width()).arg(original->viewport()->height()).arg(original->linesOnScreen())
              .arg(QString::fromUtf8(original->styleFont(STYLE_DEFAULT))).arg(original->styleSize(STYLE_DEFAULT))
              : QStringLiteral("original editor destroyed"));
    return checks;
}
