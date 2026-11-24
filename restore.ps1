<#
.SYNOPSIS
Restores the exact RedPrism and DxUi dependencies.
.PARAMETER Platform
x64 or ARM64 target architecture.
#>
#Requires -Version 7.0
[CmdletBinding()]
param([ValidateSet('x64','ARM64')][string]$Platform='x64')

Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
Import-Module (Join-Path $PSScriptRoot 'Build/VisualStudioDiscovery.psm1') -Scope Local -ErrorAction Stop
$binarySources=$env:VCPKG_BINARY_SOURCES
$previousToolArchitecture=$env:PreferredToolArchitecture
try {
    $env:VCPKG_BINARY_SOURCES='clear'
    # Match the dependency identity selected by build.ps1 for the same host.
    if ($null -eq $previousToolArchitecture -and
        [Runtime.InteropServices.RuntimeInformation]::ProcessArchitecture -eq [Runtime.InteropServices.Architecture]::X64) {
        $env:PreferredToolArchitecture='x64'
    }
    $msbuild=Find-RedPrismMSBuild
    & (Join-Path $PSScriptRoot 'vcpkg-install.ps1') -Platform $Platform -MSBuildPath $msbuild
    & (Join-Path $PSScriptRoot 'restore-dxui.ps1') -Platform $Platform -MSBuildPath $msbuild
} finally {
    $env:VCPKG_BINARY_SOURCES=$binarySources
    $env:PreferredToolArchitecture=$previousToolArchitecture
}
