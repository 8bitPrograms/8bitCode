// SPDX-License-Identifier: GPL-3.0-or-later
#include "HandleFs.h"

#include <QDirIterator>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QtTest>

#include <cerrno>
#include <functional>
#include <sys/stat.h>

using namespace HandleFs;

namespace {
int hookCodes[16] = {};
Step actionStep;
std::function<void()> hookAction;

int hook(Step step, const QString &)
{
    if (hookAction && step == actionStep)
        std::exchange(hookAction, nullptr)();
    return hookCodes[int(step)];
}

// Fails step with code; action runs once when the step is reached.
void inject(Step step, int code, std::function<void()> action = nullptr)
{
    hookCodes[int(step)] = code;
    if (action) {
        actionStep = step;
        hookAction = std::move(action);
    }
    setTestHook(hook);
}

bool writeFile(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

// Every entry with its type, size, mode and time; links are listed, not followed.
QStringList snapshot(const QString &root)
{
    QStringList out;
    QDirIterator it(root, QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QFileInfo i = it.nextFileInfo();
        out << QStringLiteral("%1 %2 %3 %4 %5").arg(i.filePath()).arg(i.isSymLink()).arg(i.size()).arg(i.permissions())
                   .arg(i.lastModified().toMSecsSinceEpoch());
    }
    out.sort();
    return out;
}

// Writes a temp and publishes it as name; the temp is removed if that fails.
Status publish(const Dir &dir, const QString &name, const QByteArray &bytes)
{
    const QString temp = QLatin1Char('.') + name + QLatin1String(".tmp");
    File file = createExclusiveAt(dir, temp, 0644);
    if (!file.isOpen() || !file.writeAll(bytes.constData(), bytes.size()).ok())
        return {EIO};
    const Status status = renameNoReplaceAt(dir, file, temp, name);
    if (!status.ok())
        discard(dir, file, temp);
    return status;
}
}

class HandleFsTest : public QObject
{
    Q_OBJECT
    QTemporaryDir tmp;
    QString root;
    int runs = 0;

    QString path(const char *name) const { return root + QLatin1Char('/') + QLatin1String(name); }
    QStringList files() const { return QDir(root).entryList(QDir::Hidden | QDir::Files, QDir::Name); }

private slots:
    void init()
    {
        root = tmp.path() + QLatin1Char('/') + QString::number(++runs);
        QVERIFY(QDir().mkpath(path("sub/deep")) && writeFile(path("a.txt"), "alpha"));
        QVERIFY(QFile::link(path("sub"), path("dirlink")) && QFile::link(path("a.txt"), path("filelink")));
    }
    void cleanup()
    {
        setTestHook(nullptr);
        std::fill(std::begin(hookCodes), std::end(hookCodes), 0);
        hookAction = nullptr;
    }

    void openRootCreatesOnlyWhenAsked()
    {
        QVERIFY(!Dir::openRoot(path("x/y"), Create::No).isOpen());
        QVERIFY(!QFileInfo::exists(path("x")));
        Dir dir = Dir::openRoot(path("x/y"), Create::Yes);
        QVERIFY(dir.isOpen() && dir.id().valid && QFileInfo(path("x/y")).isDir());
        QCOMPARE(dir.id(), Dir::openRoot(path("x/y"), Create::No).id());
        QVERIFY(dir.id() != Dir::openRoot(root, Create::No).id());
    }

    void readCallsLeaveTheTreeUnchanged()
    {
        const QStringList before = snapshot(root);
        Dir dir = Dir::openRoot(root, Create::No);
        Dir sub = openDirAt(dir, QStringLiteral("sub"), Create::No);
        QVERIFY(sub.isOpen());
        const Dir none = openDirAt(sub, QStringLiteral("none"), Create::No);
        QVERIFY(!none.isOpen() && none.status().code == ENOENT && none.kind() == Kind::Missing);
        File file = openFileAt(dir, QStringLiteral("a.txt"));
        char buf[16];
        QCOMPARE(file.read(buf, sizeof buf), 5);
        QCOMPARE(file.read(buf, sizeof buf), 0);
        QCOMPARE(file.size(), 5);
        QVERIFY(listAt(dir) && listAt(sub));
        QCOMPARE(statAt(dir, QStringLiteral("none")), Kind::Missing);
        QCOMPARE(openFileAt(dir, QStringLiteral("none")).status().code, ENOENT);
        QCOMPARE(snapshot(root), before);
    }

