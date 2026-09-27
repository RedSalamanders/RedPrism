[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$profilePath = Join-Path $repo 'Build\RedPrism.wprp'
$wprCommand = Get-Command 'wpr.exe' -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $wprCommand) {
    throw 'Windows Performance Recorder (wpr.exe) is unavailable. Install the Windows Performance Toolkit on this Windows 11 host to validate RedPrism.wprp.'
}

function Invoke-RedPrismWprReadOnly {
    param([Parameter(Mandatory)][string[]]$Arguments)

    $output = @(& $wprCommand.Source @Arguments 2>&1 | ForEach-Object { $_.ToString() })
    $exitCode = $LASTEXITCODE
    if ($exitCode -ne 0) {
        $text = $output -join [Environment]::NewLine
        throw "Read-only WPR validation failed (exit $exitCode): wpr $($Arguments -join ' ')`n$text"
    }
    return ($output -join [Environment]::NewLine)
}

if (-not (Test-Path -LiteralPath $profilePath -PathType Leaf)) {
    throw "WPR profile was not found: $profilePath"
}

try {
    [xml]$profile = Get-Content -LiteralPath $profilePath -Raw
} catch {
    throw "RedPrism.wprp is not well-formed XML: $($_.Exception.Message)"
}

$profileNode = @($profile.WindowsPerformanceRecorder.Profiles.Profile | Where-Object { $_.Id -eq 'RedPrism.Verbose.File' })
if ($profileNode.Count -ne 1 -or $profileNode[0].Name -ne 'RedPrism' -or
    $profileNode[0].DetailLevel -ne 'Verbose' -or $profileNode[0].LoggingMode -ne 'File') {
    throw 'The XML must define exactly one RedPrism.Verbose.File profile with Name=RedPrism, Verbose detail, and File logging.'
}

$pipelineGuid = [guid]'7B5D9C3E-42F1-4B76-9A2D-61E8C4F037A5'
$collectors = @($profile.WindowsPerformanceRecorder.Profiles.SystemCollector) +
    @($profile.WindowsPerformanceRecorder.Profiles.EventCollector)
$bufferBytes = 0L
foreach ($collector in $collectors) {
    $bufferBytes += [long]$collector.BufferSize.Value * [long]$collector.Buffers.Value * 1024
}
if ($bufferBytes -ne 24MB) {
    throw "The profile's collector buffers must total 24 MiB; actual bytes: $bufferBytes."
}
$pipelineProvider = @($profile.WindowsPerformanceRecorder.Profiles.EventProvider | Where-Object {
    try { ([guid]$_.Name) -eq $pipelineGuid } catch { $false }
})
if ($pipelineProvider.Count -ne 1) {
    throw "The TraceLogging EventProvider Name must be GUID $pipelineGuid."
}
$pipelineKeywords = 0L
foreach ($keyword in $pipelineProvider[0].Keywords.Keyword) {
    $hex = ([string]$keyword.Value) -replace '^0x', ''
    $pipelineKeywords = $pipelineKeywords -bor [convert]::ToInt64($hex, 16)
}
if (($pipelineKeywords -band 0x7) -ne 0x7) {
    throw 'The TraceLogging provider must enable RedPrism keywords 0x1 (UiFree), 0x2 (Memory) and 0x4 (UI timing).'
}

$systemProvider = @($profile.WindowsPerformanceRecorder.Profiles.SystemProvider | Where-Object { $_.Id -eq 'RedPrismSystemProvider' })
if ($systemProvider.Count -ne 1) {
    throw 'The RedPrism system provider is missing or duplicated.'
}
$systemKeywords = @($systemProvider[0].Keywords.Keyword | ForEach-Object Value)
foreach ($requiredKeyword in 'ProcessThread', 'Loader', 'SampledProfile', 'CSwitch', 'DiskIO', 'FileIO', 'FileIOInit', 'Filename', 'AllFaults', 'HardFaults') {
    if ($requiredKeyword -notin $systemKeywords) {
        throw "The RedPrism system provider is missing required keyword '$requiredKeyword'."
    }
}
$stacks = @($systemProvider[0].Stacks.Stack | ForEach-Object Value)
foreach ($requiredStack in 'SampledProfile', 'CSwitch', 'ReadyThread', 'PagefaultHard') {
    if ($requiredStack -notin $stacks) {
        throw "The RedPrism system provider is missing required stack '$requiredStack'."
    }
}

$referencedProviderIds = @($profileNode[0].Collectors.EventCollectorId.EventProviders.EventProviderId | ForEach-Object Value)
if ($pipelineProvider[0].Id -notin $referencedProviderIds -or $pipelineProvider[0].Level -ne '5') {
    throw 'RedPrism must reference its TraceLogging provider at Verbose level to capture UI timing.'
}
$eventProviders = @($profile.WindowsPerformanceRecorder.Profiles.EventProvider)
foreach ($gpuGuid in @(
        [guid]'802EC45A-1E99-4B83-9920-87C98277BA9D', # Microsoft-Windows-DxgKrnl
        [guid]'CA11C036-0102-4A2D-A6AD-F03CFED5D3C9', # Microsoft-Windows-DXGI
        [guid]'DB6F6DDB-AC77-4E88-8253-819DF9BBF140', # Microsoft-Windows-Direct3D11
        [guid]'8C416C79-D49B-4F01-A467-E56D3AA8234C'  # Microsoft-Windows-Win32k
    )) {
    $provider = @($eventProviders | Where-Object {
        try { ([guid]$_.Name) -eq $gpuGuid } catch { $false }
    })
    if ($provider.Count -ne 1 -or $provider[0].Id -notin $referencedProviderIds -or $provider[0].Stack -ne 'true') {
        throw "GPU provider $gpuGuid must be defined once, referenced by RedPrism, and enable event stacks."
    }
}

# These are read-only WPR metadata queries; this test never starts, stops, or cancels a trace.
$listedProfiles = Invoke-RedPrismWprReadOnly -Arguments @('-profiles', $profilePath)
if ($listedProfiles -notmatch '(?im)^\s*RedPrism\s') {
    throw "wpr -profiles did not list the RedPrism profile.`n$listedProfiles"
}
Write-Host 'PASS wpr-profile.listed'

$profileDetails = Invoke-RedPrismWprReadOnly -Arguments @('-profiledetails', "$profilePath`!RedPrism", '-filemode')
if ($profileDetails -notmatch '(?im)^\s*Profile\s*:\s*RedPrism\.Verbose\.File\s*$') {
    throw "wpr -profiledetails did not resolve RedPrism.Verbose.File.`n$profileDetails"
}
foreach ($requiredProvider in $pipelineGuid.ToString(), 'Microsoft-Windows-DxgKrnl', 'Microsoft-Windows-DXGI',
        'Microsoft-Windows-Direct3D11', 'Microsoft-Windows-Win32k') {
    if ($profileDetails -notmatch [regex]::Escape($requiredProvider)) {
        throw "wpr -profiledetails omitted provider '$requiredProvider'."
    }
}
Write-Host 'PASS wpr-profile.details'
Write-Host 'PASS wpr-profile.xml-providers-keywords-stacks'
