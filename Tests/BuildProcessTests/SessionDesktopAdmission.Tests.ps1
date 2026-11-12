# Actual runner argument/scope selection and owned-drain behavior; no product launch or clipboard mutation.
[CmdletBinding()]param()
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$root=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$errors=$null;$ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $root 'test.ps1'),[ref]$null,[ref]$errors)
if($errors.Count){throw 'Test runner syntax is invalid.'}
$loops=@($ast.FindAll({param($n)$n -is [Management.Automation.Language.ForEachStatementAst] -and $n.Variable.VariablePath.UserPath -ceq 'suite' -and $n.Condition.Extent.Text.Contains('RequiresInteractiveDesktop')},$true))
if($loops.Count -ne 1){throw 'Expected the actual console suite inventory.'}
$hostTests='C:/synthetic-not-launched/HostTests.exe';$outputRoot='C:/synthetic-not-launched'
$suiteRows=@(& ([scriptblock]::Create($loops[0].Condition.Extent.Text)))
$calls=@($ast.FindAll({param($n)$n -is [Management.Automation.Language.CommandAst] -and $n.GetCommandName() -ceq 'Invoke-CiInteractiveDesktopOperation'},$true))
if($calls.Count -ne 2){throw 'Expected console and hidden-runtime admission boundaries.'}
$console=@($calls|Where-Object {$_.Extent.Text.Contains('$suite.RequiresInteractiveDesktop')})
$runtime=@($calls|Where-Object {$_.Extent.Text.Contains('--self-test')})
if($console.Count -ne 1 -or $runtime.Count -ne 1){throw 'A real test launch escaped its admission operation.'}
$script:observed=@()
function Invoke-CiInteractiveDesktopOperation {
    param([bool]$RequiresInteractiveDesktop,[scriptblock]$Operation)
    $script:observed+=,$RequiresInteractiveDesktop
    # Native child code is retained as data; the canonical shared core owns actual mutex behavior controls.
}
foreach($suite in $suiteRows){
    . ([scriptblock]::Create($console[0].Extent.Text))
    if($observed[-1] -ne ($suite.Name -ceq 'IoTests')){throw 'Real clipboard IO lost admission or background suites became serialized.'}
}
. ([scriptblock]::Create($runtime[0].Extent.Text))
if(-not $observed[-1]){throw 'Hidden WARP clipboard work lost shared session admission.'}
$native=@($ast.FindAll({param($n)$n -is [Management.Automation.Language.CommandAst] -and $n.GetCommandName() -ceq 'Start-Process' -and $n.Extent.Text.Contains("'--self-test'")},$true))
if($native.Count -ne 1 -or $native[0].Extent.StartOffset -lt $runtime[0].Extent.StartOffset -or $native[0].Extent.EndOffset -gt $runtime[0].Extent.EndOffset){throw 'The hidden WARP child launches before admission.'}
$drain=@($runtime[0].FindAll({param($n)$n -is [Management.Automation.Language.TryStatementAst] -and $null -ne $n.Finally -and $n.Finally.Extent.Text.Contains('$selfTest.Dispose()')},$true))
if($drain.Count -ne 1){throw 'Owned runtime drain does not stay inside shared admission.'}
foreach($alreadyExited in @($false,$true)){
    $selfTest=[pscustomobject]@{Id=123;HasExited=$alreadyExited;waits=0;disposals=0}
    $selfTest|Add-Member ScriptMethod WaitForExit {$this.waits++;$this.HasExited=$true}
    $selfTest|Add-Member ScriptMethod Dispose {$this.disposals++}
    $body=$drain[0].Finally.Extent.Text
    . ([scriptblock]::Create($body.Substring(1,$body.Length-2)))
    if($selfTest.waits -ne $(if($alreadyExited){0}else{1}) -or $selfTest.disposals -ne 1){throw 'A failed owned runtime released admission before drain or leaked its process handle.'}
}
Write-Host 'PASS actual clipboard suite admission and owned-drain paths; background suite lifecycles unchanged.'
$global:LASTEXITCODE=0
