Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-RedPrismEnvironmentMap {
    $environment = @{}
    foreach ($entry in Get-ChildItem Env:) {
        $environment[$entry.Name] = $entry.Value
    }
    return $environment
}

function Test-RedPrismInteractiveTerminal {
    [CmdletBinding()]
    param(
        [bool] $IsOutputRedirected = [Console]::IsOutputRedirected,
        [bool] $IsErrorRedirected = [Console]::IsErrorRedirected,
        [bool] $HasRawUi = ($null -ne $Host -and $null -ne $Host.UI -and $null -ne $Host.UI.RawUI),
        [bool] $CanReadWindowTitle = $true,
        [hashtable] $Environment = @{}
    )

    if ($Environment.Count -eq 0) {
        $Environment = Get-RedPrismEnvironmentMap
    }
    if (-not $HasRawUi -or -not $CanReadWindowTitle) {
        return $false
    }

    if (-not $IsOutputRedirected -and -not $IsErrorRedirected) {
        return $true
    }

    foreach ($marker in @('CODEX_SHELL', 'WT_SESSION', 'TERM_PROGRAM', 'VSCODE_PID', 'ConEmuPID', 'ANSICON')) {
        if ($Environment.ContainsKey($marker) -and
            -not [string]::IsNullOrWhiteSpace([string] $Environment[$marker])) {
            return $true
        }
    }

    return $false
}

function Test-RedPrismTrueColorHost {
    [CmdletBinding()]
    param(
        [bool] $UseColor = $true,
        [hashtable] $Environment = @{}
    )

    if (-not $UseColor) {
        return $false
    }
    if ($Environment.Count -eq 0) {
        $Environment = Get-RedPrismEnvironmentMap
    }

    $term = [string] $Environment['TERM']
    if ($term -eq 'dumb') {
        return $false
    }

    $colorTerm = [string] $Environment['COLORTERM']
    if ($colorTerm -match '(?i)truecolor|24bit') {
        return $true
    }

    foreach ($marker in @('WT_SESSION', 'TERM_PROGRAM')) {
        if ($Environment.ContainsKey($marker) -and
            -not [string]::IsNullOrWhiteSpace([string] $Environment[$marker])) {
            return $true
        }
    }

    return $false
}

function Get-RedPrismPixelPalette {
    return @{
        'e' = [pscustomobject]@{ Console = 'Gray'; R = 168; G = 172; B = 186 }
        'W' = [pscustomobject]@{ Console = 'White'; R = 246; G = 247; B = 252 }
        'c' = [pscustomobject]@{ Console = 'DarkCyan'; R = 48; G = 142; B = 176 }
        'h' = [pscustomobject]@{ Console = 'Cyan'; R = 168; G = 236; B = 252 }
        'd' = [pscustomobject]@{ Console = 'DarkBlue'; R = 28; G = 64; B = 118 }
        's' = [pscustomobject]@{ Console = 'DarkGray'; R = 42; G = 46; B = 58 }
        'k' = [pscustomobject]@{ Console = 'DarkGray'; R = 58; G = 62; B = 74 }
        'r' = [pscustomobject]@{ Console = 'Red'; R = 255; G = 64; B = 92 }
        'o' = [pscustomobject]@{ Console = 'DarkYellow'; R = 255; G = 146; B = 48 }
        'y' = [pscustomobject]@{ Console = 'Yellow'; R = 255; G = 220; B = 64 }
        'n' = [pscustomobject]@{ Console = 'Green'; R = 56; G = 214; B = 128 }
        't' = [pscustomobject]@{ Console = 'Cyan'; R = 48; G = 214; B = 222 }
        'b' = [pscustomobject]@{ Console = 'Blue'; R = 72; G = 132; B = 255 }
        'm' = [pscustomobject]@{ Console = 'Magenta'; R = 204; G = 84; B = 255 }
        'v' = [pscustomobject]@{ Console = 'DarkMagenta'; R = 148; G = 52; B = 214 }
        '*' = [pscustomobject]@{ Console = 'Yellow'; R = 255; G = 248; B = 196 }
    }
}

function Get-RedPrismWordmarkColors {
    return @(
        [pscustomobject]@{ Console = 'Red'; R = 255; G = 64; B = 92 }
        [pscustomobject]@{ Console = 'DarkYellow'; R = 255; G = 146; B = 48 }
        [pscustomobject]@{ Console = 'Yellow'; R = 255; G = 220; B = 64 }
        [pscustomobject]@{ Console = 'Green'; R = 56; G = 214; B = 128 }
        [pscustomobject]@{ Console = 'Cyan'; R = 48; G = 214; B = 222 }
        [pscustomobject]@{ Console = 'Blue'; R = 72; G = 132; B = 255 }
        [pscustomobject]@{ Console = 'Magenta'; R = 204; G = 84; B = 255 }
        [pscustomobject]@{ Console = 'DarkMagenta'; R = 148; G = 52; B = 214 }
    )
}

