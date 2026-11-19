<#
.SYNOPSIS
Proves that Build/VcpkgToolCheckout.psm1 leaves the pinned DxUi installer a complete vcpkg checkout, and that
restore-dxui.ps1 prepares it before that installer runs.

vcpkg extracts each versioned port with git read-tree and checkout-index in an environment that holds only
GIT_INDEX_FILE, where git can reach no host, so every object a port needs must already be local. The pinned DxUi
installer makes a --filter=blob:none clone unless <output>\vcpkg-tool already is a clean checkout at its pin.

Offline: the "remote" is a local file:// repository that permits partial clones. Fixtures live under
.build/BuildProcessTests and are removed afterwards.
#>
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Import-Module (Join-Path $repo 'Build/VcpkgToolCheckout.psm1') -Force
$fixtureParent = [IO.Path]::GetFullPath((Join-Path $repo '.build/BuildProcessTests')).TrimEnd('\') + '\'
$fixture = Join-Path $fixtureParent ('VcpkgToolCheckout-' + [guid]::NewGuid().ToString('N'))
[void](New-Item -ItemType Directory -Path $fixture -Force)

function Assert-True([bool] $Condition, [string] $Name) {
    if (-not $Condition) { throw "FAIL $Name" }
    Write-Host "PASS $Name"
}

function Invoke-FixtureGit {
    # Fixture setup only: its progress is not part of the result.
    & git.exe @args 2>&1 | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "Fixture git $($args -join ' ') failed with exit code $LASTEXITCODE." }
}

function Get-CheckoutState([string] $ToolRoot) {
    [pscustomobject]@{
        Origin = [string](& git.exe -C $ToolRoot remote get-url origin)
        Head = [string](& git.exe -C $ToolRoot rev-parse HEAD)
        Dirty = @(& git.exe -C $ToolRoot status --porcelain).Count
        Filter = [string](& git.exe -C $ToolRoot config --get remote.origin.partialclonefilter)
        Shallow = [string](& git.exe -C $ToolRoot rev-parse --is-shallow-repository)
        Missing = @(& git.exe -C $ToolRoot rev-list --objects --missing=print --all | Where-Object { $_.StartsWith('?') }).Count
    }
}

function Assert-Complete([string] $ToolRoot, [string] $Name) {
    $state = Get-CheckoutState $ToolRoot
    Assert-True ($state.Origin -ceq $script:url -and $state.Head -ceq $script:pinned -and $state.Dirty -eq 0 -and
        -not $state.Filter -and $state.Shallow -ceq 'false' -and $state.Missing -eq 0) "$Name (origin, pinned HEAD, clean, no filter, not shallow, nothing missing)"
}

