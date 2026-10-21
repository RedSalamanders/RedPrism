# A documentation closeout retains the original successful native qualification; it is
# never described as execution or as a new resource/clipboard/environment observation.
[CmdletBinding()]
param([string]$VerifyInputs='',[int]$BuildNumber=-1)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot
Import-Module (Join-Path $root 'Build/ScopedTesting.psm1') -Force
$manifest=Read-ScopedTestManifest $root
$event=$null
if($env:GITHUB_EVENT_PATH -and (Test-Path -LiteralPath $env:GITHUB_EVENT_PATH)){$event=Get-Content -LiteralPath $env:GITHUB_EVENT_PATH -Raw|ConvertFrom-Json}
if($BuildNumber -lt 0){
    $BuildNumber=0
    if($manifest.project -ceq 'RedPrism'){
        $base=if($event -and $event.PSObject.Properties['pull_request']){$event.pull_request.base.sha}else{'HEAD'}
        $BuildNumber=[int](& git -C $root rev-list --count $base)
        if($LASTEXITCODE){throw 'Could not resolve the native version-stamp input.'}
    }
}
$key=Get-ScopedDigest ((Get-ScopedSourceIdentity $root -CompiledOnly)+"`nci-native-v1`nbuild-number=$BuildNumber")
if($VerifyInputs){if($VerifyInputs -cne $key){throw 'Native checkout/build inputs differ from the reviewed CI scope descriptor.'};Write-Host 'Native source/build inputs match the CI descriptor.';exit 0}
$fresh=$true;$proof='';$image=[string]$env:ImageVersion
$directory=Join-Path $root '.build/ci-scope';[void](New-Item -ItemType Directory -Path $directory -Force)
$expected=@($manifest.prCoverage|Where-Object{$_.PSObject.Properties['platform']}|ForEach-Object check)
if($env:GITHUB_EVENT_NAME -eq 'pull_request' -and $env:GH_TOKEN -and $event){
    try{
        $branch=[Uri]::EscapeDataString($event.pull_request.head.ref)
        $runs=Invoke-ScopedGhApi "repos/$($manifest.repository)/actions/workflows/ci.yml/runs?event=pull_request&branch=$branch&status=completed&per_page=5"
        foreach($run in $runs.workflow_runs){
            if([string]$run.id -ceq [string]$env:GITHUB_RUN_ID){continue}
            $prior=Join-Path $directory ('prior-'+$run.id)
            $global:LASTEXITCODE=0
            & gh run download ([string]$run.id) --repo $manifest.repository --name ci-native-inputs --dir $prior *> $null
            if($LASTEXITCODE){break} # Uncertainty never falls back to an older pass.
            $descriptor=Get-Content -LiteralPath (Join-Path $prior 'ci-native-inputs.json') -Raw|ConvertFrom-Json
            if($descriptor.version -ne 1 -or $descriptor.repository -cne $manifest.repository -or
                $descriptor.sourceHead -cne $run.head_sha){break}
            if($descriptor.inputKey -cne $key -or $descriptor.imageVersion -cne $image){continue}
            $producer=if($descriptor.fresh){[string]$run.id}else{[string]$descriptor.proofRun}
            if($producer -notmatch '^\d+$'){break}
            if($producer -cne [string]$run.id){
                $raw=Join-Path $directory ('producer-'+$producer)
                & gh run download $producer --repo $manifest.repository --name ci-native-inputs --dir $raw *> $null
                if($LASTEXITCODE){break}
                $original=Get-Content -LiteralPath (Join-Path $raw 'ci-native-inputs.json') -Raw|ConvertFrom-Json
                $producerRun=Invoke-ScopedGhApi "repos/$($manifest.repository)/actions/runs/$producer"
                if($original.version -ne 1 -or -not $original.fresh -or $original.inputKey -cne $key -or
                    $original.repository -cne $manifest.repository -or $original.imageVersion -cne $image -or
                    $producerRun.workflow_id -ne $run.workflow_id -or $producerRun.event -cne 'pull_request' -or
                    $producerRun.head_sha -cne $original.sourceHead){break}
            }
            $jobs=Invoke-ScopedGhApi "repos/$($manifest.repository)/actions/runs/$producer/jobs?per_page=100"
            if(Test-ScopedNativeCiProof $expected @($jobs.jobs)){$fresh=$false;$proof=$producer}
            break # A missing/failed/cancelled latest native attempt cannot be hidden by an older green run.
        }
    }catch [System.Management.Automation.RuntimeException]{Write-Host "Native evidence lookup unavailable; run the full matrix: $($_.Exception.Message)"}
    catch [ArgumentException]{Write-Host 'Malformed native evidence; run the full matrix.'}
}
$sourceHead=if($event -and $event.PSObject.Properties['pull_request']){$event.pull_request.head.sha}else{$env:GITHUB_SHA}
$descriptor=[ordered]@{version=1;repository=$manifest.repository;sourceHead=$sourceHead;inputKey=$key;buildNumber=$BuildNumber;imageVersion=$image;fresh=$fresh;proofRun=$proof}
[IO.File]::WriteAllText((Join-Path $directory 'ci-native-inputs.json'),($descriptor|ConvertTo-Json),[Text.UTF8Encoding]::new($false))
if($env:GITHUB_OUTPUT){@("native=$($fresh.ToString().ToLowerInvariant())","proof_run=$proof","build_number=$BuildNumber","input_key=$key")|Add-Content -LiteralPath $env:GITHUB_OUTPUT -Encoding utf8}
if($fresh){Write-Host 'Native inputs have no matching complete successful qualification; run all six profiles.'}
else{Write-Host "NATIVE_UNCHANGED: retain source/profile qualification from run $proof. No new native/resource/clipboard/environment execution is claimed."}
