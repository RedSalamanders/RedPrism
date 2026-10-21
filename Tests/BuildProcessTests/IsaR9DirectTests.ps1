[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'IsaInlineTableFixtures.ps1')
# Independent PE, PDB and listing controls. These bytes are never executed.
function New-R9DirectCase([string]$Name, [string]$Fault = '', [int]$Maximum = 12) {
    $case = New-SyntheticCase ($Name + 'Seed') -PdbData
    $pe = [IO.File]::ReadAllBytes($case.Binary)
    [uint32]$section = $script:sectionRva
    [uint32]$prefix = $section + 0x840
    [uint32]$procBegin = $prefix - 16
    [uint32]$table = $section + 0x900
    [uint32]$target = $section + 0x8c5
    [uint32]$end = $table + 4 * ($Maximum + 1)
    [byte[]]$bytes = @(0x0f,0xb6,0x47,1,0x3c,[byte]$Maximum,0x0f,0x87,0,0,0,0,
        0x4c,0x8d,0x0d,0,0,0,0,0x41,0x8b,0x84,0x81,0,0,0,0,0x49,0x03,0xc1,0xff,0xe0)
    Put-Bytes $bytes 8 ([BitConverter]::GetBytes([int32]($target-($prefix+12))))
    Put-Bytes $bytes 15 ([BitConverter]::GetBytes([int32](-[int64]($prefix+19))))
    Set-U32 $bytes 23 $table
    switch ($Fault) {
        'WrongMovzx' {$bytes[1]=0xb7}
        'WrongSource' {$bytes[2]=0x46}
        'WrongSourceOffset' {$bytes[3]=2}
        'WrongCmp' {$bytes[4]=0x3d}
        'SignedBranch' {$bytes[7]=0x8f}
        'WrongLea' {$bytes[14]=0x05}
        'WrongImageBase' {$bytes[15]++}
        'WrongLoad' {$bytes[20]=0x89}
        'WrongDestination' {$bytes[21]=0x8c}
        'WrongScale' {$bytes[22]=0x41}
        'WrongTable' {$bytes[23]++}
        'WrongAdd' {$bytes[29]=0xc9}
        'WrongJump' {$bytes[31]=0xe1}
        'DefaultInGuard' {Put-Bytes $bytes 8 ([BitConverter]::GetBytes([int32](($prefix+19)-($prefix+12))))}
        'DefaultInTable' {Put-Bytes $bytes 8 ([BitConverter]::GetBytes([int32]($table-($prefix+12))))}
        'DefaultInterior' {Put-Bytes $bytes 8 ([BitConverter]::GetBytes([int32](($target+1)-($prefix+12))))}
    }
    $definitions=[Collections.Generic.List[object]]::new()
    $starts=@(0,4,6,12,19,27,30);$lengths=@(4,2,6,7,8,3,2)
    for($i=0;$i-lt7;++$i){$definitions.Add(@{Rva=$prefix+$starts[$i];Bytes=[byte[]]$bytes[$starts[$i]..($starts[$i]+$lengths[$i]-1)];Name="Guard$i"})}
    $definitions.Add(@{Rva=$target;Bytes=[byte[]]@(0x0f,0x1f,0x40,0);Name='Target'})
    $definitions.Add(@{Rva=$table-1;Bytes=[byte[]]@(0xc3);Name='Return'})
    if($Fault-eq'WrongReturn'){$definitions[$definitions.Count-1].Bytes[0]=0x90}
    if($Fault-in'RealVexBody','RealVexTarget','RealVexAdjacent'){
        [uint32]$vex=if($Fault-eq'RealVexTarget'){$target+4}elseif($Fault-eq'RealVexAdjacent'){$end}else{$prefix+40}
        $definitions.Add(@{Rva=$vex;Bytes=[byte[]]@(0xc5,0xf8,0x10,0);Name='RealVex'})
    }
    $occupied=[Collections.Generic.HashSet[uint32]]::new()
    foreach($d in $definitions){Put-Bytes $pe (0x400+$d.Rva-$section) $d.Bytes;for($j=0;$j-lt$d.Bytes.Length;++$j){[void]$occupied.Add([uint32]($d.Rva+$j))}}
    for([uint32]$at=$procBegin;$at-lt$table;++$at){if(!$occupied.Contains($at)){$pe[0x400+$at-$section]=0x90;$definitions.Add(@{Rva=$at;Bytes=[byte[]]@(0x90);Name='Body'+$at})}}
    $rows=[Collections.Generic.List[string]]::new()
    foreach($d in $definitions){
        if($Fault-eq('Missing'+$d.Name)){continue}
        [byte[]]$reported=$d.Bytes.Clone()
        if($Fault-eq('Listing'+$d.Name)){$reported[$reported.Length-1]=$reported[$reported.Length-1]-bxor1}
        $text=if($Fault-eq('Unknown'+$d.Name)){'<unknown>'}elseif($d.Name-eq'RealVex'){'vmovups xmm0, xmmword ptr [rax]'}else{'known-control'}
        $rows.Add(('{0:x}: {1} {2}'-f($script:imageBase+$d.Rva),(Hex-Bytes $reported),$text))
    }
    for($i=0;$i-le$Maximum;++$i){
        [uint32]$entry=$target
        if($i-eq$Maximum){switch($Fault){
            'TargetInterior' {$entry=$target+1}
            'TargetGuard' {$entry=$prefix+12}
            'TargetTable' {$entry=$table}
            'TargetOutside' {$entry=$procBegin-1}
            'RealVexTarget' {$entry=$target+4}
        }}
        $at=0x400+$table-$section+4*$i;Set-U32 $pe $at $entry
        [byte[]]$reported=$pe[$at..($at+3)]
        if($Fault-eq'TableListingMismatch'-and$i-eq0){$reported[3]=1}
        $rows.Add(('{0:x}: {1} vmovups xmm0, xmmword ptr [rax]'-f($script:imageBase+$table+4*$i),(Hex-Bytes $reported)))
    }
    [uint32]$procEnd=$end
    if($Fault-eq'ShortProcedure'){$procEnd-=4};if($Fault-eq'LongProcedure'){$procEnd+=4}
    $procOwner=if($Fault-eq'WrongProcedureOwner'){1}else{0};$dataOwner=if($Fault-eq'WrongDataOwner'){1}else{0}
    $sym="  Mod $procOwner | ``SyntheticControl.obj``:`n"
    if($Fault-ne'MissingProcedure'){$sym+="  0 | S_GPROC32 [size = 16] ``R9Direct`` `n  addr = 0001:$($procBegin-$section), code size = $($procEnd-$procBegin)`n"}
    $sym+="  Mod $dataOwner | ``SyntheticControl.obj``:`n"
    if($Fault-ne'MissingData'){
        $data=if($Fault-eq'WrongDataStart'){$table+4}else{$table}
        $dataName=if($Fault-eq'NamedData'){'``named``'}else{'``'};$type=if($Fault-eq'TypedData'){'T_NOTYPE (0x0000)'}else{'<no type>'}
        $sym+="  0 | S_LDATA32 [size = 16] $dataName`n  type = $type, addr = 0001:$($data-$section)`n"
    }
    if($Fault-eq'InteriorData'){$sym+="  0 | S_LDATA32 [size = 16] `````n  type = <no type>, addr = 0001:$($table+4-$section)`n"}
    Set-Content -LiteralPath $case.Symbols -Value $sym -Encoding utf8
    Set-Content -LiteralPath $case.Disassembly -Value (@($rows|Sort-Object{[uint64]::Parse(($_-split':',2)[0],[Globalization.NumberStyles]::HexNumber)})-join"`n") -Encoding utf8
    [IO.File]::WriteAllBytes($case.Binary,$pe);$case.Name=$Name;return $case
}
foreach($maximum in @(1,12,127)) {Invoke-Control (New-R9DirectCase ('R9Direct'+$maximum) '' $maximum) $true ($maximum+1) 0}
$faults=@('WrongMovzx','WrongSource','WrongSourceOffset','WrongCmp','SignedBranch','WrongLea','WrongImageBase',
    'WrongLoad','WrongDestination','WrongScale','WrongTable','WrongAdd','WrongJump','DefaultInGuard','DefaultInTable',
    'DefaultInterior','TargetInterior','TargetGuard','TargetTable','TargetOutside','MissingData','WrongDataStart',
    'WrongDataOwner','MissingProcedure','WrongProcedureOwner','ShortProcedure','LongProcedure','NamedData','TypedData',
    'InteriorData','WrongReturn','MissingReturn','ListingReturn','UnknownReturn','TableListingMismatch',
    'RealVexBody','RealVexTarget','RealVexAdjacent')
foreach($i in 0..6){$faults+=('MissingGuard'+$i);$faults+=('ListingGuard'+$i);$faults+=('UnknownGuard'+$i)}
foreach($fault in $faults){Invoke-Control (New-R9DirectCase ('R9Direct'+$fault) $fault) $false}
Invoke-Control (New-R9DirectCase 'R9DirectZeroRange' '' 0) $false
Invoke-Control (New-R9DirectCase 'R9DirectRange128' '' 128) $false
Write-Host ('SUMMARY isa-r9-direct controls='+($faults.Count+5)+' failures=0')
