# 8bitPad
**Current version: 1.0.0.** Pixel Core branding: cobalt blue pixel tile, stepped corners and charcoal text bars, matching 8bitShell.

Local build, test, portable launch and troubleshooting instructions: [doc/Building.md](doc/Building.md).

Run `8p .` or `8p some.txt` from your terminal after Windows installation. Optional version checks and updates are in **Help > Updates**; automatic installation starts disabled. See [commands and updates](doc/Updates.md).

Cross-platform release builds, bundled runtimes, curl installation, native packages and repository maintenance are documented in [doc/RELEASING.md](doc/RELEASING.md).

A native desktop text editor with a familiar Notepad++ workflow, Vi-style editing and 29 open-source color themes. Built in C++20 with Qt 6 and Scintilla/Lexilla; no browser runtime.

Click the version seven times in Preferences to reveal Blueberi, Raspberi and the Cyberpunk-inspired Silverhand, V and K themes. Unlocking is saved per profile and keeps the current theme. See [theme unlocking](doc/Updates.md#version-and-bonus-themes).

8bitPad is a GPL-3.0-or-later fork of **Notepad Next v0.15**, with its original notices retained. It is independent of the Notepad++ and Notepad Next projects. Feature coverage and remaining compatibility differences are documented in [doc/FEATURES.md](doc/FEATURES.md).

## Install and run

**Windows x64:** use the MSI for a per-user installation with a Start menu shortcut. A portable ZIP is also available; extract the complete folder and run `Start-Portable.cmd` to keep settings in its `profile` folder.

**Linux x86_64:** use the normal runtime `.tar.gz` package and its `8bitPad` launcher, or the curl installer for a per-user installation and desktop shortcut. The release workflow targets an Ubuntu 22.04 build baseline; a locally built Fedora 44 package requires a compatible newer system.

Windows, Linux and macOS curl instructions, installation paths and options are in [doc/INSTALLATION.md](doc/INSTALLATION.md). Releases are configured for `8bitPrograms/8bitPad`; download URLs become usable after the matching release assets are published.

## Everyday commands

| Action | Shortcut / location |
| --- | --- |
| New / Open / Save | Ctrl+N / Ctrl+O / Ctrl+S |
| Find / Replace | Ctrl+F / Ctrl+H |
| Find and replace in a folder | Ctrl+Shift+F |
| Command palette | Ctrl+Shift+P |
| Enable Vi mode | Ctrl+Alt+V or Settings > Vim mode |
| Change colors | Settings > Color theme > Dark / Light |
| Customize shortcuts | Settings > Keyboard shortcuts |
| Function list / document map | View menu |
| Markdown preview | Ctrl+Shift+M or explorer right-click |
| Favorite a language | Right-click a language in its group |
| File/folder operations | Explorer right-click, F2, Ctrl+C/X/V |
| DevOps profiles, templates and checks | Language > DevOps assistance |
| Convert or reopen with an encoding | Encoding menu |

Vi mode starts in NORMAL. Press `i` to insert, `Esc` to return, and `:w`, `:q`, `:wq` or `:q!` for file actions. See [doc/VIM.md](doc/VIM.md) for the supported command subset.

Themes include Ayu Dark/Mirage/Light, Catppuccin Mocha/Frappe/Macchiato/Latte, Tokyo Night, Dracula, Nord, Gruvbox, One Dark/Light, Solarized Dark/Light, GitHub Dark/Light, Kanagawa, Rose Pine/Dawn, Everforest, Monokai and Material Ocean, Blueberi and Raspberi. Credits and individual licenses are in [assets/themes/NOTICES.md](assets/themes/NOTICES.md).

See [doc/WORKSPACE.md](doc/WORKSPACE.md) for file management, favorites, Markdown preview, DevOps profiles and their boundaries.

## Files and recovery

Atomic saving keeps the previous file when a write or encoding conversion fails. Recovery snapshots are committed every minute and on normal close. Restart restores saved tabs and unsaved documents when enabled in preferences. Changes since the last successful snapshot can be lost on a sudden shutdown.

Folder replacement first shows a preview and asks for confirmation. It skips open documents and files changed since the preview, and keeps adjacent `.8bitpad.bak-*` backups. Deliberate limits protect responsiveness; see the feature coverage document.

Large documents and very long lines automatically skip expensive background word matching, document-word completion and URL scans. Manual search and language keyword completion remain available. See [doc/PERFORMANCE.md](doc/PERFORMANCE.md).

## Build on Windows

From a native Windows terminal in the repository root:

```powershell
.\build-windows.cmd -Diagnose
.\build-windows.cmd
.\build-windows.cmd -Package
```

The helper initializes Visual Studio C++ Build Tools and locates its bundled CMake/Ninja without requiring them on PATH. It discovers Qt through explicit arguments, environment variables, ignored `tooling/windows.local.json`, repo-local tooling, or a sibling SDK. A separate matching Core5Compat module can be kept in `tooling/qt-compat`. The pinned bundled dependency sources are used by default.

The executable is `build/windows-release/8bitPad.exe`. `-Package` creates a fresh timestamped portable folder under `dist`, retaining existing packages and profiles. Full prerequisites, local configuration and options are in [doc/BUILDING-WINDOWS.md](doc/BUILDING-WINDOWS.md). MSI creation and GitHub release preparation are in [doc/RELEASING.md](doc/RELEASING.md).

## Build on Linux

Use a C++20 compiler, CMake >=3.21, Ninja, Git and Qt >=6.5 development packages. Qt modules: Core, Gui, Widgets, Network, PrintSupport, Core5Compat, Concurrent, Svg, Test and LinguistTools. Qt Advanced Docking System also needs Qt private development headers on Linux.

```bash
# Fedora
sudo dnf install gcc-c++ cmake ninja-build git qt6-qtbase-devel qt6-qtbase-private-devel qt6-qt5compat-devel qt6-qttools-devel qt6-qtsvg-devel
# Ubuntu / Debian
sudo apt install build-essential cmake ninja-build git qt6-base-dev qt6-base-private-dev qt6-tools-dev qt6-tools-dev-tools qt6-5compat-dev libqt6svg6-dev
bash scripts/build-linux.sh
cmake --install build-linux --prefix "$HOME/.local"
```

For a separate SDK, pass `-DCMAKE_PREFIX_PATH=/path/to/Qt`. The build helper runs CTest; `QT_QPA_PLATFORM=offscreen` allows checks without a desktop. GitHub Actions builds and tests both platforms, packages the MSI and Linux runtime archive, and prepares a draft release for matching version tags. See [doc/RELEASING.md](doc/RELEASING.md). Hosted CI execution and public releases still require the repository to be uploaded.

## Source and licensing

See [LICENSE](LICENSE) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). The upstream project is [Notepad Next](https://github.com/dail8859/NotepadNext). Dependencies are pinned in `thirdparty/CMakeLists.txt`; bundled component source remains in `thirdparty`. Additional pinned dependency sources and offline build overrides are included in `dependency-sources`. Keep corresponding source and notices with redistributed binaries.

This version does not provide Notepad++ Windows DLL plugin compatibility, a complete Vim engine, complete UDL/settings import, or every Notepad++ preference. It is a working cross-platform editor with documented coverage, not a claim of full binary or behavior parity.
