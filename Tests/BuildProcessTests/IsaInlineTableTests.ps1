[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$fixtureRoot = Join-Path $repo ('.build/BuildProcessTests/IsaInlineTable-' + [guid]::NewGuid().ToString('N'))
[void](New-Item -ItemType Directory -Path $fixtureRoot -Force)
if (-not ('RedPrismBuild.IsaScanner' -as [type])) {
    Add-Type -Path (Join-Path $repo 'Build/IsaScan.cs')
}

# These byte arrays and listings are parser controls, not native code or CPU execution tests.
    $imageBase = [UInt64]0x140000000
$sectionRva = [UInt32]0x0010f000
$tableRva = [UInt32]($sectionRva + 0x1000)
$dispatchRva = $sectionRva
$targetRva = [UInt32]0x0010f8c5
$guid = [guid]'9c1e2d4b-6a70-4a8b-9012-3456789abcde'
$age = [UInt32]7

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

function New-SyntheticCase {
    param(
        [Parameter(Mandatory)] [string]$Name,
        [string]$Fault = '',
        [switch]$OptionalByteIndex,
        [switch]$RealVex,
        [switch]$ShortNop,
        [switch]$R8Base,
        [switch]$HoistedR8Base,
        [switch]$RaxBase,
        [ValidateSet(0,1,2)] [int]$TargetRegister = 0,
        [switch]$PdbData
    )
    $directory = Join-Path $fixtureRoot $Name
    [void](New-Item -ItemType Directory -Path $directory -Force)
    $binary = Join-Path $directory 'SyntheticIsaParserControl.exe'
    $metadata = Join-Path $directory 'contributions.txt'
    $symbols = Join-Path $directory 'symbols.txt'
    $disassembly = Join-Path $directory 'disassembly.txt'

    # One small PE32+ image, with a single executable section and an embedded RSDS identity.
    $pe = [byte[]]::new(0x2400)
    $pe[0] = 0x4d; $pe[1] = 0x5a
    Set-U32 $pe 0x3c 0x80
    Set-U32 $pe 0x80 0x00004550
    Set-U16 $pe 0x84 0x8664
    Set-U16 $pe 0x86 1
    Set-U16 $pe 0x94 0xf0
    Set-U16 $pe 0x98 0x20b
    Set-U64 $pe (0x98+24) $imageBase
    Set-U32 $pe (0x98+56) ($sectionRva + 0x2000)
    Set-U32 $pe (0x98+60) 0x400
    Set-U32 $pe (0x98+112+6*8) ($sectionRva + 0x180)
    Set-U32 $pe (0x98+116+6*8) 28
    $section = 0x80 + 24 + 0xf0
    Put-Bytes $pe $section ([Text.Encoding]::ASCII.GetBytes('.text' + [char]0 + [char]0 + [char]0))
    Set-U32 $pe ($section+8) 0x2000
    Set-U32 $pe ($section+12) $sectionRva
    Set-U32 $pe ($section+16) 0x2000
    Set-U32 $pe ($section+20) 0x400
    Set-U32 $pe ($section+36) 0x60000020
    # IMAGE_DEBUG_DIRECTORY: type CodeView, size 24, raw pointer 0x5a0.
    Set-U32 $pe (0x400+0x180+12) 2
    Set-U32 $pe (0x400+0x180+16) 24
    Set-U32 $pe (0x400+0x180+20) ($sectionRva + 0x1a0)
    Set-U32 $pe (0x400+0x180+24) 0x5a0
    Put-Bytes $pe 0x5a0 ([Text.Encoding]::ASCII.GetBytes('RSDS'))
    Put-Bytes $pe 0x5a4 $guid.ToByteArray()
    Set-U32 $pe 0x5b4 $age

    if ($HoistedR8Base -and (-not $R8Base -or -not $PdbData -or $OptionalByteIndex)) {
        throw 'HoistedR8Base requires R8Base and PdbData, without OptionalByteIndex.'
    }
    if ($RaxBase -and (-not $PdbData -or $R8Base -or $HoistedR8Base -or $OptionalByteIndex)) {
        throw 'RaxBase requires PdbData and cannot be combined with R8Base, HoistedR8Base, or OptionalByteIndex.'
    }
    $caseDispatchRva = if ($HoistedR8Base) { [UInt32]($sectionRva + 0x880) } else { $dispatchRva }
    [byte[]]$lea = if ($RaxBase) { [byte[]]@(0x48,0x8d,0x05,0,0,0,0) } else { [byte[]]@(0x48,0x8d,0x0d,0,0,0,0) }
    if ($R8Base) { $lea[0] = 0x4c; $lea[2] = 0x05 }
    if ($Fault -eq 'WrongImageBase') { $leaDisp = [Int32]($imageBase - ($imageBase + $caseDispatchRva + 7) + 1) }
    else { $leaDisp = [Int32]($imageBase - ($imageBase + $caseDispatchRva + 7)) }
    Put-Bytes $lea 3 ([BitConverter]::GetBytes($leaDisp))
    Put-Bytes $pe (0x400+$caseDispatchRva-$sectionRva) $lea
    $cursor = [UInt32]($caseDispatchRva + 7)
    $instructions = [Collections.Generic.List[string]]::new()
    $leaText = if ($RaxBase) { 'lea rax, [rip+imagebase]' } elseif ($R8Base) { 'lea r8, [rip+imagebase]' } else { 'lea rcx, [rip+imagebase]' }
    $instructions.Add(('{0:x}: {1} {2}' -f ($imageBase+$caseDispatchRva), (Hex-Bytes $lea), $leaText))
    if ($OptionalByteIndex) {
        $indexLoad = [byte[]]@(0x0f,0xb6,0x84,0x01,0,0,0,0)
        Put-Bytes $indexLoad 4 ([BitConverter]::GetBytes([UInt32]0))
        if ($R8Base) {
            $indexLoad = [byte[]]@(0x41,0x0f,0xb6,0x84,0x00,0,0,0,0)
            if ($TargetRegister -eq 2) { $indexLoad[3] = 0x8c; $indexLoad[4] = 0x08 }
        }
        Put-Bytes $pe (0x400+$cursor-$sectionRva) $indexLoad
        $instructions.Add(('{0:x}: {1} movzx eax, byte ptr [rcx+rax]' -f ($imageBase+$cursor), (Hex-Bytes $indexLoad)))
        $cursor += $indexLoad.Length
    }
    if ($RaxBase) {
        $indexLoad = [byte[]]@(0x48,0x8b,0x8d,0,0,0,0)
        if ($Fault -eq 'CorruptedIndexLoad') { $indexLoad[2] = 0x95 }
        Put-Bytes $pe (0x400+$cursor-$sectionRva) $indexLoad
        $instructions.Add(('{0:x}: {1} mov rcx, qword ptr [rbp+index]' -f ($imageBase+$cursor), (Hex-Bytes $indexLoad)))
        $cursor += $indexLoad.Length
        if ($Fault -eq 'ClobberedIndex') {
            $clobber = [byte[]]@(0x48,0x89,0xd1)
            Put-Bytes $pe (0x400+$cursor-$sectionRva) $clobber
            $instructions.Add(('{0:x}: {1} mov rcx, rdx' -f ($imageBase+$cursor), (Hex-Bytes $clobber)))
            $cursor += $clobber.Length
        }
    }
    $actualTableRva = $tableRva
    if ($HoistedR8Base) {
        $movEdx = [byte[]]@(0x8b,0xd0)
        $stackSave = [byte[]]@(0x89,0x44,0x24,0x20)
        $movR12d = [byte[]]@(0x44,0x8b,0xe0)
        $testEax = [byte[]]@(0x85,0xc0)
        if ($Fault -eq 'WrongHoistedTest') { $testEax[1] = 0xc9 }
        if ($Fault -eq 'BrokenHoistedPrelude') { $stackSave[0] = 0x88 }
        $jeRva = [UInt32]($cursor + $movEdx.Length + $stackSave.Length + $movR12d.Length + $testEax.Length)
        $je = [byte[]]@(0x0f,0x84,0,0,0,0)
        $leaBound = [byte[]]@(0x8d,0x40,0x01)
        $cmpBound = [byte[]]@(0x83,0xf8,0x05)
        if ($Fault -eq 'WrongHoistedBounds') { $cmpBound[1] = 0xf9 }
        $jaRva = [UInt32]($jeRva + $je.Length + $leaBound.Length + $cmpBound.Length)
        $ja = [byte[]]@(0x77,0)
        $hoistedInstructions = @(
            @{ Bytes=$movEdx; Text='mov edx, eax' },
            @{ Bytes=$stackSave; Text='mov dword ptr [rsp+20h], eax' },
            @{ Bytes=$movR12d; Text='mov r12d, eax' },
            @{ Bytes=$testEax; Text='test eax, eax' },
            @{ Bytes=$je; Text='je hoisted-body' },
            @{ Bytes=$leaBound; Text='lea eax, [rax+1]' },
            @{ Bytes=$cmpBound; Text='cmp eax, 5' },
            @{ Bytes=$ja; Text='ja hoisted-body' }
        )
        if ($Fault -eq 'ClobberedR8') { $hoistedInstructions[2].Bytes = [byte[]]@(0x41,0x89,0xc0); $hoistedInstructions[2].Text='mov r8d, eax' }
        if ($Fault -eq 'PartialHoistedPrelude') { $hoistedInstructions = @($hoistedInstructions | Where-Object { $_.Text -ne 'mov r12d, eax' }) }
        $jeInstructionIndex = -1
        $jaInstructionIndex = -1
        foreach ($item in $hoistedInstructions) {
            $instructionRva = $cursor
            if ($item.Text -eq 'je hoisted-body') { $jeRva = $instructionRva; $jeInstructionIndex = $instructions.Count }
            if ($item.Text -eq 'ja hoisted-body') { $jaRva = $instructionRva; $jaInstructionIndex = $instructions.Count }
            Put-Bytes $pe (0x400+$cursor-$sectionRva) $item.Bytes
            $instructions.Add(('{0:x}: {1} {2}' -f ($imageBase+$cursor), (Hex-Bytes $item.Bytes), $item.Text))
            $cursor += $item.Bytes.Length
        }
        $tableLoadLength = 8
        $addLength = 3
        $jumpLength = 2
        $afterJump = [UInt32]($cursor + $tableLoadLength + $addLength + $jumpLength)
        $actualTableRva = [UInt32](($afterJump + 0x40 + 3) -band 0xfffffffc)
        # Keep the actual native linear decode aligned with the synthetic listing's body/table boundaries.
        for ($paddingRva = $afterJump; $paddingRva -lt $actualTableRva; ++$paddingRva) {
            $pe[0x400+$paddingRva-$sectionRva] = 0x90
        }
        # 0x0010f8c5 is both a valid body RVA and the legal VEX byte sequence c5 f8 10 00.
        $bodyFirstRva = [UInt32]($sectionRva + 0x8c5)
        $bodySecondRva = [UInt32]($sectionRva + 0x8c9)
        $jeTarget = $bodyFirstRva
        $jaTarget = $bodySecondRva
        switch ($Fault) {
            'BranchBeforeDispatch' { $jeTarget = $caseDispatchRva }
            'BranchIntoTable' { $jaTarget = $actualTableRva }
            'BranchPastTable' { $jaTarget = [UInt32]($actualTableRva + 8) }
            'BranchTargetMiddle' { $jaTarget = [UInt32]($bodyFirstRva + 1) }
            'SecondBranchBeforeDispatch' { $jaTarget = $caseDispatchRva }
            'FirstBranchPastTable' { $jeTarget = [UInt32]($actualTableRva + 8) }
        }
        if ($jeInstructionIndex -ge 0) {
            $jeBytes = [byte[]]@(0x0f,0x84,0,0,0,0)
            $jeDisp = [Int32]($jeTarget - ($jeRva + $jeBytes.Length))
            Put-Bytes $jeBytes 2 ([BitConverter]::GetBytes($jeDisp))
            Put-Bytes $pe (0x400+$jeRva-$sectionRva) $jeBytes
            $instructions[$jeInstructionIndex] = ('{0:x}: {1} je hoisted-body' -f ($imageBase+$jeRva), (Hex-Bytes $jeBytes))
        }
        if ($jaInstructionIndex -ge 0) {
            $jaDisp = [Int32]($jaTarget - ($jaRva + 2))
            if ($jaDisp -lt -128 -or $jaDisp -gt 127) { throw "Synthetic JA target is outside rel8 range: $jaDisp" }
            $jaBytes = [byte[]]@(0x77,[byte]($jaDisp -band 0xff))
            Put-Bytes $pe (0x400+$jaRva-$sectionRva) $jaBytes
            $instructions[$jaInstructionIndex] = ('{0:x}: {1} ja hoisted-body' -f ($imageBase+$jaRva), (Hex-Bytes $jaBytes))
        }
    }
    $loadDisp = [UInt32]($actualTableRva -band 0xffffffff)
    [byte[]]$tableLoad = if ($RaxBase) { [byte[]]@(0x8b,0x8c,0x88,0,0,0,0) } else { [byte[]]@(0x8b,0x84,0x81,0,0,0,0) }
    Put-Bytes $tableLoad 3 ([BitConverter]::GetBytes($loadDisp))
    if ($R8Base) {
        $tableLoad = [byte[]]@(0x41,0x8b,(0x84+8*$TargetRegister),0x80,0,0,0,0)
        if ($TargetRegister -eq 2) { $tableLoad[3] = 0x88 }
        Put-Bytes $tableLoad 4 ([BitConverter]::GetBytes($loadDisp))
    }
    if ($RaxBase) {
        switch ($Fault) {
            'MismatchedTableBase' { $tableLoad[2] = 0x89 }
            'MismatchedTableIndex' { $tableLoad[2] = 0x80 }
            'MismatchedTableDestination' { $tableLoad[1] = 0x84 }
        }
    }
    if ($Fault -eq 'BrokenSequence') { $tableLoad[0] = 0x89 }
    Put-Bytes $pe (0x400+$cursor-$sectionRva) $tableLoad
    $tableLoadText = if ($RaxBase) { 'mov ecx, dword ptr [rax+rcx*4+table]' } elseif ($HoistedR8Base) { 'mov ecx, dword ptr [r8+rax*4+table]' } else { 'mov eax, dword ptr [rcx+rax*4+table]' }
    $instructions.Add(('{0:x}: {1} {2}' -f ($imageBase+$cursor), (Hex-Bytes $tableLoad), $tableLoadText))
    $cursor += $tableLoad.Length
    $add = if ($RaxBase) { [byte[]]@(0x48,0x03,0xc8) } else { [byte[]]@(0x48,0x03,0xc1) }
    if ($R8Base) { $add = [byte[]]@(0x49,0x03,(0xc0+8*$TargetRegister)) }
    if ($HoistedR8Base) { $add = [byte[]]@(0x49,0x03,0xc8) }
    if ($Fault -eq 'WrongAddRegister') { $add[2] = 0xd8 }
    if ($RaxBase -and $Fault -eq 'WrongRaxAdd') { $add[2] = 0xc1 }
    Put-Bytes $pe (0x400+$cursor-$sectionRva) $add
    $addText = if ($RaxBase) { 'add rcx, rax' } elseif ($HoistedR8Base) { 'add rcx, r8' } else { 'add rax, rcx' }
    $instructions.Add(('{0:x}: {1} {2}' -f ($imageBase+$cursor), (Hex-Bytes $add), $addText))
    $cursor += $add.Length
    $jump = if ($RaxBase) { [byte[]]@(0xff,0xe1) } else { [byte[]]@(0xff,0xe0) }
    if ($R8Base) { $jump[1] += $TargetRegister }
    if ($HoistedR8Base) { $jump = [byte[]]@(0xff,0xe1) }
    if ($Fault -eq 'WrongJumpRegister') { $jump[1] = 0xe3 }
    if ($RaxBase -and $Fault -eq 'WrongRaxJump') { $jump[1] = 0xe0 }
    Put-Bytes $pe (0x400+$cursor-$sectionRva) $jump
    $jumpText = if ($RaxBase -or $HoistedR8Base) { 'jmp rcx' } else { 'jmp rax' }
    $instructions.Add(('{0:x}: {1} {2}' -f ($imageBase+$cursor), (Hex-Bytes $jump), $jumpText))
    $afterJump = [UInt32]($cursor + 2)

    $target = if ($HoistedR8Base) { $bodyFirstRva } else { $targetRva }
    if ($Fault -eq 'TargetBeforeDispatch') { $target = $caseDispatchRva }
    if ($Fault -eq 'TargetAtTable') { $target = $actualTableRva }
    if ($Fault -eq 'TargetPastTable') { $target = [UInt32]($actualTableRva + 8) }
    $target2 = if ($HoistedR8Base) { $bodyFirstRva } else { $targetRva }
    $tableBytes = [byte[]]::new(8)
    Put-Bytes $tableBytes 0 ([BitConverter]::GetBytes($target))
    Put-Bytes $tableBytes 4 ([BitConverter]::GetBytes($target2))
    if ($Fault -eq 'MissingDataWithRet') { $pe[0x400+$actualTableRva-$sectionRva-1] = 0xc3 }
    elseif ($Fault -eq 'NoRet' -or $PdbData) { $pe[0x400+$actualTableRva-$sectionRva-1] = 0x90 }
    elseif ($ShortNop) {
        $pe[0x400+$actualTableRva-$sectionRva-3] = 0xc3
        $pe[0x400+$actualTableRva-$sectionRva-2] = 0x66
        $pe[0x400+$actualTableRva-$sectionRva-1] = 0x90
    } else { $pe[0x400+$actualTableRva-$sectionRva-1] = 0xc3 }
    Put-Bytes $pe (0x400+$actualTableRva-$sectionRva) $tableBytes

    $dataEnd = [UInt32]($actualTableRva + 8)
    $contributionEnd = [UInt32]0x2000
    $splitTableContribution = $Fault -eq 'CrossContribution'
    if ($Fault -eq 'ContributionBounds') { $contributionEnd = [UInt32]($actualTableRva - $sectionRva + 4) }
    $contributions = if ($splitTableContribution) {
        "  SC[.text]  | mod = 0, 0001:0, size = $($actualTableRva - $sectionRva), data crc = 0, reloc crc = 0`n" +
        "  SC[.text]  | mod = 1, 0001:$($actualTableRva - $sectionRva), size = 4096, data crc = 0, reloc crc = 0`n"
    } else {
        "  SC[.text]  | mod = 0, 0001:0, size = $contributionEnd, data crc = 0, reloc crc = 0`n"
    }
    $moduleHeader = "  Mod 0000 | ``SyntheticControl.obj``:`n  Obj: ``SyntheticControl.obj``:`n"
    if ($splitTableContribution) { $moduleHeader += "  Mod 0001 | ``SyntheticTable.obj``:`n  Obj: ``SyntheticTable.obj``:`n" }
    $metadataText = "  Age: $age`n  GUID: {$guid}`n$moduleHeader`nSection Contributions`n$contributions"
    Set-Content -LiteralPath $metadata -Value $metadataText -Encoding utf8
    $symbolText = ''
    if ($PdbData -and $Fault -notin @('MissingDataSymbol','MissingDataWithRet')) {
        $dataModule = if ($Fault -eq 'WrongDataOwner') { 1 } else { 0 }
        $dataOffset = $actualTableRva - $sectionRva
        if ($Fault -eq 'WrongDataStart') { $dataOffset += 4 }
        $dataName = if ($RaxBase -and $Fault -eq 'NamedDataSymbol') { 'dispatchTable' } else { '' }
        $dataType = if ($RaxBase -and $Fault -eq 'TypedDataSymbol') { 'unsigned long' } else { '<no type>' }
        $dataSize = if ($RaxBase -and $Fault -eq 'WrongDataRecordSize') { 15 } else { 16 }
        $symbolText = "  Mod $dataModule | ``SyntheticControl.obj``:`n  0 | S_LDATA32 [size = $dataSize] ``$dataName`` `n".TrimEnd() +
            "`n  type = $dataType, addr = 0001:$dataOffset`n"
    }
    Set-Content -LiteralPath $symbols -Value $symbolText -Encoding utf8

    # The target is a decoded boundary only in the valid case; middle-target deliberately lands inside it.
    $targetInstruction = [byte[]]@(0x66,0x90)
    $targetStart = if ($Fault -eq 'MiddleOfInstruction') { $targetRva - 1 } else { $targetRva }
    Put-Bytes $pe (0x400+$targetStart-$sectionRva) $targetInstruction
    if ($Fault -eq 'UnknownTargetInstruction') { $pe[0x400+$targetStart-$sectionRva] = 0x06 }
    $realVexRva = if ($HoistedR8Base) { [UInt32]($actualTableRva + 0x20) } else { [UInt32]($caseDispatchRva + 0x20) }
    if ($RealVex) { Put-Bytes $pe (0x400+$realVexRva-$sectionRva) ([byte[]]@(0xc5,0xf8,0x10,0x00)) }
    $rows = [Collections.Generic.List[string]]::new()
    foreach ($row in $instructions) { $rows.Add($row) }
    if ($HoistedR8Base) {
        Put-Bytes $pe (0x400+$bodyFirstRva-$sectionRva) ([byte[]]@(0x66,0x90))
        $pe[0x400+$bodySecondRva-$sectionRva] = if ($Fault -eq 'UnknownBranchTarget') { 0x06 } else { 0x90 }
        $rows.Add(('{0:x}: 66 90 nop' -f ($imageBase+$bodyFirstRva)))
        if ($Fault -eq 'UnknownBranchTarget') { $rows.Add(('{0:x}: 06 <unknown>' -f ($imageBase+$bodySecondRva))) }
        else { $rows.Add(('{0:x}: 90 nop' -f ($imageBase+$bodySecondRva))) }
    }
    if ($RealVex -and -not $HoistedR8Base) { $rows.Add(('{0:x}: c5 f8 10 00 vmovups xmm0, xmmword ptr [rax]' -f ($imageBase+$realVexRva))) }
    if (-not $HoistedR8Base) {
        if ($Fault -eq 'UnknownTargetInstruction') {
            $rows.Add(('{0:x}: 06 <unknown>' -f ($imageBase+$targetRva)))
        } elseif ($Fault -ne 'UndecodedTarget' -and $Fault -ne 'MiddleOfInstruction') {
            $rows.Add(('{0:x}: 66 90 nop' -f ($imageBase+$targetRva)))
        }
        if ($Fault -eq 'MiddleOfInstruction') {
            $rows.Add(('{0:x}: 66 90 nop' -f ($imageBase+$targetRva-1)))
        }
    }
    $tableRowsStart = $rows.Count
    $rows.Add(('{0:x}: {1} vmovups xmm0, xmmword ptr [rax]' -f ($imageBase+$actualTableRva), (Hex-Bytes $tableBytes[0..3])))
    $rows.Add(('{0:x}: {1} vmovups xmm0, xmmword ptr [rax]' -f ($imageBase+$actualTableRva+4), (Hex-Bytes $tableBytes[4..7])))
    if ($RealVex -and $HoistedR8Base) { $rows.Add(('{0:x}: c5 f8 10 00 vmovups xmm0, xmmword ptr [rax]' -f ($imageBase+$realVexRva))) }
    if ($Fault -eq 'MissingDataWithRet') { $rows.Add(('{0:x}: c3 ret' -f ($imageBase+$actualTableRva-1))) }
    if ($Fault -eq 'InstructionExtendsPastTable') {
        # Deliberately substitute a synthetic decoded span; the scanner must bound the entire row.
        $pe[0x400+$actualTableRva-$sectionRva+8] = 0xc4
        $rows[$tableRowsStart+1] = ('{0:x}: c4 e1 78 58 c4 vaddps xmm0, xmm0, xmm4' -f ($imageBase+$actualTableRva+4))
    }
    $sortedRows = @($rows | Sort-Object { [UInt64]::Parse(($_ -split ':',2)[0], [Globalization.NumberStyles]::HexNumber) })
    Set-Content -LiteralPath $disassembly -Value ($sortedRows -join "`n") -Encoding utf8
    [IO.File]::WriteAllBytes($binary, $pe)
    return @{ Name=$Name; Binary=$binary; Metadata=$metadata; Symbols=$symbols; Disassembly=$disassembly; Target=$targetRva; TableEnd=$dataEnd }
}

function Invoke-Control($Case, [bool]$ExpectedPass, [int]$ExpectedInline = -1, [int]$ExpectedVectors = -1) {
    $result = [RedPrismBuild.IsaScanner]::Scan($Case.Binary, $Case.Metadata, $Case.Symbols, $Case.Disassembly,
        [string[]]@(), [string[]]@(), [string[]]@())
    if ($ExpectedPass -and $result.Violations.Count -ne 0) {
        throw "$($Case.Name) unexpectedly violated the ISA gate: $($result.Violations -join '; ')"
    }
    if (-not $ExpectedPass -and $result.Violations.Count -eq 0) {
        throw "$($Case.Name) bypassed the ISA gate."
    }
    if ($ExpectedInline -ge 0 -and $result.InlineTableVectorData -ne $ExpectedInline) {
        throw "$($Case.Name): expected $ExpectedInline inline-table vector rows, got $($result.InlineTableVectorData)."
    }
    if ($ExpectedVectors -ge 0 -and $result.VectorInstructions -ne $ExpectedVectors) {
        throw "$($Case.Name): expected $ExpectedVectors ordinary vector rows, got $($result.VectorInstructions)."
    }
    Write-Host "PASS isa-inline-table.$($Case.Name)"
}

$good = New-SyntheticCase Positive -OptionalByteIndex -ShortNop
Invoke-Control $good $true 2 0
$optionalOff = New-SyntheticCase PositiveWithoutByteIndex
Invoke-Control $optionalOff $true 2 0
$mixed = New-SyntheticCase RealVexBesideTable -RealVex
Invoke-Control $mixed $false 2 1

foreach ($targetRegister in @(0,1,2)) {
    $case = New-SyntheticCase "R8DataTarget$targetRegister" -R8Base -PdbData -OptionalByteIndex -TargetRegister $targetRegister
    Invoke-Control $case $true 2 0
}
$case = New-SyntheticCase R8DataNoRemap -R8Base -PdbData
Invoke-Control $case $true 2 0
$case = New-SyntheticCase R8DataWithRealVex -R8Base -PdbData -RealVex
Invoke-Control $case $false 2 1
foreach ($fault in @('MissingDataSymbol','WrongDataOwner','WrongDataStart','WrongAddRegister','WrongJumpRegister',
        'CrossContribution','ContributionBounds','MiddleOfInstruction','UnknownTargetInstruction','InstructionExtendsPastTable')) {
    $case = New-SyntheticCase "R8Data$fault" -R8Base -PdbData -OptionalByteIndex -TargetRegister 1 -Fault $fault
    Invoke-Control $case $false
}

$case = New-SyntheticCase HoistedR8Base -R8Base -HoistedR8Base -PdbData -TargetRegister 1
Invoke-Control $case $true 2 0
$case = New-SyntheticCase HoistedR8BaseWithRealVex -R8Base -HoistedR8Base -PdbData -TargetRegister 1 -RealVex
Invoke-Control $case $false 2 1
foreach ($fault in @('ClobberedR8','BrokenHoistedPrelude','PartialHoistedPrelude','WrongHoistedTest',
        'WrongHoistedBounds','BranchBeforeDispatch','BranchIntoTable','BranchPastTable','BranchTargetMiddle',
        'SecondBranchBeforeDispatch','FirstBranchPastTable','UnknownBranchTarget',
        'MissingDataSymbol','MissingDataWithRet','WrongDataOwner','WrongDataStart','CrossContribution')) {
    $case = New-SyntheticCase "HoistedR8Base$fault" -R8Base -HoistedR8Base -PdbData -TargetRegister 1 -Fault $fault
    Invoke-Control $case $false
}

$case = New-SyntheticCase RaxBase -RaxBase -PdbData
Invoke-Control $case $true 2 0
$case = New-SyntheticCase RaxBaseWithRealVex -RaxBase -PdbData -RealVex
Invoke-Control $case $false 2 1
foreach ($fault in @('WrongImageBase','CorruptedIndexLoad','ClobberedIndex','MismatchedTableBase',
        'MismatchedTableIndex','MismatchedTableDestination','WrongRaxAdd','WrongRaxJump',
        'MissingDataSymbol','WrongDataOwner','WrongDataStart','WrongDataRecordSize','NamedDataSymbol',
        'TypedDataSymbol','CrossContribution','ContributionBounds','MiddleOfInstruction',
        'UndecodedTarget','UnknownTargetInstruction','TargetBeforeDispatch','TargetAtTable','TargetPastTable',
        'InstructionExtendsPastTable')) {
    $case = New-SyntheticCase "RaxBase$fault" -RaxBase -PdbData -Fault $fault
    Invoke-Control $case $false
}

foreach ($name in @('WrongImageBase','BrokenSequence','CrossContribution','ContributionBounds',
        'MiddleOfInstruction','UndecodedTarget','UnknownTargetInstruction','TargetBeforeDispatch','NoRet','InstructionExtendsPastTable')) {
    $case = New-SyntheticCase $name -Fault $name
    Invoke-Control $case $false
}

Write-Host "Synthetic ISA parser fixtures: $fixtureRoot"
$global:LASTEXITCODE = 0
