// SPDX-License-Identifier: GPL-3.0-or-later
#include "TakeNotes.h"

#include "ApplicationSettings.h"
#include "MainWindow.h"
#include "NoteFormat.h"
#include "ScintillaNext.h"

#include <QAction>
#include <QClipboard>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStandardPaths>
#include <QTimer>
#include <QToolBar>
#include <QVariant>
#include <QVBoxLayout>

namespace {
constexpr auto directoryKey = "Notes/Directory";
constexpr auto patternKey = "Notes/Pattern";
constexpr auto extensionKey = "Notes/Extension";
constexpr auto templateKey = "Notes/Template";
constexpr auto counterKey = "Notes/Counter";

QString defaultDirectory()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).filePath(QStringLiteral("8bitCode Notes"));
}

class TakeNotesController final : public QObject
{
public:
    TakeNotesController(MainWindow *window, QObject *parent)
        : QObject(parent), window(window), settings(new ApplicationSettings(this))
    {
        dock = new QDockWidget(tr("Notes"), window);
        dock->setObjectName(QStringLiteral("TakeNotesDock"));
        auto *body = new QWidget(dock);
        auto *layout = new QVBoxLayout(body);
        layout->setContentsMargins(0, 0, 0, 0);
        hint = new QLabel(tr("Double-click the first row, or an empty area, to create a note."), body);
        hint->setWordWrap(true);
        hint->setContentsMargins(8, 6, 8, 6);
        list = new QListWidget(body);
        list->setObjectName(QStringLiteral("notesList"));
        list->setUniformItemSizes(true);
        list->setFrameStyle(QFrame::NoFrame);
        list->viewport()->installEventFilter(this);
        layout->addWidget(hint);
        layout->addWidget(list);
        dock->setWidget(body);
        dock->hide();
        window->addDockWidget(Qt::LeftDockWidgetArea, dock);
        if (auto *folder = window->findChild<QDockWidget *>(QStringLiteral("FolderAsWorkspaceDock")))
            window->tabifyDockWidget(folder, dock);
        if (auto *view = window->findChild<QMenu *>(QStringLiteral("menuView")))
            view->addAction(dock->toggleViewAction());

        auto *newNote = new QAction(tr("New Note"), window);
        newNote->setObjectName(QStringLiteral("actionNewNote"));
        newNote->setShortcut(QKeySequence(QStringLiteral("Ctrl+Alt+N")));
        newNote->setShortcutContext(Qt::WindowShortcut);
        window->addAction(newNote);
        auto *configure = new QAction(tr("Take Notes…"), window);
        configure->setObjectName(QStringLiteral("actionTakeNotesSettings"));
        if (auto *file = window->findChild<QMenu *>(QStringLiteral("menuFile"))) {
            if (auto *reload = window->findChild<QAction *>(QStringLiteral("actionReload")))
                file->insertAction(reload, newNote);
        }
        if (auto *settingsMenu = window->findChild<QMenu *>(QStringLiteral("menuSettings")))
            settingsMenu->addAction(configure);

        connect(newNote, &QAction::triggered, this, [this] { create(false); });
        connect(configure, &QAction::triggered, this, [this] { editSettings(); });
        connect(list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
            const QString path = item->data(Qt::UserRole).toString();
            if (path.isEmpty())
                create(false);
            else
                this->window->openFile(path);
        });
        connect(window, &MainWindow::editorActivated, this, [this](ScintillaNext *editor) { watch(editor); });
        for (auto *editor : window->editors())
            watch(editor);
        connect(dock, &QDockWidget::visibilityChanged, this, [this](bool visible) {
            if (visible)
                reload();
        });
        QTimer::singleShot(0, this, [this, newNote] {
            if (auto *bar = this->window->findChild<QToolBar *>(QStringLiteral("mainToolBar"))) {
                if (!bar->actions().contains(newNote)) {
                    bar->addSeparator();
                    bar->addAction(newNote);
                }
            }
        });
        reload();
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::MouseButtonDblClick) {
            auto *mouse = static_cast<QMouseEvent *>(event);
            if (mouse->button() == Qt::LeftButton && watched == list->viewport() && !list->itemAt(mouse->position().toPoint())) {
                create(false);
                return true;
            }
            if (mouse->button() == Qt::LeftButton && (mouse->modifiers() & Qt::ControlModifier)) {
                if (auto *view = qobject_cast<QWidget *>(watched)) {
                    if (qobject_cast<ScintillaNext *>(view->parentWidget())) {
                        create(true);
                        return true;
                    }
                }
            }
        }
        return QObject::eventFilter(watched, event);
    }

