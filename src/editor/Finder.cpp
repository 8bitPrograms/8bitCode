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

#include "Finder.h"

#include "UndoAction.h"
#include <QRegularExpression>

namespace {
QRegularExpression expressionFor(const FindOptions &options)
{
    auto flags = QRegularExpression::MultilineOption | QRegularExpression::UseUnicodePropertiesOption;
    if (!Scintilla::FlagSet(options.flags, Scintilla::FindOption::MatchCase))
        flags |= QRegularExpression::CaseInsensitiveOption;
    QString pattern = options.text;
    if (Scintilla::FlagSet(options.flags, Scintilla::FindOption::WholeWord))
        pattern = QStringLiteral("(?<!\\w)(?:%1)(?!\\w)").arg(pattern);
    return QRegularExpression(pattern, flags);
}

QString expandReplacement(const QString &replacement, const QRegularExpressionMatch &match)
{
    QString result;
    for (qsizetype i = 0; i < replacement.size(); ++i) {
        if (replacement[i] == QLatin1Char('\\') && i + 1 < replacement.size()) {
            const QChar next = replacement[i + 1];
            if (next.isDigit()) {
                int capture = next.digitValue();
                ++i;
                if (i + 1 < replacement.size() && replacement[i + 1].isDigit()) {
                    const int combined = capture * 10 + replacement[i + 1].digitValue();
                    if (combined <= match.lastCapturedIndex()) { capture = combined; ++i; }
                }
                result += match.captured(capture);
                continue;
            }
            if (next == QLatin1Char('\\')) { result += next; ++i; continue; }
        }
        result += replacement[i];
    }
    return result;
}
}

Finder::Finder(ScintillaNext *editor) :
    editor(editor)
{
}

void Finder::setEditor(ScintillaNext *editor)
{
    this->editor = editor;
}

int Finder::searchFlags() const
{
    return static_cast<int>(find_options.flags);
}

bool Finder::isRegexSearch() const
{
    return Scintilla::FlagSet(find_options.flags, Scintilla::FindOption::RegExp);
}

Sci_CharacterRange Finder::searchTarget(Sci_Position start, Sci_Position end)
{
    const QByteArray searchText = find_options.text.toUtf8();

    editor->setTargetRange(start, end);

    if (editor->searchInTarget(searchText.length(), searchText.constData()) == INVALID_POSITION) {
        return {INVALID_POSITION, INVALID_POSITION};
    }

    return {
        static_cast<Sci_PositionCR>(editor->targetStart()),
        static_cast<Sci_PositionCR>(editor->targetEnd())
    };
}

FindResult Finder::findNext()
{
    return findNextFrom(editor->selectionEnd());
}

FindResult Finder::findNextFrom(Sci_Position startPos)
{
    FindResult result;

    if (find_options.text.isEmpty())
        return result;

    editor->setSearchFlags(searchFlags());

    result.range = searchTarget(startPos, editor->length());

    // Return the valid result, or if not wrap around, go ahead and return it anyways
    if (result || !find_options.wrapAround)
        return result;

    result.range = searchTarget(0, startPos);

    if (result)
        result.wrapped = true;

    return result;
}

FindResult Finder::findPrev()
{
    FindResult result;

    if (find_options.text.isEmpty())
        return result;

    const Sci_Position startPos = editor->selectionStart();

    editor->setSearchFlags(searchFlags());

    // A target with start > end searches backwards.
    result.range = searchTarget(startPos, 0);

    // Return the valid result, or if not wrap around, go ahead and return it anyways
    if (result || !find_options.wrapAround)
        return result;

    result.range = searchTarget(editor->length(), startPos);

    if (result)
        result.wrapped = true;

    return result;
}

int Finder::count()
{
    if (find_options.text.isEmpty())
        return 0;

    int total = 0;

    if (isRegexSearch()) {
        const auto expression = expressionFor(find_options);
        if (!expression.isValid()) return 0;
        const QString subject = QString::fromUtf8(reinterpret_cast<const char *>(editor->characterPointer()), editor->length());
        auto matches = expression.globalMatch(subject);
        while (matches.hasNext()) { matches.next(); ++total; }
        return total;
    }

    forEachMatch([&total](int start, int end) {
        Q_UNUSED(start);
        ++total;
        return end;
    });

    return total;
}

