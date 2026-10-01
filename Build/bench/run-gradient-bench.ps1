[CmdletBinding()]
param(
    [string] $SnapshotDirectory,
    [string] $OutputDirectory
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if (-not $SnapshotDirectory) { $SnapshotDirectory = Join-Path $repoRoot '.build\p1-gradient-bench-qualified' }
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $repoRoot ('.build\p1-gradient-bench-runs-' + [Guid]::NewGuid().ToString('N')) }
$exe = Join-Path $SnapshotDirectory 'RedPrism.exe'
$manifestPath = Join-Path $SnapshotDirectory 'gradient-bench-capability.json'
$freezePath = Join-Path $SnapshotDirectory 'binary-freeze.json'

# A missing or stale capability record is a hard stop. In particular, never probe an older binary with new CLI args.
if (-not (Test-Path -LiteralPath $exe -PathType Leaf) -or
    -not (Test-Path -LiteralPath $manifestPath -PathType Leaf) -or
    -not (Test-Path -LiteralPath $freezePath -PathType Leaf)) {
    Write-Output 'NEED_NEW_QUALIFIED_BINARY: expected immutable .build/p1-gradient-bench-qualified snapshot and capability manifest; no process launched.'
    exit 3
}
try {
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json -ErrorAction Stop
    foreach ($field in 'schema','capability','operations','sha256') {
        if ($null -eq $manifest -or $null -eq $manifest.PSObject.Properties[$field]) {
            throw "Missing capability field: $field"
        }
    }
}
catch {
    Write-Output "NEED_NEW_QUALIFIED_BINARY: capability manifest is malformed ($($_.Exception.Message)); no process launched."
    exit 3
}
$requiredOperations = @('gradient-preview-coalesced', 'gradient-apply-ordered')
if ($manifest.schema -ne 1 -or $manifest.capability -ne 'ui-gradient-ops-v1' -or
    (@($manifest.operations) -join ',') -ne ($requiredOperations -join ',')) {
    Write-Output 'NEED_NEW_QUALIFIED_BINARY: snapshot manifest does not declare both gradient UI operations; no process launched.'
    exit 3
}
$expectedHash = ([string]$manifest.sha256).ToUpperInvariant()
$actualHash = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToUpperInvariant()
try {
    $freeze = @(Get-Content -LiteralPath $freezePath -Raw | ConvertFrom-Json -ErrorAction Stop)
}
catch {
    Write-Output "NEED_NEW_QUALIFIED_BINARY: binary-freeze manifest is malformed ($($_.Exception.Message)); no process launched."
    exit 3
}
$frozenExecutable = @($freeze | Where-Object { $_.Name -ceq 'RedPrism.exe' })
if ($expectedHash -notmatch '^[0-9A-F]{64}$' -or $actualHash -ne $expectedHash -or
    $frozenExecutable.Count -ne 1 -or ([string]$frozenExecutable[0].Hash).ToUpperInvariant() -ne $actualHash) {
    throw 'Qualified binary snapshot hash does not match both capability and immutable-freeze manifests; no benchmark process launched.'
}

function Assert-BinarySnapshot {
    foreach ($file in $freeze) {
        $name = [string]$file.Name
        if ([string]::IsNullOrEmpty($name) -or [IO.Path]::GetFileName($name) -cne $name) {
            throw 'Snapshot manifest must contain only direct file names.'
        }
        $path = Join-Path $SnapshotDirectory $name
        if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or
            (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ine [string]$file.Hash) {
            throw "Qualified snapshot file changed: $name"
        }
    }
    foreach ($runtimeFile in 'RedPrism.exe','yyjson.dll','z.dll','zip.dll') {
        if (@($freeze | Where-Object { $_.Name -ceq $runtimeFile }).Count -ne 1) {
            throw "Snapshot must freeze its exact runtime file: $runtimeFile"
        }
    }
}
Assert-BinarySnapshot

if (Test-Path -LiteralPath $OutputDirectory) {
    throw "Benchmark output directory already exists; choose a fresh unique directory: $OutputDirectory"
}
New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
$cases = foreach ($mode in @('ui', 'ui45', 'ui100')) {
    [pscustomobject]@{ Device = 'hardware'; Mode = $mode; Arguments = @('--bench', $mode) }
    [pscustomobject]@{ Device = 'warp'; Mode = $mode; Arguments = @('--warp', '--bench', $mode) }
}
foreach ($case in $cases) {
    Assert-BinarySnapshot
    $tag = "$($case.Device)-$($case.Mode)"
    $stdoutPath = Join-Path $OutputDirectory "RedPrism.$tag.log"
    $stderrPath = Join-Path $OutputDirectory "RedPrism.$tag.err.log"
    if ((Test-Path -LiteralPath $stdoutPath) -or (Test-Path -LiteralPath $stderrPath)) {
        throw "Benchmark output already exists for $tag; choose a fresh output directory to preserve evidence."
    }
    $process = Start-Process -WindowStyle Hidden -FilePath $exe -ArgumentList $case.Arguments -PassThru `
        -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath
    $null = $process.Handle
    if (-not $process.WaitForExit(7200000)) {
        throw "Benchmark $tag exceeded two hours (PID $($process.Id)); report it and do not terminate it."
    }
    $process.WaitForExit()
    $afterHash = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToUpperInvariant()
    if ($afterHash -ne $expectedHash) { throw "Qualified binary changed during $tag; stop the matrix." }
    Assert-BinarySnapshot
    $stderr = if (Test-Path -LiteralPath $stderrPath) { Get-Content -LiteralPath $stderrPath -Raw } else { '' }
    if ($process.ExitCode -ne 0) { throw "Benchmark $tag exited $($process.ExitCode). $stderr" }
    $lines = @(Get-Content -LiteralPath $stdoutPath)
    $report = @($lines | Where-Object { $_ -match '^UI BENCH REPORT status=ok .*operations=26\b' })
    $gradientRows = @($lines | Where-Object { $_ -match '^UI BENCH name=gradient-(preview-coalesced|apply-ordered)\b' })
    if ($report.Count -ne 1 -or $gradientRows.Count -ne 2) {
        throw "Benchmark $tag did not report both gradient rows and a 26-operation success report."
    }
    Write-Output "PASS $tag sha256=$expectedHash"
}
