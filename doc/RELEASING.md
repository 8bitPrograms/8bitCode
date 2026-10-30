# 8bitPad 1.0 release preparation

The application starts at **1.0.0**. The source is now in the new 8bitPrograms repository checkout, using its new Git metadata and origin. No release has been published.

## Release matrix

| Target | Runner / build environment | Packages | Status |
| --- | --- | --- | --- |
| Windows x64 | Windows 2022, native MSVC | MSI, EXE bootstrap, ZIP, tar.gz | Standard matrix; hosted run pending |
| Windows ARM64 | Windows 11 ARM, native MSVC | MSI, EXE bootstrap, ZIP, tar.gz | Standard matrix; hosted run pending |
| Linux x64 | Ubuntu 22.04, GCC 12 | tar.gz, ZIP, DEB, RPM, pkg.tar.zst | glibc 2.35 baseline; hosted run pending |
| Linux ARM64 | Ubuntu 22.04 ARM, GCC 12 | tar.gz, ZIP, DEB, RPM, pkg.tar.zst | glibc 2.35 baseline; hosted run pending |
| macOS Intel | macOS 15 Intel, Apple Clang | APP in DMG, ZIP, tar.gz | macOS 14.4 minimum; hosted run pending |
| macOS Apple Silicon | macOS 15 ARM, Apple Clang | APP in DMG, ZIP, tar.gz | macOS 14.4 minimum; hosted run pending |
| Windows x86 | Windows 2022, x86 MSVC + source-built Qt | MSI, EXE, ZIP, tar.gz | Opt-in experimental; Qt does not officially support this desktop target |
| Linux x86 | Debian 12 i386 container + source-built Qt | tar.gz, ZIP, DEB, RPM, pkg.tar.zst | Opt-in experimental; glibc 2.36 baseline |
| Linux ARMv7 | Debian 12 arm32v7, user-mode emulation + source-built Qt | tar.gz, ZIP, DEB, RPM, pkg.tar.zst | Opt-in experimental; glibc 2.36 baseline |

For Debian/Ubuntu, use DEB or the runtime archive. For Fedora, use RPM or the archive. For Arch, use pkg.tar.zst or the archive. ARM Arch-style packages target the community Arch Linux ARM ecosystem; the official Arch Linux container smoke test covers x64 only.

The ordinary release requires all six standard targets. Enable experimental 32-bit jobs through the manual legacy32 input or repository variable ENABLE_LEGACY32=true. A failed enabled target blocks draft creation. Experimental targets never silently substitute an x64 binary and require --experimental / -Experimental in the installer. There is no modern 32-bit macOS target.

## Runtime policy

