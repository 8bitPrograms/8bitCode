// SPDX-License-Identifier: GPL-3.0-or-later
// POSIX backend (Linux, macOS); 64-bit offsets and times on 32-bit glibc targets too.
#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64
#endif
#define _TIME_BITS 64

#include "HandleFs.h"

#include <QFile>
#include <QtDebug>

#include <atomic>
#include <cerrno>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/syscall.h>
#include <sys/vfs.h>
#endif

namespace HandleFs {
namespace {

#ifdef BIT8PAD_TESTING
std::atomic<int (*)(Step, const QString &)> testHook{nullptr};
#define HOOK(step, name) (testHook.load() ? testHook.load()(Step::step, name) : 0)
#else
#define HOOK(step, name) 0
#endif

Kind kindOf(mode_t mode)
{
    return S_ISDIR(mode) ? Kind::Dir : S_ISREG(mode) ? Kind::File : S_ISLNK(mode) ? Kind::Link : Kind::Special;
}

// A single component as bytes, or empty if the name is not one.
QByteArray component(const QString &name)
{
    const QByteArray n = QFile::encodeName(name);
    return n.isEmpty() || n == "." || n == ".." || n.contains('/') || n.contains('\0') ? QByteArray() : n;
}

} // namespace

void Handle::close()
{
    if (fd >= 0)
        ::close(std::exchange(fd, -1));
}

Id Handle::id() const
{
    struct stat s;
    if (fd < 0 || fstat(fd, &s) != 0)
        return {};
    Id id{quint64(s.st_dev), 0, quint64(s.st_ino), s.st_ino != 0};
#ifdef __linux__
    struct statfs f;
    if (fstatfs(fd, &f) != 0 || f.f_type == 0x65735546 /* FUSE */ || f.f_type == 0x794c7630 /* overlayfs */)
        id.valid = false;
#endif
    return id;
}

Dir Dir::openRoot(const QString &path, Create create)
{
    const QByteArray p = QFile::encodeName(path);
    if (p.isEmpty() || p.contains('\0'))
        return Dir(-1, {EINVAL});
    for (qsizetype i = 1; create == Create::Yes && i <= p.size(); ++i) {
        if ((i == p.size() || p[i] == '/') && mkdir(p.left(i).constData(), 0777) != 0 && errno != EEXIST)
            return Dir(-1, {errno});
    }
    const int fd = open(p.constData(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    return fd < 0 ? Dir(-1, {errno}) : Dir(fd);
}

Kind statAt(const Dir &dir, const QString &name)
{
    const QByteArray n = component(name);
    struct stat s;
    return !n.isEmpty() && fstatat(dir.native(), n.constData(), &s, AT_SYMLINK_NOFOLLOW) == 0 ? kindOf(s.st_mode)
                                                                                             : Kind::Missing;
}

Dir openDirAt(const Dir &dir, const QString &name, Create create)
{
    const QByteArray n = component(name);
    if (n.isEmpty())
        return Dir(-1, {EINVAL});
    if (create == Create::Yes && mkdirat(dir.native(), n.constData(), 0777) != 0 && errno != EEXIST)
        return Dir(-1, {errno});
    const int injected = HOOK(OpenDir, name);
    Dir sub(injected ? -1 : openat(dir.native(), n.constData(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
    if (!sub.isOpen()) {
        const int code = injected ? injected : errno;
        return Dir(-1, {code}, code == ELOOP ? Kind::Link : code == ENOTDIR ? statAt(dir, name) : Kind::Missing);
    }
    if (create == Create::No && sub.id().volume != dir.id().volume)
        return Dir(-1, {EXDEV}, Kind::Special);
    return sub;
}

File openFileAt(const Dir &dir, const QString &name)
{
    const QByteArray n = component(name);
    struct stat s;
    if (n.isEmpty() || fstatat(dir.native(), n.constData(), &s, AT_SYMLINK_NOFOLLOW) != 0)
        return File(-1, {n.isEmpty() ? EINVAL : errno});
    if (!S_ISREG(s.st_mode))
        return File(-1, {EINVAL}, kindOf(s.st_mode));
    // Checked again on the handle: the name may have changed since the check above.
    const int injected = HOOK(OpenFile, name);
    File file(injected ? -1 : openat(dir.native(), n.constData(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_NOCTTY | O_CLOEXEC));
    if (!file.isOpen()) {
        const int code = injected ? injected : errno;
        return File(-1, {code}, code == ELOOP ? Kind::Link : Kind::Missing);
    }
    if (fstat(file.native(), &s) != 0 || !S_ISREG(s.st_mode))
        return File(-1, {EINVAL}, kindOf(s.st_mode));
    return file;
}

std::optional<std::vector<Entry>> listAt(const Dir &dir, Status *status)
{
    // A fresh open of "." has its own read offset; a dup would share the one of dir.
    int code = HOOK(List, QString());
    const int fd = code ? -1 : openat(dir.native(), ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    DIR *d = fd < 0 ? nullptr : fdopendir(fd);
    if (!d) {
        code = code ? code : errno;
        if (fd >= 0)
            ::close(fd);
    }
    std::vector<Entry> entries;
    while (d && !code) {
        errno = 0;
        const dirent *e = readdir(d);
        if (!e || (code = HOOK(ListEntry, QFile::decodeName(e->d_name)))) {
            code = code ? code : errno;
            break;
        }
        const QByteArray n(e->d_name);
        if (n == "." || n == "..")
            continue;
        const QString name = QFile::decodeName(n);
        const unsigned char t = e->d_type;
        entries.push_back({name, t == DT_UNKNOWN ? statAt(dir, name) : t == DT_DIR ? Kind::Dir : t == DT_REG ? Kind::File
                                     : t == DT_LNK ? Kind::Link : Kind::Special});
    }
    if (d)
        closedir(d);
    if (status)
        status->code = code;
    return code ? std::nullopt : std::optional(std::move(entries));
}

qint64 File::size() const
{
    struct stat s;
    return fstat(native(), &s) == 0 ? qint64(s.st_size) : -1;
}

qint64 File::mtimeNs() const
{
    struct stat s;
    if (fstat(native(), &s) != 0)
        return -1;
#ifdef __APPLE__
    return qint64(s.st_mtimespec.tv_sec) * 1000000000 + s.st_mtimespec.tv_nsec;
#else
    return qint64(s.st_mtim.tv_sec) * 1000000000 + s.st_mtim.tv_nsec;
#endif
}

int File::mode() const
{
    struct stat s;
    return fstat(native(), &s) == 0 ? int(s.st_mode & 07777) : -1;
}

qint64 File::read(char *data, qint64 max)
{
    ssize_t n;
    do
        n = ::read(native(), data, size_t(max));
    while (n < 0 && errno == EINTR);
    return n;
}

#ifdef BIT8PAD_TESTING
void setTestHook(int (*hook)(Step step, const QString &name))
{
    qInfo("BIT8PAD_TEST_HOOK %s", hook ? "set" : "cleared");
    testHook = hook;
}
#endif

} // namespace HandleFs
