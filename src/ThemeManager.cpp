// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 8bitPad contributors
#include "ThemeManager.h"

#include <QSettings>
#include "LuaState.h"
#include "Readability.h"
#include "ScintillaNext.h"

#include <QApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPalette>
#include <QSet>
#include <QStyleFactory>
#include <QTimer>

namespace {
constexpr auto settingKey = "appearance/theme";
constexpr auto defaultTheme = "classic-light";

QColor blend(const QColor &base, const QColor &over, double amount)
{
    return QColor(qRound(base.red() * (1.0 - amount) + over.red() * amount),
                  qRound(base.green() * (1.0 - amount) + over.green() * amount),
                  qRound(base.blue() * (1.0 - amount) + over.blue() * amount));
}

// Scintilla stores RGB as 0xBBGGRR, and element colours as 0xAABBGGRR.
sptr_t sciColor(const QColor &color)
{
    return color.red() | (color.green() << 8) | (color.blue() << 16);
}

sptr_t sciOpaque(const QColor &color)
{
    return static_cast<sptr_t>(0xff000000u | static_cast<unsigned>(sciColor(color)));
}

bool containsAny(const QString &name, std::initializer_list<const char *> words)
{
    for (const char *word : words) {
        if (name.contains(QLatin1String(word)))
            return true;
    }
    return false;
}

struct Chrome
{
    QColor text;
    QColor menu;
    QColor toolbar;
    QColor tabs;
    QColor tabText;
    QColor rule;
    QColor surface;
    QColor raised;
    QColor selected;
    QColor muted;
    QColor border;
};

Chrome chromeFor(const EditorTheme &theme)
{
    Chrome chrome;
    const bool dark = theme.isDark;
    // Classic compact chrome: neutral surfaces derived from every theme, with
    // the accent reserved for selection and the active document underline.
    chrome.menu = blend(theme.background, dark ? QColor(Qt::white) : QColor(Qt::black), dark ? 0.02 : 0.015);
    chrome.toolbar = blend(theme.background, theme.foreground, dark ? 0.065 : 0.045);
    chrome.tabs = blend(theme.background, theme.foreground, dark ? 0.035 : 0.075);
    chrome.surface = blend(theme.background, theme.foreground, dark ? 0.07 : 0.04);
    chrome.raised = blend(theme.background, theme.foreground, dark ? 0.11 : 0.08);
    chrome.selected = blend(theme.background, theme.accent, dark ? 0.28 : 0.20);
    chrome.rule = blend(theme.background, theme.foreground, dark ? 0.22 : 0.20);
    chrome.border = blend(theme.background, theme.foreground, dark ? 0.30 : 0.28);
    chrome.text = theme.foreground;
    for (const QColor &surface : {chrome.menu, theme.background, chrome.toolbar, chrome.selected, chrome.raised})
        chrome.text = liftContrast(chrome.text, surface, 4.5);
    chrome.tabText = liftContrast(theme.foreground, chrome.tabs, 4.5);
    chrome.muted = liftContrast(blend(theme.foreground, theme.background, 0.35), chrome.menu, 3.2);
    return chrome;
}
}

const QList<EditorTheme> &ThemeManager::catalog()
{
    static const QList<EditorTheme> themes = [] {
        QFile file(QStringLiteral(":/themes/catalog.json"));
        if (!file.open(QIODevice::ReadOnly))
            qFatal("8bitPad theme catalog is missing from the application resources");

        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isArray())
            qFatal("Invalid 8bitPad theme catalog: %s", qPrintable(parseError.errorString()));

        QList<EditorTheme> result;
        QSet<QString> ids;
        for (const QJsonValue &value : document.array()) {
            const auto object = value.toObject();
            EditorTheme theme;
            theme.id = object.value(QStringLiteral("id")).toString();
            theme.name = object.value(QStringLiteral("name")).toString();
            theme.isDark = object.value(QStringLiteral("dark")).toBool(true);
            const auto color = [&object, &theme](const char *key) {
                QColor value(object.value(QLatin1String(key)).toString());
                if (!value.isValid())
                    qFatal("Invalid color %s in theme %s", key, qPrintable(theme.id));
                return value;
            };
            theme.background = color("background");
            theme.foreground = color("foreground");
            theme.comment = color("comment");
            theme.accent = color("accent");
            theme.secondaryAccent = object.contains(QStringLiteral("secondaryAccent"))
                ? color("secondaryAccent") : theme.accent;
            theme.keyword = color("keyword");
            theme.string = color("string");
            theme.number = color("number");
            theme.function = color("function");
            theme.type = color("type");
            theme.error = color("error");
            if (theme.id.isEmpty() || theme.name.isEmpty() || ids.contains(theme.id))
                qFatal("Theme names and IDs must be present and unique");
            ids.insert(theme.id);
            result.append(theme);
        }
        if (!ids.contains(QLatin1String(defaultTheme)))
            qFatal("The default Classic Light theme is missing");
        return result;
    }();
    return themes;
}

