# Dispatch-form controls isolate the recognizers; the default image gate retains PDB-marked proof in IsaMarkedTableTests.ps1.
[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
. (Join-Path $repo 'Tests/BuildProcessTests/IsaInlineTableFixtures.ps1') *> $null
$script:signedRdxCount = 0

function New-SignedRdxControl([string]$Name, [string]$Fault = '') {
    $case = New-SyntheticCase ('SignedRdx-' + $Name) -PdbData
    [byte[]]$pe = [IO.File]::ReadAllBytes($case.Binary)
    [UInt32]$prefix = $script:sectionRva + 0x870
    [UInt32]$dispatch = $prefix + 8
    [UInt32]$dword = $script:sectionRva + 0x1000
    [UInt32]$remap = $dword + 16
    [UInt32]$end = $remap + 32
    [UInt32]$target = $script:sectionRva + 0x8c5
    [byte[]]$sx = @(0x48,0x0f,0xbe,0x85,0xf8,0x01,0,0)
    [byte[]]$lea = @(0x48,0x8d,0x15,0,0,0,0)
    Put-Bytes $lea 3 ([BitConverter]::GetBytes([Int32](-[Int64]($dispatch+7))))
    [byte[]]$byteLoad = @(0x0f,0xb6,0x84,0x02,0,0,0,0)
    Put-Bytes $byteLoad 4 ([BitConverter]::GetBytes([UInt32]($remap+1)))
    [byte[]]$dwordLoad = @(0x8b,0x8c,0x82,0,0,0,0)
    Put-Bytes $dwordLoad 3 ([BitConverter]::GetBytes($dword))
    [byte[]]$add = @(0x48,0x03,0xca)
    [byte[]]$jump = @(0xff,0xe1)
    switch ($Fault) {
        'WrongBiasZero' { Put-Bytes $byteLoad 4 ([BitConverter]::GetBytes($remap)) }
        'WrongBiasTwo' { Put-Bytes $byteLoad 4 ([BitConverter]::GetBytes([UInt32]($remap+2))) }
        'UnsignedIndex' { $sx[2] = 0xb6 }
        'WordIndex' { $sx[2] = 0xbf }
        'WrongFrame' { $sx[3] = 0x87 }
        'WrongIndexRegister' { $sx[0] = 0x49 }
        'WrongImageBase' { $lea[3] = $lea[3] -bxor 1 }
        'WrongRemapBase' { $byteLoad[3] = 0x01 }
        'WrongDwordIndex' { $dwordLoad[2] = 0x8a }
        'WrongAdd' { $add[2] = 0xc2 }
        'WrongJump' { $jump[1] = 0xe0 }
    }
    $parts = @(
        @{Name='Index';At=$prefix;Bytes=$sx}, @{Name='Base';At=$dispatch;Bytes=$lea},
        @{Name='Remap';At=($dispatch+7);Bytes=$byteLoad}, @{Name='Dword';At=($dispatch+15);Bytes=$dwordLoad},
        @{Name='Add';At=($dispatch+22);Bytes=$add}, @{Name='Jump';At=($dispatch+25);Bytes=$jump})
    $rows = [Collections.Generic.List[string]]::new()
    foreach ($part in $parts) {
        [int]$raw = 0x400 + $part.At - $script:sectionRva
        Put-Bytes $pe $raw $part.Bytes
        [byte[]]$listed = $part.Bytes.Clone()
        if ($Fault -eq ('Mismatch'+$part.Name)) { $listed[$listed.Length-1] = $listed[$listed.Length-1] -bxor 1 }
        $text = if ($Fault -eq ('Unknown'+$part.Name)) { '<unknown>' } else { 'scalar dispatch control' }
        $row = '{0:x}: {1} {2}' -f ($script:imageBase+$part.At),(Hex-Bytes $listed),$text
        if ($Fault -ne ('Missing'+$part.Name)) { $rows.Add($row) }
        if ($Fault -eq ('Duplicate'+$part.Name)) { $rows.Add($row) }
        if ($Fault -eq ('Overlap'+$part.Name)) { $rows.Add(('{0:x}: 90 nop' -f ($script:imageBase+$part.At+1))) }
        if ($Fault -eq ('PeMismatch'+$part.Name)) { $pe[$raw] = 0x90 }
    }
    Put-Bytes $pe (0x400+$target-$script:sectionRva) ([byte[]]@(0x66,0x90))
    if ($Fault -ne 'MissingTarget') { $rows.Add(('{0:x}: 66 90 nop' -f ($script:imageBase+$target))) }
    for ($entry=0; $entry -lt 4; ++$entry) {
        [UInt32]$value = $target
        if ($entry -eq 0 -and $Fault -eq 'MiddleTarget') { $value++ }
        if ($entry -eq 0 -and $Fault -eq 'TargetInData') { $value=$dword }
        if ($entry -eq 0 -and $Fault -eq 'TargetOutsideProcedure') { $value=$script:sectionRva+0x1800 }
        [byte[]]$bytes = [BitConverter]::GetBytes($value)
        Put-Bytes $pe (0x400+$dword+4*$entry-$script:sectionRva) $bytes
        # 0x10f8c5 is a literal valid target whose DWORD bytes are a real VEX encoding.
        if ($entry -eq 0 -and $Fault -eq 'TableListingMismatch') { $bytes[1] = $bytes[1] -bxor 1 }
        $rows.Add(('{0:x}: {1} vmovups xmm0, xmmword ptr [rax]' -f ($script:imageBase+$dword+4*$entry),(Hex-Bytes $bytes)))
    }
    for ($index=0; $index -lt 32; ++$index) { $pe[0x400+$remap+$index-$script:sectionRva]=[byte]($index%4) }
    if ($Fault -eq 'InvalidLastRemap') { $pe[0x400+$end-1-$script:sectionRva]=4 }
    if ($Fault -in @('RealVex','RealEvex')) {
        [byte[]]$vector = if ($Fault -eq 'RealVex') { @(0xc5,0xf8,0x10,0x00) } else { @(0x62,0xf1,0x7c,0x48,0x58,0xc0) }
        Put-Bytes $pe (0x400+$target+2-$script:sectionRva) $vector
        $rows.Add(('{0:x}: {1} vector instruction in code' -f ($script:imageBase+$target+2),(Hex-Bytes $vector)))
    }
    $module = "  Mod 0000 | ``SyntheticControl.obj``:`n  Obj: ``Synthetic.lib``:`n"
    $contributionSize = $end-$script:sectionRva
    if ($Fault -eq 'ContributionEnd') { $contributionSize++ }
    [IO.File]::WriteAllText($case.Metadata,"  Age: $script:age`n  GUID: {$script:guid}`n$module`nSection Contributions`n  SC[.text] | mod = 0, 0001:0, size = $contributionSize, data crc = 0, reloc crc = 0`n")
    $procedureSize = $end-$script:sectionRva
    if ($Fault -eq 'ProcedureEnd') { $procedureSize-- }
    $symbols = $module
    if ($Fault -ne 'MissingProcedure') { $symbols += "  0 | S_GPROC32 [size = 40] ``SyntheticSignedRdx```n    addr = 0001:0, code size = $procedureSize`n" }
    if ($Fault -ne 'MissingDwordSymbol') { $symbols += "  0 | S_LDATA32 [size = 16] `````n  type = <no type>, addr = 0001:$($dword-$script:sectionRva)`n" }
    if ($Fault -ne 'MissingRemapSymbol') { $symbols += "  0 | S_LDATA32 [size = 16] `````n  type = <no type>, addr = 0001:$($remap-$script:sectionRva)`n" }
    if ($Fault -eq 'InteriorDataSymbol') { $symbols += "  0 | S_LDATA32 [size = 16] `````n  type = <no type>, addr = 0001:$($remap-$script:sectionRva+1)`n" }
    [IO.File]::WriteAllText($case.Symbols,$symbols)
    [IO.File]::WriteAllText($case.Disassembly,(@($rows | Sort-Object { [UInt64]::Parse(($_ -split ':',2)[0],[Globalization.NumberStyles]::HexNumber) }) -join "`n"))
    [IO.File]::WriteAllBytes($case.Binary,$pe)
    return $case
}

function Assert-SignedRdx($Case,[bool]$ExpectedPass) {
    $result = [RedPrismBuild.IsaScanner]::Scan($Case.Binary,$Case.Metadata,$Case.Symbols,$Case.Disassembly,[string[]]@(),[string[]]@(),[string[]]@(), $false)
    if ($ExpectedPass -and ($result.Violations.Count -ne 0 -or $result.InlineTableVectorData -ne 4 -or $result.VectorInstructions -ne 0)) { throw "$($Case.Name): expected exactly four proved data rows: $($result | ConvertTo-Json -Compress)" }
    if (-not $ExpectedPass -and $result.Violations.Count -eq 0) { throw "$($Case.Name): malformed data or executable vector instruction bypassed the ISA gate" }
    ++$script:signedRdxCount
    Write-Host "PASS isa-signed-rdx-remap.$($Case.Name)"
}
Assert-SignedRdx (New-SignedRdxControl 'Valid') $true
foreach ($fault in @('WrongBiasZero','WrongBiasTwo','UnsignedIndex','WordIndex','WrongFrame','WrongIndexRegister','WrongImageBase','WrongRemapBase','WrongDwordIndex','WrongAdd','WrongJump','MiddleTarget','TargetInData','TargetOutsideProcedure','MissingTarget','TableListingMismatch','InvalidLastRemap','RealVex','RealEvex','ContributionEnd','ProcedureEnd','MissingProcedure','MissingDwordSymbol','MissingRemapSymbol','InteriorDataSymbol')) {
    Assert-SignedRdx (New-SignedRdxControl $fault $fault) $false
}
foreach ($step in 'Index','Base','Remap','Dword','Add','Jump') {
    foreach ($kind in 'Missing','Duplicate','Unknown','Overlap','Mismatch','PeMismatch') {
        $fault=$kind+$step
        Assert-SignedRdx (New-SignedRdxControl $fault $fault) $false
    }
}
if ($script:signedRdxCount -ne 62) { throw 'Signed RDX parser registry differs' }
Write-Host 'PASS isa-signed-rdx-remap: 62 controls'
