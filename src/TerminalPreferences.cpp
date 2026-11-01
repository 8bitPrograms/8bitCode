// SPDX-License-Identifier: GPL-3.0-or-later
#include "TerminalPreferences.h"

#include "UiIcons.h"

#include <QCheckBox>
#include <QComboBox>
#include <QEvent>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QSet>
#include <QSettings>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>
#include <utility>

TerminalPreferences::TerminalPreferences(QSettings *settings, QWidget *parent, Discovery discovery)
    : QWidget(parent), settings(settings), discovery(std::move(discovery))
{
    setObjectName(QStringLiteral("terminalPreferences"));
    if (!this->discovery)
        this->discovery = [](bool refresh) { return ExternalTerminals::discover(refresh); };

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(16);
    auto *description = new QLabel(tr("Open Shell opens the selected folder in a separate terminal application. It does not add a terminal panel to 8bitCode."), this);
    description->setObjectName(QStringLiteral("terminalDescription"));
    description->setWordWrap(true);
    layout->addWidget(description);

    auto *defaultGroup = new QGroupBox(tr("Default terminal"), this);
    auto *defaultLayout = new QVBoxLayout(defaultGroup);
    defaultLayout->setContentsMargins(12, 12, 12, 12);
    defaultLayout->setSpacing(10);
    auto *selectionRow = new QHBoxLayout;
    selectionRow->setSpacing(10);
    defaultTerminal = new QComboBox(defaultGroup);
    defaultTerminal->setObjectName(QStringLiteral("terminalDefaultCombo"));
    defaultTerminal->setAccessibleName(tr("Default terminal"));
    defaultTerminal->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    defaultTerminal->setMinimumContentsLength(20);
    selectionRow->addWidget(defaultTerminal, 1);
    refreshButton = new QToolButton(defaultGroup);
    refreshButton->setObjectName(QStringLiteral("terminalRefreshButton"));
    refreshButton->setText(tr("Refresh"));
    refreshButton->setToolTip(tr("Refresh detected terminal applications"));
    refreshButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    selectionRow->addWidget(refreshButton);
    defaultLayout->addLayout(selectionRow);
    availability = new QLabel(defaultGroup);
    availability->setObjectName(QStringLiteral("terminalAvailabilityLabel"));
    availability->setTextFormat(Qt::PlainText);
    availability->setWordWrap(true);
    defaultLayout->addWidget(availability);
    layout->addWidget(defaultGroup);

    auto *customGroup = new QGroupBox(tr("Custom terminal"), this);
    auto *customLayout = new QVBoxLayout(customGroup);
    customLayout->setContentsMargins(12, 12, 12, 12);
    customLayout->setSpacing(10);
    auto *customHint = new QLabel(tr("Select Custom terminal above to use this configuration."), customGroup);
    customHint->setWordWrap(true);
    customLayout->addWidget(customHint);
    auto *form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setHorizontalSpacing(18);
    form->setVerticalSpacing(10);
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    customName = new QLineEdit(customGroup);
    customName->setObjectName(QStringLiteral("terminalCustomName"));
    customName->setPlaceholderText(tr("My terminal"));
    form->addRow(tr("Display name"), customName);

    auto *programRow = new QHBoxLayout;
    programRow->setSpacing(6);
    customProgram = new QLineEdit(customGroup);
    customProgram->setObjectName(QStringLiteral("terminalCustomProgram"));
    customProgram->setAccessibleName(tr("Terminal executable"));
    customProgram->setPlaceholderText(tr("Executable path or application name"));
    programRow->addWidget(customProgram, 1);
    browseButton = new QToolButton(customGroup);
    browseButton->setObjectName(QStringLiteral("terminalBrowseButton"));
    browseButton->setText(tr("Browse…"));
    browseButton->setToolTip(tr("Choose a terminal executable"));
    browseButton->setAccessibleName(tr("Choose a terminal executable"));
    programRow->addWidget(browseButton);
    form->addRow(tr("Executable"), programRow);

    customArguments = new QPlainTextEdit(customGroup);
    customArguments->setObjectName(QStringLiteral("terminalCustomArguments"));
    customArguments->setLineWrapMode(QPlainTextEdit::NoWrap);
    customArguments->setTabChangesFocus(true);
    customArguments->setMinimumHeight(90);
    customArguments->setMaximumHeight(130);
    customArguments->setPlaceholderText(tr("One argument per line"));
    form->addRow(tr("Arguments"), customArguments);
    customLayout->addLayout(form);
    auto *argumentHint = new QLabel(tr("Each line is one argument; spaces stay in that argument. Use {folder} to insert the selected folder. Do not add outer quotes. Leave this empty if the terminal uses its starting folder."), customGroup);
    argumentHint->setObjectName(QStringLiteral("terminalArgumentsHint"));
    argumentHint->setWordWrap(true);
    customLayout->addWidget(argumentHint);
    customNewConsole = new QCheckBox(tr("Open a new console window"), customGroup);
    customNewConsole->setObjectName(QStringLiteral("terminalCustomNewConsole"));
    customNewConsole->setToolTip(tr("Enable for console programs such as cmd.exe or PowerShell. Leave disabled for graphical terminal applications."));
    customLayout->addWidget(customNewConsole);
#ifndef Q_OS_WIN
    customNewConsole->hide();
#endif
    layout->addWidget(customGroup);
    layout->addStretch(1);

    const auto saved = ExternalTerminals::readSettings(settings);
    customName->setText(saved.customName);
    customProgram->setText(saved.customProgram);
    customArguments->setPlainText(ExternalTerminals::argumentsToText(saved.customArguments));
    customNewConsole->setChecked(saved.customNewConsole);
    populateApplications(false);
    refreshIcons();

    connect(refreshButton, &QToolButton::clicked, this, &TerminalPreferences::refreshApplications);
    connect(defaultTerminal, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (index < 0 || !this->settings)
            return;
        auto current = ExternalTerminals::readSettings(this->settings);
        current.terminalId = defaultTerminal->itemData(index).toString();
        ExternalTerminals::writeSettings(this->settings, current);
        updateAvailability();
        emit preferencesChanged();
    });
    connect(customName, &QLineEdit::textChanged, this, &TerminalPreferences::saveCustomConfiguration);
    connect(customProgram, &QLineEdit::textChanged, this, &TerminalPreferences::saveCustomConfiguration);
    connect(customArguments, &QPlainTextEdit::textChanged, this, &TerminalPreferences::saveCustomConfiguration);
    connect(customNewConsole, &QCheckBox::toggled, this, &TerminalPreferences::saveCustomConfiguration);
    connect(browseButton, &QToolButton::clicked, this, [this] {
#ifdef Q_OS_WIN
        const QString filter = tr("Applications (*.exe);;All files (*)");
#else
        const QString filter = tr("All files (*)");
#endif
        const QString executable = QFileDialog::getOpenFileName(this, tr("Choose terminal executable"), customProgram->text(), filter);
        if (!executable.isEmpty())
            customProgram->setText(executable);
    });
}

