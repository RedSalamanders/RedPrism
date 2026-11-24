<#
.SYNOPSIS
Validates RedPrism. Use CompilerFree for the pull request checks.
.PARAMETER CompilerFree
Checks project configuration, workflows, changelog and skill guidance with native builds disabled.
#>
[CmdletBinding(DefaultParameterSetName='Full')]
param(
    [Parameter(Mandatory, ParameterSetName='CompilerFree')][switch]$CompilerFree,
    [Parameter(ParameterSetName='CompilerFree')][string]$Base = 'origin/main',
    [Parameter(ParameterSetName='CompilerFree')][string]$EngineRoot = (Join-Path $PSScriptRoot '.ci-engine'),
    [Parameter(ParameterSetName='CompilerFree')][ValidateSet('x64','ARM64')][string]$Platform = 'x64',
    [Parameter(ParameterSetName='CompilerFree')][switch]$DevelopmentEngine
)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
if ($CompilerFree) {
    $EngineRoot = [IO.Path]::GetFullPath($EngineRoot)
    Import-Module (Join-Path $EngineRoot 'Tools/Modules/Ci.Policy.psm1') -Scope Local -ErrorAction Stop
    Import-Module (Join-Path $EngineRoot 'Tools/Modules/Ci.Project.psm1') -Scope Local -ErrorAction Stop
    $context = New-CiProjectContext -Root $PSScriptRoot -EngineRoot $EngineRoot -Phase Static -Platform $Platform -DevelopmentEngine:$DevelopmentEngine
    $previous = [Environment]::GetEnvironmentVariable('CI_NATIVE_BUILDS_DISABLED', 'Process')
    try {
        $env:CI_NATIVE_BUILDS_DISABLED = 'true'
        Test-CiProjectConfiguration $context
        & (Join-Path $EngineRoot 'Tools/Test-Workflows.ps1') -Root $PSScriptRoot
        $baseCommit = Resolve-CiCommit $PSScriptRoot $Base
        Invoke-CiProjectScript -Context $context -Script 'Tools/Test-Changelog.ps1' -Arguments @('-Base', $baseCommit)
        Invoke-CiProjectScript -Context $context -Script 'validate-skills.ps1' -Arguments @()
        Invoke-CiProjectScript -Context $context -Script 'validate-specs.ps1' -Arguments @()
    }
    finally {
        [Environment]::SetEnvironmentVariable('CI_NATIVE_BUILDS_DISABLED', $(if ($null -eq $previous) { [NullString]::Value } else { $previous }), 'Process')
    }
    return
}
$failures=[Collections.Generic.List[string]]::new()
function Check([string]$Name,[scriptblock]$Body){try{&$Body;Write-Host "PASS $Name"}catch{$failures.Add($Name+': '+$_.Exception.Message);Write-Host ('FAIL '+$failures[-1]) -ForegroundColor Red}}
Check 'Skills and repository policy' {
    & (Join-Path $PSScriptRoot 'validate-skills.ps1')
    if($LASTEXITCODE -ne 0){throw "Skill validator failed: $LASTEXITCODE"}
}
Check 'Native inventory' {
    Import-Module (Join-Path $PSScriptRoot 'Build/ScopedTesting.psm1') -Force
    [void](Assert-ScopedTestNames $PSScriptRoot)
}
Check 'PowerShell syntax' {
    $scripts=@(Get-ChildItem -LiteralPath $PSScriptRoot -File|Where-Object Extension -in @('.ps1','.psm1'))
    $scripts+=Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot 'Build'),(Join-Path $PSScriptRoot 'Tests') -Recurse -File|Where-Object Extension -in @('.ps1','.psm1')
    foreach($script in $scripts){$tokens=$null;$errors=$null;[void][Management.Automation.Language.Parser]::ParseFile($script.FullName,[ref]$tokens,[ref]$errors);if($errors.Count){throw "$($script.FullName): $($errors[0].Message)"}}
}
Check 'Scoped plan is indexed' {
    $index=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'Specs/Plans/WIP/README.md') -Raw
    $done=Join-Path $PSScriptRoot 'Specs/Plans/Done/ScopedTesting_2026-10-05.md'
    if(-not(Test-Path -LiteralPath $done) -and [regex]::Matches($index,'ScopedTesting_2026-10-05\.md').Count -ne 1){throw 'The scoped-testing WIP plan must have one index entry.'}
}
Check 'Specifications and local links' { & (Join-Path $PSScriptRoot 'validate-specs.ps1') }
Check 'Independent tooling and scope regressions' {
    & (Join-Path $PSScriptRoot 'Tests/BuildProcessTests/Invoke-ToolingTests.ps1')
    if($LASTEXITCODE -ne 0){throw "Independent tooling failed: $LASTEXITCODE"}
}
if($failures.Count){throw ($failures -join "`n")}
Write-Host 'All independent RedPrism validation passed.'
