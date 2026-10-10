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
            $cancelOutcome = if ($cancelKnown) { 1 } else { 0 }
            $sourcePresent = if ($cancelKnown) { 1 } else { 0 }
            $hidden = if ($cancelKnown) { 1 } else { 0 }
            $fields = [ordered]@{
                name=$name; trial=$trial; warmup=$(if ($trial -eq 0) { 1 } else { 0 }); width=64; height=48; outcome=$outcome; measured=$measured
                previewEnabled=$(if ($name -in @('gaussian250','motion2000')) { 0 } else { 1 })
                requestedDelayUs=20000; entryToPostedUs=0; postedToDispatchUs=0; entryToDispatchUs=0; delayKnown=1
                ticketSlot=0; ticketGeneration=1; usefulOrdinal=2; dispatchUs=1; admissionUs=1; stopToReturnUs=1; escapeToSourcePresentUs=1; settleUs=1
                rawKnown=1; rawStatus=$rawStatus; rawError='0x00000000'; applyKnown=1; applyOutcome=$applyOutcome; applyError=$applyError
                cancelKnown=$cancelKnown; cancelOutcome=$cancelOutcome; cancelError='0x00000000'; sourcePresent=$sourcePresent; hiddenAtAcceptance=$hidden
                diagnosticBytes=1; ownerBatch=32; actionP99UpperUs=1; actionMaxUs=1; applyMaxUs=1; ordinaryActivation=0; g1=0; deadlineRearms=0
            }
            $tokens = foreach ($key in $fields.Keys) { "$key=$($fields[$key])" }
            $lines.Add('UI CANCEL ' + ($tokens -join ' '))
        }
    }
    $lines.Add('UI CANCEL REPORT status=ok operations=10 width=64 height=48 smoke=1 g1=0')
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

$case = [System.Collections.Generic.List[string]]::new(); $case.AddRange($valid); $case.RemoveAt(2); Assert-Rejected 'missing row' $case.ToArray()
$case = [System.Collections.Generic.List[string]]::new(); $case.AddRange($valid); $case.Insert(3,$valid[2]); Assert-Rejected 'duplicate row' $case.ToArray()
$case = [System.Collections.Generic.List[string]]::new(); $case.AddRange($valid); $tmp=$case[2]; $case[2]=$case[3]; $case[3]=$tmp; Assert-Rejected 'wrong order' $case.ToArray()
Assert-Rejected 'wrong route' $valid 'prepared'

function Replace-RowField([string[]] $Source, [int] $Index, [string] $Pattern, [string] $Replacement) {
    $copy = [string[]]$Source.Clone()
    $copy[$Index] = [regex]::Replace($copy[$Index], $Pattern, $Replacement, [System.Text.RegularExpressions.RegexOptions]::CultureInvariant)
    return ,$copy
}
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
$prepared = [string[]]$valid.Clone(); $prepared[1] = $prepared[1] -replace 'display=direct rows=direct','display=prepared rows=prepared'; $null = Parse-UiCancellationSmoke $prepared 'prepared'
Write-Host 'PASS ui-cancel-parser.valid.prepared-route'
$allEarly = [string[]]$valid.Clone()
for ($index=2; $index -lt 32; ++$index) {
    $allEarly[$index] = $allEarly[$index] -replace 'outcome=[a-z-]+','outcome=finished-before-escape' -replace 'measured=[01]','measured=0' -replace 'rawStatus=[01]','rawStatus=0' -replace 'applyOutcome=[12]','applyOutcome=1' -replace 'applyError=0x[0-9a-fA-F]{8}','applyError=0x00000000' -replace 'cancelKnown=[01]','cancelKnown=0' -replace 'cancelOutcome=[01]','cancelOutcome=0' -replace 'sourcePresent=[01]','sourcePresent=0' -replace 'hiddenAtAcceptance=[01]','hiddenAtAcceptance=0'
}
$null = Parse-UiCancellationSmoke $allEarly 'direct'
Write-Host 'PASS ui-cancel-parser.valid.all-early-is-honest-unmeasured'
$rawFinished = Replace-RowField $valid 3 'rawStatus=1' 'rawStatus=0'; $null = Parse-UiCancellationSmoke $rawFinished 'direct'
Write-Host 'PASS ui-cancel-parser.valid.raw-finished-is-not-normalized'
if ($script:UiCancelRefusals -ne 21) { throw "Incorrect fixed refusal count: $script:UiCancelRefusals" }
Write-Host 'PASS UI cancellation parser pure controls: 5 positive models and 21 refusal cases; no native execution'


