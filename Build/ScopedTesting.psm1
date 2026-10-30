# Repository-owned scope planning. No consumer checkout, service or desktop dependency.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Invoke-ScopedGit {
    param([string] $Root, [string[]] $Arguments)
    $start = [Diagnostics.ProcessStartInfo]::new('git')
    $start.UseShellExecute = $false
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    # Git writes paths as UTF-8. The default decoder is the console code page, which turns a non-ASCII name into one
    # that does not exist.
    $start.StandardOutputEncoding = [Text.UTF8Encoding]::new($false)
    $start.StandardErrorEncoding = [Text.UTF8Encoding]::new($false)
    foreach ($argument in @('-c','core.quotepath=false','-C',$Root) + $Arguments) { $start.ArgumentList.Add($argument) }
    try { $process = [Diagnostics.Process]::Start($start) }
    catch [System.Management.Automation.MethodInvocationException] {
        throw "Git scope discovery needs Git on PATH: $($_.Exception.InnerException.Message)"
    }
    try {
        $errors = $process.StandardError.ReadToEndAsync()
        $output = $process.StandardOutput.ReadToEnd()
        $process.WaitForExit()
        $errorText = $errors.GetAwaiter().GetResult().Trim()
        # Name the command and its exit code: merge-base, for one, fails silently when the histories share no commit.
        if ($process.ExitCode) {
            throw "Git scope discovery failed: git $($Arguments -join ' ') exited with code $($process.ExitCode): $(if ($errorText) { $errorText } else { '(no message)' })"
        }
        return $output
    } finally { $process.Dispose() }
}

function Get-ScopedMergeBase {
    # A base ref that does not resolve and one that shares no history with HEAD (a shallow or unrelated clone) need
    # different remedies; both name the full gate, which needs no comparison.
    param([string] $Root, [string] $BaseRef)
    try { [void](Invoke-ScopedGit $Root @('rev-parse','--verify','--quiet',"$BaseRef^{commit}")) }
    catch [System.Management.Automation.RuntimeException] {
        throw "Scoped testing compares HEAD with '$BaseRef', which does not resolve here: fetch it, pass -BaseRef <ref>, or run the full gate (./test.ps1 -Full). $($_.Exception.Message)"
    }
    try { return (Invoke-ScopedGit $Root @('merge-base',$BaseRef,'HEAD')).Trim() }
    catch [System.Management.Automation.RuntimeException] {
        throw "HEAD shares no history with '$BaseRef': deepen a shallow clone (git fetch --unshallow), pass -BaseRef <ref>, or run the full gate (./test.ps1 -Full). $($_.Exception.Message)"
    }
}

function Get-ScopedComparisonProblem {
    # Why affected selection cannot run here (no Git, no work tree, no base ref, no shared history), or '' when it can.
    param([string] $Root, [string] $BaseRef)
    try { [void](Get-ScopedMergeBase $Root $BaseRef); return '' }
    catch [System.Management.Automation.RuntimeException] { return $_.Exception.Message }
}

function Get-ScopedTrackedPaths {
    param([string] $Root)
    return @((Invoke-ScopedGit $Root @('ls-files','--cached','--others','--exclude-standard','-z')) -split '\x00' | Where-Object { $_ } | Sort-Object -Unique)
}

function Get-ScopedChangedPaths {
    param([string] $Root, [string] $BaseRef)
    $base = Get-ScopedMergeBase $Root $BaseRef
    $committed = Invoke-ScopedGit $Root @('diff','--name-only','--no-renames','-z',$base,'HEAD','--')
    $staged = Invoke-ScopedGit $Root @('diff','--cached','--name-only','--no-renames','-z','--')
    $working = Invoke-ScopedGit $Root @('diff','--name-only','--no-renames','-z','--')
    $untracked = Invoke-ScopedGit $Root @('ls-files','--others','--exclude-standard','-z')
    return @(($committed + $staged + $working + $untracked) -split '\x00' | Where-Object { $_ } | Sort-Object -Unique)
}

function Get-ScopedBuildNumber {
    param([string] $Root, [string] $BaseRef)
    try{$base=Get-ScopedMergeBase $Root $BaseRef}catch [System.Management.Automation.RuntimeException]{$base='HEAD'}
    return [int](Invoke-ScopedGit $Root @('rev-list','--count',$base)).Trim()
}

