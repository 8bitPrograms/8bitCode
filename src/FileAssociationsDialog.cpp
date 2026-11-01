// SPDX-License-Identifier: GPL-3.0-or-later
#include "FileAssociationsDialog.h"

#include "UiIcons.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QEvent>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <utility>

namespace {
constexpr int extensionRole = Qt::UserRole;
QString canonicalExtension(QString value)
{
    value = value.trimmed().toLower();
    if (value.startsWith(QStringLiteral("*.")))
        value.remove(0, 2);
    else if (value.startsWith(QLatin1Char('.')))
        value.remove(0, 1);
    return value;
}
}

FileAssociationsDialog::FileAssociationsDialog(QWidget *parent, Callbacks callbacks)
    : QDialog(parent), callbacks(std::move(callbacks))
{
    setObjectName(QStringLiteral("fileAssociationsDialog"));
    setWindowTitle(tr("File associations"));
    resize(900, 680);
    setMinimumSize(720, 560);
    if (!this->callbacks.catalog) this->callbacks.catalog = FileAssociations::catalog;
    if (!this->callbacks.registeredExtensions) this->callbacks.registeredExtensions = FileAssociations::registeredExtensions;
    if (!this->callbacks.parseExtensions) this->callbacks.parseExtensions = FileAssociations::parseExtensions;
    if (!this->callbacks.apply) this->callbacks.apply = FileAssociations::apply;
    if (!this->callbacks.contextMenuInstalled) this->callbacks.contextMenuInstalled = FileAssociations::contextMenuInstalled;
    if (!this->callbacks.setContextMenuEnabled) this->callbacks.setContextMenuEnabled = FileAssociations::setContextMenuEnabled;

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 18, 20, 18);
    layout->setSpacing(10);
    auto *heading = new QLabel(tr("Choose file types for 8bitCode"), this);
    heading->setObjectName(QStringLiteral("associationHeading"));
    QFont headingFont = heading->font();
    headingFont.setBold(true);
    heading->setFont(headingFont);
    layout->addWidget(heading);
    auto *intro = new QLabel(tr("Check individual extensions or a group, then apply the selected types together."), this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto *filterRow = new QHBoxLayout;
    filterRow->setSpacing(8);
    search = new QLineEdit(this);
    search->setObjectName(QStringLiteral("associationSearch"));
    search->setPlaceholderText(tr("Filter by extension or group…"));
    search->setClearButtonEnabled(true);
    search->setAccessibleName(tr("Filter file types"));
    search->installEventFilter(this);
    filterRow->addWidget(search, 1);
    selectAll = new QPushButton(tr("Select all types"), this);
    selectAll->setObjectName(QStringLiteral("associationSelectAll"));
    selectAll->setToolTip(tr("Select every listed type, including those hidden by the filter."));
    filterRow->addWidget(selectAll);
    selectVisible = new QPushButton(tr("Select visible"), this);
    selectVisible->setObjectName(QStringLiteral("associationSelectVisible"));
    filterRow->addWidget(selectVisible);
    clearAll = new QPushButton(tr("Clear selection"), this);
    clearAll->setObjectName(QStringLiteral("associationClearSelection"));
    clearAll->setToolTip(tr("Clear every selection, including those hidden by the filter."));
    filterRow->addWidget(clearAll);
    layout->addLayout(filterRow);

    tree = new QTreeWidget(this);
    tree->setObjectName(QStringLiteral("associationTree"));
    tree->setColumnCount(2);
    tree->setHeaderLabels({tr("File type"), tr("Registration")});
    tree->setRootIsDecorated(true);
    tree->setUniformRowHeights(true);
    tree->setSelectionMode(QAbstractItemView::SingleSelection);
    tree->setAllColumnsShowFocus(true);
    tree->setMinimumHeight(180);
    tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    layout->addWidget(tree, 1);

    auto *customRow = new QHBoxLayout;
    customRow->setSpacing(8);
    customInput = new QLineEdit(this);
    customInput->setObjectName(QStringLiteral("associationCustomInput"));
    customInput->setPlaceholderText(tr("Add custom types: .txt, *.log, yaml"));
    customInput->setAccessibleName(tr("Custom file extensions"));
    customInput->installEventFilter(this);
    customRow->addWidget(customInput, 1);
    addCustom = new QPushButton(tr("Add and select"), this);
    addCustom->setObjectName(QStringLiteral("associationAddCustom"));
    customRow->addWidget(addCustom);
    selectedCount = new QLabel(this);
    selectedCount->setObjectName(QStringLiteral("associationSelectedCount"));
    selectedCount->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    customRow->addWidget(selectedCount);
    layout->addLayout(customRow);

    requestDefaults = new QCheckBox(this);
    requestDefaults->setObjectName(QStringLiteral("associationRequestDefaults"));
    requestDefaults->setText(tr("Make 8bitCode the default application for the selected file types"));
#ifdef Q_OS_WIN
    requestDefaults->setToolTip(tr("Apply directly where Windows allows it. Any types requiring Windows confirmation are listed here."));
#endif
    requestDefaults->setChecked(true);
    layout->addWidget(requestDefaults);
#ifndef Q_OS_WIN
    auto *mimeHint = new QLabel(tr("Linux groups some extensions under the same file type; setting a default can also affect related extensions."), this);
    mimeHint->setWordWrap(true);
    layout->addWidget(mimeHint);
#endif

    auto *contextGroup = new QGroupBox(tr("File manager context menu"), this);
    contextGroup->setObjectName(QStringLiteral("associationContextMenuGroup"));
    auto *contextLayout = new QHBoxLayout(contextGroup);
    contextLayout->setContentsMargins(12, 14, 12, 10);
    contextLayout->setSpacing(8);
    contextState = new QLabel(contextGroup);
    contextState->setObjectName(QStringLiteral("associationContextMenuState"));
    contextState->setWordWrap(true);
    contextLayout->addWidget(contextState, 1);
    addContext = new QPushButton(tr("Add context menu"), contextGroup);
    addContext->setObjectName(QStringLiteral("associationAddContextMenu"));
    contextLayout->addWidget(addContext);
    removeContext = new QPushButton(tr("Remove context menu"), contextGroup);
    removeContext->setObjectName(QStringLiteral("associationRemoveContextMenu"));
    contextLayout->addWidget(removeContext);
    layout->addWidget(contextGroup);

    status = new QLabel(this);
    status->setObjectName(QStringLiteral("associationStatus"));
    status->setTextFormat(Qt::PlainText);
    status->setWordWrap(true);
    status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    status->hide();
    layout->addWidget(status);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    buttons->setObjectName(QStringLiteral("associationButtons"));
#ifdef Q_OS_WIN
    apply = buttons->addButton(tr("Associate selected file types"), QDialogButtonBox::ApplyRole);
#else
    apply = buttons->addButton(tr("Apply selected file types"), QDialogButtonBox::ApplyRole);
#endif
    apply->setObjectName(QStringLiteral("associationApply"));
    layout->addWidget(buttons);

    populateCatalog();
    contextInstalled = this->callbacks.contextMenuInstalled();
    updateContextMenuState();
    refreshIcons();

    connect(search, &QLineEdit::textChanged, this, &FileAssociationsDialog::filterExtensions);
    connect(selectAll, &QPushButton::clicked, this, [this] { setSelection(true, false); });
    connect(selectVisible, &QPushButton::clicked, this, [this] { setSelection(true, true); });
    connect(clearAll, &QPushButton::clicked, this, [this] { setSelection(false, false); });
    connect(addCustom, &QPushButton::clicked, this, &FileAssociationsDialog::addCustomExtensions);
    connect(customInput, &QLineEdit::returnPressed, this, &FileAssociationsDialog::addCustomExtensions);
    connect(apply, &QPushButton::clicked, this, &FileAssociationsDialog::applySelection);
    connect(addContext, &QPushButton::clicked, this, [this] { setContextMenu(true); });
    connect(removeContext, &QPushButton::clicked, this, [this] { setContextMenu(false); });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(tree, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem *item, int column) {
        if (changingItems || column != 0)
            return;
        if (!item->parent()) {
            const bool checked = item->checkState(0) == Qt::Checked;
            const QSignalBlocker blocker(tree);
            for (int i = 0; i < item->childCount(); ++i) {
                if (!item->child(i)->isHidden())
                    item->child(i)->setCheckState(0, checked ? Qt::Checked : Qt::Unchecked);
            }
        }
        updateSelectionSummary();
    });
}

