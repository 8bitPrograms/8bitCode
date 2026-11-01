# Themes

8bitPad starts with **Classic Light**: a plain white editor, dark text and neutral grey controls. Choose a theme in the application's appearance
menu; the choice is saved in `appearance/theme` in 8bitPad's own settings. An
unknown saved theme falls back to Classic Light. Existing valid theme choices are preserved.

The 26 bundled themes are Classic Light, Ayu Mirage, Ayu Dark, Ayu Light, Blueberi, Raspberi,
Catppuccin Mocha, Catppuccin Frappé, Catppuccin Macchiato, Catppuccin Latte,
Dracula, Nord, Tokyo Night, Gruvbox Dark, Solarized Dark, Solarized Light,
One Dark, One Light, Monokai, GitHub Dark, GitHub Light, Everforest, Rosé Pine,
Rosé Pine Dawn, Kanagawa, and Material Ocean.

Themes cover application windows, menus, toolbars, tabs, docks, dialogs, search
fields, scrollbars, editor text, syntax, line numbers, fold markers, selection,
carets, the current line, call tips, and brace matching. Qt Fusion provides
consistent widget colors on Windows and Linux; operating-system title bars and
native file dialogs retain their platform appearance. Qt handles display scaling.

Syntax colors are assigned by semantic names in the existing Lua language
definitions, supplemented by Lexilla style metadata. Embedded languages such as
JavaScript/PHP in HTML are included. This is an adaptation of each palette, not a
byte-for-byte import of every upstream editor's syntax rules. Comments and a few
light-theme colors are adjusted for readability.

Switching themes updates existing editors and future tabs. Language changes queue
a recolor after the existing Lua language setup. The implementation changes style
attributes only: text, file encoding, selections, undo history, indentation,
fonts, font sizes, and document lexers remain intact. A theme change does not
reparse a document or rewrite its buffer. Custom syntax colors made through the
language inspector are replaced by the next theme/language application.

See [theme credits](../src/editor/themes/NOTICES.md) and the bundled full license
notices in `src/editor/themes/licenses/`.

## Integration

Add `src/ThemeManager.h` and `src/ThemeManager.cpp` to the application target
and link `EightBit::Editor`, which carries `src/editor/themes/themes.qrc`. Create a single manager
after the Lua state and `EditorManager` are initialized, before opening editors:

```cpp
themeManager = new ThemeManager(settings, luaState, editorManager, this);
```

The manager automatically observes `EditorManager::editorCreated`. If attaching
it to already open editors, call `watchEditor(editor)` for each existing editor.
Connect appearance-menu actions to `setTheme(theme.id)` and use `themeChanged`
to synchronize action checkmarks across application windows.

```cpp
for (const EditorTheme &theme : ThemeManager::catalog()) {
    // theme.id, theme.name and theme.isDark are available to build the menu.
}
```

`MainWindow::applyStyleSheet()` must use `themeManager->styleSheet()` as its base
instead of the upstream `npp.css`; its hardcoded gray/black tab rules would
otherwise override the theme. Append the user's `custom.css` afterwards and
reapply the window stylesheet when `themeChanged` fires. The manager also sets
the global application palette and stylesheet so standalone dialogs are themed.

`applyEditor(editor)` can be used after an operation that intentionally replaces
editor style colors. It preserves the Lua stack and does not change Lua's
current editor binding. It also preserves whether widget updates were disabled.

## Validation boundaries

The JSON catalog is validated at application startup for unique IDs, names,
well-formed colors, and the default theme. The resource is compiled into the
application, so theme loading does not depend on its working directory or a
network connection. `src/editor/themes/validate_catalog.py` checks every palette,
license reference, resource entry, and minimum body-text contrast offline.

Visual acceptance should include both a light and dark theme on Windows and
Linux, especially native dialogs, scaling, toolbar icons, dock tabs, C++/Python
syntax, embedded HTML languages, and selection/brace colors. Static catalog
validation alone is not physical Linux desktop validation.
