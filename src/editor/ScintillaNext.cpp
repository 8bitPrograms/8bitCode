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


#include "ScintillaNext.h"
#include "Finder.h"
#include "ScintillaCommenter.h"

#include "ByteArrayUtils.h"
#include "uchardet.h"
#include <cinttypes>

#include <QDir>
#include <QCryptographicHash>
#include <QMouseEvent>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QTextCodec>
#include <memory>


const int CHUNK_SIZE = 1024 * 1024 * 4; // Not sure what is best

inline const QByteArray BOM_UTF8    = QByteArray::fromHex("EFBBBF");
inline const QByteArray BOM_UTF16LE = QByteArray::fromHex("FFFE");
inline const QByteArray BOM_UTF16BE = QByteArray::fromHex("FEFF");

ScintillaNext::BomType detectBom(const QByteArray& data)
{
    if (data.startsWith(BOM_UTF8))    return ScintillaNext::BomType::Utf8;
    if (data.startsWith(BOM_UTF16LE)) return ScintillaNext::BomType::Utf16LE;
    if (data.startsWith(BOM_UTF16BE)) return ScintillaNext::BomType::Utf16BE;

    return ScintillaNext::BomType::None;
}

QByteArray bomData(ScintillaNext::BomType bom)
{
    switch (bom) {
    case ScintillaNext::BomType::Utf8:    return BOM_UTF8;
    case ScintillaNext::BomType::Utf16LE: return BOM_UTF16LE;
    case ScintillaNext::BomType::Utf16BE: return BOM_UTF16BE;
    case ScintillaNext::BomType::None:    return QByteArray();
    }
    return QByteArray();
}

int bomLength(ScintillaNext::BomType bom)
{
    if (bom == ScintillaNext::BomType::Utf8) return BOM_UTF8.length();
    else if (bom == ScintillaNext::BomType::Utf16LE) return BOM_UTF16LE.length();
    else if (bom == ScintillaNext::BomType::Utf16BE) return BOM_UTF16BE.length();

    return 0;
}

static QTextCodec *resolveEncoding(const QString &name, ScintillaNext::BomType &bom)
{
    QString normalized = name.trimmed().toUpper();
    normalized.replace(QLatin1Char('_'), QLatin1Char('-'));
    bom = ScintillaNext::BomType::None;
    if (normalized == QStringLiteral("UTF-8 BOM")) {
        bom = ScintillaNext::BomType::Utf8;
        return QTextCodec::codecForName("UTF-8");
    }
    if (normalized == QStringLiteral("UTF-16LE") || normalized == QStringLiteral("UTF-16BE")) {
        bom = normalized.endsWith(QStringLiteral("LE")) ? ScintillaNext::BomType::Utf16LE : ScintillaNext::BomType::Utf16BE;
        return QTextCodec::codecForName(normalized.toLatin1());
    }
    if (normalized == QStringLiteral("UTF-16LE (NO BOM)")) return QTextCodec::codecForName("UTF-16LE");
    if (normalized == QStringLiteral("UTF-16BE (NO BOM)")) return QTextCodec::codecForName("UTF-16BE");
    return QTextCodec::codecForName(name.toLatin1());
}