QTreeWidgetItem *FileAssociationsDialog::appendExtension(QTreeWidgetItem *group, const QString &extension)
{
    const QString canonical = canonicalExtension(extension);
    if (canonical.isEmpty())
        return nullptr;
    if (extensionItems.contains(canonical))
        return extensionItems.value(canonical);
    auto *item = new QTreeWidgetItem(group);
    item->setText(0, QLatin1Char('.') + canonical);
    item->setData(0, extensionRole, canonical);
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
    item->setCheckState(0, Qt::Unchecked);
    item->setText(1, registered.contains(canonical) ? tr("Registered") : QString());
    extensionItems.insert(canonical, item);
    return item;
}

void FileAssociationsDialog::populateCatalog()
{
    const QSignalBlocker blocker(tree);
    for (const auto &extension : callbacks.registeredExtensions())
        registered.insert(canonicalExtension(extension));
    for (const auto &definition : callbacks.catalog()) {
        auto *group = new QTreeWidgetItem(tree);
        group->setText(0, definition.title);
        group->setData(0, extensionRole, definition.id);
        group->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
        group->setCheckState(0, Qt::Unchecked);
        group->setToolTip(0, tr("Check or clear the visible file types in this group."));
        groups.append(group);
        for (const auto &extension : definition.extensions)
            appendExtension(group, extension);
        group->setExpanded(true);
    }
    customGroup = new QTreeWidgetItem(tree);
    customGroup->setText(0, tr("Custom"));
    customGroup->setData(0, extensionRole, QStringLiteral("custom"));
    customGroup->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
    customGroup->setCheckState(0, Qt::Unchecked);
    customGroup->setExpanded(true);
    groups.append(customGroup);
    QStringList existing = registered.values();
    existing.sort(Qt::CaseInsensitive);
    for (const auto &extension : existing)
        appendExtension(customGroup, extension);
    filterExtensions();
}

