# 8bitPad becomes 8bitCode

The public editor name is **8bitCode**, starting with version 1.0.1. The first
renamed build targets Windows x64 and is built locally. 8bitNotes continues to
use the product-independent `EightBit::Editor` target.

| Area | Rename behaviour |
| --- | --- |
| Application | `8bitCode.exe`, Code branding, About/Preferences and version output |
| Commands | `8bitcode`, `8p` and the compatibility launcher `8bitpad` |
| Profiles | Existing organization/app IDs, `8bitpad.ini`, sessions and themes are retained |
| Portable profile | `BIT8CODE_PROFILE` takes priority; `BIT8PAD_PROFILE` and `--profile` remain supported |
| Windows installation | Existing UpgradeCode and `Programs/8bitPad` directory are retained; shortcuts and Installed apps display Code |
| Associations | Existing ProgIDs, registration keys, icon DLL and COM class ID are retained; user-facing labels display Code |
| Updater | Code assets and the `8bitPrograms/8bitCode` release feed; legacy installed directory remains accepted |
| GitHub | Rename the existing repository in place, preserving its ID, history, issues, releases and access settings |
| Notes | Update the editor submodule URL to Code; its tested commit pin remains unchanged |

The local checkout folder stays `8bitPad` while existing workstation SDK paths
use it. No user data is copied, deleted or converted. The old stable release and
Pad-named development snapshot remain historical downloads.

After the Code release assets have been prepared, run from this checkout:

```powershell
.\migrate-to-code.cmd -AssetDirectory dist/releases/v1.0.1-local.20261102.2
```

This checks both clean local checkouts and release hashes, authenticates GitHub
CLI, verifies the existing repository ID, renames it in place, updates this
checkout's remote, pushes both sources without force, and publishes the verified
Code Windows prerelease. It does not change repository visibility or access.

Automated validation covers version branding, both compatibility launchers with
Unicode/quoted arguments and working directories, existing editor regressions,
and packaged runtime startup without SDK paths. A real MSI in-place upgrade
still requires interactive acceptance on a disposable Windows profile; generating
or validating an MSI is not evidence that that lifecycle has been tested.