function New-RedPrismSceneRows {
    # A fixed 62x20 half-block pixel-art scene (incoming beam, glass prism, dispersed spectrum, composition layer
    # chips, canvas checker): literal rows authored once, not a triangle rasterized at every build (RF-709). The
    # pixel codes (r/o/y/n/t/b/m spectrum, c/h/d glass, e/W beam, s stroke, k/W checker, * highlight) are the keys
    # of Get-RedPrismPixelPalette.
    return @(
        '..............................................................',
        '.rrr.............................scs..........................',
        '.rnnn............................scs..........................',
        '..nbbb..........................scccs.........................',
        '...bb..........................shcccds..............r.........',
        '..............................shhcccdds..........rrr..........',
        '.............................shhcccccdds.......rr.............',
        '............................shhccccccddds...rrr...ooo.........',
        '........eeeeeeeeeeeeeeeeeeeshhhcccccccdddsrr..oooo............',
        '..*.WWWWWWWWWWWWWWWWWWWWWWshhhccccccccddddsooo...yyyy.........',
        '...*WWWWWWWWWWWWWWWWWWWWWshhhccccccccccdddd*yyyyy.............',
        '........eeeeeeeeeeeeeeeeshhhhcccccccccccddddsnnnnnnnn.........',
        '.......................shhhhccccccccccccdddddsttttt...........',
        '......................shhhhccccccccccccccdddddsbbb.tt.........',
        '.....................shhhhhccccccccccccccddddddsmmbbb.........',
        '....................shhhhhccccccccccccccccdddddds.mmm.kWkWkW..',
        '...................shhhhhhccccccccccccccccddddddds....WkWkWk..',
        '....................sssssssssssssssssssssssssssss.....kWkWkW..',
        '......................................................WkWkWk..',
        '..............................................................'
    )
}

function Get-RedPrismWordmarkGlyphs {
    return @(
        @('#### ', '#   #', '#### ', '#  # ', '#   #'),
        @('#####', '#    ', '###  ', '#    ', '#####'),
        @('#### ', '#   #', '#   #', '#   #', '#### '),
        @('#### ', '#   #', '#### ', '#    ', '#    '),
        @('#### ', '#   #', '#### ', '#  # ', '#   #'),
        @('#####', '  #  ', '  #  ', '  #  ', '#####'),
        @('#####', '#    ', '#####', '    #', '#####'),
        @('#   #', '## ##', '# # #', '#   #', '#   #')
    )
}

function Get-RedPrismBuildBannerModel {
    $block = [char]0x2588
    $upperHalf = [char]0x2580
    $lowerHalf = [char]0x2584
    $topLeft = [char]0x2554
    $topRight = [char]0x2557
    $bottomLeft = [char]0x255A
    $bottomRight = [char]0x255D
    $bar = [char]0x2550
    $vertical = [char]0x2551

    $sceneRows = New-RedPrismSceneRows
    $glyphs = Get-RedPrismWordmarkGlyphs
    $wordmarkRows = @(0..4 | ForEach-Object {
            $rowIndex = $_
            $parts = foreach ($glyph in $glyphs) {
                $glyph[$rowIndex]
            }
            $parts -join ' '
        })

    $tagline = 'REDPRISM // RASTER COMPOSITION // BUILD LOCKED'
    $taglineLabel = " $tagline "
    $sceneWidth = $sceneRows[0].Length
    $wordmarkWidth = $wordmarkRows[0].Length
    $innerWidth = [Math]::Max(64, [Math]::Max($sceneWidth, $wordmarkWidth) + 2)
    $sceneLeftPad = [int][Math]::Floor(($innerWidth - $sceneWidth) / 2)
    $sceneRightPad = $innerWidth - $sceneWidth - $sceneLeftPad
    $wordmarkLeftPad = [int][Math]::Floor(($innerWidth - $wordmarkWidth) / 2)
    $wordmarkRightPad = $innerWidth - $wordmarkWidth - $wordmarkLeftPad
    $taglineLeftBar = [int][Math]::Floor(($innerWidth - $taglineLabel.Length) / 2)
    $taglineRightBar = $innerWidth - $taglineLabel.Length - $taglineLeftBar

    return [pscustomobject]@{
        SceneRows = @($sceneRows)
        WordmarkRows = @($wordmarkRows)
        WordmarkGlyphs = @($glyphs)
        Palette = Get-RedPrismPixelPalette
        WordmarkColors = Get-RedPrismWordmarkColors
        Tagline = $tagline
        TaglineLabel = $taglineLabel
        InnerWidth = $innerWidth
        SceneLeftPad = $sceneLeftPad
        SceneRightPad = $sceneRightPad
        WordmarkLeftPad = $wordmarkLeftPad
        WordmarkRightPad = $wordmarkRightPad
        TaglineLeftBar = $taglineLeftBar
        TaglineRightBar = $taglineRightBar
        TopLeft = $topLeft
        TopRight = $topRight
        BottomLeft = $bottomLeft
        BottomRight = $bottomRight
        Bar = $bar
        Vertical = $vertical
        Block = $block
        UpperHalf = $upperHalf
        LowerHalf = $lowerHalf
        Indent = '  '
    }
}

