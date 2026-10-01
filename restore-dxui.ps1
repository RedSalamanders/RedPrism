<#
.SYNOPSIS
Restore the exact DxUi source pin and isolated build dependencies without modifying a sibling checkout.

.DESCRIPTION
The pin is cloned under .build/dependencies/DxUi/source/<commit> with Git long paths and as a sparse checkout that leaves
out Measurements/, docs/gallery and Specs/ (Build/DxUiRestore.psm1): a build reads none of them, they hold most of the files,
and pins before API revision 3 kept tracked paths of up to 164 characters there that a deep checkout root could not create. A sibling
DxUi checkout that already has the commit is only read, as the clone source; it is never checked out, reset or cleaned.
#>
[CmdletBinding()]
param(
    [ValidateSet('x64', 'ARM64')]
    [string] $Platform = 'x64',
    [string] $MSBuildPath = '',

    [switch] $CheckUpdates
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# The DxUi API revision this product's code is written against: the lock names it and the pinned capabilities.json carries it.
$requiredApiRevision = 3
$pinPath = Join-Path $PSScriptRoot 'Dependencies/DxUi.lock.json'
$pin = Get-Content -LiteralPath $pinPath -Raw | ConvertFrom-Json
if ($pin.repository -ne 'https://github.com/RedSalamanders/DxUi' -or $pin.commit -notmatch '^[0-9a-f]{40}$' -or
    $pin.apiRevision -ne $requiredApiRevision -or @($pin.targets).Count -ne 1 -or $pin.targets[0] -ne 'DxUi') {
    throw "DxUi.lock.json must identify the canonical repository, exact commit, API revision $requiredApiRevision and single DxUi target."
}

$dependencyRoot = Join-Path $PSScriptRoot '.build/dependencies/DxUi'
$source = Join-Path $dependencyRoot "source/$($pin.commit)"
if (-not (Test-Path -LiteralPath $source)) {
    $sibling = Join-Path (Split-Path -Parent $PSScriptRoot) 'DxUi'
    $cloneFrom = ''
    if (Test-Path -LiteralPath (Join-Path $sibling '.git')) {
        & git -C $sibling cat-file -e "$($pin.commit)^{commit}" 2>$null
        if ($LASTEXITCODE -eq 0) {
            $cloneFrom = $sibling
        }
    }
    Import-Module (Join-Path $PSScriptRoot 'Build/DxUiRestore.psm1') -Force
    [void](Restore-RedPrismDxUiSource -Repository $pin.repository -Commit $pin.commit -Destination $source -CloneFrom $cloneFrom)
}

try {
    & (Join-Path $source 'Tools/validate_consumer.ps1') -DxUiRoot $source -LockFile $pinPath
} catch {
    # A restore that stopped part way leaves its directory, and a later restore keeps an existing one: say what to do about it.
    throw ($_.Exception.Message + " If '$source' was left by a restore that did not finish, delete that directory and run again.")
}
if (-not $MSBuildPath) {
    Import-Module (Join-Path $PSScriptRoot 'Build/VisualStudioDiscovery.psm1') -Force
    # Restricted to Visual Studio 2026 (18.x): an unranged vswhere lookup could pick an older MSBuild without the
    # v145 toolset RedPrism requires (RF-707).
    $MSBuildPath = Find-RedPrismMSBuild
}

Import-Module (Join-Path $source 'Tools/ConsumerBuild.psm1') -Force
$buildIdentity = Get-DxUiConsumerBuildIdentity -DxUiRoot $source -MSBuildPath $MSBuildPath -Platform $Platform
$output = (Join-Path $dependencyRoot $buildIdentity.Fingerprint) + [IO.Path]::DirectorySeparatorChar
& (Join-Path $source 'vcpkg-install.ps1') -Platform $Platform -OutputRoot $output

$escape = { param([string] $value) [System.Security.SecurityElement]::Escape($value) }
$sourceXml = & $escape $source
$outputXml = & $escape $output
$pinXml = & $escape $pinPath
$identityProperties = '<DxUiConsumerToolset>' + (& $escape $buildIdentity.Identity.toolset) + '</DxUiConsumerToolset>' +
    '<DxUiConsumerVCToolsVersion>' + (& $escape $buildIdentity.Identity.vcToolsVersion) + '</DxUiConsumerVCToolsVersion>' +
    '<DxUiConsumerSdkVersion>' + (& $escape $buildIdentity.Identity.windowsSdkVersion) + '</DxUiConsumerSdkVersion>' +
    '<DxUiConsumerPreferredToolArchitecture>' + (& $escape $buildIdentity.Identity.preferredToolArchitecture) +
    '</DxUiConsumerPreferredToolArchitecture>'
$props = "<Project><PropertyGroup>$identityProperties<DxUiRoot>$sourceXml</DxUiRoot><DxUiConsumerOutputRoot>$outputXml</DxUiConsumerOutputRoot><DxUiConsumerLockFile>$pinXml</DxUiConsumerLockFile></PropertyGroup></Project>"
$propsPath = Join-Path $dependencyRoot "DxUi.resolved.$Platform.props"
if (-not (Test-Path -LiteralPath $propsPath) -or [IO.File]::ReadAllText($propsPath) -ne $props) {
    [IO.File]::WriteAllText($propsPath, $props, [Text.UTF8Encoding]::new($false))
}
$identityPath = Join-Path $dependencyRoot "DxUi.identity.$Platform.json"
$identityJson = $buildIdentity | ConvertTo-Json -Depth 5
if (-not (Test-Path -LiteralPath $identityPath) -or [IO.File]::ReadAllText($identityPath) -ne $identityJson) {
    [IO.File]::WriteAllText($identityPath, $identityJson, [Text.UTF8Encoding]::new($false))
}

if ($CheckUpdates) {
    Import-Module (Join-Path $source 'Tools/ConsumerUpdate.psm1') -Force
    Import-Module (Join-Path $PSScriptRoot 'Build/BuildPresentation.psm1') -Force
    # The pinned helper decides the notice. A lookup failure stays an advisory and does not fail restore.
    foreach ($record in @(Show-DxUiUpdateNotice -LockFile $pinPath 6>&1)) {
        $notice = if ($record -is [System.Management.Automation.InformationRecord]) {
            [string] $record.MessageData
        }
        else {
            [string] $record
        }
        $foregroundColor = Get-RedPrismDxUiUpdateNoticeForegroundColor -Notice $notice
        if ($foregroundColor) {
            Write-Host $notice -ForegroundColor $foregroundColor
        }
    }
}

Write-Host "DxUi restored at exact pin $($pin.commit); sibling checkout unchanged."