FindResult Finder::replaceSelectionIfMatch(const QString &replaceText)
{
    FindResult result;

    if (find_options.text.isEmpty())
        return result;

    const Sci_Position selectionStart = editor->selectionStart();
    const Sci_Position selectionEnd = editor->selectionEnd();

    editor->setSearchFlags(searchFlags());
    result.range = searchTarget(selectionStart, selectionEnd);

    if (!result)
        return result;

    const QByteArray replacementText = replaceText.toUtf8();

    if (isRegexSearch()) {
        editor->replaceTargetRE(replacementText.length(), replacementText.constData());
    }
    else {
        editor->replaceTarget(replacementText.length(), replacementText.constData());
    }

    result.range = {
        static_cast<Sci_PositionCR>(editor->targetStart()),
        static_cast<Sci_PositionCR>(editor->targetEnd())
    };

    return result;
}

int Finder::replaceAll(const QString &replaceText)
{
    return replaceAllInRange(replaceText, {0, static_cast<Sci_PositionCR>(editor->length())});
}

int Finder::replaceAllInRange(const QString &replaceText, Sci_CharacterRange range)
{
    if (find_options.text.isEmpty() || editor->readOnly() || range.cpMin < 0 || range.cpMax < range.cpMin || range.cpMax > editor->length())
        return 0;

    if (isRegexSearch()) {
        const auto expression = expressionFor(find_options);
        if (!expression.isValid()) return 0;
        const QString subject = QString::fromUtf8(reinterpret_cast<const char *>(editor->characterPointer()), editor->length());
        const qsizetype start = editor->countCodeUnits(0, range.cpMin);
        const qsizetype end = editor->countCodeUnits(0, range.cpMax);
        QString output;
        output.reserve(end - start);
        qsizetype previousEnd = start;
        int total = 0;
        auto matches = expression.globalMatch(subject, start);
        while (matches.hasNext()) {
            const auto match = matches.next();
            if (match.capturedEnd() > end) break;
            output += QStringView(subject).sliced(previousEnd, match.capturedStart() - previousEnd);
            output += expandReplacement(replaceText, match);
            previousEnd = match.capturedEnd();
            ++total;
        }
        if (!total) return 0;
        output += QStringView(subject).sliced(previousEnd, end - previousEnd);
        if (output == QStringView(subject).sliced(start, end - start)) return total;
        const QByteArray bytes = output.toUtf8();
        const UndoAction undoAction(editor);
        editor->setTargetRange(range.cpMin, range.cpMax);
        editor->replaceTarget(bytes.size(), bytes.constData());
        return total;
    }

    const QByteArray searchText = find_options.text.toUtf8();
    const QByteArray replacementText = replaceText.toUtf8();

    Sci_TextToFind findText{
        range,
        searchText.constData(),
        {-1, -1}
    };

    editor->setSearchFlags(searchFlags());

    UndoAction undoAction(editor);

    int total = 0;

    // We intentionally use SCI_FINDTEXT here rather than forEachMatch().
    //
    // Replacing text changes the document length, so the search range has
    // to be updated after every replacement.
    while (editor->send(SCI_FINDTEXT, searchFlags(), reinterpret_cast<sptr_t>(&findText)) != INVALID_POSITION) {
        const Sci_Position start = findText.chrgText.cpMin;
        const Sci_Position end = findText.chrgText.cpMax;

        editor->setTargetRange(start, end);

        Sci_Position replacementLength;
        if (isRegexSearch()) {
            replacementLength= editor->replaceTargetRE(replacementText.length(), replacementText.constData());
        }
        else {
            replacementLength = editor->replaceTarget(replacementText.length(), replacementText.constData());
        }

        // Continue searching after the replacement.
        findText.chrg.cpMin = start + replacementLength;
        findText.chrg.cpMax += replacementLength - (end - start);

        ++total;

        // Empty regex matches do not consume input. Advance over an original
        // character, otherwise anchors/lookaheads can loop forever while inserting.
        if (start == end) {
            if (findText.chrg.cpMin >= findText.chrg.cpMax)
                break;
            findText.chrg.cpMin = editor->positionAfter(findText.chrg.cpMin);
        }
    }

    return total;
}
