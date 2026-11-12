# Actual argument selection plus an optional controlled shared-file writer counterfactual; no product compilation.
[CmdletBinding()]param([string]$NativeMsBuildPath='')
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$root=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$tokens=$null;$errors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $root 'build.ps1'),[ref]$tokens,[ref]$errors)
if($errors.Count){throw 'Build argument syntax is invalid.'}
$assignments=@($ast.FindAll({param($n)$n -is [Management.Automation.Language.AssignmentStatementAst] -and $n.Left.Extent.Text -ceq '$workerArgument'},$true))
if($assignments.Count -ne 1){throw 'Expected one actual MSBuild worker argument.'}
$policy=[scriptblock]::Create($assignments[0].Extent.Text)
foreach($Configuration in @('ASan Debug','asan debug','ASAN DEBUG','Debug','Release')){
    foreach($MaxCpuCount in @(0,1,2,8)){
        . $policy
        $expected=if($Configuration -eq 'ASan Debug'){'/m:1'}elseif($MaxCpuCount){'/m:'+$MaxCpuCount}else{'/m'}
        if($workerArgument -cne $expected){throw 'Actual ASan project scheduling or ordinary requested concurrency changed.'}
    }
}
Write-Host 'PASS 20 actual profile/project-worker selections, including accepted case aliases; C++ compiler flags are unchanged.'
if($NativeMsBuildPath){
    if(-not $IsWindows -or $env:CI_NATIVE_BUILDS_DISABLED -ceq 'true'){throw 'Controlled MSBuild scheduling requires explicit local native tooling.'}
    $fixture=Join-Path $root ('.build/asan-project-scheduling/'+[guid]::NewGuid().ToString('N'))
    [void][IO.Directory]::CreateDirectory($fixture)
    $holder=Join-Path $fixture 'Hold.ps1'
    [IO.File]::WriteAllText($holder,'param($Path,$Ready) $stream=[IO.File]::Open($Path,[IO.FileMode]::Create,[IO.FileAccess]::Write,[IO.FileShare]::None); try{[IO.File]::WriteAllText($Ready,"ready"); Start-Sleep -Milliseconds 2500} finally{$stream.Dispose()}')
    $waiter=Join-Path $fixture 'Wait.ps1'
    [IO.File]::WriteAllText($waiter,'param($Ready) $clock=[Diagnostics.Stopwatch]::StartNew(); while(-not [IO.File]::Exists($Ready)){if($clock.ElapsedMilliseconds -gt 10000){throw "Fixture writer did not start"}; Start-Sleep -Milliseconds 10}')
    $shell=(Get-Process -Id $PID).Path
    function Escape-Xml([string]$Value){[Security.SecurityElement]::Escape($Value)}
    foreach($role in @('parallel-counterfactual','actual-asan-policy')){
        $run=Join-Path $fixture $role;[void][IO.Directory]::CreateDirectory($run)
        $destination=Join-Path $run 'shared-runtime.dll';$ready=Join-Path $run 'ready.txt';$source=Join-Path $run 'source.dll'
        [IO.File]::WriteAllText($source,'synthetic runtime writer bytes; not a native DLL')
        $holdCommand='"'+$shell+'" -NoProfile -NonInteractive -File "'+$holder+'" -Path "'+$destination+'" -Ready "'+$ready+'"'
        $waitCommand='"'+$shell+'" -NoProfile -NonInteractive -File "'+$waiter+'" -Ready "'+$ready+'"'
        [IO.File]::WriteAllText((Join-Path $run 'A.proj'),('<Project><Target Name="Build"><Exec Command="'+(Escape-Xml $holdCommand)+'" /></Target></Project>'))
        [IO.File]::WriteAllText((Join-Path $run 'B.proj'),('<Project><Target Name="Build"><Exec Command="'+(Escape-Xml $waitCommand)+'" /><Copy SourceFiles="'+(Escape-Xml $source)+'" DestinationFiles="'+(Escape-Xml $destination)+'" Retries="0" /></Target></Project>'))
        $driver=Join-Path $run 'Driver.proj'
        [IO.File]::WriteAllText($driver,'<Project><ItemGroup><Child Include="A.proj;B.proj" /></ItemGroup><Target Name="Build"><MSBuild Projects="@(Child)" Targets="Build" BuildInParallel="true" /></Target></Project>')
        $Configuration='ASan Debug';$MaxCpuCount=8;. $policy
        $workers=if($role -ceq 'parallel-counterfactual'){'/m:2'}else{$workerArgument}
        & $NativeMsBuildPath $driver '/nologo' '/noautoresponse' '/nr:false' '/v:minimal' $workers *> (Join-Path $run 'msbuild.log')
        $code=$LASTEXITCODE
        if($role -ceq 'parallel-counterfactual'){
            if($code -eq 0 -or [IO.File]::ReadAllText((Join-Path $run 'msbuild.log')) -notmatch 'MSB3021'){throw 'Parallel writer counterfactual did not detect the controlled sharing conflict.'}
        }elseif($code -ne 0 -or [IO.File]::ReadAllText($destination) -cne [IO.File]::ReadAllText($source)){throw 'Actual ASan project scheduling lost copied bytes or failed the same writer workload.'}
    }
    Write-Host ('PASS controlled parallel writer refusal and serial actual-policy copy; retained '+$fixture)
}
$global:LASTEXITCODE=0
