<#
.SYNOPSIS
Builds RedPrism (Release) and produces the portable ZIP with its checksum, then proves the package runs from a
clean extraction. Contract: Specs/Build/Build_Packaging.md.

.PARAMETER Platform
x64 or ARM64. The smoke runs only when this host can execute the platform.

.PARAMETER SkipBuild
Package the existing .build\<Platform>\Release output without building.

.PARAMETER SkipSmoke
Skip the clean-extraction smoke (it is skipped anyway when the host cannot run the platform).

.PARAMETER OutputDirectory
Where the ZIP, its .sha256 and the staging folder go. Default: .build\packages (never committed).

.PARAMETER BuildNumber
Overrides the Git commit count that build.ps1 stamps as the third version field.
#>
#Requires -Version 7.4
[CmdletBinding()]
param(
    [ValidateSet('x64', 'ARM64')]
    [string] $Platform = 'x64',

    [switch] $SkipBuild,

    [switch] $SkipSmoke,

    [string] $OutputDirectory = '',

    [ValidateRange(0, 65535)]
    [int] $BuildNumber = 0
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$env:RedPrismEnableTests = 'false'

$repoRoot = Split-Path -Parent $PSCommandPath
$Configuration = 'Release' # the Debug CRT is not redistributable: a package is always Release
$outputRoot = Join-Path $repoRoot ".build\$Platform\$Configuration"
$executable = Join-Path $outputRoot 'RedPrism.exe'
if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path $repoRoot '.build\packages'
}
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
Import-Module (Join-Path $repoRoot 'Build\VisualStudioDiscovery.psm1') -Force

function Find-Dumpbin([string] $VisualStudio) {
    if (-not $VisualStudio) {
        return $null
    }
    $hostFolder = if ($env:PROCESSOR_ARCHITECTURE -eq 'ARM64') { 'Hostarm64\arm64' } else { 'Hostx64\x64' }
    $candidates = @(Get-ChildItem -Path (Join-Path $VisualStudio "VC\Tools\MSVC\*\bin\$hostFolder\dumpbin.exe") -ErrorAction SilentlyContinue |
        Sort-Object FullName -Descending)
    if ($candidates.Count -eq 0) {
        return $null
    }
    return $candidates[0].FullName
}

function Find-RedistCrt([string] $VisualStudio, [string] $RedistPlatform) {
    if (-not $VisualStudio) {
        return $null
    }
    $candidates = @(Get-ChildItem -Path (Join-Path $VisualStudio "VC\Redist\MSVC\*\$RedistPlatform\Microsoft.VC*.CRT") -Directory -ErrorAction SilentlyContinue |
        Sort-Object FullName -Descending)
    if ($candidates.Count -eq 0) {
        return $null
    }
    return $candidates[0].FullName
}

# The DLL names a module imports, as dumpbin lists them (empty when dumpbin is unavailable).
function Get-Dependents([string] $Module, [string] $Dumpbin) {
    if (-not $Dumpbin) {
        return @()
    }
    $lines = & $Dumpbin /nologo /dependents $Module 2>$null
    $names = @()
    foreach ($line in $lines) {
        if ($line -match '^\s+(\S+\.dll)\s*$') {
            $names += $Matches[1]
        }
    }
    return $names
}

# ── Build ────────────────────────────────────────────────────────────────────
if (-not $SkipBuild) {
    & (Join-Path $repoRoot 'build.ps1') -Platform $Platform -Configuration $Configuration -BuildNumber $BuildNumber -ProductionOnly
    if ($LASTEXITCODE -ne 0) {
        throw "build.ps1 failed with exit code $LASTEXITCODE."
    }
}
Import-Module (Join-Path $repoRoot 'Build/ScopedTesting.psm1') -Force
$effectiveBuildNumber = if ($BuildNumber -gt 0) { $BuildNumber } else { Get-ScopedBuildNumber $repoRoot }
$attestation = Join-Path $outputRoot 'build-attestation.json'
if (-not (Test-ScopedReceipt $attestation (Get-ScopedBuildAttestationKey $repoRoot $Platform $Configuration $effectiveBuildNumber -EnableTests $false))) {
    throw 'Packaging requires an exact current test-disabled production build receipt.'
}
if (-not (Test-Path -LiteralPath $executable)) {
    throw "No Release build to package: $executable"
}

# ── Version identity (Common/Version.h stays authoritative; the executable carries it) ─────────────────────────
$info = [Diagnostics.FileVersionInfo]::GetVersionInfo($executable)
if ($info.OriginalFilename -ne 'RedPrism.exe' -or $info.ProductName -ne 'RedPrism') {
    throw 'RedPrism.exe is missing its Windows version identity.'
}
$version = '{0}.{1}.{2}' -f $info.FileMajorPart, $info.FileMinorPart, $info.FileBuildPart
$packageName = "RedPrism-$version-$Platform"
Write-Host ("Package: {0} (file version {1})" -f $packageName, $info.FileVersion) -ForegroundColor Magenta

