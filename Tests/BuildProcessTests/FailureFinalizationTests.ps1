<# .SYNOPSIS Executes the actual runner catch/finally protocol with independent controlled failures; no native build, app or desktop. #>
[CmdletBinding()]param()
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$tokens=$null;$errors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $root 'test.ps1'),[ref]$tokens,[ref]$errors)
if($errors.Count){throw 'Actual native runner does not parse.'}
$blocks=@($ast.FindAll({param($Node) $Node -is [Management.Automation.Language.TryStatementAst] -and
    $null -ne $Node.Finally -and $Node.Finally.Extent.Text.Contains('Complete-TestRunEvidence -Run $evidence')},$true))
if($blocks.Count -ne 1 -or $blocks[0].CatchClauses.Count -ne 1){throw 'Expected one actual evidence completion boundary.'}
$stage=[scriptblock]::Create(@'
[CmdletBinding()]param($Primary,$Secondary,$Probe)
$evidence=@{synthetic=$true};$runPassed=$false;$runFailure=$null
function Complete-TestRunEvidence {
    param($Run,[bool]$Passed)
    $Probe.calls++;$Probe.passed=$Passed
    if($null -ne $Secondary){throw $Secondary}
}
try {if($null -ne $Primary){throw $Primary};$runPassed=$true}
'@ + $blocks[0].CatchClauses[0].Extent.Text + ' finally ' + $blocks[0].Finally.Extent.Text)
$count=0
foreach($mode in @('Success','NativeFailure','FinalizationFailure','Both')){
    $primary=if($mode -in @('NativeFailure','Both')){[InvalidOperationException]::new('controlled original native timeout')}else{$null}
    $secondary=if($mode -in @('FinalizationFailure','Both')){[IO.IOException]::new('controlled active-writer finalization refusal')}else{$null}
    $probe=@{calls=0;passed=$null};$caught=$null;$warnings=@()
    try{& $stage $primary $secondary $probe -WarningVariable warnings}catch{$caught=$_.Exception}
    if($probe.calls -ne 1 -or $probe.passed -ne ($null -eq $primary)){throw "Evidence outcome/call count changed: $mode"}
    $expected=if($null -ne $primary){$primary}else{$secondary}
    if($null -eq $expected){if($null -ne $caught){throw 'Successful execution became a failure.'}}
    elseif(-not [object]::ReferenceEquals($expected,$caught)){throw "Original failure object was replaced: $mode"}
    if($mode -ceq 'Both' -and -not (@($warnings) -join ' ').Contains($secondary.Message)){throw 'Secondary finalization failure was hidden.'}
    $count++;Write-Host "PASS actual runner failure/finalization boundary: $mode"
}
Write-Host "Failure finalization: $count independent cases passed. No native run was qualified."
$global:LASTEXITCODE=0
