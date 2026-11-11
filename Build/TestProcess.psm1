# Read-only observation of an already launched test. No process termination or success inference from logs.
Set-StrictMode -Version Latest

function Read-RedPrismTerminalFailureTail {
    param([string] $Path, [string] $TerminalFailureLine)
    $stream = $null
    try {
        $stream = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read,
            [IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete)
        # Bounded allocation and I/O even if a failed native test writes an enormous report.
        $offset = [Math]::Max(0L, $stream.Length - 16384L)
        $null = $stream.Seek($offset, [IO.SeekOrigin]::Begin)
        $bytes = [byte[]]::new(16384)
        $count = $stream.Read($bytes, 0, $bytes.Length)
        $text = [Text.Encoding]::UTF8.GetString($bytes, 0, $count)
        # A tail may start inside a line. Never accept that suffix as a complete failure record.
        if ($offset -gt 0) {
            $firstNewline = $text.IndexOf("`n", [StringComparison]::Ordinal)
            $text = if ($firstNewline -ge 0) { $text.Substring($firstNewline + 1) } else { '' }
        }
        $lines = $text.Split("`n")
        # The last element is incomplete (or empty); only newline-terminated, ordinal exact lines count.
        for ($index = 0; $index -lt $lines.Length - 1; $index++) {
            $line = $lines[$index]
            if ($line.EndsWith("`r", [StringComparison]::Ordinal)) { $line = $line.Substring(0, $line.Length - 1) }
            if ($line -ceq $TerminalFailureLine) {
                return [pscustomobject]@{ Found = $true; ReadError = $null }
            }
        }
        return [pscustomobject]@{ Found = $false; ReadError = $null }
    }
    catch [IO.IOException] { return [pscustomobject]@{ Found = $false; ReadError = $_.Exception.Message } }
    catch [UnauthorizedAccessException] { return [pscustomobject]@{ Found = $false; ReadError = $_.Exception.Message } }
    finally { if ($null -ne $stream) { $stream.Dispose() } }
}

function Wait-RedPrismTestProcess {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [Diagnostics.Process] $Process,
        [Parameter(Mandatory)] [string] $StandardErrorPath,
        [Parameter(Mandatory)] [ValidateNotNullOrEmpty()] [string] $TerminalFailureLine,
        [Parameter(Mandatory)] [ValidateRange(1, 5400000)] [int] $TimeoutMilliseconds,
        [ValidateRange(1, 30000)] [int] $FailureExitGraceMilliseconds = 5000
    )
    if ($TerminalFailureLine.Contains("`n") -or $TerminalFailureLine.Contains("`r")) {
        throw 'The terminal failure marker must be one exact line without a line ending.'
    }
    $timer = [Diagnostics.Stopwatch]::StartNew()
    $failureObservedAt = $null
    $lastReadError = $null
    # Keep the supplied handle alive; observe this child, never rediscover a process by name or PID.
    $null = $Process.Handle
    while ($true) {
        if ($Process.HasExited) {
            # Finish redirected-stream handlers only after actual exit.
            $Process.WaitForExit()
            # A fast exit can happen between observations. Retain a complete failure
            # record even when the child (incorrectly) returns zero afterward.
            if ($null -eq $failureObservedAt) {
                $tail = Read-RedPrismTerminalFailureTail $StandardErrorPath $TerminalFailureLine
                if ($tail.Found) { $failureObservedAt = $timer.ElapsedMilliseconds }
                if ($null -ne $tail.ReadError) { $lastReadError = $tail.ReadError }
            }
            return [pscustomobject]@{ Status = 'Exited'; ExitCode = $Process.ExitCode; ProcessId = $Process.Id;
                ElapsedMilliseconds = $timer.ElapsedMilliseconds; TerminalFailureObserved = ($null -ne $failureObservedAt); LastReadError = $lastReadError }
        }
        if ($null -eq $failureObservedAt) {
            $tail = Read-RedPrismTerminalFailureTail $StandardErrorPath $TerminalFailureLine
            if ($tail.Found) { $failureObservedAt = $timer.ElapsedMilliseconds }
            if ($null -ne $tail.ReadError) { $lastReadError = $tail.ReadError }
        }
        $remaining = $TimeoutMilliseconds - $timer.ElapsedMilliseconds
        $status = if ($null -ne $failureObservedAt -and
            $timer.ElapsedMilliseconds - $failureObservedAt -ge $FailureExitGraceMilliseconds) { 'TerminalFailure' }
            elseif ($remaining -le 0) { 'TimedOut' } else { $null }
        if ($null -ne $status) {
            return [pscustomobject]@{ Status = $status; ExitCode = $null; ProcessId = $Process.Id;
                ElapsedMilliseconds = $timer.ElapsedMilliseconds; TerminalFailureObserved = ($null -ne $failureObservedAt); LastReadError = $lastReadError }
        }
        $wait = [Math]::Min(250L, $remaining)
        if ($null -ne $failureObservedAt) {
            $wait = [Math]::Min($wait, $FailureExitGraceMilliseconds - ($timer.ElapsedMilliseconds - $failureObservedAt))
        }
        $null = $Process.WaitForExit([int][Math]::Max(1L, $wait))
    }
}

Export-ModuleMember -Function Wait-RedPrismTestProcess
