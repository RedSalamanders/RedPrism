<#
.SYNOPSIS
Preserves shared changelog behavior coverage in independent tooling, outside the read-only skill checker.
.DESCRIPTION
Supplies a unique repository-local .build scratch directory to the shared fixture runner. The runner owns
its synthetic children and never reads or changes the application's actual changelog or fragments.
#>
#Requires -Version 7.0
[CmdletBinding()]param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = Split-Path (Split-Path $PSScriptRoot)
$scratch = Join-Path $root ('.build/changelog-fixtures/'+[guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory($scratch)
& (Join-Path $root 'Tools/Test-ChangelogWorkflow.ps1') -ScratchRoot $scratch
