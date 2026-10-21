[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$parserPath = Join-Path $PSScriptRoot 'UiCancellationBenchmarkSmokeTests.ps1'
. $parserPath

function New-ValidCancellationLines {
    $lines = [System.Collections.Generic.List[string]]::new()
    $lines.Add('BENCH MACHINE fixture device=WARP')
    $lines.Add('UI BENCH ROUTE display=direct rows=direct asyncStroke=0 asyncWarp=0 ordinaryActivation=0')
    $names = @('levels','hsv','exposure','curves','gradient-map','grain','gaussian250','motion2000','noise','lens')
    foreach ($name in $names) {
        foreach ($trial in 0,1,2) {
            $outcome = if ($trial -eq 1) { 'returned-after-stop' } elseif ($trial -eq 2) { 'finished-before-escape' } else { 'finish-overlap' }
            $measured = if ($trial -eq 1) { 1 } else { 0 }
            $rawStatus = if ($outcome -eq 'finished-before-escape') { 0 } else { 1 }
            $applyOutcome = if ($outcome -eq 'finished-before-escape') { 1 } else { 2 }
            $applyError = if ($outcome -eq 'finished-before-escape') { '0x00000000' } else { '0x80004004' }
            $cancelKnown = if ($outcome -eq 'finished-before-escape') { 0 } else { 1 }
            $checkOutcome = if ($outcome -eq 'finished-before-escape') { 'unobserved' } elseif ($outcome -eq 'finish-overlap') { 'stop-overlap' } else { 'after-acceptance' }
            $checkKnown = [int]($checkOutcome -ne 'unobserved')
            $checkLatencyKnown = [int]($checkOutcome -eq 'after-acceptance')
            $cancelOutcome = if ($cancelKnown) { 1 } else { 0 }
            $sourcePresent = if ($cancelKnown) { 1 } else { 0 }
            $hidden = if ($cancelKnown) { 1 } else { 0 }
            $fields = [ordered]@{
                name=$name; trial=$trial; warmup=$(if ($trial -eq 0) { 1 } else { 0 }); width=64; height=48
                previewEnabled=$(if ($name -in @('gaussian250','motion2000')) { 0 } else { 1 }); outcome=$outcome; measured=$measured
                requestedDelayUs=20000; deadlineRearms=0; entryToPostedUs=0; postedToDispatchUs=0; entryToDispatchUs=0; delayKnown=1
                ticketSlot=0; ticketGeneration=1; usefulOrdinal=2; dispatchUs=1; admissionUs=1; stopToReturnUs=1; escapeToSourcePresentUs=1; settleUs=1
                rawKnown=1; rawStatus=$rawStatus; rawError='0x00000000'; applyKnown=1; applyOutcome=$applyOutcome; applyError=$applyError
                cancelKnown=$cancelKnown; cancelOutcome=$cancelOutcome; cancelError='0x00000000'; sourcePresent=$sourcePresent; hiddenAtAcceptance=$hidden
                diagnosticBytes=1; ownerBatch=32; actionP99UpperUs=1; actionMaxUs=1; applyMaxUs=1; ordinaryActivation=0; g1=0
                checkKnown=$checkKnown; checkOutcome=$checkOutcome; acceptedToCheckUs=0; checkLatencyKnown=$checkLatencyKnown; acceptancePresentOrdinal=100; sourcePresentOrdinal=$(if ($sourcePresent) { 101 } else { 0 }); firstFrameProved=$sourcePresent
            }
            $tokens = foreach ($key in $fields.Keys) { "$key=$($fields[$key])" }
            $lines.Add('UI CANCEL ' + ($tokens -join ' '))
        }
    }
    $lines.Add('UI CANCEL REPORT status=ok operations=10 width=64 height=48 smoke=1 g1=0 smokeCorrectnessOnly=1 displayAuthorityKnown=1 displayChanged=0')
    return ,$lines.ToArray()
}

$script:UiCancelRefusals = 0
function Assert-Rejected([string] $Name, [string[]] $Lines, [string] $Route = 'direct') {
    try { $null = Parse-UiCancellationSmoke $Lines $Route }
    catch { ++$script:UiCancelRefusals; Write-Host "PASS ui-cancel-parser.refuses.$Name"; return }
    throw "Parser accepted invalid case '$Name'."
}

$valid = New-ValidCancellationLines
$result = Parse-UiCancellationSmoke $valid 'direct'
if ($result.Rows.Count -ne 30 -or $result.Rows[1].RawStatus -ne 1 -or $result.Rows[1].ApplyError -ne [uint32]2147500036) { throw 'Valid fixture did not preserve typed raw/apply receipt data.' }

# Smoke is correctness-only: either display authority value is accepted independently.
foreach ($known in 0,1) { foreach ($changed in 0,1) {
    $authority = [string[]]$valid.Clone()
    $authority[-1] = "UI CANCEL REPORT status=ok operations=10 width=64 height=48 smoke=1 g1=0 smokeCorrectnessOnly=1 displayAuthorityKnown=$known displayChanged=$changed"
    $accepted = Parse-UiCancellationSmoke $authority 'direct'
    if ($accepted.Report -cne $authority[-1]) { throw "Smoke authority combination $known/$changed was not preserved." }
} }

$case = [System.Collections.Generic.List[string]]::new(); $case.AddRange($valid); $case.RemoveAt(2); Assert-Rejected 'missing row' $case.ToArray()
$case = [System.Collections.Generic.List[string]]::new(); $case.AddRange($valid); $case.Insert(3,$valid[2]); Assert-Rejected 'duplicate row' $case.ToArray()
$case = [System.Collections.Generic.List[string]]::new(); $case.AddRange($valid); $tmp=$case[2]; $case[2]=$case[3]; $case[3]=$tmp; Assert-Rejected 'wrong order' $case.ToArray()
Assert-Rejected 'wrong route' $valid 'prepared'

function Replace-RowField([string[]] $Source, [int] $Index, [string] $Pattern, [string] $Replacement) {
    $copy = [string[]]$Source.Clone()
    $copy[$Index] = [regex]::Replace($copy[$Index], $Pattern, $Replacement, [System.Text.RegularExpressions.RegexOptions]::CultureInvariant)
    return ,$copy
}
Assert-Rejected 'report missing correctness-only marker' (Replace-RowField $valid 32 ' smokeCorrectnessOnly=1' '')
Assert-Rejected 'report correctness-only marker false' (Replace-RowField $valid 32 'smokeCorrectnessOnly=1' 'smokeCorrectnessOnly=0')
Assert-Rejected 'report missing display authority known' (Replace-RowField $valid 32 ' displayAuthorityKnown=1' '')
Assert-Rejected 'report invalid display authority known' (Replace-RowField $valid 32 'displayAuthorityKnown=1' 'displayAuthorityKnown=2')
Assert-Rejected 'report missing display changed' (Replace-RowField $valid 32 ' displayChanged=0' '')
Assert-Rejected 'report invalid display changed' (Replace-RowField $valid 32 'displayChanged=0' 'displayChanged=-1')
Assert-Rejected 'report extra authority field' (Replace-RowField $valid 32 '$' ' authorityExtra=0')
$bucketCase = Replace-RowField $valid 3 'actionP99UpperUs=1 actionMaxUs=1' 'actionP99UpperUs=4 actionMaxUs=3'
$null = Parse-UiCancellationSmoke $bucketCase 'direct'
Assert-Rejected 'unknown outcome' (Replace-RowField $valid 3 'outcome=returned-after-stop' 'outcome=worker-failed')
Assert-Rejected 'false measured' (Replace-RowField $valid 3 'measured=1' 'measured=0')
Assert-Rejected 'raw failure' (Replace-RowField $valid 3 'rawStatus=1' 'rawStatus=2')
Assert-Rejected 'bad receipt' (Replace-RowField $valid 3 'applyError=0x80004004' 'applyError=0x00000000')
Assert-Rejected 'duplicate field' (Replace-RowField $valid 3 '$' ' rawKnown=1')
Assert-Rejected 'numeric invalid' (Replace-RowField $valid 3 'ticketSlot=0' 'ticketSlot=not-a-number')
Assert-Rejected 'numeric overflow' (Replace-RowField $valid 3 'ticketGeneration=1' 'ticketGeneration=18446744073709551616')
$case = Replace-RowField $valid 2 'outcome=finish-overlap' 'outcome=command-refused'; Assert-Rejected 'refusal is not smoke success' $case
$case = [System.Collections.Generic.List[string]]::new(); $case.AddRange($valid); $case.Insert(32,'UI CANCEL malformed=yes'); Assert-Rejected 'unknown namespace row' $case.ToArray()
$case = [System.Collections.Generic.List[string]]::new(); $case.AddRange($valid); $report=$case[32]; $case.RemoveAt(32); $case.Insert(2,$report); Assert-Rejected 'report before completed roster' $case.ToArray()
Assert-Rejected 'unknown field' (Replace-RowField $valid 3 '$' ' unexpected=1')
Assert-Rejected 'measured excluded warmup' (Replace-RowField $valid 2 'measured=0' 'measured=1')
Assert-Rejected 'nonboolean field' (Replace-RowField $valid 3 'rawKnown=1' 'rawKnown=2')
Assert-Rejected 'wrong preview context' (Replace-RowField $valid 21 'previewEnabled=0' 'previewEnabled=1')
Assert-Rejected 'wrong histogram bucket' (Replace-RowField $valid 3 'actionP99UpperUs=1 actionMaxUs=1' 'actionP99UpperUs=4 actionMaxUs=1')
Assert-Rejected 'deadline rearm limit' (Replace-RowField $valid 3 'deadlineRearms=0' 'deadlineRearms=3')
$case = [System.Collections.Generic.List[string]]::new(); $case.AddRange($valid); $case[32] = $case[32] -replace 'g1=0','g1=1'; Assert-Rejected 'unsupported G1 claim' $case.ToArray()
$prepared = [string[]]$valid.Clone(); $prepared[1] = $prepared[1] -replace 'display=direct rows=direct asyncStroke=0 asyncWarp=0','display=prepared rows=prepared asyncStroke=1 asyncWarp=1'; $null = Parse-UiCancellationSmoke $prepared 'prepared'
Write-Host 'PASS ui-cancel-parser.valid.prepared-route'
Assert-Rejected 'prepared partial async flags' (Replace-RowField $prepared 1 'asyncWarp=1' 'asyncWarp=0') 'prepared'
Assert-Rejected 'prepared warp-only async flags' (Replace-RowField $prepared 1 'asyncStroke=1 asyncWarp=1' 'asyncStroke=0 asyncWarp=1') 'prepared'
Assert-Rejected 'prepared disabled async flags' (Replace-RowField $prepared 1 'asyncStroke=1 asyncWarp=1' 'asyncStroke=0 asyncWarp=0') 'prepared'
Assert-Rejected 'direct enabled async flags' (Replace-RowField $valid 1 'asyncStroke=0 asyncWarp=0' 'asyncStroke=1 asyncWarp=1') 'direct'
Assert-Rejected 'route rows mismatch' (Replace-RowField $prepared 1 'rows=prepared' 'rows=direct') 'prepared'
$duplicateRoute = [Collections.Generic.List[string]]::new(); $duplicateRoute.AddRange($prepared); $duplicateRoute.Insert(2,$prepared[1]); Assert-Rejected 'duplicate route headline' $duplicateRoute.ToArray() 'prepared'
Assert-Rejected 'invented ordinary activation headline' (Replace-RowField $prepared 1 'ordinaryActivation=0' 'ordinaryActivation=1') 'prepared'
$allEarly = [string[]]$valid.Clone()
for ($index=2; $index -lt 32; ++$index) {
    $allEarly[$index] = $allEarly[$index] -replace 'outcome=[a-z-]+','outcome=finished-before-escape' -replace 'measured=[01]','measured=0' -replace 'rawStatus=[01]','rawStatus=0' -replace 'applyOutcome=[12]','applyOutcome=1' -replace 'applyError=0x[0-9a-fA-F]{8}','applyError=0x00000000' -replace 'cancelKnown=[01]','cancelKnown=0' -replace 'cancelOutcome=[01]','cancelOutcome=0' -replace 'sourcePresent=[01]','sourcePresent=0' -replace 'hiddenAtAcceptance=[01]','hiddenAtAcceptance=0' -replace 'checkKnown=[01]','checkKnown=0' -replace 'checkOutcome=[a-z-]+','checkOutcome=unobserved' -replace 'acceptedToCheckUs=\d+','acceptedToCheckUs=0' -replace 'checkLatencyKnown=[01]','checkLatencyKnown=0' -replace 'sourcePresent=[01]','sourcePresent=0' -replace 'sourcePresentOrdinal=\d+','sourcePresentOrdinal=0' -replace 'firstFrameProved=[01]','firstFrameProved=0'
}
$null = Parse-UiCancellationSmoke $allEarly 'direct'
Write-Host 'PASS ui-cancel-parser.valid.all-early-is-honest-unmeasured'
$rawFinished = Replace-RowField $valid 3 'rawStatus=1' 'rawStatus=0'; $null = Parse-UiCancellationSmoke $rawFinished 'direct'
Write-Host 'PASS ui-cancel-parser.valid.raw-finished-is-not-normalized'
$beforeStop = Replace-RowField $valid 3 'checkKnown=1 checkOutcome=after-acceptance' 'checkKnown=1 checkOutcome=before-stop'
$beforeStop = Replace-RowField $beforeStop 3 'acceptedToCheckUs=0 checkLatencyKnown=1' 'acceptedToCheckUs=0 checkLatencyKnown=0'
$null = Parse-UiCancellationSmoke $beforeStop 'direct'
Write-Host 'PASS ui-cancel-parser.valid.before-stop-check'
$boundary = Replace-RowField $valid 3 'stopToReturnUs=1' 'stopToReturnUs=140'
$boundary = Replace-RowField $boundary 3 'acceptedToCheckUs=0' 'acceptedToCheckUs=140'
$boundary = Replace-RowField $boundary 3 'acceptancePresentOrdinal=100 sourcePresentOrdinal=101' 'acceptancePresentOrdinal=18446744073709551614 sourcePresentOrdinal=18446744073709551615'
$boundaryResult = Parse-UiCancellationSmoke $boundary 'direct'
if ($boundaryResult.Rows[1].Fields.acceptedToCheckUs -ne 140 -or [decimal]$boundaryResult.Rows[1].Fields.acceptancePresentOrdinal -ne [decimal]::Parse('18446744073709551614',[System.Globalization.CultureInfo]::InvariantCulture) -or [decimal]$boundaryResult.Rows[1].Fields.sourcePresentOrdinal -ne [decimal]::Parse('18446744073709551615',[System.Globalization.CultureInfo]::InvariantCulture)) { throw 'UInt64 boundary values lost exactness.' }
Write-Host 'PASS ui-cancel-parser.valid.uint64-boundary-exact'
Assert-Rejected 'missing check field' (Replace-RowField $valid 3 ' checkOutcome=after-acceptance' '')
Assert-Rejected 'reordered check fields' (Replace-RowField $valid 3 'checkKnown=1 checkOutcome=after-acceptance' 'checkOutcome=after-acceptance checkKnown=1')
Assert-Rejected 'duplicate check field' (Replace-RowField $valid 3 '$' ' checkKnown=1')
Assert-Rejected 'check boolean range' (Replace-RowField $valid 3 'checkKnown=1' 'checkKnown=2')
Assert-Rejected 'check boolean negative' (Replace-RowField $valid 3 'checkLatencyKnown=1' 'checkLatencyKnown=-1')
Assert-Rejected 'check count decimal' (Replace-RowField $valid 3 'acceptancePresentOrdinal=100' 'acceptancePresentOrdinal=1.0')
Assert-Rejected 'check count overflow' (Replace-RowField $valid 3 'acceptancePresentOrdinal=100' 'acceptancePresentOrdinal=18446744073709551616')
Assert-Rejected 'check duration negative' (Replace-RowField $valid 3 'acceptedToCheckUs=0' 'acceptedToCheckUs=-1')
Assert-Rejected 'known check mismatch' (Replace-RowField $valid 3 'checkKnown=1' 'checkKnown=0')
Assert-Rejected 'fake body-exit proxy overlap duration' (Replace-RowField $valid 4 'acceptedToCheckUs=0' 'acceptedToCheckUs=1')
Assert-Rejected 'known check missing raw body' (Replace-RowField $valid 3 'rawKnown=1' 'rawKnown=0')
Assert-Rejected 'stale later frame gap' (Replace-RowField $valid 3 'sourcePresentOrdinal=101' 'sourcePresentOrdinal=102')
Assert-Rejected 'equal acceptance and source count' (Replace-RowField $valid 3 'sourcePresentOrdinal=101' 'sourcePresentOrdinal=100')
Assert-Rejected 'unknown proof with source count' (Replace-RowField $valid 3 'sourcePresent=1' 'sourcePresent=0')
Assert-Rejected 'measured without first frame' (Replace-RowField $valid 3 'firstFrameProved=1' 'firstFrameProved=0')
Assert-Rejected 'first frame acceptance overflow' (Replace-RowField $valid 3 'acceptancePresentOrdinal=100 sourcePresentOrdinal=101' 'acceptancePresentOrdinal=18446744073709551615 sourcePresentOrdinal=0')
Assert-Rejected 'after-acceptance latency unknown' (Replace-RowField $valid 3 'checkLatencyKnown=1' 'checkLatencyKnown=0')
Assert-Rejected 'check after body return' (Replace-RowField $valid 3 'acceptedToCheckUs=0' 'acceptedToCheckUs=141')
Assert-Rejected 'canonical preview field order' (Replace-RowField $valid 3 'previewEnabled=1 outcome=returned-after-stop measured=1' 'outcome=returned-after-stop measured=1 previewEnabled=1')
Assert-Rejected 'check tail field order' (Replace-RowField $valid 3 'checkKnown=1 checkOutcome=after-acceptance' 'checkOutcome=after-acceptance checkKnown=1')
if ($script:UiCancelRefusals -ne 55) { throw "Incorrect fixed refusal count: $script:UiCancelRefusals" }
Write-Host 'PASS UI cancellation parser pure controls: 10 positive models and 55 refusal cases; no native execution'


