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
        [switch]$SignedWideFrame,
        [switch]$RealVex,
        [switch]$ShortNop,
        [switch]$R8Base,
        [switch]$HoistedR8Base,
        [switch]$RaxBase,
        [switch]$RaxRspIndex,
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

    $SignedIndex = $SignedByteIndex -or $SignedWideFrame
    if ($RdxBase -and ($OptionalByteIndex -or $DlByteIndex -or $SignedIndex -or $R8Base -or $HoistedR8Base -or $RaxBase)) {
        throw 'RdxBase cannot be combined with another index form or base form.'
    }
    if ($DlByteIndex -and ($OptionalByteIndex -or $SignedIndex -or $R8Base -or $HoistedR8Base -or $RaxBase -or $RdxBase)) {
        throw 'DlByteIndex cannot be combined with another index form or base form.'
    }
    if ($RdxBase -and -not $PdbData) { throw 'RdxBase requires PdbData.' }
    if ($RaxRspIndex -and (-not $RaxBase -or -not $PdbData)) {
        throw 'RaxRspIndex requires both RaxBase and PdbData.'
    }
    if ($HoistedR8Base -and (-not $R8Base -or -not $PdbData -or $OptionalByteIndex)) {
        throw 'HoistedR8Base requires R8Base and PdbData, without OptionalByteIndex.'
    }
    if ($RaxBase -and (-not $PdbData -or $R8Base -or $HoistedR8Base -or $OptionalByteIndex -or $RdxBase)) {
        throw 'RaxBase requires PdbData and cannot be combined with R8Base, HoistedR8Base, or OptionalByteIndex.'
    }
    $signedIndexLength = if ($SignedWideFrame) { if ($Fault -eq 'SignedWideWrongFrameSib') { 9 } else { 8 } } elseif ($Fault -eq 'SignedBadFrame') { 6 } else { 5 }
    $signedGapLength = if ($Fault -in @('SignedNoncontiguous','SignedWideNoncontiguous')) { 1 } else { 0 }
    $caseDispatchRva = if ($HoistedR8Base) { [UInt32]($sectionRva + 0x880) } elseif ($SignedIndex) { [UInt32]($dispatchRva + $signedIndexLength + $signedGapLength) } else { $dispatchRva }
    [byte[]]$lea = if ($RaxBase) { [byte[]]@(0x48,0x8d,0x05,0,0,0,0) } elseif ($RdxBase) { [byte[]]@(0x48,0x8d,0x15,0,0,0,0) } else { [byte[]]@(0x48,0x8d,0x0d,0,0,0,0) }
    if ($RdxBase -and $Fault -eq 'RdxWrongLea') { $lea[2] = 0x0d }
    if ($R8Base) { $lea[0] = 0x4c; $lea[2] = 0x05 }
    if ($Fault -eq 'WrongImageBase') { $leaDisp = [Int32]($imageBase - ($imageBase + $caseDispatchRva + 7) + 1) }
    else { $leaDisp = [Int32]($imageBase - ($imageBase + $caseDispatchRva + 7)) }
    Put-Bytes $lea 3 ([BitConverter]::GetBytes($leaDisp))
    if ($SignedIndex) {
        [byte[]]$signedIndex = if ($SignedWideFrame) { [byte[]]@(0x48,0x0f,0xbe,0x85,0xc0,0x01,0x00,0x00) } elseif ($Fault -eq 'SignedBadFrame') { [byte[]]@(0x48,0x0f,0xbe,0x44,0x24,0xc0) } else { [byte[]]@(0x48,0x0f,0xbe,0x45,0xc0) }
        switch ($Fault) {
            'SignedBadMovsxDestination' { $signedIndex[0] = 0x49 }
            'SignedBadFrame' { $signedIndex[4] = 0x08 }
            'SignedBadOpcode' { $signedIndex[2] = 0xb6 }
            'SignedBadSize' { $signedIndex = [byte[]]@(0x48,0x0f,0xbf,0x45,0xc0) }
            'SignedWideWrongRexDestination' { $signedIndex[0] = 0x49 }
            'SignedWideWrongOpcode' { $signedIndex[2] = 0xb6 }
            'SignedWideWrongFrameBase' { $signedIndex[3] = 0x87 }
            'SignedWideWrongFrameSib' { $signedIndex = [byte[]]@(0x48,0x0f,0xbe,0x84,0x05,0xc0,0x01,0x00,0x00) }
        }
        Put-Bytes $pe (0x400+$dispatchRva-$sectionRva) $signedIndex
        if ($signedGapLength -ne 0) { $pe[0x400+$dispatchRva-$sectionRva+$signedIndex.Length] = 0x90 }
    }
    Put-Bytes $pe (0x400+$caseDispatchRva-$sectionRva) $lea
    $cursor = [UInt32]($caseDispatchRva + 7)
    $instructions = [Collections.Generic.List[string]]::new()
    $leaText = if ($RaxBase) { 'lea rax, [rip+imagebase]' } elseif ($RdxBase) { 'lea rdx, [rip+imagebase]' } elseif ($R8Base) { 'lea r8, [rip+imagebase]' } else { 'lea rcx, [rip+imagebase]' }
    $instructions.Add(('{0:x}: {1} {2}' -f ($imageBase+$caseDispatchRva), (Hex-Bytes $lea), $leaText))
    $optionalIndexInstructionRva = [UInt32]0
    $optionalIndexInstructionIndex = -1
    $optionalIndexText = ''
    if ($SignedIndex) {
        $indexText = if ($Fault -eq 'SignedBadFrame') { 'movsx rax, byte ptr [rsp-40h]' } elseif ($Fault -eq 'SignedBadSize') { 'movsx rax, word ptr [rbp-40h]' } elseif ($Fault -eq 'SignedWideWrongFrameBase') { 'movsx rax, byte ptr [rdi+1c0h]' } elseif ($Fault -eq 'SignedWideWrongFrameSib') { 'movsx rax, byte ptr [rbp+rax+1c0h]' } elseif ($SignedWideFrame) { 'movsx rax, byte ptr [rbp+1c0h]' } else { 'movsx rax, byte ptr [rbp-40h]' }
        $instructions.Insert(0, ('{0:x}: {1} {2}' -f ($imageBase+$dispatchRva), (Hex-Bytes $signedIndex), $indexText))
        if ($signedGapLength -ne 0) {
            $instructions.Insert(1, ('{0:x}: 90 nop' -f ($imageBase+$dispatchRva+$signedIndex.Length)))
        }
    }
    if ($OptionalByteIndex) {
        $indexLoad = [byte[]]@(0x0f,0xb6,0x84,0x01,0,0,0,0)
        if ($R8Base) {
            $indexLoad = [byte[]]@(0x41,0x0f,0xb6,0x84,0x00,0,0,0,0)
            if ($TargetRegister -eq 2) { $indexLoad[3] = 0x8c; $indexLoad[4] = 0x08 }
        }
        $optionalIndexInstructionRva = $cursor
        $optionalIndexInstructionIndex = $instructions.Count
        $optionalIndexText = if ($R8Base) { 'movzx eax, byte ptr [r8+rax+index]' } else { 'movzx eax, byte ptr [rcx+rax+index]' }
        Put-Bytes $pe (0x400+$cursor-$sectionRva) $indexLoad
        $instructions.Add(('{0:x}: {1} {2}' -f ($imageBase+$cursor), (Hex-Bytes $indexLoad), $optionalIndexText))
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
        if ($RaxRspIndex) {
            [byte[]]$indexLoad = [byte[]]@(0x48,0x8b,0x4c,0x24,0x20)
            switch ($Fault) {
                'RaxRspWrongRexDestination' { $indexLoad[0] = 0x4c }
                'RaxRspWrongOpcode' { $indexLoad[1] = 0x89 }
                'RaxRspWrongLoadSize' { $indexLoad[0] = 0x40 }
                'RaxRspWrongFrameBase' { $indexLoad[3] = 0x25 }
                'RaxRspWrongSib' { $indexLoad[3] = 0x44 }
            }
            $indexLoadText = switch ($Fault) {
                'RaxRspWrongRexDestination' { 'mov r9, qword ptr [rsp+20h]' }
                'RaxRspWrongOpcode' { 'mov qword ptr [rsp+20h], rcx' }
                'RaxRspWrongLoadSize' { 'mov ecx, dword ptr [rsp+20h]' }
                'RaxRspWrongFrameBase' { 'mov rcx, qword ptr [rbp+20h]' }
                'RaxRspWrongSib' { 'mov rcx, qword ptr [rsp+rax*2+20h]' }
                default { 'mov rcx, qword ptr [rsp+20h]' }
            }
        } else {
            [byte[]]$indexLoad = [byte[]]@(0x48,0x8b,0x8d,0,0,0,0)
            if ($Fault -eq 'CorruptedIndexLoad') { $indexLoad[2] = 0x95 }
            $indexLoadText = 'mov rcx, qword ptr [rbp+index]'
        }
        Put-Bytes $pe (0x400+$cursor-$sectionRva) $indexLoad
        $instructions.Add(('{0:x}: {1} {2}' -f ($imageBase+$cursor), (Hex-Bytes $indexLoad), $indexLoadText))
        $cursor += $indexLoad.Length
        if ($Fault -in @('ClobberedIndex','RaxRspClobberedIndex')) {
            $clobber = [byte[]]@(0x48,0x89,0xd1)
            Put-Bytes $pe (0x400+$cursor-$sectionRva) $clobber
            $instructions.Add(('{0:x}: {1} mov rcx, rdx' -f ($imageBase+$cursor), (Hex-Bytes $clobber)))
            $cursor += $clobber.Length
        }
        if ($Fault -eq 'RaxRspNoncontiguous') {
            $pe[0x400+$cursor-$sectionRva] = 0x90
            $instructions.Add(('{0:x}: 90 nop' -f ($imageBase+$cursor)))
            $cursor++
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
    $loadDisp = if ($SignedIndex) { [UInt32](($actualTableRva + $(if ($Fault -eq 'SignedBiasTwoEntries') { 8 } else { 4 })) -band 0xffffffff) } else { [UInt32]($actualTableRva -band 0xffffffff) }
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
    $tableLoadText = if ($RaxBase) { 'mov ecx, dword ptr [rax+rcx*4+table]' } elseif ($RdxBase) { 'mov ecx, dword ptr [rdx+rax*4+table]' } elseif ($HoistedR8Base) { 'mov ecx, dword ptr [r8+rax*4+table]' } elseif ($SignedIndex) { 'mov eax, dword ptr [rcx+4*rax+table+4]' } else { 'mov eax, dword ptr [rcx+rax*4+table]' }
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
    if ($OptionalByteIndex) {
        $byteIndexTableRva = [UInt32]($actualTableRva + 8)
        $dispOffset = if ($R8Base) { 5 } else { 4 }
        Put-Bytes $indexLoad $dispOffset ([BitConverter]::GetBytes($byteIndexTableRva))
        Put-Bytes $pe (0x400+$optionalIndexInstructionRva-$sectionRva) $indexLoad
        $instructions[$optionalIndexInstructionIndex] = ('{0:x}: {1} {2}' -f
            ($imageBase+$optionalIndexInstructionRva), (Hex-Bytes $indexLoad), $optionalIndexText)
    }

    $target = if ($HoistedR8Base) { $bodyFirstRva } else { $targetRva }
    if ($Fault -eq 'TargetBeforeDispatch') { $target = $caseDispatchRva }
    if ($Fault -eq 'TargetAtTable') { $target = $actualTableRva }
    if ($Fault -eq 'TargetPastTable') {
        $pastTableOffset = if ($RdxBase) { 28 } elseif ($DlByteIndex) { 16 } else { 8 }
        $target = [UInt32]($actualTableRva + $pastTableOffset)
    }
    $target2 = if ($HoistedR8Base) { $bodyFirstRva } else { $targetRva }
    if ($SignedIndex) {
        switch ($Fault) {
            'TargetBeforeDispatch' { $target = $caseDispatchRva }
            'TargetAtTable' { $target = $actualTableRva }
            'TargetPastTable' { $target = [UInt32]($actualTableRva + 16) }
            'SignedDefaultMiddle' { $target = [UInt32]($targetRva + 1) }
            'SignedDefaultMissing' { $target = [UInt32]($targetRva + 0x10) }
            'SignedDefaultPastContribution' { $target = [UInt32]($sectionRva + 0x2000) }
        }
    }
    [byte[]]$tableBytes = if ($RdxBase) { [byte[]]::new(28) } elseif ($SignedIndex -or $DlByteIndex) { [byte[]]::new(16) } else { [byte[]]::new(8) }
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
    if ($SignedIndex -or $DlByteIndex) {
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
    if ($OptionalByteIndex) {
        Put-Bytes $pe (0x400+$actualTableRva+$tableBytes.Length-$sectionRva) ([byte[]]@(0,1,0,1))
    }

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
    if (($PdbData -or $OptionalByteIndex -or $SignedIndex -or $DlByteIndex -or $RdxBase) -and $Fault -notin @('MissingDataSymbol','MissingDataWithRet')) {
        $dataModule = if ($Fault -eq 'WrongDataOwner') { 1 } else { 0 }
        $dataOffset = $actualTableRva - $sectionRva
        if ($Fault -eq 'WrongDataStart') { $dataOffset += 4 }
        $dataName = if (($RaxBase -or $SignedIndex -or $DlByteIndex -or $RdxBase) -and $Fault -eq 'NamedDataSymbol') { 'dispatchTable' } else { '' }
        $dataType = if (($RaxBase -or $SignedIndex -or $DlByteIndex -or $RdxBase) -and $Fault -eq 'TypedDataSymbol') { 'unsigned long' } else { '<no type>' }
        $dataSize = if (($RdxBase -or $RaxBase -or $SignedIndex -or $DlByteIndex) -and $Fault -eq 'WrongDataRecordSize') { 15 } else { 16 }
        $symbolText = "  Mod $dataModule | ``SyntheticControl.obj``:`n  0 | S_LDATA32 [size = $dataSize] ``$dataName`` `n".TrimEnd() +
            "`n  type = $dataType, addr = 0001:$dataOffset`n"
        if ($OptionalByteIndex) {
            $byteModule = if ($Fault -eq 'WrongDataOwner') { 1 } else { 0 }
            $byteOffset = [UInt32]($dataEnd - $sectionRva)
            $symbolText += "  Mod $byteModule | ``SyntheticControl.obj``:`n  0 | S_LDATA32 [size = 16] `````n  type = <no type>, addr = 0001:$byteOffset`n"
            $symbolText += "  Mod $byteModule | ``SyntheticControl.obj``:`n  0 | S_LDATA32 [size = 16] `````n  type = <no type>, addr = 0001:$($byteOffset+4)`n"
        }
    }
    Set-Content -LiteralPath $symbols -Value $symbolText -Encoding utf8

    # The target is a decoded boundary only in the valid case; middle-target deliberately lands inside it.
    $targetInstruction = [byte[]]@(0x66,0x90)
    $targetStart = if ($Fault -eq 'MiddleOfInstruction') { $targetRva - 1 } else { $targetRva }
    Put-Bytes $pe (0x400+$targetStart-$sectionRva) $targetInstruction
    if ($Fault -eq 'UnknownTargetInstruction') { $pe[0x400+$targetStart-$sectionRva] = 0x06 }
    $realVexRva = if ($RdxBase) { [UInt32]($actualTableRva + 28) } elseif ($SignedIndex -or $DlByteIndex -or $RaxRspIndex) { [UInt32]($actualTableRva + 16) } elseif ($HoistedR8Base) { [UInt32]($actualTableRva + 0x20) } else { [UInt32]($caseDispatchRva + 0x20) }
    [byte[]]$realVexBytes = if ($SignedIndex -or $DlByteIndex -or $RdxBase) { [byte[]]@(0xc5,0xf8,0x10,0x40,0x01) } else { [byte[]]@(0xc5,0xf8,0x10,0x00) }
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
        $realVexText = if ($SignedIndex -or $DlByteIndex -or $RdxBase) { 'vmovups xmm0, xmmword ptr [rax+1]' } else { 'vmovups xmm0, xmmword ptr [rax]' }
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
    if ($SignedIndex -or $DlByteIndex) {
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
        } elseif ($SignedIndex -or $DlByteIndex) {
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

function New-DataProofCase([string]$Name, [string]$Fault = '') {
    $directory = Join-Path $fixtureRoot $Name
    [void](New-Item -ItemType Directory -Path $directory -Force)
    $binary = Join-Path $directory 'Synthetic.exe'
    $metadata = Join-Path $directory 'contributions.txt'
    $symbols = Join-Path $directory 'symbols.txt'
    $disassembly = Join-Path $directory 'disassembly.txt'
    $section = [UInt32]0x0010f000
    $dispatch = [UInt32]0x0010f000
    if ($Fault -in @('BackwardValid','MissingProcedure','BadProcedureBounds','CrossProcedure','BackwardMiddle','UnknownTarget','ShortProcedure','WrongProcedureOwner')) {
        $dispatch = [UInt32]0x0010f300
    }
    $dword = [UInt32]($section + 0x1000)
    $byteTable = [UInt32]($dword + 12)
    $terminal = $Fault.StartsWith('Terminal')
    $targets = if ($dispatch -eq $section) { @([UInt32]($section+0x262),[UInt32]($section+0x282),[UInt32]($section+0x2a2)) } else { @([UInt32]($section+0x62),[UInt32]($section+0x162),[UInt32]($section+0x262)) }
    if ($Fault -eq 'BackwardMiddle') { $targets[0]++ }
    if ($Fault -eq 'UnknownTarget') { $targets[0] = [UInt32]($section+0x100) }
    if ($Fault -eq 'MalformedDwordLastEntry') { $targets[2] = [UInt32]($dword+16) }
    $indexValues = [byte[]]@(0,1,2)
    if ($Fault -in @('MalformedByteRange','TerminalMalformedByte')) { $indexValues[2] = 3 }
    if ($Fault.StartsWith('AlignedByte')) {
        $indexValues = [byte[]]@(0,1,2,0,1,0x0f,0x1f,0)
        if ($Fault -eq 'AlignedByteMalformedNop') { $indexValues[6]=0x1e }
        if ($Fault -eq 'AlignedByteInvalidIndex') { $indexValues[4]=3 }
        if ($Fault -eq 'AlignedByteRepeatedNop') { $indexValues += [byte[]]@(0x0f,0x1f,0) }
    }
    $pe = [byte[]]::new(0x2400)
    $pe[0]=0x4d; $pe[1]=0x5a; Set-U32 $pe 0x3c 0x80; Set-U32 $pe 0x80 0x4550
    Set-U16 $pe 0x84 0x8664; Set-U16 $pe 0x86 1; Set-U16 $pe 0x94 0xf0; Set-U16 $pe 0x98 0x20b
    Set-U64 $pe (0x98+24) $imageBase; Set-U32 $pe (0x98+56) 0x2000; Set-U32 $pe (0x98+60) 0x400
    Set-U32 $pe (0x98+112+6*8) ($section+0x400); Set-U32 $pe (0x98+116+6*8) 28
    $sh=0x80+24+0xf0; Put-Bytes $pe $sh ([Text.Encoding]::ASCII.GetBytes('.text'+[char]0+[char]0+[char]0))
    Set-U32 $pe ($sh+8) 0x2000; Set-U32 $pe ($sh+12) $section; Set-U32 $pe ($sh+16) 0x2000
    Set-U32 $pe ($sh+20) 0x400; Set-U32 $pe ($sh+36) 0x60000020
    Set-U32 $pe (0x400+0x400+12) 2; Set-U32 $pe (0x400+0x400+16) 24
    Set-U32 $pe (0x400+0x400+20) ($section+0x420); Set-U32 $pe (0x400+0x400+24) 0x820
    Put-Bytes $pe 0x820 ([Text.Encoding]::ASCII.GetBytes('RSDS')); Put-Bytes $pe 0x824 $guid.ToByteArray(); Set-U32 $pe 0x834 $age
    $leaAt=$dispatch; [byte[]]$lea=@(0x4c,0x8d,0x05,0,0,0,0)
    Put-Bytes $lea 3 ([BitConverter]::GetBytes([Int32]($imageBase-($imageBase+$leaAt+7))))
    $cursor=[UInt32]($dispatch+7); $rows=[Collections.Generic.List[string]]::new()
    Put-Bytes $pe (0x400+$leaAt-$section) $lea; $rows.Add(('{0:x}: {1} lea r8, [rip+imagebase]' -f ($imageBase+$leaAt),(Hex-Bytes $lea)))
    [byte[]]$indexLoad=@(0x41,0x0f,0xb6,0x8c,0x08,0,0,0,0)
    $indexDisplacement=$byteTable
    if ($Fault -eq 'WrongDispatch') { $indexDisplacement++ }
    Put-Bytes $indexLoad 5 ([BitConverter]::GetBytes($indexDisplacement))
    Put-Bytes $pe (0x400+$cursor-$section) $indexLoad
    $rows.Add(('{0:x}: {1} movzx ecx, byte ptr [r8+rcx+index]' -f ($imageBase+$cursor),(Hex-Bytes $indexLoad))); $cursor += 9
    [byte[]]$tableLoad=@(0x41,0x8b,0x94,0x88,0,0,0,0); Put-Bytes $tableLoad 4 ([BitConverter]::GetBytes($dword))
    Put-Bytes $pe (0x400+$cursor-$section) $tableLoad
    $rows.Add(('{0:x}: {1} mov edx, dword ptr [r8+rcx*4+table]' -f ($imageBase+$cursor),(Hex-Bytes $tableLoad))); $cursor += 8
    [byte[]]$add=@(0x49,0x03,0xd0); Put-Bytes $pe (0x400+$cursor-$section) $add
    $rows.Add(('{0:x}: {1} add rdx, r8' -f ($imageBase+$cursor),(Hex-Bytes $add))); $cursor += 3
    [byte[]]$jump=@(0xff,0xe2); Put-Bytes $pe (0x400+$cursor-$section) $jump
    $rows.Add(('{0:x}: {1} jmp rdx' -f ($imageBase+$cursor),(Hex-Bytes $jump)))
    for ($i=0; $i -lt 3; ++$i) { Set-U32 $pe (0x400+$dword-$section+4*$i) $targets[$i] }
    Put-Bytes $pe (0x400+$byteTable-$section) $indexValues
    foreach ($target in ($targets | Select-Object -Unique)) {
        $instructionAddress=$target
        if ($Fault -eq 'BackwardMiddle' -and $target -eq $targets[0]) { $instructionAddress-- }
        if ($Fault -eq 'UnknownTarget' -and $target -eq $targets[0]) {
            $pe[0x400+$target-$section]=0x06; $rows.Add(('{0:x}: 06 <unknown>' -f ($imageBase+$target)))
        } else {
            Put-Bytes $pe (0x400+$instructionAddress-$section) ([byte[]]@(0x66,0x90)); $rows.Add(('{0:x}: 66 90 nop' -f ($imageBase+$instructionAddress)))
        }
    }
    if ($Fault -eq 'ActualVexOutside') {
        Put-Bytes $pe (0x400+$section-$section+0x300) ([byte[]]@(0xc5,0xf8,0x10,0x00))
        $rows.Add(('{0:x}: c5 f8 10 00 vmovups xmm0, xmmword ptr [rax]' -f ($imageBase+$section+0x300)))
    }
    foreach ($i in 0..2) { $rows.Add(('{0:x}: {1} vmovups xmm0, xmmword ptr [rax]' -f ($imageBase+$dword+4*$i),(Hex-Bytes ([byte[]]$pe[(0x400+$dword-$section+4*$i)..(0x400+$dword-$section+4*$i+3)])))) }
    $symbolsText="  Mod 0 | ``Synthetic.obj``:`n"
    if ($Fault -notin @('MissingProcedure','TerminalMissingProcedure')) {
        $procOffset=if ($Fault -eq 'CrossProcedure') { $dispatch-$section-0x80 } elseif ($Fault -eq 'BadProcedureBounds') { $dispatch-$section-0x80 } else { 0 }
        $procSize=if ($Fault -eq 'CrossProcedure') { 0x100 } else { 0x800 }
        if ($Fault -in @('BackwardValid','WrongProcedureOwner')) { $procOffset=0; $procSize=0x1100 }
        if ($Fault -eq 'BadProcedureBounds') { $procOffset=0; $procSize=0x2001 }
        if ($Fault -eq 'ShortProcedure') { $procOffset=0; $procSize=0x400 }
        if ($Fault -in @('BackwardMiddle','UnknownTarget')) { $procOffset=0; $procSize=0x1100 }
        if ($terminal) { $procOffset=0; $procSize=0x100f }
        if ($Fault -eq 'TerminalShortProcedure') { $procSize=0x100e }
        if ($Fault -in @('WrongProcedureOwner','TerminalWrongProcedureOwner')) { $symbolsText = "  Mod 1 | ``Other.obj``:`n" }
        $symbolsText += "  0 | S_GPROC32 [size = 16] ``SyntheticProc`` `n  addr = 0001:$procOffset, code size = $procSize`n"
        if ($Fault -eq 'CrossProcedure') {
            $symbolsText += "  0 | S_GPROC32 [size = 16] ``SeparateTargetProc`` `n  addr = 0001:64, code size = 128`n"
        }
    }
    $symbolsText += "  Mod 0 | ``Synthetic.obj``:`n  0 | S_LDATA32 [size = 16] `````n  type = <no type>, addr = 0001:$($dword-$section)`n"
    $byteSymbolOffset=$byteTable-$section; if ($Fault -eq 'WrongByteStart') { $byteSymbolOffset++ }
    $symbolModule=if ($Fault -eq 'WrongByteOwner') { 1 } else { 0 }
    $byteName = if ($Fault -eq 'NamedByteSymbol') { 'NamedTable' } else { '' }
    $byteType = if ($Fault -eq 'TypedByteSymbol') { '0x1000' } else { '<no type>' }
    $symbolsText += "  Mod $symbolModule | ``Synthetic.obj``:`n  0 | S_LDATA32 [size = 16] ``$byteName```n  type = $byteType, addr = 0001:$byteSymbolOffset`n"
    if ($Fault -ne 'MissingByteEnd' -and -not $terminal) {
        $endModule = if ($Fault -eq 'WrongByteEndOwner') { 1 } else { $symbolModule }
        $symbolsText += "  Mod $endModule | ``Synthetic.obj``:`n  0 | S_LDATA32 [size = 16] `````n  type = <no type>, addr = 0001:$($byteTable-$section+$indexValues.Length)`n"
    }
    $contributionSize = if ($terminal) { 0x100f } else { 8192 }
    if ($Fault -eq 'TerminalWrongContributionEnd') { $contributionSize=0x1010 }
    $metadataText="  Age: $age`n  GUID: {$guid}`n  Mod 0 | ``Synthetic.obj``:`n  Obj: ``Synthetic.obj``:`n  Mod 1 | ``Other.obj``:`n  Obj: ``Other.obj``:`n`nSection Contributions`n  SC[.text] | mod = 0, 0001:0, size = $contributionSize, data crc = 0, reloc crc = 0`n"
    [IO.File]::WriteAllBytes($binary,$pe)
    Set-Content -LiteralPath $metadata -Value $metadataText -Encoding utf8
    Set-Content -LiteralPath $symbols -Value $symbolsText -Encoding utf8
    Set-Content -LiteralPath $disassembly -Value (@($rows | Sort-Object { [UInt64]::Parse(($_ -split ':',2)[0],[Globalization.NumberStyles]::HexNumber) }) -join "`n") -Encoding utf8
    return @{Name=$Name;Binary=$binary;Metadata=$metadata;Symbols=$symbols;Disassembly=$disassembly}
}

$dataProofCount=0
foreach ($caseName in @('ValidByteIndex','MalformedByteRange','WrongByteStart','WrongByteOwner','WrongDispatch','ActualVexOutside','MalformedDwordLastEntry','MissingByteEnd','NamedByteSymbol','TypedByteSymbol','WrongByteEndOwner')) {
    $case=New-DataProofCase $caseName $(if ($caseName -eq 'ValidByteIndex') { '' } else { $caseName })
    Invoke-Control $case ($caseName -eq 'ValidByteIndex'); ++$dataProofCount
}
foreach ($caseName in @('BackwardValid','MissingProcedure','BadProcedureBounds','CrossProcedure','BackwardMiddle','UnknownTarget','ShortProcedure','WrongProcedureOwner')) {
    $case=New-DataProofCase $caseName $caseName
    Invoke-Control $case ($caseName -eq 'BackwardValid'); ++$dataProofCount
}
foreach ($caseName in @('TerminalValid','TerminalMissingProcedure','TerminalShortProcedure','TerminalWrongProcedureOwner','TerminalWrongContributionEnd','TerminalMalformedByte')) {
    $case=New-DataProofCase $caseName $caseName
    Invoke-Control $case ($caseName -eq 'TerminalValid'); ++$dataProofCount
}
foreach ($caseName in @('AlignedByteValid','AlignedByteMalformedNop','AlignedByteInvalidIndex','AlignedByteRepeatedNop')) {
    $case=New-DataProofCase $caseName $caseName
    Invoke-Control $case ($caseName -eq 'AlignedByteValid'); ++$dataProofCount
}
Write-Host "ISA data-proof synthetic controls passed: $dataProofCount"

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
    $byteIndexTable = [UInt32]($table + 28)
    Put-Bytes $index 5 ([BitConverter]::GetBytes($byteIndexTable))
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
    Put-Bytes $pe (0x400+$byteIndexTable-$case.SectionRva) ([byte[]]@(0,1,2,3,4,5,6))
    $outsideVexRva = [UInt32]($byteIndexTable+7)
    if ($RealVex) { Put-Bytes $pe (0x400+$outsideVexRva-$case.SectionRva) ([byte[]]@(0xc5,0xf8,0x10,0x40,0x01)) }
    $byteOffset = [UInt32]($byteIndexTable-$case.SectionRva)
    Add-Content -LiteralPath $case.Symbols -Value "  Mod 0 | ``SyntheticControl.obj``:`n  0 | S_LDATA32 [size = 16] `````n  type = <no type>, addr = 0001:$byteOffset`n  Mod 0 | ``SyntheticControl.obj``:`n  0 | S_LDATA32 [size = 16] `````n  type = <no type>, addr = 0001:$($byteOffset+7)`n" -Encoding utf8
    $disassemblyRows = [Collections.Generic.List[string]]::new()
    foreach ($line in [IO.File]::ReadAllLines($case.Disassembly)) {
        $address = [UInt64]::Parse(($line -split ':',2)[0], [Globalization.NumberStyles]::HexNumber)
        if (($address -lt ($case.ImageBase+$dispatch) -or $address -ge ($case.ImageBase+$cursor)) -and
            (-not $RealVex -or $address -ne ($case.ImageBase+$byteIndexTable))) {
            $disassemblyRows.Add($line)
        }
    }
    foreach ($line in $instructionRows) { $disassemblyRows.Add($line) }
    if ($RealVex) { $disassemblyRows.Add(('{0:x}: c5 f8 10 40 01 vmovups xmm0, xmmword ptr [rax+1]' -f ($case.ImageBase+$outsideVexRva))) }
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

# Observed MSVC ASan std::visit form: LEA RAX,imagebase; MOV RCX,[RSP+20h];
# MOV ECX,[RAX+4*RCX+table]; ADD RCX,RAX; JMP RCX. For this fixture, the
# LEA begins at VA 0x14010f000, the stack load at 0x14010f007, and the table
# lookup at 0x14010f00c (RVA 0x0010f00c).
$case = New-SyntheticCase RaxRspIndex -RaxBase -RaxRspIndex -PdbData
Invoke-Control $case $true 2 0
$fixtureBytes = [IO.File]::ReadAllBytes($case.Binary)
$expectedRspLoad = [byte[]]@(0x48,0x8b,0x4c,0x24,0x20)
$actualRspLoad = [byte[]]$fixtureBytes[0x407..0x40b]
if ((Hex-Bytes $actualRspLoad) -ne (Hex-Bytes $expectedRspLoad)) {
    throw 'RaxRspIndex: PE bytes at RVA 0x0010f007 do not match the literal RSP qword load.'
}
$listingRows = [IO.File]::ReadAllLines($case.Disassembly)
$expectedRspListing = '14010f007: 48 8b 4c 24 20 mov rcx, qword ptr [rsp+20h]'
$expectedTableListing = '14010f00c: 8b 8c 88 00 00 11 00 mov ecx, dword ptr [rax+rcx*4+table]'
if ($expectedRspListing -cnotin $listingRows -or $expectedTableListing -cnotin $listingRows) {
    throw 'RaxRspIndex: listing bytes or instruction addresses do not match the literal PE layout.'
}
$case = New-SyntheticCase RaxRspIndexWithAdjacentRealVex -RaxBase -RaxRspIndex -PdbData -RealVex
Invoke-Control $case $false 2 1
foreach ($fault in @(
        'RaxRspWrongRexDestination','RaxRspWrongOpcode','RaxRspWrongLoadSize',
        'RaxRspWrongFrameBase','RaxRspWrongSib','RaxRspNoncontiguous','RaxRspClobberedIndex',
        'WrongImageBase','MismatchedTableBase','MismatchedTableIndex','MismatchedTableDestination',
        'WrongRaxAdd','WrongRaxJump','MissingDataSymbol','WrongDataOwner','WrongDataStart',
        'WrongDataRecordSize','NamedDataSymbol','TypedDataSymbol','CrossContribution','ContributionBounds',
        'MiddleOfInstruction','UndecodedTarget','UnknownTargetInstruction','TargetBeforeDispatch',
        'TargetAtTable','TargetPastTable','InstructionExtendsPastTable')) {
    $case = New-SyntheticCase "RaxRspIndex$fault" -RaxBase -RaxRspIndex -PdbData -Fault $fault
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

# Observed HostPathReadTests form: movsx rax, byte ptr [rbp+1c0h] with a disp32,
# followed contiguously by the existing image-base/table-load/add/jump dispatch.
$case = New-SyntheticCase SignedWidePositiveDisp32WithDefaultBeforeTable -SignedWideFrame
Invoke-Control $case $true 4 0
$case = New-SyntheticCase SignedWideFrameWithRealVex -SignedWideFrame -RealVex
Invoke-Control $case $false 4 1
foreach ($fault in @('SignedWideWrongRexDestination','SignedWideWrongOpcode','SignedWideWrongFrameBase',
        'SignedWideWrongFrameSib','SignedWideNoncontiguous','MissingDataSymbol','WrongDataOwner',
        'WrongDataStart','WrongDataRecordSize','NamedDataSymbol','TypedDataSymbol','CrossContribution',
        'ContributionBounds','TargetBeforeDispatch','TargetAtTable','TargetPastTable','SignedDefaultMiddle',
        'SignedDefaultMissing','SignedDefaultPastContribution','SignedLastEntryMiddle','UndecodedTarget',
        'InstructionExtendsPastTable')) {
    $case = New-SyntheticCase "SignedWideFrame$fault" -SignedWideFrame -Fault $fault
    Invoke-Control $case $false
}

foreach ($name in @('WrongImageBase','BrokenSequence','CrossContribution','ContributionBounds',
        'MiddleOfInstruction','UndecodedTarget','UnknownTargetInstruction','TargetBeforeDispatch','NoRet','InstructionExtendsPastTable')) {
    $case = New-SyntheticCase $name -Fault $name
    Invoke-Control $case $false
}

function New-AdjacentTablesCase([string]$Name, [string]$Fault = '', [switch]$RealVex) {
    # Independent full-scan control using the observed seven EVEX-looking bytes across the table boundary.
    # Only the PE/CodeView envelope is reused; dispatch, RVAs, symbols and listings below are explicit.
    $case = New-SyntheticCase $Name -PdbData
    $originalPe = [IO.File]::ReadAllBytes($case.Binary)
    $pe = [byte[]]::new(0x3400)
    [Array]::Copy($originalPe, 0, $pe, 0, 0x600)
    [UInt32]$sectionStart = 0x00595000
    [UInt32]$firstTable = 0x00597000
    [UInt32]$secondTable = $firstTable + 8
    if ($Fault -eq 'Gap') { $secondTable += 4 }
    [UInt32]$firstTarget = 0x00596209
    [UInt32]$secondTarget = 0x00595e09
    $sectionHeader = 0x80 + 24 + 0xf0
    Set-U32 $pe ($sectionHeader + 8) 0x3000
    Set-U32 $pe ($sectionHeader + 12) $sectionStart
    Set-U32 $pe ($sectionHeader + 16) 0x3000
    Set-U32 $pe (0x98 + 56) ($sectionStart + 0x3000)
    Set-U32 $pe (0x98 + 112 + 6*8) ($sectionStart + 0x180)
    Set-U32 $pe (0x400 + 0x180 + 20) ($sectionStart + 0x1a0)
    $rows = [Collections.Generic.List[string]]::new()
    foreach ($index in 0,1) {
        [UInt32]$cursor = $sectionStart + $index * 0x20
        [UInt32]$table = if ($index -eq 0) { $firstTable } else { $secondTable }
        [byte[]]$lea = @(0x48,0x8d,0x0d,0,0,0,0)
        Put-Bytes $lea 3 ([BitConverter]::GetBytes([Int32](-[Int64]$cursor - 7)))
        Put-Bytes $pe (0x400 + $cursor - $sectionStart) $lea
        $rows.Add(('{0:x}: {1} lea rcx, [rip+imagebase]' -f ($imageBase+$cursor), (Hex-Bytes $lea)))
        $cursor += 7
        if ($index -eq 1 -and $Fault -eq 'SecondNoncontiguous') {
            $pe[0x400 + $cursor - $sectionStart] = 0x90
            $rows.Add(('{0:x}: 90 nop' -f ($imageBase+$cursor)))
            $cursor++
        }
        [byte[]]$load = @(0x8b,0x84,0x81,0,0,0,0)
        Put-Bytes $load 3 ([BitConverter]::GetBytes($table))
        if ($index -eq 1 -and $Fault -eq 'SecondDispatchBroken') { $load[2] = 0x80 }
        Put-Bytes $pe (0x400 + $cursor - $sectionStart) $load
        $rows.Add(('{0:x}: {1} mov eax, dword ptr [rcx+4*rax+table]' -f ($imageBase+$cursor), (Hex-Bytes $load)))
        $cursor += 7
        [byte[]]$add = @(0x48,0x03,0xc1)
        Put-Bytes $pe (0x400 + $cursor - $sectionStart) $add
        $rows.Add(('{0:x}: {1} add rax, rcx' -f ($imageBase+$cursor), (Hex-Bytes $add)))
        $cursor += 3
        [byte[]]$jump = @(0xff,0xe0)
        Put-Bytes $pe (0x400 + $cursor - $sectionStart) $jump
        $rows.Add(('{0:x}: {1} jmp rax' -f ($imageBase+$cursor), (Hex-Bytes $jump)))
    }
    foreach ($target in $secondTarget,$firstTarget) {
        [byte[]]$instruction = @(0x66,0x90)
        $text = 'nop'
        if ($Fault -eq 'SecondTargetUnknown' -and $target -eq $secondTarget) {
            $instruction[0] = 0x06
            $text = '<unknown>'
        }
        Put-Bytes $pe (0x400 + $target - $sectionStart) $instruction
        $rows.Add(('{0:x}: {1} {2}' -f ($imageBase+$target), (Hex-Bytes $instruction), $text))
    }
    [UInt32]$firstEntry = if ($Fault -eq 'FirstTargetMiddle') { $firstTarget + 1 } else { $firstTarget }
    [UInt32]$secondEntry = if ($Fault -eq 'SecondTargetMiddle') { $secondTarget + 1 } else { $secondTarget }
    Set-U32 $pe (0x400 + $firstTable - $sectionStart) $firstEntry
    Set-U32 $pe (0x400 + $firstTable - $sectionStart + 4) $firstTarget
    Set-U32 $pe (0x400 + $secondTable - $sectionStart) $secondEntry
    Set-U32 $pe (0x400 + $secondTable - $sectionStart + 4) $firstTarget
    if ($Fault -eq 'Gap') {
        Put-Bytes $pe (0x400 + $firstTable - $sectionStart + 8) ([byte[]]@(0xaa,0xaa,0xaa,0xaa))
    }
    [UInt32]$vectorAddress = $firstTable + 5
    $vectorLength = 7
    if ($Fault -eq 'Overlong') { $vectorLength = 16 }
    if ($Fault -eq 'OutsideSuffix') {
        $vectorLength = 12
        $pe[0x400 + $firstTable - $sectionStart + 16] = 0x90
    }
    if ($Fault -eq 'StartsAtEnd') {
        $vectorAddress = $secondTable + 8
        $vectorLength = 4
        Put-Bytes $pe (0x400 + $vectorAddress - $sectionStart) ([byte[]]@(0xc5,0xf8,0x10,0x00))
    }
    $vectorRaw = [int](0x400 + $vectorAddress - $sectionStart)
    [byte[]]$vectorBytes = $pe[$vectorRaw..($vectorRaw+$vectorLength-1)]
    if ($Fault -eq 'ByteMismatch') { $vectorBytes[$vectorLength-1] = 1 }
    if ($Fault -eq '') {
        if ((Hex-Bytes $vectorBytes) -cne '62 59 00 09 5e 59 00') {
            throw 'Adjacent table control does not contain the literal observed EVEX-looking bytes.'
        }
    }
    $rows.Add(('{0:x}: {1} vdivps xmm11 {{k1}}, xmm15, xmmword ptr [r25]' -f ($imageBase+$vectorAddress), (Hex-Bytes $vectorBytes)))
    if ($RealVex) {
        [UInt32]$ordinaryVector = $secondTable + 8
        [byte[]]$ordinaryBytes = @(0xc5,0xf8,0x10,0x00)
        Put-Bytes $pe (0x400 + $ordinaryVector - $sectionStart) $ordinaryBytes
        $rows.Add(('{0:x}: {1} vmovups xmm0, xmmword ptr [rax]' -f ($imageBase+$ordinaryVector), (Hex-Bytes $ordinaryBytes)))
    }
    [IO.File]::WriteAllBytes($case.Binary, $pe)
    $metadata = "Age: $age`nGUID: {$guid}`nMod 0000 | ``SyntheticControl.obj``:`nObj: ``SyntheticControl.obj``:`n" +
                "Section Contributions`nSC[.text] | mod = 0, 0001:0, size = 12288, data crc = 0, reloc crc = 0`n"
    Set-Content -LiteralPath $case.Metadata -Value $metadata -Encoding utf8
    $symbols = "Mod 0000 | ``SyntheticControl.obj``:`n0 | S_LDATA32 [size = 16] `````n" +
               "type = <no type>, addr = 0001:$($firstTable-$sectionStart)`n" +
               "16 | S_LDATA32 [size = 16] `````n" +
               "type = <no type>, addr = 0001:$($secondTable-$sectionStart)`n"
    Set-Content -LiteralPath $case.Symbols -Value $symbols -Encoding utf8
    Set-Content -LiteralPath $case.Disassembly -Value $rows -Encoding utf8
    return $case
}

$adjacent = New-AdjacentTablesCase AdjacentProvedTables
Invoke-Control $adjacent $true 1 0
$adjacent = New-AdjacentTablesCase AdjacentProvedTablesWithRealVex -RealVex
Invoke-Control $adjacent $false 1 1
foreach ($fault in 'SecondDispatchBroken','SecondNoncontiguous','SecondTargetMiddle','SecondTargetUnknown',
                    'FirstTargetMiddle','Gap','ByteMismatch','Overlong','OutsideSuffix','StartsAtEnd') {
    $adjacent = New-AdjacentTablesCase "AdjacentTables$fault" -Fault $fault
    Invoke-Control $adjacent $false
}

Write-Host "Synthetic ISA parser fixtures: $fixtureRoot"
$global:LASTEXITCODE = 0

function New-GuardedTerminalByteRemapCase([string]$Name, [string]$Fault = '') {
    # Start from the existing PE/PDB fixture helper, then replace its code, symbols and listings with
    # the observed 25-byte stack guard and the exact RCX image-base remap sequence.
    $case = New-DataProofCase ($Name + 'Seed') 'TerminalValid'
    $pe = [IO.File]::ReadAllBytes($case.Binary)
    for ($i = 0x600; $i -lt $pe.Length; ++$i) { $pe[$i] = 0x90 }
    # The seed helper keeps its CodeView directory inside the synthetic .text raw range.
    Set-U32 $pe (0x400+0x400+12) 2
    Set-U32 $pe (0x400+0x400+16) 24
    Set-U32 $pe (0x400+0x400+20) (0x0010f420)
    Set-U32 $pe (0x400+0x400+24) 0x820
    Put-Bytes $pe 0x820 ([Text.Encoding]::ASCII.GetBytes('RSDS'))
    Put-Bytes $pe 0x824 $script:guid.ToByteArray()
    Set-U32 $pe 0x834 $script:age

    [UInt32]$section = 0x0010f000
    [UInt32]$dispatch = $section + 0x300
    [UInt32]$dwordTable = $section + 0x1000
    [int]$byteCount = 13
    [UInt32]$byteTable = $dwordTable + 4 * $byteCount
    [UInt32]$procedureEnd = $byteTable + $byteCount
    [UInt32]$rspDisp = 0x40
    [UInt32]$maxIndex = $byteCount - 1
    if ($Fault -eq 'RangeTooShort') { $maxIndex-- }
    if ($Fault -eq 'RangeTooLong') { $maxIndex++ }
    if ($Fault -eq 'RangeOverflow') { $maxIndex = [UInt32]::MaxValue }

    $rows = [Collections.Generic.List[string]]::new()
    $cursor = [UInt32]($dispatch - 25)
    [byte[]]$cmp = [byte[]]@(0x81,0xbc,0x24,0,0,0,0,0,0,0,0)
    Put-Bytes $cmp 3 ([BitConverter]::GetBytes($rspDisp))
    Put-Bytes $cmp 7 ([BitConverter]::GetBytes($maxIndex))
    if ($Fault -eq 'ChangedCmp') { $cmp[1] = 0xbd }
    Put-Bytes $pe (0x400+$cursor-$section) $cmp
    if ($Fault -notin @('MissingCmp')) {
        $cmpText = if ($Fault -eq 'UnknownCmp') { '<unknown>' } else { 'cmp dword ptr [rsp+40h], 0Ch' }
        [byte[]]$cmpRow = $cmp.Clone()
        if ($Fault -eq 'WrongLengthCmp') { $cmpRow = [byte[]]$cmpRow[0..9] }
        if ($Fault -eq 'ListingCmpDisplacementMismatch') { $cmpRow[3]++ }
        if ($Fault -eq 'ListingCmpMaximumMismatch') { $cmpRow[7]++ }
        $rows.Add(('{0:x}: {1} {2}' -f ($script:imageBase+$cursor), (Hex-Bytes $cmpRow), $cmpText))
    }
    if ($Fault -eq 'OverlappingCmp') {
        $rows.Add(('{0:x}: 90 nop' -f ($script:imageBase+$cursor+1)))
    }
    $cursor += 11

    [UInt32]$defaultTarget = if ($Fault -eq 'TargetOutsideProcedure') { $section + 0x2d0 } else { $section + 0x8c5 }
    if ($Fault -eq 'WrongDefaultTarget') { $defaultTarget = $section + 0x1800 }
    [UInt32]$jaRva = $cursor
    [byte[]]$ja = [byte[]]@(0x0f,0x87,0,0,0,0)
    if ($Fault -eq 'ChangedJa') { $ja[1] = 0x86 }
    Put-Bytes $ja 2 ([BitConverter]::GetBytes([Int32]($defaultTarget - ($jaRva + 6))))
    Put-Bytes $pe (0x400+$cursor-$section) $ja
    if ($Fault -notin @('MissingJa')) {
        $jaText = if ($Fault -eq 'UnknownJa') { '<unknown>' } else { 'ja default-target' }
        [byte[]]$jaRow = $ja.Clone()
        if ($Fault -eq 'WrongLengthJa') { $jaRow = [byte[]]$jaRow[0..4] }
        if ($Fault -eq 'ListingJaTargetMismatch') { $jaRow[2]++ }
        $rows.Add(('{0:x}: {1} {2}' -f ($script:imageBase+$cursor), (Hex-Bytes $jaRow), $jaText))
    }
    if ($Fault -eq 'OverlappingJa') { $rows.Add(('{0:x}: 90 nop' -f ($script:imageBase+$cursor+1))) }
    $cursor += 6

    [UInt32]$movDisp = $rspDisp
    if ($Fault -eq 'MismatchedStackDisplacement') { $movDisp++ }
    [byte[]]$movsxd = [byte[]]@(0x48,0x63,0x84,0x24,0,0,0,0)
    Put-Bytes $movsxd 4 ([BitConverter]::GetBytes($movDisp))
    if ($Fault -eq 'ChangedMovsxd') { $movsxd[1] = 0x62 }
    Put-Bytes $pe (0x400+$cursor-$section) $movsxd
    if ($Fault -notin @('MissingMovsxd')) {
        $movText = if ($Fault -eq 'UnknownMovsxd') { '<unknown>' } else { 'movsxd rax, dword ptr [rsp+40h]' }
        [byte[]]$movRow = if ($Fault -eq 'WrongLengthMovsxd') { [byte[]]$movsxd[0..6] } else { $movsxd }
        $rows.Add(('{0:x}: {1} {2}' -f ($script:imageBase+$cursor), (Hex-Bytes $movRow), $movText))
    }
    if ($Fault -eq 'OverlappingMovsxd') { $rows.Add(('{0:x}: 90 nop' -f ($script:imageBase+$cursor+1))) }
    $cursor += 8

    [byte[]]$lea = [byte[]]@(0x48,0x8d,0x0d,0,0,0,0)
    Put-Bytes $lea 3 ([BitConverter]::GetBytes([Int32]($script:imageBase-($script:imageBase+$cursor+7))))
    Put-Bytes $pe (0x400+$cursor-$section) $lea
    $rows.Add(('{0:x}: {1} lea rcx, [rip+imagebase]' -f ($script:imageBase+$cursor), (Hex-Bytes $lea)))
    $cursor += 7
    [byte[]]$byteLoad = [byte[]]@(0x0f,0xb6,0x84,0x01,0,0,0,0)
    Put-Bytes $byteLoad 4 ([BitConverter]::GetBytes($byteTable))
    Put-Bytes $pe (0x400+$cursor-$section) $byteLoad
    $rows.Add(('{0:x}: {1} movzx eax, byte ptr [rcx+rax+byte-table]' -f ($script:imageBase+$cursor), (Hex-Bytes $byteLoad)))
    $cursor += 8
    [byte[]]$dwordLoad = [byte[]]@(0x8b,0x84,0x81,0,0,0,0)
    Put-Bytes $dwordLoad 3 ([BitConverter]::GetBytes($dwordTable))
    Put-Bytes $pe (0x400+$cursor-$section) $dwordLoad
    $rows.Add(('{0:x}: {1} mov eax, dword ptr [rcx+4*rax+dword-table]' -f ($script:imageBase+$cursor), (Hex-Bytes $dwordLoad)))
    $cursor += 7
    [byte[]]$add = [byte[]]@(0x48,0x03,0xc1)
    Put-Bytes $pe (0x400+$cursor-$section) $add
    $rows.Add(('{0:x}: {1} add rax, rcx' -f ($script:imageBase+$cursor), (Hex-Bytes $add)))
    $cursor += 3
    [byte[]]$jump = [byte[]]@(0xff,0xe0)
    Put-Bytes $pe (0x400+$cursor-$section) $jump
    $rows.Add(('{0:x}: {1} jmp rax' -f ($script:imageBase+$cursor), (Hex-Bytes $jump)))

    [byte[]]$indices = [byte[]]::new($byteCount)
    for ($i = 0; $i -lt $byteCount; ++$i) {
        [UInt32]$target = if ($i -eq 0 -and $Fault -eq 'TargetOutsideProcedure') { $section + 0x2d0 } elseif ($i -eq 0 -or ($i -eq 1 -and $Fault -eq 'TargetOutsideProcedure')) { $section + 0x8c5 } else { $section + 0x500 + 2*$i }
        Set-U32 $pe (0x400+$dwordTable-$section+4*$i) $target
        $indices[$i] = [byte]$i
        if (($i -ne 0 -or $Fault -eq 'TargetOutsideProcedure') -and
            -not ($i -eq 1 -and $Fault -eq 'TargetOutsideProcedure')) {
            Put-Bytes $pe (0x400+$target-$section) ([byte[]]@(0x66,0x90))
            $rows.Add(('{0:x}: 66 90 nop' -f ($script:imageBase+$target)))
        }
    }
    Put-Bytes $pe (0x400+$section+0x8c5-$section) ([byte[]]@(0x90))
    $rows.Add(('{0:x}: 90 nop' -f ($script:imageBase+$section+0x8c5)))
    if ($Fault -eq 'MalformedByte') { $indices[7] = [byte]$byteCount }
    Put-Bytes $pe (0x400+$byteTable-$section) $indices
    for ($i = 0; $i -lt $byteCount; ++$i) {
        $tableBytes = [byte[]]$pe[(0x400+$dwordTable-$section+4*$i)..(0x400+$dwordTable-$section+4*$i+3)]
        $rows.Add(('{0:x}: {1} vmovups xmm0, xmmword ptr [rax]' -f ($script:imageBase+$dwordTable+4*$i), (Hex-Bytes $tableBytes)))
    }

    $procBegin = [UInt32]$section
    $procEnd = [UInt32]$procedureEnd
    if ($Fault -eq 'ShortProcedure') { $procEnd-- }
    if ($Fault -eq 'LongProcedure') { $procEnd++ }
    if ($Fault -eq 'GuardOutsideProcedure') { $procBegin = $dispatch - 24 }
    if ($Fault -eq 'TargetOutsideProcedure') { $procBegin = $section + 0x2e0 }
    $procedureModule = if ($Fault -eq 'WrongProcedureOwner') { 1 } else { 0 }
    $symbols = "  Mod 0 | ``Synthetic.obj``:`n"
    if ($Fault -ne 'MissingProcedure') {
        $symbols += "  Mod $procedureModule | ``Synthetic.obj``:`n  0 | S_GPROC32 [size = 16] ``GuardedDispatch`` `n  addr = 0001:$($procBegin-$section), code size = $($procEnd-$procBegin)`n"
    }
    if ($Fault -eq 'TargetOutsideProcedure') {
        # This longer overlapping procedure lets the DWORD-table proof resolve its backward
        # target; the exact terminal procedure still excludes that decoded target.
        $shadowBegin = [UInt32]($section + 0x200)
        $shadowEnd = [UInt32]($procEnd + 0x20)
        $symbols += "  Mod 0 | ``Synthetic.obj``:`n  0 | S_GPROC32 [size = 16] ``ShadowProc`` `n  addr = 0001:$($shadowBegin-$section), code size = $($shadowEnd-$shadowBegin)`n"
    }
    $symbols += "  Mod 0 | ``Synthetic.obj``:`n  0 | S_LDATA32 [size = 16] `````n  type = <no type>, addr = 0001:$($dwordTable-$section)`n"
    $symbols += "  Mod 0 | ``Synthetic.obj``:`n  0 | S_LDATA32 [size = 16] `````n  type = <no type>, addr = 0001:$($byteTable-$section)`n"
    if ($Fault -eq 'EarlyDataSymbol') {
        $symbols += "  Mod 0 | ``Synthetic.obj``:`n  0 | S_LDATA32 [size = 16] `````n  type = <no type>, addr = 0001:$($byteTable-$section+5)`n"
    }
    if ($Fault -eq 'LaterProcedureDataSymbol') {
        $symbols += "  Mod 0 | ``Synthetic.obj``:`n  0 | S_GPROC32 [size = 16] ``LaterProc`` `n  addr = 0001:$($procEnd-$section), code size = 128`n"
        $symbols += "  Mod 0 | ``Synthetic.obj``:`n  0 | S_LDATA32 [size = 16] `````n  type = <no type>, addr = 0001:$($procEnd-$section+32)`n"
    }
    $contributionSize = [UInt32]0x2000
    $metadata = "  Age: $script:age`n  GUID: {$script:guid}`n  Mod 0 | ``Synthetic.obj``:`n  Obj: ``Synthetic.obj``:`n  Mod 1 | ``Other.obj``:`n  Obj: ``Other.obj``:`n`nSection Contributions`n  SC[.text] | mod = 0, 0001:0, size = $contributionSize, data crc = 0, reloc crc = 0`n"
    if ($Fault -eq 'VexAfterProcedure') {
        [byte[]]$vex = [byte[]]@(0xc5,0xf8,0x10,0x00)
        Put-Bytes $pe (0x400+$procEnd-$section+4) $vex
        $rows.Add(('{0:x}: {1} vmovups xmm0, xmmword ptr [rax]' -f ($script:imageBase+$procEnd+4), (Hex-Bytes $vex)))
    }
    Set-Content -LiteralPath $case.Metadata -Value $metadata -Encoding utf8
    Set-Content -LiteralPath $case.Symbols -Value $symbols -Encoding utf8
    Set-Content -LiteralPath $case.Disassembly -Value (@($rows | Sort-Object {
        [UInt64]::Parse(($_ -split ':',2)[0], [Globalization.NumberStyles]::HexNumber)
    }) -join "`n") -Encoding utf8
    [IO.File]::WriteAllBytes($case.Binary, $pe)
    $case.Name = $Name
    return $case
}

$guardedProofCount = 0
foreach ($name in 'LargerContribution','LaterProcedureDataSymbol') {
    $fault = if ($name -eq 'LaterProcedureDataSymbol') { $name } else { '' }
    $case = New-GuardedTerminalByteRemapCase $name $fault
    Invoke-Control $case $true 1 0
    ++$guardedProofCount
}
foreach ($fault in 'ChangedCmp','MissingCmp','UnknownCmp','OverlappingCmp','WrongLengthCmp',
    'ListingCmpDisplacementMismatch','ListingCmpMaximumMismatch',
    'ChangedJa','MissingJa','UnknownJa','OverlappingJa','WrongLengthJa','ListingJaTargetMismatch',
    'ChangedMovsxd','MissingMovsxd','UnknownMovsxd','OverlappingMovsxd','WrongLengthMovsxd',
    'MismatchedStackDisplacement','RangeTooShort','RangeTooLong','RangeOverflow',
    'MissingProcedure','ShortProcedure','LongProcedure','WrongProcedureOwner','WrongDefaultTarget',
    'GuardOutsideProcedure','TargetOutsideProcedure','MalformedByte','EarlyDataSymbol','VexAfterProcedure') {
    $case = New-GuardedTerminalByteRemapCase ("Guarded$fault") $fault
    Invoke-Control $case $false
    ++$guardedProofCount
}
Write-Host "Guarded terminal byte-remap synthetic controls passed: $guardedProofCount"

function New-GuardedTerminalDwordCase([string]$Name, [string]$Fault = '') {
    $case = New-SyntheticCase ($Name + 'Seed') -PdbData
    $pe = [IO.File]::ReadAllBytes($case.Binary)
    [UInt32]$section = $script:sectionRva
    [UInt32]$dispatch = $section + 0x890
    [UInt32]$table = $section + 0x900
    [UInt32]$target = $section + 0x8c5
    [UInt32]$end = $table + 24
    [UInt32]$prefix = $dispatch - 12
    # Actual observed short guard, independent numeric register/opcode encoding.
    [byte[]]$guard = @(0x83,0x7c,0x24,0x24,0x05,0x77,0,0x48,0x63,0x44,0x24,0x24)
    $guard[6] = [byte]($target - ($prefix + 7))
    switch ($Fault) {
        'WrongCmp' { $guard[1] = 0x7d }
        'WrongJa' { $guard[5] = 0x76 }
        'WrongMovsxd' { $guard[8] = 0x62 }
        'DifferentStackSlot' { $guard[11]++ }
        'ShortRange' { $guard[4]-- }
        'LongRange' { $guard[4]++ }
        'SignedRange' { $guard[4] = 0xff }
        'WrongDefault' { $guard[6]++ }
    }
    [byte[]]$lea = @(0x48,0x8d,0x0d,0,0,0,0)
    Put-Bytes $lea 3 ([BitConverter]::GetBytes([Int32](-[Int64]($dispatch+7))))
    [byte[]]$load = @(0x8b,0x84,0x81,0,0,0,0)
    Put-Bytes $load 3 ([BitConverter]::GetBytes($table))
    [byte[]]$add = @(0x48,0x03,0xc1)
    [byte[]]$jump = @(0xff,0xe0)
    switch ($Fault) {
        'WrongLoad' { $load[2] = 0x82 }
        'WrongAdd' { $add[2] = 0xc2 }
        'WrongJump' { $jump[1] = 0xe1 }
    }
    Put-Bytes $pe (0x400+$prefix-$section) $guard
    Put-Bytes $pe (0x400+$dispatch-$section) $lea
    Put-Bytes $pe (0x400+$dispatch+7-$section) $load
    Put-Bytes $pe (0x400+$dispatch+14-$section) $add
    Put-Bytes $pe (0x400+$dispatch+17-$section) $jump
    $pe[0x400+$table-$section-2] = 0xc3
    $pe[0x400+$table-$section-1] = 0x90
    $rows = [Collections.Generic.List[string]]::new()
    $starts = @(0,5,7)
    $lengths = @(5,2,5)
    for ($i=0; $i -lt 3; ++$i) {
        if ($Fault -eq "MissingGuard$i") { continue }
        $bytes = [byte[]]$guard[$starts[$i]..($starts[$i]+$lengths[$i]-1)]
        if ($Fault -eq "ListingGuard$i") { $bytes[$bytes.Length-1] = $bytes[$bytes.Length-1] -bxor 1 }
        $text = if ($Fault -eq "UnknownGuard$i") { '<unknown>' } else { 'guard' }
        $rows.Add(('{0:x}: {1} {2}' -f ($script:imageBase+$prefix+$starts[$i]), (Hex-Bytes $bytes), $text))
    }
    if ($Fault -eq 'OverlappingGuard') { $rows.Add(('{0:x}: 90 nop' -f ($script:imageBase+$prefix+1))) }
    $offsets = @(0,7,14,17)
    $instructions = @($lea,$load,$add,$jump)
    for ($i=0; $i -lt 4; ++$i) {
        [byte[]]$bytes = $instructions[$i].Clone()
        if ($Fault -eq "ListingDispatch$i") { $bytes[$bytes.Length-1] = $bytes[$bytes.Length-1] -bxor 1 }
        $rows.Add(('{0:x}: {1} dispatch' -f ($script:imageBase+$dispatch+$offsets[$i]), (Hex-Bytes $bytes)))
    }
    $pe[0x400+$target-$section] = 0x90
    $rows.Add(('{0:x}: 90 nop' -f ($script:imageBase+$target)))
    for ($i=0; $i -lt 6; ++$i) {
        [UInt32]$entry = $target
        if ($i -eq 5 -and $Fault -eq 'BadLastTarget') { $entry = $target+1 }
        if ($i -eq 5 -and $Fault -eq 'OutsideTarget') { $entry = $prefix-1 }
        Set-U32 $pe (0x400+$table-$section+4*$i) $entry
        $bytes = [byte[]]$pe[(0x400+$table-$section+4*$i)..(0x400+$table-$section+4*$i+3)]
        $rows.Add(('{0:x}: {1} vmovups xmm0, xmmword ptr [rax]' -f ($script:imageBase+$table+4*$i), (Hex-Bytes $bytes)))
    }
    $procEnd = $end
    if ($Fault -eq 'ShortProcedure') { $procEnd -= 4 }
    if ($Fault -eq 'LongProcedure') { $procEnd += 4 }
    $procOwner = if ($Fault -eq 'WrongProcedureOwner') { 1 } else { 0 }
    $sym = "  Mod $procOwner | ``SyntheticControl.obj``:`n"
    if ($Fault -ne 'MissingProcedure') {
        $sym += "  0 | S_GPROC32 [size = 16] ``GuardedDword`` `n  addr = 0001:$($prefix-$section), code size = $($procEnd-$prefix)`n"
    }
    $dataOwner = if ($Fault -eq 'WrongDataOwner') { 1 } else { 0 }
    $sym += "  Mod $dataOwner | ``SyntheticControl.obj``:`n"
    if ($Fault -ne 'MissingData') {
        $data = if ($Fault -eq 'WrongDataStart') { $table+4 } else { $table }
        $sym += "  0 | S_LDATA32 [size = 16] `````n  type = <no type>, addr = 0001:$($data-$section)`n"
    }
    $sym += "  Mod 0 | ``SyntheticControl.obj``:`n  0 | S_LDATA32 [size = 16] `````n  type = <no type>, addr = 0001:4096`n"
    if ($Fault -eq 'EarlyData') {
        $sym += "  0 | S_LDATA32 [size = 16] `````n  type = <no type>, addr = 0001:$($table-$section+8)`n"
    }
    if ($Fault -eq 'RealVectorAfter') {
        [byte[]]$vex = @(0xc5,0xf8,0x10,0x00)
        Put-Bytes $pe (0x400+$end-$section) $vex
        $rows.Add(('{0:x}: {1} vmovups xmm0, xmmword ptr [rax]' -f ($script:imageBase+$end), (Hex-Bytes $vex)))
    }
    Set-Content -LiteralPath $case.Symbols -Value $sym -Encoding utf8
    Set-Content -LiteralPath $case.Disassembly -Value (@($rows | Sort-Object { [UInt64]::Parse(($_ -split ':',2)[0], [Globalization.NumberStyles]::HexNumber) }) -join "`n") -Encoding utf8
    [IO.File]::WriteAllBytes($case.Binary,$pe)
    $case.Name = $Name
    return $case
}
$terminalDwordCount = 0
Invoke-Control (New-GuardedTerminalDwordCase 'TerminalDwordValid') $true 6 0
++$terminalDwordCount
Invoke-Control (New-GuardedTerminalDwordCase 'TerminalDwordRealVectorAfter' 'RealVectorAfter') $false 6 1
++$terminalDwordCount
foreach ($fault in 'WrongCmp','WrongJa','WrongMovsxd','DifferentStackSlot','ShortRange','LongRange','SignedRange','WrongDefault',
    'WrongLoad','WrongAdd','WrongJump','MissingGuard0','MissingGuard1','MissingGuard2','ListingGuard0','ListingGuard1','ListingGuard2',
    'UnknownGuard0','UnknownGuard1','UnknownGuard2','OverlappingGuard','ListingDispatch0','ListingDispatch1','ListingDispatch2','ListingDispatch3',
    'BadLastTarget','OutsideTarget','ShortProcedure','LongProcedure','WrongProcedureOwner','MissingProcedure','WrongDataOwner','MissingData','WrongDataStart','EarlyData') {
    Invoke-Control (New-GuardedTerminalDwordCase ("TerminalDword$fault") $fault) $false
    ++$terminalDwordCount
}
Write-Host "Guarded terminal DWORD synthetic controls passed: $terminalDwordCount"

function New-GuardedTerminalDwordLongJaCase([string]$Name, [string]$Fault = '', [int]$Maximum = 10) {
    $case = New-SyntheticCase ($Name + 'Seed') -PdbData
    $pe = [IO.File]::ReadAllBytes($case.Binary)
    [UInt32]$section = $script:sectionRva
    [UInt32]$dispatch = $section + 0x890
    [UInt32]$prefix = $dispatch - 16
    [UInt32]$table = $section + 0x900
    [UInt32]$target = $section + 0x8c5
    [UInt32]$end = $table + 4 * ($Maximum + 1)

    # Observed unsigned terminal guard: CMP [RSP+disp8],imm8; JA rel32; MOVSXD RAX,[RSP+same disp8].
    [byte[]]$guard = @(0x83,0x7c,0x24,0x24,0x0a,0x0f,0x87,0,0,0,0,0x48,0x63,0x44,0x24,0x24)
    Set-U32 $guard 7 ([UInt32]([Int32]($target - ($prefix + 11))))
    $guard[4] = [byte]$Maximum
    switch ($Fault) {
        'WrongCmp' { $guard[1] = 0x7d }
        'WrongJa' { $guard[6] = 0x86 }
        'WrongJaCondition' { $guard[5] = 0x0e }
        'WrongMovsxd' { $guard[12] = 0x62 }
        'WrongMovsxdRegister' { $guard[11] = 0x49 }
        'DifferentStackSlot' { $guard[15]++ }
        'WrongCmpStackSlot' { $guard[3]++ }
        'ZeroRange' { $guard[4] = 0 }
        'Range128' { $guard[4] = 128 }
        'Range255' { $guard[4] = 255 }
        'WrongDefault' { $guard[7]++ }
    }

    [byte[]]$lea = @(0x48,0x8d,0x0d,0,0,0,0)
    Put-Bytes $lea 3 ([BitConverter]::GetBytes([Int32](-[Int64]($dispatch+7))))
    [byte[]]$load = @(0x8b,0x84,0x81,0,0,0,0)
    Put-Bytes $load 3 ([BitConverter]::GetBytes($table))
    [byte[]]$add = @(0x48,0x03,0xc1)
    [byte[]]$jump = @(0xff,0xe0)
    switch ($Fault) {
        'WrongImageBase' { $lea[3]++ }
        'WrongLoad' { $load[2] = 0x82 }
        'WrongLoadIndex' { $load[2] = 0x91 }
        'WrongAdd' { $add[2] = 0xc2 }
        'WrongJump' { $jump[1] = 0xe1 }
    }
    Put-Bytes $pe (0x400+$prefix-$section) $guard
    Put-Bytes $pe (0x400+$dispatch-$section) $lea
    Put-Bytes $pe (0x400+$dispatch+7-$section) $load
    Put-Bytes $pe (0x400+$dispatch+14-$section) $add
    Put-Bytes $pe (0x400+$dispatch+17-$section) $jump
    $pe[0x400+$table-$section-2] = 0xc3
    $pe[0x400+$table-$section-1] = 0x90

    $rows = [Collections.Generic.List[string]]::new()
    $guardStarts = @(0,5,11)
    $guardLengths = @(5,6,5)
    for ($i=0; $i -lt 3; ++$i) {
        if ($Fault -eq "MissingGuard$i") { continue }
        $bytes = [byte[]]$guard[$guardStarts[$i]..($guardStarts[$i]+$guardLengths[$i]-1)]
        if ($Fault -eq "ListingGuard$i") { $bytes[$bytes.Length-1] = $bytes[$bytes.Length-1] -bxor 1 }
        $text = if ($Fault -eq "UnknownGuard$i") { '<unknown>' } else { 'guard' }
        $rows.Add(('{0:x}: {1} {2}' -f ($script:imageBase+$prefix+$guardStarts[$i]), (Hex-Bytes $bytes), $text))
    }
    if ($Fault -eq 'OverlappingGuard') { $rows.Add(('{0:x}: 90 nop' -f ($script:imageBase+$prefix+1))) }
    if ($Fault -eq 'OverlappingDispatch') { $rows.Add(('{0:x}: 90 nop' -f ($script:imageBase+$dispatch+1))) }
    $dispatchOffsets = @(0,7,14,17)
    $instructions = @($lea,$load,$add,$jump)
    for ($i=0; $i -lt 4; ++$i) {
        [byte[]]$bytes = $instructions[$i].Clone()
        if ($Fault -eq "ListingDispatch$i") { $bytes[$bytes.Length-1] = $bytes[$bytes.Length-1] -bxor 1 }
        $rows.Add(('{0:x}: {1} dispatch' -f ($script:imageBase+$dispatch+$dispatchOffsets[$i]), (Hex-Bytes $bytes)))
    }
    $pe[0x400+$target-$section] = 0x90
    $rows.Add(('{0:x}: 90 nop' -f ($script:imageBase+$target)))
    if ($Fault -eq 'SeparateDefault') {
        # Keep a VEX-looking low RVA byte (C4) so refusing this proof produces an ISA violation.
        $pe[0x400+($target-1)-$section] = 0x90
        $rows.Add(('{0:x}: 90 nop' -f ($script:imageBase+$target-1)))
    }
    for ($i=0; $i -le $Maximum; ++$i) {
        [UInt32]$entry = if ($Fault -eq 'SeparateDefault') { $target-1 } else { $target }
        if ($i -eq $Maximum -and $Fault -eq 'BadLastTarget') { $entry = $target+1 }
        if ($i -eq $Maximum -and $Fault -eq 'OutsideTarget') { $entry = $prefix-1 }
        Set-U32 $pe (0x400+$table-$section+4*$i) $entry
        $bytes = [byte[]]$pe[(0x400+$table-$section+4*$i)..(0x400+$table-$section+4*$i+3)]
        $rows.Add(('{0:x}: {1} vmovups xmm0, xmmword ptr [rax]' -f ($script:imageBase+$table+4*$i), (Hex-Bytes $bytes)))
    }
    if ($Fault -eq 'MiddleTarget') {
        Set-U32 $pe (0x400+$table-$section) ($target+1)
        for ($rowIndex=0; $rowIndex -lt $rows.Count; ++$rowIndex) {
            if ($rows[$rowIndex].StartsWith(('{0:x}:' -f ($script:imageBase+$table)), [StringComparison]::Ordinal)) {
                $bytes = [byte[]]$pe[(0x400+$table-$section)..(0x400+$table-$section+3)]
                $rows[$rowIndex] = '{0:x}: {1} vmovups xmm0, xmmword ptr [rax]' -f ($script:imageBase+$table), (Hex-Bytes $bytes)
                break
            }
        }
    }

    $procEnd = $end
    if ($Fault -eq 'ShortProcedure') { $procEnd -= 4 }
    if ($Fault -eq 'LongProcedure' -or $Fault -eq 'LaterUnrelatedData') { $procEnd += 4 }
    $procOwner = if ($Fault -eq 'WrongProcedureOwner') { 1 } else { 0 }
    $sym = "  Mod $procOwner | ``SyntheticControl.obj``:`n"
    if ($Fault -ne 'MissingProcedure') {
        $sym += "  0 | S_GPROC32 [size = 16] ``GuardedDwordLongJa`` `n  addr = 0001:$($prefix-$section), code size = $($procEnd-$prefix)`n"
    }
    $dataOwner = if ($Fault -eq 'WrongDataOwner') { 1 } else { 0 }
    $sym += "  Mod $dataOwner | ``SyntheticControl.obj``:`n"
    if ($Fault -ne 'MissingData') {
        $data = if ($Fault -eq 'WrongDataStart') { $table+4 } else { $table }
        $sym += "  0 | S_LDATA32 [size = 16] `````n  type = <no type>, addr = 0001:$($data-$section)`n"
    }
    if ($Fault -eq 'EarlyDataSymbol') {
        $sym += "  0 | S_LDATA32 [size = 16] `````n  type = <no type>, addr = 0001:$($table-$section+8)`n"
    }
    # A later unrelated data symbol prevents the generic unguarded-table recognizer from proving this
    # fixture. Every control must exercise the terminal guard proof, including adjacent real VEX code.
    $sym += "  Mod 0 | ``SyntheticControl.obj``:`n  0 | S_LDATA32 [size = 16] `````n  type = <no type>, addr = 0001:4096`n"
    # This closer same-module symbol cannot justify lengthening the terminal procedure's table.
    if ($Fault -eq 'LaterUnrelatedData') {
        $sym += "  0 | S_LDATA32 [size = 16] `````n  type = <no type>, addr = 0001:$($end-$section+4)`n"
    }
    if ($Fault -eq 'AdjacentVex') {
        [byte[]]$vex = @(0xc5,0xf8,0x10,0x00)
        Put-Bytes $pe (0x400+$end-$section) $vex
        $rows.Add(('{0:x}: {1} vmovups xmm0, xmmword ptr [rax]' -f ($script:imageBase+$end), (Hex-Bytes $vex)))
    }
    Set-Content -LiteralPath $case.Symbols -Value $sym -Encoding utf8
    Set-Content -LiteralPath $case.Disassembly -Value (@($rows | Sort-Object { [UInt64]::Parse(($_ -split ':',2)[0], [Globalization.NumberStyles]::HexNumber) }) -join "`n") -Encoding utf8
    [IO.File]::WriteAllBytes($case.Binary,$pe)
    $case.Name = $Name
    return $case
}
$terminalDwordLongJaCount = 0
Invoke-Control (New-GuardedTerminalDwordLongJaCase 'TerminalDwordLongJaSeparateDefault' 'SeparateDefault' 10) $true 11 0
++$terminalDwordLongJaCount
foreach ($maximum in 1,10,127) {
    $expectedRows = $maximum + 1
    Invoke-Control (New-GuardedTerminalDwordLongJaCase "TerminalDwordLongJaMaximum$maximum" '' $maximum) $true $expectedRows 0
    ++$terminalDwordLongJaCount
}
Invoke-Control (New-GuardedTerminalDwordLongJaCase 'TerminalDwordLongJaAdjacentVex' 'AdjacentVex' 10) $false
++ $terminalDwordLongJaCount
foreach ($fault in 'WrongCmp','WrongJa','WrongJaCondition','WrongMovsxd','WrongMovsxdRegister','DifferentStackSlot','WrongCmpStackSlot',
    'ZeroRange','Range128','Range255','WrongDefault','WrongImageBase','WrongLoad','WrongLoadIndex','WrongAdd','WrongJump',
    'MissingGuard0','MissingGuard1','MissingGuard2','ListingGuard0','ListingGuard1','ListingGuard2','UnknownGuard0','UnknownGuard1','UnknownGuard2',
    'OverlappingGuard','OverlappingDispatch','ListingDispatch0','ListingDispatch1','ListingDispatch2','ListingDispatch3','BadLastTarget','OutsideTarget','MiddleTarget',
    'ShortProcedure','LongProcedure','WrongProcedureOwner','MissingProcedure','WrongDataOwner','MissingData','WrongDataStart','EarlyDataSymbol',
    'LaterUnrelatedData') {
    Invoke-Control (New-GuardedTerminalDwordLongJaCase ("TerminalDwordLongJa$fault") $fault 10) $false
    ++$terminalDwordLongJaCount
}
Write-Host "Guarded terminal DWORD long-JA synthetic controls passed: $terminalDwordLongJaCount"

# Bounded independent control for MOVZX ECX, byte [RAX]; CMP CL,imm8; JA rel8;
# LEA RDX,imagebase; MOV EAX,[RDX+4*RCX+tableRva]; ADD RAX,RDX; JMP RAX.
# Keep instruction bytes as numeric literals: these controls qualify parser evidence,
# including when a disassembly mnemonic is deliberately generic or misleading.
function New-MovzxGuardedTerminalDwordCase {
    param(
        [Parameter(Mandatory)] [string]$Name,
        [string]$Fault = '',
        [ValidateRange(1,127)] [int]$Maximum = 5
    )
    $case = New-SyntheticCase ($Name + 'Seed') -PdbData
    $pe = [IO.File]::ReadAllBytes($case.Binary)
    [UInt32]$section = [UInt32]0x0010f000
    [UInt32]$prefix = $section + 0x870
    [UInt32]$dispatch = $prefix + 8
    [UInt32]$table = $section + 0x8e8
    [int]$entryCount = $Maximum + 1
    [UInt32]$end = $table + 4 * $entryCount
    [UInt32]$default = $section + 0x8c4
    [UInt32]$target = $section + 0x8c5
    [UInt32]$defaultBranchTarget = $default

    # Three independently encoded prefix instructions occupy exactly eight bytes.
    [byte[]]$movzx = @(0x0f,0xb6,0x08)
    [byte[]]$cmp = @(0x80,0xf9,0x05)
    $cmp[2] = [byte]$Maximum
    [byte[]]$ja = @(0x77,0x18)
    [byte[]]$lea = @(0x48,0x8d,0x15,0,0,0,0)
    [byte[]]$load = @(0x8b,0x84,0x8a,0,0,0,0)
    [byte[]]$add = @(0x48,0x03,0xc2)
    [byte[]]$jump = @(0xff,0xe0)

    switch ($Fault) {
        'WrongMovzxDestination' { $movzx[2] = 0x00 }
        'WrongMovzxSource' { $movzx[2] = 0x0a }
        'SignedMovsx' { $movzx[1] = 0xbe }
        'WrongCmpRegister' { $cmp[1] = 0xf8 }
        'RangeTooSmall' { $cmp[2] = [byte]($Maximum - 1) }
        'RangeTooLarge' { $cmp[2] = [byte]($Maximum + 1) }
        'WrongJa' { $ja[0] = 0x76 }
        'DefaultMidInstruction' { $defaultBranchTarget = $prefix + 1 }
        'DefaultOutsideProcedure' { $defaultBranchTarget = $prefix - 0x10 }
        'DefaultInsideTable' { $defaultBranchTarget = $table + 4 }
        'WrongImageBase' { }
        'WrongLeaOpcode' { $lea[1] = 0x8c }
        'WrongLeaDestination' { $lea[2] = 0x0d }
        'WrongLoadOpcode' { $load[0] = 0x89 }
        'WrongLoadDestination' { $load[1] = 0x8c }
        'WrongLoadScale' { $load[2] = 0x4a }
        'WrongLoadIndex' { $load[2] = 0x82 }
        'WrongLoadBase' { $load[2] = 0x89 }
        'WrongLoadAddress' { }
        'WrongAddOpcode' { $add[1] = 0x01 }
        'WrongAddRegisters' { $add[2] = 0xc1 }
        'WrongJumpOpcode' { $jump[0] = 0xff; $jump[1] = 0xe1 }
    }
    [Int64]$branchDisplacement = [Int64]$defaultBranchTarget - [Int64]($prefix + 8)
    $ja[1] = [byte]($branchDisplacement -band 0xff)

    [Int64]$imageBase = [Int64]0x140000000
    [Int32]$leaDisplacement = [Int32](-[Int64]($dispatch + 7))
    if ($Fault -eq 'WrongImageBase') { ++$leaDisplacement }
    Put-Bytes $lea 3 ([BitConverter]::GetBytes($leaDisplacement))
    [UInt32]$loadDisplacement = $table
    if ($Fault -eq 'WrongLoadAddress') { $loadDisplacement += 4 }
    Put-Bytes $load 3 ([BitConverter]::GetBytes($loadDisplacement))

    Put-Bytes $pe (0x400 + $prefix - $section) $movzx
    Put-Bytes $pe (0x400 + $prefix + 3 - $section) $cmp
    Put-Bytes $pe (0x400 + $prefix + 6 - $section) $ja
    Put-Bytes $pe (0x400 + $dispatch - $section) $lea
    Put-Bytes $pe (0x400 + $dispatch + 7 - $section) $load
    Put-Bytes $pe (0x400 + $dispatch + 14 - $section) $add
    Put-Bytes $pe (0x400 + $dispatch + 17 - $section) $jump
    # Both valid destinations are real PE instruction boundaries before the table.
    $pe[0x400 + $default - $section] = 0x90
    [byte[]]$targetNop = @(0x0f,0x1f,0x00)
    Put-Bytes $pe (0x400 + $target - $section) $targetNop
    # The bytes immediately before the anonymous table are data-adjacent sentinels.
    $pe[0x400 + $table - $section - 2] = 0xc3
    $pe[0x400 + $table - $section - 1] = 0x90

    $rows = [Collections.Generic.List[string]]::new()
    $prefixBytes = @($movzx,$cmp,$ja)
    $prefixOffsets = @(0,3,6)
    $prefixStarts = @('Prefix0','Prefix1','Prefix2')
    for ($i=0; $i -lt 3; ++$i) {
        if ($Fault -eq ('Missing' + $prefixStarts[$i])) { continue }
        [byte[]]$shown = $prefixBytes[$i].Clone()
        if ($Fault -eq ('ListingMismatch' + $prefixStarts[$i])) { $shown[0] = $shown[0] -bxor 1 }
        $mnemonic = if ($Fault -eq ('Unknown' + $prefixStarts[$i])) { '<unknown>' } else { 'control' }
        $rows.Add(('{0:x}: {1} {2}' -f ($imageBase+$prefix+$prefixOffsets[$i]),(Hex-Bytes $shown),$mnemonic))
        if ($Fault -eq ('Overlap' + $prefixStarts[$i])) {
            $rows.Add(('{0:x}: 90 overlap' -f ($imageBase+$prefix+$prefixOffsets[$i]+1)))
        }
    }
    $dispatchBytes = @($lea,$load,$add,$jump)
    $dispatchOffsets = @(0,7,14,17)
    $dispatchNames = @('Dispatch0','Dispatch1','Dispatch2','Dispatch3')
    for ($i=0; $i -lt 4; ++$i) {
        if ($Fault -eq ('Missing' + $dispatchNames[$i])) { continue }
        [byte[]]$shown = $dispatchBytes[$i].Clone()
        if ($Fault -eq ('ListingMismatch' + $dispatchNames[$i])) { $shown[$shown.Length-1] = $shown[$shown.Length-1] -bxor 1 }
        $mnemonic = if ($Fault -eq ('Unknown' + $dispatchNames[$i])) { '<unknown>' } else { 'control' }
        $rows.Add(('{0:x}: {1} {2}' -f ($imageBase+$dispatch+$dispatchOffsets[$i]),(Hex-Bytes $shown),$mnemonic))
        if ($Fault -eq ('Overlap' + $dispatchNames[$i])) {
            $rows.Add(('{0:x}: 90 overlap' -f ($imageBase+$dispatch+$dispatchOffsets[$i]+1)))
        }
    }
    $rows.Add(('{0:x}: 90 destination' -f ($imageBase+$default)))
    if ($target -ne $default) { $rows.Add(('{0:x}: {1} destination' -f ($imageBase+$target),(Hex-Bytes $targetNop))) }

    [UInt32]$badEntry = $target
    if ($Fault -eq 'BadLastTarget') { $badEntry = $target + 1 }
    if ($Fault -eq 'OutsideTarget') { $badEntry = $prefix - 1 }
    if ($Fault -eq 'MidTableTarget') { $badEntry = $table + 4 }
    for ($i=0; $i -lt $entryCount; ++$i) {
        [UInt32]$entry = if ($i -eq ($entryCount-1) -and $Fault -in @('BadLastTarget','OutsideTarget','MidTableTarget')) { $badEntry } else { $target }
        if ($Fault -eq 'DefaultInTable') { $entry = $default }
        Set-U32 $pe (0x400 + $table - $section + 4*$i) $entry
        [byte[]]$dataBytes = $pe[(0x400+$table-$section+4*$i)..(0x400+$table-$section+4*$i+3)]
        $rows.Add(('{0:x}: {1} table-data' -f ($imageBase+$table+4*$i),(Hex-Bytes $dataBytes)))
    }

    $procEnd = $end
    if ($Fault -eq 'ProcedureEndsEarly') { $procEnd -= 4 }
    if ($Fault -eq 'ProcedureEndsLate') { $procEnd += 4 }
    $procOwner = if ($Fault -eq 'WrongProcedureOwner') { 1 } else { 0 }
    $dataOwner = if ($Fault -eq 'WrongDataOwner') { 1 } else { 0 }
    $symbols = "  Mod $procOwner | ``SyntheticControl.obj``:`n"
    if ($Fault -ne 'MissingProcedure') {
        $symbols += "  0 | S_GPROC32 [size = 16] ``MovzxTerminal`` `n  addr = 0001:$($prefix-$section), code size = $($procEnd-$prefix)`n"
    }
    $symbols += "  Mod $dataOwner | ``SyntheticControl.obj``:`n"
    if ($Fault -ne 'MissingData') {
        [UInt32]$dataStart = if ($Fault -eq 'WrongDataStart') { $table+4 } else { $table }
        $symbols += "  0 | S_LDATA32 [size = 16] `````n  type = <no type>, addr = 0001:$($dataStart-$section)`n"
    }
    if ($Fault -eq 'EarlyDataSymbol') {
        $symbols += "  0 | S_LDATA32 [size = 16] `````n  type = <no type>, addr = 0001:$($table-$section+8)`n"
    }
    # The next anonymous datum belongs to a distinct procedure in the same contribution/module.
    $laterProcedure = $end + 0x20
    $laterData = $end + 0x40
    $symbols += "  Mod 0 | ``SyntheticControl.obj``:`n  0 | S_GPROC32 [size = 16] ``LaterProcedure`` `n  addr = 0001:$($laterProcedure-$section), code size = 128`n"
    $symbols += "  Mod 0 | ``SyntheticControl.obj``:`n  0 | S_LDATA32 [size = 16] `````n  type = <no type>, addr = 0001:$($laterData-$section)`n"
    Set-Content -LiteralPath $case.Symbols -Value $symbols -Encoding utf8
    Set-Content -LiteralPath $case.Disassembly -Value (@($rows | Sort-Object {
        [UInt64]::Parse(($_ -split ':',2)[0],[Globalization.NumberStyles]::HexNumber)
    }) -join "`n") -Encoding utf8
    [IO.File]::WriteAllBytes($case.Binary,$pe)
    $case.Name = $Name
    return $case
}

$movzxTerminalCount = 0
Invoke-Control (New-MovzxGuardedTerminalDwordCase 'MovzxTerminalDwordValid') $true 6 0
++$movzxTerminalCount
Invoke-Control (New-MovzxGuardedTerminalDwordCase 'MovzxTerminalDefaultInEntries' 'DefaultInTable') $true 6 0
++$movzxTerminalCount
Invoke-Control (New-MovzxGuardedTerminalDwordCase 'MovzxTerminalMaximumOne' '' 1) $true 2 0
++$movzxTerminalCount
Invoke-Control (New-MovzxGuardedTerminalDwordCase 'MovzxTerminalMaximum127' '' 127) $true 128 0
++$movzxTerminalCount
# Keep a real VEX instruction directly after the terminal table rejected.
$case = New-MovzxGuardedTerminalDwordCase 'MovzxTerminalAdjacentVex'
$pe = [IO.File]::ReadAllBytes($case.Binary)
[UInt32]$vexAt = [UInt32]0x0010f900
[byte[]]$vex = @(0xc5,0xf8,0x10,0x00)
Put-Bytes $pe (0x400+$vexAt-[UInt32]0x0010f000) $vex
$rows = [IO.File]::ReadAllText($case.Disassembly)
$rows += "`n$('{0:x}: {1} vmovups xmm0, xmmword ptr [rax]' -f ([UInt64]0x140000000+$vexAt),(Hex-Bytes $vex))"
[IO.File]::WriteAllBytes($case.Binary,$pe)
Set-Content -LiteralPath $case.Disassembly -Value $rows -Encoding utf8
Invoke-Control $case $false 6 1
++$movzxTerminalCount

foreach ($fault in 'WrongMovzxDestination','WrongMovzxSource','SignedMovsx','WrongCmpRegister','RangeTooSmall','RangeTooLarge',
    'WrongJa','DefaultMidInstruction','DefaultOutsideProcedure','DefaultInsideTable','WrongImageBase','WrongLeaOpcode','WrongLeaDestination',
    'WrongLoadOpcode','WrongLoadDestination','WrongLoadScale','WrongLoadIndex','WrongLoadBase','WrongLoadAddress',
    'WrongAddOpcode','WrongAddRegisters','WrongJumpOpcode',
    'MissingPrefix0','MissingPrefix1','MissingPrefix2','ListingMismatchPrefix0','ListingMismatchPrefix1','ListingMismatchPrefix2',
    'UnknownPrefix0','UnknownPrefix1','UnknownPrefix2','OverlapPrefix0','OverlapPrefix1','OverlapPrefix2',
    'MissingDispatch0','MissingDispatch1','MissingDispatch2','MissingDispatch3',
    'ListingMismatchDispatch0','ListingMismatchDispatch1','ListingMismatchDispatch2','ListingMismatchDispatch3',
    'UnknownDispatch0','UnknownDispatch1','UnknownDispatch2','UnknownDispatch3',
    'OverlapDispatch0','OverlapDispatch1','OverlapDispatch2','OverlapDispatch3',
    'MissingProcedure','WrongProcedureOwner','ProcedureEndsEarly','ProcedureEndsLate',
    'MissingData','WrongDataOwner','WrongDataStart','EarlyDataSymbol',
    'BadLastTarget','OutsideTarget','MidTableTarget') {
    Invoke-Control (New-MovzxGuardedTerminalDwordCase ("MovzxTerminal$fault") $fault) $false
    ++$movzxTerminalCount
}
Write-Host "MOVZX guarded terminal DWORD synthetic controls passed: $movzxTerminalCount"
