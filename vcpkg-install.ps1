<#!
.SYNOPSIS
Installs the RedPrism vcpkg manifest dependencies for x64, ARM64, or both. -Asan installs the sanitized tree the
`ASan Debug` configuration links for the selected platform (every library instrumented by AddressSanitizer, overlay
triplet x64-windows-asan or arm64-windows-asan).

.DESCRIPTION
vcpkg builds with the Visual Studio installation and MSVC toolset that build.ps1's MSBuild uses, not the newest toolset
it finds. The installation is the one the MSBuild that build.ps1 discovered belongs to, and the toolset is that
installation's default (VC\Auxiliary\Build\Microsoft.VCToolsVersion.default.txt); a missing or malformed version file fails
before anything is cloned. The two differ when a newer toolset is installed beside the default: it can lack a compiler for a
target, and libraries compiled by a toolset newer than the one that links them are a link hazard.

Every triplet vcpkg can build here (x64-windows and arm64-windows, as target or as host, and the two sanitizer triplets) gets
an overlay triplet beneath .build\vcpkg-triplets: the pinned vcpkg checkout's triplet, or RedPrism's own under
Build\vcpkg-triplets, copied unchanged, plus the two pins (VCPKG_VISUAL_STUDIO_PATH and VCPKG_PLATFORM_TOOLSET_VERSION). The
sanitizer triplets chainload asan-toolchain.cmake, which is copied beside them. A file is rewritten only when its contents
change. Changing a triplet changes vcpkg's package ABI hash, so the first restore after a change rebuilds the packages.

.PARAMETER Platform
Target platform: x64, ARM64, or All.

.PARAMETER Asan
Install the sanitized tree (x64-windows-asan or arm64-windows-asan) instead of the plain one.

.PARAMETER MSBuildPath
The MSBuild the build uses; build.ps1 passes the one it found. Defaults to the one it would find.
#>
[CmdletBinding()]
param(
    [ValidateSet('x64', 'ARM64', 'All')]
    [string] $Platform = 'x64',

    [switch] $Asan,

    [string] $MSBuildPath = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSCommandPath
Import-Module (Join-Path $repoRoot 'Build\VisualStudioDiscovery.psm1') -Force
Import-Module (Join-Path $repoRoot 'Build\VcpkgToolset.psm1') -Force
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

# vcpkg picks the newest MSVC toolset of the Visual Studio installation it prefers; build.ps1's MSBuild compiles with the
# default toolset of its own installation. Find both here, before anything is cloned or downloaded, so a broken installation
# fails at once and every triplet below is pinned to what MSBuild uses.
$msbuild = if ($MSBuildPath) { $MSBuildPath } else { Find-RedPrismMSBuild }
if (-not (Test-Path -LiteralPath $msbuild -PathType Leaf)) {
    throw "MSBuild was not found at '$msbuild'."
}
$installation = Find-RedPrismVisualStudioInstallationOfMSBuild -MSBuildPath $msbuild
if (-not $installation) {
    throw "The Visual Studio installation of '$msbuild' could not be determined. Install Visual Studio 2026 (18.x) with the Desktop development with C++ workload."
}
$toolset = Get-RedPrismDefaultToolset -Installation $installation
[xml]$msvcPolicy = Get-Content -Raw -LiteralPath (Join-Path $repoRoot 'Directory.Build.props')
$requiredTools = $msvcPolicy.SelectSingleNode('/Project/PropertyGroup/RequiredMsvcToolsVersion').InnerText
if ($toolset.MajorMinor -cne $requiredTools) { throw "This product requires default MSVC $requiredTools.x; selected $($toolset.Version)." }
Write-Host "Visual Studio: $installation" -ForegroundColor Cyan
Write-Host "MSVC toolset:  $($toolset.Version), the default MSBuild compiles with; vcpkg is pinned to $($toolset.MajorMinor)" -ForegroundColor Cyan

New-Item -ItemType Directory -Path $buildRoot -Force | Out-Null
if (-not (Test-Path -LiteralPath (Join-Path $toolRoot '.git') -PathType Container)) {
    if (Test-Path -LiteralPath $toolRoot) {
        throw "The managed vcpkg path exists but is not a Git checkout: $toolRoot"
    }

    Write-Host "Cloning pinned vcpkg tooling into $toolRoot" -ForegroundColor Cyan
    # Set only this managed checkout: a deep checkout root may exceed the traditional Win32 path limit.
    & git.exe clone -c core.longpaths=true --filter=blob:none $repository $toolRoot
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

# One overlay directory holds a pinned copy of every triplet vcpkg can pick here, as target or as host, so an ARM64 restore on
# an x64 host pins its host triplet too. A sanitizer triplet's chainloaded asan-toolchain.cmake lands beside its copy, and
# finds the managed checkout through ..\..\.build\vcpkg-tool: the overlay directory must lie two levels below the repository.
$overlayDirectory = Join-Path $buildRoot 'vcpkg-triplets'
$toolchainFromOverlay = [IO.Path]::GetFullPath((Join-Path $overlayDirectory '..\..\.build\vcpkg-tool\scripts\toolchains\windows.cmake'))
if ($toolchainFromOverlay -ne [IO.Path]::GetFullPath((Join-Path $toolRoot 'scripts\toolchains\windows.cmake'))) {
    throw "asan-toolchain.cmake would not find vcpkg's windows.cmake from $overlayDirectory; keep the overlay directory two levels below the repository root."
}
$tripletSources = [ordered]@{
    'x64-windows'        = Join-Path $toolRoot 'triplets\x64-windows.cmake'
    'arm64-windows'      = Join-Path $toolRoot 'triplets\arm64-windows.cmake'
    'x64-windows-asan'   = Join-Path $repoRoot 'Build\vcpkg-triplets\x64-windows-asan.cmake'
    'arm64-windows-asan' = Join-Path $repoRoot 'Build\vcpkg-triplets\arm64-windows-asan.cmake'
}
foreach ($name in $tripletSources.Keys) {
    $overlay = Update-RedPrismVcpkgOverlayTriplet -SourceTripletPath $tripletSources[$name] -Toolset $toolset -OutputDirectory $overlayDirectory
    Write-Host "Overlay triplet $($overlay.Path) $(if ($overlay.Changed) { 'written' } else { 'unchanged' })" -ForegroundColor DarkGray
}

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
        "--overlay-triplets=$overlayDirectory"
    )
    & $vcpkg @arguments
    if ($LASTEXITCODE -ne 0) {
        throw "vcpkg install failed for $triplet with exit code $LASTEXITCODE."
    }
}

Write-Host 'vcpkg dependencies are ready.' -ForegroundColor Green
