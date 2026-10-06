<#
.SYNOPSIS
Runs the bounded prepared layer-row benchmark smoke mode and validates correctness output.
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

# Keep each run's evidence, including prior runs, in the requested output directory.
$runId = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$stdoutPath = Join-Path $OutputDirectory "RedPrism.layer-rows-smoke.$runId.log"
$stderrPath = Join-Path $OutputDirectory "RedPrism.layer-rows-smoke.$runId.err.log"
$process = Start-Process -WindowStyle Hidden -FilePath $ExecutablePath `
    -ArgumentList @('--warp', '--bench', 'layer-rows-smoke') -PassThru `
    -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath
$null = $process.Handle # Retain exit-observation authority through the bounded wait on Windows PowerShell.
$timeoutSeconds = 120
if (-not $process.WaitForExit($timeoutSeconds * 1000)) {
    throw "RedPrism.exe --warp --bench layer-rows-smoke did not finish within $timeoutSeconds s (PID $($process.Id) is still running; report it, do not kill it). Logs: $stdoutPath ; $stderrPath"
}
$process.WaitForExit()

$stderr = if (Test-Path -LiteralPath $stderrPath) { Get-Content -LiteralPath $stderrPath -Raw } else { '' }
if ($process.ExitCode -ne 0) {
    throw "RedPrism.exe --warp --bench layer-rows-smoke exited with code $($process.ExitCode). Logs: $stdoutPath ; $stderrPath. $stderr"
}
if (-not [string]::IsNullOrEmpty($stderr)) {
    throw "Layer-row benchmark smoke wrote to stderr. See $stderrPath"
}

$lines = @(Get-Content -LiteralPath $stdoutPath)
if (($lines -join "`n") -match '(?i)\bFAIL\b|status=failed') {
    throw "Layer-row benchmark smoke output contains FAIL or status=failed. See $stdoutPath"
}

$machineRows = @($lines | Where-Object { $_ -match '^LAYER ROWS MACHINE\b' })
if ($machineRows.Count -ne 1 -or
    $machineRows[0] -notmatch '\bdevice=WARP\b' -or
    $machineRows[0] -notmatch '\bsmoke=1\b' -or
    $machineRows[0] -notmatch '\bframeGate=unmeasured\b') {
    throw "Expected exactly one WARP smoke machine row with frameGate=unmeasured. See $stdoutPath"
}

$expectedTrials = @{
    0 = 'initial'
    1 = 'rename'
    2 = 'rename'
}
$benchRows = @($lines | Where-Object { $_ -match '^LAYER ROWS BENCH trial=' })
if ($benchRows.Count -ne 3) {
    throw "Layer-row smoke printed $($benchRows.Count) BENCH rows, expected exactly three. See $stdoutPath"
}
$seenTrials = [System.Collections.Generic.HashSet[int]]::new()
foreach ($row in $benchRows) {
    if ($row -notmatch '^LAYER ROWS BENCH trial=(?<trial>\d+) boundary=(?<boundary>\w+) layers=(?<layers>\d+) nameUnits=(?<nameUnits>\d+) elapsedWithWorkerWaitUs=(?<elapsed>\d+) reservedBytes=(?<reserved>\d+) namesRead=(?<names>\d+) totalNameUnits=(?<totalNameUnits>\d+) clockFailures=(?<clockFailures>\d+)$') {
        throw "Malformed layer-row BENCH row: $row"
    }
    $trial = [int]::Parse($Matches['trial'], [System.Globalization.CultureInfo]::InvariantCulture)
    if (-not $expectedTrials.ContainsKey($trial) -or -not $seenTrials.Add($trial)) {
        throw "Unexpected or duplicate layer-row BENCH trial: $row"
    }
    if ($Matches['boundary'] -cne $expectedTrials[$trial] -or
        [int]::Parse($Matches['layers'], [System.Globalization.CultureInfo]::InvariantCulture) -ne 128 -or
        [int]::Parse($Matches['nameUnits'], [System.Globalization.CultureInfo]::InvariantCulture) -ne 1024 -or
        [long]::Parse($Matches['elapsed'], [System.Globalization.CultureInfo]::InvariantCulture) -lt 0 -or
        [long]::Parse($Matches['reserved'], [System.Globalization.CultureInfo]::InvariantCulture) -le 0 -or
        [long]::Parse($Matches['names'], [System.Globalization.CultureInfo]::InvariantCulture) -ne 128 -or
        [long]::Parse($Matches['totalNameUnits'], [System.Globalization.CultureInfo]::InvariantCulture) -ne 131072 -or
        [long]::Parse($Matches['clockFailures'], [System.Globalization.CultureInfo]::InvariantCulture) -ne 0) {
        throw "Layer-row BENCH row violates smoke fixture expectations: $row"
    }
}
if ($seenTrials.Count -ne 3) {
    throw "Layer-row smoke did not report trials 0, 1, and 2. See $stdoutPath"
}

