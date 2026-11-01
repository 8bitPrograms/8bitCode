// SPDX-License-Identifier: GPL-3.0-or-later
// Links only EightBit::Editor. A source tab is opened, highlighted from its Lua
// profile, edited, undone and saved without any 8bitPad application code.
// Every fixture is synthetic and generated in a temporary directory.
#include "ScintillaNext.h"
#include "ILexer.h"
#include "Lexilla.h"
#include "LuaExtension.h"
#include "LuaState.h"
#include "SciLexer.h"

#include <QFile>
#include <QStringConverter>
#include <QTemporaryDir>
#include <QTextCodec>
#include <QtTest>
#include <memory>

class EditorLibraryTest : public QObject
{
    Q_OBJECT

    std::unique_ptr<LuaState> lua;

    static QByteArray contents(ScintillaNext &editor)
    {
        return QByteArray(reinterpret_cast<const char *>(editor.characterPointer()), editor.length());
    }

    static bool writeFile(const QString &path, const QByteArray &bytes)
    {
        QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
    }

    static QByteArray readFile(const QString &path)
    {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }

    // Same Lua queries as NotepadNextApplication::detectLanguageFromExtension()
    // and setEditorLanguage(), using only what the library provides.
    QString languageForExtension(const QString &extension)
    {
        lua->setVariable("ext", extension);
        return lua->executeAndReturn<QString>(R"(
        for name, L in pairs(languages) do
            if L.extensions then
                for _, v in ipairs(L.extensions) do
                    if v == ext then return name end
                end
            end
        end
        return "Text"
        )");
    }

    void applyLanguage(ScintillaNext *editor, const QString &languageName)
    {
        LuaExtension::Instance().setEditor(editor);
        lua->setVariable("languageName", languageName);
        const QString lexer = lua->executeAndReturn<QString>("return languages[languageName].lexer");
        editor->languageName = languageName;
        editor->languageSingleLineComment = lua->executeAndReturn<QString>("return languages[languageName].singleLineComment or \"\"").toUtf8();
        editor->setILexer(reinterpret_cast<sptr_t>(CreateLexer(lexer.toLatin1().constData())));
        editor->clearDocumentStyle();
        lua->setVariable("skip_tabs", false);
        lua->setVariable("skip_tabwidth", false);
        lua->execute("SetLanguage(languageName)");
    }

private slots:
    void initTestCase()
    {
        QVERIFY(QFile::exists(QStringLiteral(":/scripts/init.lua")));
        lua = std::make_unique<LuaState>();
        lua->executeFile(QStringLiteral(":/scripts/init.lua"));
        LuaExtension::Instance().Initialise(lua->L, nullptr); // always returns false
    }

    void cleanupTestCase()
    {
        // LuaState owns and closes the Lua state, as in the application.
        LuaExtension::Instance().setEditor(nullptr);
    }

    void resourcesTravelWithLibrary()
    {
        QVERIFY(QFile::exists(QStringLiteral(":/languages/cpp.lua")));
        QVERIFY(QFile::exists(QStringLiteral(":/languages/python.lua")));
        QVERIFY(!readFile(QStringLiteral(":/themes/catalog.json")).isEmpty());
        QVERIFY(QFile::exists(QStringLiteral(":/themes/NOTICES.md")));
    }

