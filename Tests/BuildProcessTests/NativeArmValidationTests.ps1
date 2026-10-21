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
$hostDiscovery = Join-Path $RepositoryRoot 'Build/VisualStudioDiscovery.psm1'
foreach ($required in @($helper,$testEntrypoint,$hostDiscovery)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) { throw "Missing candidate input: $required" }
}
$sourceHashes = [ordered]@{}
foreach ($source in @($helper, $testEntrypoint, $hostDiscovery, $PSCommandPath)) {
    $sourceHashes[$source] = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
}
$tokens=$null; $parseErrors=$null
$helperAst=[Management.Automation.Language.Parser]::ParseFile($helper,[ref]$tokens,[ref]$parseErrors)
if ($parseErrors.Count) { throw "Helper parse failed: $parseErrors" }
$entryTokens=$null; $entryErrors=$null
$entryAst=[Management.Automation.Language.Parser]::ParseFile($testEntrypoint,[ref]$entryTokens,[ref]$entryErrors)
if ($entryErrors.Count) { throw "test.ps1 parse failed: $entryErrors" }

# Extract the actual candidate functions, without executing the CI helper's top-level/native checks.
# Discovery alone is replaced. FileVersionInfo, file existence, JSON receipts and hashing remain real.
$hostInitializer = @($helperAst.FindAll({param($node)
    $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq 'Initialize-NativeBuildHost'
}, $true))
$hostWriter = @($helperAst.FindAll({param($node)
    $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq 'Write-Evidence'
}, $true))
Assert-Check ($hostInitializer.Count -eq 1 -and $hostWriter.Count -eq 1) 'toolset-one-actual-initializer-and-writer'
$metadataSource = & {
    Import-Module $hostDiscovery -Force -Scope Local
    Find-RedPrismMSBuild
}
Assert-Check ($metadataSource -and (Test-Path -LiteralPath $metadataSource -PathType Leaf) -and
    [Diagnostics.FileVersionInfo]::GetVersionInfo($metadataSource).FileMajorPart -eq 18) 'toolset-MSBuild-stand-in-has-real-major-18-metadata'