function Assert-ScopedRuntimePlatform {
    # The rule test.ps1 applies: ARM64 runtime tests need an ARM64 host, and x64 runs on one under emulation. The
    # architecture in the environment identity keeps emulated evidence apart from native x64 evidence.
    param([string] $Platform, [string] $HostArchitecture = [Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString())
    if ($Platform -eq 'ARM64' -and $HostArchitecture -ne 'Arm64') {
        throw 'ARM64 runtime qualification requires a native ARM64 host; use build.ps1 for cross-compilation.'
    }
}

function Test-ScopedPattern {
    param([string] $Path, [string] $Pattern)
    $expression = [regex]::Escape($Pattern).Replace('\*\*/','(?:.*/)?').Replace('\*\*','.*').Replace('\*','[^/]*').Replace('\?','[^/]')
    return [regex]::IsMatch($Path, '^' + $expression + '$', [Text.RegularExpressions.RegexOptions]::IgnoreCase)
}

function Read-ScopedTestManifest {
    param([string] $Root)
    $manifest = Get-Content -LiteralPath (Join-Path $Root 'Tests/test-scopes.json') -Raw | ConvertFrom-Json
    if ($manifest.version -ne 1 -or @($manifest.scopes).Count -eq 0) { throw 'Invalid test scope manifest.' }
    $names = @($manifest.scopes | ForEach-Object { $_.name })
    if (@($names | Sort-Object -Unique).Count -ne $names.Count -or @($names | Where-Object { $_ -notmatch '^[A-Za-z][A-Za-z0-9.-]*$' }).Count) { throw 'Invalid or duplicate test scopes.' }
    foreach ($rule in $manifest.rules) {
        foreach ($name in $rule.scopes) { if ($name -ne '*' -and $name -notin $names) { throw "Rule selects unknown scope '$name'." } }
    }
    foreach ($job in $manifest.prCoverage) {
        # Each entry is one PR check. One that names a profile runs that profile only; one that names none (the tooling
        # job) runs for every profile, so it can cover only profile-independent scopes.
        if (-not $job.PSObject.Properties['check'] -or -not $job.check) { throw 'Every prCoverage entry names the PR check that runs it.' }
        $bound = [bool]$job.PSObject.Properties['platform']
        if ($bound -ne [bool]$job.PSObject.Properties['configuration']) { throw "PR check '$($job.check)' names a platform or a configuration without the other." }
        foreach ($name in $job.scopes) {
            $scope = @($manifest.scopes | Where-Object { $_.name -eq $name })
            if (-not $scope.Count) { throw "PR check '$($job.check)' covers unknown scope '$name'." }
            if (-not $bound -and $scope[0].native) { throw "PR check '$($job.check)' names no profile, so it cannot cover the native scope '$name'." }
        }
    }
    if($manifest.PSObject.Properties['projectBindings']){$manifest|Add-Member -NotePropertyName liveRules -NotePropertyValue @(Get-ScopedProjectRules $Root $manifest.projectBindings)}
    return $manifest
}

function Assert-ScopedTestNames {
    param([string] $Root)
    $manifest=Read-ScopedTestManifest $Root
    $inventory=Get-Content -LiteralPath (Join-Path $Root $manifest.nativeInventory) -Raw | ConvertFrom-Json
    $declared=@(if($inventory.PSObject.Properties['files']){$inventory.files.path}else{$inventory})
    if(@($declared|Sort-Object -Unique).Count -ne $declared.Count){throw 'Duplicate native test inventory paths.'}
    foreach($path in $declared){
        if($path -match '^(Specs|Measurements|legacy|ThirdParty)/'){throw "Historical/external source cannot enter the active inventory: $path"}
        if($path -cnotmatch '(^|/)[^/]+\.Tests\.[^/]+\.(cpp|h)$' -or -not(Test-Path -LiteralPath (Join-Path $Root $path) -PathType Leaf)){throw "Invalid/missing native test source: $path"}
    }
    $live=@(Get-ScopedTrackedPaths $Root|Where-Object{
        $_ -notmatch '^(Specs|Measurements|legacy|ThirdParty)/' -and $_ -match '\.(cpp|h)$' -and
        ($_ -match '^Tests/|(^|/)[^/]*\.Tests\.[^/]*$' -or $_ -cmatch '(^|/)[^/]*(SelfTest|Tests?|Mock|Fake)(?=[A-Z0-9_.-])[^/]*$') -and
        (Test-Path -LiteralPath (Join-Path $Root $_) -PathType Leaf)
    })
    foreach($path in $live){if($path -notin $declared){throw "Native test source is absent from $($manifest.nativeInventory): $path"}}
    return $declared.Count
}

function Get-ScopedTestPlan {
    param([object] $Manifest, [AllowEmptyCollection()][string[]] $ChangedPaths, [string[]] $Scopes = @(), [switch] $Full)
    $names = @($Manifest.scopes | ForEach-Object { $_.name })
    $selected = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $reasons = [Collections.Generic.List[object]]::new()
    foreach ($scope in $Scopes) {
        if ($scope -notin $names) { throw "Unknown test scope '$scope'. Available: $($names -join ', ')" }
        [void]$selected.Add($scope)
        $reasons.Add([pscustomobject]@{path='(explicit)'; scopes=@($scope); reason='explicit scope'})
    }
    if ($Full) { foreach ($name in $names) { [void]$selected.Add($name) } }
    if (-not $Scopes.Count -and -not $Full) {
        foreach ($path in $ChangedPaths) {
            if ($path -match '(^/|(^|/)\.\.(/|$)|:|\\)') { throw "Expected a repository-relative Git path: $path" }
            if ($path -match '(^(README|CHANGELOG)\.md$|(^|/)AGENTS\.md$|^(docs|Specs|\.agents|Changes)/.*\.md$)') {
                $targets = @($Manifest.scopes | Where-Object { -not $_.native } | ForEach-Object name)
                foreach ($name in $targets) { [void]$selected.Add($name) }
                $reasons.Add([pscustomobject]@{path=$path; scopes=$targets; reason='documentation/skill validation; no native test input'})
                continue
            }
            # Exact validator-only modules have no product/build consumers. Bypass broad Build/**
            # and project-manifest fallback only for this reviewed closure; shared policy stays broad.
            if ($path -cin @('Build/SkillPackageIntegrity.psm1','Build/SkillValidation.psm1')) {
                if ('Tooling' -notin $names) { throw 'Validator-only planning requires the Tooling scope.' }
                [void]$selected.Add('Tooling')
                $reasons.Add([pscustomobject]@{path=$path; scopes=@('Tooling'); reason='exact independent skill-validator module; no native consumer'})
                continue
            }
            $allRules=@($Manifest.rules);if($Manifest.PSObject.Properties['liveRules']){$allRules+=@($Manifest.liveRules)}
            $matched = @($allRules | Where-Object { Test-ScopedPattern $path $_.pattern })
            if(-not @($matched|Where-Object{$_.pattern -notin @('**/*.cpp','**/*.h')}).Count){$matched=@()}
            if ($matched.Count) {
                foreach ($rule in $matched) {
                    $targets = if ('*' -in $rule.scopes) { $names } else { @($rule.scopes) }
                    foreach ($name in $targets) { [void]$selected.Add($name) }
                    $reasons.Add([pscustomobject]@{path=$path; scopes=$targets; reason=$rule.reason})
                }
            } elseif ($path -match '^(legacy|docs|Measurements|Mockups)/') {
                $reasons.Add([pscustomobject]@{path=$path; scopes=@(); reason='documentation; no native test input'})
            } else {
                foreach ($name in $names) { [void]$selected.Add($name) }
                $reasons.Add([pscustomobject]@{path=$path; scopes=$names; reason='unmapped input; conservative full fallback'})
            }
        }
    }
    return [pscustomobject]@{scopes=@($names | Where-Object { $selected.Contains($_) }); reasons=@($reasons); full=($selected.Count -eq $names.Count)}
}

function Get-ScopedDigest {
    param([string] $Text)
    $hash = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($hash.ComputeHash([Text.Encoding]::UTF8.GetBytes($Text)))).Replace('-','').ToLowerInvariant() }
    finally { $hash.Dispose() }
}

