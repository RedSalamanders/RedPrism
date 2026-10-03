[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Parser controls for the observed R10 AL guard. Dot-sourcing the existing helper follows
# IsaAlGuardTests.ps1: the established inline-table controls run when this sidecar is invoked.
$repo = (Get-Item -LiteralPath $PSScriptRoot).FullName
while (-not (Test-Path -LiteralPath (Join-Path $repo 'Build/IsaScan.cs'))) {
    $repo = Split-Path -Parent $repo
    if ([string]::IsNullOrEmpty($repo)) { throw 'Could not resolve repository root.' }
}
. (Join-Path $repo 'Tests/BuildProcessTests/IsaInlineTableTests.ps1')

$r10FixtureRoot = Join-Path $repo ('.build/BuildProcessTests/IsaR10AlGuard-' + [guid]::NewGuid().ToString('N'))
[void](New-Item -ItemType Directory -Path $r10FixtureRoot -Force)
$r10ImageBase = [UInt64]0x140000000
$r10Section = [UInt32]0x0010f000
$r10Lea = [UInt32]($r10Section + 0x84f)
$r10Guard = [UInt32]($r10Lea + 90)
$r10Table = [UInt32]($r10Section + 0x1000)
$r10Body = [UInt32]($r10Guard + 28)
$r10Guid = $script:guid
$r10Age = $script:age

function New-R10AlGuardCase {
    param(
        [Parameter(Mandatory)] [string]$Name,
        [ValidateSet('Pass','Reject')] [string]$Expectation,
        [string]$Fault = '',
        [ValidateRange(1,127)] [int]$Maximum = 6,
        [ValidateSet('Mixed','AllSame')] [string]$Targets = 'Mixed',
        [ValidateSet('CallCcNop','RetNop')] [string]$Tail = 'CallCcNop'
    )

    $directory = Join-Path $r10FixtureRoot $Name
    [void](New-Item -ItemType Directory -Path $directory -Force)
    $binary = Join-Path $directory 'SyntheticIsaParserControl.exe'
    $metadata = Join-Path $directory 'contributions.txt'
    $symbols = Join-Path $directory 'symbols.txt'
    $disassembly = Join-Path $directory 'disassembly.txt'

    # Seed the exact PE32+ / CodeView shape through the existing test helper.
    $seed = New-SyntheticCase ($Name + 'Seed') -PdbData
    [byte[]]$pe = [IO.File]::ReadAllBytes($seed.Binary)
    $rawBase = [int](0x400 + $r10Section - $script:sectionRva)
    $entryCount = $Maximum + 1
    [UInt32]$tableEnd = [UInt32]($r10Table + 4 * $entryCount)

    # Prefix layout is fixed: 4c8d15 rel32; 0fb683 disp32; 3c imm8;
    # 0f87 rel32; 41 8b8482 tableRVA; 49 03c2; ff e0.
    [byte[]]$lea = @(0x4c,0x8d,0x15,0,0,0,0)
    [Int32]$leaDisp = [Int32]([Int64]$r10ImageBase - [Int64]($r10ImageBase + $r10Lea + 7))
    if ($Fault -eq 'ImageBase') { ++$leaDisp }
    Put-Bytes $lea 3 ([BitConverter]::GetBytes($leaDisp))
    [byte[]]$loadAl = @(0x0f,0xb6,0x83,0x20,0,0,0)
    [byte[]]$compare = @(0x3c,[byte]$Maximum)
    [byte[]]$guardBranch = @(0x0f,0x87,0,0,0,0)
    [byte[]]$tableLoad = @(0x41,0x8b,0x84,0x82,0,0,0,0)
    Put-Bytes $tableLoad 4 ([BitConverter]::GetBytes([UInt32]$r10Table))
    [byte[]]$add = @(0x49,0x03,0xc2)
    [byte[]]$jump = @(0xff,0xe0)

    switch ($Fault) {
        'GuardLoad' { $loadAl[1] = 0xbe }
        'GuardCompare' { $compare[0] = 0x38 }
        'GuardBranch' { $guardBranch[1] = 0x86 }
        'TableLoad' { $tableLoad[1] = 0x89 }
        'Add' { $add[1] = 0x01 }
        'Jump' { $jump[1] = 0xe1 }
        'TableAddress' { $tableLoad[4]++ }
    }

    # The observed nonclobbering prelude totals 83 bytes. Its conditional branches
    # and short jump land on decoded prelude boundaries or the body immediately after the guard.
    $script:prelude = [Collections.Generic.List[byte]]::new()
    $script:preludeRows = [Collections.Generic.List[object]]::new()
    $prelude = $script:prelude
    $preludeRows = $script:preludeRows
    function Add-R10Instruction([byte[]]$Bytes, [string]$Text) {
        $at = [UInt32]($script:r10Lea + 7 + $script:prelude.Count)
        $script:preludeRows.Add(@{ Rva=$at; Bytes=$Bytes; Text=$Text })
        $script:prelude.AddRange($Bytes)
    }
    [byte[]]$leaR9 = @(0x4c,0x8d,0x8b,0,0,0,0)
    [byte[]]$leaR8 = @(0x4c,0x8d,0x83,0,0,0,0)
    [byte[]]$leaRdx = @(0x48,0x8d,0x93,0,0,0,0)
    [byte[]]$cmpRbx = @(0x80,0xbb,0,0,0,0,0)
    [byte[]]$jeNear = @(0x0f,0x84,0,0,0,0)
    [byte[]]$testRdi = @(0x48,0x85,0xff)
    [byte[]]$jeShort = @(0x74,0)
    [byte[]]$cmpR15 = @(0x41,0x80,0x3f,0)
    [byte[]]$jneShort = @(0x75,0)
    [byte[]]$movR13Rdi = @(0x4c,0x8b,0xef)
    [byte[]]$leaRsi = @(0x48,0x8d,0xb3,0,0,0,0)
    [byte[]]$leaR14 = @(0x4c,0x8d,0xb3,0,0,0,0)
    [byte[]]$jmpShort = @(0xeb,0)
    [byte[]]$movR13R11 = @(0x4d,0x8b,0xeb)
    [byte[]]$movEsi = @(0xbe,1,0,0,0)
    [byte[]]$movR14d = @(0x41,0xbe,2,0,0,0)
    [byte[]]$storeR14 = @(0x4c,0x89,0x74,0x24,0x20)
    if ($Fault -eq 'PreludeClobber') { $movR13Rdi[2] = 0xd7 } # MOV R10,RDI must invalidate the base proof.
    Add-R10Instruction $leaR9 'lea r9, [rbx+disp32]'
    Add-R10Instruction $leaR8 'lea r8, [rbx+disp32]'
    Add-R10Instruction $leaRdx 'lea rdx, [rbx+disp32]'
    Add-R10Instruction $cmpRbx 'cmp byte ptr [rbx+disp32], 0'
    Add-R10Instruction $jeNear 'je body'
    Add-R10Instruction $testRdi 'test rdi, rdi'
    Add-R10Instruction $jeShort 'je body'
    Add-R10Instruction $cmpR15 'cmp byte ptr [r15], 0'
    Add-R10Instruction $jneShort 'jne body'
    Add-R10Instruction $movR13Rdi 'mov r13, rdi'
    Add-R10Instruction $leaRsi 'lea rsi, [rbx+disp32]'
    Add-R10Instruction $leaR14 'lea r14, [rbx+disp32]'
    Add-R10Instruction $jmpShort 'jmp body'
    Add-R10Instruction $movR13R11 'mov r13, r11'
    Add-R10Instruction $movEsi 'mov esi, 1'
    Add-R10Instruction $movR14d 'mov r14d, 2'
    Add-R10Instruction $storeR14 'mov qword ptr [rsp+20h], r14'
    if ($prelude.Count -ne 83) { throw "Observed R10 prelude encoded as $($prelude.Count) bytes, expected 83." }

    # Displacements target the first decoded body row, outside the protected 28-byte prefix.
    $bodyTarget = [UInt32]$r10Body
    $branchOffsets = @(
        @{ Index=4; End=6; Bytes=$jeNear; Kind='near' },
        @{ Index=6; End=2; Bytes=$jeShort; Kind='short' },
        @{ Index=8; End=2; Bytes=$jneShort; Kind='short' },
        @{ Index=12; End=2; Bytes=$jmpShort; Kind='short' }
    )
    foreach ($branch in $branchOffsets) {
        $branchRva = [UInt32]$preludeRows[$branch.Index].Rva
        $disp = [Int64]$bodyTarget - [Int64]($branchRva + $branch.End)
        if ($branch.Kind -eq 'near') { Put-Bytes $branch.Bytes 2 ([BitConverter]::GetBytes([Int32]$disp)) }
        else { $branch.Bytes[1] = [byte]$disp }
    }
    if ($Fault -eq 'PreludeBranch') { $jeShort[1] = 1 }

    $branchEnd = [UInt32]($r10Guard + 15)
    [Int32]$guardDisp = [Int32]([Int64]$bodyTarget - [Int64]$branchEnd)
    Put-Bytes $guardBranch 2 ([BitConverter]::GetBytes($guardDisp))
    if ($Fault -eq 'DefaultNotInEntries') { Put-Bytes $guardBranch 2 ([BitConverter]::GetBytes([Int32]($bodyTarget+0x200-$branchEnd))) }
    if ($Fault -eq 'MaximumZero') { $compare[1] = 0 }
    if ($Fault -eq 'Maximum128') { $compare[1] = 128 }
    if ($Fault -eq 'MaximumTooSmall') { $compare[1]-- }
    if ($Fault -eq 'MaximumTooLarge') { $compare[1]++ }
    if ($Fault -eq 'ImageBase') {
        # Keep the encoding malformed even though its rel32 was recomputed above.
        $lea[0] = 0x48
    }

    Put-Bytes $pe ($rawBase + [int]($r10Lea-$r10Section)) $lea
    foreach ($row in $preludeRows) { Put-Bytes $pe ($rawBase + [int]($row.Rva-$r10Section)) $row.Bytes }
    $prefixBytes = @($loadAl,$compare,$guardBranch,$tableLoad,$add,$jump)
    $prefixOffsets = @(0,7,9,15,23,26)
    $prefixNames = @('GuardLoad','GuardCompare','GuardBranch','TableLoad','Add','Jump')
    for ($i=0; $i -lt $prefixBytes.Count; ++$i) {
        Put-Bytes $pe ($rawBase + [int]($r10Guard-$r10Section) + $prefixOffsets[$i]) $prefixBytes[$i]
    }

    # Three scalar NOP body rows have RVA bytes beginning c5 f8/f9/fa when encoded in the DWORD table.
    # They all follow the jump, remain before the table, and are represented identically in PE/PDB metadata.
    [UInt32]$bodySecond = [UInt32]($bodyTarget + 0x100)
    [UInt32]$bodyThird = [UInt32]($bodyTarget + 0x200)
    [byte[]]$vexRow0 = @(0x0f,0x1f,0x40,0x00)
    [byte[]]$vexRow1 = @(0x0f,0x1f,0x40,0x00)
    [byte[]]$vexRow2 = @(0x0f,0x1f,0x40,0x00)
    Put-Bytes $pe ($rawBase + [int]($bodyTarget-$r10Section)) $vexRow0
    Put-Bytes $pe ($rawBase + [int]($bodySecond-$r10Section)) $vexRow1
    Put-Bytes $pe ($rawBase + [int]($bodyThird-$r10Section)) $vexRow2
    $entryTargets = if ($Targets -eq 'AllSame') { @($bodyTarget) * $entryCount } else { @($bodyTarget,$bodySecond,$bodyThird) }
    for ($i=0; $i -lt $entryCount; ++$i) {
        [UInt32]$target = [UInt32]$entryTargets[$i % $entryTargets.Count]
        if ($Fault -eq 'TableEntry' -and $i -eq ($entryCount-1)) { $target = [UInt32]($bodyTarget + 1) }
        if ($Fault -eq 'BackwardTarget' -and $i -eq 0) { $target = [UInt32]($r10Lea-0x20) }
        Set-U32 $pe ($rawBase + [int]($r10Table-$r10Section) + 4*$i) $target
    }

    $tailBytes = if ($Tail -eq 'RetNop') { [byte[]]@(0xc3,0x66,0x90) } else { [byte[]]@(0xc3,0xe8,0,0,0,0,0xcc,0x66,0x90) }
        [UInt32]$tailAt = $r10Table - $tailBytes.Length
    if ($Tail -ne 'RetNop') { Put-Bytes $tailBytes 2 ([BitConverter]::GetBytes([Int32]($bodyTarget-($tailAt+6)))) }
    Put-Bytes $pe ($rawBase + [int]($tailAt-$r10Section)) $tailBytes
    if ($Fault -eq 'MissingReturn') { $pe[$rawBase+[int]($tailAt-$r10Section)] = 0x90 }

    $rows = [Collections.Generic.List[string]]::new()
    $rows.Add(('{0:x}: {1} lea r10, [rip+imagebase]' -f ($r10ImageBase+$r10Lea),(Hex-Bytes $lea)))
    foreach ($row in $preludeRows) {
        [byte[]]$shown = ([byte[]]$row.Bytes).Clone()
        if ($Fault -eq 'ListingMismatchPrelude' -and $row.Rva -eq $preludeRows[0].Rva) { $shown[0] = $shown[0] -bxor 1 }
        $rows.Add(('{0:x}: {1} {2}' -f ($r10ImageBase+$row.Rva),(Hex-Bytes $shown),$row.Text))
    }
    for ($i=0; $i -lt $prefixBytes.Count; ++$i) {
        if ($Fault -eq 'MissingPEListing' -and $prefixNames[$i] -eq 'TableLoad') { continue }
        [byte[]]$shown = $prefixBytes[$i]
        if ($Fault -eq "ListingMismatch$($prefixNames[$i])") { $shown = $shown.Clone(); $shown[0] = $shown[0] -bxor 1 }
        $rows.Add(('{0:x}: {1} synthetic-guard' -f ($r10ImageBase+$r10Guard+$prefixOffsets[$i]),(Hex-Bytes $shown)))
        if ($Fault -eq 'Overlap' -and $prefixNames[$i] -eq 'TableLoad') {
            $rows.Add(('{0:x}: 90 overlap' -f ($r10ImageBase+$r10Guard+$prefixOffsets[$i]+1)))
        }
    }
    $rows.Add(('{0:x}: 0f 1f 40 00 nop dword ptr [rax]' -f ($r10ImageBase+$bodyTarget)))
    $rows.Add(('{0:x}: 0f 1f 40 00 nop dword ptr [rax]' -f ($r10ImageBase+$bodySecond)))
    $rows.Add(('{0:x}: 0f 1f 40 00 nop dword ptr [rax]' -f ($r10ImageBase+$bodyThird)))

    # These DWORD bytes intentionally decode as VEX-like instruction starts; the whole table is data.
    for ($i=0; $i -lt $entryCount; ++$i) {
        $at = [UInt32]($r10Table + 4*$i)
        [byte[]]$dword = $pe[($rawBase+[int]($at-$r10Section))..($rawBase+[int]($at-$r10Section)+3)]
        $rows.Add(('{0:x}: {1} vmovups synthetic-table-bytes' -f ($r10ImageBase+$at),(Hex-Bytes $dword)))
    }
        $rows.Add(('{0:x}: c3 ret' -f ($r10ImageBase+$tailAt)))
    if ($Tail -ne 'RetNop') {
        $rows.Add(('{0:x}: {1} call body' -f ($r10ImageBase+$tailAt+1),(Hex-Bytes ([byte[]]$tailBytes[1..5]))))
        $rows.Add(('{0:x}: cc int3' -f ($r10ImageBase+$tailAt+6)))
    }
    $rows.Add(('{0:x}: 66 90 nop' -f ($r10ImageBase+$r10Table-2)))

    if ($Fault -in 'BackwardTarget','BypassBase','DirectEntryToBase') {
        $entryAt = [UInt32]($r10Lea-0x20)
        [byte[]]$entryBytes = @(0x0f,0x1f,0x40,0x00)
        $entryText = 'nop dword ptr [rax]'
        if ($Fault -ne 'BackwardTarget') {
            $entryBytes = [byte[]]@(0xe9,0,0,0,0)
            $entryTarget = if($Fault -eq 'BypassBase'){ $r10Guard+7 } else { $r10Lea }
            Put-Bytes $entryBytes 1 ([BitConverter]::GetBytes([Int32]($entryTarget-($entryAt+5))))
            $entryText = 'jmp guard-or-base'
        }
        Put-Bytes $pe ($rawBase+[int]($entryAt-$r10Section)) $entryBytes
        $rows.Add(('{0:x}: {1} {2}' -f ($r10ImageBase+$entryAt),(Hex-Bytes $entryBytes),$entryText))
    }
    if ($Fault -match '^Row(Missing|Duplicate|Unknown|Overlap|Listing|Pe)(\d+)$') {
        $kind = $Matches[1]; $rowIndex = [int]$Matches[2]
        if ($rowIndex -ge 24) { throw 'Row fault outside protected prelude/guard' }
        $row = $rows[$rowIndex]
        $address = [UInt64]::Parse(($row -split ':',2)[0],[Globalization.NumberStyles]::HexNumber)
        if($kind -eq 'Missing'){ $rows.RemoveAt($rowIndex) }
        elseif($kind -eq 'Duplicate'){ $rows.Add($row) }
        elseif($kind -eq 'Unknown'){ $rows[$rowIndex] = ($row -replace ' (lea |synthetic-guard|cmp |mov |test |je |jne |jmp ).*$', ' <unknown>') }
        elseif($kind -eq 'Overlap'){ $rows.Add(('{0:x}: 90 nop' -f ($address+1))) }
        elseif($kind -eq 'Listing'){
            $start = $row.IndexOf(':')+2
            $first = [byte]::Parse($row.Substring($start,2),[Globalization.NumberStyles]::HexNumber)
            $rows[$rowIndex]=$row.Substring(0,$start)+('{0:x2}' -f ($first -bxor 1))+$row.Substring($start+2)
        }
        else { $offset=$rawBase+[int]($address-$r10ImageBase-$r10Section); $pe[$offset] = $pe[$offset] -bxor 1 }
    }
    if ($Fault -in 'BodyVex','BodyEvex') {
        [byte[]]$bodyBytes = if($Fault -eq 'BodyVex'){ @(0xc5,0xf8,0x10,0x00) }else{ @(0x62,0xf1,0x7c,0x48,0x10,0x00) }
        Put-Bytes $pe ($rawBase+[int]($bodyTarget-$r10Section)) $bodyBytes
        for($index=0;$index -lt $rows.Count;$index++){
            if($rows[$index].StartsWith(('{0:x}:' -f ($r10ImageBase+$bodyTarget)))){
                $rows[$index]=('{0:x}: {1} vmovups real-body' -f ($r10ImageBase+$bodyTarget),(Hex-Bytes $bodyBytes))
                break
            }
        }
    }

    if ($Fault -in @('AdjacentVex','AdjacentEvex')) {
        [byte[]]$adjacent = if ($Fault -eq 'AdjacentVex') { [byte[]]@(0xc5,0xf8,0x10,0x00) } else { [byte[]]@(0x62,0xf1,0x7c,0x48,0x10,0x00) }
        $at = [UInt32]$tableEnd
        Put-Bytes $pe ($rawBase + [int]($at-$r10Section)) $adjacent
        $mnemonic = if ($Fault -eq 'AdjacentVex') { 'vmovups xmm0, xmmword ptr [rax]' } else { 'vmovups zmm0, zmmword ptr [rax]' }
        $rows.Add(('{0:x}: {1} {2}' -f ($r10ImageBase+$at),(Hex-Bytes $adjacent),$mnemonic))
    }
    [IO.File]::WriteAllBytes($binary,$pe)
    Set-Content -LiteralPath $disassembly -Value (@($rows | Sort-Object { [UInt64]::Parse(($_ -split ':',2)[0],[Globalization.NumberStyles]::HexNumber) }) -join "`n") -Encoding utf8

    # One matching PE/PDB image: procedure and terminal data are owned by module 0.
    $procedureEnd = [UInt32]($r10Table + 4 * $entryCount)
    $procedureOwner = if ($Fault -eq 'ProcedureOwner') { 1 } else { 0 }
    $dataOwner = if ($Fault -eq 'DataOwner') { 1 } else { 0 }
    $procedureBegin = if ($Fault -eq 'ProcedureBegin') { [UInt32]($r10Guard+1) } else { $r10Lea }
    if ($Fault -in 'BackwardTarget','BypassBase','DirectEntryToBase') { $procedureBegin = [UInt32]($r10Lea-0x20) }
    if ($Fault -eq 'ProcedureEnd') { $procedureEnd -= 4 }
    $dataAddress = if ($Fault -eq 'DataAddress') { [UInt32]($r10Table+4) } else { $r10Table }
    $symbolText = "  Mod 0 | ``SyntheticControl.obj``:`n  Obj: ``SyntheticControl.obj```:`n"
    if ($Fault -ne 'MissingProcedure') {
        $symbolText += "  Mod $procedureOwner | ``SyntheticControl.obj``:`n  0 | S_GPROC32 [size = 16] ``R10AlGuard`` `n  addr = 0001:$($procedureBegin-$r10Section), code size = $($procedureEnd-$procedureBegin)`n"
    }
    if ($Fault -ne 'MissingDataSymbol') {
        $symbolText += "  Mod $dataOwner | ``SyntheticControl.obj``:`n  0 | S_LDATA32 [size = 16] `````n  type = <no type>, addr = 0001:$($dataAddress-$r10Section)`n"
    }
    Set-Content -LiteralPath $symbols -Value $symbolText -Encoding utf8

    $contribution = "  SC[.text] | mod = 0, 0001:0, size = 8192, data crc = 0, reloc crc = 0`n"
    $metadataText = "  Age: $r10Age`n  GUID: {$r10Guid}`n  Mod 0 | ``SyntheticControl.obj``:`n  Obj: ``SyntheticControl.obj```:`n  Mod 1 | ``Other.obj``:`n  Obj: ``Other.obj```:`n`nSection Contributions`n$contribution"
    Set-Content -LiteralPath $metadata -Value $metadataText -Encoding utf8

    return @{ Name=$Name; Expectation=$Expectation; Fault=$Fault; Maximum=$Maximum; Binary=$binary; Metadata=$metadata; Symbols=$symbols; Disassembly=$disassembly; TableRva=$r10Table; TableEnd=$tableEnd }
}

# Independent passing and rejecting fixtures for the exact observed dispatch.
$script:R10AlGuardDraftCases = [Collections.Generic.List[object]]::new()
function Add-R10DraftCase([string]$Name,[string]$Expectation,[string]$Fault='',[int]$Maximum=6,[string]$Targets='Mixed',[string]$Tail='CallCcNop') {
    $script:R10AlGuardDraftCases.Add(@{Name=$Name;Expectation=$Expectation;Fault=$Fault;Maximum=$Maximum;Targets=$Targets;Tail=$Tail})
}

Add-R10DraftCase 'R10AlGuardDefaultMixed' Pass '' 6 Mixed CallCcNop
Add-R10DraftCase 'R10AlGuardAllTargetsSame' Pass '' 6 AllSame CallCcNop
Add-R10DraftCase 'R10AlGuardMaximum1' Pass '' 1 Mixed CallCcNop
Add-R10DraftCase 'R10AlGuardMaximum127' Pass '' 127 Mixed CallCcNop
Add-R10DraftCase 'R10AlGuardPlainRetTail' Pass '' 6 Mixed RetNop
Add-R10DraftCase 'R10AlGuardSeparateDefault' Pass 'DefaultNotInEntries' 6 AllSame
Add-R10DraftCase 'R10AlGuardBackwardTarget' Pass 'BackwardTarget'
Add-R10DraftCase 'R10AlGuardDirectEntryToBase' Pass 'DirectEntryToBase'
foreach ($fault in 'GuardLoad','GuardCompare','GuardBranch','TableLoad','Add','Jump','ImageBase','PreludeClobber','PreludeBranch','TableAddress') {
    Add-R10DraftCase "R10AlGuard$fault" Reject $fault
}
Add-R10DraftCase 'R10AlGuardOverlap' Reject 'Overlap'
Add-R10DraftCase 'R10AlGuardMissingPEListing' Reject 'MissingPEListing'
Add-R10DraftCase 'R10AlGuardListingMismatchLoad' Reject 'ListingMismatchTableLoad'
Add-R10DraftCase 'R10AlGuardProcedureBeginMismatch' Reject 'ProcedureBegin'
Add-R10DraftCase 'R10AlGuardMissingProcedure' Reject 'MissingProcedure'
Add-R10DraftCase 'R10AlGuardWrongProcedureOwner' Reject 'ProcedureOwner'
Add-R10DraftCase 'R10AlGuardWrongProcedureEnd' Reject 'ProcedureEnd'
Add-R10DraftCase 'R10AlGuardWrongDataOwner' Reject 'DataOwner'
Add-R10DraftCase 'R10AlGuardWrongTableSymbol' Reject 'DataAddress'
Add-R10DraftCase 'R10AlGuardMissingTableSymbol' Reject 'MissingDataSymbol'
Add-R10DraftCase 'R10AlGuardBadTableEntry' Reject 'TableEntry'
Add-R10DraftCase 'R10AlGuardAdjacentVEX' Reject 'AdjacentVex'
Add-R10DraftCase 'R10AlGuardAdjacentEVEX' Reject 'AdjacentEvex'
foreach ($fault in 'MaximumZero','Maximum128','MaximumTooSmall','MaximumTooLarge','MissingReturn','BypassBase','BodyVex','BodyEvex') {
    Add-R10DraftCase "R10AlGuard$fault" Reject $fault
}
foreach ($kind in 'Missing','Duplicate','Unknown','Overlap','Listing','Pe') {
    for($index=0;$index -lt 24;$index++){ Add-R10DraftCase "R10AlGuardRow$kind$index" Reject "Row$kind$index" }
}

$r10Count = 0
foreach ($control in $script:R10AlGuardDraftCases) {
    $case = New-R10AlGuardCase @control
    if ($control.Expectation -eq 'Pass') {
        $inline = $control.Maximum+1
        if($control.Fault -eq 'BackwardTarget'){--$inline}
        Invoke-Control $case $true $inline 0
    }
    elseif ($control.Fault -in 'AdjacentVex','AdjacentEvex','BodyVex','BodyEvex') {
        Invoke-Control $case $false ($control.Maximum+1) 1
        $proof = [RedPrismBuild.IsaScanner]::Scan($case.Binary,$case.Metadata,$case.Symbols,$case.Disassembly,[string[]]@(),[string[]]@(),[string[]]@())
        $expectedEvex = if($control.Fault -in 'AdjacentEvex','BodyEvex'){1}else{0}
        if($proof.EvexInstructions -ne $expectedEvex){throw 'Adjacent EVEX classification mismatch'}
    }
    else { Invoke-Control $case $false }
    ++$r10Count
}
Write-Host "R10 AL guarded terminal DWORD parser controls passed: $r10Count"
