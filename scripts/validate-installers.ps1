[CmdletBinding()]
param(
 [Parameter(Mandatory=$true)][string]$Msi,
 [Parameter(Mandatory=$true)][string]$Setup,
 [Parameter(Mandatory=$true)][string]$PayloadManifest,
 [Parameter(Mandatory=$true)][string]$WixExe,
 [Parameter(Mandatory=$true)][string]$OutputDirectory
)
# Read-only MSI inspection, cabinet extraction and bootstrap checks. No install.
$ErrorActionPreference='Stop'
$Msi=(Resolve-Path -LiteralPath $Msi).Path
$Setup=(Resolve-Path -LiteralPath $Setup).Path
$WixExe=(Resolve-Path -LiteralPath $WixExe).Path
$manifest=Get-Content -LiteralPath $PayloadManifest -Raw | ConvertFrom-Json
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
if(Test-Path -LiteralPath $OutputDirectory){throw 'Choose a fresh validation directory.'}
[IO.Directory]::CreateDirectory($OutputDirectory) | Out-Null
$checks=New-Object 'System.Collections.Generic.List[string]'
function Assert([bool]$ok,[string]$message){if(!$ok){throw $message};$checks.Add($message)}
function Hash([string]$path){$h=[Security.Cryptography.SHA256]::Create();$s=[IO.File]::OpenRead($path);try{[BitConverter]::ToString($h.ComputeHash($s)).Replace('-','').ToLowerInvariant()}finally{$s.Dispose();$h.Dispose()}}
function Rows($db,[string]$query,[string[]]$columns){
 $v=$db.OpenView($query)
 try{$v.Execute() | Out-Null;while($r=$v.Fetch()){try{$o=[ordered]@{};for($j=0;$j -lt $columns.Count;$j++){$o[$columns[$j]]=$r.StringData($j+1)};[pscustomobject]$o}finally{[Runtime.InteropServices.Marshal]::ReleaseComObject($r)|Out-Null}}}
 finally{$v.Close() | Out-Null;[Runtime.InteropServices.Marshal]::ReleaseComObject($v)|Out-Null}
}
$installer=New-Object -ComObject WindowsInstaller.Installer
$db=$installer.OpenDatabase($Msi,0)
try{
 $properties=@{};foreach($r in Rows $db 'SELECT `Property`, `Value` FROM `Property`' @('Name','Value')){$properties[$r.Name]=$r.Value}
 $app=$properties['ProductName'];$legacy=if($app -eq '8bitCode'){'8bitPad'}else{'8bitNotes'}
 $upgrade=if($app -eq '8bitCode'){'672CB70D-9BEE-47A3-9462-9013D2B29A44'}else{'A21FE678-3130-495B-8F0D-BC0E32886490'}
 Assert ($app -in @('8bitCode','8bitNotes')) 'Recognized product name'
 Assert ($properties['Manufacturer'] -eq '8bitPrograms') 'Manufacturer is 8bitPrograms'
 Assert ($properties['UpgradeCode'].Trim('{}') -eq $upgrade -and $manifest.upgradeCode -eq $upgrade) 'Independent stable upgrade identity'
 Assert ($properties['ProductCode'].Trim('{}') -eq $manifest.productCode) 'Version-derived product identity'
 Assert ($properties['ProductVersion'] -eq $manifest.version) 'Product version matches payload manifest'
 Assert (!$properties.ContainsKey('ALLUSERS')) 'Per-user installation'
 $summary=$db.SummaryInformation(0)
 Assert ($summary.Property(7) -eq 'x64;1033' -and (([int]$summary.Property(15) -band 8) -eq 8)) 'x64 package requires no elevation'
 [Runtime.InteropServices.Marshal]::ReleaseComObject($summary)|Out-Null
 $shortcuts=@(Rows $db 'SELECT `Name`, `Target`, `Arguments` FROM `Shortcut`' @('Name','Target','Arguments'))
 Assert ($shortcuts.Count -eq 2 -and @($shortcuts|Where-Object {$_.Target -eq "[INSTALLFOLDER]$app.exe"}).Count -eq 1) 'Application Start menu shortcut'
 $uninstall=@($shortcuts|Where-Object {$_.Target -eq '[System64Folder]msiexec.exe'})
 Assert ($uninstall.Count -eq 1 -and $uninstall[0].Arguments -eq '/x [ProductCode]' -and $uninstall[0].Name.EndsWith("Uninstall $app")) 'Uninstall shortcut removes this MSI product'
 $remove=@(Rows $db 'SELECT `FileName`, `InstallMode` FROM `RemoveFile`' @('Name','Mode'))
 Assert (@($remove|Where-Object {$_.Name -or $_.Mode -ne '2'}).Count -eq 0) 'Uninstall removes only empty owned directories; no file wildcards'
 $registry=@(Rows $db 'SELECT `Root`, `Key` FROM `Registry`' @('Root','Key'))
 Assert (@($registry|Where-Object {$_.Root -ne '1' -or ($_.Key -ne "Software\$legacy\Installer\Components" -and !($app -eq '8bitCode' -and $_.Key.StartsWith('Software\Classes\CLSID\{D325870F-4B1C-4D26-A03C-3575B1607635}')))}).Count -eq 0) 'Registry writes limited to own HKCU installer markers and Code icon handler'
 $actions=@(Rows $db 'SELECT `Action`, `Type`, `Target` FROM `CustomAction`' @('Name','Type','Target'))
 Assert (@($actions|Where-Object {$_.Name -notmatch '^Set(INSTALLFOLDER|ARPINSTALLLOCATION)$|^WixUIPrintEula(_X64)?$'}).Count -eq 0) 'No arbitrary installation or user-data cleanup programs'
 $dir=@($actions|Where-Object {$_.Name -eq 'SetINSTALLFOLDER'})
 Assert ($dir.Count -eq 1 -and $dir[0].Target -eq "[LocalAppDataFolder]Programs\$legacy") 'Expected per-user installation folder'
 $environment=@(Rows $db 'SELECT `Name`, `Value` FROM `Environment`' @('Name','Value'))
 Assert ($environment.Count -eq 1 -and $environment[0].Value -eq '[~];[INSTALLFOLDER]' -and $environment[0].Name -eq '=-PATH') 'PATH adds and removes only this application directory'
 $files=@(Rows $db 'SELECT `File`, `Component_`, `FileSize` FROM `File`' @('Id','Component','Size'))
 $components=@{};foreach($r in Rows $db 'SELECT `Component`, `ComponentId`, `Attributes` FROM `Component`' @('Id','Guid','Attributes')){$components[$r.Id]=$r}
 Assert ($files.Count -eq @($manifest.files).Count) 'MSI has exactly the intended payload files'
 Assert (@($components.Values|Where-Object {([int]$_.Attributes -band 260) -ne 260}).Count -eq 0) 'Components use x64 and per-user registry key paths'
 $sequence=@{};foreach($r in Rows $db 'SELECT `Action`, `Sequence` FROM `InstallExecuteSequence`' @('Name','Order')){$sequence[$r.Name]=[int]$r.Order}
 Assert ($sequence.RemoveExistingProducts -gt $sequence.InstallInitialize -and $sequence.RemoveExistingProducts -lt $sequence.InstallFinalize) 'Upgrade removal is covered by MSI rollback'
}finally{[Runtime.InteropServices.Marshal]::ReleaseComObject($db)|Out-Null;[Runtime.InteropServices.Marshal]::ReleaseComObject($installer)|Out-Null}
$extracted=Join-Path $OutputDirectory 'extracted';$wxs=Join-Path $OutputDirectory 'decompiled.wxs'
& $WixExe msi decompile $Msi -x $extracted -out $wxs *> (Join-Path $OutputDirectory 'decompile.log')
if($LASTEXITCODE){throw 'MSI cabinet extraction failed.'}
$xml=[xml][IO.File]::ReadAllText($wxs);$nodes=@{}
foreach($n in $xml.SelectNodes('//*[local-name()="File"]')){$nodes[$n.Id]=$n}
$byComponent=@{};foreach($f in $files){$byComponent[$f.Component]=$f}
foreach($item in $manifest.files){
 $file=$byComponent[$item.component];$node=$nodes[$file.Id]
 Assert ($null -ne $node -and [long]$file.Size -eq [long]$item.size -and $components[$item.component].Guid.Trim('{}') -eq $item.guid) ("File identity/size: "+$item.path)
 # WiX 6 authors SourceDir\File\<id>; -x places that file under
 # the requested extraction directory rather than making Source absolute.
 $relative=([string]$node.Source) -replace '^SourceDir[\\/]',''
 if([IO.Path]::IsPathRooted($relative) -or $relative -match '(^|[\\/])\.\.([\\/]|$)'){throw 'Invalid extracted source path.'}
 $path=[IO.Path]::GetFullPath((Join-Path $extracted $relative))
 Assert ($path.StartsWith($extracted+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase) -and (Hash $path) -eq $item.sha256) ("Cabinet SHA256: "+$item.path)
}
function RunSetup([string]$arguments){
 $s=New-Object Diagnostics.ProcessStartInfo;$s.FileName=$Setup;$s.Arguments=$arguments;$s.UseShellExecute=$false;$s.CreateNoWindow=$true
 $p=[Diagnostics.Process]::Start($s);try{if(!$p.WaitForExit(60000)){$p.Kill();throw 'Setup check timed out.'};return $p.ExitCode}finally{$p.Dispose()}
}
$copy=Join-Path $OutputDirectory 'embedded.msi'
Assert ((RunSetup ('--extract-msi "'+$copy+'"')) -eq 0 -and (Hash $copy) -eq (Hash $Msi)) 'EXE contains byte-identical MSI'
Assert ((RunSetup ('--extract-msi "'+$copy+'"')) -eq 80 -and (Hash $copy) -eq (Hash $Msi)) 'EXE refuses to overwrite extraction targets'
Assert ((RunSetup '/quiet /unsupported') -eq 87) 'EXE rejects unsupported arguments'
$report=[ordered]@{application=$app;version=$manifest.version;msiSha256=(Hash $Msi);setupSha256=(Hash $Setup);passed=$checks.Count;payloadFiles=$files.Count;checks=$checks;windowsInstallerIce='Required separately before publishing';installUninstallLifecycle='Not run against the user profile'}
[IO.File]::WriteAllText((Join-Path $OutputDirectory 'validation.json'),($report|ConvertTo-Json -Depth 6),[Text.UTF8Encoding]::new($false))
[pscustomobject]@{Passed=$checks.Count;PayloadFiles=$files.Count;Report=(Join-Path $OutputDirectory 'validation.json')}
