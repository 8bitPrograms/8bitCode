[CmdletBinding()]
param([string]$OutputDirectory = '', [string]$WixRoot = '')
# Structural tests only: temporary synthetic payloads, no MSI installation,
# registry access, associations, existing profiles, or user documents.
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2
$packageScript = Join-Path $PSScriptRoot 'package-msi.ps1'
if (!$OutputDirectory) { $OutputDirectory = Join-Path ([IO.Path]::GetTempPath()) ('8bitpad-msi-tests-' + [guid]::NewGuid().ToString('N')) }
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $OutputDirectory) { throw 'Choose a fresh test output directory.' }
[IO.Directory]::CreateDirectory($OutputDirectory) | Out-Null
$script:checks = New-Object 'System.Collections.Generic.List[string]'
function Assert([bool]$Condition, [string]$Message) {
    if (!$Condition) { throw "FAILED: $Message" }
    $script:checks.Add($Message)
}
function Expect-Failure([scriptblock]$Action, [string]$Expected, [string]$Message) {
    $failure = ''
    try { & $Action | Out-Null } catch { $failure = $_.Exception.Message }
    Assert ($failure -like ('*' + $Expected + '*')) ($Message + ': ' + $failure)
}
function Write-Text([string]$Path, [string]$Text) {
    [IO.Directory]::CreateDirectory((Split-Path -Parent $Path)) | Out-Null
    [IO.File]::WriteAllText($Path, $Text, (New-Object Text.UTF8Encoding($false)))
}
function Update-Manifest([string]$Package) {
    $entries = New-Object 'System.Collections.Generic.List[string]'
    foreach ($file in Get-ChildItem -LiteralPath $Package -File -Recurse | Sort-Object FullName) {
        $relative = $file.FullName.Substring($Package.Length + 1).Replace('\', '/')
        if ($relative -eq 'SHA256SUMS.txt' -or $relative -like 'profile/*') { continue }
        $hash = [Security.Cryptography.SHA256]::Create()
        $stream = [IO.File]::OpenRead($file.FullName)
        try { $digest = [BitConverter]::ToString($hash.ComputeHash($stream)).Replace('-', '').ToLowerInvariant() }
        finally { $stream.Dispose(); $hash.Dispose() }
        $entries.Add($digest + '  ' + $relative)
    }
    [IO.File]::WriteAllLines((Join-Path $Package 'SHA256SUMS.txt'), $entries, (New-Object Text.UTF8Encoding($false)))
}
$accented = 'caf' + [char]0xe9
$unicodeDoc = 'doc/hello & ' + $accented + '.txt'
$package = Join-Path $OutputDirectory ('bundle space & ' + $accented)
[IO.Directory]::CreateDirectory($package) | Out-Null
$bytes = New-Object byte[] 512
$bytes[0] = 0x4d; $bytes[1] = 0x5a; $bytes[0x3c] = 0x80
$bytes[0x80] = 0x50; $bytes[0x81] = 0x45; $bytes[0x84] = 0x64; $bytes[0x85] = 0x86
[IO.File]::WriteAllBytes((Join-Path $package '8bitCode.exe'), $bytes)
foreach ($relative in @('8p.exe', '8bitpad.exe', 'Qt6Core.dll', 'Qt6Core5Compat.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll', 'Qt6Network.dll', 'platforms/qwindows.dll', 'msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll', 'qt.conf', 'LICENSE', 'THIRD_PARTY_NOTICES.md', 'licenses/Qt.txt', 'theme-licenses/ayu.txt', $unicodeDoc, 'Start-Portable.cmd', 'profile/session.json')) {
    Write-Text (Join-Path $package $relative) ('fixture ' + $relative)
}
Write-Text (Join-Path $package '8bitPadFileIcons.dll') 'native icon handler fixture'
Update-Manifest $package
$first = & $packageScript -PackageDirectory $package -OutputMsi (Join-Path $OutputDirectory '8bitPad-0.1.0-windows-x64.msi') -Version 0.1.0 -GenerateOnly
$manifest = Get-Content -LiteralPath (Join-Path $first.WorkDirectory 'payload.json') -Raw -Encoding UTF8 | ConvertFrom-Json
[xml]$payloadXml = Get-Content -LiteralPath (Join-Path $first.WorkDirectory 'Payload.wxs') -Raw -Encoding UTF8
[xml]$productXml = Get-Content -LiteralPath (Join-Path $first.WorkDirectory '8bitPad.wxs') -Raw -Encoding UTF8
$ns = New-Object Xml.XmlNamespaceManager($payloadXml.NameTable)
$ns.AddNamespace('w', 'http://schemas.microsoft.com/wix/2006/wi')
$pns = New-Object Xml.XmlNamespaceManager($productXml.NameTable)
$pns.AddNamespace('w', 'http://schemas.microsoft.com/wix/2006/wi')
$paths = @($manifest.files | ForEach-Object { $_.path })
Assert ($first.GeneratedOnly -and !(Test-Path -LiteralPath $first.OutputMsi)) 'GenerateOnly never creates or installs an MSI'
Assert ($paths.Count -eq 19) 'Complete fixture runtime, command aliases, native icons, plugins and license payload is included'
Assert (($paths -contains '8p.exe') -and ($paths -contains '8bitpad.exe')) 'Both compatibility commands are installed'
Assert ($productXml.Wix.Product.Name -eq '8bitCode') 'The product displays the Code brand'
Assert ($productXml.Wix.Product.UpgradeCode -eq '672CB70D-9BEE-47A3-9462-9013D2B29A44') 'The original Pad upgrade identity is retained'
Assert ($paths -contains '8bitPadFileIcons.dll') 'Native Explorer icon handler is included'
Assert ($null -ne $productXml.SelectSingleNode('//w:Component[@Id="PadFileIconHandler"]/w:RegistryValue[@KeyPath="yes"][@Value="[INSTALLFOLDER]8bitPadFileIcons.dll"]', $pns)) 'MSI owns native icon server registration and removal'
Assert (!(@($paths | Where-Object { $_ -like 'profile/*' -or $_ -in 'Start-Portable.cmd','SHA256SUMS.txt' }).Count)) 'Portable launcher, profile and old checksum manifest are excluded'
Assert ($paths -contains $unicodeDoc) 'Spaces, XML metacharacters and Unicode file paths survive authoring'
$pkg = $productXml.SelectSingleNode('//w:Package', $pns)
Assert ($pkg.InstallScope -eq 'perUser' -and $pkg.InstallPrivileges -eq 'limited') 'Installation is fixed per-user with limited privileges'
Assert ($null -ne $productXml.SelectSingleNode('//w:SetDirectory[@Id="INSTALLFOLDER"][@Value="[LocalAppDataFolder]Programs\8bitPad"]', $pns)) 'Default install directory is LocalAppData/Programs/8bitPad'
Assert ($null -eq $productXml.SelectSingleNode('//w:Directory[@Name="Programs"]', $pns)) 'Shared Programs parent is not owned or removed by this installer'
Assert ($null -ne $productXml.SelectSingleNode('//w:Shortcut[@Target="[INSTALLFOLDER]8bitCode.exe"]', $pns)) 'Start Menu shortcut targets installed executable'
Assert ($productXml.SelectSingleNode('//w:Directory[@Id="BitPadMenuFolder"]', $pns).Name -eq '8bitPrograms') 'Both products share the 8bitPrograms Start Menu group'
Assert ($null -ne $productXml.SelectSingleNode('//w:Shortcut[@Id="BitPadUninstall"][@Arguments="/x [ProductCode]"][@Target="[System64Folder]msiexec.exe"]', $pns)) 'Uninstall shortcut removes the installed MSI product'
Assert ($productXml.SelectSingleNode('//w:Product', $pns).Manufacturer -eq '8bitPrograms') 'Installed apps lists the 8bitPrograms publisher'
Assert ($productXml.SelectSingleNode('//w:MajorUpgrade', $pns).Schedule -eq 'afterInstallInitialize') 'Major upgrade removal is inside MSI rollback transaction'
$components = @($payloadXml.SelectNodes('//w:Component', $ns)) + @($productXml.SelectNodes('//w:Component', $pns))
Assert (@($components | Where-Object { $_.Win64 -ne 'yes' -or $_.SelectNodes('w:RegistryValue[@Root="HKCU"][@KeyPath="yes"]', $pns).Count -ne 1 -or $_.SelectNodes('w:RegistryValue[@Root!="HKCU"]', $pns).Count -ne 0 }).Count -eq 0) 'Every component is x64 with a per-user HKCU key path'
Assert ($payloadXml.SelectNodes('//w:RemoveFile | //w:RemoveRegistryKey | //w:CustomAction', $ns).Count -eq 0 -and $productXml.SelectNodes('//w:CustomAction | //w:RemoveRegistryKey | //w:RemoveFile | //w:ProgId | //w:Extension', $pns).Count -eq 0) 'No recursive deletion, executable custom actions or association changes'
Assert ($productXml.SelectNodes('//w:Property[@Id="ALLUSERS"] | //w:Publish[@Event="DoAction"]', $pns).Count -eq 0) 'No machine scope or auto-launch authoring'
Assert ($null -ne $productXml.SelectSingleNode('//w:Property[@Id="MSIDISABLERMRESTART"][@Value="1"]', $pns)) 'Restart Manager cannot relaunch the app after upgrade'
Assert ($payloadXml.SelectNodes('//w:RemoveFolder[@On="uninstall"]', $ns).Count -gt 0) 'Only empty installer directories are cleaned on uninstall'
$copy = Join-Path $OutputDirectory 'second source location'
Copy-Item -LiteralPath $package -Destination $copy -Recurse
$second = & $packageScript -PackageDirectory $copy -OutputMsi (Join-Path $OutputDirectory '8bitPad-0.1.1-windows-x64.msi') -Version 0.1.1 -GenerateOnly
$next = Get-Content -LiteralPath (Join-Path $second.WorkDirectory 'payload.json') -Raw -Encoding UTF8 | ConvertFrom-Json
Assert (($manifest.files.guid -join ',') -eq ($next.files.guid -join ',') -and ($manifest.files.component -join ',') -eq ($next.files.component -join ',')) 'Component identities survive different source roots and new release versions'
Assert ($manifest.productCode -ne $next.productCode -and $manifest.upgradeCode -eq $next.upgradeCode) 'New version gets a new ProductCode and retains UpgradeCode'
$third = & $packageScript -PackageDirectory $copy -OutputMsi (Join-Path $OutputDirectory 'same-version.msi') -Version 0.1.0 -GenerateOnly
Assert ($third.ProductCode -eq $first.ProductCode) 'Rebuilding the same version retains ProductCode'
Expect-Failure { & $packageScript -PackageDirectory $package -OutputMsi (Join-Path $OutputDirectory 'bad-version.msi') -Version 0.1.0-preview -GenerateOnly } 'Version must' 'Prereleases are not silently truncated to an MSI version'
Expect-Failure { & $packageScript -PackageDirectory $package -OutputMsi (Join-Path $package 'inside.msi') -GenerateOnly } 'outside the portable package' 'Installer output cannot pollute its source bundle'
Write-Text (Join-Path $OutputDirectory 'existing.msi') 'do not replace'
Expect-Failure { & $packageScript -PackageDirectory $package -OutputMsi (Join-Path $OutputDirectory 'existing.msi') -GenerateOnly } 'Refusing to overwrite' 'Existing installer output is preserved'
Write-Text (Join-Path $copy 'Qt6Core.dll') 'changed after packaging'
Expect-Failure { & $packageScript -PackageDirectory $copy -OutputMsi (Join-Path $OutputDirectory 'changed.msi') -GenerateOnly } 'checksum mismatch' 'Modified payload bytes fail checksum validation'
Copy-Item -LiteralPath (Join-Path $package 'Qt6Core.dll') -Destination (Join-Path $copy 'Qt6Core.dll')
Write-Text (Join-Path $copy 'unexpected.txt') 'unmanifested'
Expect-Failure { & $packageScript -PackageDirectory $copy -OutputMsi (Join-Path $OutputDirectory 'extra.msi') -GenerateOnly } 'not in portable checksum manifest' 'Unmanifested extra files cannot silently enter the installer'
$bytes[0x84] = 0x4c; $bytes[0x85] = 0x01
[IO.File]::WriteAllBytes((Join-Path $copy '8bitCode.exe'), $bytes)
Expect-Failure { & $packageScript -PackageDirectory $copy -OutputMsi (Join-Path $OutputDirectory 'x86.msi') -GenerateOnly } 'Windows x64' 'An x86 executable cannot be labelled as an x64 installer'
if ($WixRoot) {
    # Optional real-tool integration regression: quoted output path with spaces,
    # ampersand, and a Unicode input path. Still builds only a dummy fixture MSI;
    # never installs it or starts the synthetic executable.
    $built = & $packageScript -PackageDirectory $package -OutputMsi (Join-Path $OutputDirectory 'tool quoting & test/fixture.msi') -Version 0.1.0 -WixRoot $WixRoot
    Assert (!$built.GeneratedOnly -and (Test-Path -LiteralPath $built.OutputMsi -PathType Leaf)) 'Native WiX invocation supports spaces, ampersands and Unicode paths'
    $validation = & (Join-Path $PSScriptRoot 'validate-msi.ps1') -Msi $built.OutputMsi -PayloadManifest (Join-Path $built.WorkDirectory 'payload.json') -WixRoot $WixRoot -OutputDirectory (Join-Path $OutputDirectory 'fixture-validation')
    Assert ($validation.FileCount -eq 17 -and $validation.Passed -ge 28) 'Built fixture MSI passes native table and extracted-byte validation'
}
[pscustomobject]@{ passed=$script:checks.Count; checks=$script:checks; output=$OutputDirectory } | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'results.json') -Encoding UTF8
Write-Output "MSI structural tests passed: $($script:checks.Count). Results: $OutputDirectory"
