// SPDX-License-Identifier: GPL-3.0-or-later
// Synthetic profiles in temporary folders only; never a real configuration folder.
#include "LegacyProfileImport.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>
#ifdef Q_OS_UNIX
#include <sys/stat.h>
#include <unistd.h>
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
// A start long enough after a failed run that the import tries again.
QDateTime aDayLater() { return QDateTime::currentDateTimeUtc().addSecs(25 * 3600); }

// Relative path, size, time and content hash of every entry, to prove a folder is unchanged.
QStringList snapshot(const QString &root)
{
    QStringList entries;
    QDirIterator it(root, QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QFileInfo info(it.next());
        // A link is recorded as itself; its target is not part of this folder.
        if (info.isSymLink()) {
            entries << QDir(root).relativeFilePath(info.filePath()) + " -> " + info.symLinkTarget();
            continue;
        }
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

#ifdef Q_OS_UNIX
    void linksInTheOldFolderAreNotFollowed()
    {
        QVERIFY(write(current + "/mine.txt", "already in 8bitCode"));
        QVERIFY(write(temp.filePath("outside/secret.txt"), "not part of any profile"));
        QVERIFY(QFile::link(current, legacy + "/to-new"));
        QVERIFY(QFile::link(temp.filePath("outside"), legacy + "/session/to-outside"));
        QVERIFY(QFile::link(temp.filePath("outside/secret.txt"), legacy + "/to-file"));
        const QStringList before = snapshot(legacy), outside = snapshot(temp.filePath("outside"));
        const auto result = LegacyProfileImport::run(legacy, current);
        QVERIFY2(result.errors.isEmpty(), qPrintable(result.errors.join('\n')));
        QCOMPARE(result.copied, 4);
        QStringList links = result.links;
        links.sort();
        QCOMPARE(links, QStringList({"session/to-outside", "to-file", "to-new"}));
        QStringList names;
        for (const QString &entry : snapshot(current)) names << entry.section(' ', 0, 0);
        QCOMPARE(names, QStringList({".imported-from-8bitPad", "8bitpad.ini", "mine.txt", "session", "session/session.ini",
            "session/unsaved-1.txt", "updates.ini"}));
        QCOMPARE(snapshot(legacy), before);
        QCOMPARE(snapshot(temp.filePath("outside")), outside);
    }

    void linkLoopsEnd()
    {
        QVERIFY(QFile::link(".", legacy + "/loop"));
        QVERIFY(QFile::link("..", legacy + "/session/up"));
        const auto result = LegacyProfileImport::run(legacy, current);
        QVERIFY2(result.errors.isEmpty(), qPrintable(result.errors.join('\n')));
        QCOMPARE(result.copied, 4);
        QCOMPARE(result.links.size(), 2);
        QVERIFY(!QFileInfo::exists(current + "/loop") && !QFileInfo(current + "/loop").isSymLink());
        QVERIFY(!QFileInfo::exists(current + "/session/up") && !QFileInfo(current + "/session/up").isSymLink());
    }

    void oldFolderContainingTheNewOneIsRefused()
    {
        // The old profile is a link to a folder that holds the new profile:
        // copying would walk into its own output.
        QVERIFY(write(current + "/mine.txt", "already in 8bitCode"));
        QVERIFY(QDir(legacy).removeRecursively());
        QVERIFY(QFile::link(temp.filePath("data"), legacy));
        const QStringList before = snapshot(temp.filePath("data"));
        const auto result = LegacyProfileImport::run(legacy, current);
        QCOMPARE(snapshot(temp.filePath("data")), before);
        QVERIFY(!result.ran);
        QCOMPARE(result.copied, 0);
        QCOMPARE(result.errors.size(), 1);
    }

    void newFolderInsideTheOldOneIsRefused()
    {
        // The new profile is a link into the old one: every copy would change the old folder.
        QVERIFY(QDir(temp.filePath("data/8bitCode")).mkpath("."));
        QVERIFY(QFile::link(legacy + "/session", current));
        const QStringList before = snapshot(legacy);
        const auto result = LegacyProfileImport::run(legacy, current);
        QCOMPARE(snapshot(legacy), before);
        QVERIFY(!result.ran);
        QCOMPARE(result.copied, 0);
        QCOMPARE(result.errors.size(), 1);
    }

    void sameFolderThroughALinkIsLeftAlone()
    {
        QVERIFY(QDir(temp.filePath("data/8bitCode")).mkpath("."));
        QVERIFY(QFile::link(legacy, current));
        const QStringList before = snapshot(legacy);
        const auto result = LegacyProfileImport::run(legacy, current);
        QVERIFY(!result.ran);
        QVERIFY(result.errors.isEmpty());
        QCOMPARE(snapshot(legacy), before);
    }

    void linksInTheNewFolderAreNotWrittenThrough()
    {
        // Links already in the new folder must not carry a copy outside it.
        QVERIFY(QDir(temp.filePath("outside")).mkpath("."));
        QVERIFY(QDir(current).mkpath("."));
        QVERIFY(QFile::link(temp.filePath("outside"), current + "/session"));
        QVERIFY(QFile::link(temp.filePath("outside/updates.ini"), current + "/updates.ini"));
        QVERIFY(QFile::link(temp.filePath("outside/marker"), current + '/' + LegacyProfileImport::MarkerName));
        const QStringList before = snapshot(legacy);
        const auto result = LegacyProfileImport::run(legacy, current);
        QCOMPARE(snapshot(temp.filePath("outside")), QStringList());
        QStringList errors = result.errors;
        errors.sort();
        QCOMPARE(errors.size(), 3);
        QVERIFY(errors.at(0).startsWith("session/session.ini: "));
        QVERIFY(errors.at(1).startsWith("session/unsaved-1.txt: "));
        QVERIFY(errors.at(2).startsWith("updates.ini: "));
        QCOMPARE(result.copied, 1);
        QCOMPARE(snapshot(legacy), before);

        // Even with every copy possible, the marker is not written through a link.
        QVERIFY(QFile::remove(current + "/session"));
        QVERIFY(QFile::remove(current + "/updates.ini"));
        const auto again = LegacyProfileImport::run(legacy, current, aDayLater());
        QCOMPARE(again.errors.size(), 1);
        QVERIFY(again.errors.first().startsWith(LegacyProfileImport::MarkerName));
        QCOMPARE(snapshot(temp.filePath("outside")), QStringList());
    }
#endif

#ifdef Q_OS_WIN
    void junctionsAreNotFollowed()
    {
        // Junctions are not symlinks to every API; the import must still not follow them.
        QVERIFY(write(current + "/mine.txt", "already in 8bitCode"));
        QVERIFY(write(temp.filePath("outside/secret.txt"), "not part of any profile"));
        const auto junction = [](const QString &link, const QString &target) {
            return QProcess::execute("cmd", {"/c", "mklink", "/J", QDir::toNativeSeparators(link), QDir::toNativeSeparators(target)}) == 0;
        };
        QVERIFY(junction(legacy + "/to-new", current));
        QVERIFY(junction(legacy + "/session/to-outside", temp.filePath("outside")));
        const QStringList before = snapshot(temp.filePath("outside"));
        const auto result = LegacyProfileImport::run(legacy, current);
        QVERIFY2(result.errors.isEmpty(), qPrintable(result.errors.join('\n')));
        QCOMPARE(result.copied, 4);
        QStringList links = result.links;
        links.sort();
        QCOMPARE(links, QStringList({"session/to-outside", "to-new"}));
        QVERIFY(!QFileInfo::exists(current + "/to-new"));
        QVERIFY(!QFileInfo::exists(current + "/session/to-outside"));
        QCOMPARE(snapshot(temp.filePath("outside")), before);
    }

    void junctionedFoldersInsideEachOtherAreRefused()
    {
        const auto junction = [](const QString &link, const QString &target) {
            return QProcess::execute("cmd", {"/c", "mklink", "/J", QDir::toNativeSeparators(link), QDir::toNativeSeparators(target)}) == 0;
        };
        // The old profile is a junction to the folder holding the new one.
        QVERIFY(write(current + "/mine.txt", "already in 8bitCode"));
        QVERIFY(QDir(legacy).removeRecursively());
        QVERIFY(junction(legacy, temp.filePath("data/8bitCode")));
        auto result = LegacyProfileImport::run(legacy, current);
        QCOMPARE(QDir(current).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot), QStringList{"mine.txt"});
        QVERIFY(!result.ran);
        QCOMPARE(result.errors.size(), 1);

        // The new profile is a junction into the old one.
        QVERIFY(QDir().rmdir(legacy));
        QVERIFY(write(legacy + "/session/session.ini", "[Session]\nCount=1\n"));
        QVERIFY(QDir(current).removeRecursively());
        QVERIFY(junction(current, legacy + "/session"));
        result = LegacyProfileImport::run(legacy, current);
        QCOMPARE(QDir(legacy + "/session").entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot), QStringList{"session.ini"});
        QVERIFY(!result.ran);
        QCOMPARE(result.errors.size(), 1);
        QVERIFY(QDir().rmdir(current));
    }

    void junctionInTheNewFolderIsNotWrittenThrough()
    {
        QVERIFY(QDir(temp.filePath("outside")).mkpath("."));
        QVERIFY(QDir(current).mkpath("."));
        QCOMPARE(QProcess::execute("cmd", {"/c", "mklink", "/J", QDir::toNativeSeparators(current + "/session"),
            QDir::toNativeSeparators(temp.filePath("outside"))}), 0);
        const auto result = LegacyProfileImport::run(legacy, current);
        QCOMPARE(result.errors.size(), 2);
        QCOMPARE(result.copied, 2);
        QCOMPARE(QDir(temp.filePath("outside")).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot), QStringList());
        QVERIFY(!QFileInfo::exists(current + '/' + LegacyProfileImport::MarkerName));
    }