    void sourceTabIsHighlighted()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("main.cpp"));
        const QByteArray source("// synthetic fixture\nint main() { return 42; }\n");
        QVERIFY(writeFile(path, source));
        std::unique_ptr<ScintillaNext> editor(ScintillaNext::fromFile(path));
        QVERIFY(editor);

        const QString language = languageForExtension(editor->getFileInfo().suffix().toLower());
        QCOMPARE(language, QStringLiteral("C++"));
        applyLanguage(editor.get(), language);
        editor->colourise(0, -1);

        QCOMPARE(editor->lexerLanguage(), QByteArray("cpp"));
        QCOMPARE(editor->languageSingleLineComment, QByteArray("// "));
        QCOMPARE(editor->styleAt(source.indexOf("//")), sptr_t(SCE_C_COMMENTLINE));
        QCOMPARE(editor->styleAt(source.indexOf("return")), sptr_t(SCE_C_WORD));
        QCOMPARE(editor->styleAt(source.indexOf("int")), sptr_t(SCE_C_WORD2));
        QCOMPARE(editor->styleAt(source.indexOf("42")), sptr_t(SCE_C_NUMBER));
        QVERIFY(editor->styleFore(SCE_C_WORD) != editor->styleFore(SCE_C_DEFAULT));
        QCOMPARE(contents(*editor), source); // styling never touches the buffer
        QVERIFY(!editor->modify());
    }

    void undoRestoresOpenedText()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("undo.py"));
        const QByteArray source("def greet():\r\n    return 'hi'\r\n");
        QVERIFY(writeFile(path, source));
        std::unique_ptr<ScintillaNext> editor(ScintillaNext::fromFile(path));
        QVERIFY(editor);
        applyLanguage(editor.get(), languageForExtension(QStringLiteral("py")));
        QVERIFY(!editor->canUndo()); // loading the file is not an undoable edit

        editor->insertText(source.indexOf("'hi'"), "'hello' + ");
        editor->deleteRange(0, 4);
        const QByteArray edited = contents(*editor);
        QVERIFY(editor->modify());
        editor->undo();
        editor->undo();
        QCOMPARE(contents(*editor), source);
        QVERIFY(!editor->modify());
        editor->redo();
        editor->redo();
        QCOMPARE(contents(*editor), edited);
        QCOMPARE(editor->save(), QFileDevice::NoError);
        QCOMPARE(readFile(path), edited);
        QVERIFY(!editor->modify());
    }

    void roundTripKeepsEncodingBomAndEol_data()
    {
        QTest::addColumn<QByteArray>("bom");
        QTest::addColumn<QByteArray>("codec");
        QTest::addColumn<QString>("openAs");
        QTest::addColumn<int>("bomType");
        QTest::addColumn<QString>("encodingName");
        QTest::addColumn<QByteArray>("eol");

        struct Encoding { const char *name; QByteArray bom; QByteArray codec; QString openAs; ScintillaNext::BomType bomType; QString encodingName; };
        const QList<Encoding> encodings {
            {"utf8", {}, "UTF-8", {}, ScintillaNext::BomType::None, QStringLiteral("UTF-8")},
            {"utf8-bom", QByteArray::fromHex("efbbbf"), "UTF-8", {}, ScintillaNext::BomType::Utf8, QStringLiteral("UTF-8 BOM")},
            {"utf16le-bom", QByteArray::fromHex("fffe"), "UTF-16LE", {}, ScintillaNext::BomType::Utf16LE, QStringLiteral("UTF-16LE")},
            {"utf16be-bom", QByteArray::fromHex("feff"), "UTF-16BE", {}, ScintillaNext::BomType::Utf16BE, QStringLiteral("UTF-16BE")},
            {"windows-1252", {}, "windows-1252", QStringLiteral("windows-1252"), ScintillaNext::BomType::None, QStringLiteral("windows-1252")},
        };
        const QList<QPair<const char *, QByteArray>> eols {{"lf", "\n"}, {"crlf", "\r\n"}, {"cr", "\r"}};
        for (const auto &encoding : encodings) {
            for (const auto &eol : eols) {
                QTest::newRow(QByteArray(encoding.name).append('-').append(eol.first).constData())
                    << encoding.bom << encoding.codec << encoding.openAs << int(encoding.bomType)
                    << encoding.encodingName << eol.second;
            }
        }
    }

    void roundTripKeepsEncodingBomAndEol()
    {
        QFETCH(QByteArray, bom);
        QFETCH(QByteArray, codec);
        QFETCH(QString, openAs);
        QFETCH(int, bomType);
        QFETCH(QString, encodingName);
        QFETCH(QByteArray, eol);

        // QStringEncoder never writes a BOM by itself, so the fixture controls it.
        const auto encode = [&](const QString &text) -> QByteArray {
            if (codec == "UTF-8") return QStringEncoder(QStringConverter::Utf8)(text);
            if (codec == "UTF-16LE") return QStringEncoder(QStringConverter::Utf16LE)(text);
            if (codec == "UTF-16BE") return QStringEncoder(QStringConverter::Utf16BE)(text);
            auto *legacy = QTextCodec::codecForName(codec);
            return legacy ? legacy->fromUnicode(text) : QByteArray();
        };
        const QString nl = QString::fromLatin1(eol);
        const QString text = QStringLiteral("// café — synthetic fixture") + nl
            + QStringLiteral("int main() {") + nl
            + QStringLiteral("    return 0;") + nl
            + QStringLiteral("}") + nl;
        QString expectedText = text;
        expectedText.replace(QStringLiteral("return 0;"), QStringLiteral("return 42;"));
        expectedText.replace(QStringLiteral("café"), QStringLiteral("cafés"));

        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("main.cpp"));
        const QByteArray original = bom + encode(text);
        QVERIFY(writeFile(path, original));

        std::unique_ptr<ScintillaNext> editor(ScintillaNext::fromFile(path, false, openAs));
        QVERIFY(editor);
        QCOMPARE(int(editor->bom()), bomType);
        QCOMPARE(editor->encodingName(), encodingName);
        QCOMPARE(contents(*editor), text.toUtf8());
        applyLanguage(editor.get(), languageForExtension(QStringLiteral("cpp")));
        editor->colourise(0, -1);
        QCOMPARE(editor->styleAt(contents(*editor).indexOf("return")), sptr_t(SCE_C_WORD));

        // Edit inside lines only: the EOL bytes in the buffer must survive as loaded.
        QByteArray buffer = contents(*editor);
        editor->insertText(buffer.indexOf("0;"), "42");
        buffer = contents(*editor);
        editor->deleteRange(buffer.indexOf("0;"), 1);
        editor->insertText(contents(*editor).indexOf(" \xe2\x80\x94"), "s");
        QCOMPARE(contents(*editor), expectedText.toUtf8());

        QCOMPARE(editor->save(), QFileDevice::NoError);
        QVERIFY(!editor->modify());
        const QByteArray saved = readFile(path);
        QCOMPARE(saved, bom + encode(expectedText));
        QVERIFY(saved.startsWith(bom));
        QCOMPARE(int(editor->bom()), bomType);
        QCOMPARE(editor->encodingName(), encodingName);

        // Undo back to the opened text and save again: byte-identical to the original.
        while (editor->canUndo())
            editor->undo();
        QCOMPARE(contents(*editor), text.toUtf8());
        QCOMPARE(editor->save(), QFileDevice::NoError);
        QCOMPARE(readFile(path), original);
    }
};

QTEST_MAIN(EditorLibraryTest)
#include "EditorLibraryTest.moc"
