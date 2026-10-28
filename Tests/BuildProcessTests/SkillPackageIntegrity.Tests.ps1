<#
.SYNOPSIS
Checks declared skill references and command signatures with owned, non-executing fixtures.
.DESCRIPTION
Retains a unique repository-local .build fixture. Snapshots verify that validation creates no files,
changes no file bytes/write timestamps and never executes a referenced script or example.
#>
#Requires -Version 7.0
[CmdletBinding()]param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = Split-Path (Split-Path $PSScriptRoot)
Import-Module (Join-Path $root 'Build/SkillPackageIntegrity.psm1') -Force
$run = [IO.Path]::GetFullPath((Join-Path $root ('.build/skill-package-tests/'+[guid]::NewGuid().ToString('N'))))
$checks = 0
$skippedLinks = 0
function Put([string]$Name,[string]$Text) {
    $path = Join-Path $run $Name
    [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($path))
    [IO.File]::WriteAllText($path,$Text,[Text.UTF8Encoding]::new($false))
}
function Snapshot {
    return (@(Get-ChildItem -LiteralPath $run -Force -Recurse | Sort-Object FullName | ForEach-Object {
        $relative = [IO.Path]::GetRelativePath($run,$_.FullName)
        if ($_.PSIsContainer) { 'D:'+$relative }
        else { 'F:'+$relative+':'+$_.LastWriteTimeUtc.Ticks+':'+(Get-FileHash -LiteralPath $_.FullName).Hash }
    }) -join "`n")
}
function Expect([string]$Name,[bool]$Accepted,[string]$ExpectedProblem='') {
    $before = Snapshot
    $problem = $null
    try { Test-RedPrismSkillPackageIntegrity -Root $run }
    catch [System.Management.Automation.RuntimeException] { $problem = $_.Exception.Message }
    if ((-not $problem) -ne $Accepted) { throw "Package control '$Name' expected accepted=$Accepted`: $problem" }
    if ($ExpectedProblem -and $problem -notlike "*$ExpectedProblem*") { throw "Control '$Name' rejected for the wrong reason: $problem" }
    if ((Snapshot) -cne $before) { throw "Validation wrote during '$Name'." }
    Write-Host "PASS skill package $Name"
    $script:checks++
}
$skill = '.agents/skills/example-skill/SKILL.md'
Put $skill 'Read [contract](../../../Specs/contract.md).'
Put 'Specs/contract.md' '# Contract'
Expect 'valid local contract' $true
Put $skill 'Read [missing](../../../Specs/missing.md).'
Expect 'missing contract refused' $false
Put $skill 'Read [outside](../../../../outside.md).'
Expect 'outside repository refused' $false
Put $skill 'Read [contract](../../../Specs/contract%20name.md#section).'
Put 'Specs/contract name.md' '# Named contract'
Expect 'encoded path and fragment' $true
Put $skill 'Read [repository](../../..).'
Expect 'repository directory target is contained' $true
Put 'Specs/contract(v1).md' '# Parenthesized contract'
Put $skill 'Read [contract](../../../Specs/contract(v1).md).'
Expect 'balanced destination parentheses' $true
Put $skill 'The syntax is `[example](missing.md)` or `` `[another](absent.md)` ``.'
Expect 'inline-code pseudo-links ignored' $true
Put $skill '[External](//example.invalid/no-fetch)'
Expect 'protocol-relative external URL ignored' $true
Put $skill ('Read [contract]('+((Join-Path $run 'Specs/contract.md').Replace('\','/'))+').')
Expect 'contained absolute path checked' $true
Put $skill ('Read [contract]('+([uri](Join-Path $run 'Specs/contract.md')).AbsoluteUri+').')
Expect 'contained local file URI checked' $true
Put $skill ('Read [missing]('+((Join-Path $run 'Specs/absent.md').Replace('\','/'))+').')
Expect 'absolute missing local path refused' $false 'Missing or non-repository'
Put $skill ('Read [missing]('+([uri](Join-Path $run 'Specs/absent.md')).AbsoluteUri+').')
Expect 'missing local file URI refused' $false 'Missing or non-repository'
Put $skill "Read [contract][owner].`n[owner]: ../../../Specs/contract%20name.md"
Expect 'reference-style encoded destination' $true
Put $skill "Read [OWNER][].`n[owner]: ../../../Specs/contract(v1).md"
Expect 'collapsed case-insensitive reference' $true
Put $skill "Read [contract][owner].`n[owner]: ../../../Specs/absent.md"
Expect 'missing reference destination refused' $false 'Missing or non-repository'
Put $skill 'Read [contract][undefined].'
Expect 'undefined reference label refused' $false 'Unresolved Markdown reference label'
Put $skill "Read [contract][owner].`n[owner]: ../../../Specs/contract.md`n[OWNER]: ../../../Specs/absent.md"
Expect 'first normalized reference definition wins' $true
Put $skill "Read [owner] [ordinary].`n[owner]: ../../../Specs/contract.md"
Expect 'space separates a shortcut from unrelated bracket text' $true
Put $skill "Read [owner]`t[ordinary].`n[owner]: ../../../Specs/contract.md"
Expect 'tab separates a shortcut from unrelated bracket text' $true
Put $skill "Read [owner]`n[ordinary].`n[owner]: ../../../Specs/contract.md"
Expect 'newline separates a shortcut from unrelated bracket text' $true
Put $skill "Read [owner][owner].`n[owner]: ../../../Specs/contract.md"
Expect 'adjacent labels retain full-reference semantics' $true
Put $skill 'Read [web](https://example.invalid/no-fetch) and [local section](#section).'
Expect 'web and section are not fetched' $true
Put $skill ('```markdown'+"`n[illustrative](missing.md)`n"+'```')
Expect 'illustrative fenced link ignored' $true
Put $skill 'Read [reference](references/working.md).'
Put '.agents/skills/example-skill/references/working.md' 'Read [missing support](absent.md).'
Expect 'broken supporting reference refused' $false
Put '.agents/skills/example-skill/references/working.md' 'Read [contract](../../../../Specs/contract.md).'
Expect 'supporting reference resolved from its directory' $true
Put 'probe.ps1' "[CmdletBinding()]param([string]`$Configuration,[switch]`$Check)`nthrow 'Referenced scripts must never execute.'"
Put $skill ('```powershell'+"`n./probe.ps1 -Configuration Release -Check -Verbose`n"+'```')
Expect 'literal parameter signature without execution' $true
Put 'probe.ps1' "[CmdletBinding(SupportsShouldProcess=`$true)]param([Alias('cfg','c')][string]`$Configuration)`nthrow 'Targets must never execute.'"
Put $skill ('```powershell'+"`n./probe.ps1 -cfg Release -WhatIf -Confirm:`$false`n"+'```')
Expect 'declared alias and ShouldProcess common parameters' $true
Put 'probe.ps1' "[System.Management.Automation.CmdletBindingAttribute(SupportsShouldProcess)]param([System.Management.Automation.AliasAttribute('cfg')][string]`$Configuration)`nthrow 'Targets must never execute.'"
Put $skill ('```powershell'+"`n./probe.ps1 -cfg Release -WhatIf -Confirm:`$false -ea Stop`n"+'```')
Expect 'qualified attributes implicit ShouldProcess and common alias' $true
Put 'probe.ps1' "[CmdletBinding(SupportsShouldProcess=`$false)]param([Alias('cfg')][string]`$Configuration)`nthrow 'Targets must never execute.'"
Expect 'WhatIf refused when ShouldProcess is disabled' $false 'Unknown skill example parameter -WhatIf'
Put $skill ('```powershell'+"`n./probe.ps1 -undeclared Release`n"+'```')
Expect 'undeclared alias refused' $false 'Unknown skill example parameter -undeclared'
Put 'probe.ps1' "throw 'A parameterless target must never execute.'"
Put $skill ('```powershell'+"`n./probe.ps1`n"+'```')
Expect 'parameterless literal script is not executed' $true
Put 'probe.ps1' "[CmdletBinding()]param([string]`$Configuration,[switch]`$Check)`nthrow 'Referenced scripts must never execute.'"
Put $skill ('```powershell'+"`n./probe.ps1 -Unknown Release`n"+'```')
Expect 'unknown parameter refused' $false
Put $skill ('```powershell'+"`n./missing.ps1 -Check`n"+'```')
Expect 'missing script refused' $false
Put $skill ('```text'+"`n./missing.ps1 -Check`n"+'```')
Expect 'non-command illustration ignored' $true
[void][IO.Directory]::CreateDirectory((Join-Path $run '.agents/skills/incomplete'))
Expect 'missing package entrypoint refused' $false
[IO.Directory]::Delete((Join-Path $run '.agents/skills/incomplete'))
# Link targets are synthetic bytes within the same owned fixture. Assert the specific reparse
# refusal before any strict UTF-8 read or script parse; unavailable link privileges are explicit skips.
$entrypoint = Join-Path $run $skill
$sentinel = Join-Path $run 'fixtures/not-readable-as-utf8.dat'
[void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($sentinel))
[IO.File]::WriteAllBytes($sentinel,[byte[]]@(0xff))
[IO.File]::Delete($entrypoint)
$linkCreated = $false
try { [void][IO.File]::CreateSymbolicLink($entrypoint,$sentinel); $linkCreated = $true }
catch [UnauthorizedAccessException] { $skippedLinks += 3; Write-Host 'SKIP reparse controls: symbolic-link privilege unavailable.' }
try {
    if ($linkCreated) { Expect 'reparse SKILL refused before reading' $false 'Reparse' }
} finally { if ($linkCreated) { [IO.File]::Delete($entrypoint) }; Put $skill 'Read the contract.' }
if ($linkCreated) {
    $scriptPath = Join-Path $run 'probe.ps1'
    [IO.File]::Delete($scriptPath)
    [void][IO.File]::CreateSymbolicLink($scriptPath,$sentinel)
    Put $skill ('```powershell'+"`n./probe.ps1`n"+'```')
    try { Expect 'reparse script refused before parsing' $false 'Reparse' }
    finally { [IO.File]::Delete($scriptPath); Put 'probe.ps1' "throw 'Target must never execute.'" }
    Put 'fixtures/docs/contract.md' '# Ordinary target'
    $alias = Join-Path $run 'Specs/mirror'
    [void][IO.Directory]::CreateSymbolicLink($alias,(Join-Path $run 'fixtures/docs'))
    Put $skill 'Read [contract](../../../Specs/mirror/contract.md).'
    try { Expect 'reparse reference ancestor refused' $false 'Reparse' }
    finally { [IO.Directory]::Delete($alias); Put $skill 'Read the contract.' }
}
# Exercise the actual narrow entrypoint in an isolated checkout-shaped fixture, including its
# metadata module and read-only changelog prerequisite. Never run its skill examples.
foreach ($relative in 'validate-skills.ps1','Build/SkillValidation.psm1','Build/SkillPackageIntegrity.psm1','Tools/Test-Changelog.ps1','Tools/Modules/Documentation/Changelog.psm1') {
    $destination = Join-Path $run $relative
    [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($destination))
    [IO.File]::Copy((Join-Path $root $relative),$destination)
}
Put $skill "---`nname: example-skill`ndescription: Validate references.`n---`nRead the contract."
Put 'Changes/README.md' '# Changelog fragments'
Put 'CHANGELOG.md' "# Changes`n`n## Unreleased`n"
$before = Snapshot
$start = [Diagnostics.ProcessStartInfo]::new((Get-Process -Id $PID).Path)
foreach ($argument in '-NoProfile','-NonInteractive','-File',(Join-Path $run 'validate-skills.ps1')) { $start.ArgumentList.Add($argument) }
$start.UseShellExecute = $false; $start.CreateNoWindow = $true
$start.RedirectStandardOutput = $true; $start.RedirectStandardError = $true
$child = [Diagnostics.Process]::new(); $child.StartInfo = $start
$started = $false
try {
    if (-not $child.Start()) { throw 'Could not start the owned skill-validator child.' }
    $started = $true
    $stdout = $child.StandardOutput.ReadToEndAsync(); $stderr = $child.StandardError.ReadToEndAsync()
    if (-not $child.WaitForExit(30000)) { throw 'Owned skill-validator fixture timed out.' }
    $drain = [Threading.Tasks.Task]::WhenAll([Threading.Tasks.Task[]]@($stdout,$stderr))
    if (-not $drain.Wait(5000)) { throw 'Owned skill-validator pipes did not close.' }
    if ($child.ExitCode -ne 0) { throw ('Skill-validator fixture failed: '+$stdout.Result+$stderr.Result) }
    if ((Snapshot) -cne $before) { throw 'The narrow skill-validation entrypoint wrote fixture files.' }
} finally {
    if ($started -and -not $child.HasExited) { $child.Kill($true); [void]$child.WaitForExit(5000) }
    $child.Dispose()
}
Write-Host 'PASS narrow skill entrypoint is read-only with no generated scratch.'
$checks++
Test-RedPrismSkillPackageIntegrity -Root $root
Write-Host "PASS $checks package/signature/read-only controls and the current repository corpus; symbolic-link controls skipped: $skippedLinks. Fixture: $run"
