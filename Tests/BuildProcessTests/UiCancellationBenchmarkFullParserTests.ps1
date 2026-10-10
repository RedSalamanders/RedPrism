[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'UiCancellationBenchmarkFullTests.ps1')

$script:Names = @('levels','hsv','exposure','curves','gradient-map','grain','gaussian250','motion2000','noise','lens')
$script:ActiveOrder = @('name','trial','warmup','width','height','previewEnabled','outcome','measured','requestedDelayUs','deadlineRearms','entryToPostedUs','postedToDispatchUs','entryToDispatchUs','delayKnown','ticketSlot','ticketGeneration','usefulOrdinal','dispatchUs','admissionUs','stopToReturnUs','escapeToSourcePresentUs','settleUs','rawKnown','rawStatus','rawError','applyKnown','applyOutcome','applyError','cancelKnown','cancelOutcome','cancelError','sourcePresent','hiddenAtAcceptance','diagnosticBytes','ownerBatch','actionP99UpperUs','actionMaxUs','applyMaxUs','ordinaryActivation','g1','checkKnown','checkOutcome','acceptedToCheckUs','checkLatencyKnown','acceptancePresentOrdinal','sourcePresentOrdinal','firstFrameProved')

function New-ActiveLine([string] $Name, [int] $Trial, [int] $Width, [int] $Height, [bool] $AllExcluded) {
    $early = $AllExcluded -or $Trial -eq 0
    $values = @{
        name=$Name; trial=$Trial; warmup=[int]($Trial -eq 0); width=$Width; height=$Height
        previewEnabled=[int]($Name -cnotin @('gaussian250','motion2000'))
        outcome=$(if ($early) { 'finished-before-escape' } else { 'returned-after-stop' }); measured=[int](-not $early)
        requestedDelayUs=20000; deadlineRearms=0; entryToPostedUs=20000; postedToDispatchUs=20; entryToDispatchUs=20020; delayKnown=1
        ticketSlot=1; ticketGeneration=2; usefulOrdinal=2; dispatchUs=100; admissionUs=$(if ($early) { 0 } else { $Trial*100 })
        stopToReturnUs=140; escapeToSourcePresentUs=160; settleUs=180; rawKnown=1; rawStatus=$(if ($early) { 0 } else { 1 }); rawError='0x00000000'
        applyKnown=1; applyOutcome=$(if ($early) { 1 } else { 2 }); applyError=$(if ($early) { '0x00000000' } else { '0x80004004' })
        cancelKnown=[int](-not $early); cancelOutcome=[int](-not $early); cancelError='0x00000000'; sourcePresent=[int](-not $early)
        hiddenAtAcceptance=[int](-not $early); diagnosticBytes=64; ownerBatch=32; actionP99UpperUs=125
        actionMaxUs=130; applyMaxUs=50
        ordinaryActivation=0; g1=0
        checkKnown=[int](-not $early); checkOutcome=$(if ($early) { 'unobserved' } elseif ($Trial % 2 -eq 0) { 'stop-overlap' } else { 'after-acceptance' }); acceptedToCheckUs=0; checkLatencyKnown=[int](-not $early -and $Trial % 2 -ne 0); acceptancePresentOrdinal=100; sourcePresentOrdinal=$(if (-not $early) { 101 } else { 0 }); firstFrameProved=[int](-not $early)
    }
    $tokens = foreach ($key in $script:ActiveOrder) { "$key=$($values[$key])" }
    'UI CANCEL ' + ($tokens -join ' ')
}

function New-RefusalLine([string] $Name, [int] $Trial, [int] $Width, [int] $Height, [string] $Stage) {
    $warmup = [int]($Trial -eq 0)
    "UI CANCEL REFUSAL name=$Name trial=$Trial warmup=$warmup width=$Width height=$Height stage=$Stage reportedBudgetNotices=1 sourceUnchanged=1 historyUnchanged=1 commitWorkerAdmitted=0 kernelMeasured=0 cancelMeasured=0 ownerCostMeasured=0 ordinaryActivation=0 g1=0"
}