#endif

    void existingFilesKeepTheirTime()
    {
        // Same size as the old file but another time: this is 8bitCode's own
        // file (or a finished copy), so neither its data nor its time may change.
        QVERIFY(write(current + "/session/unsaved-1.txt", "unsaved synthetic data"));
        QFile own(current + "/session/unsaved-1.txt");
        QVERIFY(own.open(QIODevice::ReadWrite));
        QVERIFY(own.setFileTime(QDateTime::fromMSecsSinceEpoch(1000000000000), QFileDevice::FileModificationTime));
        own.close();
        const auto result = LegacyProfileImport::run(legacy, current);
        QVERIFY2(result.errors.isEmpty(), qPrintable(result.errors.join('\n')));
        QCOMPARE(result.copied, 3);
        QCOMPARE(result.skipped, QStringList{"session/unsaved-1.txt"});
        QCOMPARE(read(current + "/session/unsaved-1.txt"), QByteArray("unsaved synthetic data"));
        QCOMPARE(QFileInfo(current + "/session/unsaved-1.txt").lastModified(), QDateTime::fromMSecsSinceEpoch(1000000000000));
        // Every copy has the old file's time, set before the copy was put in place.
        for (const QString &name : {"8bitpad.ini", "session/session.ini", "updates.ini"})
            QCOMPARE(QFileInfo(current + '/' + name).lastModified(), QFileInfo(legacy + '/' + name).lastModified());
    }

