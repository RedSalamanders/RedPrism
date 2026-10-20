[CmdletBinding()]
param(
    [string] $RepositoryRoot = (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)),
    [string] $OutputRoot
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$RepositoryRoot = [IO.Path]::GetFullPath($RepositoryRoot)
if (-not $OutputRoot) {
    $stamp = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')
    $OutputRoot = Join-Path $RepositoryRoot ".build/BuildProcessTests/NativeArmValidation-$stamp-$PID"
}
$OutputRoot = [IO.Path]::GetFullPath($OutputRoot)
if (Test-Path -LiteralPath $OutputRoot) { throw 'Refusing to overwrite existing validation evidence.' }
New-Item -ItemType Directory -Path $OutputRoot | Out-Null
$results = [Collections.Generic.List[object]]::new()
$checkNames = [Collections.Generic.List[string]]::new()
function Assert-Check([bool] $Condition, [string] $Name) {
    if (-not $Condition) { throw "FAIL native-arm-validation.$Name" }
    $script:checkNames.Add($Name)
    Write-Host "PASS native-arm-validation.$Name"
}
function Add-Result([string] $Name, [object] $Value) { $script:results.Add([pscustomobject]@{name=$Name;value=$Value}) }
$helper = Join-Path $RepositoryRoot '.github/scripts/Run-NativeArm64.ps1'
$testEntrypoint = Join-Path $RepositoryRoot 'test.ps1'
foreach ($required in @($helper,$testEntrypoint)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) { throw "Missing candidate input: $required" }
}
$sourceHashes = [ordered]@{}
foreach ($source in @($helper, $testEntrypoint, $PSCommandPath)) {
    $sourceHashes[$source] = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
}
$tokens=$null; $parseErrors=$null
$helperAst=[Management.Automation.Language.Parser]::ParseFile($helper,[ref]$tokens,[ref]$parseErrors)
if ($parseErrors.Count) { throw "Helper parse failed: $parseErrors" }
$entryTokens=$null; $entryErrors=$null
$entryAst=[Management.Automation.Language.Parser]::ParseFile($testEntrypoint,[ref]$entryTokens,[ref]$entryErrors)
if ($entryErrors.Count) { throw "test.ps1 parse failed: $entryErrors" }
$diag=@($helperAst.FindAll({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq 'Get-NativeProgressEvidence'},$true))
Assert-Check ($diag.Count -eq 1) 'one-diagnostic-function'
. ([ScriptBlock]::Create($diag[0].Extent.Text))

