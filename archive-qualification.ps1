<#
.SYNOPSIS
Archives explicitly frozen qualification inputs with verified, read-only payload sharing.
.DESCRIPTION
InputManifest is a JSON array of Path (absolute physical input), RelativePath (within its role), Role
(Sources or Evidence), Length and Hash (SHA-256). Optional ProvenancePath retains the original logical input
name when Path is a frozen source copy. RunRecord is the collector's JSON attempt record, retained verbatim.
The caller still owns source-freeze, attempt freshness, output/profile/stage and independent qualification checks.
ArchiveRoot must already exist under an authorized workspace; this command never creates a drive-root folder.
See Specs/Build/Build_QualificationArchives.md. The returned receipt proves preservation, not a test pass.
.EXAMPLE
./archive-qualification.ps1 -ArchiveRoot D:\RedPrism.Perf\archives\qualification -ArchiveName attempt-001 `
    -InputManifest C:\work\frozen-inputs.json -RunRecord C:\work\result.json
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $ArchiveRoot,
    [Parameter(Mandatory)] [ValidatePattern('^[A-Za-z0-9][A-Za-z0-9_.-]{0,95}$')] [string] $ArchiveName,
    [Parameter(Mandatory)] [string] $InputManifest,
    [Parameter(Mandatory)] [string] $RunRecord
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'Build\QualificationArchive.psm1') -Force
if ($ArchiveName -match '[. ]$|^(?i:CON|PRN|AUX|NUL|COM[0-9]|LPT[0-9])(?:\.|$)') { throw 'Unsafe archive name.' }

function Write-ArchiveJson([string] $Path, $Value) {
    $bytes = [Text.UTF8Encoding]::new($false).GetBytes((ConvertTo-Json -InputObject $Value -Depth 30))
    $file = [IO.FileStream]::new($Path, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::Read,
        4096, [IO.FileOptions]::WriteThrough)
    try { $file.Write($bytes); $file.Flush($true) } finally { $file.Dispose() }
    [IO.File]::SetAttributes($Path, [IO.File]::GetAttributes($Path) -bor [IO.FileAttributes]::ReadOnly)
}
function Get-Snapshot([string] $Path) {
    $item = Get-Item -LiteralPath $Path -Force
    if ($item.PSIsContainer) { throw "Expected a file: $Path" }
    @{ Path = $item.FullName; Length = $item.Length; Hash = (Get-FileHash -LiteralPath $item.FullName -Algorithm SHA256).Hash }
}

$root = [IO.Path]::GetFullPath($ArchiveRoot).TrimEnd('\')
if (-not [IO.Directory]::Exists($root)) { throw 'ArchiveRoot must already exist in an authorized workspace.' }
$inputSnapshot = Get-Snapshot $InputManifest
$runSnapshot = Get-Snapshot $RunRecord
$rows = @(Get-Content -LiteralPath $inputSnapshot.Path -Raw | ConvertFrom-Json -DateKind String)
$null = Get-Content -LiteralPath $runSnapshot.Path -Raw | ConvertFrom-Json -DateKind String
if ($rows.Count -eq 0) { throw 'An empty input manifest cannot establish a qualification archive.' }
$names = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($row in $rows) {
    foreach ($field in 'Path', 'RelativePath', 'Role', 'Length', 'Hash') {
        if ($field -notin $row.PSObject.Properties.Name) { throw "Missing manifest field: $field" }
    }
    if ($row.Role -cnotin 'Sources', 'Evidence') { throw 'Only Sources and Evidence roles are supported.' }
    $relative = [string]$row.RelativePath
    # Refuse traversal, ADS, reserved devices, aliases and Win32 trailing-dot/space normalization.
    if ([IO.Path]::IsPathRooted($relative) -or $relative -match '[:*?"<>|]' -or
        @($relative -split '[\\/]' | Where-Object {
            $_ -in '', '.', '..' -or $_ -match '[. ]$|^(?i:CON|PRN|AUX|NUL|COM[0-9]|LPT[0-9])(?:\.|$)'
        }).Count) { throw "Unsafe relative archive path: $relative" }
    if (-not $names.Add($row.Role + '\' + $relative.Replace('/', '\'))) { throw "Duplicate archive path: $relative" }
    if ([string]$row.Hash -notmatch '^[a-fA-F0-9]{64}$' -or [string]$row.Length -notmatch '^\d+$' -or [long]$row.Length -lt 0) { throw 'Invalid expected hash/length.' }
    if (-not [IO.Path]::IsPathFullyQualified([string]$row.Path)) { throw 'Input paths must be absolute.' }
}
$archive = Join-Path $root $ArchiveName
$store = [RedPrism.Archives.Store]::new($root)
$created = $false
try {
    if ([IO.Directory]::Exists($archive) -or [IO.File]::Exists($archive)) { throw 'Refuse archive reuse, including incomplete attempts.' }
    $store.CreateDirectory($archive)
    $created = $true
    Write-ArchiveJson (Join-Path $archive 'archive-started.json') ([ordered]@{
        Schema = 1; StartedUtc = [DateTimeOffset]::UtcNow.ToString('O'); WriterHash = (Get-FileHash -LiteralPath $PSCommandPath).Hash
        ModuleHash = (Get-FileHash -LiteralPath (Join-Path $PSScriptRoot 'Build\QualificationArchive.psm1')).Hash
        BackendHash = (Get-FileHash -LiteralPath (Join-Path $PSScriptRoot 'Build\QualificationArchive.cs')).Hash
        InputManifestHash = $inputSnapshot.Hash; RunRecordHash = $runSnapshot.Hash
    })
    $sources = [Collections.Generic.List[object]]::new()
    $evidence = [Collections.Generic.List[object]]::new()
    $runRecords = @(
        $store.Archive($inputSnapshot.Path, (Join-Path $archive 'Run\inputs.json'), $inputSnapshot.Hash, $inputSnapshot.Length, $false)
        $store.Archive($runSnapshot.Path, (Join-Path $archive 'Run\result.json'), $runSnapshot.Hash, $runSnapshot.Length, $false)
    )
    foreach ($row in $rows) {
        # Run logs, captures, receipts and manifests always have independent file objects, even if bytes match.
        $share = $row.Role -ceq 'Sources' -or [IO.Path]::GetExtension($row.RelativePath).ToLowerInvariant() -in '.exe', '.pdb', '.dll', '.lib', '.obj'
        if ($row.RelativePath -match '(^|[\\/])\.git([\\/]|$)|\.bundle$') { $share = $false }
        $entry = $store.Archive([string]$row.Path, (Join-Path $archive ($row.Role + '\' + $row.RelativePath)),
            [string]$row.Hash, [long]$row.Length, $share)
        if ('ProvenancePath' -in $row.PSObject.Properties.Name) { $entry.Path = [string]$row.ProvenancePath }
        if ($row.Role -ceq 'Sources') { $sources.Add($entry) } else { $evidence.Add($entry) }
    }
    Write-ArchiveJson (Join-Path $archive 'source-manifest.json') @($sources.ToArray())
    Write-ArchiveJson (Join-Path $archive 'evidence-manifest.json') @($evidence.ToArray())
    Write-ArchiveJson (Join-Path $archive 'run-manifest.json') $runRecords
    # Separate path-based full readback, after storage operations and before the completion marker.
    $all = @($sources.ToArray()) + @($evidence.ToArray()) + $runRecords
    foreach ($entry in $all) {
        if ((Get-Item -LiteralPath $entry.ArchivedPath -Force).Length -ne $entry.Length -or
            (Get-FileHash -LiteralPath $entry.ArchivedPath -Algorithm SHA256).Hash -cne $entry.Hash) {
            throw "Independent archive readback differs: $($entry.ArchivedPath)"
        }
    }
    $completed = [ordered]@{
        Schema = 1; Attempt = (Get-Content -LiteralPath (Join-Path $archive 'Run\result.json') -Raw | ConvertFrom-Json -DateKind String)
        Inputs = $sources.Count; Evidence = $evidence.Count; RunRecords = $runRecords.Count
        SourceManifestHash = (Get-FileHash -LiteralPath (Join-Path $archive 'source-manifest.json')).Hash
        EvidenceManifestHash = (Get-FileHash -LiteralPath (Join-Path $archive 'evidence-manifest.json')).Hash
        RunManifestHash = (Get-FileHash -LiteralPath (Join-Path $archive 'run-manifest.json')).Hash
        CompleteUtc = [DateTimeOffset]::UtcNow.ToString('O')
    }
    Write-ArchiveJson (Join-Path $archive 'archive-completed.json') $completed
    [pscustomobject]@{
        Archive = $archive; Copies = $all.Count
        SharedReused = @($all | Where-Object Storage -EQ Reused).Count
        SharedCreated = @($all | Where-Object Storage -EQ NewObject).Count
        Independent = @($all | Where-Object Storage -EQ Independent).Count
        SourceManifestHash = $completed.SourceManifestHash; EvidenceManifestHash = $completed.EvidenceManifestHash
        CompletionHash = (Get-FileHash -LiteralPath (Join-Path $archive 'archive-completed.json')).Hash
        VerifiedUtc = [DateTimeOffset]::UtcNow.ToString('O')
    }
} catch {
    if ($created -and [IO.Directory]::Exists($archive) -and -not [IO.File]::Exists((Join-Path $archive 'archive-failed.json')) -and
        -not [IO.File]::Exists((Join-Path $archive 'archive-completed.json'))) {
        Write-ArchiveJson (Join-Path $archive 'archive-failed.json') @{ Error = $_.Exception.Message; FailedUtc = [DateTimeOffset]::UtcNow.ToString('O') }
    }
    throw
} finally { $store.Dispose() }
