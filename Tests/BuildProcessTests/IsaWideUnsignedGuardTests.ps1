[CmdletBinding()]
param([string]$ScannerSource)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Reuse independent PE/PDB primitives and retain the earlier controls with the current scanner.
$wideUnsignedRepo = $PSScriptRoot
while (-not (Test-Path -LiteralPath (Join-Path $wideUnsignedRepo 'Tests\BuildProcessTests\IsaInlineTableTests.ps1'))) {
    $parent = Split-Path -Parent $wideUnsignedRepo
    if (-not $parent -or $parent -eq $wideUnsignedRepo) { throw 'C-checkout fixture root not found' }
    $wideUnsignedRepo = $parent
}
$wideUnsignedScanner = if ([string]::IsNullOrWhiteSpace($ScannerSource)) {
    Join-Path $wideUnsignedRepo 'Build\IsaScan.cs'
} else { $ScannerSource }
if (-not (Test-Path -LiteralPath $wideUnsignedScanner)) { throw "Private scanner missing: $wideUnsignedScanner" }
$wideUnsignedScanner = (Resolve-Path -LiteralPath $wideUnsignedScanner).Path
if (-not ('RedPrismBuild.IsaScanner' -as [type])) { Add-Type -Path $wideUnsignedScanner }
. (Join-Path $wideUnsignedRepo 'Tests\BuildProcessTests\IsaWideStackGuardTests.ps1')

function Set-WideUnsignedU16([byte[]]$Bytes,[int]$Offset,[UInt16]$Value) { [Array]::Copy([BitConverter]::GetBytes($Value),0,$Bytes,$Offset,2) }
function Set-WideUnsignedU32([byte[]]$Bytes,[int]$Offset,[UInt32]$Value) { [Array]::Copy([BitConverter]::GetBytes($Value),0,$Bytes,$Offset,4) }
function Set-WideUnsignedU64([byte[]]$Bytes,[int]$Offset,[UInt64]$Value) { [Array]::Copy([BitConverter]::GetBytes($Value),0,$Bytes,$Offset,8) }
function Put-WideUnsignedBytes([byte[]]$Bytes,[int]$Offset,[byte[]]$Value) { [Array]::Copy($Value,0,$Bytes,$Offset,$Value.Length) }
function Format-WideUnsignedBytes([byte[]]$Bytes) { (($Bytes | ForEach-Object { $_.ToString('x2') }) -join ' ') }

