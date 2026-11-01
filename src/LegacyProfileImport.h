// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>
#include <QStringList>

// One-time copy of the profile 8bitPad kept before the app became 8bitCode.
namespace LegacyProfileImport {

inline constexpr char MarkerName[] = ".imported-from-8bitPad";

struct Result {
    bool ran = false;
    int copied = 0;
    QStringList skipped; // already present in the new profile; left untouched
    QStringList errors;  // any error keeps the marker away, so the next start retries
};

// The 8bitPad folder next to an 8bitCode AppDataLocation, or empty if the layout differs.
QString legacyDirectoryFor(const QString &currentDirectory);

// Copies files from legacyDirectory that do not exist yet in currentDirectory, then
// writes the marker. Never changes legacyDirectory. With the marker present this is one stat.
Result run(const QString &legacyDirectory, const QString &currentDirectory);

}
