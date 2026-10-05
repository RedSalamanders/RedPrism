<#
.SYNOPSIS
Runs the real UI benchmark loop on its bounded 64x48 smoke fixture and validates its completion report.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $ExecutablePath,
    [Parameter(Mandatory)] [string] $OutputDirectory
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $ExecutablePath -PathType Leaf)) {
    throw "RedPrism.exe was not found: $ExecutablePath"
}
if (-not (Test-Path -LiteralPath $OutputDirectory -PathType Container)) {
    throw "Benchmark smoke output directory was not found: $OutputDirectory"
}

$stdoutPath = Join-Path $OutputDirectory 'RedPrism.ui-smoke.log'
$stderrPath = Join-Path $OutputDirectory 'RedPrism.ui-smoke.err.log'
Remove-Item -LiteralPath $stdoutPath, $stderrPath -ErrorAction SilentlyContinue
$process = Start-Process -WindowStyle Hidden -FilePath $ExecutablePath `
    -ArgumentList @('--warp', '--bench', 'ui-smoke', '--isa', 'scalar') -PassThru `
    -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath
$null = $process.Handle # Retain exit-observation authority through the bounded wait on Windows PowerShell.
$timeoutSeconds = 120
if (-not $process.WaitForExit($timeoutSeconds * 1000)) {
    throw "RedPrism.exe --warp --bench ui-smoke --isa scalar did not finish within $timeoutSeconds s (PID $($process.Id) is still running; report it, do not kill it)."
}
$process.WaitForExit()

$stderr = if (Test-Path -LiteralPath $stderrPath) { Get-Content -LiteralPath $stderrPath -Raw } else { '' }
if ($process.ExitCode -ne 0) {
    throw "RedPrism.exe --bench ui-smoke exited with code $($process.ExitCode). $stderr"
}

$lines = @(Get-Content -LiteralPath $stdoutPath)
$machineRows = @($lines | Where-Object { $_ -match '^BENCH MACHINE\b' })
if ($machineRows.Count -ne 1) {
    throw "UI benchmark smoke printed $($machineRows.Count) BENCH MACHINE lines, expected exactly one ($stdoutPath)."
}

$expectedNames = @(
    'zoom-actual', 'zoom-fit', 'levels-open', 'levels-update', 'levels-commit', 'hsv-update',
    'exposure-update', 'gaussian-update', 'noise-update', 'grain-update', 'lens-update',
    'invert', 'fill', 'clear', 'undo', 'canvas-size-fill', 'image-size-nearest', 'image-size-resolution', 'project-save', 'project-open',
    'png-export', 'image-import-new-canvas', 'image-import-existing-layer', 'jpeg-export',
    'gradient-preview-coalesced', 'gradient-apply-ordered', 'sampler-accepted-prefix'
)
$operationRows = @($lines | Where-Object { $_ -match '^UI BENCH name=' })
if ($operationRows.Count -ne $expectedNames.Count) {
    throw "UI benchmark smoke printed $($operationRows.Count) operation rows, expected $($expectedNames.Count) ($stdoutPath)."
}