function New-WideUnsignedFixture {
    param([Parameter(Mandatory)][string]$Name,[ValidateRange(1,255)][int]$Maximum=14,[string]$Fault='')
    $dir=Join-Path $fixtureRoot ('WideUnsigned-'+$Name)
    [void](New-Item -ItemType Directory -Path $dir -Force)
    $binary=Join-Path $dir 'Synthetic.exe'; $metadata=Join-Path $dir 'contributions.txt'
    $symbols=Join-Path $dir 'symbols.txt'; $disassembly=Join-Path $dir 'disassembly.txt'
    [UInt64]$base=0x140000000; [UInt32]$section=0x0010f000
    [UInt32]$prefix=$section+0x800; [UInt32]$dispatch=$prefix+21
    [UInt32]$afterJump=$dispatch+19; [UInt32]$table=$section+0x1000
    [UInt32]$target=$section+0x8c5; [UInt32]$validTarget=$target; [UInt32]$end=$table+4*($Maximum+1)
    $pe=[byte[]]::new(0x2400); $pe[0]=0x4d; $pe[1]=0x5a
    Set-WideUnsignedU32 $pe 0x3c 0x80; Set-WideUnsignedU32 $pe 0x80 0x4550
    Set-WideUnsignedU16 $pe 0x84 0x8664; Set-WideUnsignedU16 $pe 0x86 1
    Set-WideUnsignedU16 $pe 0x94 0xf0; Set-WideUnsignedU16 $pe 0x98 0x20b
    Set-WideUnsignedU64 $pe (0x98+24) $base
    Set-WideUnsignedU32 $pe (0x98+56) 0x2000; Set-WideUnsignedU32 $pe (0x98+60) 0x400
    Set-WideUnsignedU32 $pe (0x98+112+6*8) ($section+0x180); Set-WideUnsignedU32 $pe (0x98+116+6*8) 28
    $sh=0x80+24+0xf0; Put-WideUnsignedBytes $pe $sh ([Text.Encoding]::ASCII.GetBytes('.text'+[char]0+[char]0+[char]0))
    Set-WideUnsignedU32 $pe ($sh+8) 0x2000; Set-WideUnsignedU32 $pe ($sh+12) $section
    Set-WideUnsignedU32 $pe ($sh+16) 0x2000; Set-WideUnsignedU32 $pe ($sh+20) 0x400
    Set-WideUnsignedU32 $pe ($sh+36) 0x60000020
    Set-WideUnsignedU32 $pe (0x400+0x180+12) 2; Set-WideUnsignedU32 $pe (0x400+0x180+16) 24
    Set-WideUnsignedU32 $pe (0x400+0x180+20) ($section+0x1a0); Set-WideUnsignedU32 $pe (0x400+0x180+24) 0x5a0
    $guid=[guid]'9c1e2d4b-6a70-4a8b-9012-3456789abcde'; [UInt32]$age=7
    Put-WideUnsignedBytes $pe 0x5a0 ([Text.Encoding]::ASCII.GetBytes('RSDS'))
    Put-WideUnsignedBytes $pe 0x5a4 $guid.ToByteArray(); Set-WideUnsignedU32 $pe 0x5b4 $age

    # Exact observed guard: CMP [RSP+disp32],imm8 (8), JA rel32 (6), MOV EAX,[RSP+same disp32] (7).
    [byte[]]$guard=@(0x83,0xbc,0x24,0x70,0x01,0,0,0x0e,0x0f,0x87,0,0,0,0,0x8b,0x84,0x24,0x70,0x01,0,0)
    $guard[7]=[byte]$Maximum
    [byte[]]$dispatchBytes=@(0x48,0x8d,0x0d,0,0,0,0,0x8b,0x84,0x81,0,0,0,0,0x48,0x03,0xc1,0xff,0xe0)
    Put-WideUnsignedBytes $dispatchBytes 3 ([BitConverter]::GetBytes([Int32]($base-($base+$dispatch+7))))
    Put-WideUnsignedBytes $dispatchBytes 10 ([BitConverter]::GetBytes($table))
    [Int32]$defaultDisp=[Int32]($target-($prefix+14)); Put-WideUnsignedBytes $guard 10 ([BitConverter]::GetBytes($defaultDisp))

    switch($Fault) {
        'GuardOpcode' {$guard[0]=0x81}
        'GuardRegister' {$guard[15]=0x89}
        'GuardSib' {$guard[2]=0x25}
        'StackSlotFourBytes' {$guard[17]=0x74}
        'GuardBranch' {$guard[9]=0x86}
        'GuardDefault' { Put-WideUnsignedBytes $guard 10 ([BitConverter]::GetBytes([Int32]($table-($prefix+14)))) }
        'ImageBase' {$dispatchBytes[3]=[byte]($dispatchBytes[3]+1)}
        'TableLoad' {$dispatchBytes[9]=0x91}
        'TableAdd' {$dispatchBytes[16]=0xc2}
        'TableJump' {$dispatchBytes[18]=0xe1}
        'Maximum128' {$guard[7]=128; $Maximum=128; $end=$table+4*($Maximum+1)}
    }
    $rawPrefix=0x400+$prefix-$section; $rawDispatch=0x400+$dispatch-$section
    Put-WideUnsignedBytes $pe $rawPrefix $guard; Put-WideUnsignedBytes $pe $rawDispatch $dispatchBytes
    # A complete decoded body lies between the dispatch and RET immediately preceding table data.
    for([UInt32]$rva=$afterJump;$rva -lt $table-1;$rva++){ $pe[0x400+$rva-$section]=0x90 }
    $pe[0x400+$table-$section-1]=0xc3
    $pe[0x400+$validTarget-$section]=0x66; $pe[0x400+$validTarget+1-$section]=0x90
    [UInt32]$outside=$table+4*($Maximum+1)+0x10
    $pe[0x400+$outside-$section]=0x90
    if($Fault -eq 'BodyUnknown'){ $pe[0x400+$afterJump+9-$section]=0x06 }
    if($Fault -eq 'BodyBranchIntoTable') { $branchAt=$afterJump+9; $pe[0x400+$branchAt-$section]=0xe9; Put-WideUnsignedBytes $pe (0x400+$branchAt+1-$section) ([BitConverter]::GetBytes([Int32]($table-($branchAt+5)))) }
    $extraBranchAt=$table-32
    if($Fault -eq 'BodyLoopIntoTable') {
        Put-WideUnsignedBytes $pe (0x400+$extraBranchAt-$section) ([byte[]]@(0xe2,0x1e))
    }
    if($Fault -eq 'BodyPrefixedBranchIntoTable') {
        Put-WideUnsignedBytes $pe (0x400+$extraBranchAt-$section) ([byte[]]@(0x66,0xe9,0x1a,0,0,0))
    }
    if($Fault -eq 'AvxOutsideTable' -or $Fault -eq 'TrailingTableBytes') {
        [byte[]]$vex=@(0xc5,0xf8,0x10,0x00); $vexAt=if($Fault -eq 'TrailingTableBytes'){$end}else{$outside}
        Put-WideUnsignedBytes $pe (0x400+$vexAt-$section) $vex
    }
    if($Fault -eq 'OutsideTarget') { $target=$outside }
    if($Fault -eq 'CaseTargetMiddle') { $target++ }
    if($Fault -eq 'RET') { $pe[0x400+$table-$section-1]=0x90 }
    if($Fault -eq 'TableExtent') { Set-WideUnsignedU32 $pe ($sh+8) 0x1000; Set-WideUnsignedU32 $pe ($sh+16) 0x1000; Set-WideUnsignedU32 $pe ($sh+20) 0x1000 }
    if($Fault -eq 'Contribution') { $end-=4 }

    $rows=[Collections.Generic.List[string]]::new()
    $rows.Add(('{0:x}: {1} cmp dword ptr [rsp+170h], {2}' -f ($base+$prefix),(Format-WideUnsignedBytes $guard[0..7]),$Maximum))
    $rows.Add(('{0:x}: {1} ja default' -f ($base+$prefix+8),(Format-WideUnsignedBytes $guard[8..13])))
    $rows.Add(('{0:x}: {1} mov eax, dword ptr [rsp+170h]' -f ($base+$prefix+14),(Format-WideUnsignedBytes $guard[14..20])))
    $dispatchOffsets=@(0,7,14,17); $dispatchLengths=@(7,7,3,2)
    for($d=0;$d -lt 4;$d++){$o=$dispatchOffsets[$d];$n=$dispatchLengths[$d];$rows.Add(('{0:x}: {1} dispatch' -f ($base+$dispatch+$o),(Format-WideUnsignedBytes $dispatchBytes[$o..($o+$n-1)])))}
    for([UInt32]$rva=$afterJump;$rva -lt $table-1;$rva++){
        if($Fault -eq 'BodyGap' -and $rva -eq $afterJump+12){continue}
        if($Fault -eq 'BodyBranchIntoTable' -and $rva -gt $afterJump+9 -and $rva -lt $afterJump+14){continue}
        if($rva -eq $validTarget+1){continue}
        $extraLength=if($Fault -eq 'BodyLoopIntoTable'){2}elseif($Fault -eq 'BodyPrefixedBranchIntoTable'){6}else{0}
        if($extraLength -and $rva -gt $extraBranchAt -and $rva -lt $extraBranchAt+$extraLength){continue}
        $len=if($Fault -eq 'BodyBranchIntoTable' -and $rva -eq $afterJump+9){5}elseif($rva -eq $validTarget){2}else{1}
        if($extraLength -and $rva -eq $extraBranchAt){$len=$extraLength}
        $b=[byte[]]$pe[(0x400+$rva-$section)..(0x400+$rva-$section+$len-1)]
        if($extraLength -and $rva -eq $extraBranchAt){$rows.Add((('{0:x}: {1} '+$(if($extraLength -eq 2){'loop table'}else{'jmp table'})) -f ($base+$rva),(Format-WideUnsignedBytes $b)))}
        elseif($Fault -eq 'BodyUnknown' -and $rva -eq $afterJump+9){$rows.Add(('{0:x}: {1} <unknown>' -f ($base+$rva),(Format-WideUnsignedBytes $b)))}
        elseif($Fault -eq 'BodyBranchIntoTable' -and $rva -eq $afterJump+9){$rows.Add(('{0:x}: {1} jmp table' -f ($base+$rva),(Format-WideUnsignedBytes $b)))}
        elseif($rva -eq $validTarget){$rows.Add(('{0:x}: {1} nop' -f ($base+$rva),(Format-WideUnsignedBytes $b)))}
        else {$rows.Add(('{0:x}: {1} nop' -f ($base+$rva),(Format-WideUnsignedBytes $b)))}
    }
    $rows.Add(('{0:x}: {1} ret' -f ($base+$table-1),(Format-WideUnsignedBytes ([byte[]]@($pe[0x400+$table-$section-1])))))
    for($entry=0;$entry -le $Maximum -and $entry -lt 128;$entry++){
        $value=[UInt32]$validTarget
        if($Fault -in @('OutsideTarget','CaseTargetMiddle') -and $entry -eq 0){$value=$target}
        if($Fault -eq 'CaseTargetMiddle' -and $entry -eq 0){$value=$target}
        Set-WideUnsignedU32 $pe (0x400+$table-$section+4*$entry) $value
        [byte[]]$entryBytes=$pe[(0x400+$table-$section+4*$entry)..(0x400+$table-$section+4*$entry+3)]
        if($Fault -eq 'Mismatch' -and $entry -eq 1){$entryBytes[0]=$entryBytes[0] -bxor 1}
        $rows.Add(('{0:x}: {1} vmovups xmm0, xmmword ptr [rax]' -f ($base+$table+4*$entry),(Format-WideUnsignedBytes $entryBytes)))
    }
    if($Fault -eq 'Overlap'){$rows.Add(('{0:x}: 90 overlapping-row' -f ($base+$validTarget+1)))}
    if($Fault -eq 'TrailingTableBytes'){$rows.Add(('{0:x}: c5 f8 10 00 vmovups xmm0, xmmword ptr [rax]' -f ($base+$end)))}
    elseif($Fault -eq 'AvxOutsideTable'){$rows.Add(('{0:x}: c5 f8 10 00 vmovups xmm0, xmmword ptr [rax]' -f ($base+$outside)))}

    $contributionEnd=0x2000
    if($Fault -eq 'Contribution'){$contributionEnd=$end-$section}
    $metadataText="  Age: $age`n  GUID: {$guid}`n  Mod 0000 | ``SyntheticControl.obj``:`n  Obj: ``SyntheticControl.obj``:`n`nSection Contributions`n  SC[.text]  | mod = 0, 0001:0, size = $contributionEnd, data crc = 0, reloc crc = 0`n"
    Set-Content -LiteralPath $metadata -Value $metadataText -Encoding utf8
    $procedureEnd=if($Fault -eq 'Procedure'){$table+4*($Maximum+1)-4}else{$table+4*($Maximum+1)+0x20}
    $symbolsText="  Mod 0 | ``SyntheticControl.obj``:`n  0 | S_GPROC32 [size = 16] ``GuardedWideUnsigned`` `n  addr = 0001:$($prefix-$section), code size = $($procedureEnd-$prefix)`n"
    if($Fault -eq 'InsideAnonymousSymbol'){$symbolsText+="  0 | S_LDATA32 [size = 16] `````n  type = <no type>, addr = 0001:$($table-$section+4)`n"}
    # No data symbol at the table start; this unrelated anonymous symbol is later in the same module.
    $symbolsText+="  Mod 0 | ``SyntheticControl.obj``:`n  0 | S_LDATA32 [size = 16] `````n  type = <no type>, addr = 0001:6144`n"
    Set-Content -LiteralPath $symbols -Value $symbolsText -Encoding utf8
    if($Fault -eq 'PEListing'){$dispatchAddress='{0:x}: ' -f ($base+$dispatch); for($j=0;$j -lt $rows.Count;$j++){if($rows[$j].StartsWith($dispatchAddress,[StringComparison]::Ordinal)){$rows[$j]=$rows[$j].Replace('48 8d 0d','48 8d 0c');break}}}
    Set-Content -LiteralPath $disassembly -Value (@($rows|Sort-Object{[UInt64]::Parse(($_ -split ':',2)[0],[Globalization.NumberStyles]::HexNumber)}) -join "`n") -Encoding utf8
    [IO.File]::WriteAllBytes($binary,$pe)
    return @{Name=$Name;Binary=$binary;Metadata=$metadata;Symbols=$symbols;Disassembly=$disassembly}
}