ThemeManager::ThemeManager(QSettings *settings, LuaState *lua, QObject *parent)
    : QObject(parent), mSettings(settings), mLua(lua)
{
    // Fusion consistently observes the application palette on Windows and Linux.
    // Platform fonts, file dialogs, DPI scaling, and window decorations stay native.
    if (QStyle *fusion = QStyleFactory::create(QStringLiteral("Fusion")))
        QApplication::setStyle(fusion);

    QString requested = settings->value(QLatin1String(settingKey),
                                       QLatin1String(defaultTheme)).toString();
    if (!setTheme(requested))
        setTheme(QLatin1String(defaultTheme));
}

bool ThemeManager::isBonusTheme(const QString &id)
{
    return id == "blueberry" || id == "blueberry-light" || id.startsWith("cyberpunk-");
}

bool ThemeManager::bonusThemesUnlocked() const
{
    return mSettings->value("appearance/bonusThemesUnlocked", false).toBool();
}

QList<EditorTheme> ThemeManager::availableThemes() const
{
    QList<EditorTheme> result;
    for (const auto &theme : catalog())
        if (bonusThemesUnlocked() || !isBonusTheme(theme.id))
            result.append(theme);
    return result;
}

bool ThemeManager::registerVersionTap()
{
    if (bonusThemesUnlocked()) return false;
    if (!mVersionTapTimer.isValid() || mVersionTapTimer.elapsed() > 1500)
        mVersionTapCount = 0;
    mVersionTapTimer.start();
    if (++mVersionTapCount < 7) return false;
    mVersionTapCount = 0;
    mSettings->setValue("appearance/bonusThemesUnlocked", true);
    mSettings->sync();
    emit bonusThemesUnlockedChanged();
    return true;
}

const EditorTheme &ThemeManager::currentTheme() const
{
    for (const auto &theme : catalog()) {
        if (theme.id == mThemeId)
            return theme;
    }
    // The constructor validates the catalog and sets a known theme before use.
    return catalog().first();
}

bool ThemeManager::setTheme(const QString &idOrName)
{
    const EditorTheme *selected = nullptr;
    for (const auto &theme : catalog()) {
        if (theme.id.compare(idOrName, Qt::CaseInsensitive) == 0 ||
            theme.name.compare(idOrName, Qt::CaseInsensitive) == 0) {
            selected = &theme;
            break;
        }
    }
    if (!selected)
        return false;
    if (mThemeId == selected->id)
        return true;

    mThemeId = selected->id;
    mSettings->setValue(QLatin1String(settingKey), mThemeId);
    applyApplication();
    for (auto it = mEditors.begin(); it != mEditors.end();) {
        if (it->isNull()) {
            it = mEditors.erase(it);
        } else {
            applyEditor(it->data());
            ++it;
        }
    }
    emit themeChanged(mThemeId);
    return true;
}

void ThemeManager::watchEditor(ScintillaNext *editor)
{
    if (!editor)
        return;
    for (const auto &existing : mEditors) {
        if (existing == editor)
            return;
    }
    mEditors.append(QPointer<ScintillaNext>(editor));

    // setEditorLanguage emits lexerChanged before its Lua styles are applied.
    // Queue this repaint after that call completes; never replace the lexer.
    const QPointer<ScintillaNext> guarded(editor);
    connect(editor, &ScintillaNext::lexerChanged, this, [this, guarded] {
        if (guarded)
            applyEditor(guarded.data());
    }, Qt::QueuedConnection);
    applyEditor(editor);
    QTimer::singleShot(0, this, [this, guarded] {
        if (guarded)
            applyEditor(guarded.data());
    });
}

