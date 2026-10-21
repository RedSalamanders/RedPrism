[CmdletBinding()]
param([string]$CandidateSource='', [string]$FixtureHelper='', [string]$RunRoot='')
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if(!$CandidateSource){$CandidateSource=Join-Path $repo 'Build/IsaScan.cs'}
if(!$FixtureHelper){$FixtureHelper=Join-Path $PSScriptRoot 'IsaInlineTableFixtures.ps1'}
if(!$RunRoot){$RunRoot=Join-Path $repo ('.build/BuildProcessTests/IsaR9dRcxR8-'+[guid]::NewGuid().ToString('N'))}
[void](New-Item -ItemType Directory -Path $RunRoot -Force)
Start-Transcript -LiteralPath (Join-Path $RunRoot 'controls.log')|Out-Null
try {
$helperNames=@('Set-U16','Set-U32','Set-U64','Put-Bytes','Hex-Bytes','New-SyntheticCase','Invoke-Control')
$tokens=$null;$parseErrors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile($FixtureHelper,[ref]$tokens,[ref]$parseErrors)
if($parseErrors.Count){throw 'Shared fixture helper has parse errors'}
$definitions=@($ast.FindAll({param($node) $node-is[Management.Automation.Language.FunctionDefinitionAst]-and$helperNames-contains$node.Name},$false))
if($definitions.Count-ne$helperNames.Count){throw 'Shared fixture helper roster differs'}
. ([scriptblock]::Create(($definitions|ForEach-Object{$_.Extent.Text})-join"`n"))
if(-not('RedPrismBuild.IsaScanner'-as[type])){Add-Type -Path $CandidateSource}
$fixtureRoot=Join-Path $RunRoot 'fixtures';[void](New-Item -ItemType Directory -Path $fixtureRoot)
$imageBase=[uint64]0x140000000;$sectionRva=[uint32]0x0010f000;$tableRva=[uint32]($sectionRva+0x1000)
$dispatchRva=$sectionRva;$targetRva=[uint32]0x0010f8c5
$guid=[guid]'9c1e2d4b-6a70-4a8b-9012-3456789abcde';$age=[uint32]7
# Independently encoded PE/PDB/listing controls. Their synthetic instructions are never executed.
function New-R9dRcxR8Case([string]$Name, [string]$Fault = '', [int]$Maximum = 6, [int]$Padding = 0) {
    $case = New-SyntheticCase ($Name + 'Seed') -PdbData
    $pe = [IO.File]::ReadAllBytes($case.Binary)
    [uint32]$section = $script:sectionRva
    [uint32]$prefix = $section + 0x840
    [uint32]$procBegin = $prefix - 16
    [uint32]$table = $section + 0x900
    [uint32]$target = $section + 0x8c5
    [uint32]$end = $table + 4 * ($Maximum + 1)
    [byte[]]$bytes = @(0x41,0x83,0xf9,[byte]$Maximum,0x77,0,
        0x48,0x8d,0x0d,0,0,0,0,0x49,0x63,0xc1,
        0x44,0x8b,0x84,0x81,0,0,0,0,0x4c,0x03,0xc1,0x41,0xff,0xe0)
    $bytes[5]=[byte]($target-($prefix+6))
    Put-Bytes $bytes 9 ([BitConverter]::GetBytes([int32](-[int64]($prefix+13))))
    Set-U32 $bytes 20 $table
    switch ($Fault) {
        'WrongCmpRex' {$bytes[0]=0x49}
        'WrongCmpOpcode' {$bytes[1]=0x81}
        'WrongCmpRegister' {$bytes[2]=0xf8}
        'WrongMaximum' {$bytes[3]--}
        'SignedBranch' {$bytes[4]=0x7f}
        'WrongLeaRex' {$bytes[6]=0x4c}
        'WrongLeaRegister' {$bytes[8]=0x05}
        'WrongImageBase' {$bytes[9]++}
        'WrongIndexRex' {$bytes[13]=0x48}
        'WrongIndexOpcode' {$bytes[14]=0x8b}
        'WrongIndexRegister' {$bytes[15]=0xc0}
        'WrongLoadRex' {$bytes[16]=0x4c}
        'WrongLoadOpcode' {$bytes[17]=0x89}
        'WrongLoadRegister' {$bytes[18]=0x8c}
        'WrongScale' {$bytes[19]=0x41}
        'WrongTable' {$bytes[20]++}
        'WrongAddRex' {$bytes[24]=0x48}
        'WrongAddRegister' {$bytes[26]=0xc9}
        'WrongJumpRex' {$bytes[27]=0x40}
        'WrongJumpRegister' {$bytes[29]=0xe1}
        'DefaultInGuard' {$bytes[5]=[byte]10}
        'DefaultInterior' {$bytes[5]++}
    }
    $definitions=[Collections.Generic.List[object]]::new()
    $starts=@(0,4,6,13,16,24,27);$lengths=@(4,2,7,3,8,3,3)
    for($i=0;$i-lt7;++$i){$definitions.Add(@{Rva=$prefix+$starts[$i];Bytes=[byte[]]$bytes[$starts[$i]..($starts[$i]+$lengths[$i]-1)];Name="Guard$i"})}
    $definitions.Add(@{Rva=$target;Bytes=[byte[]]@(0x0f,0x1f,0x40,0);Name='Target'})
    $definitions.Add(@{Rva=$table-1-$Padding;Bytes=[byte[]]@(0xc3);Name='Return'})
    if($Padding-eq3){$definitions.Add(@{Rva=$table-3;Bytes=[byte[]]@(0x0f,0x1f,0);Name='Padding'})}
    if($Fault-in'GuardStartEntry','RealEvexBody','RealEvexTarget','RealEvexAdjacent','DuplicateProcedure','OverlappingProcedure',
'GuardInteriorEntry','BodyDataEntry','OutsideEntry') {
        [uint32]$destination=if($Fault-eq'GuardStartEntry'){$prefix}elseif($Fault-eq'GuardInteriorEntry'){$prefix+6}elseif($Fault-eq'BodyDataEntry'){$table}else{$procBegin-1}
        [byte[]]$branch=@(0xe9,0,0,0,0)
        Put-Bytes $branch 1 ([BitConverter]::GetBytes([int32]($destination-($procBegin+5))))
        $definitions.Add(@{Rva=$procBegin;Bytes=$branch;Name='Branch'})
    }
    if($Fault-eq'UnknownIndirect'){$definitions.Add(@{Rva=$procBegin;Bytes=[byte[]]@(0xff,0xe0);Name='OtherJump'})}
    if($Fault-eq'OrphanPrefix'){$definitions.Add(@{Rva=$procBegin;Bytes=[byte[]]@(0xf0);Name='Orphan'})}
    if($Fault-eq'WrongReturn'){$definitions[$definitions.Count-1].Bytes[0]=0x90}
    if($Fault-in'RealVexBody','RealVexTarget','RealVexAdjacent','RealEvexBody','RealEvexTarget','RealEvexAdjacent'){
        [uint32]$vex=if($Fault-in'RealVexTarget','RealEvexTarget'){$target+4}elseif($Fault-in'RealVexAdjacent','RealEvexAdjacent'){$end}else{$prefix+40}
        $definitions.Add(@{Rva=$vex;Bytes=$(if($Fault-like'RealEvex*'){[byte[]]@(0x62,0xf1,0x7c,0x48,0x10,0)}else{[byte[]]@(0xc5,0xf8,0x10,0)});Name='RealVex'})
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
        if($Fault-eq'DuplicateGuard'-and$d.Name-eq'Guard0'){$rows.Add($rows[$rows.Count-1])}
        if($Fault-eq'OverlapGuard'-and$d.Name-eq'Guard0'){$rows.Add(('{0:x}: 90 nop'-f($script:imageBase+$d.Rva+1)))}
    }
    for($i=0;$i-le$Maximum;++$i){
        [uint32]$entry=$target
        if($i-eq$Maximum){switch($Fault){
            'TargetInterior' {$entry=$target+1}
            'TargetGuard' {$entry=$prefix+12}
            'TargetTable' {$entry=$table}
            'TargetOutside' {$entry=$procBegin-1}
            'RealVexTarget' {$entry=$target+4}
            'RealEvexTarget' {$entry=$target+4}
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
    if($Fault-in'DuplicateProcedure','OverlappingProcedure'){
        $otherBegin=if($Fault-eq'DuplicateProcedure'){$procBegin}else{$procBegin-1}
        $sym+="  0 | S_GPROC32 [size = 16] ``OtherScope`` `n  addr = 0001:$($otherBegin-$section), code size = $($procEnd-$otherBegin)`n"
    }
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

foreach($maximum in @(1,6,12,127)){Invoke-Control (New-R9dRcxR8Case ('R9dRcxR8'+$maximum) '' $maximum) $true ($maximum+1) 0}
Invoke-Control (New-R9dRcxR8Case 'R9dRcxR8Padding3' '' 6 3) $true 7 0
Invoke-Control (New-R9dRcxR8Case 'R9dRcxR8GuardStart' 'GuardStartEntry') $true 7 0
$faults=@('WrongCmpRex','WrongCmpOpcode','WrongCmpRegister','WrongMaximum','SignedBranch',
'WrongLeaRex','WrongLeaRegister','WrongImageBase','WrongIndexRex','WrongIndexOpcode','WrongIndexRegister',
'WrongLoadRex','WrongLoadOpcode','WrongLoadRegister','WrongScale','WrongTable','WrongAddRex','WrongAddRegister',
'WrongJumpRex','WrongJumpRegister','DefaultInGuard','DefaultInterior','TargetInterior','TargetGuard','TargetTable',
'TargetOutside','MissingData','WrongDataStart','WrongDataOwner','MissingProcedure','WrongProcedureOwner',
'ShortProcedure','LongProcedure','NamedData','TypedData','InteriorData','WrongReturn','MissingReturn',
'ListingReturn','UnknownReturn','TableListingMismatch','RealVexBody','RealVexTarget','RealVexAdjacent',
'RealEvexBody','RealEvexTarget','RealEvexAdjacent','DuplicateProcedure','OverlappingProcedure',
'GuardInteriorEntry','BodyDataEntry','OutsideEntry','UnknownIndirect','OrphanPrefix','DuplicateGuard','OverlapGuard')
foreach($i in 0..6){$faults+=('MissingGuard'+$i);$faults+=('ListingGuard'+$i);$faults+=('UnknownGuard'+$i)}
foreach($fault in $faults){Invoke-Control (New-R9dRcxR8Case ('R9dRcxR8'+$fault) $fault) $false}
Invoke-Control (New-R9dRcxR8Case 'R9dRcxR8Zero' '' 0) $false
Invoke-Control (New-R9dRcxR8Case 'R9dRcxR8Range128' '' 128) $false
Write-Host ('SUMMARY isa-r9d-rcx-r8 controls='+($faults.Count+8)+' failures=0')
} finally { Stop-Transcript|Out-Null }
