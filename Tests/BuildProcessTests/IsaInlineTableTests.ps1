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
        [switch]$DlByteIndex,
        [switch]$SignedByteIndex,
        [switch]$RealVex,
        [switch]$ShortNop,
        [switch]$R8Base,
        [switch]$HoistedR8Base,
        [switch]$RaxBase,
        [switch]$RdxBase,
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

    if ($RdxBase -and ($OptionalByteIndex -or $DlByteIndex -or $SignedByteIndex -or $R8Base -or $HoistedR8Base -or $RaxBase)) {
        throw 'RdxBase cannot be combined with another index form or base form.'
    }
    if ($DlByteIndex -and ($OptionalByteIndex -or $SignedByteIndex -or $R8Base -or $HoistedR8Base -or $RaxBase -or $RdxBase)) {
        throw 'DlByteIndex cannot be combined with another index form or base form.'
    }
    if ($RdxBase -and -not $PdbData) { throw 'RdxBase requires PdbData.' }
    if ($HoistedR8Base -and (-not $R8Base -or -not $PdbData -or $OptionalByteIndex)) {
        throw 'HoistedR8Base requires R8Base and PdbData, without OptionalByteIndex.'
    }
    if ($RaxBase -and (-not $PdbData -or $R8Base -or $HoistedR8Base -or $OptionalByteIndex -or $RdxBase)) {
        throw 'RaxBase requires PdbData and cannot be combined with R8Base, HoistedR8Base, or OptionalByteIndex.'
    }
    $caseDispatchRva = if ($HoistedR8Base) { [UInt32]($sectionRva + 0x880) } elseif ($SignedByteIndex) { [UInt32]($dispatchRva + $(if ($Fault -in @('SignedNoncontiguous','SignedBadFrame')) { 6 } else { 5 })) } else { $dispatchRva }
    [byte[]]$lea = if ($RaxBase) { [byte[]]@(0x48,0x8d,0x05,0,0,0,0) } elseif ($RdxBase) { [byte[]]@(0x48,0x8d,0x15,0,0,0,0) } else { [byte[]]@(0x48,0x8d,0x0d,0,0,0,0) }
    if ($RdxBase -and $Fault -eq 'RdxWrongLea') { $lea[2] = 0x0d }
    if ($R8Base) { $lea[0] = 0x4c; $lea[2] = 0x05 }
    if ($Fault -eq 'WrongImageBase') { $leaDisp = [Int32]($imageBase - ($imageBase + $caseDispatchRva + 7) + 1) }
    else { $leaDisp = [Int32]($imageBase - ($imageBase + $caseDispatchRva + 7)) }
    Put-Bytes $lea 3 ([BitConverter]::GetBytes($leaDisp))
    if ($SignedByteIndex) {
        [byte[]]$signedIndex = if ($Fault -eq 'SignedBadFrame') { [byte[]]@(0x48,0x0f,0xbe,0x44,0x24,0xc0) } else { [byte[]]@(0x48,0x0f,0xbe,0x45,0xc0) }
        switch ($Fault) {
            'SignedBadMovsxDestination' { $signedIndex[0] = 0x49 }
            'SignedBadFrame' { $signedIndex[4] = 0x08 }
            'SignedBadOpcode' { $signedIndex[2] = 0xb6 }
            'SignedBadSize' { $signedIndex = [byte[]]@(0x48,0x0f,0xbf,0x45,0xc0) }
        }
        Put-Bytes $pe (0x400+$dispatchRva-$sectionRva) $signedIndex
        if ($Fault -eq 'SignedNoncontiguous') { $pe[0x400+$dispatchRva-$sectionRva+5] = 0x90 }
    }
    Put-Bytes $pe (0x400+$caseDispatchRva-$sectionRva) $lea
    $cursor = [UInt32]($caseDispatchRva + 7)
    $instructions = [Collections.Generic.List[string]]::new()
    $leaText = if ($RaxBase) { 'lea rax, [rip+imagebase]' } elseif ($RdxBase) { 'lea rdx, [rip+imagebase]' } elseif ($R8Base) { 'lea r8, [rip+imagebase]' } else { 'lea rcx, [rip+imagebase]' }
    $instructions.Add(('{0:x}: {1} {2}' -f ($imageBase+$caseDispatchRva), (Hex-Bytes $lea), $leaText))
    if ($SignedByteIndex) {
        $indexText = if ($Fault -eq 'SignedNoncontiguous') { 'nop' } elseif ($Fault -eq 'SignedBadFrame') { 'movsx rax, byte ptr [rsp-40h]' } elseif ($Fault -eq 'SignedBadSize') { 'movsx rax, word ptr [rbp-40h]' } else { 'movsx rax, byte ptr [rbp-40h]' }
        $instructions.Insert(0, ('{0:x}: {1} {2}' -f ($imageBase+$dispatchRva), (Hex-Bytes $signedIndex), $indexText))
        if ($Fault -eq 'SignedNoncontiguous') {
            $instructions.Insert(1, ('{0:x}: 90 nop' -f ($imageBase+$dispatchRva+5)))
        }
    }
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
    if ($DlByteIndex) {
        if ($Fault -eq 'DlNoncontiguous') {
            $pe[0x400+$cursor-$sectionRva] = 0x90
            $instructions.Add(('{0:x}: 90 nop' -f ($imageBase+$cursor)))
            $cursor++
        }
        [byte[]]$dlIndex = [byte[]]@(0x0f,0xb6,0xc2)
        switch ($Fault) {
            'WrongDlDestination' { $dlIndex[2] = 0xca }
            'WrongDlSource' { $dlIndex[2] = 0xc6 }
            'WrongDlOpcode' { $dlIndex[1] = 0xbe }
        }
        Put-Bytes $pe (0x400+$cursor-$sectionRva) $dlIndex
        $dlText = switch ($Fault) {
            'WrongDlDestination' { 'movzx ecx, dl' }
            'WrongDlSource' { 'movzx eax, dh' }
            'WrongDlOpcode' { 'movsx eax, dl' }
            default { 'movzx eax, dl' }
        }
        $instructions.Add(('{0:x}: {1} {2}' -f ($imageBase+$cursor), (Hex-Bytes $dlIndex), $dlText))
        $cursor += $dlIndex.Length
        if ($Fault -eq 'DlDuplicate') {
            [byte[]]$duplicateDl = [byte[]]@(0x0f,0xb6,0xc2)
            Put-Bytes $pe (0x400+$cursor-$sectionRva) $duplicateDl
            $instructions.Add(('{0:x}: {1} movzx eax, dl' -f ($imageBase+$cursor), (Hex-Bytes $duplicateDl)))
            $cursor += $duplicateDl.Length
        }
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
    if ($RdxBase) {
        if ($Fault -eq 'RdxNoncontiguous') {
            $pe[0x400+$cursor-$sectionRva] = 0x90
            $instructions.Add(('{0:x}: 90 nop' -f ($imageBase+$cursor)))
            $cursor++
        } elseif ($Fault -eq 'RdxDoubleLoad') {
            [byte[]]$extraLoad = [byte[]]@(0x8b,0x8c,0x82,0,0,0,0)
            Put-Bytes $extraLoad 3 ([BitConverter]::GetBytes([UInt32]($actualTableRva -band 0xffffffff)))
            Put-Bytes $pe (0x400+$cursor-$sectionRva) $extraLoad
            $instructions.Add(('{0:x}: {1} mov ecx, dword ptr [rdx+rax*4+table]' -f ($imageBase+$cursor), (Hex-Bytes $extraLoad)))
            $cursor += $extraLoad.Length
        }
    }
    $loadDisp = if ($SignedByteIndex) { [UInt32](($actualTableRva + $(if ($Fault -eq 'SignedBiasTwoEntries') { 8 } else { 4 })) -band 0xffffffff) } else { [UInt32]($actualTableRva -band 0xffffffff) }
    [byte[]]$tableLoad = if ($RaxBase) { [byte[]]@(0x8b,0x8c,0x88,0,0,0,0) } elseif ($RdxBase) { [byte[]]@(0x8b,0x8c,0x82,0,0,0,0) } else { [byte[]]@(0x8b,0x84,0x81,0,0,0,0) }
    Put-Bytes $tableLoad 3 ([BitConverter]::GetBytes($loadDisp))
    if ($RdxBase -and $Fault -eq 'RdxWrongLoadIndex') { $tableLoad[2] = 0x8a }
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
    $tableLoadText = if ($RaxBase) { 'mov ecx, dword ptr [rax+rcx*4+table]' } elseif ($RdxBase) { 'mov ecx, dword ptr [rdx+rax*4+table]' } elseif ($HoistedR8Base) { 'mov ecx, dword ptr [r8+rax*4+table]' } elseif ($SignedByteIndex) { 'mov eax, dword ptr [rcx+4*rax+table+4]' } else { 'mov eax, dword ptr [rcx+rax*4+table]' }
    $instructions.Add(('{0:x}: {1} {2}' -f ($imageBase+$cursor), (Hex-Bytes $tableLoad), $tableLoadText))
    $cursor += $tableLoad.Length
    $add = if ($RaxBase) { [byte[]]@(0x48,0x03,0xc8) } elseif ($RdxBase) { [byte[]]@(0x48,0x03,0xca) } else { [byte[]]@(0x48,0x03,0xc1) }
    if ($R8Base) { $add = [byte[]]@(0x49,0x03,(0xc0+8*$TargetRegister)) }
    if ($HoistedR8Base) { $add = [byte[]]@(0x49,0x03,0xc8) }
    if ($Fault -eq 'WrongAddRegister') { $add[2] = 0xd8 }
    if ($RaxBase -and $Fault -eq 'WrongRaxAdd') { $add[2] = 0xc1 }
    if ($RdxBase -and $Fault -eq 'RdxWrongAdd') { $add[2] = 0xc8 }
    Put-Bytes $pe (0x400+$cursor-$sectionRva) $add
    $addText = if ($RaxBase) { 'add rcx, rax' } elseif ($RdxBase) { 'add rcx, rdx' } elseif ($HoistedR8Base) { 'add rcx, r8' } else { 'add rax, rcx' }
    $instructions.Add(('{0:x}: {1} {2}' -f ($imageBase+$cursor), (Hex-Bytes $add), $addText))
    $cursor += $add.Length
    $jump = if ($RaxBase -or $RdxBase) { [byte[]]@(0xff,0xe1) } else { [byte[]]@(0xff,0xe0) }
    if ($R8Base) { $jump[1] += $TargetRegister }
    if ($HoistedR8Base) { $jump = [byte[]]@(0xff,0xe1) }
    if ($Fault -eq 'WrongJumpRegister') { $jump[1] = 0xe3 }
    if ($RaxBase -and $Fault -eq 'WrongRaxJump') { $jump[1] = 0xe0 }
    if ($RdxBase -and $Fault -eq 'RdxWrongJump') { $jump[1] = 0xe0 }
    Put-Bytes $pe (0x400+$cursor-$sectionRva) $jump
    $jumpText = if ($RaxBase -or $RdxBase -or $HoistedR8Base) { 'jmp rcx' } else { 'jmp rax' }
    $instructions.Add(('{0:x}: {1} {2}' -f ($imageBase+$cursor), (Hex-Bytes $jump), $jumpText))
    $afterJump = [UInt32]($cursor + 2)

    $target = if ($HoistedR8Base) { $bodyFirstRva } else { $targetRva }
    if ($Fault -eq 'TargetBeforeDispatch') { $target = $caseDispatchRva }
    if ($Fault -eq 'TargetAtTable') { $target = $actualTableRva }
    if ($Fault -eq 'TargetPastTable') {
        $pastTableOffset = if ($RdxBase) { 28 } elseif ($DlByteIndex) { 16 } else { 8 }
        $target = [UInt32]($actualTableRva + $pastTableOffset)
    }
    $target2 = if ($HoistedR8Base) { $bodyFirstRva } else { $targetRva }
    if ($SignedByteIndex) {
        switch ($Fault) {
            'TargetBeforeDispatch' { $target = $caseDispatchRva }
            'TargetAtTable' { $target = $actualTableRva }
            'TargetPastTable' { $target = [UInt32]($actualTableRva + 16) }
            'SignedDefaultMiddle' { $target = [UInt32]($targetRva + 1) }
            'SignedDefaultMissing' { $target = [UInt32]($targetRva + 0x10) }
            'SignedDefaultPastContribution' { $target = [UInt32]($sectionRva + 0x2000) }
        }
    }
    [byte[]]$tableBytes = if ($RdxBase) { [byte[]]::new(28) } elseif ($SignedByteIndex -or $DlByteIndex) { [byte[]]::new(16) } else { [byte[]]::new(8) }
    Put-Bytes $tableBytes 0 ([BitConverter]::GetBytes($target))
    Put-Bytes $tableBytes 4 ([BitConverter]::GetBytes($target2))
    if ($RdxBase) {
        for ($entry = 2; $entry -lt 7; ++$entry) {
            Put-Bytes $tableBytes (4*$entry) ([BitConverter]::GetBytes($targetRva))
        }
        switch ($Fault) {
            'RdxTargetMiddle' { Put-Bytes $tableBytes 0 ([BitConverter]::GetBytes([UInt32]($targetRva + 1))) }
            'RdxTargetMissing' { Put-Bytes $tableBytes 0 ([BitConverter]::GetBytes([UInt32]($targetRva + 0x100))) }
            'RdxTargetInvalid' { Put-Bytes $tableBytes 0 ([BitConverter]::GetBytes([UInt32]($targetRva + 0x80))) }
        }
    }
    if ($SignedByteIndex -or $DlByteIndex) {
        Put-Bytes $tableBytes 8 ([BitConverter]::GetBytes($target2))
        Put-Bytes $tableBytes 12 ([BitConverter]::GetBytes($target2))
        if ($Fault -eq 'SignedLastEntryMiddle') {
            Put-Bytes $tableBytes 12 ([BitConverter]::GetBytes([UInt32]($targetRva + 1)))
        }
    }
    if ($Fault -eq 'MissingDataWithRet') { $pe[0x400+$actualTableRva-$sectionRva-1] = 0xc3 }
    elseif ($Fault -eq 'NoRet' -or ($PdbData -and -not $DlByteIndex -and -not $RdxBase)) { $pe[0x400+$actualTableRva-$sectionRva-1] = 0x90 }
    elseif ($ShortNop) {
        $pe[0x400+$actualTableRva-$sectionRva-3] = 0xc3
        $pe[0x400+$actualTableRva-$sectionRva-2] = 0x66
        $pe[0x400+$actualTableRva-$sectionRva-1] = 0x90
    } else { $pe[0x400+$actualTableRva-$sectionRva-1] = 0xc3 }
    Put-Bytes $pe (0x400+$actualTableRva-$sectionRva) $tableBytes

    $dataEnd = [UInt32]($actualTableRva + $tableBytes.Length)
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
    if (($PdbData -or $SignedByteIndex -or $DlByteIndex -or $RdxBase) -and $Fault -notin @('MissingDataSymbol','MissingDataWithRet')) {
        $dataModule = if ($Fault -eq 'WrongDataOwner') { 1 } else { 0 }
        $dataOffset = $actualTableRva - $sectionRva
        if ($Fault -eq 'WrongDataStart') { $dataOffset += 4 }
        $dataName = if (($RaxBase -or $SignedByteIndex -or $DlByteIndex -or $RdxBase) -and $Fault -eq 'NamedDataSymbol') { 'dispatchTable' } else { '' }
        $dataType = if (($RaxBase -or $SignedByteIndex -or $DlByteIndex -or $RdxBase) -and $Fault -eq 'TypedDataSymbol') { 'unsigned long' } else { '<no type>' }
        $dataSize = if (($RdxBase -or $RaxBase -or $SignedByteIndex -or $DlByteIndex) -and $Fault -eq 'WrongDataRecordSize') { 15 } else { 16 }
        $symbolText = "  Mod $dataModule | ``SyntheticControl.obj``:`n  0 | S_LDATA32 [size = $dataSize] ``$dataName`` `n".TrimEnd() +
            "`n  type = $dataType, addr = 0001:$dataOffset`n"
    }
    Set-Content -LiteralPath $symbols -Value $symbolText -Encoding utf8

    # The target is a decoded boundary only in the valid case; middle-target deliberately lands inside it.
    $targetInstruction = [byte[]]@(0x66,0x90)
    $targetStart = if ($Fault -eq 'MiddleOfInstruction') { $targetRva - 1 } else { $targetRva }
    Put-Bytes $pe (0x400+$targetStart-$sectionRva) $targetInstruction
    if ($Fault -eq 'UnknownTargetInstruction') { $pe[0x400+$targetStart-$sectionRva] = 0x06 }
    $realVexRva = if ($RdxBase) { [UInt32]($actualTableRva + 28) } elseif ($SignedByteIndex -or $DlByteIndex) { [UInt32]($actualTableRva + 16) } elseif ($HoistedR8Base) { [UInt32]($actualTableRva + 0x20) } else { [UInt32]($caseDispatchRva + 0x20) }
    [byte[]]$realVexBytes = if ($SignedByteIndex -or $DlByteIndex -or $RdxBase) { [byte[]]@(0xc5,0xf8,0x10,0x40,0x01) } else { [byte[]]@(0xc5,0xf8,0x10,0x00) }
    if ($RealVex) { Put-Bytes $pe (0x400+$realVexRva-$sectionRva) $realVexBytes }
    $rows = [Collections.Generic.List[string]]::new()
    foreach ($row in $instructions) { $rows.Add($row) }
    if ($HoistedR8Base) {
        Put-Bytes $pe (0x400+$bodyFirstRva-$sectionRva) ([byte[]]@(0x66,0x90))
        $pe[0x400+$bodySecondRva-$sectionRva] = if ($Fault -eq 'UnknownBranchTarget') { 0x06 } else { 0x90 }
        $rows.Add(('{0:x}: 66 90 nop' -f ($imageBase+$bodyFirstRva)))
        if ($Fault -eq 'UnknownBranchTarget') { $rows.Add(('{0:x}: 06 <unknown>' -f ($imageBase+$bodySecondRva))) }
        else { $rows.Add(('{0:x}: 90 nop' -f ($imageBase+$bodySecondRva))) }
    }
    if ($RealVex -and -not $HoistedR8Base) {
        $realVexText = if ($SignedByteIndex -or $DlByteIndex -or $RdxBase) { 'vmovups xmm0, xmmword ptr [rax+1]' } else { 'vmovups xmm0, xmmword ptr [rax]' }
        $rows.Add(('{0:x}: {1} {2}' -f ($imageBase+$realVexRva), (Hex-Bytes $realVexBytes), $realVexText))
    }
    if (-not $HoistedR8Base) {
        if ($RdxBase) {
            # Seven distinct table slots all target this decoded function, keeping every four-byte
            # table row's synthetic bytes recognizable as vector data for the classification count.
            if ($Fault -ne 'RdxTargetMissing') { $rows.Add(('{0:x}: 66 90 nop' -f ($imageBase+$targetRva))) }
            if ($Fault -eq 'RdxTargetInvalid') {
                $invalidTarget = [UInt32]($targetRva + 0x80)
                $pe[0x400+$invalidTarget-$sectionRva] = 0x06
                $rows.Add(('{0:x}: 06 <unknown>' -f ($imageBase+$invalidTarget)))
            }
        } elseif ($Fault -eq 'UnknownTargetInstruction') {
            $rows.Add(('{0:x}: 06 <unknown>' -f ($imageBase+$targetRva)))
        } elseif ($Fault -ne 'UndecodedTarget' -and $Fault -ne 'MiddleOfInstruction') {
            $rows.Add(('{0:x}: 66 90 nop' -f ($imageBase+$targetRva)))
        }
        if ($Fault -eq 'MiddleOfInstruction') {
            $rows.Add(('{0:x}: 66 90 nop' -f ($imageBase+$targetRva-1)))
        }
    }
    $tableRowsStart = $rows.Count
    if (-not $RdxBase) {
        $rows.Add(('{0:x}: {1} vmovups xmm0, xmmword ptr [rax]' -f ($imageBase+$actualTableRva), (Hex-Bytes $tableBytes[0..3])))
        $rows.Add(('{0:x}: {1} vmovups xmm0, xmmword ptr [rax]' -f ($imageBase+$actualTableRva+4), (Hex-Bytes $tableBytes[4..7])))
    }
    if ($SignedByteIndex -or $DlByteIndex) {
        $rows.Add(('{0:x}: {1} vmovups xmm0, xmmword ptr [rax]' -f ($imageBase+$actualTableRva+8), (Hex-Bytes $tableBytes[8..11])))
        $rows.Add(('{0:x}: {1} vmovups xmm0, xmmword ptr [rax]' -f ($imageBase+$actualTableRva+12), (Hex-Bytes $tableBytes[12..15])))
    }
    if ($RdxBase) {
        for ($entry = 0; $entry -lt 7; ++$entry) {
            $entryBytes = $tableBytes[(4*$entry)..(4*$entry+3)]
            $rows.Add(('{0:x}: {1} vmovups xmm0, xmmword ptr [rax]' -f ($imageBase+$actualTableRva+4*$entry), (Hex-Bytes $entryBytes)))
        }
    }
    if ($RealVex -and $HoistedR8Base) { $rows.Add(('{0:x}: c5 f8 10 00 vmovups xmm0, xmmword ptr [rax]' -f ($imageBase+$realVexRva))) }
    if ($Fault -eq 'MissingDataWithRet') { $rows.Add(('{0:x}: c3 ret' -f ($imageBase+$actualTableRva-1))) }
    if ($Fault -eq 'InstructionExtendsPastTable') {
        # Deliberately substitute a synthetic decoded span; the scanner must bound the entire row.
        if ($RdxBase) {
            [byte[]]$crossingVex = [byte[]]@(0xc5,0xf8,0x10,0x40,0x01)
            Put-Bytes $pe (0x400+$actualTableRva-$sectionRva+24) $crossingVex
            $rows[$tableRowsStart+6] = ('{0:x}: {1} vmovups xmm0, xmmword ptr [rax+1]' -f ($imageBase+$actualTableRva+24), (Hex-Bytes $crossingVex))
        } elseif ($SignedByteIndex -or $DlByteIndex) {
            [byte[]]$crossingVex = [byte[]]@(0xc5,0xf8,0x10,0x40,0x01)
            Put-Bytes $pe (0x400+$actualTableRva-$sectionRva+12) $crossingVex
            $rows[$tableRowsStart+3] = ('{0:x}: {1} vmovups xmm0, xmmword ptr [rax+1]' -f ($imageBase+$actualTableRva+12), (Hex-Bytes $crossingVex))
        } else {
            $pe[0x400+$actualTableRva-$sectionRva+$tableBytes.Length] = 0xc4
            $rows[$tableRowsStart+1] = ('{0:x}: c4 e1 78 58 c4 vaddps xmm0, xmm0, xmm4' -f ($imageBase+$actualTableRva+4))
        }
    }
    $sortedRows = @($rows | Sort-Object { [UInt64]::Parse(($_ -split ':',2)[0], [Globalization.NumberStyles]::HexNumber) })
    Set-Content -LiteralPath $disassembly -Value ($sortedRows -join "`n") -Encoding utf8
    [IO.File]::WriteAllBytes($binary, $pe)
    return @{ Name=$Name; Binary=$binary; Metadata=$metadata; Symbols=$symbols; Disassembly=$disassembly; Target=$targetRva; TableRva=$actualTableRva; TableEnd=$dataEnd; SectionRva=$sectionRva; ImageBase=$imageBase }
}