static QFileDevice::FileError writeToDisk(const QByteArray &data, const QString &path, ScintillaNext::BomType bom, const QByteArray &encoding = "UTF-8")
{
    qInfo(Q_FUNC_INFO);

    QSaveFile file(path);
    // Never truncate the existing document if the temporary file cannot be created.
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning("writeToDisk() failed to open file %s: %s", qPrintable(path), qPrintable(file.errorString()));
        return file.error();
    }

    auto writeAll = [&file](QByteArrayView bytes) {
        while (!bytes.isEmpty()) {
            const qint64 written = file.write(bytes.data(), bytes.size());
            if (written <= 0)
                return false;
            bytes = bytes.sliced(written);
        }
        return true;
    };

    // Write BOM
    const QByteArray bomBytes = bomData(bom);
    if (!writeAll(bomBytes)) {
        qWarning("writeToDisk() failed writing BOM: %s", qPrintable(file.errorString()));
        file.cancelWriting();
        return QFileDevice::WriteError;
    }

    QTextCodec *codec = QTextCodec::codecForName(encoding);
    if (!codec) { file.cancelWriting(); return QFileDevice::WriteError; }
    if (codec->mibEnum() != 106) {
        QTextCodec *utf8 = QTextCodec::codecForName("UTF-8");
        QTextCodec::ConverterState inputState(QTextCodec::IgnoreHeader);
        QTextCodec::ConverterState outputState(QTextCodec::IgnoreHeader);
        QTextCodec::ConverterState verificationState(QTextCodec::IgnoreHeader);
        const bool verifyGb18030 = codec->name() == "GB18030";
        for (qsizetype offset = 0; offset < data.size(); offset += CHUNK_SIZE) {
            const auto bytes = QByteArrayView(data).sliced(offset, qMin(qsizetype(CHUNK_SIZE), data.size() - offset));
            const QString text = utf8->toUnicode(bytes.data(), int(bytes.size()), &inputState);
            const QByteArray encoded = codec->fromUnicode(text.constData(), int(text.size()), &outputState);
            // Some Qt GB18030 codecs use remainingChars as an initialized-state
            // marker. Validate the actual round trip instead, including Unicode
            // planes which some Qt versions cannot decode without truncation.
            const bool exactGb18030 = !verifyGb18030 ||
                (codec->toUnicode(encoded.constData(), int(encoded.size()), &verificationState) == text &&
                 !verificationState.invalidChars && !verificationState.remainingChars);
            if (inputState.invalidChars || outputState.invalidChars || !exactGb18030 || !writeAll(encoded)) {
                qWarning("Encoding conversion/write failed for %s (input errors %lld, output errors %lld)",
                         encoding.constData(), qlonglong(inputState.invalidChars), qlonglong(outputState.invalidChars));
                file.cancelWriting();
                return QFileDevice::WriteError;
            }
        }
        if (inputState.remainingChars || (!verifyGb18030 && outputState.remainingChars)) {
            qWarning("Incomplete encoding conversion for %s (input pending %lld, output pending %lld)",
                     encoding.constData(), qlonglong(inputState.remainingChars), qlonglong(outputState.remainingChars));
            file.cancelWriting();
            return QFileDevice::WriteError;
        }
    }
    else if (!writeAll(data)) {
        file.cancelWriting();
        return QFileDevice::WriteError;
    }

    if (!file.commit())
        return file.error() == QFileDevice::NoError ? QFileDevice::WriteError : file.error();
    return QFileDevice::NoError;
}

static bool isNewlineCharacter(char c)
{
    return c == '\n' || c == '\r';
}

ScintillaNext::ScintillaNext(QString name, QWidget *parent) :
    ScintillaEdit(parent),
    name(name),
    indicatorResources(INDICATOR_MAX + 1)
{
    // Per the scintilla documentation, some parts of the range are not generally available
    indicatorResources.disableRange(0, 7);
    indicatorResources.disableRange(INDICATOR_IME, INDICATOR_IME_MAX);
    indicatorResources.disableRange(INDICATOR_HISTORY_REVERTED_TO_ORIGIN_INSERTION, INDICATOR_HISTORY_REVERTED_TO_MODIFIED_DELETION);
    QObject::setProperty("8bitpadContentRevision", quint64(0));
    connect(this, &ScintillaNext::modified, this, [this](Scintilla::ModificationFlags flags) {
        if ((static_cast<int>(flags) & (SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT)) != 0)
            QObject::setProperty("8bitpadContentRevision", QObject::property("8bitpadContentRevision").toULongLong() + 1);
    });
}

ScintillaNext::~ScintillaNext()
{
}

