<#
.SYNOPSIS
Collect paired x64 Release WARP layer-row timing evidence from exact RedPrism revisions.
.PARAMETER BaselineRevision
Revision to build and measure as the baseline.
.PARAMETER CandidateRevision
Revision to build and measure as the candidate. The default is HEAD.
.PARAMETER CalibrationAA
Measure one explicit revision under both labels to assess host variation.
.PARAMETER OutputDirectory
Parent for the unique run directory beneath this repository's .build.
.NOTES
Builds are isolated from one another. The per-run evidence is a diagnostic for the hidden layer-row workload.
#>
#Requires -Version 7.4
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string] $BaselineRevision,
    [string] $CandidateRevision = 'HEAD',
    [ValidateRange(12, 20)][int] $Blocks = 12,
    [ValidateRange(-1, [int]::MaxValue)][int] $Seed = -1,
    [switch] $CalibrationAA,
    [string] $OutputDirectory = '',
    [ValidateRange(60, 900)][int] $TimeoutSeconds = 300
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not [OperatingSystem]::IsWindows() -or [Runtime.InteropServices.RuntimeInformation]::OSArchitecture -ne 'X64') {
    throw 'Use native x64 Windows for the WARP layer-row benchmark.'
}
$root = $PSScriptRoot
$engine = Join-Path $root '.ci-engine'
Import-Module (Join-Path $root 'Build/ScopedTesting.psm1') -Force
Import-Module (Join-Path $engine 'Tools/Modules/Ci.Policy.psm1') -Force
Import-Module (Join-Path $engine 'Tools/Modules/Ci.Project.psm1') -Force
Import-Module (Join-Path $root 'Build/LayerRowsBenchmark.psm1') -Force

$driver = Resolve-CiCommit $root 'HEAD'
if (git -C $root status --porcelain) { throw 'Commit the benchmark driver before collecting evidence.' }
if ($LASTEXITCODE) { throw 'Could not verify the benchmark driver checkout.' }
$baseline = Resolve-CiCommit $root $BaselineRevision
$candidate = Resolve-CiCommit $root $CandidateRevision
if (($baseline -ceq $candidate) -ne [bool]$CalibrationAA) {
    throw 'Use distinct revisions for a comparison and the same revision for CalibrationAA.'
}
$enginePin = Resolve-CiCommit $engine 'HEAD'
$sharedInputs = [ordered]@{}
foreach ($path in @('RedPrism/ApplicationLayerRowsBenchmark.cpp','Dependencies/DxUi.lock.json','Dependencies/RedMonitor.lock.json','vcpkg.json','vcpkg-tool.json')) {
    $before = git -C $root rev-parse --verify "${baseline}:$path"
    if ($LASTEXITCODE) { throw "Missing baseline input $path." }
    $after = git -C $root rev-parse --verify "${candidate}:$path"
    if ($LASTEXITCODE -or $before -cne $after) { throw "The paired benchmark input differs. $path" }
    $sharedInputs[$path] = $before
}
foreach ($revision in @($baseline, $candidate) | Select-Object -Unique) {
    $policy = git -C $root show "${revision}:.ci/project.json" | Out-String | ConvertFrom-Json
    if ($LASTEXITCODE -or $policy.repository -cne 'RedSalamanders/RedPrism' -or $policy.engine.ref -cne $enginePin) {
        throw "The measured source must use the driver's verified engine pin. $revision"
    }
}