function Set-TailPaddingVexArtifact {
    param(
        [Parameter(Mandatory)]$Case,
        [ValidateRange(1,14)][int]$PaddingBytes = 2,
        [switch]$NonCcTail,
        [switch]$PeTailMismatch,
        [switch]$VexAtTableEnd,
        [switch]$OverlongInstruction
    )
    $pe = [IO.File]::ReadAllBytes($Case.Binary)
    $tableRaw = 0x400 + $Case.TableRva - $Case.SectionRva
    $tailRaw = 0x400 + $Case.TableEnd - $Case.SectionRva
    $listing = [Collections.Generic.List[string]]::new()
    foreach ($line in [IO.File]::ReadAllLines($Case.Disassembly)) {
        $addressText = ($line -split ':',2)[0]
        $address = [UInt64]::Parse($addressText, [Globalization.NumberStyles]::HexNumber)
        $tableAddress = $Case.ImageBase + $Case.TableRva
        if ($address -eq $tableAddress -or $address -eq ($tableAddress + 4)) { continue }
        $listing.Add($line)
    }

    # Two identical valid targets have bytes 54 88 c5 00. The VEX row begins two
    # bytes into the first DWORD and consumes its remaining six table bytes plus INT3 padding.
    $listing.Add(('{0:x}: {1} .long 0x{2:x8}' -f ($Case.ImageBase + $Case.TableRva),
        (Hex-Bytes ([byte[]]$pe[$tableRaw..($tableRaw+3)])), [BitConverter]::ToUInt32($pe, $tableRaw)))
    [byte[]]$crossing = [byte[]]@(0xc5,0x00,0x54,0x88,0xc5,0x00) + [byte[]]::new($PaddingBytes)
    for ($i = 0; $i -lt $PaddingBytes; ++$i) { $crossing[6+$i] = 0xcc }
    if ($NonCcTail) { $crossing[$crossing.Length-1] = 0x90 }
    if ($OverlongInstruction) {
        $crossing = [byte[]]@(0xc5,0x00,0x54,0x88,0xc5,0x00,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc)
    }
    Put-Bytes $pe ($tableRaw + 2) $crossing
    if ($PeTailMismatch) { $pe[$tailRaw + $PaddingBytes - 1] = 0x90 }
    $listing.Add(('{0:x}: {1} vandps xmm9, xmm15, xmmword ptr [rcx]' -f
        ($Case.ImageBase + $Case.TableRva + 2), (Hex-Bytes $crossing)))
    if ($VexAtTableEnd) {
        [byte[]]$afterTable = [byte[]]@(0xc5,0xf8,0x10,0x00)
        Put-Bytes $pe $tailRaw $afterTable
        $listing.Add(('{0:x}: {1} vmovups xmm0, xmmword ptr [rax]' -f
            ($Case.ImageBase + $Case.TableEnd), (Hex-Bytes $afterTable)))
    }
    Set-Content -LiteralPath $Case.Disassembly -Value (@($listing | Sort-Object {
        [UInt64]::Parse(($_ -split ':',2)[0], [Globalization.NumberStyles]::HexNumber)
    }) -join "`n") -Encoding utf8
    [IO.File]::WriteAllBytes($Case.Binary, $pe)
}

