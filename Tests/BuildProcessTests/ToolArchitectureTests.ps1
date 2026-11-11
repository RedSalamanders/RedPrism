[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$fixture = Join-Path $repo ('.build/ToolTests/ToolArchitecture-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path (Join-Path $fixture 'Build') -Force | Out-Null

# Execute the exact build entrypoint with dependency/compiler stand-ins. These controls never compile C++.
foreach ($relative in @('build.ps1', 'Build/BuildConsole.psm1', 'Build/BuildPresentation.psm1', 'Build/BuildPreflight.psm1', 'Build/VisualStudioDiscovery.psm1')) {
    $source = Join-Path $repo $relative
    $target = Join-Path $fixture $relative
    Copy-Item -LiteralPath $source -Destination $target
    if ((Get-FileHash -LiteralPath $source).Hash -cne (Get-FileHash -LiteralPath $target).Hash) { throw "Fixture differs: $relative" }
}
$utf8 = [Text.UTF8Encoding]::new($false)
function Write-Fixture([string] $Relative, [string] $Text) {
    [IO.File]::WriteAllText((Join-Path $fixture $Relative), $Text, $utf8)
}
Write-Fixture 'Boundary.ps1' @'
function Invoke-ToolBoundary([string] $Root, [string] $Name) {
    $state = Get-Content -LiteralPath (Join-Path $Root 'case.json') -Raw | ConvertFrom-Json
    [ordered]@{boundary=$Name;architecture=[Environment]::GetEnvironmentVariable('PreferredToolArchitecture', 'Process')} |
        ConvertTo-Json -Compress | Add-Content -LiteralPath (Join-Path $Root 'trace.jsonl') -Encoding utf8
    if ($state.failure -eq $Name) { throw "CONTROL_FAILURE_$Name" }
}
'@
Write-Fixture 'vcpkg-install.ps1' @'
param([string] $Platform, [switch] $Asan, [string] $MSBuildPath)
. (Join-Path $PSScriptRoot 'Boundary.ps1')
Invoke-ToolBoundary $PSScriptRoot 'dependencies'
'@
Write-Fixture 'restore-dxui.ps1' @'
param([string] $Platform, [string] $MSBuildPath, [switch] $CheckUpdates)
. (Join-Path $PSScriptRoot 'Boundary.ps1')
Invoke-ToolBoundary $PSScriptRoot 'restore'
'@
Write-Fixture 'MSBuild.ps1' @'
param([Parameter(ValueFromRemainingArguments)][string[]] $Arguments)
. (Join-Path $PSScriptRoot 'Boundary.ps1')
$preflight='/t:ValidateDxUiSdkProfile;CiValidateSdkConsumer' -cin $Arguments
Invoke-ToolBoundary $PSScriptRoot $(if($preflight){'sdk-profile'}else{'solution'})
if($preflight){$global:LASTEXITCODE=0;return}
$state = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'case.json') -Raw | ConvertFrom-Json
if ($state.failure -eq 'solution-exit') { $global:LASTEXITCODE = 19; return }
$logArgument = @($Arguments | Where-Object { $_ -match '^/flp:LogFile=' })[0]
if (-not $logArgument) { throw 'The real build entrypoint must capture a native diagnostic log.' }
$nativeLog = $logArgument.Substring('/flp:LogFile='.Length).Split(';')[0]
if ($state.failure -ne 'missing-log') {
    $text = switch ($state.failure) {
        'warning-log' { 'fixture.cpp(1): warning C4189: zero native exit is insufficient' }
        'error-log' { 'LINK : error LNK2001: zero native exit is insufficient' }
        default { 'Build succeeded.' }
    }
    [IO.File]::WriteAllText($nativeLog, $text)
}
$output = Join-Path $PSScriptRoot '.build/ARM64/Release'
New-Item -ItemType Directory -Path $output -Force | Out-Null
[IO.File]::WriteAllText((Join-Path $output 'RedPrism.exe'), 'not executable; build-boundary fixture only')
$global:LASTEXITCODE = 0
'@
Write-Fixture 'Build/DxUiProvenance.psm1' @'
. (Join-Path $PSScriptRoot '../Boundary.ps1')
function Write-RedPrismDxUiProvenance([string] $RepoRoot, [string] $Platform, [string] $Configuration, $SdkInput=$null) {
    Invoke-ToolBoundary $RepoRoot 'provenance'
    [IO.File]::WriteAllText((Join-Path $RepoRoot ".build/$Platform/$Configuration/DxUi.provenance.json"),'fixture provenance')
    return 'isolated fixture provenance'
}
Export-ModuleMember -Function Write-RedPrismDxUiProvenance
'@
Write-Fixture 'Build/ScopedTesting.psm1' @'
function Get-ScopedSourceIdentity([string]$Root,[switch]$CompiledOnly) { 'fixture-source' }
function Get-ScopedEnvironmentIdentity { 'fixture-environment' }
function Get-ScopedBuildAttestationKey([string]$Root,[string]$Platform,[string]$Configuration,[int]$BuildNumber) { 'fixture-attestation' }
function Write-ScopedReceipt([string]$Path,[string]$Key) { [IO.File]::WriteAllText($Path,$Key) }
Export-ModuleMember -Function Get-ScopedSourceIdentity,Get-ScopedEnvironmentIdentity,Get-ScopedBuildAttestationKey,Write-ScopedReceipt
'@

[void][IO.Directory]::CreateDirectory((Join-Path $fixture '.ci-engine/Tools/Modules'))
# This fixture already substitutes the compiler with an in-process PowerShell script.
# Preserve that boundary when the real entrypoint routes through the shared native bridge;
# actual child transport belongs to engine controls and actual native builds.
Write-Fixture '.ci-engine/Tools/Modules/Ci.Policy.psm1' @'
function Invoke-CiNativeCommand {
    param([string]$FilePath,[string]$WorkingDirectory,[string[]]$Arguments)
    $root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
    if($WorkingDirectory -cne $root -or $FilePath -cne (Join-Path $root 'MSBuild.ps1')){throw 'Unexpected actual native launch boundary.'}
    & $FilePath @Arguments
}
Export-ModuleMember -Function Invoke-CiNativeCommand
'@
Write-Fixture '.ci-engine/Tools/Modules/Ci.SdkConsumer.psm1' @'
. (Join-Path $PSScriptRoot '../../../Boundary.ps1')
function Open-CiApplicationSdkInput {
    param($ProjectRoot,$Descriptor,$Platform,$Configuration,$MSBuildPath)
    if($Descriptor -cne (Join-Path $ProjectRoot '.build/descriptor.json') -or $MSBuildPath -cne $env:MSBUILD_EXE_PATH -or $Configuration -cne 'Release' -or $Platform -cne 'ARM64'){throw 'Wrong actual SDK reader arguments.'}
    Invoke-ToolBoundary $ProjectRoot 'sdk-open'
    return @{Closed=$false;ProjectRoot=$ProjectRoot;SdkRoot=(Join-Path $ProjectRoot '.build/sdk');LeasePath=(Join-Path $ProjectRoot '.build/lease.json');Evidence=@{recipe='PinnedQualifiedReleaseSdkV2';inputKey=('a'*64)}}
}
function Close-CiPinnedSdkInput {
    param($Reader)
    $Reader.Closed=$true
    Invoke-ToolBoundary $Reader.ProjectRoot 'sdk-close'
}
Export-ModuleMember -Function Open-CiApplicationSdkInput,Close-CiPinnedSdkInput
'@

$previousArchitecture = [Environment]::GetEnvironmentVariable('PreferredToolArchitecture', 'Process')
$previousMSBuild = [Environment]::GetEnvironmentVariable('MSBUILD_EXE_PATH', 'Process')
$previousPath = [Environment]::GetEnvironmentVariable('PATH', 'Process')
function Set-TestEnvironment([string] $Name, [AllowNull()][AllowEmptyString()][object] $Value) {
    if ($null -eq $Value) { Remove-Item -LiteralPath ('Env:' + $Name) -ErrorAction SilentlyContinue }
    else { [Environment]::SetEnvironmentVariable($Name, [string]$Value, 'Process') }
}
try {
    $env:MSBUILD_EXE_PATH = Join-Path $fixture 'MSBuild.ps1'
    $defaultArchitecture = if ([Runtime.InteropServices.RuntimeInformation]::ProcessArchitecture -eq
        [Runtime.InteropServices.Architecture]::X64) { 'x64' } else { $null }
    $cases = @(
        @{name='unset';value=$null;failure='';clean=$false;boundaries=@('dependencies','restore','solution','provenance')},
        @{name='explicit-x86';value='x86';failure='';clean=$false;boundaries=@('dependencies','restore','solution','provenance')},
        @{name='explicit-x64';value='x64';failure='';clean=$false;boundaries=@('dependencies','restore','solution','provenance')},
        @{name='explicit-arm64';value='arm64';failure='';clean=$false;boundaries=@('dependencies','restore','solution','provenance')},
        @{name='empty-input';value='';failure='';clean=$false;boundaries=@('dependencies','restore','solution','provenance')},
        @{name='dependency-failure';value=$null;failure='dependencies';clean=$false;boundaries=@('dependencies')},
        @{name='restore-failure';value='arm64';failure='restore';clean=$false;boundaries=@('dependencies','restore')},
        @{name='solution-failure';value=$null;failure='solution-exit';clean=$false;boundaries=@('dependencies','restore','solution')},
        @{name='native-warning';value=$null;failure='warning-log';clean=$false;boundaries=@('dependencies','restore','solution')},
        @{name='native-error';value=$null;failure='error-log';clean=$false;boundaries=@('dependencies','restore','solution')},
        @{name='missing-native-log';value=$null;failure='missing-log';clean=$false;boundaries=@('dependencies','restore','solution')},
        @{name='provenance-failure';value=$null;failure='provenance';clean=$false;boundaries=@('dependencies','restore','solution','provenance')},
        @{name='clean';value=$null;failure='';clean=$true;boundaries=@('solution')},
        @{name='sdk-valid';value=$null;failure='';clean=$false;sdk=$true;boundaries=@('sdk-open','dependencies','sdk-profile','solution','provenance','sdk-close')},
        @{name='sdk-close-failure';value='x64';failure='sdk-close';clean=$false;sdk=$true;boundaries=@('sdk-open','dependencies','sdk-profile','solution','provenance','sdk-close')},
        @{name='sdk-solution-failure';value='arm64';failure='solution-exit';clean=$false;sdk=$true;boundaries=@('sdk-open','dependencies','sdk-profile','solution','sdk-close')},
        @{name='sdk-profile-failure';value='x64';failure='sdk-profile';clean=$false;sdk=$true;boundaries=@('sdk-open','dependencies','sdk-profile','sdk-close')}
    )
    foreach ($case in $cases) {
        Set-TestEnvironment 'PreferredToolArchitecture' $case.value
        $callerArchitecture = [Environment]::GetEnvironmentVariable('PreferredToolArchitecture', 'Process')
        $case | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $fixture 'case.json') -Encoding utf8
        [IO.File]::WriteAllText((Join-Path $fixture 'trace.jsonl'), '', $utf8)
        $failure = $null
        $arguments = @{Platform='ARM64';Configuration='Release';BuildNumber=1;MaxCpuCount=1;Clean=$case.clean}
        if($case.ContainsKey('sdk')){$arguments.DxUiSdkInputDescriptor=Join-Path $fixture '.build/descriptor.json'}
        try {
            & (Join-Path $fixture 'build.ps1') @arguments *> (Join-Path $fixture ($case.name + '.log'))
        } catch { $failure = $_.Exception.Message }
        if ($case.failure -eq '') {
            if ($failure) { throw "$($case.name): $failure" }
        } else {
            $expected = switch ($case.failure) {
                'solution-exit' { 'MSBuild failed with exit code 19' }
                'warning-log' { 'Zero-warning build validation failed: 1 warning(s), 0 error(s)' }
                'error-log' { 'Zero-warning build validation failed: 0 warning(s), 1 error(s)' }
                'missing-log' { 'Build diagnostics log is missing' }
                default { 'CONTROL_FAILURE_' + $case.failure }
            }
            if (-not $failure -or -not $failure.Contains($expected)) { throw "$($case.name) lost the original failure: $failure" }
        }
        $trace = @(Get-Content -LiteralPath (Join-Path $fixture 'trace.jsonl') | ForEach-Object { $_ | ConvertFrom-Json })
        if (($trace.boundary -join '|') -cne ($case.boundaries -join '|')) { throw "$($case.name) reached an unexpected boundary." }
        $expectedArchitecture = if ($null -eq $callerArchitecture) { $defaultArchitecture } else { $callerArchitecture }
        foreach ($row in $trace) {
            if ($row.architecture -cne $expectedArchitecture) { throw "$($case.name) selected '$($row.architecture)' at $($row.boundary)." }
        }
        if ([Environment]::GetEnvironmentVariable('PreferredToolArchitecture', 'Process') -cne $callerArchitecture -or
            [Environment]::GetEnvironmentVariable('MSBUILD_EXE_PATH', 'Process') -cne (Join-Path $fixture 'MSBuild.ps1') -or
            [Environment]::GetEnvironmentVariable('PATH', 'Process') -cne $previousPath) { throw "$($case.name) leaked a caller environment change." }
        if($case.failure -cin @('sdk-close','sdk-profile')){
            foreach($name in @('DxUi.provenance.json','build-attestation.json')){
                if($name -ceq 'DxUi.provenance.json' -and $case.failure -ceq 'sdk-profile'){continue}
                if(Test-Path -LiteralPath (Join-Path $fixture ('.build/ARM64/Release/'+$name))){throw "Failed SDK attempt retained accepted $name."}
            }
        }
        Copy-Item -LiteralPath (Join-Path $fixture 'trace.jsonl') -Destination (Join-Path $fixture ($case.name + '.jsonl'))
        Write-Host "PASS toolarchitecture.$($case.name).boundaries-failure-and-environment"
    }

    # Exercise discovery precedence with inert files; neither stand-in is launched here.
    Import-Module (Join-Path $repo 'Build/VisualStudioDiscovery.psm1') -Force
    $pathRoot = Join-Path $fixture 'PathChoice'
    New-Item -ItemType Directory -Path $pathRoot | Out-Null
    $pathMSBuild = Join-Path $pathRoot 'msbuild.exe'
    [IO.File]::WriteAllText($pathMSBuild, 'discovery only')
    $env:PATH = $pathRoot
    if ((Find-RedPrismMSBuild) -cne (Join-Path $fixture 'MSBuild.ps1')) { throw 'Explicit MSBuild override lost precedence.' }
    Set-TestEnvironment 'MSBUILD_EXE_PATH' $null
    if ((Find-RedPrismMSBuild) -cne $pathMSBuild) { throw 'PATH-selected MSBuild lost precedence.' }
    Write-Host 'PASS toolarchitecture.discovery-preserves-explicit-and-path-selection'
} finally {
    Set-TestEnvironment 'PreferredToolArchitecture' $previousArchitecture
    Set-TestEnvironment 'MSBUILD_EXE_PATH' $previousMSBuild
    Set-TestEnvironment 'PATH' $previousPath
}
# Expected compiler refusals above must not escape as a failure after every control passed.
$global:LASTEXITCODE=0