$metadataHash = (Get-FileHash -LiteralPath $metadataSource -Algorithm SHA256).Hash
$ambientMSBuild = [Environment]::GetEnvironmentVariable('MSBUILD_EXE_PATH', 'Process')
$ambientArchitecture = [Environment]::GetEnvironmentVariable('PreferredToolArchitecture', 'Process')
$toolsetCases = @(
    @{name='required';version='14.52.37025';compiler=$true;success=$true;matched=$true},
    @{name='older-default';version='14.51.36231';compiler=$true;success=$false;matched=$false},
    @{name='newer-default';version='14.53.37100';compiler=$true;success=$false;matched=$false},
    @{name='missing-native-compiler';version='14.52.37025';compiler=$false;success=$false;matched=$true}
)
foreach ($toolsetCase in $toolsetCases) {
    # This child scope keeps the extracted functions and fake discovery exports out of the progress controls.
    & {
        param($Case, $InitializerText, $WriterText, $MetadataSource, $MetadataHash, $FixtureOutput)
        $repoRoot = [IO.Path]::GetFullPath((Join-Path $FixtureOutput ('ToolsetBoundary/' + $Case.name)))
        $outRoot = Join-Path $repoRoot 'Evidence'
        $installation = Join-Path $repoRoot 'SelectedVS'
        $originalMSBuild = Join-Path $installation 'MSBuild/Current/Bin/MSBuild.exe'
        $nativeMSBuild = Join-Path $installation 'MSBuild/Current/Bin/arm64/MSBuild.exe'
        $defaultVersion = Join-Path $installation 'VC/Auxiliary/Build/Microsoft.VCToolsVersion.default.txt'
        $compiler = Join-Path $installation ("VC/Tools/MSVC/" + $Case.version + '/bin/HostARM64/ARM64/cl.exe')
        $discoveryPath = [IO.Path]::GetFullPath((Join-Path $repoRoot 'Build/VisualStudioDiscovery.psm1'))
        New-Item -ItemType Directory -Path $outRoot,(Split-Path -Parent $originalMSBuild),
            (Split-Path -Parent $nativeMSBuild),(Split-Path -Parent $defaultVersion),
            (Split-Path -Parent $compiler),(Split-Path -Parent $discoveryPath) -Force | Out-Null
        [IO.File]::WriteAllBytes($originalMSBuild, [IO.File]::ReadAllBytes($MetadataSource))
        [IO.File]::WriteAllBytes($nativeMSBuild, [IO.File]::ReadAllBytes($MetadataSource))
        # Whitespace is intentional: the real initializer must trim the default-toolset file.
        [IO.File]::WriteAllText($defaultVersion, ('  ' + $Case.version + "`r`n"), [Text.UTF8Encoding]::new($false))
        $defaultVersionHash = (Get-FileHash -LiteralPath $defaultVersion -Algorithm SHA256).Hash
        $compilerBytes = [Text.Encoding]::UTF8.GetBytes('LITERAL COMPILER STAND-IN; NEVER EXECUTED')
        $sha = [Security.Cryptography.SHA256]::Create()
        try { $compilerHash = [BitConverter]::ToString($sha.ComputeHash($compilerBytes)).Replace('-', '') }
        finally { $sha.Dispose() }
        if ($Case.compiler) { [IO.File]::WriteAllBytes($compiler, $compilerBytes) }
        $discoveryText = @'
function Find-RedPrismMSBuild {
    Join-Path (Split-Path -Parent $PSScriptRoot) 'SelectedVS/MSBuild/Current/Bin/MSBuild.exe'
}
function Find-RedPrismVisualStudioInstallationOfMSBuild {
    param([string] $MSBuildPath)
    $installation = Join-Path (Split-Path -Parent $PSScriptRoot) 'SelectedVS'
    $expected = Join-Path $installation 'MSBuild/Current/Bin/MSBuild.exe'
    if ($MSBuildPath -cne $expected) { throw 'Unexpected isolated discovery input.' }
    $installation
}
Export-ModuleMember -Function Find-RedPrismMSBuild,Find-RedPrismVisualStudioInstallationOfMSBuild
'@
        [IO.File]::WriteAllText($discoveryPath, $discoveryText, [Text.UTF8Encoding]::new($false))
        . ([ScriptBlock]::Create($WriterText))
        . ([ScriptBlock]::Create($InitializerText))
        $savedEnvironment = [Environment]::GetEnvironmentVariables('Process')
        $failure = $null
        $succeeded = $false
        $inherited = $null
        try {
            $env:MSBUILD_EXE_PATH = $originalMSBuild
            $env:PreferredToolArchitecture = 'literal-prior-host'
            $environmentBefore = [Environment]::GetEnvironmentVariables('Process')
            try {
                Initialize-NativeBuildHost
                $succeeded = $true
            } catch {
                $failure = $_.Exception.Message
            }
            $environmentAfter = [Environment]::GetEnvironmentVariables('Process')
            $environmentKeys = @(@($environmentBefore.Keys) + @($environmentAfter.Keys) | Sort-Object -Unique)
            $changedEnvironmentKeys = @($environmentKeys | Where-Object {
                -not $environmentBefore.Contains($_) -or -not $environmentAfter.Contains($_) -or
                    [string]$environmentBefore[$_] -cne [string]$environmentAfter[$_]
            })
            $prefix = 'toolset-' + $Case.name + '-'
            Assert-Check ($succeeded -eq $Case.success) ($prefix + 'actual-initializer-outcome')
            $requirementPath = Join-Path $outRoot 'toolset-requirement.json'
            Assert-Check (Test-Path -LiteralPath $requirementPath -PathType Leaf) ($prefix + 'requirement-receipt-exists')
            $requirement = [IO.File]::ReadAllText($requirementPath) | ConvertFrom-Json
            $expectedNames = @('requiredDefaultVCToolsVersion','actualDefaultVCToolsVersion','selectedInstallation','matched','interpretation')
            $actualNames = @($requirement.PSObject.Properties.Name)
            Assert-Check ($actualNames.Count -eq $expectedNames.Count -and
                @(Compare-Object $expectedNames $actualNames).Count -eq 0 -and
                $requirement.requiredDefaultVCToolsVersion -ceq '14.52.37025' -and
                $requirement.actualDefaultVCToolsVersion -ceq $Case.version -and
                $requirement.selectedInstallation -ceq $installation -and
                $requirement.matched -is [bool] -and $requirement.matched -eq $Case.matched -and
                $requirement.interpretation -ceq 'Default-toolset preflight only; actual compiler/dependency products and native runtime still require independent qualification'
            ) ($prefix + 'exact-requirement-receipt')
            $selectionPath = Join-Path $outRoot 'build-host-selection.json'
            if (-not $Case.success) {
                $expectedFailure = if (-not $Case.matched) {
                    'Native ARM64 requires default MSVC 14.52.37025; selected installation provides ' +
                        $Case.version + '. No canonical work started.'
                } else { 'Native ARM64-hosted compiler is unavailable in the default v145 toolset.' }
                Assert-Check ($failure -ceq $expectedFailure) ($prefix + 'exact-refusal')
                Assert-Check ($changedEnvironmentKeys.Count -eq 0 -and $env:MSBUILD_EXE_PATH -ceq $originalMSBuild -and
                    $env:PreferredToolArchitecture -ceq 'literal-prior-host' -and
                    -not (Test-Path -LiteralPath $selectionPath)) ($prefix + 'refuses-before-host-environment-mutation')
            } else {
                Assert-Check ($null -eq $failure -and $changedEnvironmentKeys.Count -eq 2 -and
                    @(Compare-Object @('MSBUILD_EXE_PATH','PreferredToolArchitecture') $changedEnvironmentKeys).Count -eq 0 -and
                    $env:MSBUILD_EXE_PATH -ceq $nativeMSBuild -and
                    $env:PreferredToolArchitecture -ceq 'arm64') ($prefix + 'selects-native-path-and-architecture')
                $selection = [IO.File]::ReadAllText($selectionPath) | ConvertFrom-Json
                $selectionNames = @('originalMSBuild','selectedMSBuild','requestedPreferredToolArchitecture',
                    'msbuildSha256','availableNativeCompiler','availableNativeCompilerSha256','interpretation')
                $actualSelectionNames = @($selection.PSObject.Properties.Name)
                Assert-Check ($actualSelectionNames.Count -eq $selectionNames.Count -and
                    @(Compare-Object $selectionNames $actualSelectionNames).Count -eq 0 -and
                    $selection.originalMSBuild -ceq $originalMSBuild -and
                    $selection.selectedMSBuild -ceq $nativeMSBuild -and
                    $selection.requestedPreferredToolArchitecture -ceq 'arm64' -and
                    $selection.msbuildSha256 -ceq $MetadataHash -and
                    $selection.availableNativeCompiler -ceq $compiler -and
                    $selection.availableNativeCompilerSha256 -ceq $compilerHash -and
                    $selection.interpretation -ceq 'Native build-host selection only; runtime suite success still requires actual completion'
                ) ($prefix + 'exact-selection-paths-and-real-byte-hashes')
                # This read-only child observes inherited environment only. It never executes either stand-in.
                $start = [Diagnostics.ProcessStartInfo]::new()
                $start.FileName = Join-Path $PSHOME 'pwsh.exe'
                $start.WorkingDirectory = $repoRoot
                $start.UseShellExecute = $false
                $start.CreateNoWindow = $true
                $start.RedirectStandardOutput = $true
                $start.RedirectStandardError = $true
                foreach ($argument in @('-NoLogo','-NoProfile','-NonInteractive','-Command',
                        '[ordered]@{msbuild=$env:MSBUILD_EXE_PATH;architecture=$env:PreferredToolArchitecture}|ConvertTo-Json -Compress')) {
                    $start.ArgumentList.Add($argument)
                }
                $child = [Diagnostics.Process]::new()
                $child.StartInfo = $start
                try {
                    if (-not $child.Start()) { throw 'Environment inheritance observer did not start.' }
                    $childId = $child.Id
                    $stdout = $child.StandardOutput.ReadToEndAsync()
                    $stderr = $child.StandardError.ReadToEndAsync()
                    if (-not $child.WaitForExit(15000)) {
                        Write-Evidence 'inheritance-child-result.json' ([ordered]@{pid=$childId;exit=$null;status='timeout-not-terminated'})
                        throw "Environment inheritance observer PID $childId timed out; it was not terminated."
                    }
                    $exit = $child.ExitCode
                    $drain = [Threading.Tasks.Task]::WhenAll([Threading.Tasks.Task[]]@($stdout,$stderr))
                    if (-not $drain.Wait(5000)) { throw 'Environment observer exited without bounded output drain.' }
                    $drain.GetAwaiter().GetResult()
                    $output = $stdout.GetAwaiter().GetResult()
                    $errorOutput = $stderr.GetAwaiter().GetResult()
                    Write-Evidence 'inheritance-child-result.json' ([ordered]@{
                        pid=$childId;exit=$exit;stdout=$output;stderr=$errorOutput
                        nativeCompilerExecuted=$false;canonicalTestExecuted=$false
                    })
                    $inherited = $output | ConvertFrom-Json
                    Assert-Check ($exit -eq 0 -and $errorOutput -ceq '' -and
                        $inherited.msbuild -ceq $nativeMSBuild -and $inherited.architecture -ceq 'arm64'
                    ) ($prefix + 'actual-read-only-child-inherits-both-host-settings')
                } finally { $child.Dispose() }
            }
            $compilerUnchanged = if ($Case.compiler) {
                (Get-FileHash -LiteralPath $compiler -Algorithm SHA256).Hash -ceq $compilerHash
            } else { $true }
            Assert-Check ($compilerUnchanged -and
                (Get-FileHash -LiteralPath $defaultVersion -Algorithm SHA256).Hash -ceq $defaultVersionHash -and
                (Get-FileHash -LiteralPath $nativeMSBuild -Algorithm SHA256).Hash -ceq $MetadataHash -and
                [Diagnostics.FileVersionInfo]::GetVersionInfo($nativeMSBuild).FileMajorPart -eq 18 -and
                ([IO.File]::ReadAllText($defaultVersion)).Trim() -ceq $Case.version -and
                ((Test-Path -LiteralPath $compiler -PathType Leaf) -eq $Case.compiler)) ($prefix + 'installation-stand-ins-remain-unchanged')
            Add-Result ('toolset-' + $Case.name) ([ordered]@{
                initializerExecuted=$true;requirement=$requirement;failure=$failure
                changedEnvironmentKeys=$changedEnvironmentKeys
                inheritedEnvironment=$inherited;metadataSource=$MetadataSource;metadataSha256=$MetadataHash
                compilerStandInSha256=$(if ($Case.compiler) { $compilerHash } else { $null })
                compilerStandInExecuted=$false;canonicalChildStarted=$false;nativeRuntimeQualified=$false
            })
        } finally {
            $currentEnvironment = [Environment]::GetEnvironmentVariables('Process')
            foreach ($environmentKey in @(@($savedEnvironment.Keys) + @($currentEnvironment.Keys) | Sort-Object -Unique)) {
                if ($savedEnvironment.Contains($environmentKey)) {
                    [Environment]::SetEnvironmentVariable($environmentKey, $savedEnvironment[$environmentKey], 'Process')
                } else {
                    # A bare PowerShell null converts to an empty string for the .NET string overload.
                    [Environment]::SetEnvironmentVariable($environmentKey,
                        [Management.Automation.Language.NullString]::Value, 'Process')
                }
            }
            Get-Module | Where-Object { $_.Path -ieq $discoveryPath } | Remove-Module -Force
        }
    } $toolsetCase $hostInitializer[0].Extent.Text $hostWriter[0].Extent.Text $metadataSource $metadataHash $OutputRoot
}
Assert-Check ([Environment]::GetEnvironmentVariable('MSBUILD_EXE_PATH','Process') -ceq $ambientMSBuild -and
    [Environment]::GetEnvironmentVariable('PreferredToolArchitecture','Process') -ceq $ambientArchitecture
) 'toolset-fixtures-restore-ambient-host-environment'

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
$initializeCalls = @($mainTry[0].Body.FindAll({param($node)
    $node -is [Management.Automation.Language.CommandAst] -and $node.GetCommandName() -ceq 'Initialize-NativeBuildHost'
}, $true))
$environmentCalls = @($mainTry[0].Body.FindAll({param($node)
    $node -is [Management.Automation.Language.CommandAst] -and $node.GetCommandName() -ceq 'Get-EnvironmentEvidence'
}, $true))
$canonicalStarts = @($mainTry[0].Body.FindAll({param($node)
    $node -is [Management.Automation.Language.InvokeMemberExpressionAst] -and
        $node.Expression.Extent.Text -ceq '$child' -and
        $node.Member -is [Management.Automation.Language.StringConstantExpressionAst] -and $node.Member.Value -ceq 'Start'
}, $true))
Assert-Check ($initializeCalls.Count -eq 1 -and $environmentCalls.Count -eq 2 -and $canonicalStarts.Count -eq 1 -and
    @($environmentCalls | Where-Object { $_.Extent.StartOffset -le $initializeCalls[0].Extent.StartOffset }).Count -eq 0 -and
    $initializeCalls[0].Extent.StartOffset -lt $canonicalStarts[0].Extent.StartOffset
) 'toolset-preflight-precedes-canonical-environment-and-child-start'
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
    actualBuildHostFunctionExecuted=$true;buildHostFunctionCases=$toolsetCases.Count
    compilerStandInsExecuted=$false;msbuildStandInsExecuted=$false
    actualNativeApplicationExecuted=$false;buildSentinelReached=$false;canonicalProfileQualified=$false
    p1Done=$false;utc=[DateTime]::UtcNow.ToString('o')}
$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $OutputRoot 'result.json') -Encoding utf8
$global:LASTEXITCODE = 0
$invalidChildResults = @($results | Where-Object { $_.name -like 'invalid-*' })
Write-Host "PASS native-arm-validation.complete ($($checkNames.Count) checks, $($invalidChildResults.Count) exact-refusal child receipts, $($toolsetCases.Count) isolated initializer cases)"
