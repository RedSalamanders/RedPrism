# Product data and entrypoint contracts. Generic planner, runner, and receipt controls live in the pinned engine.
[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$root = Split-Path $PSScriptRoot
$engine = Join-Path $root '.ci-engine'
$pwsh = (Get-Process -Id $PID).Path
$fixture = Join-Path $root ('.build/ToolTests/scoped-' + [guid]::NewGuid().ToString('N'))
Import-Module (Join-Path $root 'Build/ScopedTesting.psm1') -Force
Import-Module (Join-Path $engine 'Tools/Modules/Ci.Policy.psm1')
$manifest = Read-ScopedTestManifest -Root $root
$passed = 0

function Assert([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

function Case([string]$Name, [scriptblock]$Body) {
    & $Body
    $script:passed++
    Write-Host "PASS $Name"
}

function Put([string]$Path, [string]$Content) {
    [void][IO.Directory]::CreateDirectory((Split-Path $Path))
    [IO.File]::WriteAllText($Path, $Content)
}

function Select-Scopes([string]$Path, [string]$Configuration = 'ASan Debug') {
    return (Get-ScopedTestPlan -Manifest $manifest -ChangedPaths @($Path) -Configuration $Configuration).scopes
}

try {
    Case 'native inventory is contextual and complete' {
        Assert ((Assert-ScopedTestNames -Root $root -Manifest $manifest) -gt 0) 'Empty native source inventory'
        $paths = Get-Content (Join-Path $root $manifest.nativeInventory.path) -Raw | ConvertFrom-Json
        Assert (-not @($paths | Where-Object { $_ -match '(^|/)Scope\.Tests\.' }).Count) 'Placeholder test scope'
    }
    Case 'product rules preserve source owners and widen on unknown compile inputs' {
        foreach ($path in 'Build/SkillPackageIntegrity.psm1','Build/SkillValidation.psm1') {
            Assert ((Select-Scopes $path) -join ',' -ceq 'Tooling') "Expected only Tooling for $path"
        }
        foreach ($path in 'Build/ScopedTesting.psm1','Build/Unknown.psm1','Unmapped/New.cpp','res/Welcome.md') {
            Assert ((Get-ScopedTestPlan -Manifest $manifest -ChangedPaths @($path)).full) "Expected conservative full coverage for $path"
        }
        Assert ((Select-Scopes 'README.md') -join ',' -ceq 'Tooling,Format') 'Project prose selected native tests'
        $jobs = Select-Scopes 'Common/Jobs.cpp'
        foreach ($scope in 'Host','Performance','Captures') {
            Assert ($scope -in $jobs) "Jobs source lost $scope"
        }
        $hostHeader = Get-Content (Join-Path $root $manifest.nativeInventory.path) -Raw | ConvertFrom-Json |
            Where-Object { $_ -like 'Tests/HostTests/*.h' } | Select-Object -First 1
        Assert ((Select-Scopes $hostHeader) -join ',' -ceq 'Host,Stress,Format') 'Standalone Host helper selected unrelated suites'
        Assert ((Select-Scopes $hostHeader Release) -join ',' -ceq 'Host,Format') 'Stress ran in Release'
    }
    Case 'clipboard suites remain separate in affected and full plans' {
        Assert (-not @($manifest.scopes | Where-Object { $_.name -in @('IO','Runtime') }).Count) 'Scoped testing admitted a clipboard suite'
        foreach ($path in 'Tests/IoTests/IO.Tests.Runner.cpp','RedPrism/Application.cpp','NewNativeInput.inl') {
            $manual = @(Get-ScopedInteractiveObligations -Manifest $manifest -ChangedPaths @($path))
            Assert ($manual.suite -join ',' -ceq 'IO,Runtime') "Clipboard obligation omitted for $path"
        }
        Assert ((@(Get-ScopedInteractiveObligations -Manifest $manifest -Full).suite -join ',') -ceq 'IO,Runtime') 'Full omitted the clipboard obligations'
        $entrypoint = [IO.File]::ReadAllText((Join-Path $root 'test.ps1'))
        Assert ($entrypoint.Contains("Scope = 'IO'") -and $entrypoint.Contains("-in " + [char]36 + 'Suites') -and
            $entrypoint.Contains('Invoke-CiInteractiveDesktopOperation -RequiresInteractiveDesktop $true')) 'Direct native test runner lost a desktop gate'
    }
    Case 'PrePush profiles and hosted native gates retain product ownership' {
        $profiles = @($manifest.prePushProfiles | ForEach-Object { "$($_.platform)/$($_.configuration)/$($_.buildOnly)" })
        Assert (($profiles -join '|') -ceq 'x64/Debug/False|x64/Release/False|x64/ASan Debug/False|ARM64/Debug/True|ARM64/Release/True|ARM64/ASan Debug/True') 'Local PrePush profile matrix drifted'
        Import-Module (Join-Path $engine 'Tools/Modules/Ci.Project.psm1')
        Test-CiProjectConfiguration @{root=$root; engineRoot=$engine; project=(Read-CiProject $root); development=$false}
        $adapter = [IO.File]::ReadAllText((Join-Path $root 'Tools/Invoke-ProjectCi.ps1'))
        Assert ($adapter.Contains('Invoke-ToolingTests.ps1')) 'Hosted independent tooling owner missing'
        Assert ($adapter.Contains('-Full')) 'Hosted regression lost the full native runner'
        $native = [IO.File]::ReadAllText((Join-Path $engine '.github/workflows/native.yml'))
        Assert ($native.Contains('platform: x64') -and $native.Contains('platform: ARM64')) 'Both native architectures are required'
        Assert ($native.Contains('needs: [admission, test-gate]')) 'Production must wait for native regression'
    }

    $product = Join-Path $fixture 'product'
    Put (Join-Path $product '.gitignore') ".build/`n.ci-engine/`n"
    & git -C $product init --quiet --initial-branch=main
    if ($LASTEXITCODE) { throw 'Cannot initialize scoped testing fixture.' }
    Put (Join-Path $product '.ci/project.json') ([IO.File]::ReadAllText((Join-Path $root '.ci/project.json')))
    $fixtureManifest = Get-Content (Join-Path $root 'Tests/test-scopes.json') -Raw | ConvertFrom-Json -AsHashtable
    $fixtureManifest.Remove('projectBindings')
    Put (Join-Path $product 'Tests/test-scopes.json') ($fixtureManifest | ConvertTo-Json -Depth 12)
    Put (Join-Path $product 'runtime.cpp') 'synthetic compiled input'
    $engineCopy = Join-Path $product '.ci-engine'
    & git clone --quiet --shared $engine $engineCopy
    if ($LASTEXITCODE) { throw 'Cannot clone the owned local engine fixture.' }
    & git -C $engineCopy checkout --quiet --detach (Read-CiProject $root).engine.ref
    if ($LASTEXITCODE) { throw 'Cannot bind fixture to the exact engine pin.' }

    Case 'format and native evidence bind the ignored engine policy bytes' {
        $source = Get-ScopedSourceIdentity -Root $product
        $compiled = Get-ScopedSourceIdentity -Root $product -CompiledOnly
        $map = Get-ScopedMapIdentity (Get-ScopedSourceMap -Root $product)
        if ($IsWindows) {
            Assert ((Get-ScopedSourceIdentity -Root $product.ToUpperInvariant()) -ceq $source) 'Windows path casing changed tooling evidence'
            Assert ((Get-ScopedSourceIdentity -Root $product.ToUpperInvariant() -CompiledOnly) -ceq $compiled) 'Windows path casing changed native evidence'
        }
        Put (Join-Path $engineCopy '.build/fixture-output.json') '{"generated":true}'
        Assert ((Get-ScopedSourceIdentity -Root $product) -ceq $source) 'Generated engine output changed tooling evidence'
        $policy = Join-Path $engineCopy 'Policy/clang-format.json'
        $bytes = [IO.File]::ReadAllBytes($policy)
        try {
            [IO.File]::AppendAllText($policy, "`n ")
            foreach ($nativeOnly in $false,$true) {
                $refused = $false
                try { [void](Get-ScopedSourceIdentity -Root $product -CompiledOnly:$nativeOnly) }
                catch { $refused = $_.Exception.Message -match 'must be clean' }
                Assert $refused 'Modified pinned engine retained reusable evidence'
            }
            Assert ((Get-ScopedMapIdentity (Get-ScopedSourceMap -Root $product)) -ceq $map) 'Engine state changed product iteration map'
        } finally { [IO.File]::WriteAllBytes($policy, $bytes) }
        Assert ((Get-ScopedSourceIdentity -Root $product) -ceq $source) 'Restored policy failed to recover tooling evidence'
        Assert ((Get-ScopedSourceIdentity -Root $product -CompiledOnly) -ceq $compiled) 'Restored policy failed to recover native evidence'
    }
    Case 'packaging and tests require different roles and deployed DxUi evidence' {
        $outputs = Join-Path $product '.build/x64/Release'
        $provenance = Join-Path $outputs 'DxUi.provenance.json'
        $exe = Join-Path $outputs 'RedPrism.exe'
        Put $exe 'synthetic executable'
        $refused = $false
        try { [void](Get-ScopedArtifactIdentity -Root $product -Platform x64 -Configuration Release) }
        catch { $refused = $_.Exception.Message -match 'Missing deployed runtime input: DxUi.provenance.json' }
        Assert $refused 'Artifact identity accepted a missing deployed dependency receipt'
        Put $provenance '{"source":"exact bytes"}'
        $build = @{Root=$product; Platform='x64'; Configuration='Release'; BuildNumber=17}
        $attested = Join-Path $outputs 'build-attestation.json'
        $normal = Get-ScopedBuildAttestationKey @build
        $production = Get-ScopedBuildAttestationKey @build -EnableTests $false
        Write-ScopedReceipt -Path $attested -Identity $production
        Assert (Test-ScopedReceipt -Path $attested -Identity $production) 'Production build cannot serve its own receipt'
        Assert (-not (Test-ScopedReceipt -Path $attested -Identity $normal)) 'Production build qualified native tests'
        Write-ScopedReceipt -Path $attested -Identity $normal
        Assert (-not (Test-ScopedReceipt -Path $attested -Identity $production)) 'Test-enabled build qualified a package'
        Put $provenance '{"source":"changed bytes"}'
        Assert ((Get-ScopedBuildAttestationKey @build) -cne $normal) 'Changed DxUi provenance retained the build identity'
    }
    Case 'direct native refusal revokes iteration and selected scoped receipts before any native work' {
        Put (Join-Path $product 'test.ps1') ([IO.File]::ReadAllText((Join-Path $root 'test.ps1')))
        Put (Join-Path $product 'Build/ScopedTesting.psm1') ([IO.File]::ReadAllText((Join-Path $root 'Build/ScopedTesting.psm1')))
        $platform = 'x64'
        $reports = Get-ScopedReportRoot -Root $product -Platform $platform -Configuration Debug
        $iteration = Join-Path $reports 'iteration.json'
        $selected = Join-Path $reports 'Document.json'
        $other = Join-Path $reports 'Composition.json'
        foreach ($output in $iteration,$selected,$other) { Put $output '{"fixture":"old successful evidence"}' }
        $log = Join-Path $fixture 'direct-refusal.log'
        & $pwsh -NoProfile -NonInteractive -File (Join-Path $product 'test.ps1') -Platform $platform -Suites Document -SelfTestTimeoutSeconds 1201 *> $log
        Assert ($LASTEXITCODE -ne 0 -and (Get-Content $log -Raw).Contains('An extended self-test failure bound requires native ARM64.')) 'Direct test did not reach the expected refusal'
        Assert (-not ([IO.File]::Exists($iteration) -or [IO.File]::Exists($selected))) 'Direct failure retained reusable selected evidence'
        Assert ([IO.File]::Exists($other)) 'Refusal also revoked the unrelated scoped receipt'
    }
    Case 'hidden Win32 fixture keeps its exact canvas without changing display settings' {
        $start=[Diagnostics.ProcessStartInfo]::new((Get-Process -Id $PID).Path)
        $start.UseShellExecute=$false;$start.CreateNoWindow=$true
        $start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true
        foreach($argument in @('-NoProfile','-NonInteractive','-File',(Join-Path $root 'Tests/Tooling.Tests.WindowFixture.ps1'),'-ReportPath',(Join-Path $fixture 'window-fixture.json'))){[void]$start.ArgumentList.Add($argument)}
        $process=[Diagnostics.Process]::Start($start)
        try{
            if(-not $process.WaitForExit(30000)){$process.Kill($true);$process.WaitForExit();throw 'Hidden window fixture timed out.'}
            $output=$process.StandardOutput.ReadToEnd()+$process.StandardError.ReadToEnd()
            Assert ($process.ExitCode -eq 0) ("Hidden window fixture failed: "+$output)
            $record=Get-Content -LiteralPath (Join-Path $fixture 'window-fixture.json') -Raw|ConvertFrom-Json
            Assert ($record.passed -and $record.hidden -and -not $record.displaySettingsChanged -and $record.dpi -gt 0 -and $record.clientWidth -gt 0 -and $record.clientHeight -gt 0) 'Incomplete hidden-window evidence'
        }finally{$process.Dispose()}
    }
    Write-Host "Scoped testing: $passed product cases passed."
} finally {
    $owned = Assert-CiContainedPath -Root (Join-Path $root '.build/ToolTests') -Path $fixture
    if (Test-Path -LiteralPath $owned) { Remove-Item -LiteralPath $owned -Recurse -Force }
}
exit 0
