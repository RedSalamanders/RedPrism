# Git/content/process fixtures exercise the public runner without a native build or desktop use.
[CmdletBinding()]param()
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot
Import-Module (Join-Path $root 'Build/ScopedTesting.psm1') -Force
$manifest=Read-ScopedTestManifest $root
$fixture=Join-Path $root ('.build/ToolTests/scoped-'+[guid]::NewGuid().ToString('N'))
$passed=0
function Assert([bool]$Value,[string]$Message){if(-not $Value){throw $Message}}
function Case([string]$Name,[scriptblock]$Body){&$Body;$script:passed++;Write-Host "PASS $Name"}
function Put([string]$Path,[string]$Text){[void](New-Item -ItemType Directory -Path (Split-Path $Path) -Force);[IO.File]::WriteAllText($Path,$Text,[Text.UTF8Encoding]::new($false))}
function Invoke-FixtureGit([string]$Directory,[string[]]$Arguments){& git -C $Directory @Arguments *> $null;if($LASTEXITCODE){throw "Fixture Git failed: $Arguments"}}
function New-Repository([string]$Directory){
    [void](New-Item -ItemType Directory -Path $Directory -Force)
    Put (Join-Path $Directory '.gitignore') ".build/`n"
    Invoke-FixtureGit $Directory @('init','-q','-b','main');Invoke-FixtureGit $Directory @('config','user.name','Scope fixture');Invoke-FixtureGit $Directory @('config','user.email','fixture@example.invalid')
}
try{
    Case 'active native naming and inventory are contextual and complete' {
        Assert ((Assert-ScopedTestNames $root) -gt 0) 'Empty inventory'
        $paths=@(Get-Content (Join-Path $root $manifest.nativeInventory) -Raw|ConvertFrom-Json)
        if($manifest.project -ceq 'RedTilde'){$paths=@($paths[0].files.path)}
        Assert (@($paths|Where-Object{$_ -match '(^|/)Scope\.Tests\.'}).Count -eq 0) 'Placeholder Scope was retained instead of a context'
    }
    Case 'unknown executable inputs widen even though formatting also matches' {
        foreach($path in @('Unmapped/New.cpp','Unmapped/New.h','Specs/Unmapped.schema.json','NewGenerator.inl')){
            Assert ((Get-ScopedTestPlan $manifest @($path)).full) "Unmapped input narrowed coverage: $path"
        }
    }
    Case 'prose omits native tests, embedded Markdown does not, and empty changes select nothing' {
        $plan=Get-ScopedTestPlan $manifest @('README.md','CHANGELOG.md')
        Assert (@($manifest.scopes|Where-Object{$_.native -and $_.name -in $plan.scopes}).Count -eq 0) 'Prose selected native tests'
        Assert ((Get-ScopedTestPlan $manifest @('res/Welcome.md')).full) 'Embedded content mistaken for prose'
        Assert ((Get-ScopedTestPlan $manifest @()).scopes.Count -eq 0) 'Empty changes selected tests'
    }
    Case 'actual consumers and standalone test owners are selected' {
        if($manifest.project -ceq 'RedPrism'){
            $plan=Get-ScopedTestPlan $manifest @('Common/Jobs.cpp')
            foreach($consumer in @('Host','Runtime')){Assert ($consumer -in $plan.scopes) "Job consumer omitted: $consumer"}
            $test=@(Get-Content (Join-Path $root $manifest.nativeInventory) -Raw|ConvertFrom-Json)|Where-Object{$_ -like 'Tests/HostTests/*.h'}|Select-Object -First 1
            $plan=Get-ScopedTestPlan $manifest @($test)
            Assert ('Host' -in $plan.scopes -and 'IO' -notin $plan.scopes) 'A standalone Host helper ran unrelated IO'
        }else{
            $plan=Get-ScopedTestPlan $manifest @('Common/Storage.cpp')
            foreach($consumer in @('Storage','Recovery','RecoveryRestart')){Assert ($consumer -in $plan.scopes) "Storage consumer omitted: $consumer"}
            Assert ('Captures' -notin $plan.scopes -and 'Editor' -notin $plan.scopes) 'Storage-only edit selected unrelated native render cases'
            $plan=Get-ScopedTestPlan $manifest @('Tests/Application.Tests.Performance.cpp')
            Assert ('Editor' -in $plan.scopes -and 'Performance' -in $plan.scopes) 'Native performance helper lost its behavioral consumer'
            $catalog=Get-Content (Join-Path $root 'Tests/native-case-catalog.json') -Raw|ConvertFrom-Json
            $declared=@([regex]::Matches([IO.File]::ReadAllText((Join-Path $root 'Tests/Application.Tests.Core.cpp')),'\btest\("([^"]+)"')|ForEach-Object{$_.Groups[1].Value})
            Assert (-not(Compare-Object $declared @($catalog.cases.name))) 'Reviewed native catalog differs from source'
            Assert (@($catalog.cases.name|Sort-Object -Unique).Count -eq $declared.Count) 'Duplicate native case'
        }
    }
    Case 'invalid selectors and absolute/traversing paths are rejected' {
        foreach($action in @({Get-ScopedTestPlan $manifest @() @('Unknown')},{Get-ScopedTestPlan $manifest @('../code.cpp')},{Get-ScopedTestPlan $manifest @('C:/code.cpp')})){
            $threw=$false;try{&$action|Out-Null}catch [System.Management.Automation.RuntimeException]{$threw=$true}
            Assert $threw 'Invalid selector/path accepted'
        }
    }
    $gitRoot=Join-Path $fixture 'discovery';New-Repository $gitRoot
    foreach($name in @('code.cpp','staged.cpp','deleted.cpp','rename 東京.cpp')){Put (Join-Path $gitRoot $name) 'initial'}
    Invoke-FixtureGit $gitRoot @('add','-A');Invoke-FixtureGit $gitRoot @('commit','-qm','baseline')
    $base=(& git -C $gitRoot rev-parse HEAD).Trim()
    Put (Join-Path $gitRoot 'committed.cpp') 'committed';Invoke-FixtureGit $gitRoot @('add','-A');Invoke-FixtureGit $gitRoot @('commit','-qm','candidate')
    Put (Join-Path $gitRoot 'staged.cpp') 'staged';Invoke-FixtureGit $gitRoot @('add','staged.cpp');Put (Join-Path $gitRoot 'staged.cpp') 'initial'
    Put (Join-Path $gitRoot 'code.cpp') 'working';Remove-Item -LiteralPath (Join-Path $gitRoot 'deleted.cpp')
    Move-Item -LiteralPath (Join-Path $gitRoot 'rename 東京.cpp') -Destination (Join-Path $gitRoot 'renamed 東京.cpp')
    Put (Join-Path $gitRoot 'untracked space.cpp') 'new'
    Case 'committed/staged/working/deleted/rename sides/Unicode/untracked are independently discovered' {
        $paths=@(Get-ScopedChangedPaths $gitRoot $base)
        foreach($name in @('committed.cpp','staged.cpp','code.cpp','deleted.cpp','rename 東京.cpp','renamed 東京.cpp','untracked space.cpp')){Assert ($name -cin $paths) "Missing change: $name"}
    }
    Case 'missing comparison refs fail with a full-run remedy' {
        $message='';try{Get-ScopedChangedPaths $gitRoot missing-ref|Out-Null}catch [System.Management.Automation.RuntimeException]{$message=$_.Exception.Message}
        Assert ($message -match 'BaseRef|full gate|fetch') 'Missing base silently selected nothing'
    }
    $mini=[ordered]@{version=1;project='Fixture';repository='fixture/example';defaultBranch='main';nativeInventory='Tests/native-test-files.json';runtimeFiles=@('input.json');
        scopes=@(@{name='Example';native=$true;reuse=$true});rules=@();toolingCommands=@();prCoverage=@();prWorkflowDigest='none'}
    Put (Join-Path $gitRoot 'Tests/test-scopes.json') ($mini|ConvertTo-Json -Depth 6)
    foreach($testConfiguration in @('Debug','Release')){Put (Join-Path $gitRoot ".build/x64/$testConfiguration/example.exe") 'binary';Put (Join-Path $gitRoot ".build/x64/$testConfiguration/input.json") 'runtime'}
    Case 'native identity changes with source/binary/runtime/profile/environment' {
        $before=Get-ScopedRunIdentity $gitRoot x64 Debug Example
        Put (Join-Path $gitRoot '.build/x64/Debug/example.exe') 'changed binary'
        Assert ((Get-ScopedRunIdentity $gitRoot x64 Debug Example) -cne $before) 'Binary mutation omitted'
        Put (Join-Path $gitRoot '.build/x64/Debug/example.exe') 'binary';Put (Join-Path $gitRoot '.build/x64/Debug/input.json') 'changed runtime'
        Assert ((Get-ScopedRunIdentity $gitRoot x64 Debug Example) -cne $before) 'Runtime mutation omitted'
        Put (Join-Path $gitRoot '.build/x64/Debug/input.json') 'runtime'
        Assert ((Get-ScopedRunIdentity $gitRoot x64 Release Example) -cne $before) 'Profile omitted'
        $old=$env:ASAN_OPTIONS
        try{$env:ASAN_OPTIONS='scope-fixture=changed';Assert ((Get-ScopedRunIdentity $gitRoot x64 Debug Example) -cne $before) 'Sanitizer environment omitted'}finally{$env:ASAN_OPTIONS=$old}
        Put (Join-Path $gitRoot 'code.cpp') 'changed source';Assert ((Get-ScopedRunIdentity $gitRoot x64 Debug Example) -cne $before) 'Source omitted'
    }
    Case 'missing required runtime inputs and JSON-only profiles fail before identity creation' {
        Remove-Item -LiteralPath (Join-Path $gitRoot '.build/x64/Debug/input.json')
        $message='';try{Get-ScopedArtifactIdentity $gitRoot x64 Debug|Out-Null}catch [System.Management.Automation.RuntimeException]{$message=$_.Exception.Message}
        Assert ($message -like 'Missing deployed runtime input:*') 'Missing initial runtime closure accepted'
        Put (Join-Path $gitRoot '.build/x64/Debug/input.json') 'runtime';Remove-Item -LiteralPath (Join-Path $gitRoot '.build/x64/Debug/example.exe')
        $message='';try{Get-ScopedArtifactIdentity $gitRoot x64 Debug|Out-Null}catch [System.Management.Automation.RuntimeException]{$message=$_.Exception.Message}
        Assert ($message -eq 'No executable build artifacts were found.') 'JSON-only profile accepted'
        Put (Join-Path $gitRoot '.build/x64/Debug/example.exe') 'binary'
    }
    Case 'only intact identical successful receipts are reusable' {
        $path=Join-Path $gitRoot '.build/pass.json';$key=Get-ScopedDigest 'identity'
        Assert (-not(Test-ScopedReceipt $path $key)) 'Missing receipt reused'
        Write-ScopedReceipt $path $key;Assert (Test-ScopedReceipt $path $key) 'Identical pass not reused'
        Assert (-not(Test-ScopedReceipt $path (Get-ScopedDigest 'changed'))) 'Wrong identity reused'
        Put $path '{broken';Assert (-not(Test-ScopedReceipt $path $key)) 'Corrupt receipt reused'
        Write-ScopedReceipt $path $key;$record=Get-Content $path -Raw|ConvertFrom-Json;$record.outcome='FAILED';Put $path ($record|ConvertTo-Json)
        Assert (-not(Test-ScopedReceipt $path $key)) 'Failed receipt reused'
    }
    Case 'compiled identity excludes prose but includes embedded Markdown and inline generators' {
        $native=Get-ScopedSourceIdentity $gitRoot -CompiledOnly;$all=Get-ScopedSourceIdentity $gitRoot
        Put (Join-Path $gitRoot 'README.md') 'prose'
        Assert ((Get-ScopedSourceIdentity $gitRoot -CompiledOnly) -ceq $native) 'Prose invalidated compilation'
        Assert ((Get-ScopedSourceIdentity $gitRoot) -cne $all) 'Validator prose omitted'
        Put (Join-Path $gitRoot 'res/Welcome.md') 'embedded content'
        Assert ((Get-ScopedSourceIdentity $gitRoot -CompiledOnly) -cne $native) 'Embedded Markdown omitted'
        $native=Get-ScopedSourceIdentity $gitRoot -CompiledOnly;Put (Join-Path $gitRoot 'generator.inl') 'new generator'
        Assert ((Get-ScopedSourceIdentity $gitRoot -CompiledOnly) -cne $native) 'New executable input extension omitted'
    }
    Case 'iteration state is source/index/profile/environment bound and rejects corruption' {
        $path=Join-Path $gitRoot '.build/iteration.json';$source=Get-ScopedSourceMap $gitRoot;$index=Get-ScopedIndexMap $gitRoot
        Write-ScopedIteration $path $gitRoot x64 Debug environment $source $index artifact
        Assert ($null -ne (Read-ScopedIteration $path $gitRoot x64 Debug environment)) 'Valid iteration rejected'
        Assert ($null -eq (Read-ScopedIteration $path $gitRoot x64 Release environment)) 'Wrong profile accepted'
        Assert ($null -eq (Read-ScopedIteration $path $gitRoot x64 Debug changed)) 'Wrong environment accepted'
        Put $path '{}';Assert ($null -eq (Read-ScopedIteration $path $gitRoot x64 Debug environment)) 'Corrupt state accepted'
    }
    Case 'CI covers six exact native profiles and tooling once with no duplicate main-push run' {
        $workflow=[IO.File]::ReadAllText((Join-Path $root '.github/workflows/ci.yml')) -replace "`r`n","`n"
        Assert ((Get-ScopedDigest $workflow) -ceq $manifest.prWorkflowDigest) 'Workflow contract digest stale'
        Assert ($workflow.Contains('test.ps1 -Full -SkipTooling') -and $workflow.Contains('platform: [x64, ARM64]')) 'Full native matrix narrowed'
        Assert ($workflow -notmatch '(?m)^  push:') 'Identical merged tree automatically repeats the PR matrix'
        Assert ((@($manifest.prCoverage|Where-Object{$_.PSObject.Properties['platform']})).Count -eq 6) 'Missing native profile contracts'
        foreach($platform in @('x64','ARM64')){foreach($configuration in @('Debug','Release','ASan Debug')){
            $declared=@($manifest.prCoverage|Where-Object{$_.PSObject.Properties['platform'] -and $_.platform -eq $platform -and $_.configuration -eq $configuration})
            Assert ($declared.Count -eq 1) "Profile duplicated/omitted: $platform $configuration"
        }}
    }
    Case 'live project/include consumers follow added dependencies and terminate include cycles' {
        $graph=Join-Path $fixture 'graph'
        Put (Join-Path $graph 'host.vcxproj') '<Project><ItemGroup><ClCompile Include="host.cpp"/></ItemGroup></Project>'
        Put (Join-Path $graph 'document.vcxproj') '<Project><ItemGroup><ClCompile Include="document.cpp"/></ItemGroup></Project>'
        Put (Join-Path $graph 'host.cpp') 'int host;'
        Put (Join-Path $graph 'document.cpp') '#include "shared.h"'
        Put (Join-Path $graph 'shared.h') '#include "cycle.h"'
        Put (Join-Path $graph 'cycle.h') '#include "shared.h"'
        New-Repository $graph
        $bindings=@([pscustomobject]@{path='host.vcxproj';scope='Host'},[pscustomobject]@{path='document.vcxproj';scope='Document'})
        $before=@(Get-ScopedProjectRules $graph $bindings|Where-Object pattern -ceq 'shared.h')
        Assert ('Document' -in $before.scopes -and 'Host' -notin $before.scopes) 'Initial include graph is wrong'
        Put (Join-Path $graph 'host.cpp') '#include "shared.h"'
        $after=@(Get-ScopedProjectRules $graph $bindings|Where-Object pattern -ceq 'shared.h')
        Assert ('Document' -in $after.scopes -and 'Host' -in $after.scopes) 'New native caller was omitted after adding an include'
        Put (Join-Path $graph 'host.cpp') '#include SELECTED_HEADER'
        Assert (@(Get-ScopedProjectRules $graph $bindings|Where-Object{$_.pattern -ceq '**' -and '*' -in $_.scopes}).Count -eq 1) 'Macro include silently narrowed coverage'
    }
    $gate=Join-Path $fixture 'gate';New-Repository $gate
    $workflow="name: fixture`non:`n  pull_request:`n"
    Put (Join-Path $gate '.github/workflows/ci.yml') $workflow
    Put (Join-Path $gate 'code.cpp') 'baseline'
    Invoke-FixtureGit $gate @('add','-A');Invoke-FixtureGit $gate @('commit','-qm','baseline');Invoke-FixtureGit $gate @('update-ref','refs/remotes/origin/main','HEAD');Invoke-FixtureGit $gate @('switch','-qc','candidate')
    Put (Join-Path $gate 'code.cpp') 'candidate';Invoke-FixtureGit $gate @('add','code.cpp');Invoke-FixtureGit $gate @('commit','-qm','candidate')
    $coverage=[pscustomobject]@{repository='fixture/example';defaultBranch='main';toolingCommands=@();prWorkflowDigest=(Get-ScopedDigest $workflow);
        scopes=@([pscustomobject]@{name='Example';native=$true});prCoverage=@([pscustomobject]@{check='native';platform='x64';configuration='Release';scopes=@('Example')})}
    $previousGh=Get-Item Function:\global:gh -ErrorAction SilentlyContinue
    $global:ScopedGateCalls=0;$global:ScopedGateRoot=$gate;$global:ScopedGateMutation='';$global:ScopedGateState='active';$global:ScopedGateError=$false
    function global:gh {
        param([Parameter(ValueFromRemainingArguments=$true)][string[]]$FixtureArguments)
        $global:ScopedGateCalls++;$global:LASTEXITCODE=0
        if($global:ScopedGateError){$global:LASTEXITCODE=1;return}
        if($global:ScopedGateMutation -eq 'working'){[IO.File]::WriteAllText((Join-Path $global:ScopedGateRoot 'during-api.cpp'),'changed')}
        if($global:ScopedGateMutation -eq 'commit'){
            [IO.File]::WriteAllText((Join-Path $global:ScopedGateRoot 'code.cpp'),'concurrent commit')
            & git -C $global:ScopedGateRoot -c commit.gpgsign=false add code.cpp *> $null
            & git -C $global:ScopedGateRoot -c commit.gpgsign=false commit -qm 'concurrent commit' *> $null
        }
        '{"state":"'+$global:ScopedGateState+'"}'
    }
    try{
        Case 'only a clean committed candidate can delegate matching enabled CI, always as pending' {
            Assert ('Example' -in @(Get-ScopedPrCoverage $gate $coverage x64 Release)) 'Clean reviewed candidate not delegated'
            foreach($state in @('untracked','working','staged')){
                $path=Join-Path $gate $(if($state -eq 'untracked'){'pending.cpp'}else{'code.cpp'})
                Put $path 'pending'
                if($state -eq 'staged'){Invoke-FixtureGit $gate @('add','code.cpp')}
                $calls=$global:ScopedGateCalls
                Assert (@(Get-ScopedPrCoverage $gate $coverage x64 Release).Count -eq 0) "Dirty candidate delegated: $state"
                Assert ($global:ScopedGateCalls -eq $calls) 'Dirty candidate queried GitHub before retaining local work'
                if($state -eq 'untracked'){Remove-Item -LiteralPath $path}else{Invoke-FixtureGit $gate @('restore','--staged','--worktree','--','code.cpp')}
            }
        }
        Case 'disabled/unavailable CI, stale workflow and mismatched profiles keep obligations local' {
            $global:ScopedGateState='disabled_manually';Assert (@(Get-ScopedPrCoverage $gate $coverage x64 Release).Count -eq 0) 'Disabled workflow delegated'
            $global:ScopedGateState='active';$global:ScopedGateError=$true;Assert (@(Get-ScopedPrCoverage $gate $coverage x64 Release).Count -eq 0) 'API failure delegated';$global:ScopedGateError=$false
            Assert (@(Get-ScopedPrCoverage $gate $coverage ARM64 Release).Count -eq 0) 'Unsupported profile delegated'
            $digest=$coverage.prWorkflowDigest;$coverage.prWorkflowDigest='stale'
            Assert (@(Get-ScopedPrCoverage $gate $coverage x64 Release).Count -eq 0) 'Unreviewed workflow delegated';$coverage.prWorkflowDigest=$digest
        }
        Case 'working and clean-commit races during coverage lookup refuse delegation' {
            $global:ScopedGateMutation='working'
            Assert (@(Get-ScopedPrCoverage $gate $coverage x64 Release).Count -eq 0) 'Concurrent working edit delegated'
            Remove-Item -LiteralPath (Join-Path $gate 'during-api.cpp')
            $global:ScopedGateMutation='commit'
            Assert (@(Get-ScopedPrCoverage $gate $coverage x64 Release).Count -eq 0) 'Concurrent clean commit delegated'
            $global:ScopedGateMutation=''
        }
        Case 'direct default-branch pushes have no forthcoming PR coverage' {
            Invoke-FixtureGit $gate @('switch','-q','main')
            Assert (@(Get-ScopedPrCoverage $gate $coverage x64 Release).Count -eq 0) 'Default branch push incorrectly assumed a PR'
        }
    }finally{
        if($previousGh){Set-Item Function:\global:gh -Value $previousGh.ScriptBlock}else{Remove-Item Function:\global:gh}
        Remove-Variable -Name ScopedGateCalls,ScopedGateRoot,ScopedGateMutation,ScopedGateState,ScopedGateError -Scope Global
    }

    Case 'retained CI proof requires all six actual successful native executions' {
        $expected=@($manifest.prCoverage|Where-Object{$_.PSObject.Properties['platform']}|ForEach-Object check)
        $jobs=@($expected|ForEach-Object{[pscustomobject]@{name=$_;status='completed';conclusion='success';steps=@([pscustomobject]@{name='Full native qualification';conclusion='success'})}})
        Assert (Test-ScopedNativeCiProof $expected $jobs) 'Complete native matrix rejected'
        foreach($outcome in @('failure','cancelled','skipped','timed_out')){
            $jobs[0].conclusion=$outcome;Assert (-not(Test-ScopedNativeCiProof $expected $jobs)) "Nonpassing native result inherited: $outcome"
        }
        $jobs[0].conclusion='success';$jobs[0].steps[0].conclusion='skipped'
        Assert (-not(Test-ScopedNativeCiProof $expected $jobs)) 'A skipped/listed/inherited row was treated as actual execution'
        Assert (-not(Test-ScopedNativeCiProof $expected @($jobs|Select-Object -Skip 1))) 'Incomplete matrix inherited'
    }
    $public=Join-Path $fixture 'public';New-Repository $public
    foreach($name in @('Example','Other')){Put (Join-Path $public "Tests/$name.Tests.Case.cpp") 'initial'}
    Put (Join-Path $public 'Tests/native-test-files.json') '["Tests/Example.Tests.Case.cpp","Tests/Other.Tests.Case.cpp"]'
    Copy-Item -LiteralPath (Join-Path $root 'Test-Changes.ps1') -Destination (Join-Path $public 'Test-Changes.ps1')
    Put (Join-Path $public 'Build/ScopedTesting.psm1') ([IO.File]::ReadAllText((Join-Path $root 'Build/ScopedTesting.psm1')))
    $mini.runtimeFiles=@();$mini.scopes=@(@{name='Example';native=$true;reuse=$true},@{name='Other';native=$true;reuse=$true},@{name='Tooling';native=$false;reuse=$true;commands=@(@{path='validate.ps1';arguments=@()})})
    $mini.rules=@(@{pattern='Tests/Example.Tests.Case.cpp';scopes=@('Example');reason='example owner'},@{pattern='Tests/Other.Tests.Case.cpp';scopes=@('Other');reason='other owner'})
    Put (Join-Path $public 'Tests/test-scopes.json') ($mini|ConvertTo-Json -Depth 8)
    Put (Join-Path $public 'build.ps1') @'
param($Platform,$Configuration)
$output=Join-Path $PSScriptRoot ".build/$Platform/$Configuration"
[void](New-Item -ItemType Directory -Path $output -Force)
$bytes=[IO.File]::ReadAllText((Join-Path $PSScriptRoot 'Tests/Example.Tests.Case.cpp'))+[IO.File]::ReadAllText((Join-Path $PSScriptRoot 'Tests/Other.Tests.Case.cpp'))
[IO.File]::WriteAllText((Join-Path $output 'example.exe'),$bytes)
exit 0
'@
    Put (Join-Path $public 'test.ps1') @'
param($Platform,$Configuration,[string[]]$Suites,[switch]$SkipBuild,[switch]$SkipTooling)
[IO.File]::AppendAllText((Join-Path $PSScriptRoot '.build/calls.txt'),($Suites -join ',')+"`n")
if(Test-Path (Join-Path $PSScriptRoot '.build/fail')){exit 1}
if(Test-Path (Join-Path $PSScriptRoot '.build/mutate')){[IO.File]::AppendAllText((Join-Path $PSScriptRoot 'Tests/Other.Tests.Case.cpp'),'mutated')}
exit 0
'@
    Put (Join-Path $public 'validate.ps1') "[IO.File]::AppendAllText((Join-Path `$PSScriptRoot '.build/tooling.txt'),`"run``n`")"
    Invoke-FixtureGit $public @('add','-A');Invoke-FixtureGit $public @('commit','-qm','baseline');Invoke-FixtureGit $public @('update-ref','refs/remotes/origin/main','HEAD');Invoke-FixtureGit $public @('switch','-qc','candidate')
    $platform=if([Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString() -eq 'Arm64'){'ARM64'}else{'x64'}
    $state=Join-Path $public ".build/reports/scoped-tests/$platform-Debug/iteration.json"
    function Run([string[]]$Arguments=@()){
        $binary=Join-Path $PSHOME $(if($IsWindows){'pwsh.exe'}else{'pwsh'})
        $start=[Diagnostics.ProcessStartInfo]::new($binary);$start.UseShellExecute=$false;$start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true
        foreach($arg in @('-NoProfile','-File',(Join-Path $public 'Test-Changes.ps1'),'-Platform',$platform)+$Arguments){$start.ArgumentList.Add($arg)}
        $process=[Diagnostics.Process]::Start($start)
        try{$out=$process.StandardOutput.ReadToEndAsync();$err=$process.StandardError.ReadToEndAsync();if(-not $process.WaitForExit(120000)){$process.Kill($true);throw 'Owned protocol fixture timed out'};[pscustomobject]@{Exit=$process.ExitCode;Text=$out.GetAwaiter().GetResult()+$err.GetAwaiter().GetResult()}}finally{$process.Dispose()}
    }
    Case 'public default records a successful affected baseline and then runs nothing unchanged' {
        Put (Join-Path $public 'Tests/Example.Tests.Case.cpp') 'example changed'
        $r=Run;Assert ($r.Exit -eq 0 -and (Test-Path $state)) "Initial public run failed: $($r.Text)"
        $r=Run;Assert ($r.Exit -eq 0 -and $r.Text.Contains('NOTHING_SELECTED')) "Identical default repeated: $($r.Text)"
        Assert (@(Get-Content (Join-Path $public '.build/calls.txt')).Count -eq 1) 'Identical code executed twice'
    }
    Case 'a later edit selects only the newly affected scope, not earlier branch changes' {
        Put (Join-Path $public 'Tests/Other.Tests.Case.cpp') 'other changed'
        $r=Run;Assert ($r.Exit -eq 0) "Later affected run failed: $($r.Text)"
        Assert (@(Get-Content (Join-Path $public '.build/calls.txt'))[-1] -ceq 'Other') 'Earlier Example scope reran after unrelated edit'
        Invoke-FixtureGit $public @('add','Tests');Invoke-FixtureGit $public @('commit','-qm','same tested bytes')
        $r=Run;Assert ($r.Exit -eq 0 -and $r.Text.Contains('NOTHING_SELECTED')) "Staging/committing the same bytes repeated tests: $($r.Text)"
    }
    Case 'explicit identical success is reused and partial selection never advances the default baseline' {
        $r=Run @('-Scopes','Example');Assert ($r.Exit -eq 0) "Explicit current-binary proof failed: $($r.Text)"
        $r=Run @('-Scopes','Example','-SkipBuild');Assert ($r.Exit -eq 0 -and $r.Text.Contains('REUSED Example')) "Exact explicit success repeated: $($r.Text)"
        $before=(Get-FileHash $state).Hash
        Put (Join-Path $public 'Tests/Example.Tests.Case.cpp') 'second example';Put (Join-Path $public 'Tests/Other.Tests.Case.cpp') 'second other'
        $r=Run @('-Scopes','Example');Assert ($r.Exit -eq 0) "Partial run failed: $($r.Text)"
        Assert ((Get-FileHash $state).Hash -ceq $before) 'Partial selection advanced the aggregate baseline'
        $r=Run;Assert ($r.Exit -eq 0 -and $r.Text.Contains('scopes: Example, Other')) "Partial selection concealed the other edit: $($r.Text)"
    }
    Case 'failures and post-run mutations never advance the affected baseline or retain reusable success' {
        $before=(Get-FileHash $state).Hash;Put (Join-Path $public 'Tests/Example.Tests.Case.cpp') 'failure candidate';Put (Join-Path $public '.build/fail') 'fail'
        $r=Run;Assert ($r.Exit -ne 0) 'Failure qualified';Assert ((Get-FileHash $state).Hash -ceq $before) 'Failure advanced baseline'
        $receipt=Join-Path $public ".build/reports/scoped-tests/$platform-Debug/Example.json"
        Assert (-not(Test-Path $receipt)) 'Failure retained earlier success'
        Remove-Item -LiteralPath (Join-Path $public '.build/fail');$r=Run;Assert ($r.Exit -eq 0) "Failed candidate was not rerun: $($r.Text)"
        $before=(Get-FileHash $state).Hash;Put (Join-Path $public 'Tests/Example.Tests.Case.cpp') 'mutation candidate';Put (Join-Path $public '.build/mutate') 'mutate'
        $r=Run;Assert ($r.Exit -ne 0) 'Mutation qualified';Assert ((Get-FileHash $state).Hash -ceq $before) 'Mutation advanced baseline'
        Remove-Item -LiteralPath (Join-Path $public '.build/mutate')
        $r=Run @('-SkipBuild');Assert ($r.Exit -ne 0 -and $r.Text.Contains('SkipBuild refused')) 'Stale compiled source accepted'
        $r=Run;Assert ($r.Exit -eq 0) "Mutated sources did not recover: $($r.Text)"
    }
    Write-Host "Scoped testing: $passed cases passed."
}finally{
    $absolute=[IO.Path]::GetFullPath($fixture);$allowed=[IO.Path]::GetFullPath((Join-Path $root '.build/ToolTests'))+[IO.Path]::DirectorySeparatorChar
    if(-not $absolute.StartsWith($allowed,[StringComparison]::OrdinalIgnoreCase)){throw 'Fixture cleanup escaped its owned root'}
    if(Test-Path -LiteralPath $absolute){Remove-Item -LiteralPath $absolute -Recurse -Force}
}
exit 0
