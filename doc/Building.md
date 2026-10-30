# Build and test 8bitPad locally

Run commands from the **8bitPad** repository root. Source builds need development
tools; the resulting portable application includes its runtime.

## Windows x64: quickest path

Prerequisites: Visual Studio 2022 C++ Build Tools with the Windows SDK and
CMake/Ninja, plus a matching Qt MSVC x64 SDK. The tested SDK is **Qt 6.11.2**.
Modules: Core, Gui, Widgets, Network, PrintSupport, Core5Compat, Concurrent, Svg,
Test and LinguistTools. Pinned editor/docking dependencies are included in
`dependency-sources`.

From a normal PowerShell window:

```powershell
.\build-windows.cmd -Diagnose
.\build-windows.cmd
# Build, test, and create a fresh complete portable application:
.\build-windows.cmd -Package -Jobs 4
```

The helper initializes MSVC and finds Visual Studio's CMake/Ninja; a preconfigured
developer terminal is not required. The current workstation already has an
ignored `tooling/windows.local.json` pointing to the sibling
`8bitShell/build/tooling/qt-sdk` and this repo's `tooling/qt-compat`.

For another machine, supply your own SDK:

```powershell
.\build-windows.cmd -QtRoot C:/Qt/6.11.2/msvc2022_64 -Package
# Only if Core5Compat is a separate matching overlay:
.\build-windows.cmd -QtRoot C:/Qt/6.11.2/msvc2022_64 -QtCompatRoot C:/Qt/compat-overlay -Package
```

Do not modify a shared base SDK to add Core5Compat. Its overlay must match the Qt
version, compiler and architecture. The lookup order and ignored JSON format are
in [BUILDING-WINDOWS.md](BUILDING-WINDOWS.md).

| Output | Location |
| --- | --- |
| CMake/Ninja build | `build/windows-release` |
| Application binary | `build/windows-release/8bitPad.exe` |
| Test details | `build/windows-release/tests/*.xml` |
| Application smoke report | `build/windows-release/tests/smoke/smoke-results.json` |
| CTest failure log | `build/windows-release/Testing/Temporary/LastTest.log` |
| New portable application | `dist/8bitPad-windows-x64-<timestamp>` |

Run **Start-Portable.cmd** from the entire new package folder. Its settings stay
in that folder's `profile` directory. A loose build EXE needs the SDK environment;
the portable folder is the convenient launch path after the helper exits.

`-PackageDirectory dist/my-new-folder` selects a fresh destination and refuses
to overwrite an existing package. `-BuildDirectory build/my-new-build` selects
another CMake tree. The normal build runs tests; `-ConfigureOnly` does not.
Installer generation is separate: see [RELEASING.md](RELEASING.md). Do not install
an MSI or change real file associations as a build test.

## Linux: native development build

Use a C++20 compiler, CMake >= 3.21, Ninja, Git and Qt >= 6.5 with the modules above.
Qt's matching private GUI development headers are also required by the docking
library. Use Qt 6.11.2 for parity with the release configuration.

With the required SDK installed, use the included dependency sources explicitly:

```bash
# Set this only when Qt is in a custom prefix:
export CMAKE_PREFIX_PATH=/absolute/path/to/qt-sdk
bash scripts/build-linux.sh \
  -DCPM_ads_SOURCE="$PWD/dependency-sources/ads" \
  -DCPM_SingleApplication_SOURCE="$PWD/dependency-sources/singleapplication" \
  -DCPM_editorconfig_core_qt_SOURCE="$PWD/dependency-sources/editorconfig-core-qt" \
  -DFETCHCONTENT_FULLY_DISCONNECTED=ON
./build-linux/src/8bitPad
```

The helper builds Release and runs tests offscreen. Set
`CMAKE_BUILD_PARALLEL_LEVEL=4` to adjust build parallelism or
`BIT8PAD_BUILD_DIR="$PWD/build/local-linux"` to select another build tree.
Fedora and Debian/Ubuntu package examples are in
[BUILDING-LINUX.md](BUILDING-LINUX.md); check their installed Qt version.
Arch uses the same CMake build with a matching native Qt SDK.

After tests pass, an optional development install is:

```bash
cmake --install build-linux --prefix "$HOME/.local"
```

A local CPack archive is a system-dependency build. For a fully collected runtime
archive and DEB/RPM/Arch packages, use [RELEASING.md](RELEASING.md).

## macOS: native development build

Use Xcode's C/C++ developer tools, CMake, Ninja, and a matching Intel or Apple
Silicon Qt SDK including Core5Compat and LinguistTools. Release configuration:
Qt **6.11.2**, macOS **14.4+**.

