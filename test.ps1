<#!
.SYNOPSIS
Builds RedPrism and runs host tests plus the hidden WARP self-test.
#>
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release', 'ASan Debug')]
    [string] $Configuration = 'Debug',

    [ValidateSet('x64', 'ARM64')]
    [string] $Platform = 'x64',

    [switch] $Rebuild
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSCommandPath
$nativeArchitecture = [System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString()
if ($Platform -eq 'ARM64' -and $nativeArchitecture -ne 'Arm64') {
    throw 'ARM64 runtime qualification requires a native ARM64 host; use build.ps1 for cross-compilation.'
}

$buildArguments = @{
    Configuration = $Configuration
    Platform = $Platform
}
if ($Rebuild) {
    $buildArguments.Rebuild = $true
}

& (Join-Path $repoRoot 'build.ps1') @buildArguments
if ($LASTEXITCODE -ne 0) {
    throw "Build entrypoint failed with exit code $LASTEXITCODE."
}

$outputRoot = Join-Path $repoRoot ".build\$Platform\$Configuration"
$executable = Join-Path $outputRoot 'RedPrism.exe'
$hostTests = Join-Path $outputRoot 'HostTests.exe'

$info = [Diagnostics.FileVersionInfo]::GetVersionInfo($executable)
if ($info.OriginalFilename -ne 'RedPrism.exe' -or $info.ProductName -ne 'RedPrism') {
    throw 'RedPrism.exe is missing its Windows version identity.'
}

Write-Host 'Running host contract tests...' -ForegroundColor Cyan
& $hostTests
if ($LASTEXITCODE -ne 0) {
    throw "HostTests.exe failed with exit code $LASTEXITCODE."
}

if ($Configuration -eq 'ASan Debug') {
    $probeLog = Join-Path $repoRoot ".build\logs\ASan-probe-$Platform-$([guid]::NewGuid().ToString('N')).log"
    New-Item -ItemType Directory -Path (Split-Path $probeLog) -Force | Out-Null
    $previousOptions = $env:ASAN_OPTIONS
    try {
        $env:ASAN_OPTIONS = 'halt_on_error=1:abort_on_error=0:detect_leaks=0'
        & $hostTests --asan-probe *> $probeLog
        $probeExit = $LASTEXITCODE
    } finally {
        $env:ASAN_OPTIONS = $previousOptions
    }
    if ($probeExit -eq 0 -or -not (Select-String -LiteralPath $probeLog -SimpleMatch 'AddressSanitizer: heap-use-after-free' -ErrorAction SilentlyContinue)) {
        throw "ASAN failed to diagnose the isolated deliberate defect: $probeLog"
    }
    Write-Host "PASS AddressSanitizer detection probe: $probeLog"
}

Write-Host 'Running command-line help...' -ForegroundColor Cyan
$helpLog = Join-Path $outputRoot 'RedPrism.help.log'
$helpErr = Join-Path $outputRoot 'RedPrism.help.err.log'
Remove-Item -LiteralPath $helpLog, $helpErr -ErrorAction SilentlyContinue
$help = Start-Process -WindowStyle Hidden -FilePath $executable -ArgumentList @('--help') -Wait -PassThru `
    -RedirectStandardOutput $helpLog -RedirectStandardError $helpErr
if ($help.ExitCode -ne 0) {
    throw "RedPrism.exe --help exited with code $($help.ExitCode)."
}
$helpText = Get-Content -LiteralPath $helpLog -Raw -ErrorAction SilentlyContinue
if ($helpText -notmatch '--self-test') {
    throw 'RedPrism.exe --help did not print the command catalog.'
}

$unknownLog = Join-Path $outputRoot 'RedPrism.unknown.log'
$unknownErr = Join-Path $outputRoot 'RedPrism.unknown.err.log'
Remove-Item -LiteralPath $unknownLog, $unknownErr -ErrorAction SilentlyContinue
$unknown = Start-Process -WindowStyle Hidden -FilePath $executable -ArgumentList @('--no-such-switch') -Wait -PassThru `
    -RedirectStandardOutput $unknownLog -RedirectStandardError $unknownErr
if ($unknown.ExitCode -ne 2) {
    throw "RedPrism.exe did not reject an unknown switch with exit code 2 (got $($unknown.ExitCode))."
}

Write-Host 'Running hidden WARP self-test...' -ForegroundColor Cyan
$selfTest = Start-Process -WindowStyle Hidden -FilePath $executable -ArgumentList @('--self-test', '--warp') -Wait -PassThru
if ($selfTest.ExitCode -ne 0) {
    throw "RedPrism.exe --self-test --warp exited with code $($selfTest.ExitCode)."
}

Write-Host 'All tests passed.' -ForegroundColor Green
exit 0
