[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# One bounded feasibility run. Repository entrypoints retain every existing build/test gate.
# Observe qualified shutdown and portable Polygonal DPI source; every existing gate remains required.
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
function Install-PinnedNativeToolchain {
    # This runs only inside the already-validated disposable native CI job, before canonical work.
    if ([Runtime.InteropServices.RuntimeInformation]::ProcessArchitecture -ne [Runtime.InteropServices.Architecture]::Arm64) {
        throw 'Pinned native toolchain provisioning requires ARM64 PowerShell.'
    }
    $installPath = 'C:\P1NativeTools1452'
    if (Test-Path -LiteralPath $installPath) { throw 'Refusing an existing pinned native toolchain installation.' }
    $inputRoot = Join-Path $outRoot 'toolchain-provisioning'
    New-Item -ItemType Directory -Path $inputRoot | Out-Null
    $inputs = @(
        @{name='vs_BuildTools.exe';sha256='BDA908A7951B50B9005370514EA8B01C4E618415BED1E2D1F14EAC2868B45C06';uri='https://download.visualstudio.microsoft.com/download/pr/491c9921-628b-407b-a732-61b530a808e7/bda908a7951b50b9005370514ea8b01c4e618415bed1e2d1f14eac2868b45c06/vs_BuildTools.exe'},
        @{name='VisualStudio.18.Preview.chman';sha256='5176A053353DBFF233C35399B40BB182A7B7194B4A279F547C6CE11901A97396';uri='https://download.visualstudio.microsoft.com/download/pr/491c9921-628b-407b-a732-61b530a808e7/e97cc81fc995069ea4787559d4c35401b86a9e6b2ce0815e4178701c1ae5c44c/VisualStudio.18.Preview.chman'},
        @{name='VisualStudioPreview.vsman';sha256='1B8B0F296EE61BB3B10D33E5320D643028115F7862740F4B00E5D8359E0F8E52';uri='https://download.visualstudio.microsoft.com/download/pr/491c9921-628b-407b-a732-61b530a808e7/2f9bb2412eec996157b4e904687d46cd9398737beb82c8f9bb6822e47636ce33/VisualStudioPreview.vsman'}
    )
    $observed = foreach ($pinnedInput in $inputs) {
        $path = Join-Path $inputRoot $pinnedInput.name
        Invoke-WebRequest -Uri $pinnedInput.uri -OutFile $path -TimeoutSec 120
        $actual = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
        if ($actual -cne $pinnedInput.sha256) {
            Write-Evidence 'toolchain-provisioning-integrity-failure.json' ([ordered]@{
                name=$pinnedInput.name;uri=$pinnedInput.uri;expectedSha256=$pinnedInput.sha256;actualSha256=$actual
            })
            throw "Pinned toolchain input hash differs: $($pinnedInput.name)"
        }
        [ordered]@{name=$pinnedInput.name;uri=$pinnedInput.uri;sha256=$actual;bytes=(Get-Item -LiteralPath $path).Length}
    }
    $bootstrapper = Join-Path $inputRoot 'vs_BuildTools.exe'
    $signature = Get-AuthenticodeSignature -LiteralPath $bootstrapper
    if ($signature.Status -ne [Management.Automation.SignatureStatus]::Valid -or
        $signature.SignerCertificate.Subject -notmatch '(^|,\s*)O=Microsoft Corporation(,|$)') {
        throw 'Pinned toolchain bootstrapper has no valid Microsoft publisher signature.'
    }
    Write-Evidence 'toolchain-provisioning-inputs.json' ([ordered]@{
        inputs=$observed;signatureStatus=$signature.Status.ToString();signer=$signature.SignerCertificate.Subject
        installPath=$installPath;catalogVersion='18.12.12316.0';requiredDefaultVCToolsVersion='14.52.37025'
        interpretation='Pinned provisioning inputs only; installation, actual products and runtime remain unqualified'
    })
    $arguments = @('--quiet','--wait','--norestart','--nocache','--installPath',$installPath,
        '--productId','Microsoft.VisualStudio.Product.BuildTools','--channelId','VisualStudio.18.Preview',
        '--channelUri',('"'+(Join-Path $inputRoot 'VisualStudio.18.Preview.chman')+'"'),
        '--installChannelUri',('"'+(Join-Path $inputRoot 'VisualStudio.18.Preview.chman')+'"'),
        '--installCatalogUri',('"'+(Join-Path $inputRoot 'VisualStudioPreview.vsman')+'"'),
        '--add','Microsoft.VisualStudio.Workload.MSBuildTools',
        '--add','Microsoft.VisualStudio.Workload.VCTools',
        '--add','Microsoft.VisualStudio.Component.VC.Tools.ARM64',
        '--add','Microsoft.VisualStudio.Component.Windows11SDK.26100')
    $started = [DateTime]::UtcNow
    $installerFailed = $false
    Write-Host 'Installing pinned BuildTools 18.12.12316.0 / native MSVC 14.52.37025 in the disposable runner.'
    try {
        $installer = Start-Process -FilePath $bootstrapper -ArgumentList $arguments -WorkingDirectory $inputRoot -WindowStyle Hidden -PassThru -Wait `
            -RedirectStandardOutput (Join-Path $inputRoot 'installer.stdout.log') `
            -RedirectStandardError (Join-Path $inputRoot 'installer.stderr.log')
        Write-Evidence 'toolchain-provisioning-result.json' ([ordered]@{
            pid=$installer.Id;exit=$installer.ExitCode;startedUtc=$started.ToString('o');endedUtc=[DateTime]::UtcNow.ToString('o')
            arguments=$arguments;installPath=$installPath;runtimeQualified=$false
        })
        if ($installer.ExitCode -ne 0) {
            throw "Pinned native toolchain installation exited $($installer.ExitCode); restart-required outcomes cannot qualify this job."
        }
    } catch {
        $installerFailed = $true
        throw
    } finally {
        try {
            $logs = Join-Path $inputRoot 'InstallerLogs'
            New-Item -ItemType Directory -Path $logs -Force | Out-Null
            Get-ChildItem -LiteralPath $env:TEMP -File -Filter 'dd_*.log' -ErrorAction Stop |
                Where-Object { $_.LastWriteTimeUtc -ge $started } |
                ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $logs $_.Name) }
        } catch {
            $collectionError = $_
            try {
                Write-Evidence 'toolchain-provisioning-log-collection-failure.json' ([ordered]@{
                    message=$collectionError.Exception.Message;primaryInstallerFailure=$installerFailed
                })
            } catch { Write-Warning ('Cannot retain log-collection failure receipt: ' + $_.Exception.Message) }
            if (-not $installerFailed) { throw $collectionError }
            Write-Warning ('Installer log collection also failed: ' + $collectionError.Exception.Message)
        }
    }
    $native = Join-Path $installPath 'MSBuild/Current/Bin/arm64/MSBuild.exe'
    $compiler = Join-Path $installPath 'VC/Tools/MSVC/14.52.37025/bin/HostARM64/ARM64/cl.exe'
    if (-not (Test-Path -LiteralPath $native -PathType Leaf) -or -not (Test-Path -LiteralPath $compiler -PathType Leaf)) {
        throw 'Pinned installation did not produce native ARM64 MSBuild and compiler.'
    }
    $nativeHash = (Get-FileHash -LiteralPath $native).Hash
    $compilerHash = (Get-FileHash -LiteralPath $compiler).Hash
    $expectedNativeHash = '7C67299F429B832719ACBDC43240F6BF5D50B26014E7462E361392C0A261A60B'
    $expectedCompilerHash = '745C496597A6658804E1A8DBD514CE916783E342B31BFE974ED2A867E06078C8'
    $productsMatched = $nativeHash -ceq $expectedNativeHash -and $compilerHash -ceq $expectedCompilerHash
    Write-Evidence 'toolchain-provisioning-products.json' ([ordered]@{
        nativeMSBuild=$native;nativeMSBuildSha256=$nativeHash;nativeCompiler=$compiler;nativeCompilerSha256=$compilerHash
        expectedNativeMSBuildSha256=$expectedNativeHash;expectedNativeCompilerSha256=$expectedCompilerHash
        matched=$productsMatched
        interpretation='Installed tool bytes only; default-version guard and canonical product/runtime checks still follow'
    })
    if (-not $productsMatched) {
        throw 'Installed native tool bytes differ from the independently verified pinned AA64 package members.'
    }
    return $native
}