Linux and Windows build Qt 6.11.2 from versioned official source archives with their official SHA256 sidecars. Hosted macOS jobs install the matching official universal desktop SDK (Qt's `clang_64` macOS package, via aqtinstall 3.3.0) and cache it. Compiling Qt on the hosted macOS runners took long enough that later pushes cancelled the job before its cache could be saved. A private workstation SDK stays outside the hosted build.

Windows packages carry their Qt DLLs, plugins and app-local redistributable MSVC runtime. Pad carries its editor runtime and opens external terminals only when the user requests one; a separate terminal is not an application startup dependency.

Linux packages carry Qt, QML where applicable, plugins, fonts, OpenSSL and other linked non-system libraries. Runtime dependency discovery rejects unresolved ELF libraries, sets relative paths, and excludes glibc/its loader. X11/Wayland services, kernel interfaces, hardware graphics drivers and a supported glibc remain properties of the operating system. Bundling these would not make an arbitrary old/minimal/non-graphical OS a compatible desktop.

macOS packages carry a native APP and its Qt frameworks/plugins from that official SDK, plus the license notices from the matching Qt module tags. The application itself is still compiled on the native Intel or Apple Silicon runner. Homebrew, Qt, Python and a compiler are build-machine tools; users do not need them to run a package.

The package smoke tests clear SDK/library environment paths. Linux also starts the archive in Ubuntu, Debian and Fedora containers with no Qt SDK installed; x64 adds Arch. These are offscreen startup/application checks. They do not replace interactive desktop or installer lifecycle testing.

## Curl installation

After the first published release exists, substitute the real organization and repository in these examples. The scripts accept --repo / -Repository today; the release workflow injects github.repository into the downloadable installer assets, so source files do not need a hardcoded organization.

Linux and macOS:

    curl -fsSL https://github.com/8bitPrograms/8bitPad/releases/latest/download/install.sh | sh

Pinned version:

    curl -fsSL https://raw.githubusercontent.com/8bitPrograms/8bitPad/main/install.sh | sh -s -- --repo 8bitPrograms/8bitPad --version 1.0.0

Portable directory:

    curl -fsSL https://raw.githubusercontent.com/8bitPrograms/8bitPad/main/install.sh | sh -s -- --repo 8bitPrograms/8bitPad --portable "$HOME/8bitPad-portable"

Windows, in PowerShell:

    curl.exe -fsSL https://github.com/8bitPrograms/8bitPad/releases/latest/download/install.ps1 -o "$env:TEMP\8bitPad-install.ps1"
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$env:TEMP\8bitPad-install.ps1"

Windows portable ZIP:

    powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$env:TEMP\8bitPad-install.ps1" -Portable -InstallDirectory "$HOME\8bitPad-portable"

Source checkout before publishing:

    .\install.ps1 -Repository 8bitPrograms/8bitPad -Version 1.0.0 -DownloadOnly
    sh install.sh --repo 8bitPrograms/8bitPad --version 1.0.0 --download-only

The installer uses a small TSV release index instead of requiring Python/jq to parse a GitHub API response. It resolves latest to one exact release, checks OS/architecture and SHA256 before extracting, and never executes an installer in fixture tests. Linux/macOS installations are per-user and retain previous versions; Windows uses the per-user MSI. Existing application profiles are preserved. Portable launchers keep a separate profile next to the application.

## Build and package flow

The main entry point is .github/workflows/build.yml. Packaging runs after application tests succeed. Each target contributes a package manifest containing its OS, CPU, digest and version. The final job verifies these manifests, target coverage and version tag, creates the index/checksum list, attaches corresponding application/Qt/runtime-library source archives, and creates a **draft** only. Publishing remains a separate human action.

The release toolchain is copied into each repository; the apps do not depend on a sibling checkout or a future monorepo. Shared changes must be applied to both copies.

On a prepared native build host:

    python scripts/ci/build-qt.py --prefix tooling/qt-sdk-NATIVE_ARCH --jobs 2
    python scripts/ci/build.py --qt-root tooling/qt-sdk-NATIVE_ARCH --jobs 2

Windows initializes the matching MSVC environment through:

    .\scripts\ci\run-windows.ps1 -Architecture x64
    .\scripts\ci\run-windows.ps1 -Architecture arm64
    .\scripts\ci\run-windows.ps1 -Architecture x86

Build prerequisites belong only on the CI/developer host. Source-built Qt takes longer on the first run; its SDK and verified source archives are cached for later runs.

## macOS signing

Local/branch candidates use ad-hoc signing when no Apple identity is configured. For public macOS distribution, configure these GitHub secrets:

- MACOS_CERTIFICATE_P12: base64 Developer ID Application certificate.
- MACOS_CERTIFICATE_PASSWORD: certificate password.
- MACOS_SIGN_IDENTITY: exact Developer ID signing identity.
- MACOS_NOTARY_APPLE_ID, MACOS_NOTARY_TEAM_ID, MACOS_NOTARY_PASSWORD: notarytool credentials.

The signing helper imports the identity into a temporary runner keychain. The packaging step signs the APP, submits its DMG to Apple when notarization credentials exist, and staples the accepted DMG. Never commit credentials. Signing and notarization are not claimed as validated until a real credential-backed build has run.

Windows packages are unsigned until an Authenticode signing process/certificate is configured. They still include runtime libraries; signing status is a separate release property.

## Publication checklist

1. Review and commit the migrated source in this repository.
2. Push the reviewed initial commit to its existing 8bitPrograms origin.
3. Run the six-target Actions matrix and inspect package and application reports.
4. Complete signing setup and any existing publication-policy gates.
5. Tag the reviewed 1.0.0 commit as v1.0.0, inspect the generated draft and publish it after validation.

The two repositories have independent source and Git history. Build products, local SDKs, private profiles and dependency caches are ignored. The local source no longer depends on a previous project checkout.

## Sources used for platform decisions

- Qt supported desktop targets: https://doc.qt.io/qt-6/supported-platforms.html
- GitHub native hosted runner labels: https://docs.github.com/en/actions/reference/runners/github-hosted-runners
- Qt deployment tools: https://doc.qt.io/qt-6/qt-generate-deploy-app-script.html
- Qt QML deployment: https://doc.qt.io/qt-6/qt-generate-deploy-qml-app-script.html

Local checks and known limitations are recorded in the release validation log, kept outside this repository. Preparing files locally is not evidence that hosted Windows ARM, Linux, macOS, emulated 32-bit, signing, or distribution install tests have passed.

## Windows Start menu and removal

MSI and EXE installations appear under the 8bitPrograms publisher in Windows Installed apps. The shared Start menu folder is 8bitPrograms, containing 8bitShell / 8bitPad and Uninstall 8bitShell / Uninstall 8bitPad shortcuts. Each uninstall shortcut invokes Windows Installer for its own installed ProductCode. Removing one app retains the other app's shortcuts. Only empty installer-owned directories are removed; normal settings, editor recovery and saved connections/keys are retained.

Curl's normal Windows installation uses the same MSI and removal entries. ZIP/tar.gz packages remain portable: close the app and remove its extracted directory; copy its profile directory first if its portable settings are needed. Portable extraction creates no installed-app registration or Start menu shortcuts.
