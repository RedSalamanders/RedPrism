[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$wideStackRepo = $PSScriptRoot
while (-not (Test-Path -LiteralPath (Join-Path $wideStackRepo 'Build/IsaScan.cs')) -or
    -not (Test-Path -LiteralPath (Join-Path $wideStackRepo 'Tests/BuildProcessTests/IsaInlineTableFixtures.ps1'))) {
    $wideStackParent = Split-Path -Parent $wideStackRepo
    if (-not $wideStackParent -or $wideStackParent -eq $wideStackRepo) { throw 'Repository root not found' }
    $wideStackRepo = $wideStackParent
}
if (-not ('RedPrismBuild.IsaScanner' -as [type])) {
    Add-Type -Path (Join-Path $wideStackRepo 'Build/IsaScan.cs')
}
# Reuse independently encoded PE/PDB fixture primitives and rerun all earlier controls.
. (Join-Path $wideStackRepo 'Tests/BuildProcessTests/IsaInlineTableFixtures.ps1')

function New-GuardedTerminalDwordWideStackCase([string]$Name, [string]$Fault = '', [int]$Maximum = 10) {
    $case = New-SyntheticCase ($Name + 'Seed') -PdbData
    $pe = [IO.File]::ReadAllBytes($case.Binary)
    [UInt32]$section = $script:sectionRva
    [UInt32]$dispatch = $section + 0x890
    [UInt32]$prefix = $dispatch - 22
    [UInt32]$table = $section + 0x900
    [UInt32]$target = $section + 0x8c5
    [UInt32]$end = $table + 4 * ($Maximum + 1)

    # Observed unsigned terminal guard: CMP [RSP+disp32],imm8; JA rel32; MOVSXD RAX,[RSP+same disp32].
    [byte[]]$guard = @(0x83,0xbc,0x24,0x70,0x01,0,0,0x0a,0x0f,0x87,0,0,0,0,0x48,0x63,0x84,0x24,0x70,0x01,0,0)
    Set-U32 $guard 10 ([UInt32]([Int32]($target - ($prefix + 14))))
    $guard[7] = [byte]$Maximum
    switch ($Fault) {
        'WrongCmp' { $guard[1] = 0xbd }
        'WrongJa' { $guard[9] = 0x86 }
        'WrongJaCondition' { $guard[8] = 0x0e }
        'WrongMovsxd' { $guard[15] = 0x62 }
        'WrongMovsxdRegister' { $guard[14] = 0x49 }
        'DifferentStackSlot' { $guard[18]++ }
        'WrongCmpStackSlot' { $guard[3]++ }
        'DifferentStackSlotHighByte1' { $guard[19]++ }
        'DifferentStackSlotHighByte2' { $guard[20]++ }
        'DifferentStackSlotHighByte3' { $guard[21]++ }
        'WrongCmpSib' { $guard[2] = 0x25 }
        'WrongMovsxdSib' { $guard[17] = 0x25 }
        'SignedCmpRange' { $guard[1] = 0xb4 }
        'ZeroRange' { $guard[7] = 0 }
        'Range128' { $guard[7] = 128 }
        'Range255' { $guard[7] = 255 }
        'WrongDefault' { $guard[10]++ }
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
    $guardStarts = @(0,8,14)
    $guardLengths = @(8,6,8)
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
        $sym += "  0 | S_GPROC32 [size = 16] ``GuardedDwordWideStack`` `n  addr = 0001:$($prefix-$section), code size = $($procEnd-$prefix)`n"
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
$terminalDwordWideStackCount = 0
Invoke-Control (New-GuardedTerminalDwordWideStackCase 'TerminalDwordWideStackSeparateDefault' 'SeparateDefault' 10) $true 11 0
++ $terminalDwordWideStackCount
foreach ($maximum in 1,10,127) {
    Invoke-Control (New-GuardedTerminalDwordWideStackCase "TerminalDwordWideStackMaximum$maximum" '' $maximum) $true ($maximum+1) 0
    ++$terminalDwordWideStackCount
}
Invoke-Control (New-GuardedTerminalDwordWideStackCase 'TerminalDwordWideStackAdjacentVex' 'AdjacentVex' 10) $false 11 1
++ $terminalDwordWideStackCount
foreach ($fault in 'WrongCmp','WrongJa','WrongJaCondition','WrongMovsxd','WrongMovsxdRegister','DifferentStackSlot','WrongCmpStackSlot',
    'DifferentStackSlotHighByte1','DifferentStackSlotHighByte2','DifferentStackSlotHighByte3','WrongCmpSib','WrongMovsxdSib','SignedCmpRange',
    'ZeroRange','Range128','Range255','WrongDefault','WrongImageBase','WrongLoad','WrongLoadIndex','WrongAdd','WrongJump',
    'MissingGuard0','MissingGuard1','MissingGuard2','ListingGuard0','ListingGuard1','ListingGuard2','UnknownGuard0','UnknownGuard1','UnknownGuard2',
    'OverlappingGuard','OverlappingDispatch','ListingDispatch0','ListingDispatch1','ListingDispatch2','ListingDispatch3','BadLastTarget','OutsideTarget','MiddleTarget',
    'ShortProcedure','LongProcedure','WrongProcedureOwner','MissingProcedure','WrongDataOwner','MissingData','WrongDataStart','EarlyDataSymbol',
    'LaterUnrelatedData') {
    Invoke-Control (New-GuardedTerminalDwordWideStackCase ("TerminalDwordWideStack$fault") $fault 10) $false
    ++$terminalDwordWideStackCount
}
Write-Host "Guarded terminal DWORD wide-stack synthetic controls passed: $terminalDwordWideStackCount"
