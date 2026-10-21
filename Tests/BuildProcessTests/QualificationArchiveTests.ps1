<# .SYNOPSIS
Exercises archive storage through real filesystem objects. Fixtures and failures are retained; no application runs.
#>
[CmdletBinding()]
param([string] $FixtureRoot = (Join-Path ([IO.Path]::GetTempPath()) ('RedPrism-archive-tests-' + [guid]::NewGuid().ToString('N'))))
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
Import-Module (Join-Path $repo 'Build\QualificationArchive.psm1') -Force
$root = [IO.Path]::GetFullPath($FixtureRoot)
if (Test-Path -LiteralPath $root) { throw 'Fixture root must be new.' }
if ([IO.Path]::GetDirectoryName($root).Length -le 3) { throw 'Drive-root fixture directories are forbidden.' }
$null = [IO.Directory]::CreateDirectory($root)
$inputRoot = Join-Path $root 'inputs'
$archiveRoot = Join-Path $root 'archives'
$null = [IO.Directory]::CreateDirectory($inputRoot)
$null = [IO.Directory]::CreateDirectory($archiveRoot)
$passes = [Collections.Generic.List[string]]::new()
function Check([string] $Name, [scriptblock] $Body) { & $Body; $passes.Add($Name); Write-Host "PASS archive.$Name" }
function Need([bool] $Condition, [string] $Message) { if (-not $Condition) { throw $Message } }
function Refuses([scriptblock] $Body) { $refused = $false; try { & $Body | Out-Null } catch { $refused = $true }; Need $refused 'Expected a refusal.' }
function Hash([string] $Path) { (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash }
function Json([string] $Path, $Value) { [IO.File]::WriteAllText($Path, (ConvertTo-Json -InputObject $Value -Depth 15)) }
function Row([string] $Path, [string] $Relative, [string] $Role = 'Sources') {
    [pscustomobject]@{ Path = $Path; RelativePath = $Relative; Role = $Role; Length = (Get-Item -LiteralPath $Path).Length; Hash = (Hash $Path) }
}
function NewInput([string] $Name, [string] $Text) {
    $path = Join-Path $inputRoot $Name
    [IO.File]::WriteAllText($path, $Text)
    [IO.File]::SetLastWriteTimeUtc($path, [datetime]'2026-10-01T10:00:00Z')
    $path
}
$source = NewInput 'source.cpp' ('payload-' * 150000)
$binary = NewInput 'test.exe' 'binary-payload'
$log = NewInput 'stdout.log' 'same log bytes retained independently'
$record = NewInput 'result.json' '{"ExitCode":7,"Scope":"fixture-failure","RunId":"one"}'
$manifest = Join-Path $inputRoot 'inputs.json'
Json $manifest @((Row $source 'src\source.cpp'), (Row $binary 'test.exe' 'Evidence'), (Row $log 'stdout.log' 'Evidence'))
$invoke = @{ ArchiveRoot = $archiveRoot; InputManifest = $manifest; RunRecord = $record }
$sourceHash = Hash $source
try {
    $first = & (Join-Path $repo 'archive-qualification.ps1') @invoke -ArchiveName run-one
    Json $record @{ ExitCode = 7; Scope = 'fixture-failure'; RunId = 'two' }
    $second = & (Join-Path $repo 'archive-qualification.ps1') @invoke -ArchiveName run-two
    $one = @(Get-Content (Join-Path $first.Archive 'source-manifest.json') -Raw | ConvertFrom-Json)
    $two = @(Get-Content (Join-Path $second.Archive 'source-manifest.json') -Raw | ConvertFrom-Json)
    Check 'two-runs-share-without-payload-copy' {
        Need ($first.SharedCreated -eq 2 -and $second.SharedReused -eq 2) 'Expected source and binary reuse.'
        Need ($one[0].ArchivedId -ceq $two[0].ArchivedId -and $one[0].ArchivedId -cne $one[0].OriginalId) 'Input must remain independent of archive objects.'
        Need ((Hash $two[0].ArchivedPath) -ceq $sourceHash) 'Reused bytes differ.'
    }
    Check 'independent-run-evidence-and-failure-status' {
        $a = @(Get-Content (Join-Path $first.Archive 'evidence-manifest.json') -Raw | ConvertFrom-Json) | Where-Object Path -EQ $log
        $b = @(Get-Content (Join-Path $second.Archive 'evidence-manifest.json') -Raw | ConvertFrom-Json) | Where-Object Path -EQ $log
        Need ($a.Storage -eq 'Independent' -and $a.ArchivedId -cne $b.ArchivedId) 'Identical logs must remain independent.'
        $completion = Get-Content (Join-Path $second.Archive 'archive-completed.json') -Raw | ConvertFrom-Json
        Need ($completion.Attempt.ExitCode -eq 7) 'Storage must not turn a failed qualification into success.'
        $priorCompletion = Get-Content (Join-Path $first.Archive 'archive-completed.json') -Raw | ConvertFrom-Json
        Need ($priorCompletion.Attempt.RunId -ceq 'one' -and $completion.Attempt.RunId -ceq 'two') 'Distinct run records lost.'
        Need ((Hash (Join-Path $second.Archive 'Run\result.json')) -ceq (Hash $record)) 'Run record changed.'
        Need ($completion.SourceManifestHash -ceq (Hash (Join-Path $second.Archive 'source-manifest.json'))) 'Completion binding differs.'
    }
    Check 'read-only-and-independent-copy-out' {
        Refuses { [IO.File]::WriteAllText($two[0].ArchivedPath, 'damage') }
        $editable = Join-Path $inputRoot 'editable.cpp'
        [IO.File]::Copy($two[0].ArchivedPath, $editable, $false)
        [IO.File]::SetAttributes($editable, [IO.FileAttributes]::Normal)
        [IO.File]::WriteAllText($editable, 'edited independent copy')
        Need ((Hash $one[0].ArchivedPath) -ceq $sourceHash -and (Hash $source) -ceq $sourceHash) 'Copy-out changed shared/source bytes.'
    }
    Check 'archive-reuse-refused-without-changing-old-record' {
        $before = Hash (Join-Path $first.Archive 'archive-completed.json')
        Refuses { & (Join-Path $repo 'archive-qualification.ps1') @invoke -ArchiveName run-one }
        Need ((Hash (Join-Path $first.Archive 'archive-completed.json')) -ceq $before) 'Old completion changed.'
        Need (-not (Test-Path (Join-Path $first.Archive 'archive-failed.json'))) 'Reuse refusal modified old evidence.'
    }
    Check 'stale-input-and-partial-attempt-preserved' {
        $bad = @((Row $source 'ok.cpp'), (Row $binary 'bad.exe' 'Evidence')); $bad[1].Hash = '0' * 64
        $badManifest = Join-Path $inputRoot 'stale.json'; Json $badManifest $bad
        Refuses { & (Join-Path $repo 'archive-qualification.ps1') -ArchiveRoot $archiveRoot -InputManifest $badManifest -RunRecord $record -ArchiveName stale }
        Need ((Test-Path (Join-Path $archiveRoot 'stale\Sources\ok.cpp')) -and (Test-Path (Join-Path $archiveRoot 'stale\archive-failed.json'))) 'Partial evidence lost.'
        Need (-not (Test-Path (Join-Path $archiveRoot 'stale\archive-completed.json'))) 'Failed archive published completion.'
        Need ((Hash $source) -ceq $sourceHash) 'Original changed.'
    }
    Check 'unsafe-paths-and-collisions' {
        foreach ($relative in '..\escape', 'C:\escape', 'file:stream', 'CON.txt', 'trailing.\file', 'a\\b') {
            $badPath = Join-Path $inputRoot 'unsafe.json'; Json $badPath @((Row $source $relative))
            Refuses { & (Join-Path $repo 'archive-qualification.ps1') -ArchiveRoot $archiveRoot -InputManifest $badPath -RunRecord $record -ArchiveName unsafe }
        }
        Json $badPath @((Row $source 'same.cpp'), (Row $source 'SAME.cpp'))
        Refuses { & (Join-Path $repo 'archive-qualification.ps1') -ArchiveRoot $archiveRoot -InputManifest $badPath -RunRecord $record -ArchiveName collision }
        Need (-not (Test-Path (Join-Path $archiveRoot 'unsafe'))) 'Unsafe manifest created an archive.'
    }
    $store = [RedPrism.Archives.Store]::new($archiveRoot)
    try {
        Check 'exclusive-writer-and-outside-destination' {
            Refuses { $other = [RedPrism.Archives.Store]::new($archiveRoot); $other.Dispose() }
            Refuses { $store.Archive($source, (Join-Path $root 'outside.cpp'), $sourceHash, (Get-Item $source).Length, $true) }
        }
        Check 'different-content-length-and-time' {
            $changed = NewInput 'changed.cpp' ('different' * 120000)
            $a = $store.Archive($changed, (Join-Path $archiveRoot 'changed\x.cpp'), (Hash $changed), (Get-Item $changed).Length, $true)
            Need ($a.Storage -eq 'NewObject' -and $a.ArchivedId -cne $one[0].ArchivedId) 'Different content shared.'
            $sameSize = NewInput 'same-size.cpp' ('changed-' * 150000)
            $same = $store.Archive($sameSize, (Join-Path $archiveRoot 'same-size\x.cpp'), (Hash $sameSize), (Get-Item $sameSize).Length, $true)
            Need ($same.Length -eq $one[0].Length -and $same.ArchivedId -cne $one[0].ArchivedId) 'Same-size different bytes shared.'
            Refuses { $store.Archive($changed, (Join-Path $archiveRoot 'bad-length\x'), (Hash $changed), 1, $true) }
            [IO.File]::SetLastWriteTimeUtc($source, [datetime]'2026-10-02T10:00:00Z')
            $b = $store.Archive($source, (Join-Path $archiveRoot 'mtime\x.cpp'), $sourceHash, (Get-Item $source).Length, $true)
            Need ($b.Storage -eq 'NewObject' -and $b.ArchivedId -cne $one[0].ArchivedId) 'Different last-write time shared.'
            [IO.File]::SetLastWriteTimeUtc($source, [datetime]'2026-10-01T10:00:00Z')
        }
        Check 'alternate-stream-and-locked-input' {
            $ads = NewInput 'streams.cpp' 'stream-main'; [IO.File]::WriteAllText($ads + ':private', 'unique stream')
            Refuses { $store.Archive($ads, (Join-Path $archiveRoot 'ads\x'), (Hash $ads), (Get-Item $ads).Length, $true) }
            Need ([IO.File]::ReadAllText($ads + ':private') -ceq 'unique stream') 'Input stream lost.'
            $locked = [IO.FileStream]::new($source, [IO.FileMode]::Open, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
            try { Refuses { $store.Archive($source, (Join-Path $archiveRoot 'locked\x'), $sourceHash, (Get-Item $source).Length, $true) } } finally { $locked.Dispose() }
        }
        Check 'reparse-parent-refused' {
            $junction = Join-Path $inputRoot 'junction'
            New-Item -ItemType Junction -Path $junction -Target $inputRoot | Out-Null
            Refuses { $store.Archive((Join-Path $junction 'source.cpp'), (Join-Path $archiveRoot 'junction\x'), $sourceHash, (Get-Item $source).Length, $true) }
        }
        Check 'unicode-long-path' {
            $relative = ('long-漢字-' * 12) + '\' + ('unicode-測試-' * 12) + '\x.cpp'
            $entry = $store.Archive($source, (Join-Path $archiveRoot $relative), $sourceHash, (Get-Item $source).Length, $true)
            Need ($entry.ArchivedPath.Length -gt 260 -and (Hash $entry.ArchivedPath) -ceq $sourceHash) 'Long-path archive differs.'
        }
        Check 'different-dacl-stays-distinct' {
            $secured = NewInput 'secured.cpp' ([IO.File]::ReadAllText($source))
            $acl = Get-Acl -LiteralPath $secured
            $identity = [Security.Principal.WindowsIdentity]::GetCurrent().User
            $acl.AddAccessRule([Security.AccessControl.FileSystemAccessRule]::new($identity, 'Read', 'Allow'))
            Set-Acl -LiteralPath $secured -AclObject $acl
            $entry = $store.Archive($secured, (Join-Path $archiveRoot 'security\x.cpp'), $sourceHash, (Get-Item $secured).Length, $true)
            Need ($entry.Storage -eq 'NewObject' -and $entry.ArchivedId -cne $one[0].ArchivedId) 'Different DACL shared.'
            Need ((Get-Acl -LiteralPath $entry.ArchivedPath).Sddl -ceq (Get-Acl -LiteralPath $secured).Sddl) 'DACL not preserved.'
        }
        Check 'protected-read-only-dacl' {
            $restricted = NewInput 'restricted.cpp' 'restricted readable payload'
            $acl = Get-Acl -LiteralPath $restricted
            $acl.SetAccessRuleProtection($true, $false)
            $acl.AddAccessRule([Security.AccessControl.FileSystemAccessRule]::new([Security.Principal.WindowsIdentity]::GetCurrent().User, 'Read', 'Allow'))
            Set-Acl -LiteralPath $restricted -AclObject $acl
            $entry = $store.Archive($restricted, (Join-Path $archiveRoot 'restricted\x.cpp'), (Hash $restricted), (Get-Item $restricted).Length, $true)
            Need ((Get-Item -LiteralPath $entry.ArchivedPath -Force).IsReadOnly) 'Restricted copy is mutable.'
            Need ((Get-Acl -LiteralPath $entry.ArchivedPath).Sddl -ceq (Get-Acl -LiteralPath $restricted).Sddl) 'Protected DACL changed.'
            Need ($entry.Storage -eq 'Independent') 'Restrictive DACL should use a verified independent copy.'
        }
        Check 'link-limit-starts-new-object' {
            # Exercise the actual production cap, without requiring elevated fsutil access.
            $existing = [RedPrism.Archives.Store]::LinkCount($one[0].ObjectPath)
            for ($index = $existing; $index -lt 900; $index++) {
                New-Item -ItemType HardLink -Path (Join-Path $archiveRoot ('cap-' + $index)) -Target $one[0].ArchivedPath | Out-Null
            }
            $entry = $store.Archive($source, (Join-Path $archiveRoot 'link-cap\x.cpp'), $sourceHash, (Get-Item $source).Length, $true)
            Need ($entry.Storage -eq 'NewObject' -and $entry.ArchivedId -cne $one[0].ArchivedId) 'Link cap did not create independent shard.'
        }
        Check 'readonly-input-and-preexisting-hardlinks' {
            $readonly = NewInput 'readonly.cpp' 'original readonly payload'
            [IO.File]::SetAttributes($readonly, [IO.FileAttributes]::ReadOnly)
            $entry = $store.Archive($readonly, (Join-Path $archiveRoot 'readonly\x.cpp'), (Hash $readonly), (Get-Item $readonly).Length, $true)
            Need ((Get-Item -LiteralPath $readonly -Force).IsReadOnly -and $entry.Storage -eq 'NewObject') 'Readonly input changed.'
            $repeat = $store.Archive($readonly, (Join-Path $archiveRoot 'readonly-two\x.cpp'), (Hash $readonly), (Get-Item $readonly).Length, $true)
            Need ($repeat.Storage -eq 'Reused' -and [IO.File]::GetAttributes($entry.ArchivedPath) -eq [IO.File]::GetAttributes($readonly)) 'Link creation changed the original backup attributes.'
            New-Item -ItemType HardLink -Path (Join-Path $inputRoot 'prelinked.cpp') -Target $source | Out-Null
            $entry = $store.Archive($source, (Join-Path $archiveRoot 'prelinked\x.cpp'), $sourceHash, (Get-Item $source).Length, $true)
            Need ($entry.Storage -eq 'Independent' -and $entry.ArchivedId -cne $entry.OriginalId) 'Prelinked input must stay independent.'
        }
    } finally { $store.Dispose() }
    Check 'mutable-shared-object-refused' {
        $binaryEntry = @(Get-Content (Join-Path $first.Archive 'evidence-manifest.json') -Raw | ConvertFrom-Json) | Where-Object Path -EQ $binary
        [IO.File]::SetAttributes($binaryEntry.ObjectPath, [IO.FileAttributes]::Normal)
        try {
            Refuses { & (Join-Path $repo 'archive-qualification.ps1') @invoke -ArchiveName mutable-object }
            Need (-not (Test-Path (Join-Path $archiveRoot 'mutable-object\archive-completed.json'))) 'Writable object accepted.'
        } finally { [IO.File]::SetAttributes($binaryEntry.ObjectPath, [IO.FileAttributes]::ReadOnly) }
    }
    Check 'corrupt-shared-object-fails-closed' {
        $binaryEntry = @(Get-Content (Join-Path $first.Archive 'evidence-manifest.json') -Raw | ConvertFrom-Json) | Where-Object Path -EQ $binary
        $blob = $binaryEntry.ObjectPath
        [IO.File]::SetAttributes($blob, [IO.FileAttributes]::Normal)
        [IO.File]::WriteAllText($blob, 'intentional isolated fixture corruption')
        [IO.File]::SetAttributes($blob, [IO.FileAttributes]::ReadOnly)
        Refuses { & (Join-Path $repo 'archive-qualification.ps1') @invoke -ArchiveName corrupt }
        Need (-not (Test-Path (Join-Path $archiveRoot 'corrupt\archive-completed.json'))) 'Corruption accepted.'
        Need ((Hash $source) -ceq $sourceHash) 'Original lost after store corruption.'
    }
    Json (Join-Path $root 'tests-passed.json') @{ Passed = $passes.Count; Cases = $passes.ToArray(); FixtureRoot = $root; ScriptHash = (Hash $PSCommandPath) }
    Write-Host "$($passes.Count) archive scenarios passed; retained fixtures: $root"
} catch {
    Json (Join-Path $root 'tests-failed.json') @{ Error = $_.Exception.Message; Passed = $passes.ToArray(); FixtureRoot = $root }
    throw
}
