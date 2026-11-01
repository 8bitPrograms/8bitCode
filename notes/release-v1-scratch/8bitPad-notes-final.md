# 8bitPad 1.0.0

The first release under the 8bitPad name. 8bitPad is a native desktop text and code editor with a Notepad++-style workflow. It is written in C++20 with Qt 6 and Scintilla/Lexilla, with no browser runtime. It is a GPL-3.0-or-later fork of Notepad Next v0.15.

## Highlights

- **Editing:** tabs and split views, multiple and rectangular selections, line tools, sorting, case conversion, comments, code folding, brace matching and EditorConfig support.
- **Optional Vi mode** (Ctrl+Alt+V). It covers the documented command subset, including `:w`, `:q`, `:wq` and `:q!`.
- **Search:** find and replace with literal, extended or regex patterns. Folder search and replace (Ctrl+Shift+F) runs in the background, shows a preview first and keeps `.8bitpad.bak-*` backups.
- **Command palette** (Ctrl+Shift+P), a keyboard-shortcut editor with conflict checks, and dockable Function List and Document Map panels.
- **Workspace:** folder explorer with file operations, a live Markdown preview (Ctrl+Shift+M), language favorites and contextual DevOps profiles, templates and checks.
- **29 open-source color themes**, including Catppuccin, Tokyo Night, Dracula, Nord, Gruvbox, Solarized and GitHub. Clicking the version seven times in Preferences unlocks the bonus themes.
- **Safe files:** saves are atomic, so the previous file stays when a write or encoding conversion fails. Recovery snapshots are taken every minute and on close, and restart can restore unsaved documents.
- **Encodings:** UTF-8 and UTF-16 with or without BOM, plus legacy codecs such as Windows-1254/1252, ISO-8859-9/1, Shift-JIS and GB18030. Existing encoding, BOM and line endings are kept when you save.
- **Automation:** macro recording and replay, and embedded Lua with a Lua console.
- **Updates:** optional stable-release checks and SHA-256-verified downloads are in Help > Updates. Automatic installation is off by default.
- **Windows command line:** the MSI adds `8p` to your user PATH. `8p .` and `8p some.txt` open paths in the running window. File-type associations are applied per user.
- New Pixel Core icon: a cobalt blue editor tile that matches 8bitShell.

## Install

Packages bundle Qt and the app's runtime libraries, so you don't need a Qt SDK.

**Windows (x64, ARM64)**
- `8bitPad-1.0.0-windows-<arch>.msi`: per-user install with a Start menu entry and `8p` on your PATH. Uninstalling removes the PATH entry.
- `8bitPad-1.0.0-windows-<arch>-setup.exe`: a bootstrap that runs the same MSI.
- `8bitPad-1.0.0-windows-<arch>.zip`: portable. Extract the whole folder and run `Start-Portable.cmd` to keep settings in its `profile` folder.

**Linux (x64, ARM64; glibc 2.35 or newer, e.g. Ubuntu 22.04)**
- Debian/Ubuntu: `sudo apt install ./8bitPad-1.0.0-linux-<arch>.deb`
- Fedora/RHEL: `sudo dnf install ./8bitPad-1.0.0-linux-<arch>.rpm`
- Arch: `sudo pacman -U 8bitPad-1.0.0-linux-<arch>.pkg.tar.zst`
- Any distribution: extract `8bitPad-1.0.0-linux-<arch>.tar.gz` and run `./8bitPad`.
- Per-user curl install: `curl -fsSL https://github.com/8bitPrograms/8bitPad/releases/latest/download/install.sh | sh`

**macOS (Apple Silicon and Intel, macOS 14.4 or newer)**
- Open `8bitPad-1.0.0-macos-<arch>.dmg` and drag 8bitPad to Applications. Portable `.zip` and `.tar.gz` archives are also attached.

Use `x64` or `arm64` as `<arch>`.

## Known issues

- **macOS:** the builds are ad-hoc signed and not notarized, so Gatekeeper warns on first launch. To open the app, Control-click it and choose Open, or allow it under System Settings > Privacy & Security.
- **Windows:** packages aren't Authenticode-signed yet, so SmartScreen may warn.
- Notepad++ DLL plugins aren't supported. Lua is the extension mechanism.
- Vi mode is a subset of Vim. Vimscript, Neovim plugins and full text objects aren't supported.
- Regex uses Qt's engine, so some syntax and edge cases differ from Notepad++.
- Legacy encoding detection is a heuristic and can guess wrong on short files. If that happens, use Reload as Encoding.
- Documents are held in memory, so very large files aren't guaranteed to stay fast. Folder search skips files over 32 MiB and shows at most 10,000 files and 5,000 results per preview.
- Changes made after the last recovery snapshot (taken every minute) can be lost if the system shuts down suddenly.

## Checksums

SHA-256 sums are attached as `SHA256SUMS.txt`.
