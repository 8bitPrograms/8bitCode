// SPDX-License-Identifier: GPL-3.0-or-later
#include "LegacyProfileImport.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <iterator>
#include <set>
#include <utility>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/stat.h>
#endif

namespace LegacyProfileImport {
namespace {
// Symlinks, Windows junctions, mount points and every other reparse point
// lead out of the folder they sit in; the import never follows one.
bool isLink(const QFileInfo &info)
{
    if (info.isSymLink()) return true;
#ifdef Q_OS_WIN
    const DWORD attributes = GetFileAttributesW(reinterpret_cast<const wchar_t *>(
        QDir::toNativeSeparators(info.absoluteFilePath()).utf16()));
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT);
#else
    return false;
#endif
}

// Where an existing path really is. Qt's canonical path does not resolve
// Windows junctions, so ask the file system there.
QString canonical(const QString &path)
{
#ifdef Q_OS_WIN
    const HANDLE handle = CreateFileW(reinterpret_cast<const wchar_t *>(QDir::toNativeSeparators(path).utf16()), 0,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return {};
    wchar_t buffer[32768];
    const DWORD length = GetFinalPathNameByHandleW(handle, buffer, DWORD(std::size(buffer)), FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    CloseHandle(handle);
    if (length == 0 || length >= std::size(buffer)) return {};
    QString result = QString::fromWCharArray(buffer, int(length));
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
    for (QFileInfo info(existing); !info.exists() && !info.isSymLink(); info.setFile(existing)) {
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

// True if any part of root/relative below root is a link, even one to nowhere.
bool throughLink(const QString &root, const QString &relative)
{
    QString path = root;
    for (const QString &part : relative.split('/')) {
        path += '/' + part;
        const QFileInfo info(path);
        if (isLink(info)) return true;
        if (!info.exists()) return false;
    }
    return false;
}

#ifndef Q_OS_WIN
// Identity of a directory, to notice the same folder reached a second way
// (a bind mount, for example), which no path comparison can see.
std::pair<dev_t, ino_t> identity(const QString &path)
{
    struct stat st{};
    if (::stat(QFile::encodeName(path).constData(), &st) != 0) return {0, 0};
    return {st.st_dev, st.st_ino};
}
#endif

bool copyFile(const QFileInfo &source, const QString &target, const QString &root, QString &error)
{
    QFile in(source.absoluteFilePath());
    if (!in.open(QIODevice::ReadOnly)) { error = in.errorString(); return false; }
    if (!QDir().mkpath(QFileInfo(target).absolutePath())) { error = QStringLiteral("cannot create folder"); return false; }
    // Checked again now that the folder exists: it must still be a real folder in the new profile.
    if (!inside(root, canonical(QFileInfo(target).absolutePath()), fileNameCase)) {
        error = QStringLiteral("leads outside the 8bitCode folder; not copied");
        return false;
    }
    // QSaveFile commits only a complete copy, so a full disk never leaves a truncated file.
    QSaveFile out(target);
    if (!out.open(QIODevice::WriteOnly)) { error = out.errorString(); return false; }
    QByteArray chunk;
    while (!(chunk = in.read(1 << 20)).isEmpty()) {
        if (out.write(chunk) != chunk.size()) { error = out.errorString(); out.cancelWriting(); return false; }
    }
    if (in.error() != QFileDevice::NoError) { error = in.errorString(); out.cancelWriting(); return false; }
    if (!out.commit()) { error = out.errorString(); return false; }
    // Recovery snapshots are compared by time; keep the original one.
    // A wrong time is a failed import, because the next start could not
    // recognize an unfinished copy without it.
    QFile copied(target);
    if (!copied.open(QIODevice::ReadWrite)) { error = copied.errorString(); return false; }
    if (!copied.setFileTime(source.lastModified(), QFileDevice::FileModificationTime)) {
        error = copied.errorString().isEmpty() ? QStringLiteral("cannot set the file time") : copied.errorString();
        return false;
    }
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

Result run(const QString &legacyDirectory, const QString &currentDirectory)
{
    Result result;
    const QDir current(currentDirectory);
    if (legacyDirectory.isEmpty() || QFileInfo::exists(current.filePath(MarkerName))) return result;
    if (!QFileInfo(legacyDirectory).isDir()) return result;
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

#ifndef Q_OS_WIN
    std::set<std::pair<dev_t, ino_t>> visited{identity(legacyRoot)};
    if (QDir().mkpath(currentRoot)) visited.insert(identity(currentRoot));
#endif
    // Only the old root itself may be a link; below it the walk sees links as entries and skips them.
    QList<QString> folders{QString()};
    while (!folders.isEmpty()) {
        const QString folder = folders.takeLast();
        const QFileInfoList entries = QDir(folder.isEmpty() ? legacyRoot : legacyRoot + '/' + folder)
            .entryInfoList(QDir::Files | QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot, QDir::Name);
        for (const QFileInfo &source : entries) {
            const QString relative = folder.isEmpty() ? source.fileName() : folder + '/' + source.fileName();
            if (isLink(source)) { result.links << relative; continue; }
            if (source.isDir()) {
#ifndef Q_OS_WIN
                if (!visited.insert(identity(source.absoluteFilePath())).second) { result.links << relative; continue; }
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
            const QFileInfo targetInfo(target);
            if (throughLink(currentRoot, relative) || !inside(currentRoot, resolved(target), fileNameCase)) {
                result.errors << relative + QStringLiteral(": leads outside the 8bitCode folder; not copied");
                continue;
            }
            if (targetInfo.exists()) {
                // Same size but a different time: an earlier unfinished import had
                // already written the contents, then died before the marker. Finish
                // it by fixing only the time, never the data.
                if (targetInfo.size() != source.size() || targetInfo.lastModified() == source.lastModified()) {
                    result.skipped << relative;
                    continue;
                }
                QFile copied(target);
                if (!copied.open(QIODevice::ReadWrite)
                    || !copied.setFileTime(source.lastModified(), QFileDevice::FileModificationTime))
                    result.errors << relative + QStringLiteral(": cannot restore the original file time");
                continue;
            }
            QString error;
            if (copyFile(source, target, currentRoot, error)) ++result.copied;
            else result.errors << relative + QStringLiteral(": ") + error;
        }
    }
    if (result.errors.isEmpty()) {
        // QSaveFile writes through a link, so the marker gets the same check as every copy.
        const QString markerPath = currentRoot + '/' + QLatin1String(MarkerName);
        QSaveFile marker(markerPath);
        const QByteArray text = "Copied from the 8bitPad profile; the original folder was left unchanged.\n";
        if (isLink(QFileInfo(markerPath)))
            result.errors << QLatin1String(MarkerName) + QStringLiteral(": leads outside the 8bitCode folder; not written");
        else if (!QDir().mkpath(currentRoot) || !marker.open(QIODevice::WriteOnly) || marker.write(text) != text.size() || !marker.commit())
            result.errors << QStringLiteral("%1: %2").arg(QLatin1String(MarkerName), marker.errorString());
    }
    return result;
}

}
