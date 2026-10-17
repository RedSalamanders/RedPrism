[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$r9Repo=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
. (Join-Path $r9Repo 'Tests/BuildProcessTests/IsaInlineTableTests.ps1')

# Independent PE/listing/PDB parser controls. No synthetic byte array is executed.
function New-R9ByteSwitchCase([string]$Name,[string]$Fault='',[int]$Maximum=12,[switch]$AlignedTail) {
    $case=New-SyntheticCase ($Name+'Seed') -PdbData
    $pe=[IO.File]::ReadAllBytes($case.Binary)
    [uint32]$section=$script:sectionRva
    [uint32]$prefix=$section+0x840
    [uint32]$procBegin=$prefix-32
    [uint32]$table=$section+0x900
    [uint32]$target=$section+0x8c5 # Low RVA byte C5 deliberately looks like a VEX prefix.
    [uint32]$target2=$target+4
    [uint32]$end=$table+4*($Maximum+1)
    [byte[]]$bytes=@(0x41,0x80,0xf9,[byte]$Maximum,0x0f,0x87,0,0,0,0,
        0x48,0x8d,0x15,0,0,0,0,0x42,0x8b,0x84,0x8a,0,0,0,0,0x48,0x03,0xc2,0xff,0xe0)
    Put-Bytes $bytes 6 ([BitConverter]::GetBytes([int32]($target-($prefix+10))))
    Put-Bytes $bytes 13 ([BitConverter]::GetBytes([int32](-[int64]($prefix+17))))
    Set-U32 $bytes 21 $table
    switch($Fault){
        'WrongCmpRex' {$bytes[0]=0x40}
        'WrongCmpOpcode' {$bytes[1]=0x81}
        'WrongCmpRegister' {$bytes[2]=0xf8}
        'WrongJa' {$bytes[5]=0x86}
        'SignedJa' {$bytes[5]=0x8f}
        'WrongBase' {$bytes[12]=0x0d}
        'WrongImageBase' {$bytes[13]++}
        'WrongLoadRex' {$bytes[17]=0x40}
        'WrongLoad' {$bytes[18]=0x89}
        'WrongDestination' {$bytes[19]=0x8c}
        'WrongSib' {$bytes[20]=0x88}
        'WrongTableRva' {$bytes[21]++}
        'OutsideTableRva' {Set-U32 $bytes 21 ($section+0x3000)}
        'WrongAdd' {$bytes[27]=0xc1}
        'WrongJump' {$bytes[29]=0xe1}
        'ZeroRange' {$bytes[3]=0}
        'Range128' {$bytes[3]=128}
        'Range255' {$bytes[3]=255}
        'DefaultInGuard' {Put-Bytes $bytes 6 ([BitConverter]::GetBytes([int32](($prefix+4)-($prefix+10))))}
        'DefaultInTable' {Put-Bytes $bytes 6 ([BitConverter]::GetBytes([int32]($table-($prefix+10))))}
        'DefaultInTail' {Put-Bytes $bytes 6 ([BitConverter]::GetBytes([int32](($table-8)-($prefix+10))))}
        'DefaultInterior' {Put-Bytes $bytes 6 ([BitConverter]::GetBytes([int32](($target+1)-($prefix+10))))}
        'DefaultOutside' {Put-Bytes $bytes 6 ([BitConverter]::GetBytes([int32](($procBegin-1)-($prefix+10))))}
    }
    $definitions=[Collections.Generic.List[object]]::new()
    $starts=@(0,4,10,17,25,28);$lengths=@(4,6,7,8,3,2)
    for($i=0;$i-lt6;++$i){$definitions.Add(@{Rva=$prefix+$starts[$i];Bytes=[byte[]]$bytes[$starts[$i]..($starts[$i]+$lengths[$i]-1)];Name="Guard$i"})}
    $definitions.Add(@{Rva=$target;Bytes=[byte[]]@(0x0f,0x1f,0x40,0);Name='Target'})
    [int]$tailSize=if($AlignedTail){9}else{8}
    [byte[]]$tail=if($AlignedTail){@(0xc3,0xe8,0,0,0,0,0xcc,0x66,0x90)}else{@(0xc3,0xe8,0,0,0,0,0xcc,0x90)}
    Put-Bytes $tail 2 ([BitConverter]::GetBytes([int32](($section+0x600)-($table-$tailSize+6))))
    switch($Fault){
        'WrongRet' {$tail[0]=0x90}
        'WrongCall' {$tail[1]=0xe9}
        'WrongInt3' {$tail[6]=0x90}
        'WrongNop' {$tail[7]=0xcc}
        'WrongNopSecond' {$tail[8]=0x91}
        'TailCallIntoGuard' {Put-Bytes $tail 2 ([BitConverter]::GetBytes([int32](($prefix+17)-($table-$tailSize+6))))}
        'TailCallIntoTable' {Put-Bytes $tail 2 ([BitConverter]::GetBytes([int32]($table-($table-$tailSize+6))))}
    }
    $tailStarts=@(0,1,6,7);$tailLengths=@(1,5,1,$(if($AlignedTail){2}else{1}))
    for($i=0;$i-lt4;++$i){$definitions.Add(@{Rva=$table-$tailSize+$tailStarts[$i];Bytes=[byte[]]$tail[$tailStarts[$i]..($tailStarts[$i]+$tailLengths[$i]-1)];Name="Tail$i"})}
    if($Fault.StartsWith('Alternate',[StringComparison]::Ordinal)){
        [byte[]]$comparison=@(0x41,0x80,0xf9,[byte]$Maximum)
        [byte[]]$branch=@(0x0f,0x85,0,0,0,0)
        [uint32]$alternate=$prefix-16
        Put-Bytes $branch 2 ([BitConverter]::GetBytes([int32](($prefix+4)-($alternate+10))))
        if($Fault.StartsWith('AlternateShort',[StringComparison]::Ordinal)){
            $branch=[byte[]]@(0x75,[byte](($prefix+4)-($alternate+6)))
            if($Fault-eq'AlternateShortWrongJne'){$branch[0]=0x74}
        }
        if($Fault-eq'AlternateWrongCmp'){$comparison[3]--}
        if($Fault-eq'AlternateWrongRegister'){$comparison[2]=0xf8}
        if($Fault-eq'AlternateWrongJne'){$branch[1]=0x84}
        $definitions.Add(@{Rva=$alternate;Bytes=$comparison;Name='AlternateCmp'})
        $definitions.Add(@{Rva=$alternate+4;Bytes=$branch;Name='AlternateJne'})
    }
    if($Fault.StartsWith('Incoming',[StringComparison]::Ordinal)){
        [uint32]$destination=switch($Fault){
            'IncomingCmp' {$prefix}
            'IncomingJa' {$prefix+4}
            'IncomingIndex' {$prefix+17}
            'IncomingTable' {$table}
            'IncomingCallTable' {$table}
            'IncomingCallGuard' {$prefix+4}
            default {$prefix+1}
        }
        [byte[]]$branch=@(0xe9,0,0,0,0)
        if($Fault.StartsWith('IncomingCall',[StringComparison]::Ordinal)){$branch[0]=0xe8}
        Put-Bytes $branch 1 ([BitConverter]::GetBytes([int32]($destination-($procBegin+6))))
        $definitions.Add(@{Rva=$procBegin+1;Bytes=$branch;Name='Incoming'})
    }
    if($Fault.StartsWith('Loop',[StringComparison]::Ordinal)){
        [byte]$op=[byte]([int]$Fault.Substring(4,1)+0xe0)
        [byte[]]$branch=@($op,[byte](($prefix+4)-($prefix-2)))
        $definitions.Add(@{Rva=$prefix-4;Bytes=$branch;Name='Loop'})
    }
    if($Fault-eq'PrefixedIncoming'){
        [byte[]]$branch=@(0x67,0xe3,[byte](($prefix+4)-($prefix-1)))
        $definitions.Add(@{Rva=$prefix-4;Bytes=$branch;Name='Prefixed'})
    }
    if($Fault-eq'SplitLock'){
        $definitions.Add(@{Rva=$prefix+40;Bytes=[byte[]]@(0xf0);Name='Lock'})
        $definitions.Add(@{Rva=$prefix+41;Bytes=[byte[]]@(0x0f,0xc1,0x43,0x08);Name='Xadd'})
    }
    if($Fault-in'RealVexBody','RealVexTarget'){
        [uint32]$vex=if($Fault-eq'RealVexTarget'){$target2}else{$prefix+40}
        $definitions.Add(@{Rva=$vex;Bytes=[byte[]]@(0xc5,0xf8,0x10,0);Name='RealVex'})
    }
    # Fill the complete body. Reserved row widths remain reserved even for deliberately missing listings.
    $occupied=[Collections.Generic.HashSet[uint32]]::new()
    foreach($d in $definitions){Put-Bytes $pe (0x400+$d.Rva-$section) $d.Bytes;for($j=0;$j-lt$d.Bytes.Length;++$j){[void]$occupied.Add([uint32]($d.Rva+$j))}}
    for([uint32]$at=$procBegin;$at-lt$table;++$at){if(!$occupied.Contains($at)){$pe[0x400+$at-$section]=0x90;$definitions.Add(@{Rva=$at;Bytes=[byte[]]@(0x90);Name='Body'+$at})}}
    $bodyRva=$prefix+38
    $rows=[Collections.Generic.List[string]]::new()
    foreach($d in $definitions){
        if($Fault.EndsWith('Missing'+$d.Name,[StringComparison]::Ordinal)-or($Fault-eq'MissingBody'-and$d.Rva-eq$bodyRva)){continue}
        [byte[]]$reported=$d.Bytes.Clone()
        if($Fault.EndsWith('Listing'+$d.Name,[StringComparison]::Ordinal)){$reported[$reported.Length-1]=$reported[$reported.Length-1]-bxor1}
        if($Fault-eq'BodyListingMismatch'-and$d.Rva-eq$bodyRva){$reported[0]=0xcc}
        $text=if($Fault.EndsWith('Unknown'+$d.Name,[StringComparison]::Ordinal)-or($Fault-eq'UnknownBody'-and$d.Rva-eq$bodyRva)){'<unknown>'}elseif($d.Name-eq'RealVex'){'vmovups xmm0, xmmword ptr [rax]'}else{'known-control'}
        $rows.Add(('{0:x}: {1} {2}'-f($script:imageBase+$d.Rva),(Hex-Bytes $reported),$text))
    }
    if($Fault-eq'OverlapGuard'){$rows.Add(('{0:x}: 90 nop'-f($script:imageBase+$prefix+1)))}
    if($Fault-eq'OverlapBody'){$rows.Add(('{0:x}: 90 nop'-f($script:imageBase+$target+1)))}
    for($i=0;$i-le$Maximum;++$i){
        [uint32]$entry=$target
        if($Fault-in'DistinctTargets','RealVexTarget'-and$i-eq$Maximum){$entry=$target2}
        if($i-eq$Maximum){switch($Fault){
            'TargetInterior' {$entry=$target+1}
            'TargetOutside' {$entry=$procBegin-1}
            'TargetGuard' {$entry=$prefix+10}
            'TargetTable' {$entry=$table}
            'TargetTail' {$entry=$table-8}
        }}
        $at=0x400+$table-$section+4*$i
        Set-U32 $pe $at $entry
        $rows.Add(('{0:x}: {1} vmovups xmm0, xmmword ptr [rax]'-f($script:imageBase+$table+4*$i),(Hex-Bytes ([byte[]]$pe[$at..($at+3)]))))
    }
    if($Fault-eq'RealVexAdjacent'){
        [byte[]]$vex=@(0xc5,0xf8,0x10,0)
        Put-Bytes $pe (0x400+$end-$section) $vex
        $rows.Add(('{0:x}: c5 f8 10 00 vmovups xmm0, xmmword ptr [rax]'-f($script:imageBase+$end)))
    }
    if($Fault-eq'VirtualOnlyTable'){
        Set-U32 $pe (0x80+24+0xf0+16) 0x900
    }
    [uint32]$procEnd=$end
    if($Fault-eq'ShortProcedure'){$procEnd-=4}
    if($Fault-eq'LongProcedure'){$procEnd+=4}
    $procOwner=if($Fault-eq'WrongProcedureOwner'){1}else{0}
    $sym="  Mod $procOwner | ``SyntheticControl.obj``:`n"
    if($Fault-ne'MissingProcedure'){$sym+="  0 | S_GPROC32 [size = 16] ``R9ByteSwitch`` `n  addr = 0001:$($procBegin-$section), code size = $($procEnd-$procBegin)`n"}
    $dataOwner=if($Fault-eq'WrongDataOwner'){1}else{0}
    $sym+="  Mod $dataOwner | ``SyntheticControl.obj``:`n"
    if($Fault-ne'MissingData'){
        $data=if($Fault-eq'WrongDataStart'){$table+4}else{$table}
        $dataName=if($Fault-eq'NamedData'){'``named``'}else{'``'}
        $type=if($Fault-eq'TypedData'){'T_NOTYPE (0x0000)'}else{'<no type>'}
        $sym+="  0 | S_LDATA32 [size = 16] $dataName`n  type = $type, addr = 0001:$($data-$section)`n"
    }
    if($Fault-eq'InteriorData'){$sym+="  0 | S_LDATA32 [size = 16] `````n  type = <no type>, addr = 0001:$($table+4-$section)`n"}
    $sym+="  Mod 0 | ``SyntheticControl.obj``:`n  0 | S_LDATA32 [size = 16] `````n  type = <no type>, addr = 0001:4096`n"
    Set-Content -LiteralPath $case.Symbols -Value $sym -Encoding utf8
    Set-Content -LiteralPath $case.Disassembly -Value (@($rows|Sort-Object{[uint64]::Parse(($_-split':',2)[0],[Globalization.NumberStyles]::HexNumber)})-join"`n") -Encoding utf8
    [IO.File]::WriteAllBytes($case.Binary,$pe)
    $case.Name=$Name
    $case
}
$r9Names=[Collections.Generic.List[string]]::new()
$r9Accepted=0
function Invoke-R9Control([string]$Name,[string]$Fault,[int]$Maximum,[bool]$Accepted,[int]$Tables=0,[int]$Vectors=-1){
    Invoke-Control (New-R9ByteSwitchCase $Name $Fault $Maximum) $Accepted $Tables $Vectors
    $r9Names.Add($Name)
    if($Accepted){++$script:r9Accepted}
    Write-Host "PASS isa-r9-byte-switch.$Name"
}
foreach($maximum in 1,12,127){Invoke-R9Control "Maximum$maximum" '' $maximum $true ($maximum+1) 0}
foreach($positive in 'RepeatedTargets','AlternateValid','IncomingCmp','SplitLock'){Invoke-R9Control $positive $positive 12 $true 13 0}
Invoke-R9Control 'DistinctTargets' 'DistinctTargets' 12 $true 12 0
foreach($vexCaseName in 'RealVexBody','RealVexAdjacent'){Invoke-R9Control $vexCaseName $vexCaseName 12 $false 13 1}
Invoke-R9Control 'RealVexTarget' 'RealVexTarget' 12 $false 12 1
$faults=@('WrongCmpRex','WrongCmpOpcode','WrongCmpRegister','WrongJa','SignedJa','WrongBase','WrongImageBase',
    'WrongLoadRex','WrongLoad','WrongDestination','WrongSib','WrongTableRva','OutsideTableRva','WrongAdd','WrongJump',
    'ZeroRange','Range128','Range255','DefaultInGuard','DefaultInTable','DefaultInTail','DefaultInterior','DefaultOutside',
    'TargetInterior','TargetOutside','TargetGuard','TargetTable','TargetTail','ShortProcedure','LongProcedure',
    'WrongProcedureOwner','MissingProcedure','WrongDataOwner','MissingData','WrongDataStart','NamedData','TypedData','InteriorData',
    'WrongRet','WrongCall','WrongInt3','WrongNop','TailCallIntoGuard','TailCallIntoTable','OverlapGuard','OverlapBody','MissingBody','UnknownBody',
    'BodyListingMismatch','VirtualOnlyTable','Loop0','Loop1','Loop2','Loop3','PrefixedIncoming',
    'IncomingJa','IncomingIndex','IncomingTable','IncomingCallTable','IncomingCallGuard',
    'AlternateWrongCmp','AlternateWrongRegister','AlternateWrongJne','AlternateMissingAlternateJne',
    'AlternateUnknownAlternateJne','AlternateListingAlternateCmp','AlternateListingAlternateJne')
foreach($fault in $faults){Invoke-R9Control $fault $fault 12 $false}
foreach($row in @('Guard0','Guard1','Guard2','Guard3','Guard4','Guard5','Tail0','Tail1','Tail2','Tail3')){
    foreach($kind in 'Missing','Listing','Unknown'){Invoke-R9Control ($kind+$row) ($kind+$row) 12 $false}
}
Write-Host "SUMMARY isa-r9-byte-switch controls=$($r9Names.Count) accepted=$r9Accepted refused=$($r9Names.Count-$r9Accepted)"

# Exact two-byte alignment sibling; unchanged old cases above remain mandatory.
$alignedControls=0
foreach($maximum in 1,12,127){
    Invoke-Control (New-R9ByteSwitchCase ("AlignedMaximum"+$maximum) '' $maximum -AlignedTail) $true ($maximum+1) 0
    ++$alignedControls
}
foreach($positive in 'AlternateValid','AlternateShortValid','IncomingCmp','SplitLock'){
    Invoke-Control (New-R9ByteSwitchCase ("Aligned"+$positive) $positive 12 -AlignedTail) $true 13 0
    ++$alignedControls
}
foreach($fault in @($faults)+@('WrongNopSecond','AlternateShortWrongJne')){
    Invoke-Control (New-R9ByteSwitchCase ("Aligned"+$fault) $fault 12 -AlignedTail) $false
    ++$alignedControls
}
foreach($row in @('Guard0','Guard1','Guard2','Guard3','Guard4','Guard5','Tail0','Tail1','Tail2','Tail3','AlternateJne')){
    foreach($kind in 'Missing','Listing','Unknown'){
        $fault=if($row-eq'AlternateJne'){"AlternateShort"+$kind+$row}else{$kind+$row}
        Invoke-Control (New-R9ByteSwitchCase ("Aligned"+$fault) $fault 12 -AlignedTail) $false
        ++$alignedControls
    }
}
Write-Host "SUMMARY isa-r9-aligned-tail controls=$alignedControls accepted=7 failures=0"