QStringList FileAssociationsDialog::selectedExtensions() const
{
    QStringList result;
    for (auto it = extensionItems.cbegin(); it != extensionItems.cend(); ++it) {
        if (it.value()->checkState(0) == Qt::Checked)
            result.append(it.key());
    }
    result.sort(Qt::CaseInsensitive);
    return result;
}

void FileAssociationsDialog::filterExtensions()
{
    const QString filter = search->text().trimmed();
    for (QTreeWidgetItem *group : std::as_const(groups)) {
        const bool groupMatches = filter.isEmpty() || group->text(0).contains(filter, Qt::CaseInsensitive)
            || group->data(0, extensionRole).toString().contains(filter, Qt::CaseInsensitive);
        int visible = 0;
        for (int i = 0; i < group->childCount(); ++i) {
            auto *item = group->child(i);
            const bool match = groupMatches || item->text(0).contains(filter, Qt::CaseInsensitive);
            item->setHidden(!match);
            visible += match;
        }
        group->setHidden(visible == 0);
        if (!filter.isEmpty() && visible > 0)
            group->setExpanded(true);
    }
    selectVisible->setVisible(!filter.isEmpty());
    updateSelectionSummary();
}

void FileAssociationsDialog::updateSelectionSummary()
{
    const QSignalBlocker blocker(tree);
    int totalVisible = 0;
    for (QTreeWidgetItem *group : std::as_const(groups)) {
        int visible = 0, checked = 0;
        for (int i = 0; i < group->childCount(); ++i) {
            auto *item = group->child(i);
            if (!item->isHidden()) {
                ++visible;
                checked += item->checkState(0) == Qt::Checked;
            }
        }
        totalVisible += visible;
        group->setCheckState(0, checked == 0 ? Qt::Unchecked : checked == visible ? Qt::Checked : Qt::PartiallyChecked);
        group->setText(1, tr("%1 types").arg(group->childCount()));
    }
    const auto selected = selectedExtensions();
    selectedCount->setText(tr("%1 selected · %2 visible").arg(selected.size()).arg(totalVisible));
    apply->setEnabled(!selected.isEmpty());
    clearAll->setEnabled(!selected.isEmpty());
    selectAll->setEnabled(selected.size() < extensionItems.size());
    selectVisible->setEnabled(totalVisible > 0);
}

void FileAssociationsDialog::setSelection(bool checked, bool visibleOnly)
{
    const QSignalBlocker blocker(tree);
    for (QTreeWidgetItem *item : std::as_const(extensionItems)) {
        if (!visibleOnly || !item->isHidden())
            item->setCheckState(0, checked ? Qt::Checked : Qt::Unchecked);
    }
    updateSelectionSummary();
}

void FileAssociationsDialog::addCustomExtensions()
{
    const auto parsed = callbacks.parseExtensions(customInput->text());
    if (!parsed.valid()) {
        showStatus(tr("The custom extensions were not added."), {parsed.error});
        return;
    }
    if (parsed.extensions.isEmpty())
        return;
    QTreeWidgetItem *first = nullptr;
    {
        const QSignalBlocker blocker(tree);
        for (const auto &extension : parsed.extensions) {
            if (auto *item = appendExtension(customGroup, extension)) {
                item->setCheckState(0, Qt::Checked);
                if (!first) first = item;
            }
        }
    }
    customInput->clear();
    search->clear();
    customGroup->setExpanded(true);
    filterExtensions();
    refreshIcons();
    if (first)
        tree->scrollToItem(first);
    showStatus(tr("Custom types added to the selection. Apply to register them."));
}

