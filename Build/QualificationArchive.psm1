Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if (-not ('RedPrism.Archives.Store' -as [type])) {
    Add-Type -Path (Join-Path $PSScriptRoot 'QualificationArchive.cs')
}
