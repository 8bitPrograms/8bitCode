#Requires -Version 5.1
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$AssetDirectory,
    [string]$NotesRepository = ''
)
$ErrorActionPreference = 'Stop'
function Get-MigrationSha256([string]$Path) {
    $hash = [Security.Cryptography.SHA256]::Create()
    $stream = [IO.File]::OpenRead($Path)
    try { return [BitConverter]::ToString($hash.ComputeHash($stream)).Replace('-', '').ToLowerInvariant() }
    finally { $stream.Dispose(); $hash.Dispose() }
}
$repo = Split-Path -Parent $PSScriptRoot
if (!$NotesRepository) { $NotesRepository = Join-Path (Split-Path -Parent $repo) '8bitNotes' }
$ghCommand = Get-Command gh.exe -ErrorAction SilentlyContinue
$gh = if ($ghCommand) { $ghCommand.Source } else { Join-Path $env:ProgramFiles 'GitHub CLI/gh.exe' }
if (!(Test-Path -LiteralPath $gh)) { throw 'GitHub CLI is required.' }
foreach ($checkout in @($repo, $NotesRepository)) {
    $dirty = & git -C $checkout status --porcelain
    if ($LASTEXITCODE -or $dirty) { throw "Checkout must be clean: $checkout" }
}
$assetRoot = $AssetDirectory
if (![IO.Path]::IsPathRooted($assetRoot)) { $assetRoot = Join-Path $repo $assetRoot }
$manifest = Get-Content -LiteralPath (Join-Path $assetRoot 'release-manifest.json') -Raw | ConvertFrom-Json
if ($manifest.application -ne '8bitCode' -or $manifest.repository -ne '8bitPrograms/8bitCode' -or
    $manifest.sourceCommit -ne ((& git -C $repo rev-parse HEAD).Trim())) {
    throw 'Prepare the tested Code release for this commit before migration.'
}
foreach ($asset in $manifest.assets) {
    if ([IO.Path]::GetFileName($asset.name) -ne $asset.name) { throw 'Invalid release asset name.' }
    $file = Join-Path $assetRoot $asset.name
    if ((Get-MigrationSha256 $file) -ne $asset.sha256) {
        throw "Release asset changed: $file"
    }
}
& $gh auth status --hostname github.com
if ($LASTEXITCODE) { throw 'GitHub authentication/network check failed.' }
$login = & $gh api user --jq .login
if ($LASTEXITCODE -or $login.Trim() -ne 'blueberi99') { throw 'Use the blueberi99 account.' }
$json = & $gh api repos/8bitPrograms/8bitPad
if ($LASTEXITCODE) { throw 'Cannot resolve the original Pad repository.' }
$remote = $json | ConvertFrom-Json
if ($remote.id -ne 1435638928 -or $remote.owner.login -ne '8bitPrograms' -or !$remote.permissions.admin) {
    throw 'Repository identity/admin access differs from the expected Pad repository.'
}
if ($remote.name -eq '8bitPad') {
    # Rename in place: preserve history, issues, releases, and access settings.
    $renamed = & $gh api --method PATCH repos/8bitPrograms/8bitPad -f name=8bitCode --jq .full_name
    if ($LASTEXITCODE -or $renamed.Trim() -ne '8bitPrograms/8bitCode') { throw 'Repository rename failed.' }
} elseif ($remote.name -ne '8bitCode') { throw 'Unexpected repository name.' }
$verifiedId = & $gh api repos/8bitPrograms/8bitCode --jq .id
if ($LASTEXITCODE -or $verifiedId.Trim() -ne '1435638928') { throw 'Renamed repository identity check failed.' }
& git -C $repo remote set-url origin git@github.com:8bitPrograms/8bitCode.git
if ($LASTEXITCODE) { throw 'Could not update the local remote.' }
if (Test-Path -LiteralPath (Join-Path $NotesRepository 'vendor/editor/.git')) {
    & git -C (Join-Path $NotesRepository 'vendor/editor') remote set-url origin https://github.com/8bitPrograms/8bitCode.git
    if ($LASTEXITCODE) { throw 'Could not update the initialized Notes editor remote.' }
}
foreach ($checkout in @($repo, $NotesRepository)) {
    & git -C $checkout push origin HEAD:refs/heads/main
    if ($LASTEXITCODE) { throw "Non-force source push failed: $checkout. Reconcile main before retrying." }
}
& (Join-Path $PSScriptRoot 'publish-windows.ps1') -AssetDirectory $assetRoot