void FileAssociationsDialog::applySelection()
{
    const QStringList selected = selectedExtensions();
    if (selected.isEmpty())
        return;
    apply->setEnabled(false);
    setCursor(Qt::WaitCursor);
    const auto result = callbacks.apply(selected, requestDefaults->isChecked());
    unsetCursor();
    for (const auto &extension : result.registeredExtensions)
        registered.insert(canonicalExtension(extension));
    {
        const QSignalBlocker blocker(tree);
        for (auto it = extensionItems.cbegin(); it != extensionItems.cend(); ++it) {
            QString state = registered.contains(it.key()) ? tr("Registered") : QString();
            if (result.defaultExtensions.contains(it.key()))
                state = tr("Default");
            else if (result.pendingDefaultExtensions.contains(it.key()))
                state = tr("Needs Windows confirmation");
            it.value()->setText(1, state);
        }
    }
    QString message = tr("%1 file types registered.").arg(result.registeredExtensions.size());
#ifdef Q_OS_WIN
    if (!result.defaultExtensions.isEmpty())
        message += QLatin1Char(' ') + tr("Default application set for %1 file types.").arg(result.defaultExtensions.size());
    if (!result.pendingDefaultExtensions.isEmpty()) {
        QStringList types;
        for (const QString &extension : result.pendingDefaultExtensions.mid(0, 6))
            types << '.' + extension;
        if (result.pendingDefaultExtensions.size() > types.size())
            types << tr("%1 more").arg(result.pendingDefaultExtensions.size() - types.size());
        message += QLatin1Char(' ') + tr("Windows requires confirmation for %1 file types: %2.")
            .arg(result.pendingDefaultExtensions.size()).arg(types.join(", "));
    }
#else
    if (!result.defaultMimeTypes.isEmpty())
        message += QLatin1Char(' ') + tr("Default application updated for %1 content types.").arg(result.defaultMimeTypes.size());
#endif
    showStatus(message, result.errors);
    updateSelectionSummary();
}

void FileAssociationsDialog::setContextMenu(bool enabled)
{
    addContext->setEnabled(false);
    removeContext->setEnabled(false);
    setCursor(Qt::WaitCursor);
    const auto result = callbacks.setContextMenuEnabled(enabled);
    unsetCursor();
    if (result.ok())
        contextInstalled = enabled;
    else
        contextInstalled = callbacks.contextMenuInstalled();
    updateContextMenuState();
    showStatus(result.ok() ? (enabled ? tr("Context menu integration added. File associations were not changed.")
                                     : tr("Context menu integration removed. File associations were not changed."))
                          : tr("Context menu integration could not be updated."), result.errors);
}

void FileAssociationsDialog::updateContextMenuState()
{
    contextState->setText(contextInstalled ? tr("8bitCode is available in the file manager context menu.")
                                          : tr("Add 8bitCode to the file manager context menu."));
    addContext->setEnabled(!contextInstalled);
    removeContext->setEnabled(contextInstalled);
}

void FileAssociationsDialog::showStatus(const QString &message, const QStringList &errors)
{
    QString text = message;
    const int shownErrors = std::min(3, int(errors.size()));
    for (int i = 0; i < shownErrors; ++i)
        text += QLatin1Char('\n') + errors[i];
    if (errors.size() > shownErrors)
        text += QLatin1Char('\n') + tr("%1 more errors; hover for details.").arg(errors.size() - shownErrors);
    status->setText(text);
    status->setToolTip(errors.join(QLatin1Char('\n')));
    status->show();
}

void FileAssociationsDialog::refreshIcons()
{
    if (apply) apply->setIcon(uiIcon(UiIcon::Save, apply));
    if (addCustom) addCustom->setIcon(uiIcon(UiIcon::Plus, addCustom));
    if (addContext) addContext->setIcon(uiIcon(UiIcon::Plus, addContext));
    if (removeContext) removeContext->setIcon(uiIcon(UiIcon::Dismiss, removeContext));
    if (!tree)
        return;
    // Setting a group's icon emits itemChanged in column zero too. Block it
    // so a palette refresh can never be mistaken for a checkbox click.
    const QSignalBlocker blocker(tree);
    for (auto *group : std::as_const(groups))
        group->setIcon(0, uiIcon(UiIcon::Folder, tree));
    for (auto *item : std::as_const(extensionItems))
        item->setIcon(0, uiIcon(UiIcon::File, tree));
}

void FileAssociationsDialog::changeEvent(QEvent *event)
{
    QDialog::changeEvent(event);
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::StyleChange)
        refreshIcons();
}

bool FileAssociationsDialog::eventFilter(QObject *watched, QEvent *event)
{
    if ((watched == customInput || watched == search) && event->type() == QEvent::KeyPress) {
        const auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
            // Editing an extension/filter is selection work, not approval to
            // write OS associations via QDialog's implicit default button.
            if (watched == customInput)
                addCustomExtensions();
            return true;
        }
    }
    return QDialog::eventFilter(watched, event);
}
