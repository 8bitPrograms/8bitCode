[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$QtRoot,
    [string]$QtCompatRoot = '',
    [string]$BuildDirectory = 'build/windows-release',
    [string]$OutputDirectory = 'dist/8bitCode-windows-x64',
    [string]$VcRedistDirectory = ''
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$buildPath = $BuildDirectory
if (![System.IO.Path]::IsPathRooted($buildPath)) { $buildPath = Join-Path $repo $buildPath }
$outputPath = $OutputDirectory
if (![System.IO.Path]::IsPathRooted($outputPath)) { $outputPath = Join-Path $repo $outputPath }
$build = [System.IO.Path]::GetFullPath($buildPath)
$output = [System.IO.Path]::GetFullPath($outputPath).TrimEnd('\')
$QtRoot = (Resolve-Path -LiteralPath $QtRoot).Path
if ($QtCompatRoot) { $QtCompatRoot = (Resolve-Path -LiteralPath $QtCompatRoot).Path }
if (Test-Path -LiteralPath $output) { throw "Choose a fresh package directory: $output" }
New-Item -ItemType Directory -Path $output | Out-Null
Copy-Item -LiteralPath (Join-Path $build '8bitCode.exe') -Destination $output
Copy-Item -LiteralPath (Join-Path $build '8p.exe') -Destination $output
Copy-Item -LiteralPath (Join-Path $build '8bitpad.exe') -Destination $output
Copy-Item -LiteralPath (Join-Path $build '8bitPadFileIcons.dll') -Destination $output
Copy-Item -LiteralPath (Join-Path $repo 'src/update/Apply-Update.ps1') -Destination $output
# Keep the SDK read-only. A private tool directory shares its executable/DLL
# files via hard links and adds the separately installed Core5Compat module.
# Only bin is overlaid; plugins, libraries, metadata, and other paths remain
# rooted in the original SDK. Deployment still selects the app's dependencies.
$tooling = Join-Path $build ('deployment-tooling-' + [guid]::NewGuid().ToString('N'))
$toolBin = Join-Path $tooling 'bin'
New-Item -ItemType Directory -Path $toolBin -Force | Out-Null
foreach ($file in Get-ChildItem -LiteralPath (Join-Path $QtRoot 'bin') -File) {
    if ($file.Extension -notin '.dll', '.exe') { continue }
    if ($QtCompatRoot -and $file.Name -eq 'Qt6Core5Compat.dll') { continue }
    $target = Join-Path $toolBin $file.Name
    try { New-Item -ItemType HardLink -Path $target -Target $file.FullName -ErrorAction Stop | Out-Null }
    catch { Copy-Item -LiteralPath $file.FullName -Destination $target }
}
if ($QtCompatRoot) {
    Copy-Item -LiteralPath (Join-Path $QtCompatRoot 'bin/Qt6Core5Compat.dll') -Destination $toolBin
}
$sdkConfigPath = $QtRoot.Replace('\', '/')
$binConfigPath = $toolBin.Replace('\', '/')
@"
[Paths]
Prefix=$sdkConfigPath
Binaries=$binConfigPath
HostBinaries=$binConfigPath
"@ | Set-Content -LiteralPath (Join-Path $toolBin 'qt.conf') -Encoding utf8
$qtpaths = Join-Path $toolBin 'qtpaths6.exe'
if (!(Test-Path -LiteralPath $qtpaths)) { $qtpaths = Join-Path $toolBin 'qtpaths.exe' }
$queriedBins = & $qtpaths --query QT_INSTALL_BINS
if ($LASTEXITCODE -or [System.IO.Path]::GetFullPath($queriedBins.Trim()) -ne [System.IO.Path]::GetFullPath($toolBin)) {
    throw "Qt deployment bin overlay was not selected: $queriedBins"
}
& (Join-Path $toolBin 'windeployqt.exe') --qtpaths $qtpaths --release --no-translations --no-opengl-sw --no-quick-import --no-compiler-runtime --include-plugins qoffscreen (Join-Path $output '8bitCode.exe')
if ($LASTEXITCODE) { throw 'Qt runtime deployment failed.' }
# App-local MSVC runtime makes the portable bundle independent of a developer
# shell or a separately installed runtime. Copy only the redistributable CRT.
if (!$VcRedistDirectory) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $vsRoot = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (!$vsRoot) { throw 'Visual Studio C++ redistributable location was not found.' }
    $redist = Get-ChildItem -LiteralPath (Join-Path $vsRoot 'VC/Redist/MSVC') -Directory |
        Where-Object { $_.Name -match '^\d+\.\d+\.\d+$' } | Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
    if (!$redist) { throw 'MSVC redistributable CRT was not found.' }
    $VcRedistDirectory = Join-Path $redist.FullName 'x64/Microsoft.VC143.CRT'
}
foreach ($file in Get-ChildItem -LiteralPath $VcRedistDirectory -File -Filter '*.dll') {
    Copy-Item -LiteralPath $file.FullName -Destination $output
}
foreach ($required in @('8bitPadFileIcons.dll', 'Qt6Core.dll', 'Qt6Core5Compat.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll', 'Qt6Network.dll', 'platforms/qwindows.dll', 'msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll')) {
    if (!(Test-Path -LiteralPath (Join-Path $output $required))) { throw "Missing runtime file: $required" }
}
Copy-Item -LiteralPath (Join-Path $repo 'LICENSE') -Destination $output
Copy-Item -LiteralPath (Join-Path $repo 'README.md') -Destination $output
Copy-Item -LiteralPath (Join-Path $repo 'THIRD_PARTY_NOTICES.md') -Destination $output
Copy-Item -LiteralPath (Join-Path $repo 'licenses') -Destination (Join-Path $output 'licenses') -Recurse
Copy-Item -LiteralPath (Join-Path $repo 'doc') -Destination (Join-Path $output 'doc') -Recurse
Copy-Item -LiteralPath (Join-Path $repo 'src/editor/themes') -Destination (Join-Path $output 'theme-licenses') -Recurse
@'
[Paths]
Prefix=.
Plugins=.
'@ | Set-Content -LiteralPath (Join-Path $output 'qt.conf') -Encoding utf8
@'
@echo off
set "BIT8CODE_PROFILE=%~dp0profile"
start "" "%~dp08bitCode.exe" %*
'@ | Set-Content -LiteralPath (Join-Path $output 'Start-Portable.cmd') -Encoding ascii
# Use .NET directly: Get-FileHash may not be exported in a Windows
# PowerShell process inheriting another agent runtime's module environment.
function Get-BitPadSha256([string]$FilePath) {
    $hasher = [System.Security.Cryptography.SHA256]::Create()
    $stream = [System.IO.File]::OpenRead($FilePath)
    try { [BitConverter]::ToString($hasher.ComputeHash($stream)).Replace('-', '').ToLowerInvariant() }
    finally { $stream.Dispose(); $hasher.Dispose() }
}
$checksumPath = Join-Path $output 'SHA256SUMS.txt'
Get-ChildItem -LiteralPath $output -File -Recurse | Where-Object { $_.FullName -ne $checksumPath } | Sort-Object FullName | ForEach-Object {
    $relative = $_.FullName.Substring($output.Length + 1)
    "{0}  {1}" -f (Get-BitPadSha256 $_.FullName), $relative
} | Set-Content -LiteralPath $checksumPath -Encoding utf8
Write-Output "Package ready: $output"