private:
    QString directory() const
    {
        const QString stored = settings->value(QLatin1String(directoryKey)).toString();
        return stored.isEmpty() ? defaultDirectory() : stored;
    }

    NoteContext context(bool currentLine) const
    {
        NoteContext note;
        note.when = QDateTime::currentDateTime();
        note.counter = settings->value(QLatin1String(counterKey), 1).toInt();
        note.user = QDir::home().dirName();
        if (auto *clipboard = QGuiApplication::clipboard())
            note.clipboard = clipboard->text().left(4000);
        if (auto *editor = window->currentEditor()) {
            note.selection = QString::fromUtf8(editor->getSelText());
            if (note.selection.isEmpty() && currentLine) {
                const int line = editor->lineFromPosition(editor->currentPos());
                const int start = static_cast<int>(editor->positionFromLine(line));
                const int end = static_cast<int>(editor->lineEndPosition(line));
                note.selection = QString::fromUtf8(editor->get_text_range(start, end));
            }
        }
        return note;
    }

    void create(bool currentLine)
    {
        QString error;
        const QString path = createNoteFile(directory(),
            settings->value(QLatin1String(patternKey), QStringLiteral("{date} {time}")).toString(),
            settings->value(QLatin1String(extensionKey), QStringLiteral("txt")).toString(),
            settings->value(QLatin1String(templateKey)).toString(),
            context(currentLine), &error);
        if (path.isEmpty()) {
            QMessageBox::warning(window, tr("Take Notes"), error);
            return;
        }
        settings->setValue(QLatin1String(counterKey), context(false).counter + 1);
        reload();
        dock->show();
        dock->raise();
        window->openFile(path);
    }

    void reload()
    {
        list->clear();
        auto *createItem = new QListWidgetItem(tr("Double-click to take a note"));
        createItem->setData(Qt::UserRole, QString());
        QFont font = createItem->font();
        font.setItalic(true);
        createItem->setFont(font);
        list->addItem(createItem);

        QDir dir(directory());
        if (!dir.exists())
            return;
        const QFileInfoList files = dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot, QDir::Time);
        const int cap = qMin(files.size(), 400);
        for (int i = 0; i < cap; ++i) {
            auto *item = new QListWidgetItem(files.at(i).fileName());
            item->setData(Qt::UserRole, files.at(i).absoluteFilePath());
            item->setToolTip(files.at(i).absoluteFilePath());
            list->addItem(item);
        }
    }

    void editSettings()
    {
        QDialog dialog(window);
        dialog.setWindowTitle(tr("Take Notes"));
        auto *layout = new QVBoxLayout(&dialog);
        auto *form = new QFormLayout;
        auto *folder = new QLineEdit(directory(), &dialog);
        auto *browse = new QPushButton(tr("Browse…"), &dialog);
        auto *folderRow = new QHBoxLayout;
        folderRow->addWidget(folder);
        folderRow->addWidget(browse);
        auto *pattern = new QLineEdit(settings->value(QLatin1String(patternKey), QStringLiteral("{date} {time}")).toString(), &dialog);
        auto *extension = new QLineEdit(settings->value(QLatin1String(extensionKey), QStringLiteral("txt")).toString(), &dialog);
        auto *templ = new QPlainTextEdit(settings->value(QLatin1String(templateKey)).toString(), &dialog);
        templ->setPlaceholderText(tr("{datetime}\n\n{selection}"));
        templ->setTabChangesFocus(true);
        form->addRow(tr("Folder"), folderRow);
        form->addRow(tr("File name"), pattern);
        form->addRow(tr("Extension"), extension);
        form->addRow(tr("Template"), templ);
        layout->addLayout(form);
        layout->addWidget(new QLabel(tr("Tokens: {date} {time} {datetime} {year} {month} {day} {hour} {minute} {second} {counter} {selection} {clipboard} {user}. strftime tokens %Y %m %d %H %M %S also work. Ctrl+double-click in the editor uses the current line when nothing is selected. An empty template stores that text as the note."), &dialog));
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        layout->addWidget(buttons);
        connect(browse, &QPushButton::clicked, &dialog, [this, folder] {
            const QString chosen = QFileDialog::getExistingDirectory(window, tr("Notes folder"), folder->text());
            if (!chosen.isEmpty())
                folder->setText(chosen);
        });
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() != QDialog::Accepted)
            return;
        settings->setValue(QLatin1String(directoryKey), folder->text().trimmed());
        settings->setValue(QLatin1String(patternKey), pattern->text().trimmed().isEmpty() ? QStringLiteral("{date} {time}") : pattern->text().trimmed());
        settings->setValue(QLatin1String(extensionKey), extension->text().trimmed().isEmpty() ? QStringLiteral("txt") : extension->text().trimmed());
        settings->setValue(QLatin1String(templateKey), templ->toPlainText());
        reload();
    }

    void watch(ScintillaNext *editor)
    {
        if (!editor || editor->QObject::property("noteGesture").toBool())
            return;
        editor->QObject::setProperty("noteGesture", true);
        editor->viewport()->installEventFilter(this);
    }

    MainWindow *window;
    ApplicationSettings *settings;
    QDockWidget *dock = nullptr;
    QListWidget *list = nullptr;
    QLabel *hint = nullptr;
};
}

void installTakeNotes(MainWindow *window, NotepadNextApplication *)
{
    new TakeNotesController(window, window);
}
