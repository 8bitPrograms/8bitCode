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


#include "PreferencesDialog.h"
#include "NotepadNextApplication.h"
#include "TranslationManager.h"
#include "ui_PreferencesDialog.h"
#include "ScintillaNext.h"
#include "ThemeManager.h"
#include "UiIcons.h"
#include "TerminalPreferences.h"

#include <QButtonGroup>
#include <QFileDialog>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QFontMetrics>
#include <QScrollArea>
#include <QPushButton>
#include <algorithm>


PreferencesDialog::PreferencesDialog(ApplicationSettings *settings, QWidget *parent) :
    QDialog(parent, Qt::Tool),
    ui(new Ui::PreferencesDialog),
    settings(settings)
{
    ui->setupUi(this);
    // Populate the terminal page only when opened. General preferences must
    // not trigger application discovery or registry/PATH scans.
    auto *terminalPage = new QScrollArea(ui->preferencesPages);
    terminalPage->setObjectName(QStringLiteral("preferencesTerminalPage"));
    terminalPage->setWidgetResizable(true);
    terminalPage->setFrameShape(QFrame::NoFrame);
    terminalPage->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    ui->preferencesPages->addWidget(terminalPage);
    connect(ui->preferencesPages, &QStackedWidget::currentChanged, this, [this](int index) {
        if (index == 4)
            ensureTerminalPreferences();
    });
    setupCategoryNavigation();
    populateThemeComboBox();
    auto *version = new QPushButton(tr("8bitCode %1").arg(QCoreApplication::applicationVersion()), this);
    version->setObjectName(QStringLiteral("applicationVersionButton"));
    version->setFlat(true);
    version->setAccessibleName(tr("Application version"));
    ui->preferencesFooter->insertWidget(0, version);
    auto *bonusHint = new QLabel(tr("Blueberi, Raspberi, Silverhand, V and K unlocked."), this);
    bonusHint->setObjectName(QStringLiteral("bonusThemesHint"));
    bonusHint->setVisible(false);
    ui->preferencesLayout->insertWidget(ui->preferencesLayout->count() - 1, bonusHint);
    if (auto *app = qobject_cast<NotepadNextApplication *>(qApp)) {
        const QPointer<ThemeManager> themes(app->getThemeManager());
        connect(version, &QPushButton::clicked, this, [themes, bonusHint] {
            if (themes && themes->registerVersionTap()) bonusHint->show();
        });
    }

    ui->labelAppRestartIcon->hide();
    ui->labelAppRestart->hide();

    MapSettingToCheckBox(ui->checkBoxMenuBar, &ApplicationSettings::showMenuBar, &ApplicationSettings::setShowMenuBar, &ApplicationSettings::showMenuBarChanged);
    MapSettingToCheckBox(ui->checkBoxToolBar, &ApplicationSettings::showToolBar, &ApplicationSettings::setShowToolBar, &ApplicationSettings::showToolBarChanged);
    MapSettingToCheckBox(ui->checkBoxStatusBar, &ApplicationSettings::showStatusBar, &ApplicationSettings::setShowStatusBar, &ApplicationSettings::showStatusBarChanged);
    MapSettingToCheckBox(ui->checkBoxRecenterSearchDialog, &ApplicationSettings::centerSearchDialog, &ApplicationSettings::setCenterSearchDialog, &ApplicationSettings::centerSearchDialogChanged);

    MapSettingToGroupBox(ui->gbxRestorePreviousSession, &ApplicationSettings::restorePreviousSession, &ApplicationSettings::setRestorePreviousSession, &ApplicationSettings::restorePreviousSessionChanged);
    connect(ui->gbxRestorePreviousSession, &QGroupBox::toggled, this, [=, this](bool checked) {
        if (!checked) {
            ui->checkBoxUnsavedFiles->setChecked(false);
            ui->checkBoxRestoreTempFiles->setChecked(false);
        }
        else {
            QMessageBox::warning(this, tr("Warning"), tr("This feature is experimental and it should not be considered safe for critically important work. It may lead to possible data loss. Use at your own risk."));
        }
    });

    MapSettingToCheckBox(ui->checkBoxUnsavedFiles, &ApplicationSettings::restoreUnsavedFiles, &ApplicationSettings::setRestoreUnsavedFiles, &ApplicationSettings::restoreUnsavedFilesChanged);
    MapSettingToCheckBox(ui->checkBoxRestoreTempFiles, &ApplicationSettings::restoreTempFiles, &ApplicationSettings::setRestoreTempFiles, &ApplicationSettings::restoreTempFilesChanged);

    MapSettingToCheckBox(ui->checkBoxCombineSearchResults, &ApplicationSettings::combineSearchResults, &ApplicationSettings::setCombineSearchResults, &ApplicationSettings::combineSearchResultsChanged);

    populateTranslationComboBox();
    connect(ui->comboBoxTranslation, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [=, this](int index) {
        settings->setTranslation(ui->comboBoxTranslation->itemData(index).toString());
        showApplicationRestartRequired();
    });

    MapSettingToCheckBox(ui->checkBoxExitOnLastTabClosed, &ApplicationSettings::exitOnLastTabClosed, &ApplicationSettings::setExitOnLastTabClosed, &ApplicationSettings::exitOnLastTabClosedChanged);

    ui->fcbDefaultFont->setCurrentFont(QFont(settings->fontName()));
    connect(ui->fcbDefaultFont, &QFontComboBox::currentFontChanged, this, [=, this](const QFont &f) {
        if (settings->fontName() != f.family())
            settings->setFontName(f.family());
    });
    connect(settings, &ApplicationSettings::fontNameChanged, this, [=, this](QString fontName){
        // QFontComboBox may substitute/normalize the requested family. A
        // settings refresh must not write that substitute back into settings
        // and synchronously trigger another refresh.
        const QSignalBlocker blocker(ui->fcbDefaultFont);
        ui->fcbDefaultFont->setCurrentFont(QFont(fontName));
    });

    ui->spbDefaultFontSize->setValue(settings->fontSize());
    connect(ui->spbDefaultFontSize, QOverload<int>::of(&QSpinBox::valueChanged), settings, &ApplicationSettings::setFontSize);
    connect(settings, &ApplicationSettings::fontSizeChanged, ui->spbDefaultFontSize, &QSpinBox::setValue);

    ui->comboBoxLineEndings->addItem(tr("System Default"), QString(""));
    ui->comboBoxLineEndings->addItem(tr("Windows (CR LF)"), ScintillaNext::eolModeToString(SC_EOL_CRLF));
    ui->comboBoxLineEndings->addItem(tr("Linux (LF)"), ScintillaNext::eolModeToString(SC_EOL_LF));
    ui->comboBoxLineEndings->addItem(tr("Macintosh (CR)"), ScintillaNext::eolModeToString(SC_EOL_CR));

    // Select the current one
    int index = ui->comboBoxLineEndings->findData(settings->defaultEOLMode());
    ui->comboBoxLineEndings->setCurrentIndex(index == -1 ? 0 : index);

    connect(ui->comboBoxLineEndings, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [=, this](int index) {
        settings->setDefaultEOLMode(ui->comboBoxLineEndings->itemData(index).toString());
    });
    connect(settings, &ApplicationSettings::defaultEOLModeChanged, this, [=, this](QString defaultEOLMode) {
        int index = ui->comboBoxLineEndings->findData(defaultEOLMode);
        ui->comboBoxLineEndings->setCurrentIndex(index == -1 ? 0 : index);
    });

    MapSettingToCheckBox(ui->checkBoxHighlightURLs, &ApplicationSettings::urlHighlighting, &ApplicationSettings::setURLHighlighting, &ApplicationSettings::urlHighlightingChanged);
    MapSettingToCheckBox(ui->checkBoxShowLineNumbers, &ApplicationSettings::showLineNumbers, &ApplicationSettings::setShowLineNumbers, &ApplicationSettings::showLineNumbersChanged);
    MapSettingToCheckBox(ui->checkBoxAutoCompletion, &ApplicationSettings::autoCompletion, &ApplicationSettings::setAutoCompletion, &ApplicationSettings::autoCompletionChanged);

    QButtonGroup *buttonGroup = new QButtonGroup(this);
    buttonGroup->addButton(ui->radioFollowCurrentDirectory, ApplicationSettings::FollowCurrentDocument);
    buttonGroup->addButton(ui->radioLastUsedDirectory, ApplicationSettings::RememberLastUsed);
    buttonGroup->addButton(ui->radioHardCoded, ApplicationSettings::HardCoded);

    connect(buttonGroup, &QButtonGroup::idClicked, this, [=, this](int id) {
        ApplicationSettings::DefaultDirectoryBehaviorEnum e = static_cast<ApplicationSettings::DefaultDirectoryBehaviorEnum>(id);
        settings->setDefaultDirectoryBehavior(e);
    });

    connect(ui->radioHardCoded, &QRadioButton::toggled, this, [=, this](bool checked){
        ui->btnSelectHardCodedPath->setEnabled(checked);
        ui->txtHardCodedPath->setEnabled(checked);
    });

    connect(ui->btnSelectHardCodedPath, &QToolButton::clicked, this, [=, this]() {
        QString dir = QFileDialog::getExistingDirectory(this, tr("Default Directory"));
        if (dir.isEmpty()) return; // user cancelled

        settings->setDefaultDirectory(QDir::fromNativeSeparators(dir));
        ui->txtHardCodedPath->setText(QDir::toNativeSeparators(dir));
    });

    connect(ui->txtHardCodedPath, &QLineEdit::editingFinished, this, [=, this]() {
        QString dir = ui->txtHardCodedPath->text();
        settings->setDefaultDirectory(QDir::fromNativeSeparators(dir));
        ui->txtHardCodedPath->setText(QDir::toNativeSeparators(dir));
    });

    if (auto b = buttonGroup->button(settings->defaultDirectoryBehavior())) {
        b->setChecked(true);
    }

    if (settings->defaultDirectoryBehavior() == ApplicationSettings::HardCoded) {
        ui->txtHardCodedPath->setText((QDir::toNativeSeparators(settings->defaultDirectory())));
    }
    else {
        ui->txtHardCodedPath->setText(QString());
    }
    const bool customDirectory = settings->defaultDirectoryBehavior() == ApplicationSettings::HardCoded;
    ui->btnSelectHardCodedPath->setEnabled(customDirectory);
    ui->txtHardCodedPath->setEnabled(customDirectory);
    refreshPreferencesIcons();
}

