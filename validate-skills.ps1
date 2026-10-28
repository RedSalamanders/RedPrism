<#
.SYNOPSIS
Validates repository-local skill packages, references, literal script signatures and metadata.
.DESCRIPTION
Runs the repository-owned PowerShell 7 validator. Supports the documented YAML subset,
including multiline descriptions; requires no Python, external YAML module or Codex installation.
.OUTPUTS
Validation diagnostics. Fails on any invalid skill.
.NOTES
Reads skills and validates changelog fragments; does not modify files or access the network.
.EXAMPLE
./validate-skills.ps1
#>
#Requires -Version 7.0
[CmdletBinding()]param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$global:LASTEXITCODE = 0
& (Join-Path $PSScriptRoot 'Tools/Test-Changelog.ps1')
if ($LASTEXITCODE -ne 0) { throw 'Changelog validation failed.' }
# Reject package-file and ancestor reparses before the metadata parser opens SKILL.md.
Import-Module (Join-Path $PSScriptRoot 'Build/SkillPackageIntegrity.psm1') -Force
Test-RedPrismSkillPackageIntegrity -Root $PSScriptRoot
Import-Module (Join-Path $PSScriptRoot 'Build/SkillValidation.psm1') -Force
$count = Test-RepositorySkills -Root $PSScriptRoot
Write-Host "Validated $count repository skills." -ForegroundColor Green
$global:LASTEXITCODE = 0
