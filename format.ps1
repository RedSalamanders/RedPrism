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

& $clangFormat @formatArgs -- @($sourceFiles.FullName)
if ($LASTEXITCODE -ne 0) {
    throw "clang-format failed with exit code $LASTEXITCODE."
}

if ($Check) {
    Write-Host "Checked $($sourceFiles.Count) files." -ForegroundColor Green
} else {
    Write-Host "Formatted $($sourceFiles.Count) files." -ForegroundColor Green
}
