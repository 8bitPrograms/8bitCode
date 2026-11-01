[CmdletBinding()]
param(
 [Parameter(Mandatory=$true)][string]$Stage,
 [Parameter(Mandatory=$true)][string]$OutputDirectory,
 [ValidateSet('x64','arm64','x86')][string]$Architecture='x64',
 [switch]$GenerateOnly
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version 2
function Get-Bit8Python {
    if($env:BIT8_PYTHON){
        if(Test-Path -LiteralPath $env:BIT8_PYTHON){ return (Resolve-Path -LiteralPath $env:BIT8_PYTHON).Path }
        throw "BIT8_PYTHON does not exist: $($env:BIT8_PYTHON)"
    }
    $commands=@(Get-Command python.exe,python3.exe,python -All -ErrorAction SilentlyContinue)
    foreach($command in $commands){
        if($command.Source -and $command.Source -notmatch '\\WindowsApps\\'){ return $command.Source }
    }
    throw 'Python 3 was not found on PATH. Set BIT8_PYTHON to python.exe.'
}
$python=Get-Bit8Python
$repo=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$config=Get-Content -LiteralPath (Join-Path $repo 'packaging/release.json') -Raw | ConvertFrom-Json
$app=$config.app
$publisher=$config.publisher
$menuGroup=$config.startMenuFolder
$version=& $python (Join-Path $PSScriptRoot 'distribution.py') version
if($LASTEXITCODE){throw 'Could not read version'}
$version=$version.Trim()
$Stage=[IO.Path]::GetFullPath((Resolve-Path -LiteralPath $Stage).Path).TrimEnd('\')
function To-StageRelative([string]$full){
    $base=$Stage+'\'
    $path=[IO.Path]::GetFullPath($full)
    if(-not $path.StartsWith($base,[StringComparison]::OrdinalIgnoreCase)){throw "Path is outside the stage: $path"}
    return $path.Substring($base.Length)
}
$OutputDirectory=if([IO.Path]::IsPathRooted($OutputDirectory)){[IO.Path]::GetFullPath($OutputDirectory)}else{[IO.Path]::GetFullPath((Join-Path $repo $OutputDirectory))}
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$stream=[IO.File]::OpenRead((Join-Path $Stage "bin/$app.exe"));$reader=New-Object IO.BinaryReader($stream)
try{
 if($reader.ReadUInt16() -ne 0x5a4d){throw 'Invalid PE executable'}
 $stream.Position=0x3c;$offset=$reader.ReadUInt32();$stream.Position=$offset
 if($reader.ReadUInt32() -ne 0x4550){throw 'Missing PE signature'}
 if($reader.ReadUInt16() -ne @{x64=0x8664;arm64=0xaa64;x86=0x14c}[$Architecture]){throw 'Wrong payload architecture'}
}finally{$reader.Dispose();$stream.Dispose()}
foreach($relative in @('bin/Apply-Update.ps1','bin/Qt6Core.dll','bin/msvcp140.dll','bin/vcruntime140.dll','bin/qt.conf','plugins/platforms/qwindows.dll')){
 if(!(Test-Path -LiteralPath (Join-Path $Stage $relative))){throw "Missing runtime: $relative"}
}
$applicationFiles=if($app -eq '8bitShell'){@('bin/Qt6Qml.dll','bin/Qt6Quick.dll','bin/Qt6Network.dll','bin/Qt6Svg.dll','bin/puttygen.exe','bin/openssh/ssh.exe','bin/openssh/sftp.exe','bin/openssh/ssh-keygen.exe')}else{@('bin/8p.exe','bin/8bitpad.exe','bin/Qt6Widgets.dll','bin/Qt6Network.dll','bin/Qt6Svg.dll','bin/Qt6Core5Compat.dll')}
foreach($relative in $applicationFiles){if(!(Test-Path -LiteralPath (Join-Path $Stage $relative))){throw "Missing application runtime: $relative"}}
if($app -eq '8bitCode' -and !(Test-Path -LiteralPath (Join-Path $Stage 'bin/8bitCodeFileIcons.dll'))){
 throw 'Missing native file icon handler'
}
$work=Join-Path $repo "build/installer-$Architecture-$([guid]::NewGuid().ToString('N'))"
New-Item -ItemType Directory -Path $work | Out-Null
function Xml([string]$s){return [Security.SecurityElement]::Escape($s.Replace('$(','$$('))}
function Hash([string]$s){
 $h=[Security.Cryptography.SHA256]::Create()
 try{return [BitConverter]::ToString($h.ComputeHash([Text.Encoding]::UTF8.GetBytes($s))).Replace('-','').ToLowerInvariant()}
 finally{$h.Dispose()}
}
function GuidFor([string]$s){
 $h=Hash "$app/$Architecture/$s"
 return ('{0}-{1}-8{2}-a{3}-{4}' -f $h.Substring(0,8),$h.Substring(8,4),$h.Substring(13,3),$h.Substring(17,3),$h.Substring(20,12)).ToUpperInvariant()
}
$win64=if($Architecture -eq 'x86'){'no'}else{'yes'}
$lines=New-Object 'Collections.Generic.List[string]'
$components=New-Object 'Collections.Generic.List[string]'
$lines.Add('<?xml version="1.0" encoding="utf-8"?><Wix xmlns="http://schemas.microsoft.com/wix/2006/wi">')
$lines.Add(('<Product Id="{0}" Name="{1}" Language="1033" Version="{2}" Manufacturer="{4}" UpgradeCode="{3}">' -f (GuidFor "product/$version"),$app,$version,$config.upgradeCode,(Xml $publisher)))
$lines.Add(('<Package InstallerVersion="500" Compressed="yes" InstallScope="perUser" Platform="{0}"/>' -f $Architecture))
$lines.Add('<MajorUpgrade DowngradeErrorMessage="A newer version is installed." Schedule="afterInstallInitialize"/><MediaTemplate EmbedCab="yes"/>')
# Windows Installer registers each product in Installed apps; expose the install
# location and suppress a misleading Modify entry.
$lines.Add('<Property Id="ARPNOMODIFY" Value="1"/><Property Id="MSIRESTARTMANAGERCONTROL" Value="DisableShutdown"/><Property Id="MSIDISABLERMRESTART" Value="1"/><SetProperty Id="ARPINSTALLLOCATION" Value="[INSTALLDIR]" After="CostFinalize"/>')
$lines.Add(('<Icon Id="AppIcon" SourceFile="{0}"/><Property Id="ARPPRODUCTICON" Value="AppIcon"/>' -f (Xml (Join-Path $repo $config.iconIco))))
$lines.Add(('<Directory Id="TARGETDIR" Name="SourceDir"><Directory Id="LocalAppDataFolder"><Directory Id="Programs" Name="Programs"><Directory Id="INSTALLDIR" Name="{0}">' -f $app))
$dirs=@{''='INSTALLDIR'}
foreach($d in Get-ChildItem -LiteralPath $Stage -Recurse -Directory | Sort-Object { $_.FullName.Length }){
 if(($d.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0){throw 'Links are not allowed in Windows payload'}
 $rel=To-StageRelative $d.FullName;$dirs[$rel]='d'+(Hash $rel).Substring(0,24)
}
$lines.Add('</Directory></Directory></Directory><Directory Id="ProgramMenuFolder"><Directory Id="MenuDir" Name="'+(Xml $menuGroup)+'"/></Directory></Directory>')
foreach($rel in $dirs.Keys | Where-Object {$_} | Sort-Object Length){
 $parent=Split-Path -Parent $rel;$name=Split-Path -Leaf $rel
 $lines.Add(('<DirectoryRef Id="{0}"><Directory Id="{1}" Name="{2}"/></DirectoryRef>' -f $dirs[$parent],$dirs[$rel],(Xml $name)))
}
foreach($f in Get-ChildItem -LiteralPath $Stage -Recurse -File | Sort-Object FullName){
 $rel=To-StageRelative $f.FullName
 if($rel -match '^(?i:profile)(\\|$)' -or $f.Name -like 'Start-Portable*'){continue}
 if(($f.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0){throw "Payload contains a link: $rel"}
 $id='c'+(Hash $rel).Substring(0,24);$components.Add($id)
 $fileId=if($rel -eq "bin\$app.exe"){'APP_EXE'}else{'f'+(Hash $rel).Substring(0,24)}
 $lines.Add(('<DirectoryRef Id="{0}"><Component Id="{1}" Guid="{2}" Win64="{3}"><File Id="{4}" Source="{5}"/><RegistryValue Root="HKCU" Key="Software\{6}\Installer" Name="{1}" Type="integer" Value="1" KeyPath="yes"/></Component></DirectoryRef>' -f $dirs[(Split-Path -Parent $rel)],$id,(GuidFor $rel),$win64,$fileId,(Xml $f.FullName),$app))
}
# Append only this application's command directory to the user PATH.
if($app -eq '8bitCode'){
 # MSI owns the COM server registration, including uninstall/repair. File-type
 # choices and the generic Open With icon handler are enabled by Pad itself.
 $components.Add('PadFileIconHandler')
 $classKey='Software\Classes\CLSID\{D325870F-4B1C-4D26-A03C-3575B1607635}'
 $lines.Add(('<DirectoryRef Id="INSTALLDIR"><Component Id="PadFileIconHandler" Guid="{0}" Win64="{1}"><RegistryValue Root="HKCU" Key="{2}" Type="string" Value="8bitCode file icons"/><RegistryValue Root="HKCU" Key="{2}\InprocServer32" Type="string" Value="[INSTALLDIR]bin\8bitCodeFileIcons.dll" KeyPath="yes"/><RegistryValue Root="HKCU" Key="{2}\InprocServer32" Name="ThreadingModel" Type="string" Value="Apartment"/></Component></DirectoryRef>' -f (GuidFor 'native-file-icon-handler'),$win64,$classKey))
}
# MSI removes that segment on uninstall and leaves other entries intact.
$components.Add('UserCommandPath')
$lines.Add(('<DirectoryRef Id="INSTALLDIR"><Component Id="UserCommandPath" Guid="{0}" Win64="{1}"><Environment Id="UserCommandPathValue" Name="PATH" Action="set" Part="last" System="no" Permanent="no" Value="[INSTALLDIR]bin"/><RegistryValue Root="HKCU" Key="Software\{2}\Installer" Name="UserCommandPath" Type="integer" Value="1" KeyPath="yes"/></Component></DirectoryRef>' -f (GuidFor 'user-command-path'),$win64,$app))
$systemFolder=if($Architecture -eq 'x86'){'SystemFolder'}else{'System64Folder'}
$lines.Add(('<DirectoryRef Id="MenuDir"><Component Id="StartMenu" Guid="{0}" Win64="{1}"><Shortcut Id="AppShortcut" Name="{2}" Target="[#APP_EXE]" WorkingDirectory="INSTALLDIR" Icon="AppIcon"/><Shortcut Id="UninstallShortcut" Name="Uninstall {2}" Description="Remove {2}; keep settings and saved work" Target="[{3}]msiexec.exe" Arguments="/x [ProductCode]" WorkingDirectory="{3}" Icon="AppIcon"/>' -f (GuidFor 'shortcut'),$win64,$app,$systemFolder))
# Remove only empty owned directories. The shared Start Menu folder remains
# while the sibling application still has shortcuts in it.
foreach($rel in $dirs.Keys){
 $lines.Add(('<RemoveFolder Id="r{0}" Directory="{1}" On="uninstall"/>' -f (Hash ('cleanup/'+$rel)).Substring(0,24),$dirs[$rel]))
}
$lines.Add(('<RemoveFolder Id="RemoveMenu" On="uninstall"/><RegistryValue Root="HKCU" Key="Software\{0}\Installer" Name="StartMenu" Type="integer" Value="1" KeyPath="yes"/></Component></DirectoryRef>' -f $app))
$components.Add('StartMenu');$lines.Add('<Feature Id="Complete" Title="'+$app+'" Level="1">')
foreach($id in $components){$lines.Add('<ComponentRef Id="'+$id+'"/>')}
$lines.Add('</Feature></Product></Wix>')
$source=Join-Path $work 'Application.wxs'
[IO.File]::WriteAllText($source,($lines -join [Environment]::NewLine),[Text.UTF8Encoding]::new($false))
if($GenerateOnly){Write-Output $source;return}
$wix=Join-Path $repo 'tooling/wix3141'
if(!(Test-Path -LiteralPath (Join-Path $wix 'candle.exe'))){
 $archive=Join-Path $repo 'tooling/wix314-binaries.zip'
 & $python -c "import sys;sys.path.insert(0,sys.argv[1]);from distribution import download;download('https://github.com/wixtoolset/wix3/releases/download/wix3141rtm/wix314-binaries.zip',sys.argv[2],'6ac824e1642d6f7277d0ed7ea09411a508f6116ba6fae0aa5f2c7daa2ff43d31')" $PSScriptRoot $archive
 if($LASTEXITCODE){throw 'Verified WiX download failed'}
 Expand-Archive -LiteralPath $archive -DestinationPath $wix
}
$obj=Join-Path $work 'Application.wixobj'
& (Join-Path $wix 'candle.exe') -nologo -arch $Architecture -out $obj $source
if($LASTEXITCODE){throw 'WiX compilation failed'}
$base="$app-$version-windows-$Architecture";$msi=Join-Path $OutputDirectory ($base+'.msi')
# ICE91 is the per-user LocalAppData hint. ICE64 wants RemoveFolder for the
# shared Programs directory; uninstall must leave other apps installed there.
# Keep the WiX debug database in the scratch folder, not next to the release MSI.
& (Join-Path $wix 'light.exe') -nologo -sice:ICE91 -sice:ICE64 -pdbout (Join-Path $work 'Application.wixpdb') -out $msi $obj
if($LASTEXITCODE){throw 'MSI link/validation failed'}
$nsis=Get-Command makensis.exe -ErrorAction SilentlyContinue
$compiler=if($nsis){$nsis.Source}else{Join-Path ([Environment]::GetEnvironmentVariable('ProgramFiles(x86)')) 'NSIS/makensis.exe'}
if(!(Test-Path -LiteralPath $compiler)){throw 'Install NSIS 3.11 on the build machine'}
$bootstrap=Join-Path $work 'Bootstrap.nsi'
$nsi=@'
Unicode true
Name "@APP@"
OutFile "@SETUP@"
RequestExecutionLevel user
Icon "@ICON@"
VIProductVersion "@VERSION@.0"
VIAddVersionKey "ProductName" "@APP@"
VIAddVersionKey "CompanyName" "8bitPrograms"
VIAddVersionKey "FileDescription" "@APP@ installer"
Section
 InitPluginsDir
 SetOutPath "$PLUGINSDIR"
 File /oname=Application.msi "@MSI@"
 ExecWait '"$SYSDIR\msiexec.exe" /i "$PLUGINSDIR\Application.msi"' $0
 SetErrorLevel $0
SectionEnd
'@
$nsi=$nsi.Replace('@APP@',$app).Replace('@SETUP@',(Join-Path $OutputDirectory ($base+'-setup.exe'))).Replace('@ICON@',(Join-Path $repo $config.iconIco)).Replace('@VERSION@',$version).Replace('@MSI@',$msi)
[IO.File]::WriteAllText($bootstrap,$nsi,[Text.UTF8Encoding]::new($false))
& $compiler $bootstrap
if($LASTEXITCODE){throw 'EXE bootstrap compilation failed'}
Write-Output "Created MSI and EXE packages for $app $version $Architecture"
