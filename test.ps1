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

foreach ($suite in @(
        @{ Name = 'HostTests'; Path = $hostTests; Label = 'host contract' },
        @{ Name = 'DocumentTests'; Path = (Join-Path $outputRoot 'DocumentTests.exe'); Label = 'document model' },
        @{ Name = 'CompositionTests'; Path = (Join-Path $outputRoot 'CompositionTests.exe'); Label = 'composition' },
        @{ Name = 'IoTests'; Path = (Join-Path $outputRoot 'IoTests.exe'); Label = 'project I/O' }
    )) {
    Write-Host "Running $($suite.Label) tests..." -ForegroundColor Cyan
    & $suite.Path
    if ($LASTEXITCODE -ne 0) {
        throw "$($suite.Name).exe failed with exit code $LASTEXITCODE."
    }
}

if ($Configuration -eq 'ASan Debug') {
    Write-Host 'Running concurrent raster stress...' -ForegroundColor Cyan
    & (Join-Path $outputRoot 'DocumentTests.exe') --stress
    if ($LASTEXITCODE -ne 0) {
        throw "DocumentTests.exe --stress failed with exit code $LASTEXITCODE."
    }
    Write-Host 'Running concurrent worker stress...' -ForegroundColor Cyan
    & $hostTests --stress
    if ($LASTEXITCODE -ne 0) {
        throw "HostTests.exe --stress failed with exit code $LASTEXITCODE."
    }
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

foreach ($probeKind in @('main', 'worker')) {
    Write-Host "Running $probeKind crash probe..." -ForegroundColor Cyan
    $crashRoot = Join-Path $repoRoot (".build\logs\crash-probe-$probeKind-$Platform-$($Configuration -replace '\s','')-" + [guid]::NewGuid().ToString('N'))
    $crashErr = Join-Path $outputRoot "RedPrism.crash-probe-$probeKind.err.log"
    $probeArguments = @('--crash-probe', ('"' + $crashRoot + '"'))
    if ($probeKind -eq 'worker') { $probeArguments += '--crash-probe-worker' }
    $probe = Start-Process -WindowStyle Hidden -FilePath $executable -ArgumentList $probeArguments -PassThru `
        -RedirectStandardError $crashErr
    if (-not $probe.WaitForExit(30000)) {
        throw "The $probeKind crash probe timed out (PID $($probe.Id)); it was not terminated."
    }
    $probe.WaitForExit()
    $dumps = @(Get-ChildItem -LiteralPath $crashRoot -Filter 'RedPrism-*.dmp' -ErrorAction SilentlyContinue)
    $pendingMarker = Join-Path $crashRoot 'pending.txt'
    $pendingText = if (Test-Path -LiteralPath $pendingMarker) { (Get-Content -LiteralPath $pendingMarker -Raw).Trim() } else { '' }
    if ($probe.ExitCode -eq 0 -or $dumps.Count -ne 1 -or $dumps[0].Length -le 0 -or $pendingText -ne $dumps[0].FullName) {
        throw "The $probeKind crash probe did not leave one dump and marker in $crashRoot (exit $($probe.ExitCode), $($dumps.Count) dumps, marker '$pendingText')."
    }
    Write-Host "PASS $probeKind crash probe: $($dumps[0].Name) ($([math]::Round($dumps[0].Length / 1KB)) KB)"
    $resolvedCrashRoot = [IO.Path]::GetFullPath($crashRoot)
    $expectedCrashParent = [IO.Path]::GetFullPath((Join-Path $repoRoot '.build\logs')) + [IO.Path]::DirectorySeparatorChar
    if (-not $resolvedCrashRoot.StartsWith($expectedCrashParent, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing probe cleanup outside $expectedCrashParent"
    }
    Remove-Item -LiteralPath $resolvedCrashRoot -Recurse -Force
}
$global:LASTEXITCODE = 0

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
$selfTestTimeoutSeconds = 1200
$selfTest = Start-Process -WindowStyle Hidden -FilePath $executable -ArgumentList @('--self-test', '--warp') -PassThru `
    -RedirectStandardError $selfTestErr
if (-not $selfTest.WaitForExit($selfTestTimeoutSeconds * 1000)) {
    # AGENTS.md: never terminate a RedPrism.exe this script did not confirm is safe to close. Report the PID and
    # leave it running rather than kill it (RF-702).
    throw "RedPrism.exe --self-test --warp did not finish within $selfTestTimeoutSeconds s (PID $($selfTest.Id) is still running; report it, do not kill it)."
}
if ($selfTest.ExitCode -ne 0) {
    $selfTestText = Get-Content -LiteralPath $selfTestErr -Raw -ErrorAction SilentlyContinue
    throw "RedPrism.exe --self-test --warp exited with code $($selfTest.ExitCode). $selfTestText"
}

Write-Host 'Running kernel benchmark smoke...' -ForegroundColor Cyan
$kernelSmokeLog = Join-Path $outputRoot 'RedPrism.kernels-smoke.log'
$kernelSmokeErr = Join-Path $outputRoot 'RedPrism.kernels-smoke.err.log'
Remove-Item -LiteralPath $kernelSmokeLog, $kernelSmokeErr -ErrorAction SilentlyContinue
$kernelSmoke = Start-Process -WindowStyle Hidden -FilePath $executable -ArgumentList @('--bench', 'kernels-smoke', '--isa', 'scalar') `
    -PassThru -RedirectStandardOutput $kernelSmokeLog -RedirectStandardError $kernelSmokeErr
$kernelSmokeTimeoutSeconds = 60
if (-not $kernelSmoke.WaitForExit($kernelSmokeTimeoutSeconds * 1000)) {
    throw "RedPrism.exe --bench kernels-smoke did not finish within $kernelSmokeTimeoutSeconds s (PID $($kernelSmoke.Id) is still running; report it, do not kill it)."
}
if ($kernelSmoke.ExitCode -ne 0) {
    $kernelSmokeError = Get-Content -LiteralPath $kernelSmokeErr -Raw -ErrorAction SilentlyContinue
    throw "RedPrism.exe --bench kernels-smoke exited with code $($kernelSmoke.ExitCode). $kernelSmokeError"
}
$kernelSmokeLines = @(Get-Content -LiteralPath $kernelSmokeLog -ErrorAction SilentlyContinue)
$kernelOperationRows = @($kernelSmokeLines | Where-Object { $_ -match '^KERNEL BENCH name=' })
if ($kernelOperationRows.Count -ne 13) {
    throw "RedPrism.exe --bench kernels-smoke printed $($kernelOperationRows.Count) operation rows, expected 13 ($kernelSmokeLog)."
}
$kernelNames = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::Ordinal)
$kernelChecksums = @{}
foreach ($row in $kernelOperationRows) {
    if ($row -match '^KERNEL BENCH name=(?<name>[A-Za-z0-9_]+) width=32 height=24 samples=2 warmup=1 median_ms=\S+ p95_ms=\S+ max_ms=\S+ checksum=(?<checksum>[0-9A-Fa-f]+) arch=\S+ config=\S+ isa=scalar$') {
        $kernelName = $Matches['name']
        $kernelChecksums[$kernelName] = $Matches['checksum']
    } else {
        throw "Malformed kernel benchmark smoke operation row: $row"
    }
    if (-not $kernelNames.Add($kernelName)) {
        throw "Duplicate kernel benchmark smoke operation: $kernelName"
    }
}
$kernelReports = @($kernelSmokeLines | Where-Object { $_ -match '^KERNEL BENCH REPORT\b' })
if ($kernelReports.Count -ne 1 -or $kernelReports[0] -notmatch '^KERNEL BENCH REPORT status=ok width=32 height=24 operations=13 arch=\S+ config=\S+$') {
    throw "RedPrism.exe --bench kernels-smoke did not print exactly one valid completion report ($kernelSmokeLog)."
}
$bufferRows = @($kernelSmokeLines | Where-Object { $_ -match '^BUFFER BENCH name=' })
$bufferNames = @{ levels = 'levels_scalar'; hue_saturation = 'hue_saturation_scalar'; uniform_noise = 'uniform_noise_scalar' }
if ($bufferRows.Count -ne 3) {
    throw "Expected three buffer comparison rows, got $($bufferRows.Count)."
}
foreach ($row in $bufferRows) {
    if ($row -notmatch '^BUFFER BENCH name=(?<name>[A-Za-z0-9_]+) width=32 height=24 samples=2 warmup=1 copy_median_ms=\S+ adopt_median_ms=\S+ median_saved_ms=\S+ median_speedup=\S+ checksum=(?<checksum>[0-9A-Fa-f]+) arch=\S+ config=\S+$') {
        throw "Malformed buffer comparison row: $row"
    }
    $bufferName = $Matches['name']
    if (-not $bufferNames.ContainsKey($bufferName) -or $Matches['checksum'] -ne $kernelChecksums[$bufferNames[$bufferName]]) {
        throw "Unknown, duplicate or mismatched buffer comparison: $row"
    }
    $bufferNames.Remove($bufferName)
}
$bufferReports = @($kernelSmokeLines | Where-Object { $_ -match '^BUFFER BENCH REPORT\b' })
if ($bufferReports.Count -ne 1 -or $bufferReports[0] -notmatch '^BUFFER BENCH REPORT status=ok width=32 height=24 operations=3 arch=\S+ config=\S+$') {
    throw 'Missing or malformed buffer comparison completion report.'
}
Write-Host 'PASS kernel benchmark smoke: 13 operations and 3 equivalent copy/adopt pairs, 32x24, 2 samples each'

Write-Host 'Running UI benchmark smoke...' -ForegroundColor Cyan
& (Join-Path $repoRoot 'Tests\BuildProcessTests\UiBenchmarkSmokeTests.ps1') `
    -ExecutablePath $executable -OutputDirectory $outputRoot
if ($LASTEXITCODE -ne 0) {
    throw "UI benchmark smoke failed with exit code $LASTEXITCODE."
}
$global:LASTEXITCODE = 0

Write-Host 'Running qualified gradient benchmark runner controls...' -ForegroundColor Cyan
& (Join-Path $repoRoot 'Tests\BuildProcessTests\GradientBenchmarkRunnerTests.ps1')
if ($LASTEXITCODE -ne 0) {
    throw "Gradient benchmark runner controls failed with exit code $LASTEXITCODE."
}

$foreignIsa = if ($Platform -eq 'x64') { 'neon' } else { 'sse2' }
foreach ($isaCase in @(
    @{ Name = 'not-implemented'; Value = 'avx512'; Pattern = 'not implemented' },
    @{ Name = 'unsupported'; Value = $foreignIsa; Pattern = 'not supported' },
    @{ Name = 'malformed'; Value = 'auto'; Pattern = 'Unknown argument' }
)) {
    $isaError = Join-Path $outputRoot "RedPrism.isa-$($isaCase.Name).err.log"
    $isaOutput = Join-Path $outputRoot "RedPrism.isa-$($isaCase.Name).log"
    $isaProcess = Start-Process -WindowStyle Hidden -FilePath $executable `
        -ArgumentList @('--bench', 'kernels-smoke', '--isa', $isaCase.Value) -PassThru `
        -RedirectStandardOutput $isaOutput -RedirectStandardError $isaError
    if (-not $isaProcess.WaitForExit(30000)) {
        throw "ISA refusal did not exit (PID $($isaProcess.Id)); it was not terminated."
    }
    if ($isaProcess.ExitCode -ne 2 -or (Get-Content -LiteralPath $isaError -Raw) -notmatch $isaCase.Pattern) {
        throw "ISA refusal $($isaCase.Name) did not return its expected diagnostic and exit 2."
    }
    Write-Host "PASS kernel-isa.$($isaCase.Name)"
}

Write-Host 'Running CRT report hook probe...' -ForegroundColor Cyan
& (Join-Path $repoRoot 'Tests\BuildProcessTests\CrtReportHookTests.ps1') -HostTestsPath $hostTests -Configuration $Configuration
if ($LASTEXITCODE -ne 0) {
    throw "CRT report hook tests failed with exit code $LASTEXITCODE."
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

Write-Host 'Validating the build preflight helper...' -ForegroundColor Cyan
& (Join-Path $repoRoot 'Tests\BuildProcessTests\BuildPreflightTests.ps1')
if ($LASTEXITCODE -ne 0) {
    throw "Build preflight tests failed with exit code $LASTEXITCODE."
}

Write-Host 'Validating tool discovery...' -ForegroundColor Cyan
& (Join-Path $repoRoot 'Tests\BuildProcessTests\ToolDiscoveryTests.ps1')
if ($LASTEXITCODE -ne 0) {
    throw "Tool discovery tests failed with exit code $LASTEXITCODE."
}

Write-Host 'Validating ARM64 ASan wiring...' -ForegroundColor Cyan
& (Join-Path $repoRoot 'Tests\BuildProcessTests\AsanArm64Tests.ps1')
if ($LASTEXITCODE -ne 0) {
    throw "ARM64 ASan wiring tests failed with exit code $LASTEXITCODE."
}

Write-Host 'Validating the WPR performance profile (read-only)...' -ForegroundColor Cyan
& (Join-Path $repoRoot 'Tests\BuildProcessTests\WprProfileTests.ps1')
if ($LASTEXITCODE -ne 0) {
    throw "WPR profile tests failed with exit code $LASTEXITCODE."
}

Write-Host 'Validating RAW corpus preparation (offline only)...' -ForegroundColor Cyan
& (Join-Path $repoRoot 'Tests\BuildProcessTests\RawCorpusTests.ps1')
if ($LASTEXITCODE -ne 0) {
    throw "RAW corpus preparation tests failed with exit code $LASTEXITCODE."
}

Write-Host 'Capturing harness screenshots (empty-document shell)...' -ForegroundColor Cyan
Add-Type -AssemblyName System.Drawing

function Test-RedPrismScreenshotContent {
    <#
    .SYNOPSIS
    Rejects a harness screenshot that is not a real capture. RF-701: the old gate only checked the 8-byte PNG
    signature, so all 27 captures of the 2026-09-21 run — every one a fully transparent client area — passed.
    This decodes the PNG and samples a grid of pixels across it, rejecting a capture that is too small to be a real
    client area, fully transparent, or a single uniform color.
    #>
    param([Parameter(Mandatory)] [string] $Path)

    $bitmap = [Drawing.Bitmap]::new($Path)
    try {
        $width = $bitmap.Width
        $height = $bitmap.Height
        if ($width -lt 32 -or $height -lt 32) {
            return "the captured size ${width}x${height} is too small to be a real client area"
        }
        $stepX = [Math]::Max(1, [int]($width / 96))
        $stepY = [Math]::Max(1, [int]($height / 96))
        $first = $bitmap.GetPixel(0, 0)
        $uniform = $true
        $transparent = $true
        for ($y = 0; $y -lt $height; $y += $stepY) {
            for ($x = 0; $x -lt $width; $x += $stepX) {
                $pixel = $bitmap.GetPixel($x, $y)
                if ($pixel.A -ne 0) {
                    $transparent = $false
                }
                if ($pixel.ToArgb() -ne $first.ToArgb()) {
                    $uniform = $false
                }
            }
        }
        if ($transparent) {
            return 'the captured client area is fully transparent'
        }
        if ($uniform) {
            return 'the captured client area is a single uniform color'
        }
        return $null
    } finally {
        $bitmap.Dispose()
    }
}

$shotDir = Join-Path $repoRoot '.build\screenshots'
New-Item -ItemType Directory -Path $shotDir -Force | Out-Null
foreach ($shotCase in @(@{ dpi = 96; demo = '' }, @{ dpi = 144; demo = '' }, @{ dpi = 96; demo = 'transform' }, @{ dpi = 96; demo = 'selection' }, @{ dpi = 96; demo = 'brush' }, @{ dpi = 96; demo = 'retouch' }, @{ dpi = 96; demo = 'adjust' }, @{ dpi = 96; demo = 'canvassize' }, @{ dpi = 96; demo = 'crop' })) {
    $dpi = $shotCase.dpi
    $demo = $shotCase.demo
    $shotName = if ($demo) { "$demo-$dpi" } else { "empty-shell-$dpi" }
    $shotPath = Join-Path $shotDir "$shotName-$Platform-$($Configuration -replace '\s','').png"
    Remove-Item -LiteralPath $shotPath -ErrorAction SilentlyContinue
    $shotLog = Join-Path $outputRoot "RedPrism.screenshot-$shotName.log"
    $shotErr = Join-Path $outputRoot "RedPrism.screenshot-$shotName.err.log"
    Remove-Item -LiteralPath $shotLog, $shotErr -ErrorAction SilentlyContinue
    # The path is quoted as its own argument: Start-Process -ArgumentList joins array elements with a bare space and
    # does not quote them itself, so an unquoted path with a space (a username, "Program Files", ...) would split
    # into two arguments (the crash probe below already quotes its directory the same way).
    $shotArgs = @('--screenshot', ('"' + $shotPath + '"'), '--warp', '--dpi', "$dpi")
    if ($demo) { $shotArgs += @('--demo', $demo) }
    $shot = Start-Process -FilePath $executable -ArgumentList $shotArgs `
        -PassThru -RedirectStandardOutput $shotLog -RedirectStandardError $shotErr
    if (-not $shot.WaitForExit(60000)) {
        $shot.Kill()
        throw "RedPrism.exe --screenshot ($shotName) did not exit within 60 seconds."
    }
    if ($shot.ExitCode -eq 8) {
        # WP4: RedPrism returns 8 only when the harness capture path is unsupported on this host. Any other
        # nonzero exit (including the distinct "capture failed" code) is a real failure below, not a skip.
        Write-Host "SKIP harness screenshot ($shotName): capture unsupported (exit 8)." -ForegroundColor Yellow
        continue
    }
    if ($shot.ExitCode -ne 0) {
        throw "RedPrism.exe --screenshot ($shotName) exited with code $($shot.ExitCode)."
    }
    if (-not (Test-Path -LiteralPath $shotPath -PathType Leaf)) {
        throw "Screenshot was not written: $shotPath"
    }
    $header = [IO.File]::ReadAllBytes($shotPath)
    if ($header.Length -lt 8 -or $header[0] -ne 0x89 -or $header[1] -ne 0x50 -or $header[2] -ne 0x4E -or $header[3] -ne 0x47) {
        throw "Screenshot is not a PNG: $shotPath"
    }
    $problem = Test-RedPrismScreenshotContent -Path $shotPath
    if ($problem) {
        throw "Screenshot ($shotName) failed content validation: $problem ($shotPath)."
    }
    Write-Host "PASS harness screenshot ($shotName): $shotPath"
}

Write-Host 'All tests passed.' -ForegroundColor Green
exit 0
