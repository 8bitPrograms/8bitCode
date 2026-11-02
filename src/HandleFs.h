// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Files and folders reached relative to an open folder handle. Only Dir::openRoot resolves a full
// path; every other call takes a single name and never follows a link. Errors are platform codes
// (errno on Linux and macOS).

#include <QString>

#include <optional>
#include <utility>
#include <vector>

namespace HandleFs {

struct Status {
    int code = 0;
    bool ok() const { return code == 0; }
};

// valid is false where the file system has no stable file numbers; volume is always set.
struct Id {
    quint64 volume = 0, hi = 0, lo = 0;
    bool valid = false;
    bool operator==(const Id &) const = default;
};

enum class Kind { Missing, Dir, File, Link, Special };
enum class Create { No, Yes }; // No never creates anything; used for every source-side call.

struct Entry {
    QString name;
    Kind kind;
};

class Handle {
public:
    explicit Handle(int fd = -1, Status status = {}, Kind kind = Kind::Missing) : fd(fd), st(status), found(kind) {}
    Handle(Handle &&o) noexcept : fd(std::exchange(o.fd, -1)), st(o.st), found(o.found) {}
    Handle &operator=(Handle o) noexcept
    {
        std::swap(fd, o.fd);
        st = o.st;
        found = o.found;
        return *this;
    }
    ~Handle() { close(); }
    void close();
    bool isOpen() const { return fd >= 0; }
    int native() const { return fd; }
    Status status() const { return st; }
    Kind kind() const { return found; } // what was found when the open failed, e.g. Link
    Id id() const;

private:
    int fd;
    Status st;
    Kind found;
};

class Dir : public Handle {
public:
    using Handle::Handle;
    static Dir openRoot(const QString &path, Create create); // Yes: makes missing folders first
};

class File : public Handle {
public:
    using Handle::Handle;
    qint64 size() const;
    qint64 mtimeNs() const;
    int mode() const;
    bool setMTime(qint64 ns);
    qint64 read(char *data, qint64 max); // 0 at the end, -1 on error
    Status writeAll(const char *data, qint64 size);
};

// Link or non-folder → kind(). With Create::No, a folder on another file system reports EXDEV.
Dir openDirAt(const Dir &dir, const QString &name, Create create);
File openFileAt(const Dir &dir, const QString &name); // regular files only; never blocks
Kind statAt(const Dir &dir, const QString &name);
File createExclusiveAt(const Dir &dir, const QString &name, int mode); // the kernel applies the umask to mode & 0666
Status renameNoReplaceAt(const Dir &dir, File &file, const QString &from, const QString &to); // EEXIST if to exists
Status unlinkAt(const Dir &dir, const QString &name); // regular files only
Status discard(const Dir &dir, File &file, const QString &name); // removes name only if it is still file
Status syncAt(File &file);
Status syncDir(Dir &dir);
// nullopt if the folder cannot be listed completely; never a partial or empty list.
std::optional<std::vector<Entry>> listAt(const Dir &dir, Status *status = nullptr);

#ifdef BIT8PAD_TESTING
enum class Step { OpenDir, OpenFile, List, ListEntry, Create, RenameNoReplace, Link, Rename, Unlink };
// Runs before the step's system call; a non-zero return fails the step with that errno.
// Setting it also resets the cached rename step.
void setTestHook(int (*hook)(Step step, const QString &name));
#endif

} // namespace HandleFs