PreferencesDialog::~PreferencesDialog()
{
    delete ui;
}

void PreferencesDialog::showApplicationRestartRequired() const
{
    ui->labelAppRestartIcon->show();
    ui->labelAppRestart->show();
}

void PreferencesDialog::ensureTerminalPreferences()
{
    auto *page = qobject_cast<QScrollArea *>(ui->preferencesPages->widget(4));
    if (page && !page->widget())
        page->setWidget(new TerminalPreferences(settings, page));
}

void PreferencesDialog::showTerminalSettings()
{
    ui->preferencesCategories->setCurrentRow(4);
    ensureTerminalPreferences();
    show();
    raise();
    activateWindow();
}

template<typename Func1, typename Func2, typename Func3>
void PreferencesDialog::MapSettingToCheckBox(QCheckBox *checkBox, Func1 getter, Func2 setter, Func3 notifier) const
{
    // Get the value and set the checkbox state
    checkBox->setChecked(std::bind(getter, settings)());

    // Set up two way connection
    connect(settings, notifier, checkBox, &QCheckBox::setChecked);
    connect(checkBox, &QCheckBox::toggled, settings, setter);
}

template<typename Func1, typename Func2, typename Func3>
void PreferencesDialog::MapSettingToGroupBox(QGroupBox *groupBox, Func1 getter, Func2 setter, Func3 notifier) const
{
    // Get the value and set the checkbox state
    groupBox->setChecked(std::bind(getter, settings)());

    // Set up two way connection
    connect(settings, notifier, groupBox, &QGroupBox::setChecked);
    connect(groupBox, &QGroupBox::toggled, settings, setter);
}