try {
    # A remote whose newest commit no longer holds an older tree's blob, as microsoft/vcpkg master after de36db0455.
    $remote = Join-Path $fixture 'remote'
    Invoke-FixtureGit init -q -b master $remote
    Invoke-FixtureGit -C $remote config uploadpack.allowFilter true
    Invoke-FixtureGit -C $remote config uploadpack.allowAnySHA1InWant true
    foreach ($index in 1..5) {
        [IO.File]::WriteAllText((Join-Path $remote "port$index.txt"), "port $index")
        [IO.File]::WriteAllText((Join-Path $remote 'shared.txt'), "shared $index")
        Invoke-FixtureGit -C $remote add -A
        Invoke-FixtureGit -C $remote -c user.name=fixture -c user.email=fixture@invalid commit -q -m "commit $index"
    }
    $script:pinned = [string](& git.exe -C $remote rev-parse 'HEAD~2')
    $olderTree = [string](& git.exe -C $remote rev-parse 'HEAD~3^{tree}')
    $script:url = 'file:///' + $remote.Replace('\', '/')
    $identity = Join-Path $fixture 'vcpkg-tool.json'
    [IO.File]::WriteAllText($identity, (@{ repository = $script:url; commit = $script:pinned } | ConvertTo-Json))

    $fresh = Join-Path $fixture 'fresh\vcpkg-tool'
    Initialize-VcpkgToolCheckout -ToolIdentityPath $identity -ToolRoot $fresh
    Assert-Complete $fresh 'vcpkg-tool-checkout.fresh-clone'
    Initialize-VcpkgToolCheckout -ToolIdentityPath $identity -ToolRoot $fresh
    Assert-Complete $fresh 'vcpkg-tool-checkout.rerun-keeps-it'

    # What a pinned DxUi installer leaves: partial, shallow at the pin, detached.
    $partial = Join-Path $fixture 'partial\vcpkg-tool'
    Invoke-FixtureGit clone -q -c core.longpaths=true --filter=blob:none $script:url $partial
    Invoke-FixtureGit -C $partial fetch -q --depth 1 origin $script:pinned
    Invoke-FixtureGit -C $partial checkout -q --detach $script:pinned
    $before = Get-CheckoutState $partial
    Assert-True ($before.Filter -ceq 'blob:none' -and $before.Shallow -ceq 'true' -and $before.Missing -gt 0) 'vcpkg-tool-checkout.fixture-reproduces-the-partial-shallow-checkout'
    Initialize-VcpkgToolCheckout -ToolIdentityPath $identity -ToolRoot $partial
    Assert-Complete $partial 'vcpkg-tool-checkout.completes-a-partial-shallow-clone-in-place'

    # vcpkg's extraction of an older port tree, offline: an index file and nothing else that could reach a remote.
    $extract = Join-Path $fixture 'extract'
    [void](New-Item -ItemType Directory -Path $extract -Force)
    $previousIndex = $env:GIT_INDEX_FILE
    $previousNoLazy = $env:GIT_NO_LAZY_FETCH
    try {
        $env:GIT_INDEX_FILE = Join-Path $fixture 'extract.index'
        $env:GIT_NO_LAZY_FETCH = '1'
        & git.exe --git-dir (Join-Path $partial '.git') read-tree $olderTree 2>&1 | Out-Null
        $readTree = $LASTEXITCODE
        & git.exe --git-dir (Join-Path $partial '.git') --work-tree $extract checkout-index -af "--prefix=$extract/" 2>&1 | Out-Null
        $checkoutIndex = $LASTEXITCODE
    } finally {
        $env:GIT_INDEX_FILE = $previousIndex
        $env:GIT_NO_LAZY_FETCH = $previousNoLazy
    }
    Assert-True ($readTree -eq 0 -and $checkoutIndex -eq 0 -and @(Get-ChildItem -LiteralPath $extract -File).Count -eq 3) 'vcpkg-tool-checkout.older-tree-extracts-without-the-network'

    $foreign = Join-Path $fixture 'foreign\vcpkg-tool'
    Invoke-FixtureGit clone -q $script:url $foreign
    Invoke-FixtureGit -C $foreign remote set-url origin 'https://example.invalid/other-vcpkg'
    $refusal = $null
    try { Initialize-VcpkgToolCheckout -ToolIdentityPath $identity -ToolRoot $foreign } catch { $refusal = $_.Exception.Message }
    Assert-True ($null -ne $refusal -and $refusal.Contains('does not use the pinned repository') -and
        [string](& git.exe -C $foreign remote get-url origin) -ceq 'https://example.invalid/other-vcpkg') 'vcpkg-tool-checkout.refuses-another-repository-and-changes-nothing'

    $notGit = Join-Path $fixture 'not-git\vcpkg-tool'
    [void](New-Item -ItemType Directory -Path $notGit -Force)
    [IO.File]::WriteAllText((Join-Path $notGit 'keep.txt'), 'not a checkout')
    $refusal = $null
    try { Initialize-VcpkgToolCheckout -ToolIdentityPath $identity -ToolRoot $notGit } catch { $refusal = $_.Exception.Message }
    Assert-True ($null -ne $refusal -and $refusal.Contains('is not a Git checkout') -and (Test-Path -LiteralPath (Join-Path $notGit 'keep.txt'))) 'vcpkg-tool-checkout.refuses-a-directory-that-is-not-a-checkout'

    # restore-dxui.ps1 prepares the checkout of the pinned DxUi's own vcpkg-tool.json, under the same output root,
    # before it runs that pin's installer.
    $restore = [IO.File]::ReadAllText((Join-Path $repo 'restore-dxui.ps1'))
    $prepare = $restore.IndexOf("Initialize-VcpkgToolCheckout -ToolIdentityPath (Join-Path `$source 'vcpkg-tool.json') -ToolRoot (Join-Path `$output 'vcpkg-tool')", [StringComparison]::Ordinal)
    $install = $restore.IndexOf("& (Join-Path `$source 'vcpkg-install.ps1') -Platform `$Platform -OutputRoot `$output", [StringComparison]::Ordinal)
    $output = $restore.IndexOf('$output = Initialize-RedPrismDxUiOutputRoot', [StringComparison]::Ordinal)
    Assert-True ($output -ge 0 -and $prepare -gt $output -and $install -gt $prepare) 'vcpkg-tool-checkout.restore-prepares-it-before-the-pinned-installer'
} finally {
    $resolved = [IO.Path]::GetFullPath($fixture)
    if (-not $resolved.StartsWith($fixtureParent, [StringComparison]::OrdinalIgnoreCase) -or (Split-Path -Leaf $resolved) -notlike 'VcpkgToolCheckout-*') {
        throw "Unsafe fixture cleanup path: $resolved"
    }
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
exit 0