# These controls preserve the independent literal fixtures exercised by Main's r7 progress script.
$script:mode='unavailable'; $script:queries=[Collections.Generic.List[string]]::new()
$script:fixtureRoot=Join-Path $OutputRoot 'Fixture'; $script:logReadUnavailable=$false
function Get-CimInstance {
    [CmdletBinding()]
    param([string]$ClassName,[int]$OperationTimeoutSec,[string]$Filter)
    if($OperationTimeoutSec -ne 5){throw 'Unexpected CIM operation bound.'}
    $script:queries.Add($ClassName+':'+$Filter)
    if($script:mode -eq 'unavailable'){throw 'literal-CIM-unavailable'}
    if($ClassName -eq 'Win32_OperatingSystem'){return [pscustomobject]@{FreePhysicalMemory=1024}}
    if($ClassName -ne 'Win32_Process' -or $Filter -cne "ParentProcessId=12345 AND Name='RedPrism.exe'"){throw 'Unexpected process ownership query.'}
    foreach($i in 1..6){[pscustomobject]@{ExecutablePath=(Join-Path $script:fixtureRoot '.build/ARM64/Release/../Release/RedPrism.exe');ProcessId=5000+$i;ParentProcessId=12345;CreationDate='literal-created';UserModeTime=10*$i;KernelModeTime=20*$i;WorkingSetSize=30*$i}}
    [pscustomobject]@{ExecutablePath=(Join-Path $script:fixtureRoot 'Foreign/RedPrism.exe')}; [pscustomobject]@{ExecutablePath=$null}
}
function Get-Content {
    [CmdletBinding()]
    param([string]$LiteralPath,[int]$Tail)
    if($script:logReadUnavailable){throw 'literal-log-disappeared'}
    Microsoft.PowerShell.Management\Get-Content -LiteralPath $LiteralPath -Tail $Tail -ErrorAction Stop
}
$absent=Get-NativeProgressEvidence -RepositoryRoot (Join-Path $OutputRoot 'Absent') -CanonicalChildId 12345 -ElapsedSeconds 12.25
Assert-Check ($null -eq $absent['freeMemoryBytes'] -and $null -eq $absent['ownedApplicationCount']) 'CIM-unavailable-explicit-null'
Assert-Check (-not $absent['ownedApplicationQueryAvailable'] -and -not $absent['applicationLogAvailable'] -and -not $absent['applicationStderrAvailable']) 'unavailable-distinct-from-empty'
Assert-Check ($absent['diagnosticErrors'].Count -eq 2 -and $script:queries.Count -eq 2) 'both-CIM-errors-nonfatal'
Assert-Check ($absent['elapsedSeconds'] -eq 12 -and $absent['ownedApplications'].Count -eq 0) 'unavailable-retains-progress'
$script:mode='available'
$parentLog=Join-Path $script:fixtureRoot '.build/native-arm-ci/test.stdout.log'; $appLog=Join-Path $script:fixtureRoot '.build/ARM64/Release/RedPrism.self-test.log'
New-Item -ItemType Directory -Path (Split-Path -Parent $parentLog),(Split-Path -Parent $appLog) -Force | Out-Null
[IO.File]::WriteAllText($parentLog,"parent-first`nparent-last`n",[Text.UTF8Encoding]::new($false)); [IO.File]::WriteAllText($appLog,"app-first`napp-last`n",[Text.UTF8Encoding]::new($false))
$appErrorLog = Join-Path $script:fixtureRoot '.build/ARM64/Release/RedPrism.self-test.err.log'
[IO.File]::WriteAllText($appErrorLog, "err-first`nerr-last`n", [Text.UTF8Encoding]::new($false))
$available=Get-NativeProgressEvidence -RepositoryRoot $script:fixtureRoot -CanonicalChildId 12345 -ElapsedSeconds 30.1
Assert-Check ($available['freeMemoryBytes'] -eq 1048576 -and $available['diagnosticErrors'].Count -eq 0) 'memory-unit-conversion'
Assert-Check ($available['ownedApplicationQueryAvailable'] -and $available['ownedApplicationCount'] -eq 6 -and $available['ownedApplications'].Count -eq 4) 'normalized-owned-paths-foreign-null-filter-four-cap'
Assert-Check ($available['ownedApplications'][0]['pid'] -eq 5001 -and $available['ownedApplications'][0]['parent'] -eq 12345 -and $available['ownedApplications'][0]['userCpu100ns'] -eq 10 -and $available['ownedApplications'][0]['kernelCpu100ns'] -eq 20 -and $available['ownedApplications'][0]['workingSetBytes'] -eq 30) 'literal-process-cpu-memory-values'
Assert-Check ($available['applicationLogAvailable'] -and $available['lastOutput'] -ceq 'parent-last' -and $available['applicationLastOutput'] -ceq 'app-last' -and $available['applicationStdoutBytes'] -eq 19) 'literal-parent-and-app-log-tails'
Assert-Check ($available['applicationStderrAvailable'] -and $available['applicationStderrBytes'] -eq 19 -and
    $available['applicationLastErrorOutput'] -ceq 'err-last') 'independent-application-stderr-is-recorded'
