#Requires -Version 7.4
<#
.SYNOPSIS
Preview or explicitly download the reviewed P4 RAW corpus.
.DESCRIPTION
Without -Download, reads the manifest and prints the selection only.
-ManifestPath selects another manifest file for controlled validation; it does not
change the fixed PerfRoot or corpus-child ownership rules.
-Download uses an EXISTING marked X:\RedPrism.Perf on a fixed local drive,
where X is a drive letter. It may create the corpus child, but never adopts
an unmarked root or creates/repairs its marker. Root/marker semantics match
Common/PerfRoot.cpp. Root selection: -PerfRoot, REDPRISM_PERF_ROOT, then
existing marked roots on X followed by the other fixed drives in letter order.

Default selection includes the five performance gating files and the smaller
correctness files for every planned v1 extension. -IncludeSupplemental adds the
stripped Leica DNG. No retries, redirects, resumptions, or overwrites.
Successful materialization also saves the reviewed CC0 legal text and an exact
manifest snapshot beside the images. Existing differing files are preserved.
-Offline verifies already-present files and refuses missing files before creating
an HTTP client or sending a request.
Ceilings: 128 MiB/file, 768 MiB/run, 180 seconds/file, 1200 seconds/run.
The deadlines cover HTTP headers/body and checksum reads, using cancellation;
they cannot interrupt a stalled operating-system filesystem operation.
.EXAMPLE
pwsh -File Build/bench/get-raw-corpus.ps1
.EXAMPLE
pwsh -File Build/bench/get-raw-corpus.ps1 -Download -PerfRoot C:\RedPrism.Perf
#>
[CmdletBinding()]
param(
    [switch] $Download,
    [switch] $IncludeSupplemental,
    [switch] $Offline,
    [string] $PerfRoot = $env:REDPRISM_PERF_ROOT,
    [string] $ManifestPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$maxFileBytes = 128MB
$maxRunBytes = 768MB
$fileTimeoutSeconds = 180
$runTimeoutSeconds = 1200
$licenseUrl = 'https://creativecommons.org/publicdomain/zero/1.0/'
$apiUrl = 'https://raw.pixls.us/json/getrepository.php?set=all'
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$manifestPath = if ($ManifestPath) { $ManifestPath } else { Join-Path $repoRoot 'Tests\Fixtures\raw-corpus.json' }
if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
    throw "RAW corpus manifest does not exist: $manifestPath"
}
$manifestBytes = [IO.File]::ReadAllBytes($manifestPath)
$manifest = [Text.Encoding]::UTF8.GetString($manifestBytes) | ConvertFrom-Json
if ($manifest.schema -cne 'redprism.raw-corpus' -or $manifest.version -ne 1) {
    throw 'Unsupported RAW corpus manifest schema/version.'
}
$licensePath = Join-Path $repoRoot 'Tests\Fixtures\Licenses\CC0-1.0.txt'
$licenseBytes = [IO.File]::ReadAllBytes($licensePath)
$licenseHash = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($licenseBytes)).ToLowerInvariant()
if ($manifest.licenseText.sourceUrl -cne 'https://creativecommons.org/publicdomain/zero/1.0/legalcode.txt' -or
    $manifest.licenseText.sha256 -cne $licenseHash) {
    throw 'CC0 legal text does not match the reviewed manifest.'
}
$manifestHash = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($manifestBytes)).ToLowerInvariant()

