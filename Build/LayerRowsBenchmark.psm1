Set-StrictMode -Version Latest
Import-Module (Join-Path $PSScriptRoot '../Tests/BuildProcessTests/LayerRowsAccounting.psm1') -Scope Local -ErrorAction Stop

function Read-RedPrismLayerRowsBenchmark {
    [CmdletBinding()]
    param([Parameter(Mandatory)][string[]] $Lines)

    $expectedTrials = 11
    $kinds = @('request','prepare','uia-navigation-server','uia-name-server','dispatch','window-message','completion-drain','result-apply','chrome-sync')
    $reportedKinds = @('request','prepare','uia-navigation-server','uia-name-server')
    $machine = @($Lines -cmatch '^LAYER ROWS MACHINE ')
    $completion = @($Lines -cmatch '^LAYER ROWS BENCH REPORT ')
    if ($machine.Count -ne 1 -or $machine[0] -cnotmatch ' device=WARP smoke=0 uia=embedded-STA-server frameGate=unmeasured$' -or
        $completion.Count -ne 1 -or $completion[0] -cne 'LAYER ROWS BENCH REPORT status=ok layers=10000 renames=10 smoke=0 performanceGate=unmeasured') {
        throw 'Expected one completed full-size WARP layer-row benchmark.'
    }

    $reservations = @{}
    $elapsed = @{}
    foreach ($line in @($Lines -cmatch '^LAYER ROWS BENCH trial=')) {
        if ($line -cnotmatch '^LAYER ROWS BENCH trial=(?<trial>\d+) boundary=(?<boundary>initial|rename) layers=10000 nameUnits=1024 elapsedWithWorkerWaitUs=(?<elapsed>\d+) reservedBytes=(?<reserved>\d+) namesRead=10000 totalNameUnits=10240000 clockFailures=0$') {
            throw "Invalid layer-row trial evidence. $line"
        }
        $trial = [int]$Matches.trial
        $boundary = if ($trial -eq 0) { 'initial' } else { 'rename' }
        if ($trial -ne $reservations.Count -or $trial -ge $expectedTrials -or $Matches.boundary -cne $boundary) {
            throw "Unexpected layer-row trial or phase. $line"
        }
        $reservations[$trial] = [uint64]$Matches.reserved
        $elapsed[$trial] = [uint64]$Matches.elapsed
    }
    Assert-LayerRowsAccounting -Lines $Lines -BenchReservations $reservations -ExpectedTrials $expectedTrials

    $cost = @{}
    foreach ($line in @($Lines -cmatch '^LAYER ROWS COST trial=')) {
        if ($line -cnotmatch '^LAYER ROWS COST trial=(?<trial>\d+) kind=(?<kind>[a-z-]+) samples=(?<samples>\d+) totalUs=(?<total>\d+) p99UpperUs=\d+ maxUs=\d+ totalOverflow=0$') {
            throw "Invalid layer-row timing evidence. $line"
        }
        $trial = [int]$Matches.trial
        $kind = $Matches.kind
        $key = "$trial/$kind"
        if (-not $reservations.ContainsKey($trial) -or $kind -cnotin $kinds -or $cost.ContainsKey($key)) {
            throw "Unexpected or repeated layer-row timing. $line"
        }
        $samples = [uint64]$Matches.samples
        if (($kind -cin @('request','prepare') -and $samples -ne 1) -or
            ($kind -cin @('uia-navigation-server','uia-name-server') -and $samples -ne 10000)) {
            throw "The layer-row timing sample count differs from the fixture. $line"
        }
        $cost[$key] = [uint64]$Matches.total
    }
    if ($cost.Count -ne $expectedTrials * $kinds.Count) { throw 'Layer-row timing evidence is incomplete.' }

    $initial = [ordered]@{ elapsedWithWorkerWaitUs = $elapsed[0] }
    $rename = [ordered]@{ elapsedWithWorkerWaitUs = (1..10 | ForEach-Object { $elapsed[$_] }) }
    foreach ($kind in $reportedKinds) {
        $initial[$kind] = $cost["0/$kind"]
        $rename[$kind] = @(1..10 | ForEach-Object { $cost["$_/$kind"] })
    }
    return [ordered]@{ machine = $machine[0]; initialUs = $initial; renameUs = $rename }
}

Export-ModuleMember -Function Read-RedPrismLayerRowsBenchmark