ScintillaNext *ScintillaNext::fromFile(const QString &filePath, bool tryToCreate, const QString &encoding)
{
    QFile file(filePath);
    ScintillaNext *editor = new ScintillaNext(file.fileName());

    if(tryToCreate && !file.exists()) {
        qInfo("Trying to create %s", qUtf8Printable(filePath));
        QDir d;
        d.mkpath(QFileInfo(file).path());

        QFile f(filePath);
        f.open(QIODevice::WriteOnly);
        f.close();
    }

    bool readSuccessful = editor->readFromDisk(file, encoding);

    if (!readSuccessful) {
        delete editor;
        return Q_NULLPTR;
    }

    editor->setFileInfo(filePath);

    return editor;
}

QString ScintillaNext::eolModeToString(int eolMode)
{
    if (eolMode == SC_EOL_CRLF)
        return QStringLiteral("crlf");
    else if (eolMode == SC_EOL_CR)
        return QStringLiteral("cr");
    else if (eolMode == SC_EOL_LF)
        return QStringLiteral("lf");
    else
        return QString(); // unknown
}

int ScintillaNext::stringToEolMode(QString eolMode)
{
    if (eolMode == QStringLiteral("crlf"))
        return SC_EOL_CRLF;
    else if (eolMode == QStringLiteral("cr"))
        return SC_EOL_CR;
    else if (eolMode == QStringLiteral("lf"))
        return SC_EOL_LF;
    else
        return -1;
}

int ScintillaNext::allocateIndicator(const QString &name)
{
    return indicatorResources.requestResource(name);
}

void ScintillaNext::goToRange(const Sci_CharacterRange &range)
{
    qInfo(Q_FUNC_INFO);

    if (isRangeValid(range)) {
        // Lines can be folded so make sure they are visible
        ensureVisible(lineFromPosition(range.cpMin));
        ensureVisible(lineFromPosition(range.cpMax));

        setSelection(range.cpMax, range.cpMin);
        scrollRange(range.cpMax, range.cpMin);
    }
}

QByteArray ScintillaNext::eolString() const
{
    const int eol = eOLMode();

    if (eol == SC_EOL_LF) return QByteArrayLiteral("\n");
    else if (eol == SC_EOL_CRLF) return QByteArrayLiteral("\r\n");
    else return QByteArrayLiteral("\r");
}

bool ScintillaNext::lineIsEmpty(int line)
{
    return (lineEndPosition(line) - positionFromLine(line)) == 0;
}

void ScintillaNext::deleteLine(int line)
{
    deleteRange(positionFromLine(line), lineLength(line));
}

void ScintillaNext::cutAllowLine()
{
    if (selectionEmpty()) {
        copyAllowLine();
        lineDelete();
    }
    else {
        cut();
    }
}

Sci_CharacterRange ScintillaNext::getContextText()
{
    if (!selectionEmpty()) {
        if (selections() == 1) {
            int start = selectionStart();
            int end = selectionEnd();

            if (end - start < 1024 &&
                lineFromPosition(start) == lineFromPosition(end)) {
                return { start, end };
            }
        }

        return { INVALID_POSITION, INVALID_POSITION };
    }

    return wordAtPosition(currentPos());
}

Sci_CharacterRange ScintillaNext::wordAtPosition(int pos)
{
    int start = wordStartPosition(pos, true);
    int end = wordEndPosition(pos, true);

    if (end > start) {
        return { start, end };
    }

    return { INVALID_POSITION, INVALID_POSITION };
}

void ScintillaNext::modifyFoldLevels(int level, int action)
{
    const int totalLines = lineCount();

    int line = 0;
    while (line < totalLines) {
        int foldFlags = foldLevel(line); // Even though its called fold level it contains several other flags
        bool isHeader = foldFlags & SC_FOLDLEVELHEADERFLAG;
        int actualLevel = (foldFlags & SC_FOLDLEVELNUMBERMASK) - SC_FOLDLEVELBASE;

        if (isHeader && actualLevel == level) {
            foldLine(line, action);
            line = lastChild(line, -1) + 1;
        }
        else {
            ++line;
        }
    }
}

void ScintillaNext::foldAllLevels(int level)
{
    modifyFoldLevels(level, SC_FOLDACTION_CONTRACT);
}

