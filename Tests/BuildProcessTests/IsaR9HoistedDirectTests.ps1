[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
. (Join-Path $repo 'Tests/BuildProcessTests/IsaInlineTableTests.ps1')

# These fixtures are literal parser controls. They are never executed as native code.
# The seed supplies the same small PE32+ / RSDS image used by the neighboring controls;
# each case replaces its text rows, table bytes, disassembly and PDB records independently.
$hdRoot = Join-Path $repo ('.build/BuildProcessTests/IsaR9HoistedDirect-' + [guid]::NewGuid().ToString('N'))
[void](New-Item -ItemType Directory -Path $hdRoot -Force)
$hdCases = [Collections.Generic.List[string]]::new()
$hdAccepted = 0

function Hd-SetU32([byte[]]$Bytes, [int]$Offset, [UInt32]$Value) {
    [Array]::Copy([BitConverter]::GetBytes($Value), 0, $Bytes, $Offset, 4)
}
function Hd-Put([byte[]]$Bytes, [int]$Offset, [byte[]]$Value) {
    [Array]::Copy($Value, 0, $Bytes, $Offset, $Value.Length)
}
function Hd-Hex([byte[]]$Bytes) {
    return (($Bytes | ForEach-Object { $_.ToString('x2') }) -join ' ')
}
function Hd-AddRow([Collections.Generic.List[string]]$Rows, [UInt32]$Rva, [byte[]]$Bytes, [string]$Text) {
    $Rows.Add(('{0:x}: {1} {2}' -f ($script:imageBase + $Rva), (Hd-Hex $Bytes), $Text))
}

function New-HoistedDirectCase([string]$Name, [string]$Fault = '', [int]$Maximum = 14) {
    $dir = Join-Path $script:hdRoot $Name
    [void](New-Item -ItemType Directory -Path $dir -Force)
    $seed = New-SyntheticCase ($Name + 'Seed') -PdbData
    $pe = [IO.File]::ReadAllBytes($seed.Binary)
    [UInt32]$section = $script:sectionRva
    [UInt32]$procBegin = $section + 0x800
    [UInt32]$baseRva = $procBegin + 16
    [UInt32]$guard = $section + 0x840
    [UInt32]$table = $section + 0x900
    [UInt32]$tableEnd = $table + 4 * ($Maximum + 1)
    [UInt32]$target = $section + 0x8c5 # Low RVA byte C5 is deliberately VEX-looking.
    [UInt32]$default = $target
    if ($Fault -eq 'DefaultAtBodyEnd') { $default = $table - 1 }
    if ($Fault -eq 'DistinctDefaultTarget') { $default = $guard + 29 }
    if ($Fault -eq 'DefaultInterior') { $default = $target + 1 }
    if ($Fault -eq 'DefaultGuard') { $default = $guard + 7 }
    if ($Fault -eq 'DefaultData') { $default = $table }
    if ($Fault -eq 'DefaultOutside') { $default = $section + 0x1500 }
    if ($Fault -eq 'DefaultUnknown') { $default = $table - 2 }

    # Reset executable section bytes, then lay down a conditional join, RIP-relative
    # R9 image-base definition, safe NOPs and direct jump into the guarded dispatch.
    for ($i = 0x600; $i -lt 0x1400; ++$i) { $pe[$i] = 0x90 }
    [byte[]]$join = @(0x74, 0x06) # both edges join before the R9 definition
    Hd-Put $pe (0x400 + $procBegin - $section) $join
    $numericBypass = $Fault -in @('GuardedBypassNever','GuardedBypassAdmitted','BypassFlagClobber')
    if ($numericBypass) {
        [byte[]]$entryCmp = @(0x41,0x81,0xff,0,0,0,0)
        Hd-SetU32 $entryCmp 3 $(if ($Fault -eq 'GuardedBypassAdmitted') { 260 } else { 512 })
        Hd-Put $pe (0x400+$procBegin-$section) $entryCmp
        [UInt32]$entryBranchRva=$procBegin+7
        if ($Fault -eq 'BypassFlagClobber') {
            Hd-Put $pe (0x400+$entryBranchRva-$section) ([byte[]]@(0x33,0xc0))
            $entryBranchRva+=2
        }
        [byte[]]$entryBranch=@(0x0f,0x87,0,0,0,0)
        Hd-SetU32 $entryBranch 2 ([UInt32]($guard-$entryBranchRva-6))
        Hd-Put $pe (0x400+$entryBranchRva-$section) $entryBranch
    }
    [byte[]]$base = @(0x4c,0x8d,0x0d,0,0,0,0)
    [Int32]$imageDisplacement = -[Int64]($baseRva + 7)
    if ($Fault -eq 'WrongImageBase') { ++$imageDisplacement }
    if ($Fault -eq 'WrongBaseBytes') { $base[0] = 0x48 }
    [Array]::Copy([BitConverter]::GetBytes($imageDisplacement), 0, $base, 3, 4)
    Hd-Put $pe (0x400 + $baseRva - $section) $base
    [byte[]]$jumpToGuard = @(0xe9,0,0,0,0)
    [UInt32]$preGuardJump = $baseRva + 10
    if ($Fault -in @('ClobberR9','ClobberR15')) { $preGuardJump += 3 }
    if ($Fault -eq 'CallBetweenBaseAndGuard') { $preGuardJump += 5 }
    [Int32]$jumpDisplacement = [Int32]([Int64]$guard - [Int64]($preGuardJump + 5))
    [Array]::Copy([BitConverter]::GetBytes($jumpDisplacement), 0, $jumpToGuard, 1, 4)
    if ($Fault -in @('ClobberR9','ClobberR15')) {
        Hd-Put $pe (0x400 + $baseRva + 10 - $section) ([byte[]]@(0x45,0x31,$(if($Fault-eq'ClobberR9'){0xc9}else{0xff})))
    } elseif ($Fault -eq 'CallBetweenBaseAndGuard') {
        Hd-Put $pe (0x400 + $baseRva + 10 - $section) ([byte[]]@(0xe8,0,0,0,0))
    }
    Hd-Put $pe (0x400 + $preGuardJump - $section) $jumpToGuard

    [byte[]]$leaIndex = @(0x41,0x8d,0x87,0xff,0xfe,0xff,0xff)
    [byte[]]$cmp = @(0x83,0xf8,[byte]$Maximum)
    [byte[]]$ja = @(0x0f,0x87,0,0,0,0)
    [byte[]]$load = @(0x41,0x8b,0x8c,0x81,0,0,0,0)
    [byte[]]$add = @(0x49,0x03,0xc9)
    [byte[]]$indirect = @(0xff,0xe1)
    [Int32]$jaDisplacement = [Int32]([Int64]$default - [Int64]($guard + 16))
    [Array]::Copy([BitConverter]::GetBytes($jaDisplacement), 0, $ja, 2, 4)
    Hd-SetU32 $load 4 $table
    switch ($Fault) {
        'WrongBias' { $leaIndex[3] = 0xfe }
        'WrongIndexRex' { $leaIndex[0] = 0x49 }
        'WrongIndexModRm' { $leaIndex[2] = 0x86 }
        'WrongIndexDisp' { $leaIndex[3] = 0x00 }
        'WrongCmpOpcode' { $cmp[0] = 0x81 }
        'WrongCmpRegister' { $cmp[1] = 0xf9 }
        'WrongJaOpcode' { $ja[1] = 0x86 }
        'WrongJaWidth' { $ja[0] = 0x70 }
        'WrongLoadRex' { $load[0] = 0x49 }
        'WrongLoadOpcode' { $load[1] = 0x89 }
        'WrongLoadDestination' { $load[2] = 0x94 }
        'WrongLoadSib' { $load[3] = 0x91 }
        'WrongTableRva' { $load[4]++ }
        'WrongAddRex' { $add[0] = 0x48 }
        'WrongAddRegister' { $add[2] = 0xc1 }
        'WrongJumpRegister' { $indirect[1] = 0xe2 }
        'Range0' { $cmp[2] = 0 }
        'Range128' { $cmp[2] = 128 }
        'Range255' { $cmp[2] = 255 }
        'AbsentBase' { $base = [byte[]]@(0x90,0x90,0x90,0x90,0x90,0x90,0x90); Hd-Put $pe (0x400 + $baseRva - $section) $base }
        'UnalignedTable' { Hd-SetU32 $load 4 ($table + 1) }
    }
    $guardParts = @(
        @{Rva=$guard; Bytes=$leaIndex; Text='lea eax, [r15 - 0x101]'},
        @{Rva=$guard+7; Bytes=$cmp; Text='cmp eax, max'},
        @{Rva=$guard+10; Bytes=$ja; Text='ja default'},
        @{Rva=$guard+16; Bytes=$load; Text='mov ecx, dword ptr [r9 + rax*4 + table]'},
        @{Rva=$guard+24; Bytes=$add; Text='add rcx, r9'},
        @{Rva=$guard+27; Bytes=$indirect; Text='jmp rcx'}
    )
    foreach ($part in $guardParts) { Hd-Put $pe (0x400 + $part.Rva - $section) $part.Bytes }

    # Populate the complete pre-table procedure with decoded one-byte NOPs. The
    # table itself is exactly Maximum+1 DWORD RVAs and begins at an aligned RVA.
    for ([UInt32]$rva = $guard + 29; $rva -lt $table; ++$rva) { $pe[0x400 + $rva - $section] = 0x90 }
    $pe[0x400 + $table - 1 - $section] = 0xc3
    if ($target -ne ($table - 1)) { Hd-Put $pe (0x400 + $target - $section) ([byte[]]@(0x0f,0x1f,0x40,0x00)) }

    if ($Fault -eq 'IncomingGuardInterior' -or $Fault -eq 'IncomingGuardStart' -or $Fault -eq 'IncomingTable') {
        [UInt32]$dest = if ($Fault -eq 'IncomingGuardStart') { $guard } elseif ($Fault -eq 'IncomingGuardInterior') { $guard + 1 } else { $table }
        [byte[]]$incoming = @(0xe9,0,0,0,0)
        [UInt32]$src = $target
        [Int32]$rel = [Int32]([Int64]$dest - [Int64]($src + 5))
        [Array]::Copy([BitConverter]::GetBytes($rel), 0, $incoming, 1, 4)
        Hd-Put $pe (0x400 + $src - $section) $incoming
    }
    if ($Fault -eq 'BypassBasePath') {
        # Conditional edge from the procedure entry straight to the guard while
        # its other edge reaches the explicit R9 definition.
        [byte[]]$bypass = @(0x74,0,0x90,0x90,0x90,0x90,0x90,0x90)
        [Int32]$rel = [Int32]([Int64]$guard - [Int64]($procBegin + 2))
        $bypass[1] = [byte]$rel
        Hd-Put $pe (0x400 + $procBegin - $section) $bypass
    }

    for ($i = 0; $i -le $Maximum; ++$i) {
        [UInt32]$entry = $target
        if ($Fault -in @('DistinctDefaultTarget','DefaultAtBodyEnd') -and $i -eq $Maximum) { $entry = $default }
        if ($Fault -eq 'TargetAtBodyEnd' -and $i -eq $Maximum) { $entry = $table - 1 }
        if ($Fault -eq 'TargetOutside' -and $i -eq $Maximum) { $entry = $section + 0x1500 }
        if ($Fault -eq 'TargetInterior' -and $i -eq $Maximum) { $entry = $target + 1 }
        if ($Fault -eq 'TargetGuard' -and $i -eq $Maximum) { $entry = $guard + 7 }
        if ($Fault -eq 'TargetData' -and $i -eq $Maximum) { $entry = $table }
        if ($Fault -eq 'TargetUnknown' -and $i -eq $Maximum) { $entry = $table - 2 }
        Hd-SetU32 $pe (0x400 + $table - $section + 4*$i) $entry
    }

    $rows = [Collections.Generic.List[string]]::new()
    [byte[]]$joinRow = [byte[]]$pe[(0x400+$procBegin-$section)..(0x400+$procBegin-$section+1)]
    if ($numericBypass) {
        Hd-AddRow $rows $procBegin $entryCmp 'cmp r15d, literal'
        if ($Fault -eq 'BypassFlagClobber') { Hd-AddRow $rows ($procBegin+7) ([byte[]]@(0x33,0xc0)) 'xor eax, eax' }
        Hd-AddRow $rows $entryBranchRva $entryBranch 'ja guard'
        $entryEnd=$entryBranchRva+6
    }else{
        Hd-AddRow $rows $procBegin $joinRow $(if ($Fault -eq 'BypassBasePath') { 'je guard' } else { 'je base' })
        $entryEnd=$procBegin+2
    }
    for ([UInt32]$rva = $entryEnd; $rva -lt $baseRva; ++$rva) { Hd-AddRow $rows $rva ([byte[]]@(0x90)) 'nop' }
    if ($Fault -eq 'AbsentBase') {
        for ([UInt32]$rva = $baseRva; $rva -lt $baseRva + 7; ++$rva) { Hd-AddRow $rows $rva ([byte[]]@(0x90)) 'nop' }
    } elseif ($Fault -eq 'MissingBaseListing') {
        # Keep the PE definition while omitting its required disassembly row.
    } elseif ($Fault -eq 'UnknownBaseListing') {
        Hd-AddRow $rows $baseRva $base '<unknown>'
    } elseif ($Fault -eq 'ListingBase') {
        [byte[]]$shownBase = [byte[]]$base.Clone(); $shownBase[6] = $shownBase[6] -bxor 1
        Hd-AddRow $rows $baseRva $shownBase 'lea r9, [rip + imagebase]'
    } elseif ($Fault -eq 'OverlapBase') {
        Hd-AddRow $rows $baseRva $base 'lea r9, [rip + imagebase]'
        Hd-AddRow $rows ($baseRva+1) ([byte[]]@(0x90)) 'nop'
    } else { Hd-AddRow $rows $baseRva $base 'lea r9, [rip + imagebase]' }
    for ([UInt32]$rva = $baseRva + 7; $rva -lt $baseRva + 10; ++$rva) { Hd-AddRow $rows $rva ([byte[]]@(0x90)) 'nop' }
    if ($Fault -eq 'ClobberR9') { Hd-AddRow $rows ($baseRva+10) ([byte[]]@(0x45,0x31,0xc9)) 'xor r9d, r9d' }
    if ($Fault -eq 'ClobberR15') { Hd-AddRow $rows ($baseRva+10) ([byte[]]@(0x45,0x31,0xff)) 'xor r15d, r15d' }
    if ($Fault -eq 'CallBetweenBaseAndGuard') { Hd-AddRow $rows ($baseRva+10) ([byte[]]@(0xe8,0,0,0,0)) 'call unknown' }
    Hd-AddRow $rows $preGuardJump $jumpToGuard 'jmp guard'
    for ([UInt32]$rva = $preGuardJump + 5; $rva -lt $guard; ++$rva) { Hd-AddRow $rows $rva ([byte[]]@(0x90)) 'nop' }
    $guardRowStarts = @(0,7,10,16,24,27)
    foreach ($i in 0..5) {
        $part = $guardParts[$i]
        $rowBytes = [byte[]]$part.Bytes.Clone()
        $rowText = $part.Text
        if ($Fault -eq "MissingGuard$i") { continue }
        if ($Fault -eq "UnknownGuard$i") { $rowText = '<unknown>' }
        if ($Fault -eq "ListingGuard$i") { $rowBytes[$rowBytes.Length-1] = $rowBytes[$rowBytes.Length-1] -bxor 1 }
        if ($Fault -eq 'ListingJaTarget' -and $i -eq 2) { $rowBytes[2] = $rowBytes[2] -bxor 1 }
        if ($Fault -eq "OverlapGuard$i") { Hd-AddRow $rows ($part.Rva+1) ([byte[]]@(0x90)) 'nop' }
        if ($Fault -eq "OffsetGuard$i") { Hd-AddRow $rows ($part.Rva+1) $rowBytes $rowText }
        else { Hd-AddRow $rows $part.Rva $rowBytes $rowText }
    }
    for ([UInt32]$rva = $guard + 29; $rva -lt $table;) {
        if ($Fault -in @('IncomingGuardInterior','IncomingGuardStart','IncomingTable') -and $rva -eq $target) {
            [byte[]]$incoming = [byte[]]$pe[(0x400+$rva-$section)..(0x400+$rva-$section+4)]
            Hd-AddRow $rows $rva $incoming 'jmp incoming-target'
            $rva += 5
            continue
        }
        if ($Fault -in @('RealVexBeforeTarget','RealVexAtTarget','RealVexAfterTarget','RealVexBeforeData')) {
            [UInt32]$vexAt = switch ($Fault) {
                'RealVexBeforeTarget' { $target-4 }
                'RealVexAtTarget' { $target }
                'RealVexAfterTarget' { $target+4 }
                'RealVexBeforeData' { $table-5 }
            }
            if ($rva -eq $vexAt) {
                Hd-Put $pe (0x400 + $rva - $section) ([byte[]]@(0xc5,0xf8,0x10,0x00))
                Hd-AddRow $rows $rva ([byte[]]@(0xc5,0xf8,0x10,0x00)) 'vmovups xmm0, xmmword ptr [rax]'
                $rva += 4
                continue
            }
        }
        if ($rva -eq $target -and $target -ne ($table-1)) {
            Hd-AddRow $rows $rva ([byte[]]@(0x0f,0x1f,0x40,0x00)) 'nop dword ptr [rax]'
            $rva += 4
            continue
        }
        if ($rva -eq ($table-1)) {
            Hd-AddRow $rows $rva ([byte[]]@(0xc3)) 'ret'
            ++$rva
            continue
        }
        $text = if ($Fault -eq 'TargetUnknown' -and $rva -eq ($table-2)) { '<unknown>' } else { 'nop' }
        Hd-AddRow $rows $rva ([byte[]]@(0x90)) $text
        ++$rva
    }
    if ($Fault -eq 'WrongListingBase') { $rows.Add(('{0:x}: 4c 8d 0d 00 00 00 00 lea r9, [rip + imagebase]' -f ($script:imageBase+$baseRva))) }

    for ($i = 0; $i -le $Maximum; ++$i) {
        [int]$at = 0x400 + $table - $section + 4*$i
        [byte[]]$entryBytes = [byte[]]$pe[$at..($at+3)]
        if ($Fault -eq 'MissingTableRow' -and $i -eq $Maximum) { continue }
        if ($Fault -eq 'UnknownTableRow' -and $i -eq 0) { Hd-AddRow $rows ($table+4*$i) $entryBytes '<unknown>'; continue }
        if ($Fault -eq 'ListingTableRow' -and $i -eq 0) { $entryBytes[0] = $entryBytes[0] -bxor 1 }
        Hd-AddRow $rows ($table+4*$i) $entryBytes 'vmovups xmm0, xmmword ptr [rax]'
    }
    if ($Fault -eq 'OverlapTableRow') { Hd-AddRow $rows ($table+1) ([byte[]]@(0x90)) 'nop' }
    if ($Fault -in @('NamedNextDataVex','RealVexAfterData')) {
        Hd-Put $pe (0x400 + $tableEnd - $section) ([byte[]]@(0xc5,0xf8,0x10,0x00))
        Hd-AddRow $rows $tableEnd ([byte[]]@(0xc5,0xf8,0x10,0x00)) 'vmovups xmm0, xmmword ptr [rax]'
    }

    [UInt32]$procedureEnd = $tableEnd
    if ($Fault -eq 'LongProcedure' -or $Fault -eq 'LongTableSymbol') { $procedureEnd += 4 }
    if ($Fault -eq 'ShortProcedure') { $procedureEnd -= 4 }
    if ($Fault -eq 'WrongProcedureEnd') { $procedureEnd += 8 }
    [UInt32]$nextData = $procedureEnd
    if ($Fault -eq 'ShortTableSymbol') { $nextData = $tableEnd - 4 }
    if ($Fault -eq 'LongTableSymbol') { $nextData = $tableEnd + 4 }
    $owner = if ($Fault -eq 'WrongOwner') { 1 } else { 0 }
    $procedure = "  Mod $owner | ``SyntheticControl.obj``:`n  0 | S_GPROC32 [size = 16] ``HoistedDirect`` `n  addr = 0001:$($procBegin-$section), code size = $($procedureEnd-$procBegin)`n"
    if ($Fault -eq 'MissingProcedure') { $procedure = '' }
    $symbolText = $procedure
    if ($Fault -ne 'MissingTableSymbol') {
        $symOwner = if ($Fault -eq 'WrongTableOwner') { 1 } else { 0 }
        $symName = if ($Fault -eq 'NamedTable') { '``dispatch``' } else { '``' }
        $symbolText += "  Mod $symOwner | ``SyntheticControl.obj``:`n  0 | S_LDATA32 [size = 16] $symName`n  type = <no type>, addr = 0001:$($table-$section)`n"
        if ($Fault -eq 'NamedNextDataVex') {
            $symbolText += "  Mod 0 | ``SyntheticControl.obj``:`n  0 | S_LDATA32 [size = 16] ``named`` `n  type = <no type>, addr = 0001:$($tableEnd-$section)`n"
        } elseif ($Fault -in @('ShortTableSymbol','LongTableSymbol')) {
            $symbolText += "  Mod 0 | ``SyntheticControl.obj``:`n  0 | S_LDATA32 [size = 16] `````n  type = <no type>, addr = 0001:$($nextData-$section)`n"
        }
    }
    if ($Fault -eq 'InteriorDataSymbol') {
        $symbolText += "  Mod 0 | ``SyntheticControl.obj``:`n  0 | S_LDATA32 [size = 16] `````n  type = <no type>, addr = 0001:$($table+4-$section)`n"
    }
    if ($Fault -eq 'AnonymousDataWrongType') { $symbolText = $symbolText.Replace('<no type>','T_NOTYPE (0x0000)') }
    if ($Fault -eq 'WrongTableStart') { $symbolText = $symbolText.Replace("addr = 0001:$($table-$section)","addr = 0001:$($table+4-$section)") }
    Set-Content -LiteralPath $seed.Symbols -Value $symbolText -Encoding utf8
    Set-Content -LiteralPath $seed.Disassembly -Value (@($rows | Sort-Object { [UInt64]::Parse(($_ -split ':',2)[0], [Globalization.NumberStyles]::HexNumber) }) -join "`n") -Encoding utf8
    [IO.File]::WriteAllBytes($seed.Binary, $pe)
    $case = @{Name=$Name; Binary=$seed.Binary; Metadata=$seed.Metadata; Symbols=$seed.Symbols; Disassembly=$seed.Disassembly}
    if ($Fault.StartsWith('R8Companion',[StringComparison]::Ordinal)) { Add-R8Companion $case $Fault $Maximum }
    return $case
}

function Add-R8Companion($Case,[string]$Fault,[int]$Maximum) {
    $pe=[IO.File]::ReadAllBytes($Case.Binary)
    [UInt32]$start=$script:sectionRva+0x760
    [UInt32]$r9=$start
    [UInt32]$guard=$start+7
    [UInt32]$table=$script:sectionRva+0x900+4*($Maximum+1)
    [UInt32]$target=$script:sectionRva+0x840
    [UInt32]$end=$table+4*($Maximum+1)
    [byte[]]$base9=@(0x4c,0x8d,0x0d,0,0,0,0)
    [Array]::Copy([BitConverter]::GetBytes(-[Int32]($r9+7)),0,$base9,3,4)
    [byte[]]$index=@(0x41,0x8d,0x87,0xff,0xfe,0xff,0xff)
    [byte[]]$cmp=@(0x83,0xf8,[byte]$Maximum)
    [byte[]]$ja=@(0x0f,0x87,0,0,0,0)
    Hd-SetU32 $ja 2 ($target-$guard-16)
    [byte[]]$base8=@(0x4c,0x8d,0x05,0,0,0,0)
    [Array]::Copy([BitConverter]::GetBytes(-[Int32]($guard+23)),0,$base8,3,4)
    [byte[]]$load=@(0x41,0x8b,0x94,0x80,0,0,0,0)
    Hd-SetU32 $load 4 $table
    [byte[]]$add=@(0x49,0x03,0xd0)
    [byte[]]$jump=@(0xff,0xe2)
    switch($Fault){
        'R8CompanionWrongBase' {$base8[3]=$base8[3]-bxor1}
        'R8CompanionWrongBaseRegister' {$base8[2]=0x0d}
        'R8CompanionWrongIndex' {$index[2]=0x86}
        'R8CompanionWrongGuard' {$ja[1]=0x86}
        'R8CompanionNegativeMaximum' {$cmp[2]=128}
        'R8CompanionZeroMaximum' {$cmp[2]=0}
        'R8CompanionWrongLoad' {$load[2]=0x8c}
        'R8CompanionWrongSib' {$load[3]=0x81}
        'R8CompanionWrongAdd' {$add[2]=0xc9}
        'R8CompanionWrongJump' {$jump[1]=0xe0}
        'R8CompanionUnaligned' {Hd-SetU32 $load 4 ($table+1)}
        'R8CompanionR9Clobber' {$base9[2]=0x05}
    }
    $parts=@(
        @{At=$r9;Bytes=$base9},@{At=$guard;Bytes=$index},@{At=$guard+7;Bytes=$cmp},
        @{At=$guard+10;Bytes=$ja},@{At=$guard+16;Bytes=$base8},@{At=$guard+23;Bytes=$load},
        @{At=$guard+31;Bytes=$add},@{At=$guard+34;Bytes=$jump}
    )
    $rows=[Collections.Generic.List[string]]::new()
    foreach($part in $parts){
        Hd-Put $pe (0x400+$part.At-$script:sectionRva) $part.Bytes
        Hd-AddRow $rows $part.At $part.Bytes 'literal companion instruction'
    }
    for([UInt32]$at=$guard+36;$at-lt$script:sectionRva+0x800;$at++){
        $pe[0x400+$at-$script:sectionRva]=0x90
        Hd-AddRow $rows $at ([byte[]]@(0x90)) 'nop'
    }
    if($Fault-eq'R8CompanionBypassGuard'){
        # An incoming direct branch to the DWORD load bypasses its CMP/JA and base definition.
        [UInt32]$at=$guard+36
        [byte[]]$branch=@(0xeb,[byte]0xe5)
        Hd-Put $pe (0x400+$at-$script:sectionRva) $branch
        $rows.RemoveAll([Predicate[string]]{param($row);$address=[UInt64]::Parse(($row-split':',2)[0],[Globalization.NumberStyles]::HexNumber);$address-ge$script:imageBase+$at-and$address-lt$script:imageBase+$at+2})|Out-Null
        Hd-AddRow $rows $at $branch 'jmp companion load'
    }
    foreach($row in Get-Content -LiteralPath $Case.Disassembly){$rows.Add($row)}
    for($i=0;$i-le$Maximum;$i++){
        [UInt32]$entry=if($Fault-eq'R8CompanionBadTarget'-and$i-eq$Maximum){$target+1}else{$target}
        Hd-SetU32 $pe (0x400+$table-$script:sectionRva+4*$i) $entry
        Hd-AddRow $rows ($table+4*$i) ([BitConverter]::GetBytes($entry)) 'opaque companion data'
    }
    $symbol=Get-Content -LiteralPath $Case.Symbols -Raw
    $symbol=$symbol.Replace("addr = 0001:$((0x800)), code size = $($table-$script:sectionRva-0x800)","addr = 0001:$($start-$script:sectionRva), code size = $($end-$start)")
    if(-not$symbol.Contains("addr = 0001:$($start-$script:sectionRva), code size")){throw 'Companion procedure replacement failed'}
    if($Fault-ne'R8CompanionMissingSymbol'){
        $owner=if($Fault-eq'R8CompanionWrongOwner'){1}else{0}
        $symbol+=("  Mod {0} | " -f $owner)+'`SyntheticControl.obj`:'+"`n"+
            '  0 | S_LDATA32 [size = 16] ``'+"`n  type = <no type>, addr = 0001:$($table-$script:sectionRva)`n"
    }
    Set-Content -LiteralPath $Case.Symbols -Value $symbol -Encoding utf8
    Set-Content -LiteralPath $Case.Disassembly -Value ($rows -join "`n") -Encoding utf8
    [IO.File]::WriteAllBytes($Case.Binary,$pe)
}

function Invoke-HoistedDirectControl([string]$Name, [string]$Fault, [int]$Maximum, [bool]$ExpectedPass, [int]$ExpectedInline = -1, [int]$ExpectedVectors = -1) {
    $case = New-HoistedDirectCase $Name $Fault $Maximum
    $result = [RedPrismBuild.IsaScanner]::Scan($case.Binary, $case.Metadata, $case.Symbols, $case.Disassembly,
        [string[]]@(), [string[]]@(), [string[]]@())
    if ($ExpectedPass -and $result.Violations.Count -ne 0) { throw "${Name}: unexpected ISA rejection: $($result.Violations -join '; ')" }
    if (-not $ExpectedPass -and $result.Violations.Count -eq 0) { throw "${Name}: malformed synthetic fixture was accepted." }
    if ($ExpectedInline -ge 0 -and $result.InlineTableVectorData -ne $ExpectedInline) { throw "${Name}: expected $ExpectedInline inline data rows, got $($result.InlineTableVectorData)." }
    if ($ExpectedVectors -ge 0 -and $result.VectorInstructions -ne $ExpectedVectors) { throw "${Name}: expected $ExpectedVectors vector instructions, got $($result.VectorInstructions)." }
    $script:hdCases.Add($Name)
    if ($ExpectedPass) { ++$script:hdAccepted }
    Write-Host "PASS isa-r9-hoisted-direct.$Name"
}

# Current observed maximum and both signed-immediate boundaries; each has the
# same body-boundary default and a single repeated in-procedure target.
Invoke-HoistedDirectControl 'CurrentMaximum14' '' 14 $true 15 0
Invoke-HoistedDirectControl 'MinimumMaximum1' '' 1 $true 2 0
Invoke-HoistedDirectControl 'Maximum127' '' 127 $true 128 0
Invoke-HoistedDirectControl 'DistinctDefaultTarget' 'DistinctDefaultTarget' 14 $true 14 0
Invoke-HoistedDirectControl 'DefaultAtBodyEnd' 'DefaultAtBodyEnd' 14 $true 14 0
Invoke-HoistedDirectControl 'TargetAtBodyEnd' 'TargetAtBodyEnd' 14 $true 14 0
Invoke-HoistedDirectControl 'MissingOpaqueDataRow' 'MissingTableRow' 14 $true 14 0
Invoke-HoistedDirectControl 'UnknownOpaqueDataRow' 'UnknownTableRow' 14 $true
Invoke-HoistedDirectControl 'OverlappingOpaqueDataRow' 'OverlapTableRow' 14 $true 15 0
Invoke-HoistedDirectControl 'GuardedBypassNever' 'GuardedBypassNever' 14 $true 15 0
Invoke-HoistedDirectControl 'R8Companion' 'R8Companion' 14 $true 15 0
foreach($fault in @('R8CompanionWrongBase','R8CompanionWrongBaseRegister','R8CompanionWrongIndex',
    'R8CompanionWrongGuard','R8CompanionNegativeMaximum','R8CompanionZeroMaximum','R8CompanionWrongLoad',
    'R8CompanionWrongSib','R8CompanionWrongAdd','R8CompanionWrongJump','R8CompanionUnaligned',
    'R8CompanionR9Clobber','R8CompanionBadTarget','R8CompanionMissingSymbol','R8CompanionWrongOwner',
    'R8CompanionBypassGuard')){Invoke-HoistedDirectControl $fault $fault 14 $false}

$bad = @(
    'WrongBias','WrongIndexRex','WrongIndexModRm','WrongIndexDisp','WrongCmpOpcode','WrongCmpRegister',
    'WrongJaOpcode','WrongJaWidth','WrongLoadRex','WrongLoadOpcode','WrongLoadDestination','WrongLoadSib',
    'WrongTableRva','WrongAddRex','WrongAddRegister','WrongJumpRegister','Range0','Range128','Range255',
    'WrongImageBase','WrongBaseBytes','AbsentBase','ClobberR9','ClobberR15','CallBetweenBaseAndGuard','BypassBasePath',
    'GuardedBypassAdmitted','BypassFlagClobber','IncomingGuardStart',
    'UnalignedTable','MissingProcedure','WrongOwner','MissingTableSymbol','WrongTableOwner','NamedTable',
    'UnknownBaseListing','ListingBase','OverlapBase',
    'AnonymousDataWrongType','WrongTableStart','ShortTableSymbol','LongTableSymbol','ShortProcedure','LongProcedure',
    'WrongProcedureEnd','InteriorDataSymbol','TargetOutside','TargetInterior','TargetGuard','TargetData','TargetUnknown',
    'DefaultInterior','DefaultGuard','DefaultData','DefaultOutside','DefaultUnknown',
    'IncomingGuardInterior','IncomingTable','ListingTableRow',
    'MissingBaseListing','WrongListingBase','ListingJaTarget'
)
foreach ($fault in $bad) { Invoke-HoistedDirectControl $fault $fault 14 $false }

# Every one of the six fixed guard rows must be present, nonoverlapping, known,
# and byte-identical to the PE. Include a shifted row for each instruction too.
foreach ($i in 0..5) {
    foreach ($kind in 'MissingGuard','UnknownGuard','ListingGuard','OverlapGuard','OffsetGuard') {
        $fault = "$kind$i"
        Invoke-HoistedDirectControl $fault $fault 14 $false
    }
}

# Genuine VEX rows remain violations around the recognized dispatch and data.
# A named next symbol cannot extend the anonymous table's exemption.
foreach ($fault in 'RealVexBeforeTarget','RealVexAtTarget','RealVexAfterTarget','RealVexBeforeData','RealVexAfterData','NamedNextDataVex') {
    Invoke-HoistedDirectControl $fault $fault 14 $false 15 1
}

Write-Host "SUMMARY isa-r9-hoisted-direct controls=$($hdCases.Count) accepted=$hdAccepted refused=$($hdCases.Count-$hdAccepted)"
