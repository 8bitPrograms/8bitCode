/*
 * This file is part of Notepad Next.
 * Copyright 2019 Justin Dailey
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Notepad Next is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Notepad Next.  If not, see <https://www.gnu.org/licenses/>.
 */


#include "QRegexSearch.h"

#include <QtGlobal>
#include <QRegularExpression>

using namespace Scintilla;

#ifdef SCI_OWNREGEX
RegexSearchBase *Scintilla::Internal::CreateRegexSearch(CharClassify *charClassTable)
{
    Q_UNUSED(charClassTable);

    qInfo(Q_FUNC_INFO);

    return new QRegexSearch();
}
#endif

QRegexSearch::QRegexSearch()
{

}

Sci::Position QRegexSearch::FindText(Document *doc, Sci::Position minPos, Sci::Position maxPos, const char *s, bool caseSensitive, bool word, bool wordStart, Scintilla::FindOption flags, Sci::Position *length)
{
    Q_UNUSED(caseSensitive);
    Q_UNUSED(word)
    Q_UNUSED(wordStart)
    // -----------------------------------------------------------------------------------------------------------------------
    // NOTE: This section of code has to be very careful about what units of measure is being used. Scintilla wants to operate
    // in units of bytes (e.g. position 3 is 3 bytes into the text). Qt wants to operate in units of UTF16 chars. The trouble is
    // when you start using characters that are >1 byte a piece. Meaning position 3 (3 bytes into a file) could be 1 character.
    // -----------------------------------------------------------------------------------------------------------------------

    const bool backwards = minPos > maxPos;
    if (backwards)
        std::swap(minPos, maxPos);
    minPos = doc->MovePositionOutsideChar(minPos, 1, false);
    maxPos = doc->MovePositionOutsideChar(maxPos, -1, false);

    //qInfo(Q_FUNC_INFO);
    //qInfo("\tminPos %d", minPos);
    //qInfo("\tmaxPos %d", maxPos);
    //qInfo("\ts %s", s);
    //qInfo("\tcaseSensitive %s", caseSensitive ? "true" : "false");
    //qInfo("\tword %s", word ? "true" : "false");
    //qInfo("\twordStart %s", wordStart ? "true" : "false");
    //qInfo("\tflags %d", flags);

    auto options = QRegularExpression::MultilineOption | QRegularExpression::UseUnicodePropertiesOption;

    if (!FlagSet(flags, FindOption::MatchCase))
        options |= QRegularExpression::CaseInsensitiveOption;

    // TODO: does (*ANYCRLF) need prepended to the search string?
    QRegularExpression re(s, options);
    if (!re.isValid())
        return -1; // Invalid regular expression

    // Keep the original subject so ^, \A, boundaries and lookbehind retain their
    // meaning when the caller advances the search start after a match.
    const QString subject = QString::fromUtf8(doc->RangePointer(0, doc->Length()), doc->Length());
    const qsizetype startOffset = QString::fromUtf8(doc->RangePointer(0, minPos), minPos).size();
    const qsizetype endOffset = QString::fromUtf8(doc->RangePointer(0, maxPos), maxPos).size();
    QRegularExpressionMatch candidate;
    if (backwards) {
        auto matches = re.globalMatch(subject, startOffset);
        while (matches.hasNext()) {
            const auto next = matches.next();
            if (next.capturedEnd() > endOffset)
                break;
            candidate = next;
        }
    }
    else {
        candidate = re.match(subject, startOffset);
        if (candidate.hasMatch() && candidate.capturedEnd() > endOffset)
            return -1;
    }
    if (!candidate.hasMatch())
        return -1;
    match = candidate;

    const Sci::Position positionStart = doc->GetRelativePositionUTF16(0, match.capturedStart(0));
    const Sci::Position positionEnd = doc->GetRelativePositionUTF16(positionStart, match.capturedLength(0));

    // The length is the number of bytes that was matched
    *length = positionEnd - positionStart;

    return positionStart;
}

const char *QRegexSearch::SubstituteByPosition(Document *doc, const char *text, Sci::Position *length)
{
    Q_UNUSED(doc);

    qInfo(Q_FUNC_INFO);

    Q_ASSERT(match.isValid());
    Q_ASSERT(match.hasMatch());

    // Expand captures from the original match; searching the captured substring
    // again loses lookaround context and breaks zero-width replacements.
    const QString replacement = QString::fromUtf8(text, *length);
    QString newString;
    for (qsizetype i = 0; i < replacement.size(); ++i) {
        if (replacement[i] == QLatin1Char('\\') && i + 1 < replacement.size()) {
            const QChar next = replacement[i + 1];
            if (next.isDigit()) {
                int capture = next.digitValue();
                ++i;
                if (i + 1 < replacement.size() && replacement[i + 1].isDigit()) {
                    const int combined = capture * 10 + replacement[i + 1].digitValue();
                    if (combined <= match.lastCapturedIndex()) {
                        capture = combined;
                        ++i;
                    }
                }
                newString += match.captured(capture);
                continue;
            }
            if (next == QLatin1Char('\\')) {
                newString += next;
                ++i;
                continue;
            }
        }
        newString += replacement[i];
    }

    // TODO: figure out why this has to be new'd and can't be an instantiated class member
    if (substituted) {
        delete substituted;
    }

    substituted = new QByteArray(newString.toUtf8());
    *length = substituted->length();
    return substituted->data();
}
