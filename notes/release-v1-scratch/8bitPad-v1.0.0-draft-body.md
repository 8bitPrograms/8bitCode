# Version 1.0.0

Optional stable-release checks and SHA-256 verified downloads are available in Help > Updates. Automatic installation is off by default; installed per-user Windows copies can opt in to installation on exit. Portable, Linux and macOS copies offer manual downloads.

Windows MSI installation adds the app's command directory to user PATH and includes the native `8p` launcher. `8p .` and `8p some.txt` resolve paths from the calling terminal and forward them to the existing window. Uninstall removes the app's own PATH entry. Isolated profiles have separate command channels.

First release under the 8bitShell / 8bitPad names. Matching Pixel Core icons: teal terminal and cobalt blue editor, shared stepped corners, charcoal symbols and transparent icon backgrounds.

Windows file-type association now applies per-user mappings directly, checks the effective Shell handler and reports protected choices inside Pad without opening Settings. Ineffective mapping changes are restored.

Packages bundle Qt and application runtime libraries. Windows packages also carry the redistributable MSVC runtime; Shell carries its SSH command-line tools. Linux requires a supported glibc desktop, its display service, and graphics drivers. macOS requires 14.4 or newer.

32-bit packages, when enabled and successfully built, are experimental and require explicit opt-in in the curl installers. Published artifacts must pass the platform build, application tests, package smoke tests, and checksum verification.

Candidates are unsigned/ad-hoc unless signing credentials have been configured. Review the target build reports, signing/notarization status, and project licensing before publishing the draft.
