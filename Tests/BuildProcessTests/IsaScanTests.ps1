[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $MSBuildPath,
    [ValidateSet('Debug', 'Release', 'ASan Debug')] [string] $Configuration = 'Debug'
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Import-Module (Join-Path $repo 'Build\IsaScan.psm1') -Force
& (Join-Path $PSScriptRoot 'IsaRsiDwordTests.ps1')
if ($LASTEXITCODE -ne 0) { throw 'Guarded RSI terminal DWORD parser controls failed.' }
& (Join-Path $PSScriptRoot 'IsaAlGuardTests.ps1')
& (Join-Path $PSScriptRoot 'IsaClGuardTests.ps1')
if ($LASTEXITCODE -ne 0) { throw 'Inline switch-table parser controls failed.' }
& (Join-Path $PSScriptRoot 'IsaRdxByteRemapTests.ps1')
if ($LASTEXITCODE -ne 0) { throw 'Guarded RDX byte-remap parser controls failed.' }
& (Join-Path $PSScriptRoot 'IsaCompactRcxByteRemapTests.ps1')
& (Join-Path $PSScriptRoot 'IsaCompactSignedRcxByteRemapTests.ps1')
if ($LASTEXITCODE -ne 0) { throw 'Compact signed RCX byte-remap parser controls failed.' }
& (Join-Path $PSScriptRoot 'IsaDisp8RcxByteRemapTests.ps1')
if ($LASTEXITCODE -ne 0) { throw 'Disp8 RCX byte-remap parser controls failed.' }
& (Join-Path $PSScriptRoot 'IsaDisp8RcxImm8ByteRemapTests.ps1')
if ($LASTEXITCODE -ne 0) { throw 'Disp8 RCX imm8 byte-remap parser controls failed.' }
& (Join-Path $PSScriptRoot 'IsaR10AlGuardTests.ps1')
if ($LASTEXITCODE -ne 0) { throw 'Compact RCX byte-remap parser controls failed.' }
& (Join-Path $PSScriptRoot 'IsaR10EcxGuardTests.ps1')
if ($LASTEXITCODE -ne 0) { throw 'R10 ECX terminal DWORD parser controls failed.' }
& (Join-Path $PSScriptRoot 'IsaWideStackGuardTests.ps1')
if ($LASTEXITCODE -ne 0) { throw 'Wide-stack terminal DWORD parser controls failed.' }
& (Join-Path $PSScriptRoot 'IsaQwordStackRaxTests.ps1')
& (Join-Path $PSScriptRoot 'IsaQwordPaddingTests.ps1')
if ($LASTEXITCODE -ne 0) { throw 'Exact QWORD terminal-padding parser controls failed.' }
if ($LASTEXITCODE -ne 0) { throw 'QWORD stack/RAX terminal DWORD parser controls failed.' }
& (Join-Path $PSScriptRoot 'IsaR9ByteSwitchTests.ps1')
if ($LASTEXITCODE -ne 0) { throw 'R9 byte-switch terminal DWORD parser controls failed.' }
& (Join-Path $PSScriptRoot 'IsaR9HoistedDirectTests.ps1')
if ($LASTEXITCODE -ne 0) { throw 'Hoisted R9 message-switch parser controls failed.' }
& (Join-Path $PSScriptRoot 'IsaRdiIndexTests.ps1')
if ($LASTEXITCODE -ne 0) { throw 'RDI-indexed DWORD parser controls failed.' }
& (Join-Path $PSScriptRoot 'IsaRdiBaseByteRemapTests.ps1')
if ($LASTEXITCODE -ne 0) { throw 'RDI-image-base byte-remap parser controls failed.' }
& (Join-Path $PSScriptRoot 'IsaR15SignedRemapTests.ps1')
if ($LASTEXITCODE -ne 0) { throw 'Hoisted R15 signed byte-remap parser controls failed.' }
& (Join-Path $PSScriptRoot 'IsaSignedRdxRemapTests.ps1')
if ($LASTEXITCODE -ne 0) { throw 'Signed RDX byte-remap parser controls failed.' }
$root = Join-Path $repo ".build\IsaScan\Controls\$($Configuration -replace '\s', '')"
$source = [Security.SecurityElement]::Escape((Join-Path $PSScriptRoot 'IsaProbe.cpp'))

function Invoke-Probe {
    param([string] $Name, [string] $ObjectName, [string] $Definition, [bool] $Negative)
    $directory = Join-Path $root $Name
    New-Item -ItemType Directory -Path $directory -Force | Out-Null
    $output = Join-Path $directory 'out'
    $intermediate = Join-Path $directory 'obj'
    $projectPath = Join-Path $directory 'IsaProbe.vcxproj'
    $project = @"
<?xml version="1.0" encoding="utf-8"?>
<Project DefaultTargets="Build" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <ItemGroup Label="ProjectConfigurations">
    <ProjectConfiguration Include="$Configuration|x64"><Configuration>$Configuration</Configuration><Platform>x64</Platform></ProjectConfiguration>
  </ItemGroup>
  <PropertyGroup Label="Globals"><ProjectGuid>{95A4B4C7-C915-4961-BAE6-3D269E07479B}</ProjectGuid><Keyword>Win32Proj</Keyword></PropertyGroup>
  <Import Project="`$(VCTargetsPath)\Microsoft.Cpp.Default.props" />
  <PropertyGroup Label="Configuration">
    <ConfigurationType>Application</ConfigurationType><PlatformToolset>v145</PlatformToolset>
    <WindowsTargetPlatformVersion>10.0.26100.0</WindowsTargetPlatformVersion>
    <UseDebugLibraries Condition="'`$(Configuration)'=='Release'">false</UseDebugLibraries>
    <UseDebugLibraries Condition="'`$(Configuration)'!='Release'">true</UseDebugLibraries>
  </PropertyGroup>
  <Import Project="`$(VCTargetsPath)\Microsoft.Cpp.props" />
  <PropertyGroup><LinkIncremental>false</LinkIncremental></PropertyGroup>
  <ItemDefinitionGroup>
    <ClCompile><PreprocessorDefinitions>$Definition;`%(PreprocessorDefinitions)</PreprocessorDefinitions><DebugInformationFormat>ProgramDatabase</DebugInformationFormat></ClCompile>
    <Link><GenerateDebugInformation>true</GenerateDebugInformation><SubSystem>Console</SubSystem></Link>
  </ItemDefinitionGroup>
  <ItemGroup><ClCompile Include="$source"><ObjectFileName>`$(IntDir)$ObjectName.obj</ObjectFileName></ClCompile></ItemGroup>
  <Import Project="`$(VCTargetsPath)\Microsoft.Cpp.targets" />
</Project>
"@
    Set-Content -LiteralPath $projectPath -Value $project -Encoding utf8
    $lines = @(& $MSBuildPath $projectPath /t:Rebuild "/p:Configuration=$Configuration" /p:Platform=x64 `
        "/p:OutDir=$output\" "/p:IntDir=$intermediate\" /nologo /verbosity:minimal 2>&1)
    $code = $LASTEXITCODE
    $lines | Set-Content -LiteralPath (Join-Path $directory 'build.log') -Encoding utf8
    if ($code -ne 0) { throw "ISA control $Name did not compile: $directory\build.log" }
    $scanDirectory = Join-Path $directory 'scan'
    $binary = Join-Path $output 'IsaProbe.exe'
    $caught = $false
    try {
        $scan = Invoke-RedPrismIsaScan -Binary $binary -OutputDirectory $scanDirectory
    } catch {
        if (-not $Negative -or $_.Exception.Message -notmatch 'ISA scan rejected') { throw }
        $caught = $true
    }
    if ($Negative) {
        if (-not $caught) { throw "The $Name control failed to detect deliberately misplaced vector instructions." }
        $report = Get-Content -LiteralPath (Join-Path $scanDirectory 'report.json') -Raw | ConvertFrom-Json
        if ($report.violations.Count -eq 0 -or ($report.violations -join "`n") -notmatch ([regex]::Escape($ObjectName + '.obj'))) {
            throw "The $Name control failed for a different object."
        }
        Write-Host "PASS isa-scan.control.$Name (expected rejection)"
    } elseif ($Name -ne 'Scalar' -and $scan.TierInstructions -eq 0) {
        throw "The $Name control emitted no tier instructions."
    }
    if (-not $Negative -and $Name -eq 'Avx512' -and $scan.TierEvexInstructions -eq 0) {
        throw 'The AVX-512 positive control did not prove an accepted EVEX instruction.'
    }
    $global:LASTEXITCODE = 0
    return $binary
}