$buildRoot = Join-Path $root '.build'
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $buildRoot 'paired' }
$parent = Assert-CiContainedPath -Root $buildRoot -Path ([IO.Path]::GetFullPath($OutputDirectory, $root))
$runRoot = Assert-CiContainedPath -Root $root -Path (Join-Path $parent ([guid]::NewGuid().ToString('N').Substring(0, 12)))
if (Test-Path -LiteralPath $runRoot) { throw 'The benchmark run requires a new directory.' }
$evidence = [IO.Directory]::CreateDirectory((Join-Path $runRoot 'evidence')).FullName
$runTemp = [IO.Directory]::CreateDirectory((Join-Path $runRoot 'tmp')).FullName
$schedule = [Collections.Generic.List[string]]::new()
if ($Seed -eq -1) { $Seed = [Security.Cryptography.RandomNumberGenerator]::GetInt32([int]::MaxValue) }
$random = [Random]::new($Seed)
for ($block = 0; $block -lt $Blocks; ++$block) {
    $schedule.Add($(if ($random.Next(2) -eq 0) { 'ABBA' } else { 'BAAB' }))
}
$report = [ordered]@{
    schemaVersion = 1
    status = 'started'
    scope = 'hidden WARP layer-row timing'
    driver = $driver
    engine = $enginePin
    baseline = $baseline
    candidate = $candidate
    calibrationAA = [bool]$CalibrationAA
    platform = 'x64'
    configuration = 'Release'
    seed = $Seed
    schedule = $schedule
    sharedInputs = $sharedInputs
    builds = [ordered]@{}
    observations = [Collections.Generic.List[object]]::new()
    pairedDifferencesUs = [Collections.Generic.List[object]]::new()
}
$receiptPath = Join-Path $evidence 'result.json'

function Get-ReviewedBuild([string] $Tree, [string] $Revision) {
    if ((Resolve-CiCommit $Tree 'HEAD') -cne $Revision -or (git -C $Tree status --porcelain)) {
        throw "Measured source changed. $Revision"
    }
    if ($LASTEXITCODE) { throw "Could not verify the measured source. $Revision" }
    $profile = Join-Path $Tree '.build/x64/Release'
    $exe = Join-Path $profile 'RedPrism.exe'
    Assert-CiPeArchitecture -Path $exe -Platform x64
    $version = [Diagnostics.FileVersionInfo]::GetVersionInfo($exe)
    $attestation = Join-Path $profile 'build-attestation.json'
    $key = Get-ScopedBuildAttestationKey -Root $Tree -Platform x64 -Configuration Release -BuildNumber $version.FileBuildPart -EnableTests $true
    if ($version.OriginalFilename -cne 'RedPrism.exe' -or -not (Test-ScopedReceipt -Path $attestation -Identity $key)) {
        throw "A source-bound test-enabled Release build is required. $Revision"
    }
    return [ordered]@{ path = $exe; version = $version.FileVersion; sha256 = (Get-FileHash -LiteralPath $exe).Hash; attestation = (Get-FileHash -LiteralPath $attestation).Hash }
}

function New-BenchmarkBuild([string] $Role, [string] $Revision) {
    $tree = Join-Path $runRoot $Role
    git -C $root -c core.longpaths=true worktree add --quiet --detach $tree $Revision
    if ($LASTEXITCODE) { throw "Could not create the measured worktree. $Role" }
    $treeEngine = Join-Path $tree '.ci-engine'
    git -c core.longpaths=true clone --quiet --no-hardlinks --no-checkout -- $engine $treeEngine
    if ($LASTEXITCODE) { throw 'Could not provision the pinned engine in the measured worktree.' }
    git -C $treeEngine remote set-url origin 'https://github.com/RedSalamanders/.github.git'
    if ($LASTEXITCODE) { throw 'Could not set the engine remote.' }
    git -C $treeEngine switch --quiet --detach $enginePin
    if ($LASTEXITCODE -or (Resolve-CiCommit $treeEngine 'HEAD') -cne $enginePin) { throw 'Could not select the pinned engine.' }
    $buildLog = Join-Path $evidence "build-$Role.log"
    pwsh -NoProfile -NonInteractive -File (Join-Path $tree 'build.ps1') -Configuration Release -Platform x64 -EnableTests -MaxCpuCount 2 *> $buildLog
    if ($LASTEXITCODE -or -not (Select-String -LiteralPath $buildLog -Pattern 'Diagnostics: 0 warning\(s\), 0 error\(s\)' -Quiet)) {
        throw "The measured Release build failed. $buildLog"
    }
    $binary = Get-ReviewedBuild $tree $Revision
    Copy-Item -LiteralPath (Join-Path $tree '.build/x64/Release/build-attestation.json') -Destination (Join-Path $evidence "attestation-$Role.json")
    return [ordered]@{ tree = $tree; source = $Revision; binary = $binary }
}