void PreferencesDialog::populateTranslationComboBox()
{
    NotepadNextApplication *app = qobject_cast<NotepadNextApplication *>(qApp);

    // Add the system default at the top
    ui->comboBoxTranslation->addItem(tr("<System Default>"), QStringLiteral(""));

    // TODO: sort this list and keep the system default at the top
    for (const auto &localeName : app->getTranslationManager()->availableTranslations())
    {
        QLocale locale(localeName);
        const QString localeDisplay = TranslationManager::FormatLocaleTerritoryAndLanguage(locale);
        ui->comboBoxTranslation->addItem(localeDisplay, localeName);
    }

    // Select the current one
    int index = ui->comboBoxTranslation->findData(settings->translation());
    if (index != -1) {
        ui->comboBoxTranslation->setCurrentIndex(index);
    }
}

void PreferencesDialog::setupCategoryNavigation()
{
    struct Category { QString title; QString description; };
    const QList<Category> categories = {
        {tr("General"), tr("Application language, startup and search behavior.")},
        {tr("Editor"), tr("Code font, readability and editing assistance.")},
        {tr("Files & Session"), tr("Session recovery, new files and the default folder.")},
        {tr("Appearance"), tr("Color theme and the parts of the interface you use.")},
        {tr("Terminal"), tr("Open folders in your preferred terminal application.")}
    };
    int categoryWidth = 180;
    const QFontMetrics metrics(ui->preferencesCategories->font());
    for (const auto &category : categories) {
        auto *item = new QListWidgetItem(category.title, ui->preferencesCategories);
        item->setData(Qt::UserRole, category.description);
        item->setToolTip(category.title);
        categoryWidth = std::max(categoryWidth, metrics.horizontalAdvance(category.title) + 90);
    }
    ui->preferencesCategories->setFixedWidth(categoryWidth);
    QFont headingFont = ui->preferencesHeading->font();
    headingFont.setBold(true);
    if (headingFont.pointSizeF() > 0)
        headingFont.setPointSizeF(headingFont.pointSizeF() + 2);
    else
        headingFont.setPixelSize(headingFont.pixelSize() + 2);
    ui->preferencesHeading->setFont(headingFont);
    connect(ui->preferencesCategories, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row < 0 || row >= ui->preferencesPages->count())
            return;
        ui->preferencesPages->setCurrentIndex(row);
        const auto *item = ui->preferencesCategories->item(row);
        ui->preferencesHeading->setText(item->text());
        ui->preferencesDescription->setText(item->data(Qt::UserRole).toString());
    });
    ui->preferencesCategories->setCurrentRow(0);
    ui->label->setBuddy(ui->comboBoxTranslation);
    ui->label_2->setBuddy(ui->fcbDefaultFont);
    ui->label_3->setBuddy(ui->spbDefaultFontSize);
    ui->label_4->setBuddy(ui->comboBoxLineEndings);
}

