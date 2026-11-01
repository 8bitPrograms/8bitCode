#Requires -Version 5.1
[CmdletBinding()]
param(
    [string]$QtRoot = '',
    [string]$QtCompatRoot = '',
    [string]$BuildDirectory = 'build/windows-release',
    [int]$Jobs = 6,
    [switch]$ConfigureOnly,
    [switch]$Diagnose,
    [switch]$FetchDependencies,
    [switch]$Package,
    [string]$PackageDirectory = ''
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'windows-toolchain.ps1')
$tools = Resolve-BitPadWindowsToolchain -Repo $repo -QtRoot $QtRoot -QtCompatRoot $QtCompatRoot
$tools | Format-List | Out-Host
if ($Diagnose) {
    & $tools.CMake --version
    if ($LASTEXITCODE) { throw 'CMake could not start.' }
    & $tools.Ninja --version
    if ($LASTEXITCODE) { throw 'Ninja could not start.' }
    return
}
$savedEnvironment = @{}
foreach ($name in @('PATH', 'QT_PLUGIN_PATH', 'QT_QPA_PLATFORM', 'QT_QPA_FONTDIR', 'TEMP', 'TMP')) {
    $savedEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
}
Push-Location $repo
try {
    Import-Module (Join-Path $tools.VisualStudio 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll')
    Enter-VsDevShell -VsInstallPath $tools.VisualStudio -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'
    $toolPaths = @((Join-Path $tools.QtRoot 'bin'), (Split-Path -Parent $tools.CMake), (Split-Path -Parent $tools.Ninja))
    if ($tools.QtCompatRoot) { $toolPaths = @((Join-Path $tools.QtCompatRoot 'bin')) + $toolPaths }
    $env:PATH = ($toolPaths -join ';') + ';' + $env:PATH
    $env:QT_PLUGIN_PATH = Join-Path $tools.QtRoot 'plugins'
    $prefix = $tools.QtRoot
    if ($tools.QtCompatRoot) { $prefix += ';' + $tools.QtCompatRoot }
    $build = $BuildDirectory
    if (![IO.Path]::IsPathRooted($build)) { $build = Join-Path $repo $build }
    $build = [IO.Path]::GetFullPath($build)
    $configure = @('-S', $repo, '-B', $build, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_TRY_COMPILE_CONFIGURATION=Release', "-DCMAKE_PREFIX_PATH=$prefix", "-DCMAKE_MAKE_PROGRAM=$($tools.Ninja)", '-DBUILD_TESTING=ON')
    if ($tools.QtCompatRoot) { $configure += "-DQt6Core5Compat_DIR=$($tools.QtCompatRoot)/lib/cmake/Qt6Core5Compat" }
    $dependencyNames = @{ ads = 'ads'; SingleApplication = 'singleapplication'; editorconfig_core_qt = 'editorconfig-core-qt' }
    $bundled = !$FetchDependencies
    foreach ($name in $dependencyNames.Keys) {
        if (!(Test-Path -LiteralPath (Join-Path $repo "dependency-sources/$($dependencyNames[$name])/CMakeLists.txt"))) { $bundled = $false }
    }
    if ($bundled) {
        foreach ($name in $dependencyNames.Keys) { $configure += "-DCPM_${name}_SOURCE=$repo/dependency-sources/$($dependencyNames[$name])" }
        $configure += '-DFETCHCONTENT_FULLY_DISCONNECTED=ON'
        Write-Host 'Using bundled pinned dependency sources; no dependency download is needed.'
    } elseif ($FetchDependencies) {
        foreach ($name in $dependencyNames.Keys) { $configure += "-UCPM_${name}_SOURCE" }
        $configure += '-DFETCHCONTENT_FULLY_DISCONNECTED=OFF'
    }
    & $tools.CMake @configure
    if ($LASTEXITCODE) { throw 'CMake configuration failed.' }
    if ($ConfigureOnly) { return }
    & $tools.CMake --build $build --parallel $Jobs
    if ($LASTEXITCODE) { throw 'Build failed.' }
    $env:QT_QPA_PLATFORM = 'offscreen'
    $env:QT_QPA_FONTDIR = Join-Path $env:WINDIR 'Fonts'
    $testTemp = Join-Path $build 'test-temp'
    New-Item -ItemType Directory -Force -Path $testTemp | Out-Null
    $env:TEMP = $testTemp
    $env:TMP = $testTemp
    & $tools.CTest --test-dir $build --output-on-failure --no-tests=error
    if ($LASTEXITCODE) { throw 'Tests failed.' }
    if ($Package) {
        if (!$PackageDirectory) { $PackageDirectory = 'dist/8bitCode-windows-x64-' + (Get-Date -Format 'yyyyMMdd-HHmmss') }
        & (Join-Path $PSScriptRoot 'package-windows.ps1') -QtRoot $tools.QtRoot -QtCompatRoot $tools.QtCompatRoot -BuildDirectory $build -OutputDirectory $PackageDirectory
    }
    Write-Host "Windows build and tests succeeded: $build/8bitCode.exe"
} finally {
    Pop-Location
    foreach ($name in $savedEnvironment.Keys) { [Environment]::SetEnvironmentVariable($name, $savedEnvironment[$name], 'Process') }
}