function Get-ScopedSourceIdentity {
    param([string] $Root, [switch] $CompiledOnly)
    return Get-ScopedMapIdentity (Get-ScopedSourceMap $Root -CompiledOnly:$CompiledOnly)
}

function Get-ScopedArtifactIdentity {
    param([string] $Root, [string] $Platform, [string] $Configuration)
    $directory=Join-Path $Root ".build/$Platform/$Configuration"
    if(-not(Test-Path -LiteralPath $directory -PathType Container)){throw "Missing build profile: $directory"}
    $binaries=@(Get-ChildItem -LiteralPath $directory -Recurse -File -Force|Where-Object{$_.Extension -in @('.exe','.dll','.pdb')})
    if(-not $binaries.Count){throw 'No executable build artifacts were found.'}
    $manifest=Get-Content -LiteralPath (Join-Path $Root 'Tests/test-scopes.json') -Raw|ConvertFrom-Json
    $deployed=@(foreach($relative in $manifest.runtimeFiles){
        $path=Join-Path $directory $relative
        if(-not(Test-Path -LiteralPath $path -PathType Leaf)){throw "Missing deployed runtime input: $relative"}
        Get-Item -LiteralPath $path -Force -ErrorAction Stop
    })
    $rows=@(@($binaries+$deployed)|Sort-Object FullName|ForEach-Object{
        [IO.Path]::GetRelativePath($directory,$_.FullName)+':'+(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
    })
    return Get-ScopedDigest ($rows -join "`n")
}

function Get-ScopedEnvironmentInputs {
    # Only the digest of these rows is stored, never a secret. Different machines, OS builds or servicing levels,
    # PowerShell, graphics, text or imaging runtimes, Git or sanitizer options cannot reuse evidence. Tooling evidence
    # uses the same PowerShell runtime as repository validation; no Python is discovered.
    param([switch] $Tooling)
    $values = @([Environment]::MachineName,[Runtime.InteropServices.RuntimeInformation]::OSDescription,
        [Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString(),$PSVersionTable.PSVersion.ToString(),
        $env:ASAN_OPTIONS,$env:CI,$env:GITHUB_ACTIONS,$env:PROCESSOR_IDENTIFIER,$env:PATH,
        $env:VCToolsVersion,$env:WindowsSDKVersion)
    $currentVersion = 'HKLM:/SOFTWARE/Microsoft/Windows NT/CurrentVersion'
    if ($env:SystemRoot -and (Test-Path -LiteralPath $currentVersion)) {
        # The update revision changes with every cumulative update, including one that services only unhashed DLLs.
        $build = Get-ItemProperty -LiteralPath $currentVersion | Select-Object CurrentBuildNumber,UBR
        $values += "os-build=$($build.CurrentBuildNumber).$($build.UBR)"
    }
    # The suites draw through Direct2D (DxUi), Direct3D, WARP and DirectWrite, present through DXGI and decode through WIC.
    foreach ($file in @('d2d1.dll','d3d10warp.dll','d3d11.dll','dwrite.dll','dxgi.dll','windowscodecs.dll')) {
        if (-not $env:SystemRoot) { continue }
        $path = Join-Path $env:SystemRoot "System32/$file"
        $values += "$file=" + $(if (Test-Path -LiteralPath $path) { (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash } else { 'absent' })
    }
    # Scope discovery and the Git fixtures run the installed Git.
    $git = Get-Command git -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    $values += 'git=' + $(if ($git) { (& $git.Source --version) -join ' ' } else { 'absent' })
    # Tooling fixtures can discover or invoke the installed compiler. Updates must invalidate their receipts too.
    $vswhere = @(${env:ProgramFiles(x86)},$env:ProgramFiles | Where-Object { $_ } | ForEach-Object {
        Join-Path $_ 'Microsoft Visual Studio/Installer/vswhere.exe'
    }) | Where-Object {Test-Path -LiteralPath $_} | Select-Object -First 1
    if ($vswhere) {
        $installations = & $vswhere -all -prerelease -products '*' -format json
        if ($LASTEXITCODE) { throw 'Cannot attest the installed Visual Studio environment.' }
        $values += ($installations -join "`n")
        foreach ($installation in @($installations -join "`n" | ConvertFrom-Json)) {
            $toolset = Join-Path $installation.installationPath 'VC/Auxiliary/Build/Microsoft.VCToolsVersion.default.txt'
            if (Test-Path -LiteralPath $toolset) { $values += (Get-FileHash -LiteralPath $toolset).Hash }
        }
    }
    if ($env:SystemRoot -and (Test-Path 'HKLM:/SOFTWARE/Microsoft/Windows Kits/Installed Roots')) {
        $values += (Get-ChildItem 'HKLM:/SOFTWARE/Microsoft/Windows Kits/Installed Roots' | Sort-Object Name | ForEach-Object Name) -join "`n"
    }
    if ($Tooling) {
        $values += 'tooling-runtime=PowerShell'
        $discovery = Join-Path $PSScriptRoot 'VisualStudioDiscovery.psm1'
        $formatter = $null
        if (Test-Path -LiteralPath $discovery) {
            Import-Module $discovery -Force
            $formatter = Find-RedPrismClangFormat
        } else {
            $command = Get-Command clang-format.exe -CommandType Application -ErrorAction SilentlyContinue
            if ($command) { $formatter = $command.Source }
        }
        $values += 'formatter=' + $(if ($formatter) { $formatter + ':' + (Get-FileHash -LiteralPath $formatter).Hash } else { 'absent' })
    }
    return $values
}

function Get-ScopedEnvironmentIdentity {
    param([switch] $Tooling)
    return Get-ScopedDigest ((Get-ScopedEnvironmentInputs -Tooling:$Tooling) -join "`n")
}

function Get-ScopedRunIdentity {
    param([string] $Root, [string] $Platform, [string] $Configuration, [string] $Scope, [string] $Options = '')
    return Get-ScopedDigest (@([IO.Path]::GetFullPath($Root),$Platform,$Configuration,$Scope,$Options,
        (Get-ScopedSourceIdentity $Root -CompiledOnly),(Get-ScopedArtifactIdentity $Root $Platform $Configuration),
        (Get-ScopedEnvironmentIdentity)) -join "`n")
}

function Test-ScopedReceipt {
    param([string] $Path, [string] $Identity)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return $false }
    try {
        $receipt = Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
        # PowerShell 7.5 parses ISO JSON strings into DateTime; preserve the canonical UTC spelling on earlier hosts too.
        $completed = if ($receipt.completedUtc -is [DateTime]) { $receipt.completedUtc.ToUniversalTime().ToString('o') } else { [string]$receipt.completedUtc }
        return $receipt.version -eq 1 -and $receipt.outcome -eq 'PASSED' -and $receipt.identity -ceq $Identity -and
            $receipt.digest -ceq (Get-ScopedDigest ($receipt.identity + "`n" + $completed + "`nPASSED"))
    } catch [ArgumentException] { return $false }
    catch [System.Management.Automation.RuntimeException] { return $false }
}

function Write-ScopedReceipt {
    param([string] $Path, [string] $Identity)
    $completed = [DateTime]::UtcNow.ToString('o')
    $receipt = [ordered]@{version=1; outcome='PASSED'; identity=$Identity; completedUtc=$completed;
        digest=(Get-ScopedDigest ($Identity + "`n" + $completed + "`nPASSED"))}
    [void](New-Item -ItemType Directory -Path (Split-Path $Path) -Force)
    $temporary = $Path + '.' + [guid]::NewGuid().ToString('N') + '.tmp'
    try {
        [IO.File]::WriteAllText($temporary,($receipt | ConvertTo-Json),[Text.UTF8Encoding]::new($false))
        Move-Item -LiteralPath $temporary -Destination $Path -Force
    } finally { if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary } }
}

function Get-ScopedPrJobs {
    # The PR checks that run for a local profile: a check that names a platform and configuration runs that native
    # profile only; one that names neither (the tooling job) runs the same profile-independent work for every profile.
    param([object] $Manifest, [string] $Platform, [string] $Configuration)
    return @($Manifest.prCoverage | Where-Object {
        -not $_.PSObject.Properties['platform'] -or ($_.platform -eq $Platform -and $_.configuration -eq $Configuration)
    })
}

function Get-ScopedPrCandidateScopes {
    # The scopes the PR checks run for a local profile. Every native check runs on every pull request (ci.yml has no path
    # filter or job condition), so they do not depend on what changed.
    param([object] $Manifest, [string] $Platform, [string] $Configuration)
    return @(Get-ScopedPrJobs $Manifest $Platform $Configuration | ForEach-Object { $_.scopes } | Sort-Object -Unique)
}

function Invoke-ScopedGhApi {
    # Returns the parsed response. A failure carries gh's own message: not signed in, offline, no access, not found.
    param([string] $Path)
    if (-not (Get-Command gh -ErrorAction SilentlyContinue)) { throw 'gh (GitHub CLI) is not installed.' }
    $global:LASTEXITCODE = 0
    $lines = @(& gh api $Path 2>&1)
    $errors = @($lines | Where-Object { $_ -is [Management.Automation.ErrorRecord] } | ForEach-Object { "$_".Trim() } | Where-Object { $_ })
    if ($LASTEXITCODE) { throw "gh api $Path exited with code $LASTEXITCODE$(if ($errors.Count) { ': ' + ($errors -join ' ') })" }
    return @($lines | Where-Object { $_ -isnot [Management.Automation.ErrorRecord] }) -join "`n" | ConvertFrom-Json
}

function Get-ScopedPrDelegation {
    param([string] $Root, [object] $Manifest, [string] $Platform, [string] $Configuration)
    $keepLocal = { param([string] $Reason) [pscustomobject]@{scopes=@(); refusal=$Reason} }
    $jobs = @(Get-ScopedPrJobs $Manifest $Platform $Configuration)
    if (-not $jobs.Count) { return & $keepLocal "no PR check runs $Platform $Configuration" }
    # The forthcoming PR executes its candidate workflow. Its reviewed digest must match; API problems keep work local.
    try {
        # GitHub receives committed bytes. Dirty or concurrently changing work cannot be delegated.
        $candidate = (Invoke-ScopedGit $Root @('rev-parse','HEAD')).Trim()
        if ((Invoke-ScopedGit $Root @('branch','--show-current')).Trim() -ceq $Manifest.defaultBranch) { return & $keepLocal 'a direct default-branch push has no forthcoming PR gate' }
        if (Invoke-ScopedGit $Root @('status','--porcelain','--untracked-files=normal')) {
            return & $keepLocal 'the working tree has uncommitted or untracked changes, and a PR runs committed bytes only'
        }
        $local = [IO.File]::ReadAllText((Join-Path $Root '.github/workflows/ci.yml')) -replace "`r`n","`n"
        if ((Get-ScopedDigest $local) -cne $Manifest.prWorkflowDigest) {
            return & $keepLocal '.github/workflows/ci.yml is not the workflow reviewed in Tests/test-scopes.json (prWorkflowDigest)'
        }
        if ($local -notmatch '(?m)^  pull_request:') { return & $keepLocal '.github/workflows/ci.yml has no pull_request trigger' }
        # A PR runs the workflow and runner of its merge with the base branch: the base must not have changed them since
        # this branch left it.
        $baseRef = 'origin/' + $Manifest.defaultBranch
        $base = Get-ScopedMergeBase $Root $baseRef
        $runner = @('.github/workflows/ci.yml','test.ps1','Tests/test-scopes.json','Build/ScopedTesting.psm1','Tools/Get-CiNativeScope.ps1','Tests/Tooling.Tests.WindowFixture.ps1') + @($Manifest.toolingCommands)
        $moved = @((Invoke-ScopedGit $Root (@('diff','--name-only','--no-renames','-z',$base,$baseRef,'--') + $runner)) -split '\x00' | Where-Object { $_ })
        if ($moved.Count) {
            return & $keepLocal "$baseRef changed $($moved -join ', ') after this branch left it, so the PR would not run the reviewed version; integrate $baseRef first"
        }
        $workflow = Invoke-ScopedGhApi "repos/$($Manifest.repository)/actions/workflows/ci.yml"
        if ($workflow.state -ne 'active') { return & $keepLocal "the CI workflow is $($workflow.state) on GitHub" }
        $covered=@(Get-ScopedPrCandidateScopes $Manifest $Platform $Configuration)
        $refusal='CI_PENDING is not merge approval: all named checks must pass on this candidate. GitHub account restrictions may prevent branch-rule enforcement.'
        if ((Invoke-ScopedGit $Root @('rev-parse','HEAD')).Trim() -cne $candidate -or
            (Invoke-ScopedGit $Root @('status','--porcelain','--untracked-files=normal'))) {
            return & $keepLocal 'HEAD or the working tree changed while delegation was checked'
        }
        return [pscustomobject]@{scopes=$covered; refusal=$refusal}
    } catch [System.Management.Automation.RuntimeException] { return & $keepLocal $_.Exception.Message }
    catch [ArgumentException] { return & $keepLocal $_.Exception.Message }
}

function Get-ScopedPrCoverage {
    # The scopes the forthcoming PR's required checks run for this profile. Refusal receives why the work of any check
    # that runs for it stays local, or '' when none does.
    param([string] $Root, [object] $Manifest, [string] $Platform, [string] $Configuration, [ref] $Refusal)
    $delegation = Get-ScopedPrDelegation $Root $Manifest $Platform $Configuration
    if ($null -ne $Refusal) { $Refusal.Value = $delegation.refusal }
    return $delegation.scopes
}

Export-ModuleMember -Function Get-ScopedChangedPaths, Get-ScopedComparisonProblem, Get-ScopedBuildNumber, Assert-ScopedRuntimePlatform, Read-ScopedTestManifest,
    Assert-ScopedTestNames, Get-ScopedTestPlan, Get-ScopedSourceIdentity, Get-ScopedArtifactIdentity, Get-ScopedRunIdentity,
    Get-ScopedDigest, Test-ScopedReceipt, Write-ScopedReceipt, Get-ScopedPrCoverage, Get-ScopedPrCandidateScopes,
    Get-ScopedEnvironmentInputs, Get-ScopedEnvironmentIdentity


function Get-ScopedSourceMap {
    param([string]$Root,[switch]$CompiledOnly)
    $map=@{};$deleted=$null
    foreach($path in @(Get-ScopedTrackedPaths $Root)){
        if($path -match '^\.build/'){continue}
        # Historical native payloads and third-party oracles are not build inputs. Keep their
        # Markdown in validator identities because link validators read it.
        if($path -match '^(Measurements|legacy|Specs/(TestRuns|Plans/Done))/' -and $path -notmatch '\.md$'){continue}
        if($CompiledOnly -and ($path -match '^(Measurements|legacy|docs|Specs/(Plans|TestRuns))/' -or
            $path -match '(^(README|CHANGELOG)\.md$|(^|/)AGENTS\.md$|^(Specs|\.agents|Changes)/.*\.md$)')){continue}
        $full=Join-Path $Root $path
        if(-not(Test-Path -LiteralPath $full -PathType Leaf)){
            if(Test-Path -LiteralPath $full -PathType Container){continue}
            if($null -eq $deleted){$deleted=@((Invoke-ScopedGit $Root @('ls-files','--deleted','-z')) -split '\x00'|Where-Object{$_})}
            if($path -cin $deleted){continue}
            throw "Source identity input is neither on disk nor a tracked deletion: $path"
        }
        $map[$path]=(Get-FileHash -LiteralPath $full -Algorithm SHA256).Hash
    }
    return ,$map
}

function Get-ScopedMapIdentity {
    param([System.Collections.IDictionary]$Map)
    return Get-ScopedDigest (@($Map.Keys|Sort-Object -CaseSensitive|ForEach-Object{"$_`0$($Map[$_])"}) -join "`n")
}

function Get-ScopedIndexMap {
    param([string]$Root)
    $map=@{}
    foreach($entry in ((Invoke-ScopedGit $Root @('ls-files','--stage','-z')) -split '\x00'|Where-Object{$_})){
        if($entry -notmatch '^\d+ ([a-f0-9]+) ([0-3])\t(.*)$'){throw 'Unrecognized Git index record'}
        if($Matches[2] -ne '0'){throw 'Resolve Git conflicts before selecting affected tests.'}
        $map[$Matches[3]]=$Matches[1]
    }
    return ,$map
}

function Get-ScopedMapDelta {
    param([System.Collections.IDictionary]$Before,[System.Collections.IDictionary]$After)
    return @(@($Before.Keys)+@($After.Keys)|Sort-Object -Unique -CaseSensitive|Where-Object{
        -not $Before.Contains($_) -or -not $After.Contains($_) -or $Before[$_] -cne $After[$_]
    })
}

function Get-ScopedIterationDigest {
    param([System.Collections.IDictionary]$State)
    return Get-ScopedDigest (@($State.version,$State.root,$State.platform,$State.configuration,$State.environment,
        $State.artifact,(Get-ScopedMapIdentity $State.source),(Get-ScopedMapIdentity $State.index)) -join "`n")
}

function Read-ScopedIteration {
    param([string]$Path,[string]$Root,[string]$Platform,[string]$Configuration,[string]$Environment)
    if(-not(Test-Path -LiteralPath $Path -PathType Leaf)){return $null}
    try{
        $state=Get-Content -LiteralPath $Path -Raw|ConvertFrom-Json -AsHashtable
        if($state.version -ne 1 -or $state.root -cne [IO.Path]::GetFullPath($Root) -or $state.platform -cne $Platform -or
            $state.configuration -cne $Configuration -or $state.environment -cne $Environment -or
            $state.digest -cne (Get-ScopedIterationDigest $state)){return $null}
        return $state
    }catch [System.Management.Automation.RuntimeException]{return $null}
    catch [ArgumentException]{return $null}
}

function Write-ScopedIteration {
    param([string]$Path,[string]$Root,[string]$Platform,[string]$Configuration,[string]$Environment,
        [System.Collections.IDictionary]$Source,[System.Collections.IDictionary]$Index,[string]$Artifact)
    $state=[ordered]@{version=1;root=[IO.Path]::GetFullPath($Root);platform=$Platform;configuration=$Configuration;
        environment=$Environment;artifact=$Artifact;source=$Source;index=$Index}
    $state['digest']=Get-ScopedIterationDigest $state
    [void](New-Item -ItemType Directory -Path (Split-Path $Path) -Force)
    $temporary=$Path+'.'+[guid]::NewGuid().ToString('N')+'.tmp'
    try{[IO.File]::WriteAllText($temporary,($state|ConvertTo-Json -Depth 6),[Text.UTF8Encoding]::new($false));Move-Item -LiteralPath $temporary -Destination $Path -Force}
    finally{if(Test-Path -LiteralPath $temporary){Remove-Item -LiteralPath $temporary}}
}

Export-ModuleMember -Function Get-ScopedSourceMap,Get-ScopedIndexMap,Get-ScopedMapIdentity,Get-ScopedMapDelta,
    Read-ScopedIteration,Write-ScopedIteration
function Get-ScopedPendingIndexDelta {
    param([string]$Root,[System.Collections.IDictionary]$Before,[System.Collections.IDictionary]$After)
    # Staging/committing already-tested working bytes adds no native obligation. Preserve
    # a genuinely different staged blob that the working file would otherwise conceal.
    $working=@((Invoke-ScopedGit $Root @('diff','--name-only','--no-renames','-z','--')) -split '\x00'|Where-Object{$_})
    return @(Get-ScopedMapDelta $Before $After|Where-Object{$_ -cin $working})
}
Export-ModuleMember -Function Get-ScopedPendingIndexDelta

function Get-ScopedProjectRules {
    param([string]$Root,[object[]]$Bindings)
    $owners=@{};$includes=@{};$unresolved=$false
    $ownedByLeaf=@{}
    foreach($owned in @(Get-ScopedTrackedPaths $Root|Where-Object{$_ -notmatch '^(legacy|ThirdParty|Specs|Measurements|\.build)/'})){
        $leaf=[IO.Path]::GetFileName($owned)
        if(-not $ownedByLeaf.ContainsKey($leaf)){$ownedByLeaf[$leaf]=@()}
        $ownedByLeaf[$leaf]+=$owned
    }
    foreach($binding in $Bindings){
        $directory=Split-Path (Join-Path $Root $binding.path)
        [xml]$project=Get-Content -LiteralPath (Join-Path $Root $binding.path) -Raw
        $stack=[Collections.Generic.Stack[string]]::new();$seen=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
        foreach($node in $project.SelectNodes('//*[local-name()="ClCompile" or local-name()="ResourceCompile" or local-name()="CustomBuild" or local-name()="FxCompile"]')){
            if(-not $node.HasAttribute('Include')){continue}
            $include=$node.GetAttribute('Include')
            if($include -match '\$|%|\*|\?'){$unresolved=$true;continue}
            $file=[IO.Path]::GetFullPath((Join-Path $directory $include))
            if(-not(Test-Path -LiteralPath $file -PathType Leaf)){throw "Missing native project input: $($binding.path) -> $include"}
            $stack.Push($file)
        }
        while($stack.Count){
            $file=$stack.Pop();if(-not $seen.Add($file)){continue}
            $relative=[IO.Path]::GetRelativePath($Root,$file) -replace '\\','/'
            if($relative -match '^\.\./|^\.build/'){continue}
            if(-not $owners.ContainsKey($relative)){$owners[$relative]=[Collections.Generic.HashSet[string]]::new()}
            [void]$owners[$relative].Add($binding.scope)
            if($binding.scope -in @('Host','Document')){[void]$owners[$relative].Add('Stress')}
            if($binding.scope -eq 'Runtime' -and $relative -notmatch '\.Tests\..*\.cpp$'){
                [void]$owners[$relative].Add('Performance');[void]$owners[$relative].Add('Captures')
            }
            if(-not $includes.ContainsKey($file)){
                $body=[IO.File]::ReadAllText($file);$includes[$file]=@()
                if($body -match '(?m)^\s*#\s*include\s+[^\s"<]'){$unresolved=$true}
                foreach($match in [regex]::Matches($body,'(?m)^\s*#\s*include\s*[<"]([^">]+)[">]')){
                    $name=$match.Groups[1].Value -replace '\\','/'
                    $leaf=[IO.Path]::GetFileName($name)
                    if(-not $ownedByLeaf.ContainsKey($leaf)){continue}
                    $candidates=@((Join-Path (Split-Path $file) $name),(Join-Path $Root $name),(Join-Path $Root ('Common/'+$name)))|ForEach-Object{[IO.Path]::GetRelativePath($Root,[IO.Path]::GetFullPath($_)) -replace '\\','/'}
                    $found=@($ownedByLeaf[$leaf]|Where-Object{$_ -cin $candidates -or $_ -ceq $name -or $_.EndsWith('/'+$name,[StringComparison]::OrdinalIgnoreCase)}|ForEach-Object{Join-Path $Root $_})
                    foreach($candidate in $found){$includes[$file]+=[IO.Path]::GetFullPath($candidate)}
                }
            }
            foreach($include in $includes[$file]){$stack.Push($include)}
        }
    }
    $rules=@(foreach($path in @($owners.Keys|Sort-Object)){
        [pscustomobject]@{pattern=$path;scopes=@($owners[$path]|Sort-Object);reason='current native project and transitive local-include consumers'}
    })
    if($unresolved){$rules+= [pscustomobject]@{pattern='**';scopes=@('*');reason='unresolved macro/wildcard native dependency; conservative full fallback'}}
    return $rules
}
Export-ModuleMember -Function Get-ScopedProjectRules

function Get-ScopedBuildAttestationKey {
    param([string]$Root,[string]$Platform,[string]$Configuration,[int]$BuildNumber)
    return Get-ScopedDigest (@('canonical-build-v1',[IO.Path]::GetFullPath($Root),$Platform,$Configuration,$BuildNumber,
        ('tests-enabled='+[string]($env:RedPrismEnableTests -ne 'false')),
        (Get-ScopedSourceIdentity $Root -CompiledOnly),(Get-ScopedArtifactIdentity $Root $Platform $Configuration),
        (Get-ScopedEnvironmentIdentity)) -join "`n")
}
Export-ModuleMember -Function Get-ScopedBuildAttestationKey
function Test-ScopedNativeCiProof {
    param([string[]]$ExpectedChecks,[object[]]$Jobs)
    if($ExpectedChecks.Count -ne 6 -or @($ExpectedChecks|Sort-Object -Unique).Count -ne 6){return $false}
    foreach($name in $ExpectedChecks){
        $foundJobs=@($Jobs|Where-Object name -CEQ $name)
        if($foundJobs.Count -ne 1 -or $foundJobs[0].status -cne 'completed' -or $foundJobs[0].conclusion -cne 'success'){return $false}
        $execution=@($foundJobs[0].steps|Where-Object name -CEQ 'Full native qualification')
        if($execution.Count -ne 1 -or $execution[0].conclusion -cne 'success'){return $false}
    }
    return $true
}
Export-ModuleMember -Function Test-ScopedNativeCiProof,Invoke-ScopedGhApi
