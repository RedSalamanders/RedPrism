[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$downloader = Join-Path $repo 'Build/bench/get-raw-corpus.ps1'
$sourceManifest = Join-Path $repo 'Tests/Fixtures/raw-corpus.json'
$licensePath = Join-Path $repo 'Tests/Fixtures/Licenses/CC0-1.0.txt'
$fixture = Join-Path $repo ('.build/BuildProcessTests/RawCorpus-' + [guid]::NewGuid().ToString('N'))
[void](New-Item -ItemType Directory -Path $fixture -Force)

try {
    $baseline = Get-Content -Raw -LiteralPath $sourceManifest | ConvertFrom-Json

    function Copy-Manifest($Value) {
        return ($Value | ConvertTo-Json -Depth 100 | ConvertFrom-Json)
    }

    function Write-Manifest($Value, [string] $Name) {
        $path = Join-Path $fixture ($Name + '.json')
        $Value | ConvertTo-Json -Depth 100 | Set-Content -LiteralPath $path -Encoding utf8NoBOM
        return $path
    }

    function Invoke-Preview([string] $Path, [switch] $IncludeSupplemental) {
        $arguments = @{ ManifestPath = $Path }
        if ($IncludeSupplemental) { $arguments.IncludeSupplemental = $true }
        $before = @(Get-ChildItem -LiteralPath $fixture -File | Select-Object -ExpandProperty Name | Sort-Object)
        $output = (& $downloader @arguments *>&1 | Out-String)
        $after = @(Get-ChildItem -LiteralPath $fixture -File | Select-Object -ExpandProperty Name | Sort-Object)
        if (Compare-Object $before $after) { throw "Preview wrote files in the control directory: $output" }
        if ($output -match '(?i)downloading id|httpsend|http request') {
            throw "Preview unexpectedly attempted a network download: $output"
        }
        return $output
    }

    function Assert-PreviewRejected([string] $Name, [string] $Path, [string] $ExpectedText = '') {
        $rejected = $false
        try { $output = Invoke-Preview $Path }
        catch { $rejected = $true; $output = $_.Exception.Message }
        if (-not $rejected -or ($ExpectedText -and $output -notmatch [regex]::Escape($ExpectedText))) {
            throw "Expected RAW corpus preview rejection '$Name'; got: $output"
        }
        Write-Host "PASS raw-corpus.$Name"
    }

    function Assert-HasId([string] $Name, [string] $Output, [int] $Id, [bool] $Expected) {
        $found = $Output -match ("(?m)\bid\s*:\s*" + $Id + "\b")
        if ($found -ne $Expected) {
            throw "RAW corpus preview selection '$Name' expected id $Id presence=$Expected; output: $Output"
        }
    }

    function Assert-Rejected([string] $Name, [string] $Path, [string] $ExpectedText, [string] $Root = '') {
        $arguments = @{ ManifestPath = $Path; Download = $true; Offline = $true }
        if ($Root) { $arguments.PerfRoot = $Root }
        $rejected = $false
        try { $output = (& $downloader @arguments 2>&1 | Out-String) }
        catch { $rejected = $true; $output = $_.Exception.Message }
        if (-not $rejected -or $output -notmatch [regex]::Escape($ExpectedText)) {
            throw "Expected RAW corpus rejection '$Name' containing '$ExpectedText'; got: $output"
        }
        Write-Host "PASS raw-corpus.$Name"
    }

    # The local legal text is part of the checked-in provenance, not something
    # fetched by preview or by this test.
    if (-not (Test-Path -LiteralPath $licensePath -PathType Leaf)) {
        throw "Canonical local CC0 license text is missing: $licensePath"
    }
    $licenseHash = (Get-FileHash -LiteralPath $licensePath -Algorithm SHA256).Hash.ToLowerInvariant()
    if (-not $baseline.licenseText -or
        $baseline.licenseText.sourceUrl -cne 'https://creativecommons.org/publicdomain/zero/1.0/legalcode.txt' -or
        $baseline.licenseText.sha256 -cne $licenseHash) {
        throw 'Manifest licenseText sourceUrl/sha256 does not match the canonical local CC0 legal text.'
    }
    Write-Host 'PASS raw-corpus.canonical local CC0 text hash matches manifest'

    $defaultPreview = Invoke-Preview $sourceManifest
    $defaultEntries = @($baseline.files | Where-Object { $_.intendedUse -in @('gating', 'correctness') })
    $correctnessEntries = @($baseline.files | Where-Object intendedUse -eq 'correctness')
    $supplementalEntries = @($baseline.files | Where-Object intendedUse -eq 'supplemental')
    $actualExtensions = @($defaultEntries | ForEach-Object { [IO.Path]::GetExtension($_.file).TrimStart('.').ToLowerInvariant() } | Sort-Object -Unique)
    $requiredExtensions = @('arw', 'cr2', 'cr3', 'dng', 'nef', 'orf', 'pef', 'raf', 'rw2')
    if (Compare-Object $requiredExtensions $actualExtensions) {
        throw 'The real default corpus must retain a sample of every planned v1 RAW extension.'
    }
    Write-Host 'PASS raw-corpus.real default corpus covers every planned v1 RAW extension'
    if ($correctnessEntries.Count -eq 0 -or $supplementalEntries.Count -eq 0 -or
        $defaultPreview -notmatch ("Preview only: " + $defaultEntries.Count + ' files,') -or
        $defaultPreview -match ('(?m)\bid\s*:\s*' + [regex]::Escape([string]$supplementalEntries[0].id) + '\b')) {
        throw "Default preview must include gating and correctness records and omit supplemental: $defaultPreview"
    }
    foreach ($entry in $defaultEntries) {
        Assert-HasId 'default selected role' $defaultPreview ([int]$entry.id) $true
    }
    foreach ($entry in $supplementalEntries) {
        Assert-HasId 'default excludes supplemental' $defaultPreview ([int]$entry.id) $false
    }
    Write-Host 'PASS raw-corpus.default preview selects gating and correctness roles'

    $supplementalPreview = Invoke-Preview $sourceManifest -IncludeSupplemental
    if ($supplementalPreview -notmatch ("Preview only: " + ($defaultEntries.Count + $supplementalEntries.Count) + ' files,')) {
        throw "Supplemental opt-in must add the supplemental record: $supplementalPreview"
    }
    foreach ($entry in $supplementalEntries) {
        Assert-HasId 'supplemental opt-in selection' $supplementalPreview ([int]$entry.id) $true
    }
    Write-Host 'PASS raw-corpus.supplemental opt-in selection'

    # Exercise the complete extension allowlist with isolated, one-row manifests.
    foreach ($extension in @('arw', 'cr2', 'cr3', 'dng', 'nef', 'orf', 'pef', 'raf', 'rw2')) {
        $single = Copy-Manifest $baseline
        $entry = Copy-Manifest ($baseline.files | Where-Object intendedUse -eq 'gating' | Select-Object -First 1)
        $entry.file = [IO.Path]::GetFileNameWithoutExtension($entry.file) + '.' + $extension
        $entry.intendedUse = 'correctness'
        $single.files = @($entry)
        $preview = Invoke-Preview (Write-Manifest $single "extension-$extension")
        if ($preview -notmatch 'Preview only: 1 files,') {
            throw "Supported extension .$extension was not accepted by preview: $preview"
        }
        Write-Host "PASS raw-corpus.accepts .$extension"
    }

    $invalidSchema = Copy-Manifest $baseline
    $invalidSchema.schema = 'redprism.other'
    Assert-PreviewRejected 'rejects unsupported schema' (Write-Manifest $invalidSchema 'schema') 'Unsupported RAW corpus manifest schema/version.'

    $unsupportedExtension = Copy-Manifest $baseline
    $unsupportedExtension.files = @((Copy-Manifest $baseline.files[0]))
    $unsupportedExtension.files[0].file = [IO.Path]::GetFileNameWithoutExtension($unsupportedExtension.files[0].file) + '.jpg'
    Assert-PreviewRejected 'rejects unsupported extension' (Write-Manifest $unsupportedExtension 'unsupported-extension')

    $traversal = Copy-Manifest $baseline
    $traversal.files = @((Copy-Manifest $baseline.files[0]))
    $traversal.files[0].file = '../outside.arw'
    Assert-PreviewRejected 'rejects traversal filename' (Write-Manifest $traversal 'traversal')

    $unknownRole = Copy-Manifest $baseline
    $unknownRole.files = @((Copy-Manifest $baseline.files[0]))
    $unknownRole.files[0].intendedUse = 'optional'
    Assert-PreviewRejected 'rejects unrecognized role' (Write-Manifest $unknownRole 'unknown-role')

    $invalidEvidence = Copy-Manifest $baseline
    $invalidSupplemental = $invalidEvidence.files | Where-Object intendedUse -eq 'supplemental' | Select-Object -First 1
    $invalidSupplemental.licenseEvidence.rowLicenseHtml = '<a href=''https://example.invalid/''>CC0</a>'
    Assert-PreviewRejected 'validates unselected supplemental license evidence' (Write-Manifest $invalidEvidence 'license-evidence') 'Missing original per-file CC0 evidence'

    $invalidLicenseHash = Copy-Manifest $baseline
    $invalidLicenseHash.licenseText.sha256 = 'f' * 64
    Assert-PreviewRejected 'rejects mismatched local license text hash' (Write-Manifest $invalidLicenseHash 'license-hash') 'CC0 legal text does not match the reviewed manifest.'

    Assert-Rejected 'rejects unsafe PerfRoot path' $sourceManifest 'PerfRoot must be exactly X:\RedPrism.Perf' 'C:\Temp\RedPrism.Perf'

    $root = 'C:\RedPrism.Perf'
    $marker = Join-Path $root '.redprism-perf-owner'
    $corpus = Join-Path $root 'corpus'
    $first = $baseline.files | Where-Object intendedUse -eq 'gating' | Select-Object -First 1
    $destination = Join-Path $corpus $first.file
    $rootUsable = (Test-Path -LiteralPath $marker -PathType Leaf) -and
        (Test-Path -LiteralPath $corpus -PathType Container) -and
        (Test-Path -LiteralPath $destination -PathType Leaf)
    if ($rootUsable) {
        $before = Get-Item -LiteralPath $destination
        $beforeHash = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($before.Length -ne $first.size.bytes -or $beforeHash -cne $first.suppliedSha256) {
            throw "Existing gating corpus file differs from the manifest; preserved without replacement: $destination"
        }
    }
    if ($rootUsable) {

        $selected = @($baseline.files | Where-Object { $_.intendedUse -in @('gating', 'correctness') })
        $allSelectedMatch = $true
        foreach ($entry in $selected) {
            $selectedPath = Join-Path $corpus $entry.file
            if (-not (Test-Path -LiteralPath $selectedPath -PathType Leaf)) {
                $allSelectedMatch = $false
                break
            }
            $selectedItem = Get-Item -LiteralPath $selectedPath
            if ($selectedItem.Length -ne $entry.size.bytes -or
                (Get-FileHash -LiteralPath $selectedPath -Algorithm SHA256).Hash.ToLowerInvariant() -cne $entry.suppliedSha256) {
                throw "Existing selected corpus file differs from the manifest; preserved without replacement: $selectedPath"
            }
        }
        if ($allSelectedMatch) {
            $manifestBytes = [IO.File]::ReadAllBytes($sourceManifest)
            $manifestHash = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($manifestBytes)).ToLowerInvariant()
            $evidencePath = Join-Path $corpus "raw-corpus-$manifestHash.json"
            $licenseDestination = Join-Path $corpus 'CC0-1.0.txt'
            $verifyOutput = (& $downloader -ManifestPath $sourceManifest -Download -Offline -PerfRoot $root *>&1 | Out-String)
            $verifiedCount = [regex]::Matches($verifyOutput, 'Verified existing:').Count
            if ($verifiedCount -ne $selected.Count) {
                throw "Offline mode verified $verifiedCount RAW files; expected all $($selected.Count): $verifyOutput"
            }
            if (-not (Test-Path -LiteralPath $licenseDestination -PathType Leaf) -or
                -not (Test-Path -LiteralPath $evidencePath -PathType Leaf)) {
                throw 'Offline verification did not materialize the local CC0 text and full-manifest evidence.'
            }
            $savedLicense = Get-Item -LiteralPath $licenseDestination
            $savedLicenseHash = (Get-FileHash -LiteralPath $licenseDestination -Algorithm SHA256).Hash.ToLowerInvariant()
            if ($savedLicense.Length -ne (Get-Item -LiteralPath $licensePath).Length -or $savedLicenseHash -cne $licenseHash) {
                throw "Corpus CC0 text is not byte-identical to the canonical local license: $licenseDestination"
            }
            $savedEvidence = Get-Item -LiteralPath $evidencePath
            $savedEvidenceHash = (Get-FileHash -LiteralPath $evidencePath -Algorithm SHA256).Hash.ToLowerInvariant()
            if ($savedEvidence.Length -ne $manifestBytes.Length -or $savedEvidenceHash -cne $manifestHash) {
                throw "Corpus evidence snapshot is not byte-identical to the exact full manifest: $evidencePath"
            }
            $verifyAgain = (& $downloader -ManifestPath $sourceManifest -Download -Offline -PerfRoot $root *>&1 | Out-String)
            if ([regex]::Matches($verifyAgain, 'Verified existing:').Count -ne $selected.Count) {
                throw "Offline mode did not re-verify every existing RAW file and accept existing CC0/evidence: $verifyAgain"
            }
            $afterLicenseHash = (Get-FileHash -LiteralPath $licenseDestination -Algorithm SHA256).Hash.ToLowerInvariant()
            $afterEvidenceHash = (Get-FileHash -LiteralPath $evidencePath -Algorithm SHA256).Hash.ToLowerInvariant()
            if ($afterLicenseHash -cne $licenseHash -or $afterEvidenceHash -cne $manifestHash) {
                throw 'Offline verification changed the existing CC0 text or exact manifest evidence bytes.'
            }
            Write-Host "PASS raw-corpus.offline verifies all $($selected.Count) RAW files and exact CC0/full-manifest bytes"
        } else {
            Write-Host 'SKIP raw-corpus all selected local files are not present with matching size and SHA256; full-manifest offline materialization was not run.'
        }

        $mismatch = Copy-Manifest $baseline
        $mismatch.files = @((Copy-Manifest $first))
        $mismatch.files[0].suppliedSha256 = 'f' * 64
        Assert-Rejected 'preserves mismatched existing destination' (Write-Manifest $mismatch 'mismatch') 'Existing file differs from the manifest; preserved without replacement' $root
        $after = Get-Item -LiteralPath $destination
        $afterHash = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($after.Length -ne $before.Length -or $afterHash -cne $beforeHash) {
            throw "The mismatched destination was modified: $destination"
        }
        Write-Host 'PASS raw-corpus.mismatched destination bytes preserved'

        $offline = Copy-Manifest $baseline
        $offline.files = @((Copy-Manifest $first))
        $offline.files[0].file = "$($first.id)-offline-control-$([guid]::NewGuid().ToString('N')).arw"
        $offlineDestination = Join-Path $corpus $offline.files[0].file
        if (Test-Path -LiteralPath $offlineDestination) { throw 'Offline test destination unexpectedly exists.' }
        Assert-Rejected 'offline refuses missing file before HTTP' (Write-Manifest $offline 'offline-missing') 'Offline mode refused 1 missing corpus file(s); no HTTP request was sent.' $root
        if (Test-Path -LiteralPath $offlineDestination) { throw 'Offline control created its missing destination.' }
    } else {
        if (-not (Test-Path -LiteralPath $marker -PathType Leaf) -or -not (Test-Path -LiteralPath $corpus -PathType Container) -or
            -not (Test-Path -LiteralPath $destination -PathType Leaf)) {
            Write-Host 'SKIP raw-corpus marked corpus or an existing matching gating file is unavailable; offline byte controls were not run.'
        }
    }
} finally {
    $path = [IO.Path]::GetFullPath($fixture)
    $parent = [IO.Path]::GetFullPath((Join-Path $repo '.build/BuildProcessTests')).TrimEnd('\') + '\'
    if (-not $path.StartsWith($parent, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Unsafe fixture cleanup path.'
    }
    Remove-Item -LiteralPath $path -Recurse -Force
}