#ifdef Q_OS_UNIX
    void hardLinkInTheNewFolderIsLeftAlone()
    {
        // A hard link shares the old file's data and time; touching it would change the old folder.
        QVERIFY(QDir(current).mkpath("."));
        QCOMPARE(::link(QFile::encodeName(legacy + "/updates.ini").constData(), QFile::encodeName(current + "/updates.ini").constData()), 0);
        const QStringList before = snapshot(legacy);
        const auto result = LegacyProfileImport::run(legacy, current);
        QVERIFY2(result.errors.isEmpty(), qPrintable(result.errors.join('\n')));
        QCOMPARE(result.skipped, QStringList{"updates.ini"});
        QCOMPARE(snapshot(legacy), before);
    }

    void unreadableFolderIsAnError()
    {
        if (::geteuid() == 0) QSKIP("root can read any folder");
        QVERIFY(::chmod(QFile::encodeName(legacy + "/session").constData(), 0) == 0);
        const auto result = LegacyProfileImport::run(legacy, current);
        QVERIFY(::chmod(QFile::encodeName(legacy + "/session").constData(), 0755) == 0);
        QVERIFY(!QFileInfo::exists(current + '/' + LegacyProfileImport::MarkerName));
        QCOMPARE(result.errors, QStringList{"session: cannot read the folder"});
        QCOMPARE(result.copied, 2);
    }

    void entryThatCannotBeCheckedIsAnError()
    {
        // A folder that can be listed but not entered: its entries cannot be checked.
        if (::geteuid() == 0) QSKIP("root can enter any folder");
        QVERIFY(write(legacy + "/vault/inner/note.txt", "synthetic"));
        QVERIFY(::chmod(QFile::encodeName(legacy + "/vault").constData(), 0444) == 0);
        const auto result = LegacyProfileImport::run(legacy, current);
        QVERIFY(::chmod(QFile::encodeName(legacy + "/vault").constData(), 0755) == 0);
        QVERIFY(!QFileInfo::exists(current + '/' + LegacyProfileImport::MarkerName));
        QVERIFY2(!result.errors.isEmpty(), "an entry that cannot be checked was ignored");
        QVERIFY(result.errors.first().startsWith("vault"));
    }