void PreferencesDialog::populateThemeComboBox()
{
    auto *app = qobject_cast<NotepadNextApplication *>(qApp);
    ThemeManager *themes = app ? app->getThemeManager() : nullptr;
    if (!themes) {
        ui->comboBoxTheme->setEnabled(false);
        return;
    }
    refreshThemeChoices();
    connect(themes, &ThemeManager::bonusThemesUnlockedChanged, this, &PreferencesDialog::refreshThemeChoices);
    const QPointer<ThemeManager> guardedThemes(themes);
    connect(ui->comboBoxTheme, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, guardedThemes](int index) {
        if (index >= 0 && guardedThemes)
            guardedThemes->setTheme(ui->comboBoxTheme->itemData(index).toString());
    });
    connect(themes, &ThemeManager::themeChanged, this, [this](const QString &) {
        refreshThemeChoices();
        refreshPreferencesIcons();
    });
}

void PreferencesDialog::refreshThemeChoices()
{
    auto *app = qobject_cast<NotepadNextApplication *>(qApp);
    auto *themes = app ? app->getThemeManager() : nullptr;
    if (!themes) return;
    const QSignalBlocker blocker(ui->comboBoxTheme);
    ui->comboBoxTheme->clear();
    for (const auto &theme : themes->availableThemes())
        ui->comboBoxTheme->addItem(theme.name, theme.id);
    ui->comboBoxTheme->setCurrentIndex(ui->comboBoxTheme->findData(themes->currentThemeId()));
}

void PreferencesDialog::refreshPreferencesIcons()
{
    const UiIcon categories[] = {UiIcon::Settings, UiIcon::Font, UiIcon::Folder, UiIcon::Theme, UiIcon::Terminal};
    ui->preferencesCategories->setIconSize(QSize(16, 16));
    for (int row = 0; row < ui->preferencesCategories->count() && row < 5; ++row)
        ui->preferencesCategories->item(row)->setIcon(uiIcon(categories[row], ui->preferencesCategories));
    ui->btnSelectHardCodedPath->setIcon(uiIcon(UiIcon::Folder, ui->btnSelectHardCodedPath));
    ui->labelAppRestartIcon->setPixmap(uiIcon(UiIcon::Info, this).pixmap(QSize(16, 16)));
    installUiIcons(this);
}
