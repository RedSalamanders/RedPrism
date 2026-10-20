[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
. (Join-Path $PSScriptRoot 'IsaInlineTableFixtures.ps1')

# Independent PE/listing/PDB controls for the exact std::format switch observed in Release.
# Numeric instruction bytes are authored here, not obtained from the parser's match patterns.
function New-R10EcxCase([string]$Name, [string]$Fault = '', [int]$Maximum = 13) {
    $case = New-SyntheticCase ($Name + 'Seed') -PdbData
    $case.Name = $Name
    [byte[]]$pe = [IO.File]::ReadAllBytes($case.Binary)
    [UInt32]$prefix = $sectionRva + 0x870
    [UInt32]$begin = $prefix - 5
    [UInt32]$bodyStart = $prefix + 40
    [UInt32]$target = $sectionRva + 0x8c5
    [UInt32]$fallback = $target + 3
    [UInt32]$table = $sectionRva + 0x8e0
    [UInt32]$end = $table + 4 * ($Maximum + 1)
    $rows = [Collections.Generic.List[object]]::new()
    $encodings = @(
        [byte[]]@(0x0f,0xb6,0x01), [byte[]]@(0x4d,0x8b,0xf8), [byte[]]@(0x4c,0x8b,0xf2),
        [byte[]]@(0x4c,0x8b,0xc1), [byte[]]@(0x3c,[byte]$Maximum), [byte[]]@(0x0f,0x87,0,0,0,0),
        [byte[]]@(0x4c,0x8d,0x15,0,0,0,0), [byte[]]@(0x41,0x8b,0x8c,0x82,0,0,0,0),
        [byte[]]@(0x49,0x03,0xca), [byte[]]@(0xff,0xe1)
    )
    $offsets = @(0,3,6,9,12,14,20,27,35,38)
    Put-Bytes $encodings[5] 2 ([BitConverter]::GetBytes([int]($fallback-$prefix-20)))
    Put-Bytes $encodings[6] 3 ([BitConverter]::GetBytes([int](-[int64]($prefix+27))))
    Put-Bytes $encodings[7] 4 ([BitConverter]::GetBytes($table))
    if ($Fault -eq 'ImageBase') { $encodings[6][3]++ }
    if ($Fault -eq 'SignedIndex') { $encodings[0][1] = 0xbe }
    if ($Fault -eq 'ClobberedIndex') { $encodings[1][0] = 0x48; $encodings[1][2] = 0xc2 }
    if ($Fault -eq 'BoundZero') { $encodings[4][1] = 0 }
    if ($Fault -eq 'Bound128') { $encodings[4][1] = 128 }
    if ($Fault -eq 'BoundSmaller') { $encodings[4][1]-- }
    if ($Fault -eq 'BoundLarger') { $encodings[4][1]++ }
    if ($Fault -eq 'WrongBranch') { $encodings[5][1] = 0x86 }
    if ($Fault -eq 'DefaultMiddle') { Put-Bytes $encodings[5] 2 ([BitConverter]::GetBytes([int]($target+1-$prefix-20))) }
    if ($Fault -eq 'DefaultProtected') { Put-Bytes $encodings[5] 2 ([BitConverter]::GetBytes([int](-17))) }
    if ($Fault -eq 'DefaultTable') { Put-Bytes $encodings[5] 2 ([BitConverter]::GetBytes([int]($table-$prefix-20))) }
    if ($Fault -eq 'DefaultOutside') { Put-Bytes $encodings[5] 2 ([BitConverter]::GetBytes([int](-128))) }
    if ($Fault -eq 'WrongLoadRegister') { $encodings[7][2] = 0x84 }
    if ($Fault -eq 'WrongLoadScale') { $encodings[7][3] = 0x42 }
    if ($Fault -eq 'WrongLoadBase') { $encodings[7][3] = 0x83 }
    if ($Fault -eq 'WrongLoadIndex') { $encodings[7][3] = 0x8a }
    if ($Fault -eq 'WrongLoadAddress') { $encodings[7][4]++ }
    if ($Fault -eq 'WrongAdd') { $encodings[8][2] = 0xc2 }
    if ($Fault -eq 'WrongJump') { $encodings[9][1] = 0xe0 }
    [byte[]]$entry = @(0xe9,0,0,0,0)
    [UInt32]$entryTarget = $prefix
    if ($Fault -match '^Bypass(\d+)$') { $entryTarget = $prefix + $offsets[[int]$Matches[1]] }
    Put-Bytes $entry 1 ([BitConverter]::GetBytes([int]($entryTarget-$begin-5)))
    $rows.Add(@{ At=$begin; Bytes=$entry; Name='Entry'; Text='jmp definition' })
    for ($i=0; $i -lt 10; ++$i) { $rows.Add(@{ At=($prefix+$offsets[$i]); Bytes=$encodings[$i]; Name="Protected$i"; Text='control' }) }
    [UInt32]$cursor = $bodyStart
    while ($cursor -lt $table-8) {
        [byte[]]$bytes = if ($cursor -eq $target) { @(0x0f,0x1f,0x00) } else { @(0x90) }
        if ($cursor -eq $bodyStart+1 -and $Fault -in 'BodyVex','BodyEvex') {
            $bytes = if ($Fault -eq 'BodyVex') { [byte[]]@(0xc5,0xf8,0x10,0x00) } else { [byte[]]@(0x62,0xf1,0x7c,0x48,0x10,0x00) }
        }
        $text = if ($bytes[0] -in 0xc5,0x62) { 'vmovups xmm0, [rax]' } else { 'nop' }
        $rows.Add(@{ At=$cursor; Bytes=$bytes; Name="Body$($cursor-$bodyStart)"; Text=$text })
        $cursor += $bytes.Length
    }
    [byte[]]$call = @(0xe8,0,0,0,0)
    Put-Bytes $call 1 ([BitConverter]::GetBytes([int]($bodyStart-$table+3)))
    $rows.Add(@{ At=($table-8); Bytes=$call; Name='Call'; Text='call helper' })
    $rows.Add(@{ At=($table-3); Bytes=[byte[]]@(0xcc); Name='Trap'; Text='int3' })
    $rows.Add(@{ At=($table-2); Bytes=[byte[]]@(0x66,0x90); Name='Padding'; Text='nop' })
    foreach ($row in $rows) { Put-Bytes $pe (0x400+$row.At-$sectionRva) $row.Bytes }
    $listing = [Collections.Generic.List[string]]::new()
    foreach ($row in $rows) {
        if ($Fault -eq "Missing$($row.Name)") { continue }
        [byte[]]$shown = $row.Bytes.Clone()
        if ($Fault -eq "Listing$($row.Name)") { $shown[0] = $shown[0] -bxor 1 }
        if ($Fault -eq "Pe$($row.Name)") { $pe[0x400+$row.At-$sectionRva] = $shown[0] -bxor 1 }
        $text = if ($Fault -eq "Unknown$($row.Name)") { '<unknown>' } else { $row.Text }
        $line = '{0:x}: {1} {2}' -f ($imageBase+$row.At),(Hex-Bytes $shown),$text
        $listing.Add($line)
        if ($Fault -eq "Duplicate$($row.Name)") { $listing.Add($line) }
        if ($Fault -eq "Overlap$($row.Name)") { $listing.Add(('{0:x}: 90 overlap' -f ($imageBase+$row.At+1))) }
    }
    for ($i=0; $i -le $Maximum; ++$i) {
        [UInt32]$destination = $target
        if ($i -eq $Maximum) {
            switch ($Fault) {
                'TargetMiddle' { $destination++ }
                'TargetProtected' { $destination = $prefix+20 }
                'TargetOutside' { $destination = $begin-1 }
                'TargetTable' { $destination = $table }
                'TargetPadding' { $destination = $table-2 }
                'TargetTailCall' { $destination = $table-8 }
                'DefaultInEntries' { $destination = $fallback }
            }
        }
        Set-U32 $pe (0x400+$table-$sectionRva+4*$i) $destination
        $at = 0x400+$table-$sectionRva+4*$i
        [byte[]]$shown = $pe[$at..($at+3)]
        if ($Fault -eq 'ListingTable' -and $i -eq 0) { $shown[0] = $shown[0] -bxor 1 }
        $listing.Add(('{0:x}: {1} vmovups table-data' -f ($imageBase+$table+4*$i),(Hex-Bytes $shown)))
    }
    if ($Fault -in 'AdjacentVex','AdjacentEvex') {
        [byte[]]$vector = if ($Fault -eq 'AdjacentVex') { @(0xc5,0xf8,0x10,0x00) } else { @(0x62,0xf1,0x7c,0x48,0x10,0x00) }
        Put-Bytes $pe (0x400+$end-$sectionRva) $vector
        $listing.Add(('{0:x}: {1} vmovups xmm0, [rax]' -f ($imageBase+$end),(Hex-Bytes $vector)))
    }
    $procOwner = if ($Fault -eq 'ProcedureOwner') { 1 } else { 0 }
    $dataOwner = if ($Fault -eq 'DataOwner') { 1 } else { 0 }
    $procEnd = $end
    if ($Fault -eq 'ProcedureEarly') { $procEnd-=4 }
    if ($Fault -eq 'ProcedureLate') { $procEnd+=4 }
    $data = if ($Fault -eq 'DataAddress') { $table+4 } else { $table }
    $symbols = "  Mod $procOwner | ``SyntheticControl.obj``:`n"
    if ($Fault -ne 'MissingProcedure') { $symbols += "  0 | S_GPROC32 [size = 16] ``R10EcxTerminal```n  addr = 0001:$($begin-$sectionRva), code size = $($procEnd-$begin)`n" }
    $symbols += "  Mod $dataOwner | ``SyntheticControl.obj``:`n"
    if ($Fault -ne 'MissingData') { $symbols += "  0 | S_LDATA32 [size = 16] `````n  type = <no type>, addr = 0001:$($data-$sectionRva)`n" }
    if ($Fault -eq 'InteriorData') { $symbols += "  0 | S_LDATA32 [size = 16] `````n  type = <no type>, addr = 0001:$($table-$sectionRva+4)`n" }
    [IO.File]::WriteAllBytes($case.Binary,$pe)
    Set-Content -LiteralPath $case.Symbols -Value $symbols -Encoding utf8
    Set-Content -LiteralPath $case.Disassembly -Value ($listing -join "`n") -Encoding utf8
    return $case
}

$count = 0
foreach ($maximum in 1,13,127) { Invoke-Control (New-R10EcxCase "R10EcxMaximum$maximum" '' $maximum) $true ($maximum+1) 0; ++$count }
Invoke-Control (New-R10EcxCase 'R10EcxDefaultInEntries' 'DefaultInEntries') $true 13 0
++ $count
foreach ($fault in 'ImageBase','SignedIndex','ClobberedIndex','BoundZero','Bound128','BoundSmaller','BoundLarger','WrongBranch',
    'DefaultMiddle','DefaultProtected','DefaultTable','DefaultOutside','WrongLoadRegister','WrongLoadScale','WrongLoadBase','WrongLoadIndex',
    'WrongLoadAddress','WrongAdd','WrongJump','TargetMiddle','TargetProtected','TargetOutside','TargetTable','TargetPadding','TargetTailCall',
    'MissingProcedure','ProcedureOwner','ProcedureEarly','ProcedureLate','DataOwner','DataAddress','MissingData','InteriorData','ListingTable') {
    Invoke-Control (New-R10EcxCase "R10Ecx$fault" $fault) $false
    ++$count
}
foreach ($row in @('Entry','Call','Trap','Padding','Body0') + (0..9 | ForEach-Object { "Protected$_" })) {
    foreach ($kind in 'Missing','Duplicate','Unknown','Overlap','Listing','Pe') {
        Invoke-Control (New-R10EcxCase "R10Ecx$kind$row" "$kind$row") $false
        ++$count
    }
}
foreach ($i in 1..9) { Invoke-Control (New-R10EcxCase "R10EcxBypass$i" "Bypass$i") $false; ++$count }
foreach ($fault in 'BodyVex','BodyEvex','AdjacentVex','AdjacentEvex') {
    $case = New-R10EcxCase "R10Ecx$fault" $fault
    Invoke-Control $case $false 14 1
    $proof = [RedPrismBuild.IsaScanner]::Scan($case.Binary,$case.Metadata,$case.Symbols,$case.Disassembly,[string[]]@(),[string[]]@(),[string[]]@())
    $expectedEvex = if ($fault -in 'BodyEvex','AdjacentEvex') { 1 } else { 0 }
    if ($proof.EvexInstructions -ne $expectedEvex) { throw "EVEX count mismatch: $fault" }
    ++$count
}
Write-Host "R10 ECX guarded terminal DWORD parser controls passed: $count"