function New-FullOutput([string] $Profile='3840x2160', [string] $Route='direct', [bool] $AllExcluded=$false) {
    $dimensions = switch ($Profile) { '3840x2160' { @(3840,2160) } '8192x5464' { @(8192,5464) } '10000x10000' { @(10000,10000) } default { throw "bad test profile: $Profile" } }
    $lines = [Collections.Generic.List[string]]::new()
    $lines.Add('BENCH MACHINE independent-fixture device=WARP fixtureChecksum=0123456789abcdef')
    $lines.Add("UI BENCH ROUTE display=$Route rows=$Route asyncStroke=0 asyncWarp=0 ordinaryActivation=0")
    foreach ($name in $script:Names) {
        for ($trial=0; $trial -le 10; ++$trial) {
            $refused = $Profile -cne '3840x2160' -and $name -cin @('gaussian250','motion2000')
            if ($refused) {
                $stage = if ($Profile -ceq '8192x5464') { 'settings' } else { 'opening' }
                $lines.Add((New-RefusalLine $name $trial $dimensions[0] $dimensions[1] $stage))
            } else { $lines.Add((New-ActiveLine $name $trial $dimensions[0] $dimensions[1] $AllExcluded)) }
        }
    }
    $lines.Add("UI CANCEL REPORT status=ok operations=10 width=$($dimensions[0]) height=$($dimensions[1]) smoke=0 g1=0")
    ,$lines.ToArray()
}

$script:Rejected = 0
function Assert-Rejected([string] $Name, [string[]] $Lines, [string] $Route='direct', [string] $Profile='3840x2160') {
    try { $null = Parse-UiCancellationFull $Lines $Route $Profile; throw "Expected rejection: $Name" }
    catch { if ($_.Exception.Message -like 'Expected rejection:*') { throw }; ++$script:Rejected; Write-Host "PASS ui-cancel-full.reject.$Name" }
}
function Replace-Line([string[]] $Lines, [int] $Index, [string] $Pattern, [string] $Replacement) {
    $copy = [string[]]$Lines.Clone()
    $copy[$Index] = [regex]::Replace($copy[$Index],$Pattern,$Replacement,[System.Text.RegularExpressions.RegexOptions]::CultureInvariant)
    ,$copy
}
function Find-Refusal([string[]] $Lines, [int] $Ordinal=0) {
    $found = @()
    for ($i=0; $i -lt $Lines.Count; ++$i) { if ($Lines[$i].StartsWith('UI CANCEL REFUSAL ',[StringComparison]::Ordinal)) { $found += $i } }
    $found[$Ordinal]
}

# Positive coverage: both routes at every profile, with literal roster/count expectations.
foreach ($profileCase in @(
    @{ Profile='3840x2160'; Width=3840; Height=2160; Active=110; Refusals=0; RecordedActive=100; Measured=100; RecordedRefusals=0; WarmupRefusals=0 },
    @{ Profile='8192x5464'; Width=8192; Height=5464; Active=88; Refusals=22; RecordedActive=80; Measured=80; RecordedRefusals=20; WarmupRefusals=2 },
    @{ Profile='10000x10000'; Width=10000; Height=10000; Active=88; Refusals=22; RecordedActive=80; Measured=80; RecordedRefusals=20; WarmupRefusals=2 }
)) {
    foreach ($route in 'direct','prepared') {
        $fixture = New-FullOutput $profileCase.Profile $route
        $result = Parse-UiCancellationFull $fixture $route $profileCase.Profile
        if ($result.Rows.Count -ne $profileCase.Active -or $result.RefusalRows.Count -ne $profileCase.Refusals -or $result.Summary.activeRows -ne $profileCase.Active -or $result.Summary.recordedActiveRows -ne $profileCase.RecordedActive -or $result.Summary.measuredRows -ne $profileCase.Measured -or $result.Summary.refusalRows -ne $profileCase.Refusals -or $result.Summary.refusalRecordedRows -ne $profileCase.RecordedRefusals -or $result.Summary.refusalWarmupRows -ne $profileCase.WarmupRefusals) { throw "Independent roster oracle failed for $($profileCase.Profile)/$route." }
        if ($result.Report -cne "UI CANCEL REPORT status=ok operations=10 width=$($profileCase.Width) height=$($profileCase.Height) smoke=0 g1=0" -or $result.Summary.nativeQualification -or $result.Summary.g1 -or $result.Summary.ordinaryActivation -or $result.Summary.p1DoneClaim) { throw "Report/qualification oracle failed for $($profileCase.Profile)/$route." }
        if ($result.Rows.Count + $result.RefusalRows.Count -ne 110 -or $result.OrderedRows.Count -ne 110) { throw 'Active-only Rows and separate RefusalRows shape changed.' }
        if ($result.Summary.recordedOwnerDistributions.actionMaxUs.count -ne $profileCase.RecordedActive -or $result.Summary.recordedOwnerDistributions.applyMaxUs.count -ne $profileCase.RecordedActive) { throw 'Recorded active owner distributions lost rows or included refusals.' }
        if ($profileCase.Refusals -gt 0 -and $result.Summary.refusedOwnerCost -cne 'unmeasured') { throw 'Refused owner-cost projection must remain explicitly unmeasured.' }
        Write-Host "PASS ui-cancel-full.accept.$($profileCase.Profile).$route"
    }
}

