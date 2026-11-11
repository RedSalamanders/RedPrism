# Real owned children exercise failure observation; events release every waiting fixture without termination.
[CmdletBinding()]param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$module = Import-Module (Join-Path $root 'Build/TestProcess.psm1') -Force -PassThru
$scratch = Join-Path $root ('.build/test-process-controls/' + [guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory($scratch)
$marker = 'Clipboard host work did not acknowledge within the bounded wait.'
$script:checks = 0
function Assert-Process([bool] $Condition, [string] $Message) {
    if (-not $Condition) { throw $Message }
    $script:checks++
}
function Invoke-OwnedChild {
    param([string] $Name, [string] $Text, [scriptblock] $Check, [int] $ExitCode = 0, [switch] $ExitImmediately)
    $path = Join-Path $scratch ($Name + '.err.log')
    $readyName = 'Local\RedPrismTestReady-' + [guid]::NewGuid().ToString('N')
    $releaseName = 'Local\RedPrismTestRelease-' + [guid]::NewGuid().ToString('N')
    $ready = [Threading.EventWaitHandle]::new($false, [Threading.EventResetMode]::ManualReset, $readyName)
    $release = [Threading.EventWaitHandle]::new($false, [Threading.EventResetMode]::ManualReset, $releaseName)
    $child = $null
    try {
        $body = @'
$ErrorActionPreference = 'Stop'
$ready = [Threading.EventWaitHandle]::OpenExisting('__READY__')
$release = [Threading.EventWaitHandle]::OpenExisting('__RELEASE__')
try {
    [IO.File]::WriteAllText('__PATH__', [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String('__TEXT__')))
    [void]$ready.Set()
    if (__WAIT__) { if (-not $release.WaitOne(30000)) { exit 71 } }
} finally { $ready.Dispose(); $release.Dispose() }
exit __EXIT__
'@
        $body = $body.Replace('__READY__', $readyName).Replace('__RELEASE__', $releaseName).
            Replace('__PATH__', $path.Replace("'", "''")).
            Replace('__TEXT__', [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($Text))).
            Replace('__WAIT__', $(if ($ExitImmediately) { '$false' } else { '$true' })).Replace('__EXIT__', [string]$ExitCode)
        $start = [Diagnostics.ProcessStartInfo]::new((Get-Process -Id $PID).Path)
        $start.UseShellExecute = $false
        $start.CreateNoWindow = $true
        foreach ($argument in @('-NoProfile', '-EncodedCommand', [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($body)))) {
            $start.ArgumentList.Add($argument)
        }
        $child = [Diagnostics.Process]::Start($start)
        if (-not $ready.WaitOne(10000)) { throw "Owned fixture '$Name' did not become ready." }
        & $Check $child $path $release
    } finally {
        [void]$release.Set()
        if ($null -ne $child) {
            if (-not $child.WaitForExit(10000)) { throw "Owned fixture '$Name' did not drain; PID $($child.Id) was not terminated." }
            $child.Dispose()
        }
        $ready.Dispose(); $release.Dispose()
    }
}

foreach ($code in @(0, 19)) {
    Invoke-OwnedChild ('exit-' + $code) 'ordinary output' {
        param($child, $path, $release)
        $result = Wait-RedPrismTestProcess $child $path $marker 10000
        Assert-Process ($result.Status -ceq 'Exited' -and $result.ExitCode -eq $code) 'Actual exit code changed.'
    } -ExitImmediately -ExitCode $code
}
Invoke-OwnedChild 'terminal-live' ($marker + "`r`n") {
    param($child, $path, $release)
    $result = Wait-RedPrismTestProcess $child $path $marker 10000 -FailureExitGraceMilliseconds 50
    Assert-Process ($result.Status -ceq 'TerminalFailure' -and $result.TerminalFailureObserved -and $null -eq $result.ExitCode) 'Terminal failure was not distinguished from exit/timeout.'
    Assert-Process (-not $child.HasExited -and $result.ProcessId -eq $child.Id) 'Observer terminated or confused the live child.'
}
foreach ($text in @(('prefix ' + $marker + "`n"), $marker, ($marker.ToLowerInvariant() + "`n"), ($marker + "`r`r`n"))) {
    Invoke-OwnedChild ('similar-' + [guid]::NewGuid().ToString('N')) $text {
        param($child, $path, $release)
        $result = Wait-RedPrismTestProcess $child $path $marker 80 -FailureExitGraceMilliseconds 1
        Assert-Process ($result.Status -ceq 'TimedOut' -and -not $result.TerminalFailureObserved -and -not $child.HasExited) 'A similar, partial or differently cased line was accepted.'
    }
}
Invoke-OwnedChild 'grace-natural-exit' ($marker + "`n") {
    param($child, $path, $release)
    [void]$release.Set()
    $result = Wait-RedPrismTestProcess $child $path $marker 10000
    Assert-Process ($result.Status -ceq 'Exited' -and $result.ExitCode -eq 23) 'A real exit during grace lost its nonzero result.'
} -ExitCode 23
Invoke-OwnedChild 'terminal-zero-exit' ($marker + "`n") {
    param($child, $path, $release)
    [void]$release.Set()
    if (-not $child.WaitForExit(10000)) { throw 'Owned zero-exit fixture did not exit.' }
    $result = Wait-RedPrismTestProcess $child $path $marker 10000
    Assert-Process ($result.Status -ceq 'Exited' -and $result.ExitCode -eq 0 -and $result.TerminalFailureObserved) 'A fast zero exit hid the complete terminal failure.'
}
Invoke-OwnedChild 'missing-log' '' {
    param($child, $path, $release)
    $result = Wait-RedPrismTestProcess $child ($path + '.absent') $marker 80
    Assert-Process ($result.Status -ceq 'TimedOut' -and $result.LastReadError -and -not $child.HasExited) 'An unavailable log became a pass or terminated the child.'
}
Invoke-OwnedChild 'locked-log' ($marker + "`n") {
    param($child, $path, $release)
    $lock = [IO.File]::Open($path, [IO.FileMode]::Open, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
    try {
        $result = Wait-RedPrismTestProcess $child $path $marker 80
        Assert-Process ($result.Status -ceq 'TimedOut' -and $result.LastReadError -and -not $child.HasExited) 'A locked log became a terminal result or success.'
    } finally { $lock.Dispose() }
}
# Tail boundaries cannot turn a truncated prefix into an exact record. A complete line inside a large tail works.
$tail = Join-Path $scratch 'tail.log'
foreach ($case in @(
    @{ text = ('x' * 20000) + "`n" + $marker + "`n"; found = $true },
    @{ text = 'prefix' + $marker + "`n" + ('x' * (16384 - $marker.Length - 1)); found = $false }
)) {
    [IO.File]::WriteAllText($tail, $case.text)
    $read = & $module { param($path, $line) Read-RedPrismTerminalFailureTail $path $line } $tail $marker
    Assert-Process ($read.Found -eq $case.found) 'Bounded tail line-boundary handling changed.'
}
# Execute the actual entrypoint's observation/decision block. No successful suite/receipt can follow failure.
$runner = [IO.File]::ReadAllText((Join-Path $root 'test.ps1'))
$begin = $runner.IndexOf("Import-Module (Join-Path `$repoRoot 'Build/TestProcess.psm1') -Force", [StringComparison]::Ordinal)
$end = $runner.IndexOf('Write-Host "PASS hidden WARP self-test:', $begin, [StringComparison]::Ordinal)
if ($begin -lt 0 -or $end -le $begin) { throw 'The actual observation/decision block was not found.' }
$decision = [scriptblock]::Create($runner.Substring($begin, $end - $begin))
Invoke-OwnedChild 'actual-entrypoint' ($marker + "`n") {
    param($child, $path, $release)
    $repoRoot = $root; $selfTest = $child; $selfTestErr = $path; $selfTestLog = $path; $SelfTestTimeoutSeconds = 1200
    $rejected = $false; $advanced = $false
    try { . $decision; $advanced = $true } catch {
        $rejected = $_.Exception.Message.Contains('terminal clipboard acknowledgement failure')
    }
    Assert-Process ($rejected -and -not $advanced -and -not $child.HasExited) 'The real entrypoint did not stop on terminal failure while preserving the live child.'
}
$global:LASTEXITCODE = 0
Write-Host "PASS $script:checks process observation controls; real owned children drained, no compiler or product qualification. Evidence: $scratch"