    void linksAreNotFollowed()
    {
        Dir dir = Dir::openRoot(root, Create::No);
        for (const QString &name : {QStringLiteral("dirlink"), QStringLiteral("filelink")}) {
            QCOMPARE(statAt(dir, name), Kind::Link);
            QCOMPARE(openDirAt(dir, name, Create::Yes).kind(), Kind::Link);
            QCOMPARE(openFileAt(dir, name).kind(), Kind::Link);
            QCOMPARE(unlinkAt(dir, name).code, EINVAL);
        }
        QCOMPARE(openDirAt(dir, QStringLiteral("a.txt"), Create::No).kind(), Kind::File);
        // Swapped for a link after the check: the open refuses it.
        inject(Step::OpenFile, 0, [this] { QFile::remove(path("a.txt")), QFile::link(path("sub"), path("a.txt")); });
        const File file = openFileAt(dir, QStringLiteral("a.txt"));
        QVERIFY(!file.isOpen() && file.kind() == Kind::Link);
        QVERIFY(QFileInfo(path("dirlink")).isSymLink() && QFileInfo(path("filelink")).isSymLink());
    }

    void fifoIsRejectedWithoutBlocking()
    {
        QCOMPARE(mkfifo(QFile::encodeName(path("fifo")).constData(), 0600), 0);
        Dir dir = Dir::openRoot(root, Create::No);
        QElapsedTimer timer;
        timer.start();
        QCOMPARE(openFileAt(dir, QStringLiteral("fifo")).kind(), Kind::Special);
        // Swapped for a FIFO after the check: the open does not wait for a writer.
        inject(Step::OpenFile, 0, [this] { ::rename(QFile::encodeName(path("fifo")), QFile::encodeName(path("a.txt"))); });
        const File file = openFileAt(dir, QStringLiteral("a.txt"));
        QVERIFY(!file.isOpen() && file.kind() == Kind::Special);
        QVERIFY(timer.elapsed() < 5000);
    }

    void listAtReportsKindsAndFailures()
    {
        Dir dir = Dir::openRoot(root, Create::No);
        const auto entries = listAt(dir);
        QStringList found;
        for (const Entry &e : entries.value())
            found << e.name + QLatin1Char('=') + QString::number(int(e.kind));
        found.sort();
        QCOMPARE(found, QStringList({"a.txt=2", "dirlink=3", "filelink=3", "sub=1"}));
        QCOMPARE(listAt(openDirAt(dir, QStringLiteral("sub"), Create::No))->size(), size_t(1));
        for (Step step : {Step::List, Step::ListEntry}) {
            inject(step, EIO);
            Status status;
            QVERIFY(!listAt(dir, &status));
            QCOMPARE(status.code, EIO);
            inject(step, 0);
        }
    }

    void publishNeverReplaces()
    {
        Dir dir = Dir::openRoot(root, Create::No);
        QCOMPARE(publish(dir, QStringLiteral("a.txt"), "new").code, EEXIST);
        QCOMPARE(readFile(path("a.txt")), QByteArray("alpha"));
        QVERIFY(publish(dir, QStringLiteral("b.txt"), "beta").ok());
        QCOMPARE(readFile(path("b.txt")), QByteArray("beta"));
        QCOMPARE(files(), QStringList({"a.txt", "b.txt", "filelink"}));
        QCOMPARE(createExclusiveAt(dir, QStringLiteral("b.txt"), 0600).status().code, EEXIST);
    }