void ScintillaNext::unFoldAllLevels(int level)
{
    modifyFoldLevels(level, SC_FOLDACTION_EXPAND);
}

void ScintillaNext::deleteLeadingEmptyLines()
{
    while (lineCount() > 1 && lineIsEmpty(0)) {
        deleteLine(0);
    }
}

void ScintillaNext::deleteTrailingEmptyLines()
{
    const int docLength = length();
    int position = docLength;

    while (position > 0 && isNewlineCharacter(charAt(position - 1))) {
        position--;
    }

    deleteRange(position, docLength - position);
}

bool ScintillaNext::isSavedToDisk() const
{
    return !canSaveToDisk();
}

bool ScintillaNext::canSaveToDisk() const
{
    // The buffer can be saved if:
    // - It is marked as a temporary since as soon as it gets saved it is no longer a temporary buffer
    // - A modified file
    // - A missing file since as soon as it is saved it is no longer missing.
    return temporary ||
           (bufferType == ScintillaNext::New && modify()) ||
           (bufferType == ScintillaNext::File && modify()) ||
            (bufferType == ScintillaNext::FileMissing);
}

void ScintillaNext::setName(const QString &name)
{
    this->name = name;

    emit renamed();
}

bool ScintillaNext::isFile() const
{
    return bufferType == ScintillaNext::File || bufferType == ScintillaNext::FileMissing;
}

QFileInfo ScintillaNext::getFileInfo() const
{
    Q_ASSERT(isFile());

    return fileInfo;
}

QString ScintillaNext::getPath() const
{
    Q_ASSERT(isFile());

    return QDir::toNativeSeparators(fileInfo.canonicalPath());
}

QString ScintillaNext::getFilePath() const
{
    Q_ASSERT(isFile());

    return QDir::toNativeSeparators(fileInfo.canonicalFilePath());
}

void ScintillaNext::setFoldMarkers(const QString &type)
{
    QMap<QString, QList<int>> map{
        {"simple", {SC_MARK_MINUS, SC_MARK_PLUS, SC_MARK_EMPTY, SC_MARK_EMPTY, SC_MARK_EMPTY, SC_MARK_EMPTY, SC_MARK_EMPTY}},
        {"arrow",  {SC_MARK_ARROWDOWN, SC_MARK_ARROW, SC_MARK_EMPTY, SC_MARK_EMPTY, SC_MARK_EMPTY, SC_MARK_EMPTY, SC_MARK_EMPTY}},
        {"circle", {SC_MARK_CIRCLEMINUS, SC_MARK_CIRCLEPLUS, SC_MARK_VLINE, SC_MARK_LCORNERCURVE, SC_MARK_CIRCLEPLUSCONNECTED, SC_MARK_CIRCLEMINUSCONNECTED, SC_MARK_TCORNERCURVE }},
        {"box",    {SC_MARK_BOXMINUS, SC_MARK_BOXPLUS, SC_MARK_VLINE, SC_MARK_LCORNER, SC_MARK_BOXPLUSCONNECTED, SC_MARK_BOXMINUSCONNECTED, SC_MARK_TCORNER }},
    };

    if (!map.contains(type))
        return;

    const auto types = map[type];
    markerDefine(SC_MARKNUM_FOLDEROPEN, types[0]);
    markerDefine(SC_MARKNUM_FOLDER, types[1]);
    markerDefine(SC_MARKNUM_FOLDERSUB, types[2]);
    markerDefine(SC_MARKNUM_FOLDERTAIL, types[3]);
    markerDefine(SC_MARKNUM_FOLDEREND, types[4]);
    markerDefine(SC_MARKNUM_FOLDEROPENMID, types[5]);
    markerDefine(SC_MARKNUM_FOLDERMIDTAIL, types[6]);
}

void ScintillaNext::close()
{
    emit closed();

    deleteLater();
}

