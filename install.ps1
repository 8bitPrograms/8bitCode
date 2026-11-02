#Requires -Version 5.1
[CmdletBinding()]
param(
 [string]$Repository=$env:BIT8PAD_REPOSITORY,
 [string]$Version='latest',
 [switch]$Portable,
 [switch]$DownloadOnly,
 [switch]$Experimental,
 [string]$InstallDirectory='',
 [string]$DownloadDirectory=$PWD.Path,
 [string]$FixtureDirectory='',
 [ValidateSet('','x64','arm64','x86')][string]$Architecture=''
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version 2
$app='8bitCode'
if(!$Repository){$Repository='@REPOSITORY@'}
if($Repository.Contains('@') -or $Repository -match '^OWNER/' -or $Repository -notmatch '^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$'){throw 'Set -Repository to the actual GitHub owner/repository.'}
if($Version -ne 'latest'){
 $Version=$Version.TrimStart('v')
 if($Version -notmatch '^\d+\.\d+\.\d+$'){throw 'Invalid version'}
}
if($FixtureDirectory -and !$DownloadOnly -and !$InstallDirectory){throw 'Fixture installs require an explicit InstallDirectory'}
if($Architecture -and !$DownloadOnly -and !$FixtureDirectory){throw 'Architecture overrides are restricted to download-only mode'}
if(!$Architecture){
 $native=if($env:PROCESSOR_ARCHITEW6432){$env:PROCESSOR_ARCHITEW6432}else{$env:PROCESSOR_ARCHITECTURE}
 $Architecture=switch($native.ToLowerInvariant()){'amd64'{'x64'};'arm64'{'arm64'};'x86'{'x86'};default{throw "Unsupported CPU: $native"}}
}
[Net.ServicePointManager]::SecurityProtocol=[Net.SecurityProtocolType]::Tls12
$tempBase=[IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\')+'\'
$temp=Join-Path $tempBase ("8bitPad-install-"+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $temp | Out-Null
function Fetch([string]$Name,[string]$Destination,[string]$Release){
 if($FixtureDirectory){Copy-Item -LiteralPath (Join-Path $FixtureDirectory $Name) -Destination $Destination}
 else{
  $url=if($Release -eq 'latest'){"https://github.com/$Repository/releases/latest/download/$Name"}else{"https://github.com/$Repository/releases/download/v$Release/$Name"}
  $client=New-Object Net.WebClient
  try{$client.DownloadFile($url,$Destination)}finally{$client.Dispose()}
 }
}
function Sha256([string]$Path){
 $h=[Security.Cryptography.SHA256]::Create();$f=[IO.File]::OpenRead($Path)
 try{return [BitConverter]::ToString($h.ComputeHash($f)).Replace('-','').ToLowerInvariant()}
 finally{$f.Dispose();$h.Dispose()}
}
try{
 $index=Join-Path $temp 'install-index.tsv';Fetch 'install-index.tsv' $index $Version
 $kind=if($Portable){'portable'}else{'msi'}
 $rows=@(Get-Content -LiteralPath $index | ForEach-Object {
  $f=$_.Split([char]9)
  if($f.Length -eq 8 -and $f[1] -eq 'windows' -and $f[2] -eq $Architecture -and $f[3] -eq $kind){,$f}
 })
 if($rows.Count -ne 1){throw "No unique Windows/$Architecture/$kind package is available"}
 $row=$rows[0];$releaseVersion=$row[0];$asset=$row[4];$hash=$row[5]
 if($releaseVersion -notmatch '^\d+\.\d+\.\d+$' -or $hash -notmatch '^[0-9a-f]{64}$'){throw 'Invalid release index'}
 if($Version -ne 'latest' -and $Version -ne $releaseVersion){throw 'Release index version mismatch'}
 $extension=if($Portable){'zip'}else{'msi'}
 if($asset -ne "$app-$releaseVersion-windows-$Architecture.$extension"){throw 'Unexpected release asset name'}
 if($row[7] -ne 'stable' -and !$Experimental){throw 'Experimental architecture: pass -Experimental after reading its release notes'}
 $package=Join-Path $temp $asset;Fetch $asset $package $releaseVersion
 if((Sha256 $package) -ne $hash){throw 'SHA256 mismatch; nothing was installed'}
 if($DownloadOnly){
  New-Item -ItemType Directory -Force -Path $DownloadDirectory | Out-Null
  $destination=Join-Path $DownloadDirectory $asset
  if(Test-Path -LiteralPath $destination){throw 'Download destination already exists'}
  Copy-Item -LiteralPath $package -Destination $destination;Write-Output $destination;return
 }
 if(!$Portable){
  if($FixtureDirectory){throw 'Fixture mode never executes Windows Installer'}
  $process=Start-Process -FilePath (Join-Path $env:SystemRoot 'System32/msiexec.exe') -ArgumentList @('/i',('"'+$package+'"'),'/passive','/norestart') -WindowStyle Hidden -Wait -PassThru
  if($process.ExitCode -notin 0,3010){throw "Windows Installer failed: $($process.ExitCode)"}
  Write-Output "$app $releaseVersion installed. Profiles were preserved.";return
 }
 if(!$InstallDirectory){$InstallDirectory=Join-Path $PWD.Path "$app-$releaseVersion-portable"}
 $InstallDirectory=[IO.Path]::GetFullPath($InstallDirectory)
 if(Test-Path -LiteralPath $InstallDirectory){throw 'Choose a fresh portable destination'}
 Add-Type -AssemblyName System.IO.Compression.FileSystem
 $unpack=Join-Path $temp 'unpacked';New-Item -ItemType Directory -Path $unpack | Out-Null
 $zip=[IO.Compression.ZipFile]::OpenRead($package)
 try{
  $total=[long]0;$count=0
  foreach($entry in $zip.Entries){
   $count++;$total+=$entry.Length
   if($count -gt 30000 -or $total -gt 4GB){throw 'ZIP exceeds extraction limits'}
   if($entry.FullName -match '(^[\\/]|(^|[\\/])\.\.([\\/]|$)|:)'){throw 'Unsafe ZIP path'}
   if((($entry.ExternalAttributes -shr 16) -band 0xf000) -eq 0xa000){throw 'ZIP symlinks are not supported'}
  }
 }finally{$zip.Dispose()}
 [IO.Compression.ZipFile]::ExtractToDirectory($package,$unpack)
 $root=Join-Path $unpack "$app-$releaseVersion-windows-$Architecture"
 if(!(Test-Path -LiteralPath (Join-Path $root "bin/$app.exe"))){throw 'Portable executable missing'}
 New-Item -ItemType Directory -Force -Path (Split-Path -Parent $InstallDirectory) | Out-Null
 Move-Item -LiteralPath $root -Destination $InstallDirectory
 Write-Output "Portable $app $releaseVersion is ready in $InstallDirectory. Run Start-Portable.cmd."
}finally{
 $resolved=[IO.Path]::GetFullPath($temp)
 if(!$resolved.StartsWith($tempBase,[StringComparison]::OrdinalIgnoreCase) -or (Split-Path -Leaf $resolved) -notlike '8bitPad-install-*'){throw 'Unexpected cleanup path'}
 Remove-Item -LiteralPath $resolved -Recurse -Force -ErrorAction SilentlyContinue
}