$seenIds = [Collections.Generic.HashSet[int]]::new()
$seenFiles = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($entry in $manifest.files) {
    if ($entry.id -le 0 -or -not $seenIds.Add($entry.id) -or
        $entry.file -cnotmatch '\A[0-9]+-[a-z0-9-]+\.(arw|cr2|cr3|dng|nef|orf|pef|raf|rw2)\z' -or
        -not $seenFiles.Add($entry.file) -or
        $entry.suppliedSha256 -cnotmatch '\A[0-9a-f]{64}\z' -or
        $entry.intendedUse -notin @('gating', 'correctness', 'supplemental')) {
        throw 'Invalid or duplicate corpus id, simple filename, SHA256, or role.'
    }
    if ($entry.license -cne 'CC0-1.0' -or $entry.licenseUrl -cne $licenseUrl -or
        $entry.licenseEvidenceApi -cne $apiUrl -or $entry.licenseEvidence.status -cne 'confirmed' -or
        $entry.sourceRow[5] -cne $entry.licenseEvidence.rowLicenseHtml -or
        -not $entry.licenseEvidence.rowLicenseHtml.Contains("href='$licenseUrl'")) {
        throw "Missing original per-file CC0 evidence for id $($entry.id)."
    }
    $uri = [uri]$entry.originalDirectUrl
    if (-not $uri.IsAbsoluteUri -or $uri.Scheme -cne 'https' -or
        $uri.DnsSafeHost -cne 'raw.pixls.us' -or $uri.Port -ne 443 -or
        $uri.UserInfo -or $uri.Query -or $uri.Fragment -or
        -not $uri.AbsolutePath.StartsWith("/getfile.php/$($entry.id)/nice/", [StringComparison]::Ordinal)) {
        throw "Expected the original HTTPS raw.pixls.us URL for id $($entry.id)."
    }
    if ($entry.size.status -cne 'confirmed' -or $entry.size.bytes -le 0 -or
        $entry.size.bytes -gt $maxFileBytes) {
        throw "Missing or oversized server-reported byte count for id $($entry.id)."
    }
}
$selected = @($manifest.files | Where-Object { $_.intendedUse -in @('gating', 'correctness') -or $IncludeSupplemental })
$expectedRunBytes = [long](($selected | Measure-Object -Property { $_.size.bytes } -Sum).Sum)
if ($selected.Count -eq 0 -or $expectedRunBytes -gt $maxRunBytes) {
    throw 'Empty selection or selection exceeds the fixed run byte budget.'
}
if (-not $Download) {
    Write-Host "Preview only: $($selected.Count) files, $expectedRunBytes server-reported bytes. Use -Download to opt in."
    $selected | Select-Object id, file, intendedUse, @{ Name = 'Bytes'; Expression = { $_.size.bytes } }, suppliedSha256
    return
}
if (-not $IsWindows) {
    throw 'The marked PerfRoot downloader requires Windows.'
}
if ($PerfRoot -and $PerfRoot -notmatch '\A[A-Za-z]:\\RedPrism\.Perf\z') {
    throw 'PerfRoot must be exactly X:\RedPrism.Perf, with a fixed local drive letter.'
}

