[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$runnerPath = Join-Path $repoRoot 'Build\bench\run-gradient-bench.ps1'
if (-not (Test-Path -LiteralPath $runnerPath -PathType Leaf)) {
    throw "Runner script was not found: $runnerPath"
}

$runRoot = Join-Path $repoRoot ('.build\BuildProcessTests\GradientBenchmarkRunner-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $runRoot | Out-Null
$hostExe = (Get-Process -Id $PID).Path
$results = [System.Collections.Generic.List[string]]::new()

function New-FakeSnapshot {
    param([Parameter(Mandatory)] [string] $Path)

    New-Item -ItemType Directory -Path $Path | Out-Null
    $exePath = Join-Path $Path 'RedPrism.exe'
    [IO.File]::WriteAllBytes($exePath, [byte[]](0x52, 0x50, 0x46, 0x41, 0x4B, 0x45))
    foreach ($name in 'yyjson.dll', 'z.dll', 'zip.dll') {
        [IO.File]::WriteAllBytes((Join-Path $Path $name), [byte[]](0x52, 0x55, 0x4E, 0x54, 0x49, 0x4D, 0x45))
    }

    $freeze = @('RedPrism.exe', 'yyjson.dll', 'z.dll', 'zip.dll') | ForEach-Object {
        [pscustomobject]@{
            Name = $_
            Hash = (Get-FileHash -LiteralPath (Join-Path $Path $_) -Algorithm SHA256).Hash
        }
    }
    $freeze | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $Path 'binary-freeze.json') -Encoding UTF8
    [pscustomobject]@{
        schema = 1
        capability = 'ui-gradient-ops-v1'
        operations = @('gradient-preview-coalesced', 'gradient-apply-ordered')
        sha256 = (Get-FileHash -LiteralPath $exePath -Algorithm SHA256).Hash
    } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $Path 'gradient-bench-capability.json') -Encoding UTF8
}

function Invoke-RunnerControl {
    param(
        [Parameter(Mandatory)] [string] $Name,
        [Parameter(Mandatory)] [string] $Snapshot,
        [Parameter(Mandatory)] [string] $Output,
        [Parameter(Mandatory)] [int] $ExpectedExitCode,
        [Parameter(Mandatory)] [string] $ExpectedText
    )

    $outputText = @(& $hostExe -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $runnerPath `
        -SnapshotDirectory $Snapshot -OutputDirectory $Output 2>&1 | ForEach-Object { [string]$_ }) -join [Environment]::NewLine
    $exitCode = $LASTEXITCODE
    $logPath = Join-Path $runRoot "$Name.diagnostic.log"
    Set-Content -LiteralPath $logPath -Value $outputText -Encoding UTF8
    if ($exitCode -ne $ExpectedExitCode -or $outputText -notmatch [regex]::Escape($ExpectedText)) {
        throw "Control '$Name' failed: exit=$exitCode expected=$ExpectedExitCode; expected diagnostic '$ExpectedText'. See $logPath`n$outputText"
    }
    $results.Add("PASS $Name exit=$exitCode diagnostic=$ExpectedText")
}

$missingCapability = Join-Path $runRoot 'missing-capability'
New-FakeSnapshot -Path $missingCapability
Remove-Item -LiteralPath (Join-Path $missingCapability 'gradient-bench-capability.json')
Invoke-RunnerControl -Name 'missing-capability' -Snapshot $missingCapability `
    -Output (Join-Path $runRoot 'out-missing-capability') -ExpectedExitCode 3 -ExpectedText 'NEED_NEW_QUALIFIED_BINARY'

$malformedCapability = Join-Path $runRoot 'malformed-capability'
New-FakeSnapshot -Path $malformedCapability
Set-Content -LiteralPath (Join-Path $malformedCapability 'gradient-bench-capability.json') -Value '{ malformed' -Encoding UTF8
Invoke-RunnerControl -Name 'malformed-capability' -Snapshot $malformedCapability `
    -Output (Join-Path $runRoot 'out-malformed-capability') -ExpectedExitCode 3 -ExpectedText 'capability manifest is malformed'

