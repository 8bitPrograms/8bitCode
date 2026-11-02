[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$Msi,
    [Parameter(Mandatory=$true)][string]$PayloadManifest,
    [Parameter(Mandatory=$true)][string]$WixRoot,
    [Parameter(Mandatory=$true)][string]$OutputDirectory
)
# Read-only MSI database inspection and cabinet extraction. Never calls msiexec,
# opens an install session, executes custom actions, or modifies registration.
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2
$Msi = (Resolve-Path -LiteralPath $Msi).Path
$PayloadManifest = (Resolve-Path -LiteralPath $PayloadManifest).Path
$WixRoot = (Resolve-Path -LiteralPath $WixRoot).Path
if (!(Test-Path -LiteralPath (Join-Path $WixRoot 'dark.exe'))) { $WixRoot = Join-Path $WixRoot 'bin' }
$dark = Join-Path $WixRoot 'dark.exe'
if (!(Test-Path -LiteralPath $dark -PathType Leaf)) { throw 'WiX dark.exe was not found.' }
if ([Diagnostics.FileVersionInfo]::GetVersionInfo($dark).ProductVersion -notmatch '^3\.14\.1\.') { throw 'Use pinned WiX 3.14.1 for validation.' }
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $OutputDirectory) { throw 'Choose a fresh validation output directory.' }
[IO.Directory]::CreateDirectory($OutputDirectory) | Out-Null
$manifest = [IO.File]::ReadAllText($PayloadManifest) | ConvertFrom-Json
$checks = New-Object 'System.Collections.Generic.List[string]'
function Assert([bool]$Condition, [string]$Message) {
    if (!$Condition) { throw "MSI validation failed: $Message" }
    $checks.Add($Message)
}
function Hash([string]$Path) {
    $hash = [Security.Cryptography.SHA256]::Create()
    $stream = [IO.File]::OpenRead($Path)
    try { return [BitConverter]::ToString($hash.ComputeHash($stream)).Replace('-', '').ToLowerInvariant() }
    finally { $stream.Dispose(); $hash.Dispose() }
}
function Rows($Database, [string]$Query, [string[]]$Columns) {
    $view = $Database.OpenView($Query)
    try {
        $view.Execute() | Out-Null
        while ($record = $view.Fetch()) {
            try {
                $row = [ordered]@{}
                for ($i=0; $i -lt $Columns.Count; $i++) { $row[$Columns[$i]] = $record.StringData($i+1) }
                [pscustomobject]$row
            } finally { [Runtime.InteropServices.Marshal]::ReleaseComObject($record) | Out-Null }
        }
    } finally { $view.Close() | Out-Null; [Runtime.InteropServices.Marshal]::ReleaseComObject($view) | Out-Null }
}
$installer = New-Object -ComObject WindowsInstaller.Installer
$database = $installer.OpenDatabase($Msi, 0) # msiOpenDatabaseModeReadOnly
try {
    $summary = $database.SummaryInformation(0)
    try { $platform = $summary.Property(7); $wordCount = [int]$summary.Property(15) }
    finally { [Runtime.InteropServices.Marshal]::ReleaseComObject($summary) | Out-Null }
    $propertyRows = @(Rows $database 'SELECT `Property`, `Value` FROM `Property`' @('Property','Value'))
    $properties = @{}
    foreach ($row in $propertyRows) { $properties[$row.Property] = $row.Value }
    $tables = @(Rows $database 'SELECT `Name` FROM `_Tables`' @('Name') | ForEach-Object { $_.Name })
    $files = @(Rows $database 'SELECT `File`, `Component_`, `FileName`, `FileSize` FROM `File`' @('File','Component','Name','Size'))
    $components = @(Rows $database 'SELECT `Component`, `ComponentId`, `Directory_`, `Attributes`, `KeyPath` FROM `Component`' @('Component','Guid','Directory','Attributes','KeyPath'))
    $registry = @(Rows $database 'SELECT `Registry`, `Root`, `Key`, `Name`, `Value` FROM `Registry`' @('Registry','Root','Key','Name','Value'))
    $shortcuts = @(Rows $database 'SELECT `Shortcut`, `Directory_`, `Name`, `Target`, `WkDir`, `Arguments` FROM `Shortcut`' @('Shortcut','Directory','Name','Target','WorkingDirectory','Arguments'))
    $actions = @(Rows $database 'SELECT `Action`, `Type`, `Source`, `Target` FROM `CustomAction`' @('Action','Type','Source','Target'))
    $sequence = @(Rows $database 'SELECT `Action`, `Sequence` FROM `InstallExecuteSequence`' @('Action','Sequence'))
    $remove = @(Rows $database 'SELECT `FileKey`, `FileName`, `DirProperty`, `InstallMode` FROM `RemoveFile`' @('FileKey','FileName','Directory','Mode'))
    $upgrades = @(Rows $database 'SELECT `UpgradeCode`, `VersionMin`, `VersionMax`, `Attributes` FROM `Upgrade`' @('UpgradeCode','VersionMin','VersionMax','Attributes'))
    Assert ($platform -eq 'x64;1033') 'Package platform is x64'
    Assert (($wordCount -band 8) -eq 8) 'Summary flags declare no elevated privileges'
    Assert (!$properties.ContainsKey('ALLUSERS') -or !$properties['ALLUSERS']) 'ALLUSERS does not request machine installation'
    Assert ($properties['ProductVersion'] -eq $manifest.version) 'Product version matches packaging manifest'
    Assert ($properties['ProductCode'].Trim('{}') -ieq $manifest.productCode) 'ProductCode matches version-derived manifest identity'
    Assert ($properties['UpgradeCode'].Trim('{}') -ieq $manifest.upgradeCode) 'Stable UpgradeCode matches manifest'
    Assert ($properties['MSIRESTARTMANAGERCONTROL'] -eq 'DisableShutdown' -and $properties['MSIDISABLERMRESTART'] -eq '1') 'Installer does not shut down or restart the application automatically'
    Assert ($files.Count -eq @($manifest.files).Count) 'MSI file table has exactly the manifested payload'
    Assert (@($components | Where-Object { ([int]$_.Attributes -band 260) -ne 260 }).Count -eq 0) 'Every component is x64 and uses a registry key path'
    Assert (@($registry | Where-Object { $_.Root -ne '1' -or $_.Key -ne 'Software\8bitPad\Installer\Components' }).Count -eq 0) 'Registry entries are only owned HKCU installer markers'
    $launch=@($shortcuts | Where-Object { $_.Target -eq '[INSTALLFOLDER]8bitCode.exe' })
    $uninstall=@($shortcuts | Where-Object { $_.Target -eq '[System64Folder]msiexec.exe' })
    Assert ($shortcuts.Count -eq 2 -and @($shortcuts | Where-Object { $_.Directory -ne 'BitPadMenuFolder' }).Count -eq 0 -and $launch.Count -eq 1 -and $launch[0].WorkingDirectory -eq 'INSTALLFOLDER') 'Launch and uninstall shortcuts share the 8bitPrograms folder'
    Assert ($uninstall.Count -eq 1 -and $uninstall[0].Arguments -eq ('/x {'+$manifest.productCode+'}')) 'Uninstall shortcut targets this exact ProductCode' 
    Assert (@($remove | Where-Object { $_.FileName -or $_.Mode -ne '2' -or ($_.Directory -notin 'INSTALLFOLDER','BitPadMenuFolder' -and $_.Directory -notmatch '^D_[a-f0-9]{32}$') }).Count -eq 0) 'Uninstall only removes empty 8bitPad-owned directories'
    foreach ($table in @('Extension','ProgId','Class','ServiceInstall','ServiceControl','Environment','RemoveRegistry')) {
        Assert ($tables -notcontains $table) ("No $table side effects are present")
    }
    $allowedActions = @('SetINSTALLFOLDER','SetARPINSTALLLOCATION','WixUIPrintEula')
    Assert (@($actions | Where-Object { $_.Action -notin $allowedActions }).Count -eq 0) 'Custom action inventory is restricted to two property setters and manual license printing'
    foreach ($action in $actions) {
        if ($action.Action -eq 'WixUIPrintEula') {
            Assert ($action.Type -eq '65' -and $action.Source -eq 'WixUIWixca' -and $action.Target -eq 'PrintEula' -and $sequence.Action -notcontains 'WixUIPrintEula') 'WiX license printing helper is never run during install execution'
        } else { Assert ($action.Type -eq '51') ($action.Action + ' only sets a property; it cannot execute a program') }
    }
    $setDirectory = @($actions | Where-Object { $_.Action -eq 'SetINSTALLFOLDER' })
    Assert ($setDirectory.Count -eq 1 -and $setDirectory[0].Source -eq 'INSTALLFOLDER' -and $setDirectory[0].Target -eq '[LocalAppDataFolder]Programs\8bitPad') 'Installation target is LocalAppData/Programs/8bitPad'
    $order = @{}
    foreach ($row in $sequence) { $order[$row.Action] = [int]$row.Sequence }
    Assert ($order['RemoveExistingProducts'] -gt $order['InstallInitialize'] -and $order['RemoveExistingProducts'] -lt $order['InstallFinalize']) 'Upgrade removal is inside the rollback transaction'
    Assert ($upgrades.Count -eq 2 -and @($upgrades | Where-Object { $_.UpgradeCode.Trim('{}') -ine $manifest.upgradeCode }).Count -eq 0) 'Both upgrade and downgrade detection use the stable UpgradeCode'
    $registryIds = @($registry | ForEach-Object { $_.Registry })
    Assert (@($components | Where-Object { $_.KeyPath -notin $registryIds }).Count -eq 0) 'Every component key path refers to an owned HKCU marker'
} finally {
    [Runtime.InteropServices.Marshal]::ReleaseComObject($database) | Out-Null
    [Runtime.InteropServices.Marshal]::ReleaseComObject($installer) | Out-Null
}
$extracted = Join-Path $OutputDirectory 'extracted'
$wxs = Join-Path $OutputDirectory 'decompiled.wxs'
& $dark -nologo -x $extracted -o $wxs $Msi 2>&1 | Tee-Object -FilePath (Join-Path $OutputDirectory 'dark.log') | Write-Host
if ($LASTEXITCODE) { throw "WiX dark extraction failed ($LASTEXITCODE)." }
[xml]$xml = [IO.File]::ReadAllText($wxs)
$ns = New-Object Xml.XmlNamespaceManager($xml.NameTable)
$ns.AddNamespace('w','http://schemas.microsoft.com/wix/2006/wi')
$fileNodes = @{}
foreach ($node in $xml.SelectNodes('//w:File', $ns)) { $fileNodes[$node.Id] = $node }
$filesByComponent = @{}
foreach ($file in $files) { $filesByComponent[$file.Component] = $file }
$componentsById = @{}
foreach ($component in $components) { $componentsById[$component.Component] = $component }
$verifiedPayload = Join-Path $OutputDirectory 'verified-payload'
[IO.Directory]::CreateDirectory($verifiedPayload) | Out-Null
$verified = @()
foreach ($item in $manifest.files) {
    $relative = [string]$item.path
    if ([IO.Path]::IsPathRooted($relative) -or $relative -match '(^|[\\/])\.\.?([\\/]|$)' -or $relative.Contains(':')) { throw 'Invalid manifest-relative output path.' }
    if ($relative -match '^(?i:profile)(?:/|$)|^(?i:Start-Portable\.cmd|SHA256SUMS\.txt)$') { throw 'Portable state unexpectedly present in installer manifest.' }
    $file = $filesByComponent[$item.component]
    if (!$file) { throw "Manifest component is missing from MSI: $relative" }
    if ($componentsById[$item.component].Guid.Trim('{}') -ine $item.guid) { throw "Component GUID mismatch: $relative" }
    if ([long]$file.Size -ne [long]$item.size) { throw "File table size mismatch: $relative" }
    $node = $fileNodes[$file.File]
    if (!$node) { throw "Extracted file node missing: $relative" }
    $source = [IO.Path]::GetFullPath([string]$node.Source)
    if (!$source.StartsWith($extracted + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw 'Extracted source escaped the validation directory.' }
    $digest = Hash $source
    if ($digest -ne $item.sha256) { throw "Extracted MSI payload hash mismatch: $relative" }
    $destination = Join-Path $verifiedPayload $relative
    [IO.Directory]::CreateDirectory((Split-Path -Parent $destination)) | Out-Null
    [IO.File]::Copy($source, $destination, $false)
    $verified += [pscustomobject]@{ path=$relative; sha256=$digest; size=[long]$file.Size }
}
Assert ($verified.Count -eq $files.Count) 'Every cabinet file matches its expected size, SHA256 and stable component identity'
$report = [ordered]@{ msi=$Msi; sha256=(Hash $Msi); passed=$checks.Count; checks=$checks; platform=$platform; wordCount=$wordCount; properties=$properties; files=$verified; components=$components; shortcuts=$shortcuts; customActions=$actions; upgrades=$upgrades; removeDirectories=$remove; verifiedPayload=$verifiedPayload }
[IO.File]::WriteAllText((Join-Path $OutputDirectory 'validation.json'), ($report | ConvertTo-Json -Depth 7), (New-Object Text.UTF8Encoding($false)))
[pscustomobject]@{ Passed=$checks.Count; FileCount=$verified.Count; VerifiedPayload=$verifiedPayload; Report=(Join-Path $OutputDirectory 'validation.json') }