# ── Stage ────────────────────────────────────────────────────────────────────
$stageRoot = Join-Path $OutputDirectory 'stage'
$stage = Join-Path $stageRoot $packageName
if (Test-Path -LiteralPath $stage) {
    Remove-Item -LiteralPath $stage -Recurse -Force
}
New-Item -ItemType Directory -Path $stage -Force | Out-Null
Copy-Item -LiteralPath $executable -Destination $stage

$visualStudio = Find-RedPrismVisualStudioInstallationPath
$dumpbin = Find-Dumpbin -VisualStudio $visualStudio
$redistPlatform = if ($Platform -eq 'ARM64') { 'arm64' } else { 'x64' }
$redistCrt = Find-RedistCrt -VisualStudio $visualStudio -RedistPlatform $redistPlatform
if (-not $redistCrt) {
    throw 'The Visual C++ redistributable CRT folder was not found under Visual Studio (VC\Redist\MSVC\<version>\<platform>\Microsoft.VC*.CRT).'
}

$staged = [ordered] @{ 'RedPrism.exe' = $executable }
if ($dumpbin) {
    # Walk the import tables from the executable: a dependency next to the executable (vcpkg's app-local copies)
    # or in the redistributable CRT folder is staged and walked in turn; everything else is Windows' own.
    $queue = [Collections.Generic.Queue[string]]::new()
    $queue.Enqueue($executable)
    while ($queue.Count -gt 0) {
        $module = $queue.Dequeue()
        foreach ($name in (Get-Dependents -Module $module -Dumpbin $dumpbin)) {
            if ($staged.Contains($name)) {
                continue
            }
            $local = Join-Path $outputRoot $name
            $redist = Join-Path $redistCrt $name
            $source = if (Test-Path -LiteralPath $local) { $local } elseif (Test-Path -LiteralPath $redist) { $redist } else { $null }
            if ($source) {
                $staged[$name] = $source
                Copy-Item -LiteralPath $source -Destination $stage
                $queue.Enqueue($source)
            }
        }
    }
} else {
    Write-Warning 'dumpbin was not found: staging every DLL next to the executable and the standard CRT set.'
    foreach ($dll in Get-ChildItem -LiteralPath $outputRoot -Filter '*.dll') {
        $staged[$dll.Name] = $dll.FullName
        Copy-Item -LiteralPath $dll.FullName -Destination $stage
    }
    foreach ($name in 'vcruntime140.dll', 'vcruntime140_1.dll', 'msvcp140.dll', 'msvcp140_1.dll', 'msvcp140_2.dll', 'msvcp140_atomic_wait.dll', 'msvcp140_codecvt_ids.dll') {
        $redist = Join-Path $redistCrt $name
        if (Test-Path -LiteralPath $redist) {
            $staged[$name] = $redist
            Copy-Item -LiteralPath $redist -Destination $stage
        }
    }
}

# Third-party notices for the libraries the package carries or compiles in.
$scope = if ($Platform -eq 'ARM64') { 'arm64' } else { 'x64' }
$triplet = if ($Platform -eq 'ARM64') { 'arm64-windows' } else { 'x64-windows' }
$share = Join-Path $repoRoot ".build\vcpkg_installed\$scope\$triplet\share"
$notices = Join-Path $stage 'ThirdPartyNotices'
New-Item -ItemType Directory -Path $notices -Force | Out-Null
foreach ($port in 'yyjson', 'libzip', 'zlib', 'clipper2', 'wil') {
    $copyright = Join-Path $share "$port\copyright"
    if (Test-Path -LiteralPath $copyright) {
        Copy-Item -LiteralPath $copyright -Destination (Join-Path $notices "$port.txt")
    } else {
        Write-Warning "No vcpkg copyright file for $port under $share"
    }
}
Copy-Item -LiteralPath (Join-Path $repoRoot 'ThirdParty\Lucide\LICENSE') -Destination (Join-Path $notices 'lucide.txt')
Copy-Item -LiteralPath (Join-Path $repoRoot 'ThirdParty/RedMonitor/LICENSE.txt') -Destination (Join-Path $notices 'redmonitor.txt')

# DxUi.lib is linked into RedPrism.exe, so its own license ships too. restore-dxui.ps1 (run by build.ps1 above)
# leaves the pinned source under .build\dependencies\DxUi\source\<commit>, with its license at that root.
$dxUiLock = Get-Content -Raw -LiteralPath (Join-Path $repoRoot 'Dependencies\DxUi.lock.json') | ConvertFrom-Json
$dxUiSource = Join-Path $repoRoot ".build\dependencies\DxUi\source\$($dxUiLock.commit)"
$dxUiLicense = Get-ChildItem -LiteralPath $dxUiSource -Filter 'LICENSE*' -File -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $dxUiLicense) {
    throw "DxUi's license file was not found under $dxUiSource. Run restore-dxui.ps1 -Platform $Platform before packaging."
}
Copy-Item -LiteralPath $dxUiLicense.FullName -Destination (Join-Path $notices 'dxui.txt')

