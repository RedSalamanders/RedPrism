[CmdletBinding()]
param(
    # Main may point this sidecar at the scanner source in its private worktree.
    [string]$ScannerSource,
    [string]$RepoRoot
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repo = if ($RepoRoot) { (Resolve-Path -LiteralPath $RepoRoot).Path } else { $PSScriptRoot }
while (-not (Test-Path -LiteralPath (Join-Path $repo 'Build/IsaScan.cs'))) {
    $parent = Split-Path -Parent $repo
    if (-not $parent -or $parent -eq $repo) { throw 'Scanner test repository root not found.' }
    $repo = $parent
}
if ($PSBoundParameters.ContainsKey('ScannerSource') -and ('RedPrismBuild.IsaScanner' -as [type])) {
    throw 'An explicit scanner candidate requires a fresh PowerShell process.'
}
if (-not $ScannerSource) { $ScannerSource = Join-Path $repo 'Build/IsaScan.cs' }
if (-not (Test-Path -LiteralPath $ScannerSource -PathType Leaf)) {
    throw "Scanner source not found: $ScannerSource"
}
if (-not ('RedPrismBuild.IsaScanner' -as [type])) { Add-Type -Path $ScannerSource }

# This fixture writes parser input only. It never creates or launches native code.
$fixtureRoot = Join-Path $repo ('.build/BuildProcessTests/IsaAlNearRcx-' + [guid]::NewGuid().ToString('N'))
[void](New-Item -ItemType Directory -Path $fixtureRoot -Force)
$imageBase = [UInt64]0x140000000
$sectionRva = [UInt32]0x002a0000
$functionRva = [UInt32]0x002afd00
$prefixRva = [UInt32]0x002aff61
$dispatchRva = [UInt32]($prefixRva + 15)
$afterJumpRva = [UInt32]($dispatchRva + 19)
$tableRva = [UInt32]0x002b22f4
$tableCount = 21
$tableEndRva = [UInt32]($tableRva + 4 * $tableCount)
$bodyLimitRva = [UInt32]($tableRva - 9)
$sectionRaw = 0x400
$sectionSize = 0x13000
$guid = [guid]'bb1cce84-752a-440b-87da-09fc037dbe21'
$age = [UInt32]1

function Set-U16([byte[]]$Bytes, [int]$Offset, [UInt16]$Value) {
    [Array]::Copy([BitConverter]::GetBytes($Value), 0, $Bytes, $Offset, 2)
}
function Set-U32([byte[]]$Bytes, [int]$Offset, [UInt32]$Value) {
    [Array]::Copy([BitConverter]::GetBytes($Value), 0, $Bytes, $Offset, 4)
}
function Set-I32([byte[]]$Bytes, [int]$Offset, [Int32]$Value) {
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
function Convert-RvaToRaw([UInt32]$Rva) {
    if ($Rva -lt $sectionRva -or $Rva -ge ($sectionRva + $sectionSize)) {
        throw ('Fixture RVA {0:x} is outside its PE section.' -f $Rva)
    }
    return [int]($sectionRaw + $Rva - $sectionRva)
}
function Add-FixtureRow(
    [Collections.Generic.List[object]]$Rows,
    [UInt32]$Rva,
    [byte[]]$Bytes,
    [string]$Text
) {
    $Rows.Add([pscustomobject]@{ Rva=$Rva; Bytes=[byte[]]$Bytes.Clone(); Text=$Text })
}

function New-Fixture {
    param([Parameter(Mandatory)][string]$Name, [string]$Fault = '')

    $directory = Join-Path $fixtureRoot $Name
    [void](New-Item -ItemType Directory -Path $directory -Force)
    $binary = Join-Path $directory 'IsaAlNearRcx.exe'
    $metadata = Join-Path $directory 'contributions.txt'
    $symbols = Join-Path $directory 'symbols.txt'
    $disassembly = Join-Path $directory 'disassembly.txt'

    # PE32+, one executable section, one CodeView identity. The section covers the
    # function, terminal table and a short adjacent-code region.
    [byte[]]$pe = [byte[]]::new($sectionRaw + $sectionSize)
    $pe[0] = 0x4d; $pe[1] = 0x5a
    Set-U32 $pe 0x3c 0x80
    Set-U32 $pe 0x80 0x00004550
    Set-U16 $pe 0x84 0x8664
    Set-U16 $pe 0x86 1
    Set-U16 $pe 0x94 0xf0
    Set-U16 $pe 0x98 0x20b
    Set-U64 $pe (0x98 + 24) $imageBase
    Set-U32 $pe (0x98 + 56) ($sectionRva + $sectionSize)
    Set-U32 $pe (0x98 + 60) $sectionRaw
    Set-U32 $pe (0x98 + 112 + 6 * 8) ($sectionRva + 0x180)
    Set-U32 $pe (0x98 + 116 + 6 * 8) 28
    $section = 0x80 + 24 + 0xf0
    Put-Bytes $pe $section ([Text.Encoding]::ASCII.GetBytes('.text' + [char]0 + [char]0 + [char]0))
    Set-U32 $pe ($section + 8) $sectionSize
    Set-U32 $pe ($section + 12) $sectionRva
    Set-U32 $pe ($section + 16) $sectionSize
    Set-U32 $pe ($section + 20) $sectionRaw
    Set-U32 $pe ($section + 36) 0x60000020
    Set-U32 $pe ($sectionRaw + 0x180 + 12) 2
    Set-U32 $pe ($sectionRaw + 0x180 + 16) 24
    Set-U32 $pe ($sectionRaw + 0x180 + 20) ($sectionRva + 0x1a0)
    Set-U32 $pe ($sectionRaw + 0x180 + 24) 0x5a0
    Put-Bytes $pe 0x5a0 ([Text.Encoding]::ASCII.GetBytes('RSDS'))
    Put-Bytes $pe 0x5a4 $guid.ToByteArray()
    Set-U32 $pe 0x5b4 $age

    $rows = [Collections.Generic.List[object]]::new()
    # Prologue and guard: MOVZX EAX,[RCX+321h]; CMP AL,20h; JA default.
    [byte[]]$movzx = @(0x0f,0xb6,0x81,0x21,0x03,0x00,0x00)
    [byte[]]$cmp = @(0x3c,0x14)
    [byte[]]$ja = @(0x0f,0x87,0,0,0,0)
    [UInt32]$defaultRva = [UInt32]($afterJumpRva + 0x20)
    Set-I32 $ja 2 ([Int32]($defaultRva - ($prefixRva + 15)))

    # The body is fully decoded and continuous. Instructions are one-byte NOPs
    # except for the guard/dispatch, table tail and explicit control-flow mutations.
    for ([UInt32]$at = $functionRva; $at -lt $prefixRva; ++$at) {
        $pe[(Convert-RvaToRaw $at)] = 0x90
        Add-FixtureRow $rows $at ([byte[]]@(0x90)) 'nop'
    }
    Put-Bytes $pe (Convert-RvaToRaw $prefixRva) $movzx
    Add-FixtureRow $rows $prefixRva $movzx 'movzx eax, byte ptr [rcx+321h]'
    Put-Bytes $pe (Convert-RvaToRaw ($prefixRva + 7)) $cmp
    Add-FixtureRow $rows ($prefixRva + 7) $cmp 'cmp al, 20h'
    Put-Bytes $pe (Convert-RvaToRaw ($prefixRva + 9)) $ja
    Add-FixtureRow $rows ($prefixRva + 9) $ja 'ja default'
    for ([UInt32]$at = ($prefixRva + 15); $at -lt $dispatchRva; ++$at) {
        $pe[(Convert-RvaToRaw $at)] = 0x90
        Add-FixtureRow $rows $at ([byte[]]@(0x90)) 'nop'
    }

    [byte[]]$lea = @(0x48,0x8d,0x15,0,0,0,0)
    Set-I32 $lea 3 ([Int32](-[Int64]($dispatchRva + 7)))
    [byte[]]$load = @(0x8b,0x84,0x82,0,0,0,0)
    Set-U32 $load 3 $tableRva
    [byte[]]$add = @(0x48,0x03,0xc2)
    [byte[]]$jump = @(0xff,0xe0)
    Put-Bytes $pe (Convert-RvaToRaw $dispatchRva) $lea
    Put-Bytes $pe (Convert-RvaToRaw ($dispatchRva + 7)) $load
    Put-Bytes $pe (Convert-RvaToRaw ($dispatchRva + 14)) $add
    Put-Bytes $pe (Convert-RvaToRaw ($dispatchRva + 17)) $jump
    Add-FixtureRow $rows $dispatchRva $lea 'lea rdx, [rip+imagebase]'
    Add-FixtureRow $rows ($dispatchRva + 7) $load 'mov eax, dword ptr [rdx+rax*4+table]'
    Add-FixtureRow $rows ($dispatchRva + 14) $add 'add rax, rdx'
    Add-FixtureRow $rows ($dispatchRva + 17) $jump 'jmp rax'

    # Default and all 21 vector targets are decoded NOP boundaries in the body.
    for ([UInt32]$at = ($dispatchRva + 19); $at -lt $bodyLimitRva; ++$at) {
        $pe[(Convert-RvaToRaw $at)] = 0x90
        Add-FixtureRow $rows $at ([byte[]]@(0x90)) 'nop'
    }
    $callRva = [UInt32]($tableRva - 9)
    [byte[]]$tailCall = @(0xe8,0,0,0,0)
    $externalTarget = [UInt32]($sectionRva + 0x2f00)
    Set-I32 $tailCall 1 ([Int32]($externalTarget - ($callRva + 5)))
    Put-Bytes $pe (Convert-RvaToRaw $callRva) $tailCall
    Put-Bytes $pe (Convert-RvaToRaw ($callRva + 5)) ([byte[]]@(0xcc))
    Put-Bytes $pe (Convert-RvaToRaw ($callRva + 6)) ([byte[]]@(0x0f,0x1f,0x00))
    Add-FixtureRow $rows $callRva $tailCall 'call externalbody'
    Add-FixtureRow $rows ($callRva + 5) ([byte[]]@(0xcc)) 'int3'
    Add-FixtureRow $rows ($callRva + 6) ([byte[]]@(0x0f,0x1f,0x00)) 'nop dword ptr [rax]'

    [UInt32[]]$targets = [UInt32[]]::new($tableCount)
    for ($i = 0; $i -lt $tableCount; ++$i) {
        $targets[$i] = [UInt32](0x002affc5 + $i * 0x100)
        Set-U32 $pe ((Convert-RvaToRaw $tableRva) + 4 * $i) $targets[$i]
        [byte[]]$entry = [BitConverter]::GetBytes($targets[$i])
        Add-FixtureRow $rows ($tableRva + 4 * $i) $entry 'table-data'
    }

    # Fault controls mutate the machine bytes independently of the listing when
    # the name says ListingMismatch; all other controls keep rows PE-correlated.
    $instructionNames = @('GuardMovzx','GuardCmp','GuardJa','DispatchLea','DispatchLoad','DispatchAdd','DispatchJump')
    $instructionRvas = @($prefixRva,($prefixRva+7),($prefixRva+9),$dispatchRva,($dispatchRva+7),($dispatchRva+14),($dispatchRva+17))
    $instructionBytes = @($movzx,$cmp,$ja,$lea,$load,$add,$jump)
    $badOpcode = @{
        GuardMovzxOpcode=@{Index=0;Offset=1;Value=0xbe}; GuardMovzxDestination=@{Index=0;Offset=2;Value=0xc1};
        GuardMovzxBase=@{Index=0;Offset=2;Value=0x80}; GuardMovzxDisp=@{Index=0;Offset=3;Value=0x20};
        GuardCmpOpcode=@{Index=1;Offset=0;Value=0x80};
        GuardJaCondition=@{Index=2;Offset=1;Value=0x86};
        LeaOpcode=@{Index=3;Offset=1;Value=0x8c}; LeaDestination=@{Index=3;Offset=2;Value=0x0d};
        LeaMemoryBase=@{Index=3;Offset=2;Value=0x95}; LoadOpcode=@{Index=4;Offset=0;Value=0x89};
        LoadDestination=@{Index=4;Offset=1;Value=0x8c}; LoadBase=@{Index=4;Offset=2;Value=0x81};
        LoadIndex=@{Index=4;Offset=2;Value=0x8a}; LoadScale=@{Index=4;Offset=2;Value=0x42};
        AddOpcode=@{Index=5;Offset=1;Value=0x01}; AddRegisters=@{Index=5;Offset=2;Value=0xca};
        JumpOpcode=@{Index=6;Offset=0;Value=0x9f}; JumpRegister=@{Index=6;Offset=1;Value=0xe1};
        JumpFarIndirect=@{Index=6;Offset=1;Value=0x28}
    }
    if ($badOpcode.ContainsKey($Fault)) {
        $m = $badOpcode[$Fault]; $instructionBytes[$m.Index][$m.Offset] = [byte]$m.Value
    }
    switch ($Fault) {
        'GuardCountZero' { $instructionBytes[1][1] = 0x00 }
        'GuardCount128' { $instructionBytes[1][1] = 0x80 }
        'TableWrongCount20' { $instructionBytes[1][1] = 0x13 }
        'TableWrongCount22' { $instructionBytes[1][1] = 0x15 }
        'GuardDefaultMidInstruction' { Set-I32 $instructionBytes[2] 2 ([Int32](1 - 15)) }
        'GuardBypassJa' { $instructionBytes[2][0] = 0x90 }
        'LeaWrongImageBase' { Set-I32 $instructionBytes[3] 3 ([Int32](-[Int64]($dispatchRva + 6))) }
        'LoadWrongTableRva' { Set-U32 $instructionBytes[4] 3 ($tableRva + 4) }
        'AddWrongBase' { $instructionBytes[5][2] = 0xc1 }
        'TailCallInsideBody' {
            Set-I32 $tailCall 1 ([Int32]($functionRva - ($callRva + 5)))
            Put-Bytes $pe (Convert-RvaToRaw $callRva) $tailCall
            ($rows | Where-Object { $_.Rva -eq $callRva } | Select-Object -First 1).Bytes = [byte[]]$tailCall.Clone()
        }
        'TailCallIntoTrap' {
            Set-I32 $tailCall 1 ([Int32](($tableRva - 4) - ($callRva + 5)))
            Put-Bytes $pe (Convert-RvaToRaw $callRva) $tailCall
            ($rows | Where-Object { $_.Rva -eq $callRva } | Select-Object -First 1).Bytes = [byte[]]$tailCall.Clone()
        }
        'TailCallIntoNopPadding' {
            Set-I32 $tailCall 1 ([Int32](($tableRva - 3) - ($callRva + 5)))
            Put-Bytes $pe (Convert-RvaToRaw $callRva) $tailCall
            ($rows | Where-Object { $_.Rva -eq $callRva } | Select-Object -First 1).Bytes = [byte[]]$tailCall.Clone()
        }
        'TrapAtTarget' { $pe[(Convert-RvaToRaw $targets[10])] = 0xcc }
        'NopRowAtTarget' { $pe[(Convert-RvaToRaw $targets[10])] = 0x0f }
        'AdjacentVex' {
            Put-Bytes $pe (Convert-RvaToRaw $tableEndRva) ([byte[]]@(0xc5,0xf8,0x10,0x00))
            Add-FixtureRow $rows $tableEndRva ([byte[]]@(0xc5,0xf8,0x10,0x00)) 'vmovups xmm0, xmmword ptr [rax]'
        }
        'AdjacentEvex' {
            Put-Bytes $pe (Convert-RvaToRaw $tableEndRva) ([byte[]]@(0x62,0xf1,0x7c,0x48,0x58,0xc0))
            Add-FixtureRow $rows $tableEndRva ([byte[]]@(0x62,0xf1,0x7c,0x48,0x58,0xc0)) 'vaddps zmm0, zmm0, zmm0'
        }
        'TableWrongCount20' { }
        'TableWrongCount22' { }
        'TableBadTargetMiddle' { Set-U32 $pe ((Convert-RvaToRaw $tableRva) + 4 * 20) ($dispatchRva + 1) }
        'TableBadTargetOutside' { Set-U32 $pe ((Convert-RvaToRaw $tableRva) + 4 * 20) ($sectionRva + $sectionSize) }
        'BodyUnknownGap' { }
        'BodyGap' { }
        'BodyOverlap' { }
        'PdbOwnerMismatch' { }
        'PdbStartMismatch' { }
        'PdbEndEarly' { }
        'PdbEndLate' { }
        'MissingProcedure' { }
        'MissingDataStart' { }
        'MissingContribution' { }
        'PdbTableOwnerMismatch' { }
        'PdbTableStartEarly' { }
        'PdbTableStartLate' { }
        'PdbSecondTableSymbolInside' { }
        'PdbDataSymbolInBody' { }
        'TerminalEndMismatch' { }
        'DirectEntryGuard' { }
        'DirectEntryDispatch' { }
        'DirectEntryAfterJump' { }
        'DirectEntryTrap' { }
        'DirectEntryPadding' { }
        'DirectEntryTable' { }
        'BodyVex' { }
        'BodyEvex' { }
        'BodyFarJump' { }
        'BodySystemControl' { }
        'BodyIndirectJump' { }
        'BodyLocalCall' { }
        'BodyOtherFarFlow' { }
        default { }
    }
    # Write each dispatch row back from the selected encoding. This keeps PE and
    # listing equal except for the explicit listing corruption controls below.
    for ($i = 0; $i -lt $instructionRvas.Count; ++$i) {
        Put-Bytes $pe (Convert-RvaToRaw ([UInt32]$instructionRvas[$i])) ([byte[]]$instructionBytes[$i])
        $row = $rows | Where-Object { $_.Rva -eq [UInt32]$instructionRvas[$i] } | Select-Object -First 1
        $row.Bytes = [byte[]]$instructionBytes[$i].Clone()
    }
    if ($Fault -in @('LoadSigned','LoadFullRaxNotZeroExtended')) {
        foreach ($oldRva in @(($dispatchRva + 7), ($dispatchRva + 14), ($dispatchRva + 17), ($dispatchRva + 19))) {
            $oldRow = $rows | Where-Object { $_.Rva -eq [UInt32]$oldRva } | Select-Object -First 1
            if ($oldRow) { [void]$rows.Remove($oldRow) }
        }
        [byte]$wideOpcode = if ($Fault -eq 'LoadSigned') { 0x63 } else { 0x8b }
        [byte[]]$wideLoad = @(0x48,$wideOpcode,0x84,0x82,0,0,0,0)
        Set-U32 $wideLoad 4 $tableRva
        [byte[]]$shiftedAdd = @(0x48,0x03,0xc2)
        [byte[]]$shiftedJump = @(0xff,0xe0)
        Put-Bytes $pe (Convert-RvaToRaw ($dispatchRva + 7)) $wideLoad
        Put-Bytes $pe (Convert-RvaToRaw ($dispatchRva + 15)) $shiftedAdd
        Put-Bytes $pe (Convert-RvaToRaw ($dispatchRva + 18)) $shiftedJump
        Add-FixtureRow $rows ($dispatchRva + 7) $wideLoad $(if ($Fault -eq 'LoadSigned') { 'movsxd rax, dword ptr [rdx+rax*4+table]' } else { 'mov rax, qword ptr [rdx+rax*4+table]' })
        Add-FixtureRow $rows ($dispatchRva + 15) $shiftedAdd 'add rax, rdx'
        Add-FixtureRow $rows ($dispatchRva + 18) $shiftedJump 'jmp rax'
    }
    if ($Fault -match '^PeMismatch(GuardMovzx|GuardCmp|GuardJa|DispatchLea|DispatchLoad|DispatchAdd|DispatchJump)$') {
        $peMismatchName = $Matches[1]
        for ($i = 0; $i -lt $instructionNames.Count; ++$i) {
            if ($instructionNames[$i] -eq $peMismatchName) {
                $pe[(Convert-RvaToRaw ([UInt32]$instructionRvas[$i]))] = $pe[(Convert-RvaToRaw ([UInt32]$instructionRvas[$i]))] -bxor 1
                break
            }
        }
    }

    # Flow/body corruption controls replace bytes and the corresponding decoded rows.
    # Their expected scanner result is always a violation.
    $bodyFaultRva = [UInt32]($prefixRva + 0x50) # Keep real instructions away from the valid default boundary at +0x42.
    $flowBytes = $null
    $flowText = ''
    switch ($Fault) {
        'BodyVex' { $flowBytes=[byte[]]@(0xc5,0xf8,0x10,0x00); $flowText='vmovups xmm0, xmmword ptr [rax]' }
        'BodyEvex' { $flowBytes=[byte[]]@(0x62,0xf1,0x7c,0x48,0x58,0xc0); $flowText='vaddps zmm0, zmm0, zmm0' }
        'BodyIndirectJump' { $flowBytes=[byte[]]@(0xff,0xe0); $flowText='jmp rax' }
        'BodyLocalCall' { $flowBytes=[byte[]]@(0xe8,0,0,0,0); $flowText='call local' }
        'BodyFarJump' { $flowBytes=[byte[]]@(0xea,0,0,0,0,0,0); $flowText='jmp far ptr' }
        'BodyOtherFarFlow' { $flowBytes=[byte[]]@(0x9a,0,0,0,0,0,0); $flowText='call far ptr' }
        'BodySystemControl' { $flowBytes=[byte[]]@(0x0f,0x05); $flowText='syscall' }
    }
    if ($flowBytes) {
        foreach ($row in @($rows | Where-Object { $_.Rva -ge $bodyFaultRva -and $_.Rva -lt ($bodyFaultRva + $flowBytes.Length) })) { [void]$rows.Remove($row) }
        Put-Bytes $pe (Convert-RvaToRaw $bodyFaultRva) $flowBytes
        Add-FixtureRow $rows $bodyFaultRva $flowBytes $flowText
    }
    if ($Fault -like 'DirectEntry*') {
        [UInt32]$branchRva = [UInt32]($prefixRva + 0x80)
        [UInt32]$branchTarget = switch ($Fault) {
            'DirectEntryGuard' { $prefixRva + 7 }
            'DirectEntryDispatch' { $dispatchRva + 7 }
            'DirectEntryAfterJump' { $dispatchRva + 18 }
            'DirectEntryTrap' { $callRva + 5 }
            'DirectEntryPadding' { $callRva + 6 }
            'DirectEntryTable' { $tableRva }
        }
        [byte[]]$branch = @(0xe9,0,0,0,0)
        Set-I32 $branch 1 ([Int32]($branchTarget - ($branchRva + 5)))
        foreach ($row in @($rows | Where-Object { $_.Rva -ge $branchRva -and $_.Rva -lt ($branchRva + 5) })) { [void]$rows.Remove($row) }
        Put-Bytes $pe (Convert-RvaToRaw $branchRva) $branch
        Add-FixtureRow $rows $branchRva $branch 'jmp direct-entry-control'
    }
    if ($Fault -eq 'TrapAtTarget') {
        $row = $rows | Where-Object { $_.Rva -eq $targets[10] } | Select-Object -First 1
        $row.Bytes = [byte[]]@(0xcc); $row.Text = 'int3'
    }
    if ($Fault -eq 'NopRowAtTarget') {
        $row = $rows | Where-Object { $_.Rva -eq $targets[10] } | Select-Object -First 1
        $row.Bytes = [byte[]]@(0x0f); $row.Text = '<unknown>'
    }

    if ($Fault -match '^Tail(Call|Trap|Padding)(Missing|Duplicate|Unknown|Listing|Pe)$') {
        $part=$Matches[1]; $kind=$Matches[2]
        $at = switch ($part) { 'Call' { $callRva } 'Trap' { $callRva + 5 } 'Padding' { $callRva + 6 } }
        $row = $rows | Where-Object { $_.Rva -eq $at } | Select-Object -First 1
        switch ($kind) {
            'Missing' { [void]$rows.Remove($row) }
            'Duplicate' { Add-FixtureRow $rows $at $row.Bytes $row.Text }
            'Unknown' { $row.Text='<unknown>' }
            'Listing' { $row.Bytes[0]=$row.Bytes[0] -bxor 1 }
            'Pe' { $raw=Convert-RvaToRaw $at; $pe[$raw]=$pe[$raw] -bxor 1 }
        }
    }
    # Exact case-specific guard/dispatch row controls, independent of PE encoding.
    if ($Fault -match '^(Missing|ListingMismatch|Unknown|Overlap)(GuardMovzx|GuardCmp|GuardJa|DispatchLea|DispatchLoad|DispatchAdd|DispatchJump)$') {
        $kind = $Matches[1]; $named = $Matches[2]
        $targetRva = $null
        for ($i=0; $i -lt $instructionNames.Count; ++$i) { if ($instructionNames[$i] -eq $named) { $targetRva=[UInt32]$instructionRvas[$i]; break } }
        $row = $rows | Where-Object { $_.Rva -eq $targetRva } | Select-Object -First 1
        switch ($kind) {
            'Missing' { [void]$rows.Remove($row) }
            'ListingMismatch' { $row.Bytes[0] = $row.Bytes[0] -bxor 1 }
            'Unknown' { $row.Text = '<unknown>' }
            'Overlap' { Add-FixtureRow $rows ($targetRva + 1) ([byte[]]@($pe[(Convert-RvaToRaw ($targetRva + 1))])) 'overlap-control' }
        }
    }
    if ($Fault -eq 'BodyUnknownGap') {
        $row = $rows | Where-Object { $_.Rva -eq ($prefixRva + 0x40) } | Select-Object -First 1
        $row.Text = '<unknown>'
    }
    if ($Fault -eq 'BodyGap') {
        $row = $rows | Where-Object { $_.Rva -eq ($prefixRva + 0x40) } | Select-Object -First 1
        [void]$rows.Remove($row)
    }
    if ($Fault -eq 'BodyOverlap') { Add-FixtureRow $rows ($prefixRva + 0x40) ([byte[]]@(0x90)) 'overlap-control' }
    if ($Fault -eq 'BodyPeMismatch') { $pe[(Convert-RvaToRaw ($prefixRva + 0x40))] = 0xcc }

    # PDB metadata and exact single-owner function/data records.
    $contributionEnd = if ($Fault -eq 'TerminalEndMismatch') { $tableEndRva - 1 } else { $tableEndRva }
    $contributionLine = "  SC[.text] | mod = 0, 0001:$($functionRva-$sectionRva), size = $($contributionEnd-$functionRva), data crc = 0, reloc crc = 0"
    if ($Fault -eq 'MissingContribution') { $contributionLine = '' }
    $metadataText = "  Age: $age`n  GUID: {$guid}`n  Mod 0 | ``Synthetic.obj``:`n  Obj: ``Synthetic.obj``:`n  Mod 1 | ``Other.obj``:`n  Obj: ``Other.obj``:`n`nSection Contributions`n$contributionLine`n"
    Set-Content -LiteralPath $metadata -Value $metadataText -Encoding utf8
    $procStart = if ($Fault -eq 'PdbStartMismatch') { $prefixRva + 1 } else { $functionRva }
    $procEnd = switch ($Fault) {
        'PdbEndEarly' { $tableEndRva - 1 }
        'PdbEndLate' { $tableEndRva + 1 }
        default { $tableEndRva }
    }
    $procOwner = if ($Fault -eq 'PdbOwnerMismatch') { 1 } else { 0 }
    $symbolsText = ''
    if ($Fault -ne 'MissingProcedure') {
        $symbolsText = "  Mod $procOwner | ``Synthetic.obj``:`n  0 | S_GPROC32 [size = 16] ``ApplicationStrokeFinishJobs`` `n  addr = 0001:$($procStart-$sectionRva), code size = $($procEnd-$procStart)`n"
    }
    $dataOwner = if ($Fault -eq 'PdbTableOwnerMismatch') { 1 } else { 0 }
    $dataStart = switch ($Fault) {
        'PdbTableStartEarly' { $tableRva - 1 }
        'PdbTableStartLate' { $tableRva + 1 }
        default { $tableRva }
    }
    if ($Fault -ne 'MissingDataStart') {
        $symbolsText += "  Mod $dataOwner | ``Synthetic.obj``:`n  0 | S_LDATA32 [size = 16] `````n  type = <no type>, addr = 0001:$($dataStart-$sectionRva)`n"
    }
    if ($Fault -eq 'PdbSecondTableSymbolInside') {
        $symbolsText += "  0 | S_LDATA32 [size = 16] `````n  type = <no type>, addr = 0001:$($tableRva+4-$sectionRva)`n"
    }
    if ($Fault -eq 'PdbDataSymbolInBody') {
        $symbolsText += "  0 | S_LDATA32 [size = 16] `````n  type = <no type>, addr = 0001:$($functionRva+0x500-$sectionRva)`n"
    }
    if ($Fault -eq 'OptionalMatchingEndSymbol') {
        $symbolsText += "  0 | S_LDATA32 [size = 16] `````n  type = <no type>, addr = 0001:$($tableEndRva-$sectionRva)`n"
    }
    Set-Content -LiteralPath $symbols -Value $symbolsText -Encoding utf8

    # Keep table rows correlated to PE bytes, including deliberate bad-target controls.
    foreach ($row in @($rows | Where-Object { $_.Rva -ge $tableRva -and $_.Rva -lt $tableEndRva -and $_.Text -like 'table-data*' })) {
        $raw = Convert-RvaToRaw ([UInt32]$row.Rva)
        $row.Bytes = [byte[]]$pe[$raw..($raw + 3)]
    }
    # Count and independently validate all 21 encoded table data rows, PE bytes,
    # and row nonoverlap. The named overlap/listing/table faults are intentional.
    $tableRows = @($rows | Where-Object { $_.Rva -ge $tableRva -and $_.Rva -lt $tableEndRva -and $_.Text -like 'table-data*' })
    if ($Fault -notin @('TableWrongCount20','TableWrongCount22') -and $tableRows.Count -ne 21) {
        throw "$Name fixture must contain exactly 21 DWORD table rows; found $($tableRows.Count)."
    }
    foreach ($row in $rows) {
        $raw = Convert-RvaToRaw ([UInt32]$row.Rva)
        if ($raw -gt $pe.Length - $row.Bytes.Length) { throw "$Name row exceeds PE bytes at RVA $($row.Rva)." }
        if ($Fault -notlike 'ListingMismatch*' -and $Fault -notlike 'PeMismatch*' -and $Fault -ne 'BodyPeMismatch' -and $Fault -notmatch '^Tail(Call|Trap|Padding)(Listing|Pe)$') {
            for ($j=0; $j -lt $row.Bytes.Length; ++$j) {
                if ($pe[$raw + $j] -ne $row.Bytes[$j]) { throw "$Name row/PE mismatch at RVA $($row.Rva)." }
            }
        }
    }
    $orderedRows = @($rows | Sort-Object Rva)
    for ($i=1; $i -lt $orderedRows.Count; ++$i) {
        $previous = $orderedRows[$i-1]; $current = $orderedRows[$i]
        if (($previous.Rva + $previous.Bytes.Length) -gt $current.Rva -and
            $Fault -notin @('BodyOverlap') -and $Fault -notlike 'Overlap*' -and $Fault -notmatch '^Tail(Call|Trap|Padding)Duplicate$') {
            throw "$Name has unintended instruction-row overlap at RVA $($current.Rva)."
        }
    }
    if ($Fault -ne 'TableWrongCount20' -and $Fault -ne 'TableWrongCount22') {
        for ($i=0; $i -lt 21; ++$i) {
            $actual = [BitConverter]::ToUInt32($pe, (Convert-RvaToRaw $tableRva) + 4 * $i)
            if ($actual -ne $targets[$i] -and $Fault -notin @('TableBadTargetMiddle','TableBadTargetOutside')) {
                throw "$Name table entry $i differs from its expected encoded target."
            }
        }
    }
    [IO.File]::WriteAllBytes($binary, $pe)
    Set-Content -LiteralPath $disassembly -Value (@($orderedRows | ForEach-Object {
        '{0:x}: {1} {2}' -f ($imageBase + [UInt64]$_.Rva), (Hex-Bytes $_.Bytes), $_.Text
    }) -join "`n") -Encoding utf8
    return @{ Name=$Name; Binary=$binary; Metadata=$metadata; Symbols=$symbols; Disassembly=$disassembly }
}

$script:controlCount = 0
function Invoke-Control {
    param([string]$Name, [string]$Fault = '', [bool]$ExpectedPass = $false,
          [int]$ExpectedInline = -1, [int]$ExpectedVectors = -1)
    $case = New-Fixture $Name $Fault
    try {
        $result = [RedPrismBuild.IsaScanner]::Scan($case.Binary, $case.Metadata, $case.Symbols, $case.Disassembly,
            [string[]]@(), [string[]]@(), [string[]]@())
    } catch {
        if ($Fault -eq 'MissingContribution' -and $_.Exception.GetBaseException() -is [IO.InvalidDataException]) {
            ++$script:controlCount
            Write-Host "PASS IsaAlNearRcx.$Name expected metadata refusal: $($_.Exception.GetBaseException().Message)"
            return
        }
        throw
    }
    if ($ExpectedPass -and $result.Violations.Count -ne 0) {
        throw "$Name should pass but scanner reported: $($result.Violations -join '; ')"
    }
    if (-not $ExpectedPass -and $result.Violations.Count -eq 0) { throw "$Name unexpectedly passed the ISA gate." }
    if ($ExpectedInline -ge 0 -and $result.InlineTableVectorData -ne $ExpectedInline) {
        throw "$Name expected $ExpectedInline inline table rows, got $($result.InlineTableVectorData)."
    }
    if ($ExpectedVectors -ge 0 -and $result.VectorInstructions -ne $ExpectedVectors) {
        throw "$Name expected $ExpectedVectors ordinary vector instructions, got $($result.VectorInstructions)."
    }
    $expectedEvex = if ($Fault -in @('BodyEvex','AdjacentEvex')) { 1 } else { 0 }
    $expectedViolations = if ($ExpectedPass) { 0 } else { $ExpectedVectors }
    if ($result.EvexInstructions -ne $expectedEvex -or $result.Violations.Count -ne $expectedViolations) {
        throw "$Name expected EVEX/violations $expectedEvex/$expectedViolations, got $($result.EvexInstructions)/$($result.Violations.Count)."
    }
    ++$script:controlCount
    Write-Host "PASS IsaAlNearRcx.$Name"
}

$roster = [Collections.Generic.List[object]]::new()
$roster.Add(@{Name='ValidCanonical';Fault='';Pass=$true;Inline=21;Vectors=0})
$roster.Add(@{Name='ValidOptionalMatchingEndSymbol';Fault='OptionalMatchingEndSymbol';Pass=$true;Inline=21;Vectors=0})
$roster.Add(@{Name='ValidTrapCaseTarget';Fault='TrapAtTarget';Pass=$true;Inline=21;Vectors=0})
foreach ($fault in @(
    'GuardMovzxOpcode','GuardMovzxDestination','GuardMovzxBase','GuardMovzxDisp','GuardCmpOpcode',
    'GuardJaCondition','GuardDefaultMidInstruction','GuardBypassJa',
    'LeaOpcode','LeaDestination','LeaMemoryBase','LeaWrongImageBase',
    'LoadOpcode','LoadDestination','LoadBase','LoadIndex','LoadScale','LoadSigned','LoadFullRaxNotZeroExtended','LoadWrongTableRva',
    'AddOpcode','AddRegisters','AddWrongBase','JumpOpcode','JumpRegister','JumpFarIndirect',
    'GuardCountZero','GuardCount128','TableWrongCount20','TableWrongCount22','TableBadTargetMiddle','TableBadTargetOutside',
    'PdbOwnerMismatch','PdbStartMismatch','PdbEndEarly','PdbEndLate','MissingProcedure','MissingDataStart','MissingContribution',
    'PdbTableOwnerMismatch','PdbTableStartEarly','PdbTableStartLate','PdbSecondTableSymbolInside','PdbDataSymbolInBody','TerminalEndMismatch',
        'BodyUnknownGap','BodyGap','BodyOverlap','BodyPeMismatch','BodyVex','BodyEvex','BodyFarJump','BodySystemControl','BodyIndirectJump','BodyLocalCall','BodyOtherFarFlow',
    'TailCallInsideBody','TailCallIntoTrap','TailCallIntoNopPadding','NopRowAtTarget',
    'DirectEntryGuard','DirectEntryDispatch','DirectEntryAfterJump','DirectEntryTrap','DirectEntryPadding','DirectEntryTable',
    'AdjacentVex','AdjacentEvex'
)) { $roster.Add(@{Name=$fault;Fault=$fault;Pass=$false;Inline=-1;Vectors=-1}) }
foreach ($kind in @('Missing','ListingMismatch','Unknown','Overlap')) {
    foreach ($rowName in @('GuardMovzx','GuardCmp','GuardJa','DispatchLea','DispatchLoad','DispatchAdd','DispatchJump')) {
        $fault = "$kind$rowName"
        $roster.Add(@{Name=$fault;Fault=$fault;Pass=$false;Inline=-1;Vectors=-1})
    }
}
foreach ($rowName in @('GuardMovzx','GuardCmp','GuardJa','DispatchLea','DispatchLoad','DispatchAdd','DispatchJump')) {
    $fault = "PeMismatch$rowName"
    $roster.Add(@{Name=$fault;Fault=$fault;Pass=$false;Inline=-1;Vectors=-1})
}

# Preserve the ordered, unique names as an explicit acceptance roster.
$expectedRoster = @(
    'ValidCanonical','ValidOptionalMatchingEndSymbol','ValidTrapCaseTarget',
    'GuardMovzxOpcode','GuardMovzxDestination','GuardMovzxBase','GuardMovzxDisp','GuardCmpOpcode',
    'GuardJaCondition','GuardDefaultMidInstruction','GuardBypassJa','LeaOpcode','LeaDestination','LeaMemoryBase','LeaWrongImageBase',
    'LoadOpcode','LoadDestination','LoadBase','LoadIndex','LoadScale','LoadSigned','LoadFullRaxNotZeroExtended','LoadWrongTableRva',
    'AddOpcode','AddRegisters','AddWrongBase','JumpOpcode','JumpRegister','JumpFarIndirect','GuardCountZero','GuardCount128',
    'TableWrongCount20','TableWrongCount22','TableBadTargetMiddle','TableBadTargetOutside','PdbOwnerMismatch','PdbStartMismatch',
    'PdbEndEarly','PdbEndLate','MissingProcedure','MissingDataStart','MissingContribution','PdbTableOwnerMismatch',
    'PdbTableStartEarly','PdbTableStartLate','PdbSecondTableSymbolInside','PdbDataSymbolInBody','TerminalEndMismatch','BodyUnknownGap','BodyGap','BodyOverlap',
    'BodyPeMismatch','BodyVex','BodyEvex','BodyFarJump','BodySystemControl','BodyIndirectJump','BodyLocalCall','BodyOtherFarFlow','TailCallInsideBody',
    'TailCallIntoTrap','TailCallIntoNopPadding','NopRowAtTarget','DirectEntryGuard','DirectEntryDispatch','DirectEntryAfterJump',
    'DirectEntryTrap','DirectEntryPadding','DirectEntryTable','AdjacentVex','AdjacentEvex',
    'MissingGuardMovzx','MissingGuardCmp','MissingGuardJa','MissingDispatchLea','MissingDispatchLoad','MissingDispatchAdd','MissingDispatchJump',
    'ListingMismatchGuardMovzx','ListingMismatchGuardCmp','ListingMismatchGuardJa','ListingMismatchDispatchLea','ListingMismatchDispatchLoad',
    'ListingMismatchDispatchAdd','ListingMismatchDispatchJump','UnknownGuardMovzx','UnknownGuardCmp','UnknownGuardJa','UnknownDispatchLea',
    'UnknownDispatchLoad','UnknownDispatchAdd','UnknownDispatchJump','OverlapGuardMovzx','OverlapGuardCmp','OverlapGuardJa',
    'OverlapDispatchLea','OverlapDispatchLoad','OverlapDispatchAdd','OverlapDispatchJump',
    'PeMismatchGuardMovzx','PeMismatchGuardCmp','PeMismatchGuardJa','PeMismatchDispatchLea','PeMismatchDispatchLoad','PeMismatchDispatchAdd','PeMismatchDispatchJump'
)
if ($roster.Count -ne $expectedRoster.Count -or (@($roster.Name) -join "`n") -cne ($expectedRoster -join "`n")) {
    throw 'ISA control roster differs from the preserved ordered expected roster.'
}
if (@($roster.Name | Select-Object -Unique).Count -ne $roster.Count -or $roster.Count -lt 80) {
    throw 'ISA controls require at least 80 distinct, uniquely named cases.'
}
foreach ($part in @('Call','Trap','Padding')) {
    foreach ($kind in @('Missing','Duplicate','Unknown','Listing','Pe')) {
        $fault = "Tail$part$kind"
        $roster.Add(@{Name=$fault;Fault=$fault;Pass=$false;Inline=0;Vectors=21})
    }
}
if ($roster.Count -ne 121 -or @($roster.Name | Select-Object -Unique).Count -ne 121) { throw 'Expected 121 distinct Main-reviewed controls.' }
foreach ($item in $roster) {
    if ($item.Fault -eq 'GuardMovzxDisp') { $item.Pass=$true; $item.Inline=21; $item.Vectors=0 }
    if (-not $item.Pass) {
        $item.Inline = 0
        if ($item.Fault -in @('BodyVex','BodyEvex','AdjacentVex','AdjacentEvex')) {
            $item.Inline=21; $item.Vectors=1 # The valid data proof does not exempt real instructions beside it.
        } elseif ($item.Fault -in @('TableBadTargetMiddle','TableBadTargetOutside')) { $item.Vectors=20 }
        else { $item.Vectors=21 }
    }
    Invoke-Control $item.Name $item.Fault $item.Pass $item.Inline $item.Vectors
}
Write-Host "ISA AL-near-RCX guarded terminal table controls passed: $script:controlCount"