$missingFields = Join-Path $runRoot 'missing-capability-fields'
New-FakeSnapshot -Path $missingFields
Set-Content -LiteralPath (Join-Path $missingFields 'gradient-bench-capability.json') -Value '{}' -Encoding UTF8
Invoke-RunnerControl -Name 'missing-capability-fields' -Snapshot $missingFields `
    -Output (Join-Path $runRoot 'out-missing-capability-fields') -ExpectedExitCode 3 -ExpectedText 'capability manifest is malformed'

$mismatchedHash = Join-Path $runRoot 'mismatched-hash'
New-FakeSnapshot -Path $mismatchedHash
$capabilityPath = Join-Path $mismatchedHash 'gradient-bench-capability.json'
$capability = Get-Content -LiteralPath $capabilityPath -Raw | ConvertFrom-Json
$capability.sha256 = '0' * 64
$capability | ConvertTo-Json | Set-Content -LiteralPath $capabilityPath -Encoding UTF8
Invoke-RunnerControl -Name 'mismatched-hash' -Snapshot $mismatchedHash `
    -Output (Join-Path $runRoot 'out-mismatched-hash') -ExpectedExitCode 1 -ExpectedText 'does not match both capability and immutable-freeze manifests'

$absentRuntime = Join-Path $runRoot 'absent-runtime'
New-FakeSnapshot -Path $absentRuntime
Remove-Item -LiteralPath (Join-Path $absentRuntime 'yyjson.dll')
Invoke-RunnerControl -Name 'absent-runtime' -Snapshot $absentRuntime `
    -Output (Join-Path $runRoot 'out-absent-runtime') -ExpectedExitCode 1 -ExpectedText 'Qualified snapshot file changed: yyjson.dll'

$pathTraversal = Join-Path $runRoot 'path-traversal'
New-FakeSnapshot -Path $pathTraversal
$freezePath = Join-Path $pathTraversal 'binary-freeze.json'
$freeze = @(Get-Content -LiteralPath $freezePath -Raw | ConvertFrom-Json)
$freeze += [pscustomobject]@{ Name = '..\outside.bin'; Hash = ('A' * 64) }
$freeze | ConvertTo-Json | Set-Content -LiteralPath $freezePath -Encoding UTF8
Invoke-RunnerControl -Name 'path-traversal' -Snapshot $pathTraversal `
    -Output (Join-Path $runRoot 'out-path-traversal') -ExpectedExitCode 1 -ExpectedText 'direct file names'

$duplicateOutput = Join-Path $runRoot 'duplicate-output'
New-FakeSnapshot -Path (Join-Path $duplicateOutput 'snapshot')
$existingOutput = Join-Path $duplicateOutput 'existing-output'
New-Item -ItemType Directory -Path $existingOutput | Out-Null
Set-Content -LiteralPath (Join-Path $existingOutput 'preserve.me') -Value 'existing diagnostic sentinel'
Invoke-RunnerControl -Name 'duplicate-output' -Snapshot (Join-Path $duplicateOutput 'snapshot') `
    -Output $existingOutput -ExpectedExitCode 1 -ExpectedText 'output directory already exists'
if ((Get-Content -LiteralPath (Join-Path $existingOutput 'preserve.me') -Raw).Trim() -ne 'existing diagnostic sentinel') {
    throw 'Duplicate-output control modified the pre-existing output directory.'
}

$oldSnapshot = Join-Path $repoRoot '.build\p1-gradient-qualified'
if (Test-Path -LiteralPath $oldSnapshot -PathType Container) {
    $oldOutput = Join-Path $runRoot 'out-old-snapshot'
    Invoke-RunnerControl -Name 'existing-old-snapshot' -Snapshot $oldSnapshot -Output $oldOutput `
        -ExpectedExitCode 3 -ExpectedText 'NEED_NEW_QUALIFIED_BINARY'
    if (Test-Path -LiteralPath $oldOutput) {
        throw 'Old-snapshot refusal unexpectedly created its output directory.'
    }
}
else {
    $results.Add('SKIP existing-old-snapshot: .build/p1-gradient-qualified was absent')
}

$results | Set-Content -LiteralPath (Join-Path $runRoot 'results.txt') -Encoding UTF8
$results | ForEach-Object { Write-Output $_ }
Write-Output "DIAGNOSTICS $runRoot"
$global:LASTEXITCODE = 0 # expected refusal children do not fail this successful control suite
