[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$modulePath = Join-Path $repo 'Build/DxUiOutputPath.psm1'
if (-not (Test-Path -LiteralPath $modulePath -PathType Leaf)) {
    throw "Required output-path module is missing: $modulePath"
}
Import-Module $modulePath -Force

$fixtureRoot = Join-Path $repo ('.build/BuildProcessTests/DxUiOutputPath-' + [guid]::NewGuid().ToString('N'))
$dependencyRoot = Join-Path $fixtureRoot 'dependencies/DxUi'
[void](New-Item -ItemType Directory -Path $dependencyRoot -Force)

$identityA = [pscustomobject]@{
    Fingerprint = ('a' * 64)
    Identity = [pscustomobject]@{ commit = ('1' * 40); platform = 'x64'; apiRevision = 3 }
}
$identityCollision = [pscustomobject]@{
    Fingerprint = (('a' * 16) + ('b' * 48))
    Identity = [pscustomobject]@{ commit = ('2' * 40); platform = 'x64'; apiRevision = 3 }
}

function Assert-True([bool] $Condition, [string] $Name) {
    if (-not $Condition) { throw "FAIL $Name" }
    Write-Host "PASS $Name"
}

function Assert-Rejected([scriptblock] $Action, [string] $Name) {
    $rejected = $false
    try {
        & $Action
    } catch {
        $rejected = $true
    }
    if (-not $rejected) { throw "FAIL accepted invalid output-path case: $Name" }
    Write-Host "PASS rejects $Name"
}

function Write-ResolvedProps([string] $Path, [string] $Root) {
    $escapedRoot = [Security.SecurityElement]::Escape($Root)
    $xml = "<Project><PropertyGroup><DxUiConsumerOutputRoot>$escapedRoot</DxUiConsumerOutputRoot></PropertyGroup></Project>"
    [IO.File]::WriteAllText($Path, $xml, [Text.UTF8Encoding]::new($false))
}

try {
    $expectedFolder = Join-Path $dependencyRoot ($identityA.Fingerprint.Substring(0, 16))
    $outputA = Initialize-RedPrismDxUiOutputRoot -DependencyRoot $dependencyRoot -BuildIdentity $identityA
    Assert-True ($outputA -eq ([IO.Path]::GetFullPath($expectedFolder).TrimEnd('\') + '\')) 'uses exact first 16 fingerprint hex characters and returns separator-terminated absolute path'

    $markerPath = Join-Path $expectedFolder 'DxUi.build-identity.json'
    $markerBytesBefore = [IO.File]::ReadAllBytes($markerPath)
    $markerTime = [DateTime]::new(2020, 1, 2, 3, 4, 5, [DateTimeKind]::Utc)
    [IO.File]::SetLastWriteTimeUtc($markerPath, $markerTime)
    $outputAgain = Initialize-RedPrismDxUiOutputRoot -DependencyRoot $dependencyRoot -BuildIdentity $identityA
    $markerBytesAfter = [IO.File]::ReadAllBytes($markerPath)
    Assert-True ($outputAgain -eq $outputA) 'same identity initialization is idempotent'
    Assert-True ([Convert]::ToBase64String($markerBytesBefore) -ceq [Convert]::ToBase64String($markerBytesAfter)) 'idempotent initialization does not rewrite marker'
    Assert-True ([IO.File]::GetLastWriteTimeUtc($markerPath) -eq $markerTime) 'idempotent initialization preserves marker timestamp'
    $marker = Get-Content -Raw -LiteralPath $markerPath | ConvertFrom-Json
    Assert-True ($marker.Fingerprint -ceq $identityA.Fingerprint) 'marker retains full 64-character fingerprint'
    Assert-True ($marker.Identity.commit -ceq $identityA.Identity.commit -and $marker.Identity.platform -ceq $identityA.Identity.platform) 'marker retains full build identity'

    Assert-Rejected { Initialize-RedPrismDxUiOutputRoot -DependencyRoot $dependencyRoot -BuildIdentity $identityCollision } 'same 16-character prefix with different full fingerprint'
    $markerAfterCollision = Get-Content -Raw -LiteralPath $markerPath | ConvertFrom-Json
    Assert-True ($markerAfterCollision.Fingerprint -ceq $identityA.Fingerprint) 'collision rejection preserves original marker'
    $changedIdentity = [pscustomobject]@{ Fingerprint = $identityA.Fingerprint; Identity = $identityCollision.Identity }
    Assert-Rejected { Initialize-RedPrismDxUiOutputRoot -DependencyRoot $dependencyRoot -BuildIdentity $changedIdentity } 'same fingerprint with changed full identity'
    Assert-True ([Convert]::ToBase64String([IO.File]::ReadAllBytes($markerPath)) -ceq [Convert]::ToBase64String($markerBytesBefore)) 'identity rejection does not rewrite marker'

    $emptyIdentity = [pscustomobject]@{ Fingerprint = ('7' * 64); Identity = $identityA.Identity }
    $emptyFolder = Join-Path $dependencyRoot ('7' * 16)
    [void](New-Item -ItemType Directory -Path $emptyFolder)
    $emptyOutput = Initialize-RedPrismDxUiOutputRoot -DependencyRoot $dependencyRoot -BuildIdentity $emptyIdentity
    Assert-True ($emptyOutput -eq ([IO.Path]::GetFullPath($emptyFolder) + '\')) 'empty existing folder receives its full identity'

    $missingMarkerFolder = Join-Path $dependencyRoot ('c' * 16)
    [void](New-Item -ItemType Directory -Path $missingMarkerFolder -Force)
    Set-Content -LiteralPath (Join-Path $missingMarkerFolder 'cached.bin') -Value 'preexisting data'
    $identityC = [pscustomobject]@{ Fingerprint = (('c' * 16) + ('d' * 48)); Identity = $identityA.Identity }
    Assert-Rejected { Initialize-RedPrismDxUiOutputRoot -DependencyRoot $dependencyRoot -BuildIdentity $identityC } 'nonempty preexisting folder without marker'

    $wrongMarkerFolder = Join-Path $dependencyRoot ('e' * 16)
    [void](New-Item -ItemType Directory -Path $wrongMarkerFolder -Force)
    $wrongMarker = [ordered]@{ Fingerprint = ('f' * 64); Identity = $identityA.Identity }
    $wrongMarker | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $wrongMarkerFolder 'DxUi.build-identity.json')
    $identityE = [pscustomobject]@{ Fingerprint = (('e' * 16) + ('1' * 48)); Identity = $identityA.Identity }
    Assert-Rejected { Initialize-RedPrismDxUiOutputRoot -DependencyRoot $dependencyRoot -BuildIdentity $identityE } 'existing marker with wrong full identity'

    $resolvedProps = Join-Path $dependencyRoot 'DxUi.resolved.x64.props'
    Write-ResolvedProps $resolvedProps $outputA
    $resolved = Read-RedPrismDxUiResolvedOutputRoot -DependencyRoot $dependencyRoot -Platform x64 -BuildIdentity $identityA
    Assert-True ($resolved -eq $outputA) 'short output root resolves through props and marker'
    Write-ResolvedProps $resolvedProps $missingMarkerFolder
    Assert-Rejected { Read-RedPrismDxUiResolvedOutputRoot -DependencyRoot $dependencyRoot -Platform x64 -BuildIdentity $identityA } 'resolved output root missing its identity marker'
    # Existing full-length output roots remain valid when props name them and their marker proves identity.
    $legacyFullRoot = Join-Path $dependencyRoot $identityA.Fingerprint
    [void](New-Item -ItemType Directory -Path $legacyFullRoot -Force)
    $legacyMarker = $identityA
    $legacyMarker | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $legacyFullRoot 'DxUi.build-identity.json')
    Write-ResolvedProps $resolvedProps $legacyFullRoot
    $resolved = Read-RedPrismDxUiResolvedOutputRoot -DependencyRoot $dependencyRoot -Platform x64 -BuildIdentity $identityA
    Assert-True ($resolved -eq ([IO.Path]::GetFullPath($legacyFullRoot).TrimEnd('\') + '\')) 'uses resolved props as authority and accepts marked full-fingerprint path'

    $outsideRoot = Join-Path $fixtureRoot 'outside'
    [void](New-Item -ItemType Directory -Path $outsideRoot -Force)
    $badProps = @(
        @{ name = 'malformed XML'; content = '<Project><PropertyGroup>' },
        @{ name = 'missing output-root element'; content = '<Project><PropertyGroup><Other>value</Other></PropertyGroup></Project>' },
        @{ name = 'duplicate output-root elements'; content = "<Project><PropertyGroup><DxUiConsumerOutputRoot>$([Security.SecurityElement]::Escape($legacyFullRoot))</DxUiConsumerOutputRoot><DxUiConsumerOutputRoot>$([Security.SecurityElement]::Escape($legacyFullRoot))</DxUiConsumerOutputRoot></PropertyGroup></Project>" },
        @{ name = 'blank output-root'; content = '<Project><PropertyGroup><DxUiConsumerOutputRoot> </DxUiConsumerOutputRoot></PropertyGroup></Project>' },
        @{ name = 'relative output-root'; content = '<Project><PropertyGroup><DxUiConsumerOutputRoot>relative/cache</DxUiConsumerOutputRoot></PropertyGroup></Project>' },
        @{ name = 'drive-relative output-root'; content = '<Project><PropertyGroup><DxUiConsumerOutputRoot>C:cache</DxUiConsumerOutputRoot></PropertyGroup></Project>' },
        @{ name = 'current-drive rooted output-root'; content = '<Project><PropertyGroup><DxUiConsumerOutputRoot>\cache</DxUiConsumerOutputRoot></PropertyGroup></Project>' },
        @{ name = 'dependency root itself'; content = "<Project><PropertyGroup><DxUiConsumerOutputRoot>$([Security.SecurityElement]::Escape($dependencyRoot))</DxUiConsumerOutputRoot></PropertyGroup></Project>" },
        @{ name = 'output-root outside dependency root'; content = "<Project><PropertyGroup><DxUiConsumerOutputRoot>$([Security.SecurityElement]::Escape($outsideRoot))</DxUiConsumerOutputRoot></PropertyGroup></Project>" },
        @{ name = 'output-root escaping dependency root'; content = '<Project><PropertyGroup><DxUiConsumerOutputRoot>..\outside</DxUiConsumerOutputRoot></PropertyGroup></Project>' },
        @{ name = 'absolute output-root escaping dependency root'; content = "<Project><PropertyGroup><DxUiConsumerOutputRoot>$([Security.SecurityElement]::Escape((Join-Path $dependencyRoot '../outside')))</DxUiConsumerOutputRoot></PropertyGroup></Project>" },
        @{ name = 'unresolved MSBuild variable'; content = '<Project><PropertyGroup><DxUiConsumerOutputRoot>$(DxUiRoot)\cache</DxUiConsumerOutputRoot></PropertyGroup></Project>' }
    )
    foreach ($case in $badProps) {
        [IO.File]::WriteAllText($resolvedProps, $case.content, [Text.UTF8Encoding]::new($false))
        Assert-Rejected { Read-RedPrismDxUiResolvedOutputRoot -DependencyRoot $dependencyRoot -Platform x64 -BuildIdentity $identityA } $case.name
    }

    $wrongResolvedRoot = Join-Path $dependencyRoot ('9' * 16)
    [void](New-Item -ItemType Directory -Path $wrongResolvedRoot -Force)
    $wrongResolvedMarker = [ordered]@{ Fingerprint = ('8' * 64); Identity = $identityA.Identity }
    $wrongResolvedMarker | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $wrongResolvedRoot 'DxUi.build-identity.json')
    Write-ResolvedProps $resolvedProps $wrongResolvedRoot
    Assert-Rejected { Read-RedPrismDxUiResolvedOutputRoot -DependencyRoot $dependencyRoot -Platform x64 -BuildIdentity $identityA } 'resolved marker with wrong fingerprint/collision identity'

    $malformedBuildIdentity = [pscustomobject]@{ Fingerprint = 'not-64-hex'; Identity = $identityA.Identity }
    Assert-Rejected { Initialize-RedPrismDxUiOutputRoot -DependencyRoot $dependencyRoot -BuildIdentity $malformedBuildIdentity } 'malformed build fingerprint'
} finally {
    $cleanupPath = [IO.Path]::GetFullPath($fixtureRoot).TrimEnd('\')
    $cleanupParent = [IO.Path]::GetFullPath((Join-Path $repo '.build/BuildProcessTests')).TrimEnd('\') + '\'
    if (-not $cleanupPath.StartsWith($cleanupParent, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Unsafe fixture cleanup path; refusing to remove anything.'
    }
    Remove-Item -LiteralPath $cleanupPath -Recurse -Force
}
