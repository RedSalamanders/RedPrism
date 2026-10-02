# The exact DxUi source pin, restored beside nothing else: a clone of the library checked out at the locked commit under
# .build\dependencies\DxUi\source\<commit>, with Git long paths and without the directories no build reads. restore-dxui.ps1
# validates and builds from it; this module only populates it, so a test can restore a fixture repository from a deep root.
# Plain ASCII and Windows PowerShell 5.1 compatible, like the script that imports it.
Set-StrictMode -Version Latest

# What the pinned checkout leaves out. A build, Tools/validate_consumer.ps1, Tools/ConsumerBuild.psm1 and vcpkg-install.ps1 need
# everything else (capabilities.json, Tools, Build, src, include, the vcpkg files and the root scripts) and read none of these.
# Measurements (thousands of retained receipts) and docs/gallery (rendered sheets) hold most of the files, and pins before API
# revision 3 kept tracked paths of up to 164 characters there and in Specs (176 on DxUi main for a while): a checkout from a
# deep root could not create them, which left the working tree dirty, and the pin validation then failed. A sparse checkout
# never creates them.
$script:RedPrismDxUiSparsePatterns = @('/*', '!/Measurements/', '!/docs/gallery/', '!/Specs/')

function Get-RedPrismDxUiSparsePatterns {
    <# The sparse-checkout patterns (non-cone mode: everything at the top, minus the directories above). #>
    [CmdletBinding()]
    param()
    return @($script:RedPrismDxUiSparsePatterns)
}

function Restore-RedPrismDxUiSource {
    <# Clones Repository (or CloneFrom, a local checkout that already has Commit, else Repository itself) into Destination and
       checks out Commit detached, sparse and with Git long paths, then records the canonical origin. Does nothing when
       Destination exists: Tools/validate_consumer.ps1 decides whether what is there is the pin and clean. Never touches CloneFrom.
       A restore that fails part way leaves the directory it made, and the next run finds it and keeps it: the failures below
       therefore say to delete it before retrying, and so does restore-dxui.ps1 when the pin validation rejects what is there.

       core.longpaths is given to clone as its own option, so it is stored in the new repository's configuration: the status the
       pin validation runs later needs it as much as the checkout does. A clone from a network URL is blobless, so only the
       files the sparse checkout names are downloaded; a clone from a local checkout copies it, which ignores a filter anyway. #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string] $Repository,
        [Parameter(Mandatory)][ValidatePattern('^[0-9a-f]{40}$')][string] $Commit,
        [Parameter(Mandatory)][string] $Destination,
        [string] $CloneFrom = ''
    )
    $ErrorActionPreference = 'Stop'
    if (Test-Path -LiteralPath $Destination) {
        return $Destination
    }
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Destination) | Out-Null
    $retry = " If '$Destination' was left behind, delete that directory before trying again: a later restore keeps an existing one as it is."
    $cloneSource = if ($CloneFrom) { $CloneFrom } else { "$Repository.git" }
    $cloneArguments = @('clone', '-c', 'core.longpaths=true', '--no-checkout', '--no-hardlinks')
    if (-not (Test-Path -LiteralPath $cloneSource -PathType Container)) {
        $cloneArguments += '--filter=blob:none'
    }
    & git @cloneArguments $cloneSource $Destination
    if ($LASTEXITCODE -ne 0) {
        throw ('DxUi source restore failed. Check Git/network access to the public repository and retry; no custom access token is required.' + $retry)
    }
    & git -C $Destination sparse-checkout set --no-cone @($script:RedPrismDxUiSparsePatterns)
    if ($LASTEXITCODE -ne 0) {
        throw ('The DxUi sparse checkout could not be configured.' + $retry)
    }
    & git -C $Destination -c core.longpaths=true checkout --detach $Commit
    if ($LASTEXITCODE -ne 0) {
        throw ('The exact DxUi source pin could not be checked out.' + $retry)
    }
    & git -C $Destination remote set-url origin "$Repository.git"
    if ($LASTEXITCODE -ne 0) {
        throw ('Could not record the canonical DxUi origin.' + $retry)
    }
    return $Destination
}

Export-ModuleMember -Function Get-RedPrismDxUiSparsePatterns, Restore-RedPrismDxUiSource