[IO.File]::WriteAllText($appLog, '', [Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText($appErrorLog, "err-advanced`n", [Text.UTF8Encoding]::new($false))
$stderrOnly = Get-NativeProgressEvidence -RepositoryRoot $script:fixtureRoot -CanonicalChildId 12345 -ElapsedSeconds 45
Assert-Check ($stderrOnly['applicationLogAvailable'] -and $stderrOnly['applicationStdoutBytes'] -eq 0 -and
    $null -eq $stderrOnly['applicationLastOutput'] -and $stderrOnly['applicationStderrAvailable'] -and
    $stderrOnly['applicationStderrBytes'] -eq 13 -and $stderrOnly['applicationLastErrorOutput'] -ceq 'err-advanced') 'empty-stdout-still-observes-advancing-stderr'
$script:logReadUnavailable=$true
$logFailure=Get-NativeProgressEvidence -RepositoryRoot $script:fixtureRoot -CanonicalChildId 12345 -ElapsedSeconds 60
Assert-Check ($logFailure['diagnosticErrors'].Count -eq 1 -and $logFailure['diagnosticErrors'][0] -ceq 'logs: literal-log-disappeared' -and -not $logFailure['applicationLogAvailable'] -and $logFailure['ownedApplicationQueryAvailable']) 'disappearing-log-does-not-stop-process-query'

# Inspect executable AST commands in the main try body; comments/string mentions cannot satisfy this guard.
$mainTry = @($helperAst.EndBlock.Statements | Where-Object { $_ -is [Management.Automation.Language.TryStatementAst] })
Assert-Check ($mainTry.Count -eq 1) 'one-main-try-body'
$commands = @($mainTry[0].Body.FindAll({param($node)
    $node -is [Management.Automation.Language.CommandAst] -and $node.GetCommandName() -ceq 'Write-Evidence'
}, $true))
function Evidence-Commands([string] $Name) {
    @($commands | Where-Object {
        $_.CommandElements.Count -gt 1 -and
        $_.CommandElements[1] -is [Management.Automation.Language.StringConstantExpressionAst] -and
        $_.CommandElements[1].Value -ceq $Name
    })
}
$afterAssignment = @($mainTry[0].Body.FindAll({param($node)
    $node -is [Management.Automation.Language.AssignmentStatementAst] -and $node.Left.Extent.Text -ceq '$after'
}, $true))
$drainAssignment = @($mainTry[0].Body.FindAll({param($node)
    $node -is [Management.Automation.Language.AssignmentStatementAst] -and $node.Left.Extent.Text -ceq '$drain'
}, $true))
Assert-Check ($afterAssignment.Count -eq 1 -and $drainAssignment.Count -eq 1) 'one-closing-snapshot-and-raw-drain'
$prior = $afterAssignment[0].Extent.StartOffset
foreach ($name in @('source-after.json', 'environment-after.json', 'products.json')) {
    $writes = @(Evidence-Commands $name)
    Assert-Check ($writes.Count -eq 1 -and $writes[0].Extent.StartOffset -gt $prior -and
        $writes[0].Extent.StartOffset -lt $drainAssignment[0].Extent.StartOffset) ('evidence-before-raw-drain.' + $name)
    $prior = $writes[0].Extent.StartOffset
}
$childResults = @(Evidence-Commands 'child-result.json' | Sort-Object { $_.Extent.StartOffset })
Assert-Check ($childResults.Count -eq 2 -and $childResults[-1].Extent.StartOffset -lt $afterAssignment[0].Extent.StartOffset -and
    $childResults[-1].Extent.Text.Contains('exit=$child.ExitCode')) 'actual-parent-exit-recorded-before-closeout'
$terminalProgress = @(Evidence-Commands 'terminal-progress.json')
Assert-Check ($terminalProgress.Count -eq 1 -and
    $terminalProgress[0].Extent.StartOffset -gt $childResults[-1].Extent.StartOffset -and
    $terminalProgress[0].Extent.StartOffset -lt $afterAssignment[0].Extent.StartOffset) 'terminal-progress-recorded-before-closing-identity-and-drain'
$beforeSource = @(Evidence-Commands 'source-before.json')
$beforeEnvironment = @(Evidence-Commands 'environment-before.json')
Assert-Check ($beforeSource.Count -eq 1 -and $beforeEnvironment.Count -eq 1 -and
    $beforeSource[0].Extent.StartOffset -lt $afterAssignment[0].Extent.StartOffset -and
    $beforeEnvironment[0].Extent.StartOffset -lt $afterAssignment[0].Extent.StartOffset) 'opening-source-environment-receipts-retained'

# Confirm canonical parameter metadata, then run only invalid parameter children that fail before build.ps1.
$params=@($entryAst.ParamBlock.Parameters)
$timeoutParam=$params | Where-Object {$_.Name.VariablePath.UserPath -eq 'SelfTestTimeoutSeconds'}
$platformParam=$params | Where-Object {$_.Name.VariablePath.UserPath -eq 'Platform'}
$timeoutText=if($timeoutParam){$timeoutParam.Extent.Text}else{''}; $platformText=if($platformParam){$platformParam.Extent.Text}else{''}
Assert-Check ($timeoutText -match 'ValidateRange\(1200,\s*5400\)' -and $platformText -match "ValidateSet\('x64',\s*'ARM64'\)") 'actual-entrypoint-parameter-ranges'
Assert-Check ($timeoutParam.DefaultValue -is [Management.Automation.Language.ConstantExpressionAst] -and
    $timeoutParam.DefaultValue.Value -eq 1200) 'default-failure-bound-remains-1200'

# Test the exact entrypoint bytes in an isolated root with a build sentinel. Even a broken guard cannot compile.
$rootAssignments = @($entryAst.EndBlock.Statements | Where-Object {
    $_ -is [Management.Automation.Language.AssignmentStatementAst] -and $_.Left.Extent.Text -ceq '$repoRoot'
})
Assert-Check ($rootAssignments.Count -eq 1 -and
    $rootAssignments[0].Right.Extent.Text -ceq 'Split-Path -Parent $PSCommandPath') 'copied-entrypoint-root-is-isolated'
$boundary = Join-Path $OutputRoot 'InvalidArgumentBoundary'
New-Item -ItemType Directory -Path $boundary | Out-Null
$copiedEntrypoint = Join-Path $boundary 'test.ps1'
[IO.File]::WriteAllBytes($copiedEntrypoint, [IO.File]::ReadAllBytes($testEntrypoint))
Assert-Check ((Get-FileHash -LiteralPath $copiedEntrypoint -Algorithm SHA256).Hash -ceq $sourceHashes[$testEntrypoint]) 'invalid-argument-entrypoint-bytes-identical'
$sentinel = Join-Path $boundary 'build-reached.txt'
$stub = @'
[IO.File]::WriteAllText((Join-Path $PSScriptRoot 'build-reached.txt'), 'BUILD_BOUNDARY_REACHED')
throw 'Invalid-argument validation reached its isolated build sentinel.'
'@
[IO.File]::WriteAllText((Join-Path $boundary 'build.ps1'), $stub, [Text.UTF8Encoding]::new($false))
$driver = Join-Path $boundary 'InvokeInvalid.ps1'
$driverText = @'
param([string] $Entrypoint, [int] $TimeoutValue)
$ErrorActionPreference = 'Stop'
try {
    & $Entrypoint -Platform x64 -SelfTestTimeoutSeconds $TimeoutValue
    [ordered]@{unexpectedSuccess=$true} | ConvertTo-Json -Compress
    exit 0
} catch {
    $parameter = if ($_.Exception -is [Management.Automation.ParameterBindingException]) { $_.Exception.ParameterName } else { $null }
    [ordered]@{exceptionType=$_.Exception.GetType().FullName;parameterName=$parameter
        message=$_.Exception.Message;errorId=$_.FullyQualifiedErrorId} | ConvertTo-Json -Compress
    exit 1
}
'@
[IO.File]::WriteAllText($driver, $driverText, [Text.UTF8Encoding]::new($false))
$cases = @(@{value=1199;kind='range'}, @{value=5401;kind='range'}, @{value=5400;kind='native-only'})
$pwsh = Join-Path $PSHOME 'pwsh.exe'
foreach ($case in $cases) {
    $prefix = Join-Path $OutputRoot ("invalid-" + $case.value)
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $pwsh
    $start.WorkingDirectory = $boundary
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    foreach ($argument in @('-NoLogo', '-NoProfile', '-NonInteractive', '-File', $driver,
            '-Entrypoint', $copiedEntrypoint, '-TimeoutValue', [string]$case.value)) {
        $start.ArgumentList.Add($argument)
    }
    $proc = [Diagnostics.Process]::new()
    $proc.StartInfo = $start
    $outStream = [IO.File]::Open($prefix + '.stdout.log', [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::Read)
    $errStream = [IO.File]::Open($prefix + '.stderr.log', [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::Read)
    try {
        if (-not $proc.Start()) { throw 'Invalid-argument control did not start.' }
        $childId = $proc.Id
        [ordered]@{pid=$childId;arguments=@($start.ArgumentList);utc=[DateTime]::UtcNow.ToString('o')} |
            ConvertTo-Json -Depth 4 | Set-Content -LiteralPath ($prefix + '.started.json') -Encoding utf8
        $outTask = $proc.StandardOutput.BaseStream.CopyToAsync($outStream)
        $errTask = $proc.StandardError.BaseStream.CopyToAsync($errStream)
        if (-not $proc.WaitForExit(15000)) {
            [ordered]@{pid=$childId;exit=$null;status='timeout-not-terminated'} | ConvertTo-Json |
                Set-Content -LiteralPath ($prefix + '.child-result.json') -Encoding utf8
            throw "Invalid-argument control PID $childId timed out; it was not terminated."
        }
        $actualExit = $proc.ExitCode
        [ordered]@{pid=$childId;exit=$actualExit;utc=[DateTime]::UtcNow.ToString('o')} | ConvertTo-Json |
            Set-Content -LiteralPath ($prefix + '.child-result.json') -Encoding utf8
        $drain = [Threading.Tasks.Task]::WhenAll([Threading.Tasks.Task[]]@($outTask, $errTask))
        if (-not $drain.Wait(5000)) { throw "Invalid-argument control PID $childId exited but output did not drain." }
        $drain.GetAwaiter().GetResult()
        $outStream.Flush()
        $errStream.Flush()
        $outStream.Dispose()
        $outStream = $null
        $errStream.Dispose()
        $errStream = $null
        $detail = [IO.File]::ReadAllText($prefix + '.stdout.log') | ConvertFrom-Json
        $correct = if ($case.kind -eq 'range') {
            $detail.exceptionType -ceq 'System.Management.Automation.ParameterBindingValidationException' -and
                $detail.parameterName -ceq 'SelfTestTimeoutSeconds'
        } else {
            $detail.exceptionType -ceq 'System.Management.Automation.RuntimeException' -and
                $detail.message -ceq 'An extended self-test failure bound requires native ARM64.'
        }
        Assert-Check ($actualExit -eq 1 -and $correct -and -not (Test-Path -LiteralPath $sentinel)) ("invalid-" + $case.value + '-exact-refusal-before-build')
        Add-Result ("invalid-" + $case.value) ([ordered]@{pid=$childId;exit=$actualExit;detail=$detail
            testedEntrypoint=$copiedEntrypoint;buildSentinelReached=$false})
    } finally {
        if ($outStream) { $outStream.Dispose() }
        if ($errStream) { $errStream.Dispose() }
        $proc.Dispose()
    }
}
foreach ($source in $sourceHashes.Keys) {
    Assert-Check ((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -ceq $sourceHashes[$source]) ('source-bytes-unchanged.' + [IO.Path]::GetFileName($source))
}
$report = [ordered]@{status='NATIVE_ARM_VALIDATION_REGRESSION_CONTROLS_PASS';checks=$checkNames.ToArray()
    tests=$results.ToArray();sourceHashes=$sourceHashes;actualCIHelperScriptExecuted=$false
    actualNativeApplicationExecuted=$false;buildSentinelReached=$false;canonicalProfileQualified=$false
    p1Done=$false;utc=[DateTime]::UtcNow.ToString('o')}
$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $OutputRoot 'result.json') -Encoding utf8
$global:LASTEXITCODE = 0
Write-Host "PASS native-arm-validation.complete ($($checkNames.Count) checks, $($results.Count) exact-refusal child receipts)"