void ThemeManager::applyApplication() const
{
    const auto &t = currentTheme();
    const Chrome chrome = chromeFor(t);
    const QColor text = chrome.text;

    QPalette palette;
    palette.setColor(QPalette::Window, chrome.menu);
    palette.setColor(QPalette::WindowText, text);
    palette.setColor(QPalette::Base, t.background);
    palette.setColor(QPalette::AlternateBase, chrome.surface);
    palette.setColor(QPalette::Text, text);
    palette.setColor(QPalette::Button, chrome.toolbar);
    palette.setColor(QPalette::ButtonText, text);
    palette.setColor(QPalette::BrightText, liftContrast(t.error, t.background, 4.5));
    palette.setColor(QPalette::Highlight, chrome.selected);
    palette.setColor(QPalette::HighlightedText, text);
    palette.setColor(QPalette::Link, liftContrast(t.function, t.background, 4.5));
    palette.setColor(QPalette::LinkVisited, liftContrast(t.keyword, t.background, 4.5));
    palette.setColor(QPalette::ToolTipBase, chrome.raised);
    palette.setColor(QPalette::ToolTipText, text);
    palette.setColor(QPalette::PlaceholderText, chrome.muted);
    palette.setColor(QPalette::Light, chrome.border);
    palette.setColor(QPalette::Midlight, chrome.raised);
    palette.setColor(QPalette::Mid, chrome.border);
    palette.setColor(QPalette::Dark, chrome.rule);
    palette.setColor(QPalette::Shadow, chrome.rule);
    for (auto role : {QPalette::Text, QPalette::WindowText, QPalette::ButtonText})
        palette.setColor(QPalette::Disabled, role, chrome.muted);
    QApplication::setPalette(palette);
    qApp->setStyleSheet(styleSheet());
}