# Independent distribution oracle: measured admission values are 100..1000 ten times.
$direct = Parse-UiCancellationFull (New-FullOutput) 'direct' '3840x2160'
$distribution = $direct.Summary.measuredDistributions.admissionUs
if ($distribution.count -ne 100 -or $distribution.min -ne 100 -or $distribution.median -ne 550 -or $distribution.p95 -ne 1000 -or $distribution.max -ne 1000 -or $distribution.values.Count -ne 100) { throw 'Independent measured median/p95 oracle failed.' }
$perOperation = $direct.Summary.perOperation.levels.measuredDistributions.admissionUs
if ($perOperation.count -ne 10 -or $perOperation.median -ne 550 -or $perOperation.p95 -ne 1000) { throw 'Independent per-operation median/p95 oracle failed.' }
Write-Host 'PASS ui-cancel-full.distribution.literal-median-p95'

# A fully excluded active roster has empty distributions represented by count=0, values=[], null statistics.
$empty = Parse-UiCancellationFull (New-FullOutput '8192x5464' 'prepared' $true) 'prepared' '8192x5464'
$emptyDistribution = $empty.Summary.perOperation.levels.measuredDistributions.stopToReturnUs
if ($empty.Summary.measuredRows -ne 0 -or $emptyDistribution.count -ne 0 -or $emptyDistribution.values.Count -ne 0 -or $null -ne $emptyDistribution.min -or $null -ne $emptyDistribution.median -or $null -ne $emptyDistribution.p95 -or $null -ne $emptyDistribution.max) { throw 'Empty distribution must have count=0, values=[], and null statistics.' }
if ($empty.Summary.recordedOwnerDistributions.applyMaxUs.count -ne 80) { throw 'Early active completions must remain in recorded owner-cost distributions.' }
Write-Host 'PASS ui-cancel-full.distribution.empty-is-honest'

# Exercise the public saved-file CLI path in a child PowerShell process; it never launches RedPrism.exe.
$cliPath = Join-Path ([IO.Path]::GetTempPath()) ("UiCancellationFullPrepared-{0}.log" -f [guid]::NewGuid().ToString('N'))
try {
    (New-FullOutput '3840x2160' 'prepared') | Set-Content -LiteralPath $cliPath -Encoding utf8
    $shellPath = (Get-Process -Id $PID).Path
    $cliJson = & $shellPath -NoProfile -File (Join-Path $PSScriptRoot 'UiCancellationBenchmarkFullTests.ps1') -OutputPath $cliPath -DisplayRoute prepared -Profile 3840x2160
    if ($LASTEXITCODE -ne 0) { throw "Prepared saved-output CLI exited with $LASTEXITCODE." }
    $cliSummary = ($cliJson -join [Environment]::NewLine) | ConvertFrom-Json
    if ($cliSummary.route -cne 'prepared' -or $cliSummary.profile -cne '3840x2160' -or $cliSummary.activeRows -ne 110 -or $cliSummary.nativeQualification) { throw 'Prepared saved-output CLI result did not preserve selected route or safe evidence flags.' }
    Write-Host 'PASS ui-cancel-full.cli.prepared-saved-reader'
} finally {
    if (Test-Path -LiteralPath $cliPath) { Remove-Item -LiteralPath $cliPath -Force }
}

$fourK = New-FullOutput
$beforeStopModel = [string[]]$fourK.Clone()
$beforeStopModel[2] = [regex]::Replace($beforeStopModel[2], 'checkKnown=0 checkOutcome=unobserved', 'checkKnown=1 checkOutcome=before-stop')
$beforeStopResult = Parse-UiCancellationFull $beforeStopModel 'direct' '3840x2160'
if ($beforeStopResult.Rows[0].Fields.checkOutcome -cne 'before-stop' -or $beforeStopResult.Rows[0].Fields.checkLatencyKnown -ne 0) { throw 'before-stop check positive model failed.' }
$boundaryModel = [string[]]$fourK.Clone()
$boundaryModel[3] = [regex]::Replace($boundaryModel[3], 'acceptedToCheckUs=0', 'acceptedToCheckUs=140')
$boundaryModel[3] = [regex]::Replace($boundaryModel[3], 'acceptancePresentOrdinal=100 sourcePresentOrdinal=101', 'acceptancePresentOrdinal=18446744073709551614 sourcePresentOrdinal=18446744073709551615')