QFileDevice::FileError ScintillaNext::save()
{
    qInfo(Q_FUNC_INFO);

    Q_ASSERT(isFile());

    emit aboutToSave();

    const QByteArray data = QByteArray::fromRawData((char*)characterPointer(), textLength());
    const QString path = fileInfo.filePath();
    QFileDevice::FileError writeSuccessful = writeToDisk(data, path, bomType, sourceEncoding);

    if (writeSuccessful == QFileDevice::NoError) {
        updateTimestamp();
        setSavePoint();

        // If this was a temporary file, make sure it is not any more
        setTemporary(false);

        emit saved();
    }

    return writeSuccessful;
}

void ScintillaNext::reload()
{
    reloadWithEncoding(QString());
}

bool ScintillaNext::reloadWithEncoding(const QString &encoding)
{
    if (!isFile()) return false;

    // Ensure the file still exists.
    if (!QFile::exists(fileInfo.canonicalFilePath())) {
        return false;
    }

    const int line = firstVisibleLine();
    const int caret = selectionNCaret(mainSelection());
    const int anchor = selectionNAnchor(mainSelection());

    // Decode into a separate document before replacing this buffer and its undo history.
    std::unique_ptr<ScintillaNext> replacement(fromFile(fileInfo.canonicalFilePath(), false, encoding));
    if (!replacement) {
        return false;
    }

    setReadOnly(false);
    {
        const QSignalBlocker blocker(this);
        setUndoCollection(false);
        clearAll();
        appendText(replacement->length(), reinterpret_cast<const char *>(replacement->characterPointer()));
        emptyUndoBuffer();
        setUndoCollection(true);
    }
    bomType = replacement->bom();
    sourceEncoding = replacement->sourceEncoding;
    QObject::setProperty("8bitpadContentRevision", QObject::property("8bitpadContentRevision").toULongLong() + 1);
    setReadOnly(replacement->readOnly());

    updateTimestamp();
    setSavePoint();

    // If this was a temporary file, make sure it is not any more
    if (isTemporary())
        setTemporary(false);

    scrollVertical(line, 0);
    setSelection(caret, anchor);

    emit reloaded();
    emit encodingChanged();
    return true;
}

void ScintillaNext::omitModifications()
{
    // If file modifications will be omitted just update file timestamp
    // so pop-up will be displayed only once per file modifications.
    updateTimestamp();
    setTemporary(true);

    return;
}

QFileDevice::FileError ScintillaNext::saveAs(const QString &newFilePath)
{
    bool isRenamed = bufferType == ScintillaNext::New || fileInfo.canonicalFilePath() != newFilePath;

    emit aboutToSave();

    const QByteArray data = QByteArray::fromRawData((char*)characterPointer(), textLength());
    QFileDevice::FileError saveSuccessful = writeToDisk(data, newFilePath, bomType, sourceEncoding);

    if (saveSuccessful == QFileDevice::NoError) {
        setFileInfo(newFilePath);
        setSavePoint();

        // If this was a temporary file, make sure it is not any more
        setTemporary(false);

        emit saved();

        if (isRenamed) {
            emit renamed();
        }
    }

    return saveSuccessful;
}

QFileDevice::FileError ScintillaNext::saveCopyAs(const QString &filePath)
{
    const QByteArray data = QByteArray::fromRawData((char*)characterPointer(), textLength());
    return writeToDisk(data, filePath, bomType, sourceEncoding);
}

QFileDevice::FileError ScintillaNext::saveRecoveryCopyAs(const QString &path)
{
    const QByteArray data = QByteArray::fromRawData(reinterpret_cast<const char *>(characterPointer()), textLength());
    return writeToDisk(data, path, BomType::None, "UTF-8");
}

QString ScintillaNext::encodingName() const
{
    if (bomType == BomType::Utf8) return QStringLiteral("UTF-8 BOM");
    if (sourceEncoding == "UTF-16LE" || sourceEncoding == "UTF-16BE")
        return QString::fromLatin1(sourceEncoding) + (bomType == BomType::None ? QStringLiteral(" (no BOM)") : QString());
    return QString::fromLatin1(sourceEncoding);
}

