<#!
.SYNOPSIS
Builds, cleans, rebuilds, or runs the RedPrism solution.
#>
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release', 'ASan Debug')]
    [string] $Configuration = 'Debug',

    [ValidateSet('x64', 'ARM64')]
    [string] $Platform = 'x64',

    [switch] $Clean,
    [switch] $Rebuild,
    [switch] $Analyze,
    [switch] $Run,

    [ValidateRange(0, 256)]
    [int] $MaxCpuCount = 0,

    [ValidateRange(0, 65535)]
    [int] $BuildNumber = 0
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if ($Clean -and $Rebuild) {
    throw 'Choose either -Clean or -Rebuild, not both.'
}
if ($Clean -and $Run) {
    throw '-Run cannot be combined with -Clean.'
}
if ($Clean -and $Analyze) {
    throw '-Analyze cannot be combined with -Clean.'
}

$repoRoot = Split-Path -Parent $PSCommandPath
$solutionPath = Join-Path $repoRoot 'RedPrism.sln'
$executable = Join-Path $repoRoot ".build\$Platform\$Configuration\RedPrism.exe"

Import-Module (Join-Path $repoRoot 'Build\BuildPresentation.psm1') -Force
Import-Module (Join-Path $repoRoot 'Build\BuildPreflight.psm1') -Force
Import-Module (Join-Path $repoRoot 'Build\VisualStudioDiscovery.psm1') -Force
$useInteractiveTerminal = Test-RedPrismInteractiveTerminal
Write-RedPrismBuildBanner -UseColor $useInteractiveTerminal

function Resolve-BuildNumber {
    param([int] $Requested)
    if ($Requested -ne 0) {
        return $Requested
    }
    Push-Location $repoRoot
    try {
        $count = & git.exe rev-list --count HEAD 2>$null
        if ($LASTEXITCODE -eq 0 -and $count -match '^\d+$') {
            return [int] $count
        }
    } finally {
        Pop-Location
    }
    return 0
}

Assert-RedPrismBuildOutputProcessNotRunning -ExpectedExecutablePath $executable

$BuildNumber = Resolve-BuildNumber -Requested $BuildNumber
Write-Host ("Target: {0} | {1} | version 0.1.{2}.0" -f $Platform, $Configuration, $BuildNumber) -ForegroundColor Magenta

$msbuild = Find-RedPrismMSBuild
$target = if ($Clean) { 'Clean' } elseif ($Rebuild -or $Analyze) { 'Rebuild' } else { 'Build' }
if (-not $Clean) {
    Write-Host '[1/2] Dependencies' -ForegroundColor Cyan
    & (Join-Path $repoRoot 'vcpkg-install.ps1') -Platform $Platform -Asan:($Configuration -eq 'ASan Debug') -MSBuildPath $msbuild
    & (Join-Path $repoRoot 'restore-dxui.ps1') -Platform $Platform -MSBuildPath $msbuild -CheckUpdates
    Write-Host ''
}

$workerArgument = if ($MaxCpuCount -gt 0) { "/m:$MaxCpuCount" } else { '/m' }
$logDir = Join-Path $repoRoot '.build\logs'
New-Item -ItemType Directory -Path $logDir -Force | Out-Null
$logFlavor = if ($Analyze) { '-Analyze' } else { '' }
$logPath = Join-Path $logDir ("{0}-{1}-{2}{3}.log" -f $target, $Platform, ($Configuration -replace '\s', ''), $logFlavor)
$arguments = @(
    $solutionPath,
    "/t:$target",
    "/p:Configuration=$Configuration",
    "/p:Platform=$Platform",
    "/p:RedPrismBuildNumber=$BuildNumber",
    $workerArgument,
    '/nologo',
    '/verbosity:minimal',
    "/flp:LogFile=$logPath;Verbosity=minimal"
)
if ($Analyze) {
    # The root targets scope /analyze and its concurrency plugin to RedPrism projects. Rebuild ensures no stale
    # objects bypass analysis; a distinct log preserves these diagnostics beside ordinary build logs.
    $arguments += '/p:RedPrismAnalyze=true'
}

$stageLabel = if ($Clean) { '[1/1]' } else { '[2/2]' }
Write-Host "$stageLabel $target" -ForegroundColor Cyan
Write-Host "MSBuild: $msbuild" -ForegroundColor DarkGray
if ($Analyze) {
    $previousEspExtensions = [Environment]::GetEnvironmentVariable('Esp.Extensions', 'Process')
    $existingExtensions = @($previousEspExtensions -split ';' | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
    if ($existingExtensions -notcontains 'ConcurrencyCheck.dll') {
        $existingExtensions += 'ConcurrencyCheck.dll'
    }
    [Environment]::SetEnvironmentVariable('Esp.Extensions', ($existingExtensions -join ';'), 'Process')
    try {
        & $msbuild @arguments
        $exitCode = $LASTEXITCODE
    } finally {
        [Environment]::SetEnvironmentVariable('Esp.Extensions', $previousEspExtensions, 'Process')
    }
} else {
    & $msbuild @arguments
    $exitCode = $LASTEXITCODE
}
if ($exitCode -ne 0) {
    Write-Host "Captured log: $logPath" -ForegroundColor DarkGray
    throw "MSBuild failed with exit code $exitCode."
}

if ($Analyze) {
    Write-Host 'Validating native concurrency analysis controls...' -ForegroundColor Cyan
    & (Join-Path $repoRoot 'Tests\BuildProcessTests\CodeAnalysisTests.ps1') `
        -MSBuildPath $msbuild -Platform $Platform -Configuration $Configuration
    if ($LASTEXITCODE -ne 0) {
        throw "Native concurrency analysis controls failed with exit code $LASTEXITCODE."
    }
}

if (-not $Clean) {
    if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
        throw "Build succeeded but the expected executable was not found: $executable"
    }
    if ($Platform -eq 'x64') {
        Write-Host 'Checking linked x64 instructions and ISA gate controls...' -ForegroundColor Cyan
        Import-Module (Join-Path $repoRoot 'Build\IsaScan.psm1') -Force
        $scanRoot = Join-Path $repoRoot ".build\IsaScan\x64\$($Configuration -replace '\s', '')"
        foreach ($name in @('RedPrism', 'HostTests', 'DocumentTests', 'CompositionTests', 'IoTests')) {
            $binary = Join-Path (Split-Path -Parent $executable) "$name.exe"
            $null = Invoke-RedPrismIsaScan -Binary $binary -OutputDirectory (Join-Path $scanRoot $name)
        }
        & (Join-Path $repoRoot 'Tests\BuildProcessTests\IsaScanTests.ps1') -MSBuildPath $msbuild -Configuration $Configuration
    }
    Write-Host "Ready: $executable" -ForegroundColor Green
    Import-Module (Join-Path $repoRoot 'Build\DxUiProvenance.psm1') -Force
    $provenance = Write-RedPrismDxUiProvenance -RepoRoot $repoRoot -Platform $Platform -Configuration $Configuration
    Write-Host "DxUi provenance: $provenance" -ForegroundColor DarkGray
    if ($Run) {
        $native = [System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString()
        if ($Platform -eq 'ARM64' -and $native -ne 'Arm64') {
            throw 'This host cannot directly run the ARM64 build. Build it without -Run or run it on ARM64 Windows.'
        }
        Start-Process -FilePath $executable
    }
}

Write-Host "Captured log: $logPath" -ForegroundColor DarkGray
