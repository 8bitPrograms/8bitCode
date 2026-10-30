# Installing 8bitPad 1.0

The new GitHub repository and published release do not exist yet. Replace ORG/REPOSITORY below after setup. Draft releases are unavailable through the latest-download URL.

| System | Standard CPUs | Formats |
| --- | --- | --- |
| Windows | x64, ARM64 | MSI, EXE, ZIP, tar.gz |
| Linux | x64, ARM64 | tar.gz, ZIP, DEB, RPM, pkg.tar.zst |
| macOS 14.4+ | Intel, Apple Silicon | APP in DMG, ZIP, tar.gz |

Windows x86 and Linux x86/ARMv7 are opt-in experimental build targets. No 32-bit macOS package is planned.

## Curl

Linux and macOS, once the first release is published:

    curl -fsSL https://github.com/ORG/REPOSITORY/releases/latest/download/install.sh | sh

Windows, in PowerShell:

    curl.exe -fsSL https://github.com/ORG/REPOSITORY/releases/latest/download/install.ps1 -o "$env:TEMP\8bitPad-install.ps1"
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$env:TEMP\8bitPad-install.ps1"

The downloadable scripts receive the actual repository during release preparation. From the source checkout, pass --repo / -Repository explicitly. Both scripts also accept BIT8PAD_REPOSITORY.

Choose version 1.0.0:

    sh install.sh --repo ORG/REPOSITORY --version 1.0.0
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\install.ps1 -Repository ORG/REPOSITORY -Version 1.0.0

Download without installing:

    sh install.sh --repo ORG/REPOSITORY --download-only --download-dir ./downloads
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\install.ps1 -Repository ORG/REPOSITORY -DownloadOnly -DownloadDirectory .\downloads

The installer selects the native OS/CPU from the versioned release index and verifies its SHA256 before extraction or installation. It needs no Python or jq on the user's computer.

## Installed and portable layouts

Windows MSI installs per user under LocalAppData\Programs\8bitPad, with a Start menu shortcut and Windows uninstall entry. EXE wraps the same MSI. The portable ZIP includes Start.cmd for the normal profile and Start-Portable.cmd for a profile beside the app.

Linux curl installation keeps version directories below ~/.local/opt/8bitpad, with a launcher in ~/.local/bin and desktop integration in the user data directory. Native DEB, RPM and Arch packages provide the same private runtime under /opt/8bitpad. Use the package manager matching the distribution.

macOS curl installation copies the APP into ~/Applications. The DMG provides the usual APP-to-Applications drag installation. Public packages need the maintainer's Developer ID signing and notarization setup.

Portable directories can be requested directly:

    sh install.sh --repo ORG/REPOSITORY --portable "$HOME/8bitPad-portable"
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\install.ps1 -Repository ORG/REPOSITORY -Portable -InstallDirectory "$HOME\8bitPad-portable"

Linux portable archives include Start-Portable.sh; macOS archives include Start-Portable.command. Portable profiles stay beside the app. Regular upgrades retain existing settings and recovery data. Existing 8bitPad profiles are reused in place.

Qt and app runtime libraries are packaged with the application. A supported graphical OS is still required: Linux x64/ARM64 uses a glibc 2.35 baseline; experimental 32-bit Linux uses 2.36. Kernel/graphics services remain part of the OS. macOS users need no Homebrew or SDK.

Current signing and tested-platform status are tracked outside this repository. Full build, release, architecture and migration instructions are in RELEASING.md.
