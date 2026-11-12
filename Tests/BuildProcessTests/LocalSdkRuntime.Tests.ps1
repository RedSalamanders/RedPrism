# Execute actual routing/publication statements with recording boundaries; no native launch or SDK acceptance.
[CmdletBinding()]param()
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$root=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$file=Join-Path $root 'test.ps1'
$tokens=$null;$errors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile($file,[ref]$tokens,[ref]$errors)
if($errors.Count){throw 'Runtime command syntax is invalid.'}
$commands=@($ast.FindAll({param($n)$n -is [Management.Automation.Language.CommandAst] -and $n.GetCommandName() -ceq 'Open-CiLocalSdkApplicationRuntimeInput'},$true))
if($commands.Count -ne 1){throw 'Expected one original shared runtime admission.'}
function Find-RedPrismMSBuild {'C:/synthetic-not-launched/MSBuild.exe'}
function Open-CiLocalSdkApplicationRuntimeInput {
    param($ProjectRoot,$BuildReceipt,$ExpectedBuildSha256,$MSBuildPath)
    $script:received=$PSBoundParameters
    [pscustomobject]@{Build=@{executable='C:/synthetic-not-launched/RedPrism.exe'}}
}
$repoRoot=$root;$LocalSdkBuildReceipt='C:/synthetic-not-read/record.json';$LocalSdkBuildSha256='a'*64
$result=& ([scriptblock]::Create($commands[0].Extent.Text))
if($received.ProjectRoot -cne $root -or $received.BuildReceipt -cne $LocalSdkBuildReceipt -or $received.ExpectedBuildSha256 -cne $LocalSdkBuildSha256 -or $result.Build.executable -cne 'C:/synthetic-not-launched/RedPrism.exe'){throw 'Runtime routing changed original record/root/digest.'}
$saved=$env:CI_NATIVE_BUILDS_DISABLED
try{
    $env:CI_NATIVE_BUILDS_DISABLED='true';$refused=$false
    try{& $file -Configuration Release -SkipBuild -Suites Runtime -LocalSdkBuildReceipt $LocalSdkBuildReceipt -LocalSdkBuildSha256 $LocalSdkBuildSha256}
    catch{if($_.Exception.Message -notmatch 'requires local Windows native execution'){throw};$refused=$true}
    if(-not $refused){throw 'Hosted/compile-free runtime reached discovery.'}
}finally{$env:CI_NATIVE_BUILDS_DISABLED=$saved}
$scopeGuards=@($ast.FindAll({param($n)$n -is [Management.Automation.Language.IfStatementAst] -and $n.Extent.Text.Contains("throw 'Local SDK runtime requires the original build record/digest")},$true))
if($scopeGuards.Count -ne 1){throw 'Expected one original runtime scope admission.'}
# Evaluate the actual pre-discovery scope guard in every host, including hosted
# compiler-free validation. No CI markers are cleared and no discovery can run.
foreach($arguments in @(@{Suites=@('Host')},@{Suites=@('Runtime');Full=$true},@{Suites=@('Runtime');SkipBuild=$false},@{Suites=@('Runtime');Configuration='Debug'})){
    $localSdkMode=$true;$Configuration='Release';$SkipBuild=$true;$Suites=@('Runtime');$Full=$false;$Rebuild=$false
    foreach($key in $arguments.Keys){Set-Variable -Name $key -Value $arguments[$key]}
    $refused=$false
    try{. ([scriptblock]::Create($scopeGuards[0].Extent.Text))}catch{if($_.Exception.Message -notmatch 'requires the original build record/digest'){throw};$refused=$true}
    if(-not $refused){throw 'Unsupported runtime scope reached discovery.'}
}
$shotFinalizers=@($ast.FindAll({param($n)$n -is [Management.Automation.Language.TryStatementAst] -and $n.Finally -and $n.Finally.Extent.Text.Contains('Waiting for owned screenshot process')},$true))
if($shotFinalizers.Count -ne 1){throw 'Expected one owned screenshot drain boundary.'}
$shot=[pscustomobject]@{HasExited=$false;Id=42;Drained=$false;Disposed=$false}
$shot|Add-Member ScriptMethod WaitForExit {$this.Drained=$true}
$shot|Add-Member ScriptMethod Dispose {if(-not $this.Drained){throw 'Input ownership released before the child drained.'};$this.Disposed=$true}
$shot|Add-Member ScriptMethod Kill {throw 'Owned screenshot process must not be terminated.'}
. ([scriptblock]::Create(($shotFinalizers[0].Finally.Statements.Extent.Text -join "`n")))
if(-not $shot.Drained -or -not $shot.Disposed){throw 'Screenshot timeout cleanup skipped its owned process.'}
Write-Host 'PASS actual runtime admission, early hosted/unsupported-scope refusals and screenshot process drain; no native execution.'
$global:LASTEXITCODE=0
