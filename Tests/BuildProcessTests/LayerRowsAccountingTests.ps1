[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$modulePath = Join-Path $PSScriptRoot 'LayerRowsAccounting.psm1'
Import-Module -Name $modulePath -Force

$accountingLines = @(
    'LAYER ROWS ACCOUNTING trial=0 reservedBytes=40676124 rowBytes=40676124 metadataBytes=0 releasingBytes=0 originals=0'
    'LAYER ROWS ACCOUNTING trial=1 reservedBytes=2780348 rowBytes=2306436 metadataBytes=473912 releasingBytes=0 originals=1'
    'LAYER ROWS ACCOUNTING trial=2 reservedBytes=3231012 rowBytes=2306436 metadataBytes=924576 releasingBytes=0 originals=2'
)
$benchReservations = @{
    0 = [uint64]40676124
    1 = [uint64]2780348
    2 = [uint64]3231012
}
$results = [System.Collections.Generic.List[string]]::new()

function Expect-Accepted {
    param(
        [Parameter(Mandatory)] [string] $Name,
        [Parameter(Mandatory)] [string[]] $Lines,
        [Parameter(Mandatory)] [hashtable] $Reservations
    )

    try {
        Assert-LayerRowsAccounting -Lines $Lines -BenchReservations $Reservations
        [void]$results.Add("PASS layer-rows-accounting.$Name")
    }
    catch {
        throw "FAIL layer-rows-accounting.$Name was rejected: $($_.Exception.Message)"
    }
}

function Expect-Rejected {
    param(
        [Parameter(Mandatory)] [string] $Name,
        [Parameter(Mandatory)] [string[]] $Lines,
        [Parameter(Mandatory)] [hashtable] $Reservations
    )

    try {
        Assert-LayerRowsAccounting -Lines $Lines -BenchReservations $Reservations
    }
    catch {
        [void]$results.Add("PASS layer-rows-accounting.$Name")
        return
    }
    throw "FAIL layer-rows-accounting.$Name was accepted"
}

Expect-Accepted -Name 'literal-valid-three-trial-corpus' -Lines $accountingLines -Reservations $benchReservations

$maxBytes = [uint64]::Parse('18446744073709551615', [Globalization.CultureInfo]::InvariantCulture)
$nearMaxLines = @(
    'LAYER ROWS ACCOUNTING trial=0 reservedBytes=18446744073709551615 rowBytes=18446744073709551615 metadataBytes=0 releasingBytes=0 originals=0'
    'LAYER ROWS ACCOUNTING trial=1 reservedBytes=18446744073709551615 rowBytes=18446744073709551614 metadataBytes=1 releasingBytes=0 originals=1'
    'LAYER ROWS ACCOUNTING trial=2 reservedBytes=18446744073709551615 rowBytes=18446744073709551613 metadataBytes=2 releasingBytes=0 originals=2'
)
$nearMaxReservations = @{
    0 = $maxBytes
    1 = $maxBytes
    2 = $maxBytes
}
Expect-Accepted -Name 'valid-near-uint64-maximum-three-trial-corpus' -Lines $nearMaxLines -Reservations $nearMaxReservations

# Each of these keeps the per-trial reserved total equal to BENCH, so a total-only
# implementation cannot pass by overlooking the independent accounting invariants.
$wrongInitialCharge = [string[]]$accountingLines.Clone()
$wrongInitialCharge[0] = 'LAYER ROWS ACCOUNTING trial=0 reservedBytes=40676124 rowBytes=40676123 metadataBytes=1 releasingBytes=0 originals=0'
Expect-Rejected -Name 'initial-metadata-charge-even-when-total-matches' -Lines $wrongInitialCharge -Reservations $benchReservations

$wrongOriginalCount = [string[]]$accountingLines.Clone()
$wrongOriginalCount[2] = 'LAYER ROWS ACCOUNTING trial=2 reservedBytes=3231012 rowBytes=2306436 metadataBytes=924576 releasingBytes=0 originals=1'
Expect-Rejected -Name 'original-count-must-equal-trial' -Lines $wrongOriginalCount -Reservations $benchReservations

$nonIncreasingMetadata = [string[]]$accountingLines.Clone()
$nonIncreasingMetadata[1] = 'LAYER ROWS ACCOUNTING trial=1 reservedBytes=2780348 rowBytes=1855772 metadataBytes=924576 releasingBytes=0 originals=1'
Expect-Rejected -Name 'metadata-must-strictly-increase-even-when-totals-match' -Lines $nonIncreasingMetadata -Reservations $benchReservations

$nonZeroReleasing = [string[]]$accountingLines.Clone()
$nonZeroReleasing[1] = 'LAYER ROWS ACCOUNTING trial=1 reservedBytes=2780348 rowBytes=2306436 metadataBytes=473912 releasingBytes=1 originals=1'
Expect-Rejected -Name 'releasing-bytes-must-be-zero' -Lines $nonZeroReleasing -Reservations $benchReservations

$zeroRowBytes = [string[]]$accountingLines.Clone()
$zeroRowBytes[1] = 'LAYER ROWS ACCOUNTING trial=1 reservedBytes=2780348 rowBytes=0 metadataBytes=2780348 releasingBytes=0 originals=1'
Expect-Rejected -Name 'row-bytes-must-be-positive' -Lines $zeroRowBytes -Reservations $benchReservations

$outOfOrderTrials = [string[]]@($accountingLines[1], $accountingLines[0], $accountingLines[2])
Expect-Rejected -Name 'three-row-out-of-order-trials' -Lines $outOfOrderTrials -Reservations $benchReservations

$duplicateReplacement = [string[]]@($accountingLines[0], $accountingLines[1], $accountingLines[1])
Expect-Rejected -Name 'three-row-duplicate-replaces-trial-two' -Lines $duplicateReplacement -Reservations $benchReservations

$missingTrial = [string[]]@($accountingLines[0], $accountingLines[2])
Expect-Rejected -Name 'missing-trial-row' -Lines $missingTrial -Reservations $benchReservations

$unknownTrial = [string[]]@($accountingLines) + @('LAYER ROWS ACCOUNTING trial=3 reservedBytes=1 rowBytes=1 metadataBytes=0 releasingBytes=0 originals=3')
Expect-Rejected -Name 'unknown-extra-trial' -Lines $unknownTrial -Reservations $benchReservations

$badSchema = [string[]]$accountingLines.Clone()
$badSchema[1] = 'LAYER ROWS ACCOUNTING trial=1 reservedBytes=2780348 rowBytes=2306436 metadataBytes=473912 originals=1 releasingBytes=0'
Expect-Rejected -Name 'field-order-schema-mismatch' -Lines $badSchema -Reservations $benchReservations

$extraField = [string[]]$accountingLines.Clone()
$extraField[1] += ' source=unexpected'
Expect-Rejected -Name 'unknown-extra-field' -Lines $extraField -Reservations $benchReservations

$malformedNumber = [string[]]$accountingLines.Clone()
$malformedNumber[1] = 'LAYER ROWS ACCOUNTING trial=1 reservedBytes=2780348 rowBytes=2306436 metadataBytes=473912x releasingBytes=0 originals=1'
Expect-Rejected -Name 'malformed-unsigned-value' -Lines $malformedNumber -Reservations $benchReservations

$wrongReservedTotal = [string[]]$accountingLines.Clone()
$wrongReservedTotal[1] = 'LAYER ROWS ACCOUNTING trial=1 reservedBytes=2780349 rowBytes=2306436 metadataBytes=473912 releasingBytes=0 originals=1'
Expect-Rejected -Name 'reported-total-must-match-bench' -Lines $wrongReservedTotal -Reservations $benchReservations

$changedBenchReservation = @{
    0 = [uint64]40676124
    1 = [uint64]2780349
    2 = [uint64]3231012
}
Expect-Rejected -Name 'bench-mismatch-with-valid-original-accounting-rows' -Lines $accountingLines -Reservations $changedBenchReservation

$unmatchedExtraReservation = @{
    0 = [uint64]40676124
    1 = [uint64]2780348
    2 = [uint64]3231012
    3 = [uint64]1
}
Expect-Rejected -Name 'unmatched-extra-bench-reservation' -Lines $accountingLines -Reservations $unmatchedExtraReservation

$overflowLine = [string[]]$accountingLines.Clone()
$overflowLine[2] = 'LAYER ROWS ACCOUNTING trial=2 reservedBytes=18446744073709551615 rowBytes=18446744073709551615 metadataBytes=924576 releasingBytes=0 originals=2'
$overflowReservations = @{
    0 = [uint64]40676124
    1 = [uint64]2780348
    2 = $maxBytes
}
Expect-Rejected -Name 'uint64-addition-overflow-on-trial-two-with-three-valid-trial-identities' -Lines $overflowLine -Reservations $overflowReservations

Write-Output "BEGIN layer-rows-accounting-controls"
$results | ForEach-Object { Write-Output $_ }
Write-Output ("PASS layer-rows-accounting.summary controls={0}" -f $results.Count)
Write-Output "END layer-rows-accounting-controls"
$global:LASTEXITCODE = 0