$wideUnsignedCount=0
function Invoke-WideUnsignedControl($Name,[bool]$ShouldPass,[int]$Maximum=14,[string]$Fault='',[int]$ExpectedInline=-1,[int]$ExpectedVectors=-1){
    $case=New-WideUnsignedFixture -Name $Name -Maximum $Maximum -Fault $Fault
    $result=[RedPrismBuild.IsaScanner]::Scan($case.Binary,$case.Metadata,$case.Symbols,$case.Disassembly,[string[]]@(),[string[]]@(),[string[]]@())
    if($ShouldPass -and $result.Violations.Count -ne 0){throw "$Name unexpectedly failed: $($result.Violations -join '; ')"}
    if(-not $ShouldPass -and $result.Violations.Count -eq 0){throw "$Name was accepted despite its named fault."}
    if($ExpectedInline -ge 0 -and $result.InlineTableVectorData -ne $ExpectedInline){throw "${Name}: expected $ExpectedInline inline data rows, got $($result.InlineTableVectorData)."}
    if($ExpectedVectors -ge 0 -and $result.VectorInstructions -ne $ExpectedVectors){throw "${Name}: expected $ExpectedVectors ordinary vectors, got $($result.VectorInstructions)."}
    Write-Host "PASS isa-wide-unsigned.$Name"
    $script:wideUnsignedCount++
}

