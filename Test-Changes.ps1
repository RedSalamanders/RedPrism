<#
.SYNOPSIS
Explains and runs affected tests; unchanged successful evidence is reused exactly.
.DESCRIPTION
The first affected run uses the branch merge base plus independent staged, working,
deleted, renamed and untracked discovery. A successful default affected run records
the worktree/index baseline for the next edit. Partial explicit selections, failures,
mutations and pending CI never advance that baseline. Full and PrePush remain explicit.
#>
[CmdletBinding()]
param(
    [ValidateSet('Affected','Full','PrePush')][string]$Mode='Affected',
    [string]$BaseRef='', [string[]]$Scopes=@(),
    [ValidateSet('x64','ARM64')][string]$Platform='x64',
    [ValidateSet('Debug','Release','ASan Debug')][string]$Configuration='Debug',
    [switch]$Explain,[switch]$Force,[switch]$SkipBuild
)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
# Pending fragments are expected: they are folded on the default branch after merge. CHANGELOG.md may change only by such a fold.
if (-not $Explain -and $Mode -eq 'PrePush') { & (Join-Path $PSScriptRoot 'Tools/Test-Changelog.ps1') -Base $(if ($BaseRef) { $BaseRef } else { 'origin/main' }) }
$root=$PSScriptRoot;$module=Join-Path $root 'Build/ScopedTesting.psm1'
Import-Module $module -Force
$iterationLease=if(-not $Explain){Enter-ScopedIterationLease $root $Platform $Configuration}else{$null}
try {
$manifest=Read-ScopedTestManifest $root
$Scopes=@($Scopes|ForEach-Object{$_ -split ','}|ForEach-Object{$_.Trim()}|Where-Object{$_})
if($Scopes.Count -and $Mode -ne 'Affected'){throw '-Scopes is only valid for explicit Affected iteration.'}
if(-not $BaseRef){$BaseRef='origin/'+$manifest.defaultBranch}
$reports=Join-Path $root ".build/reports/scoped-tests/$Platform-$($Configuration -replace ' ','')"
$iterationPath=Join-Path $reports 'iteration.json'
$source=Get-ScopedSourceMap $root;$index=Get-ScopedIndexMap $root
$environment=Get-ScopedEnvironmentIdentity
$artifact='absent'
if(Test-Path -LiteralPath (Join-Path $root ".build/$Platform/$Configuration") -PathType Container){
    try{$artifact=Get-ScopedArtifactIdentity $root $Platform $Configuration}
    catch [System.Management.Automation.RuntimeException]{$artifact='incomplete'}
}
$changed=@();$basis='explicit scope/full obligation';$baseline=$null;$forceFull=$false
if($Mode -eq 'Affected' -and -not $Scopes.Count){
    # Always prove that the requested branch comparison still resolves; a rebase or explicit
    # base override must not silently use an unrelated saved baseline.
    $branchPaths=@(Get-ScopedChangedPaths $root $BaseRef)
    $baseline=Read-ScopedIteration $iterationPath $root $Platform $Configuration $environment
    if($baseline -and -not $Force -and -not $PSBoundParameters.ContainsKey('BaseRef')){
        $changed=@(@(Get-ScopedMapDelta $baseline.source $source)+@(Get-ScopedPendingIndexDelta $root $baseline.index $index)|Sort-Object -Unique)
        $basis='last successful default affected run (working bytes and index)'
        if(-not $changed.Count -and $artifact -cne $baseline.artifact){$forceFull=$true;$basis='unexplained artifact change since the last successful iteration'}
    }else{$changed=$branchPaths;$basis="branch merge base with $BaseRef (no matching iteration baseline)"}
}
Write-Host "Native test naming: $(Assert-ScopedTestNames $root) files checked."
$plan=Get-ScopedTestPlan -Manifest $manifest -ChangedPaths $changed -Scopes $Scopes -Full:($Mode -ne 'Affected' -or $forceFull)
$applicable=@($manifest.scopes|Where-Object{-not $_.PSObject.Properties['configurations'] -or $Configuration -in $_.configurations})
foreach($scope in $Scopes){if($scope -notin $applicable.name){throw "Scope '$scope' does not run in $Configuration."}}
$names=@($plan.scopes|Where-Object{$_ -in $applicable.name})
Write-Host "Test plan: $Mode ($Platform $Configuration); baseline: $basis; scopes: $($names -join ', ')"
foreach($reason in $plan.reasons){Write-Host "  $($reason.path): $($reason.reason) => $($reason.scopes -join ', ')"}
$refusal='';$deferred=@(if($Mode -eq 'PrePush'){Get-ScopedPrCoverage $root $manifest $Platform $Configuration -Refusal ([ref]$refusal)})
if($deferred.Count){Write-Host "CI_PENDING (not merge approval): $($deferred -join ', ')"}
if($refusal){Write-Host "CI policy: $refusal" -ForegroundColor Yellow}
if($Explain){exit 0}
$selected=@($applicable|Where-Object{$_.name -in $names -and $_.name -notin $deferred})
if(-not $selected.Count){
    if($deferred.Count){Write-Host 'LOCAL_OBLIGATIONS_NONE; CI_PENDING'}else{Write-Host 'NOTHING_SELECTED; repository NOT_EVALUATED'}
    exit 0
}
$native=@($selected|Where-Object native)
$previousIteration=Suspend-ScopedIteration $iterationPath $root
$buildOptions=''
if($native.Count){
    Assert-ScopedRuntimePlatform $Platform
    $compiledBefore=Get-ScopedSourceIdentity $root -CompiledOnly
    $buildArguments=@{Platform=$Platform;Configuration=$Configuration}
    if($manifest.project -ceq 'RedPrism'){
        $number=Get-ScopedBuildNumber $root $BaseRef
        $buildArguments.BuildNumber=$number;$buildOptions="build-number=$number"
    }
    $buildKey=Get-ScopedDigest ($compiledBefore+"`n"+$artifact+"`n"+$buildOptions+"`n"+$environment)
    $attestation=Join-Path $reports 'build.json'
    if($SkipBuild){
        if(-not(Test-ScopedReceipt $attestation $buildKey)){throw 'SkipBuild refused: source, artifacts, toolchain/environment or build options changed. Run without -SkipBuild.'}
    }else{
        $global:LASTEXITCODE=0
        & (Join-Path $root 'build.ps1') @buildArguments
        if(-not $? -or $LASTEXITCODE -ne 0){throw "Build failed: $LASTEXITCODE"}
        Import-Module $module -Force
        if((Get-ScopedSourceIdentity $root -CompiledOnly) -cne $compiledBefore){throw 'Source changed during build; no attestation was published.'}
        $artifact=Get-ScopedArtifactIdentity $root $Platform $Configuration
        Write-ScopedReceipt $attestation (Get-ScopedDigest ($compiledBefore+"`n"+$artifact+"`n"+$buildOptions+"`n"+$environment))
    }
}
$pending=[Collections.Generic.List[object]]::new();$identities=@{}
$nativeKey=if($native.Count){Get-ScopedRunIdentity $root $Platform $Configuration '(common)' $buildOptions}else{''}
$formatKey=Get-ScopedDigest ((Get-ScopedSourceIdentity $root -CompiledOnly)+"`n"+(Get-ScopedEnvironmentIdentity -Tooling))
$toolingKey=Get-ScopedDigest ((Get-ScopedSourceIdentity $root)+"`n"+(Get-ScopedEnvironmentIdentity -Tooling))
function Get-ReceiptPath($Scope){
    if($Scope.native){Join-Path $reports ($Scope.name+'.json')}
    else{Join-Path $root ('.build/reports/scoped-tests/independent/'+$Scope.name+'.json')}
}
foreach($scope in $selected){
    $key=Get-ScopedDigest ($(if($scope.native){$nativeKey}elseif($scope.name -ceq 'Format'){$formatKey}else{$toolingKey})+"`n"+$scope.name)
    $identities[$scope.name]=$key;$receipt=Get-ReceiptPath $scope
    if(-not $Force -and $scope.reuse -and (Test-ScopedReceipt $receipt $key)){Write-Host "REUSED $($scope.name) (identical successful evidence)"}
    else{if(Test-Path -LiteralPath $receipt){Remove-Item -LiteralPath $receipt};$pending.Add($scope)}
}
if($pending.Count){
    if(@($pending|Where-Object{-not $_.native}).Count){
        foreach($scope in @($pending|Where-Object{-not $_.native})){
            foreach($command in $scope.commands){
                $commandArguments=@($command.arguments)
                $engine=Join-Path $PSHOME $(if($IsWindows){'pwsh.exe'}else{'pwsh'})
                $invocationArguments=@('-NoProfile','-File',(Join-Path $root $command.path))+$commandArguments
                $global:LASTEXITCODE=0;& $engine @invocationArguments
                if(-not $? -or $LASTEXITCODE -ne 0){throw "Independent scope failed: $($scope.name) ($LASTEXITCODE)"}
            }
        }
    }
    $runtime=@($pending|Where-Object native|ForEach-Object name)
    if($runtime.Count){
        $global:LASTEXITCODE=0
        & (Join-Path $root 'test.ps1') -Platform $Platform -Configuration $Configuration -Suites $runtime -SkipBuild -SkipTooling
        if(-not $? -or $LASTEXITCODE -ne 0){throw "Selected runtime tests failed: $LASTEXITCODE"}
    }
    Import-Module $module -Force
    $afterNative=if($runtime.Count){Get-ScopedRunIdentity $root $Platform $Configuration '(common)' $buildOptions}else{$nativeKey}
    $afterFormat=Get-ScopedDigest ((Get-ScopedSourceIdentity $root -CompiledOnly)+"`n"+(Get-ScopedEnvironmentIdentity -Tooling))
    $afterTooling=Get-ScopedDigest ((Get-ScopedSourceIdentity $root)+"`n"+(Get-ScopedEnvironmentIdentity -Tooling))
    foreach($scope in $pending){
        $after=Get-ScopedDigest ($(if($scope.native){$afterNative}elseif($scope.name -ceq 'Format'){$afterFormat}else{$afterTooling})+"`n"+$scope.name)
        if($after -cne $identities[$scope.name]){throw "Inputs changed during $($scope.name); no reusable success was published."}
    }
    foreach($scope in $pending){if($scope.reuse){Write-ScopedReceipt (Get-ReceiptPath $scope) $identities[$scope.name]}}
}
if((Get-ScopedMapIdentity (Get-ScopedSourceMap $root)) -cne (Get-ScopedMapIdentity $source) -or
    (Get-ScopedMapIdentity (Get-ScopedIndexMap $root)) -cne (Get-ScopedMapIdentity $index) -or
    (Get-ScopedEnvironmentIdentity) -cne $environment){throw 'Inputs/index/environment changed during the run; the iteration baseline was not advanced.'}
if($Mode -eq 'Affected' -and -not $Scopes.Count -and -not $deferred.Count){
    Write-ScopedIteration $iterationPath $root $Platform $Configuration $environment $source $index $artifact
}elseif(-not $deferred.Count){
    Restore-ScopedIteration $iterationPath $root $previousIteration
}
if($deferred.Count){Write-Host 'LOCAL_OBLIGATIONS_PASSED; CI_PENDING'}
elseif($names.Count -eq $applicable.Count){Write-Host 'FULL_NONINTERACTIVE_PASSED; manual/platform/resource qualification remains separate'}
else{Write-Host 'SELECTED_PASSED; repository NOT_EVALUATED'}
exit 0
}finally{if($iterationLease){try{$iterationLease.ReleaseMutex()}finally{$iterationLease.Dispose()}}}
