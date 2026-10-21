[CmdletBinding()]param()
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$root=Split-Path (Split-Path $PSScriptRoot)
$listed=@(& (Join-Path $PSScriptRoot 'Invoke-ToolingTests.ps1') -ListScripts)
$names=@($listed|ForEach-Object{Split-Path $_ -Leaf})
foreach($native in @('FailureReportTests.ps1','UiBenchmarkSmokeTests.ps1','UiCancellationBenchmarkSmokeTests.ps1','UiCancellationBenchmarkFullTests.ps1','LayerRowsBenchmarkSmokeTests.ps1','IsaScanTests.ps1','CodeAnalysisTests.ps1')){
    if($native -in $names){throw "Native profile gate is in independent tooling: $native"}
}
if(@($names|Group-Object|Where-Object Count -ne 1).Count){throw 'Independent tooling has duplicate owners.'}
$runner=[IO.File]::ReadAllText((Join-Path $root 'test.ps1'))
foreach($pure in @('UiCancellationBenchmarkSmokeParserTests.ps1','UiCancellationBenchmarkFullParserTests.ps1','GradientBenchmarkRunnerTests.ps1','DxUiProvenanceTests.ps1','DxUiOutputPathTests.ps1','BuildPresentationTests.ps1','BuildPreflightTests.ps1','ToolDiscoveryTests.ps1','VcpkgToolsetTests.ps1','DxUiRestoreTests.ps1','AsanArm64Tests.ps1','WprProfileTests.ps1','RawCorpusTests.ps1','NativeArmValidationTests.ps1')){
    if($pure -notin $names){throw "Independent tooling omits $pure"}
    if($runner.Contains($pure)){throw "Native runner repeats independent tooling: $pure"}
}
foreach($native in @('UiCancellationBenchmarkSmokeTests.ps1','LayerRowsBenchmarkSmokeTests.ps1')){
    if(-not $runner.Contains($native)){throw "Native runner lost P1 gate: $native"}
}
$isa=[IO.File]::ReadAllText((Join-Path $PSScriptRoot 'IsaScanTests.ps1'))
foreach($parser in @($names|Where-Object{$_ -match '^Isa.*Tests\.ps1$'})){
    if($isa.Contains("'"+$parser+"'")){throw "Native compiler gate repeats independent parser controls: $parser"}
}
foreach($required in @('function Invoke-Probe','Invoke-Probe MisplacedAvx2','Invoke-Probe EvexInAvx2','Invoke-RedPrismIsaScan')){
    if(-not $isa.Contains($required)){throw "Native ISA compiler/image gate lost $required"}
}
Write-Host 'PASS tooling routing: independent controls have one owner; P1 benchmark smoke keeps native profile ownership.'
