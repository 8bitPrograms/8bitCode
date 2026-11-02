// SPDX-License-Identifier: GPL-3.0-or-later
// Synthetic profiles in temporary folders only; never a real configuration folder.
#include "LegacyProfileImport.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>
#ifdef Q_OS_UNIX
#include <sys/stat.h>
#endif

namespace {
bool write(const QString &path, const QByteArray &data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(data) == data.size();
}
QByteArray read(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray("<missing>");
}
// Relative path, size, time and content hash of every entry, to prove a folder is unchanged.
QStringList snapshot(const QString &root)
{
    QStringList entries;
    QDirIterator it(root, QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QFileInfo info(it.next());
        entries << QDir(root).relativeFilePath(info.filePath()) + ' ' + QString::number(info.size()) + ' '
            + QString::number(info.lastModified().toMSecsSinceEpoch())
            + ' ' + QCryptographicHash::hash(info.isFile() ? read(info.filePath()) : QByteArray(), QCryptographicHash::Sha256).toHex();
    }
    entries.sort();
    return entries;
}
}

class LegacyProfileImportTest : public QObject
{
    Q_OBJECT
    QTemporaryDir temp;
    QString legacy, current;
private slots:
    void init()
    {
        QVERIFY(temp.isValid());
        legacy = temp.filePath("data/8bitPad/8bitPad");
        current = temp.filePath("data/8bitCode/8bitCode");
        QDir(temp.path()).removeRecursively();
        QVERIFY(write(legacy + "/8bitpad.ini", "[App]\nRestorePreviousSession=true\n"));
        QVERIFY(write(legacy + "/session/session.ini", "[Session]\nCount=1\n"));
        QVERIFY(write(legacy + "/session/unsaved-1.txt", "unsaved synthetic text"));
        QVERIFY(write(legacy + "/updates.ini", "[Updates]\nAutoCheck=false\n"));
        QVERIFY(write(legacy + "/8bitpad.ini.lock", "stale"));
    }

    void derivesTheLegacyFolder()
    {
        QCOMPARE(LegacyProfileImport::legacyDirectoryFor(current), legacy);
        QCOMPARE(LegacyProfileImport::legacyDirectoryFor(current + '/'), legacy);
        QVERIFY(LegacyProfileImport::legacyDirectoryFor(temp.filePath("portable/profile")).isEmpty());
    }

    void freshImportCopiesAndKeepsTheOldFolder()
    {
        const QStringList before = snapshot(legacy);
        const auto result = LegacyProfileImport::run(legacy, current);
        QVERIFY2(result.errors.isEmpty(), qPrintable(result.errors.join('\n')));
        QVERIFY(result.ran);
        QCOMPARE(result.copied, 4);
        QCOMPARE(read(current + "/8bitpad.ini"), read(legacy + "/8bitpad.ini"));
        QCOMPARE(read(current + "/session/unsaved-1.txt"), QByteArray("unsaved synthetic text"));
        QCOMPARE(QFileInfo(current + "/session/session.ini").lastModified(), QFileInfo(legacy + "/session/session.ini").lastModified());
        QVERIFY(!QFileInfo::exists(current + "/8bitpad.ini.lock"));
        QVERIFY(QFileInfo::exists(current + '/' + LegacyProfileImport::MarkerName));
        QCOMPARE(snapshot(legacy), before);
    }

    void secondRunIsANoOp()
    {
        QVERIFY(LegacyProfileImport::run(legacy, current).errors.isEmpty());
        QVERIFY(write(legacy + "/later.txt", "written by 8bitPad after the import"));
        QVERIFY(write(current + "/8bitpad.ini", "changed in 8bitCode"));
        const QStringList after = snapshot(current);
        const auto again = LegacyProfileImport::run(legacy, current);
        QVERIFY(!again.ran);
        QCOMPARE(again.copied, 0);
        QCOMPARE(snapshot(current), after);
    }

