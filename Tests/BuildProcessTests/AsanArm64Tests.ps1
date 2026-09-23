<#
.SYNOPSIS
Static regression checks for RF-703 / Decision D8: an arm64-windows-asan overlay triplet exists, vcpkg-install.ps1
no longer refuses -Platform ARM64 -Asan, and Directory.Build.props maps ARM64 ASan Debug to the sanitized tree.
This does not install or build anything -- the live proof is a gate build in an isolated worktree (see the WP7
final report for the exact reproduction steps and its outcome).
#>
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)

$arm64Triplet = Join-Path $repo 'Build\vcpkg-triplets\arm64-windows-asan.cmake'
if (-not (Test-Path -LiteralPath $arm64Triplet -PathType Leaf)) {
    throw 'Build/vcpkg-triplets/arm64-windows-asan.cmake is missing (RF-703 / Decision D8).'
}
$arm64TripletText = Get-Content -LiteralPath $arm64Triplet -Raw
if ($arm64TripletText -notmatch 'VCPKG_TARGET_ARCHITECTURE arm64' -or
    $arm64TripletText -notmatch '/fsanitize=address' -or
    $arm64TripletText -notmatch 'asan-toolchain\.cmake') {
    throw 'arm64-windows-asan.cmake does not mirror the sanitized settings of x64-windows-asan.cmake.'
}
Write-Host 'PASS asan-arm64.triplet-file'

$vcpkgInstall = Get-Content -LiteralPath (Join-Path $repo 'vcpkg-install.ps1') -Raw
if ($vcpkgInstall -match "The sanitized dependency tree is built for x64 only") {
    throw 'vcpkg-install.ps1 still refuses -Platform ARM64 -Asan (RF-703 / Decision D8).'
}
if ($vcpkgInstall -notmatch 'arm64-windows-asan') {
    throw 'vcpkg-install.ps1 does not install the arm64-windows-asan triplet.'
}
Write-Host 'PASS asan-arm64.vcpkg-install-script'

$buildProps = Get-Content -LiteralPath (Join-Path $repo 'Directory.Build.props') -Raw
if ($buildProps -notmatch 'ARM64.{0,40}ASan Debug.{0,20}arm64-asan') {
    throw 'Directory.Build.props does not map ARM64 ASan Debug to the arm64-asan vcpkg scope.'
}
if ($buildProps -notmatch 'arm64-windows-asan') {
    throw 'Directory.Build.props does not map ARM64 ASan Debug to the arm64-windows-asan triplet.'
}
Write-Host 'PASS asan-arm64.directory-build-props'
