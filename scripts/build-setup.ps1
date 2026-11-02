[CmdletBinding()]
param(
 [Parameter(Mandatory=$true)][string]$Msi,
 [Parameter(Mandatory=$true)][string]$Output,
 [Parameter(Mandatory=$true)][string]$Version,
 [ValidateSet('x64','arm64','x86')][string]$Architecture='x64'
)
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent $PSScriptRoot
$Msi=(Resolve-Path -LiteralPath $Msi).Path
$Output=[IO.Path]::GetFullPath($Output)
if(Test-Path -LiteralPath $Output){throw 'Setup output already exists; choose a fresh output path.'}
if($Version -notmatch '^\d+\.\d+\.\d+$'){throw 'A three-part installer version is required.'}
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(!$vs){throw 'MSVC C++ Build Tools are required to build setup.'}
Import-Module (Join-Path $vs 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments "-arch=$Architecture -host_arch=x64"
$work=Join-Path $repo ('build/bootstrap-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $work | Out-Null
function RcPath([string]$path){return $path.Replace('\','\\').Replace('"','\"')}
$resource=Join-Path $work 'setup.rc'
$numericVersion=$Version.Replace('.',',')+',0'
$text=@"
#include <windows.h>
1 ICON "$(RcPath (Join-Path $repo 'icon/8bitPad.ico'))"
101 RCDATA "$(RcPath $Msi)"
1 VERSIONINFO
 FILEVERSION $numericVersion
 PRODUCTVERSION $numericVersion
 FILEOS VOS_NT_WINDOWS32
 FILETYPE VFT_APP
BEGIN
 BLOCK "StringFileInfo"
 BEGIN
  BLOCK "040904B0"
  BEGIN
   VALUE "CompanyName", "8bitPrograms\0"
   VALUE "FileDescription", "8bitCode setup\0"
   VALUE "FileVersion", "$Version\0"
   VALUE "ProductName", "8bitCode\0"
   VALUE "ProductVersion", "$Version\0"
  END
 END
 BLOCK "VarFileInfo"
 BEGIN
  VALUE "Translation", 0x0409, 1200
 END
END
"@
[IO.File]::WriteAllText($resource,$text,[Text.UTF8Encoding]::new($false))
& rc.exe /nologo "/fo$(Join-Path $work 'setup.res')" $resource
if($LASTEXITCODE){throw 'Setup resource compilation failed.'}
& cl.exe /nologo /std:c++20 /O2 /MT /W4 /DUNICODE /D_UNICODE /guard:cf "/Fo$(Join-Path $work 'setup.obj')" "/Fe$Output" (Join-Path $repo 'deploy/windows/setup.cpp') (Join-Path $work 'setup.res') user32.lib shell32.lib ole32.lib advapi32.lib /link /SUBSYSTEM:WINDOWS /DYNAMICBASE /NXCOMPAT /HIGHENTROPYVA /guard:cf /MANIFEST:EMBED "/MANIFESTINPUT:$(Join-Path $repo 'deploy/windows/setup.manifest')"
if($LASTEXITCODE){throw 'Setup executable compilation failed.'}
Write-Output "Created native setup: $Output"