function Get-Median([double[]] $Values) {
    $ordered = @($Values | Sort-Object)
    $middle = [int][Math]::Floor($ordered.Count / 2)
    if ($ordered.Count % 2) { return $ordered[$middle] }
    return ($ordered[$middle - 1] + $ordered[$middle]) / 2
}

$previousTemp = $env:TEMP
$previousTmp = $env:TMP
$previousBinarySources = $env:VCPKG_BINARY_SOURCES
try {
    $env:TEMP = $env:TMP = $runTemp
    $env:VCPKG_BINARY_SOURCES = 'clear'
    $report.builds.A = New-BenchmarkBuild 'A' $baseline
    $report.builds.B = if ($CalibrationAA) { $report.builds.A } else { New-BenchmarkBuild 'B' $candidate }
    $machine = $null
    for ($block = 0; $block -lt $Blocks; ++$block) {
        $replicates = @{ A = [Collections.Generic.List[object]]::new(); B = [Collections.Generic.List[object]]::new() }
        for ($position = 0; $position -lt 4; ++$position) {
            $role = [string]$schedule[$block][$position]
            $side = $report.builds[$role]
            $stem = "block-$($block + 1)-$($position + 1)-$role"
            $stdout = Join-Path $evidence "$stem.out.log"
            $stderr = Join-Path $evidence "$stem.err.log"
            $process = Start-Process -WindowStyle Hidden -FilePath $side.binary.path -WorkingDirectory $side.tree `
                -ArgumentList @('--warp','--bench','layer-rows') -RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru
            try {
                $null = $process.Handle
                if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
                    throw "Benchmark run exceeded its time budget. PID $($process.Id). See $stdout."
                }
                $process.WaitForExit()
                if ($null -eq $process.ExitCode -or $process.ExitCode -ne 0 -or (Get-Item -LiteralPath $stderr).Length -ne 0) {
                    throw "Benchmark run failed. Exit $($process.ExitCode). See $stdout and $stderr."
                }
                $measured = Read-RedPrismLayerRowsBenchmark -Lines (Get-Content -LiteralPath $stdout)
                $version = [regex]::Match($measured.machine, ' application_version="(?<version>[^"]+)"')
                if (-not $version.Success -or $version.Groups['version'].Value -cne $side.binary.version) {
                    throw "The measured application version differs from the attested executable. See $stdout."
                }
                $environment = $measured.machine.Remove($version.Index, $version.Length)
                if ($null -eq $machine) { $machine = $environment }
                if ($environment -cne $machine) { throw "Benchmark machine or profile changed. See $stdout." }
                $report.observations.Add([ordered]@{ block = $block + 1; position = $position + 1; role = $role; log = "$stem.out.log"; result = $measured })
                $replicates[$role].Add($measured)
            } finally {
                $process.Dispose()
            }
        }
        foreach ($phase in @('initialUs','renameUs')) {
            foreach ($metric in $replicates.A[0][$phase].Keys) {
                $a = @($replicates.A | ForEach-Object { Get-Median $_[$phase][$metric] })
                $b = @($replicates.B | ForEach-Object { Get-Median $_[$phase][$metric] })
                $report.pairedDifferencesUs.Add([ordered]@{
                    block = $block + 1
                    phase = $phase
                    metric = $metric
                    baseline = $a
                    candidate = $b
                    candidateMinusBaseline = (Get-Median $b) - (Get-Median $a)
                })
            }
        }
    }
    foreach ($side in $report.builds.Values) {
        $after = Get-ReviewedBuild $side.tree $side.source
        if ($after.sha256 -cne $side.binary.sha256 -or $after.attestation -cne $side.binary.attestation) {
            throw "Measured artifacts changed during collection. $($side.source)"
        }
    }
    $report.status = 'collected'
    Write-Host "Paired layer-row evidence written to $receiptPath"
} catch {
    $report.status = 'failed'
    $report.error = $_.Exception.Message
    throw
} finally {
    $env:TEMP = $previousTemp
    $env:TMP = $previousTmp
    $env:VCPKG_BINARY_SOURCES = $previousBinarySources
    $report | ConvertTo-Json -Depth 14 | Set-Content -LiteralPath $receiptPath -Encoding utf8NoBOM
}
