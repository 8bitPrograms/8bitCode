# Shared editor consumers

`EightBit::Editor` supplies source document I/O, Scintilla/Lexilla, Lua language resources, theme presentation, Markdown rendering and modal editing. It is a library in this repository, not another application. Code and Notes pin/use the same implementation.

Consumers enable C++20, Qt AUTOMOC and AUTORCC, provide the four vendored dependencies, and link `EightBit::Editor` once per final binary. Lua and theme resource collections are interface sources so AUTORCC compiles them in each consumer; this also works with CMake 3.28, which omits generated resource objects from a forwarded `TARGET_OBJECTS` expression. The regex callback remains a directly forwarded object.

`ThemeManager` borrows the consumer's `QSettings` and `LuaState`. Its settings therefore remain in that application's profile. The consumer registers editors with `watchEditor`; Code connects its editor-manager signal at its application boundary. `setLanguageProfiles` permits lazy Lua initialization. The `editorThemeApplied` signal and `applyEmbeddedLanguage` let Notes colour borrowed fence style slots with the same semantic rules as source tabs. Theme switching does not replace a lexer or alter text, caret or undo history.

`MarkdownRenderer` and `VimMode` have no product-settings or window dependency. Product code owns preview resource policy, vault resolution and Vim save/close commands.

Standalone checks, with the Qt prefix supplied by the developer environment:

```sh
cmake -S tests/shared -B build/shared-checks -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/shared-checks
ctest --test-dir build/shared-checks --output-on-failure
```

The standalone suite covers editor encoding/BOM/EOL round trips, consumer-owned theme settings and unlocking, live/embedded style parity, and the existing Vim regressions. Full Code application and Notes integration checks remain separate; Windows release validation is still required.

Validation checkpoint: Qt 6.8.3/GCC 13.3 Linux Release build passed all 17 Code CTest suites, including ApplicationSmoke (21.18 seconds total). The isolated shared-library build passed its three suites (0.14 seconds). This is development validation with runtime bundling disabled; no Windows package or release was produced.