function New-TailPaddingCase([string]$Name) {
    $savedGlobals = @{
        SectionRva=$script:sectionRva; DispatchRva=$script:dispatchRva; TableRva=$script:tableRva; TargetRva=$script:targetRva
    }
    try {
        $script:sectionRva = [UInt32]0x00c58000
        $script:dispatchRva = $script:sectionRva
        $script:tableRva = [UInt32]($script:sectionRva + 0x1000)
        $script:targetRva = [UInt32]0x00c58854
        return New-SyntheticCase $Name
    } finally {
        $script:sectionRva = $savedGlobals.SectionRva
        $script:dispatchRva = $savedGlobals.DispatchRva
        $script:tableRva = $savedGlobals.TableRva
        $script:targetRva = $savedGlobals.TargetRva
    }
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

function New-R9R8DispatchCase([string]$Name, [string]$Fault = '', [switch]$RealVex) {
    # Reuse the checked PE/PDB/table fixture, replacing only the dispatch bytes and their listing.
    # The accepted sequence is exactly LEA R9,imagebase; MOVZX EAX,byte [R9+RAX+disp32];
    # MOV R8D,[R9+RAX*4+disp32]; ADD R8,R9; JMP R8. PDB record size remains 16.
    $baseFault = switch ($Fault) {
        { $_ -in @('MissingDataSymbol','WrongDataOwner','WrongDataStart','WrongDataRecordSize',
            'NamedDataSymbol','TypedDataSymbol','CrossContribution','ContributionBounds',
            'TargetBeforeDispatch','TargetAtTable','TargetPastTable') } { $_; break }
        'R9TargetMiddle' { 'RdxTargetMiddle'; break }
        'R9TargetMissing' { 'RdxTargetMissing'; break }
        'R9TargetInvalid' { 'RdxTargetInvalid'; break }
        default { '' }
    }
    $case = if ($RealVex) {
        New-SyntheticCase $Name -RdxBase -PdbData -RealVex -Fault $baseFault
    } else {
        New-SyntheticCase $Name -RdxBase -PdbData -Fault $baseFault
    }
    $pe = [IO.File]::ReadAllBytes($case.Binary)
    $dispatch = [UInt32]$script:dispatchRva
    $table = [UInt32]$case.TableRva
    $raw = 0x400 + $dispatch - $case.SectionRva
    $cursor = $dispatch
    [byte[]]$lea = [byte[]]@(0x4c,0x8d,0x0d,0,0,0,0)
    $leaDisp = [Int32](-[Int64]$dispatch - $lea.Length)
    if ($Fault -eq 'R9WrongImageBase') { $leaDisp++ }
    if ($Fault -eq 'R9WrongLeaOpcode') { $lea[1] = 0x89 }
    if ($Fault -eq 'R9WrongLeaDestination') { $lea[0] = 0x48 }
    if ($Fault -eq 'R9WrongLeaPrefix') { $lea[0] = 0x4d }
    Put-Bytes $lea 3 ([BitConverter]::GetBytes($leaDisp))
    $sequence = [Collections.Generic.List[object]]::new()
    $sequence.Add(@{ Bytes=$lea; Text='lea r9, [rip+imagebase]' })

    [byte[]]$index = [byte[]]@(0x41,0x0f,0xb6,0x84,0x01,0,0,0,0)
    Put-Bytes $index 5 ([BitConverter]::GetBytes([UInt32]0x1234))
    switch ($Fault) {
        'R9WrongIndexOpcode' { $index[2] = 0xb7 }
        'R9WrongIndexDestination' { $index[3] = 0x8c }
        'R9WrongIndexBase' { $index[4] = 0x00 }
        'R9WrongIndexRegister' { $index[4] = 0x09 }
        'R9WrongIndexScale' { $index[4] = 0x41 }
        'R9WrongIndexPrefix' { $index[0] = 0x49 }
    }
    if ($Fault -eq 'R9NoncontiguousAfterLea') { $sequence.Add(@{ Bytes=[byte[]]@(0x90); Text='nop' }) }
    if ($Fault -eq 'R9MissingByteIndex') { $index = [byte[]]::new(0) }
    if ($index.Length -ne 0) { $sequence.Add(@{ Bytes=$index; Text='movzx eax, byte ptr [r9+rax+1234h]' }) }

    [byte[]]$load = [byte[]]@(0x45,0x8b,0x84,0x81,0,0,0,0)
    Put-Bytes $load 4 ([BitConverter]::GetBytes($table))
    switch ($Fault) {
        'R9WrongLoadOpcode' { $load[1] = 0x89 }
        'R9WrongLoadDestination' { $load[2] = 0x8c }
        'R9WrongLoadBase' { $load[3] = 0x80 }
        'R9WrongLoadIndex' { $load[3] = 0x89 }
        'R9WrongLoadScale' { $load[3] = 0x41 }
        'R9WrongLoadPrefix' { $load[0] = 0x4d }
    }
    if ($Fault -eq 'R9NoncontiguousAfterIndex') { $sequence.Add(@{ Bytes=[byte[]]@(0x90); Text='nop' }) }
    $sequence.Add(@{ Bytes=$load; Text='mov r8d, dword ptr [r9+rax*4+table]' })

    [byte[]]$add = [byte[]]@(0x4d,0x03,0xc1)
    switch ($Fault) {
        'R9WrongAddOpcode' { $add[1] = 0x01 }
        'R9WrongAddDestination' { $add[2] = 0xc9 }
        'R9WrongAddSource' { $add[2] = 0xc0 }
        'R9WrongAddPrefix' { $add[0] = 0x49 }
    }
    if ($Fault -eq 'R9NoncontiguousAfterLoad') { $sequence.Add(@{ Bytes=[byte[]]@(0x90); Text='nop' }) }
    $sequence.Add(@{ Bytes=$add; Text='add r8, r9' })

    [byte[]]$jump = [byte[]]@(0x41,0xff,0xe0)
    if ($Fault -eq 'R9WrongJumpOpcode') { $jump[1] = 0xfe }
    if ($Fault -eq 'R9WrongJumpTarget') { $jump[2] = 0xe1 }
    if ($Fault -eq 'R9WrongJumpPrefix') { $jump[0] = 0x49 }
    if ($Fault -eq 'R9NoncontiguousAfterAdd') { $sequence.Add(@{ Bytes=[byte[]]@(0x90); Text='nop' }) }
    $sequence.Add(@{ Bytes=$jump; Text='jmp r8' })

    $instructionRows = [Collections.Generic.List[string]]::new()
    foreach ($item in $sequence) {
        if ($item.Bytes.Length -ne 0) {
            Put-Bytes $pe (0x400+$cursor-$case.SectionRva) $item.Bytes
            $instructionRows.Add(('{0:x}: {1} {2}' -f ($case.ImageBase+$cursor), (Hex-Bytes $item.Bytes), $item.Text))
            $cursor += $item.Bytes.Length
        }
    }
    if ($Fault -eq 'R9TruncatedTable') {
        # Make the second DWORD terminate table discovery, leaving fewer than two proved entries.
        $tableRaw = 0x400 + $table - $case.SectionRva
        Put-Bytes $pe ($tableRaw+4) ([BitConverter]::GetBytes([UInt32]$table))
    }
    $disassemblyRows = [Collections.Generic.List[string]]::new()
    foreach ($line in [IO.File]::ReadAllLines($case.Disassembly)) {
        $address = [UInt64]::Parse(($line -split ':',2)[0], [Globalization.NumberStyles]::HexNumber)
        if ($address -lt ($case.ImageBase+$dispatch) -or $address -ge ($case.ImageBase+$cursor)) {
            $disassemblyRows.Add($line)
        }
    }
    foreach ($line in $instructionRows) { $disassemblyRows.Add($line) }
    Set-Content -LiteralPath $case.Disassembly -Value (@($disassemblyRows | Sort-Object {
        [UInt64]::Parse(($_ -split ':',2)[0], [Globalization.NumberStyles]::HexNumber)
    }) -join "`n") -Encoding utf8
    [IO.File]::WriteAllBytes($case.Binary, $pe)
    return $case
}

function Assert-DlCombinationRejected([string]$Name, [hashtable]$Options) {
    $rejected = $false
    try {
        $null = New-SyntheticCase $Name -DlByteIndex @Options
    } catch {
        if ($_.Exception.Message -notmatch 'DlByteIndex cannot be combined') { throw }
        $rejected = $true
    }
    if (-not $rejected) { throw "$Name accepted an unsupported DlByteIndex combination." }
    Write-Host "PASS isa-inline-table.$Name"
}

function Assert-RdxCombinationRejected([string]$Name, [hashtable]$Options) {
    $rejected = $false
    try {
        $null = New-SyntheticCase $Name -RdxBase -PdbData @Options
    } catch {
        if ($_.Exception.Message -notmatch 'RdxBase cannot be combined') { throw }
        $rejected = $true
    }
    if (-not $rejected) { throw "$Name accepted an unsupported RdxBase combination." }
    Write-Host "PASS isa-inline-table.$Name"
}

$rdxBase = New-SyntheticCase RdxBase -RdxBase -PdbData
# Mirrors ImageCodecs' observed dispatch at 0x14009011c, its table at 0x140091138,
# and the anonymous 16-byte S_LDATA32 at 0001:590136; the synthetic PE relocates the shape.
Invoke-Control $rdxBase $true 7 0
$case = New-SyntheticCase RdxBaseWithRealVex -RdxBase -PdbData -RealVex
Invoke-Control $case $false 7 1
foreach ($fault in @('RdxWrongLea','RdxWrongLoadIndex','RdxWrongAdd','RdxWrongJump',
        'RdxNoncontiguous','RdxDoubleLoad','MissingDataSymbol','WrongDataOwner','WrongDataStart',
        'WrongDataRecordSize','NamedDataSymbol','TypedDataSymbol','CrossContribution','ContributionBounds',
        'RdxTargetMiddle','RdxTargetMissing','RdxTargetInvalid','TargetBeforeDispatch',
        'TargetAtTable','TargetPastTable','InstructionExtendsPastTable')) {
    $case = New-SyntheticCase "RdxBase$fault" -RdxBase -PdbData -Fault $fault
    Invoke-Control $case $false
}
Assert-RdxCombinationRejected RdxBaseWithOptionalByteIndex @{ OptionalByteIndex=$true }
Assert-RdxCombinationRejected RdxBaseWithDlByteIndex @{ DlByteIndex=$true }
Assert-RdxCombinationRejected RdxBaseWithSignedByteIndex @{ SignedByteIndex=$true }
Assert-RdxCombinationRejected RdxBaseWithR8Base @{ R8Base=$true }
Assert-RdxCombinationRejected RdxBaseWithHoistedR8Base @{ R8Base=$true; HoistedR8Base=$true }
Assert-RdxCombinationRejected RdxBaseWithRaxBase @{ RaxBase=$true }

$case = New-R9R8DispatchCase R9R8Dispatch
Invoke-Control $case $true 7 0
$case = New-R9R8DispatchCase R9R8DispatchWithAdjacentRealVex -RealVex
Invoke-Control $case $false 7 1
foreach ($fault in @(
        'R9WrongLeaDestination','R9WrongLeaOpcode','R9WrongLeaPrefix','R9WrongImageBase',
        'R9WrongIndexOpcode','R9WrongIndexDestination','R9WrongIndexBase','R9WrongIndexRegister',
        'R9WrongIndexScale','R9WrongIndexPrefix','R9MissingByteIndex',
        'R9WrongLoadOpcode','R9WrongLoadDestination','R9WrongLoadBase','R9WrongLoadIndex',
        'R9WrongLoadScale','R9WrongLoadPrefix','R9WrongAddOpcode','R9WrongAddDestination',
        'R9WrongAddSource','R9WrongAddPrefix','R9WrongJumpOpcode','R9WrongJumpTarget','R9WrongJumpPrefix',
        'R9NoncontiguousAfterLea','R9NoncontiguousAfterIndex','R9NoncontiguousAfterLoad','R9NoncontiguousAfterAdd',
        'MissingDataSymbol','WrongDataOwner','WrongDataStart','WrongDataRecordSize','NamedDataSymbol','TypedDataSymbol',
        'CrossContribution','ContributionBounds','TargetBeforeDispatch','TargetAtTable','TargetPastTable',
        'R9TargetMiddle','R9TargetMissing','R9TargetInvalid','R9TruncatedTable')) {
    $case = New-R9R8DispatchCase "R9R8Dispatch$fault" -Fault $fault
    Invoke-Control $case $false
}

$dlByteIndex = New-SyntheticCase DlByteIndex -DlByteIndex
Invoke-Control $dlByteIndex $true 4 0
$case = New-SyntheticCase DlByteIndexWithRealVex -DlByteIndex -RealVex
Invoke-Control $case $false 4 1
foreach ($fault in @('WrongDlDestination','WrongDlSource','WrongDlOpcode','DlNoncontiguous','DlDuplicate',
        'MissingDataSymbol','WrongDataOwner','WrongDataStart','WrongDataRecordSize','NamedDataSymbol',
        'TypedDataSymbol','CrossContribution','ContributionBounds','TargetBeforeDispatch','TargetAtTable',
        'TargetPastTable','MiddleOfInstruction','UndecodedTarget','UnknownTargetInstruction',
        'SignedLastEntryMiddle','InstructionExtendsPastTable')) {
    $case = New-SyntheticCase "DlByteIndex$fault" -DlByteIndex -Fault $fault
    Invoke-Control $case $false
}
Assert-DlCombinationRejected DlByteIndexWithOptionalIndex @{ OptionalByteIndex=$true }
Assert-DlCombinationRejected DlByteIndexWithSignedIndex @{ SignedByteIndex=$true }
Assert-DlCombinationRejected DlByteIndexWithR8Base @{ R8Base=$true }
Assert-DlCombinationRejected DlByteIndexWithHoistedR8Base @{ R8Base=$true; HoistedR8Base=$true; PdbData=$true }
Assert-DlCombinationRejected DlByteIndexWithRaxBase @{ RaxBase=$true; PdbData=$true }

$good = New-SyntheticCase Positive -OptionalByteIndex -ShortNop
Invoke-Control $good $true 2 0
$optionalOff = New-SyntheticCase PositiveWithoutByteIndex
Invoke-Control $optionalOff $true 2 0
$tailPaddingCase = New-TailPaddingCase TailPaddingVex
Set-TailPaddingVexArtifact $tailPaddingCase
Invoke-Control $tailPaddingCase $true 1 0
$case = New-TailPaddingCase TailPaddingVexNonCc
Set-TailPaddingVexArtifact $case -NonCcTail
Invoke-Control $case $false
$case = New-TailPaddingCase TailPaddingVexAtTableEnd
Set-TailPaddingVexArtifact $case -PeTailMismatch
Invoke-Control $case $false
$case = New-TailPaddingCase TailPaddingVexAtTableEndReal
Set-TailPaddingVexArtifact $case -VexAtTableEnd
Invoke-Control $case $false
$case = New-TailPaddingCase TailPaddingVexOverlong
Set-TailPaddingVexArtifact $case -OverlongInstruction
Invoke-Control $case $false
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

$case = New-SyntheticCase SignedByteIndex -SignedByteIndex
Invoke-Control $case $true 4 0
$case = New-SyntheticCase SignedByteIndexWithVexAfterTable -SignedByteIndex -RealVex
Invoke-Control $case $false 4 1
foreach ($fault in @('SignedBadMovsxDestination','SignedBadFrame','SignedBadOpcode','SignedBadSize',
        'SignedNoncontiguous','SignedBiasTwoEntries','MissingDataSymbol','WrongDataOwner','WrongDataStart',
        'WrongDataRecordSize','NamedDataSymbol','TypedDataSymbol','CrossContribution','ContributionBounds',
        'TargetBeforeDispatch','TargetAtTable','SignedDefaultMiddle','SignedDefaultMissing',
        'SignedDefaultPastContribution','SignedLastEntryMiddle','InstructionExtendsPastTable')) {
    $case = New-SyntheticCase "SignedByteIndex$fault" -SignedByteIndex -Fault $fault
    Invoke-Control $case $false
}

foreach ($name in @('WrongImageBase','BrokenSequence','CrossContribution','ContributionBounds',
        'MiddleOfInstruction','UndecodedTarget','UnknownTargetInstruction','TargetBeforeDispatch','NoRet','InstructionExtendsPastTable')) {
    $case = New-SyntheticCase $name -Fault $name
    Invoke-Control $case $false
}

Write-Host "Synthetic ISA parser fixtures: $fixtureRoot"
$global:LASTEXITCODE = 0
