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

Write-Host 'Running document model tests...' -ForegroundColor Cyan
$documentTests = Join-Path $outputRoot 'DocumentTests.exe'
& $documentTests
if ($LASTEXITCODE -ne 0) {
    throw "DocumentTests.exe failed with exit code $LASTEXITCODE."
}

Write-Host 'Running composition tests...' -ForegroundColor Cyan
$compositionTests = Join-Path $outputRoot 'CompositionTests.exe'
& $compositionTests
if ($LASTEXITCODE -ne 0) {
    throw "CompositionTests.exe failed with exit code $LASTEXITCODE."
}

Write-Host 'Running project I/O tests...' -ForegroundColor Cyan
$ioTests = Join-Path $outputRoot 'IoTests.exe'
& $ioTests
if ($LASTEXITCODE -ne 0) {
    throw "IoTests.exe failed with exit code $LASTEXITCODE."
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
    # The probe executable must exit non-zero. Clear LASTEXITCODE so later PowerShell
    # steps (Start-Process, provenance scripts) do not inherit that native failure.
    $global:LASTEXITCODE = 0
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
if ($helpText -notmatch '--self-test' -or $helpText -notmatch '--dpi') {
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
$selfTestErr = Join-Path $outputRoot 'RedPrism.self-test.err.log'
Remove-Item -LiteralPath $selfTestErr -ErrorAction SilentlyContinue
$selfTest = Start-Process -WindowStyle Hidden -FilePath $executable -ArgumentList @('--self-test', '--warp') -Wait -PassThru `
    -RedirectStandardError $selfTestErr
if ($selfTest.ExitCode -ne 0) {
    $selfTestText = Get-Content -LiteralPath $selfTestErr -Raw -ErrorAction SilentlyContinue
    throw "RedPrism.exe --self-test --warp exited with code $($selfTest.ExitCode). $selfTestText"
}

Write-Host 'Validating DxUi provenance...' -ForegroundColor Cyan
Import-Module (Join-Path $repoRoot 'Build\DxUiProvenance.psm1') -Force
Assert-RedPrismDxUiProvenance -OutputRoot $outputRoot -LockFile (Join-Path $repoRoot 'Dependencies\DxUi.lock.json') `
    -Platform $Platform -Configuration $Configuration
& (Join-Path $repoRoot 'Tests\BuildProcessTests\DxUiProvenanceTests.ps1')
if ($LASTEXITCODE -ne 0) {
    throw "DxUi provenance tests failed with exit code $LASTEXITCODE."
}

Write-Host 'Validating build presentation...' -ForegroundColor Cyan
& (Join-Path $repoRoot 'Tests\BuildProcessTests\BuildPresentationTests.ps1')
if ($LASTEXITCODE -ne 0) {
    throw "Build presentation tests failed with exit code $LASTEXITCODE."
}

Write-Host 'Capturing harness screenshots (empty-document shell)...' -ForegroundColor Cyan
$shotDir = Join-Path $repoRoot '.build\screenshots'
New-Item -ItemType Directory -Path $shotDir -Force | Out-Null
foreach ($shotCase in @(@{ dpi = 96; demo = '' }, @{ dpi = 144; demo = '' }, @{ dpi = 96; demo = 'transform' }, @{ dpi = 96; demo = 'selection' }, @{ dpi = 96; demo = 'brush' })) {
    $dpi = $shotCase.dpi
    $demo = $shotCase.demo
    $shotName = if ($demo) { "$demo-$dpi" } else { "empty-shell-$dpi" }
    $shotPath = Join-Path $shotDir "$shotName-$Platform-$($Configuration -replace '\s','').png"
    Remove-Item -LiteralPath $shotPath -ErrorAction SilentlyContinue
    $shotLog = Join-Path $outputRoot "RedPrism.screenshot-$shotName.log"
    $shotErr = Join-Path $outputRoot "RedPrism.screenshot-$shotName.err.log"
    Remove-Item -LiteralPath $shotLog, $shotErr -ErrorAction SilentlyContinue
    $shotArgs = @('--screenshot', $shotPath, '--warp', '--dpi', "$dpi")
    if ($demo) { $shotArgs += @('--demo', $demo) }
    $shot = Start-Process -FilePath $executable -ArgumentList $shotArgs `
        -PassThru -RedirectStandardOutput $shotLog -RedirectStandardError $shotErr
    if (-not $shot.WaitForExit(60000)) {
        $shot.Kill()
        throw "RedPrism.exe --screenshot --dpi $dpi did not exit within 60 seconds."
    }
    if ($shot.ExitCode -eq 8) {
        Write-Host "SKIP harness screenshot at $dpi DPI (capture unavailable, exit 8)." -ForegroundColor Yellow
    } elseif ($shot.ExitCode -ne 0) {
        throw "RedPrism.exe --screenshot --dpi $dpi exited with code $($shot.ExitCode)."
    } elseif (-not (Test-Path -LiteralPath $shotPath -PathType Leaf)) {
        throw "Screenshot was not written: $shotPath"
    } else {
        $header = [IO.File]::ReadAllBytes($shotPath)
        if ($header.Length -lt 8 -or $header[0] -ne 0x89 -or $header[1] -ne 0x50 -or $header[2] -ne 0x4E -or $header[3] -ne 0x47) {
            throw "Screenshot is not a PNG: $shotPath"
        }
        Write-Host "PASS harness screenshot ($dpi DPI): $shotPath"
    }
}

Write-Host 'All tests passed.' -ForegroundColor Green
exit 0
