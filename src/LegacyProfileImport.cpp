// SPDX-License-Identifier: GPL-3.0-or-later
#include "LegacyProfileImport.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

namespace LegacyProfileImport {
namespace {
bool copyFile(const QFileInfo &source, const QString &target, QString &error)
{
    QFile in(source.absoluteFilePath());
    if (!in.open(QIODevice::ReadOnly)) { error = in.errorString(); return false; }
    if (!QDir().mkpath(QFileInfo(target).absolutePath())) { error = QStringLiteral("cannot create folder"); return false; }
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
    const QDir legacy(legacyDirectory);
    if (!legacy.exists() || legacy == current) return result;
    result.ran = true;
    QDirIterator it(legacy.absolutePath(), QDir::Files | QDir::Hidden | QDir::NoSymLinks,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QFileInfo source(it.next());
        // FIFOs or sockets would hang forever in QFile::open(QIODevice::ReadOnly).
        if (!source.isFile()) continue;
        const QString relative = legacy.relativeFilePath(source.absoluteFilePath());
        if (relative.endsWith(QStringLiteral(".lock"))) continue; // stale QSettings/QLockFile locks
        const QString target = current.filePath(relative);
        const QFileInfo targetInfo(target);
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
        if (copyFile(source, target, error)) ++result.copied;
        else result.errors << relative + QStringLiteral(": ") + error;
    }
    if (result.errors.isEmpty()) {
        QSaveFile marker(current.filePath(MarkerName));
        const QByteArray text = "Copied from the 8bitPad profile; the original folder was left unchanged.\n";
        if (!QDir().mkpath(current.absolutePath()) || !marker.open(QIODevice::WriteOnly) || marker.write(text) != text.size() || !marker.commit())
            result.errors << QStringLiteral("%1: %2").arg(QLatin1String(MarkerName), marker.errorString());
    }
    return result;
}

}
