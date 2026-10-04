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

# Leave room for the executable/options and Windows quoting within CreateProcess's 32,767-character limit.
# A deep worktree has much longer absolute paths than the primary checkout; every file still gets checked.
$batch = [Collections.Generic.List[string]]::new()
$batchCharacters = 0
foreach ($sourceFile in $sourceFiles) {
    $argumentCharacters = $sourceFile.FullName.Length + 3
    if ($argumentCharacters -gt 24000) {
        throw "A source path exceeds the formatter command-line budget: $($sourceFile.FullName)"
    }
    if ($batchCharacters + $argumentCharacters -gt 24000) {
        & $clangFormat @formatArgs -- @($batch.ToArray())
        if ($LASTEXITCODE -ne 0) {
            throw "clang-format failed with exit code $LASTEXITCODE."
        }
        $batch.Clear()
        $batchCharacters = 0
    }
    $batch.Add($sourceFile.FullName)
    $batchCharacters += $argumentCharacters
}
if ($batch.Count -ne 0) {
    & $clangFormat @formatArgs -- @($batch.ToArray())
    if ($LASTEXITCODE -ne 0) {
        throw "clang-format failed with exit code $LASTEXITCODE."
    }
}

if ($Check) {
    Write-Host "Checked $($sourceFiles.Count) files." -ForegroundColor Green
} else {
    Write-Host "Formatted $($sourceFiles.Count) files." -ForegroundColor Green
}