$seenNames = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::Ordinal)
foreach ($row in $operationRows) {
    if ($row -notmatch '^UI BENCH name=(?<name>[a-z0-9-]+) width=64 height=48 runs=2 warmup=1\b') {
        throw "Malformed UI benchmark smoke operation row (expected dimensions and run counts): $row"
    }
    $name = $Matches['name']
    if ($row -notmatch '\bframes=(?<frames>\d+)\b') {
        throw "UI benchmark smoke operation row is missing its frame count: $row"
    }
    $frames = [long]$Matches['frames']
    $durations = @{}
    foreach ($field in 'elapsedMedianUs', 'elapsedP95Us', 'elapsedMaxUs', 'dispatchP99UpperUs', 'dispatchMaxUs',
        'messageP99UpperUs', 'messageMaxUs', 'drainP99UpperUs', 'drainMaxUs', 'applyMaxUs', 'actionMaxUs',
        'frameP99UpperUs', 'frameMaxUs') {
        if ($row -notmatch "\b$field=\d+(?:\.\d+)?\b") {
            throw "UI benchmark smoke operation row is missing timing field '$field': $row"
        }
        $durations[$field] = [double]::Parse(($Matches[0] -split '=')[1], [System.Globalization.CultureInfo]::InvariantCulture)
    }
    if ($durations.elapsedMedianUs -gt $durations.elapsedP95Us -or $durations.elapsedP95Us -gt $durations.elapsedMaxUs) {
        throw "UI benchmark smoke operation '$name' has inconsistent elapsed distribution: $row"
    }
    if (-not $seenNames.Add($name)) {
        throw "Duplicate UI benchmark smoke operation: $name"
    }
    # Report sums presentations over both recorded trials; each must have at least one.
    if ($frames -lt 2) {
        throw "UI benchmark smoke operation '$name' reported fewer than two frames: $row"
    }
    $readback = @{}
    foreach ($field in 'readbackCopies', 'readbackMaps', 'readinessBarriers', 'busyFailures') {
        if ($row -notmatch "\b$field=(?<count>\d+)\b") { throw "Missing readback counter '$field': $row" }
        $readback[$field] = [long]$Matches['count']
    }
    if ($readback.busyFailures -ne 0 -or $readback.readbackMaps -ne $readback.readbackCopies + $readback.readinessBarriers -or
        $readback.readinessBarriers -gt $readback.readbackCopies) {
        throw "Invalid bounded readback counters: $row"
    }
    if ($name -in @('png-export', 'jpeg-export') -and $readback.readbackCopies -ne 2) {
        throw "Each smoke export trial must traverse one GPU tile: $row"
    }
    if ($name -eq 'sampler-accepted-prefix' -and $readback.readbackCopies -ne 6) {
        throw "Each smoke sampler trial must read its three accepted coordinates: $row"
    }
}
foreach ($name in $expectedNames) {
    if (-not $seenNames.Contains($name)) {
        throw "UI benchmark smoke did not report expected operation '$name'."
    }
}

$reports = @($lines | Where-Object { $_ -match '^UI BENCH REPORT\b' })
if ($reports.Count -ne 1 -or
    $reports[0] -notmatch '^UI BENCH REPORT status=ok width=64 height=48 operations=27(?:\s+.*)?$') {
    throw "UI benchmark smoke did not print exactly one valid completion report ($stdoutPath)."
}

Write-Host 'PASS UI benchmark smoke: 27 operations, 64x48, 2 samples after one warmup'
$global:LASTEXITCODE = 0

$samplerStdoutPath = Join-Path $OutputDirectory 'RedPrism.ui-sampler-smoke.log'
$samplerStderrPath = Join-Path $OutputDirectory 'RedPrism.ui-sampler-smoke.err.log'
Remove-Item -LiteralPath $samplerStdoutPath, $samplerStderrPath -ErrorAction SilentlyContinue
$samplerProcess = Start-Process -WindowStyle Hidden -FilePath $ExecutablePath `
    -ArgumentList @('--warp', '--bench', 'ui-sampler-smoke', '--isa', 'scalar') -PassThru `
    -RedirectStandardOutput $samplerStdoutPath -RedirectStandardError $samplerStderrPath
$null = $samplerProcess.Handle # Retain exit-observation authority through the bounded wait on Windows PowerShell.
if (-not $samplerProcess.WaitForExit($timeoutSeconds * 1000)) {
    throw "RedPrism.exe --warp --bench ui-sampler-smoke --isa scalar did not finish within $timeoutSeconds s (PID $($samplerProcess.Id) is still running; report it, do not kill it)."
}
$samplerProcess.WaitForExit()

$samplerStderr = if (Test-Path -LiteralPath $samplerStderrPath) { Get-Content -LiteralPath $samplerStderrPath -Raw } else { '' }
if ($samplerProcess.ExitCode -ne 0) {
    throw "RedPrism.exe --bench ui-sampler-smoke exited with code $($samplerProcess.ExitCode). $samplerStderr"
}

