<#
.SYNOPSIS
Proves the installed CRT report hook (RF-702) actually fails a harness run: HostTests.exe --crt-report-probe
deliberately trips a _CRT_ERROR report, and the hook the wmain CRT setup block installs must route it to stderr and
make the process exit non-zero, the same way --asan-probe proves the sanitizer is really wired up.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $HostTestsPath,
    [string] $Configuration = 'Debug'
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if ($Configuration -eq 'Release') {
    # _CRT_ERROR / _CRT_ASSERT reporting (and the --crt-report-probe switch itself) only exists under _DEBUG.
    Write-Host 'SKIP CRT report hook probe (Release has no debug CRT reporting).' -ForegroundColor Yellow
    exit 0
}
if (-not (Test-Path -LiteralPath $HostTestsPath -PathType Leaf)) {
    throw "HostTests.exe was not found: $HostTestsPath"
}

$err = Join-Path ([IO.Path]::GetTempPath()) ("RedPrism.CrtReportHookTests-" + [guid]::NewGuid().ToString('N') + '.err.log')
Remove-Item -LiteralPath $err -ErrorAction SilentlyContinue
try {
    $probe = Start-Process -WindowStyle Hidden -FilePath $HostTestsPath -ArgumentList @('--crt-report-probe') `
        -Wait -PassThru -RedirectStandardError $err
    $text = if (Test-Path -LiteralPath $err) { Get-Content -LiteralPath $err -Raw -ErrorAction SilentlyContinue } else { '' }
    if ($probe.ExitCode -eq 0) {
        throw 'HostTests.exe --crt-report-probe exited 0; the installed CRT report hook did not fail the run.'
    }
    if ([string]::IsNullOrEmpty($text) -or $text -notmatch 'RF-702 deliberate CRT report probe') {
        throw "HostTests.exe --crt-report-probe did not route the CRT report to stderr: '$text'"
    }
} finally {
    Remove-Item -LiteralPath $err -ErrorAction SilentlyContinue
}

Write-Host 'PASS CRT report hook probe (HostTests.exe)'
exit 0