Copy-Item -LiteralPath (Join-Path $repoRoot 'docs\README.md') -Destination (Join-Path $stage 'README.md')
Copy-Item -LiteralPath (Join-Path $repoRoot 'docs\usage.md') -Destination (Join-Path $stage 'usage.md')
Copy-Item -LiteralPath (Join-Path $repoRoot 'LICENSE.txt') -Destination (Join-Path $stage 'LICENSE.txt')

# ── ZIP + checksum ───────────────────────────────────────────────────────────
$zipPath = Join-Path $OutputDirectory "$packageName.zip"
Remove-Item -LiteralPath $zipPath, "$zipPath.sha256" -ErrorAction SilentlyContinue
Compress-Archive -LiteralPath $stage -DestinationPath $zipPath -CompressionLevel Optimal
$hash = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText("$zipPath.sha256", "$hash *$packageName.zip`n")

# ── Clean-extraction smoke ───────────────────────────────────────────────────
$canRun = $Platform -eq 'x64' -or $env:PROCESSOR_ARCHITECTURE -eq 'ARM64'
if ($SkipSmoke) {
    Write-Host 'Smoke skipped (-SkipSmoke).' -ForegroundColor Yellow
} elseif (-not $canRun) {
    Write-Host "Smoke skipped: this host cannot run $Platform." -ForegroundColor Yellow
} else {
    Write-Host 'Clean-extraction smoke...' -ForegroundColor Cyan
    $smoke = Join-Path $OutputDirectory ("smoke-" + [guid]::NewGuid().ToString('N'))
    Expand-Archive -LiteralPath $zipPath -DestinationPath $smoke
    $extracted = Join-Path $smoke "$packageName\RedPrism.exe"
    if (-not (Test-Path -LiteralPath $extracted)) {
        throw "The ZIP did not extract to $packageName\RedPrism.exe."
    }
    # Only what Windows itself provides is on the path, so a DLL the package forgot cannot be found elsewhere.
    $systemPath = "$env:SystemRoot\System32;$env:SystemRoot;$env:SystemRoot\System32\Wbem"
    $helpLog = Join-Path $smoke 'help.log'
    $helpErr = Join-Path $smoke 'help.err.log'
    $help = Start-Process -FilePath $extracted -ArgumentList @('--help') -WindowStyle Hidden -Wait -PassThru `
        -RedirectStandardOutput $helpLog -RedirectStandardError $helpErr -Environment @{ Path = $systemPath }
    $helpText = Get-Content -LiteralPath $helpLog -Raw -ErrorAction SilentlyContinue
    if ($help.ExitCode -ne 0 -or $helpText -notmatch '--self-test') {
        throw "The extracted RedPrism.exe --help exited with $($help.ExitCode) or did not print the catalog."
    }
    $selfErr = Join-Path $smoke 'self-test.err.log'
    $self = Start-Process -FilePath $extracted -ArgumentList @('--self-test', '--warp') -WindowStyle Hidden -Wait -PassThru `
        -RedirectStandardError $selfErr -Environment @{ Path = $systemPath }
    if ($self.ExitCode -ne 2) {
        $selfText = Get-Content -LiteralPath $selfErr -Raw -ErrorAction SilentlyContinue
        throw "The extracted production binary did not refuse test mode (exit $($self.ExitCode)). $selfText"
    }
    $capture = Join-Path $smoke 'portable-startup.png'
    $captureErr = Join-Path $smoke 'portable-startup.err.log'
    $startup = Start-Process -FilePath $extracted -ArgumentList @('--screenshot', ('"'+$capture+'"'), '--warp') -WindowStyle Hidden -Wait -PassThru `
        -RedirectStandardError $captureErr -Environment @{ Path = $systemPath }
    if ($startup.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $capture -PathType Leaf) -or (Get-Item -LiteralPath $capture).Length -eq 0) {
        throw "The extracted production binary failed its hidden native startup/capture (exit $($startup.ExitCode))."
    }
    $extractedInfo = [Diagnostics.FileVersionInfo]::GetVersionInfo($extracted)
    if ($extractedInfo.FileVersion -ne $info.FileVersion) {
        throw 'The extracted executable does not carry the packaged version.'
    }
    Write-Host 'PASS clean-extraction smoke (help, test-mode refusal, hidden WARP capture, version identity)' -ForegroundColor Green
}

# ── Summary ──────────────────────────────────────────────────────────────────
$zipItem = Get-Item -LiteralPath $zipPath
Write-Host ''
Write-Host ("Ready: {0} ({1} KB)" -f $zipItem.FullName, [math]::Round($zipItem.Length / 1KB)) -ForegroundColor Green
Write-Host ("SHA-256: {0}" -f $hash) -ForegroundColor DarkGray
Write-Host 'Contents:' -ForegroundColor DarkGray
foreach ($entry in Get-ChildItem -LiteralPath $stage -Recurse -File) {
    Write-Host ("  {0}" -f $entry.FullName.Substring($stage.Length + 1)) -ForegroundColor DarkGray
}