$scalar = Invoke-Probe Scalar Kernels_Scalar_Probe REDPRISM_ISA_PROBE_SCALAR $false
$avx2 = Invoke-Probe Avx2 Kernels_Avx2_Probe REDPRISM_ISA_PROBE_AVX2 $false
$null = Invoke-Probe MisplacedAvx2 BaselineLeak REDPRISM_ISA_PROBE_AVX2 $true
$null = Invoke-Probe RuntimeObjectWrongLibrary vector_algorithms REDPRISM_ISA_PROBE_AVX2 $true
$null = Invoke-Probe Avx512 Kernels_Avx512_Probe REDPRISM_ISA_PROBE_EVEX $false
$null = Invoke-Probe EvexInAvx2 Kernels_Avx2_Probe REDPRISM_ISA_PROBE_EVEX $true

# Reuse the compiled negative fixture, changing only its parsed procedure name to a runtime name. This
# independently proves that the function exemption requires the observed owning object, not just its name.
$negativeRoot = Join-Path $root 'MisplacedAvx2'
$negativeScan = Join-Path $negativeRoot 'scan'
$symbolText = Get-Content -LiteralPath (Join-Path $negativeScan 'symbols.txt') -Raw
if (-not $symbolText.Contains('`Kernels_Avx2_Probe_Copy`')) { throw 'Missing compiled probe procedure record.' }
$alteredSymbols = Join-Path $negativeRoot 'runtime-name-symbols.txt'
$symbolText.Replace('`Kernels_Avx2_Probe_Copy`', '`wmemchr`') | Set-Content -LiteralPath $alteredSymbols -Encoding utf8
$allowlist = Import-PowerShellDataFile (Join-Path $repo 'Build\IsaRuntimeAllowlist.psd1')
$result = [RedPrismBuild.IsaScanner]::Scan((Join-Path $negativeRoot 'out\IsaProbe.exe'),
    (Join-Path $negativeScan 'contributions.txt'), $alteredSymbols, (Join-Path $negativeScan 'disassembly.txt'),
    [string[]] $allowlist.Objects, [string[]] $allowlist.Libraries, [string[]] $allowlist.Functions)
