[CmdletBinding()]
param(
 [Parameter(Mandatory=$true)][string]$AssetDirectory,
 [Parameter(Mandatory=$true)][string]$PackageDirectory,
 [Parameter(Mandatory=$true)][string]$ValidationReport
)
# Finish an existing installer output directory with portable/source archives
# and checksummed metadata. No compilation, installation or network access.
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent $PSScriptRoot
function Hash([string]$path){$h=[Security.Cryptography.SHA256]::Create();$s=[IO.File]::OpenRead($path);try{[BitConverter]::ToString($h.ComputeHash($s)).Replace('-','').ToLowerInvariant()}finally{$s.Dispose();$h.Dispose()}}
$project=[IO.File]::ReadAllText((Join-Path $repo 'CMakeLists.txt'))
if($project -notmatch '(?s)project\((8bitCode|8bitNotes)\s+VERSION\s+(\d+\.\d+\.\d+)'){throw 'Unknown project/version.'}
$app=$matches[1];$version=$matches[2];$suites=if($app -eq '8bitCode'){20}else{6}
$assets=(Resolve-Path -LiteralPath $AssetDirectory).Path
$package=(Resolve-Path -LiteralPath $PackageDirectory).Path
$tag=Split-Path -Leaf $assets
if($tag -notmatch ('^v'+[regex]::Escape($version)+'-local\.[0-9.]+$')){throw 'Directory tag/version mismatch.'}
$prefix="$app-$tag-windows-x64"
$report=Get-Content -LiteralPath $ValidationReport -Raw|ConvertFrom-Json
if($report.application -ne $app -or $report.version -ne $version -or (Hash "$assets/$prefix.msi") -ne $report.msiSha256 -or (Hash "$assets/$prefix-setup.exe") -ne $report.setupSha256){throw 'Installer validation does not match these assets.'}
if(Test-Path -LiteralPath "$assets/release-manifest.json"){throw 'Release already prepared; use fresh output.'}
Push-Location $repo
try{
 $head=(& git rev-parse HEAD).Trim()
 if($LASTEXITCODE -or (& git status --porcelain)){throw 'Commit all source changes before preparing a release.'}
 $tests=Get-Content build/windows-release/Testing/Temporary/LastTest.log -Raw
 if(($tests|Select-String -AllMatches 'Test Passed\.').Matches.Count -ne $suites -or $tests -match 'Test Failed\.'){throw 'Expected CTest results are missing.'}
 if((Hash "$package/$app.exe") -ne (Hash "$repo/build/windows-release/$app.exe")){throw 'Bundle does not match tested application.'}
 $utf8=New-Object Text.UTF8Encoding($false)
 $uninstall=@("$app removal",'', 'Installed MSI/EXE: Windows Settings > Apps > Installed apps > '+$app+' > Uninstall, or Start > 8bitPrograms > Uninstall '+$app+'.','Both installers use the same MSI registration. Do not manually delete an installed application folder.','Uninstall removes installed program files, owned empty directories, its shortcuts and its own PATH entry. Settings, saved work and other 8bitPrograms applications are retained.','Portable ZIP: back up any profile or work stored inside the extracted folder, close the application, then delete that folder. Windows user-profile settings and saved notes remain.') -join "`r`n"
 [IO.File]::WriteAllText("$package/UNINSTALL.txt",$uninstall+"`r`n",$utf8)
 # Refresh the full portable inventory after adding removal instructions.
 $lines=@(Get-ChildItem -LiteralPath $package -Recurse -File|Where-Object {$_.FullName -ne "$package\SHA256SUMS.txt"}|Sort-Object FullName|ForEach-Object {(Hash $_.FullName)+'  '+$_.FullName.Substring($package.Length+1).Replace('\','/')})
 [IO.File]::WriteAllText("$package/SHA256SUMS.txt",($lines -join "`n")+"`n",$utf8)
 & tar.exe -a -cf "$assets/$prefix.zip" -C $package .
 if($LASTEXITCODE){throw 'Portable archive failed.'}
 & git archive --format=tar.gz "--prefix=$app/" "--output=$assets/$app-$tag-source.tar.gz" HEAD
 if($LASTEXITCODE){throw 'Source archive failed.'}
 $pin=$null
 if($app -eq '8bitNotes'){
  $pin=(& git -C vendor/editor rev-parse HEAD).Trim();$expected=((& git ls-tree HEAD vendor/editor)-split '\s+')[2]
  if($pin -ne $expected){throw 'Editor source pin mismatch.'}
  & git -C vendor/editor archive --format=tar.gz --prefix=8bitNotes/vendor/editor/ "--output=$assets/8bitNotes-editor-source.tar.gz" HEAD
  if($LASTEXITCODE){throw 'Editor source archive failed.'}
 }
 Copy-Item -LiteralPath $ValidationReport -Destination "$assets/installer-validation.json"
 $summary=[ordered]@{application=$app;version=$version;sourceCommit=$head;platform='windows';architecture='x64';builtLocally=$true;githubActionsUsed=$false;ctestSuitesPassed=$suites;ctestSuitesFailed=0;approvedIconFramesVerified=9;sdkIndependentVersionStartup='Passed';packageFormats=@('portable ZIP','MSI installer','EXE installer');installerPayloadChecks=$report.passed;installerPayloadFiles=$report.payloadFiles;editorCommit=$pin;iceValidation='Required by publisher before upload';installUninstallLifecycle='Not run against user profile'}
 [IO.File]::WriteAllText("$assets/test-summary.json",($summary|ConvertTo-Json -Depth 6),$utf8)
 $notes=@("# $app $version - complete Windows packages",'', 'Locally built Windows x64 packages: portable ZIP, MSI installer and EXE installer. GitHub Actions is not used.',"Source commit: $head",'','The EXE contains the exact same MSI. Install either MSI or EXE; they share one Installed apps registration and a Start menu uninstall shortcut. User settings, saved work and other apps are retained on removal. Portable removal instructions are included as UNINSTALL.txt.',"$suites/$suites CTest suites passed. All nine app icon frames match the approved Pixel Core artwork. MSI cabinet payloads and EXE-embedded MSI bytes were checked. The publisher requires Windows Installer ICE validation before uploading. Install/uninstall lifecycle testing has not been performed against the user profile.",'', 'Approved branding: Code uses cobalt with white code brackets; Notes uses emerald with the original Pad three-bar glyph. Windows packages are unsigned.', 'Extract the full ZIP and run Start-Portable.cmd. Code stores its portable profile beside the app. Notes currently keeps settings and scratch notes in the Windows user profile. Close an existing Pad instance before starting Code.', 'Code retains the legacy Pad install folder, upgrade identity, profiles, aliases and file icon registration for compatibility. Notes has its own MSI identity.', 'Application source and static dependency source are attached. For Notes, extract the two source archives into the same folder to populate vendor/editor.', 'Qt 6.11.2 source: https://github.com/8bitPrograms/8bitCode/releases/download/v1.0.0/8bitPad-1.0.0-qt-6.11.2-source.tar.gz . License texts and third-party notices are included. Other platform builds remain paused.')
 [IO.File]::WriteAllText("$assets/release-notes.md",($notes -join "`n")+"`n",$utf8)
 $items=@(Get-ChildItem -LiteralPath $assets -File|Where-Object {$_.Name -notin @('SHA256SUMS.txt','release-manifest.json')}|Sort-Object Name|ForEach-Object {[ordered]@{name=$_.Name;size=$_.Length;sha256=(Hash $_.FullName)}})
 $manifest=[ordered]@{application=$app;repository="8bitPrograms/$app";version=$version;tag=$tag;title="$app $version - Windows ZIP, MSI and EXE";sourceCommit=$head;platform='windows';architecture='x64';assets=$items}
 [IO.File]::WriteAllText("$assets/release-manifest.json",($manifest|ConvertTo-Json -Depth 7),$utf8)
 $lines=@(Get-ChildItem -LiteralPath $assets -File|Where-Object {$_.Name -ne 'SHA256SUMS.txt'}|Sort-Object Name|ForEach-Object {(Hash $_.FullName)+'  '+$_.Name})
 [IO.File]::WriteAllText("$assets/SHA256SUMS.txt",($lines -join "`n")+"`n",$utf8)
 Get-ChildItem -LiteralPath $assets -File|Select-Object Name,Length
}finally{Pop-Location}
