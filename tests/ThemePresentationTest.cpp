// SPDX-License-Identifier: GPL-3.0-or-later
#include "ThemeManager.h"
#include "LuaState.h"
#include "LuaExtension.h"
#include "ScintillaNext.h"
#include "ILexer.h"
#include "Lexilla.h"
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <memory>
class ThemePresentationTest : public QObject
{
    Q_OBJECT
private slots:
    void settingsBelongToTheConsumerAndUnlockPersists()
    {
        QTemporaryDir dir;
        QSettings settings(dir.filePath("notes.ini"), QSettings::IniFormat);
        ThemeManager themes(&settings, nullptr);
        QCOMPARE(themes.currentThemeId(), QStringLiteral("classic-light"));
        QVERIFY(!themes.bonusThemesUnlocked());
        const auto locked = themes.availableThemes().size();
        for (int i = 0; i < 6; ++i)
            QVERIFY(!themes.registerVersionTap());
        QVERIFY(themes.registerVersionTap());
        QVERIFY(themes.availableThemes().size() > locked);
        QVERIFY(themes.setTheme("blueberry"));
        ThemeManager restored(&settings, nullptr);
        QVERIFY(restored.bonusThemesUnlocked());
        QCOMPARE(restored.currentThemeId(), QStringLiteral("blueberry"));
        QVERIFY(!themes.setTheme("missing theme"));
    }
    void liveThemesPreserveBufferAndStyleEmbeddedLanguages()
    {
        QTemporaryDir dir;
        QSettings settings(dir.filePath("consumer.ini"), QSettings::IniFormat);
        LuaState lua;
        lua.executeFile(QStringLiteral(":/scripts/init.lua"));
        LuaExtension::Instance().Initialise(lua.L, nullptr);
        ScintillaNext editor("sample.sql");
        editor.languageName = "SQL";
        LuaExtension::Instance().setEditor(&editor);
        editor.setILexer(reinterpret_cast<sptr_t>(CreateLexer("sql")));
        lua.execute("SetLanguage('SQL')");
        editor.appendText(9, "select 1;");
        editor.gotoPos(3);
        ThemeManager themes(&settings, &lua);
        themes.watchEditor(&editor);
        QVERIFY(themes.setTheme("dracula"));
        QCOMPARE(editor.get_text_range(0, editor.length()), QByteArray("select 1;"));
        QCOMPARE(editor.currentPos(), 3);
        QVERIFY(editor.canUndo());
        auto color = [&](const QColor& c) { return c.red() | (c.green() << 8) | (c.blue() << 16); };
        QCOMPARE(editor.styleFore(0), color(themes.currentTheme().foreground));
        // The same profile/semantic rule colours a source slot and a borrowed
        // fence slot. It does not require either product's settings or manager.
        themes.applyEmbeddedLanguage(&editor, "SQL", 80);
        QCOMPARE(editor.styleFore(85), editor.styleFore(5));
        editor.undo();
        QCOMPARE(editor.length(), 0);
        LuaExtension::Instance().setEditor(nullptr);
    }
};
QTEST_MAIN(ThemePresentationTest)
#include "ThemePresentationTest.moc"