    void renameFallbacks_data()
    {
        QTest::addColumn<int>("step");
        QTest::addColumn<int>("code");
        for (int code : {EPERM, ENOSYS, EINVAL, EOPNOTSUPP})
            QTest::addRow("rename %d", code) << int(Step::RenameNoReplace) << code;
        for (int code : {EPERM, EMLINK})
            QTest::addRow("link %d", code) << int(Step::Link) << code;
    }
    void renameFallbacks()
    {
        QFETCH(int, step);
        QFETCH(int, code);
        Dir dir = Dir::openRoot(root, Create::No);
        if (step == int(Step::Link)) // reached only when the no-replace rename fails too
            inject(Step::RenameNoReplace, ENOSYS);
        inject(Step(step), code);
        QCOMPARE(publish(dir, QStringLiteral("a.txt"), "new").code, EEXIST);
        QCOMPARE(readFile(path("a.txt")), QByteArray("alpha"));
        QVERIFY(publish(dir, QStringLiteral("b.txt"), "beta").ok()); // the cached step
        QCOMPARE(readFile(path("b.txt")), QByteArray("beta"));
        QCOMPARE(files(), QStringList({"a.txt", "b.txt", "filelink"}));
        inject(Step(step), EIO); // any other error is returned as it is
        QCOMPARE(publish(dir, QStringLiteral("c.txt"), "c").code, EIO);
        QCOMPARE(files(), QStringList({"a.txt", "b.txt", "filelink"}));
    }

    void tempModeFollowsUmask()
    {
        Dir dir = Dir::openRoot(root, Create::No);
        const mode_t old = umask(077);
        const File a = createExclusiveAt(dir, QStringLiteral("a.tmp"), 0777);
        umask(022);
        const File b = createExclusiveAt(dir, QStringLiteral("b.tmp"), 04755);
        umask(old);
        QCOMPARE(a.mode(), 0600);
        QCOMPARE(b.mode(), 0644);
    }

    void unlinkAndDiscard()
    {
        Dir dir = Dir::openRoot(root, Create::No);
        QCOMPARE(unlinkAt(dir, QStringLiteral("sub")).code, EINVAL);
        QCOMPARE(unlinkAt(dir, QStringLiteral("none")).code, ENOENT);
        QVERIFY(unlinkAt(dir, QStringLiteral("a.txt")).ok() && !QFileInfo::exists(path("a.txt")));
        File temp = createExclusiveAt(dir, QStringLiteral("t"), 0600);
        QVERIFY(discard(dir, temp, QStringLiteral("t")).ok() && !temp.isOpen() && !QFileInfo::exists(path("t")));
        // Not our file any more: left alone.
        temp = createExclusiveAt(dir, QStringLiteral("t"), 0600);
        QVERIFY(QFile::remove(path("t")) && writeFile(path("t"), "other"));
        QCOMPARE(discard(dir, temp, QStringLiteral("t")).code, ENOENT);
        QCOMPARE(readFile(path("t")), QByteArray("other"));
    }

    void timesAndSync()
    {
        Dir dir = Dir::openRoot(root, Create::No);
        File file = createExclusiveAt(dir, QStringLiteral("t"), 0600);
        QVERIFY(file.writeAll("abc", 3).ok() && file.setMTime(-1500000000));
        QCOMPARE(file.mtimeNs(), -1500000000);
        QVERIFY(file.setMTime(1234567890123456789) && syncAt(file).ok() && syncDir(dir).ok());
        QCOMPARE(openFileAt(dir, QStringLiteral("t")).mtimeNs(), 1234567890123456789);
    }

    void otherFileSystemIsReported()
    {
        struct stat top, dev;
        if (stat("/", &top) != 0 || stat("/dev", &dev) != 0 || top.st_dev == dev.st_dev)
            QSKIP("/dev is not a separate file system here");
        const Dir other = openDirAt(Dir::openRoot(QStringLiteral("/"), Create::No), QStringLiteral("dev"), Create::No);
        QVERIFY(!other.isOpen() && other.status().code == EXDEV && other.kind() == Kind::Special);
    }

    void namesAreSingleComponents()
    {
        Dir dir = Dir::openRoot(root, Create::No);
        for (const QString &name : {QString(), QStringLiteral("."), QStringLiteral(".."), QStringLiteral("sub/deep"),
                                    QStringLiteral("a") + QChar(0)}) {
            QCOMPARE(openDirAt(dir, name, Create::Yes).status().code, EINVAL);
            QCOMPARE(openFileAt(dir, name).status().code, EINVAL);
            QCOMPARE(createExclusiveAt(dir, name, 0600).status().code, EINVAL);
        }
    }
};

QTEST_GUILESS_MAIN(HandleFsTest)
#include "HandleFsTest.moc"
