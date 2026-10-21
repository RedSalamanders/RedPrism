[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$qwordRepo=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
. (Join-Path $qwordRepo 'Tests/BuildProcessTests/IsaInlineTableFixtures.ps1')

function New-QwordStackRaxCase([string]$Name,[string]$Fault='',[int]$Maximum=64) {
    $case=New-SyntheticCase ($Name+'Seed') -PdbData
    $pe=[IO.File]::ReadAllBytes($case.Binary)
    [uint32]$section=$script:sectionRva
    [uint32]$dispatch=$section+0x890
    [uint32]$prefix=$dispatch-15
    [uint32]$table=$section+0x900
    [uint32]$target=$section+0x8c5
    [uint32]$end=$table+4*($Maximum+1)
    # Independently encoded from the failed ASan listing: QWORD CMP/JA, RAX image base,
    # QWORD RCX load from the same full disp32 stack slot, DWORD RVA load, ADD RCX,RAX, JMP RCX.
    [byte[]]$bytes=@(0x48,0x83,0xbc,0x24,0x38,0x02,0,0,[byte]$Maximum,
        0x0f,0x87,0,0,0,0,0x48,0x8d,0x05,0,0,0,0,
        0x48,0x8b,0x8c,0x24,0x38,0x02,0,0,0x8b,0x8c,0x88,0,0,0,0,0x48,0x03,0xc8,0xff,0xe1)
    Put-Bytes $bytes 11 ([BitConverter]::GetBytes([int32]($target-($prefix+15))))
    Put-Bytes $bytes 18 ([BitConverter]::GetBytes([int32](-[int64]($dispatch+7))))
    Set-U32 $bytes 33 $table
    switch($Fault) {
        'DwordCmp' {$bytes[0]=0x40}
        'WrongCmp' {$bytes[2]=0xbd}
        'WrongCmpSib' {$bytes[3]=0x25}
        'WrongJa' {$bytes[10]=0x86}
        'SignedJa' {$bytes[10]=0x8f}
        'WrongBase' {$bytes[17]=0x0d}
        'WrongImageBase' {$bytes[18]++}
        'WrongIndexRegister' {$bytes[24]=0x84}
        'DwordIndex' {$bytes[22]=0x40}
        'WrongIndexSib' {$bytes[25]=0x25}
        'StackByte0' {$bytes[26]++}
        'StackByte1' {$bytes[27]++}
        'StackByte2' {$bytes[28]++}
        'StackByte3' {$bytes[29]++}
        'WrongLoad' {$bytes[30]=0x89}
        'WrongLoadSib' {$bytes[32]=0x89}
        'WrongAdd' {$bytes[39]=0xc1}
        'WrongJump' {$bytes[41]=0xe0}
        'ZeroRange' {$bytes[8]=0}
        'Range128' {$bytes[8]=128}
        'Range255' {$bytes[8]=255}
        'DefaultNotInTable' {Put-Bytes $bytes 11 ([BitConverter]::GetBytes([int32]($target-1-($prefix+15))))}
        'DefaultInGuard' {Put-Bytes $bytes 11 ([BitConverter]::GetBytes([int32]($dispatch-($prefix+15))))}
    }
    Put-Bytes $pe (0x400+$prefix-$section) $bytes
    $pe[0x400+$target-$section]=0x90
    $pe[0x400+$target-1-$section]=0x90
    $pe[0x400+$table-1-$section]=if($Fault-eq'WrongTerminalPe'){0x90}else{0xc3}
    $rows=[Collections.Generic.List[string]]::new()
    $starts=@(0,9,15,22,30,37,40);$lengths=@(9,6,7,8,7,3,2)
    for($i=0;$i-lt7;++$i){
        if($Fault-eq"MissingRow$i"){continue}
        [byte[]]$row=$bytes[$starts[$i]..($starts[$i]+$lengths[$i]-1)]
        if($Fault-eq"ListingRow$i"){$row[$row.Length-1]=$row[$row.Length-1]-bxor1}
        $text=if($Fault-eq"UnknownRow$i"){'<unknown>'}else{'guard-dispatch'}
        $rows.Add(('{0:x}: {1} {2}'-f($script:imageBase+$prefix+$starts[$i]),(Hex-Bytes $row),$text))
    }
    if($Fault-eq'OverlapGuard'){$rows.Add(('{0:x}: 90 nop'-f($script:imageBase+$prefix+1)))}
    if($Fault-eq'OverlapDispatch'){$rows.Add(('{0:x}: 90 nop'-f($script:imageBase+$dispatch+1)))}
    $targetText=if($Fault-eq'UnknownDefault'){'<unknown>'}else{'nop'}
    $bodyText=if($Fault-eq'UnknownBodyTarget'){'<unknown>'}else{'nop'}
    $rows.Add(('{0:x}: 90 {1}'-f($script:imageBase+$target),$targetText))
    $rows.Add(('{0:x}: 90 {1}'-f($script:imageBase+$target-1),$bodyText))
    if($Fault-ne'MissingTerminal'){
        $terminal=if($Fault-eq'UnknownTerminal'){'<unknown>'}else{'ret'}
        $terminalBytes=if($Fault-eq'WrongTerminalListing'){'90'}else{'c3'}
        $rows.Add(('{0:x}: {1} {2}'-f($script:imageBase+$table-1),$terminalBytes,$terminal))
    }
    for($i=0;$i-le$Maximum;++$i){
        [uint32]$entry=$target
        if($i-eq0-and$Fault-in'KnownBodyTarget','UnknownBodyTarget'){$entry=$target-1}
        if($i-eq$Maximum){
            switch($Fault){
                'MiddleTarget' {$entry=$prefix+1}
                'OutsideTarget' {$entry=$prefix-1}
                'GuardTarget' {$entry=$dispatch}
                'TableTarget' {$entry=$table}
                'MissingTarget' {$entry=$target+1}
            }
        }
        $at=0x400+$table-$section+4*$i
        Set-U32 $pe $at $entry
        $rows.Add(('{0:x}: {1} vmovups xmm0, xmmword ptr [rax]'-f($script:imageBase+$table+4*$i),(Hex-Bytes ([byte[]]$pe[$at..($at+3)]))))
    }
    [uint32]$procBegin=$prefix
    [uint32]$procEnd=$end
    if($Fault-in'EntryToIndex','EntryToTable','EntryToJa'){
        $procBegin=$prefix-5
        [uint32]$into=if($Fault-eq'EntryToTable'){$table}elseif($Fault-eq'EntryToJa'){$prefix+9}else{$dispatch+7}
        [byte[]]$branch=@(0xe9,0,0,0,0)
        Put-Bytes $branch 1 ([BitConverter]::GetBytes([int32]($into-($procBegin+5))))
        Put-Bytes $pe (0x400+$procBegin-$section) $branch
        $rows.Add(('{0:x}: {1} jmp'-f($script:imageBase+$procBegin),(Hex-Bytes $branch)))
    }
    if($Fault-eq'ShortProcedure'){$procEnd-=4}
    if($Fault-eq'LongProcedure'){$procEnd+=4}
    $procOwner=if($Fault-eq'WrongProcedureOwner'){1}else{0}
    $sym="  Mod $procOwner | ``SyntheticControl.obj``:`n"
    if($Fault-ne'MissingProcedure'){$sym+="  0 | S_GPROC32 [size = 16] ``GuardedQwordStackRax`` `n  addr = 0001:$($procBegin-$section), code size = $($procEnd-$procBegin)`n"}
    $dataOwner=if($Fault-eq'WrongDataOwner'){1}else{0}
    $sym+="  Mod $dataOwner | ``SyntheticControl.obj``:`n"
    if($Fault-ne'MissingData'){
        $data=if($Fault-eq'WrongDataStart'){$table+4}else{$table}
        $sym+="  0 | S_LDATA32 [size = 16] `````n  type = <no type>, addr = 0001:$($data-$section)`n"
    }
    if($Fault-eq'InteriorData'){$sym+="  0 | S_LDATA32 [size = 16] `````n  type = <no type>, addr = 0001:$($table+4-$section)`n"}
    # Keep the generic unguarded recognizer from substituting an unrelated table boundary.
    $sym+="  Mod 0 | ``SyntheticControl.obj``:`n  0 | S_LDATA32 [size = 16] `````n  type = <no type>, addr = 0001:4096`n"
    if($Fault-eq'AdjacentVex'){
        [byte[]]$vex=@(0xc5,0xf8,0x10,0x00)
        Put-Bytes $pe (0x400+$end-$section) $vex
        $rows.Add(('{0:x}: {1} vmovups xmm0, xmmword ptr [rax]'-f($script:imageBase+$end),(Hex-Bytes $vex)))
    }
    Set-Content -LiteralPath $case.Symbols -Value $sym -Encoding utf8
    Set-Content -LiteralPath $case.Disassembly -Value (@($rows|Sort-Object{[uint64]::Parse(($_-split':',2)[0],[Globalization.NumberStyles]::HexNumber)})-join"`n") -Encoding utf8
    [IO.File]::WriteAllBytes($case.Binary,$pe)
    $case.Name=$Name
    $case
}
$qwordNames=[Collections.Generic.List[string]]::new()
function Invoke-QwordControl([string]$Name,[string]$Fault,[int]$Maximum,[bool]$Accepted,[int]$Tables=0,[int]$Violations=-1){
    Invoke-Control (New-QwordStackRaxCase $Name $Fault $Maximum) $Accepted $Tables $Violations
    $qwordNames.Add($Name)
    Write-Host "PASS isa-qword-stack-rax.$Name"
}
foreach($maximum in 1,64,127){Invoke-QwordControl "Maximum$maximum" '' $maximum $true ($maximum+1) 0}
Invoke-QwordControl 'KnownBodyTarget' 'KnownBodyTarget' 10 $true 11 0
Invoke-QwordControl 'AdjacentVex' 'AdjacentVex' 10 $false 11 1
$faults=@('DwordCmp','WrongCmp','WrongCmpSib','WrongJa','SignedJa','WrongBase','WrongImageBase',
    'WrongIndexRegister','DwordIndex','WrongIndexSib','StackByte0','StackByte1','StackByte2','StackByte3',
    'WrongLoad','WrongLoadSib','WrongAdd','WrongJump','ZeroRange','Range128','Range255',
    'DefaultNotInTable','DefaultInGuard','OverlapGuard','OverlapDispatch','UnknownDefault','UnknownBodyTarget',
    'WrongTerminalPe','MissingTerminal','UnknownTerminal','WrongTerminalListing','MiddleTarget','OutsideTarget',
    'GuardTarget','TableTarget','MissingTarget','ShortProcedure','LongProcedure','WrongProcedureOwner',
    'MissingProcedure','WrongDataOwner','MissingData','WrongDataStart','InteriorData','EntryToIndex','EntryToTable','EntryToJa')
foreach($fault in $faults){Invoke-QwordControl $fault $fault 10 $false}
foreach($row in 0..6){foreach($kind in 'Missing','Listing','Unknown'){Invoke-QwordControl "$($kind)Row$row" "$($kind)Row$row" 10 $false}}
Write-Host "SUMMARY isa-qword-stack-rax controls=$($qwordNames.Count) accepted=4 refused=$($qwordNames.Count-4)"
