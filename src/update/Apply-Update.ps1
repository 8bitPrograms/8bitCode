#Requires -Version 5.1
[CmdletBinding()]
param(
 [Parameter(Mandatory=$true)][string]$Package,
 [Parameter(Mandatory=$true)][ValidatePattern('^[a-fA-F0-9]{64}$')][string]$Sha256,
 [Parameter(Mandatory=$true)][int]$ParentPid,
 [Parameter(Mandatory=$true)][ValidateSet('8bitPad','8bitCode','8bitShell')][string]$Application,
 [Parameter(Mandatory=$true)][string]$Executable,
 [switch]$Restart
)
$ErrorActionPreference='Stop'
$log=Join-Path (Split-Path -Parent $Package) 'install-update.log'
try {
 if([IO.Path]::GetExtension($Package) -ine '.msi'){throw 'Expected a Windows Installer package'}
 $directory=if($Application -eq '8bitCode'){'8bitPad'}else{$Application}
 $root=[IO.Path]::GetFullPath((Join-Path $env:LOCALAPPDATA "Programs/$directory")).TrimEnd('\')
 $exe=[IO.Path]::GetFullPath($Executable)
 if($exe -ine "$root\bin\$Application.exe" -and $exe -ine "$root\$Application.exe"){throw 'Unexpected application directory'}
 $parent=Get-Process -Id $ParentPid -ErrorAction SilentlyContinue
 if($parent){Wait-Process -Id $ParentPid -Timeout 120 -ErrorAction Stop}
 $deadline=[DateTime]::UtcNow.AddMinutes(5)
 do {
  $others=@(Get-Process -Name $Application -ErrorAction SilentlyContinue | Where-Object {
   try { [IO.Path]::GetFullPath($_.Path) -ieq $exe } catch { $true }
  })
  if(!$others.Count){break}
  if([DateTime]::UtcNow -ge $deadline){throw 'Another application window is open; update was postponed'}
  Start-Sleep -Milliseconds 500
 } while($true)
 $hash=[Security.Cryptography.SHA256]::Create()
 $stream=[IO.File]::OpenRead($Package)
 try{$actual=[BitConverter]::ToString($hash.ComputeHash($stream)).Replace('-','')}finally{$stream.Dispose();$hash.Dispose()}
 if($actual -ine $Sha256){throw 'Update checksum mismatch'}
 $arguments=@('/i',('"'+$Package+'"'),'/passive','/norestart','/L*v',('"'+$log+'"'))
 $result=Start-Process -FilePath (Join-Path $env:SystemRoot 'System32/msiexec.exe') -ArgumentList $arguments -WindowStyle Hidden -Wait -PassThru
 if($result.ExitCode -notin @(0,3010)){throw "Windows Installer failed (exit $($result.ExitCode)); see $log"}
 if($Restart -and $result.ExitCode -eq 0){Start-Process -FilePath $exe -WorkingDirectory ([IO.Path]::GetDirectoryName($exe)) -WindowStyle Hidden | Out-Null}
} catch {
 [IO.File]::AppendAllText($log,([Environment]::NewLine+$_.Exception.Message+[Environment]::NewLine))
 exit 1
}
