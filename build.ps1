<#!
.SYNOPSIS
Builds, cleans, rebuilds, or runs the RedPrism solution.
.PARAMETER DxUiSdkInputDescriptor
Independent pinned Release SDK descriptor below .build. Exact misses use source; Debug/ASan and analysis use source.
#>
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release', 'ASan Debug')]
    [string] $Configuration = 'Debug',

    [ValidateSet('x64', 'ARM64')]
    [string] $Platform = 'x64',

    [switch] $Clean,
    [switch] $Rebuild,
    [switch] $Analyze,
    [switch] $Run,
    [switch] $ProductionOnly,

    [ValidateRange(0, 256)]
    [int] $MaxCpuCount = 0,

    [ValidateRange(0, 65535)]
    [int] $BuildNumber = 0,
    [string] $DxUiSdkInputDescriptor = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if ($env:CI_NATIVE_BUILDS_DISABLED -eq 'true') { throw 'Native builds are forbidden in compiler-free PR checks.' }
if($DxUiSdkInputDescriptor -and ($Configuration -cne 'Release' -or $Clean -or $Analyze)){throw 'Pinned SDK inputs require an ordinary Release build.'}
$env:RedPrismEnableTests = if ($ProductionOnly) { 'false' } else { 'true' }
$nativeEntryElapsed = [Diagnostics.Stopwatch]::StartNew()
Import-Module (Join-Path $PSScriptRoot 'Build/BuildConsole.psm1') -ErrorAction Stop

if ($Clean -and $Rebuild) {
    throw 'Choose either -Clean or -Rebuild, not both.'
}
if ($Clean -and $Run) {
    throw '-Run cannot be combined with -Clean.'
}
if ($Clean -and $Analyze) {
    throw '-Analyze cannot be combined with -Clean.'
}

$repoRoot = Split-Path -Parent $PSCommandPath
$solutionPath = Join-Path $repoRoot 'RedPrism.sln'
$executable = Join-Path $repoRoot ".build\$Platform\$Configuration\RedPrism.exe"

Import-Module (Join-Path $repoRoot 'Build\BuildPresentation.psm1') -Force
Import-Module (Join-Path $repoRoot 'Build\BuildPreflight.psm1') -Force
Import-Module (Join-Path $repoRoot 'Build\VisualStudioDiscovery.psm1') -Force
$useInteractiveTerminal = Test-RedPrismInteractiveTerminal
Write-RedPrismBuildBanner -UseColor $useInteractiveTerminal

function Resolve-BuildNumber {
    param([int] $Requested)
    if ($Requested -ne 0) {
        return $Requested
    }
    Push-Location $repoRoot
    try {
        $count = & git.exe rev-list --count HEAD 2>$null
        if ($LASTEXITCODE -eq 0 -and $count -match '^\d+$') {
            return [int] $count
        }
    } finally {
        Pop-Location
    }
    return 0
}

Assert-RedPrismBuildOutputProcessNotRunning -ExpectedExecutablePath $executable
$buildAttestation=Join-Path $repoRoot ".build/$Platform/$Configuration/build-attestation.json"
if(-not $Clean -and (Test-Path -LiteralPath $buildAttestation)){Remove-Item -LiteralPath $buildAttestation -ErrorAction Stop}

Import-Module (Join-Path $repoRoot 'Build/ScopedTesting.psm1') -Force
$scopedSourceBefore=Get-ScopedSourceIdentity $repoRoot -CompiledOnly
$scopedEnvironmentBefore=Get-ScopedEnvironmentIdentity
Import-Module (Join-Path $repoRoot '.ci-engine/Tools/Modules/Ci.Policy.psm1') -ErrorAction Stop
$BuildNumber = Resolve-BuildNumber -Requested $BuildNumber
Write-Host ("Target: {0} | {1} | version 0.1.{2}.0" -f $Platform, $Configuration, $BuildNumber) -ForegroundColor Magenta

