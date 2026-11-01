// SPDX-License-Identifier: GPL-3.0-or-later
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QtTest>

class CommandLauncherTest : public QObject {
    Q_OBJECT
private slots:
    void preservesArgumentsAndWorkingDirectory()
    {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString bin = fixture.filePath("program folder");
        const QString cwd = fixture.filePath(QString::fromUtf8("work \xC3\xBC & % space"));
        QVERIFY(QDir().mkpath(bin));
        QVERIFY(QDir().mkpath(cwd));
        const QString alias = QDir(bin).filePath("8p.exe");
        QVERIFY(QFile::copy(QDir(QCoreApplication::applicationDirPath()).filePath("8p.exe"), alias));
        QVERIFY(QFile::copy(QCoreApplication::applicationFilePath(), QDir(bin).filePath("8bitCode.exe")));
        const QString report = fixture.filePath("captured.json");
        const QStringList expected{".", "some.txt", "space & % ! ^ name.txt",
            QString::fromUtf8("T\xC3\xBCrk\xC3\xA7" "e.txt"), "", "quoted\"name", "trailing\\", "--", "-name.txt"};
        QProcess process;
        process.setWorkingDirectory(cwd);
        process.start(alias, QStringList{"--capture-command", report} + expected);
        QVERIFY(process.waitForStarted());
        QVERIFY(process.waitForFinished());
        QCOMPARE(process.exitCode(), 0);
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(report), 5000);
        QFile output(report);
        QVERIFY(output.open(QIODevice::ReadOnly));
        const QJsonObject actual = QJsonDocument::fromJson(output.readAll()).object();
        QCOMPARE(actual["cwd"].toString(), QDir::fromNativeSeparators(cwd));
        QCOMPARE(actual["arguments"].toArray(), QJsonArray::fromStringList(expected));
    }
    void missingAdjacentApplicationFails()
    {
        QTemporaryDir fixture;
        const QString alias = fixture.filePath("8p.exe");
        QVERIFY(QFile::copy(QDir(QCoreApplication::applicationDirPath()).filePath("8p.exe"), alias));
        QProcess process;
        process.start(alias, {"."});
        QVERIFY(process.waitForStarted());
        QVERIFY(process.waitForFinished());
        QVERIFY(process.exitCode() != 0);
    }
};

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    const auto args = application.arguments();
    if (args.size() >= 3 && args[1] == "--capture-command") {
        QSaveFile file(args[2]);
        if (!file.open(QIODevice::WriteOnly)) return 2;
        const QJsonObject captured{{"cwd", QDir::currentPath()},
            {"arguments", QJsonArray::fromStringList(args.mid(3))}};
        if (file.write(QJsonDocument(captured).toJson()) < 0 || !file.commit()) return 3;
        return 0;
    }
    CommandLauncherTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "CommandLauncherTest.moc"
