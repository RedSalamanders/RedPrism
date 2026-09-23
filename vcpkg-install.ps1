<#!
.SYNOPSIS
Installs the RedPrism vcpkg manifest dependencies for x64, ARM64, or both. -Asan installs the sanitized tree the
`ASan Debug` configuration links for the selected platform (every library instrumented by AddressSanitizer, overlay
triplet x64-windows-asan or arm64-windows-asan).
#>
[CmdletBinding()]
param(
    [ValidateSet('x64', 'ARM64', 'All')]
    [string] $Platform = 'x64',

    [switch] $Asan
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSCommandPath
$buildRoot = Join-Path $repoRoot '.build'
$toolRoot = Join-Path $buildRoot 'vcpkg-tool'
$toolStampPath = Join-Path $buildRoot 'vcpkg-tool.commit'
$toolIdentityPath = Join-Path $repoRoot 'vcpkg-tool.json'
$manifestPath = Join-Path $repoRoot 'vcpkg.json'

if (-not (Get-Command 'git.exe' -ErrorAction SilentlyContinue)) {
    throw 'Git was not found. Install Git for Windows and ensure git.exe is on PATH.'
}
if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
    throw "The vcpkg manifest was not found: $manifestPath"
}

$toolIdentity = Get-Content -Raw -LiteralPath $toolIdentityPath | ConvertFrom-Json
$repository = [string] $toolIdentity.repository
$commit = [string] $toolIdentity.commit
if ([string]::IsNullOrWhiteSpace($repository) -or $commit -notmatch '^[0-9a-fA-F]{40}$') {
    throw 'vcpkg-tool.json must contain a repository URL and a full 40-character commit hash.'
}

New-Item -ItemType Directory -Path $buildRoot -Force | Out-Null
if (-not (Test-Path -LiteralPath (Join-Path $toolRoot '.git') -PathType Container)) {
    if (Test-Path -LiteralPath $toolRoot) {
        throw "The managed vcpkg path exists but is not a Git checkout: $toolRoot"
    }

    Write-Host "Cloning pinned vcpkg tooling into $toolRoot" -ForegroundColor Cyan
    & git.exe clone --filter=blob:none $repository $toolRoot
    if ($LASTEXITCODE -ne 0) {
        throw "git clone failed with exit code $LASTEXITCODE."
    }
}

$origin = & git.exe -C $toolRoot remote get-url origin
if ($LASTEXITCODE -ne 0 -or $origin.TrimEnd('/') -ne $repository.TrimEnd('/')) {
    throw "The managed vcpkg checkout does not use the pinned repository: $repository"
}

$dirty = & git.exe -C $toolRoot status --porcelain
if ($LASTEXITCODE -ne 0) {
    throw 'Unable to inspect the managed vcpkg checkout.'
}
if ($dirty) {
    throw "The managed vcpkg checkout contains local changes: $toolRoot"
}

$head = & git.exe -C $toolRoot rev-parse HEAD 2>$null
if ($LASTEXITCODE -ne 0 -or $head -ne $commit) {
    Write-Host "Selecting pinned vcpkg commit $commit" -ForegroundColor Cyan
    & git.exe -C $toolRoot fetch --depth 1 origin $commit
    if ($LASTEXITCODE -ne 0) {
        throw "Unable to fetch pinned vcpkg commit $commit."
    }
    & git.exe -C $toolRoot checkout --detach $commit
    if ($LASTEXITCODE -ne 0) {
        throw "Unable to check out pinned vcpkg commit $commit."
    }
}

$vcpkg = Join-Path $toolRoot 'vcpkg.exe'
$stampedCommit = if (Test-Path -LiteralPath $toolStampPath -PathType Leaf) {
    (Get-Content -Raw -LiteralPath $toolStampPath).Trim()
} else {
    ''
}
if (-not (Test-Path -LiteralPath $vcpkg -PathType Leaf) -or $stampedCommit -ne $commit) {
    Write-Host 'Bootstrapping vcpkg...' -ForegroundColor Cyan
    & (Join-Path $toolRoot 'bootstrap-vcpkg.bat') -disableMetrics
    if ($LASTEXITCODE -ne 0) {
        throw "vcpkg bootstrap failed with exit code $LASTEXITCODE."
    }
    Set-Content -LiteralPath $toolStampPath -Value $commit -Encoding ascii
}

$overlayTriplets = Join-Path $repoRoot 'Build\vcpkg-triplets'
$platforms = if ($Platform -eq 'All') { @('x64', 'ARM64') } else { @($Platform) }
foreach ($targetPlatform in $platforms) {
    $platformScope = $targetPlatform.ToLowerInvariant()
    $triplet = if ($targetPlatform -eq 'ARM64') { 'arm64-windows' } else { 'x64-windows' }
    if ($Asan) {
        # Decision D8 (Specs/Plans/WIP/ReviewFixes_2026-09-23.md): the sanitized tree exists per platform, each with
        # its own overlay triplet (x64-windows-asan, arm64-windows-asan) so a static library such as Clipper2 is
        # built with the same AddressSanitizer instrumentation as the platform's application and DxUi objects.
        $platformScope = "$($targetPlatform.ToLowerInvariant())-asan"
        $triplet = if ($targetPlatform -eq 'ARM64') { 'arm64-windows-asan' } else { 'x64-windows-asan' }
    }
    $installRoot = Join-Path $buildRoot "vcpkg_installed\$platformScope"
    $buildTreesRoot = Join-Path $buildRoot "vcpkg_buildtrees\$platformScope"
    $packagesRoot = Join-Path $buildRoot "vcpkg_packages\$platformScope"
    $downloadsRoot = Join-Path $buildRoot 'vcpkg_downloads'

    Write-Host "Installing RedPrism dependencies ($triplet)" -ForegroundColor Cyan
    $arguments = @(
        'install',
        "--triplet=$triplet",
        "--x-manifest-root=$repoRoot",
        "--x-install-root=$installRoot",
        "--x-buildtrees-root=$buildTreesRoot",
        "--x-packages-root=$packagesRoot",
        "--downloads-root=$downloadsRoot",
        "--overlay-triplets=$overlayTriplets"
    )
    & $vcpkg @arguments
    if ($LASTEXITCODE -ne 0) {
        throw "vcpkg install failed for $triplet with exit code $LASTEXITCODE."
    }
}

Write-Host 'vcpkg dependencies are ready.' -ForegroundColor Green