# Share one tool-host choice with dependency identity/restore, the solution and its provenance.
# Cross-targeting ARM64 otherwise defaults to x86-hosted tools even under amd64 MSBuild.
$previousToolArchitecture = [Environment]::GetEnvironmentVariable('PreferredToolArchitecture', 'Process')
$sdkInput=$null;$sdkFailure=$null;$sdkProvenanceWritten=$false
try {
    if ($null -eq $previousToolArchitecture -and
        [Runtime.InteropServices.RuntimeInformation]::ProcessArchitecture -eq [Runtime.InteropServices.Architecture]::X64) {
        [Environment]::SetEnvironmentVariable('PreferredToolArchitecture', 'x64', 'Process')
    }

    $msbuild = Find-RedPrismMSBuild
    $sdkProperties=@()
    if($DxUiSdkInputDescriptor){
        $engine=Join-Path $repoRoot '.ci-engine'
        Import-Module (Join-Path $engine 'Tools/Modules/Ci.SdkConsumer.psm1')
        $sdkInput=Open-CiApplicationSdkInput -ProjectRoot $repoRoot -Descriptor $DxUiSdkInputDescriptor -Platform $Platform -Configuration $Configuration -MSBuildPath $msbuild
        if($null -ne $sdkInput){
            foreach($path in @($sdkInput.SdkRoot,$sdkInput.LeasePath,$engine,$repoRoot)){if($path -match '[%;"\r\n]'){throw 'SDK paths contain unsupported MSBuild delimiters.'}}
            $sdkProperties=@("/p:RedPrismDxUiSdkRoot=$($sdkInput.SdkRoot)","/p:CiSdkRoot=$($sdkInput.SdkRoot)","/p:CiSdkProjectRoot=$repoRoot","/p:CiSdkLeasePath=$($sdkInput.LeasePath)","/p:RedPrismCiEngineRoot=$engine")
            if($sdkInput.Evidence.recipe -ceq 'PinnedQualifiedReleaseSdkV2'){$sdkProperties+=@('/p:CiSdkContract=QualifiedAbiV2')}
            Write-Host "Using byte-verified pinned DxUi SDK input $($sdkInput.Evidence.inputKey)."
        }else{Write-Host 'Exact pinned SDK input unavailable; retaining the governed source recipe.'}
    }
    $target = if ($Clean) { 'Clean' } elseif ($Rebuild -or $Analyze) { 'Rebuild' } else { 'Build' }
    if ($ProductionOnly) {
        if ($Configuration -ne 'Release' -or $Clean -or $Analyze) { throw 'ProductionOnly requires an ordinary Release build.' }
        $productionTargets = @([regex]::Matches([IO.File]::ReadAllText($solutionPath),
            'Project\("\{[^}]+\}"\)\s*=\s*"(?<name>[^"]+)",\s*"(?<path>[^"]+\.vcxproj)"') |
            Where-Object { $_.Groups['path'].Value -notmatch '^Tests[\\/]' } |
            ForEach-Object { $_.Groups['name'].Value })
        if (-not $productionTargets.Count) { throw 'No production solution targets were selected.' }
        $productionTargetArgument = (@($productionTargets | ForEach-Object { if ($Rebuild) { $_ + ':Rebuild' } else { $_ } }) -join ';')
    }
    if (-not $Clean) {
        Write-Host '[1/2] Dependencies' -ForegroundColor Cyan
        & (Join-Path $repoRoot 'vcpkg-install.ps1') -Platform $Platform -Asan:($Configuration -eq 'ASan Debug') -MSBuildPath $msbuild
        if($null -eq $sdkInput){& (Join-Path $repoRoot 'restore-dxui.ps1') -Platform $Platform -MSBuildPath $msbuild -CheckUpdates}
        Write-Host ''
    }

    if($null -ne $sdkInput){
        $sdkLogDirectory=Join-Path $repoRoot '.build/logs'
        [void][IO.Directory]::CreateDirectory($sdkLogDirectory)
        $sdkLog=Join-Path $sdkLogDirectory ('sdk-profile-'+[guid]::NewGuid().ToString('N')+'.log')
        $sdkArguments=@((Join-Path $repoRoot 'RedPrism/RedPrism.vcxproj'),'/nologo','/v:minimal','/t:ValidateDxUiSdkProfile;CiValidateSdkConsumer',"/p:Platform=$Platform","/p:Configuration=$Configuration","/flp:LogFile=$sdkLog;Verbosity=normal")+$sdkProperties
        Invoke-CiNativeCommand -FilePath $msbuild -WorkingDirectory $repoRoot -Arguments $sdkArguments
        if($LASTEXITCODE -ne 0){throw "Pinned SDK actual-project profile preflight failed. Preserve $sdkLog"}
    }

    $workerArgument = if ($MaxCpuCount -gt 0) { "/m:$MaxCpuCount" } else { '/m' }
    $logDir = Join-Path $repoRoot '.build\logs'
    New-Item -ItemType Directory -Path $logDir -Force | Out-Null
    $logFlavor = if ($Analyze) { '-Analyze' } else { '' }
    $logPath = Join-Path $logDir ("{0}-{1}-{2}{3}-{4}-{5}.log" -f $target, $Platform, ($Configuration -replace '\s', ''), $logFlavor, [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ'), [guid]::NewGuid().ToString('N'))
    $arguments = @(
        $solutionPath,
        $(if ($ProductionOnly) { "/t:$productionTargetArgument" } else { "/t:$target" }),
        "/p:Configuration=$Configuration",
        "/p:Platform=$Platform",
        "/p:RedPrismBuildNumber=$BuildNumber",
        "/p:RedPrismEnableTests=$env:RedPrismEnableTests",
        $workerArgument,
        '/nologo',
        '/verbosity:minimal',
        "/flp:LogFile=$logPath;Verbosity=minimal"
    ) + $sdkProperties
    if ($MaxCpuCount -gt 0) {
        # Bound each C++ compiler batch as well as MSBuild projects; /m alone leaves /MP unbounded.
        $arguments += "/p:CL_MPCount=$MaxCpuCount"
    }
    if ($Analyze) {
        # The root targets scope /analyze and its concurrency plugin to RedPrism projects. Rebuild ensures no stale
        # objects bypass analysis; a distinct log preserves these diagnostics beside ordinary build logs.
        $arguments += '/p:RedPrismAnalyze=true'
    }

    $stageLabel = if ($Clean) { '[1/1]' } else { '[2/2]' }
    Write-Host "$stageLabel $target" -ForegroundColor Cyan
    Write-Host "MSBuild: $msbuild" -ForegroundColor DarkGray
    if ($Analyze) {
        $previousEspExtensions = [Environment]::GetEnvironmentVariable('Esp.Extensions', 'Process')
        $existingExtensions = @($previousEspExtensions -split ';' | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
        if ($existingExtensions -notcontains 'ConcurrencyCheck.dll') {
            $existingExtensions += 'ConcurrencyCheck.dll'
        }
        [Environment]::SetEnvironmentVariable('Esp.Extensions', ($existingExtensions -join ';'), 'Process')
        try {
            Invoke-CiNativeCommand -FilePath $msbuild -WorkingDirectory $repoRoot -Arguments $arguments
            $exitCode = $LASTEXITCODE
        } finally {
            [Environment]::SetEnvironmentVariable('Esp.Extensions', $previousEspExtensions, 'Process')
        }
    } else {
        Invoke-CiNativeCommand -FilePath $msbuild -WorkingDirectory $repoRoot -Arguments $arguments
        $exitCode = $LASTEXITCODE
    }
    if ($exitCode -ne 0) {
        Write-Host "Captured log: $logPath" -ForegroundColor DarkGray
        throw "MSBuild failed with exit code $exitCode."
    }

    Assert-NativeBuildDiagnostics -LogPath $logPath
    if ($Analyze) {
        Write-Host 'Validating native concurrency analysis controls...' -ForegroundColor Cyan
        & (Join-Path $repoRoot 'Tests\BuildProcessTests\CodeAnalysisTests.ps1') `
            -MSBuildPath $msbuild -Platform $Platform -Configuration $Configuration
        if ($LASTEXITCODE -ne 0) {
            throw "Native concurrency analysis controls failed with exit code $LASTEXITCODE."
        }
    }

    if (-not $Clean) {
        if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
            throw "Build succeeded but the expected executable was not found: $executable"
        }
        if ($Platform -eq 'x64') {
            Write-Host 'Checking linked x64 instructions and ISA gate controls...' -ForegroundColor Cyan
            Import-Module (Join-Path $repoRoot 'Build\IsaScan.psm1') -Force
            $scanRoot = Join-Path $repoRoot ".build\IsaScan\x64\$($Configuration -replace '\s', '')"
            $scanNames = if ($ProductionOnly) { @('RedPrism') } else { @('RedPrism', 'HostTests', 'DocumentTests', 'CompositionTests', 'IoTests') }
            foreach ($name in $scanNames) {
                $binary = Join-Path (Split-Path -Parent $executable) "$name.exe"
                $null = Invoke-RedPrismIsaScan -Binary $binary -OutputDirectory (Join-Path $scanRoot $name)
            }
            if (-not $ProductionOnly) {
                & (Join-Path $repoRoot 'Tests\BuildProcessTests\IsaScanTests.ps1') -MSBuildPath $msbuild -Configuration $Configuration
                if ($LASTEXITCODE -ne 0) { throw 'ISA compiler controls failed.' }
            }
        }

        Import-Module (Join-Path $repoRoot 'Build\DxUiProvenance.psm1') -Force
        $provenance = Write-RedPrismDxUiProvenance -RepoRoot $repoRoot -Platform $Platform -Configuration $Configuration -SdkInput $sdkInput
        $sdkProvenanceWritten=$null -ne $sdkInput
        if($null -ne $sdkInput){Close-CiPinnedSdkInput $sdkInput}
        Write-Host "DxUi provenance: $provenance" -ForegroundColor DarkGray
        Import-Module (Join-Path $repoRoot 'Build/ScopedTesting.psm1') -Force
        if ((Get-ScopedSourceIdentity $repoRoot -CompiledOnly) -cne $scopedSourceBefore -or (Get-ScopedEnvironmentIdentity) -cne $scopedEnvironmentBefore) {
            throw 'Source/toolchain environment changed during build; no build attestation was published.'
        }
        Write-ScopedReceipt (Join-Path (Split-Path $executable) 'build-attestation.json') (Get-ScopedBuildAttestationKey $repoRoot $Platform $Configuration $BuildNumber -EnableTests (-not $ProductionOnly))
        Write-NativeBuildResult -Project RedPrism -Platform $Platform -Configuration $Configuration -LogPath $logPath -Artifacts @($executable) -Executable $executable -Duration $nativeEntryElapsed.Elapsed
        if ($Run) {
            $native = [System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString()
            if ($Platform -eq 'ARM64' -and $native -ne 'Arm64') {
                throw 'This host cannot directly run the ARM64 build. Build it without -Run or run it on ARM64 Windows.'
            }
            Start-Process -FilePath $executable
        }
    }

    Write-Host "Captured log: $logPath" -ForegroundColor DarkGray
} catch {
    $sdkFailure=$_
    if($sdkProvenanceWritten){
        foreach($name in @('DxUi.provenance.json','build-attestation.json')){
            try{Remove-Item -LiteralPath (Join-Path $repoRoot ".build/$Platform/$Configuration/$name") -ErrorAction Stop}
            catch{Write-Warning ('Failed SDK provenance cleanup also failed: '+$_.Exception.Message)}
        }
    }
    throw $sdkFailure
} finally {
    try{if($null -ne $sdkInput -and -not $sdkInput.Closed){Close-CiPinnedSdkInput $sdkInput}}
    catch{if($null -eq $sdkFailure){throw}else{Write-Warning ('SDK close also failed: '+$_.Exception.Message)}}
    finally{
    if ($null -eq $previousToolArchitecture) {
        [Environment]::SetEnvironmentVariable('PreferredToolArchitecture', [System.Management.Automation.Language.NullString]::Value, 'Process')
    } else {
        [Environment]::SetEnvironmentVariable('PreferredToolArchitecture', $previousToolArchitecture, 'Process')
    }
    }
}
