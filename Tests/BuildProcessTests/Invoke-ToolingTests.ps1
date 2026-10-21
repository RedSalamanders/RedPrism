# Once per host qualification. Native compiler/child-process gates retain their canonical build/test owners.
[CmdletBinding()]param()
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$failures=[Collections.Generic.List[string]]::new()
$native=@('FailureReportTests.ps1','UiBenchmarkSmokeTests.ps1','IsaScanTests.ps1','CodeAnalysisTests.ps1')
$scripts=@(Get-ChildItem -LiteralPath $PSScriptRoot -Filter '*.ps1'|Where-Object{$_.Name -ne 'Invoke-ToolingTests.ps1' -and $_.Name -notin $native}|Sort-Object Name)
$scripts+=Get-Item -LiteralPath (Join-Path (Split-Path $PSScriptRoot) 'Tooling.Tests.ScopedTesting.ps1')
foreach($script in $scripts){
    Write-Host "== $($script.Name)"
    try{$global:LASTEXITCODE=0;& $script.FullName;if(-not $? -or $LASTEXITCODE -ne 0){throw "Exit $LASTEXITCODE"}}
    catch{$failures.Add($script.Name+': '+$_.Exception.Message);Write-Host ('FAIL '+$failures[-1]) -ForegroundColor Red}
}
if($failures.Count){throw ($failures -join "`n")}
Write-Host 'All independent tooling passed. Native routing, compiler controls and benchmark smoke retain separate profile gates.'
