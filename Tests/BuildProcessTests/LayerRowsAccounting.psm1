Set-StrictMode -Version Latest

# Validate the independent original-output retirement report, rather than accepting a lower bound.
function Assert-LayerRowsAccounting {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string[]] $Lines,
        [Parameter(Mandatory)] [hashtable] $BenchReservations
    )
    $rows = @($Lines | Where-Object { $_ -match '^LAYER ROWS ACCOUNTING\b' })
    if ($rows.Count -ne 3 -or $BenchReservations.Count -ne 3) {
        throw 'Expected exactly three accounting rows and three BENCH reservations.'
    }
    $seen = [System.Collections.Generic.HashSet[int]]::new()
    $previousMetadata = [uint64] 0
    foreach ($row in $rows) {
        if ($row -notmatch '^LAYER ROWS ACCOUNTING trial=(?<trial>[0-2]) reservedBytes=(?<reserved>\d+) rowBytes=(?<rows>\d+) metadataBytes=(?<metadata>\d+) releasingBytes=(?<releasing>\d+) originals=(?<originals>\d+)$') {
            throw "Malformed layer-row accounting row: $row"
        }
        $trial = [int]::Parse($Matches['trial'], [System.Globalization.CultureInfo]::InvariantCulture)
        if ($trial -ne $seen.Count -or -not $seen.Add($trial) -or -not $BenchReservations.ContainsKey($trial)) {
            throw "Unexpected, out-of-order or duplicate accounting trial: $row"
        }
        $numbers = @{}
        foreach ($name in 'reserved', 'rows', 'metadata', 'releasing', 'originals') {
            $numbers[$name] = [uint64]::Parse($Matches[$name], [System.Globalization.CultureInfo]::InvariantCulture)
        }
        $rowBytes = $numbers['rows']
        $metadata = $numbers['metadata']
        # Subtract before adding: PowerShell may otherwise promote an overflowing UInt64 sum.
        if ($rowBytes -eq 0 -or $metadata -gt ([uint64]::MaxValue - $rowBytes) -or
            $numbers['reserved'] -ne ($rowBytes + $metadata) -or
            $numbers['reserved'] -ne [uint64] $BenchReservations[$trial] -or
            $numbers['releasing'] -ne 0 -or $numbers['originals'] -ne $trial -or
            ($trial -eq 0 -and $metadata -ne 0) -or
            ($trial -gt 0 -and $metadata -le $previousMetadata)) {
            throw "Layer-row accounting violates original-output retirement: $row"
        }
        $previousMetadata = $metadata
    }
}

Export-ModuleMember -Function Assert-LayerRowsAccounting
