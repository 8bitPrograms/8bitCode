# Windows build

For the full local workflow, test reports, portable launch and Open Shell checks,
see [Building.md](Building.md).

From a native Windows shell in the source directory:

```powershell
.\build-windows.cmd -Diagnose
.\build-windows.cmd
.\build-windows.cmd -Package
```

No preconfigured developer terminal or global PATH changes are needed. The launcher uses Windows PowerShell 5.1; the script also supports PowerShell 7. It initializes Visual Studio C++ Build Tools and locates CMake/Ninja by absolute path. qmake is not required for this CMake project.

The local SDK configuration lives in `tooling/windows.local.json`, excluded from Git:

```json
{
  "QtRoot": "C:/path/to/Qt/6.11.2/msvc2022_64",
  "QtCompatRoot": "C:/path/to/matching/qt-compat"
}
```

Omit `QtCompatRoot` when the base SDK already contains Core5Compat. The checked-in helper also accepts explicit paths and environment variables:

```powershell
.\build-windows.cmd -QtRoot C:/Qt/6.11.2/msvc2022_64
# Or directly from PowerShell:
.\scripts\build-windows.ps1 -QtRoot C:/Qt/6.11.2/msvc2022_64 -QtCompatRoot C:/Qt/compat-overlay
```

Qt >=6.5 MSVC x64 development modules required: Core, Gui, Widgets, Network, PrintSupport, Core5Compat, Concurrent, Svg, Test and LinguistTools. Visual Studio requires the C++ toolchain, Windows SDK and C++ CMake tools. The helper finds Visual Studio through `vswhere.exe`.

Pinned ADS, SingleApplication and editorconfig sources are included in `dependency-sources` and used automatically. `-FetchDependencies` opts into fetching the revisions from CMake instead. `-ConfigureOnly` configures without compiling; `-BuildDirectory` chooses another build tree; `-Jobs` controls parallel compilation.

Build output: `build/windows-release/8bitPad.exe`. Test output: `build/windows-release/tests/*.xml` and `tests/smoke/smoke-results.json`. `-Package` creates a new timestamped folder under `dist`; select a fresh custom destination with `-PackageDirectory dist/my-new-package`. Existing packages and profiles are retained. Run `Start-Portable.cmd` from the complete packaged folder.

If another agent reports that tools are absent, provide the `-Diagnose` output. An executable absent from PATH may still be installed. A sandbox permission denial does not establish that a directory is missing.
