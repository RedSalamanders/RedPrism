<#
.SYNOPSIS
Runs the real UI benchmark loop on its bounded 64x48 smoke fixture and validates its completion report.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $ExecutablePath,
    [Parameter(Mandatory)] [string] $OutputDirectory,
    [ValidateSet('direct', 'prepared', 'size-prepared')] [string] $DisplayRoute = 'direct'
)

Set-StrictMode -Version Latest

# Finite bucket limits from Common/UiTimingHistogram.h. The open bucket reports the exact observed maximum.
$histogramBounds = @(0L, 1L, 2L, 4L, 8L, 16L, 32L, 64L, 125L, 250L, 500L, 1000L,
    2000L, 4000L, 8000L, 16000L, 33000L, 50000L, 100000L, 250000L, 1000000L)

function Assert-PercentileFitsMaximumBucket([double] $percentileUpper, [long] $maximum, [string] $label, [string] $row) {
    $bucketUpper = $maximum
    foreach ($bound in $histogramBounds) {
        if ($maximum -le $bound) { $bucketUpper = $bound; break }
    }
    $validBoundary = $percentileUpper -in $histogramBounds -or ($maximum -gt 1000000 -and $percentileUpper -eq $maximum)
    if (-not $validBoundary -or $percentileUpper -gt $bucketUpper) {
        throw "$label is not a valid percentile upper bound for the maximum sample's histogram bucket ($bucketUpper): $row"
    }
}
$ErrorActionPreference = 'Stop'


function Assert-UiBenchmarkDisplayRoute([string[]] $lines, [string] $route) {
    if ($route -cnotin 'direct', 'prepared', 'size-prepared') { throw 'Unknown benchmark display route.' }
    $rows = @($lines | Where-Object { $_ -cmatch '^UI BENCH ROUTE\b' })
    $prepared = $route -cne 'direct'
    $async = [int]$prepared
    $finalPublication = if ($route -ceq 'size-prepared') { ' finalPublication=size-prepared-benchmark' } else { '' }
    $routeName = if ($route -ceq 'size-prepared') { 'prepared' } else { $route }
    $expected = "UI BENCH ROUTE display=$routeName rows=$routeName asyncStroke=$async asyncWarp=$async$finalPublication ordinaryActivation=0"
    if ($rows.Count -ne 1 -or $rows[0] -cne $expected) {
        throw "Benchmark must report its exact selected route once: $expected"
    }
}

if (-not (Test-Path -LiteralPath $ExecutablePath -PathType Leaf)) {
    throw "RedPrism.exe was not found: $ExecutablePath"
}
if (-not (Test-Path -LiteralPath $OutputDirectory -PathType Container)) {
    throw "Benchmark smoke output directory was not found: $OutputDirectory"
}

$benchMode = switch ($DisplayRoute) {
    'prepared' { 'ui-prepared-smoke' }
    'size-prepared' { 'ui-size-prepared-smoke' }
    default { 'ui-smoke' }
}
$stdoutPath = Join-Path $OutputDirectory ('RedPrism.' + $benchMode + '.log')
$stderrPath = Join-Path $OutputDirectory ('RedPrism.' + $benchMode + '.err.log')
Remove-Item -LiteralPath $stdoutPath, $stderrPath -ErrorAction SilentlyContinue
$process = Start-Process -WindowStyle Hidden -FilePath $ExecutablePath `
    -ArgumentList @('--warp', '--bench', $benchMode, '--isa', 'scalar') -PassThru `
    -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath
$null = $process.Handle # Retain exit-observation authority through the bounded wait on Windows PowerShell.
$timeoutSeconds = 120
if (-not $process.WaitForExit($timeoutSeconds * 1000)) {
    throw "RedPrism.exe --warp --bench $benchMode --isa scalar did not finish within $timeoutSeconds s (PID $($process.Id) is still running; report it, do not kill it)."
}
$process.WaitForExit()

$stderr = if (Test-Path -LiteralPath $stderrPath) { Get-Content -LiteralPath $stderrPath -Raw } else { '' }
if ($process.ExitCode -ne 0) {
    throw "RedPrism.exe --bench $benchMode exited with code $($process.ExitCode). $stderr"
}

$lines = @(Get-Content -LiteralPath $stdoutPath)
Assert-UiBenchmarkDisplayRoute $lines $DisplayRoute
$machineRows = @($lines | Where-Object { $_ -match '^BENCH MACHINE\b' })
if ($machineRows.Count -ne 1) {
    throw "UI benchmark smoke printed $($machineRows.Count) BENCH MACHINE lines, expected exactly one ($stdoutPath)."
}

