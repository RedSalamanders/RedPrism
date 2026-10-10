<#
.SYNOPSIS
Runs and parses the opt-in actual UI cancellation smoke benchmark.
.DESCRIPTION
Dot-source this file to use the pure parser without launching RedPrism.exe.
#>
[CmdletBinding()]
param(
    [string] $ExecutablePath,
    [string] $OutputDirectory,
    [ValidateSet('direct', 'prepared')] [string] $DisplayRoute = 'direct'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$script:UiCancelNames = @('levels', 'hsv', 'exposure', 'curves', 'gradient-map', 'grain', 'gaussian250', 'motion2000', 'noise', 'lens')
$script:UiCancelHistogramBounds = @(0L, 1L, 2L, 4L, 8L, 16L, 32L, 64L, 125L, 250L, 500L, 1000L, 2000L, 4000L, 8000L, 16000L, 33000L, 50000L, 100000L, 250000L, 1000000L)
$script:UiCancelFieldTypes = @{
    name='string'; trial='uint'; warmup='uint'; width='uint'; height='uint'; outcome='string'; measured='uint'; previewEnabled='uint'
    requestedDelayUs='uint64'; entryToPostedUs='uint64'; postedToDispatchUs='uint64'; entryToDispatchUs='uint64'; delayKnown='uint'
    ticketSlot='uint'; ticketGeneration='uint64'; usefulOrdinal='uint'; dispatchUs='uint64'; admissionUs='uint64'
    stopToReturnUs='uint64'; escapeToSourcePresentUs='uint64'; settleUs='uint64'; rawKnown='uint'; rawStatus='uint'
    rawError='hex32'; applyKnown='uint'; applyOutcome='uint'; applyError='hex32'; cancelKnown='uint'; cancelOutcome='uint'
    cancelError='hex32'; sourcePresent='uint'; hiddenAtAcceptance='uint'; diagnosticBytes='uint'; ownerBatch='uint'
    actionP99UpperUs='uint64'; actionMaxUs='uint64'; applyMaxUs='uint64'; ordinaryActivation='uint'; g1='uint'; deadlineRearms='uint'
}

function Convert-UiCancelField([string] $Key, [string] $Value, [string] $Row) {
    if (-not $script:UiCancelFieldTypes.ContainsKey($Key)) { throw "Unknown UI CANCEL field '$Key': $Row" }
    $style = [System.Globalization.NumberStyles]::None
    $culture = [System.Globalization.CultureInfo]::InvariantCulture
    switch ($script:UiCancelFieldTypes[$Key]) {
        'string' { if ([string]::IsNullOrWhiteSpace($Value)) { throw "Empty UI CANCEL field '$Key': $Row" }; return $Value }
        'uint' {
            [uint32]$number = 0
            if (-not [uint32]::TryParse($Value, $style, $culture, [ref]$number)) { throw "Invalid or overflowing UI CANCEL field '$Key': $Row" }
            return $number
        }
        'uint64' {
            [uint64]$number = 0
            if (-not [uint64]::TryParse($Value, $style, $culture, [ref]$number)) { throw "Invalid or overflowing UI CANCEL field '$Key': $Row" }
            return $number
        }
        'hex32' {
            if ($Value -cnotmatch '^0x[0-9A-Fa-f]{8}$') { throw "Invalid 32-bit hexadecimal UI CANCEL field '$Key': $Row" }
            [uint32]$number = 0
            if (-not [uint32]::TryParse($Value.Substring(2), [System.Globalization.NumberStyles]::AllowHexSpecifier, $culture, [ref]$number)) { throw "Invalid hexadecimal UI CANCEL field '$Key': $Row" }
            return $number
        }
    }
    throw "Unsupported parser type for '$Key'."
}

function Convert-UiCancelRow([string] $Row) {
    if (-not $Row.StartsWith('UI CANCEL ', [System.StringComparison]::Ordinal)) { throw "Malformed UI CANCEL row: $Row" }
    $fields = [System.Collections.Generic.Dictionary[string,object]]::new([System.StringComparer]::Ordinal)
    $tokens = $Row.Substring('UI CANCEL '.Length).Split([char[]]@(' '), [System.StringSplitOptions]::RemoveEmptyEntries)
    foreach ($token in $tokens) {
        if ($token -cnotmatch '^(?<key>[A-Za-z][A-Za-z0-9]*)=(?<value>\S+)$') { throw "Malformed field token '$token': $Row" }
        $key = $Matches.key
        if ($fields.ContainsKey($key)) { throw "Duplicate UI CANCEL field '$key': $Row" }
        $fields.Add($key, (Convert-UiCancelField $key $Matches.value $Row))
    }
    if ($fields.Count -ne $script:UiCancelFieldTypes.Count) { throw "UI CANCEL row has missing or extra fields: $Row" }
    foreach ($key in $script:UiCancelFieldTypes.Keys) { if (-not $fields.ContainsKey($key)) { throw "Missing UI CANCEL field '$key': $Row" } }
    foreach ($key in 'warmup','measured','previewEnabled','delayKnown','rawKnown','applyKnown','cancelKnown','sourcePresent','hiddenAtAcceptance','ordinaryActivation','g1') {
        if ($fields[$key] -notin 0,1) { throw "UI CANCEL boolean field '$key' must be 0 or 1: $Row" }
    }
    foreach ($key in 'requestedDelayUs','entryToPostedUs','postedToDispatchUs','entryToDispatchUs','dispatchUs','admissionUs','stopToReturnUs','escapeToSourcePresentUs','settleUs','actionP99UpperUs','actionMaxUs','applyMaxUs') {
        if ($fields[$key] -lt 0) { throw "Negative duration in '$key': $Row" }
    }
    if ($fields.width -ne 64 -or $fields.height -ne 48 -or $fields.requestedDelayUs -ne 20000 -or $fields.usefulOrdinal -ne 2 -or
        $fields.ticketSlot -ge 128 -or $fields.ticketGeneration -eq 0 -or $fields.diagnosticBytes -eq 0 -or $fields.ownerBatch -ne 32 -or
        $fields.ordinaryActivation -ne 0 -or $fields.g1 -ne 0 -or $fields.delayKnown -ne 1 -or $fields.deadlineRearms -gt 2) {
        throw "UI CANCEL row violates fixed smoke dimensions or admission diagnostics: $Row"
    }
    if ($fields.actionP99UpperUs -notin $script:UiCancelHistogramBounds -and
        -not ($fields.actionMaxUs -gt 1000000 -and $fields.actionP99UpperUs -eq $fields.actionMaxUs)) {
        throw "actionP99UpperUs is not a valid histogram boundary: $Row"
    }
    $bucketUpper = $fields.actionMaxUs
    foreach ($bound in $script:UiCancelHistogramBounds) { if ($fields.actionMaxUs -le $bound) { $bucketUpper = $bound; break } }
    if ($fields.actionP99UpperUs -gt $bucketUpper) { throw "actionP99UpperUs exceeds the maximum sample bucket: $Row" }

    if ($fields.outcome -cnotin @('returned-after-stop','finish-overlap','finished-before-stop','finished-before-escape')) {
        throw "Disallowed UI cancellation smoke outcome '$($fields.outcome)': $Row"
    }
    if ($fields.outcome -ceq 'returned-after-stop') {
        $expectedMeasured = if ($fields.trial -eq 0) { 0 } else { 1 }
        if ($fields.measured -ne $expectedMeasured) { throw "returned-after-stop measured flag does not match warmup/trial: $Row" }
    } elseif ($fields.measured -ne 0) { throw "Only returned-after-stop recorded trials may be measured: $Row" }

    if ($fields.outcome -ceq 'finished-before-escape') {
        if ($fields.rawKnown -ne 1 -or $fields.rawStatus -ne 0 -or $fields.rawError -ne 0 -or
            $fields.applyKnown -ne 1 -or $fields.applyOutcome -ne 1 -or $fields.applyError -ne 0 -or
            $fields.cancelKnown -ne 0 -or $fields.sourcePresent -ne 0 -or $fields.hiddenAtAcceptance -ne 0 -or $fields.measured -ne 0) {
            throw "finished-before-escape receipt contract failed: $Row"
        }
    } else {
        if ($fields.rawKnown -ne 1 -or $fields.rawStatus -notin 0,1 -or $fields.rawError -ne 0 -or
            $fields.applyKnown -ne 1 -or $fields.applyOutcome -ne 2 -or $fields.applyError -ne [uint32]2147500036 -or
            $fields.cancelKnown -ne 1 -or $fields.cancelOutcome -ne 1 -or $fields.cancelError -ne 0 -or
            $fields.sourcePresent -ne 1 -or $fields.hiddenAtAcceptance -ne 1) {
            throw "Cancellation admission receipt contract failed: $Row"
        }
    }
    [pscustomobject]@{ Raw = $Row; Fields = $fields; RawStatus = $fields.rawStatus; RawError = $fields.rawError; ApplyOutcome = $fields.applyOutcome; ApplyError = $fields.applyError; CancelOutcome = $fields.cancelOutcome; CancelError = $fields.cancelError }
}

function Parse-UiCancellationSmoke([string[]] $Lines, [ValidateSet('direct','prepared')] [string] $Route) {
    if ($Route -cnotin @('direct','prepared')) { throw "Unknown display route '$Route'." }
    $machineRows = @($Lines | Where-Object { $_ -cmatch '^BENCH MACHINE\b' })
    if ($machineRows.Count -ne 1) { throw "Expected exactly one BENCH MACHINE row; found $($machineRows.Count)." }
    $expectedRoute = "UI BENCH ROUTE display=$Route rows=$Route asyncStroke=0 asyncWarp=0 ordinaryActivation=0"
    $routeRows = @($Lines | Where-Object { $_ -cmatch '^UI BENCH ROUTE\b' })
    if ($routeRows.Count -ne 1 -or $routeRows[0] -cne $expectedRoute) { throw "Expected selected UI BENCH ROUTE exactly once: $expectedRoute" }
    $namespace = @($Lines | Where-Object { $_ -cmatch '^UI CANCEL\b' })
    $rows = @($namespace | Where-Object { $_ -cnotmatch '^UI CANCEL REPORT\b' })
    if ($rows.Count -ne 30) { throw "Expected exactly 30 UI CANCEL rows; found $($rows.Count)." }
    $parsed = [System.Collections.Generic.List[object]]::new()
    $cursor = 0
    foreach ($name in $script:UiCancelNames) {
        foreach ($trial in 0,1,2) {
            $row = Convert-UiCancelRow $rows[$cursor]
            if ($row.Fields.name -cne $name -or $row.Fields.trial -ne $trial -or $row.Fields.warmup -ne $(if ($trial -eq 0) { 1 } else { 0 })) {
                throw "UI CANCEL rows are out of required operation/trial order at row $($cursor + 1): $($row.Raw)"
            }
            $expectedPreviewEnabled = if ($name -cin @('gaussian250','motion2000')) { 0 } else { 1 }
            if ($row.Fields.previewEnabled -ne $expectedPreviewEnabled) {
                throw "UI CANCEL previewEnabled does not match operation '$name' at row $($cursor + 1): $($row.Raw)"
            }
            $parsed.Add($row)
            $cursor++
        }
    }
    $reports = @($Lines | Where-Object { $_ -cmatch '^UI CANCEL REPORT\b' })
    $expectedReport = 'UI CANCEL REPORT status=ok operations=10 width=64 height=48 smoke=1 g1=0'
    if ($reports.Count -ne 1 -or $reports[0] -cne $expectedReport) { throw "Expected exactly one successful UI CANCEL REPORT: $expectedReport" }
    if ($namespace.Count -ne 31 -or $namespace[-1] -cne $expectedReport) { throw 'Cancellation report must follow exactly the complete 30-row namespace.' }
    [pscustomobject]@{ Route=$Route; Machine=$machineRows[0]; Rows=$parsed.ToArray(); Report=$reports[0] }
}

function Invoke-UiCancellationSmoke {
    if ([string]::IsNullOrWhiteSpace($ExecutablePath)) { throw '-ExecutablePath is required.' }
    if ([string]::IsNullOrWhiteSpace($OutputDirectory)) { throw '-OutputDirectory is required.' }
    if (-not (Test-Path -LiteralPath $ExecutablePath -PathType Leaf)) { throw "RedPrism.exe was not found: $ExecutablePath" }
    if (-not (Test-Path -LiteralPath $OutputDirectory -PathType Container)) { throw "Smoke output directory was not found: $OutputDirectory" }
    $mode = if ($DisplayRoute -ceq 'prepared') { 'ui-cancel-prepared-smoke' } else { 'ui-cancel-smoke' }
    $stdoutPath = Join-Path $OutputDirectory "RedPrism.$mode.log"
    $stderrPath = Join-Path $OutputDirectory "RedPrism.$mode.err.log"
    Remove-Item -LiteralPath $stdoutPath,$stderrPath -ErrorAction SilentlyContinue
    $process = Start-Process -WindowStyle Hidden -FilePath $ExecutablePath -ArgumentList @('--warp','--bench',$mode,'--isa','scalar') -PassThru -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath
    $null = $process.Handle
    $applicationDeadlineSeconds = 90
    $runnerTimeoutSeconds = 120
    $finishedWithinApplicationDeadline = $process.WaitForExit($applicationDeadlineSeconds * 1000)
    if (-not $finishedWithinApplicationDeadline -and -not $process.WaitForExit(($runnerTimeoutSeconds - $applicationDeadlineSeconds) * 1000)) {
        throw "RedPrism.exe --warp --bench $mode --isa scalar did not finish within $runnerTimeoutSeconds s (PID $($process.Id) is still running; report it, do not kill it)."
    }
    $process.WaitForExit()
    $stderr = if (Test-Path -LiteralPath $stderrPath) { Get-Content -LiteralPath $stderrPath -Raw } else { '' }
    if ($process.ExitCode -ne 0) { throw "RedPrism.exe --bench $mode exited with code $($process.ExitCode). $stderr" }
    if (-not $finishedWithinApplicationDeadline) {
        throw "RedPrism.exe --bench $mode exceeded the $applicationDeadlineSeconds s application smoke deadline, then exited with code 0 inside the $runnerTimeoutSeconds s runner window. $stderr"
    }
    $lines = @(Get-Content -LiteralPath $stdoutPath)
    $result = Parse-UiCancellationSmoke $lines $DisplayRoute
    Write-Host "PASS UI cancellation smoke: route=$DisplayRoute, 10 operations x 3 trials, 64x48"
    $global:LASTEXITCODE = 0
}

if ($MyInvocation.InvocationName -ne '.') { Invoke-UiCancellationSmoke }