$boundaryResult = Parse-UiCancellationFull $boundaryModel 'direct' '3840x2160'
if ($boundaryResult.Rows[1].Fields.acceptedToCheckUs -ne 140 -or [decimal]$boundaryResult.Rows[1].Fields.acceptancePresentOrdinal -ne [decimal]::Parse('18446744073709551614',[System.Globalization.CultureInfo]::InvariantCulture) -or [decimal]$boundaryResult.Rows[1].Fields.sourcePresentOrdinal -ne [decimal]::Parse('18446744073709551615',[System.Globalization.CultureInfo]::InvariantCulture)) { throw 'Full parser UInt64 boundary values lost exactness.' }
Write-Host 'PASS ui-cancel-full.check.before-stop-and-uint64-boundary'
$overlapBody = [string[]]$fourK.Clone(); $overlapBody[4] = [regex]::Replace($overlapBody[4], 'outcome=returned-after-stop measured=1', 'outcome=finish-overlap measured=0')
$overlapResult = Parse-UiCancellationFull $overlapBody 'direct' '3840x2160'
if ($overlapResult.Summary.measuredRows -ne 99 -or $overlapResult.Summary.measuredDistributions.stopToReturnUs.count -ne 99 -or $overlapResult.Summary.excludedOutcomeCounts['finish-overlap'] -ne 1) { throw 'Body-return distribution included finish-overlap work.' }
Write-Host 'PASS ui-cancel-full.distribution.excludes-body-overlap'
$large = New-FullOutput '8192x5464'
$refusalIndex = Find-Refusal $large
Assert-Rejected 'refusal-wrong-stage' (Replace-Line $large $refusalIndex 'stage=settings' 'stage=opening') 'direct' '8192x5464'
Assert-Rejected 'refusal-wrong-name' (Replace-Line $large $refusalIndex 'name=gaussian250' 'name=motion2000') 'direct' '8192x5464'
Assert-Rejected 'refusal-wrong-dimension' (Replace-Line $large $refusalIndex 'width=8192' 'width=8191') 'direct' '8192x5464'
Assert-Rejected 'refusal-wrong-trial' (Replace-Line $large $refusalIndex 'trial=0' 'trial=1') 'direct' '8192x5464'
Assert-Rejected 'refusal-wrong-warmup' (Replace-Line $large $refusalIndex 'warmup=1' 'warmup=0') 'direct' '8192x5464'
Assert-Rejected 'refusal-extra-field' (Replace-Line $large $refusalIndex '$' ' applyKnown=1') 'direct' '8192x5464'
Assert-Rejected 'refusal-missing-field' (Replace-Line $large $refusalIndex ' commitWorkerAdmitted=0' '') 'direct' '8192x5464'
Assert-Rejected 'refusal-duplicate-field' (Replace-Line $large $refusalIndex '$' ' commitWorkerAdmitted=0') 'direct' '8192x5464'
Assert-Rejected 'refusal-fabricated-duration' (Replace-Line $large $refusalIndex '$' ' admissionUs=0') 'direct' '8192x5464'
Assert-Rejected 'refusal-commit-flag-misclaim' (Replace-Line $large $refusalIndex 'commitWorkerAdmitted=0' 'commitWorkerAdmitted=1') 'direct' '8192x5464'
Assert-Rejected 'refusal-kernel-flag-misclaim' (Replace-Line $large $refusalIndex 'kernelMeasured=0' 'kernelMeasured=1') 'direct' '8192x5464'
Assert-Rejected 'refusal-measured-flag-misclaim' (Replace-Line $large $refusalIndex 'ownerCostMeasured=0' 'ownerCostMeasured=1') 'direct' '8192x5464'

$fourK = New-FullOutput
$validFourKRefusal = New-RefusalLine 'gaussian250' 0 3840 2160 'opening'
$refusalInActiveContext = [string[]]$fourK.Clone(); $refusalInActiveContext[2+(6*11)] = $validFourKRefusal
Assert-Rejected 'refusal-in-active-context' $refusalInActiveContext
$validLargeActive = New-ActiveLine 'gaussian250' 0 8192 5464 $false
Assert-Rejected 'active-in-refusal-context' (Replace-Line $large $refusalIndex '^.*$' $validLargeActive) 'direct' '8192x5464'
$activeIndex = 2
Assert-Rejected 'active-extra-field' (Replace-Line $fourK $activeIndex '$' ' surprise=1')
Assert-Rejected 'active-missing-field' (Replace-Line $fourK $activeIndex ' ticketSlot=1' '')
Assert-Rejected 'active-duplicate-field' (Replace-Line $fourK $activeIndex '$' ' name=levels')
Assert-Rejected 'active-receipt-field' (Replace-Line $fourK ($activeIndex+1) 'applyError=0x80004004' 'applyError=0x00000000')
Assert-Rejected 'active-flag-misclaim' (Replace-Line $fourK $activeIndex 'ordinaryActivation=0' 'ordinaryActivation=1')
Assert-Rejected 'active-order' (Replace-Line $fourK 2 '^UI CANCEL ' 'UI CANCEL REFUSAL ')

