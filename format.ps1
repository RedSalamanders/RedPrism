<#!
.SYNOPSIS
Formats the RedPrism C++ sources with clang-format.
#>
[CmdletBinding()]
param(
    [switch] $Check
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSCommandPath
Import-Module (Join-Path $repoRoot 'Build\VisualStudioDiscovery.psm1') -Force
$clangFormat = Find-RedPrismClangFormat

if (-not $clangFormat) {
    throw 'clang-format was not found. Install the Visual Studio C++ Clang tools component (any Visual Studio 2026 edition, not only Insiders) or LLVM.'
}

$formatterVersion = & $clangFormat --version
if ($LASTEXITCODE -ne 0) { throw 'Could not identify the selected clang-format.' }
Write-Host "Formatter: $formatterVersion ($clangFormat)"

$sourceRoots = @('Common', 'RedPrism', 'Tests') | ForEach-Object { Join-Path $repoRoot $_ }
$sourceFiles = Get-ChildItem -LiteralPath $sourceRoots -Recurse -File |
    Where-Object { $_.Extension -in @('.cpp', '.h') } |
    Sort-Object FullName

if ($sourceFiles.Count -eq 0) {
    throw 'No C++ source files were found.'
}

$formatArgs = @('--style=file')
if ($Check) {
    $formatArgs += @('--dry-run', '-Werror')
} else {
    $formatArgs += '-i'
}

# Long checkout roots plus the native test inventory exceed Windows' process command-line
# limit. Bound each invocation by characters, retaining every source in Check and write modes.
$batch = [Collections.Generic.List[string]]::new()
$characters = $clangFormat.Length + 256
function Invoke-FormatBatch {
    if (-not $batch.Count) { return }
    & $clangFormat @formatArgs -- @($batch)
    if ($LASTEXITCODE -ne 0) { throw "clang-format failed with exit code $LASTEXITCODE." }
    $batch.Clear()
}
foreach ($sourceFile in $sourceFiles) {
    $length = $sourceFile.FullName.Length + 3
    if ($characters + $length -gt 14000) {
        Invoke-FormatBatch
        $characters = $clangFormat.Length + 256
    }
    $batch.Add($sourceFile.FullName)
    $characters += $length
}
Invoke-FormatBatch

if ($Check) {
    Write-Host "Checked $($sourceFiles.Count) files." -ForegroundColor Green
} else {
    Write-Host "Formatted $($sourceFiles.Count) files." -ForegroundColor Green
}
