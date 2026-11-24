<#
.SYNOPSIS
Validates RedPrism domain and active plan documents and their local Markdown links.
#>
#Requires -Version 7.4
[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$specRoot = Join-Path $PSScriptRoot 'Specs'
foreach ($index in 'README.md', 'Plans/WIP/README.md', 'Plans/Done/README.md') {
    if (-not (Test-Path -LiteralPath (Join-Path $specRoot $index) -PathType Leaf)) {
        throw "Missing specification index: Specs/$index"
    }
}
$referenceRoots = @('Plans/Done', 'TestRuns', 'Mockups') | ForEach-Object {
    (Join-Path $specRoot $_) + [IO.Path]::DirectorySeparatorChar
}
$documents = @(Get-ChildItem -LiteralPath $specRoot -Recurse -File -Filter '*.md' | Where-Object {
    $path = $_.FullName
    -not ($referenceRoots | Where-Object { $path.StartsWith($_, [StringComparison]::OrdinalIgnoreCase) })
})
foreach ($document in $documents) {
    foreach ($match in [regex]::Matches((Get-Content -LiteralPath $document.FullName -Raw), '\]\(([^)]+)\)')) {
        $relative = $match.Groups[1].Value.Trim('<', '>').Split('#')[0]
        if (-not $relative -or $relative -match '^[a-z][a-z0-9+.-]*:') { continue }
        $target = Join-Path $document.DirectoryName ([Uri]::UnescapeDataString($relative))
        if (-not (Test-Path -LiteralPath $target)) {
            throw "Broken local link in $($document.Name): $relative"
        }
    }
}
Write-Host "Validated local links in $($documents.Count) specification documents."