$expectedKinds = @(
    'request', 'prepare', 'uia-navigation-server', 'uia-name-server', 'dispatch',
    'window-message', 'completion-drain', 'result-apply', 'chrome-sync'
)
$costRows = @($lines | Where-Object { $_ -match '^LAYER ROWS COST trial=' })
if ($costRows.Count -ne 27) {
    throw "Layer-row smoke printed $($costRows.Count) COST rows, expected nine per trial (27 total). See $stdoutPath"
}
foreach ($trial in 0, 1, 2) {
    $trialRows = @($costRows | Where-Object { $_ -match "^LAYER ROWS COST trial=$trial\b" })
    if ($trialRows.Count -ne 9) {
        throw "Layer-row smoke trial $trial printed $($trialRows.Count) COST rows, expected nine. See $stdoutPath"
    }
    $seenKinds = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::Ordinal)
    foreach ($row in $trialRows) {
        if ($row -notmatch '^LAYER ROWS COST trial=(?<trial>\d+) kind=(?<kind>[a-z-]+) samples=(?<samples>\d+) totalUs=(?<totalUs>\d+) p99UpperUs=(?<p99UpperUs>\d+) maxUs=(?<maxUs>\d+) totalOverflow=(?<overflow>[01])$') {
            throw "Malformed layer-row COST row: $row"
        }
        $kind = $Matches['kind']
        if ($kind -notin $expectedKinds -or -not $seenKinds.Add($kind)) {
            throw "Unexpected or duplicate COST kind in trial ${trial}: $row"
        }
        $samples = [long]::Parse($Matches['samples'], [System.Globalization.CultureInfo]::InvariantCulture)
        foreach ($field in 'totalUs', 'p99UpperUs', 'maxUs') {
            if ([long]::Parse($Matches[$field], [System.Globalization.CultureInfo]::InvariantCulture) -lt 0) {
                throw "Negative COST numeric field '$($field)': $row"
            }
        }
        if ($Matches['overflow'] -ne '0') {
            throw "COST row reports total overflow: $row"
        }
        if (($kind -in @('uia-navigation-server', 'uia-name-server') -and $samples -ne 128) -or
            ($kind -in @('request', 'prepare') -and $samples -ne 1)) {
            throw "COST sample count is incorrect for '$kind': $row"
        }
    }
    foreach ($kind in $expectedKinds) {
        if (-not $seenKinds.Contains($kind)) {
            throw "Layer-row smoke trial $trial omitted COST kind '$kind'."
        }
    }
}

$reports = @($lines | Where-Object { $_ -match '^LAYER ROWS BENCH REPORT\b' })
if ($reports.Count -ne 1 -or
    $reports[0] -notmatch '^LAYER ROWS BENCH REPORT status=ok layers=128 renames=2 smoke=1 performanceGate=unmeasured$') {
    throw "Layer-row smoke did not print exactly one valid final report. See $stdoutPath"
}

Write-Host "PASS layer-row benchmark smoke: correctness output validated; logs: $stdoutPath ; $stderrPath"
$global:LASTEXITCODE = 0
