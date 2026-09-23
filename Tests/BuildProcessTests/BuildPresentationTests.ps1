[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Import-Module (Join-Path $repo 'Build\BuildPresentation.psm1') -Force

$model = Get-RedPrismBuildBannerModel
if ($model.SceneRows.Count -lt 2 -or ($model.SceneRows.Count % 2) -ne 0) {
    throw 'The RedPrism banner scene lost its even half-block geometry.'
}
$sceneWidth = $model.SceneRows[0].Length
foreach ($row in $model.SceneRows) {
    if ($row.Length -ne $sceneWidth) {
        throw "Banner scene rows are not rectangular ($($row.Length) vs $sceneWidth)."
    }
}
$scene = $model.SceneRows -join ''
$glassCount = @($scene.ToCharArray() | Where-Object { $_ -eq 'c' }).Count
if ($glassCount -lt 80 -or $scene.IndexOf('W') -lt 0 -or $scene.IndexOf('e') -lt 0) {
    throw 'The RedPrism banner lost its incoming beam or glass prism.'
}
foreach ($ray in @('r', 'o', 'y', 'n', 't', 'b', 'm')) {
    if ($scene.IndexOf($ray) -lt 0) {
        throw "The RedPrism banner lost spectrum ray '$ray'."
    }
}

$bannerText = @(& { Write-RedPrismBuildBanner -UseColor $false -Environment @{ TERM = 'dumb' } } 6>&1 |
        ForEach-Object { $_.ToString() }) -join "`n"
$fullBlock = [char]0x2588
$upperHalf = [char]0x2580
$lowerHalf = [char]0x2584
$boxTopLeft = [char]0x2554
$pixelCount = @($bannerText.ToCharArray() | Where-Object {
        $_ -eq $fullBlock -or $_ -eq $upperHalf -or $_ -eq $lowerHalf
    }).Count
if ($pixelCount -lt 80 -or
    $bannerText.IndexOf($boxTopLeft) -lt 0 -or
    $bannerText -notmatch 'REDPRISM // RASTER COMPOSITION // BUILD LOCKED') {
    throw "The RedPrism banner lost its framed prism mark or build-locked tagline: $bannerText"
}
if ($bannerText -match 'XENEON' -or $bannerText -match 'BUILD SIGNAL LOCKED') {
    throw 'The RedPrism banner copied another product mark.'
}

$redirectedInteractive = Test-RedPrismInteractiveTerminal `
    -IsOutputRedirected $true `
    -IsErrorRedirected $true `
    -HasRawUi $true `
    -CanReadWindowTitle $true `
    -Environment @{ CI = 'true' }
if ($redirectedInteractive) {
    throw 'A CI-style redirected host was incorrectly treated as an interactive terminal.'
}

$windowsTerminal = Test-RedPrismInteractiveTerminal `
    -IsOutputRedirected $true `
    -IsErrorRedirected $true `
    -HasRawUi $true `
    -CanReadWindowTitle $true `
    -Environment @{ WT_SESSION = '1' }
if (-not $windowsTerminal) {
    throw 'Windows Terminal was not treated as an interactive color host.'
}

$dumbTrueColor = Test-RedPrismTrueColorHost -UseColor $true -Environment @{ TERM = 'dumb'; WT_SESSION = '1' }
if ($dumbTrueColor) {
    throw 'A dumb terminal was incorrectly treated as a truecolor host.'
}

$wtTrueColor = Test-RedPrismTrueColorHost -UseColor $true -Environment @{ WT_SESSION = '1' }
if (-not $wtTrueColor) {
    throw 'Windows Terminal was not treated as a truecolor host.'
}

$plainTrueColor = Test-RedPrismTrueColorHost -UseColor $false -Environment @{ WT_SESSION = '1' }
if ($plainTrueColor) {
    throw 'Truecolor leaked into the plain banner path.'
}

Write-Host 'PASS build presentation banner identity'

$validated = 'DxUi update available: pinned aaaaaaaaaaaa, available bbbbbbbbbbbb. https://example . Update lock on a branch and run the product regressions. (Checked 2026-09-22 09:00:00Z)'
$unvalidated = 'DxUi main bbbbbbbbbbbb has no successful completed validation; keep pinned aaaaaaaaaaaa. (Checked 2026-09-22 09:00:00Z)'
$unavailable = 'DxUi update check unavailable; the exact pinned dependency remains selected. (Checked 2026-09-22 09:00:00Z)'
if ((Get-RedPrismDxUiUpdateNoticeForegroundColor -Notice $validated) -ne 'Yellow' -or
    (Get-RedPrismDxUiUpdateNoticeForegroundColor -Notice $unvalidated) -ne 'Red' -or
    (Get-RedPrismDxUiUpdateNoticeForegroundColor -Notice $unavailable) -ne 'DarkYellow' -or
    (Get-RedPrismDxUiUpdateNoticeForegroundColor -Notice 'DxUi pin and main diverge; review lock before any change.') -ne 'DarkYellow' -or
    $null -ne (Get-RedPrismDxUiUpdateNoticeForegroundColor -Notice '   ')) {
    throw 'DxUi update notices did not keep the RedSalamander severity colors.'
}

$buildScript = Get-Content -LiteralPath (Join-Path $repo 'build.ps1') -Raw
$restoreScript = Get-Content -LiteralPath (Join-Path $repo 'restore-dxui.ps1') -Raw
if ($buildScript -notmatch 'restore-dxui\.ps1''\) -Platform \$Platform -MSBuildPath \$msbuild -CheckUpdates' -or
    $restoreScript -notmatch 'Show-DxUiUpdateNotice -LockFile \$pinPath') {
    throw 'The RedPrism build does not request the DxUi update advisory.'
}

Write-Host 'PASS DxUi update notice'

$lucideLockPath = Join-Path $repo 'Dependencies\Lucide.lock.json'
$lucideLock = Get-Content -LiteralPath $lucideLockPath -Raw | ConvertFrom-Json
if ($lucideLock.repository -ne 'https://github.com/lucide-icons/lucide' -or
    $lucideLock.commit -notmatch '^[0-9a-f]{40}$' -or $lucideLock.license -ne 'ISC') {
    throw 'Lucide.lock.json is not the pinned ISC Lucide commit.'
}
$lucideLicense = Get-Content -LiteralPath (Join-Path $repo 'ThirdParty\Lucide\LICENSE') -Raw
if ($lucideLicense -notmatch 'ISC License' -or $lucideLicense -notmatch 'Cole Bemis') {
    throw 'ThirdParty/Lucide/LICENSE is missing the ISC grant or the Feather MIT notice.'
}
foreach ($icon in $lucideLock.icons) {
    $svg = Join-Path $repo "ThirdParty\Lucide\icons\$icon.svg"
    if (-not (Test-Path -LiteralPath $svg)) {
        throw "Lucide tool icon is missing: $icon.svg"
    }
}
$packageScript = Get-Content -LiteralPath (Join-Path $repo 'package.ps1') -Raw
if ($packageScript -notmatch 'ThirdParty\\Lucide\\LICENSE') {
    throw 'The package does not ship the Lucide license.'
}

Write-Host 'PASS Lucide pin and license'
