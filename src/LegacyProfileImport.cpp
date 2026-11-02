// SPDX-License-Identifier: GPL-3.0-or-later
#include "LegacyProfileImport.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <optional>
#include <set>
#include <string>
#include <utility>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <dirent.h>
#include <sys/stat.h>
#endif

namespace LegacyProfileImport {
namespace {
#ifdef Q_OS_WIN
// The \\?\ form lifts the 260-character limit of the plain Windows calls.
std::wstring longPath(const QString &path)
{
    QString native = QDir::toNativeSeparators(QDir::cleanPath(QFileInfo(path).absoluteFilePath()));
    if (native.startsWith(QStringLiteral("\\\\?\\"))) {}
    else if (native.startsWith(QStringLiteral("\\\\"))) native = QStringLiteral("\\\\?\\UNC\\") + native.mid(2);
    else native = QStringLiteral("\\\\?\\") + native;
    return native.toStdWString();
}
#endif

enum class Kind { Missing, Link, Other, Unknown };

// What is at path, without following it. Symlinks and Windows junctions lead
// out of the folder they sit in; the import never follows one. Unknown means
// the check itself failed, which the caller must treat as an error.
Kind kindOf(const QString &path)
{
#ifdef Q_OS_WIN
    const std::wstring native = longPath(path);
    const DWORD attributes = GetFileAttributesW(native.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        const DWORD error = GetLastError();
        return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND ? Kind::Missing : Kind::Unknown;
    }
    if (!(attributes & FILE_ATTRIBUTE_REPARSE_POINT)) return Kind::Other;
    WIN32_FIND_DATAW data;
    const HANDLE find = FindFirstFileW(native.c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) return Kind::Unknown;
    FindClose(find);
    // Only reparse points that name another place are links. Cloud placeholders,
    // deduplicated or compressed files are reparse points too, but ordinary data.
    const DWORD tag = data.dwReserved0;
    return tag == IO_REPARSE_TAG_SYMLINK || tag == IO_REPARSE_TAG_MOUNT_POINT || IsReparseTagNameSurrogate(tag)
        ? Kind::Link : Kind::Other;
#else
    struct stat st{};
    if (::lstat(QFile::encodeName(path).constData(), &st) != 0)
        return errno == ENOENT || errno == ENOTDIR ? Kind::Missing : Kind::Unknown;
    return S_ISLNK(st.st_mode) ? Kind::Link : Kind::Other;
#endif
}

// QDir lists an unreadable folder as empty; ask the system so that is an error.
bool canList(const QString &path)
{
#ifdef Q_OS_WIN
    WIN32_FIND_DATAW data;
    const HANDLE find = FindFirstFileW(longPath(path + QStringLiteral("/*")).c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) return GetLastError() == ERROR_FILE_NOT_FOUND;
    FindClose(find);
    return true;
#else
    DIR *dir = ::opendir(QFile::encodeName(path).constData());
    if (!dir) return false;
    ::closedir(dir);
    return true;
#endif
}

// Where an existing path really is. Qt's canonical path does not resolve
// Windows junctions, so ask the file system there.
QString canonical(const QString &path)
{
#ifdef Q_OS_WIN
    const HANDLE handle = CreateFileW(longPath(path).c_str(), 0,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return {};
    std::wstring buffer(32768, L'\0');
    const DWORD length = GetFinalPathNameByHandleW(handle, buffer.data(), DWORD(buffer.size()), FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    CloseHandle(handle);
    if (length == 0 || length >= buffer.size()) return {};
    QString result = QString::fromWCharArray(buffer.data(), int(length));
    if (result.startsWith(QStringLiteral("\\\\?\\UNC\\"))) result = QStringLiteral("\\\\") + result.mid(8);
    else if (result.startsWith(QStringLiteral("\\\\?\\"))) result = result.mid(4);
    return QDir::cleanPath(QDir::fromNativeSeparators(result));
#else
    return QFileInfo(path).canonicalFilePath();
#endif
}

// The path with all links resolved. For a path that does not exist yet, its
// nearest existing ancestor is resolved and the rest appended. Empty if that
// ancestor cannot be resolved (for example a link to nowhere).
QString resolved(const QString &path)
{
    QString existing = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    QStringList rest;
    for (Kind kind = kindOf(existing); kind == Kind::Missing; kind = kindOf(existing)) {
        const QFileInfo info(existing);
        const QString parent = info.absolutePath();
        if (parent == existing) return {};
        rest.prepend(info.fileName());
        existing = parent;
    }
    const QString real = canonical(existing);
    if (real.isEmpty() || rest.isEmpty()) return real;
    return QDir::cleanPath(real + '/' + rest.join('/'));
}

// Both paths resolved. Equal counts as inside.
bool inside(const QString &root, const QString &path, Qt::CaseSensitivity cs)
{
    return path.compare(root, cs) == 0 || path.startsWith(root.endsWith('/') ? root : root + '/', cs);
}

#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
constexpr Qt::CaseSensitivity fileNameCase = Qt::CaseInsensitive;
#else
constexpr Qt::CaseSensitivity fileNameCase = Qt::CaseSensitive;
#endif

// False if any part of root/relative below root is a link (even one to
// nowhere) or cannot be checked.
bool noLinkBelow(const QString &root, const QString &relative)
{
    QString path = root;
    for (const QString &part : relative.split('/')) {
        path += '/' + part;
        switch (kindOf(path)) {
        case Kind::Link: case Kind::Unknown: return false;
        case Kind::Missing: return true;
        case Kind::Other: break;
        }
    }
    return true;
}

#ifndef Q_OS_WIN
// Identity of a directory, to notice the same folder reached a second way
// (a bind mount, for example), which no path comparison can see.
std::optional<std::pair<dev_t, ino_t>> identity(const QString &path)
{
    struct stat st{};
    if (::stat(QFile::encodeName(path).constData(), &st) != 0) return std::nullopt;
    return std::pair{st.st_dev, st.st_ino};
}
#endif

// Writes a small note into the new folder, never through a link.
bool writeNote(const QString &path, const QByteArray &text, QString &error)
{
    if (kindOf(path) == Kind::Link || kindOf(path) == Kind::Unknown) {
        error = QStringLiteral("leads outside the 8bitCode folder; not written");
        return false;
    }
    QSaveFile note(path);
    if (!note.open(QIODevice::WriteOnly) || note.write(text) != text.size() || !note.commit()) {
        error = note.errorString();
        return false;
    }
    return true;
}

bool copyFile(const QFileInfo &source, const QString &target, const QString &root, QString &error)
{
    QFile in(source.absoluteFilePath());
    if (!in.open(QIODevice::ReadOnly)) { error = in.errorString(); return false; }
    if (!QDir().mkpath(QFileInfo(target).absolutePath())) { error = QStringLiteral("cannot create folder"); return false; }
    // Defense in depth: the caller already checked every part of the path, so
    // this only fails if something changed it meanwhile; no test reaches it.
    if (!inside(root, canonical(QFileInfo(target).absolutePath()), fileNameCase)) {
        error = QStringLiteral("leads outside the 8bitCode folder; not copied");
        return false;
    }
    // QSaveFile follows a link at the target, so check the name itself last,
    // right before it is opened.
    if (kindOf(target) != Kind::Missing) { error = QStringLiteral("appeared during the import; not copied"); return false; }
    // QSaveFile commits only a complete copy, so a full disk never leaves a truncated file.
    QSaveFile out(target);
    if (!out.open(QIODevice::WriteOnly)) { error = out.errorString(); return false; }
    QByteArray chunk;
    while (!(chunk = in.read(1 << 20)).isEmpty()) {
        if (out.write(chunk) != chunk.size()) { error = out.errorString(); out.cancelWriting(); return false; }
    }
    if (in.error() != QFileDevice::NoError) { error = in.errorString(); out.cancelWriting(); return false; }
    // Recovery snapshots are compared by time; keep the original one. It is set
    // before the commit, so a copy in place always has the right time.
    if (!out.flush() || !out.setFileTime(source.lastModified(), QFileDevice::FileModificationTime)) {
        error = out.errorString().isEmpty() ? QStringLiteral("cannot set the file time") : out.errorString();
        out.cancelWriting();
        return false;
    }
    if (!out.commit()) { error = out.errorString(); return false; }
    return true;
}
}

QString legacyDirectoryFor(const QString &currentDirectory)
{
    const QString suffix = QStringLiteral("/8bitCode/8bitCode");
    const QString current = QDir::fromNativeSeparators(QDir::cleanPath(currentDirectory));
    if (!current.endsWith(suffix)) return {};
    return current.chopped(suffix.size()) + QStringLiteral("/8bitPad/8bitPad");
}

Result run(const QString &legacyDirectory, const QString &currentDirectory, const QDateTime &now)
{
    Result result;
    const QDir current(currentDirectory);
    if (legacyDirectory.isEmpty() || QFileInfo::exists(current.filePath(MarkerName))) return result;
    if (!QFileInfo(legacyDirectory).isDir()) return result;
    // After a failed run, wait a day: an error that stays would otherwise
    // repeat the whole import on every start.
    {
        QFile failed(current.filePath(FailedName));
        if (failed.open(QIODevice::ReadOnly)) {
            const QDateTime at = QDateTime::fromString(QString::fromLatin1(failed.read(64)).trimmed(), Qt::ISODate);
            if (at.isValid() && at <= now && now < at.addDays(1)) { result.postponed = true; return result; }
        }
    }
    // Compare where both folders really are. One inside the other would copy
    // into the old folder, or walk into its own output.
    const QString legacyRoot = resolved(legacyDirectory);
    const QString currentRoot = resolved(currentDirectory);
    if (legacyRoot.isEmpty() || currentRoot.isEmpty()) {
        result.errors << QStringLiteral("cannot resolve the 8bitPad or 8bitCode folder; nothing copied");
        return result;
    }
    // The same folder reached through a link: it is already the profile.
    if (legacyRoot.compare(currentRoot, Qt::CaseInsensitive) == 0) return result;
    // Case-insensitive on every system, so a doubtful case is refused.
    if (inside(legacyRoot, currentRoot, Qt::CaseInsensitive) || inside(currentRoot, legacyRoot, Qt::CaseInsensitive)) {
        result.errors << QStringLiteral("the 8bitPad and 8bitCode folders are inside each other; nothing copied");
        return result;
    }
    result.ran = true;
    QDir().mkpath(currentRoot);

#ifndef Q_OS_WIN
    std::set<std::pair<dev_t, ino_t>> visited;
    for (const QString &root : {legacyRoot, currentRoot}) {
        const auto id = identity(root);
        if (id) visited.insert(*id);
        else result.errors << root + QStringLiteral(": cannot read the folder");
    }
#endif
    // Only the old root itself may be a link; below it the walk sees links as entries and skips them.
    QList<QString> folders;
    if (result.errors.isEmpty()) folders << QString();
    while (!folders.isEmpty()) {
        const QString folder = folders.takeLast();
        const QString path = folder.isEmpty() ? legacyRoot : legacyRoot + '/' + folder;
        if (!canList(path)) {
            result.errors << (folder.isEmpty() ? QStringLiteral(".") : folder) + QStringLiteral(": cannot read the folder");
            continue;
        }
        const QFileInfoList entries = QDir(path).entryInfoList(QDir::Files | QDir::Dirs | QDir::Hidden | QDir::System
            | QDir::NoDotAndDotDot, QDir::Name);
        for (const QFileInfo &source : entries) {
            const QString relative = folder.isEmpty() ? source.fileName() : folder + '/' + source.fileName();
            const Kind kind = kindOf(source.absoluteFilePath());
            if (kind == Kind::Unknown) { result.errors << relative + QStringLiteral(": cannot check it"); continue; }
            if (kind == Kind::Missing) continue; // removed since the listing
            if (kind == Kind::Link) { result.links << relative; continue; }
            if (source.isDir()) {
#ifndef Q_OS_WIN
                const auto id = identity(source.absoluteFilePath());
                if (!id) { result.errors << relative + QStringLiteral(": cannot read the folder"); continue; }
                if (!visited.insert(*id).second) { result.links << relative; continue; }
#endif
                folders << relative;
                continue;
            }
            // FIFOs or sockets would hang forever in QFile::open(QIODevice::ReadOnly).
            if (!source.isFile()) continue;
            if (relative.endsWith(QStringLiteral(".lock"))) continue; // stale QSettings/QLockFile locks
            const QString target = currentRoot + '/' + relative;
            // A link already in the new folder could send the copy anywhere,
            // including into the old folder.
            if (!noLinkBelow(currentRoot, relative) || !inside(currentRoot, resolved(target), fileNameCase)) {
                result.errors << relative + QStringLiteral(": leads outside the 8bitCode folder; not copied");
                continue;
            }
            // Anything already there belongs to 8bitCode (or to a finished copy,
            // which has the right time already): never touch it.
            if (kindOf(target) != Kind::Missing) { result.skipped << relative; continue; }
            QString error;
            if (copyFile(source, target, currentRoot, error)) ++result.copied;
            else result.errors << relative + QStringLiteral(": ") + error;
        }
    }
    QString error;
    if (result.errors.isEmpty()) {
        if (writeNote(currentRoot + '/' + QLatin1String(MarkerName),
                "Copied from the 8bitPad profile; the original folder was left unchanged.\n", error))
            QFile::remove(currentRoot + '/' + QLatin1String(FailedName));
        else
            result.errors << QLatin1String(MarkerName) + QStringLiteral(": ") + error;
    }
    if (!result.errors.isEmpty()
        && !writeNote(currentRoot + '/' + QLatin1String(FailedName), now.toUTC().toString(Qt::ISODate).toLatin1() + '\n', error))
        result.errors << QLatin1String(FailedName) + QStringLiteral(": ") + error;
    return result;
}

}
