[CmdletBinding()]param()
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
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
Check 'Independent tooling and scope regressions' {
    & (Join-Path $PSScriptRoot 'Tests/BuildProcessTests/Invoke-ToolingTests.ps1')
    if($LASTEXITCODE -ne 0){throw "Independent tooling failed: $LASTEXITCODE"}
}
if($failures.Count){throw ($failures -join "`n")}
Write-Host 'All independent RedPrism validation passed.'