Invoke-WideUnsignedControl 'Maximum14' $true 14 '' 15 0
Invoke-WideUnsignedControl 'SmallMaximum1' $true 1 '' 2 0
Invoke-WideUnsignedControl 'Boundary127' $true 127 '' 128 0
$wideUnsignedFaults=@(
    @('GuardOpcode','GuardOpcode'),@('GuardRegister','GuardRegister'),@('GuardSib','GuardSib'),@('SameSlotFourByteMismatch','StackSlotFourBytes'),
    @('BranchCondition','GuardBranch'),@('DefaultTarget','GuardDefault'),@('ImageBase','ImageBase'),@('LoadForm','TableLoad'),@('AddForm','TableAdd'),@('JumpRegister','TableJump'),
    @('PEListingMismatch','PEListing'),@('OverlappingRows','Overlap'),@('UnknownBodyRow','BodyUnknown'),@('BodyGap','BodyGap'),@('RetMismatch','RET'),
    @('CaseTargetMiddle','CaseTargetMiddle'),@('OutsideTarget','OutsideTarget'),@('Maximum128TableExtent','Maximum128'),@('ContributionExtent','Contribution'),
    @('ProcedureExtent','Procedure'),@('InteriorAnonymousSymbol','InsideAnonymousSymbol'),@('DirectBodyBranchIntoTable','BodyBranchIntoTable'),
    @('LoopBodyBranchIntoTable','BodyLoopIntoTable'),@('PrefixedBodyBranchIntoTable','BodyPrefixedBranchIntoTable'),
    @('TrailingBytesBeyondMaximum','TrailingTableBytes'),@('ReportedTableMismatch','Mismatch'),@('TrueAvxOutsideTable','AvxOutsideTable'))
foreach($faultCase in $wideUnsignedFaults){
    if($faultCase[1] -eq 'AvxOutsideTable'){Invoke-WideUnsignedControl $faultCase[0] $false 14 $faultCase[1] 15 1}
    else {Invoke-WideUnsignedControl $faultCase[0] $false 14 $faultCase[1]}
}
if($wideUnsignedCount -ne 30){throw "Wide unsigned expected 30 named facts; observed $wideUnsignedCount"}
Write-Host 'Wide unsigned guarded inline DWORD controls: 30 (3 accepted, 27 refused)'