```bash
export CMAKE_PREFIX_PATH=/absolute/path/to/native/qt-sdk
cmake -S . -B build/local-macos -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_DEPLOYMENT_TARGET=14.4 \
  -DBUILD_TESTING=ON \
  -DCPM_ads_SOURCE="$PWD/dependency-sources/ads" \
  -DCPM_SingleApplication_SOURCE="$PWD/dependency-sources/singleapplication" \
  -DCPM_editorconfig_core_qt_SOURCE="$PWD/dependency-sources/editorconfig-core-qt" \
  -DFETCHCONTENT_FULLY_DISCONNECTED=ON
cmake --build build/local-macos --parallel 4
QT_QPA_PLATFORM=offscreen ctest --test-dir build/local-macos --output-on-failure --no-tests=error
open build/local-macos/src/8bitPad.app
```

Keep each architecture in its own build directory. This macOS recipe is prepared
for native execution and has not been run on this Windows workstation. Framework
deployment, DMG creation, signing and notarization are separate release steps.

## Repeat tests and inspect failures

Rerun `build-windows.cmd` for the full configured Windows test environment.
From a developer PowerShell with CMake/CTest and Qt on PATH, a focused run is:

```powershell
$env:PATH = "$PWD/../8bitShell/build/tooling/qt-sdk/bin;$PWD/tooling/qt-compat/bin;" + $env:PATH
$env:QT_QPA_PLATFORM = 'offscreen'
$env:TEMP = "$PWD/build/windows-release/test-temp"
$env:TMP = $env:TEMP
ctest --test-dir build/windows-release -R 'ExternalTerminalsTest|TerminalPreferencesTest|OpenShellMenuTest' --output-on-failure
```

Use your own Qt paths if the SDK is elsewhere. For the offline release/installer
contract tests, use Python 3.12+:

```powershell
python -m unittest discover -s scripts/ci/tests -v
```

Linux/macOS use `ctest --test-dir <build-directory>`. Read the actual CTest summary;
test counts can change. Inspect XML, smoke JSON and LastTest.log for failures.
Offscreen checks cover application behavior, not native desktop graphics or
installer lifecycle.

## Check Open Shell with 8bitShell

1. Build a fresh portable **8bitShell** and **8bitPad**. Their build guides are
   independent; 8bitPad itself does not need 8bitShell to start.
2. Start the newly built Pad through its `Start-Portable.cmd`.
3. In **Preferences > Terminal**, use a discovered 8bitShell installation or
   select **Custom terminal** and browse to the newly built
   `8bitShell.exe`. Keep **Open a new console window** off.
4. With Custom terminal you can leave arguments empty for 8bitShell. Pad supplies
   its directory option. For an explicit configuration, enter one argument per
   line, without surrounding quotes:

   ```text
   --working-directory
   {folder}
   ```

5. In Pad's file explorer, right-click an existing folder and choose
   **Open Shell > 8bitShell** (or the configured default/custom choice). For a
   file, the action uses its containing folder.
6. In Shell's PowerShell tab, run `Get-Location`. On Linux/macOS run `pwd`.
   Confirm it is the selected folder.
7. Repeat with a folder containing spaces, Turkish characters and punctuation.
   Set Shell's default local profile to a different starting directory and try
   again. The requested folder should win for the initial tab; saved profile
   settings and later tabs should retain their normal behavior.

For a portable/local build, selecting the EXE explicitly is expected; discovery
does not recursively search source/build folders. An old running app or an old
package will retain old behavior. Restart the fresh packages after rebuilding.

The automated terminal tests launch a controlled argument/cwd helper, preserve
literal paths, and do not start a real terminal or change your preferences.

## Troubleshooting

For optional updates, the native `8p` command, user PATH integration and installed
desktop verification, see [Updates.md](Updates.md). Automatic installation is off
by default.

- Start with `-Diagnose`. No global qmake/CMake/Ninja command does not establish
  that the SDK/tools are missing.
- Match MSVC, Qt version, architecture and Core5Compat/private headers.
- After moving the checkout, choose a fresh `-BuildDirectory` instead of reusing
  a CMake cache containing absolute paths to another location.
- The Windows helper configures Release compiler probes to avoid mixed PDB tools.
- Missing runtime DLLs/plugins from a loose EXE: run the complete fresh portable
  folder. Do not copy individual DLLs into Windows system directories.
- Report access-denied errors separately from missing prerequisites.

Windows x64 is locally exercised; hosted ARM/Linux/macOS and experimental 32-bit
release checks are tracked outside this repository.
