[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$rsiRepo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
. (Join-Path $rsiRepo 'Tests/BuildProcessTests/IsaInlineTableTests.ps1')

function New-RsiDwordCase([string]$Name, [string]$Fault = '') {
    $case = New-SyntheticCase ($Name + 'Seed') -PdbData
    $pe = [IO.File]::ReadAllBytes($case.Binary)
    [uint32]$section = $script:sectionRva
    [uint32]$prefix = $section + 0x780
    [uint32]$table = $section + 0x900
    [uint32]$target = $section + 0x862
    [uint32]$default = $section + 0x8c5
    [uint32]$end = $table + 24
    # Independent PE/listing fixture from the Release failure: sign-extended RSI, preserved prelude,
    # unsigned ESI guard, RDX image base, six DWORD targets, terminal INT3/NOP, decoded data crossing padding.
    [byte[]]$code = @(0x48,0x63,0xf2,0x48,0x8b,0xf9,0xc7,0x44,0x24,0x20,0,0,0,0,
        0xe8,0,0,0,0,0xc7,0x44,0x24,0x20,1,0,0,0,0x83,0xfe,5,0x0f,0x87,0,0,0,0,
        0x48,0x8d,0x15,0,0,0,0,0x8b,0x8c,0xb2,0,0,0,0,0x48,0x03,0xca,0xff,0xe1)
    Put-Bytes $code 15 ([BitConverter]::GetBytes([int32]($default - ($prefix + 19))))
    Put-Bytes $code 32 ([BitConverter]::GetBytes([int32]($default - ($prefix + 36))))
    Put-Bytes $code 39 ([BitConverter]::GetBytes([int32](-[int64]($prefix + 43))))
    Set-U32 $code 46 $table
    $faultBytes = @{ Producer = 2; PreludeRegister = 5; FirstStackSlot = 9; FirstValue = 10;
        Call = 14; SecondStackSlot = 22; SecondValue = 23; Compare = 28; Guard = 31;
        Base = 38; BaseAddress = 39; Load = 43; Index = 45; Add = 52; Jump = 54 }
    if ($faultBytes.ContainsKey($Fault)) { $code[$faultBytes[$Fault]] = $code[$faultBytes[$Fault]] -bxor 1 }
    if ($Fault -eq 'ZeroMaximum') { $code[29] = 0 }
    if ($Fault -eq 'NegativeMaximum') { $code[29] = 255 }
    if ($Fault -eq 'GuardIntoPrefix') { Put-Bytes $code 32 ([BitConverter]::GetBytes([int32]7)) }
    Put-Bytes $pe (0x400 + $prefix - $section) $code
    $rows = [Collections.Generic.List[string]]::new()
    $starts = @(0,3,6,14,19,27,30,36,43,50,53)
    $lengths = @(3,3,8,5,8,3,6,7,7,3,2)
    foreach ($i in 0..10) {
        if ($Fault -eq "MissingRow$i") { continue }
        [byte[]]$row = $code[$starts[$i]..($starts[$i] + $lengths[$i] - 1)]
        if ($Fault -eq "ListingRow$i") { $row[$row.Length - 1] = $row[$row.Length - 1] -bxor 1 }
        $text = if ($Fault -eq "UnknownRow$i") { '<unknown>' } else { 'guard-dispatch' }
        $rows.Add(('{0:x}: {1} {2}' -f ($script:imageBase + $prefix + $starts[$i]), (Hex-Bytes $row), $text))
    }
    foreach ($address in @($target, $default)) {
        $pe[0x400 + $address - $section] = 0x90
        $text = if ($Fault -eq 'UnknownTarget' -and $address -eq $target) { '<unknown>' } else { 'nop' }
        $rows.Add(('{0:x}: 90 {1}' -f ($script:imageBase + $address), $text))
    }
    if ($Fault -eq 'EntryToGuard' -or $Fault -eq 'EntryToTable') {
        [byte[]]$branch = @(0xe9,0,0,0,0)
        $destination = if ($Fault -eq 'EntryToGuard') { $prefix + 43 } else { $table }
        Put-Bytes $branch 1 ([BitConverter]::GetBytes([int32]($destination - ($default + 6))))
        Put-Bytes $pe (0x400 + $default + 1 - $section) $branch
        $rows.Add(('{0:x}: {1} jmp body' -f ($script:imageBase + $default + 1), (Hex-Bytes $branch)))
    }
    [byte[]]$terminal = @(0xcc,0x66,0x90)
    if ($Fault -eq 'Terminal') { $terminal[0] = 0x90 }
    Put-Bytes $pe (0x400 + $table - 3 - $section) $terminal
    $rows.Add(('{0:x}: {1:x2} int3' -f ($script:imageBase + $table - 3), $terminal[0]))
    $rows.Add(('{0:x}: 66 90 nop' -f ($script:imageBase + $table - 2)))
    foreach ($i in 0..5) {
        $entry = if ($i -eq 5 -and $Fault -eq 'MiddleTarget') { $target + 1 } elseif ($i -eq 5 -and $Fault -eq 'TableTarget') { $table } else { $target }
        Set-U32 $pe (0x400 + $table - $section + 4 * $i) $entry
    }
    $pe[0x400 + $end - $section] = 0xcc
    $pe[0x400 + $end + 1 - $section] = if ($Fault -eq 'NonPaddingSuffix') { 0x90 } else { 0xcc }
    [byte[]]$data = $pe[(0x400 + $table + 16 - $section)..(0x400 + $end + 1 - $section)]
    if ($Fault -eq 'DataListingMismatch') { $data[1] = $data[1] -bxor 1 }
    $rows.Add(('{0:x}: {1} decoded-evex-data' -f ($script:imageBase + $table + 16), (Hex-Bytes $data)))
    if ($Fault -eq 'AdjacentVex') {
        [byte[]]$vex = @(0xc5,0xf8,0x10,0x00)
        Put-Bytes $pe (0x400 + $end + 2 - $section) $vex
        $rows.Add(('{0:x}: {1} vmovups xmm0, xmmword ptr [rax]' -f ($script:imageBase + $end + 2), (Hex-Bytes $vex)))
    }
    $procOwner = if ($Fault -eq 'ProcedureOwner') { 1 } else { 0 }
    $dataOwner = if ($Fault -eq 'DataOwner') { 1 } else { 0 }
    $procEnd = if ($Fault -eq 'ProcedureEnd') { $end - 1 } else { $end }
    $symbols = "  Mod $procOwner | ``Synthetic.obj``:`n"
    if ($Fault -ne 'MissingProcedure') { $symbols += "  0 | S_GPROC32 [size = 16] ``GuardedRsi`` `n  addr = 0001:$($prefix - $section), code size = $($procEnd - $prefix)`n" }
    $symbols += "  Mod $dataOwner | ``Synthetic.obj``:`n"
    if ($Fault -ne 'MissingData') { $symbols += "  0 | S_LDATA32 [size = 16] `````n  type = <no type>, addr = 0001:$($table - $section)`n" }
    if ($Fault -eq 'InteriorData') { $symbols += "  0 | S_LDATA32 [size = 16] `````n  type = <no type>, addr = 0001:$($table + 4 - $section)`n" }
    Set-Content -LiteralPath $case.Symbols -Value $symbols -Encoding utf8
    Set-Content -LiteralPath $case.Disassembly -Value (@($rows | Sort-Object { [uint64]::Parse(($_ -split ':', 2)[0], [Globalization.NumberStyles]::HexNumber) }) -join "`n") -Encoding utf8
    [IO.File]::WriteAllBytes($case.Binary, $pe)
    $case.Name = $Name
    return $case
}

Invoke-Control (New-RsiDwordCase 'RsiExact') $true 1 0
Invoke-Control (New-RsiDwordCase 'RsiAdjacentVex' 'AdjacentVex') $false 1 1
$rsiCount = 2
foreach ($fault in @('Producer','PreludeRegister','FirstStackSlot','FirstValue','Call','SecondStackSlot','SecondValue',
    'Compare','Guard','Base','BaseAddress','Load','Index','Add','Jump','ZeroMaximum','NegativeMaximum','GuardIntoPrefix',
    'UnknownTarget','EntryToGuard','EntryToTable','Terminal','MiddleTarget','TableTarget','NonPaddingSuffix',
    'DataListingMismatch','ProcedureOwner','DataOwner','ProcedureEnd','MissingProcedure','MissingData','InteriorData')) {
    Invoke-Control (New-RsiDwordCase ('Rsi' + $fault) $fault) $false
    ++$rsiCount
}
foreach ($i in 0..10) {
    foreach ($kind in @('Missing','Listing','Unknown')) {
        $fault = "${kind}Row$i"
        Invoke-Control (New-RsiDwordCase ('Rsi' + $fault) $fault) $false
        ++$rsiCount
    }
}
Write-Host "SUMMARY isa-rsi-dword controls=$rsiCount accepted=1 refused=$($rsiCount - 1)"
$global:LASTEXITCODE = 0
