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

}

class HandleFsTest : public QObject
{
    Q_OBJECT
    QTemporaryDir tmp;
    QString root;
    int runs = 0;

    QString path(const char *name) const { return root + QLatin1Char('/') + QLatin1String(name); }

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
        }
    }
};

QTEST_GUILESS_MAIN(HandleFsTest)
#include "HandleFsTest.moc"