Assert-Rejected 'check missing field' (Replace-Line $fourK $activeIndex ' checkOutcome=unobserved' '')
Assert-Rejected 'check fields reordered' (Replace-Line $fourK $activeIndex 'checkKnown=0 checkOutcome=unobserved' 'checkOutcome=unobserved checkKnown=0')
Assert-Rejected 'duplicate check field' (Replace-Line $fourK $activeIndex '$' ' checkKnown=0')
Assert-Rejected 'check boolean range' (Replace-Line $fourK $activeIndex 'checkKnown=0' 'checkKnown=2')
Assert-Rejected 'check count decimal' (Replace-Line $fourK $activeIndex 'acceptancePresentOrdinal=100' 'acceptancePresentOrdinal=1.0')
Assert-Rejected 'check count overflow' (Replace-Line $fourK $activeIndex 'acceptancePresentOrdinal=100' 'acceptancePresentOrdinal=18446744073709551616')
Assert-Rejected 'negative check duration' (Replace-Line $fourK $activeIndex 'acceptedToCheckUs=0' 'acceptedToCheckUs=-1')
Assert-Rejected 'known mismatch' (Replace-Line $fourK 3 'checkKnown=1' 'checkKnown=0')
Assert-Rejected 'overlap duration body-exit proxy' (Replace-Line $fourK 4 'acceptedToCheckUs=0' 'acceptedToCheckUs=140')
Assert-Rejected 'known check missing raw body' (Replace-Line $fourK 3 'rawKnown=1' 'rawKnown=0')
Assert-Rejected 'stale later present gap' (Replace-Line $fourK 3 'sourcePresentOrdinal=101' 'sourcePresentOrdinal=102')
Assert-Rejected 'equal acceptance and source ordinal' (Replace-Line $fourK 3 'sourcePresentOrdinal=101' 'sourcePresentOrdinal=100')
Assert-Rejected 'unknown frame proof source ordinal' (Replace-Line $fourK 3 'firstFrameProved=1' 'firstFrameProved=0')
Assert-Rejected 'measured frame unproved' (Replace-Line $fourK 3 'firstFrameProved=1' 'firstFrameProved=0')
Assert-Rejected 'acceptance ordinal overflow' (Replace-Line $fourK 3 'acceptancePresentOrdinal=100 sourcePresentOrdinal=101' 'acceptancePresentOrdinal=18446744073709551615 sourcePresentOrdinal=0')
Assert-Rejected 'after acceptance latency unknown' (Replace-Line $fourK 3 'checkLatencyKnown=1' 'checkLatencyKnown=0')
Assert-Rejected 'check after body return' (Replace-Line $fourK 3 'acceptedToCheckUs=0' 'acceptedToCheckUs=141')
$reordered = [string[]]$fourK.Clone(); $temp=$reordered[2]; $reordered[2]=$reordered[3]; $reordered[3]=$temp
Assert-Rejected 'context-order' $reordered
$wrongReport = Replace-Line $fourK ($fourK.Count-1) 'operations=10' 'operations=9'
Assert-Rejected 'report-count' $wrongReport
$duplicateReport = [Collections.Generic.List[string]]::new(); $duplicateReport.AddRange($fourK); $duplicateReport.Add($fourK[-1])
Assert-Rejected 'duplicate-report' $duplicateReport.ToArray()
$wrongRoute = New-FullOutput '3840x2160' 'prepared'
Assert-Rejected 'selected-route' $wrongRoute 'direct' '3840x2160'
$wrongStageProfile = New-FullOutput '10000x10000'
Assert-Rejected '100mp-opening-stage' (Replace-Line $wrongStageProfile (Find-Refusal $wrongStageProfile) 'stage=opening' 'stage=settings') 'direct' '10000x10000'

if ($script:Rejected -ne 42) { throw "Expected 42 independent negative controls; observed $script:Rejected." }
Write-Host "PASS UI cancellation full parser pure controls: six profile/route positives, check boundary models, independent distributions, empty-statistics control and $script:Rejected negative controls; no native execution."
