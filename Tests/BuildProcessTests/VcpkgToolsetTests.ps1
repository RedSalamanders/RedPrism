<#
.SYNOPSIS
Proves that vcpkg is pinned to the Visual Studio installation and default MSVC toolset MSBuild compiles RedPrism with
(Build/VcpkgToolset.psm1, vcpkg-install.ps1), for every triplet the installer builds, sanitizer triplets included.

Left alone, vcpkg builds with the newest toolset of the installation. On a machine whose newest toolset beside the default
lacks a compiler for a target (VS 18 Insiders' 14.52 has no x64-hosted ARM64 compiler), a fresh ARM64 restore failed
configuring its first library; compiled libraries from a toolset newer than the one that links them are a link hazard too.

Nothing here installs or builds anything: the fixtures are fake installations and the real triplets are read, never run.
#>
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Import-Module (Join-Path $repo 'Build/VcpkgToolset.psm1') -Force
Import-Module (Join-Path $repo 'Build/VisualStudioDiscovery.psm1') -Force
$fixtureParent = [IO.Path]::GetFullPath((Join-Path $repo '.build/BuildProcessTests')).TrimEnd('\') + '\'
$fixture = Join-Path $fixtureParent ('Vcpkg-' + [guid]::NewGuid().ToString('N'))
[void](New-Item -ItemType Directory -Path $fixture -Force)

function Assert-True([bool] $Condition, [string] $Message) {
    if (-not $Condition) { throw $Message }
}

function Assert-Throws([scriptblock] $Action, [string] $Pattern, [string] $Message) {
    $text = $null
    try { & $Action } catch { $text = $_.Exception.Message }
    Assert-True ($null -ne $text -and $text -match $Pattern) "$Message (got: $text)"
}

function New-FakeInstallation([string] $Name, [byte[]] $VersionFileBytes) {
    $installation = Join-Path $fixture $Name
    $build = Join-Path $installation 'VC\Auxiliary\Build'
    [void](New-Item -ItemType Directory -Path $build -Force)
    if ($null -ne $VersionFileBytes) {
        [IO.File]::WriteAllBytes((Join-Path $build 'Microsoft.VCToolsVersion.default.txt'), $VersionFileBytes)
    }
    return $installation
}

function ConvertTo-Bytes([string] $Text, [bool] $Bom = $false) {
    $encoding = [Text.UTF8Encoding]::new($Bom)
    # The comma keeps an empty result an empty array: a bare empty array leaves a function as nothing, which would make
    # an empty version file indistinguishable from a missing one.
    return , [byte[]](@($encoding.GetPreamble()) + @($encoding.GetBytes($Text)))
}

try {
    # --- The default toolset: the installation's version file, and a clear failure before anything is cloned -------------------
    foreach ($case in @(
            @{ Name = 'lf'; Text = "14.51.36231`n"; Bom = $false },
            @{ Name = 'crlf'; Text = "14.51.36231`r`n"; Bom = $false },
            @{ Name = 'bom'; Text = "14.51.36231`r`n"; Bom = $true },
            @{ Name = 'bare'; Text = '14.51.36231'; Bom = $false },
            @{ Name = 'padded'; Text = "  14.51.36231  `r`n`r`n"; Bom = $false })) {
        $installation = New-FakeInstallation "version-$($case.Name)" (ConvertTo-Bytes $case.Text $case.Bom)
        $toolset = Get-RedPrismDefaultToolset -Installation $installation
        Assert-True ($toolset.Version -ceq '14.51.36231' -and $toolset.MajorMinor -ceq '14.51' -and $toolset.Installation -ceq $installation) "The version file ($($case.Name)) was not read as 14.51.36231."
    }
    Write-Host 'PASS vcpkg-toolset.default-toolset-from-the-version-file'

    $missing = New-FakeInstallation 'version-missing' $null
    Assert-Throws { Get-RedPrismDefaultToolset -Installation $missing } 'Microsoft\.VCToolsVersion\.default\.txt.*does not exist.*Desktop development with C\+\+' 'A missing version file did not fail naming the file and the repair.'
    $empty = New-FakeInstallation 'version-empty' (ConvertTo-Bytes "`r`n")
    Assert-Throws { Get-RedPrismDefaultToolset -Installation $empty } 'is empty.*not a toolset version' 'An empty version file did not fail.'
    $zeroBytes = New-FakeInstallation 'version-zero-bytes' (ConvertTo-Bytes '')
    Assert-True ((Get-Item -LiteralPath (Join-Path $zeroBytes 'VC\Auxiliary\Build\Microsoft.VCToolsVersion.default.txt')).Length -eq 0) 'The zero-byte fixture was not written as a zero-byte file.'
    Assert-Throws { Get-RedPrismDefaultToolset -Installation $zeroBytes } 'is empty.*not a toolset version' 'A zero-byte version file did not fail.'
    foreach ($text in 'garbage', '14.51', '14.51.', 'v145', '14.51.36231 extra', '14.5x.1') {
        $malformed = New-FakeInstallation ('version-malformed-' + [guid]::NewGuid().ToString('N').Substring(0, 6)) (ConvertTo-Bytes $text)
        Assert-Throws { Get-RedPrismDefaultToolset -Installation $malformed } 'not a toolset version such as 14\.51\.36231' "A malformed version file '$text' did not fail."
    }
    Write-Host 'PASS vcpkg-toolset.default-toolset-fails-before-anything-is-cloned'

    # --- The overlay text: the source unchanged, then the pins, in its own line endings -------------------------------------------
    $pinned = [pscustomobject]@{ Installation = 'C:\Program Files\Microsoft Visual Studio\18\Insiders'; MajorMinor = '14.51' }
    $lf = Get-RedPrismVcpkgOverlayTripletText -SourceText "set(VCPKG_TARGET_ARCHITECTURE x64)`nset(VCPKG_CRT_LINKAGE dynamic)`n" -Installation $pinned.Installation -ToolsetVersion $pinned.MajorMinor
    $expectedPins = "set(VCPKG_VISUAL_STUDIO_PATH `"C:\\Program Files\\Microsoft Visual Studio\\18\\Insiders`")`nset(VCPKG_PLATFORM_TOOLSET_VERSION `"14.51`")`n"
    Assert-True ($lf.StartsWith("set(VCPKG_TARGET_ARCHITECTURE x64)`nset(VCPKG_CRT_LINKAGE dynamic)`n`n# RedPrism:") -and $lf.EndsWith($expectedPins) -and -not $lf.Contains("`r")) "The LF overlay text is not the source, a blank line, a comment and the two pins: $lf"
    $crlf = Get-RedPrismVcpkgOverlayTripletText -SourceText "set(VCPKG_TARGET_ARCHITECTURE arm64)`r`nset(VCPKG_CRT_LINKAGE dynamic)`r`n" -Installation $pinned.Installation -ToolsetVersion $pinned.MajorMinor
    Assert-True (($crlf -replace "`r`n", '') -notmatch "[`r`n]" -and $crlf.EndsWith($expectedPins.Replace("`n", "`r`n"))) 'The CRLF source did not keep its line endings through the pins.'
    $noNewline = Get-RedPrismVcpkgOverlayTripletText -SourceText 'set(VCPKG_CRT_LINKAGE dynamic)' -Installation $pinned.Installation -ToolsetVersion $pinned.MajorMinor
    Assert-True ($noNewline.StartsWith("set(VCPKG_CRT_LINKAGE dynamic)`n`n# RedPrism:")) 'A source without a final newline did not get one before the pins.'
    $tricky = Get-RedPrismVcpkgOverlayTripletText -SourceText '' -Installation 'D:\a "b" $c\VS' -ToolsetVersion '14.51'
    Assert-True ($tricky.Contains('set(VCPKG_VISUAL_STUDIO_PATH "D:\\a \"b\" \$c\\VS")')) "A backslash, a quote and a dollar sign were not escaped for CMake: $tricky"
    Assert-Throws { Get-RedPrismVcpkgOverlayTripletText -SourceText '' -Installation 'C:\VS' -ToolsetVersion '14.51.36231' } 'ToolsetVersion' 'A full toolset version was accepted where the pin is major.minor.'
    Write-Host 'PASS vcpkg-toolset.overlay-text'

    # --- Writing the overlay: only when the bytes change, and a file the triplet names beside itself comes along ------------------
    $source = Join-Path $fixture 'stock\x64-windows.cmake'
    [void](New-Item -ItemType Directory -Path (Split-Path -Parent $source) -Force)
    [IO.File]::WriteAllText($source, "set(VCPKG_TARGET_ARCHITECTURE x64)`nset(VCPKG_CRT_LINKAGE dynamic)`n", [Text.UTF8Encoding]::new($false))
    $toolset = Get-RedPrismDefaultToolset -Installation (New-FakeInstallation 'writer' (ConvertTo-Bytes "14.51.36231`r`n"))
    $output = Join-Path $fixture 'overlay'
    $first = Update-RedPrismVcpkgOverlayTriplet -SourceTripletPath $source -Toolset $toolset -OutputDirectory $output
    Assert-True ($first.Changed -and $first.Triplet -ceq 'x64-windows' -and $first.Directory -ceq $output -and $first.Path -ceq (Join-Path $output 'x64-windows.cmake') -and $first.Companions.Count -eq 0) 'The first write did not report the overlay it wrote.'
    $stamp = [IO.File]::GetLastWriteTimeUtc($first.Path)
    Start-Sleep -Milliseconds 50
    $second = Update-RedPrismVcpkgOverlayTriplet -SourceTripletPath $source -Toolset $toolset -OutputDirectory $output
    Assert-True (-not $second.Changed -and [IO.File]::GetLastWriteTimeUtc($first.Path) -eq $stamp) 'An unchanged restore rewrote the overlay or its timestamp.'
    $newer = [pscustomobject]@{ Installation = $toolset.Installation; MajorMinor = '14.52' }
    $third = Update-RedPrismVcpkgOverlayTriplet -SourceTripletPath $source -Toolset $newer -OutputDirectory $output
    Assert-True ($third.Changed -and [IO.File]::ReadAllText($first.Path).Contains('"14.52"')) 'A changed toolset did not rewrite the overlay.'
    Assert-Throws { Update-RedPrismVcpkgOverlayTriplet -SourceTripletPath (Join-Path $fixture 'stock\nope.cmake') -Toolset $toolset -OutputDirectory $output } 'does not exist' 'A missing source triplet did not fail.'
    Write-Host 'PASS vcpkg-toolset.overlay-written-only-when-changed'

    $companionSource = Join-Path $fixture 'asan\x64-windows-asan.cmake'
    [void](New-Item -ItemType Directory -Path (Split-Path -Parent $companionSource) -Force)
    [IO.File]::WriteAllText($companionSource, "set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE `"`${CMAKE_CURRENT_LIST_DIR}/asan-toolchain.cmake`")`n", [Text.UTF8Encoding]::new($false))
    Assert-Throws { Update-RedPrismVcpkgOverlayTriplet -SourceTripletPath $companionSource -Toolset $toolset -OutputDirectory (Join-Path $fixture 'overlay2') } 'asan-toolchain\.cmake.*does not exist' 'A chainloaded file that is not beside the source triplet did not fail.'
    [IO.File]::WriteAllText((Join-Path $fixture 'asan\asan-toolchain.cmake'), "# the toolchain`n", [Text.UTF8Encoding]::new($false))
    $withCompanion = Update-RedPrismVcpkgOverlayTriplet -SourceTripletPath $companionSource -Toolset $toolset -OutputDirectory (Join-Path $fixture 'overlay2')
    Assert-True ($withCompanion.Companions.Count -eq 1 -and [IO.File]::ReadAllText($withCompanion.Companions[0]) -ceq "# the toolchain`n" -and (Split-Path -Parent $withCompanion.Companions[0]) -ceq $withCompanion.Directory) 'The chainloaded toolchain was not copied beside the overlay.'
    $again = Update-RedPrismVcpkgOverlayTriplet -SourceTripletPath $companionSource -Toolset $toolset -OutputDirectory (Join-Path $fixture 'overlay2')
    Assert-True (-not $again.Changed) 'An unchanged companion made the restore report a change.'
    Write-Host 'PASS vcpkg-toolset.chainloaded-file-comes-along'

    # --- The installation: the one the MSBuild in use belongs to ---------------------------------------------------------------
    $insiders = 'C:\Program Files\Microsoft Visual Studio\18\Insiders'
    foreach ($msbuild in "$insiders\MSBuild\Current\Bin\amd64\MSBuild.exe", "$insiders\MSBuild\Current\Bin\MSBuild.exe", "$insiders\MSBuild\Current\Bin\arm64\MSBuild.exe", ($insiders.ToLowerInvariant() + '\msbuild\current\bin\msbuild.exe'), "$insiders\MSBuild\Current\Bin\..\Bin\amd64\MSBuild.exe") {
        $found = Find-RedPrismVisualStudioInstallationOfMSBuild -MSBuildPath $msbuild
        Assert-True ($found -ieq $insiders) "The installation of '$msbuild' was '$found', not '$insiders'."
    }
    $other = 'D:\Tools\VS 2026 Preview'
    Assert-True ((Find-RedPrismVisualStudioInstallationOfMSBuild -MSBuildPath "$other\MSBuild\Current\Bin\amd64\MSBuild.exe") -ieq $other) 'A second installation was not told apart from the newest one.'
    Write-Host 'PASS vcpkg-toolset.installation-of-the-msbuild-in-use'

    # --- The real triplets: every one the installer builds is pinned, and the sanitizer ones keep their toolchain ------------------
    $installer = Get-Content -LiteralPath (Join-Path $repo 'vcpkg-install.ps1') -Raw
    $buildScript = Get-Content -LiteralPath (Join-Path $repo 'build.ps1') -Raw
    Assert-True ($installer -match 'Find-RedPrismVisualStudioInstallationOfMSBuild' -and $installer -match 'Get-RedPrismDefaultToolset' -and $installer -match 'Update-RedPrismVcpkgOverlayTriplet') 'vcpkg-install.ps1 does not pin vcpkg through Build/VcpkgToolset.psm1 and the installation of the MSBuild in use.'
    Assert-True ($installer -notmatch 'vswhere') 'vcpkg-install.ps1 runs its own vswhere lookup; discovery belongs to Build/VisualStudioDiscovery.psm1 (RF-707).'
    Assert-True ($buildScript -match 'vcpkg-install\.ps1''\) -Platform \$Platform -Asan:\(\$Configuration -eq ''ASan Debug''\) -MSBuildPath \$msbuild') 'build.ps1 does not hand the installer the MSBuild it found.'
    $toolsetIndex = $installer.IndexOf('$toolset = Get-RedPrismDefaultToolset -Installation')
    $cloneIndex = $installer.IndexOf('& git.exe clone')
    Assert-True ($toolsetIndex -ge 0 -and $cloneIndex -gt $toolsetIndex) 'vcpkg-install.ps1 reads the default toolset after it clones: a broken installation must fail before anything is cloned.'
    Assert-True ($installer -match 'core\.longpaths=true') 'vcpkg-install.ps1 clones the managed vcpkg checkout without Git long paths.'
    Assert-True ($installer -notmatch '--overlay-triplets=\$overlayTriplets' -and $installer -match '--overlay-triplets=\$overlayDirectory') 'vcpkg-install.ps1 still takes the unpinned triplets as its overlay.'
    foreach ($triplet in 'x64-windows', 'arm64-windows', 'x64-windows-asan', 'arm64-windows-asan') {
        Assert-True ($installer.Contains("'$triplet'")) "vcpkg-install.ps1 does not pin the $triplet triplet."
    }

    $realToolset = [pscustomobject]@{ Installation = $insiders; MajorMinor = '14.51' }
    foreach ($triplet in 'x64-windows-asan', 'arm64-windows-asan') {
        $overlay = Update-RedPrismVcpkgOverlayTriplet -SourceTripletPath (Join-Path $repo "Build/vcpkg-triplets/$triplet.cmake") -Toolset $realToolset -OutputDirectory (Join-Path $fixture "real-$triplet")
        $text = [IO.File]::ReadAllText($overlay.Path)
        Assert-True ($text.Contains('/fsanitize=address') -and $text.Contains('VCPKG_PLATFORM_TOOLSET_VERSION "14.51"') -and $text.Contains('VCPKG_VISUAL_STUDIO_PATH')) "The $triplet overlay lost the sanitizer settings or the pins."
        Assert-True ($overlay.Companions.Count -eq 1 -and (Split-Path -Leaf $overlay.Companions[0]) -ceq 'asan-toolchain.cmake') "The $triplet overlay did not bring asan-toolchain.cmake."
        Assert-True ([IO.File]::ReadAllText($overlay.Companions[0]) -ceq [IO.File]::ReadAllText((Join-Path $repo 'Build/vcpkg-triplets/asan-toolchain.cmake'))) 'The copied asan-toolchain.cmake is not the repository one.'
    }
    Write-Host 'PASS vcpkg-toolset.installer-pins-every-triplet'

    # --- The same behavior as DxUi's own pin, whenever that pin is restored. This repository's copy exists only so that
    # vcpkg-install.ps1 needs neither the restore nor the pin; a drift fails here. Only what the pinned capabilities.json lists in
    # its consumerInterface is called: Get-DxUiDefaultToolset and Update-DxUiVcpkgOverlayTriplet.
    $lock = Get-Content -Raw -LiteralPath (Join-Path $repo 'Dependencies/DxUi.lock.json') | ConvertFrom-Json
    $dxuiRoot = Join-Path $repo ".build/dependencies/DxUi/source/$($lock.commit)"
    $dxuiTools = Join-Path $dxuiRoot 'Tools'
    if ((Test-Path -LiteralPath (Join-Path $dxuiTools 'VcpkgTriplet.psm1')) -and (Test-Path -LiteralPath (Join-Path $dxuiTools 'VisualStudio.psm1'))) {
        $dxuiModules = @(Import-Module (Join-Path $dxuiTools 'VisualStudio.psm1') -Force -PassThru) + @(Import-Module (Join-Path $dxuiTools 'VcpkgTriplet.psm1') -Force -PassThru)
        try {
            $listed = (Get-Content -Raw -LiteralPath (Join-Path $dxuiRoot 'capabilities.json') | ConvertFrom-Json).consumerInterface.modules
            $toolsetParameters = @($listed.'Tools/VisualStudio.psm1'.'Get-DxUiDefaultToolset')
            $overlayParameters = @($listed.'Tools/VcpkgTriplet.psm1'.'Update-DxUiVcpkgOverlayTriplet')
            Assert-True (($toolsetParameters -contains 'Installation') -and ($overlayParameters -contains 'StockTripletPath') -and
                ($overlayParameters -contains 'Toolset') -and ($overlayParameters -contains 'OutputDirectory')) 'The pinned consumerInterface no longer lists the DxUi functions and parameters this test calls; call only what it lists.'
            $dxuiToolset = Get-DxUiDefaultToolset -Installation (New-FakeInstallation 'parity-overlay' (ConvertTo-Bytes "14.51.36231`r`n"))
            $sources = @(
                "set(VCPKG_TARGET_ARCHITECTURE x64)`nset(VCPKG_CRT_LINKAGE dynamic)`n",
                "set(VCPKG_TARGET_ARCHITECTURE arm64)`r`nset(VCPKG_CRT_LINKAGE dynamic)`r`n",
                'set(VCPKG_CRT_LINKAGE dynamic)',
                '',
                [IO.File]::ReadAllText((Join-Path $repo 'Build/vcpkg-triplets/x64-windows-asan.cmake')))
            $caseNumber = 0
            foreach ($source in $sources) {
                $caseNumber++
                $stock = Join-Path $fixture "parity-stock\case$caseNumber.cmake"
                [void](New-Item -ItemType Directory -Path (Split-Path -Parent $stock) -Force)
                [IO.File]::WriteAllBytes($stock, [Text.UTF8Encoding]::new($false).GetBytes($source))
                $written = Update-DxUiVcpkgOverlayTriplet -StockTripletPath $stock -Toolset $dxuiToolset -OutputDirectory (Join-Path $fixture 'parity-dxui')
                $theirs = [IO.File]::ReadAllText($written.Path)
                $ours = Get-RedPrismVcpkgOverlayTripletText -SourceText $source -Installation $dxuiToolset.Installation -ToolsetVersion $dxuiToolset.MajorMinor
                Assert-True (($ours -replace '# RedPrism: ', '# DxUi: ') -ceq $theirs) "The overlay text differs from DxUi's for source '$($source.Substring(0, [Math]::Min(40, $source.Length)))'."
            }
            foreach ($text in "14.51.36231`r`n", "14.51.36231", "  14.51.36231  `n", "garbage", "", "14.51") {
                $installation = New-FakeInstallation ('parity-' + [guid]::NewGuid().ToString('N').Substring(0, 8)) (ConvertTo-Bytes $text)
                $oursError = $null; $theirsError = $null; $ours = $null; $theirs = $null
                try { $ours = Get-RedPrismDefaultToolset -Installation $installation } catch { $oursError = $_.Exception.Message }
                try { $theirs = Get-DxUiDefaultToolset -Installation $installation } catch { $theirsError = $_.Exception.Message }
                Assert-True (($null -eq $oursError) -eq ($null -eq $theirsError)) "The default toolset of '$text' is accepted by one of the two implementations only."
                if ($null -eq $oursError) {
                    Assert-True ($ours.Version -ceq $theirs.Version -and $ours.MajorMinor -ceq $theirs.MajorMinor) "The default toolset of '$text' differs from DxUi's."
                } else {
                    Assert-True ($oursError -ceq $theirsError) "The failure for '$text' differs from DxUi's: '$oursError' against '$theirsError'."
                }
            }
            Write-Host 'PASS vcpkg-toolset.same-behavior-as-the-dxui-pin'
        } finally {
            $dxuiModules | Remove-Module -Force -ErrorAction SilentlyContinue
        }
    } else {
        Write-Host 'SKIP vcpkg-toolset.same-behavior-as-the-dxui-pin (the DxUi pin is not restored)' -ForegroundColor Yellow
    }
} finally {
    $resolved = [IO.Path]::GetFullPath($fixture)
    if (-not $resolved.StartsWith($fixtureParent, [StringComparison]::OrdinalIgnoreCase) -or (Split-Path -Leaf $resolved) -notlike 'Vcpkg-*') {
        throw "Unsafe fixture cleanup path: $resolved"
    }
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
exit 0