QStringList ScintillaNext::availableEncodings()
{
    QStringList result {QStringLiteral("UTF-8"), QStringLiteral("UTF-8 BOM"), QStringLiteral("UTF-16LE"), QStringLiteral("UTF-16BE"),
        QStringLiteral("UTF-16LE (no BOM)"), QStringLiteral("UTF-16BE (no BOM)")};
    QStringList legacy;
    for (const auto &name : QTextCodec::availableCodecs()) {
        auto *codec = QTextCodec::codecForName(name);
        if (!codec) continue;
        const QString canonical = QString::fromLatin1(codec->name());
        if (!canonical.startsWith(QStringLiteral("UTF-"), Qt::CaseInsensitive) && !legacy.contains(canonical))
            legacy.append(canonical);
    }
    legacy.sort(Qt::CaseInsensitive);
    result.append(legacy);
    return result;
}

bool ScintillaNext::setEncoding(const QString &encoding)
{
    BomType newBom;
    auto *codec = resolveEncoding(encoding, newBom);
    if (!codec) return false;
    if (sourceEncoding == codec->name() && bomType == newBom) return true;
    sourceEncoding = codec->name();
    bomType = newBom;
    setTemporary(true);
    emit encodingChanged();
    return true;
}

bool ScintillaNext::rename(const QString &newFilePath)
{
    const QString oldPath = fileInfo.canonicalFilePath();
    const QString existingDestination = QFileInfo(newFilePath).canonicalFilePath();
#ifdef Q_OS_WIN
    const bool sameFile = !oldPath.isEmpty() && oldPath.compare(existingDestination, Qt::CaseInsensitive) == 0;
#else
    const bool sameFile = !oldPath.isEmpty() && oldPath == existingDestination;
#endif
    if (sameFile)
        return save() == QFileDevice::NoError;

    emit aboutToSave();

    // Write out the buffer to the new path
    if (saveCopyAs(newFilePath) == QFileDevice::NoError) {
        // Remove the old file
        if (!oldPath.isEmpty() && !QFile::remove(oldPath))
            return false; // The saved copy is retained; do not report a completed rename.

        // Everything worked fine, so update the buffer's info
        setFileInfo(newFilePath);
        setSavePoint();

        // If this was a temporary file, make sure it is not any more
        setTemporary(false);

        emit saved();

        emit renamed();

        return true;
    }

    return false;
}

bool ScintillaNext::followFileMove(const QString &oldPath, const QString &newPath)
{
    if (!isFile()) return false;
    const QString oldRoot = QDir::cleanPath(QFileInfo(oldPath).absoluteFilePath());
    const QString current = QDir::cleanPath(fileInfo.absoluteFilePath());
#ifdef Q_OS_WIN
    const auto sensitivity = Qt::CaseInsensitive;
#else
    const auto sensitivity = Qt::CaseSensitive;
#endif
    QString suffix;
    if (current.compare(oldRoot, sensitivity) != 0) {
        const QString prefix = oldRoot + QLatin1Char('/');
        if (!current.startsWith(prefix, sensitivity)) return false;
        suffix = current.mid(prefix.size());
    }
    const QString destination = suffix.isEmpty() ? newPath : QDir(newPath).filePath(suffix);
    if (!QFileInfo(destination).isFile()) return false;
    setFileInfo(destination);
    emit renamed();
    return true;
}

ScintillaNext::FileStateChange ScintillaNext::checkFileForStateChange()
{
    if (bufferType == BufferType::New) {
        return FileStateChange::NoChange;
    }
    else if (bufferType == BufferType::File) {
        // refresh else exists() fails to notice missing file
        fileInfo.refresh();

        if (!fileInfo.exists()) {
            bufferType = BufferType::FileMissing;

            emit savePointChanged(false);

            return FileStateChange::Deleted;
        }

        // See if the timestamp changed
        if (modifiedTime != fileTimestamp()) {
            return FileStateChange::Modified;
        }
        else {
            return FileStateChange::NoChange;
        }
    }
    else if (bufferType == BufferType::FileMissing) {
        // See if it reappeared
        fileInfo.refresh();

        if (fileInfo.exists()) {
            bufferType = BufferType::File;

            return FileStateChange::Restored;
        }
        else {
            return FileStateChange::NoChange;
        }
    }

    qInfo("type() = %d", bufferType);
    Q_ASSERT(false);

    return FileStateChange::NoChange;
}

