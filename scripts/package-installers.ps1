[CmdletBinding()]
param(
 [Parameter(Mandatory=$true)][string]$PackageDirectory,
 [Parameter(Mandatory=$true)][string]$OutputDirectory,
 [Parameter(Mandatory=$true)][string]$Version,
 [Parameter(Mandatory=$true)][string]$Tag,
 [Parameter(Mandatory=$true)][string]$WixExe,
 [Parameter(Mandatory=$true)][string]$WixUiExtension,
 [switch]$DeferIceValidation
)
# Local builds only. Both setup formats use the same MSI and uninstall identity.
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent $PSScriptRoot
$project=[IO.File]::ReadAllText((Join-Path $repo 'CMakeLists.txt'))
if($project -notmatch 'project\((8bitCode|8bitNotes)\s'){throw 'Unknown application.'}
$app=$matches[1]
$legacy=if($app -eq '8bitCode'){'8bitPad'}else{'8bitNotes'}
$icon=if($app -eq '8bitCode'){'icon/8bitPad.ico'}else{'icon/8bitnotes.ico'}
if($Tag -notmatch ('^v'+[regex]::Escape($Version)+'-local\.[0-9.]+$')){throw 'Tag/version mismatch.'}
$WixExe=(Resolve-Path -LiteralPath $WixExe).Path
$WixUiExtension=(Resolve-Path -LiteralPath $WixUiExtension).Path
$toolVersion=& $WixExe --version
if($LASTEXITCODE -or $toolVersion -notmatch '^6\.0\.2\+'){throw 'Use pinned WiX 6.0.2.'}
if(!(Test-Path -LiteralPath $OutputDirectory)){[IO.Directory]::CreateDirectory([IO.Path]::GetFullPath($OutputDirectory)) | Out-Null}
$OutputDirectory=(Resolve-Path -LiteralPath $OutputDirectory).Path
$msi=Join-Path $OutputDirectory "$app-$Tag-windows-x64.msi"
$setup=Join-Path $OutputDirectory "$app-$Tag-windows-x64-setup.exe"
if((Test-Path -LiteralPath $msi) -or (Test-Path -LiteralPath $setup)){throw 'Installer output exists; choose a fresh output directory.'}
$generated=& (Join-Path $PSScriptRoot 'package-msi.ps1') -PackageDirectory $PackageDirectory -OutputMsi $msi -Version $Version -GenerateOnly
$work=$generated.WorkDirectory
$source=Join-Path $work "$legacy.wxs"
$payload=Join-Path $work 'Payload.wxs'
& $WixExe convert $source $payload *> (Join-Path $work 'convert.log')
# convert returns the number of conversion diagnostics, including successful
# informational conversions. Reject actual errors and verify both namespaces.
if(Select-String -LiteralPath (Join-Path $work 'convert.log') -Pattern ': error ' -Quiet){throw "WiX conversion failed: $work"}
if(([xml][IO.File]::ReadAllText($payload)).DocumentElement.NamespaceURI -ne 'http://wixtoolset.org/schemas/v4/wxs'){throw 'Payload conversion did not complete.'}
# WiX 6 Minimal UI provides ARPNOMODIFY itself (v3 did not).
$xml=[xml][IO.File]::ReadAllText($source)
if($xml.DocumentElement.NamespaceURI -ne 'http://wixtoolset.org/schemas/v4/wxs'){throw 'WiX conversion did not complete.'}
$node=$xml.SelectSingleNode('//*[local-name()="Property" and @Id="ARPNOMODIFY"]')
if($node){$node.ParentNode.RemoveChild($node) | Out-Null}
$xml.Save($source)
$package=(Resolve-Path -LiteralPath $PackageDirectory).Path
$staged=Join-Path $work "$app.msi"
$args=@('build','-arch','x64','-bcgg','-ext',$WixUiExtension,'-d',"ProductVersion=$Version",'-d',"ProductCode=$($generated.ProductCode)",'-d',"PackageDirectory=$package",'-d',('ProductIcon='+(Join-Path $repo $icon)),'-d',('LicenseRtf='+(Join-Path $work 'License.rtf')),'-out',$staged,$source,$payload)
& $WixExe @args *> (Join-Path $work 'build.log')
if($LASTEXITCODE){throw "WiX build failed: $work"}
if(!$DeferIceValidation){
 & $WixExe msi validate $staged *> (Join-Path $work 'ice.log')
 if($LASTEXITCODE){throw "MSI ICE validation failed: $work. The unvalidated MSI has not been published."}
} else {
 [IO.File]::WriteAllText((Join-Path $work 'ice.log'),'Deferred: publication must run MSI ICE validation in an interactive Windows session.')
}
[IO.File]::Move($staged,$msi)
& (Join-Path $PSScriptRoot 'build-setup.ps1') -Msi $msi -Output $setup -Version $Version
if(!(Test-Path -LiteralPath $setup)){throw 'EXE setup was not produced.'}
[pscustomobject]@{Application=$app;Msi=$msi;Setup=$setup;PayloadManifest=(Join-Path $work 'payload.json');IceLog=(Join-Path $work 'ice.log');IceValidationPassed=(!$DeferIceValidation)}
