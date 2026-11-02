// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDateTime>
#include <QString>
#include <QStringList>

// One-time copy of the profile 8bitPad kept before the app became 8bitCode.
namespace LegacyProfileImport {

inline constexpr char MarkerName[] = ".imported-from-8bitPad";
// Time of the last failed run; the next try waits a day after it.
inline constexpr char FailedName[] = ".import-from-8bitPad-failed";

struct Result {
    bool ran = false;
    bool postponed = false; // an earlier run failed less than a day ago
    int copied = 0;
    QStringList skipped; // already present in the new profile; left untouched
    QStringList links;   // symlinks, junctions and folders seen twice; never followed or copied
    QStringList errors;  // any error keeps the marker away, so a later start retries
};

// The 8bitPad folder next to an 8bitCode AppDataLocation, or empty if the layout differs.
QString legacyDirectoryFor(const QString &currentDirectory);

// Copies files from legacyDirectory that do not exist yet in currentDirectory, then
// writes the marker. Never changes legacyDirectory and writes only inside
// currentDirectory: folders inside each other are refused, and links are never
// followed. With the marker present this is one stat.
//
// Links inside the old folder (symlinks, junctions, a folder reached twice) are
// skipped on purpose and do not count as errors: following one could read or
// write anywhere. Their names are in Result::links, so the caller can log them;
// what they point to is not copied. Files already in the new folder are never
// changed. Folders or entries that cannot be read or checked are errors.
//
// After a failed run, a note with its time keeps further runs away for a day.
Result run(const QString &legacyDirectory, const QString &currentDirectory,
    const QDateTime &now = QDateTime::currentDateTimeUtc());

}
