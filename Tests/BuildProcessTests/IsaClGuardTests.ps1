[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Resolve from this file's location. The production test runner may later copy this
# sidecar into Tests/BuildProcessTests without changing the repository lookup.
$repo = (Get-Item -LiteralPath $PSScriptRoot).FullName
while (-not (Test-Path -LiteralPath (Join-Path $repo 'Build/IsaScan.cs'))) {
    $parent = Split-Path -Parent $repo
    if ([string]::IsNullOrEmpty($parent) -or $parent -eq $repo) {
        throw 'Could not find the RedPrism repository root from the sidecar location.'
    }
    $repo = $parent
}

# Reuse the PE/PDB/listing fixture writer and parser-only Invoke-Control helper.
. (Join-Path $repo 'Tests/BuildProcessTests/IsaInlineTableTests.ps1')

function New-ClGuardedTerminalDwordCase {
    param(
        [Parameter(Mandatory)] [string]$Name,
        [string]$Fault = '',
        [ValidateRange(1,127)] [int]$Maximum = 8
    )

    $case = New-SyntheticCase ($Name + 'Seed') -PdbData
    [byte[]]$pe = [IO.File]::ReadAllBytes($case.Binary)
    [UInt32]$section = $script:sectionRva
    [UInt32]$prefix = $section + 0x870
    [UInt32]$dispatch = $prefix + 16
    [UInt32]$table = $section + 0x900
    [int]$entryCount = $Maximum + 1
    [UInt32]$end = $table + 4 * $entryCount
    [UInt32]$default = $section + 0x8c4
    [UInt32]$target = $section + 0x8c5
    [UInt32]$branchTarget = $default
    [UInt32]$tailTarget = $target
    [UInt32]$tableAddress = $table
    if ($Fault -eq 'BadDefaultTarget') { $branchTarget = $prefix + 1 }
    if ($Fault -eq 'DefaultInTable') { $branchTarget = $table + 4 }
    if ($Fault -eq 'BadTailTarget') { $tailTarget = $target + 1 }

    # Exact observed guard: MOVZX ECX,[RBX+disp8]; CMP CL,max; JA rel32;
    # MOVZX ECX,CL. The latter is part of the 16-byte guard proof.
    [byte[]]$guardLoad = [byte[]]@(0x0f,0xb6,0x4b,0x11)
    [byte[]]$guardCmp = [byte[]]@(0x80,0xf9,[byte]$Maximum)
    [byte[]]$guardJa = [byte[]]@(0x0f,0x87,0,0,0,0)
    [byte[]]$guardIndex = [byte[]]@(0x0f,0xb6,0xc9)
    # Exact 20-byte dispatch: LEA R8,imagebase; MOV ECX,[R8+4*RCX+table];
    # ADD RCX,R8; JMP RCX.
    [byte[]]$lea = [byte[]]@(0x4c,0x8d,0x05,0,0,0,0)
    [byte[]]$load = [byte[]]@(0x41,0x8b,0x8c,0x88,0,0,0,0)
    [byte[]]$add = [byte[]]@(0x49,0x03,0xc8)
    [byte[]]$jump = [byte[]]@(0xff,0xe1)
    [byte[]]$tail = [byte[]]@(0xeb,0x00,0x90)

    switch ($Fault) {
        'WrongGuardLoadOpcode' { $guardLoad[1] = 0xbe }
        'WrongGuardLoadDestination' { $guardLoad[2] = 0x43 }
        'WrongGuardLoadBase' { $guardLoad[2] = 0x49 }
        'ChangedSourceDisplacement' { $guardLoad[3] = 0x12 }
        'WrongGuardCmpOpcode' { $guardCmp[0] = 0x38 }
        'WrongGuardCmpRegister' { $guardCmp[1] = 0xf8 }
        'BoundZero' { $guardCmp[2] = 0 }
        'Bound128' { $guardCmp[2] = 128 }
        'BoundTooSmall' { $guardCmp[2] = [byte]($Maximum - 1) }
        'BoundTooLarge' { $guardCmp[2] = [byte]($Maximum + 1) }
        'WrongGuardJa' { $guardJa[1] = 0x86 }
        'WrongGuardJaWidth' { $guardJa[0] = 0x77 }
        'WrongGuardIndexOpcode' { $guardIndex[1] = 0xbe }
        'WrongGuardIndexDestination' { $guardIndex[2] = 0xc1 }
        'WrongGuardIndexSource' { $guardIndex[2] = 0xc8 }
        'WrongLeaOpcode' { $lea[1] = 0x8c }
        'WrongLeaDestination' { $lea[2] = 0x0d }
        'WrongLeaBase' { $lea[0] = 0x48 }
        'WrongImageBase' { }
        'WrongLoadOpcode' { $load[1] = 0x89 }
        'WrongLoadDestination' { $load[2] = 0x84 }
        'WrongLoadScale' { $load[3] = 0x48 }
        'WrongLoadIndex' { $load[3] = 0xa0 }
        'WrongLoadBase' { $load[3] = 0x80 }
        'WrongDispatchSource' { $load[3] = 0x90 }
        'WrongAddOpcode' { $add[1] = 0x01 }
        'WrongAddRegisters' { $add[2] = 0xc1 }
        'WrongJumpOpcode' { $jump[1] = 0xe0 }
        'BadTailJump' { $tail[0] = 0xe9 }
        'BadTailNop' { $tail[2] = 0xcc }
        'BadDefaultTarget' { }
        'BadTailTarget' { }
        'BadLastTarget' { }
        'TargetOutsideProcedure' { }
        'TargetMidInstruction' { }
        'TargetAtTable' { }
        'DefaultInTable' { }
        'WrongTableAddress' { $tableAddress += 4 }
        'WrongDataStart' { }
    }

    [Int64]$imageBase = [Int64]0x140000000
    [Int32]$jaDisplacement = [Int32]([Int64]$branchTarget - [Int64]($prefix + 13))
    Put-Bytes $guardJa 2 ([BitConverter]::GetBytes($jaDisplacement))
    [Int32]$leaDisplacement = [Int32](-[Int64]($dispatch + 7))
    if ($Fault -eq 'WrongImageBase') { ++$leaDisplacement }
    Put-Bytes $lea 3 ([BitConverter]::GetBytes($leaDisplacement))
    [UInt32]$loadDisplacement = $tableAddress
    Put-Bytes $load 4 ([BitConverter]::GetBytes($loadDisplacement))

    [Int32]$tailDisplacement = [Int32]([Int64]$tailTarget - [Int64]($table - 1))
    if ($tailDisplacement -lt -128 -or $tailDisplacement -gt 127) {
        throw "Synthetic tail target is outside rel8 range: $tailDisplacement"
    }
    $tail[1] = [byte]($tailDisplacement -band 0xff)

    # Independently write the expected code bytes into the PE image.
    $guardBytes = @($guardLoad,$guardCmp,$guardJa,$guardIndex)
    $guardOffsets = @(0,4,7,13)
    $dispatchBytes = @($lea,$load,$add,$jump)
    $dispatchOffsets = @(0,7,15,18)
    for ($i = 0; $i -lt $guardBytes.Count; ++$i) {
        Put-Bytes $pe (0x400 + $prefix + $guardOffsets[$i] - $section) $guardBytes[$i]
    }
    for ($i = 0; $i -lt $dispatchBytes.Count; ++$i) {
        Put-Bytes $pe (0x400 + $dispatch + $dispatchOffsets[$i] - $section) $dispatchBytes[$i]
    }

    # Two real decoded instruction boundaries: the default branch target and
    # table destinations. The latter is also the rel8 tail-jump destination.
    $pe[0x400 + $default - $section] = 0x90
    [byte[]]$targetNop = [byte[]]@(0x0f,0x1f,0x00)
    Put-Bytes $pe (0x400 + $target - $section) $targetNop
    Put-Bytes $pe (0x400 + $table - $section - 3) $tail

    # PE and listing bytes are independently mutable for all eight proof rows.
    $guardNames = @('GuardLoad','GuardCmp','GuardJa','GuardIndex')
    $dispatchNames = @('DispatchLea','DispatchLoad','DispatchAdd','DispatchJump')
    if ($Fault -like 'PeMismatch*' -and $Fault -notin @('PeMismatchTail','PeMismatchTailNop')) {
        $which = $Fault.Substring('PeMismatch'.Length)
        $allNames = @($guardNames) + @($dispatchNames)
        $allBytes = @($guardBytes) + @($dispatchBytes)
        $allOffsets = @($guardOffsets) + @($dispatchOffsets | ForEach-Object { 16 + $_ })
        $index = [Array]::IndexOf([string[]]$allNames, $which)
        if ($index -lt 0) { throw "Unknown PE mismatch control: $Fault" }
        $pe[0x400 + $prefix + $allOffsets[$index] - $section] =
            $pe[0x400 + $prefix + $allOffsets[$index] - $section] -bxor 1
    }

    $rows = [Collections.Generic.List[string]]::new()
    for ($i = 0; $i -lt $guardBytes.Count; ++$i) {
        $rowName = $guardNames[$i]
        if ($Fault -eq "Missing$rowName") { continue }
        [byte[]]$shown = $guardBytes[$i].Clone()
        if ($Fault -eq "ListingMismatch$rowName") { $shown[0] = $shown[0] -bxor 1 }
        $mnemonic = if ($Fault -eq "Unknown$rowName") { '<unknown>' } else { 'control' }
        $rows.Add(('{0:x}: {1} {2}' -f ($imageBase+$prefix+$guardOffsets[$i]),(Hex-Bytes $shown),$mnemonic))
        if ($Fault -eq "Overlap$rowName") {
            $rows.Add(('{0:x}: 90 overlap' -f ($imageBase+$prefix+$guardOffsets[$i]+1)))
        }
    }
    for ($i = 0; $i -lt $dispatchBytes.Count; ++$i) {
        $rowName = $dispatchNames[$i]
        if ($Fault -eq "Missing$rowName") { continue }
        [byte[]]$shown = $dispatchBytes[$i].Clone()
        if ($Fault -eq "ListingMismatch$rowName") { $shown[$shown.Length-1] = $shown[$shown.Length-1] -bxor 1 }
        $mnemonic = if ($Fault -eq "Unknown$rowName") { '<unknown>' } else { 'control' }
        $rows.Add(('{0:x}: {1} {2}' -f ($imageBase+$dispatch+$dispatchOffsets[$i]),(Hex-Bytes $shown),$mnemonic))
        if ($Fault -eq "Overlap$rowName") {
            $rows.Add(('{0:x}: 90 overlap' -f ($imageBase+$dispatch+$dispatchOffsets[$i]+1)))
        }
    }
    $rows.Add(('{0:x}: 90 default-target' -f ($imageBase+$default)))
    $rows.Add(('{0:x}: {1} table-target' -f ($imageBase+$target),(Hex-Bytes $targetNop)))
    [byte[]]$shownTailJump = [byte[]]@($tail[0],$tail[1])
    [byte[]]$shownTailNop = [byte[]]@($tail[2])
    if ($Fault -eq 'ListingMismatchTail') { $shownTailJump[0] = $shownTailJump[0] -bxor 1 }
    if ($Fault -eq 'ListingMismatchTailNop') { $shownTailNop[0] = 0xcc }
    if ($Fault -eq 'UnknownTail') { $tailMnemonic = '<unknown>' } else { $tailMnemonic = 'jmp' }
    if ($Fault -ne 'MissingTail') {
        $rows.Add(('{0:x}: {1} {2}' -f ($imageBase+$table-3),(Hex-Bytes $shownTailJump),$tailMnemonic))
    }
    if ($Fault -ne 'MissingTailNop') { $rows.Add(('{0:x}: {1} nop' -f ($imageBase+$table-1),(Hex-Bytes $shownTailNop))) }
    if ($Fault -eq 'PeMismatchTail') { $pe[0x400+$table-$section-3] = $pe[0x400+$table-$section-3] -bxor 1 }
    if ($Fault -eq 'PeMismatchTailNop') { $pe[0x400+$table-$section-1] = $pe[0x400+$table-$section-1] -bxor 1 }

    [UInt32]$badEntry = $target
    if ($Fault -eq 'BadLastTarget') { $badEntry = $target + 1 }
    if ($Fault -eq 'TargetOutsideProcedure') { $badEntry = $prefix - 1 }
    if ($Fault -eq 'TargetMidInstruction') { $badEntry = $target + 1 }
    if ($Fault -eq 'TargetAtTable') { $badEntry = $table }
    for ($i = 0; $i -lt $entryCount; ++$i) {
        [UInt32]$entry = $target
        if ($i -eq ($entryCount - 1) -and $Fault -in @('BadLastTarget','TargetOutsideProcedure','TargetMidInstruction','TargetAtTable')) {
            $entry = $badEntry
        }
        Set-U32 $pe (0x400 + $table - $section + 4*$i) $entry
        [byte[]]$dataBytes = $pe[(0x400+$table-$section+4*$i)..(0x400+$table-$section+4*$i+3)]
        $rows.Add(('{0:x}: {1} table-data' -f ($imageBase+$table+4*$i),(Hex-Bytes $dataBytes)))
    }

    [UInt32]$procBegin = $prefix
    [UInt32]$procEnd = $end
    if ($Fault -eq 'ProcedureEndsEarly') { $procEnd -= 4 }
    if ($Fault -eq 'ProcedureEndsLate') { $procEnd += 4 }
    if ($Fault -eq 'ProcedureStartsLate') { ++$procBegin }
    $procOwner = if ($Fault -eq 'WrongProcedureOwner') { 1 } else { 0 }
    $dataOwner = if ($Fault -eq 'WrongDataOwner') { 1 } else { 0 }
    $symbols = "  Mod $procOwner | ``SyntheticControl.obj``:`n"
    if ($Fault -ne 'MissingProcedure') {
        $symbols += "  0 | S_GPROC32 [size = 16] ``ClGuardedDispatch`` `n  addr = 0001:$($procBegin-$section), code size = $($procEnd-$procBegin)`n"
    }
    $symbols += "  Mod $dataOwner | ``SyntheticControl.obj``:`n"
    if ($Fault -ne 'MissingData') {
        [UInt32]$dataStart = if ($Fault -eq 'WrongDataStart') { $table + 4 } else { $table }
        $symbols += "  0 | S_LDATA32 [size = 16] `````n  type = <no type>, addr = 0001:$($dataStart-$section)`n"
    }
    if ($Fault -eq 'EarlyDataSymbol') {
        $symbols += "  0 | S_LDATA32 [size = 16] `````n  type = <no type>, addr = 0001:$($table+8-$section)`n"
    }
    $symbols += "  Mod 0 | ``SyntheticControl.obj``:`n  0 | S_GPROC32 [size = 16] ``LaterProcedure`` `n  addr = 0001:$($end+0x20-$section), code size = 128`n"
    $symbols += "  Mod 0 | ``SyntheticControl.obj``:`n  0 | S_LDATA32 [size = 16] `````n  type = <no type>, addr = 0001:$($end+0x40-$section)`n"

    Set-Content -LiteralPath $case.Symbols -Value $symbols -Encoding utf8
    Set-Content -LiteralPath $case.Disassembly -Value (@($rows | Sort-Object {
        [UInt64]::Parse(($_ -split ':',2)[0],[Globalization.NumberStyles]::HexNumber)
    }) -join "`n") -Encoding utf8
    [IO.File]::WriteAllBytes($case.Binary,$pe)
    $case.Name = $Name
    return $case
}

$clGuardCount = 0
foreach ($maximum in @(1,8,127)) {
    Invoke-Control (New-ClGuardedTerminalDwordCase "ClGuardMaximum$maximum" '' $maximum) $true ($maximum + 1) 0
    ++$clGuardCount
}
Invoke-Control (New-ClGuardedTerminalDwordCase 'ClGuardChangedSourceDisplacement' 'ChangedSourceDisplacement') $true 9 0
++ $clGuardCount

# Each of the eight guard/dispatch rows independently proves byte-for-byte PE
# corroboration, known decoded listing text, no overlap, and row presence.
foreach ($kind in 'PeMismatch','ListingMismatch','Unknown','Overlap','Missing') {
    foreach ($row in 'GuardLoad','GuardCmp','GuardJa','GuardIndex','DispatchLea','DispatchLoad','DispatchAdd','DispatchJump') {
        Invoke-Control (New-ClGuardedTerminalDwordCase "ClGuard$kind$row" "$kind$row") $false
        ++$clGuardCount
    }
}

foreach ($fault in @(
    'WrongGuardLoadOpcode','WrongGuardLoadDestination','WrongGuardLoadBase',
    'WrongGuardCmpOpcode','WrongGuardCmpRegister','WrongGuardJa','WrongGuardJaWidth',
    'WrongGuardIndexOpcode','WrongGuardIndexDestination','WrongGuardIndexSource',
    'BoundZero','Bound128','BoundTooSmall','BoundTooLarge',
    'WrongLeaOpcode','WrongLeaDestination','WrongLeaBase','WrongImageBase',
    'WrongLoadOpcode','WrongLoadDestination','WrongLoadScale','WrongLoadIndex','WrongLoadBase','WrongDispatchSource',
    'WrongAddOpcode','WrongAddRegisters','WrongJumpOpcode',
    'BadDefaultTarget','BadLastTarget','TargetOutsideProcedure','TargetMidInstruction','TargetAtTable','DefaultInTable',
    'MissingProcedure','WrongProcedureOwner','ProcedureStartsLate','ProcedureEndsEarly','ProcedureEndsLate',
    'MissingData','WrongDataOwner','WrongDataStart','EarlyDataSymbol','WrongTableAddress',
    'BadTailJump','BadTailNop','BadTailTarget','MissingTail','MissingTailNop','UnknownTail','ListingMismatchTail',
    'ListingMismatchTailNop','PeMismatchTail','PeMismatchTailNop'
)) {
    Invoke-Control (New-ClGuardedTerminalDwordCase "ClGuard$fault" $fault) $false
    ++$clGuardCount
}

# Genuine adjacent VEX/EVEX rows remain ordinary vector violations, outside the
# proven table. Keep the parser's independent inline-row expectation explicit.
foreach ($adjacent in @(
    @{ Name='Vex'; Bytes=[byte[]]@(0xc5,0xf8,0x10,0x00); Text='vmovups xmm0, xmmword ptr [rax]' },
    @{ Name='Evex'; Bytes=[byte[]]@(0x62,0xf1,0x7c,0x48,0x10,0x00); Text='vmovups zmm0, zmmword ptr [rax]' }
)) {
    $case = New-ClGuardedTerminalDwordCase "ClGuardAdjacent$($adjacent.Name)"
    [byte[]]$pe = [IO.File]::ReadAllBytes($case.Binary)
    [UInt32]$adjacentRva = $script:sectionRva + 0xb00
    Put-Bytes $pe (0x400+$adjacentRva-[UInt32]$script:sectionRva) $adjacent.Bytes
    $rows = [IO.File]::ReadAllText($case.Disassembly)
    $rows += "`n$('{0:x}: {1} {2}' -f ([UInt64]0x140000000+$adjacentRva),(Hex-Bytes $adjacent.Bytes),$adjacent.Text)"
    [IO.File]::WriteAllBytes($case.Binary,$pe)
    Set-Content -LiteralPath $case.Disassembly -Value $rows -Encoding utf8
    Invoke-Control $case $false 9 1
    ++$clGuardCount
}

Write-Host "CL guarded R8 synthetic parser controls passed: $clGuardCount"
