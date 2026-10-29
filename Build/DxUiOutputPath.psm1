# Compatibility names retain the reviewed RedPrism caller surface.
Import-Module (Join-Path $PSScriptRoot 'DxUiPaths.psm1') -Scope Local -ErrorAction Stop
function Initialize-RedPrismDxUiOutputRoot {
    [CmdletBinding()]param([Parameter(Mandatory)][string]$DependencyRoot,[Parameter(Mandatory)]$BuildIdentity)
    Initialize-DxUiOutputRoot -DependencyRoot $DependencyRoot -BuildIdentity $BuildIdentity
}
function Read-RedPrismDxUiResolvedOutputRoot {
    [CmdletBinding()]param([Parameter(Mandatory)][string]$DependencyRoot,[Parameter(Mandatory)][ValidateSet('x64','ARM64')][string]$Platform,[Parameter(Mandatory)]$BuildIdentity)
    Read-DxUiResolvedOutputRoot -DependencyRoot $DependencyRoot -Platform $Platform -BuildIdentity $BuildIdentity
}
Export-ModuleMember -Function Initialize-RedPrismDxUiOutputRoot,Read-RedPrismDxUiResolvedOutputRoot