<#
.SYNOPSIS
Proves native concurrency SAL analysis accepts a guarded access and rejects an unguarded access.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [ValidateNotNullOrEmpty()]
    [string] $MSBuildPath,

    [ValidateSet('x64', 'ARM64')]
    [string] $Platform = 'x64',

    [ValidateSet('Debug', 'Release', 'ASan Debug')]
    [string] $Configuration = 'Debug'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$sourcePath = Join-Path $PSScriptRoot 'Tooling.Tests.ConcurrencyAnalysisProbe.cpp'
$analysisRoot = Join-Path $repo ".build\AnalysisProbe\$Platform\$($Configuration -replace '\s', '')"
$previousExtensions = [Environment]::GetEnvironmentVariable('Esp.Extensions', 'Process')
$extensions = @($previousExtensions -split ';' | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
if ($extensions -notcontains 'ConcurrencyCheck.dll') {
    $extensions += 'ConcurrencyCheck.dll'
}

function Invoke-AnalysisProbe {
    param(
        [Parameter(Mandatory)] [string] $Name,
        [Parameter(Mandatory)] [bool] $Negative
    )

    $caseRoot = Join-Path $analysisRoot "$Name-ConcurrencyAnalysisProbe"
    $outputRoot = Join-Path $caseRoot 'out'
    $intermediateRoot = Join-Path $caseRoot 'obj'
    New-Item -ItemType Directory -Path $outputRoot, $intermediateRoot -Force | Out-Null

    $projectPath = Join-Path $caseRoot 'ConcurrencyAnalysisProbe.vcxproj'
    $escapedSourcePath = [System.Security.SecurityElement]::Escape($sourcePath)
    $negativeDefinition = if ($Negative) { 'REDPRISM_NEGATIVE_ANALYSIS_PROBE;' } else { '' }
    $project = @"
<?xml version="1.0" encoding="utf-8"?>
<Project DefaultTargets="Build" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <ItemGroup Label="ProjectConfigurations">
    <ProjectConfiguration Include="$Configuration|$Platform">
      <Configuration>$Configuration</Configuration>
      <Platform>$Platform</Platform>
    </ProjectConfiguration>
  </ItemGroup>
  <PropertyGroup Label="Globals">
    <ProjectGuid>{4B1E6B62-91B1-4E84-AE74-AF27577354E5}</ProjectGuid>
    <Keyword>Win32Proj</Keyword>
    <RootNamespace>ConcurrencyAnalysisProbe</RootNamespace>
  </PropertyGroup>
  <Import Project="`$(VCTargetsPath)\Microsoft.Cpp.Default.props" />
  <PropertyGroup Condition="'`$(Configuration)|`$(Platform)'=='$Configuration|$Platform'" Label="Configuration">
    <ConfigurationType>StaticLibrary</ConfigurationType>
    <UseDebugLibraries Condition="'`$(Configuration)'=='Release'">false</UseDebugLibraries>
    <UseDebugLibraries Condition="'`$(Configuration)'!='Release'">true</UseDebugLibraries>
    <PlatformToolset>v145</PlatformToolset>
    <WindowsTargetPlatformVersion>10.0.26100.0</WindowsTargetPlatformVersion>
  </PropertyGroup>
  <Import Project="`$(VCTargetsPath)\Microsoft.Cpp.props" />
  <ItemDefinitionGroup>
    <ClCompile>
      <PreprocessorDefinitions>$negativeDefinition`%(PreprocessorDefinitions)</PreprocessorDefinitions>
    </ClCompile>
  </ItemDefinitionGroup>
  <ItemGroup>
    <ClCompile Include="$escapedSourcePath" />
  </ItemGroup>
  <Import Project="`$(VCTargetsPath)\Microsoft.Cpp.targets" />
</Project>
"@
    # The projects live under .build so MSBuild imports the repository's Directory.Build.props/targets.
    Set-Content -LiteralPath $projectPath -Value $project -Encoding UTF8

    $logPath = Join-Path $analysisRoot "$Name-$Platform-$($Configuration -replace '\s', '')-Analyze.log"
    $arguments = @(
        $projectPath,
        '/t:Rebuild',
        "/p:Configuration=$Configuration",
        "/p:Platform=$Platform",
        '/p:RedPrismAnalyze=true',
        '/p:RedPrismAnalysisProbe=true',
        "/p:OutDir=$outputRoot\",
        "/p:IntDir=$intermediateRoot\",
        '/nologo',
        '/verbosity:minimal',
        "/flp:LogFile=$logPath;Verbosity=minimal"
    )

    $lines = @(& $MSBuildPath @arguments 2>&1)
    $exitCode = $LASTEXITCODE
    $lines | Set-Content -LiteralPath $logPath -Encoding UTF8
    $logText = Get-Content -LiteralPath $logPath -Raw

    if ($Negative) {
        if ($exitCode -eq 0) {
            throw "Negative concurrency SAL probe unexpectedly compiled. Log: $logPath"
        }
        if ($logText -notmatch "(?m)^.*Tooling\.Tests\.ConcurrencyAnalysisProbe\.cpp\(\d+(?:,\d+)?\): warning C261\d{2}:.*function 'ConcurrencyAnalysisProbe::Unsafe'.*$") {
            throw "Negative concurrency SAL probe failed without a C261xx diagnostic at the intended Unsafe access. Log: $logPath"
        }
        Write-Host "PASS negative concurrency SAL control (expected C261xx; log: $logPath)"
        $global:LASTEXITCODE = 0
        return
    }

    if ($exitCode -ne 0) {
        throw "Positive concurrency SAL probe failed with exit code $exitCode. Log: $logPath"
    }
    Write-Host "PASS positive concurrency SAL control (log: $logPath)"
}

try {
    [Environment]::SetEnvironmentVariable('Esp.Extensions', ($extensions -join ';'), 'Process')
    Invoke-AnalysisProbe -Name 'Positive' -Negative $false
    Invoke-AnalysisProbe -Name 'Negative' -Negative $true
    $global:LASTEXITCODE = 0
    Write-Host 'PASS code-analysis.concurrency-sal-controls'
    return
} finally {
    [Environment]::SetEnvironmentVariable('Esp.Extensions', $previousExtensions, 'Process')
}