if ($result.Violations.Count -eq 0) { throw 'A same-named runtime function in an unapproved object bypassed the gate.' }
Write-Host 'PASS isa-scan.control.RuntimeFunctionWrongObject (expected rejection)'

foreach ($case in @(
    @{ Name = 'MissingPdb'; Pdb = (Join-Path $root 'absent.pdb'); Pattern = 'needs the linked image and its full PDB' },
    @{ Name = 'MismatchedPdb'; Pdb = [IO.Path]::ChangeExtension($avx2, '.pdb'); Pattern = 'PDB GUID/age does not match' }
)) {
    $caught = $false
    try {
        $null = Invoke-RedPrismIsaScan -Binary $scalar -Pdb $case.Pdb -OutputDirectory (Join-Path $root $case.Name)
    } catch {
        if ($_.Exception.Message -notmatch $case.Pattern) { throw }
        $caught = $true
    }
    if (-not $caught) { throw "ISA control $($case.Name) unexpectedly passed." }
    Write-Host "PASS isa-scan.control.$($case.Name) (expected rejection)"
}

# Identity must reject an unrelated PDB before its offsets are interpreted against the linked image.
# Give both mismatch controls a deliberately impossible contribution, independent of compiler section layout.
$scalarScan = Join-Path $root 'Scalar\scan'
$scalarMetadata = Get-Content -LiteralPath (Join-Path $scalarScan 'contributions.txt') -Raw
$scalarSymbols = Join-Path $scalarScan 'symbols.txt'
$scalarDisassembly = Join-Path $scalarScan 'disassembly.txt'
$guidPattern = '(?m)^\s*GUID: (\{.+\})\r?$'
$agePattern = '(?m)^\s*Age: (\d+)\r?$'
$guidRecord = [regex]::Match($scalarMetadata, $guidPattern)
$ageRecord = [regex]::Match($scalarMetadata, $agePattern)
if (-not $guidRecord.Success -or -not $ageRecord.Success) { throw 'Scalar PDB identity controls need a GUID and age.' }
$wrongGuid = [guid]::NewGuid()
if ($wrongGuid -eq [guid]::Parse($guidRecord.Groups[1].Value)) { throw 'Independent GUID unexpectedly matches the fixture.' }
$wrongAge = if ([uint32]::Parse($ageRecord.Groups[1].Value) -eq [uint32]::MaxValue) { 0 } else { [uint32]::Parse($ageRecord.Groups[1].Value) + 1 }
$impossibleContribution = "`nSection Contributions`n  SC[identity-first] | mod = 0, 65534:18446744073709551615, size = 1,`n"
foreach ($case in @(
    @{ Name = 'GuidBeforeMalformedContribution'; Metadata = [regex]::Replace($scalarMetadata, $guidPattern, ('  GUID: {' + $wrongGuid.ToString().ToUpperInvariant() + '}')) },
    @{ Name = 'AgeBeforeMalformedContribution'; Metadata = [regex]::Replace($scalarMetadata, $agePattern, ('  Age: ' + $wrongAge)) }
)) {
    $path = Join-Path $root ($case.Name + '.txt')
    ($case.Metadata + $impossibleContribution) | Set-Content -LiteralPath $path -Encoding utf8
    $caught = $false
    try {
        $null = [RedPrismBuild.IsaScanner]::Scan($scalar, $path, $scalarSymbols, $scalarDisassembly,
            [string[]] $allowlist.Objects, [string[]] $allowlist.Libraries, [string[]] $allowlist.Functions)
    } catch {
        if ($_.Exception.Message -notmatch 'PDB GUID/age does not match') { throw }
        $caught = $true
    }
    if (-not $caught) { throw "ISA control $($case.Name) interpreted a mismatched PDB." }
    Write-Host "PASS isa-scan.control.$($case.Name) (expected rejection)"
}
$global:LASTEXITCODE = 0
