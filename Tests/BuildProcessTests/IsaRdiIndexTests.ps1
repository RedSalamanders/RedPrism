[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Locate the private scanner by walking upward from this sidecar's location.
$privateRoot = (Get-Item -LiteralPath $PSScriptRoot).FullName
$scannerPath = $null
while ($privateRoot) {
    $candidate = Join-Path $privateRoot 'Build/IsaScan.cs'
    if (Test-Path -LiteralPath $candidate -PathType Leaf) { $scannerPath = $candidate; break }
    $parent = Split-Path -Parent $privateRoot
    if (-not $parent -or $parent -eq $privateRoot) { break }
    $privateRoot = $parent
}
if (-not $scannerPath) { throw "Could not find Build/IsaScan.cs above $PSScriptRoot." }
$sharedTests = Join-Path $privateRoot 'Tests/BuildProcessTests/IsaInlineTableTests.ps1'
if (-not (Test-Path -LiteralPath $sharedTests -PathType Leaf)) { throw "Missing shared ISA fixture source: $sharedTests" }

# Import the existing builders and scanner assertion helpers. The shared script also runs its own established controls.
. $sharedTests
if (-not ('RedPrismBuild.IsaScanner' -as [type])) { Add-Type -Path $scannerPath }

$script:isaRdiCount = 0
function Assert-IsaRdiIndex {
    param(
        [Parameter(Mandatory)] [hashtable]$Case,
        [Parameter(Mandatory)] [string]$Label,
        [Parameter(Mandatory)] [bool]$ExpectedPass,
        [int]$ExpectedInline = -1,
        [int]$ExpectedVectors = -1
    )
    # The dispatch recognizer alone; the full gate's PDB-marked table proof is controlled in IsaMarkedTableTests.ps1. Under the
    # full gate the valid controls stay accepted and the genuine vector instructions (AdjacentVex, AdjacentEvex) stay rejected.
    $gate = [RedPrismBuild.IsaScanner]::Scan($Case.Binary, $Case.Metadata, $Case.Symbols, $Case.Disassembly,
        [string[]]@(), [string[]]@(), [string[]]@())
    if ($ExpectedPass -and $gate.Violations.Count -ne 0) { throw "$Label violated the full ISA gate: $($gate.Violations -join '; ')" }
    if (-not $ExpectedPass -and $Case.Name -match 'vex' -and $gate.Violations.Count -eq 0) {
        throw "${Label}: the full ISA gate accepted a genuine vector instruction."
    }
    $result = [RedPrismBuild.IsaScanner]::Scan($Case.Binary, $Case.Metadata, $Case.Symbols, $Case.Disassembly,
        [string[]]@(), [string[]]@(), [string[]]@(), $false)
    if ($ExpectedPass -and $result.Violations.Count -ne 0) {
        throw "$Label unexpectedly violated the ISA gate: $($result.Violations -join '; ')"
    }
    if (-not $ExpectedPass -and $result.Violations.Count -eq 0) { throw "$Label bypassed the ISA gate." }
    if ($ExpectedInline -ge 0 -and $result.InlineTableVectorData -ne $ExpectedInline) {
        throw "$Label expected $ExpectedInline inline-table rows, got $($result.InlineTableVectorData)."
    }
    if ($ExpectedVectors -ge 0 -and $result.VectorInstructions -ne $ExpectedVectors) {
        throw "$Label expected $ExpectedVectors ordinary vector rows, got $($result.VectorInstructions)."
    }
    ++$script:isaRdiCount
    Write-Host "PASS isa-rdi-index.$Label"
}

function New-RdiIndexCase {
    param(
        [Parameter(Mandatory)] [string]$Name,
        [string]$Fault = ''
    )
    # Start with the shared PDB-backed RDX image-base fixture, then transform its dispatch rows in place.
    # Each replacement preserves instruction width so independent controls retain the same table/layout.
    $case = New-SyntheticCase ("RdiIndex-$Name") -RdxBase -PdbData
    [byte[]]$pe = [IO.File]::ReadAllBytes($case.Binary)
    $leaRow = [IO.File]::ReadAllLines($case.Disassembly) | Where-Object { $_ -match ': 48 8d 15 ' } | Select-Object -First 1
    if (-not $leaRow) { throw "$Name fixture has no RDX image-base LEA row." }
    $leaAddress = [UInt64]::Parse(($leaRow -split ':',2)[0], [Globalization.NumberStyles]::HexNumber)
    $imageBase = [BitConverter]::ToUInt64($pe, 0x98 + 24)
    $sectionRva = [UInt32]0x0010f000
    $rawOffset = [UInt32]0x400
    $leaRva = [UInt32]($leaAddress - $imageBase)
    $loadRva = [UInt32]($leaRva + 7)
    $addRva = [UInt32]($loadRva + 7)
    $jumpRva = [UInt32]($addRva + 3)
    $loadRaw = [int]($rawOffset + $loadRva - $sectionRva)
    $addRaw = [int]($rawOffset + $addRva - $sectionRva)
    $jumpRaw = [int]($rawOffset + $jumpRva - $sectionRva)
    [byte[]]$rdiLoad = @(0x8b,0x84,0xba,0,0,0,0)
    [Array]::Copy($pe, $loadRaw + 3, $rdiLoad, 3, 4)
    [byte[]]$rdiAdd = @(0x48,0x03,0xc2)
    [byte[]]$rdiJump = @(0xff,0xe0)
    [Array]::Copy($rdiLoad, 0, $pe, $loadRaw, 7)
    [Array]::Copy($rdiAdd, 0, $pe, $addRaw, 3)
    [Array]::Copy($rdiJump, 0, $pe, $jumpRaw, 2)
    $tableRva = [BitConverter]::ToUInt32($rdiLoad, 3)
    $tableRaw = [int]($rawOffset + $tableRva - $sectionRva)
    $tableEndRva = [UInt32]($tableRva + 28)

    $rows = [Collections.Generic.List[string]]::new()
    foreach ($line in [IO.File]::ReadAllLines($case.Disassembly)) {
        $addrText = ($line -split ':',2)[0]
        $address = [UInt64]::Parse($addrText, [Globalization.NumberStyles]::HexNumber)
        $rva = [UInt32]($address - $imageBase)
        if ($rva -eq $loadRva) { $line = ('{0:x}: {1} mov eax, dword ptr [rdx+4*rdi+table]' -f $address, (($rdiLoad | ForEach-Object { $_.ToString('x2') }) -join ' ')) }
        elseif ($rva -eq $addRva) { $line = ('{0:x}: 48 03 c2 add rax, rdx' -f $address) }
        elseif ($rva -eq $jumpRva) { $line = ('{0:x}: ff e0 jmp rax' -f $address) }
        $rows.Add($line)
    }
    [IO.File]::WriteAllBytes($case.Binary, $pe)
    Set-Content -LiteralPath $case.Disassembly -Value ($rows -join "`n") -Encoding utf8

    # Establish a same-owner next-data boundary for exactly seven DWORD entries.
    Add-Content -LiteralPath $case.Symbols -Value "  Mod 0 | ``SyntheticControl.obj``:`n  0 | S_LDATA32 [size = 16] `````n  type = <no type>, addr = 0001:$($tableEndRva - $sectionRva)`n" -Encoding utf8

    switch ($Fault) {
        'ListingMismatch' {
            $lines = [IO.File]::ReadAllLines($case.Disassembly)
            for ($i=0; $i -lt $lines.Length; ++$i) { if ($lines[$i] -match ': 8b 84 ba ') { $lines[$i] = $lines[$i] -replace '8b 84 ba','8b 84 aa'; break } }
            Set-Content -LiteralPath $case.Disassembly -Value ($lines -join "`n") -Encoding utf8
        }
        'ListingUnknown' {
            $lines = [IO.File]::ReadAllLines($case.Disassembly)
            for ($i=0; $i -lt $lines.Length; ++$i) { if ($lines[$i] -match ': 8b 84 ba ') { $lines[$i] = $lines[$i] -replace 'mov eax, dword ptr.*$','<unknown>'; break } }
            Set-Content -LiteralPath $case.Disassembly -Value ($lines -join "`n") -Encoding utf8
        }
        'ListingOverlap' {
            Add-Content -LiteralPath $case.Disassembly -Value ('{0:x}: 90 nop' -f ($imageBase+$loadRva+1)) -Encoding utf8
        }
        'LeaBaseMismatch' { $pe[$rawOffset+$leaRva-$sectionRva+2] = 0x0d }
        'LoadOpcodeMismatch' { $pe[$loadRaw] = 0x89 }
        'LoadIndexMismatch' { $pe[$loadRaw+2] = 0x8a }
        'LoadBaseMismatch' { $pe[$loadRaw+2] = 0xb8 }
        'LoadDestinationMismatch' { $pe[$loadRaw+1] = 0x8c }
        'AddRegisterMismatch' { $pe[$addRaw+2] = 0xc1 }
        'JumpRegisterMismatch' { $pe[$jumpRaw+1] = 0xe1 }
        'MissingPdbStart' {
            $text = [IO.File]::ReadAllText($case.Symbols)
            $pattern = '(?m)^  Mod 0 \| `SyntheticControl\.obj`:\r?\n  0 \| S_LDATA32 \[size = 16\] ``\r?\n  type = <no type>, addr = 0001:' + [regex]::Escape(($tableRva-$sectionRva).ToString()) + '\r?\n'
            if ([regex]::Matches($text,$pattern).Count -ne 1) { throw 'MissingPdbStart mutation was not reached' }
            $text = [regex]::Replace($text, $pattern, '')
            Set-Content -LiteralPath $case.Symbols -Value $text -Encoding utf8
        }
        'WrongPdbOwner' {
            $text = [IO.File]::ReadAllText($case.Symbols)
            $offset = $tableRva - $sectionRva
            $pattern = '(?m)^  Mod 0 \| `SyntheticControl\.obj`:\r?\n(?=  0 \| S_LDATA32 \[size = 16\] ``\r?\n  type = <no type>, addr = 0001:' + [regex]::Escape($offset.ToString()) + '\r?$)'
            if ([regex]::Matches($text,$pattern).Count -ne 1) { throw 'WrongPdbOwner mutation was not reached' }
            $text = [regex]::Replace($text, $pattern, '  Mod 1 | `SyntheticControl.obj`:')
            Set-Content -LiteralPath $case.Symbols -Value $text -Encoding utf8
        }
        'PdbStartEarly' {
            $text = [IO.File]::ReadAllText($case.Symbols)
            $offset = $tableRva - $sectionRva
            $pattern = '(?m)(addr = 0001:)' + [regex]::Escape($offset.ToString()) + '(?=\r?$)'
            if ([regex]::Matches($text,$pattern).Count -ne 1) { throw 'PdbStartEarly mutation was not reached' }
            $text = [regex]::Replace($text, $pattern, ('${1}' + ($offset-1)))
            Set-Content -LiteralPath $case.Symbols -Value $text -Encoding utf8
        }
        'EarlyNextSymbol' {
            Add-Content -LiteralPath $case.Symbols -Value "  Mod 0 | ``SyntheticControl.obj``:`n  0 | S_LDATA32 [size = 16] `````n  type = <no type>, addr = 0001:$($tableRva-$sectionRva+4)`n" -Encoding utf8
        }
        'UnrelatedNextSymbol' {
            Add-Content -LiteralPath $case.Symbols -Value "  Mod 0 | ``SyntheticControl.obj``:`n  0 | S_LDATA32 [size = 16] `````n  type = <no type>, addr = 0001:$($tableRva-$sectionRva+64)`n" -Encoding utf8
        }
        'BadDefaultTarget' { [Array]::Copy([BitConverter]::GetBytes([UInt32]$tableRva),0,$pe,$tableRaw,4) }
        'MiddleTarget' { [Array]::Copy([BitConverter]::GetBytes([UInt32]($case.Target+1)),0,$pe,$tableRaw,4) }
        'OutsideTarget' { [Array]::Copy([BitConverter]::GetBytes([UInt32]($sectionRva+0x2000)),0,$pe,$tableRaw,4) }
        'BackwardSameProcedure' {
            [Array]::Copy([BitConverter]::GetBytes([UInt32]$leaRva),0,$pe,$tableRaw,4)
            Add-Content -LiteralPath $case.Symbols -Value "  Mod 0 | ``SyntheticControl.obj``:`n  0 | S_GPROC32 [size = 16] ``SyntheticRdiOwner`` `n  addr = 0001:$($leaRva-$sectionRva), code size = $($tableRva+28-$leaRva)`n" -Encoding utf8
        }
        'BackwardOutsideProcedure' {
            [Array]::Copy([BitConverter]::GetBytes([UInt32]$leaRva),0,$pe,$tableRaw,4)
            # The PDB procedure begins at the load, so it excludes the backward target at the dispatch LEA.
            Add-Content -LiteralPath $case.Symbols -Value "  Mod 0 | ``SyntheticControl.obj``:`n  0 | S_GPROC32 [size = 16] ``SyntheticRdiOwner`` `n  addr = 0001:$($leaRva-$sectionRva+7), code size = $($tableRva+28-$leaRva-7)`n" -Encoding utf8
        }
        'AdjacentVex' {
            [byte[]]$vex = @(0xc5,0xf8,0x10,0x00)
            [Array]::Copy($vex,0,$pe,$tableRaw+28,4)
            $rows.Add(('{0:x}: c5 f8 10 00 vmovups xmm0, xmmword ptr [rax]' -f ($imageBase+$tableEndRva)))
            Set-Content -LiteralPath $case.Disassembly -Value ($rows -join "`n") -Encoding utf8
        }
        'AdjacentEvex' {
            [byte[]]$evex = @(0x62,0xf1,0x7c,0x48,0x58,0xc0)
            [Array]::Copy($evex,0,$pe,$tableRaw+28,$evex.Length)
            $rows.Add(('{0:x}: 62 f1 7c 48 58 c0 vaddps zmm0, zmm0, zmm0' -f ($imageBase+$tableEndRva)))
            Set-Content -LiteralPath $case.Disassembly -Value ($rows -join "`n") -Encoding utf8
        }
        default { }
    }
    [IO.File]::WriteAllBytes($case.Binary, $pe)
    return $case
}

# Canonical image-relative RDI DWORD-index dispatch: exact LEA/MOV/ADD/JMP bytes, a RET before
# the table, exact same-owner PDB start/end, and valid decoded target boundaries.
Assert-IsaRdiIndex (New-RdiIndexCase 'Valid') 'valid' $true 7 0
Assert-IsaRdiIndex (New-RdiIndexCase 'BackwardSameProcedure' 'BackwardSameProcedure') 'backward-target-same-procedure' $true 7 0

foreach ($fault in @(
    'ListingMismatch','ListingUnknown','ListingOverlap',
    'LeaBaseMismatch','LoadOpcodeMismatch','LoadIndexMismatch','LoadBaseMismatch','LoadDestinationMismatch',
    'AddRegisterMismatch','JumpRegisterMismatch',
    'MissingPdbStart','WrongPdbOwner','PdbStartEarly','EarlyNextSymbol',
    'BadDefaultTarget','MiddleTarget','OutsideTarget','BackwardOutsideProcedure'
)) {
    Assert-IsaRdiIndex (New-RdiIndexCase $fault $fault) $fault $false
}
Assert-IsaRdiIndex (New-RdiIndexCase 'AdjacentVex' 'AdjacentVex') 'adjacent-vex-stays-forbidden' $false -1 1
Assert-IsaRdiIndex (New-RdiIndexCase 'UnrelatedNextSymbol' 'UnrelatedNextSymbol') 'later-unrelated-symbol-does-not-widen-table' $true 7 0
Assert-IsaRdiIndex (New-RdiIndexCase 'AdjacentEvex' 'AdjacentEvex') 'adjacent-evex-stays-forbidden' $false -1 1

Write-Host "RDI-index ISA synthetic controls passed: $script:isaRdiCount"
