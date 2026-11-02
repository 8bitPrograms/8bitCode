#Requires -Version 5.1
[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$AssetDirectory)
$ErrorActionPreference = 'Stop'
function Get-PublishSha256([string]$Path) {
    $hash = [Security.Cryptography.SHA256]::Create()
    $stream = [IO.File]::OpenRead($Path)
    try { return [BitConverter]::ToString($hash.ComputeHash($stream)).Replace('-', '').ToLowerInvariant() }
    finally { $stream.Dispose(); $hash.Dispose() }
}
$repo = Split-Path -Parent $PSScriptRoot
if (![IO.Path]::IsPathRooted($AssetDirectory)) { $AssetDirectory = Join-Path $repo $AssetDirectory }
$assets = (Resolve-Path -LiteralPath $AssetDirectory).Path
$manifestPath = Join-Path $assets 'release-manifest.json'
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if ($manifest.repository -notmatch '^8bitPrograms/(8bitCode|8bitNotes)$' -or
    $manifest.tag -notmatch '^v[0-9]+\.[0-9]+\.[0-9]+-local\.[0-9.]+$' -or
    $manifest.sourceCommit -notmatch '^[0-9a-f]{40}$' -or
    $manifest.architecture -ne 'x64' -or $manifest.platform -ne 'windows') {
    throw 'Invalid local Windows release manifest.'
}
if ($manifest.application -ne ($manifest.repository -split '/')[1]) { throw 'Application/repository mismatch.' }
$project = Get-Content -LiteralPath (Join-Path $repo 'CMakeLists.txt') -Raw
if ($project -notmatch ('project\(' + [regex]::Escape($manifest.application) + '\s')) {
    throw 'The manifest belongs to a different application.'
}
Push-Location $repo
try {
    $head = (& git rev-parse HEAD).Trim()
    if ($LASTEXITCODE -or $head -ne $manifest.sourceCommit) { throw 'Checkout does not match the tested release commit.' }
    $dirty = & git -c diff.ignoreSubmodules=none status --porcelain --untracked-files=normal
    if ($LASTEXITCODE -or $dirty) { throw 'Commit or preserve working-tree changes before publishing.' }
    $names = @()
    foreach ($asset in $manifest.assets) {
        if ([IO.Path]::GetFileName($asset.name) -ne $asset.name -or $asset.name -match '[#*?]') { throw 'Invalid asset filename.' }
        $file = Join-Path $assets $asset.name
        if (!(Test-Path -LiteralPath $file -PathType Leaf)) { throw "Missing asset: $file" }
        $item = Get-Item -LiteralPath $file
        if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Release assets must be ordinary files.' }
        $hash = Get-PublishSha256 $file
        if ($hash -ne $asset.sha256 -or $item.Length -ne $asset.size) { throw "Asset changed: $($asset.name)" }
        $names += $file
    }
    if (!$names.Count) { throw 'Manifest has no release assets.' }
    $gh = Get-Command gh.exe -ErrorAction SilentlyContinue
    $ghPath = if ($gh) { $gh.Source } else { Join-Path $env:ProgramFiles 'GitHub CLI/gh.exe' }
    if (!(Test-Path -LiteralPath $ghPath)) { throw 'Install GitHub CLI and run gh auth login first.' }
    & $ghPath auth status --hostname github.com
    if ($LASTEXITCODE) { throw 'GitHub CLI authentication/network check failed. Run this command in your own signed-in terminal.' }
    $account = & $ghPath api user --jq .login
    if ($LASTEXITCODE -or $account.Trim() -ne 'blueberi99') { throw 'Use the blueberi99 GitHub account for these repositories.' }
    $remote = & $ghPath api "repos/$($manifest.repository)/branches/main" --jq .commit.sha
    if ($LASTEXITCODE) { throw 'Cannot read GitHub main; no upload attempted.' }
    if ($remote.Trim() -ne $head) {
        # The ordinary non-force push refuses to overwrite a newer remote main.
        & git push origin HEAD:refs/heads/main
        if ($LASTEXITCODE) { throw 'Source push failed; reconcile main before publishing.' }
    }
    $releaseArguments = @('release', 'create', $manifest.tag, '--repo', $manifest.repository,
        '--target', $head, '--title', $manifest.title, '--notes-file', (Join-Path $assets 'release-notes.md'),
        '--prerelease', '--latest=false', '--draft') + $names + @($manifestPath, (Join-Path $assets 'SHA256SUMS.txt'))
    & $ghPath @releaseArguments
    if ($LASTEXITCODE) { throw 'Release creation/upload failed. Any draft is retained; inspect it before retrying. Existing releases are never overwritten.' }
    # Compare the uploaded bytes with the local manifest before publishing.
    $json = & $ghPath api "repos/$($manifest.repository)/releases?per_page=100"
    if ($LASTEXITCODE) { throw 'Could not verify the uploaded draft; it remains unpublished.' }
    $releases = $json | ConvertFrom-Json
    $drafts = @($releases | Where-Object { $_.tag_name -eq $manifest.tag -and $_.draft })
    if ($drafts.Count -ne 1) { throw 'Could not identify exactly one uploaded draft.' }
    $release = $drafts[0]
    $checks = @($manifest.assets)
    foreach ($path in @($manifestPath, (Join-Path $assets 'SHA256SUMS.txt'))) {
        $checks += [pscustomobject]@{ name = [IO.Path]::GetFileName($path); size = (Get-Item -LiteralPath $path).Length;
            sha256 = (Get-PublishSha256 $path) }
    }
    foreach ($asset in $checks) {
        $uploaded = @($release.assets | Where-Object { $_.name -eq $asset.name })
        if ($uploaded.Count -ne 1 -or $uploaded[0].size -ne $asset.size -or
            $uploaded[0].digest -ne ('sha256:' + $asset.sha256)) {
            throw "Uploaded digest/size mismatch: $($asset.name). Draft retained."
        }
    }
    & $ghPath release edit $manifest.tag --repo $manifest.repository --draft=false --prerelease --latest=false
    if ($LASTEXITCODE) { throw 'Files uploaded, but publishing failed; the draft remains available.' }
    & $ghPath release view $manifest.tag --repo $manifest.repository --json url --jq .url
} finally { Pop-Location }
