# Current release workflow

The editor is now **8bitCode 1.0.1**. See [rename and compatibility](MIGRATION-8BITCODE.md)
for the one-time GitHub migration command and retained Pad profile/install IDs.

Effective 2 November 2026: build and test Windows x64 on the maintainer's PC,
then upload the resulting packages to this repository's GitHub Releases.
GitHub Actions is manual-only. Linux, macOS and other architectures are paused;
their existing source and released assets are retained. 8bitShell is managed
separately.

```powershell
.\build-windows.cmd -Diagnose
.\build-windows.cmd -Package -Jobs 6
```

Packaging runs only after the build and all CTest tests succeed. The helper
creates a fresh directory under `dist/`, with Qt and the MSVC runtime included.
Keep local SDK paths in ignored `tooling/windows.local.json`.

For each upload, record the source commit, Windows x64 architecture, Qt version,
test results and SHA256 of the archive. Exclude SDKs, profiles and build caches
from uploaded source. Attach corresponding source and preserve license notices.
Use a distinct prerelease tag for a development build; do not overwrite a
published release or mark a development package as the latest stable release.

GitHub CLI is the preferred publishing tool. Sign in as `blueberi99` with
`gh auth login`, then publish a prepared asset directory:

```powershell
.\publish-windows.cmd -AssetDirectory dist/releases/<tag>
```

The helper checks source and asset hashes, pushes source without force, uploads
a draft, verifies GitHub's asset digests, and publishes a prerelease. Existing
releases are never overwritten; failed uploads remain drafts for inspection.

The existing `v1.0.0` release remains available. The historical multi-platform
process in RELEASING.md is paused by this policy. Both repositories already live
under 8bitPrograms; no ownership or visibility change is needed for local builds.

## Required Windows release formats

Every release must contain a portable ZIP, MSI installer and EXE setup. The publisher rejects incomplete sets. MSI and EXE install the same per-user product and provide an Installed apps entry plus a Start menu uninstall shortcut. User data and other applications are retained on uninstall.

Use scripts/package-installers.ps1 with PackageDirectory, OutputDirectory, Version, Tag, WixExe and WixUiExtension (pinned WiX 6.0.2 and matching UI extension). The MSVC bootstrap embeds the MSI and does not use a separate installer identity. Code retains Pad's existing upgrade identity and install directory; Notes has an independent identity.

Validate using scripts/validate-installers.ps1 with Msi, Setup, PayloadManifest, WixExe and a fresh OutputDirectory. It reads the MSI database, compares every cabinet payload hash and stable component GUID, checks uninstall ownership, and verifies the EXE's MSI bytes and argument handling without installing anything.

After committing source, scripts/prepare-release.ps1 takes AssetDirectory, PackageDirectory and ValidationReport to add portable/source archives and checksummed metadata. Publish with the existing publish-windows.cmd helper. Final Windows Installer ICE validation is mandatory before any GitHub upload. A restricted build session may defer ICE validation using -DeferIceValidation; such outputs cannot be uploaded until the normal-session publisher passes ICE validation. Full interactive install/upgrade/uninstall testing remains a separate lifecycle check.