$samplerLines = @(Get-Content -LiteralPath $samplerStdoutPath)
$samplerMachineRows = @($samplerLines | Where-Object { $_ -match '^BENCH MACHINE\b' })
if ($samplerMachineRows.Count -ne 1) {
    throw "UI sampler smoke printed $($samplerMachineRows.Count) BENCH MACHINE lines, expected exactly one ($samplerStdoutPath)."
}
$samplerRows = @($samplerLines | Where-Object { $_ -match '^UI BENCH name=' })
if ($samplerRows.Count -ne 1 -or
    $samplerRows[0] -notmatch '^UI BENCH name=sampler-accepted-prefix width=64 height=48 runs=2 warmup=1\b') {
    throw "UI sampler smoke must print exactly one sampler-accepted-prefix row at 64x48 with two runs and one warmup ($samplerStdoutPath)."
}

$samplerReadback = @{}
foreach ($field in 'readbackCopies', 'readbackMaps', 'readinessBarriers', 'busyFailures') {
    if ($samplerRows[0] -notmatch "\b$field=(?<count>\d+)\b") {
        throw "UI sampler smoke row is missing readback counter '$field': $($samplerRows[0])"
    }
    $samplerReadback[$field] = [long]$Matches['count']
}
if ($samplerReadback.readbackCopies -ne 6 -or
    $samplerReadback.readbackMaps -ne ($samplerReadback.readbackCopies + $samplerReadback.readinessBarriers) -or
    $samplerReadback.readinessBarriers -gt $samplerReadback.readbackCopies -or $samplerReadback.busyFailures -ne 0) {
    throw "UI sampler smoke must read three accepted points per trial (six copies), allow bounded readiness barriers, and avoid busy failures: $($samplerRows[0])"
}

$samplerBoundaryRows = @($samplerLines | Where-Object { $_ -match '^UI BENCH BOUNDARY\b' })
if ($samplerBoundaryRows.Count -ne 2) {
    throw "UI sampler smoke printed $($samplerBoundaryRows.Count) boundary rows, expected exactly two ($samplerStdoutPath)."
}
$samplerBoundaries = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::Ordinal)
foreach ($row in $samplerBoundaryRows) {
    if ($row -notmatch '^UI BENCH BOUNDARY name=sampler-accepted-prefix boundary=(?<boundary>initial-display|excluded-warmup) width=64 height=48\b') {
        throw "Malformed UI sampler smoke boundary row: $row"
    }
    $boundary = $Matches['boundary']
    if (-not $samplerBoundaries.Add($boundary)) {
        throw "Duplicate UI sampler smoke boundary '$boundary': $row"
    }
    if ($row -notmatch '\bframes=(?<frames>\d+)\b' -or [long]$Matches['frames'] -lt 1 -or
        $row -notmatch '\bclockFailures=(?<failures>\d+)\b' -or [long]$Matches['failures'] -ne 0) {
        throw "UI sampler smoke boundary must observe at least one frame with zero clock failures: $row"
    }
}
if (-not $samplerBoundaries.Contains('initial-display') -or -not $samplerBoundaries.Contains('excluded-warmup')) {
    throw "UI sampler smoke must report initial-display and excluded-warmup boundaries exactly once."
}

$samplerReports = @($samplerLines | Where-Object { $_ -match '^UI BENCH REPORT\b' })
if ($samplerReports.Count -ne 1 -or
    $samplerReports[0] -notmatch '^UI BENCH REPORT status=ok width=64 height=48 operations=1(?:\s+.*)?$') {
    throw "UI sampler smoke did not print exactly one successful one-operation report ($samplerStdoutPath)."
}

Write-Host 'PASS UI sampler smoke: sampler-accepted-prefix only, 64x48, 2 samples after one warmup'
$global:LASTEXITCODE = 0
