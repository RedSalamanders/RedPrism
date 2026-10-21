# Dispatch-form controls isolate the recognizers; the default image gate retains PDB-marked proof in IsaMarkedTableTests.ps1.
[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
. (Join-Path $repo 'Tests/BuildProcessTests/IsaInlineTableFixtures.ps1') *> $null
$script:rdxDwordCount = 0

# Independent synthetic parser controls for the exact 27-byte RDX signed-index dispatch.
function New-RdxDwordControl([string]$Name, [string]$Fault = '') {
    $case = New-SyntheticCase ('RdxDword-' + $Name) -PdbData
    [byte[]]$pe = [IO.File]::ReadAllBytes($case.Binary)
    [UInt32]$prefix = $script:sectionRva + 0x870
    [UInt32]$leaAt = $prefix + 8
    [UInt32]$loadAt = $leaAt + 7
    [UInt32]$addAt = $loadAt + 7
    [UInt32]$jumpAt = $addAt + 3
    [UInt32]$afterJump = $jumpAt + 2
    [UInt32]$biasRva = $script:sectionRva + 0x1000
    [UInt32]$tableRva = $biasRva + 4
    [UInt32]$target = $script:sectionRva + 0x8c5
    [byte[]]$sx = @(0x48,0x0f,0xbe,0x85,0xf8,0x01,0,0)
    [byte[]]$lea = @(0x48,0x8d,0x15,0,0,0,0)
    Put-Bytes $lea 3 ([BitConverter]::GetBytes([Int32](-[Int64]($leaAt+7))))
    [byte[]]$load = @(0x8b,0x84,0x82,0,0,0,0)
    Put-Bytes $load 3 ([BitConverter]::GetBytes($tableRva))
    [byte[]]$add = @(0x48,0x03,0xc2)
    [byte[]]$jump = @(0xff,0xe0)
    switch ($Fault) {
        'WrongBias' { Put-Bytes $load 3 ([BitConverter]::GetBytes([UInt32]($tableRva-3))) }
        'WrongLoadDestination' { $load[1] = 0x8c }
        'WrongSib' { $load[2] = 0x8a }
        'WrongLoadWidth' { $load[0] = 0x89 }
        'UnsignedIndex' { $sx[2] = 0xb6 }
        'WrongMovsxDestination' { $sx[0] = 0x49 }
        'WrongRex' { $sx[0] = 0x40 }
        'WrongFrame' { $sx[3] = 0x87 }
        'Disp8' { $sx = [byte[]]@(0x48,0x0f,0xbe,0x45,0xf8) }
        'WrongImageBase' { $lea[3] = $lea[3] -bxor 1 }
        'WrongLeaRegister' { $lea[2] = 0x0d }
        'WrongAddRegister' { $add[2] = 0xca }
        'WrongJumpRegister' { $jump[1] = 0xe1 }
    }
    # A real one-byte scalar gap moves every subsequent instruction, with its PE bytes/listing intact.
    $gapNames=@('GapBeforeLea','GapBeforeLoad','GapBeforeAdd','GapBeforeJump')
    [UInt32]$gapAt=0
    if($Fault -in $gapNames) {
        $gapAt=switch($Fault){'GapBeforeLea'{$leaAt} 'GapBeforeLoad'{$loadAt} 'GapBeforeAdd'{$addAt} 'GapBeforeJump'{$jumpAt}}
        if($leaAt -ge $gapAt){++$leaAt}; if($loadAt -ge $gapAt){++$loadAt}; if($addAt -ge $gapAt){++$addAt}; if($jumpAt -ge $gapAt){++$jumpAt}; ++$afterJump
        Put-Bytes $lea 3 ([BitConverter]::GetBytes([Int32](-[Int64]($leaAt+7))))
        Put-Bytes $pe (0x400+$gapAt-$script:sectionRva) ([byte[]]@(0x90))
    }
    $parts = @(
        @{Name='Index';At=$prefix;Bytes=$sx}, @{Name='Lea';At=$leaAt;Bytes=$lea},
        @{Name='Load';At=$loadAt;Bytes=$load}, @{Name='Add';At=$addAt;Bytes=$add},
        @{Name='Jump';At=$jumpAt;Bytes=$jump})
    $rows = [Collections.Generic.List[string]]::new()
    foreach ($part in $parts) {
        [int]$raw = 0x400 + $part.At - $script:sectionRva
        Put-Bytes $pe $raw $part.Bytes
        [byte[]]$listed = $part.Bytes.Clone()
        if ($Fault -eq ('Mismatch'+$part.Name)) { $listed[$listed.Length-1] = $listed[$listed.Length-1] -bxor 1 }
        $text = if ($Fault -eq ('Unknown'+$part.Name)) { '<unknown>' } else { 'dispatch control' }
        $row = '{0:x}: {1} {2}' -f ($script:imageBase+$part.At),(Hex-Bytes $listed),$text
        if ($Fault -ne ('Missing'+$part.Name)) { $rows.Add($row) }
        if ($Fault -eq ('Duplicate'+$part.Name)) { $rows.Add($row) }
        if ($Fault -eq ('Overlap'+$part.Name)) { $rows.Add(('{0:x}: 90 nop' -f ($script:imageBase+$part.At+1))) }
        if ($Fault -eq ('PeMismatch'+$part.Name)) { $pe[$raw] = $pe[$raw] -bxor 1 }
    }
    if($gapAt){$rows.Add(('{0:x}: 90 nop' -f ($script:imageBase+$gapAt)))}
    Put-Bytes $pe (0x400+$target-$script:sectionRva) ([byte[]]@(0x66,0x90))
    if ($Fault -ne 'MissingTarget') { $rows.Add(('{0:x}: 66 90 nop' -f ($script:imageBase+$target))) }
    # Bias slot represents signed index -1; remaining slots represent nonnegative indices.
    [UInt32[]]$targets = @($target,$target,$target,$target)
    if ($Fault -eq 'WrongDefaultTarget') { $targets[0] = $target+1 }
    if ($Fault -eq 'WrongOtherTarget') { $targets[2] = $script:sectionRva+0x8d0 }
    if ($Fault -eq 'InteriorTarget') { $targets[1] = $target+1 }
    if ($Fault -eq 'TargetBeforeJump') { $targets[1] = $prefix }
    if ($Fault -eq 'TargetInData') { $targets[1] = $biasRva }
    if ($Fault -eq 'TargetOutsideProcedure') { $targets[1] = $script:sectionRva+0x1800 }
    for ($i=0; $i -lt $targets.Length; ++$i) {
        [byte[]]$entry = [BitConverter]::GetBytes($targets[$i])
        Put-Bytes $pe (0x400+$biasRva+4*$i-$script:sectionRva) $entry
        $rows.Add(('{0:x}: {1} table dword' -f ($script:imageBase+$biasRva+4*$i),(Hex-Bytes $entry)))
    }
    if ($Fault -eq 'RealVexAdjacent') {
        [byte[]]$vex = @(0xc5,0xf8,0x10,0x00)
        Put-Bytes $pe (0x400+$afterJump-$script:sectionRva) $vex
        $rows.Add(('{0:x}: {1} vmovups xmm0, xmmword ptr [rax]' -f ($script:imageBase+$afterJump),(Hex-Bytes $vex)))
    }
    $module = "  Mod 0000 | ``SyntheticControl.obj``:`n  Obj: ``Synthetic.lib``:`n"
    [UInt32]$contributionEnd = $biasRva + 16
    if ($Fault -eq 'ContributionBounds') { $contributionEnd = $biasRva + 12 }
    [IO.File]::WriteAllText($case.Metadata,"  Age: $script:age`n  GUID: {$script:guid}`n$module`nSection Contributions`n  SC[.text] | mod = 0, 0001:0, size = $($contributionEnd-$script:sectionRva), data crc = 0, reloc crc = 0`n")
    $symbols = $module + "  0 | S_GPROC32 [size = 40] ``SyntheticRdxDword```n    addr = 0001:0, code size = $($contributionEnd-$script:sectionRva)`n"
    if ($Fault -ne 'MissingOwnerSymbol') {
        [UInt32]$symbolAt = if ($Fault -eq 'WrongOwnerSymbol') { $biasRva+4 } else { $biasRva }
        if($Fault -eq 'ForeignOwnerSymbol'){$symbols += "  Mod 0001 | ``ForeignControl.obj``:`n  Obj: ``Synthetic.lib``:`n"}
        $dataName=if($Fault -eq 'NamedDataSymbol'){'named-data'}else{''}; $dataType=if($Fault -eq 'TypedDataSymbol'){'0x1234 (unsigned int)'}else{'<no type>'}
        $symbols += "  0 | S_LDATA32 [size = 16] ``$dataName```n  type = $dataType, addr = 0001:$($symbolAt-$script:sectionRva)`n"
    }
    [IO.File]::WriteAllText($case.Symbols,$symbols)
    [IO.File]::WriteAllText($case.Disassembly,(@($rows | Sort-Object { [UInt64]::Parse(($_ -split ':',2)[0],[Globalization.NumberStyles]::HexNumber) }) -join "`n"))
    [IO.File]::WriteAllBytes($case.Binary,$pe)
    return $case
}
function Assert-RdxDword($Case,[bool]$ExpectedPass) {
    $result = [RedPrismBuild.IsaScanner]::Scan($Case.Binary,$Case.Metadata,$Case.Symbols,$Case.Disassembly,[string[]]@(),[string[]]@(),[string[]]@(), $false)
    if ($ExpectedPass -and ($result.Violations.Count -ne 0 -or $result.InlineTableVectorData -ne 4 -or $result.VectorInstructions -ne 0)) { throw "$($Case.Name): expected four proved data rows: $($result | ConvertTo-Json -Compress)" }
    if (-not $ExpectedPass -and $result.Violations.Count -eq 0) { throw "$($Case.Name): invalid signed RDX table or executable vector instruction passed" }
    ++$script:rdxDwordCount
    Write-Host "PASS isa-signed-rdx-dword.$($Case.Name)"
}
Assert-RdxDword (New-RdxDwordControl 'ValidBiasAndDefault') $true
$faults = @('WrongBias','WrongLoadDestination','WrongSib','WrongLoadWidth','UnsignedIndex','WrongMovsxDestination','WrongRex','WrongFrame','Disp8','WrongImageBase','WrongLeaRegister','WrongAddRegister','WrongJumpRegister','WrongDefaultTarget','WrongOtherTarget','InteriorTarget','TargetBeforeJump','TargetInData','TargetOutsideProcedure','MissingTarget','MissingOwnerSymbol','WrongOwnerSymbol','ForeignOwnerSymbol','NamedDataSymbol','TypedDataSymbol','ContributionBounds','RealVexAdjacent','GapBeforeLea','GapBeforeLoad','GapBeforeAdd','GapBeforeJump')
foreach ($fault in $faults) { Assert-RdxDword (New-RdxDwordControl $fault $fault) $false }
foreach ($step in 'Index','Lea','Load','Add','Jump') {
    foreach ($kind in 'Missing','Duplicate','Unknown','Overlap','Mismatch','PeMismatch') {
        $fault=$kind+$step
        Assert-RdxDword (New-RdxDwordControl $fault $fault) $false
    }
}
if ($script:rdxDwordCount -ne 62) { throw "RDX signed DWORD control registry differs: $script:rdxDwordCount" }
Write-Host 'PASS isa-signed-rdx-dword: 62 controls'


