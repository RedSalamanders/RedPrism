Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# RedPrism requires Visual Studio 2026 (18.x), toolset v145 (AGENTS.md). Every vswhere lookup in the repo goes
# through this module and this version range, so a host that also has an older Visual Studio (e.g. 2022) installed
# cannot silently resolve MSBuild or clang-format from the wrong major version (RF-707).
New-Variable -Name RedPrismVisualStudioVersionRange -Value '[18.0,19.0)' -Option ReadOnly -Scope Script -Force

function Get-RedPrismVswherePath {
    <#
    .SYNOPSIS
    The path to vswhere.exe (ships with the Visual Studio Installer), or $null when it is not present.
    #>
    [CmdletBinding()]
    param()
    $candidates = @(
        (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'),
        (Join-Path $env:ProgramFiles 'Microsoft Visual Studio\Installer\vswhere.exe')
    ) | Where-Object { $_ -and (Test-Path -LiteralPath $_ -PathType Leaf) }
    return $candidates | Select-Object -First 1
}

function Find-RedPrismVisualStudioInstallationPath {
    <#
    .SYNOPSIS
    The installationPath of the newest installed Visual Studio 2026 (18.x) instance with the C++ workload,
    including prerelease (e.g. Insiders) editions, or $null when none is found.
    #>
    [CmdletBinding()]
    param()
    $vswhere = Get-RedPrismVswherePath
    if (-not $vswhere) {
        return $null
    }
    $path = & $vswhere -latest -prerelease -products '*' -version $RedPrismVisualStudioVersionRange `
        -requires Microsoft.Component.MSBuild -property installationPath 2>$null
    if ($LASTEXITCODE -ne 0 -or -not $path) {
        return $null
    }
    return ($path | Select-Object -First 1).Trim()
}

function Find-RedPrismVisualStudioInstallationOfMSBuild {
    <#
    .SYNOPSIS
    The Visual Studio installation the given MSBuild.exe belongs to: the path before its MSBuild\<version>\Bin directory. That
    is the installation whose default MSVC toolset the build compiles with, whichever of several installed ones
    Find-RedPrismMSBuild picked (MSBUILD_EXE_PATH and PATH win over the search). An MSBuild outside any installation, such as a
    copy, falls back to the newest Visual Studio 2026 (18.x) with MSBuild, like the rest of the repository; $null when there
    is none. Runs no vswhere of its own.
    #>
    [CmdletBinding()]
    param([Parameter(Mandatory)][string] $MSBuildPath)
    $path = [IO.Path]::GetFullPath($MSBuildPath)
    $match = [regex]::Match($path, '^(?<installation>.+?)\\MSBuild\\(?:Current|[0-9]+\.[0-9]+)\\Bin\\', [Text.RegularExpressions.RegexOptions]::IgnoreCase)
    if ($match.Success) {
        return $match.Groups['installation'].Value
    }
    return Find-RedPrismVisualStudioInstallationPath
}

function Find-RedPrismMSBuild {
    <#
    .SYNOPSIS
    Resolves MSBuild.exe: $env:MSBUILD_EXE_PATH, then PATH, then vswhere restricted to Visual Studio 2026 (18.x),
    including prerelease. Throws when none of those find it.
    #>
    [CmdletBinding()]
    param()
    if ($env:MSBUILD_EXE_PATH -and (Test-Path -LiteralPath $env:MSBUILD_EXE_PATH -PathType Leaf)) {
        return $env:MSBUILD_EXE_PATH
    }

    $pathCommand = Get-Command 'msbuild.exe' -ErrorAction SilentlyContinue
    if ($pathCommand) {
        return $pathCommand.Source
    }

    $vswhere = Get-RedPrismVswherePath
    if ($vswhere) {
        $found = & $vswhere -all -prerelease -products '*' -version $RedPrismVisualStudioVersionRange `
            -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\amd64\MSBuild.exe' 2>$null
        $candidate = $found | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
        if ($candidate) {
            return $candidate
        }
    }

    throw 'MSBuild was not found. Install Visual Studio 2026 (18.x) with the Desktop development with C++ workload.'
}

function Find-RedPrismClangFormat {
    <#
    .SYNOPSIS
    Resolves clang-format.exe: PATH, then any installed Visual Studio 2026 (18.x) edition's C++ Clang tools
    component (not only Insiders), then a standalone LLVM install. Returns $null when none is found.
    #>
    [CmdletBinding()]
    param()
    $pathCommand = Get-Command 'clang-format.exe' -ErrorAction SilentlyContinue
    if ($pathCommand) {
        return $pathCommand.Source
    }

    $installationPath = Find-RedPrismVisualStudioInstallationPath
    if ($installationPath) {
        $candidates = @(Get-ChildItem -Path @(
                (Join-Path $installationPath 'VC\Tools\Llvm\x64\bin\clang-format.exe'),
                (Join-Path $installationPath 'VC\Tools\Llvm\bin\clang-format.exe')
            ) -ErrorAction SilentlyContinue | Sort-Object FullName -Descending)
        if ($candidates.Count -gt 0) {
            return $candidates[0].FullName
        }
    }

    $standalone = Join-Path $env:ProgramFiles 'LLVM\bin\clang-format.exe'
    if (Test-Path -LiteralPath $standalone -PathType Leaf) {
        return $standalone
    }

    return $null
}

Export-ModuleMember -Function @(
    'Find-RedPrismVisualStudioInstallationPath',
    'Find-RedPrismVisualStudioInstallationOfMSBuild',
    'Find-RedPrismMSBuild',
    'Find-RedPrismClangFormat'
) -Variable @('RedPrismVisualStudioVersionRange')