    void existingFilesAreNeverOverwritten()
    {
        QVERIFY(write(current + "/8bitpad.ini", "8bitCode settings"));
        const auto result = LegacyProfileImport::run(legacy, current);
        QVERIFY(result.errors.isEmpty());
        QCOMPARE(result.skipped, QStringList{"8bitpad.ini"});
        QCOMPARE(result.copied, 3);
        QCOMPARE(read(current + "/8bitpad.ini"), QByteArray("8bitCode settings"));
    }

#ifdef Q_OS_UNIX
    void specialFilesAreSkippedInsteadOfBlocking()
    {
        // A FIFO must not be opened: reading it would block the primary
        // instance forever waiting for a writer.
        QVERIFY2(::mkfifo(QFile::encodeName(legacy + "/session/report.pipe").constData(), 0666) == 0,
            qPrintable(legacy + "/session/report.pipe"));
        const QStringList before = snapshot(legacy);
        const auto result = LegacyProfileImport::run(legacy, current);
        QVERIFY2(result.errors.isEmpty(), qPrintable(result.errors.join('\n')));
        QVERIFY(result.ran);
        QCOMPARE(result.copied, 4);
        QVERIFY(!QFileInfo::exists(current + "/session/report.pipe"));
        QVERIFY(QFileInfo::exists(current + '/' + LegacyProfileImport::MarkerName));
        QCOMPARE(snapshot(legacy), before);
    }
#endif

    void unfinishedImportIsFinishedByRepairingTimestamps()
    {
        // Same size but a different time: an earlier unfinished import had
        // written the contents, then died before the marker and setFileTime().
        QVERIFY(write(current + "/session/unsaved-1.txt", "unsaved synthetic data"));
        QFile previousCopy(current + "/session/unsaved-1.txt");
        QVERIFY(previousCopy.open(QIODevice::ReadWrite));
        QVERIFY(previousCopy.setFileTime(QDateTime::fromMSecsSinceEpoch(1000000000000), QFileDevice::FileModificationTime));
        previousCopy.close();
        // A different size is not an earlier copy of ours; never touch it.
        QVERIFY(write(current + "/8bitpad.ini", "changed by the user"));
        QFile other(current + "/8bitpad.ini");
        QVERIFY(other.open(QIODevice::ReadWrite));
        QVERIFY(other.setFileTime(QDateTime::fromMSecsSinceEpoch(1000000000123), QFileDevice::FileModificationTime));
        other.close();
        const QDateTime before = QFileInfo(current + "/8bitpad.ini").lastModified();

        const auto result = LegacyProfileImport::run(legacy, current);
        QVERIFY2(result.errors.isEmpty(), qPrintable(result.errors.join('\n')));
        QCOMPARE(result.copied, 2); // session.ini and updates.ini are still new
        QCOMPARE(result.skipped, QStringList{"8bitpad.ini"});
        QVERIFY(QFileInfo::exists(current + '/' + LegacyProfileImport::MarkerName));
        // The unfinished copy was finished in time only, never rewritten.
        QCOMPARE(read(current + "/session/unsaved-1.txt"), QByteArray("unsaved synthetic data"));
        QCOMPARE(QFileInfo(current + "/session/unsaved-1.txt").lastModified(),
            QFileInfo(legacy + "/session/unsaved-1.txt").lastModified());
        QCOMPARE(read(current + "/8bitpad.ini"), QByteArray("changed by the user"));
        QCOMPARE(QFileInfo(current + "/8bitpad.ini").lastModified(), before);
    }

    void missingOldFolderDoesNothing()
    {
        QVERIFY(QDir(temp.filePath("data/8bitPad")).removeRecursively());
        const auto result = LegacyProfileImport::run(legacy, current);
        QVERIFY(!result.ran);
        QVERIFY(!QFileInfo::exists(current + '/' + LegacyProfileImport::MarkerName));
        QVERIFY(!LegacyProfileImport::run(QString(), current).ran);
    }

    void failureLeavesNoMarkerAndRetries()
    {
        // A file where the session folder must go makes that copy fail on every platform.
        QVERIFY(write(current + "/session", "blocks the folder"));
        const QStringList before = snapshot(legacy);
        const auto failed = LegacyProfileImport::run(legacy, current);
        QCOMPARE(failed.errors.size(), 2);
        QVERIFY(!QFileInfo::exists(current + '/' + LegacyProfileImport::MarkerName));
        QCOMPARE(read(current + "/8bitpad.ini"), read(legacy + "/8bitpad.ini"));
        QCOMPARE(snapshot(legacy), before);

        QVERIFY(QFile::remove(current + "/session"));
        const auto retried = LegacyProfileImport::run(legacy, current);
        QVERIFY(retried.errors.isEmpty());
        QCOMPARE(retried.copied, 2);
        QCOMPARE(retried.skipped.size(), 2);
        QVERIFY(QFileInfo::exists(current + '/' + LegacyProfileImport::MarkerName));
    }
};

QTEST_GUILESS_MAIN(LegacyProfileImportTest)
#include "LegacyProfileImportTest.moc"
