# Third-party notices

8bitPad is a GPL-3.0-or-later derivative of **Notepad Next v0.15**, commit
`fc4c807cf2b755b37d40de27cf29a5268c7c2fc5`, by Justin Dailey and contributors.
Original copyright notices and the full GPL text are retained. 8bitPad is
independent of the Notepad++ and Notepad Next projects.

Source: https://github.com/dail8859/NotepadNext

The distribution includes or links these open-source components:

| Component | License / notice location |
| --- | --- |
| Qt 6 (Core, Gui, Widgets, Network, PrintSupport, Core5Compat, SVG) | LGPL-3.0 / GPL-3.0; dynamically linked. https://www.qt.io/licensing/open-source-lgpl-obligations |
| Scintilla and Lexilla | Permissive Scintilla license; `thirdparty/scintilla/License.txt` and `thirdparty/lexilla/License.txt` |
| Lua | MIT; `thirdparty/lua` |
| uchardet | MPL-1.1 / GPL-2.0-or-later / LGPL-2.1-or-later; `thirdparty/uchardet/COPYING` |
| Qt Advanced Docking System v5.1.1 | LGPL-2.1-or-later; `licenses/ads/LICENSE` |
| SingleApplication v3.3.4 | MIT; `licenses/singleapplication/LICENSE` |
| editorconfig-core-qt | MIT; dependency's LICENSE file |
| Editor theme palettes | See `src/editor/themes/NOTICES.md` and retained upstream license files |

All application additions are licensed under GPL-3.0-or-later. Theme palettes
are distributed with their own upstream notices. The Blueberi and Raspberi
palette mappings are independently authored 8bitPad data matching the user's
palette preferences; no unlicensed implementation from another project is included.

Windows bundles may include Microsoft's Visual C++ runtime redistributables,
under Microsoft's redistributable license. Linux system packages remain under
their individual distribution licenses. No proprietary Notepad++ plugin binary
is included.

When redistributing a binary, provide the corresponding 8bitPad source, retained
notices and dependency versions. Replacing the shared Qt libraries remains
possible; this application does not prevent library replacement or reverse
engineering for debugging such modifications.