function Initialize-NativeBuildHost {
    param([string] $MSBuildPath)
    Import-Module (Join-Path $repoRoot 'Build/VisualStudioDiscovery.psm1') -Force
    $original = if ($MSBuildPath) { $MSBuildPath } else { Find-RedPrismMSBuild }
    $installation = Find-RedPrismVisualStudioInstallationOfMSBuild $original
    $native = Join-Path $installation 'MSBuild/Current/Bin/arm64/MSBuild.exe'
    if (-not (Test-Path -LiteralPath $native -PathType Leaf)) {
        throw 'The selected Visual Studio installation has no ARM64-hosted MSBuild; refusing another emulated build.'
    }
    if ([Diagnostics.FileVersionInfo]::GetVersionInfo($native).FileMajorPart -ne 18) {
        throw 'Native MSBuild is outside the required Visual Studio 2026 version.'
    }
    $version = (Get-Content -LiteralPath (Join-Path $installation 'VC/Auxiliary/Build/Microsoft.VCToolsVersion.default.txt') -Raw).Trim()
    $requiredVersion = '14.52.37025'
    Write-Evidence 'toolset-requirement.json' ([ordered]@{
        requiredDefaultVCToolsVersion=$requiredVersion;actualDefaultVCToolsVersion=$version
        selectedInstallation=$installation;matched=($version -ceq $requiredVersion)
        interpretation='Default-toolset preflight only; actual compiler/dependency products and native runtime still require independent qualification'
    })
    if ($version -cne $requiredVersion) {
        throw "Native ARM64 requires default MSVC $requiredVersion; selected installation provides $version. No canonical work started."
    }
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
        platform='ARM64';configuration='Release';maxCpuCount=2;selfTestFailureBoundSeconds=5400;performanceQualification=$false;p1Done=$false
    }
}
function Get-NativeProgressEvidence([string] $RepositoryRoot, [int] $CanonicalChildId, [double] $ElapsedSeconds) {
    # Optional diagnostics must not abort canonical tests when CIM, a log or a process disappears.
    $progress = [ordered]@{
        utc=[DateTime]::UtcNow.ToString('o');elapsedSeconds=[math]::Round($ElapsedSeconds)
        freeMemoryBytes=$null;lastOutput='';applicationLogAvailable=$false
        applicationStderrAvailable=$false
        ownedApplicationQueryAvailable=$false;ownedApplicationCount=$null;ownedApplications=@()
    }
    $errors = [Collections.Generic.List[string]]::new()
    try {
        $osNow = Get-CimInstance Win32_OperatingSystem -OperationTimeoutSec 5 -ErrorAction Stop
        $progress['freeMemoryBytes'] = [uint64]$osNow.FreePhysicalMemory*1024
    } catch {
        $message = $_.Exception.Message
        $errors.Add('memory: ' + $message.Substring(0, [math]::Min(256, $message.Length)))
    }
    try {
        $parentLog = Join-Path $RepositoryRoot '.build/native-arm-ci/test.stdout.log'
        if (Test-Path -LiteralPath $parentLog -PathType Leaf) {
            $progress['lastOutput'] = [string](Get-Content -LiteralPath $parentLog -Tail 1 -ErrorAction Stop)
        }
        $applicationLog = Join-Path $RepositoryRoot '.build/ARM64/Release/RedPrism.self-test.log'
        if (Test-Path -LiteralPath $applicationLog -PathType Leaf) {
            $applicationInfo = Get-Item -LiteralPath $applicationLog -ErrorAction Stop
            $progress['applicationStdoutBytes'] = $applicationInfo.Length
            $progress['applicationLastWriteUtc'] = $applicationInfo.LastWriteTimeUtc.ToString('o')
            $progress['applicationLastOutput'] = [string](Get-Content -LiteralPath $applicationLog -Tail 1 -ErrorAction Stop)
            $progress['applicationLogAvailable'] = $true
        }
        # Self-test stage/fact output may be on stderr while stdout remains empty.
        $applicationErrorLog = Join-Path $RepositoryRoot '.build/ARM64/Release/RedPrism.self-test.err.log'
        if (Test-Path -LiteralPath $applicationErrorLog -PathType Leaf) {
            $errorInfo = Get-Item -LiteralPath $applicationErrorLog -ErrorAction Stop
            $progress['applicationStderrBytes'] = $errorInfo.Length
            $progress['applicationStderrLastWriteUtc'] = $errorInfo.LastWriteTimeUtc.ToString('o')
            $progress['applicationLastErrorOutput'] = [string](Get-Content -LiteralPath $applicationErrorLog -Tail 1 -ErrorAction Stop)
            $progress['applicationStderrAvailable'] = $true
        }
    } catch {
        $message = $_.Exception.Message
        $errors.Add('logs: ' + $message.Substring(0, [math]::Min(256, $message.Length)))
    }
    try {
        $applicationPath = [IO.Path]::GetFullPath((Join-Path $RepositoryRoot '.build/ARM64/Release/RedPrism.exe'))
        $ownedApplications = @(Get-CimInstance Win32_Process -OperationTimeoutSec 5 -ErrorAction Stop `
            -Filter "ParentProcessId=$CanonicalChildId AND Name='RedPrism.exe'" | Where-Object {
                $_.ExecutablePath -and [IO.Path]::GetFullPath($_.ExecutablePath) -ieq $applicationPath
            })
        $progress['ownedApplicationCount'] = $ownedApplications.Count
        $progress['ownedApplications'] = @($ownedApplications | Select-Object -First 4 | ForEach-Object {
            [ordered]@{pid=$_.ProcessId;parent=$_.ParentProcessId;created=[string]$_.CreationDate
                userCpu100ns=[uint64]$_.UserModeTime;kernelCpu100ns=[uint64]$_.KernelModeTime
                workingSetBytes=[uint64]$_.WorkingSetSize}
        })
        $progress['ownedApplicationQueryAvailable'] = $true
    } catch {
        $message = $_.Exception.Message
        $errors.Add('application-process: ' + $message.Substring(0, [math]::Min(256, $message.Length)))
    }
    $progress['diagnosticErrors'] = $errors.ToArray()
    return $progress
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
    $provisionedMSBuild = Install-PinnedNativeToolchain
    Initialize-NativeBuildHost -MSBuildPath $provisionedMSBuild
    Write-Evidence 'environment-before.json' (Get-EnvironmentEvidence)

    $info = [Diagnostics.ProcessStartInfo]::new()
    $info.FileName = Join-Path $PSHOME 'pwsh.exe'
    $info.WorkingDirectory = $repoRoot
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    foreach ($argument in @('-NoProfile','-NonInteractive','-File',(Join-Path $repoRoot 'test.ps1'),'-Platform','ARM64','-Configuration','Release','-MaxCpuCount','2','-SelfTestTimeoutSeconds','5400')) {
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
        $progress = Get-NativeProgressEvidence -RepositoryRoot $repoRoot -CanonicalChildId $child.Id -ElapsedSeconds $elapsed.Elapsed.TotalSeconds
        ($progress | ConvertTo-Json -Depth 6 -Compress) | Add-Content -LiteralPath (Join-Path $outRoot 'progress.jsonl') -Encoding utf8
        $freeMemory = if ($null -ne $progress['freeMemoryBytes']) { [math]::Round($progress['freeMemoryBytes']/1MB) } else { 'unavailable' }
        Write-Host ("Native ARM64 canonical run: {0}s, free memory {1} MiB, last output: {2}; Application stderr: {3}" -f $progress['elapsedSeconds'],$freeMemory,$progress['lastOutput'],$progress['applicationLastErrorOutput'])
    }
    if (-not $child.HasExited) {
        Write-Evidence 'child-result.json' ([ordered]@{pid=$child.Id;exit=$null;status='timeout-child-not-terminated';nativeQualified=$false})
        throw 'Canonical test exceeded its bounded interval; no successful runtime result is claimed.'
    }
    # Record the actual process exit before waiting for inherited output handles to drain.
    Write-Evidence 'child-result.json' ([ordered]@{pid=$child.Id;exit=$child.ExitCode;utc=[DateTime]::UtcNow.ToString('o')})
    Write-Evidence 'terminal-progress.json' (Get-NativeProgressEvidence -RepositoryRoot $repoRoot -CanonicalChildId $child.Id -ElapsedSeconds $elapsed.Elapsed.TotalSeconds)
    # Preserve closing identity before any raw-pipe drain can refuse a failed child with a live descendant.
    $after = Get-SourceSnapshot
    Write-Evidence 'source-after.json' $after
    Write-Evidence 'environment-after.json' (Get-EnvironmentEvidence)
    $completeProducts = $true
    foreach ($name in @('RedPrism','HostTests','DocumentTests','CompositionTests','IoTests')) {
        foreach ($extension in @('exe','pdb')) {
            if (-not (Test-Path -LiteralPath (Join-Path $repoRoot ".build/ARM64/Release/$name.$extension") -PathType Leaf)) {
                $completeProducts = $false
            }
        }
    }
    if ($completeProducts) { Write-Evidence 'products.json' (Get-ProductEvidence) }
    Assert-CleanSource
    if (($before | ConvertTo-Json -Depth 4 -Compress) -cne ($after | ConvertTo-Json -Depth 4 -Compress)) {
        throw 'Tracked source bytes changed during the canonical run.'
    }
    $drain = [Threading.Tasks.Task]::WhenAll([Threading.Tasks.Task[]]@($stdoutCopy,$stderrCopy))
    if (-not $drain.Wait(30000)) { throw 'Canonical child exited, but its raw output stream did not drain within 30 seconds.' }
    $drain.GetAwaiter().GetResult()
    $stdoutStream.Flush();$stderrStream.Flush()
    if ($child.ExitCode -ne 0) { throw "Canonical test.ps1 failed with actual exit $($child.ExitCode)." }
    if (-not $completeProducts) { throw 'Canonical test exited zero without all five executable/PDB pairs.' }
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
