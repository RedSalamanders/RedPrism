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

    [switch] $Rebuild,
    [string[]] $Suites=@('Host','Document','IO','Composition','Runtime','Stress','Performance','Captures','Tooling'),
    [switch] $Full,
    [switch] $SkipBuild,
    [switch] $SkipTooling,
    [ValidateRange(0, 65535)]
    [int] $BuildNumber = 0,

    [ValidateRange(0, 256)]
    [int] $MaxCpuCount = 0,

    # A deadlock/failure bound, never a performance target. Extended bounds require native ARM64.
    [ValidateRange(1200, 5400)]
    [int] $SelfTestTimeoutSeconds = 1200
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSCommandPath
$available=@('Host','Document','IO','Composition','Runtime','Stress','Performance','Captures','Tooling')
if($Full){$Suites=$available}
$Suites=@($Suites|ForEach-Object{$_ -split ','}|ForEach-Object{$_.Trim()}|Where-Object{$_}|Sort-Object -Unique)
foreach($suite in $Suites){if($suite -notin $available){throw "Unknown suite '$suite'."}}
if($SkipTooling){$Suites=@($Suites|Where-Object{$_ -ne 'Tooling'})}
if($Configuration -ne 'ASan Debug'){$Suites=@($Suites|Where-Object{$_ -ne 'Stress'})}
$nativeArchitecture = [System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString()
if ($Platform -eq 'ARM64' -and $nativeArchitecture -ne 'Arm64') {
    throw 'ARM64 runtime qualification requires a native ARM64 host; use build.ps1 for cross-compilation.'
}

if ($SelfTestTimeoutSeconds -ne 1200 -and ($Platform -ne 'ARM64' -or $nativeArchitecture -ne 'Arm64')) {
    throw 'An extended self-test failure bound requires native ARM64.'
}

if('Tooling' -in $Suites){
    & (Join-Path $repoRoot 'validate.ps1')
    if($LASTEXITCODE -ne 0){throw 'Independent validation failed.'}
}
if(-not @($Suites|Where-Object{$_ -ne 'Tooling'}).Count){Write-Host 'Independent tooling passed; native coverage NOT_EVALUATED';exit 0}
& (Join-Path $repoRoot 'Tools/Fold-Changelog.ps1') -Check
$buildArguments = @{
    Configuration = $Configuration
    Platform = $Platform
    MaxCpuCount = $MaxCpuCount
}
if($BuildNumber -gt 0){$buildArguments.BuildNumber=$BuildNumber}
if ($Rebuild) {
    $buildArguments.Rebuild = $true
}

if(-not $SkipBuild){
& (Join-Path $repoRoot 'build.ps1') @buildArguments
if ($LASTEXITCODE -ne 0) {
    throw "Build entrypoint failed with exit code $LASTEXITCODE."
}

}

Import-Module (Join-Path $repoRoot 'Build/TestRunEvidence.psm1') -Force
$evidence = Start-TestRunEvidence -Repository $repoRoot -Context @{ project='RedPrism'; platform=$Platform; configuration=$Configuration }
$runPassed = $false
try {
$outputRoot = Join-Path $repoRoot ".build\$Platform\$Configuration"
$executable = Join-Path $outputRoot 'RedPrism.exe'
$hostTests = Join-Path $outputRoot 'HostTests.exe'

$info = [Diagnostics.FileVersionInfo]::GetVersionInfo($executable)
if ($info.OriginalFilename -ne 'RedPrism.exe' -or $info.ProductName -ne 'RedPrism') {
    throw 'RedPrism.exe is missing its Windows version identity.'
}

Import-Module (Join-Path $repoRoot 'Build/ScopedTesting.psm1') -Force
$buildAttestation=Join-Path $outputRoot 'build-attestation.json'
if(-not(Test-ScopedReceipt $buildAttestation (Get-ScopedBuildAttestationKey $repoRoot $Platform $Configuration $info.FileBuildPart))){throw 'Native artifact/source/profile/toolchain attestation is stale or missing. Run build.ps1 before -SkipBuild.'}
# First, so that every later run is known to end on a failed runtime check instead of opening a dialog on the desktop.
Write-Host 'Running failure report self-tests...' -ForegroundColor Cyan
& (Join-Path $repoRoot 'Tests\BuildProcessTests\FailureReportTests.ps1') -OutputRoot $outputRoot -Configuration $Configuration -Suites @($Suites | Where-Object {$_ -in @('Host','Document','IO','Composition')} | ForEach-Object {if($_ -eq 'IO'){'IoTests'}else{$_+'Tests'}}) -EvidenceRun $evidence
if ($LASTEXITCODE -ne 0) {
    throw "Failure report self-tests failed with exit code $LASTEXITCODE."
}

foreach ($suite in @(
        @{ Name = 'HostTests'; Scope = 'Host'; Path = $hostTests; Label = 'host contract' },
        @{ Name = 'DocumentTests'; Scope = 'Document'; Path = (Join-Path $outputRoot 'DocumentTests.exe'); Label = 'document model' },
        @{ Name = 'CompositionTests'; Scope = 'Composition'; Path = (Join-Path $outputRoot 'CompositionTests.exe'); Label = 'composition' },
        @{ Name = 'IoTests'; Scope = 'IO'; Path = (Join-Path $outputRoot 'IoTests.exe'); Label = 'project I/O' }
    )) {
    if($suite.Scope -notin $Suites){continue}
    Write-Host "Running $($suite.Label) tests..." -ForegroundColor Cyan
    & $suite.Path 2>&1 | Tee-Object -FilePath (Join-Path $evidence.Path ($suite.Name + '.log'))
    if ($LASTEXITCODE -ne 0) {
        throw "$($suite.Name).exe failed with exit code $LASTEXITCODE."
    }
}

if ($Configuration -eq 'ASan Debug' -and 'Stress' -in $Suites) {
    Write-Host 'Running concurrent raster stress...' -ForegroundColor Cyan
    & (Join-Path $outputRoot 'DocumentTests.exe') --stress 2>&1 | Tee-Object -FilePath (Join-Path $evidence.Path 'DocumentTests.stress.log')
    if ($LASTEXITCODE -ne 0) {
        throw "DocumentTests.exe --stress failed with exit code $LASTEXITCODE."
    }
    Write-Host 'Running concurrent worker stress...' -ForegroundColor Cyan
    & $hostTests --stress 2>&1 | Tee-Object -FilePath (Join-Path $evidence.Path 'HostTests.stress.log')
    if ($LASTEXITCODE -ne 0) {
        throw "HostTests.exe --stress failed with exit code $LASTEXITCODE."
    }
    $probeLog = Join-Path $evidence.Path "ASan-probe-$Platform-$([guid]::NewGuid().ToString('N')).log"
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

if('Runtime' -in $Suites){
foreach ($probeKind in @('main', 'worker')) {
    Write-Host "Running $probeKind crash probe..." -ForegroundColor Cyan
    $crashRoot = Join-Path $evidence.Path ("crash-probe-$probeKind-$Platform-$($Configuration -replace '\s','')-" + [guid]::NewGuid().ToString('N'))
    $crashErr = Join-Path $evidence.Path "RedPrism.crash-probe-$probeKind.err.log"
    $probeArguments = @('--crash-probe', ('"' + $crashRoot + '"'))
    if ($probeKind -eq 'worker') { $probeArguments += '--crash-probe-worker' }
    $probe = Start-Process -WindowStyle Hidden -FilePath $executable -ArgumentList $probeArguments -PassThru `
        -RedirectStandardError $crashErr
    # Keep the process handle open until exit observation. Windows PowerShell can otherwise return an empty
    # ExitCode after a bounded WaitForExit; an unobserved terminal result must never qualify a gate.
    $null = $probe.Handle
    if (-not $probe.WaitForExit(30000)) {
        throw "The $probeKind crash probe timed out (PID $($probe.Id)); it was not terminated."
    }
    $probe.WaitForExit()
    $dumps = @(Get-ChildItem -LiteralPath $crashRoot -Filter 'RedPrism-*.dmp' -ErrorAction SilentlyContinue)
    $pendingMarker = Join-Path $crashRoot 'pending.txt'
    $pendingText = if (Test-Path -LiteralPath $pendingMarker) { (Get-Content -LiteralPath $pendingMarker -Raw).Trim() } else { '' }
    if ($null -eq $probe.ExitCode -or $probe.ExitCode -eq 0 -or $dumps.Count -ne 1 -or $dumps[0].Length -le 0 -or $pendingText -ne $dumps[0].FullName) {
        throw "The $probeKind crash probe did not leave one dump and marker in $crashRoot (exit $($probe.ExitCode), $($dumps.Count) dumps, marker '$pendingText')."
    }
    Write-Host "PASS $probeKind crash probe: $($dumps[0].Name) ($([math]::Round($dumps[0].Length / 1KB)) KB)"

}
$global:LASTEXITCODE = 0

}

if('Runtime' -in $Suites){
Write-Host 'Running command-line help...' -ForegroundColor Cyan
$helpLog = Join-Path $evidence.Path 'RedPrism.help.log'
$helpErr = Join-Path $evidence.Path 'RedPrism.help.err.log'
$help = Start-Process -WindowStyle Hidden -FilePath $executable -ArgumentList @('--help') -Wait -PassThru `
    -RedirectStandardOutput $helpLog -RedirectStandardError $helpErr
if ($help.ExitCode -ne 0) {
    throw "RedPrism.exe --help exited with code $($help.ExitCode)."
}
$helpText = Get-Content -LiteralPath $helpLog -Raw -ErrorAction SilentlyContinue
if ($helpText -notmatch '--self-test' -or $helpText -notmatch '--dpi') {
    throw 'RedPrism.exe --help did not print the command catalog.'
}

$unknownLog = Join-Path $evidence.Path 'RedPrism.unknown.log'
$unknownErr = Join-Path $evidence.Path 'RedPrism.unknown.err.log'
$unknown = Start-Process -WindowStyle Hidden -FilePath $executable -ArgumentList @('--no-such-switch') -Wait -PassThru `
    -RedirectStandardOutput $unknownLog -RedirectStandardError $unknownErr
if ($unknown.ExitCode -ne 2) {
    throw "RedPrism.exe did not reject an unknown switch with exit code 2 (got $($unknown.ExitCode))."
}

Write-Host 'Running hidden WARP self-test...' -ForegroundColor Cyan
$selfTestErr = Join-Path $evidence.Path 'RedPrism.self-test.err.log'
$selfTestLog = Join-Path $evidence.Path 'RedPrism.self-test.log'
$selfTest = Start-Process -WindowStyle Hidden -FilePath $executable -ArgumentList @('--self-test', '--warp') -PassThru `
    -RedirectStandardOutput $selfTestLog -RedirectStandardError $selfTestErr
$null = $selfTest.Handle
Write-Host "Hidden WARP self-test PID $($selfTest.Id); failure bound $SelfTestTimeoutSeconds seconds. This is not a performance qualification."
if (-not $selfTest.WaitForExit($selfTestTimeoutSeconds * 1000)) {
    # AGENTS.md: never terminate a RedPrism.exe this script did not confirm is safe to close. Report the PID and
    # leave it running rather than kill it (RF-702).
    throw "RedPrism.exe --self-test --warp did not finish within $selfTestTimeoutSeconds s (PID $($selfTest.Id) is still running; report it, do not kill it)."
}
# The process has exited; finish asynchronous redirected-stream handlers before inspecting or archiving logs.
$selfTest.WaitForExit()
if ($selfTest.ExitCode -ne 0) {
    $selfTestText = (Get-Content -LiteralPath $selfTestErr -Tail 15 -ErrorAction SilentlyContinue) -join "`n"
    $selfTestOutput = (Get-Content -LiteralPath $selfTestLog -Tail 15 -ErrorAction SilentlyContinue) -join "`n"
    throw "RedPrism.exe --self-test --warp exited with code $($selfTest.ExitCode). Logs: $selfTestLog; $selfTestErr. $selfTestOutput`n$selfTestText"
}
Write-Host "PASS hidden WARP self-test: $selfTestLog" -ForegroundColor Green

}

if('Performance' -in $Suites){
Write-Host 'Running kernel benchmark smoke...' -ForegroundColor Cyan
$kernelSmokeLog = Join-Path $evidence.Path 'RedPrism.kernels-smoke.log'
$kernelSmokeErr = Join-Path $evidence.Path 'RedPrism.kernels-smoke.err.log'
$kernelSmoke = Start-Process -WindowStyle Hidden -FilePath $executable -ArgumentList @('--bench', 'kernels-smoke', '--isa', 'scalar') `
    -PassThru -RedirectStandardOutput $kernelSmokeLog -RedirectStandardError $kernelSmokeErr
$null = $kernelSmoke.Handle
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
    -ExecutablePath $executable -OutputDirectory $evidence.Path
if ($LASTEXITCODE -ne 0) {
    throw "UI benchmark smoke failed with exit code $LASTEXITCODE."
}
$global:LASTEXITCODE = 0

Write-Host 'Running prepared display benchmark smoke...' -ForegroundColor Cyan
& (Join-Path $repoRoot 'Tests\BuildProcessTests\UiBenchmarkSmokeTests.ps1') `
    -ExecutablePath $executable -OutputDirectory $evidence.Path -DisplayRoute prepared
if ($LASTEXITCODE -ne 0) {
    throw "Prepared display benchmark smoke failed with exit code $LASTEXITCODE."
}
$global:LASTEXITCODE = 0

Write-Host 'Running prepared Size final-publication benchmark smoke...' -ForegroundColor Cyan
& (Join-Path $repoRoot 'Tests\BuildProcessTests\UiBenchmarkSmokeTests.ps1') `
    -ExecutablePath $executable -OutputDirectory $evidence.Path -DisplayRoute size-prepared
if ($LASTEXITCODE -ne 0) {
    throw "Prepared Size final-publication benchmark smoke failed with exit code $LASTEXITCODE."
}
$global:LASTEXITCODE = 0

foreach ($cancelRoute in @('direct', 'prepared')) {
    Write-Host "Running UI cancellation smoke ($cancelRoute)..." -ForegroundColor Cyan
    & (Join-Path $repoRoot 'Tests\BuildProcessTests\UiCancellationBenchmarkSmokeTests.ps1') `
        -ExecutablePath $executable -OutputDirectory $evidence.Path -DisplayRoute $cancelRoute
    if ($LASTEXITCODE -ne 0) { throw "UI cancellation smoke ($cancelRoute) failed with exit code $LASTEXITCODE." }
    $global:LASTEXITCODE = 0
}

Write-Host 'Running prepared layer-row benchmark smoke...' -ForegroundColor Cyan
& (Join-Path $repoRoot 'Tests\BuildProcessTests\LayerRowsBenchmarkSmokeTests.ps1') `
    -ExecutablePath $executable -OutputDirectory $evidence.Path
if ($LASTEXITCODE -ne 0) {
    throw "Layer-row benchmark smoke failed with exit code $LASTEXITCODE."
}
$global:LASTEXITCODE = 0

}

if('Performance' -in $Suites){
$foreignIsa = if ($Platform -eq 'x64') { 'neon' } else { 'sse2' }
foreach ($isaCase in @(
    @{ Name = 'not-implemented'; Value = 'avx512'; Pattern = 'not implemented' },
    @{ Name = 'unsupported'; Value = $foreignIsa; Pattern = 'not supported' },
    @{ Name = 'malformed'; Value = 'auto'; Pattern = 'Unknown argument' }
)) {
    $isaError = Join-Path $evidence.Path "RedPrism.isa-$($isaCase.Name).err.log"
    $isaOutput = Join-Path $evidence.Path "RedPrism.isa-$($isaCase.Name).log"
    $isaProcess = Start-Process -WindowStyle Hidden -FilePath $executable `
        -ArgumentList @('--bench', 'kernels-smoke', '--isa', $isaCase.Value) -PassThru `
        -RedirectStandardOutput $isaOutput -RedirectStandardError $isaError
    $null = $isaProcess.Handle
    if (-not $isaProcess.WaitForExit(30000)) {
        throw "ISA refusal did not exit (PID $($isaProcess.Id)); it was not terminated."
    }
    if ($isaProcess.ExitCode -ne 2 -or (Get-Content -LiteralPath $isaError -Raw) -notmatch $isaCase.Pattern) {
        throw "ISA refusal $($isaCase.Name) did not return its expected diagnostic and exit 2."
    }
    Write-Host "PASS kernel-isa.$($isaCase.Name)"
}

}

Write-Host 'Validating DxUi provenance...' -ForegroundColor Cyan
Import-Module (Join-Path $repoRoot 'Build\DxUiProvenance.psm1') -Force
Assert-RedPrismDxUiProvenance -OutputRoot $outputRoot -LockFile (Join-Path $repoRoot 'Dependencies\DxUi.lock.json') `
    -Platform $Platform -Configuration $Configuration
if('Captures' -in $Suites){
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

$shotDir = Join-Path $evidence.Path 'screenshots'
New-Item -ItemType Directory -Path $shotDir -Force | Out-Null
foreach ($shotCase in @(@{ dpi = 96; demo = '' }, @{ dpi = 144; demo = '' }, @{ dpi = 96; demo = 'transform' }, @{ dpi = 96; demo = 'selection' }, @{ dpi = 96; demo = 'brush' }, @{ dpi = 96; demo = 'retouch' }, @{ dpi = 96; demo = 'adjust' }, @{ dpi = 96; demo = 'canvassize' }, @{ dpi = 96; demo = 'crop' })) {
    $dpi = $shotCase.dpi
    $demo = $shotCase.demo
    $shotName = if ($demo) { "$demo-$dpi" } else { "empty-shell-$dpi" }
    $shotPath = Join-Path $shotDir "$shotName-$Platform-$($Configuration -replace '\s','').png"
    $shotLog = Join-Path $evidence.Path "RedPrism.screenshot-$shotName.log"
    $shotErr = Join-Path $evidence.Path "RedPrism.screenshot-$shotName.err.log"
    # The path is quoted as its own argument: Start-Process -ArgumentList joins array elements with a bare space and
    # does not quote them itself, so an unquoted path with a space (a username, "Program Files", ...) would split
    # into two arguments (the crash probe below already quotes its directory the same way).
    $shotArgs = @('--screenshot', ('"' + $shotPath + '"'), '--warp', '--dpi', "$dpi")
    if ($demo) { $shotArgs += @('--demo', $demo) }
    $shot = Start-Process -FilePath $executable -ArgumentList $shotArgs `
        -PassThru -RedirectStandardOutput $shotLog -RedirectStandardError $shotErr
    $null = $shot.Handle
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

}

Write-Host ('Selected suites passed: '+($Suites -join ', ')+'. Unselected/manual/platform/resource coverage remains NOT_EVALUATED.') -ForegroundColor Green
$runPassed = $true
} finally { Complete-TestRunEvidence -Run $evidence -Passed $runPassed }
exit 0
