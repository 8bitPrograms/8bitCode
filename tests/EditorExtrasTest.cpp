// SPDX-License-Identifier: GPL-3.0-or-later
#include "GitStatus.h"
#include "LineDiff.h"
#include "NoteFormat.h"
#include "Readability.h"
#include "ShellIntegration.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

class EditorExtrasTest : public QObject
{
    Q_OBJECT
private slots:
    void diffReconstructsBothSides();
    void diffMarksASingleChange();
    void notePatternAndFile();
    void gitPorcelain();
    void contrastFloor();
    void shellText();
    void sqlDialectsAreBundled();
};

void EditorExtrasTest::diffReconstructsBothSides()
{
    const QStringList left = {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c"), QStringLiteral("d")};
    const QStringList right = {QStringLiteral("a"), QStringLiteral("x"), QStringLiteral("c"), QStringLiteral("d"), QStringLiteral("e")};
    const DiffResult diff = diffLines(left, right);
    QStringList rebuiltLeft;
    QStringList rebuiltRight;
    for (const DiffRow &row : diff.rows) {
        if (row.kind != DiffRow::Insert)
            rebuiltLeft.append(row.left);
        if (row.kind != DiffRow::Delete)
            rebuiltRight.append(row.right);
    }
    QCOMPARE(rebuiltLeft, left);
    QCOMPARE(rebuiltRight, right);
    QVERIFY(!diff.truncated);
}

void EditorExtrasTest::diffMarksASingleChange()
{
    const DiffResult same = diffLines({QStringLiteral("only")}, {QStringLiteral("only")});
    QCOMPARE(same.rows.size(), 1);
    QCOMPARE(same.rows.at(0).kind, DiffRow::Equal);

    const DiffResult changed = diffLines({QStringLiteral("keep"), QStringLiteral("old"), QStringLiteral("tail")},
                                         {QStringLiteral("keep"), QStringLiteral("new"), QStringLiteral("tail")});
    QCOMPARE(changed.rows.size(), 3);
    QCOMPARE(changed.rows.at(0).kind, DiffRow::Equal);
    QCOMPARE(changed.rows.at(1).kind, DiffRow::Change);
    QCOMPARE(changed.rows.at(1).left, QStringLiteral("old"));
    QCOMPARE(changed.rows.at(1).right, QStringLiteral("new"));
    QCOMPARE(changed.rows.at(2).kind, DiffRow::Equal);
}

void EditorExtrasTest::notePatternAndFile()
{
    NoteContext context;
    context.when = QDateTime(QDate(2026, 10, 23), QTime(20, 48, 1));
    context.counter = 7;
    context.selection = QStringLiteral("selected line");
    context.user = QStringLiteral("berry");
    QCOMPARE(expandNotePattern(QStringLiteral("{date} {counter}"), context), QStringLiteral("2026-10-23 7"));
    QCOMPARE(expandNotePattern(QStringLiteral("%Y-%m-%d"), context), QStringLiteral("2026-10-23"));
    QCOMPARE(noteFileName(QStringLiteral("Meet {user}"), QStringLiteral(".TXT"), context), QStringLiteral("Meet berry.txt"));

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString error;
    const QString path = createNoteFile(dir.path(), QStringLiteral("Note {n}"), QStringLiteral("txt"),
                                        QStringLiteral("Hello {selection}"), context, &error);
    QVERIFY2(!path.isEmpty(), qPrintable(error));
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(QString::fromUtf8(file.readAll()), QStringLiteral("Hello selected line\n"));
    QVERIFY(QFileInfo(path).fileName().startsWith(QStringLiteral("Note 7")));
}

void EditorExtrasTest::gitPorcelain()
{
    const QString repo = QStringLiteral("C:/repo");
    QByteArray output = "## main...origin/main";
    output += '\0';
    output += " M src/a.cpp";
    output += '\0';
    output += "?? src/b.cpp";
    output += '\0';
    output += "A  src/c.cpp";
    output += '\0';
    const GitSnapshot snapshot = parseGitStatus(output, repo, repo);
    QCOMPARE(snapshot.branch, QStringLiteral("main"));
    QCOMPARE(snapshot.changed, 2);
    QCOMPARE(snapshot.untracked, 1);
    QCOMPARE(snapshot.paths.value(normalizePath(QStringLiteral("C:/repo/src/a.cpp"))), GitPathState::Modified);
    QCOMPARE(snapshot.paths.value(normalizePath(QStringLiteral("C:/repo/src/b.cpp"))), GitPathState::Untracked);
    QCOMPARE(snapshot.paths.value(normalizePath(QStringLiteral("C:/repo/src/c.cpp"))), GitPathState::Added);
    QCOMPARE(snapshot.paths.value(normalizePath(QStringLiteral("C:/repo/src"))), GitPathState::Modified);
    QVERIFY(snapshot.label().contains(QStringLiteral("main")));
    QVERIFY(snapshot.label().contains(QStringLiteral("•")));

    QTemporaryDir workspace;
    QVERIFY(workspace.isValid());
    QDir(workspace.path()).mkdir(QStringLiteral("nested"));
    QFile git(QDir(workspace.path()).filePath(QStringLiteral(".git")));
    QVERIFY(git.open(QIODevice::WriteOnly));
    git.write("gitdir: elsewhere\n");
    git.close();
    QCOMPARE(QDir(findGitRoot(QDir(workspace.path()).filePath(QStringLiteral("nested")))).absolutePath(),
             QDir(workspace.path()).absolutePath());
    QVERIFY(findGitRoot(QDir(workspace.path()).filePath(QStringLiteral("missing"))).isEmpty());
}

void EditorExtrasTest::contrastFloor()
{
    const QColor white(255, 255, 255);
    const QColor black(0, 0, 0);
    QVERIFY(contrastRatio(white, black) > 4.5);
    QCOMPARE(liftContrast(white, black, 4.5), white);
    const QColor lifted = liftContrast(QColor(40, 40, 40), QColor(32, 32, 32), 4.5);
    QVERIFY(contrastRatio(lifted, QColor(32, 32, 32)) >= 4.5);
    QVERIFY(lifted.lightness() > 40);
}

void EditorExtrasTest::shellText()
{
    const QString command = windowsOpenCommand(QStringLiteral("C:/Program Files/8bitPad.exe"), QStringLiteral("%1"));
    QCOMPARE(command, QLatin1Char('"') + QDir::toNativeSeparators(QStringLiteral("C:/Program Files/8bitPad.exe")) + QStringLiteral("\" \"%1\""));
    const QString desktop = linuxDesktopEntry(QStringLiteral("/opt/8bitPad"));
    QVERIFY(desktop.contains(QStringLiteral("Exec=/opt/8bitPad %F")));
    QVERIFY(desktop.contains(QStringLiteral("inode/directory")));
    QCOMPARE(mimeTypeForExtension(QStringLiteral(".sql")), QStringLiteral("application/sql"));
    QCOMPARE(mimeTypeForExtension(QStringLiteral("notes")), QStringLiteral("application/x-8bitpad-notes"));
    QVERIFY(mimeTypeForExtension(QStringLiteral("../x")).isEmpty());
}

void EditorExtrasTest::sqlDialectsAreBundled()
{
    const QString root = QStringLiteral(BIT8PAD_SOURCE_DIR);
    const QStringList files = {QStringLiteral("sql-oracle.lua"), QStringLiteral("sql-postgresql.lua"), QStringLiteral("sql-mssql.lua")};
    QFile init(QDir(root).filePath(QStringLiteral("src/editor/scripts/init.lua")));
    QVERIFY2(init.open(QIODevice::ReadOnly), qPrintable(init.fileName()));
    const QString initText = QString::fromUtf8(init.readAll());
    QVERIFY(initText.contains(QStringLiteral("CopySqlLanguage")));
    QVERIFY(initText.contains(QStringLiteral("SQL (Oracle)")));
    QVERIFY(initText.contains(QStringLiteral("SQL (PostgreSQL)")));
    QVERIFY(initText.contains(QStringLiteral("SQL (MSSQL)")));
    QFile qrc(QDir(root).filePath(QStringLiteral("src/editor/scripts.qrc")));
    QVERIFY(qrc.open(QIODevice::ReadOnly));
    const QString qrcText = QString::fromUtf8(qrc.readAll());
    for (const QString &file : files) {
        QFile source(QDir(root).filePath(QStringLiteral("src/editor/languages/") + file));
        QVERIFY2(source.open(QIODevice::ReadOnly), qPrintable(source.fileName()));
        const QString text = QString::fromUtf8(source.readAll());
        QVERIFY(text.contains(QStringLiteral("CopySqlLanguage")));
        QVERIFY(!text.contains(QStringLiteral("\"sql\"")));
        QVERIFY(qrcText.contains(file));
    }
}

QTEST_GUILESS_MAIN(EditorExtrasTest)
#include "EditorExtrasTest.moc"
