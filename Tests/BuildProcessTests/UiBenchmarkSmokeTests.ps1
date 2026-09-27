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
    'invert', 'fill', 'clear', 'undo'
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
}
foreach ($name in $expectedNames) {
    if (-not $seenNames.Contains($name)) {
        throw "UI benchmark smoke did not report expected operation '$name'."
    }
}

$reports = @($lines | Where-Object { $_ -match '^UI BENCH REPORT\b' })
if ($reports.Count -ne 1 -or
    $reports[0] -notmatch '^UI BENCH REPORT status=ok width=64 height=48 operations=15(?:\s+.*)?$') {
    throw "UI benchmark smoke did not print exactly one valid completion report ($stdoutPath)."
}

Write-Host 'PASS UI benchmark smoke: 15 operations, 64x48, 2 samples after one warmup'
$global:LASTEXITCODE = 0