function Write-RedPrismBannerChrome {
    param(
        [Parameter(Mandatory)]
        [string] $Text,

        [bool] $UseColor,

        [switch] $NoNewline
    )

    if ($UseColor) {
        if ($NoNewline) {
            Write-Host $Text -ForegroundColor DarkGray -NoNewline
        }
        else {
            Write-Host $Text -ForegroundColor DarkGray
        }
    }
    elseif ($NoNewline) {
        Write-Host $Text -NoNewline
    }
    else {
        Write-Host $Text
    }
}

function Get-RedPrismPixelKey {
    param(
        [string] $Row,
        [int] $Index
    )

    if ($Index -lt 0 -or $Index -ge $Row.Length) {
        return '.'
    }
    return [string]$Row[$Index]
}

function Write-RedPrismPixelCell {
    param(
        [string] $UpperKey,
        [string] $LowerKey,
        [hashtable] $Palette,
        [bool] $UseColor,
        [bool] $UseTrueColor,
        [char] $Block,
        [char] $UpperHalf,
        [char] $LowerHalf
    )

    $upperEmpty = [string]::IsNullOrEmpty($UpperKey) -or $UpperKey -eq '.'
    $lowerEmpty = [string]::IsNullOrEmpty($LowerKey) -or $LowerKey -eq '.'
    if ($upperEmpty -and $lowerEmpty) {
        Write-Host ' ' -NoNewline
        return
    }

    $esc = [char]0x1b
    $reset = "$esc[0m"

    if ($UseTrueColor) {
        if (-not $upperEmpty -and -not $lowerEmpty) {
            $upper = $Palette[$UpperKey]
            $lower = $Palette[$LowerKey]
            if ($UpperKey -eq $LowerKey) {
                Write-Host ("{0}[38;2;{1};{2};{3}m{4}{5}" -f $esc, $upper.R, $upper.G, $upper.B, $Block, $reset) -NoNewline
            }
            else {
                Write-Host (
                    "{0}[38;2;{1};{2};{3}m{0}[48;2;{4};{5};{6}m{7}{8}" -f `
                        $esc, $upper.R, $upper.G, $upper.B, $lower.R, $lower.G, $lower.B, $UpperHalf, $reset) -NoNewline
            }
        }
        elseif (-not $upperEmpty) {
            $upper = $Palette[$UpperKey]
            Write-Host ("{0}[38;2;{1};{2};{3}m{4}{5}" -f $esc, $upper.R, $upper.G, $upper.B, $UpperHalf, $reset) -NoNewline
        }
        else {
            $lower = $Palette[$LowerKey]
            Write-Host ("{0}[38;2;{1};{2};{3}m{4}{5}" -f $esc, $lower.R, $lower.G, $lower.B, $LowerHalf, $reset) -NoNewline
        }
        return
    }

    if ($UseColor) {
        if (-not $upperEmpty -and -not $lowerEmpty) {
            $upper = $Palette[$UpperKey]
            $lower = $Palette[$LowerKey]
            if ($UpperKey -eq $LowerKey) {
                Write-Host $Block -ForegroundColor $upper.Console -NoNewline
            }
            else {
                Write-Host $UpperHalf -ForegroundColor $upper.Console -BackgroundColor $lower.Console -NoNewline
            }
        }
        elseif (-not $upperEmpty) {
            Write-Host $UpperHalf -ForegroundColor $Palette[$UpperKey].Console -NoNewline
        }
        else {
            Write-Host $LowerHalf -ForegroundColor $Palette[$LowerKey].Console -NoNewline
        }
        return
    }

    if (-not $upperEmpty -and -not $lowerEmpty) {
        Write-Host $Block -NoNewline
    }
    elseif (-not $upperEmpty) {
        Write-Host $UpperHalf -NoNewline
    }
    else {
        Write-Host $LowerHalf -NoNewline
    }
}

function Write-RedPrismWordmarkRow {
    param(
        $Model,
        [int] $RowIndex,
        [bool] $UseColor,
        [bool] $UseTrueColor
    )

    $esc = [char]0x1b
    $reset = "$esc[0m"
    $glyphs = $Model.WordmarkGlyphs
    $colors = $Model.WordmarkColors
    for ($letter = 0; $letter -lt $glyphs.Count; ++$letter) {
        $pattern = $glyphs[$letter][$RowIndex]
        $color = $colors[$letter]
        foreach ($cell in $pattern.ToCharArray()) {
            if ($cell -eq '#') {
                if ($UseTrueColor) {
                    Write-Host ("{0}[38;2;{1};{2};{3}m{4}{5}" -f $esc, $color.R, $color.G, $color.B, $Model.Block, $reset) -NoNewline
                }
                elseif ($UseColor) {
                    Write-Host $Model.Block -ForegroundColor $color.Console -NoNewline
                }
                else {
                    Write-Host $Model.Block -NoNewline
                }
            }
            else {
                Write-Host ' ' -NoNewline
            }
        }
        if ($letter -lt ($glyphs.Count - 1)) {
            Write-Host ' ' -NoNewline
        }
    }
}

function Write-RedPrismBuildBanner {
    [CmdletBinding()]
    param(
        [bool] $UseColor = $true,

        [hashtable] $Environment = @{}
    )

    $model = Get-RedPrismBuildBannerModel
    $useTrueColor = Test-RedPrismTrueColorHost -UseColor $UseColor -Environment $Environment
    $bar = $model.Bar.ToString()
    $palette = $model.Palette

    Write-Host ''
    Write-RedPrismBannerChrome -UseColor $UseColor -Text (
        '{0}{1}{2}{3}' -f $model.Indent, $model.TopLeft, ($bar * $model.InnerWidth), $model.TopRight)

    for ($pair = 0; $pair -lt $model.SceneRows.Count; $pair += 2) {
        $upper = $model.SceneRows[$pair]
        $lower = $model.SceneRows[$pair + 1]
        Write-RedPrismBannerChrome -UseColor $UseColor -NoNewline -Text (
            '{0}{1}{2}' -f $model.Indent, $model.Vertical, (' ' * $model.SceneLeftPad))
        for ($x = 0; $x -lt $upper.Length; ++$x) {
            Write-RedPrismPixelCell `
                -UpperKey (Get-RedPrismPixelKey -Row $upper -Index $x) `
                -LowerKey (Get-RedPrismPixelKey -Row $lower -Index $x) `
                -Palette $palette `
                -UseColor $UseColor `
                -UseTrueColor $useTrueColor `
                -Block $model.Block `
                -UpperHalf $model.UpperHalf `
                -LowerHalf $model.LowerHalf
        }
        Write-RedPrismBannerChrome -UseColor $UseColor -Text (
            '{0}{1}' -f (' ' * $model.SceneRightPad), $model.Vertical)
    }

    Write-RedPrismBannerChrome -UseColor $UseColor -Text (
        '{0}{1}{2}{1}' -f $model.Indent, $model.Vertical, (' ' * $model.InnerWidth))

    for ($rowIndex = 0; $rowIndex -lt $model.WordmarkRows.Count; ++$rowIndex) {
        Write-RedPrismBannerChrome -UseColor $UseColor -NoNewline -Text (
            '{0}{1}{2}' -f $model.Indent, $model.Vertical, (' ' * $model.WordmarkLeftPad))
        Write-RedPrismWordmarkRow -Model $model -RowIndex $rowIndex -UseColor $UseColor -UseTrueColor $useTrueColor
        Write-RedPrismBannerChrome -UseColor $UseColor -Text (
            '{0}{1}' -f (' ' * $model.WordmarkRightPad), $model.Vertical)
    }

    Write-RedPrismBannerChrome -UseColor $UseColor -Text (
        '{0}{1}{2}{3}{4}{5}' -f $model.Indent, $model.BottomLeft, ($bar * $model.TaglineLeftBar),
        $model.TaglineLabel, ($bar * $model.TaglineRightBar), $model.BottomRight)
    Write-Host ''
}

function Get-RedPrismDxUiUpdateNoticeForegroundColor {
    [CmdletBinding()]
    param(
        [AllowNull()]
        [string] $Notice
    )

    if ([string]::IsNullOrWhiteSpace($Notice)) {
        return $null
    }

    # The pinned DxUi helper owns the read-only update decision. This adapter owns only the color.
    if ($Notice -like 'DxUi main * has no successful completed validation;*') {
        return 'Red'
    }
    if ($Notice -like 'DxUi update available:*') {
        return 'Yellow'
    }

    return 'DarkYellow'
}

Export-ModuleMember -Function @(
    'Test-RedPrismInteractiveTerminal',
    'Test-RedPrismTrueColorHost',
    'Get-RedPrismBuildBannerModel',
    'Write-RedPrismBuildBanner',
    'Get-RedPrismDxUiUpdateNoticeForegroundColor'
)