QString ThemeManager::styleSheet() const
{
    const auto &t = currentTheme();
    const Chrome chrome = chromeFor(t);
    QString sheet = QStringLiteral(R"QSS(
QMainWindow, QDialog, QMessageBox { background: @menu; }
QMenuBar { background: @menu; color: @text; border: none; border-bottom: 1px solid @rule; }
QMenuBar::item { background: transparent; color: @text; padding: 4px 10px; }
QMenuBar::item:selected { background: @selected; }
QMenu { background: @menu; color: @text; border: 1px solid @rule; padding: 2px; }
QMenu::item { padding: 5px 28px 5px 22px; }
QMenu::item:selected { background: @selected; color: @text; }
QMenu::item:disabled { color: @muted; }
QMenu::separator { height: 1px; background: @rule; margin: 4px 8px; }
QToolBar, QToolBar#mainToolBar { background: @toolbar; color: @text; border: none; border-bottom: 1px solid @rule; spacing: 2px; padding: 2px 4px; }
QToolBar::separator { background: @rule; width: 1px; margin: 3px 4px; }
QToolButton { color: @text; background: transparent; border: 1px solid transparent; padding: 3px; border-radius: 0; }
QToolButton:hover, QToolButton:checked { background: @selected; border-color: @rule; }
QPushButton { background: @raised; color: @text; border: 1px solid @rule; padding: 4px 12px; border-radius: 0; }
QPushButton:hover { background: @selected; }
QPushButton:default, QPushButton:focus { border-color: @accent; }
QPushButton:disabled { color: @muted; }
QLineEdit, QTextEdit, QPlainTextEdit, QAbstractSpinBox, QComboBox {
    background: @background; color: @text; border: 1px solid @rule; border-radius: 0;
    padding: 3px; selection-background-color: @selected; selection-color: @text;
}
QLineEdit:focus, QTextEdit:focus, QPlainTextEdit:focus, QAbstractSpinBox:focus, QComboBox:focus { border-color: @accent; }
QComboBox QAbstractItemView, QAbstractItemView, QTreeView, QListView {
    background: @background; alternate-background-color: @surface;
    selection-background-color: @selected; selection-color: @text; border: none; outline: none;
}
QHeaderView::section { background: @toolbar; color: @text; padding: 5px 7px; border: none; border-right: 1px solid @rule; border-bottom: 1px solid @rule; }
QTabWidget::pane { border: 1px solid @rule; }
QTabBar::tab { background: @tabs; color: @tabText; padding: 6px 12px; border: 1px solid @rule; border-bottom: none; border-radius: 0; }
QTabBar::tab:selected { background: @background; color: @text; border-bottom: 2px solid @accent; }
QTabBar::tab:hover { background: @raised; color: @text; }
QCheckBox, QRadioButton, QLabel, QGroupBox { color: @text; }
QCheckBox, QRadioButton { spacing: 7px; }
QCheckBox:disabled, QRadioButton:disabled, QLabel:disabled { color: @muted; }
QListWidget#preferencesCategories { background: @toolbar; border: none; border-right: 1px solid @rule; padding: 6px; }
QListWidget#preferencesCategories::item { padding: 9px 10px; margin: 1px 0; }
QListWidget#preferencesCategories::item:selected { background: @selected; color: @text; border-left: 2px solid @accent; }
QLabel#preferencesHeading { font-size: 15px; font-weight: 600; padding-bottom: 6px; }
QTreeView#treeView::item { min-height: 23px; }
QTreeView#associationTree::indicator:unchecked { width: 12px; height: 12px; border: 1px solid @muted; background: @background; }
QGroupBox { border: 1px solid @rule; border-radius: 0; margin-top: 10px; padding-top: 8px; }
QGroupBox::title { subcontrol-origin: margin; left: 8px; color: @text; padding: 0 4px; }
QSplitter::handle { background: @rule; }
QMainWindow::separator { background: @rule; width: 2px; height: 2px; }
QDockWidget { border: 1px solid @rule; }
QDockWidget::title { background: @menu; color: @text; padding: 4px 6px; border-bottom: 1px solid @rule; }
QStatusBar { background: @menu; color: @text; border-top: 1px solid @rule; min-height: 22px; }
QStatusBar::item { border: none; }
QToolTip { background: @raised; color: @text; border: 1px solid @rule; padding: 4px; }
QScrollBar:vertical { background: @tabs; width: 12px; margin: 0; }
QScrollBar:horizontal { background: @tabs; height: 12px; margin: 0; }
QScrollBar::handle { background: @rule; border: 2px solid @tabs; border-radius: 0; }
QScrollBar::handle:vertical { min-height: 24px; }
QScrollBar::handle:horizontal { min-width: 24px; }
QScrollBar::handle:hover { background: @muted; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
ads--CDockContainerWidget, ads--CDockAreaWidget, ads--CDockWidget { background: @background; border: none; }
ads--CDockAreaTitleBar { background: @tabs; border-top: 1px solid @rule; border-bottom: 1px solid @rule; }
ads--CDockAreaTabBar, #tabsContainerWidget { background: @tabs; border: none; }
ads--CDockWidgetTab { background: @tabs; color: @tabText; border: none; border-right: 1px solid @rule; border-bottom: 2px solid transparent; border-radius: 0; padding: 5px 9px; margin: 0; }
ads--CDockWidgetTab[activeTab="true"] { background: @background; color: @text; border-bottom: 2px solid @accent; }
ads--CDockWidgetTab QLabel { color: @tabText; background: transparent; }
ads--CDockWidgetTab[activeTab="true"] QLabel { color: @text; }
ads--CDockWidgetTab:hover { background: @raised; }
ads--CDockAreaTitleBar QToolButton { background: transparent; color: @tabText; border-radius: 0; }
#QuickFindWidget { background: @toolbar; border: none; border-bottom: 1px solid @rule; }
#tabCloseButton { background: transparent; border: none; padding: 2px; }
#tabCloseButton:hover { background: @selected; }
)QSS");
    if (t.id.startsWith(QLatin1String("cyberpunk-"))) {
        // These three palettes use the concept's saturated chrome as well as
        // its editor colors. Keep the compact layout and readable code surface.
        sheet += QStringLiteral(R"QSS(
QMenuBar { background: @accent; color: @onAccent; border-bottom: 2px solid @secondaryAccent; }
QMenuBar::item { color: @onAccent; }
QMenuBar::item:selected { background: @accentHover; color: @onAccent; }
QMenuBar QToolBar, QMenuBar QToolButton { background: @accent; color: @onAccent; border: none; }
QMenuBar QToolButton:hover { background: @accentHover; }
QToolBar#mainToolBar { border-bottom: 1px solid @secondaryAccent; }
QDockWidget::title { background: @accentSurface; color: @secondaryAccent; border-bottom: 1px solid @secondaryAccent; }
QTabBar::tab:selected { background: @accent; color: @onAccent; border-bottom: 2px solid @accent; }
ads--CDockWidgetTab[activeTab="true"], ads--CDockWidgetTab[activeTab="true"]:hover {
    background: @accent; color: @onAccent; border-bottom: 2px solid @accent;
}
ads--CDockWidgetTab[activeTab="true"] QLabel { color: @onAccent; background: @background; }
ads--CDockWidgetTab[activeTab="true"] QLabel#dockWidgetTabLabel,
ads--CDockWidgetTab[activeTab="true"] QToolButton { color: @onAccent; background: transparent; }
QStatusBar { background: @accentSurface; color: @accent; border-top: 2px solid @accent; }
QStatusBar QLabel { color: @accent; }
QLabel#preferencesHeading { color: @secondaryAccent; }
QListWidget#preferencesCategories::item:selected { background: @accent; color: @onAccent; border-left: 2px solid @secondaryAccent; }
QListWidget#preferencesCategories { selection-background-color: @accent; selection-color: @onAccent; }
QLineEdit:focus, QTextEdit:focus, QPlainTextEdit:focus, QAbstractSpinBox:focus, QComboBox:focus { border-color: @secondaryAccent; }
QPushButton:default { background: @accent; color: @onAccent; border-color: @accent; }
)QSS");
        sheet.replace(QLatin1String("@secondaryAccent"), t.secondaryAccent.name());
        sheet.replace(QLatin1String("@onAccent"), liftContrast(t.background, t.accent, 4.5).name());
        sheet.replace(QLatin1String("@accentHover"), blend(t.accent, QColor(Qt::white), 0.25).name());
        sheet.replace(QLatin1String("@accentSurface"), blend(t.background, t.accent, 0.10).name());
    }
    sheet.replace(QLatin1String("@background"), t.background.name());
    sheet.replace(QLatin1String("@menu"), chrome.menu.name());
    sheet.replace(QLatin1String("@toolbar"), chrome.toolbar.name());
    sheet.replace(QLatin1String("@tabs"), chrome.tabs.name());
    sheet.replace(QLatin1String("@tabText"), chrome.tabText.name());
    sheet.replace(QLatin1String("@surface"), chrome.surface.name());
    sheet.replace(QLatin1String("@raised"), chrome.raised.name());
    sheet.replace(QLatin1String("@selected"), chrome.selected.name());
    sheet.replace(QLatin1String("@rule"), chrome.rule.name());
    sheet.replace(QLatin1String("@muted"), chrome.muted.name());
    sheet.replace(QLatin1String("@text"), chrome.text.name());
    sheet.replace(QLatin1String("@accent"), t.accent.name());
    return sheet;
}

QColor ThemeManager::syntaxColor(const QString &styleName) const
{
    const auto &t = currentTheme();
    const QString name = styleName.toUpper();
    QColor color = t.foreground;
    if (containsAny(name, {"COMMENT", "DOCBLOCK"})) color = t.comment;
    else if (containsAny(name, {"ERROR", "ILLEGAL", "INVALID", "BAD", "DELETED", "REMOVED"})) color = t.error;
    else if (containsAny(name, {"ADDED", "INSERTED", "STRING", "CHARACTER", "VERBATIM", "TRIPLE", "QUOTE", "REGEX", "RAWSTRING", "HEREDOC"})) color = t.string;
    else if (containsAny(name, {"NUMBER", "NUMERIC", "HEXNUMBER", "OCTAL", "BINARY", "FLOAT"})) color = t.number;
    else if (containsAny(name, {"PREPROCESS", "DECORATOR", "ANNOTATION", "DIRECTIVE"})) color = t.accent;
    else if (containsAny(name, {"TYPE", "CLASS", "BUILTIN", "CONSTANT", "ENUM"})) color = t.type;
    else if (containsAny(name, {"FUNCTION", "DEFNAME", "METHOD", "HEADER", "HEADING", "LABEL"})) color = t.function;
    else if (containsAny(name, {"KEYWORD", "INSTRUCTION", "RESERVED", "WORD", "TAG", "BOOLEAN"})) color = t.keyword;
    else if (containsAny(name, {"OPERATOR", "PUNCTUATION", "SYMBOL", "BRACE"})) color = t.accent;
    else if (containsAny(name, {"LINK", "URL", "COMMAND"})) color = t.function;
    return liftContrast(color, t.background, 4.5);
}

void ThemeManager::applyLanguageStyles(ScintillaNext *editor, const QString &language, int depth, int styleBase) const
{
    if (!mLua || !mLua->L || language.isEmpty() || depth > 8)
        return;
    lua_State *state = mLua->L;
    const int originalTop = lua_gettop(state);
    lua_getglobal(state, "languages");
    if (!lua_istable(state, -1)) {
        lua_settop(state, originalTop);
        return;
    }
    const QByteArray name = language.toUtf8();
    lua_getfield(state, -1, name.constData());
    if (!lua_istable(state, -1)) {
        lua_settop(state, originalTop);
        return;
    }
    const int languageIndex = lua_absindex(state, -1);
    lua_getfield(state, languageIndex, "styles");
    if (lua_istable(state, -1)) {
        lua_pushnil(state);
        while (lua_next(state, -2)) {
            // Do not coerce numeric table keys: lua_next requires an unchanged
            // key on the stack, and lua_tostring would replace a numeric key.
            if (lua_type(state, -2) == LUA_TSTRING && lua_istable(state, -1)) {
                const QString styleName = QString::fromUtf8(lua_tostring(state, -2));
                lua_getfield(state, -1, "id");
                const lua_Integer rawId = lua_tointeger(state, -1);
                const bool validId = lua_isinteger(state, -1) && rawId >= 0 && rawId <= STYLE_MAX;
                const int id = validId ? static_cast<int>(rawId) + styleBase : 0;
                lua_pop(state, 1);
                if (validId && id <= STYLE_MAX) {
                    editor->styleSetFore(id, sciColor(syntaxColor(styleName)));
                    lua_getfield(state, -1, "fontStyle");
                    const int fontStyle = static_cast<int>(lua_tointeger(state, -1));
                    lua_pop(state, 1);
                    editor->styleSetBold(id, (fontStyle & 1) != 0);
                    editor->styleSetItalic(id, (fontStyle & 2) != 0);
                    editor->styleSetUnderline(id, (fontStyle & 4) != 0);
                    editor->styleSetEOLFilled(id, (fontStyle & 8) != 0);
                }
            }
            lua_pop(state, 1);
        }
    }
    lua_pop(state, 1);
    QStringList embedded;
    lua_getfield(state, languageIndex, "additionalLanguages");
    if (lua_istable(state, -1)) {
        lua_pushnil(state);
        while (lua_next(state, -2)) {
            if (lua_isstring(state, -1))
                embedded.append(QString::fromUtf8(lua_tostring(state, -1)));
            lua_pop(state, 1);
        }
    }
    lua_settop(state, originalTop);
    for (const QString &child : embedded)
        applyLanguageStyles(editor, child, depth + 1, styleBase);
}

void ThemeManager::applyEditor(ScintillaNext *editor) const
{
    if (!editor)
        return;
    const auto &t = currentTheme();
    const Chrome chrome = chromeFor(t);
    const QColor text = chrome.text;
    const QColor surface = chrome.surface;
    const QColor border = chrome.border;
    const QColor muted = liftContrast(chrome.muted, surface, 4.5);
    const QColor selected = chrome.selected;
    // updatesEnabled() includes temporary suppression inherited from an
    // ancestor while Qt repolishes a stylesheet. Preserve only the editor's
    // own flag; restoring an inherited false would explicitly disable this
    // child forever after the ancestor resumes painting.
    const bool explicitlyDisabled = editor->testAttribute(Qt::WA_ForceUpdatesDisabled);
    editor->setUpdatesEnabled(false);

    // Recolour style slots, not document bytes. Do not call styleClearAll(): it
    // would also reset fonts and language-specific formatting chosen by users.
    for (int id = 0; id <= STYLE_MAX; ++id) {
        editor->styleSetBack(id, sciColor(t.background));
        editor->styleSetFore(id, sciColor(text));
    }
    // Lexilla tags cover newer lexer styles absent from the Lua catalog.
    const int styles = qMin(static_cast<int>(editor->namedStyles()), STYLE_MAX + 1);
    for (int id = 0; id < styles; ++id) {
        const QString metadata = QString::fromUtf8(editor->nameOfStyle(id)) + QLatin1Char(' ') +
                                 QString::fromUtf8(editor->tagsOfStyle(id));
        editor->styleSetFore(id, sciColor(syntaxColor(metadata)));
    }
    applyLanguageStyles(editor, editor->languageName);

    editor->styleSetFore(STYLE_LINENUMBER, sciColor(muted));
    editor->styleSetBack(STYLE_LINENUMBER, sciColor(surface));
    editor->styleSetFore(STYLE_INDENTGUIDE, sciColor(border));
    editor->styleSetFore(STYLE_BRACELIGHT, sciColor(t.accent));
    editor->styleSetBack(STYLE_BRACELIGHT, sciColor(selected));
    editor->styleSetFore(STYLE_BRACEBAD, sciColor(t.error));
    editor->styleSetBack(STYLE_BRACEBAD, sciColor(blend(t.background, t.error, 0.18)));
    editor->styleSetFore(STYLE_CALLTIP, sciColor(text));
    editor->styleSetBack(STYLE_CALLTIP, sciColor(surface));
    editor->setFoldMarginColour(true, sciColor(surface));
    editor->setFoldMarginHiColour(true, sciColor(surface));
    for (int marker = SC_MARKNUM_FOLDEREND; marker <= SC_MARKNUM_FOLDEROPEN; ++marker) {
        editor->markerSetFore(marker, sciColor(surface));
        editor->markerSetBack(marker, sciColor(muted));
        editor->markerSetBackSelected(marker, sciColor(t.accent));
    }
    editor->setElementColour(SC_ELEMENT_SELECTION_TEXT, sciOpaque(text));
    editor->setElementColour(SC_ELEMENT_SELECTION_BACK, sciOpaque(selected));
    editor->setElementColour(SC_ELEMENT_SELECTION_ADDITIONAL_TEXT, sciOpaque(text));
    editor->setElementColour(SC_ELEMENT_SELECTION_ADDITIONAL_BACK, sciOpaque(selected));
    editor->setElementColour(SC_ELEMENT_SELECTION_SECONDARY_TEXT, sciOpaque(text));
    editor->setElementColour(SC_ELEMENT_SELECTION_SECONDARY_BACK, sciOpaque(selected));
    editor->setElementColour(SC_ELEMENT_SELECTION_INACTIVE_TEXT, sciOpaque(text));
    editor->setElementColour(SC_ELEMENT_SELECTION_INACTIVE_BACK, sciOpaque(blend(t.background, t.foreground, 0.13)));
    editor->setElementColour(SC_ELEMENT_CARET, sciOpaque(t.accent));
    editor->setElementColour(SC_ELEMENT_CARET_ADDITIONAL, sciOpaque(t.accent));
    editor->setElementColour(SC_ELEMENT_CARET_LINE_BACK, sciOpaque(surface));
    editor->setElementColour(SC_ELEMENT_WHITE_SPACE, sciOpaque(border));
    editor->setElementColour(SC_ELEMENT_FOLD_LINE, sciOpaque(border));
    editor->setEdgeColour(sciColor(border));
    editor->callTipSetBack(sciColor(surface));
    editor->callTipSetFore(sciColor(text));
    editor->callTipSetForeHlt(sciColor(t.accent));
    editor->setHotspotActiveFore(true, sciColor(t.function));
    editor->setUpdatesEnabled(!explicitlyDisabled);
    if (editor->updatesEnabled())
        editor->viewport()->update();
    emit editorThemeApplied(editor);
}

void ThemeManager::setLanguageProfiles(LuaState *lua)
{
    if (mLua == lua) return;
    mLua = lua;
    for (const auto &editor : std::as_const(mEditors))
        if (editor) applyEditor(editor.data());
}

void ThemeManager::applyEmbeddedLanguage(ScintillaNext *editor, const QString &language, int styleBase) const
{
    if (editor && styleBase >= 0 && styleBase <= STYLE_MAX)
        applyLanguageStyles(editor, language, 0, styleBase);
}