QString TerminalPreferences::customDisplayName() const
{
    return customName->text().trimmed().isEmpty() ? tr("Custom terminal")
        : tr("Custom terminal — %1").arg(customName->text().trimmed());
}

void TerminalPreferences::populateApplications(bool refresh)
{
    // Discovery only inspects files/PATH; opening Preferences never starts an
    // application. Keep missing selections and never reload custom edit fields.
    applications = discovery(refresh);
    const auto saved = ExternalTerminals::readSettings(settings);
    const QSignalBlocker blocker(defaultTerminal);
    defaultTerminal->clear();
    defaultTerminal->addItem(tr("Automatic"), QStringLiteral("auto"));
    QSet<QString> ids {QStringLiteral("auto"), QStringLiteral("custom")};
    for (const auto &application : std::as_const(applications)) {
        if (application.id.isEmpty() || ids.contains(application.id))
            continue;
        ids.insert(application.id);
        defaultTerminal->addItem(application.name, application.id);
        defaultTerminal->setItemData(defaultTerminal->count() - 1, application.program, Qt::ToolTipRole);
    }
    defaultTerminal->addItem(customDisplayName(), QStringLiteral("custom"));
    if (!ids.contains(saved.terminalId) && !saved.terminalId.isEmpty())
        defaultTerminal->addItem(tr("Unavailable: %1").arg(saved.terminalId), saved.terminalId);
    const int selected = defaultTerminal->findData(saved.terminalId);
    defaultTerminal->setCurrentIndex(selected >= 0 ? selected : 0);
    updateAvailability();
}

void TerminalPreferences::refreshApplications()
{
    populateApplications(true);
}

void TerminalPreferences::saveCustomConfiguration()
{
    if (!settings)
        return;
    // Read the selected id afresh: a context menu may change the default while
    // this dialog remains open. Editing custom details must not overwrite it.
    auto current = ExternalTerminals::readSettings(settings);
    current.customName = customName->text();
    current.customProgram = customProgram->text();
    current.customArguments = ExternalTerminals::argumentsFromText(customArguments->toPlainText());
    current.customNewConsole = customNewConsole->isChecked();
    ExternalTerminals::writeSettings(settings, current);
    const int customIndex = defaultTerminal->findData(QStringLiteral("custom"));
    if (customIndex >= 0)
        defaultTerminal->setItemText(customIndex, customDisplayName());
    updateAvailability();
    emit preferencesChanged();
}

void TerminalPreferences::updateAvailability()
{
    availability->setToolTip(QString());
    auto configured = ExternalTerminals::readSettings(settings);
    configured.terminalId = defaultTerminal->currentData().toString();
    if (configured.terminalId == QStringLiteral("custom")) {
        availability->setText(configured.customProgram.trimmed().isEmpty()
            ? tr("Choose an executable for the custom terminal before using Open Shell.")
            : tr("Open Shell will use %1. The executable is checked when you open it.").arg(configured.customProgram));
        return;
    }
    const auto selected = ExternalTerminals::terminalForSettings(configured, applications);
    if (selected.program.isEmpty()) {
        availability->setText(configured.terminalId == QStringLiteral("auto")
            ? tr("No terminal application was found. Install one and refresh, or configure a custom terminal below.")
            : tr("The selected terminal is currently unavailable. Its selection has been kept; refresh after installing it or choose another application."));
    } else {
        availability->setText(tr("Opens %1 in the selected folder.").arg(selected.name));
        availability->setToolTip(selected.program);
    }
}

void TerminalPreferences::refreshIcons()
{
    if (refreshButton)
        refreshButton->setIcon(uiIcon(UiIcon::Refresh, refreshButton));
    if (browseButton)
        browseButton->setIcon(uiIcon(UiIcon::Folder, browseButton));
}

void TerminalPreferences::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::StyleChange)
        refreshIcons();
}
