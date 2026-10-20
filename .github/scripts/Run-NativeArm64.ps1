[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# One bounded feasibility run. Repository entrypoints retain every existing build/test gate.
if ($PSVersionTable.PSVersion.Major -lt 7) { throw 'Native validation requires PowerShell 7.' }
if ($env:GITHUB_ACTIONS -cne 'true') { throw 'This helper requires its reviewed GitHub Actions workflow.' }
$repoRoot = (Get-Item -LiteralPath $PSScriptRoot).Parent.Parent.FullName
$outRoot = Join-Path $repoRoot '.build/native-arm-ci'
New-Item -ItemType Directory -Path $outRoot -Force | Out-Null

function Write-Evidence([string] $Name, [object] $Value) {
    $path = Join-Path $outRoot $Name
    if (Test-Path -LiteralPath $path) { throw "Refusing to overwrite evidence: $path" }
    $Value | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $path -Encoding utf8
}
function Get-RepoGit([string[]] $Arguments) {
    $value = @(& git -C $repoRoot @Arguments)
    if ($LASTEXITCODE -ne 0) { throw "Read-only Git command failed: $($Arguments[0])" }
    return $value
}
function Get-SourceSnapshot {
    $files = [ordered]@{}
    foreach ($relative in @(Get-RepoGit -Arguments @('-c','core.quotepath=false','ls-files'))) {
        $path = Join-Path $repoRoot $relative
        $files[$relative] = [ordered]@{bytes=(Get-Item -LiteralPath $path).Length;sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash}
    }
    return $files
}
function Assert-CleanSource {
    $actual = (Get-RepoGit -Arguments @('rev-parse','HEAD')) -join ''
    if ($actual -cne $env:GITHUB_SHA) { throw 'Checked-out commit differs from the event SHA.' }
    if (@(Get-RepoGit -Arguments @('status','--porcelain=v1','--untracked-files=all')).Count -ne 0) {
        throw 'Source worktree changed; this run cannot qualify the event commit.'
    }
}
function Initialize-NativeBuildHost {
    Import-Module (Join-Path $repoRoot 'Build/VisualStudioDiscovery.psm1') -Force
    $original = Find-RedPrismMSBuild
    $installation = Find-RedPrismVisualStudioInstallationOfMSBuild $original
    $native = Join-Path $installation 'MSBuild/Current/Bin/arm64/MSBuild.exe'
    if (-not (Test-Path -LiteralPath $native -PathType Leaf)) {
        throw 'The selected Visual Studio installation has no ARM64-hosted MSBuild; refusing another emulated build.'
    }
    if ([Diagnostics.FileVersionInfo]::GetVersionInfo($native).FileMajorPart -ne 18) {
        throw 'Native MSBuild is outside the required Visual Studio 2026 version.'
    }
    $version = (Get-Content -LiteralPath (Join-Path $installation 'VC/Auxiliary/Build/Microsoft.VCToolsVersion.default.txt') -Raw).Trim()
    $compiler = Join-Path $installation "VC/Tools/MSVC/$version/bin/HostARM64/ARM64/cl.exe"
    if (-not (Test-Path -LiteralPath $compiler -PathType Leaf)) {
        throw 'Native ARM64-hosted compiler is unavailable in the default v145 toolset.'
    }
    # Supported canonical discovery override, inherited by test.ps1, dependency restore and build.ps1.
    # Microsoft.Cpp.ToolsetLocation.props selects arm64 only in an ARM64-hosted MSBuild process.
    $env:MSBUILD_EXE_PATH = $native
    $env:PreferredToolArchitecture = 'arm64'
    Write-Evidence 'build-host-selection.json' ([ordered]@{
        originalMSBuild=$original;selectedMSBuild=$native;requestedPreferredToolArchitecture='arm64'
        msbuildSha256=(Get-FileHash -LiteralPath $native -Algorithm SHA256).Hash
        availableNativeCompiler=$compiler;availableNativeCompilerSha256=(Get-FileHash -LiteralPath $compiler -Algorithm SHA256).Hash
        interpretation='Native build-host selection only; runtime suite success still requires actual completion'
    })
}

function Get-EnvironmentEvidence {
    Import-Module (Join-Path $repoRoot 'Build/VisualStudioDiscovery.psm1') -Force
    $msbuildPath = Find-RedPrismMSBuild
    if (-not $msbuildPath -or [Diagnostics.FileVersionInfo]::GetVersionInfo($msbuildPath).FileMajorPart -ne 18) {
        throw 'Canonical Visual Studio 2026 MSBuild discovery did not succeed.'
    }
    $vsPath = Find-RedPrismVisualStudioInstallationOfMSBuild $msbuildPath
    $os = Get-CimInstance Win32_OperatingSystem
    $cpu = Get-CimInstance Win32_Processor | Select-Object -First 1
    $disk = [IO.DriveInfo]::new([IO.Path]::GetPathRoot($repoRoot))
    return [ordered]@{
        utc=[DateTime]::UtcNow.ToString('o');commit=$env:GITHUB_SHA;ref=$env:GITHUB_REF
        nativeOsArchitecture=[Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString()
        processArchitecture=[Runtime.InteropServices.RuntimeInformation]::ProcessArchitecture.ToString()
        sessionId=[Diagnostics.Process]::GetCurrentProcess().SessionId;userInteractive=[Environment]::UserInteractive
        runnerName=$env:RUNNER_NAME;imageOS=$env:ImageOS;imageVersion=$env:ImageVersion
        runId=$env:GITHUB_RUN_ID;runAttempt=$env:GITHUB_RUN_ATTEMPT
        osVersion=$os.Version;osBuild=$os.BuildNumber;processor=$cpu.Name;logicalProcessors=[Environment]::ProcessorCount
        totalMemoryBytes=([uint64]$os.TotalVisibleMemorySize*1024);freeMemoryBytes=([uint64]$os.FreePhysicalMemory*1024)
        workspaceFreeBytes=$disk.AvailableFreeSpace;msbuildPath=$msbuildPath;visualStudioPath=$vsPath
        defaultVCToolsVersion=(Get-Content -LiteralPath (Join-Path $vsPath 'VC/Auxiliary/Build/Microsoft.VCToolsVersion.default.txt') -Raw).Trim()
        vcpkgPin=(Get-Content -LiteralPath (Join-Path $repoRoot 'vcpkg-tool.json') -Raw | ConvertFrom-Json)
        dxuiPin=(Get-Content -LiteralPath (Join-Path $repoRoot 'Dependencies/DxUi.lock.json') -Raw | ConvertFrom-Json)
        platform='ARM64';configuration='Release';maxCpuCount=2;performanceQualification=$false;p1Done=$false
    }
}
function Get-ProductEvidence {
    $products = [ordered]@{}
    foreach ($name in @('RedPrism','HostTests','DocumentTests','CompositionTests','IoTests')) {
        $path = Join-Path $repoRoot ".build/ARM64/Release/$name.exe"
        $stream = [IO.File]::OpenRead($path)
        $reader = [IO.BinaryReader]::new($stream)
        try {
            if ($reader.ReadUInt16() -ne 0x5a4d) { throw "Invalid image header: $name" }
            $stream.Position = 0x3c
            $stream.Position = $reader.ReadUInt32()
            if ($reader.ReadUInt32() -ne 0x4550 -or $reader.ReadUInt16() -ne 0xaa64) {
                throw "The $name product is not an ARM64 PE image."
            }
        } finally { $reader.Dispose();$stream.Dispose() }
        foreach ($extension in @('exe','pdb')) {
            $file = Join-Path $repoRoot ".build/ARM64/Release/$name.$extension"
            $products["$name.$extension"] = [ordered]@{bytes=(Get-Item -LiteralPath $file).Length;sha256=(Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash}
        }
    }
    return $products
}

$child = $null
$stdoutStream = $null
$stderrStream = $null
try {
    if ($env:REDPRISM_CONFIGURATION -cne 'Release') { throw 'Initial feasibility supports exactly Release.' }
    if ([Runtime.InteropServices.RuntimeInformation]::OSArchitecture -ne [Runtime.InteropServices.Architecture]::Arm64) {
        throw 'Refusing native ARM64 qualification on a non-ARM64 operating system.'
    }
    if ($env:GITHUB_REF -cne 'refs/heads/codex/p1-opacity-host-proof') { throw 'Unapproved validation branch.' }
    if ($env:EVENT_NAME -ceq 'push') {
        if ($env:PUSH_BEFORE -cnotmatch '^[0-9a-f]{40}$') { throw 'Missing original push commit.' }
        $message = (Get-RepoGit -Arguments @('log','-1','--format=%B')) -join "`n"
        if (-not $message.Contains('[arm64-validation]',[StringComparison]::Ordinal)) { throw 'Missing explicit validation marker.' }
        $changed = @(Get-RepoGit -Arguments @('diff','--name-only',$env:PUSH_BEFORE,$env:GITHUB_SHA))
        $allowed = @('.github/scripts/Run-NativeArm64.ps1','.github/workflows/native-arm64-feasibility.yml')
        if ($changed.Count -eq 0 -or @($changed | Where-Object {$_ -cnotin $allowed}).Count -ne 0) {
            throw 'Bootstrap push includes paths outside the reviewed CI pair.'
        }
    } elseif ($env:EVENT_NAME -cne 'workflow_dispatch') { throw 'Unsupported workflow event.' }
    Assert-CleanSource
    $before = Get-SourceSnapshot
    Write-Evidence 'source-before.json' $before
    Initialize-NativeBuildHost
    Write-Evidence 'environment-before.json' (Get-EnvironmentEvidence)

    $info = [Diagnostics.ProcessStartInfo]::new()
    $info.FileName = Join-Path $PSHOME 'pwsh.exe'
    $info.WorkingDirectory = $repoRoot
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    foreach ($argument in @('-NoProfile','-NonInteractive','-File',(Join-Path $repoRoot 'test.ps1'),'-Platform','ARM64','-Configuration','Release','-MaxCpuCount','2')) {
        $info.ArgumentList.Add($argument)
    }
    $stdoutStream = [IO.File]::Open((Join-Path $outRoot 'test.stdout.log'),[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
    $stderrStream = [IO.File]::Open((Join-Path $outRoot 'test.stderr.log'),[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
    $child = [Diagnostics.Process]::new()
    $child.StartInfo = $info
    if (-not $child.Start()) { throw 'Canonical test child did not start.' }
    Write-Evidence 'started.json' ([ordered]@{pid=$child.Id;utc=[DateTime]::UtcNow.ToString('o');configuration='Release';platform='ARM64'})
    $stdoutCopy = $child.StandardOutput.BaseStream.CopyToAsync($stdoutStream)
    $stderrCopy = $child.StandardError.BaseStream.CopyToAsync($stderrStream)
    $elapsed = [Diagnostics.Stopwatch]::StartNew()
    while (-not $child.WaitForExit(30000) -and $elapsed.Elapsed.TotalMinutes -lt 170) {
        $osNow = Get-CimInstance Win32_OperatingSystem
        $last = Get-Content -LiteralPath (Join-Path $outRoot 'test.stdout.log') -Tail 1 -ErrorAction SilentlyContinue
        $progress = [ordered]@{utc=[DateTime]::UtcNow.ToString('o');elapsedSeconds=[math]::Round($elapsed.Elapsed.TotalSeconds)
            freeMemoryBytes=([uint64]$osNow.FreePhysicalMemory*1024);lastOutput=[string]$last}
        ($progress | ConvertTo-Json -Compress) | Add-Content -LiteralPath (Join-Path $outRoot 'progress.jsonl') -Encoding utf8
        Write-Host ("Native ARM64 canonical run: {0}s, free memory {1} MiB, last output: {2}" -f $progress.elapsedSeconds,([math]::Round($progress.freeMemoryBytes/1MB)),$last)
    }
    if (-not $child.HasExited) {
        Write-Evidence 'child-result.json' ([ordered]@{pid=$child.Id;exit=$null;status='timeout-child-not-terminated';nativeQualified=$false})
        throw 'Canonical test exceeded its bounded interval; no successful runtime result is claimed.'
    }
    # Record the actual process exit before waiting for inherited output handles to drain.
    Write-Evidence 'child-result.json' ([ordered]@{pid=$child.Id;exit=$child.ExitCode;utc=[DateTime]::UtcNow.ToString('o')})
    $drain = [Threading.Tasks.Task]::WhenAll([Threading.Tasks.Task[]]@($stdoutCopy,$stderrCopy))
    if (-not $drain.Wait(30000)) { throw 'Canonical child exited, but its raw output stream did not drain within 30 seconds.' }
    $drain.GetAwaiter().GetResult()
    $stdoutStream.Flush();$stderrStream.Flush()
    $after = Get-SourceSnapshot
    Write-Evidence 'source-after.json' $after
    Write-Evidence 'environment-after.json' (Get-EnvironmentEvidence)
    Assert-CleanSource
    if (($before | ConvertTo-Json -Depth 4 -Compress) -cne ($after | ConvertTo-Json -Depth 4 -Compress)) {
        throw 'Tracked source bytes changed during the canonical run.'
    }
    if ($child.ExitCode -ne 0) { throw "Canonical test.ps1 failed with actual exit $($child.ExitCode)." }
    Write-Evidence 'products.json' (Get-ProductEvidence)
    Write-Evidence 'result.json' ([ordered]@{status='canonical-native-arm64-release-exited-zero';exit=0;sourceBookends=$true;nativeArm64Release=$true;debugQualified=$false;asanQualified=$false;g1Qualified=$false;p1Done=$false})
    Write-Host 'Canonical native ARM64 Release exited 0; source bookends and five ARM64 products verified.'
} catch {
    Write-Evidence 'failure.json' ([ordered]@{utc=[DateTime]::UtcNow.ToString('o');message=$_.Exception.Message;nativeQualified=$false;p1Done=$false})
    throw
} finally {
    if ($stdoutStream) {$stdoutStream.Dispose()}
    if ($stderrStream) {$stderrStream.Dispose()}
    if ($child) {$child.Dispose()}
}
