Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-RedPrismRedPrismProcesses {
    <#
    .SYNOPSIS
    The real enumerator: every running RedPrism.exe process. No -ErrorAction SilentlyContinue: a WMI/CIM failure
    must propagate so the caller can fail the build instead of silently seeing zero processes (RF-705).
    #>
    [CmdletBinding()]
    param()
    return Get-CimInstance -ClassName Win32_Process -Filter "Name='RedPrism.exe'"
}

function Assert-RedPrismBuildOutputProcessNotRunning {
    <#
    .SYNOPSIS
    Fails the build if a running RedPrism.exe process's executable path exactly matches the selected build output.
    Never terminates a process (AGENTS.md, Specs/Build/Build_Toolchain.md "Exact-output process preflight").

    .PARAMETER ExpectedExecutablePath
    The .build\<Platform>\<Configuration>\RedPrism.exe this invocation is about to build.

    .PARAMETER ProcessEnumerator
    Returns the running RedPrism.exe processes (objects with ExecutablePath / ProcessId). Defaults to a real
    Win32_Process query; a test substitutes a scriptblock that throws or returns fixed rows.
    #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $ExpectedExecutablePath,
        [scriptblock] $ProcessEnumerator = { Get-RedPrismRedPrismProcesses }
    )

    if (-not (Test-Path -LiteralPath $ExpectedExecutablePath -PathType Leaf)) {
        return
    }
    $expected = [IO.Path]::GetFullPath($ExpectedExecutablePath)

    $processes = $null
    try {
        $processes = & $ProcessEnumerator
    } catch {
        throw "Could not enumerate running RedPrism.exe processes ($($_.Exception.Message)). No process was terminated; resolve the enumeration failure (e.g. a WMI/CIM problem) and retry."
    }

    foreach ($process in $processes) {
        if ($process.ExecutablePath) {
            $running = [IO.Path]::GetFullPath($process.ExecutablePath)
            if ($running -eq $expected) {
                throw "RedPrism.exe is running from $expected (PID $($process.ProcessId)). Close it; this script will not terminate it."
            }
        }
    }
}

Export-ModuleMember -Function @(
    'Get-RedPrismRedPrismProcesses',
    'Assert-RedPrismBuildOutputProcessNotRunning'
)
