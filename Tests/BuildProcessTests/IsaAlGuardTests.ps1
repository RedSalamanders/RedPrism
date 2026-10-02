[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Resolve from this file's location so private parser fixtures can use the same helpers.
$repo = (Get-Item -LiteralPath $PSScriptRoot).FullName
while (-not (Test-Path -LiteralPath (Join-Path $repo 'Build/IsaScan.cs'))) {
    $parent = Split-Path -Parent $repo
    if ([string]::IsNullOrEmpty($parent) -or $parent -eq $repo) {
        throw 'Could not find the RedPrism repository root from the sidecar location.'
    }
    $repo = $parent
}

# Import the existing parser-only fixture helpers and run their established controls.
. (Join-Path $repo 'Tests/BuildProcessTests/IsaInlineTableTests.ps1')

function New-AlGuardedTerminalDwordCase {
    param(
        [Parameter(Mandatory)] [string]$Name,
        [string]$Fault = '',
        [ValidateRange(1,127)] [int]$Maximum = 6
    )

    $case = New-SyntheticCase ($Name + 'Seed') -PdbData
    [byte[]]$pe = [IO.File]::ReadAllBytes($case.Binary)
    [UInt32]$section = $script:sectionRva
    [UInt32]$prefix = $section + 0x870
    [UInt32]$dispatch = $prefix + 11
    [UInt32]$table = $section + 0x8e8
    [int]$entryCount = $Maximum + 1
    [UInt32]$end = $table + 4 * $entryCount
    [UInt32]$default = $section + 0x8c4
    [UInt32]$target = $section + 0x8c5
    [UInt32]$defaultBranchTarget = $default
    if ($Fault -eq 'DefaultSameEntry') { $defaultBranchTarget = $target }

    # The observed Release guard is exactly 11 bytes; dispatch starts at +11.
    [byte[]]$movzx = @(0x0f,0xb6,0x86,0xd8,0x01,0x00,0x00)
    [byte[]]$cmp = @(0x3c,0x06)
    $cmp[1] = [byte]$Maximum
    [byte[]]$ja = @(0x77,0x00)
    [byte[]]$lea = @(0x48,0x8d,0x15,0,0,0,0)
    [byte[]]$load = @(0x8b,0x84,0x82,0,0,0,0)
    [byte[]]$add = @(0x48,0x03,0xc2)
    [byte[]]$jump = @(0xff,0xe0)

    switch ($Fault) {
        'WrongMovzxOpcode' { $movzx[1] = 0xbe }
        'WrongMovzxDestination' { $movzx[2] = 0x8e }
        'WrongMovzxBase' { $movzx[2] = 0x87 }
        'ChangedMovzxDisplacement' { $movzx[3] = 0xd9 }
        'SignedMovsx' { $movzx[1] = 0xbe }
        'WrongCmpRegister' { $cmp[0] = 0x38 }
        'WrongCmpOpcode' { $cmp[0] = 0x80 }
        'RangeTooSmall' { $cmp[1] = [byte]($Maximum - 1) }
        'RangeTooLarge' { $cmp[1] = [byte]($Maximum + 1) }
        'RangeZero' { $cmp[1] = 0 }
        'Range128' { $cmp[1] = 128 }
        'WrongJa' { $ja[0] = 0x76 }
        'DefaultMidInstruction' { $defaultBranchTarget = $prefix + 1 }
        'DefaultOutsideProcedure' { $defaultBranchTarget = $prefix - 0x10 }
        'DefaultInsideTable' { $defaultBranchTarget = $table + 4 }
        'WrongImageBase' { }
        'WrongLeaOpcode' { $lea[1] = 0x8c }
        'WrongLeaDestination' { $lea[2] = 0x0d }
        'WrongLoadOpcode' { $load[0] = 0x89 }
        'WrongLoadDestination' { $load[1] = 0x8c }
        'WrongLoadScale' { $load[2] = 0x42 }
        'WrongLoadScaleEight' { $load[2] = 0xc2 }
        'WrongLoadIndex' { $load[2] = 0x86 }
        'WrongLoadBase' { $load[2] = 0x81 }
        'WrongDispatchIndex' { $load[2] = 0x8a }
        'WrongDispatchSource' { $movzx[2] = 0x87 }
        'WrongLoadAddress' { }
        'WrongAddOpcode' { $add[1] = 0x01 }
        'WrongAddRegisters' { $add[2] = 0xc1 }
        'WrongJumpOpcode' { $jump[1] = 0xe1 }
    }

    [Int64]$imageBase = [Int64]0x140000000
    [Int64]$branchDisplacement = [Int64]$defaultBranchTarget - [Int64]($prefix + 11)
    $ja[1] = [byte]($branchDisplacement -band 0xff)
    [Int32]$leaDisplacement = [Int32](-[Int64]($dispatch + 7))
    if ($Fault -eq 'WrongImageBase') { ++$leaDisplacement }
    Put-Bytes $lea 3 ([BitConverter]::GetBytes($leaDisplacement))
    [UInt32]$loadDisplacement = $table
    if ($Fault -eq 'WrongLoadAddress') { $loadDisplacement += 4 }
    Put-Bytes $load 3 ([BitConverter]::GetBytes($loadDisplacement))

    # Keep PE bytes and listing rows independently mutable for byte-corroboration controls.
    $prefixBytes = @($movzx,$cmp,$ja)
    $prefixOffsets = @(0,7,9)
    $prefixNames = @('PrefixMovzx','PrefixCmp','PrefixJa')
    $dispatchBytes = @($lea,$load,$add,$jump)
    $dispatchOffsets = @(0,7,14,17)
    $dispatchNames = @('DispatchLea','DispatchLoad','DispatchAdd','DispatchJump')
    for ($i = 0; $i -lt $prefixBytes.Count; ++$i) {
        Put-Bytes $pe (0x400 + $prefix + $prefixOffsets[$i] - $section) $prefixBytes[$i]
    }
    for ($i = 0; $i -lt $dispatchBytes.Count; ++$i) {
        Put-Bytes $pe (0x400 + $dispatch + $dispatchOffsets[$i] - $section) $dispatchBytes[$i]
    }

    # PE-only mutations ensure the parser cannot trust listing bytes or mnemonics.
    if ($Fault -like 'PeMismatch*') {
        $which = $Fault.Substring('PeMismatch'.Length)
        $allNames = @($prefixNames) + @($dispatchNames)
        $allBytes = @($prefixBytes) + @($dispatchBytes)
        $allOffsets = @($prefixOffsets | ForEach-Object { $_ }) +
            @($dispatchOffsets | ForEach-Object { 11 + $_ })
        $index = [Array]::IndexOf([string[]]$allNames, $which)
        if ($index -lt 0) { throw "Unknown PE mismatch control: $Fault" }
        $offset = 0x400 + $prefix + $allOffsets[$index] - $section
        $pe[$offset] = $pe[$offset] -bxor 1
    }

    # The table is terminal data: RET followed by the observed two-byte alignment NOP.
    # Other controls exercise the RET-only and one-/three-byte alignment forms accepted by ReadInlineTable.
    [byte[]]$preTable = @(0xc3,0x66,0x90)
    [UInt32]$preTableStart = $table - 3
    if ($Fault -eq 'MissingReturn') { $preTable[0] = 0x90 }
    if ($Fault -eq 'BadAlignment') { $preTable[1] = 0x67 }
    if ($Fault -eq 'RetOnly') { $preTable = [byte[]]@(0x90,0x90,0xc3) }
    if ($Fault -eq 'RetNop') { $preTable = [byte[]]@(0x90,0xc3,0x90) }
    if ($Fault -eq 'RetLongNop') {
        $preTable = [byte[]]@(0xc3,0x0f,0x1f,0x00)
        $preTableStart = $table - 4
    }
    Put-Bytes $pe (0x400 + $preTableStart - $section) $preTable

    # Valid table destinations are decoded instruction boundaries before the table.
    $pe[0x400 + $default - $section] = 0x90
    [byte[]]$targetNop = @(0x0f,0x1f,0x00)
    Put-Bytes $pe (0x400 + $target - $section) $targetNop

    $rows = [Collections.Generic.List[string]]::new()
    for ($i = 0; $i -lt $prefixBytes.Count; ++$i) {
        $rowName = $prefixNames[$i]
        if ($Fault -eq "Missing$rowName") { continue }
        [byte[]]$shown = $prefixBytes[$i].Clone()
        if ($Fault -eq "ListingMismatch$rowName") { $shown[0] = $shown[0] -bxor 1 }
        $mnemonic = if ($Fault -eq "Unknown$rowName") { '<unknown>' } else { 'control' }
        $rows.Add(('{0:x}: {1} {2}' -f ($imageBase+$prefix+$prefixOffsets[$i]),(Hex-Bytes $shown),$mnemonic))
        if ($Fault -eq "Overlap$rowName") {
            $rows.Add(('{0:x}: 90 overlap' -f ($imageBase+$prefix+$prefixOffsets[$i]+1)))
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
    $rows.Add(('{0:x}: 90 destination' -f ($imageBase+$default)))
    if ($target -ne $default) { $rows.Add(('{0:x}: {1} destination' -f ($imageBase+$target),(Hex-Bytes $targetNop))) }
    $rows.Add(('{0:x}: {1} return-alignment' -f ($imageBase+$preTableStart),(Hex-Bytes $preTable)))

    [UInt32]$badEntry = $target
    if ($Fault -eq 'BadLastTarget') { $badEntry = $target + 1 }
    if ($Fault -eq 'OutsideTarget') { $badEntry = $prefix - 1 }
    if ($Fault -eq 'MidInstructionTarget') { $badEntry = $target + 1 }
    if ($Fault -eq 'TargetAtTable') { $badEntry = $table }
    for ($i = 0; $i -lt $entryCount; ++$i) {
        [UInt32]$entry = $target
        if ($i -eq ($entryCount-1) -and $Fault -in @('BadLastTarget','OutsideTarget','MidInstructionTarget','TargetAtTable')) {
            $entry = $badEntry
        }
        if ($Fault -eq 'DefaultInEntries') { $entry = $default }
        Set-U32 $pe (0x400 + $table - $section + 4*$i) $entry
        [byte[]]$dataBytes = $pe[(0x400+$table-$section+4*$i)..(0x400+$table-$section+4*$i+3)]
        $rows.Add(('{0:x}: {1} table-data' -f ($imageBase+$table+4*$i),(Hex-Bytes $dataBytes)))
    }

    [UInt32]$procBegin = $prefix
    [UInt32]$procEnd = $end
    if ($Fault -eq 'ProcedureEndsEarly') { $procEnd -= 4 }
    if ($Fault -eq 'ProcedureEndsLate') { $procEnd += 4 }
    if ($Fault -eq 'GuardOutsideProcedure') { $procBegin = $prefix + 1 }
    $procOwner = if ($Fault -eq 'WrongProcedureOwner') { 1 } else { 0 }
    $dataOwner = if ($Fault -eq 'WrongDataOwner') { 1 } else { 0 }
    $symbols = "  Mod 0 | ``SyntheticControl.obj``:`n"
    if ($Fault -ne 'MissingProcedure') {
        $symbols += "  Mod $procOwner | ``SyntheticControl.obj``:`n  0 | S_GPROC32 [size = 16] ``AlGuardedDispatch`` `n  addr = 0001:$($procBegin-$section), code size = $($procEnd-$procBegin)`n"
    }
    $symbols += "  Mod $dataOwner | ``SyntheticControl.obj``:`n"
    if ($Fault -ne 'MissingData') {
        [UInt32]$dataStart = if ($Fault -eq 'WrongDataStart') { $table + 4 } else { $table }
        $symbols += "  0 | S_LDATA32 [size = 16] `````n  type = <no type>, addr = 0001:$($dataStart-$section)`n"
    }
    if ($Fault -eq 'EarlyDataSymbol') {
        $symbols += "  0 | S_LDATA32 [size = 16] `````n  type = <no type>, addr = 0001:$($table+8-$section)`n"
    }
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

$alGuardCount = 0
foreach ($maximum in @(1,6,127)) {
    $expectedRows = $maximum + 1
    Invoke-Control (New-AlGuardedTerminalDwordCase "AlGuardMaximum$maximum" '' $maximum) $true $expectedRows 0
    ++$alGuardCount
}
Invoke-Control (New-AlGuardedTerminalDwordCase 'AlGuardDefaultSeparate' 'DefaultSeparate') $true 7 0
++ $alGuardCount
Invoke-Control (New-AlGuardedTerminalDwordCase 'AlGuardDefaultSameEntry' 'DefaultSameEntry') $true 7 0
++ $alGuardCount
Invoke-Control (New-AlGuardedTerminalDwordCase 'AlGuardChangedMovzxDisplacement' 'ChangedMovzxDisplacement') $true 7 0
++ $alGuardCount
Invoke-Control (New-AlGuardedTerminalDwordCase 'AlGuardDefaultInEntries' 'DefaultInEntries') $true 7 0
++ $alGuardCount
foreach ($alignment in 'RetOnly','RetNop','RetLongNop') {
    Invoke-Control (New-AlGuardedTerminalDwordCase "AlGuard$alignment" $alignment) $true 7 0
    ++$alGuardCount
}

foreach ($adjacent in @(
    @{ Name='Vex'; Bytes=[byte[]]@(0xc5,0xf8,0x10,0x00); Text='vmovups xmm0, xmmword ptr [rax]' },
    @{ Name='Evex'; Bytes=[byte[]]@(0x62,0xf1,0x7c,0x48,0x10,0x00); Text='vmovups zmm0, zmmword ptr [rax]' }
)) {
    $case = New-AlGuardedTerminalDwordCase "AlGuardAdjacent$($adjacent.Name)"
    [byte[]]$pe = [IO.File]::ReadAllBytes($case.Binary)
    [UInt32]$vexAt = [UInt32]0x0010f980
    Put-Bytes $pe (0x400+$vexAt-[UInt32]0x0010f000) $adjacent.Bytes
    $rows = [IO.File]::ReadAllText($case.Disassembly)
    $rows += "`n$('{0:x}: {1} {2}' -f ([UInt64]0x140000000+$vexAt),(Hex-Bytes $adjacent.Bytes),$adjacent.Text)"
    [IO.File]::WriteAllBytes($case.Binary,$pe)
    Set-Content -LiteralPath $case.Disassembly -Value $rows -Encoding utf8
    Invoke-Control $case $false 7 1
    ++$alGuardCount
}

$alGuardFaults = @(
    'WrongMovzxOpcode','WrongMovzxDestination','WrongMovzxBase','SignedMovsx',
    'WrongCmpOpcode','WrongCmpRegister','RangeTooSmall','RangeTooLarge','RangeZero','Range128','WrongJa',
    'DefaultMidInstruction','DefaultOutsideProcedure','DefaultInsideTable','WrongImageBase',
    'WrongLeaOpcode','WrongLeaDestination','WrongLoadOpcode','WrongLoadDestination','WrongLoadScale','WrongLoadScaleEight',
    'WrongLoadIndex','WrongLoadBase','WrongDispatchIndex','WrongDispatchSource','WrongLoadAddress','WrongAddOpcode','WrongAddRegisters','WrongJumpOpcode',
    'MissingPrefixMovzx','MissingPrefixCmp','MissingPrefixJa','MissingDispatchLea','MissingDispatchLoad','MissingDispatchAdd','MissingDispatchJump',
    'ListingMismatchPrefixMovzx','ListingMismatchPrefixCmp','ListingMismatchPrefixJa',
    'ListingMismatchDispatchLea','ListingMismatchDispatchLoad','ListingMismatchDispatchAdd','ListingMismatchDispatchJump',
    'UnknownPrefixMovzx','UnknownPrefixCmp','UnknownPrefixJa','UnknownDispatchLea','UnknownDispatchLoad','UnknownDispatchAdd','UnknownDispatchJump',
    'OverlapPrefixMovzx','OverlapPrefixCmp','OverlapPrefixJa','OverlapDispatchLea','OverlapDispatchLoad','OverlapDispatchAdd','OverlapDispatchJump',
    'PeMismatchPrefixMovzx','PeMismatchPrefixCmp','PeMismatchPrefixJa',
    'PeMismatchDispatchLea','PeMismatchDispatchLoad','PeMismatchDispatchAdd','PeMismatchDispatchJump',
    'MissingProcedure','WrongProcedureOwner','ProcedureEndsEarly','ProcedureEndsLate','GuardOutsideProcedure',
    'MissingData','WrongDataOwner','WrongDataStart','EarlyDataSymbol','MissingReturn','BadAlignment',
    'BadLastTarget','OutsideTarget','MidInstructionTarget','TargetAtTable'
)
foreach ($fault in $alGuardFaults) {
    Invoke-Control (New-AlGuardedTerminalDwordCase ("AlGuard$fault") $fault) $false
    ++$alGuardCount
}
Write-Host "AL guarded terminal DWORD parser controls passed: $alGuardCount"