$expectedNames = if ($DisplayRoute -ceq 'size-prepared') {
    @('canvas-size-fill', 'image-size-nearest', 'image-size-resolution')
} else { @(
    'zoom-actual', 'zoom-fit', 'levels-open', 'levels-update', 'levels-commit', 'hsv-update',
    'exposure-update', 'gaussian-update', 'noise-update', 'grain-update', 'lens-update',
    'invert', 'fill', 'clear', 'undo', 'canvas-size-fill', 'image-size-nearest', 'image-size-resolution', 'project-save', 'project-open',
    'png-export', 'image-import-new-canvas', 'image-import-existing-layer', 'jpeg-export',
    'gradient-preview-coalesced', 'gradient-apply-ordered', 'sampler-accepted-prefix'
) }
$expectedOperationCount = $expectedNames.Count
$operationRows = @($lines | Where-Object { $_ -match '^UI BENCH name=' })
if ($operationRows.Count -ne $expectedNames.Count) {
    throw "UI benchmark smoke printed $($operationRows.Count) operation rows, expected $($expectedNames.Count) ($stdoutPath)."
}

$seenNames = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::Ordinal)
for ($rowIndex = 0; $rowIndex -lt $operationRows.Count; $rowIndex++) {
    $row = $operationRows[$rowIndex]
    if ($row -notmatch '^UI BENCH name=(?<name>[a-z0-9-]+) width=64 height=48 runs=2 warmup=1\b') {
        throw "Malformed UI benchmark smoke operation row (expected dimensions and run counts): $row"
    }
    $name = $Matches['name']
    if ($DisplayRoute -ceq 'size-prepared' -and $name -cne $expectedNames[$rowIndex]) {
        throw "Prepared Size smoke operation order differs at row $rowIndex; expected '$($expectedNames[$rowIndex])': $row"
    }
    if ($row -notmatch '\bframes=(?<frames>\d+)\b') {
        throw "UI benchmark smoke operation row is missing its frame count: $row"
    }
    $frames = [long]$Matches['frames']
    $durations = @{}
    foreach ($field in 'elapsedMedianUs', 'elapsedP95Us', 'elapsedMaxUs', 'dispatchP99UpperUs', 'dispatchMaxUs',
        'messageP99UpperUs', 'messageMaxUs', 'drainP99UpperUs', 'drainMaxUs', 'applyMaxUs', 'actionMaxUs',
        'frameP99UpperUs', 'frameMaxUs', 'actionP99UpperUs', 'invalidationToPresentSamples',
        'invalidationToPresentP99UpperUs', 'invalidationToPresentMaxUs', 'clockFailures', 'abandonedFrameIntervals') {
        if ($row -notmatch "\b$field=\d+(?:\.\d+)?\b") {
            throw "UI benchmark smoke operation row is missing timing field '$field': $row"
        }
        $durations[$field] = [double]::Parse(($Matches[0] -split '=')[1], [System.Globalization.CultureInfo]::InvariantCulture)
    }
    if ($durations.elapsedMedianUs -gt $durations.elapsedP95Us -or $durations.elapsedP95Us -gt $durations.elapsedMaxUs) {
        throw "UI benchmark smoke operation '$name' has inconsistent elapsed distribution: $row"
    }
    if ($durations.invalidationToPresentSamples -lt 2 -or $durations.clockFailures -ne 0 -or
        $durations.abandonedFrameIntervals -ne 0) {
        throw "Successful UI benchmark operation '$name' needs presentation samples and zero clock failures/abandoned intervals: $row"
    }
    foreach ($pair in @(@('dispatchP99UpperUs', 'dispatchMaxUs'), @('messageP99UpperUs', 'messageMaxUs'),
        @('drainP99UpperUs', 'drainMaxUs'), @('actionP99UpperUs', 'actionMaxUs'), @('frameP99UpperUs', 'frameMaxUs'),
        @('invalidationToPresentP99UpperUs', 'invalidationToPresentMaxUs'))) {
        Assert-PercentileFitsMaximumBucket $durations[$pair[0]] ([long]$durations[$pair[1]]) "$name $($pair[0])" $row
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

if ($DisplayRoute -ceq 'size-prepared') {
    $publications = @($lines | Where-Object { $_ -cmatch '^UI SIZE PUBLICATION\b' })
    if ($publications.Count -ne 3) { throw 'Prepared Size smoke requires three actual publication receipts.' }
    for ($index = 0; $index -lt $expectedNames.Count; $index++) {
        $expected = "UI SIZE PUBLICATION name=$($expectedNames[$index]) recorded=2 expected=2 warmupExcluded=1"
        if ($publications[$index] -cne $expected) {
            throw "Prepared Size smoke did not observe its two real final publications: $expected"
        }
    }

    $boundaryNames = @('initial-display', 'excluded-warmup')
    $expectedBoundaryPairs = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::Ordinal)
    foreach ($name in $expectedNames) {
        foreach ($boundary in $boundaryNames) { [void]$expectedBoundaryPairs.Add("$name|$boundary") }
    }
    $boundaryRows = @($lines | Where-Object { $_ -cmatch '^UI BENCH BOUNDARY\b' })
    if ($boundaryRows.Count -ne 6) {
        throw "Prepared Size smoke printed $($boundaryRows.Count) boundary rows, expected six ($stdoutPath)."
    }
    $seenBoundaryPairs = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::Ordinal)
    foreach ($row in $boundaryRows) {
        if ($row -notmatch '^UI BENCH BOUNDARY name=(?<name>[a-z0-9-]+) boundary=(?<boundary>initial-display|excluded-warmup) width=64 height=48\b') {
            throw "Malformed prepared Size smoke boundary row: $row"
        }
        $name = $Matches['name']
        $boundary = $Matches['boundary']
        $pair = "$name|$boundary"
        if (-not $expectedBoundaryPairs.Contains($pair) -or -not $seenBoundaryPairs.Add($pair)) {
            throw "Unexpected or duplicate prepared Size boundary pair '$pair': $row"
        }
        $boundaryValues = @{}
        foreach ($field in 'dispatchP99UpperUs', 'dispatchMaxUs', 'messageP99UpperUs', 'messageMaxUs',
            'drainP99UpperUs', 'drainMaxUs', 'applyMaxUs', 'actionP99UpperUs', 'actionMaxUs',
            'frameP99UpperUs', 'frameMaxUs', 'frames', 'invalidationToPresentSamples',
            'invalidationToPresentP99UpperUs', 'invalidationToPresentMaxUs', 'clockFailures', 'abandonedFrameIntervals') {
            if ($row -notmatch "\b$field=(?<value>\d+)\b") { throw "Prepared Size boundary is missing '$field': $row" }
            $boundaryValues[$field] = [long]$Matches['value']
        }
        if ($boundaryValues.frames -lt 1 -or $boundaryValues.invalidationToPresentSamples -lt 1 -or
            $boundaryValues.clockFailures -ne 0 -or $boundaryValues.abandonedFrameIntervals -ne 0) {
            throw "Prepared Size boundary needs a frame and completed presentation with no clock failures or abandoned intervals: $row"
        }
        foreach ($pairFields in @(@('dispatchP99UpperUs', 'dispatchMaxUs'), @('messageP99UpperUs', 'messageMaxUs'),
            @('drainP99UpperUs', 'drainMaxUs'), @('actionP99UpperUs', 'actionMaxUs'), @('frameP99UpperUs', 'frameMaxUs'),
            @('invalidationToPresentP99UpperUs', 'invalidationToPresentMaxUs'))) {
            Assert-PercentileFitsMaximumBucket $boundaryValues[$pairFields[0]] $boundaryValues[$pairFields[1]] `
                "prepared Size $name $boundary $($pairFields[0])" $row
        }
    }
    if ($seenBoundaryPairs.Count -ne $expectedBoundaryPairs.Count) {
        throw 'Prepared Size smoke must report initial-display and excluded-warmup exactly once for each operation.'
    }

    $resourceStages = @('texture-create', 'shader-view-create', 'mip-generation', 'raster-upload',
        'composition-record', 'chrome-sync')
    $expectedResourceRows = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::Ordinal)
    foreach ($name in $expectedNames) {
        foreach ($boundary in $boundaryNames) {
            foreach ($stage in $resourceStages) { [void]$expectedResourceRows.Add("$name|$boundary|$stage") }
        }
    }
    $resourceRows = @($lines | Where-Object { $_ -cmatch '^UI RESOURCE\b.*\bboundary=(initial-display|excluded-warmup)\b' })
    if ($resourceRows.Count -ne 36) {
        throw "Prepared Size smoke printed $($resourceRows.Count) boundary resource rows, expected 36 ($stdoutPath)."
    }
    $seenResourceRows = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::Ordinal)
    foreach ($row in $resourceRows) {
        if ($row -notmatch '^UI RESOURCE name=(?<name>[a-z0-9-]+) boundary=(?<boundary>initial-display|excluded-warmup) stage=(?<stage>[a-z-]+) samples=(?<samples>\d+) p99UpperUs=(?<p99>\d+) maxUs=(?<maximum>\d+) inclusive=1$') {
            throw "Malformed prepared Size boundary resource row: $row"
        }
        $resourceKey = "$($Matches['name'])|$($Matches['boundary'])|$($Matches['stage'])"
        if (-not $expectedResourceRows.Contains($resourceKey) -or -not $seenResourceRows.Add($resourceKey)) {
            throw "Unexpected or duplicate prepared Size boundary resource '$resourceKey': $row"
        }
        Assert-PercentileFitsMaximumBucket ([long]$Matches['p99']) ([long]$Matches['maximum']) `
            "prepared Size resource $resourceKey" $row
    }
    if ($seenResourceRows.Count -ne $expectedResourceRows.Count) {
        throw 'Prepared Size smoke must report all six resource stages for every operation/boundary pair.'
    }
}

$reports = @($lines | Where-Object { $_ -match '^UI BENCH REPORT\b' })
if ($reports.Count -ne 1 -or
    $reports[0] -notmatch "^UI BENCH REPORT status=ok width=64 height=48 operations=$expectedOperationCount(?:\s+.*)?$") {
    throw "UI benchmark smoke did not print exactly one valid completion report ($stdoutPath)."
}

Write-Host "PASS UI benchmark smoke: route=$DisplayRoute, $expectedOperationCount operations, 64x48, 2 samples after one warmup"
$global:LASTEXITCODE = 0

# The prepared full roster already checks its sampler row. The legacy focused sampler keeps its separate boundaries.
if ($DisplayRoute -in @('prepared', 'size-prepared')) { return }

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

$samplerTiming = @{}
foreach ($field in 'actionP99UpperUs', 'actionMaxUs', 'frameP99UpperUs', 'frameMaxUs',
    'invalidationToPresentSamples', 'invalidationToPresentP99UpperUs', 'invalidationToPresentMaxUs',
    'clockFailures', 'abandonedFrameIntervals') {
    if ($samplerRows[0] -notmatch "\b$field=(?<value>\d+)\b") {
        throw "UI sampler smoke row is missing timing field '$field': $($samplerRows[0])"
    }
    $samplerTiming[$field] = [long]$Matches['value']
}
if ($samplerTiming.invalidationToPresentSamples -lt 2 -or $samplerTiming.clockFailures -ne 0 -or
    $samplerTiming.abandonedFrameIntervals -ne 0) {
    throw "Successful UI sampler row needs presentation samples and zero clock failures/abandoned intervals: $($samplerRows[0])"
}
Assert-PercentileFitsMaximumBucket $samplerTiming.actionP99UpperUs $samplerTiming.actionMaxUs 'sampler operation action percentile' $samplerRows[0]
Assert-PercentileFitsMaximumBucket $samplerTiming.frameP99UpperUs $samplerTiming.frameMaxUs 'sampler operation frame percentile' $samplerRows[0]
Assert-PercentileFitsMaximumBucket $samplerTiming.invalidationToPresentP99UpperUs `
    $samplerTiming.invalidationToPresentMaxUs 'sampler operation presentation percentile' $samplerRows[0]

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
    if ($row -notmatch '\bframes=(?<frames>\d+)\b' -or [long]$Matches['frames'] -lt 1) {
        throw "UI sampler smoke boundary must observe at least one frame: $row"
    }
    $boundaryValues = @{}
    foreach ($field in 'actionP99UpperUs', 'actionMaxUs', 'frameP99UpperUs', 'frameMaxUs', 'invalidationToPresentSamples',
        'invalidationToPresentP99UpperUs', 'invalidationToPresentMaxUs', 'clockFailures', 'abandonedFrameIntervals') {
        if ($row -notmatch "\b$field=(?<value>\d+)\b") { throw "Sampler boundary is missing '$field': $row" }
        $boundaryValues[$field] = [long]$Matches['value']
    }
    if ($boundaryValues.invalidationToPresentSamples -lt 1 -or $boundaryValues.clockFailures -ne 0 -or
        $boundaryValues.abandonedFrameIntervals -ne 0) {
        throw "Sampler boundary needs presentation samples and zero clock failures/abandoned intervals: $row"
    }
    Assert-PercentileFitsMaximumBucket $boundaryValues.actionP99UpperUs $boundaryValues.actionMaxUs 'sampler action percentile' $row
    Assert-PercentileFitsMaximumBucket $boundaryValues.frameP99UpperUs $boundaryValues.frameMaxUs 'sampler frame percentile' $row
    Assert-PercentileFitsMaximumBucket $boundaryValues.invalidationToPresentP99UpperUs `
        $boundaryValues.invalidationToPresentMaxUs 'sampler presentation percentile' $row
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
