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

$repoRoot = Split-Path -Parent $PSCommandPath
$solutionPath = Join-Path $repoRoot 'RedPrism.sln'
$executable = Join-Path $repoRoot ".build\$Platform\$Configuration\RedPrism.exe"

function Assert-BuildOutputProcessNotRunning {
    param([string] $ExpectedExecutablePath)
    if (-not (Test-Path -LiteralPath $ExpectedExecutablePath -PathType Leaf)) {
        return
    }
    $expected = [IO.Path]::GetFullPath($ExpectedExecutablePath)
    Get-CimInstance Win32_Process -Filter "Name='RedPrism.exe'" -ErrorAction SilentlyContinue | ForEach-Object {
        if ($_.ExecutablePath) {
            $running = [IO.Path]::GetFullPath($_.ExecutablePath)
            if ($running -eq $expected) {
                throw "RedPrism.exe is running from $expected (PID $($_.ProcessId)). Close it; this script will not terminate it."
            }
        }
    }
}

function Find-MSBuild {
    if ($env:MSBUILD_EXE_PATH -and (Test-Path -LiteralPath $env:MSBUILD_EXE_PATH -PathType Leaf)) {
        return $env:MSBUILD_EXE_PATH
    }

    $pathCommand = Get-Command 'msbuild.exe' -ErrorAction SilentlyContinue
    if ($pathCommand) {
        return $pathCommand.Source
    }

    $vswhereCandidates = @(
        (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'),
        (Join-Path $env:ProgramFiles 'Microsoft Visual Studio\Installer\vswhere.exe')
    ) | Where-Object { $_ -and (Test-Path -LiteralPath $_ -PathType Leaf) }

    foreach ($vswhere in $vswhereCandidates) {
        $matches = & $vswhere -all -prerelease -products '*' -requires Microsoft.Component.MSBuild `
            -find 'MSBuild\**\Bin\amd64\MSBuild.exe' 2>$null
        $candidate = $matches | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
        if ($candidate) {
            return $candidate
        }
    }

    throw 'MSBuild was not found. Install the Visual Studio Desktop development with C++ workload.'
}

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

Assert-BuildOutputProcessNotRunning -ExpectedExecutablePath $executable

$BuildNumber = Resolve-BuildNumber -Requested $BuildNumber
Write-Host ("Target: {0} | {1} | version 0.1.{2}.0" -f $Platform, $Configuration, $BuildNumber) -ForegroundColor Magenta

$msbuild = Find-MSBuild
$target = if ($Clean) { 'Clean' } elseif ($Rebuild) { 'Rebuild' } else { 'Build' }
if (-not $Clean) {
    Write-Host '[1/2] Dependencies' -ForegroundColor Cyan
    & (Join-Path $repoRoot 'vcpkg-install.ps1') -Platform $Platform
    Write-Host ''
}

$workerArgument = if ($MaxCpuCount -gt 0) { "/m:$MaxCpuCount" } else { '/m' }
$logDir = Join-Path $repoRoot '.build\logs'
New-Item -ItemType Directory -Path $logDir -Force | Out-Null
$logPath = Join-Path $logDir ("{0}-{1}-{2}.log" -f $target, $Platform, ($Configuration -replace '\s', ''))
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

$stageLabel = if ($Clean) { '[1/1]' } else { '[2/2]' }
Write-Host "$stageLabel $target" -ForegroundColor Cyan
Write-Host "MSBuild: $msbuild" -ForegroundColor DarkGray
& $msbuild @arguments
$exitCode = $LASTEXITCODE
if ($exitCode -ne 0) {
    Write-Host "Captured log: $logPath" -ForegroundColor DarkGray
    throw "MSBuild failed with exit code $exitCode."
}

if (-not $Clean) {
    if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
        throw "Build succeeded but the expected executable was not found: $executable"
    }
    Write-Host "Ready: $executable" -ForegroundColor Green
    if ($Run) {
        $native = [System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString()
        if ($Platform -eq 'ARM64' -and $native -ne 'Arm64') {
            throw 'This host cannot directly run the ARM64 build. Build it without -Run or run it on ARM64 Windows.'
        }
        Start-Process -FilePath $executable
    }
}

Write-Host "Captured log: $logPath" -ForegroundColor DarkGray
