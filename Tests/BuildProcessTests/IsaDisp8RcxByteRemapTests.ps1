[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if (-not ('RedPrismBuild.IsaScanner' -as [type])) {
    Add-Type -Path (Join-Path $repo 'Build/IsaScan.cs')
}
$fixtureRoot = Join-Path $repo ('.build/BuildProcessTests/IsaDisp8RcxByteRemap-' + [guid]::NewGuid().ToString('N'))
[void](New-Item -ItemType Directory -Path $fixtureRoot -Force)
$imageBase = [UInt64]0x140000000
$sectionRva = [UInt32]0x1000
$dispatchRva = [UInt32]0x1100
$prefixRva = [UInt32]($dispatchRva - 18)
$dwordRva = [UInt32]0x1200
$dwordCount = 7
$byteRva = [UInt32]($dwordRva + 4 * $dwordCount)
$targetRva = [UInt32]0x11c5
$guid = [guid]'ca03f2bf-42fb-43c3-9f26-b4653bc7a16e'
$age = [UInt32]3
$controlByteCount = 45
$caseCount = 0

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
function New-Disp8RcxControl([string]$Name, [string]$Fault = '', [long]$Maximum = 163, [int]$StackDisplacement = 0x20) {
    $dir = Join-Path $fixtureRoot $Name
    [void](New-Item -ItemType Directory -Path $dir -Force)
    $binary = Join-Path $dir 'Synthetic.exe'
    $metadata = Join-Path $dir 'contributions.txt'
    $symbols = Join-Path $dir 'symbols.txt'
    $disassembly = Join-Path $dir 'disassembly.txt'
    $pe = [byte[]]::new(0x3400)
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

    # Literal 18-byte prefix: CMP dword [RSP+disp8],imm32; JA rel32; MOV EAX,[RSP+same disp8].
    # The following independent literal dispatch is 27 bytes, for exactly 45 control bytes.
    if ($Maximum -lt [Int64]::MinValue -or $Maximum -gt [UInt32]::MaxValue) { throw 'Fixture immediate must fit uint32.' }
    if ($StackDisplacement -lt 0 -or $StackDisplacement -gt 255) { throw 'Fixture displacement must fit disp8.' }
    $faultForEncoding = switch ($Fault) {
        'LaterSymbolGuardOpcode' { 'WrongGuardOpcode' }
        'LaterSymbolGuardRegister' { 'WrongGuardRegister' }
        'LaterSymbolStackDisp' { 'WrongStackDisp' }
        default { $Fault }
    }
    [byte[]]$guard = [byte[]]@(0x81,0x7c,0x24,[byte]$StackDisplacement,0,0,0,0,
        0x0f,0x87,0,0,0,0,
        0x8b,0x44,0x24,[byte]$StackDisplacement)
    $maximumBits = if ($Maximum -lt 0) { [UInt32]::MaxValue } else { [UInt32]$Maximum }
    Put-Bytes $guard 4 ([BitConverter]::GetBytes([UInt32]$maximumBits))
    $defaultTarget = [UInt32]$targetRva
    if ($Fault -eq 'BadDefault') { $defaultTarget = [UInt32]($targetRva + 1) }
    if ($Fault -eq 'DefaultOutsideProcedure') { $defaultTarget = [UInt32]0x1350 }
    if ($Fault -eq 'DefaultOutsideContribution') { $defaultTarget = [UInt32]0x3100 }
    $branchEndRva = [UInt32]($prefixRva + 14)
    $branchDisplacement = [Int32]([Int64]$defaultTarget - [Int64]$branchEndRva)
    Put-Bytes $guard 10 ([BitConverter]::GetBytes($branchDisplacement))

    [byte[]]$lea = [byte[]]@(0x48,0x8d,0x0d,0,0,0,0)
    Put-Bytes $lea 3 ([BitConverter]::GetBytes([Int32]($imageBase - ($imageBase+$dispatchRva+7))))
    [byte[]]$byteLoad = [byte[]]@(0x0f,0xb6,0x84,0x01,0,0,0,0)
    Put-Bytes $byteLoad 4 ([BitConverter]::GetBytes([UInt32]$byteRva))
    [byte[]]$dwordLoad = [byte[]]@(0x8b,0x84,0x81,0,0,0,0)
    Put-Bytes $dwordLoad 3 ([BitConverter]::GetBytes([UInt32]$dwordRva))
    [byte[]]$add = [byte[]]@(0x48,0x03,0xc1)
    [byte[]]$jump = [byte[]]@(0xff,0xe0)
    [byte[]]$control = $guard + $lea + $byteLoad + $dwordLoad + $add + $jump
    if ($control.Length -ne $controlByteCount) { throw "Expected a $controlByteCount-byte guard and dispatch, got $($control.Length)." }
    Put-Bytes $pe (0x400+$prefixRva-$sectionRva) $guard
    Put-Bytes $pe (0x400+$dispatchRva-$sectionRva) $lea
    Put-Bytes $pe (0x400+$dispatchRva+7-$sectionRva) $byteLoad
    Put-Bytes $pe (0x400+$dispatchRva+15-$sectionRva) $dwordLoad
    Put-Bytes $pe (0x400+$dispatchRva+22-$sectionRva) $add
    Put-Bytes $pe (0x400+$dispatchRva+25-$sectionRva) $jump

    [byte[]]$dwords = [byte[]]::new(4*$dwordCount)
    for ($i=0; $i -lt $dwordCount; ++$i) {
        $target = [UInt32]$targetRva
        if ($Fault -eq 'BadTargetBoundary' -and $i -eq 2) { $target = [UInt32]($targetRva+1) }
        if ($Fault -eq 'TargetOutsideProcedure' -and $i -eq 2) { $target = [UInt32]0x1350 }
        if ($Fault -eq 'TargetOutsideContribution' -and $i -eq 2) { $target = [UInt32]0x3100 }
        Put-Bytes $dwords (4*$i) ([BitConverter]::GetBytes($target))
    }
    Put-Bytes $pe (0x400+$dwordRva-$sectionRva) $dwords
    $remapLength = [Int64]$Maximum + 1
    if ($remapLength -lt 1 -or $remapLength -gt 4097) { $remapLength = 1 }
    [byte[]]$remap = [byte[]]::new([int]$remapLength)
    for ($i=0; $i -lt $remap.Length; ++$i) { $remap[$i] = [byte]($i % $dwordCount) }
    if ($Fault -eq 'BadRemapIndex') { $remap[$remap.Length-1] = [byte]$dwordCount }
    if ($Fault -eq 'NegativeRemapIndex') { $remap[$remap.Length-1] = 0xff }
    Put-Bytes $pe (0x400+$byteRva-$sectionRva) $remap
    Put-Bytes $pe (0x400+$targetRva-$sectionRva) ([byte[]]@(0x66,0x90))

    # Opcode/operand mutations are applied only to PE bytes; row mutations below are independent.
    switch ($faultForEncoding) {
        'WrongGuardOpcode' { $pe[0x400+$prefixRva-$sectionRva] = 0x69 }
        'WrongGuardRegister' { $pe[0x400+$prefixRva+1-$sectionRva] = 0x74 }
        'WrongGuardSib' { $pe[0x400+$prefixRva+2-$sectionRva] = 0x25 }
        'WrongBranchOpcode' { $pe[0x400+$prefixRva+9-$sectionRva] = 0x86 }
        'WrongIndexOpcode' { $pe[0x400+$prefixRva+14-$sectionRva] = 0x89 }
        'WrongIndexRegister' { $pe[0x400+$prefixRva+15-$sectionRva] = 0x4c }
        'WrongIndexSib' { $pe[0x400+$prefixRva+16-$sectionRva] = 0x25 }
        'WrongStackDisp' { $pe[0x400+$prefixRva+17-$sectionRva] = [byte](($StackDisplacement+1)-band 0xff) }
        'WrongLeaBase' { $pe[0x400+$dispatchRva+2-$sectionRva] = 0x15 }
        'WrongRemapOpcode' { $pe[0x400+$dispatchRva+7-$sectionRva] = 0x0e }
        'WrongRemapRegister' { $pe[0x400+$dispatchRva+9-$sectionRva] = 0x8c }
        'WrongRemapSib' { $pe[0x400+$dispatchRva+10-$sectionRva] = 0x00 }
        'WrongRemapScale' { $pe[0x400+$dispatchRva+10-$sectionRva] = 0x41 }
        'WrongRemapBase' { $pe[0x400+$dispatchRva+10-$sectionRva] = 0x81 }
        'WrongDwordOpcode' { $pe[0x400+$dispatchRva+15-$sectionRva] = 0x89 }
        'WrongDwordRegister' { $pe[0x400+$dispatchRva+16-$sectionRva] = 0x8c }
        'WrongDwordSib' { $pe[0x400+$dispatchRva+17-$sectionRva] = 0x80 }
        'WrongDwordScale' { $pe[0x400+$dispatchRva+17-$sectionRva] = 0x01 }
        'WrongDwordBase' { $pe[0x400+$dispatchRva+17-$sectionRva] = 0x88 }
        'WrongByteTableDisplacement' { $pe[0x400+$dispatchRva+11-$sectionRva]++ }
        'WrongDwordTableDisplacement' { $pe[0x400+$dispatchRva+18-$sectionRva]++ }
    }
    if ($Fault -eq 'PeMismatchDwordRow') { $pe[0x400+$dwordRva-$sectionRva] = 0xc4 }
    # Keep malformed instruction bytes identical in the PE and reported disassembly so each
    # negative isolates opcode/operand recognition rather than PE/listing disagreement.
    [Array]::Copy($pe, 0x400+$prefixRva-$sectionRva, $guard, 0, 18)
    [Array]::Copy($pe, 0x400+$dispatchRva-$sectionRva, $lea, 0, 7)
    [Array]::Copy($pe, 0x400+$dispatchRva+7-$sectionRva, $byteLoad, 0, 8)
    [Array]::Copy($pe, 0x400+$dispatchRva+15-$sectionRva, $dwordLoad, 0, 7)

    $procedureEndRva = [UInt32]($byteRva + $remap.Length)
    $procedureBegin = [UInt32]$prefixRva
    $procedureSize = [UInt32]($procedureEndRva - $procedureBegin)
    $module = "  Mod 0000 | ``Synthetic.obj``:`n  Obj: ``Synthetic.lib```n"
    $metadataText = "  Age: $age`n  GUID: {$guid}`n$module`nSection Contributions`n  SC[.text]  | mod = 0, 0001:0, size = 8192, data crc = 0, reloc crc = 0`n"
    Set-Content -LiteralPath $metadata -Value $metadataText -Encoding utf8
    $symbolText = $module +
        "  0 | S_GPROC32 [size = 40] ``SyntheticDispatch```n    addr = 0001:$($procedureBegin-$sectionRva), code size = $procedureSize`n" +
        "  0 | S_LDATA32 [size = 16] `````n  type = <no type>, addr = 0001:$($dwordRva-$sectionRva)`n" +
        "  0 | S_LDATA32 [size = 16] `````n  type = <no type>, addr = 0001:$($byteRva-$sectionRva)`n"
    if ($Fault -eq 'MissingProcedure') {
        $symbolText = $module + "  0 | S_LDATA32 [size = 16] `````n  type = <no type>, addr = 0001:$($dwordRva-$sectionRva)`n" +
            "  0 | S_LDATA32 [size = 16] `````n  type = <no type>, addr = 0001:$($byteRva-$sectionRva)`n"
    }
    if ($Fault -eq 'ShortProcedure') { $symbolText = $symbolText.Replace("code size = $procedureSize", "code size = $($procedureSize-1)") }
    if ($Fault -eq 'LongProcedure') { $symbolText = $symbolText.Replace("code size = $procedureSize", "code size = $($procedureSize+1)") }
    if ($Fault -eq 'ForeignProcedure') {
        $symbolText = $symbolText.Replace('  Mod 0000', '  Mod 0001').Replace('  0 | S_GPROC32', '  1 | S_GPROC32')
    }
    if ($Fault -in @('MissingByteSymbol','EarlierByteSymbol','LaterByteSymbol')) {
        $symbolText = $symbolText.Replace("  0 | S_LDATA32 [size = 16] `````n  type = <no type>, addr = 0001:$($byteRva-$sectionRva)`n",'')
    }
    if ($Fault -in @('MissingDwordSymbol','EarlierDwordSymbol','LaterDwordSymbol')) {
        $symbolText = $symbolText.Replace("  0 | S_LDATA32 [size = 16] `````n  type = <no type>, addr = 0001:$($dwordRva-$sectionRva)`n",'')
    }
    if ($Fault -eq 'EarlierByteSymbol') {
        $symbolText += "  0 | S_LDATA32 [size = 16] `````n  type = <no type>, addr = 0001:$($byteRva-$sectionRva-1)`n"
    }
    if ($Fault -eq 'LaterByteSymbol') {
        $symbolText += "  0 | S_LDATA32 [size = 16] `````n  type = <no type>, addr = 0001:$($byteRva-$sectionRva+1)`n"
    }
    if ($Fault -eq 'EarlierDwordSymbol') {
        $symbolText += "  0 | S_LDATA32 [size = 16] `````n  type = <no type>, addr = 0001:$($dwordRva-$sectionRva-1)`n"
    }
    if ($Fault -eq 'LaterDwordSymbol') {
        $symbolText += "  0 | S_LDATA32 [size = 16] `````n  type = <no type>, addr = 0001:$($dwordRva-$sectionRva+1)`n"
    }
    if ($Fault -in @('LaterSymbolGuardOpcode','LaterSymbolGuardRegister','LaterSymbolStackDisp')) {
        # This boundary would let the former symbol-bounded fallback infer the byte-table length.
        $symbolText += "  0 | S_LDATA32 [size = 16] `````n  type = <no type>, addr = 0001:$($procedureEndRva-$sectionRva)`n"
    }
    if ($Fault -eq 'ForeignDataSymbols') {
        $symbolText = $symbolText.Replace("S_LDATA32 [size = 16] `````n  type = <no type>, addr = 0001:$($dwordRva-$sectionRva)", "S_LDATA32 [size = 16] `````n  type = <no type>, addr = 0002:$($dwordRva-$sectionRva)")
    }
    Set-Content -LiteralPath $symbols -Value $symbolText -Encoding utf8

    $rows = [Collections.Generic.List[string]]::new()
    $guardRows = @(
        @{Name='GuardCmp';Rva=$prefixRva;Bytes=$guard[0..7];Text='cmp dword ptr [rsp + disp], imm32'},
        @{Name='GuardJa';Rva=($prefixRva+8);Bytes=$guard[8..13];Text='ja default'},
        @{Name='GuardIndex';Rva=($prefixRva+14);Bytes=$guard[14..17];Text='mov eax, dword ptr [rsp + disp]'})
    foreach ($row in $guardRows) {
        [byte[]]$listed = [byte[]]$row.Bytes.Clone()
        $rowFault = $Fault -replace '^(Missing|Duplicate|Unknown|Overlap|PeMismatch|ListingMismatch)',''
        $rowOp = [regex]::Match($Fault,'^(Missing|Duplicate|Unknown|Overlap|PeMismatch|ListingMismatch)').Groups[1].Value
        if ($row.Name -eq $rowFault -and $rowOp -eq 'Missing') { continue }
        if ($row.Name -eq $rowFault -and $rowOp -eq 'Unknown') { $row.Text = '<unknown>' }
        if ($row.Name -eq $rowFault -and $rowOp -eq 'ListingMismatch') { $listed[0] = [byte]($listed[0] -bxor 1) }
        if ($row.Name -eq $rowFault -and $rowOp -eq 'PeMismatch') {
            $pe[0x400+$row.Rva-$sectionRva] = [byte]($pe[0x400+$row.Rva-$sectionRva] -bxor 1)
        }
        $rows.Add(('{0:x}: {1} {2}' -f ($imageBase+$row.Rva),(Hex-Bytes $listed),$row.Text))
        if ($row.Name -eq $rowFault -and $rowOp -eq 'Duplicate') { $rows.Add($rows[$rows.Count-1]) }
        if ($row.Name -eq $rowFault -and $rowOp -eq 'Overlap') { $rows.Add(('{0:x}: 90 nop' -f ($imageBase+$row.Rva+1))) }
    }
    $dispatchRows = @(
        @{Name='Base';Rva=$dispatchRva;Bytes=$lea;Text='lea rcx, [rip + imagebase]'},
        @{Name='Remap';Rva=($dispatchRva+7);Bytes=$byteLoad;Text='movzx eax, byte ptr [rcx + rax + remap]'},
        @{Name='Dword';Rva=($dispatchRva+15);Bytes=$dwordLoad;Text='mov eax, dword ptr [rcx + rax*4 + targets]'},
        @{Name='Add';Rva=($dispatchRva+22);Bytes=$add;Text='add rax, rcx'},
        @{Name='Jump';Rva=($dispatchRva+25);Bytes=$jump;Text='jmp rax'})
    foreach ($row in $dispatchRows) {
        [byte[]]$listed = [byte[]]$row.Bytes.Clone()
        $rowFault = $Fault -replace '^(Missing|Duplicate|Unknown|Overlap|PeMismatch|ListingMismatch)',''
        $rowOp = [regex]::Match($Fault,'^(Missing|Duplicate|Unknown|Overlap|PeMismatch|ListingMismatch)').Groups[1].Value
        if ($row.Name -eq $rowFault -and $rowOp -eq 'Missing') { continue }
        if ($row.Name -eq $rowFault -and $rowOp -eq 'Unknown') { $row.Text = '<unknown>' }
        if ($row.Name -eq $rowFault -and $rowOp -eq 'ListingMismatch') { $listed[$listed.Length-1] = [byte]($listed[$listed.Length-1] -bxor 1) }
        if ($row.Name -eq $rowFault -and $rowOp -eq 'PeMismatch') {
            $pe[0x400+$row.Rva-$sectionRva] = [byte]($pe[0x400+$row.Rva-$sectionRva] -bxor 1)
        }
        $rows.Add(('{0:x}: {1} {2}' -f ($imageBase+$row.Rva),(Hex-Bytes $listed),$row.Text))
        if ($row.Name -eq $rowFault -and $rowOp -eq 'Duplicate') { $rows.Add($rows[$rows.Count-1]) }
        if ($row.Name -eq $rowFault -and $rowOp -eq 'Overlap') { $rows.Add(('{0:x}: 90 nop' -f ($imageBase+$row.Rva+1))) }
    }
    if ($Fault -eq 'RealVexInBody' -or $Fault -eq 'RealEvexInBody' -or $Fault -eq 'RealVexAdjacent' -or $Fault -eq 'RealEvexAdjacent') {
        $vectorRva = if ($Fault.EndsWith('Adjacent')) { [UInt32]$procedureEndRva } else { [UInt32]$targetRva }
        if ($Fault.StartsWith('RealVex')) { [byte[]]$vector = [byte[]]@(0xc5,0xf8,0x10,0x00); $vectorText = 'vmovups xmm0, xmmword ptr [rax]' }
        else { [byte[]]$vector = [byte[]]@(0x62,0xf1,0x7c,0x48,0x10,0x00); $vectorText = 'vmovups zmm0, zmmword ptr [rax]' }
        Put-Bytes $pe (0x400+$vectorRva-$sectionRva) $vector
        $rows.Add(('{0:x}: {1} {2}' -f ($imageBase+$vectorRva),(Hex-Bytes $vector),$vectorText))
    }
    if ($Fault -notin @('RealVexInBody','RealEvexInBody')) {
        $rows.Add(('{0:x}: 66 90 nop' -f ($imageBase+$targetRva)))
    }
    for ($i=0; $i -lt $dwordCount; ++$i) {
        $rva = [UInt32]($dwordRva+4*$i)
        # These byte-identical PE DWORDs decode as apparent VEX rows; only table proof can suppress them.
        [byte[]]$fakeVector = [byte[]]@(0xc5,0x11,0x00,0x00)
        if ($Fault -eq 'ListingMismatchDwordRow' -and $i -eq 0) { $fakeVector[0] = 0xc4 }
        $rows.Add(('{0:x}: {1} vmovups xmm0, xmmword ptr [rax]' -f ($imageBase+$rva),(Hex-Bytes $fakeVector)))
    }
    Set-Content -LiteralPath $disassembly -Value (@($rows | Sort-Object {
        [UInt64]::Parse(($_ -split ':',2)[0], [Globalization.NumberStyles]::HexNumber)
    }) -join "`n") -Encoding utf8
    [IO.File]::WriteAllBytes($binary, $pe)
    return @{Name=$Name; Binary=$binary; Metadata=$metadata; Symbols=$symbols; Disassembly=$disassembly; Maximum=$Maximum; Fault=$Fault}
}

function Assert-Disp8RcxControl($Case, [bool]$ExpectedPass, [bool]$VectorViolation = $false) {
    $result = [RedPrismBuild.IsaScanner]::Scan($Case.Binary, $Case.Metadata, $Case.Symbols, $Case.Disassembly,
        [string[]]@(), [string[]]@(), [string[]]@())
    if ($ExpectedPass -and ($result.Violations.Count -ne 0 -or $result.InlineTableVectorData -ne $dwordCount)) {
        throw "$($Case.Name): expected $dwordCount proven inline DWORD rows and no violations; got $($result.InlineTableVectorData), $($result.Violations -join '; ')"
    }
    if (-not $ExpectedPass -and -not $VectorViolation -and $result.Violations.Count -eq 0) {
        throw "$($Case.Name): malformed control bypassed the ISA gate."
    }
    if ($VectorViolation -and ($result.InlineTableVectorData -ne $dwordCount -or $result.Violations.Count -ne 1 -or
            $result.EvexInstructions -ne [int]($Case.Fault.StartsWith('RealEvex')))) {
        throw "$($Case.Name): the real vector instruction must remain gated while all $dwordCount table DWORDs stay data."
    }
    ++$script:caseCount
    Write-Host "PASS isa-disp8-rcx-byte-remap.$($Case.Name)"
}

foreach ($maximum in @(1,163,4095)) {
    foreach ($disp in @(0,0x20,0x7f,0x80,0xff)) {
        $name = 'ValidMax{0}Disp{1:x2}' -f $maximum,$disp
        Assert-Disp8RcxControl (New-Disp8RcxControl $name '' $maximum $disp) $true
    }
}
foreach ($badMaximum in @(0,4096,-1)) {
    $name = 'InvalidMax' + [string]$badMaximum
    Assert-Disp8RcxControl (New-Disp8RcxControl $name '' $badMaximum 0x20) $false
}

$guardAndDispatchRows = @('GuardCmp','GuardJa','GuardIndex','Base','Remap','Dword','Add','Jump')
foreach ($row in $guardAndDispatchRows) {
    foreach ($kind in @('Missing','Duplicate','Unknown','Overlap','PeMismatch','ListingMismatch')) {
        $fault = $kind+$row
        Assert-Disp8RcxControl (New-Disp8RcxControl $fault $fault 163 0x20) $false
    }
}
$additionalFaults = @(
        'WrongGuardOpcode','WrongGuardRegister','WrongGuardSib','WrongBranchOpcode','WrongIndexOpcode','WrongIndexRegister',
        'WrongIndexSib','WrongStackDisp','WrongLeaBase','WrongRemapOpcode','WrongRemapRegister','WrongRemapSib',
        'WrongRemapScale','WrongRemapBase','WrongDwordOpcode','WrongDwordRegister','WrongDwordSib','WrongDwordScale',
        'WrongDwordBase','WrongByteTableDisplacement','WrongDwordTableDisplacement','BadRemapIndex','NegativeRemapIndex',
        'BadDefault','DefaultOutsideProcedure','DefaultOutsideContribution','BadTargetBoundary','TargetOutsideProcedure',
        'TargetOutsideContribution','MissingProcedure','ShortProcedure','LongProcedure','ForeignProcedure','MissingByteSymbol',
        'MissingDwordSymbol','EarlierByteSymbol','LaterByteSymbol','EarlierDwordSymbol','LaterDwordSymbol','ForeignDataSymbols',
        'PeMismatchDwordRow','ListingMismatchDwordRow','LaterSymbolGuardOpcode','LaterSymbolGuardRegister','LaterSymbolStackDisp')
foreach ($fault in $additionalFaults) {
    Assert-Disp8RcxControl (New-Disp8RcxControl $fault $fault 163 0x20) $false
}
foreach ($fault in @('RealVexInBody','RealEvexInBody','RealVexAdjacent','RealEvexAdjacent')) {
    Assert-Disp8RcxControl (New-Disp8RcxControl $fault $fault 163 0x20) $false $true
}

Write-Host "PASS isa-disp8-rcx-byte-remap.summary cases=$caseCount control-bytes=$controlByteCount positive-matrix=3x5 invalid-maxima=3 rows=8x6 additional-faults=$($additionalFaults.Count) vector-controls=4"
