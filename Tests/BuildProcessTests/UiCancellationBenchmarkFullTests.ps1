<#
.SYNOPSIS
Strict saved-output reader for the full UI cancellation benchmark.
.DESCRIPTION
Dot-source this file to use Parse-UiCancellationFull without launching RedPrism.exe.
#>
[CmdletBinding()]
param(
    [string] $OutputPath,
    [ValidateSet('direct','prepared')] [string] $DisplayRoute = 'direct',
    [ValidateSet('3840x2160','8192x5464','10000x10000')] [string] $Profile = '3840x2160'
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$selectedDisplayRoute = $DisplayRoute
$helperPath = Join-Path $PSScriptRoot 'UiCancellationBenchmarkSmokeTests.ps1'
. (Resolve-Path -LiteralPath $helperPath).Path
$DisplayRoute = $selectedDisplayRoute
$script:UiFullNames = @('levels','hsv','exposure','curves','gradient-map','grain','gaussian250','motion2000','noise','lens')
$script:UiFullActiveFieldOrder = @(
    'name','trial','warmup','width','height','previewEnabled','outcome','measured','requestedDelayUs','deadlineRearms',
    'entryToPostedUs','postedToDispatchUs','entryToDispatchUs','delayKnown','ticketSlot','ticketGeneration','usefulOrdinal',
    'dispatchUs','admissionUs','stopToReturnUs','escapeToSourcePresentUs','settleUs','rawKnown','rawStatus','rawError',
    'applyKnown','applyOutcome','applyError','cancelKnown','cancelOutcome','cancelError','sourcePresent','hiddenAtAcceptance',
    'diagnosticBytes','ownerBatch','actionP99UpperUs','actionMaxUs','applyMaxUs','ordinaryActivation','g1',
    'checkKnown','checkOutcome','acceptedToCheckUs','checkLatencyKnown','acceptancePresentOrdinal','sourcePresentOrdinal','firstFrameProved'
)
$script:UiFullRefusalFieldOrder = @(
    'name','trial','warmup','width','height','stage','reportedBudgetNotices','sourceUnchanged','historyUnchanged',
    'commitWorkerAdmitted','kernelMeasured','cancelMeasured','ownerCostMeasured','ordinaryActivation','g1'
)

function Get-UiFullProfile([string] $SelectedProfile) {
    switch ($SelectedProfile) {
        '3840x2160' { return [pscustomobject]@{ Width=3840; Height=2160 } }
        '8192x5464' { return [pscustomobject]@{ Width=8192; Height=5464 } }
        '10000x10000' { return [pscustomobject]@{ Width=10000; Height=10000 } }
        default { throw "Unknown UI cancellation profile '$SelectedProfile'." }
    }
}

function Get-UiFullContext([string] $SelectedProfile, [string] $Name) {
    $large = $SelectedProfile -cne '3840x2160'
    $refused = $large -and $Name -cin @('gaussian250','motion2000')
    if (-not $refused) { return [pscustomobject]@{ Active=$true; Stage=$null } }
    $stage = if ($SelectedProfile -ceq '8192x5464') { 'settings' } else { 'opening' }
    return [pscustomobject]@{ Active=$false; Stage=$stage }
}

function Convert-UiFullActiveRow([string] $Line, [int] $ExpectedWidth, [int] $ExpectedHeight) {
    if (-not $Line.StartsWith('UI CANCEL ',[StringComparison]::Ordinal) -or $Line.StartsWith('UI CANCEL REFUSAL ',[StringComparison]::Ordinal) -or $Line.StartsWith('UI CANCEL REPORT ',[StringComparison]::Ordinal)) { throw "Expected active UI CANCEL row: $Line" }
    $tokens = $Line.Substring('UI CANCEL '.Length).Split([char[]]@(' '),[StringSplitOptions]::RemoveEmptyEntries)
    if ($tokens.Count -ne $script:UiFullActiveFieldOrder.Count) { throw 'Active UI CANCEL field count is not exact.' }
    for ($i=0; $i -lt $tokens.Count; ++$i) {
        if ($tokens[$i] -cnotmatch '^(?<key>[A-Za-z][A-Za-z0-9]*)=\S+$' -or $Matches.key -cne $script:UiFullActiveFieldOrder[$i]) { throw "Active UI CANCEL field roster/order mismatch at field $($i+1)." }
    }
    $fields = [Collections.Generic.Dictionary[string,object]]::new([StringComparer]::Ordinal)
    foreach ($token in $tokens) {
        $null = $token -cmatch '^(?<key>[A-Za-z][A-Za-z0-9]*)=(?<value>\S+)$'
        $key = $Matches.key
        if ($fields.ContainsKey($key)) { throw "Duplicate active UI CANCEL field '$key'." }
        $fields.Add($key,(Convert-UiCancelField $key $Matches.value $Line))
    }
    if ($fields.Count -ne $script:UiCancelFieldTypes.Count) { throw 'Active UI CANCEL field roster is incomplete or has extras.' }
    $f = $fields
    foreach ($key in 'warmup','measured','previewEnabled','delayKnown','rawKnown','applyKnown','cancelKnown','sourcePresent','hiddenAtAcceptance','ordinaryActivation','g1','checkKnown','checkLatencyKnown','firstFrameProved') { if ($f[$key] -notin 0,1) { throw "Active boolean field $key must be 0 or 1." } }
    foreach ($key in 'requestedDelayUs','entryToPostedUs','postedToDispatchUs','entryToDispatchUs','dispatchUs','admissionUs','stopToReturnUs','escapeToSourcePresentUs','settleUs','actionP99UpperUs','actionMaxUs','applyMaxUs','acceptedToCheckUs') { if ($f[$key] -lt 0) { throw "Negative active duration $key." } }
    if ($f.width -ne $ExpectedWidth -or $f.height -ne $ExpectedHeight -or $f.requestedDelayUs -ne 20000 -or $f.usefulOrdinal -ne 2 -or $f.ticketSlot -ge 128 -or $f.ticketGeneration -eq 0 -or $f.diagnosticBytes -eq 0 -or $f.ownerBatch -ne 32 -or $f.ordinaryActivation -ne 0 -or $f.g1 -ne 0 -or $f.delayKnown -ne 1 -or $f.deadlineRearms -gt 2) { throw 'Active UI CANCEL fixed dimensions or admission diagnostics failed.' }
    if ($f.actionP99UpperUs -notin $script:UiCancelHistogramBounds -and -not ($f.actionMaxUs -gt 1000000 -and $f.actionP99UpperUs -eq $f.actionMaxUs)) { throw 'Invalid active action P99 histogram boundary.' }
    $maxBucket = $f.actionMaxUs; foreach ($bound in $script:UiCancelHistogramBounds) { if ($f.actionMaxUs -le $bound) { $maxBucket=$bound; break } }
    if ($f.actionP99UpperUs -gt $maxBucket) { throw 'Active action P99 bucket exceeds maximum sample bucket.' }
    if ($f.entryToPostedUs -lt 20000 -or $f.entryToDispatchUs -lt $f.entryToPostedUs -or $f.entryToDispatchUs -lt $f.postedToDispatchUs) { throw 'Active UI CANCEL measured QPC interval ordering failed.' }
    if ($f.outcome -cnotin @('returned-after-stop','finish-overlap','finished-before-stop','finished-before-escape')) { throw "Disallowed active outcome '$($f.outcome)'." }
    $mustMeasure = $f.outcome -ceq 'returned-after-stop' -and $f.trial -ne 0
    if ($f.measured -ne [int]$mustMeasure) { throw 'Active measured flag does not match outcome and warmup status.' }
    if ($f.checkOutcome -cnotin @('unobserved','before-stop','stop-overlap','after-acceptance')) { throw "Disallowed cancellation check outcome: " }
    if (($f.checkKnown -eq 0) -ne ($f.checkOutcome -ceq 'unobserved')) { throw "checkKnown/checkOutcome disagree: " }
    if ($f.checkKnown -eq 0) {
        if ($f.acceptedToCheckUs -ne [uint64]0 -or $f.checkLatencyKnown -ne 0) { throw "Unobserved check has fabricated latency: " }
    } else {
        if ($f.rawKnown -ne 1) { throw "Known check lacks original useful raw body observation: " }
        if ($f.checkOutcome -ceq 'after-acceptance') {
            if ([decimal]$f.acceptedToCheckUs -gt [decimal]$f.stopToReturnUs) { throw 'First cancellation check follows original body return.' }
            if ($f.checkLatencyKnown -ne 1) { throw "After-acceptance check requires known latency: " }
        } elseif ($f.acceptedToCheckUs -ne [uint64]0 -or $f.checkLatencyKnown -ne 0) { throw "Before-stop or overlap check cannot claim acceptance latency: " }
    }
    if ($f.sourcePresent -ne $f.firstFrameProved) { throw "sourcePresent must equal firstFrameProved: " }
    if ($f.firstFrameProved -eq 1) {
        if ([decimal]$f.acceptancePresentOrdinal -eq [decimal]::Parse('18446744073709551615',[System.Globalization.CultureInfo]::InvariantCulture) -or [decimal]$f.sourcePresentOrdinal -ne ([decimal]$f.acceptancePresentOrdinal + [decimal]1)) { throw "First-frame proof is not the immediate exact present after acceptance: " }
    } elseif ($f.sourcePresentOrdinal -ne [uint64]0) { throw "Unproved first frame cannot report a source present ordinal: " }
    if ($f.measured -eq 1 -and $f.firstFrameProved -ne 1) { throw "Measured active row must prove the first frame: " }
    if ($f.outcome -ceq 'finished-before-escape') {
        if ($f.rawKnown -ne 1 -or $f.rawStatus -ne 0 -or $f.rawError -ne 0 -or $f.applyKnown -ne 1 -or $f.applyOutcome -ne 1 -or $f.applyError -ne 0 -or $f.cancelKnown -ne 0 -or $f.sourcePresent -ne 0 -or $f.hiddenAtAcceptance -ne 0 -or $f.measured -ne 0) { throw 'Active finished-before-escape receipt contract failed.' }
    } else {
        if ($f.rawKnown -ne 1 -or $f.rawStatus -notin 0,1 -or $f.rawError -ne 0 -or $f.applyKnown -ne 1 -or $f.applyOutcome -ne 2 -or $f.applyError -ne [uint32]2147500036 -or $f.cancelKnown -ne 1 -or $f.cancelOutcome -ne 1 -or $f.cancelError -ne 0 -or $f.sourcePresent -ne 1 -or $f.hiddenAtAcceptance -ne 1) { throw 'Active cancellation admission receipt contract failed.' }
    }
    [pscustomobject]@{ Raw=$Line; Fields=$fields; RawStatus=$f.rawStatus; RawError=$f.rawError; ApplyOutcome=$f.applyOutcome; ApplyError=$f.applyError; CancelOutcome=$f.cancelOutcome; CancelError=$f.cancelError }
}

function Convert-UiFullRefusalRow([string] $Line, [string] $Name, [int] $Trial, [int] $Width, [int] $Height, [string] $Stage) {
    $warmup = [int]($Trial -eq 0)
    $expected = "UI CANCEL REFUSAL name=$Name trial=$Trial warmup=$warmup width=$Width height=$Height stage=$Stage reportedBudgetNotices=1 sourceUnchanged=1 historyUnchanged=1 commitWorkerAdmitted=0 kernelMeasured=0 cancelMeasured=0 ownerCostMeasured=0 ordinaryActivation=0 g1=0"
    if ($Line -cne $expected) { throw "UI CANCEL refusal row does not exactly match its expected context: $Line" }
    $tokens = $Line.Substring('UI CANCEL REFUSAL '.Length).Split([char[]]@(' '),[StringSplitOptions]::RemoveEmptyEntries)
    if ($tokens.Count -ne $script:UiFullRefusalFieldOrder.Count) { throw 'Refusal UI CANCEL field count is not exact.' }
    for ($i=0; $i -lt $tokens.Count; ++$i) {
        if ($tokens[$i] -cnotmatch '^(?<key>[A-Za-z][A-Za-z0-9]*)=\S+$' -or $Matches.key -cne $script:UiFullRefusalFieldOrder[$i]) { throw "Refusal UI CANCEL field roster/order mismatch at field $($i+1)." }
    }
    [pscustomobject]@{ Raw=$Line; Name=$Name; Trial=$Trial; Warmup=$warmup; Width=$Width; Height=$Height; Stage=$Stage; Kind='refusal' }
}

function Get-UiFullDistribution([object[]] $Rows, [string] $Field) {
    $values = @($Rows | ForEach-Object { [long]$_.Fields[$Field] } | Sort-Object)
    if ($values.Count -eq 0) { return [pscustomobject]@{ count=0; min=$null; median=$null; p95=$null; max=$null; values=@() } }
    $left = [int][Math]::Floor(($values.Count-1)/2); $right = [int][Math]::Floor($values.Count/2)
    $median = ([double]$values[$left]/2)+([double]$values[$right]/2)
    $p95 = $values[[int][Math]::Ceiling(.95*$values.Count)-1]
    [pscustomobject]@{ count=$values.Count; min=$values[0]; median=$median; p95=$p95; max=$values[-1]; values=$values }
}

function Parse-UiCancellationFull([string[]] $Lines, [ValidateSet('direct','prepared')] [string] $Route, [ValidateSet('3840x2160','8192x5464','10000x10000')] [string] $SelectedProfile, [string] $OutputPath) {
    $profile = Get-UiFullProfile $SelectedProfile
    $machine = @($Lines | Where-Object { $_ -cmatch '^BENCH MACHINE\b' })
    if ($machine.Count -ne 1) { throw 'Expected exactly one BENCH MACHINE row.' }
    $expectedRoute = "UI BENCH ROUTE display=$Route rows=$Route asyncStroke=0 asyncWarp=0 ordinaryActivation=0"
    $routes = @($Lines | Where-Object { $_ -cmatch '^UI BENCH ROUTE\b' })
    if ($routes.Count -ne 1 -or $routes[0] -cne $expectedRoute) { throw "Expected exactly one selected route row: $expectedRoute" }
    $namespaceIndexes = [Collections.Generic.List[int]]::new()
    for ($i=0; $i -lt $Lines.Count; ++$i) { if ($Lines[$i] -cmatch '^UI CANCEL(?:\s|$)') { $namespaceIndexes.Add($i) } }
    for ($i=1; $i -lt $namespaceIndexes.Count; ++$i) { if ($namespaceIndexes[$i] -ne $namespaceIndexes[$i-1]+1) { throw 'UI CANCEL namespace rows and final report must be contiguous.' } }
    $namespace = @($namespaceIndexes | ForEach-Object { $Lines[$_] })
    $expectedReport = "UI CANCEL REPORT status=ok operations=10 width=$($profile.Width) height=$($profile.Height) smoke=0 g1=0"
    $expectedRefusals = if ($SelectedProfile -ceq '3840x2160') { 0 } else { 22 }
    $expectedActive = 110 - $expectedRefusals
    if ($namespace.Count -ne ($expectedActive+$expectedRefusals+1) -or $namespace[-1] -cne $expectedReport) { throw "Expected $expectedActive active rows, $expectedRefusals refusals and the exact final report '$expectedReport'." }
    $reports = @($Lines | Where-Object { $_ -cmatch '^UI CANCEL REPORT\b' })
    if ($reports.Count -ne 1 -or $reports[0] -cne $expectedReport) { throw "Expected exactly one unchanged successful report: $expectedReport" }

    $active = [Collections.Generic.List[object]]::new(); $refusals = [Collections.Generic.List[object]]::new(); $ordered = [Collections.Generic.List[object]]::new()
    $cursor = 0
    foreach ($name in $script:UiFullNames) {
        for ($trial=0; $trial -le 10; ++$trial) {
            $context = Get-UiFullContext $SelectedProfile $name
            $line = $namespace[$cursor]
            if ($context.Active) {
                $row = Convert-UiFullActiveRow $line $profile.Width $profile.Height
                if ($row.Fields.name -cne $name -or $row.Fields.trial -ne $trial -or $row.Fields.warmup -ne [int]($trial -eq 0)) { throw "Wrong active name/trial/warmup at namespace row $($cursor+1)." }
                $preview = [int]($name -cnotin @('gaussian250','motion2000'))
                if ($row.Fields.previewEnabled -ne $preview) { throw "Wrong previewEnabled for $name." }
                $active.Add($row); $ordered.Add($row)
            } else {
                $row = Convert-UiFullRefusalRow $line $name $trial $profile.Width $profile.Height $context.Stage
                $refusals.Add($row); $ordered.Add($row)
            }
            ++$cursor
        }
    }
    if ($cursor -ne ($namespace.Count-1) -or $active.Count -ne $expectedActive -or $refusals.Count -ne $expectedRefusals) { throw 'Full context roster did not consume the namespace exactly.' }

    $measured = @($active | Where-Object { $_.Fields.measured -eq 1 })
    $excluded = @($active | Where-Object { $_.Fields.measured -eq 0 })
    $outcomes = [ordered]@{}
    foreach ($outcome in 'returned-after-stop','finish-overlap','finished-before-stop','finished-before-escape') { $outcomes[$outcome] = @($excluded | Where-Object { $_.Fields.outcome -ceq $outcome }).Count }
    $metricFields = @('admissionUs','dispatchUs','stopToReturnUs','escapeToSourcePresentUs','settleUs')
    $distributions = [ordered]@{}; foreach ($field in $metricFields) { $distributions[$field] = Get-UiFullDistribution $measured $field }
    $recorded = @($active | Where-Object { $_.Fields.warmup -eq 0 })
    $ownerDistributions = [ordered]@{ actionP99UpperUs=(Get-UiFullDistribution $recorded 'actionP99UpperUs'); actionMaxUs=(Get-UiFullDistribution $recorded 'actionMaxUs'); applyMaxUs=(Get-UiFullDistribution $recorded 'applyMaxUs') }
    $perOperation = [ordered]@{}
    foreach ($name in $script:UiFullNames) {
        $operationActive = @($active | Where-Object { $_.Fields.name -ceq $name })
        $operationMeasured = @($operationActive | Where-Object { $_.Fields.measured -eq 1 })
        $operationExcluded = [ordered]@{}; foreach ($outcome in 'returned-after-stop','finish-overlap','finished-before-stop','finished-before-escape') { $operationExcluded[$outcome] = @($operationActive | Where-Object { $_.Fields.measured -eq 0 -and $_.Fields.outcome -ceq $outcome }).Count }
        $operationDistributions = [ordered]@{}; foreach ($field in $metricFields) { $operationDistributions[$field] = Get-UiFullDistribution $operationMeasured $field }
        $perOperation[$name] = [ordered]@{ activeRows=$operationActive.Count; measuredRows=$operationMeasured.Count; refusedRows=@($refusals | Where-Object { $_.Name -ceq $name }).Count; excludedOutcomes=$operationExcluded; measuredDistributions=$operationDistributions }
    }
    $resolvedOutput = if ([string]::IsNullOrWhiteSpace($OutputPath)) { $null } else { (Resolve-Path -LiteralPath $OutputPath).Path }
    $summary = [ordered]@{
        sourcePath=$resolvedOutput; route=$Route; profile=$SelectedProfile; width=$profile.Width; height=$profile.Height; operations=10; trialsPerOperation=11; warmups=10; rows=110; recordedTrials=100
        activeRows=$active.Count; recordedActiveRows=$recorded.Count; measuredRows=$measured.Count; excludedRows=$excluded.Count
        refusalRows=$refusals.Count; refusalRecordedRows=@($refusals | Where-Object { $_.Warmup -eq 0 }).Count; refusalWarmupRows=@($refusals | Where-Object { $_.Warmup -eq 1 }).Count
        excludedOutcomeCounts=$outcomes; measuredDistributions=$distributions; measuredActionMaxUs=(Get-UiFullDistribution $measured 'actionMaxUs'); measuredApplyMaxUs=(Get-UiFullDistribution $measured 'applyMaxUs')
        recordedOwnerDistributions=$ownerDistributions; refusedOwnerCost='unmeasured'; perOperation=$perOperation
        ordinaryActivation=$false; g1=$false; p1DoneClaim=$false; nativeQualification=$false
        evidenceScope='Saved output parser only; original process, source, product and archive authentication required separately'
    }
    [pscustomobject]@{ Route=$Route; Profile=$SelectedProfile; Machine=$machine[0]; Rows=$active.ToArray(); RefusalRows=$refusals.ToArray(); OrderedRows=$ordered.ToArray(); Report=$reports[0]; Summary=$summary }
}

if ($MyInvocation.InvocationName -ne '.') {
    if ([string]::IsNullOrWhiteSpace($OutputPath)) { throw '-OutputPath is required to read saved benchmark output.' }
    $resolvedPath = (Resolve-Path -LiteralPath $OutputPath -ErrorAction Stop).Path
    $lines = @(Get-Content -LiteralPath $resolvedPath)
    $result = Parse-UiCancellationFull $lines $DisplayRoute $Profile $resolvedPath
    $result.Summary | ConvertTo-Json -Depth 10
}
