[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if (-not ('RedPrismBuild.IsaScanner' -as [type])) {
    Add-Type -Path (Join-Path $repo 'Build/IsaScan.cs')
}
$fixtureRoot = Join-Path $repo ('.build/BuildProcessTests/IsaCompactSignedRcxByteRemap-' + [guid]::NewGuid().ToString('N'))
[void](New-Item -ItemType Directory -Path $fixtureRoot -Force)
$imageBase = [UInt64]0x140000000
$sectionRva = [UInt32]0x1000
$dispatchRva = [UInt32]0x1100
$guardRva = [UInt32]($dispatchRva - 22)
$dwordRva = [UInt32]0x1200
$byteRva = [UInt32]($dwordRva + 28)
$targetRva = [UInt32]0x11c5
$guid = [guid]'a05dbb28-9a9d-4ad0-931b-14e587ba16d4'
$age = [UInt32]4

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

function New-CompactSignedRcxControl {
    param(
        [string]$Name,
        [string]$Fault = '',
        [ValidateRange(1, 127)] [int]$Maximum = 31,
        [UInt32]$StackDisplacement = 0x2a8,
        [switch]$Interior
    )
    [UInt32]$caseByteRva = if ($Fault -eq 'OneEntryTable') { $dwordRva + 4 } else { $byteRva }
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

    # Literal observed form: CMP dword [RSP+disp32],imm8; JA rel32;
    # MOVSXD RAX,dword [RSP+the same disp32]. The sign-extended imm8 is 1..127.
    [byte[]]$cmp = [byte[]]@(0x83,0xbc,0x24,0,0,0,0,[byte]$Maximum)
    Put-Bytes $cmp 3 ([BitConverter]::GetBytes($StackDisplacement))
    if ($Fault -in 'WrongCmpOpcode','InvalidGuardLaterSymbol') { $cmp[0] = 0x81 }
    if ($Fault -eq 'WrongCompareDisplacement') { $cmp[3] = [byte]($cmp[3] -bxor 1) }
    if ($Fault -eq 'WrongGuardRegister') { $cmp[2] = 0x25 }
    if ($Fault -eq 'WrongGuardSib') { $cmp[2] = 0x2c }
    if ($Fault -eq 'BoundZero') { $cmp[7] = 0 }
    if ($Fault -eq 'Bound128') { $cmp[7] = 128 }
    if ($Fault -eq 'BoundMismatch') { $cmp[7] = [byte]($Maximum - 1) }
    [byte[]]$ja = [byte[]]@(0x0f,0x87,0,0,0,0)
    [byte[]]$index = [byte[]]@(0x48,0x63,0x84,0x24,0,0,0,0)
    Put-Bytes $index 4 ([BitConverter]::GetBytes($StackDisplacement))
    if ($Fault -eq 'WrongIndexOpcode') { $index[1] = 0xb6 }
    if ($Fault -eq 'WrongIndexDisplacement') { $index[4] = [byte]($index[4] -bxor 1) }
    if ($Fault -eq 'WrongIndexRegister') { $index[2] = 0x94 }
    if ($Fault -eq 'WrongIndexBaseRegister') { $index[0] = 0x49 }
    if ($Fault -eq 'WrongIndexSib') { $index[3] = 0x2c }
    [UInt32]$defaultTarget = if ($Fault -eq 'DefaultNotTableTarget') { [UInt32]0x1185 } elseif ($Fault -eq 'DefaultAndTargetNotBoundary') { [UInt32]($targetRva+1) } else { $targetRva }
    Put-Bytes $ja 2 ([BitConverter]::GetBytes([Int32]($defaultTarget - ($guardRva + 14))))
    [byte[]]$guard = [byte[]]::new(22)
    Put-Bytes $guard 0 $cmp
    Put-Bytes $guard 8 $ja
    Put-Bytes $guard 14 $index
    Put-Bytes $pe (0x400+$guardRva-$sectionRva) $guard

    # Literal 27-byte dispatch: LEA RCX,imageBase; MOVZX byte remap; indexed DWORD
    # target load; ADD RAX,RCX; JMP RAX. Table RVAs are image-relative displacements.
    [byte[]]$lea = [byte[]]@(0x48,0x8d,0x0d,0,0,0,0)
    Put-Bytes $lea 3 ([BitConverter]::GetBytes([Int32]($imageBase - ($imageBase+$dispatchRva+7))))
    [byte[]]$byteLoad = [byte[]]@(0x0f,0xb6,0x84,0x01,0,0,0,0)
    Put-Bytes $byteLoad 4 ([BitConverter]::GetBytes($caseByteRva))
    [byte[]]$dwordLoad = [byte[]]@(0x8b,0x84,0x81,0,0,0,0)
    Put-Bytes $dwordLoad 3 ([BitConverter]::GetBytes([UInt32]$dwordRva))
    [byte[]]$add = [byte[]]@(0x48,0x03,0xc1)
    [byte[]]$jump = [byte[]]@(0xff,0xe0)
    switch ($Fault) {
        'WrongLeaOpcode' { $lea[1] = 0x89 }
        'WrongRemapOpcode' { $byteLoad[1] = 0xb7 }
        'WrongRemapRva' { $byteLoad[4]++ }
        'WrongDwordOpcode' { $dwordLoad[0] = 0x89 }
        'WrongDwordRva' { $dwordLoad[3]++ }
        'WrongAddOpcode' { $add[1] = 0x01 }
        'WrongJumpOpcode' { $jump[1] = 0xe1 }
    }
    Put-Bytes $pe (0x400+$dispatchRva-$sectionRva) $lea
    Put-Bytes $pe (0x400+$dispatchRva+7-$sectionRva) $byteLoad
    Put-Bytes $pe (0x400+$dispatchRva+15-$sectionRva) $dwordLoad
    Put-Bytes $pe (0x400+$dispatchRva+22-$sectionRva) $add
    Put-Bytes $pe (0x400+$dispatchRva+25-$sectionRva) $jump

    [byte[]]$dwords = [byte[]]::new($caseByteRva - $dwordRva)
    for ($i=0; $i -lt ($dwords.Length / 4); ++$i) {
        [UInt32]$entryTarget = $targetRva
        if ($Fault -eq 'MiddleTarget' -and $i -eq 3) { $entryTarget++ }
        if ($Fault -eq 'DefaultAndTargetNotBoundary') { $entryTarget++ }
        if ($Fault -eq 'TargetInTable' -and $i -eq 2) { $entryTarget=$dwordRva }
        if ($Fault -eq 'TargetAfterTable' -and $i -eq 1) { $entryTarget=$caseByteRva }
        Put-Bytes $dwords (4*$i) ([BitConverter]::GetBytes($entryTarget))
    }
    Put-Bytes $pe (0x400+$dwordRva-$sectionRva) $dwords
    [byte[]]$remap = [byte[]]::new($Maximum + 1)
    for ($i=0; $i -lt $remap.Length; ++$i) { $remap[$i] = [byte]($i % 7) }
    if ($Fault -eq 'BadRemapEntry') { $remap[$Maximum] = 7 }
    Put-Bytes $pe (0x400+$caseByteRva-$sectionRva) $remap
    Put-Bytes $pe (0x400+$targetRva-$sectionRva) ([byte[]]@(0x66,0x90))
    if ($Fault -eq 'DefaultNotTableTarget') {
        Put-Bytes $pe (0x400+0x1185-$sectionRva) ([byte[]]@(0x66,0x90))
    }

    $procedureEnd = [UInt32]($caseByteRva + $Maximum + 1)
    $remapEnd = $procedureEnd
    $nextTable = [UInt32](($remapEnd + 3) -band 0xfffffffc)
    if ($Interior) {
        $procedureEnd = [UInt32]($nextTable + 16)
        [byte[]]$padding = switch ($nextTable - $remapEnd) {
            0 { [byte[]]@() }
            1 { [byte[]]@(0x90) }
            2 { [byte[]]@(0x66,0x90) }
            3 { [byte[]]@(0x0f,0x1f,0x00) }
        }
        if ($nextTable -ne $remapEnd) {
            Put-Bytes $pe (0x400+$remapEnd-$sectionRva) $padding
        }
        if ($Fault -eq 'InteriorWrongPadding') { $pe[0x400+$remapEnd-$sectionRva] = 0xcc }
        if ($Fault -eq 'InteriorPaddingVex') {
            Put-Bytes $pe (0x400+$remapEnd-$sectionRva) ([byte[]]@(0xc5,0xf8,0x77))
        }
    }
    $procedureSize = $procedureEnd - $sectionRva
    if ($Fault -eq 'ProcedureShort') { $procedureSize-- }
    if ($Fault -eq 'ProcedureLong') { $procedureSize++ }
    $module = "  Mod 0000 | ``Synthetic.obj``:`n  Obj: ``Synthetic.lib``:`n"
    $otherModule = "  Mod 0001 | ``Other.obj``:`n  Obj: ``Other.lib``:`n"
    [UInt32]$contributionModule = if ($Fault -eq 'WrongContributionOwner') { 1 } else { 0 }
    [UInt32]$contributionSize = 8192
    if ($Fault -eq 'ContributionShort') { $contributionSize = [UInt32]($procedureSize - 1) }
    $metadataText = "  Age: $age`n  GUID: {$guid}`n$module$otherModule`nSection Contributions`n  SC[.text]  | mod = $contributionModule, 0001:0, size = $contributionSize, data crc = 0, reloc crc = 0`n"
    Set-Content -LiteralPath $metadata -Value $metadataText -Encoding utf8
    $symbolText = ''
    if ($Fault -ne 'MissingProcedure') {
        $procedureModule = if ($Fault -eq 'WrongProcedureOwner') { $otherModule } else { $module }
        $symbolText += $procedureModule + "  0 | S_GPROC32 [size = 40] ``SyntheticDispatch```n    addr = 0001:0, code size = $procedureSize`n"
    }
    $dwordOwner = if ($Fault -eq 'WrongDwordOwner') { $otherModule } else { $module }
    if ($Fault -ne 'MissingDwordSymbol') {
        $symbolText += $dwordOwner + "  0 | S_LDATA32 [size = 16] `````n  type = <no type>, addr = 0001:512`n"
    }
    $byteOwner = if ($Fault -eq 'WrongByteOwner') { $otherModule } else { $module }
    if ($Fault -ne 'MissingByteSymbol') {
        $symbolText += $byteOwner + "  0 | S_LDATA32 [size = 16] `````n  type = <no type>, addr = 0001:$($caseByteRva-$sectionRva)`n"
    }
    if ($Fault -in 'LaterSymbol','InvalidGuardLaterSymbol','MissingGuardWithLaterSymbol') {
        $symbolText += "  0 | S_LDATA32 [size = 16] `````n  type = <no type>, addr = 0001:$($procedureEnd-$sectionRva)`n"
    }
    if ($Interior -and $Fault -ne 'InteriorMissingNextSymbol') {
        $nextOwner = if ($Fault -eq 'InteriorForeignNextSymbol') { $otherModule } else { $module }
        $nextSymbol = $nextTable
        switch ($Fault) {
            'InteriorNextSymbolUnaligned' { $nextSymbol++ }
            'InteriorNextSymbolInsideRemap' { $nextSymbol = $caseByteRva + 1 }
            'InteriorNextSymbolTooFar' { $nextSymbol += 4 }
            'InteriorNextSymbolAtProcedureEnd' { $nextSymbol = $procedureEnd }
        }
        $symbolText += $nextOwner + "  0 | S_LDATA32 [size = 16] `````n  type = <no type>, addr = 0001:$($nextSymbol-$sectionRva)`n"
    }
    Set-Content -LiteralPath $symbols -Value $symbolText -Encoding utf8

    $steps = @(
        @{Name='GuardCmp'; Rva=$guardRva; Bytes=$cmp; Text='cmp dword ptr [rsp + disp32], imm8'},
        @{Name='GuardJa'; Rva=($guardRva+8); Bytes=$ja; Text='ja default'},
        @{Name='GuardIndex'; Rva=($guardRva+14); Bytes=$index; Text='movsxd rax, dword ptr [rsp + disp32]'},
        @{Name='Base'; Rva=$dispatchRva; Bytes=$lea; Text='lea rcx, [rip + imagebase]'},
        @{Name='Remap'; Rva=($dispatchRva+7); Bytes=$byteLoad; Text='movzx eax, byte ptr [rcx + rax + remap]'},
        @{Name='Dword'; Rva=($dispatchRva+15); Bytes=$dwordLoad; Text='mov eax, dword ptr [rcx + rax*4 + targets]'},
        @{Name='Add'; Rva=($dispatchRva+22); Bytes=$add; Text='add rax, rcx'},
        @{Name='Jump'; Rva=($dispatchRva+25); Bytes=$jump; Text='jmp rax'})
    $rows = [Collections.Generic.List[string]]::new()
    foreach ($step in $steps) {
        if ($Fault -eq ('Missing'+$step.Name) -or
                ($Fault -eq 'MissingGuardWithLaterSymbol' -and $step.Name -eq 'GuardCmp')) { continue }
        [byte[]]$listed = [byte[]]$step.Bytes.Clone()
        if ($Fault -eq ('PeMismatch'+$step.Name)) {
            # Preserve the independent listing and corrupt only the executable image byte.
            $pe[0x400+$step.Rva-$sectionRva] = [byte]($pe[0x400+$step.Rva-$sectionRva] -bxor 1)
        }
        if ($Fault -eq ('ListingMismatch'+$step.Name)) { $listed[$listed.Length-1] = [byte]($listed[$listed.Length-1] -bxor 1) }
        $text = if ($Fault -eq ('Unknown'+$step.Name)) { '<unknown>' } else { $step.Text }
        $row = '{0:x}: {1} {2}' -f ($imageBase+$step.Rva),(Hex-Bytes $listed),$text
        $rows.Add($row)
        if ($Fault -eq ('Duplicate'+$step.Name)) { $rows.Add($row) }
        if ($Fault -eq ('Overlap'+$step.Name)) { $rows.Add(('{0:x}: 90 nop' -f ($imageBase+$step.Rva+1))) }
    }
    if ($Fault -ne 'MissingTargetRow') { $rows.Add(('{0:x}: 66 90 nop' -f ($imageBase+$targetRva))) }
    if ($Fault -eq 'DefaultNotTableTarget') { $rows.Add(('{0:x}: 66 90 nop' -f ($imageBase+0x1185))) }
    for ($i=0; $i -lt 7; ++$i) {
        $rva = [UInt32]($dwordRva+4*$i)
        [byte[]]$vectorData = [byte[]]@(0xc5,0x11,0x00,0x00)
        $rows.Add(('{0:x}: {1} vmovups xmm0, xmmword ptr [rax]' -f ($imageBase+$rva),(Hex-Bytes $vectorData)))
    }
    if ($Fault -eq 'InteriorPaddingVex') {
        $rows.Add(('{0:x}: c5 f8 77 vzeroupper' -f ($imageBase+$remapEnd)))
    }
    if ($Fault -in 'RealVexBody','RealEvexBody','RealVexAdjacent','RealEvexAdjacent') {
        $at = if ($Fault.EndsWith('Body')) { [UInt32]($targetRva+2) } else { [UInt32]0x1300 }
        $isEvex = $Fault.StartsWith('RealEvex', [StringComparison]::Ordinal)
        [byte[]]$vector = if ($isEvex) { [byte[]]@(0x62,0xf1,0x7c,0x48,0x10,0x00) } else { [byte[]]@(0xc5,0xf8,0x10,0x00) }
        Put-Bytes $pe (0x400+$at-$sectionRva) $vector
        $mnemonic = if ($isEvex) { 'vmovups zmm0, zmmword ptr [rax]' } else { 'vmovups xmm0, xmmword ptr [rax]' }
        $rows.Add(('{0:x}: {1} {2}' -f ($imageBase+$at),(Hex-Bytes $vector),$mnemonic))
    }
    Set-Content -LiteralPath $disassembly -Value (@($rows | Sort-Object {
        [UInt64]::Parse(($_ -split ':',2)[0], [Globalization.NumberStyles]::HexNumber)
    }) -join "`n") -Encoding utf8
    [IO.File]::WriteAllBytes($binary, $pe)
    return @{Name=$Name; Binary=$binary; Metadata=$metadata; Symbols=$symbols; Disassembly=$disassembly}
}

function Assert-CompactSignedRcxControl($Case, [bool]$ExpectedPass) {
    $result = [RedPrismBuild.IsaScanner]::Scan($Case.Binary, $Case.Metadata, $Case.Symbols, $Case.Disassembly,
        [string[]]@(), [string[]]@(), [string[]]@())
    if ($ExpectedPass -and ($result.Violations.Count -ne 0 -or $result.InlineTableVectorData -ne 7)) {
        throw "$($Case.Name): expected seven proven inline DWORD rows and no violations; got $($result.InlineTableVectorData), $($result.Violations -join '; ')"
    }
    if (-not $ExpectedPass -and $result.Violations.Count -eq 0) { throw "$($Case.Name): malformed control bypassed the ISA gate." }
    if ($Case.Name -in 'RealVexBody','RealEvexBody','RealVexAdjacent','RealEvexAdjacent') {
        $evex = [int]$Case.Name.StartsWith('RealEvex', [StringComparison]::Ordinal)
        if ($result.InlineTableVectorData -ne 7 -or $result.Violations.Count -ne 1 -or $result.EvexInstructions -ne $evex) {
            throw "$($Case.Name): the real vector instruction must remain gated while all seven DWORD rows stay data."
        }
    }
    Write-Host "PASS isa-compact-signed-rcx-byte-remap.$($Case.Name)"
    ++$script:isaCompactSignedRcxCaseCount
}

$script:isaCompactSignedRcxCaseCount = 0
# Independent positive literals span both imm8 endpoints, intermediate bounds, and stack displacements.
Assert-CompactSignedRcxControl (New-CompactSignedRcxControl 'MinimumBoundZeroDisp' '' 1 0) $true
Assert-CompactSignedRcxControl (New-CompactSignedRcxControl 'SmallBoundSmallDisp' '' 7 0x20) $true
Assert-CompactSignedRcxControl (New-CompactSignedRcxControl 'IntermediateBoundFrameDisp' '' 63 0x2a8) $true
Assert-CompactSignedRcxControl (New-CompactSignedRcxControl 'MaximumBoundLargeDisp' '' 127 0x7fffffff) $true
foreach ($bound in 1,67,127) {
    foreach ($displacement in 0,0x6a0,0x7fffffff) {
        $caseName = 'MatrixMax{0}Disp{1:x}' -f $bound,$displacement
        Assert-CompactSignedRcxControl (New-CompactSignedRcxControl $caseName '' $bound ([UInt32]$displacement)) $true
    }
}
Assert-CompactSignedRcxControl (New-CompactSignedRcxControl 'ValidLaterSymbol' 'LaterSymbol') $true

foreach ($step in 'GuardCmp','GuardJa','GuardIndex','Base','Remap','Dword','Add','Jump') {
    foreach ($kind in 'Missing','Duplicate','Unknown','Overlap','PeMismatch','ListingMismatch') {
        $fault = $kind+$step
        Assert-CompactSignedRcxControl (New-CompactSignedRcxControl $fault $fault) $false
    }
}
foreach ($fault in @(
    'WrongCmpOpcode','WrongCompareDisplacement','BoundZero','Bound128','BoundMismatch',
    'WrongGuardRegister','WrongGuardSib','WrongIndexOpcode','WrongIndexDisplacement','WrongIndexRegister',
    'WrongIndexBaseRegister','WrongIndexSib',
    'WrongLeaOpcode','WrongRemapOpcode','WrongRemapRva',
    'WrongDwordOpcode','WrongDwordRva','WrongAddOpcode','WrongJumpOpcode','BadRemapEntry',
    'OneEntryTable','MissingProcedure','MissingDwordSymbol','MissingByteSymbol','ProcedureShort','ProcedureLong',
    'ContributionShort','WrongContributionOwner','WrongProcedureOwner','WrongDwordOwner','WrongByteOwner',
    'DefaultNotTableTarget','DefaultAndTargetNotBoundary','MiddleTarget','TargetInTable','TargetAfterTable','MissingTargetRow',
    'RealVexBody','RealEvexBody','RealVexAdjacent','RealEvexAdjacent','InvalidGuardLaterSymbol','MissingGuardWithLaterSymbol')) {
    Assert-CompactSignedRcxControl (New-CompactSignedRcxControl $fault $fault) $false
}
foreach ($bound in 31,30,29,28) {
    $paddingLength = 31-$bound
    Assert-CompactSignedRcxControl (New-CompactSignedRcxControl "InteriorPadding$paddingLength" '' $bound 0x688 -Interior) $true
}
foreach ($fault in 'InteriorMissingNextSymbol','InteriorForeignNextSymbol','InteriorNextSymbolUnaligned',
        'InteriorNextSymbolInsideRemap','InteriorNextSymbolTooFar','InteriorNextSymbolAtProcedureEnd',
        'InteriorWrongPadding','InteriorPaddingVex','WrongProcedureOwner','WrongContributionOwner') {
    Assert-CompactSignedRcxControl (New-CompactSignedRcxControl ("Interior."+$fault) $fault 28 0x688 -Interior) $false
}
$expectedCaseCount = 119
if ($script:isaCompactSignedRcxCaseCount -ne $expectedCaseCount) {
    throw "Expected $expectedCaseCount ISA controls; ran $script:isaCompactSignedRcxCaseCount."
}
Write-Host "SUMMARY isa-compact-signed-rcx-byte-remap caseCount=$script:isaCompactSignedRcxCaseCount"
