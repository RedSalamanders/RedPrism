# Short dependency paths retain the full pinned build identity. Windows tools can still reject long paths
# even when Git's checkout supports them; the directory name is not the identity or provenance authority.
Set-StrictMode -Version Latest

function Assert-RedPrismDxUiBuildIdentity {
    param([Parameter(Mandatory)] $BuildIdentity)
    if ($BuildIdentity.Fingerprint -cnotmatch '^[0-9a-f]{64}$' -or -not $BuildIdentity.Identity) {
        throw 'DxUi build identity must contain the complete lowercase SHA-256 fingerprint and identity.'
    }
}

function Assert-RedPrismDxUiOutputIdentity {
    param([Parameter(Mandatory)][string] $OutputRoot, [Parameter(Mandatory)] $BuildIdentity)
    $marker = Join-Path $OutputRoot 'DxUi.build-identity.json'
    $stored = Get-Content -LiteralPath $marker -Raw | ConvertFrom-Json
    Assert-RedPrismDxUiBuildIdentity $stored
    $expectedIdentity = ConvertTo-Json -InputObject $BuildIdentity.Identity -Depth 16 -Compress
    $storedIdentity = ConvertTo-Json -InputObject $stored.Identity -Depth 16 -Compress
    if ($stored.Fingerprint -cne $BuildIdentity.Fingerprint -or $storedIdentity -cne $expectedIdentity) {
        throw "DxUi output identity mismatch or shortened-fingerprint collision: $OutputRoot"
    }
}

function Initialize-RedPrismDxUiOutputRoot {
    [CmdletBinding()]
    param([Parameter(Mandatory)][string] $DependencyRoot, [Parameter(Mandatory)] $BuildIdentity)
    $ErrorActionPreference = 'Stop'
    Assert-RedPrismDxUiBuildIdentity $BuildIdentity
    $output = [IO.Path]::GetFullPath((Join-Path $DependencyRoot $BuildIdentity.Fingerprint.Substring(0, 16)))
    $marker = Join-Path $output 'DxUi.build-identity.json'
    if (Test-Path -LiteralPath $marker -PathType Leaf) {
        Assert-RedPrismDxUiOutputIdentity -OutputRoot $output -BuildIdentity $BuildIdentity
    } else {
        if ((Test-Path -LiteralPath $output) -and @(Get-ChildItem -LiteralPath $output -Force).Count -ne 0) {
            throw "DxUi output already contains unverified data without a build identity: $output"
        }
        [void][IO.Directory]::CreateDirectory($output)
        $bytes = [Text.UTF8Encoding]::new($false).GetBytes((ConvertTo-Json -InputObject $BuildIdentity -Depth 16))
        $stream = [IO.File]::Open($marker, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
        try {
            $stream.Write($bytes, 0, $bytes.Length)
            $stream.Flush($true)
        } finally {
            $stream.Dispose()
        }
    }
    return $output.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
}

function Read-RedPrismDxUiResolvedOutputRoot {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string] $DependencyRoot,
        [Parameter(Mandatory)][ValidateSet('x64', 'ARM64')][string] $Platform,
        [Parameter(Mandatory)] $BuildIdentity
    )
    $ErrorActionPreference = 'Stop'
    Assert-RedPrismDxUiBuildIdentity $BuildIdentity
    $props = [xml][IO.File]::ReadAllText((Join-Path $DependencyRoot "DxUi.resolved.$Platform.props"))
    $nodes = $props.SelectNodes('/Project/PropertyGroup/DxUiConsumerOutputRoot')
    if ($nodes.Count -ne 1) {
        throw 'DxUi resolved props must contain exactly one output root.'
    }
    $literal = $nodes[0].InnerText
    if ([string]::IsNullOrWhiteSpace($literal) -or $literal.Contains('$(') -or $literal.Contains('%(') -or
        $literal -notmatch '^(?:[a-zA-Z]:[\\/]|\\\\[^\\/]+[\\/][^\\/]+)') {
        throw 'DxUi resolved output root must be an absolute literal path.'
    }
    $output = [IO.Path]::GetFullPath($literal).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    $parent = [IO.Path]::GetFullPath($DependencyRoot).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    if ($output -eq $parent -or -not $output.StartsWith($parent, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'DxUi resolved output root escapes the isolated dependency tree.'
    }
    Assert-RedPrismDxUiOutputIdentity -OutputRoot $output -BuildIdentity $BuildIdentity
    return $output
}

Export-ModuleMember -Function Initialize-RedPrismDxUiOutputRoot, Read-RedPrismDxUiResolvedOutputRoot
