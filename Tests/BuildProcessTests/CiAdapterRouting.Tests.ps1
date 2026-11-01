# Execute unchanged adapter bytes against isolated engine stand-ins. No build, native test, or hosted acceptance is claimed.
[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$repoRoot = Split-Path (Split-Path $PSScriptRoot)
$adapter = Join-Path $repoRoot 'Tools/Invoke-ProjectCi.ps1'
$sourceHash = (Get-FileHash -LiteralPath $adapter -Algorithm SHA256).Hash
$fixture = Join-Path $repoRoot ('.build/BuildProcessTests/CiAdapterRouting-' + [guid]::NewGuid().ToString('N'))
$fixtureTools = Join-Path $fixture 'repo/Tools'
$engineModules = Join-Path $fixture 'engine/Tools/Modules'
New-Item -ItemType Directory -Path $fixtureTools,$engineModules -Force | Out-Null
$copy = Join-Path $fixtureTools 'Invoke-ProjectCi.ps1'
Copy-Item -LiteralPath $adapter -Destination $copy
Set-Content -LiteralPath (Join-Path $engineModules 'Ci.Policy.psm1') -Value '' -Encoding utf8
@'
function Add-AdapterRecord($Record) {
    Add-Content -LiteralPath $env:PRISM_ADAPTER_ROUTING_LOG -Value ($Record | ConvertTo-Json -Compress -Depth 5)
}
function New-CiProjectContext {
    param($Root,$EngineRoot,$Phase,$Platform,$Descriptor,[switch]$DevelopmentEngine)
    @{root=$Root;phase=$Phase;platform=$Platform;batch=@{buildNumber=123}}
}
function Invoke-CiProjectScript {
    param($Context,$Script,$Arguments)
    Add-AdapterRecord @{kind='command';script=$Script;arguments=@($Arguments)}
    if ($Script -eq 'test.ps1' -and $env:PRISM_ADAPTER_ROUTING_FAIL -eq 'true') { throw 'Injected native test failure.' }
}
function Invoke-CiAssertionProbe {
    param($Context,$Executable,$ExpectedExitCode)
    Add-AdapterRecord @{kind='probe';expectedExit=$ExpectedExitCode}
    $true
}
function Get-CiSuiteEvidence {
    param($Context,$StartedAt)
    @{discovered=7;executed=7;evidence=@{fixture=$true}}
}
function Get-CiProfileArtifacts { param($Context) @() }
function Write-CiRegressionReceipt {
    param($Context,$Discovered,$Executed,$AssertionProbePassed,$Artifacts,$EvidenceDirectory,$CanonicalEvidence,$CountUnit,$Registered,$ExpectedSkipped)
    Add-AdapterRecord @{kind='receipt';discovered=$Discovered;executed=$Executed;probe=$AssertionProbePassed}
}
Export-ModuleMember -Function New-CiProjectContext,Invoke-CiProjectScript,Invoke-CiAssertionProbe,Get-CiSuiteEvidence,Get-CiProfileArtifacts,Write-CiRegressionReceipt
'@ | Set-Content -LiteralPath (Join-Path $engineModules 'Ci.Project.psm1') -Encoding utf8
$previousLog = $env:PRISM_ADAPTER_ROUTING_LOG
$previousFailure = $env:PRISM_ADAPTER_ROUTING_FAIL
$previousOutput = $env:GITHUB_OUTPUT
$callerOutputHash = if ($previousOutput -and (Test-Path -LiteralPath $previousOutput -PathType Leaf)) {
    (Get-FileHash -LiteralPath $previousOutput -Algorithm SHA256).Hash
} else { $null }
$fixtureOutput = Join-Path $fixture 'child-github-output.txt'
$checks = 0
function Assert-Adapter([bool]$Condition,[string]$Name) {
    if (-not $Condition) { throw "FAIL ci-adapter.$Name" }
    $script:checks++
    Write-Host "PASS ci-adapter.$Name"
}
try {
    # The unchanged adapter writes job outputs even with engine stand-ins. Keep every child write in this fixture.
    $env:GITHUB_OUTPUT = $fixtureOutput
    foreach ($phase in @('ReleaseTest','Qualification')) {
        foreach ($platform in @('x64','ARM64')) {
            foreach ($fail in @($false,$true)) {
                $name = "$phase-$platform-fail-$fail"
                $env:PRISM_ADAPTER_ROUTING_LOG = Join-Path $fixture ($name + '.jsonl')
                $env:PRISM_ADAPTER_ROUTING_FAIL = $fail.ToString().ToLowerInvariant()
                & (Get-Process -Id $PID).Path -NoProfile -NonInteractive -File $copy -Phase $phase -Platform $platform -EngineRoot (Join-Path $fixture 'engine') -Descriptor fixture.json *> (Join-Path $fixture ($name + '.log'))
                $childExit = $LASTEXITCODE
                $global:LASTEXITCODE = 0
                $records = @(Get-Content -LiteralPath $env:PRISM_ADAPTER_ROUTING_LOG | ForEach-Object { $_ | ConvertFrom-Json })
                $test = @($records | Where-Object { $_.kind -eq 'command' -and $_.script -eq 'test.ps1' })
                $probes = @($records | Where-Object kind -eq 'probe')
                $receipts = @($records | Where-Object kind -eq 'receipt')
                Assert-Adapter ($test.Count -eq 1) "$name.one-canonical-test-call"
                $expected = @('-Platform',$platform,'-Configuration','Release','-Full','-SkipTooling','-SkipBuild','-BuildNumber','123','-MaxCpuCount','2')
                if ($platform -eq 'ARM64') { $expected += @('-SelfTestTimeoutSeconds','5400') }
                Assert-Adapter (($test[0].arguments -join '|') -ceq ($expected -join '|')) "$name.exact-platform-bound-and-full-suite-arguments"
                Assert-Adapter (($childExit -ne 0) -eq $fail) "$name.failure-propagates"
                Assert-Adapter ($probes.Count -eq $(if ($fail) {0} else {1})) "$name.probe-only-after-success"
                if (-not $fail) { Assert-Adapter ($probes[0].expectedExit -eq 21) "$name.always-on-release-probe" }
                $expectedReceipts = if (-not $fail -and $phase -eq 'ReleaseTest') {1} else {0}
                Assert-Adapter ($receipts.Count -eq $expectedReceipts) "$name.no-unearned-regression-receipt"
            }
        }
    }
    Assert-Adapter ((Get-FileHash -LiteralPath $adapter -Algorithm SHA256).Hash -ceq $sourceHash) 'source-unchanged'
    Assert-Adapter ((Get-FileHash -LiteralPath $copy -Algorithm SHA256).Hash -ceq $sourceHash) 'copied-entrypoint-is-byte-identical'
    Assert-Adapter (Test-Path -LiteralPath $fixtureOutput -PathType Leaf) 'child-job-output-contained-in-fixture'
    if ($callerOutputHash) {
        Assert-Adapter ((Get-FileHash -LiteralPath $previousOutput -Algorithm SHA256).Hash -ceq $callerOutputHash) 'caller-job-output-unchanged'
    }
} finally {
    $env:PRISM_ADAPTER_ROUTING_LOG = $previousLog
    $env:PRISM_ADAPTER_ROUTING_FAIL = $previousFailure
    $env:GITHUB_OUTPUT = $previousOutput
}
Write-Host "CI adapter routing: $checks checks passed; fixture evidence retained at $fixture"
