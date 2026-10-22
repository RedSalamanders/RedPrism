<#
.SYNOPSIS
Proves that a failed runtime check in a test executable ends the run with its report and exit code 3 instead of waiting
on a dialog (RF-702, Tests/Support/Support.Tests.FailureReports.h). HostTests, DocumentTests, IoTests and CompositionTests each run their
hidden --failure-report-self-test, which in Debug and ASan Debug fails a CRT assertion on purpose, the way --asan-probe
proves the sanitizer is really wired up. Release has no such checks: the switch must end with exit code 0 there.

Each run is bounded. A run that does not end is blocked on a dialog the routing should have prevented; it is this
script's own child, so it is ended, the failure names it, and the dialog does not stay on the desktop.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $OutputRoot,
    [ValidateSet('Debug', 'Release', 'ASan Debug')]
    [string] $Configuration = 'Debug',
    [ValidateRange(5, 600)]
    [int] $TimeoutSeconds = 60,
    [ValidateSet('HostTests','DocumentTests','IoTests','CompositionTests')][AllowEmptyCollection()][string[]] $Suites=@('HostTests','DocumentTests','IoTests','CompositionTests'),
    $EvidenceRun = $null
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$expectedReport = 'the failure-report self-test fails this check on purpose'
Import-Module (Join-Path $PSScriptRoot '../../Build/TestRunEvidence.psm1') -Force
$ownedEvidence = $null
if (-not $EvidenceRun) {
    $ownedEvidence = Start-TestRunEvidence -Repository (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)) -Context @{project='RedPrism';scope='failure-report-controls';configuration=$Configuration}
    $EvidenceRun = $ownedEvidence
}
$repository = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if ($EvidenceRun.Repository -cne $repository -or $EvidenceRun.Owner -ne $PID -or $EvidenceRun.Closed -or
    $EvidenceRun.Path -cne (Join-Path $repository ".build\evidence\test-runs\$($EvidenceRun.Id)\payload")) {
    throw 'Failure-report controls require an owned repository-local evidence run.'
}
$logRoot = Join-Path $EvidenceRun.Path ('failure-reports-' + [guid]::NewGuid().ToString('N'))
[void](New-Item -ItemType Directory -Path $logRoot -Force)
$passed = $false
try {
    foreach ($suite in $Suites) {
        $executable = Join-Path $OutputRoot "$suite.exe"
        if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
            throw "$suite.exe was not found: $executable"
        }
        $stdout = Join-Path $logRoot "$suite.out.log"
        $stderr = Join-Path $logRoot "$suite.err.log"
        $startedAt = [DateTime]::Now
        $process = Start-Process -WindowStyle Hidden -FilePath $executable -ArgumentList @('--failure-report-self-test') `
            -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
        # Keep the handle open until the exit is observed: Windows PowerShell can otherwise return an empty ExitCode.
        $null = $process.Handle
        if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
            # Only the process this test started, through its own process object, and only once its path and start time
            # still name the child started above.
            $ownChild = $process.StartTime -ge $startedAt.AddSeconds(-5) -and
                [string]::Equals([IO.Path]::GetFullPath($process.MainModule.FileName), [IO.Path]::GetFullPath($executable), [StringComparison]::OrdinalIgnoreCase)
            if ($ownChild) {
                $process.Kill()
                $process.WaitForExit()
            }
            throw "$suite.exe --failure-report-self-test did not end within $TimeoutSeconds s (PID $($process.Id), $(if ($ownChild) { 'ended by this test' } else { 'not ended: it is not the child this test started' })): a failed runtime check waits on a dialog instead of ending the run."
        }
        $process.WaitForExit()
        $exitCode = $process.ExitCode
        $errorText = if (Test-Path -LiteralPath $stderr) { [IO.File]::ReadAllText($stderr) } else { '' }
        if ($Configuration -eq 'Release') {
            # _CRT_ERROR / _CRT_ASSERT reporting only exists under _DEBUG; the routing around it still ran first.
            if ($exitCode -ne 0) {
                throw "$suite.exe --failure-report-self-test exited with $exitCode in Release, not 0: $errorText"
            }
        } else {
            if ($exitCode -ne 3) {
                throw "$suite.exe --failure-report-self-test exited with $exitCode, not 3 (the dialog's Abort): $errorText"
            }
            if ($errorText.IndexOf($expectedReport, [StringComparison]::Ordinal) -lt 0) {
                throw "$suite.exe --failure-report-self-test did not write the CRT report to stderr: '$errorText'"
            }
            if ($errorText.IndexOf('returned instead of ending', [StringComparison]::Ordinal) -ge 0) {
                throw "$suite.exe --failure-report-self-test: the failed check returned instead of ending the run."
            }
        }
        Write-Host "PASS failure report self-test ($suite.exe, $Configuration)"
    }
    $passed = $true
} finally {
    if ($ownedEvidence) { Complete-TestRunEvidence -Run $ownedEvidence -Passed $passed }
}
exit 0