#endif

#ifdef Q_OS_WIN
    void longPathsAreCopied()
    {
        // Deeper than the 260 characters plain Windows calls accept.
        QString deep = legacy;
        while (deep.size() < 320) deep += "/a-rather-long-folder-name";
        QVERIFY(write(deep + "/note.txt", "synthetic"));
        const auto result = LegacyProfileImport::run(legacy, current);
        QVERIFY2(result.errors.isEmpty(), qPrintable(result.errors.join('\n')));
        QCOMPARE(result.copied, 5);
        QVERIFY(QFileInfo::exists(current + deep.mid(legacy.size()) + "/note.txt"));
    }
#endif

    void missingOldFolderDoesNothing()
    {
        QVERIFY(QDir(temp.filePath("data/8bitPad")).removeRecursively());
        const auto result = LegacyProfileImport::run(legacy, current);
        QVERIFY(!result.ran);
        QVERIFY(!QFileInfo::exists(current + '/' + LegacyProfileImport::MarkerName));
        QVERIFY(!LegacyProfileImport::run(QString(), current).ran);
    }

    void failureWaitsADayThenRetries()
    {
        // A file where the session folder must go makes that copy fail on every platform.
        QVERIFY(write(current + "/session", "blocks the folder"));
        const QStringList before = snapshot(legacy);
        const auto failed = LegacyProfileImport::run(legacy, current);
        QCOMPARE(failed.errors.size(), 2);
        QVERIFY(!QFileInfo::exists(current + '/' + LegacyProfileImport::MarkerName));
        QVERIFY(QFileInfo::exists(current + '/' + LegacyProfileImport::FailedName));
        QCOMPARE(read(current + "/8bitpad.ini"), read(legacy + "/8bitpad.ini"));
        QCOMPARE(snapshot(legacy), before);

        // The next starts the same day do not walk the folders again.
        const QStringList afterFailure = snapshot(current);
        for (int start = 0; start < 3; ++start) {
            const auto again = LegacyProfileImport::run(legacy, current);
            QVERIFY(again.postponed);
            QVERIFY(!again.ran);
            QVERIFY(again.errors.isEmpty());
        }
        QCOMPARE(snapshot(current), afterFailure);

        QVERIFY(QFile::remove(current + "/session"));
        const auto retried = LegacyProfileImport::run(legacy, current, aDayLater());
        QVERIFY2(retried.errors.isEmpty(), qPrintable(retried.errors.join('\n')));
        QCOMPARE(retried.copied, 2);
        QCOMPARE(retried.skipped.size(), 2);
        QVERIFY(QFileInfo::exists(current + '/' + LegacyProfileImport::MarkerName));
        QVERIFY(!QFileInfo::exists(current + '/' + LegacyProfileImport::FailedName));
    }
};

QTEST_GUILESS_MAIN(LegacyProfileImportTest)
#include "LegacyProfileImportTest.moc"
