// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 8bitPad contributors
#pragma once

#include <QColor>
#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>

class QSettings;
class LuaState;
class ScintillaNext;

struct EditorTheme
{
    QString id;
    QString name;
    bool isDark = true;
    QColor background;
    QColor foreground;
    QColor comment;
    QColor accent;
    QColor secondaryAccent;
    QColor keyword;
    QColor string;
    QColor number;
    QColor function;
    QColor type;
    QColor error;
};

// Owns presentation only: switching themes never changes documents, selections,
// undo history, lexers, fonts, indentation, or the user's editing preferences.
class ThemeManager final : public QObject
{
    Q_OBJECT

public:
    explicit ThemeManager(QSettings *settings, LuaState *lua, QObject *parent = nullptr);

    static const QList<EditorTheme> &catalog();
    static bool isBonusTheme(const QString &id);
    QList<EditorTheme> availableThemes() const;
    bool bonusThemesUnlocked() const;
    QString currentThemeId() const { return mThemeId; }
    const EditorTheme &currentTheme() const;
    QString styleSheet() const;

public slots:
    bool registerVersionTap();
    // Accepts a catalog ID or its displayed name. Unknown values are rejected.
    bool setTheme(const QString &idOrName);
    void watchEditor(ScintillaNext *editor);
    void applyEditor(ScintillaNext *editor) const;
    void setLanguageProfiles(LuaState *lua);
    void applyEmbeddedLanguage(ScintillaNext *editor, const QString &language, int styleBase) const;

signals:
    void bonusThemesUnlockedChanged();
    void themeChanged(const QString &id);
    void editorThemeApplied(ScintillaNext *editor) const;

private:
    void applyApplication() const;
    void applyLanguageStyles(ScintillaNext *editor, const QString &language,
                             int depth = 0, int styleBase = 0) const;
    QColor syntaxColor(const QString &name) const;

    QSettings *mSettings;
    LuaState *mLua;
    QString mThemeId;
    QList<QPointer<ScintillaNext>> mEditors;
    QElapsedTimer mVersionTapTimer;
    int mVersionTapCount = 0;
};
