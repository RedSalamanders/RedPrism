<#
.SYNOPSIS
Proves the DxUi source restore (Build/DxUiRestore.psm1) from a deep root: the clone is checked out sparse and with Git
long paths, so a root deep enough that a tracked path would pass the 259 characters of a Win32 path still yields a clean
checkout, and what a build never reads is not created.

The default run is hermetic. It builds a fixture repository whose tracked paths include deep ones in Specs, Measurements and
docs/gallery (the directories older DxUi pins kept their longest paths in) and in src, restores it into a root that puts those
paths beyond 259 characters, and checks the result the way Tools/validate_consumer.ps1 does: the exact commit and a clean
`git status`. -RealPin does the same for the locked DxUi commit, from the sibling checkout when it has the commit and else the
public repository (it needs one of the two, so test.ps1 does not run it), then runs the pinned validate_consumer.ps1.
#>
[CmdletBinding()]
param(
    [switch] $RealPin
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Import-Module (Join-Path $repo 'Build/DxUiRestore.psm1') -Force
$fixtureParent = [IO.Path]::GetFullPath((Join-Path $repo '.build/BuildProcessTests')).TrimEnd('\') + '\'
$fixture = Join-Path $fixtureParent ('DxUiRestore-' + [guid]::NewGuid().ToString('N'))
[void](New-Item -ItemType Directory -Path $fixture -Force)

function Assert-True([bool] $Condition, [string] $Message) {
    if (-not $Condition) { throw $Message }
}

function Invoke-Git([string] $Directory, [string[]] $Arguments) {
    # The output lines as an array, however many there are: the comma keeps one line, or none, from unrolling into a string.
    $lines = @(& git -C $Directory @Arguments)
    if ($LASTEXITCODE -ne 0) { throw "git $($Arguments -join ' ') failed in $Directory (exit $LASTEXITCODE)." }
    return , $lines
}

function New-FixtureOrigin([string] $Path, [string[]] $Files) {
    # Plumbing only, in a bare repository: the deep paths exist in the commit and never as files, so nothing here needs long paths.
    [void](New-Item -ItemType Directory -Path $Path -Force)
    [void](Invoke-Git $Path @('init', '--bare', '--quiet', '--initial-branch=main'))
    $previousIndex = $env:GIT_INDEX_FILE
    $env:GIT_INDEX_FILE = Join-Path $Path 'fixture.index'
    try {
        foreach ($file in $Files) {
            $blob = ("fixture $file" | & git -C $Path hash-object -w --stdin)
            if ($LASTEXITCODE -ne 0) { throw "git hash-object failed for $file." }
            [void](Invoke-Git $Path @('update-index', '--add', '--cacheinfo', "100644,$blob,$file"))
        }
        $tree = (Invoke-Git $Path @('write-tree'))[0]
        $commit = (Invoke-Git $Path @('-c', 'user.name=Fixture', '-c', 'user.email=fixture@example.invalid', 'commit-tree', $tree, '-m', 'fixture'))[0]
        [void](Invoke-Git $Path @('update-ref', 'refs/heads/main', $commit))
        return $commit
    } finally {
        $env:GIT_INDEX_FILE = $previousIndex
    }
}

function New-DeepRoot([string] $Base, [int] $Length) {
    # Directories of 42 characters until the root is as long as asked, or longer when $Base already is.
    $root = $Base
    $index = 0
    while ($root.Length + 43 -le $Length) {
        $index++
        $root = Join-Path $root ('deep-root-segment-for-long-path-checks-{0:D3}' -f $index)
    }
    return $root
}

function Remove-FixtureTree([string] $Path) {
    $resolved = [IO.Path]::GetFullPath($Path)
    if (-not $resolved.StartsWith($fixtureParent, [StringComparison]::OrdinalIgnoreCase) -or
        (Split-Path -Leaf $resolved) -notlike 'DxUiRestore-*') {
        throw "Unsafe fixture cleanup path: $resolved"
    }
    # The extended-length form deletes paths past 259 characters in Windows PowerShell 5.1 as well, and -Force the
    # read-only objects Git writes.
    $extended = '\\?\' + $resolved
    Remove-Item -LiteralPath $extended -Recurse -Force
}

function Assert-RestoredCleanly([string] $Destination, [string] $Commit, [string] $Label, [string[]] $MustExist, [string[]] $MustBeAbsent) {
    Assert-True ((Invoke-Git $Destination @('rev-parse', 'HEAD'))[0] -ceq $Commit) "${Label}: HEAD is not the pinned commit."
    $status = (Invoke-Git $Destination @('status', '--porcelain', '--untracked-files=normal'))
    Assert-True ($status.Count -eq 0) "${Label}: the checkout is not clean, as Tools/validate_consumer.ps1 requires: $($status -join '; ')"
    Assert-True ((Invoke-Git $Destination @('config', '--local', '--get', 'core.longpaths'))[0] -ceq 'true') "${Label}: core.longpaths is not stored in the clone, so the later status cannot see deep files."
    $missing = (Invoke-Git $Destination @('ls-files', '--deleted'))
    Assert-True ($missing.Count -eq 0) "${Label}: tracked files are missing from disk: $($missing -join '; ')"
    # The index lists every tracked path; ls-files -t tags those the sparse checkout left out S and the checked-out ones H.
    foreach ($path in $MustExist) {
        $tags = (Invoke-Git $Destination @('ls-files', '-t', '--', $path))
        Assert-True (@($tags | Where-Object { $_.StartsWith('H ') }).Count -ge 1) "${Label}: $path was not checked out."
    }
    foreach ($path in $MustBeAbsent) {
        $tags = (Invoke-Git $Destination @('ls-files', '-t', '--', $path))
        Assert-True ($tags.Count -ge 1 -and @($tags | Where-Object { -not $_.StartsWith('S ') }).Count -eq 0) "${Label}: $path was checked out although the build never reads it."
        Assert-True (-not (Test-Path -LiteralPath (Join-Path $Destination $path))) "${Label}: $path exists on disk."
    }
}

try {
    # The shape of the DxUi tree the restore sees: tracked paths of 150 to 190 characters under the three directories the
    # restore leaves out, and a deep tracked path in src, which is kept.
    $deep = 'Deep-Directory-Name-For-Long-Path-Checks'
    $deepKept = "src/Controls/$deep-A/$deep-B/Deep.cpp"
    $files = @(
        'capabilities.json', 'vcpkg.json', 'vcpkg-install.ps1', 'vcpkg-tool.json', 'Directory.Build.props', 'LICENSE.txt',
        'Tools/validate_consumer.ps1', 'Tools/ConsumerBuild.psm1', 'Build/DxUi.Consumer.props', 'src/DxUi.vcxproj',
        'include/DxUi/DxUi.h', 'docs/README.md', 'docs/getting-started.md', 'Tests/Controls/Fixture.cpp', $deepKept,
        "Specs/Build/$deep-A/$deep-B/$deep-C/Spec.md",
        "Measurements/Run/2026-09-20/$deep-A/$deep-B/Performance-x64-Release-0123456789abcdef.json.comparison.json",
        "docs/gallery/themes/$deep-A/$deep-B/$deep-C/sheet.png"
    )
    $origin = Join-Path $fixture 'origin.git'
    $commit = New-FixtureOrigin $origin $files

    # A restore root of about 215 characters: the directory itself can still be created, and the deep tracked path in src lies
    # past the 259 characters of a Win32 path.
    $root = New-DeepRoot (Join-Path $fixture 'restore') 142
    $destination = Join-Path $root ".build/dependencies/DxUi/source/$commit"
    Assert-True ($destination.Length -lt 245) "The fixture root is too deep to create its directories ($($destination.Length) characters); run the test from a shorter checkout path."
    Assert-True (($destination + '\' + $deepKept.Replace('/', '\')).Length -gt 259) 'The fixture does not put a kept path past 259 characters.'

    $result = Restore-RedPrismDxUiSource -Repository 'https://github.com/RedSalamanders/DxUi' -Commit $commit -Destination $destination -CloneFrom $origin
    Assert-True ($result -eq $destination) 'Restore-RedPrismDxUiSource did not return the destination.'
    Assert-RestoredCleanly $destination $commit 'deep fixture' `
        @($deepKept, 'capabilities.json', 'Tools/validate_consumer.ps1', 'Tools/ConsumerBuild.psm1', 'Build/DxUi.Consumer.props', 'src/DxUi.vcxproj',
            'include/DxUi/DxUi.h', 'vcpkg-install.ps1', 'vcpkg.json', 'vcpkg-tool.json', 'Directory.Build.props', 'LICENSE.txt',
            'docs/README.md', 'docs/getting-started.md', 'Tests/Controls/Fixture.cpp') `
        @('Specs', 'Measurements', 'docs/gallery')
    Write-Host 'PASS dxui-restore.deep-root-clean-sparse-checkout'

    $recordedOrigin = (Invoke-Git $destination @('config', '--get', 'remote.origin.url'))[0]
    Assert-True ($recordedOrigin -ceq 'https://github.com/RedSalamanders/DxUi.git') "The restored clone does not record the canonical origin: $recordedOrigin"
    Write-Host 'PASS dxui-restore.canonical-origin'

    # An existing destination is left as it is; the pin validation decides whether it is the pin and clean.
    $marker = Join-Path $destination 'capabilities.json'
    $before = [IO.File]::GetLastWriteTimeUtc($marker)
    $again = Restore-RedPrismDxUiSource -Repository 'https://github.com/RedSalamanders/DxUi' -Commit $commit -Destination $destination -CloneFrom $origin
    Assert-True ($again -eq $destination -and [IO.File]::GetLastWriteTimeUtc($marker) -eq $before) 'A second restore touched the existing source.'
    Write-Host 'PASS dxui-restore.existing-source-untouched'

    $patterns = Get-RedPrismDxUiSparsePatterns
    Assert-True (($patterns -join '|') -ceq '/*|!/Measurements/|!/docs/gallery/|!/Specs/') "The sparse patterns drifted: $($patterns -join ' ')"
    Write-Host 'PASS dxui-restore.sparse-patterns'

    # A commit the clone does not have fails with the checkout message, not silently, and says what to do with the directory the
    # failed restore left behind (a later restore keeps an existing one as it is).
    $rejected = $false
    try {
        # Git's own "fatal:" line for the missing commit is the expected output of this case, not a failure of the test.
        [void](Restore-RedPrismDxUiSource -Repository 'https://github.com/RedSalamanders/DxUi' -Commit ('0' * 40) -Destination (Join-Path $fixture 'missing/source') -CloneFrom $origin)
    } catch {
        $rejected = $_.Exception.Message -like '*pin could not be checked out*' -and $_.Exception.Message -like "*delete that directory*"
    }
    Assert-True $rejected 'A commit that is not in the clone was not rejected with the checkout message and the instruction to delete the directory left behind.'
    Write-Host 'PASS dxui-restore.unknown-commit-rejected'

    if ($RealPin) {
        $pinPath = Join-Path $repo 'Dependencies/DxUi.lock.json'
        $pin = Get-Content -Raw -LiteralPath $pinPath | ConvertFrom-Json
        $sibling = Join-Path (Split-Path -Parent $repo) 'DxUi'
        $cloneFrom = ''
        if (Test-Path -LiteralPath (Join-Path $sibling '.git')) {
            & git -C $sibling cat-file -e "$($pin.commit)^{commit}" 2>$null
            if ($LASTEXITCODE -eq 0) { $cloneFrom = $sibling }
        }
        $realRoot = New-DeepRoot (Join-Path $fixture 'real') 142
        $realDestination = Join-Path $realRoot ".build/dependencies/DxUi/source/$($pin.commit)"
        Write-Host "Restoring DxUi $($pin.commit) from $(if ($cloneFrom) { $cloneFrom } else { $pin.repository }) into a root of $($realDestination.Length) characters"
        [void](Restore-RedPrismDxUiSource -Repository $pin.repository -Commit $pin.commit -Destination $realDestination -CloneFrom $cloneFrom)
        Assert-RestoredCleanly $realDestination $pin.commit 'real pin' @('capabilities.json', 'Tools/validate_consumer.ps1', 'src/DxUi.vcxproj', 'include/DxUi/DxUi.h') @('Specs', 'Measurements', 'docs/gallery')
        & (Join-Path $realDestination 'Tools/validate_consumer.ps1') -DxUiRoot $realDestination -LockFile $pinPath
        Write-Host 'PASS dxui-restore.real-pin-from-a-deep-root'
    }
} finally {
    # A failed cleanup must not hide the failure that ended the run.
    try {
        Remove-FixtureTree $fixture
    } catch {
        Write-Warning "The restore fixture was not removed: $($_.Exception.Message)"
    }
}
# The rejected restore above ends in a failing git call: its exit code must not become this script's.
exit 0
