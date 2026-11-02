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
#include <stdio.h>
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

// The publish step that worked last in this run: 0 no-replace rename, 1 link + unlink, 2 check + rename.
std::atomic<int> renameStep{0};

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

Status result(int r)
{
    return {r == 0 ? 0 : errno};
}

bool sameFile(int fd, int dirFd, const char *name)
{
    struct stat a, b;
    return fstat(fd, &a) == 0 && fstatat(dirFd, name, &b, AT_SYMLINK_NOFOLLOW) == 0 && a.st_dev == b.st_dev
        && a.st_ino == b.st_ino;
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

File createExclusiveAt(const Dir &dir, const QString &name, int mode)
{
    const QByteArray n = component(name);
    const int injected = n.isEmpty() ? EINVAL : HOOK(Create, name);
    const int fd = injected ? -1
        : openat(dir.native(), n.constData(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, mode_t(mode & 0666));
    return fd < 0 ? File(-1, {injected ? injected : errno}) : File(fd);
}

Status renameNoReplaceAt(const Dir &dir, File &file, const QString &from, const QString &to)
{
    const QByteArray f = component(from), t = component(to);
    const int d = dir.native();
    if (f.isEmpty() || t.isEmpty())
        return {EINVAL};
    if (renameStep == 0) {
        int injected = HOOK(RenameNoReplace, to);
#if defined(__linux__) && defined(SYS_renameat2)
        if (!injected && syscall(SYS_renameat2, d, f.constData(), d, t.constData(), 1 /* RENAME_NOREPLACE */) == 0)
            return {};
#elif defined(__APPLE__)
        if (!injected && renameatx_np(d, f.constData(), d, t.constData(), RENAME_EXCL) == 0)
            return {};
#else
        injected = injected ? injected : ENOSYS;
#endif
        const int code = injected ? injected : errno;
        if (code != EPERM && code != ENOSYS && code != EINVAL && code != EOPNOTSUPP && code != ENOTSUP)
            return {code};
        renameStep = 1;
    }
    if (renameStep == 1) {
        const int injected = HOOK(Link, to);
        const Status linked = injected ? Status{injected} : result(linkat(d, f.constData(), d, t.constData(), 0));
        if (linked.ok() && unlinkat(d, f.constData(), 0) != 0)
            qWarning("HandleFs: published %s, but its temp name is left", t.constData());
        const int code = linked.code;
        if (code != EPERM && code != EOPNOTSUPP && code != ENOTSUP && code != EMLINK)
            return linked;
        qWarning("HandleFs: no no-replace rename or hard links here; checking the name before renaming");
        renameStep = 2;
    }
    // A name created between this check and renameat is replaced (documented residual risk).
    struct stat s;
    if (!sameFile(file.native(), d, f.constData()))
        return {ENOENT};
    const int found = fstatat(d, t.constData(), &s, AT_SYMLINK_NOFOLLOW) == 0 ? EEXIST : errno;
    if (found != ENOENT)
        return {found};
    const int injected = HOOK(Rename, to);
    return injected ? Status{injected} : result(renameat(d, f.constData(), d, t.constData()));
}

Status unlinkAt(const Dir &dir, const QString &name)
{
    const Kind kind = statAt(dir, name);
    if (kind != Kind::File)
        return {kind == Kind::Missing ? ENOENT : EINVAL};
    const int injected = HOOK(Unlink, name);
    return injected ? Status{injected} : result(unlinkat(dir.native(), component(name).constData(), 0));
}

Status discard(const Dir &dir, File &file, const QString &name)
{
    const QByteArray n = component(name);
    const Status status = !n.isEmpty() && sameFile(file.native(), dir.native(), n.constData()) ? unlinkAt(dir, name)
                                                                                              : Status{ENOENT};
    file.close();
    return status;
}

Status syncAt(File &file)
{
#ifdef __APPLE__
    return result(fsync(file.native()));
#else
    return result(fdatasync(file.native()));
#endif
}

Status syncDir(Dir &dir)
{
    return result(fsync(dir.native()));
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

bool File::setMTime(qint64 ns)
{
    const qint64 sec = ns / 1000000000 - (ns % 1000000000 < 0 ? 1 : 0);
    const timespec times[2] = {{0, UTIME_OMIT}, {time_t(sec), long(ns - sec * 1000000000)}};
    return futimens(native(), times) == 0;
}

qint64 File::read(char *data, qint64 max)
{
    ssize_t n;
    do
        n = ::read(native(), data, size_t(max));
    while (n < 0 && errno == EINTR);
    return n;
}

Status File::writeAll(const char *data, qint64 size)
{
    while (size > 0) {
        const ssize_t n = ::write(native(), data, size_t(size));
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return {n < 0 ? errno : EIO};
        data += n;
        size -= n;
    }
    return {};
}

#ifdef BIT8PAD_TESTING
void setTestHook(int (*hook)(Step step, const QString &name))
{
    qInfo("BIT8PAD_TEST_HOOK %s", hook ? "set" : "cleared");
    testHook = hook;
    renameStep = 0;
}
#endif

} // namespace HandleFs
