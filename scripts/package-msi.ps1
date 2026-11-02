[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$PackageDirectory,
    [Parameter(Mandatory=$true)][string]$OutputMsi,
    [string]$Version = '1.0.1',
    [string]$WixRoot = '',
    [switch]$GenerateOnly
)
# GenerateOnly validates a portable package and authors sources, without invoking
# WiX or Windows Installer. Normal operation builds an MSI; neither mode installs it.
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2
$repo = Split-Path -Parent $PSScriptRoot

function Resolve-RepoPath([string]$Path) {
    if (![IO.Path]::IsPathRooted($Path)) { $Path = Join-Path $repo $Path }
    return [IO.Path]::GetFullPath($Path)
}
function Get-Sha256([string]$Path) {
    $hash = [Security.Cryptography.SHA256]::Create()
    $stream = [IO.File]::OpenRead($Path)
    try { return [BitConverter]::ToString($hash.ComputeHash($stream)).Replace('-', '').ToLowerInvariant() }
    finally { $stream.Dispose(); $hash.Dispose() }
}
function Get-StableHash([string]$Name) {
    $hash = [Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($hash.ComputeHash([Text.Encoding]::UTF8.GetBytes('8bitPad-MSI-x64-v1/' + $Name.ToLowerInvariant()))).Replace('-', '').ToLowerInvariant() }
    finally { $hash.Dispose() }
}
function Get-StableGuid([string]$Name) {
    $h = Get-StableHash $Name
    # Deterministic application-specific UUIDv8; paths, not source roots or file
    # contents, identify components. Never change this scheme after publishing.
    return ('{0}-{1}-8{2}-a{3}-{4}' -f $h.Substring(0,8), $h.Substring(8,4), $h.Substring(13,3), $h.Substring(17,3), $h.Substring(20,12)).ToUpperInvariant()
}
function Escape-Wix([string]$Value) {
    # XML escaping is separate from WiX's literal-dollar preprocessor escape.
    return [Security.SecurityElement]::Escape($Value.Replace('$(', '$$('))
}
function Is-Excluded([string]$Relative) {
    return $Relative -match '^(?i:profile)(?:/|$)' -or $Relative -match '^(?i:Start-Portable\.cmd|SHA256SUMS\.txt)$'
}
function Get-PayloadFiles([string]$Directory, [string]$Base) {
    foreach ($entry in Get-ChildItem -LiteralPath $Directory -Force | Sort-Object Name) {
        $relative = $entry.FullName.Substring($Base.Length + 1).Replace('\', '/')
        if (Is-Excluded $relative) { continue }
        if (($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw "Package must not contain links or junctions: $relative" }
        if ($entry.PSIsContainer) { Get-PayloadFiles $entry.FullName $Base }
        else { $entry }
    }
}

if ($Version -notmatch '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$') { throw 'Version must be stable major.minor.patch, for example 0.1.0 (no prerelease or fourth field).' }
$parts = $Version.Split('.')
if ([long]$parts[0] -gt 255 -or [long]$parts[1] -gt 255 -or [long]$parts[2] -gt 65535) { throw 'MSI version limits are 255.255.65535.' }
$package = (Resolve-Path -LiteralPath (Resolve-RepoPath $PackageDirectory)).Path.TrimEnd('\', '/')
if (!(Test-Path -LiteralPath $package -PathType Container)) { throw 'PackageDirectory must be a directory.' }
if (((Get-Item -LiteralPath $package).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw 'PackageDirectory must not be a link or junction.' }
$output = Resolve-RepoPath $OutputMsi
if ([IO.Path]::GetExtension($output) -ine '.msi') { throw 'OutputMsi must name an .msi file.' }
if ($output.StartsWith($package + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw 'OutputMsi must be outside the portable package.' }
if (Test-Path -LiteralPath $output) { throw "Refusing to overwrite existing MSI: $output" }
$required = @('8bitCode.exe', '8p.exe', '8bitpad.exe', 'Qt6Core.dll', 'Qt6Core5Compat.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll', 'Qt6Network.dll', 'platforms/qwindows.dll', 'msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll', 'qt.conf', 'LICENSE', 'THIRD_PARTY_NOTICES.md', 'SHA256SUMS.txt')
foreach ($relative in $required) {
    if (!(Test-Path -LiteralPath (Join-Path $package $relative) -PathType Leaf)) { throw "Missing required package file: $relative" }
}
if (!(Test-Path -LiteralPath (Join-Path $package '8bitPadFileIcons.dll') -PathType Leaf)) {
    throw 'Missing required package file: 8bitPadFileIcons.dll'
}
foreach ($relative in @('licenses', 'theme-licenses')) {
    if (!(Test-Path -LiteralPath (Join-Path $package $relative) -PathType Container)) { throw "Missing package license directory: $relative" }
}
$exe = [IO.File]::OpenRead((Join-Path $package '8bitCode.exe'))
$reader = New-Object IO.BinaryReader($exe)
try {
    if ($reader.ReadUInt16() -ne 0x5a4d) { throw '8bitCode.exe is not a PE executable.' }
    $exe.Position = 0x3c
    $offset = $reader.ReadInt32()
    if ($offset -lt 64 -or $offset -gt ($exe.Length - 6)) { throw 'Invalid PE header.' }
    $exe.Position = $offset
    if ($reader.ReadUInt32() -ne 0x4550 -or $reader.ReadUInt16() -ne 0x8664) { throw '8bitCode.exe must be a Windows x64 executable.' }
} finally { $reader.Dispose(); $exe.Dispose() }

# Recheck the portable package's complete included inventory. A changed or extra
# file must be deliberately repackaged/tested before it can enter the installer.
$checksums = @{}
foreach ($line in [IO.File]::ReadAllLines((Join-Path $package 'SHA256SUMS.txt'))) {
    if (!$line.Trim()) { continue }
    if ($line -notmatch '^([a-fA-F0-9]{64})  (.+)$') { throw 'Invalid portable SHA256SUMS.txt entry.' }
    $digest = $matches[1].ToLowerInvariant()
    $relative = $matches[2].Replace('\', '/')
    if ([IO.Path]::IsPathRooted($relative) -or $relative -match '(^|/)\.\.?(/|$)' -or $relative.Contains(':')) { throw "Invalid manifest path: $relative" }
    if ($checksums.ContainsKey($relative)) { throw "Duplicate manifest path: $relative" }
    $checksums[$relative] = $digest
}
$payload = @(Get-PayloadFiles $package $package | Sort-Object FullName)
$inventory = @()
$included = @{}
foreach ($file in $payload) {
    $relative = $file.FullName.Substring($package.Length + 1).Replace('\', '/')
    if (!$checksums.ContainsKey($relative)) { throw "File is not in portable checksum manifest: $relative" }
    $digest = Get-Sha256 $file.FullName
    if ($digest -ne $checksums[$relative]) { throw "Portable checksum mismatch: $relative" }
    $included[$relative] = $true
    $inventory += [ordered]@{ path=$relative; sha256=$digest; size=$file.Length; component=('C_' + (Get-StableHash ('file/' + $relative)).Substring(0,32)); guid=(Get-StableGuid ('file/' + $relative)) }
}
foreach ($relative in $checksums.Keys) {
    if (!(Is-Excluded $relative) -and !$included.ContainsKey($relative)) { throw "Manifest file is missing: $relative" }
}

$wixBin = ''
if (!$GenerateOnly) {
    if (!$WixRoot) { throw 'Pass -WixRoot pointing to the unpacked WiX 3.14.1 toolset (or its bin folder).' }
    $wixBin = (Resolve-Path -LiteralPath (Resolve-RepoPath $WixRoot)).Path
    if (!(Test-Path -LiteralPath (Join-Path $wixBin 'candle.exe'))) { $wixBin = Join-Path $wixBin 'bin' }
    foreach ($tool in @('candle.exe', 'light.exe', 'WixUIExtension.dll')) {
        if (!(Test-Path -LiteralPath (Join-Path $wixBin $tool) -PathType Leaf)) { throw "WiX 3.14.1 tool missing: $tool" }
    }
    foreach ($tool in @('candle.exe', 'light.exe')) {
        $toolVersion = [Diagnostics.FileVersionInfo]::GetVersionInfo((Join-Path $wixBin $tool))
        # WiX 3's numeric Win32 fields encode 3.14.8722.0 even though the
        # published FileVersion/ProductVersion strings are 3.14.1.8722.
        if ($toolVersion.ProductVersion -notmatch '^3\.14\.1\.[0-9]+(?:\s.*)?$') { throw "Expected pinned WiX 3.14.1: $tool" }
    }
}
$outputParent = Split-Path -Parent $output
[IO.Directory]::CreateDirectory($outputParent) | Out-Null
$work = $output + '.work-' + [guid]::NewGuid().ToString('N')
[IO.Directory]::CreateDirectory($work) | Out-Null
$utf8 = New-Object Text.UTF8Encoding($false)
$source = Join-Path $work '8bitPad.wxs'
Copy-Item -LiteralPath (Join-Path $repo 'deploy/windows/8bitPad.wxs') -Destination $source
$license = [IO.File]::ReadAllText((Join-Path $package 'LICENSE'))
$license = $license.Replace('\', '\\').Replace('{', '\{').Replace('}', '\}').Replace("`r", '').Replace("`n", '\par ')
$licensePath = Join-Path $work 'License.rtf'
[IO.File]::WriteAllText($licensePath, ('{\rtf1\ansi\deff0{\fonttbl{\f0 Segoe UI;}}\f0\fs18 ' + $license + '}'), $utf8)

$lines = New-Object 'System.Collections.Generic.List[string]'
$lines.Add('<?xml version="1.0" encoding="utf-8"?>')
$lines.Add('<Wix xmlns="http://schemas.microsoft.com/wix/2006/wi"><Fragment>')
$directories = @{ ''='INSTALLFOLDER' }
foreach ($item in $inventory) {
    $segments = $item.path.Split('/')
    $parent = ''
    for ($i=0; $i -lt $segments.Length-1; $i++) {
        $relative = if ($parent) { $parent + '/' + $segments[$i] } else { $segments[$i] }
        if (!$directories.ContainsKey($relative)) {
            $id = 'D_' + (Get-StableHash ('dir/' + $relative)).Substring(0,32)
            $lines.Add(('<DirectoryRef Id="{0}"><Directory Id="{1}" Name="{2}" /></DirectoryRef>' -f $directories[$parent], $id, (Escape-Wix $segments[$i])))
            $directories[$relative] = $id
        }
        $parent = $relative
    }
}
$componentIds = New-Object 'System.Collections.Generic.List[string]'
foreach ($item in $inventory) {
    $slash = $item.path.LastIndexOf('/')
    $parent = if ($slash -lt 0) { '' } else { $item.path.Substring(0,$slash) }
    $fileId = 'F_' + (Get-StableHash ('file/' + $item.path)).Substring(0,32)
    $lines.Add(('<DirectoryRef Id="{0}"><Component Id="{1}" Guid="{2}" Win64="yes">' -f $directories[$parent], $item.component, $item.guid))
    $lines.Add(('<File Id="{0}" Source="$(var.PackageDirectory)\{1}" />' -f $fileId, (Escape-Wix $item.path.Replace('/', '\'))))
    $lines.Add(('<RegistryValue Root="HKCU" Key="Software\8bitPad\Installer\Components" Name="{0}" Type="integer" Value="1" KeyPath="yes" />' -f $item.component))
    $lines.Add('</Component></DirectoryRef>')
    $componentIds.Add($item.component)
}
foreach ($relative in @($directories.Keys | Sort-Object)) {
    $id = 'Cleanup_' + (Get-StableHash ('dir/' + $relative)).Substring(0,32)
    $lines.Add(('<DirectoryRef Id="{0}"><Component Id="{1}" Guid="{2}" Win64="yes">' -f $directories[$relative], $id, (Get-StableGuid ('dir/' + $relative))))
    # RemoveFolder removes only an empty installer directory. No RemoveFile *,
    # profile path, AppData settings path, or recursive cleanup is authored.
    $lines.Add(('<RemoveFolder Id="R_{0}" On="uninstall" />' -f $id))
    $lines.Add(('<RegistryValue Root="HKCU" Key="Software\8bitPad\Installer\Components" Name="{0}" Type="integer" Value="1" KeyPath="yes" />' -f $id))
    $lines.Add('</Component></DirectoryRef>')
    $componentIds.Add($id)
}
$lines.Add('<ComponentGroup Id="ApplicationPayload">')
foreach ($id in $componentIds) { $lines.Add(('<ComponentRef Id="{0}" />' -f $id)) }
$lines.Add('</ComponentGroup></Fragment></Wix>')
$payloadSource = Join-Path $work 'Payload.wxs'
[IO.File]::WriteAllLines($payloadSource, $lines, $utf8)
$productCode = Get-StableGuid ('product/' + $Version)
$manifest = [ordered]@{ version=$Version; productCode=$productCode; upgradeCode='672CB70D-9BEE-47A3-9462-9013D2B29A44'; architecture='x64'; scope='perUser'; wixVersion='3.14.1'; files=$inventory }
[IO.File]::WriteAllText((Join-Path $work 'payload.json'), ($manifest | ConvertTo-Json -Depth 6), $utf8)

if (!$GenerateOnly) {
    $definitions = @(('-dProductVersion=' + $Version), ('-dProductCode=' + $productCode), ('-dPackageDirectory=' + $package), ('-dProductIcon=' + (Join-Path $repo 'icon/8bitPad.ico')), ('-dLicenseRtf=' + $licensePath))
    # Explicit output files avoid a quoted trailing directory backslash, which
    # Windows PowerShell 5.1 otherwise passes incorrectly to native programs.
    foreach ($unit in @(@{ source=$source; output=(Join-Path $work '8bitPad.wixobj') }, @{ source=$payloadSource; output=(Join-Path $work 'Payload.wixobj') })) {
        $candleArgs = @('-nologo', '-arch', 'x64', '-ext', (Join-Path $wixBin 'WixUIExtension.dll')) + $definitions + @('-out', $unit.output, $unit.source)
        & (Join-Path $wixBin 'candle.exe') @candleArgs 2>&1 | Tee-Object -FilePath (Join-Path $work 'candle.log') -Append | Write-Host
        if ($LASTEXITCODE) { throw "WiX compile failed ($LASTEXITCODE). See $work" }
    }
    # MSI ICE validation remains enabled. Failures are errors, not suppressed.
    $stagedMsi = Join-Path $work '8bitPad.msi'
    $lightArgs = @('-nologo', '-ext', (Join-Path $wixBin 'WixUIExtension.dll'), '-cultures:en-us', '-out', $stagedMsi, (Join-Path $work '8bitPad.wixobj'), (Join-Path $work 'Payload.wixobj'))
    & (Join-Path $wixBin 'light.exe') @lightArgs 2>&1 | Tee-Object -FilePath (Join-Path $work 'light.log') | Write-Host
    if ($LASTEXITCODE) { throw "WiX link/validation failed ($LASTEXITCODE). See $work" }
    if (!(Test-Path -LiteralPath $stagedMsi -PathType Leaf)) { throw 'WiX did not produce the requested MSI.' }
    # The final asset appears only after a successful link AND ICE validation.
    # Move has no overwrite behavior and remains atomic on this same volume.
    [IO.File]::Move($stagedMsi, $output)
}
[pscustomobject]@{ OutputMsi=$output; WorkDirectory=$work; ProductCode=$productCode; FileCount=$inventory.Count; GeneratedOnly=[bool]$GenerateOnly }
