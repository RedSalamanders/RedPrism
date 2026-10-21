# Dispatch-form controls isolate the recognizers; the default image gate retains PDB-marked proof in IsaMarkedTableTests.ps1.
[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if (-not ('RedPrismBuild.IsaScanner' -as [type])) {
    Add-Type -Path (Join-Path $repo 'Build/IsaScan.cs')
}
$fixtureRoot = Join-Path $repo ('.build/BuildProcessTests/IsaCompactRcxByteRemap-' + [guid]::NewGuid().ToString('N'))
[void](New-Item -ItemType Directory -Path $fixtureRoot -Force)
$imageBase = [UInt64]0x140000000
$sectionRva = [UInt32]0x1000
$dispatchRva = [UInt32]0x1100
$dwordRva = [UInt32]0x1200
$byteRva = [UInt32]($dwordRva + 28)
$targetRva = [UInt32]0x11c5
$guid = [guid]'3b64c2aa-43ef-49a4-87da-38e4d2aa4e7f'
$age = [UInt32]3

function Set-U16([byte[]]$Bytes, [int]$Offset, [UInt16]$Value) {
    [Array]::Copy([BitConverter]::GetBytes($Value), 0, $Bytes, $Offset, 2)
}
function Set-U32([byte[]]$Bytes, [int]$Offset, [UInt32]$Value) {
    [Array]::Copy([BitConverter]::GetBytes($Value), 0, $Bytes, $Offset, 4)
}
function Set-U64([byte[]]$Bytes, [int]$Offset, [UInt64]$Value) {
    [Array]::Copy([BitConverter]::GetBytes($Value), 0, $Bytes, $Offset, 8)
}
function Put-Bytes([byte[]]$Bytes, [int]$Offset, [byte[]]$Value) {
    [Array]::Copy($Value, 0, $Bytes, $Offset, $Value.Length)
}
function Hex-Bytes([byte[]]$Bytes) {
    return (($Bytes | ForEach-Object { $_.ToString('x2') }) -join ' ')
}
function New-CompactRcxControl([string]$Name, [string]$Fault = '', [int]$Maximum = 112, [UInt32]$StackDisplacement = 0x138) {
    $dir = Join-Path $fixtureRoot $Name
    [void](New-Item -ItemType Directory -Path $dir -Force)
    $binary = Join-Path $dir 'Synthetic.exe'
    $metadata = Join-Path $dir 'contributions.txt'
    $symbols = Join-Path $dir 'symbols.txt'
    $disassembly = Join-Path $dir 'disassembly.txt'
    $pe = [byte[]]::new(0x2400)
    $pe[0] = 0x4d; $pe[1] = 0x5a
    Set-U32 $pe 0x3c 0x80
    Set-U32 $pe 0x80 0x00004550
    Set-U16 $pe 0x84 0x8664
    Set-U16 $pe 0x86 1
    Set-U16 $pe 0x94 0xf0
    Set-U16 $pe 0x98 0x20b
    Set-U64 $pe (0x98+24) $imageBase
    Set-U32 $pe (0x98+56) 0x3000
    Set-U32 $pe (0x98+60) 0x400
    Set-U32 $pe (0x98+112+6*8) 0x1180
    Set-U32 $pe (0x98+116+6*8) 28
    $section = 0x80 + 24 + 0xf0
    Put-Bytes $pe $section ([Text.Encoding]::ASCII.GetBytes('.text' + [char]0 + [char]0 + [char]0))
    Set-U32 $pe ($section+8) 0x3000
    Set-U32 $pe ($section+12) $sectionRva
    Set-U32 $pe ($section+16) 0x3000
    Set-U32 $pe ($section+20) 0x400
    Set-U32 $pe ($section+36) 0x60000020
    Set-U32 $pe (0x400+0x1180-$sectionRva+12) 2
    Set-U32 $pe (0x400+0x1180-$sectionRva+16) 24
    Set-U32 $pe (0x400+0x1180-$sectionRva+20) 0x11a0
    Set-U32 $pe (0x400+0x1180-$sectionRva+24) 0x5a0
    Put-Bytes $pe 0x5a0 ([Text.Encoding]::ASCII.GetBytes('RSDS'))
    Put-Bytes $pe 0x5a4 $guid.ToByteArray()
    Set-U32 $pe 0x5b4 $age

    # Independent literal compact stack guard and RCX dispatch seen in ASan menu code.
    # CMP dword [RSP+disp32],imm8; JA rel32; MOV EAX,[RSP+the same disp32].
    # Guard range and PDB procedure end independently bound max+1 byte indices.
    if ($Maximum -lt 1 -or $Maximum -gt 127) { throw 'Invalid fixture maximum' }
    $guardRva = [UInt32]($dispatchRva - 21)
    [byte[]]$guard = [byte[]]@(0x83,0xbc,0x24,0,0,0,0,[byte]$Maximum,0x0f,0x87,0,0,0,0,0x8b,0x84,0x24,0,0,0,0)
    Put-Bytes $guard 3 ([BitConverter]::GetBytes($StackDisplacement))
    Put-Bytes $guard 17 ([BitConverter]::GetBytes($StackDisplacement))
    $jaTarget = if ($Fault -eq 'GuardTarget') { [UInt32]($sectionRva + 0x185) } else { $targetRva }
    $jaDisp = [Int32]($jaTarget - ($guardRva + 14))
    Put-Bytes $guard 10 ([BitConverter]::GetBytes($jaDisp))
    if ($Fault -eq 'GuardLimit') { $guard[7] = 128 }
    if ($Fault -eq 'BoundZero') { $guard[7] = 0 }
    if ($Fault -eq 'StackMismatch') { $guard[17] = [byte](($guard[17] + 1) -band 0xff) }
    Put-Bytes $pe (0x400+$guardRva-$sectionRva) $guard

    [byte[]]$lea = [byte[]]@(0x48,0x8d,0x0d,0,0,0,0)
    Put-Bytes $lea 3 ([BitConverter]::GetBytes([Int32]($imageBase - ($imageBase+$dispatchRva+7))))
    [byte[]]$byteLoad = [byte[]]@(0x0f,0xb6,0x84,0x01,0,0,0,0)
    Put-Bytes $byteLoad 4 ([BitConverter]::GetBytes([UInt32]$byteRva))
    if ($Fault -eq 'WrongByteEncoding') { $byteLoad[3] = 0x02 }
    if ($Fault -eq 'PeListingMismatch') { $byteLoad[3] = 0x02 }
    [byte[]]$dwordLoad = [byte[]]@(0x8b,0x84,0x81,0,0,0,0)
    Put-Bytes $dwordLoad 3 ([BitConverter]::GetBytes([UInt32]$dwordRva))
    [byte[]]$add = [byte[]]@(0x48,0x03,0xc1)
    [byte[]]$jump = [byte[]]@(0xff,0xe0)
    Put-Bytes $pe (0x400+$dispatchRva-$sectionRva) $lea
    Put-Bytes $pe (0x400+$dispatchRva+7-$sectionRva) $byteLoad
    Put-Bytes $pe (0x400+$dispatchRva+15-$sectionRva) $dwordLoad
    Put-Bytes $pe (0x400+$dispatchRva+22-$sectionRva) $add
    Put-Bytes $pe (0x400+$dispatchRva+25-$sectionRva) $jump

    [byte[]]$dwords = [byte[]]::new(28)
    $target = if ($Fault -eq 'MiddleTarget') { [UInt32]($targetRva+1) } else { $targetRva }
    for ($i=0; $i -lt 7; ++$i) { Put-Bytes $dwords (4*$i) ([BitConverter]::GetBytes($target)) }
    Put-Bytes $pe (0x400+$dwordRva-$sectionRva) $dwords
    [byte[]]$remap = [byte[]]::new($Maximum + 1)
    for ($i=0; $i -lt $remap.Length; ++$i) { $remap[$i] = [byte]($i % 7) }
    if ($Fault -eq 'BadRemapData') { $remap[$Maximum] = 7 }
    Put-Bytes $pe (0x400+$byteRva-$sectionRva) $remap
    Put-Bytes $pe (0x400+$targetRva-$sectionRva) ([byte[]]@(0x66,0x90))

    $module = "  Mod 0000 | ``Synthetic.obj``:`n  Obj: ``Synthetic.lib``:`n"
    $metadataText = "  Age: $age`n  GUID: {$guid}`n$module`nSection Contributions`n  SC[.text]  | mod = 0, 0001:0, size = 8192, data crc = 0, reloc crc = 0`n"
    Set-Content -LiteralPath $metadata -Value $metadataText -Encoding utf8
    $symbolText = $module +
        "  0 | S_GPROC32 [size = 40] ``SyntheticDispatch```n    addr = 0001:0, code size = $($byteRva + $Maximum + 1 - $sectionRva)`n" +
        "  0 | S_LDATA32 [size = 16] `````n  type = <no type>, addr = 0001:512`n" +
        "  0 | S_LDATA32 [size = 16] `````n  type = <no type>, addr = 0001:540`n"
    if ($Fault -eq 'MissingProcedure') {
        $symbolText = $module + "  0 | S_LDATA32 [size = 16] `````n  type = <no type>, addr = 0001:512`n" +
            "  0 | S_LDATA32 [size = 16] `````n  type = <no type>, addr = 0001:540`n"
    }
    if ($Fault -eq 'ShortProcedure') { $symbolText = $symbolText.Replace(('code size = '+($byteRva + $Maximum + 1 - $sectionRva)),('code size = '+($byteRva + $Maximum - $sectionRva))) }
    if ($Fault -eq 'MissingByteSymbol') {
        $symbolText = $symbolText.Replace("  0 | S_LDATA32 [size = 16] `````n  type = <no type>, addr = 0001:540`n",'')
    }
    if ($Fault -eq 'MissingDwordSymbol') {
        $symbolText = $symbolText.Replace("  0 | S_LDATA32 [size = 16] `````n  type = <no type>, addr = 0001:512`n",'')
    }
    if ($Fault -eq 'EarlySymbol') {
        $symbolText += "  0 | S_LDATA32 [size = 16] `````n  type = <no type>, addr = 0001:$($byteRva + $Maximum - $sectionRva)`n"
    }
    if ($Fault -eq 'GuardMissingWithLaterSymbol') {
        # A later PDB data boundary must not authorize this newly recognized dispatch without its guard.
        $symbolText += "  0 | S_LDATA32 [size = 16] `````n  type = <no type>, addr = 0001:$($byteRva + $Maximum + 1 - $sectionRva)`n"
    }
    Set-Content -LiteralPath $symbols -Value $symbolText -Encoding utf8

    $rows = [Collections.Generic.List[string]]::new()
    $guardRows = @(
        @{Rva=$guardRva; Bytes=$guard[0..7]; Text='cmp dword ptr [rsp + disp], max'},
        @{Rva=($guardRva+8); Bytes=$guard[8..13]; Text='ja default'},
        @{Rva=($guardRva+14); Bytes=$guard[14..20]; Text='mov eax, dword ptr [rsp + disp]'})
    foreach ($row in $guardRows) {
        if ($Fault -in 'MissingGuard','GuardMissingWithLaterSymbol' -and $row.Rva -eq ($guardRva)) { continue }
        [byte[]]$listedGuard = [byte[]]$row.Bytes.Clone()
        if ($Fault -eq 'GuardListingMismatch' -and $row.Rva -eq ($guardRva)) { $listedGuard[1] = 0xc7 }
        $rows.Add(('{0:x}: {1} {2}' -f ($imageBase+$row.Rva),(Hex-Bytes $listedGuard),$row.Text))
    }
    if ($Fault -eq 'UnknownGuard') {
        $rows[0] = $rows[0].Replace('cmp dword ptr [rsp + disp], max','<unknown>')
    }
    if ($Fault -eq 'OverlappingGuard') {
        $rows.Add(('{0:x}: c8 enter 0, 0' -f ($imageBase+$guardRva+7)))
    }
    $rows.Add(('{0:x}: {1} lea rcx, [rip + imagebase]' -f ($imageBase+$dispatchRva),(Hex-Bytes $lea)))
    $listedByteLoad = [byte[]]$byteLoad.Clone()
    if ($Fault -eq 'ListingMismatch') { $listedByteLoad[4] = [byte]($listedByteLoad[4]+1) }
    if ($Fault -eq 'PeListingMismatch') { $listedByteLoad[3] = 0x01 }
    $rows.Add(('{0:x}: {1} movzx eax, byte ptr [rcx + rax + remap]' -f ($imageBase+$dispatchRva+7),(Hex-Bytes $listedByteLoad)))
    $rows.Add(('{0:x}: {1} mov eax, dword ptr [rcx + rax*4 + targets]' -f ($imageBase+$dispatchRva+15),(Hex-Bytes $dwordLoad)))
    $rows.Add(('{0:x}: {1} add rax, rcx' -f ($imageBase+$dispatchRva+22),(Hex-Bytes $add)))
    $rows.Add(('{0:x}: {1} jmp rax' -f ($imageBase+$dispatchRva+25),(Hex-Bytes $jump)))
    $dispatchSteps = @(
        @{Name='GuardCmp';Offset=-21;Length=8}, @{Name='GuardJa';Offset=-13;Length=6}, @{Name='GuardIndex';Offset=-7;Length=7},
        @{Name='Base';Offset=0;Length=7}, @{Name='Remap';Offset=7;Length=8},
        @{Name='Dword';Offset=15;Length=7}, @{Name='Add';Offset=22;Length=3},
        @{Name='Jump';Offset=25;Length=2})
    foreach ($step in $dispatchSteps) {
        $stepAddress = [UInt64]([Int64]$imageBase + [Int64]$dispatchRva + [Int64]$step.Offset)
        if ($Fault -eq ("Duplicate"+$step.Name)) {
            $original = @($rows | Where-Object { $_.StartsWith(('{0:x}:' -f $stepAddress)) })[0]
            $rows.Add($original)
        }
        if ($Fault -eq ("Unknown"+$step.Name)) {
            $index = [Array]::IndexOf(@('GuardCmp','GuardJa','GuardIndex','Base','Remap','Dword','Add','Jump'),$step.Name)
            $prefix = ($rows[$index] -split ' (?=cmp |ja |lea|movzx|mov |add |jmp )',2)[0]
            $rows[$index] = $prefix + ' <unknown>'
        }
        if ($Fault -eq ("Overlap"+$step.Name)) {
            $rows.Add(('{0:x}: 90 nop' -f ($stepAddress+1)))
        }
        if ($Fault -eq ("PeMismatch"+$step.Name)) {
            # Keep the listed encoding intact; corrupt only one PE opcode byte.
            $pe[0x400+$dispatchRva+$step.Offset-$sectionRva] = 0x90
        }
    }
    $rows.Add(('{0:x}: 66 90 nop' -f ($imageBase+$targetRva)))
    for ($i=0; $i -lt 7; ++$i) {
        $rva = [UInt32]($dwordRva+4*$i)
        # Keep the listing's apparent vector rows independently literal, as a native
        # disassembler does when DWORD target bytes decode as VEX. Invalid target bytes
        # must stop proof and expose these rows to the ISA check.
        [byte[]]$bytes = [byte[]]@(0xc5,0x11,0x00,0x00)
        $rows.Add(('{0:x}: {1} vmovups xmm0, xmmword ptr [rax]' -f ($imageBase+$rva),(Hex-Bytes $bytes)))
    }
    if ($Fault -eq 'ActualVex') {
        [byte[]]$vex = [byte[]]@(0xc5,0xf8,0x10,0x00)
        Put-Bytes $pe (0x400+0x1300-$sectionRva) $vex
        $rows.Add(('{0:x}: {1} vmovups xmm0, xmmword ptr [rax]' -f ($imageBase+0x1300),(Hex-Bytes $vex)))
    }
    if ($Fault -eq 'ActualEvex') {
        [byte[]]$evex = [byte[]]@(0x62,0xf1,0x7c,0x48,0x10,0x00)
        Put-Bytes $pe (0x400+0x1300-$sectionRva) $evex
        $rows.Add(('{0:x}: {1} vmovups zmm0, zmmword ptr [rax]' -f ($imageBase+0x1300),(Hex-Bytes $evex)))
    }
    Set-Content -LiteralPath $disassembly -Value (@($rows | Sort-Object {
        [UInt64]::Parse(($_ -split ':',2)[0], [Globalization.NumberStyles]::HexNumber)
    }) -join "`n") -Encoding utf8
    [IO.File]::WriteAllBytes($binary, $pe)
    return @{Name=$Name; Binary=$binary; Metadata=$metadata; Symbols=$symbols; Disassembly=$disassembly}
}

function Assert-CompactRcxControl($Case, [bool]$ExpectedPass) {
    # The dispatch recognizer alone; the full gate's PDB-marked table proof is controlled in IsaMarkedTableTests.ps1. Under the
    # full gate the valid controls stay accepted and the genuine vector instructions (ActualVex, ActualEvex) stay rejected.
    $gate = [RedPrismBuild.IsaScanner]::Scan($Case.Binary, $Case.Metadata, $Case.Symbols, $Case.Disassembly,
        [string[]]@(), [string[]]@(), [string[]]@(), $false)
    if ($ExpectedPass -and $gate.Violations.Count -ne 0) { throw "$($Case.Name) violated the full ISA gate: $($gate.Violations -join '; ')" }
    if (-not $ExpectedPass -and $Case.Name -match 'vex' -and $gate.Violations.Count -eq 0) {
        throw "$($Case.Name): the full ISA gate accepted a genuine vector instruction."
    }
    $result = [RedPrismBuild.IsaScanner]::Scan($Case.Binary, $Case.Metadata, $Case.Symbols, $Case.Disassembly,
        [string[]]@(), [string[]]@(), [string[]]@(), $false)
    if ($ExpectedPass -and ($result.Violations.Count -ne 0 -or $result.InlineTableVectorData -ne 7)) {
        throw "$($Case.Name): expected seven proven inline DWORD rows and no violations; got $($result.InlineTableVectorData), $($result.Violations -join '; ')"
    }
    if (-not $ExpectedPass -and $result.Violations.Count -eq 0) { throw "$($Case.Name): malformed control bypassed the ISA gate." }
    if ($Case.Name -in 'ActualVex','ActualEvex' -and
            ($result.InlineTableVectorData -ne 7 -or $result.Violations.Count -ne 1 -or
             $result.EvexInstructions -ne [int]($Case.Name -eq 'ActualEvex'))) {
        throw "$($Case.Name): adjacent real vector must remain gated while all seven table rows stay data."
    }
    Write-Host "PASS isa-compact-rcx-byte-remap.$($Case.Name)"
}

Assert-CompactRcxControl (New-CompactRcxControl 'Valid') $true
Assert-CompactRcxControl (New-CompactRcxControl 'MinimumBound' '' 1 0) $true
Assert-CompactRcxControl (New-CompactRcxControl 'MaximumBound' '' 127 0x7fffffff) $true
foreach ($fault in @('BadRemapData','MiddleTarget','GuardTarget','GuardLimit','BoundZero','StackMismatch','WrongByteEncoding',
        'ListingMismatch','GuardListingMismatch','PeListingMismatch','ActualVex','ActualEvex',
        'MissingProcedure','ShortProcedure','MissingByteSymbol','MissingDwordSymbol','EarlySymbol',
        'GuardMissingWithLaterSymbol','MissingGuard','UnknownGuard','OverlappingGuard')) {
    Assert-CompactRcxControl (New-CompactRcxControl $fault $fault) $false
}
foreach ($step in 'GuardCmp','GuardJa','GuardIndex','Base','Remap','Dword','Add','Jump') {
    foreach ($prefix in 'Duplicate','Unknown','Overlap','PeMismatch') {
        $fault = $prefix+$step
        Assert-CompactRcxControl (New-CompactRcxControl $fault $fault) $false
    }
}
