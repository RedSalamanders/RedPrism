[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
# Reuse the repository's PE/PDB builder and scanner setup. Its fixtures and all controls
# are synthetic parser inputs; this script never executes their bytes or launches a build.
. (Join-Path $repo 'Tests/BuildProcessTests/IsaInlineTableTests.ps1') *> $null
# Keep the helper's PE debug directory intact while placing the case target at RVA 0x9a2c4.
$script:sectionRva = [UInt32]0x0009a000
$script:dispatchRva = $script:sectionRva
$script:tableRva = [UInt32]($script:sectionRva + 0x1000)
$script:targetRva = [UInt32]($script:sectionRva + 0x8c5)

$r14Root = Join-Path $repo ('.build/BuildProcessTests/IsaR14LoopTable-' + [guid]::NewGuid().ToString('N'))
[void](New-Item -ItemType Directory -Path $r14Root -Force)
$script:r14Names = [Collections.Generic.List[string]]::new()
$script:r14Failures = [Collections.Generic.List[string]]::new()
$script:r14Accepted = 0

function R14-SetU32([byte[]]$Bytes, [int]$Offset, [UInt32]$Value) {
    [Array]::Copy([BitConverter]::GetBytes($Value), 0, $Bytes, $Offset, 4)
}

function R14-SetI32([byte[]]$Bytes, [int]$Offset, [Int32]$Value) {
    [Array]::Copy([BitConverter]::GetBytes($Value), 0, $Bytes, $Offset, 4)
}

function R14-Row([Collections.Generic.List[string]]$Rows, [UInt32]$Rva, [byte[]]$Bytes, [string]$Text) {
    $Rows.Add(('{0:x}: {1} {2}' -f ($script:imageBase + $Rva), (Hex-Bytes $Bytes), $Text))
}

function New-R14Case([string]$Name, [string]$Fault = '') {
    $seed = New-SyntheticCase ($Name + '-seed') -PdbData
    $pe = [IO.File]::ReadAllBytes($seed.Binary)
    [UInt32]$section = 0x9a000
    $base = if ($Fault -eq 'RelocatedImageBase') { [UInt64]0x180000000 } else { [UInt64]0x140000000 }
    $script:imageBase = $base
    Set-U64 $pe (0x98+24) $base
    Set-U32 $pe (0x98+56) ($section+0x2000)
    Set-U32 $pe (0x98+112+6*8) ($section+0x180)
    Set-U32 $pe (0x400+0x180+20) ($section+0x1a0)
    Set-U32 $pe (0x80+24+0xf0+12) $section
    # The two cases have RVAs ending C0/C4, matching the independent observed LLVM table decoding.
    [UInt32]$procedure = $section + 0x280
    [UInt32]$prefix = $procedure + 5
    [UInt32]$loopEnd = $prefix + 132
    [UInt32]$table = $procedure + 144
    [UInt32]$end = $table + 24
    $raw = 0x400 + [int]($procedure - $section)
    # Retain the helper's PE, RSDS identity and contribution; replace its code payload.
    for ($i = 0x200; $i -lt 0x2000; ++$i) { $pe[0x400 + $i] = 0xcc }

    $bodyRows = [Collections.Generic.List[object]]::new()
    $bodyRows.Add(@{ Offset=0; Bytes=[byte[]]@(0x33,0xc0); Text='xor eax, eax' })
    $bodyRows.Add(@{ Offset=2; Bytes=[byte[]]@(0x0f,0x1f,0x00); Text='nop dword ptr [rax]' })
    $loop = [Collections.Generic.List[object]]::new()
    $spec = @(
        @(0,  [byte[]]@(0x4c,0x8d,0x35,0,0,0,0), 'lea r14, [rip + imagebase]'),
        @(7,  [byte[]]@(0x44,0x8b,0xac,0x24,0x08,0x02,0,0), 'mov r13d, dword ptr [rsp + 208h]'),
        @(15, [byte[]]@(0x44,0x8b,0xa4,0x24,0x10,0x02,0,0), 'mov r12d, dword ptr [rsp + 210h]'),
        @(23, [byte[]]@(0x0f,0x1f,0x40,0), 'nop dword ptr [rax]'),
        @(27, [byte[]]@(0x83,0x3b,0x02), 'cmp dword ptr [rbx], 2'),
        @(30, [byte[]]@(0x75,0x58), 'jne loop.case.default'),
        @(32, [byte[]]@(0x44,0x39,0x63,0x34), 'cmp dword ptr [rbx+34h], r12d'),
        @(36, [byte[]]@(0x75,0x52), 'jne loop.case.default'),
        @(38, [byte[]]@(0x8b,0x43,0x18), 'mov eax, dword ptr [rbx+18h]'),
        @(41, [byte[]]@(0x83,0xf8,0x05), 'cmp eax, 5'),
        @(44, [byte[]]@(0x77,0x18), 'ja loop.case.default'),
        @(46, [byte[]]@(0x41,0x8b,0x8c,0x86,0,0,0,0), 'mov ecx, dword ptr [r14+4*rax+table]'),
        @(54, [byte[]]@(0x49,0x03,0xce), 'add rcx, r14'),
        @(57, [byte[]]@(0xff,0xe1), 'jmp rcx'),
        @(59, [byte[]]@(0x33,0xc0), 'xor eax, eax'),
        @(61, [byte[]]@(0xeb,0x0c), 'jmp loop.case.join'),
        @(63, [byte[]]@(0xb8,0x01,0,0,0), 'mov eax, 1'),
        @(68, [byte[]]@(0xeb,0x05), 'jmp loop.case.join'),
        @(70, [byte[]]@(0xb8,0x02,0,0,0), 'mov eax, 2'),
        @(75, [byte[]]@(0x41,0x3b,0xc5), 'cmp eax, r13d'),
        @(78, [byte[]]@(0x75,0x28), 'jne loop.repeat.exit'),
        @(80, [byte[]]@(0x48,0x85,0xff), 'test rdi, rdi'),
        @(83, [byte[]]@(0x74,0x0a), 'je loop.repeat.exit'),
        @(85, [byte[]]@(0x48,0x8b,0x47,0x10), 'mov rax, qword ptr [rdi+10h]'),
        @(89, [byte[]]@(0x48,0x39,0x43,0x10), 'cmp qword ptr [rbx+10h], rax'),
        @(93, [byte[]]@(0x73,0x19), 'jae loop.repeat.exit'),
        @(95, [byte[]]@(0x48,0x8b,0xd3), 'mov rdx, rbx'),
        @(98, [byte[]]@(0x48,0x8b,0xce), 'mov rcx, rsi'),
        @(101,[byte[]]@(0xe8,0,0,0,0), 'call external.body'),
        @(106,[byte[]]@(0x84,0xc0), 'test al, al'),
        @(108,[byte[]]@(0x48,0x0f,0x44,0xfb), 'cmove rdi, rbx'),
        @(112,[byte[]]@(0x48,0x89,0xbc,0x24,0x20,0x01,0,0), 'mov qword ptr [rsp+120h], rdi'),
        @(120,[byte[]]@(0x48,0x81,0xc3,0xd0,0,0,0), 'add rbx, 0d0h'),
        @(127,[byte[]]@(0x49,0x3b,0xdf), 'cmp rbx, r15'),
        @(130,[byte[]]@(0x75,0x97), 'jne loop.repeat')
    )
    foreach ($entry in $spec) {
        $loop.Add(@{Offset=[int]$entry[0]; Bytes=[byte[]]$entry[1].Clone(); Text=[string]$entry[2]})
    }

    # Relocations required by the synthetic image. The retained observed operands remain exact.
    [byte[]]$lea = $loop[0].Bytes
    R14-SetI32 $lea 3 ([Int32](-[Int64]($prefix + 7)))
    [byte[]]$load = $loop[11].Bytes
    R14-SetU32 $load 4 $table
    [byte[]]$call = $loop[28].Bytes
    [UInt32]$externalTarget = $section + 0x700
    R14-SetI32 $call 1 ([Int32]([Int64]$externalTarget - [Int64]($prefix + 106)))

    switch ($Fault) {
        'WrongBase' { $loop[0].Bytes[2] = 0x2d }
        'WrongProducer' { $loop[8].Bytes[1] = 0x4b }
        'WrongIndex' { $loop[11].Bytes[3] = 0x8e }
        'WrongWidth' { $loop[11].Bytes[1] = 0x89 }
        'WrongGuard' { $loop[9].Bytes[2] = 0x06 }
        'WrongLoad' { $loop[11].Bytes[2] = 0x84 }
        'WrongAdd' { $loop[12].Bytes[2] = 0xc6 }
        'WrongJump' { $loop[13].Bytes[1] = 0xe3 }
        'WrongPrelude' { $loop[1].Bytes[0] = 0x91 }
        'WrongCase' { $loop[16].Bytes[1] = 0x02 }
        'WrongLoopTransfer' { $loop[34].Bytes[1] = 0x00 }
        'CallInsideProcedure' { R14-SetI32 $loop[28].Bytes 1 ([Int32]([Int64]($prefix + 27) - [Int64]($prefix + 106))) }
        'WrongTerminalJump' { }
        'WrongPadding' { }
        'ExtraIndirectJump' { $loop[14].Bytes = [byte[]]@(0xff,0xe0) }
    }

    foreach ($row in $bodyRows) { [Array]::Copy($row.Bytes, 0, $pe, 0x400 + [int]($procedure - $section) + $row.Offset, $row.Bytes.Length) }
    foreach ($row in $loop) { [Array]::Copy($row.Bytes, 0, $pe, 0x400 + [int]($prefix - $section) + $row.Offset, $row.Bytes.Length) }

    # Exact tail: a near jump to the known procedure entry, then the observed 66 90 NOP.
    [byte[]]$terminal = [byte[]]@(0xe9,0,0,0,0)
    R14-SetI32 $terminal 1 ([Int32]([Int64]$procedure - [Int64]($table - 2)))
    if ($Fault -eq 'WrongTerminalJump') { $terminal[0] = 0xe8 }
    [Array]::Copy($terminal, 0, $pe, 0x400 + [int]($table - 7 - $section), 5)
    [byte[]]$padding = if ($Fault -eq 'WrongPadding') { [byte[]]@(0x66,0x91) } else { [byte[]]@(0x66,0x90) }
    [Array]::Copy($padding, 0, $pe, 0x400 + [int]($table - 2 - $section), 2)

    [UInt32[]]$targets = @(($prefix+59),($prefix+59),($prefix+59),($prefix+59),($prefix+63),($prefix+63))
    switch ($Fault) {
        'TargetInsideInstruction' { $targets[0] = $prefix + 60 }
        'TargetOutside' { $targets[0] = $section + 0x700 }
        'TargetPrefix' { $targets[0] = $prefix }
        'TargetDefault' { $targets[0] = $prefix + 70 }
    }
    for ($i=0; $i -lt 6; ++$i) { R14-SetU32 $pe (0x400 + [int]($table-$section) + 4*$i) $targets[$i] }

    # A direct entry from elsewhere in this procedure must not bypass the exact producer/guard.
    $outsideFault = $Fault -in @('ExternalBranchInterior','ExternalBranchTable','ExternalBranchPadding',
        'ExternalCallInterior','ExternalLoopInterior','ExternalPrefixedInterior','OutsideIndirectJump','OutsideFarCall')
    $extraRows = [Collections.Generic.List[string]]::new()
    if ($outsideFault) {
        [UInt32]$source = $procedure
        [UInt32]$destination = switch ($Fault) {
            'ExternalBranchTable' { $table }
            'ExternalBranchPadding' { $table-1 }
            default { $prefix+27 }
        }
        [byte[]]$edge = switch ($Fault) {
            'ExternalCallInterior' { [byte[]]@(0xe8,0,0,0,0) }
            'ExternalLoopInterior' { [byte[]]@(0xe2,0) }
            'ExternalPrefixedInterior' { [byte[]]@(0x67,0xe3,0) }
            'OutsideIndirectJump' { [byte[]]@(0xff,0xe0) }
            'OutsideFarCall' { [byte[]]@(0xff,0x18) }
            default { [byte[]]@(0xe9,0,0,0,0) }
        }
        if ($edge.Length -eq 5) { R14-SetI32 $edge 1 ([Int32]([Int64]$destination-[Int64]($source+5))) }
        elseif ($Fault -notin @('OutsideIndirectJump','OutsideFarCall')) { $edge[$edge.Length-1] = [byte]([Int64]$destination-[Int64]($source+$edge.Length)) }
        $bodyRows.Clear()
        $bodyRows.Add(@{Offset=0;Bytes=$edge;Text='outside entry control'})
        for($i=$edge.Length;$i -lt 5;++$i){$bodyRows.Add(@{Offset=$i;Bytes=[byte[]]@(0x90);Text='nop'})}
        foreach($row in $bodyRows){[Array]::Copy($row.Bytes,0,$pe,0x400+[int]($procedure-$section)+$row.Offset,$row.Bytes.Length)}
    }
    if ($Fault -eq 'AdjacentVex') {
        [byte[]]$vex = [byte[]]@(0xc5,0xf8,0x10,0x00)
        [Array]::Copy($vex,0,$pe,0x400+[int]($end+4-$section),4)
    } elseif ($Fault -eq 'AdjacentEvex') {
        [byte[]]$vex = [byte[]]@(0x62,0xf1,0x7c,0x48,0x10,0x00)
        [Array]::Copy($vex,0,$pe,0x400+[int]($end+4-$section),6)
    }

    $symbols = "  Mod 0 | ``SyntheticR14.obj``:`n" +
        "  0 | S_GPROC32 [size = 16] ``R14LoopControl`` `n  addr = 0001:$($procedure-$section), code size = $($end-$procedure)`n"
    $dataKind = if ($Fault -eq 'TypedTable') { 'T_NOTYPE (0x0000)' } else { '<no type>' }
    $dataOwner = if ($Fault -eq 'ForeignTable') { 1 } else { 0 }
    $dataStart = if ($Fault -eq 'WrongTableStart') { $table+4 } else { $table }
    if ($Fault -ne 'MissingTableSymbol') {
        $dataName = if ($Fault -eq 'NamedTable') { 'dispatch' } else { '' }
        $encoded = if ($dataName) { '``'+$dataName+'``' } else { '``' }
        $symbols += "  Mod $dataOwner | ``SyntheticR14.obj``:`n  0 | S_LDATA32 [size = 16] $encoded`n  type = $dataKind, addr = 0001:$($dataStart-$section)`n"
    }
    if ($Fault -eq 'InteriorSymbol') {
        $symbols += "  Mod 0 | ``SyntheticR14.obj``:`n  0 | S_LDATA32 [size = 16] `````n  type = <no type>, addr = 0001:$($table+4-$section)`n"
    }
    if ($Fault -eq 'ShortProcedure') { $symbols = $symbols.Replace("code size = $($end-$procedure)","code size = $($end-$procedure-4)") }
    if ($Fault -eq 'LongProcedure') { $symbols = $symbols.Replace("code size = $($end-$procedure)","code size = $($end-$procedure+4)") }
    if ($Fault -eq 'MissingProcedure') { $symbols = '' }
    if ($Fault -eq 'WrongProcedureOwner') { $symbols = $symbols.Replace('Mod 0 |','Mod 1 |') }

    $rows = [Collections.Generic.List[string]]::new()
    foreach ($row in $bodyRows) { R14-Row $rows ($procedure+$row.Offset) $row.Bytes $row.Text }
    foreach ($row in $loop) { R14-Row $rows ($prefix+$row.Offset) $row.Bytes $row.Text }
    R14-Row $rows ($table-7) $terminal 'jmp procedure.entry'
    R14-Row $rows ($table-2) $padding 'xchg ax, ax'
    # Independently observed LLVM boundaries for C0/C4 case RVAs: 7,2,9,2,5 bytes.
    # The final VPSHUFB begins in the last DWORD and consumes exactly one CC padding byte.
    $tableOffsets=@(0,7,9,18,20);$tableLengths=@(7,2,9,2,5)
    for($i=0;$i -lt 5;++$i){
        [byte[]]$bytes=[byte[]]::new($tableLengths[$i])
        [Array]::Copy($pe,0x400+[int]($table-$section)+$tableOffsets[$i],$bytes,0,$bytes.Length)
        if($Fault -eq "MissingTableRow$i"){continue}
        if($Fault -eq "MismatchTableRow$i"){$bytes[0]=$bytes[0] -bxor 1}
        $text=if($Fault -eq "UnknownTableRow$i"){'<unknown>'}elseif($i -eq 4){'vpshufb xmm1, xmm14, xmm4'}else{'scalar table data'}
        if($Fault -eq "OverlapTableRow$i"){R14-Row $rows ($table+$tableOffsets[$i]+1) ([byte[]]@(0x90)) 'nop'}
        if($Fault -eq "DuplicateTableRow$i"){R14-Row $rows ($table+$tableOffsets[$i]) $bytes $text}
        R14-Row $rows ($table+$tableOffsets[$i]) $bytes $text
        if($Fault -eq "CorruptPeTableRow$i"){$pe[0x400+[int]($table-$section)+$tableOffsets[$i]]=$pe[0x400+[int]($table-$section)+$tableOffsets[$i]] -bxor 1}
    }
    if ($Fault -eq 'MissingBodyRow') { $rows.RemoveAt(1) }
    if ($Fault -eq 'UnknownBodyRow') { $rows[1] = $rows[1].Replace('nop dword ptr [rax]','<unknown>') }
    if ($Fault -eq 'MismatchBodyRow') { $rows[1] = $rows[1].Replace('0f 1f 00','0f 1f 01') }
    if ($Fault -eq 'BodyGap') { $rows[1] = $rows[1].Replace(('{0:x}:' -f ($script:imageBase+$procedure+2)),('{0:x}:' -f ($script:imageBase+$procedure+3))) }
    if ($Fault -eq 'MissingLoopRow') { $rows.RemoveAt(6) }
    if ($Fault -eq 'UnknownLoopRow') { $rows[6] = $rows[6].Replace('cmp dword ptr [rbx], 2','<unknown>') }
    if ($Fault -eq 'MismatchLoopRow') { $rows[6] = $rows[6].Replace('83 3b 02','83 3b 03') }
    if ($Fault -eq 'CorruptPeLoopRow') { $pe[0x400+[int]($prefix-$section)+27] = $pe[0x400+[int]($prefix-$section)+27] -bxor 1 }
    foreach ($line in $extraRows) { $rows.Add($line) }

    if($Fault -eq 'NonCcSuffix'){$pe[0x400+[int]($end-$section)]=0x90;$rows[$rows.Count-1]=$rows[$rows.Count-1].Replace('00 cc','00 90')}
    if ($Fault -eq 'ListingCorruptTable') { $rows[39] = $rows[39].Replace('c0','c1') }
    if ($Fault -eq 'AdjacentVex') { R14-Row $rows ($end+4) ([byte[]]@(0xc5,0xf8,0x10,0x00)) 'vmovups xmm0, xmmword ptr [rax]' }
    if ($Fault -eq 'AdjacentEvex') { R14-Row $rows ($end+4) ([byte[]]@(0x62,0xf1,0x7c,0x48,0x10,0x00)) 'vmovups zmm0, zmmword ptr [rax]' }

    if ($Fault -eq 'MissingContribution') {
        $metadataText = Get-Content -LiteralPath $seed.Metadata -Raw
        $metadataText = [regex]::Replace($metadataText, '(?m)^\s*SC\[.*\r?\n', '')
        Set-Content -LiteralPath $seed.Metadata -Value $metadataText -Encoding utf8
    }
    if ($Fault -eq 'WrongContributionOwner') {
        $metadataText = Get-Content -LiteralPath $seed.Metadata -Raw
        $metadataText = $metadataText.Replace('mod = 0, 0001:0', 'mod = 1, 0001:0')
        Set-Content -LiteralPath $seed.Metadata -Value $metadataText -Encoding utf8
    }
    if ($Fault -eq 'WrongContributionExtent') {
        $metadataText = Get-Content -LiteralPath $seed.Metadata -Raw
        $metadataText = $metadataText.Replace('size = 8192', 'size = 512')
        Set-Content -LiteralPath $seed.Metadata -Value $metadataText -Encoding utf8
    }

    Set-Content -LiteralPath $seed.Symbols -Value $symbols -Encoding utf8
    $sorted = @($rows | Sort-Object { [UInt64]::Parse(($_ -split ':',2)[0], [Globalization.NumberStyles]::HexNumber) })
    Set-Content -LiteralPath $seed.Disassembly -Value ($sorted -join "`n") -Encoding utf8
    [IO.File]::WriteAllBytes($seed.Binary,$pe)
    $script:imageBase = [UInt64]0x140000000
    return $seed
}

function Invoke-R14Control([string]$Name, [string]$Fault, [bool]$ExpectedPass, [int]$ExpectedInline = -1, [int]$ExpectedVectors = -1) {
    $case = New-R14Case $Name $Fault
    try {
        $result = [RedPrismBuild.IsaScanner]::Scan($case.Binary,$case.Metadata,$case.Symbols,$case.Disassembly,
            [string[]]@(),[string[]]@(),[string[]]@())
    } catch {
        $expectedError = switch ($Fault) {
            'MissingContribution' { 'Missing PDB module/contribution data.' }
            'WrongContributionOwner' { 'Contribution refers to an absent module.' }
            default { '' }
        }
        if (-not $expectedError -or $_.Exception.InnerException -isnot [IO.InvalidDataException] -or
            $_.Exception.InnerException.Message -cne $expectedError) { throw }
        $script:r14Names.Add($Name)
        Write-Host "PASS isa-r14-loop.$Name (exact malformed-PDB refusal)"
        return
    }
    $problem = $null
    if ($ExpectedPass -and $result.Violations.Count -ne 0) { $problem = "unexpected violations: $($result.Violations -join '; ')" }
    elseif (-not $ExpectedPass -and $result.Violations.Count -eq 0 -and $result.InlineTableVectorData -ne 0) { $problem = 'malformed/vector table control was accepted' }
    elseif ($ExpectedInline -ge 0 -and $result.InlineTableVectorData -ne $ExpectedInline) { $problem = "expected inline=$ExpectedInline, got $($result.InlineTableVectorData)" }
    elseif ($ExpectedVectors -ge 0 -and $result.VectorInstructions -ne $ExpectedVectors) { $problem = "expected vectors=$ExpectedVectors, got $($result.VectorInstructions)" }
    $script:r14Names.Add($Name)
    if ($null -eq $problem) {
        if ($ExpectedPass) { ++$script:r14Accepted }
        Write-Host "PASS isa-r14-loop.$Name"
    } else {
        $script:r14Failures.Add("$Name`: $problem")
        Write-Host "FAIL isa-r14-loop.$Name - $problem"
    }
}

# Accepted controls retain the vector-shaped C4 table DWORD; relocation and the terminal
# CC suffix exercise the real data/listing checks without executing any synthetic bytes.
Invoke-R14Control 'ObservedLoopAndCcSuffix' '' $true 1 0
Invoke-R14Control 'RelocatedImageBase' 'RelocatedImageBase' $true 1 0
Invoke-R14Control 'AdjacentRealVex' 'AdjacentVex' $false 1 1
Invoke-R14Control 'AdjacentRealEvex' 'AdjacentEvex' $false 1 1

$refused = @(
    'WrongBase','WrongProducer','WrongIndex','WrongWidth','WrongGuard','WrongLoad','WrongAdd','WrongJump',
    'WrongPrelude','WrongCase','WrongLoopTransfer','NonCcSuffix','CallInsideProcedure','WrongTerminalJump','WrongPadding',
    'ExtraIndirectJump','TargetInsideInstruction','TargetOutside','TargetPrefix','TargetDefault',
    'NamedTable','TypedTable','ForeignTable','MissingTableSymbol','WrongTableStart','InteriorSymbol',
    'ShortProcedure','LongProcedure','MissingProcedure','WrongProcedureOwner','MissingContribution',
    'WrongContributionOwner','WrongContributionExtent','MissingBodyRow','UnknownBodyRow','MismatchBodyRow','BodyGap',
    'MissingLoopRow','UnknownLoopRow','MismatchLoopRow','CorruptPeLoopRow','ListingCorruptTable'
)
foreach ($fault in $refused) { Invoke-R14Control $fault $fault $false }
foreach ($i in 0..4) {
    foreach ($kind in 'MissingTableRow','MismatchTableRow','UnknownTableRow','OverlapTableRow','DuplicateTableRow','CorruptPeTableRow') {
        $fault = "$kind$i"
        Invoke-R14Control $fault $fault $false
    }
}

# Transfers from elsewhere in this procedure cannot enter the protected loop, padding or table.
foreach ($fault in @('ExternalBranchInterior','ExternalBranchTable','ExternalBranchPadding',
    'ExternalCallInterior','ExternalLoopInterior','ExternalPrefixedInterior','OutsideIndirectJump','OutsideFarCall')) {
    Invoke-R14Control $fault $fault $false
}

Write-Host "SUMMARY isa-r14-loop-table controls=$($script:r14Names.Count) accepted=$script:r14Accepted refused=$($script:r14Names.Count-$script:r14Accepted) failures=$($script:r14Failures.Count)"
if ($script:r14Failures.Count -gt 0) { throw ($script:r14Failures -join "`n") }
$global:LASTEXITCODE = 0
