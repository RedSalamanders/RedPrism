# Dispatch-form controls isolate the recognizers; the default image gate retains PDB-marked proof in IsaMarkedTableTests.ps1.
[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Synthetic parser controls only. These files are never executed as native code.
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if (-not ('RedPrismBuild.IsaScanner' -as [type])) {
    Add-Type -Path (Join-Path $repo 'Build/IsaScan.cs')
}
$fixtureRoot = Join-Path $repo ('.build/BuildProcessTests/IsaRdiBaseByteRemap-' + [guid]::NewGuid().ToString('N'))
[void](New-Item -ItemType Directory -Path $fixtureRoot -Force)
$script:isaRdiBaseByteRemapCount = 0

$imageBase = [UInt64]0x140000000
$sectionRva = [UInt32]0x0010f000
$dispatchRva = [UInt32]($sectionRva + 0x100)
$targetRva = [UInt32]($sectionRva + 0x8c5)
$guid = [guid]'6ac361e1-7a4a-4f19-a418-bc2f1d45c073'
$age = [UInt32]11

function Set-IrbU16([byte[]]$Bytes, [int]$Offset, [UInt16]$Value) {
    [Array]::Copy([BitConverter]::GetBytes($Value), 0, $Bytes, $Offset, 2)
}
function Set-IrbU32([byte[]]$Bytes, [int]$Offset, [UInt32]$Value) {
    [Array]::Copy([BitConverter]::GetBytes($Value), 0, $Bytes, $Offset, 4)
}
function Set-IrbU64([byte[]]$Bytes, [int]$Offset, [UInt64]$Value) {
    [Array]::Copy([BitConverter]::GetBytes($Value), 0, $Bytes, $Offset, 8)
}
function Put-IrbBytes([byte[]]$Bytes, [int]$Offset, [byte[]]$Value) {
    [Array]::Copy($Value, 0, $Bytes, $Offset, $Value.Length)
}
function Get-IrbHex([byte[]]$Bytes) {
    return (($Bytes | ForEach-Object { $_.ToString('x2') }) -join ' ')
}

function New-IrbCase {
    param(
        [Parameter(Mandatory)] [string]$Name,
        [string]$Fault = '',
        [UInt32]$GuardMaximum = 6
    )
    if ($GuardMaximum -gt 4095) { throw 'Fixture guard maximum must be in the recognized range 1..4095.' }
    $count = [UInt64]$GuardMaximum + 1
    if ($count -gt 4097) { throw 'The bounded fixture supports at most 4097 remap bytes.' }
    if ($Fault -eq 'OversizedCount') { $GuardMaximum = 4096; $count = 4097 }
    if ($GuardMaximum -lt 1) { throw 'Fixture guard maximum must be in the recognized range 1..4095.' }
    $directory = Join-Path $fixtureRoot $Name
    [void](New-Item -ItemType Directory -Path $directory -Force)
    $binary = Join-Path $directory 'SyntheticIsaParserControl.exe'
    $metadata = Join-Path $directory 'contributions.txt'
    $symbols = Join-Path $directory 'symbols.txt'
    $disassembly = Join-Path $directory 'disassembly.txt'

    # The table DWORD value 0x0010f8c5 is also the byte sequence c5 f8 10 00,
    # a real VEX-looking row when the linear disassembler reaches inline table data.
    [UInt32]$dwordCount = 7
    if ($Fault -eq 'InvalidIndex') { $count = 7; $GuardMaximum = 6 }
    # Preserve the VEX-looking target bytes while aligning the DWORD table; the
    # intervening 66 90 bytes are decoded code and never part of the exemption.
    $dwordRva = [UInt32](([UInt64]($targetRva + 1) + 3) -band [UInt64]4294967292)
    $byteRva = [UInt32]($dwordRva + 4 * $dwordCount)
    $byteEndRva = [UInt32]($byteRva + $count)
    [int]$alignmentPaddingLength = [int]((4 - ($byteEndRva % 4)) % 4)
    [byte[]]$padding = @(switch ($alignmentPaddingLength) {
        0 { [byte[]]@() }
        1 { [byte[]]@(0x90) }
        2 { [byte[]]@(0x66,0x90) }
        3 { [byte[]]@(0x0f,0x1f,0x00) }
    })
    if ($Fault -eq 'NoPadding') { $padding = [byte[]]@() }
    if ($Fault -eq 'Padding') { $padding = [byte[]]@($padding) + [byte[]]@(0x66,0x90,0x66,0x90) }
    $tableEndRva = [UInt32]($byteEndRva + $padding.Length)
    $sectionEndRva = [UInt32]($sectionRva + 0x3000)
    if ($tableEndRva -ge $sectionEndRva) { throw 'Synthetic tables exceed the bounded PE section.' }

    $pe = [byte[]]::new(0x3400)
    $pe[0] = 0x4d; $pe[1] = 0x5a
    Set-IrbU32 $pe 0x3c 0x80
    Set-IrbU32 $pe 0x80 0x00004550
    Set-IrbU16 $pe 0x84 0x8664
    Set-IrbU16 $pe 0x86 1
    Set-IrbU16 $pe 0x94 0xf0
    Set-IrbU16 $pe 0x98 0x20b
    Set-IrbU64 $pe (0x98+24) $imageBase
    Set-IrbU32 $pe (0x98+56) 0x3000
    Set-IrbU32 $pe (0x98+60) 0x400
    Set-IrbU32 $pe (0x98+112+6*8) ($sectionRva + 0x180)
    Set-IrbU32 $pe (0x98+116+6*8) 28
    $section = 0x80 + 24 + 0xf0
    Put-IrbBytes $pe $section ([Text.Encoding]::ASCII.GetBytes('.text' + [char]0 + [char]0 + [char]0))
    Set-IrbU32 $pe ($section+8) 0x3000
    Set-IrbU32 $pe ($section+12) $sectionRva
    Set-IrbU32 $pe ($section+16) 0x3000
    Set-IrbU32 $pe ($section+20) 0x400
    Set-IrbU32 $pe ($section+36) 0x60000020
    Set-IrbU32 $pe (0x400+0x180+12) 2
    Set-IrbU32 $pe (0x400+0x180+16) 24
    Set-IrbU32 $pe (0x400+0x180+20) ($sectionRva + 0x1a0)
    Set-IrbU32 $pe (0x400+0x180+24) 0x5a0
    Put-IrbBytes $pe 0x5a0 ([Text.Encoding]::ASCII.GetBytes('RSDS'))
    Put-IrbBytes $pe 0x5a4 $guid.ToByteArray()
    Set-IrbU32 $pe 0x5b4 $age

    # Prefix: lea eax,[rbx-64]; cmp eax,maximum; ja decoded default target.
    [byte[]]$bias = @(0x8d,0x43,0x9c)
    [byte[]]$compare = @(0x3d,0,0,0,0)
    [byte[]]$guardJump = @(0x0f,0x87,0,0,0,0)
    [byte[]]$cdqe = @(0x48,0x98)
    [byte[]]$leaBase = @(0x48,0x8d,0x3d,0,0,0,0)
    [byte[]]$byteLoad = @(0x0f,0xb6,0x84,0x07,0,0,0,0)
    [byte[]]$dwordLoad = @(0x8b,0x8c,0x87,0,0,0,0)
    [byte[]]$addBase = @(0x48,0x03,0xcf)
    [byte[]]$jumpTarget = @(0xff,0xe1)
    if ($Fault -eq 'GuardMismatch') { $bias[2] = 0x9b }
    if ($Fault -eq 'WrongBiasRegister') { $bias[1] = 0x41 }
    if ($Fault -eq 'WrongGuardOpcode') { $compare[0] = 0x81 }
    if ($Fault -eq 'WrongGuardCondition') { $guardJump[1] = 0x86 }
    if ($Fault -eq 'WrongCdqe') { $cdqe[1] = 0x99 }
    if ($Fault -eq 'WrongLeaDestination') { $leaBase[1] = 0x8d; $leaBase[2] = 0x35 }
    if ($Fault -eq 'WrongLeaBase') { $leaBase[2] = 0x15 }
    if ($Fault -eq 'WrongByteOpcode') { $byteLoad[1] = 0xbf }
    if ($Fault -eq 'WrongByteBase') { $byteLoad[3] = 0x06 }
    if ($Fault -eq 'WrongByteIndex') { $byteLoad[3] = 0x47 }
    if ($Fault -eq 'WrongByteDestination') { $byteLoad[2] = 0x8c }
    if ($Fault -eq 'WrongDwordOpcode') { $dwordLoad[0] = 0x89 }
    if ($Fault -eq 'WrongDwordBase') { $dwordLoad[2] = 0x86 }
    if ($Fault -eq 'WrongDwordIndex') { $dwordLoad[2] = 0x8f }
    if ($Fault -eq 'WrongDwordDestination') { $dwordLoad[1] = 0x84 }
    if ($Fault -eq 'WrongAdd') { $addBase[2] = 0xc7 }
    if ($Fault -eq 'WrongJump') { $jumpTarget[1] = 0xe0 }

    $cursor = [UInt32]$dispatchRva
    $ins = [Collections.Generic.List[hashtable]]::new()
    function Add-IrbInstruction([Collections.Generic.List[hashtable]]$List, [string]$Id, [UInt32]$Rva, [byte[]]$Bytes, [string]$Text) {
        $List.Add(@{ Id=$Id; Rva=$Rva; Bytes=$Bytes; Text=$Text })
    }
    Add-IrbInstruction $ins 'bias' $cursor $bias 'lea eax, [rbx - 0x64]'; $cursor += [UInt32]$bias.Length
    Add-IrbInstruction $ins 'cmp' $cursor $compare "cmp eax, 0x$($GuardMaximum.ToString('x'))"; $cursor += [UInt32]$compare.Length
    Add-IrbInstruction $ins 'ja' $cursor $guardJump 'ja default'; $cursor += [UInt32]$guardJump.Length
    Add-IrbInstruction $ins 'cdqe' $cursor $cdqe 'cdqe'; $cursor += [UInt32]$cdqe.Length
    Add-IrbInstruction $ins 'lea' $cursor $leaBase 'lea rdi, [rip + imagebase]'; $cursor += [UInt32]$leaBase.Length
    Add-IrbInstruction $ins 'byte' $cursor $byteLoad 'movzx eax, byte ptr [rdi + rax + byte-table]'; $cursor += [UInt32]$byteLoad.Length
    Add-IrbInstruction $ins 'dword' $cursor $dwordLoad 'mov ecx, dword ptr [rdi + 4*rax + dword-table]'; $cursor += [UInt32]$dwordLoad.Length
    Add-IrbInstruction $ins 'add' $cursor $addBase 'add rcx, rdi'; $cursor += [UInt32]$addBase.Length
    Add-IrbInstruction $ins 'jump' $cursor $jumpTarget 'jmp rcx'; $cursor += [UInt32]$jumpTarget.Length
    $dispatchEndRva = $cursor
    $defaultTarget = if ($Fault -eq 'GuardEscape') { [UInt32]($tableEndRva + 4) }
        elseif ($Fault -eq 'DefaultNotInTable') { [UInt32]($targetRva + 1) }
        else { $targetRva }
    $jumpOffset = [Int32]([Int64]$defaultTarget - [Int64]($ins[2].Rva + 6))
    Put-IrbBytes $guardJump 2 ([BitConverter]::GetBytes($jumpOffset))
    Put-IrbBytes $compare 1 ([BitConverter]::GetBytes([UInt32]$GuardMaximum))
    if ($Fault -eq 'WrongGuardLimit') { $compare[1] = [byte]($compare[1] -bxor 1) }
    $leaDisp = [Int32](-[Int64]($ins[4].Rva + 7))
    Put-IrbBytes $leaBase 3 ([BitConverter]::GetBytes($leaDisp))
    Put-IrbBytes $byteLoad 4 ([BitConverter]::GetBytes([UInt32]$byteRva))
    Put-IrbBytes $dwordLoad 3 ([BitConverter]::GetBytes([UInt32]$dwordRva))
    # Refresh instruction references after relocation fields are written.
    $ins[1].Bytes = $compare; $ins[2].Bytes = $guardJump; $ins[4].Bytes = $leaBase
    $ins[5].Bytes = $byteLoad; $ins[6].Bytes = $dwordLoad
    foreach ($item in $ins) { Put-IrbBytes $pe (0x400 + $item.Rva - $sectionRva) $item.Bytes }

    # All bytes between dispatch and target are decoded two-byte NOPs. The default
    # branch lands at a real instruction boundary; the procedure then reaches data.
    for ($rva = $dispatchEndRva; $rva -lt $targetRva; $rva += 2) {
        Put-IrbBytes $pe (0x400 + $rva - $sectionRva) ([byte[]]@(0x66,0x90))
    }
    Put-IrbBytes $pe (0x400 + $targetRva - $sectionRva) ([byte[]]@(0xc3))
    for ($rva = [UInt32]($targetRva + 1); $rva -lt $dwordRva; $rva += 2) {
        Put-IrbBytes $pe (0x400 + $rva - $sectionRva) ([byte[]]@(0x66,0x90))
    }
    if ($Fault -in @('GuardBypass','DirectTableEntry')) {
        $directRva = [UInt32]($targetRva - 8)
        $directTarget = if ($Fault -eq 'GuardBypass') { [UInt32]$ins[4].Rva } else { $dwordRva }
        [byte[]]$directJump = @(0xe9,0,0,0,0)
        $directDisplacement = [Int32]([Int64]$directTarget - [Int64]($directRva + 5))
        Put-IrbBytes $directJump 1 ([BitConverter]::GetBytes($directDisplacement))
        Put-IrbBytes $pe (0x400 + $directRva - $sectionRva) $directJump
        Put-IrbBytes $pe (0x400 + $directRva + 5 - $sectionRva) ([byte[]]@(0x90,0x66,0x90))
    }
    if ($Fault -eq 'InvalidTarget') { $targetValue = [UInt32]($dispatchEndRva + 1) } else { $targetValue = $targetRva }
    if ($Fault -eq 'TargetProtectedRow') { $targetValue = [UInt32]$ins[5].Rva }
    if ($Fault -eq 'OutsideProcedure') { $targetValue = [UInt32]($tableEndRva + 0x20) }
    if ($Fault -eq 'InteriorTarget') { $targetValue = [UInt32]($targetRva + 2) }
    if ($Fault -in @('KnownBodyTarget','UnknownTarget')) { $targetValue = [UInt32]($targetRva - 0x10) }
    for ($i = 0; $i -lt $dwordCount; ++$i) {
        # Keep the other entries vector-looking so malformed target controls
        # still exercise an ISA violation rather than removing its marker.
        $value = if ($i -eq ($dwordCount - 1)) { [UInt32]$targetValue } else { [UInt32]$targetRva }
        Set-IrbU32 $pe (0x400 + $dwordRva - $sectionRva + 4*$i) $value
    }
    [byte[]]$remap = [byte[]]::new([int]$count)
    for ($i=0; $i -lt $remap.Length; ++$i) { $remap[$i] = 0 }
    if ($Fault -eq 'InvalidIndex') { $remap[$remap.Length-1] = [byte]$dwordCount }
    Put-IrbBytes $pe (0x400 + $byteRva - $sectionRva) $remap
    if ($Fault -eq 'UnknownTarget') {
        # The target is mapped, but its decoded row is deliberately marked unknown below.
        $targetValue = [UInt32]($targetRva - 0x10)
        Set-IrbU32 $pe (0x400 + $dwordRva - $sectionRva + 4*($dwordCount-1)) $targetValue
    }
    if ($Fault -eq 'PeMismatch') { $pe[0x400 + $ins[4].Rva - $sectionRva] = 0x4c }

    $procedureEndRva = $tableEndRva
    if ($Fault -in @('WrongByteEnd','WrongByteEndOwner')) { $procedureEndRva = [UInt32]($tableEndRva + 4) }
    if ($Fault -eq 'ProcedureShort') { --$procedureEndRva }
    if ($Fault -eq 'ProcedureLong') { ++$procedureEndRva }
    if ($Fault -eq 'ProcedureWrongOwner') { $procOwner = 1 } else { $procOwner = 0 }
    $dwordOwner = if ($Fault -eq 'DwordWrongOwner') { 1 } else { 0 }
    $byteOwner = if ($Fault -eq 'ByteWrongOwner') { 1 } else { 0 }
    $dwordStartSymbol = $dwordRva
    $byteStartSymbol = $byteRva
    if ($Fault -eq 'DwordStartWrong') { ++$dwordStartSymbol }
    if ($Fault -eq 'ByteStartWrong') { ++$byteStartSymbol }

    $moduleHeader = "  Mod 0 | ``SyntheticIsa.obj``:`n  Obj: ``SyntheticIsa.lib``:`n"
    $metadataText = "  Age: $age`n  GUID: {$guid}`n$moduleHeader`nSection Contributions`n  SC[.text] | mod = 0, 0001:0, size = 12288, data crc = 0, reloc crc = 0`n"
    Set-Content -LiteralPath $metadata -Value $metadataText -Encoding utf8
    $symbolsText = $moduleHeader + "`n"
    if ($Fault -ne 'MissingProcedure') {
        $symbolsText += "  Mod $procOwner | ``SyntheticIsa.obj``:`n  0 | S_GPROC32 [size = 40] ``SyntheticDispatch```n  addr = 0001:$($dispatchRva-$sectionRva), code size = $($procedureEndRva-$dispatchRva)`n"
    }
    if ($Fault -ne 'MissingDwordStart') {
        $symbolsText += "  Mod $dwordOwner | ``SyntheticIsa.obj``:`n  0 | S_LDATA32 [size = 16] `````n  type = <no type>, addr = 0001:$($dwordStartSymbol-$sectionRva)`n"
    }
    if ($Fault -ne 'MissingByteStart') {
        $symbolsText += "  Mod $byteOwner | ``SyntheticIsa.obj``:`n  0 | S_LDATA32 [size = 16] `````n  type = <no type>, addr = 0001:$($byteStartSymbol-$sectionRva)`n"
    }
    if ($Fault -eq 'WrongByteEnd') {
        $symbolsText += "  Mod 0 | ``SyntheticIsa.obj``:`n  0 | S_LDATA32 [size = 16] `````n  type = <no type>, addr = 0001:$($byteEndRva-1-$sectionRva)`n"
    }
    if ($Fault -eq 'WrongByteEndOwner') {
        $symbolsText += "  Mod 1 | ``SyntheticIsa.obj``:`n  0 | S_LDATA32 [size = 16] `````n  type = <no type>, addr = 0001:$($tableEndRva-$sectionRva)`n"
    }
    if ($Fault -eq 'Padding') {
        $symbolsText += "  Mod 0 | ``SyntheticIsa.obj``:`n  0 | S_LDATA32 [size = 16] `````n  type = <no type>, addr = 0001:$($tableEndRva-$sectionRva)`n"
    }
    if ($Fault -eq 'AdjacentNopPadding') {
        $symbolsText += "  Mod 0 | ``SyntheticIsa.obj``:`n  0 | S_LDATA32 [size = 16] `````n  type = <no type>, addr = 0001:$($tableEndRva-$sectionRva)`n"
    }
    Set-Content -LiteralPath $symbols -Value $symbolsText -Encoding utf8

    $rows = [Collections.Generic.List[string]]::new()
    foreach ($item in $ins) {
        [byte[]]$shown = [byte[]]$item.Bytes.Clone()
        $text = [string]$item.Text
        if ($Fault -eq 'ListingMismatch' -and $item.Id -eq 'lea') { $shown[2] = 0x15 }
        if ($Fault -eq 'UnknownInstruction' -and $item.Id -eq 'byte') { $text = '<unknown>' }
        if ($Fault -eq 'MissingInstruction' -and $item.Id -eq 'add') { continue }
        $rows.Add(('{0:x}: {1} {2}' -f ($imageBase+$item.Rva),(Get-IrbHex $shown),$text))
        if ($Fault -eq 'Overlap' -and $item.Id -eq 'lea') {
            $rows.Add(('{0:x}: 90 nop' -f ($imageBase+$item.Rva+1)))
        }
    }
    for ($rva = $dispatchEndRva; $rva -lt $targetRva; $rva += 2) {
        if ($Fault -in @('GuardBypass','DirectTableEntry') -and $rva -ge $directRva -and $rva -lt ($directRva+8)) { continue }
        $rows.Add(('{0:x}: 66 90 nop' -f ($imageBase+$rva)))
    }
    if ($Fault -in @('GuardBypass','DirectTableEntry')) {
        $rows.Add(('{0:x}: {1} jmp 0x{2:x}' -f ($imageBase+$directRva),(Get-IrbHex $directJump),($imageBase+$directTarget)))
        $rows.Add(('{0:x}: 90 nop' -f ($imageBase+$directRva+5)))
        $rows.Add(('{0:x}: 66 90 nop' -f ($imageBase+$directRva+6)))
    }
    $rows.Add(('{0:x}: c3 ret' -f ($imageBase+$targetRva)))
    for ($rva = [UInt32]($targetRva + 1); $rva -lt $dwordRva; $rva += 2) {
        $rows.Add(('{0:x}: 66 90 nop' -f ($imageBase+$rva)))
    }
    for ($i = 0; $i -lt $dwordCount; ++$i) {
        $rva = [UInt32]($dwordRva+4*$i)
        if ($Fault -eq 'UnknownTableRow' -and $i -eq 0) { $text = '<unknown>' }
        else { $text = 'vmovups xmm0, xmmword ptr [rax]' }
        $bytes = [byte[]]$pe[(0x400+$rva-$sectionRva)..(0x400+$rva-$sectionRva+3)]
        $rows.Add(('{0:x}: {1} {2}' -f ($imageBase+$rva),(Get-IrbHex $bytes),$text))
    }
    for ($i = 0; $i -lt $remap.Length; ) {
        # Keep table rows byte-exact. The positive vector-looking payload lives in
        # the DWORD table; remap bytes use zero indices into valid DWORD entries.
        if ($i + 1 -lt $remap.Length) {
            $rows.Add(('{0:x}: 00 00 add byte ptr [rax], al' -f ($imageBase+$byteRva+$i)))
            $i += 2
        } else {
            $rows.Add(('{0:x}: {1:x2} <unknown>' -f ($imageBase+$byteRva+$i),$remap[$i]))
            ++$i
        }
    }
    if ($padding.Length -ne 0) {
        if ($Fault -eq 'WrongPadding') { $padding[0] = 0x91 }
        Put-IrbBytes $pe (0x400 + $byteEndRva - $sectionRva) $padding
        for ($padOffset = 0; $padOffset -lt $padding.Length; $padOffset += 2) {
            $padBytes = [byte[]]$padding[$padOffset..([Math]::Min($padOffset+1,$padding.Length-1))]
            $rows.Add(('{0:x}: {1} nop' -f ($imageBase+$byteEndRva+$padOffset),(Get-IrbHex $padBytes)))
        }
    }
    if ($Fault -eq 'AdjacentRealVex') {
        [byte[]]$outsideVector = @(0xc5,0xf8,0x10,0x00)
        Put-IrbBytes $pe (0x400 + $tableEndRva - $sectionRva) $outsideVector
        $rows.Add(('{0:x}: {1} vmovups xmm0, xmmword ptr [rax]' -f ($imageBase+$tableEndRva),(Get-IrbHex $outsideVector)))
    }
    if ($Fault -eq 'AdjacentRealEvex') {
        [byte[]]$outsideVector = @(0x62,0xf1,0x7c,0x48,0x58,0xc0)
        Put-IrbBytes $pe (0x400 + $tableEndRva - $sectionRva) $outsideVector
        $rows.Add(('{0:x}: {1} vaddps zmm0, zmm0, zmm0' -f ($imageBase+$tableEndRva),(Get-IrbHex $outsideVector)))
    }
    if ($Fault -eq 'UnknownTarget') {
        $unknownRva = [UInt32]($targetRva - 0x10)
        $targetRowFound = $false
        for ($i = 0; $i -lt $rows.Count; ++$i) {
            if ($rows[$i].StartsWith(('{0:x}:' -f ($imageBase+$unknownRva)))) {
                $rows[$i] = ('{0:x}: <unknown>' -f ($imageBase+$unknownRva))
                $targetRowFound = $true
                break
            }
        }
        if (-not $targetRowFound) { throw 'Unknown-target fixture did not reach its decoded body row.' }
    }
    Set-Content -LiteralPath $disassembly -Value (@($rows | Sort-Object {
        [UInt64]::Parse(($_ -split ':',2)[0], [Globalization.NumberStyles]::HexNumber)
    }) -join "`n") -Encoding utf8
    # The synthetic code filler spans these RVAs; restore a coherent RSDS record
    # after all code and table bytes have been written so PE/PDB identity survives.
    Set-IrbU32 $pe (0x400+0x180+12) 2
    Set-IrbU32 $pe (0x400+0x180+16) 24
    Set-IrbU32 $pe (0x400+0x180+20) ($sectionRva + 0x1a0)
    Set-IrbU32 $pe (0x400+0x180+24) 0x5a0
    Put-IrbBytes $pe 0x5a0 ([Text.Encoding]::ASCII.GetBytes('RSDS'))
    Put-IrbBytes $pe 0x5a4 $guid.ToByteArray()
    Set-IrbU32 $pe 0x5b4 $age
    [IO.File]::WriteAllBytes($binary, $pe)
    return @{ Name=$Name; Binary=$binary; Metadata=$metadata; Symbols=$symbols; Disassembly=$disassembly }
}

function Assert-IrbCase([hashtable]$Case, [bool]$ExpectedPass) {
    # The dispatch recognizer alone; the full gate's PDB-marked table proof is controlled in IsaMarkedTableTests.ps1. Under the
    # full gate the valid controls stay accepted and the genuine vector instructions (AdjacentRealVex, AdjacentRealEvex) stay
    # rejected.
    $gate = [RedPrismBuild.IsaScanner]::Scan($Case.Binary, $Case.Metadata, $Case.Symbols, $Case.Disassembly,
        [string[]]@(), [string[]]@(), [string[]]@(), $false)
    if ($ExpectedPass -and $gate.Violations.Count -ne 0) { throw "$($Case.Name) violated the full ISA gate: $($gate.Violations -join '; ')" }
    if (-not $ExpectedPass -and $Case.Name -match 'vex' -and $gate.Violations.Count -eq 0) {
        throw "$($Case.Name): the full ISA gate accepted a genuine vector instruction."
    }
    $result = [RedPrismBuild.IsaScanner]::Scan($Case.Binary, $Case.Metadata, $Case.Symbols, $Case.Disassembly,
        [string[]]@(), [string[]]@(), [string[]]@(), $false)
    if ($ExpectedPass -and $result.Violations.Count -ne 0) {
        throw "$($Case.Name): expected exact RDI byte-remap proof; got $($result.Violations -join '; ')"
    }
    if (-not $ExpectedPass -and $result.Violations.Count -eq 0) {
        throw "$($Case.Name): malformed RDI byte-remap control bypassed the ISA gate."
    }
    ++$script:isaRdiBaseByteRemapCount
    Write-Host "PASS isa-rdi-base-remap.$($Case.Name)"
}

Assert-IrbCase (New-IrbCase 'valid') $true
Assert-IrbCase (New-IrbCase 'relocated-guard-boundary' '' 1) $true
Assert-IrbCase (New-IrbCase 'maximum-guard-boundary' '' 4095) $true
Assert-IrbCase (New-IrbCase 'adjacent-66-90-padding' 'AdjacentNopPadding' 5) $true
Assert-IrbCase (New-IrbCase 'exact-no-padding' 'NoPadding' 3) $true
Assert-IrbCase (New-IrbCase 'unknown-table-decode-is-still-data' 'UnknownTableRow') $true
Assert-IrbCase (New-IrbCase 'known-body-target' 'KnownBodyTarget') $true
foreach ($fault in @(
    'PeMismatch','ListingMismatch','UnknownInstruction','MissingInstruction','Overlap',
    'GuardMismatch','WrongBiasRegister','WrongGuardOpcode','WrongGuardCondition','WrongGuardLimit','GuardEscape','WrongCdqe',
    'WrongLeaDestination','WrongLeaBase','WrongByteOpcode','WrongByteBase','WrongByteIndex','WrongByteDestination',
    'WrongDwordOpcode','WrongDwordBase','WrongDwordIndex','WrongDwordDestination','WrongAdd','WrongJump',
    'MissingProcedure','ProcedureShort','ProcedureLong','ProcedureWrongOwner',
    'MissingDwordStart','MissingByteStart','DwordStartWrong','ByteStartWrong','DwordWrongOwner','ByteWrongOwner',
    'WrongByteEnd','WrongByteEndOwner','WrongPadding','Padding','InvalidIndex','InvalidTarget','OutsideProcedure','InteriorTarget',
    'UnknownTarget','OversizedCount','DefaultNotInTable','AdjacentRealVex','AdjacentRealEvex',
    'GuardBypass','DirectTableEntry','TargetProtectedRow'
)) {
    Assert-IrbCase (New-IrbCase $fault $fault) $false
}
Write-Host "RDI image-base byte-remap synthetic controls completed: $script:isaRdiBaseByteRemapCount"