bool ScintillaNext::moveToTrash()
{
    if (QFile::exists(fileInfo.canonicalFilePath())) {
        QFile f(fileInfo.canonicalFilePath());

        return f.moveToTrash();
    }

    return false;
}

void ScintillaNext::toggleCommentSelection()
{
    ScintillaCommenter sc(this);
    sc.toggleSelection();
}

void ScintillaNext::commentLineSelection()
{
    ScintillaCommenter sc(this);
    sc.commentSelection();
}

void ScintillaNext::uncommentLineSelection()
{
    ScintillaCommenter sc(this);
    sc.uncommentSelection();
}

void ScintillaNext::removeDuplicateLines()
{
    QByteArray data = QByteArray::fromRawData((char*) characterPointer(), textLength());
    const QByteArray delim = eolString();

    auto lines = ByteArrayUtils::split(data, delim);
    int originalLineCount = lines.length();
    ByteArrayUtils::removeDuplicates(lines);

    if (originalLineCount == lines.length()){
        return; // No lines were removed
    }

    QByteArray result = ByteArrayUtils::join(lines, delim);

    const UndoAction ua(this);
    setTargetRange(0, textLength());
    replaceTarget(result.length(), result.constData());
}

void ScintillaNext::removeConsecutiveDuplicateLines()
{
    QByteArray data = QByteArray::fromRawData((char*) characterPointer(), textLength());
    const QByteArray delim = eolString();

    auto lines = ByteArrayUtils::split(data, delim);
    int originalLineCount = lines.length();
    ByteArrayUtils::removeConsecutiveDuplicates(lines);
    QByteArray result = ByteArrayUtils::join(lines, delim);

    if (originalLineCount == lines.length()){
        return; // No lines were removed
    }

    const UndoAction ua(this);
    setTargetRange(0, textLength());
    replaceTarget(result.length(), result.constData());
}

void ScintillaNext::dragEnterEvent(QDragEnterEvent *event)
{
    // Ignore all drag and drop events with urls and let the main application handle it
    if (event->mimeData()->hasUrls()) {
        return;
    }

    ScintillaEdit::dragEnterEvent(event);
}

void ScintillaNext::dropEvent(QDropEvent *event)
{
    // Ignore all drag and drop events with urls and let the main application handle it
    if (event->mimeData()->hasUrls()) {
        return;
    }

    ScintillaEdit::dropEvent(event);
}

