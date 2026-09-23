[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Import-Module (Join-Path $repo 'Build\BuildPreflight.psm1') -Force

# A build output that does not exist yet: nothing to preflight, whatever the enumerator says.
$missing = Join-Path ([IO.Path]::GetTempPath()) ("RedPrism.BuildPreflightTests-missing-" + [guid]::NewGuid().ToString('N') + '.exe')
Assert-RedPrismBuildOutputProcessNotRunning -ExpectedExecutablePath $missing -ProcessEnumerator { throw 'must not be called' }
Write-Host 'PASS preflight.skips-missing-output'

# A real file to preflight against, so the ExecutablePath-equality branch is exercised.
$existing = Join-Path ([IO.Path]::GetTempPath()) ("RedPrism.BuildPreflightTests-" + [guid]::NewGuid().ToString('N') + '.exe')
New-Item -ItemType File -Path $existing -Force | Out-Null
try {
    # RF-705: an enumeration failure (e.g. a WMI/CIM problem) must fail the preflight, not silently see zero
    # processes and let the build proceed while a matching RedPrism.exe keeps its file locked.
    $threw = $false
    try {
        Assert-RedPrismBuildOutputProcessNotRunning -ExpectedExecutablePath $existing -ProcessEnumerator { throw 'simulated CIM failure' }
    } catch {
        $threw = $true
        if ($_.Exception.Message -notmatch 'No process was terminated' -or $_.Exception.Message -notmatch 'simulated CIM failure') {
            throw "The enumeration-failure message lost its no-processes-killed statement or the underlying error: $($_.Exception.Message)"
        }
    }
    if (-not $threw) {
        throw 'An enumerator that throws did not fail the preflight (RF-705).'
    }
    Write-Host 'PASS preflight.enumeration-failure-fails-build'

    # A process at the exact expected path blocks the build.
    $exactMatch = [pscustomobject]@{ ExecutablePath = $existing; ProcessId = 4242 }
    $threw = $false
    try {
        Assert-RedPrismBuildOutputProcessNotRunning -ExpectedExecutablePath $existing -ProcessEnumerator { $exactMatch }.GetNewClosure()
    } catch {
        $threw = $true
        if ($_.Exception.Message -notmatch '4242') {
            throw "The running-process message lost the process id: $($_.Exception.Message)"
        }
    }
    if (-not $threw) {
        throw 'A RedPrism.exe running from the exact build output did not block the build.'
    }
    Write-Host 'PASS preflight.exact-path-match-blocks-build'

    # A same-named process from a different path (another checkout/platform/configuration) must not block the build.
    $otherPath = Join-Path ([IO.Path]::GetTempPath()) ("RedPrism.BuildPreflightTests-other-" + [guid]::NewGuid().ToString('N') + '.exe')
    $elsewhere = [pscustomobject]@{ ExecutablePath = $otherPath; ProcessId = 4343 }
    Assert-RedPrismBuildOutputProcessNotRunning -ExpectedExecutablePath $existing -ProcessEnumerator { $elsewhere }.GetNewClosure()
    Write-Host 'PASS preflight.different-path-does-not-block-build'

    # A process the enumerator could not read a path for (access denied, etc.) must not block the build by itself.
    $noPath = [pscustomobject]@{ ExecutablePath = $null; ProcessId = 4444 }
    Assert-RedPrismBuildOutputProcessNotRunning -ExpectedExecutablePath $existing -ProcessEnumerator { $noPath }.GetNewClosure()
    Write-Host 'PASS preflight.unreadable-path-does-not-block-build'
} finally {
    Remove-Item -LiteralPath $existing -ErrorAction SilentlyContinue
}

# The real enumerator (Get-RedPrismRedPrismProcesses) must be a live Win32_Process query, not a fake: prove it runs
# without throwing and returns an object shape with the fields the preflight reads.
$live = @(Get-RedPrismRedPrismProcesses)
foreach ($process in $live) {
    if (-not (Get-Member -InputObject $process -Name 'ExecutablePath') -or -not (Get-Member -InputObject $process -Name 'ProcessId')) {
        throw 'Get-RedPrismRedPrismProcesses rows are missing ExecutablePath / ProcessId.'
    }
}
Write-Host 'PASS preflight.real-enumerator-shape'