# The native operations keep directories, marker, existing files and .part files
# open without delete sharing. Attribute checks alone leave a replacement race.
# Rename/discard operate on the held .part handle, never an unchecked source path.
# https://learn.microsoft.com/windows/win32/api/winbase/ns-winbase-file_rename_info
if ($null -eq ('RedPrism.RawCorpusFilesV1' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using Microsoft.Win32.SafeHandles;

namespace RedPrism
{
    public static class RawCorpusFilesV1
    {
        const uint Read = 0x80000000, Write = 0x40000000, Delete = 0x00010000;
        const uint OpenReparse = 0x00200000, BackupSemantics = 0x02000000;
        const uint Sequential = 0x08000000, Directory = 0x10, ReparsePoint = 0x400;

        [StructLayout(LayoutKind.Sequential)]
        struct AttributeTagInfo { public uint Attributes; public uint ReparseTag; }

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        static extern SafeFileHandle CreateFileW(string name, uint access, uint share,
            IntPtr security, uint creation, uint flags, IntPtr template);

        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool GetFileInformationByHandleEx(SafeFileHandle file, int kind,
            out AttributeTagInfo info, uint size);

        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool SetFileInformationByHandle(SafeFileHandle file, int kind,
            byte[] info, uint size);

        static SafeFileHandle Open(string path, bool directory, uint access,
            uint share, uint creation, bool missingAllowed)
        {
            var handle = CreateFileW(path, access, share, IntPtr.Zero, creation,
                OpenReparse | BackupSemantics | (directory ? 0 : Sequential), IntPtr.Zero);
            if (handle.IsInvalid)
            {
                int error = Marshal.GetLastWin32Error();
                handle.Dispose();
                if (missingAllowed && (error == 2 || error == 3)) return null;
                throw new Win32Exception(error, path);
            }
            try
            {
                AttributeTagInfo info;
                if (!GetFileInformationByHandleEx(handle, 9, out info, 8))
                    throw new Win32Exception(Marshal.GetLastWin32Error(), path);
                if ((info.Attributes & ReparsePoint) != 0 ||
                    ((info.Attributes & Directory) != 0) != directory)
                    throw new IOException("Reparse point or wrong object type: " + path);
                return handle;
            }
            catch { handle.Dispose(); throw; }
        }

        public static SafeFileHandle TryPinDirectory(string path)
        {
            // FILE_READ_ATTRIBUTES, share read/write but deny delete.
            return Open(path, true, 0x80, 3, 3, true);
        }

        static FileStream Stream(SafeFileHandle handle, FileAccess access)
        {
            if (handle == null) return null;
            try { return new FileStream(handle, access, 65536, false); }
            catch { handle.Dispose(); throw; }
        }

        public static FileStream TryOpenRead(string path)
        {
            return Stream(Open(path, false, Read, 1, 3, true), FileAccess.Read);
        }

        public static FileStream CreatePart(string path)
        {
            // CREATE_NEW, deny write/delete sharing, own DELETE for handle rename.
            return Stream(Open(path, false, Read | Write | Delete, 1, 1, false), FileAccess.ReadWrite);
        }

        public static void CommitNew(FileStream part, string destination)
        {
            // FILE_RENAME_INFO: zero flags means ReplaceIfExists = FALSE.
            // Native pointer alignment differs on 32-bit and 64-bit hosts.
            int rootOffset = IntPtr.Size == 8 ? 8 : 4;
            int lengthOffset = rootOffset + IntPtr.Size;
            int nameOffset = lengthOffset + 4;
            byte[] name = Encoding.Unicode.GetBytes(destination + "\0");
            byte[] info = new byte[nameOffset + name.Length];
            Buffer.BlockCopy(BitConverter.GetBytes(name.Length - 2), 0, info, lengthOffset, 4);
            Buffer.BlockCopy(name, 0, info, nameOffset, name.Length);
            if (!SetFileInformationByHandle(part.SafeFileHandle, 3, info, (uint)info.Length))
                throw new Win32Exception(Marshal.GetLastWin32Error(), "Commit refused: " + destination);
        }

        public static void DiscardPart(FileStream part)
        {
            // FILE_DISPOSITION_INFO: delete only this owned, still-open .part.
            if (!SetFileInformationByHandle(part.SafeFileHandle, 4, new byte[] { 1 }, 1))
                throw new Win32Exception(Marshal.GetLastWin32Error(), "Could not discard the owned .part file.");
        }
    }
}
'@
}

function Open-OwnedPerfRoot([string] $Path) {
    if ($Path -notmatch '\A[A-Za-z]:\\RedPrism\.Perf\z' -or
        [IO.DriveInfo]::new($Path.Substring(0, 3)).DriveType -ne [IO.DriveType]::Fixed) {
        throw 'PerfRoot must be exactly X:\RedPrism.Perf on a fixed local drive.'
    }
    $directory = $null
    $marker = $null
    try {
        $directory = [RedPrism.RawCorpusFilesV1]::TryPinDirectory($Path)
        if ($null -eq $directory) { throw "PerfRoot does not exist: $Path" }
        $marker = [RedPrism.RawCorpusFilesV1]::TryOpenRead((Join-Path $Path '.redprism-perf-owner'))
        $expected = [Text.Encoding]::ASCII.GetBytes(('RedPrism performance root owner v1' + [char]10))
        if ($null -eq $marker -or $marker.Length -ne $expected.Length) {
            throw "Missing or invalid ownership marker: $Path"
        }
        $actual = [byte[]]::new($expected.Length)
        $marker.ReadExactly($actual, 0, $actual.Length)
        for ($index = 0; $index -lt $expected.Length; ++$index) {
            if ($actual[$index] -ne $expected[$index]) { throw "Invalid ownership marker bytes: $Path" }
        }
        return [pscustomobject]@{ Path = $Path; Directory = $directory; Marker = $marker }
    } catch {
        if ($marker) { $marker.Dispose() }
        if ($directory) { $directory.Dispose() }
        throw
    }
}

function Get-CorpusHash([IO.Stream] $Stream, [Threading.CancellationToken] $Token) {
    $Stream.Position = 0
    $hash = [Security.Cryptography.IncrementalHash]::CreateHash([Security.Cryptography.HashAlgorithmName]::SHA256)
    $buffer = [byte[]]::new(65536)
    $counted = 0L
    try {
        while ($true) {
            $Token.ThrowIfCancellationRequested()
            $count = $Stream.ReadAsync($buffer, 0, $buffer.Length, $Token).GetAwaiter().GetResult()
            if ($count -eq 0) { break }
            $counted += $count
            if ($counted -gt $maxFileBytes) { throw 'Checksum input exceeds the file byte budget.' }
            $hash.AppendData($buffer, 0, $count)
        }
        return [Convert]::ToHexString($hash.GetHashAndReset()).ToLowerInvariant()
    } finally { $hash.Dispose() }
}

function Save-CorpusEvidence([string] $Directory, [Threading.CancellationToken] $Token) {
    foreach ($artifact in @(
        @{ Name = 'CC0-1.0.txt'; Bytes = $licenseBytes; Hash = $licenseHash },
        @{ Name = "raw-corpus-$manifestHash.json"; Bytes = $manifestBytes; Hash = $manifestHash }
    )) {
        $target = Join-Path $Directory $artifact.Name
        $stream = [RedPrism.RawCorpusFilesV1]::TryOpenRead($target)
        if ($stream) {
            try {
                if ($stream.Length -ne $artifact.Bytes.Length -or (Get-CorpusHash $stream $Token) -cne $artifact.Hash) {
                    throw "Existing license/evidence differs; preserved without replacement: $target"
                }
            } finally { $stream.Dispose() }
            continue
        }
        $part = $null
        $committed = $false
        $partPath = Join-Path $Directory ($artifact.Name + '.' + [guid]::NewGuid().ToString('N') + '.part')
        try {
            $Token.ThrowIfCancellationRequested()
            $part = [RedPrism.RawCorpusFilesV1]::CreatePart($partPath)
            $part.Write($artifact.Bytes, 0, $artifact.Bytes.Length)
            $part.Flush($true)
            $Token.ThrowIfCancellationRequested()
            [RedPrism.RawCorpusFilesV1]::CommitNew($part, $target)
            $committed = $true
            Write-Host "Saved license/evidence: $target"
        } finally {
            try {
                if ($part -and -not $committed) { [RedPrism.RawCorpusFilesV1]::DiscardPart($part) }
            } finally { if ($part) { $part.Dispose() } }
        }
    }
}

$owned = $null
$corpusHandle = $null
$client = $null
$runCancellation = [Threading.CancellationTokenSource]::new([TimeSpan]::FromSeconds($runTimeoutSeconds))
$existing = [Collections.Generic.List[IDisposable]]::new()
try {
    if ($PerfRoot) {
        $owned = Open-OwnedPerfRoot $PerfRoot
    } else {
        $driveNames = @('X:\') + @(
            [IO.DriveInfo]::GetDrives() |
                Where-Object { $_.DriveType -eq [IO.DriveType]::Fixed -and $_.Name -ne 'X:\' } |
                Sort-Object Name | ForEach-Object Name
        )
        foreach ($driveName in $driveNames) {
            $runCancellation.Token.ThrowIfCancellationRequested()
            try { $owned = Open-OwnedPerfRoot ($driveName + 'RedPrism.Perf') }
            catch { Write-Verbose $_.Exception.Message }
            if ($owned) { break }
        }
        if ($null -eq $owned) {
            throw 'No valid marked PerfRoot found. Supply -PerfRoot for an existing owned X:\RedPrism.Perf.'
        }
    }

    $corpusPath = Join-Path $owned.Path 'corpus'
    $corpusHandle = [RedPrism.RawCorpusFilesV1]::TryPinDirectory($corpusPath)
    if ($null -eq $corpusHandle) {
        [IO.Directory]::CreateDirectory($corpusPath) | Out-Null
        $corpusHandle = [RedPrism.RawCorpusFilesV1]::TryPinDirectory($corpusPath)
        if ($null -eq $corpusHandle) { throw 'Could not pin the new corpus directory.' }
    }

    # Check every existing selected target before sending any HTTP request.
    # Hold matching files open through the run so they cannot change under us.
    $pending = [Collections.Generic.List[object]]::new()
    foreach ($entry in $selected) {
        $target = Join-Path $corpusPath $entry.file
        $stream = [RedPrism.RawCorpusFilesV1]::TryOpenRead($target)
        if ($null -eq $stream) { $pending.Add($entry); continue }
        $existing.Add($stream)
        $checkCancellation = [Threading.CancellationTokenSource]::CreateLinkedTokenSource($runCancellation.Token)
        try {
            $checkCancellation.CancelAfter([TimeSpan]::FromSeconds($fileTimeoutSeconds))
            if ($stream.Length -ne $entry.size.bytes -or
                (Get-CorpusHash $stream $checkCancellation.Token) -cne $entry.suppliedSha256) {
                throw "Existing file differs from the manifest; preserved without replacement: $target"
            }
            Write-Host "Verified existing: $target"
        } finally { $checkCancellation.Dispose() }
    }
    if ($pending.Count -eq 0) {
        Save-CorpusEvidence $corpusPath $runCancellation.Token
        return
    }
    if ($Offline) {
        throw "Offline mode refused $($pending.Count) missing corpus file(s); no HTTP request was sent."
    }

    # Save the license and source recipe before any image is committed. If a later transfer fails, every saved
    # image still has its license and original per-file row locally. This snapshot need not claim decode success.
    Save-CorpusEvidence $corpusPath $runCancellation.Token

    $handler = [Net.Http.HttpClientHandler]::new()
    $handler.AllowAutoRedirect = $false
    $handler.UseCookies = $false
    $handler.AutomaticDecompression = [Net.DecompressionMethods]::None
    $client = [Net.Http.HttpClient]::new($handler)
    $client.Timeout = [Threading.Timeout]::InfiniteTimeSpan
    $client.DefaultRequestHeaders.UserAgent.ParseAdd('RedPrism-P4-Corpus/1.0')
    $runBytes = 0L
    $buffer = [byte[]]::new(65536)

    foreach ($entry in $pending) {
        $target = Join-Path $corpusPath $entry.file
        $partPath = Join-Path $corpusPath ($entry.file + '.' + [guid]::NewGuid().ToString('N') + '.part')
        $fileCancellation = [Threading.CancellationTokenSource]::CreateLinkedTokenSource($runCancellation.Token)
        $request = $null
        $response = $null
        $inputStream = $null
        $part = $null
        $committed = $false
        try {
            $fileCancellation.CancelAfter([TimeSpan]::FromSeconds($fileTimeoutSeconds))
            $token = $fileCancellation.Token
            $token.ThrowIfCancellationRequested()
            Write-Host "Downloading id $($entry.id) to $target"
            $request = [Net.Http.HttpRequestMessage]::new([Net.Http.HttpMethod]::Get, [uri]$entry.originalDirectUrl)
            $response = $client.SendAsync($request, [Net.Http.HttpCompletionOption]::ResponseHeadersRead, $token).GetAwaiter().GetResult()
            if ([int]$response.StatusCode -ne 200 -or $response.Content.Headers.ContentEncoding.Count -ne 0 -or
                $response.Content.Headers.ContentLength -ne $entry.size.bytes) {
                throw "Unexpected HTTP status/encoding/length for id $($entry.id); review the source metadata."
            }
            if ($runBytes + $entry.size.bytes -gt $maxRunBytes) { throw 'Run byte budget exceeded.' }
            $part = [RedPrism.RawCorpusFilesV1]::CreatePart($partPath)
            $inputStream = $response.Content.ReadAsStreamAsync($token).GetAwaiter().GetResult()
            $fileBytes = 0L
            while ($true) {
                $token.ThrowIfCancellationRequested()
                $count = $inputStream.ReadAsync($buffer, 0, $buffer.Length, $token).GetAwaiter().GetResult()
                if ($count -eq 0) { break }
                $fileBytes += $count
                $runBytes += $count
                if ($fileBytes -gt $entry.size.bytes -or $fileBytes -gt $maxFileBytes -or $runBytes -gt $maxRunBytes) {
                    throw "Byte budget exceeded for id $($entry.id)."
                }
                $null = $part.WriteAsync($buffer, 0, $count, $token).GetAwaiter().GetResult()
            }
            if ($fileBytes -ne $entry.size.bytes) { throw "Truncated response for id $($entry.id)." }
            $null = $part.FlushAsync($token).GetAwaiter().GetResult()
            if ((Get-CorpusHash $part $token) -cne $entry.suppliedSha256) {
                throw "SHA256 mismatch for id $($entry.id); no corpus target was created."
            }
            $part.Flush($true)
            $token.ThrowIfCancellationRequested()
            # An existing/racing target (including a reparse point) makes this
            # rename fail; Windows is explicitly instructed never to replace it.
            [RedPrism.RawCorpusFilesV1]::CommitNew($part, $target)
            $committed = $true
            Write-Host "Verified and saved: $target ($fileBytes bytes)"
        } finally {
            try {
                if ($part -and -not $committed) { [RedPrism.RawCorpusFilesV1]::DiscardPart($part) }
            } finally {
                if ($part) { $part.Dispose() }
                if ($inputStream) { $inputStream.Dispose() }
                if ($response) { $response.Dispose() }
                if ($request) { $request.Dispose() }
                $fileCancellation.Dispose()
            }
        }
    }
} finally {
    if ($client) { $client.Dispose() }
    foreach ($stream in $existing) { $stream.Dispose() }
    if ($corpusHandle) { $corpusHandle.Dispose() }
    if ($owned) { $owned.Marker.Dispose(); $owned.Directory.Dispose() }
    $runCancellation.Dispose()
}
