# Current release workflow

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

The existing `v1.0.0` release remains available. The historical multi-platform
process in RELEASING.md is paused by this policy. Both repositories already live
under 8bitPrograms; no ownership or visibility change is needed for local builds.
