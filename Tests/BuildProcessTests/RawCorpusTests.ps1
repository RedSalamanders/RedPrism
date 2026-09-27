[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$downloader = Join-Path $repo 'Build/bench/get-raw-corpus.ps1'
$sourceManifest = Join-Path $repo 'Tests/Fixtures/raw-corpus.json'
$fixture = Join-Path $repo ('.build/BuildProcessTests/RawCorpus-' + [guid]::NewGuid().ToString('N'))
[void](New-Item -ItemType Directory -Path $fixture -Force)

try {
    $baseline = Get-Content -Raw -LiteralPath $sourceManifest | ConvertFrom-Json

    function Write-Manifest($Value, [string] $Name) {
        $path = Join-Path $fixture ($Name + '.json')
        $Value | ConvertTo-Json -Depth 100 | Set-Content -LiteralPath $path -Encoding utf8NoBOM
        return $path
    }

    function Assert-Rejected([string] $Name, [string] $Path, [string] $ExpectedText, [string] $Root = '') {
        $arguments = @{ ManifestPath = $Path; Download = $true; Offline = $true }
        if ($Root) { $arguments.PerfRoot = $Root }
        $rejected = $false
        try {
            $output = (& $downloader @arguments 2>&1 | Out-String)
        } catch {
            $rejected = $true
            $output = $_.Exception.Message
        }
        if (-not $rejected -or $output -notmatch [regex]::Escape($ExpectedText)) {
            throw "Expected RAW corpus rejection '$Name' containing '$ExpectedText'; got: $output"
        }
        Write-Host "PASS raw-corpus.$Name"
    }

    $invalidSchema = $baseline | ConvertTo-Json -Depth 100 | ConvertFrom-Json
    $invalidSchema.schema = 'redprism.other'
    Assert-Rejected 'rejects unsupported schema' (Write-Manifest $invalidSchema 'schema') 'Unsupported RAW corpus manifest schema/version.'

    $invalidLicense = $baseline | ConvertTo-Json -Depth 100 | ConvertFrom-Json
    $invalidLicense.files[0].licenseEvidence.rowLicenseHtml = '<a href=''https://example.invalid/''>CC0</a>'
    Assert-Rejected 'rejects mismatched license evidence' (Write-Manifest $invalidLicense 'license') 'Missing original per-file CC0 evidence'

    Assert-Rejected 'rejects unsafe PerfRoot path' $sourceManifest 'PerfRoot must be exactly X:\RedPrism.Perf' 'C:\Temp\RedPrism.Perf'

    $root = 'C:\RedPrism.Perf'
    $marker = Join-Path $root '.redprism-perf-owner'
    $corpus = Join-Path $root 'corpus'
    $rootUsable = (Test-Path -LiteralPath $marker -PathType Leaf) -and
        (Test-Path -LiteralPath $corpus -PathType Container)
    if ($rootUsable) {
        $first = $baseline.files | Where-Object intendedUse -eq 'gating' | Select-Object -First 1
        $destination = Join-Path $corpus $first.file
        if (-not (Test-Path -LiteralPath $destination -PathType Leaf)) {
            throw "Expected the already-materialized gating file for the preservation control: $destination"
        }
        $before = Get-Item -LiteralPath $destination
        $beforeHash = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($before.Length -ne $first.size.bytes -or $beforeHash -cne $first.suppliedSha256) {
            throw "The gating file changed before its read-only preservation control: $destination"
        }

        $mismatch = $baseline | ConvertTo-Json -Depth 100 | ConvertFrom-Json
        $mismatch.files = @($mismatch.files | Where-Object id -eq $first.id)
        $mismatch.files[0].suppliedSha256 = 'f' * 64
        Assert-Rejected 'preserves mismatched existing destination' (Write-Manifest $mismatch 'mismatch') 'Existing file differs from the manifest; preserved without replacement' $root
        $after = Get-Item -LiteralPath $destination
        $afterHash = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($after.Length -ne $before.Length -or $afterHash -cne $beforeHash) {
            throw "The mismatched destination was modified: $destination"
        }
        Write-Host 'PASS raw-corpus.mismatched destination bytes preserved'

        $offline = $baseline | ConvertTo-Json -Depth 100 | ConvertFrom-Json
        $offline.files = @($offline.files | Where-Object id -eq $first.id)
        $offline.files[0].file = "$($first.id)-offline-control-$([guid]::NewGuid().ToString('N')).arw"
        $offlineDestination = Join-Path $corpus $offline.files[0].file
        if (Test-Path -LiteralPath $offlineDestination) { throw 'Offline test destination unexpectedly exists.' }
        Assert-Rejected 'offline refuses missing file before HTTP' (Write-Manifest $offline 'offline') 'Offline mode refused 1 missing corpus file(s); no HTTP request was sent.' $root
        if (Test-Path -LiteralPath $offlineDestination) { throw 'Offline control created its missing destination.' }
    } else {
        Write-Host 'SKIP raw-corpus marked C:\RedPrism.Perf corpus is unavailable; root-dependent read-only controls were not run.'
    }
} finally {
    $path = [IO.Path]::GetFullPath($fixture)
    $parent = [IO.Path]::GetFullPath((Join-Path $repo '.build/BuildProcessTests')).TrimEnd('\') + '\'
    if (-not $path.StartsWith($parent, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Unsafe fixture cleanup path.'
    }
    Remove-Item -LiteralPath $path -Recurse -Force
}