bool ScintillaNext::readFromDisk(QFile &file, const QString &encoding)
{
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning("Cannot read file: %s", qUtf8Printable(file.errorString()));
        return false;
    }
    const auto detectedBom = detectBom(file.peek(3));
    QTextCodec *codec = nullptr;
    int offset = bomLength(detectedBom);
    if (!encoding.isEmpty()) {
        codec = resolveEncoding(encoding, bomType);
        // Explicit reinterpretation skips a matching header, never unrelated bytes.
        if (codec && ((detectedBom == BomType::Utf8 && codec->mibEnum() != 106) ||
            (detectedBom == BomType::Utf16LE && codec->name() != "UTF-16LE") ||
            (detectedBom == BomType::Utf16BE && codec->name() != "UTF-16BE")))
            offset = 0;
    }
    else {
        bomType = detectedBom;
        codec = QTextCodec::codecForName(detectedBom == BomType::Utf16LE ? "UTF-16LE" : detectedBom == BomType::Utf16BE ? "UTF-16BE" : "UTF-8");
    }
    if (!codec) return false;

    const QSignalBlocker blocker(this);
    setReadOnly(false);
    setCodePage(SC_CP_UTF8);
    setUndoCollection(false);
    bool success = false;
    for (int attempt = 0; attempt < 2; ++attempt) {
        clearAll();
        setStatus(SC_STATUS_OK);
        allocate(file.size());
        if (!file.seek(offset)) break;
        QTextCodec::ConverterState state(QTextCodec::IgnoreHeader);
        QTextCodec::ConverterState utf8State(QTextCodec::IgnoreHeader);
        QTextCodec::ConverterState verificationState(QTextCodec::IgnoreHeader);
        const bool verifyGb18030 = codec->name() == "GB18030";
        QCryptographicHash originalHash(QCryptographicHash::Sha256);
        QCryptographicHash roundTripHash(QCryptographicHash::Sha256);
        auto *utf8Codec = QTextCodec::codecForName("UTF-8");
        bool readFailed = false;
        while (!file.atEnd()) {
            const QByteArray bytes = file.read(CHUNK_SIZE);
            if (file.error() != QFileDevice::NoError) { readFailed = true; break; }
            const QString text = codec->toUnicode(bytes.constData(), int(bytes.size()), &state);
            if (state.invalidChars) break;
            if (verifyGb18030) {
                originalHash.addData(bytes);
                roundTripHash.addData(codec->fromUnicode(text.constData(), int(text.size()), &verificationState));
            }
            const QByteArray utf8 = utf8Codec->fromUnicode(text.constData(), int(text.size()), &utf8State);
            if (utf8State.invalidChars) break;
            appendText(utf8.size(), utf8.constData());
            if (status() != SC_STATUS_OK) break;
        }
        success = !readFailed && file.atEnd() && state.invalidChars == 0 && state.remainingChars == 0 &&
                  utf8State.invalidChars == 0 && utf8State.remainingChars == 0 && status() == SC_STATUS_OK &&
                  (!verifyGb18030 || (verificationState.invalidChars == 0 && originalHash.result() == roundTripHash.result()));
        if (success) {
            sourceEncoding = codec->name();
            break;
        }
        // Guess only BOM-less input that failed strict UTF-8. Explicit user choices
        // and malformed Unicode files must fail without silently changing encoding.
        if (attempt != 0 || !encoding.isEmpty() || detectedBom != BomType::None ||
            codec->mibEnum() != 106 || readFailed || status() != SC_STATUS_OK)
            break;
        if (!file.seek(0)) break;
        const uchardet_t detector = uchardet_new();
        if (!detector) break;
        bool detectorFailed = false;
        while (!file.atEnd()) {
            const QByteArray bytes = file.read(CHUNK_SIZE);
            if (file.error() != QFileDevice::NoError || uchardet_handle_data(detector, bytes.constData(), size_t(bytes.size())) != 0) {
                detectorFailed = true;
                break;
            }
        }
        uchardet_data_end(detector);
        const QByteArray detected = uchardet_get_charset(detector);
        uchardet_delete(detector);
        codec = detectorFailed ? nullptr : QTextCodec::codecForName(detected);
        if (!codec || codec->mibEnum() == 106) break;
        offset = 0;
    }
    setUndoCollection(true);
    if (!success) {
        qWarning("Unable to decode file without invalid characters");
        return false;
    }
    if (!QFileInfo(file).isWritable()) setReadOnly(true);
    return true;
}
QDateTime ScintillaNext::fileTimestamp()
{
    Q_ASSERT(bufferType != ScintillaNext::New);

    fileInfo.refresh();
    qInfo("%s last modified %s", qUtf8Printable(fileInfo.fileName()), qUtf8Printable(fileInfo.lastModified().toString()));
    return fileInfo.lastModified();
}

void ScintillaNext::updateTimestamp()
{
    modifiedTime = fileTimestamp();
}

void ScintillaNext::setFileInfo(const QString &filePath)
{
    fileInfo.setFile(filePath);
    fileInfo.makeAbsolute();

    Q_ASSERT(fileInfo.exists());

    name = fileInfo.fileName();
    bufferType = ScintillaNext::File;

    updateTimestamp();
}

void ScintillaNext::detachFileInfo(const QString &newName)
{
    setName(newName);

    bufferType = ScintillaNext::New;
}

void ScintillaNext::setTemporary(bool temp)
{
    temporary = temp;

    // Fake this signal
    emit savePointChanged(temporary);
}
