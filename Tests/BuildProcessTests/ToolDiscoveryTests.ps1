[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Import-Module (Join-Path $repo 'Build\VisualStudioDiscovery.psm1') -Force

# RF-707: one version-ranged lookup, reused everywhere, instead of an unranged vswhere call that could pick an
# older Visual Studio's MSBuild (no v145 toolset).
if ($RedPrismVisualStudioVersionRange -ne '[18.0,19.0)') {
    throw "The Visual Studio version range drifted from Visual Studio 2026: '$RedPrismVisualStudioVersionRange'."
}

foreach ($function in 'Find-RedPrismVisualStudioInstallationPath', 'Find-RedPrismMSBuild', 'Find-RedPrismClangFormat') {
    if (-not (Get-Command $function -Module VisualStudioDiscovery -ErrorAction SilentlyContinue)) {
        throw "$function was not exported by Build/VisualStudioDiscovery.psm1."
    }
}
Write-Host 'PASS tooldiscovery.module-exports'

& (Join-Path $PSScriptRoot 'ToolArchitectureTests.ps1')

$installation = Find-RedPrismVisualStudioInstallationPath
if ($installation) {
    $bundled = @('VC/Tools/Llvm/x64/bin/clang-format.exe','VC/Tools/Llvm/bin/clang-format.exe') |
        ForEach-Object { Join-Path $installation $_ } | Where-Object { Test-Path -LiteralPath $_ }
    if (@($bundled).Count -and (Find-RedPrismClangFormat) -notin $bundled) {
        throw 'An unrelated PATH formatter overrides the supported Visual Studio bundle.'
    }
}
Write-Host 'PASS tooldiscovery.formatter-follows-supported-visual-studio'

$formatter = Find-RedPrismClangFormat
if ($formatter) {
    Import-Module (Join-Path $repo 'Build/ScopedTesting.psm1') -Force
    $fingerprint = 'formatter=' + $formatter + ':' + (Get-FileHash -LiteralPath $formatter).Hash
    if ($fingerprint -cnotin @(Get-ScopedEnvironmentInputs -Tooling)) {
        throw 'The selected formatter bytes are missing from tooling evidence identity.'
    }
}
Write-Host 'PASS tooldiscovery.formatter-bytes-attested'

# Every script that used to run its own vswhere lookup now goes through the shared, version-ranged helper.
$buildScript = Get-Content -LiteralPath (Join-Path $repo 'build.ps1') -Raw
$restoreScript = Get-Content -LiteralPath (Join-Path $repo 'restore-dxui.ps1') -Raw
$packageScript = Get-Content -LiteralPath (Join-Path $repo 'package.ps1') -Raw
$formatScript = Get-Content -LiteralPath (Join-Path $repo 'format.ps1') -Raw

if ($buildScript -notmatch 'Find-RedPrismMSBuild' -or $buildScript -match '(?m)^\s*\$vswhere') {
    throw 'build.ps1 no longer routes MSBuild discovery through the shared, version-ranged helper.'
}
if ($restoreScript -notmatch 'Find-RedPrismMSBuild' -or $restoreScript -match 'vswhere\.exe') {
    throw 'restore-dxui.ps1 no longer routes MSBuild discovery through the shared, version-ranged helper.'
}
if ($packageScript -notmatch 'Find-RedPrismVisualStudioInstallationPath' -or $packageScript -match '(?m)^\s*function Find-VisualStudio') {
    throw 'package.ps1 no longer routes Visual Studio discovery through the shared, version-ranged helper.'
}
if ($formatScript -notmatch 'Find-RedPrismClangFormat') {
    throw 'format.ps1 no longer routes clang-format discovery through the shared helper.'
}
Write-Host 'PASS tooldiscovery.scripts-use-shared-helper'

# package.ps1 uses Start-Process -Environment, a PowerShell 7.4+ parameter: it must declare that requirement so an
# older pwsh fails at parse time with a clear message instead of a confusing runtime error mid-package.
if ($packageScript -notmatch '(?m)^\s*#Requires\s+-Version\s+7\.4\s*$') {
    throw 'package.ps1 uses Start-Process -Environment without declaring #Requires -Version 7.4.'
}
Write-Host 'PASS tooldiscovery.package-requires-pwsh74'

# DxUi.lib is linked into RedPrism.exe: its license must be staged like the other third-party notices.
if ($packageScript -notmatch 'ThirdPartyNotices' -or $packageScript -notmatch "'dxui\.txt'") {
    throw 'package.ps1 does not stage a DxUi third-party notice.'
}
$packagingSpec = Get-Content -LiteralPath (Join-Path $repo 'Specs\Build\Build_Packaging.md') -Raw
if ($packagingSpec -notmatch 'dxui\.txt') {
    throw 'Build_Packaging.md does not document the DxUi third-party notice.'
}
Write-Host 'PASS tooldiscovery.dxui-license-packaged'

# rc.exe must see _DEBUG in Debug and ASan Debug so RedPrism.rc's VS_FF_DEBUG branch is reachable (RF-707).
$buildProps = Get-Content -LiteralPath (Join-Path $repo 'Directory.Build.props') -Raw
$debugResourceCompileGroup = [regex]::Matches($buildProps, '(?s)<ItemDefinitionGroup\b[^>]*>.*?</ItemDefinitionGroup>') |
    Where-Object { $_.Value -match "'Debug'" -and $_.Value -match 'ASan Debug' -and
        $_.Value -match '<ResourceCompile>\s*<PreprocessorDefinitions>_DEBUG;' }
if (-not $debugResourceCompileGroup) {
    throw 'Directory.Build.props does not define _DEBUG for ResourceCompile in Debug / ASan Debug.'
}
$rc = Get-Content -LiteralPath (Join-Path $repo 'RedPrism\RedPrism.rc') -Raw
if ($rc -notmatch '#ifdef _DEBUG' -or $rc -notmatch 'VS_FF_DEBUG') {
    throw 'RedPrism.rc no longer branches its FILEFLAGS on _DEBUG.'
}
Write-Host 'PASS tooldiscovery.resource-compile-sees-debug'
