[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
. (Join-Path $repo 'Tests/BuildProcessTests/IsaInlineTableTests.ps1') *> $null
$fixtureRoot = Join-Path $repo ('.build/BuildProcessTests/IsaR15SignedRemap-' + [guid]::NewGuid().ToString('N'))
[void](New-Item -ItemType Directory -Path $fixtureRoot -Force)
$imageBase = [UInt64]0x140000000
$sectionRva = [UInt32]0x0010f000
$guid = [guid]'16c558d2-879f-4b11-a619-568be05a77af'
$age = [UInt32]19
$script:r15Cases = [Collections.Generic.List[string]]::new()
$script:r15Accepted = 0

function R15-SetU16([byte[]]$Bytes,[int]$Offset,[UInt16]$Value) { [Array]::Copy([BitConverter]::GetBytes($Value),0,$Bytes,$Offset,2) }
function R15-SetU32([byte[]]$Bytes,[int]$Offset,[UInt32]$Value) { [Array]::Copy([BitConverter]::GetBytes($Value),0,$Bytes,$Offset,4) }
function R15-SetU64([byte[]]$Bytes,[int]$Offset,[UInt64]$Value) { [Array]::Copy([BitConverter]::GetBytes($Value),0,$Bytes,$Offset,8) }
function R15-Put([byte[]]$Bytes,[int]$Offset,[byte[]]$Value) { [Array]::Copy($Value,0,$Bytes,$Offset,$Value.Length) }
function R15-Hex([byte[]]$Bytes) { (($Bytes | ForEach-Object { $_.ToString('x2') }) -join ' ') }
function R15-Row([Collections.Generic.List[string]]$Rows,[UInt32]$Rva,[byte[]]$Bytes,[string]$Text) {
    $Rows.Add(('{0:x}: {1} {2}' -f ($script:imageBase+$Rva),(R15-Hex $Bytes),$Text))
}

function New-R15Case {
    param([Parameter(Mandatory)][string]$Name,[string]$Fault='',[int]$DwordCount=2,
        [ValidateRange(1,2)][int]$PairCount=1,[ValidateRange(1,128)][int]$ByteExtent=31)
    if($Fault -eq 'MaxByteExtent'){$ByteExtent=128;$DwordCount=128}
    if($Fault -eq 'CountOne'){$DwordCount=1}
    if($Fault -eq 'Count129'){$DwordCount=129}
    $dir=Join-Path $script:fixtureRoot $Name; [void](New-Item -ItemType Directory -Path $dir -Force)
    $binary=Join-Path $dir 'SyntheticIsaParserControl.exe'; $metadata=Join-Path $dir 'contributions.txt'
    $symbols=Join-Path $dir 'symbols.txt'; $disassembly=Join-Path $dir 'disassembly.txt'
    $proc=[UInt32]($script:sectionRva+0x8c5)
    $pe=[byte[]]::new(0x3400); $pe[0]=0x4d;$pe[1]=0x5a
    R15-SetU32 $pe 0x3c 0x80; R15-SetU32 $pe 0x80 0x00004550; R15-SetU16 $pe 0x84 0x8664
    R15-SetU16 $pe 0x86 1; R15-SetU16 $pe 0x94 0xf0; R15-SetU16 $pe 0x98 0x20b
    R15-SetU64 $pe (0x98+24) $script:imageBase; R15-SetU32 $pe (0x98+56) 0x3000; R15-SetU32 $pe (0x98+60) 0x400
    R15-SetU32 $pe (0x98+112+6*8) ($script:sectionRva+0x180); R15-SetU32 $pe (0x98+116+6*8) 28
    $sh=0x80+24+0xf0; R15-Put $pe $sh ([Text.Encoding]::ASCII.GetBytes('.text'+[char]0+[char]0+[char]0))
    R15-SetU32 $pe ($sh+8) 0x3000;R15-SetU32 $pe ($sh+12) $script:sectionRva;R15-SetU32 $pe ($sh+16) $(if($Fault -eq 'SmallerRawSize'){0x800}else{0x3000})
    R15-SetU32 $pe ($sh+20) 0x400;R15-SetU32 $pe ($sh+36) 0x60000020
    R15-SetU32 $pe (0x400+0x180+12) 2;R15-SetU32 $pe (0x400+0x180+16) 24
    R15-SetU32 $pe (0x400+0x180+20) ($script:sectionRva+0x1a0);R15-SetU32 $pe (0x400+0x180+24) 0x5a0
    R15-Put $pe 0x5a0 ([Text.Encoding]::ASCII.GetBytes('RSDS'));R15-Put $pe 0x5a4 $script:guid.ToByteArray();R15-SetU32 $pe 0x5b4 $script:age

    $rows=[Collections.Generic.List[string]]::new();$cursor=$proc
    if($Fault -in @('LoopBypass','JrcxzBypass','BranchBypassDefinition','PrefixedBranchBypass')){
        [byte[]]$branch=if($Fault -eq 'LoopBypass'){@(0xe2,0x07)}elseif($Fault -eq 'JrcxzBypass'){@(0xe3,0x07)}elseif($Fault -eq 'PrefixedBranchBypass'){@(0x2e,0x74,0x07)}else{@(0x74,0x07)}
        R15-Put $pe (0x400+$cursor-$script:sectionRva) $branch;R15-Row $rows $cursor $branch 'branch bypasses base definition';$cursor+=[UInt32]$branch.Length
    }
    [byte[]]$lea=@(0x4c,0x8d,0x3d,0,0,0,0)
    if($Fault -eq 'LeaWrongOpcode'){$lea[0]=0x48} elseif($Fault -eq 'LeaWrongRegister'){$lea[2]=0x35} elseif($Fault -eq 'LeaWrongBase'){$lea[2]=0x0d}
    $disp=[Int32]([Int64]$script:imageBase-[Int64]($script:imageBase+$cursor+7)); if($Fault -eq 'LeaWrongImageBase'){$disp++}
    R15-Put $lea 3 ([BitConverter]::GetBytes($disp));R15-Put $pe (0x400+$cursor-$script:sectionRva) $lea
    R15-Row $rows $cursor $lea 'lea r15, [rip + imagebase]';$cursor+=7
    [byte[]]$landingNop=@(0x90);R15-Put $pe (0x400+$cursor-$script:sectionRva) $landingNop
    R15-Row $rows $cursor $landingNop 'nop table target';$cursor++
    if($Fault -eq 'BranchJoin'){
        [byte[]]$jcc=@(0x74,0x01);$nop=[byte[]]@(0x90)
        R15-Put $pe (0x400+$cursor-$script:sectionRva) $jcc;R15-Row $rows $cursor $jcc 'je join';$cursor+=2
        R15-Put $pe (0x400+$cursor-$script:sectionRva) $nop;R15-Row $rows $cursor $nop 'nop';$cursor++
    }
    if($Fault -eq 'ExternalCall'){
        [byte[]]$call=@(0xe8,0,0,0,0);[Int32]$externalRel=[Int32]([Int64]($script:sectionRva+0x2f00)-[Int64]($cursor+5))
        R15-Put $call 1 ([BitConverter]::GetBytes($externalRel));R15-Put $pe (0x400+$cursor-$script:sectionRva) $call
        R15-Row $rows $cursor $call 'call external';$cursor+=5
    }
    if($Fault -eq 'LocalSubroutineCall'){
        [byte[]]$call=@(0xe8,0,0,0,0);[Int32]$rel=[Int32]([Int64]$proc-[Int64]($cursor+5))
        R15-Put $call 1 ([BitConverter]::GetBytes($rel));R15-Put $pe (0x400+$cursor-$script:sectionRva) $call
        R15-Row $rows $cursor $call 'call local-subroutine';$cursor+=5
    }
    if($Fault -in @('ClobberMov','ClobberXor','ClobberPop')){
        [byte[]]$clobber=if($Fault -eq 'ClobberMov'){@(0x49,0x89,0xc7)}elseif($Fault -eq 'ClobberXor'){@(0x4d,0x31,0xff)}else{@(0x41,0x5f)}
        R15-Put $pe (0x400+$cursor-$script:sectionRva) $clobber;R15-Row $rows $cursor $clobber $Fault;$cursor+=[UInt32]$clobber.Length
    }
    if($Fault -eq 'AtomicSafe'){
        [byte[]]$lockPrefix=@(0xf0);[byte[]]$atomic=@(0x49,0x83,0x07,0x01)
        R15-Put $pe (0x400+$cursor-$script:sectionRva) $lockPrefix;R15-Row $rows $cursor $lockPrefix 'lock prefix';$cursor++
        R15-Put $pe (0x400+$cursor-$script:sectionRva) $atomic;R15-Row $rows $cursor $atomic 'add qword ptr [r15], 1';$cursor+=4
    }
    if($Fault -eq 'R15Read'){
        [byte[]]$read=@(0x4c,0x89,0xf8);R15-Put $pe (0x400+$cursor-$script:sectionRva) $read
        R15-Row $rows $cursor $read 'mov rax, r15';$cursor+=3
    }
    $dispatchStarts=[Collections.Generic.List[UInt32]]::new();$dispatchRows=[Collections.Generic.List[hashtable]]::new()
    $dwordRvas=[Collections.Generic.List[UInt32]]::new();$byteRvas=[Collections.Generic.List[UInt32]]::new()
    [UInt32]$incomingLength=if($Fault -in @('IncomingData','IncomingProtectedInterior')){5}else{0}
    [UInt32]$vectorLength=if($Fault -eq 'RealVex'){4}elseif($Fault -eq 'RealEvex'){6}else{0}
    [UInt32]$dispatchBranchLength=if($PairCount -eq 2){6}else{0}
    [UInt32]$anticipatedEnd=$cursor+$incomingLength+$dispatchBranchLength+30*$PairCount+$vectorLength+4
    [UInt32]$firstTable=[UInt32](($anticipatedEnd+3)-band [UInt64]4294967292)
    [UInt32]$leadingPadding=$firstTable-$anticipatedEnd
    [UInt32]$nextTable=$firstTable
    for($pair=0;$pair -lt $PairCount;$pair++){
        $dwordRvas.Add($nextTable)
        [UInt32]$byteAt=[UInt32]($nextTable+4*$DwordCount);$byteRvas.Add($byteAt)
        $extent=if($pair -eq $PairCount-1 -and $Fault -eq 'ProcedureEndAltered'){$ByteExtent+1}else{$ByteExtent}
        [UInt32]$aligned=[UInt32](($byteAt+$extent+3)-band [UInt64]4294967292)
        if($pair -eq 0 -and $PairCount -gt 1 -and $Fault -in @('TooMuchPadding','UnnecessaryPadding')){$aligned+=4}
        $nextTable=$aligned
    }
    if($incomingLength){
        [UInt32]$destination=if($Fault -eq 'IncomingData'){$firstTable}else{[UInt32]($cursor+5+$dispatchBranchLength+9)}
        [byte[]]$branch=@(0xe9,0,0,0,0);[Int32]$relative=[Int32]([Int64]$destination-[Int64]($cursor+5))
        R15-Put $branch 1 ([BitConverter]::GetBytes($relative));R15-Put $pe (0x400+$cursor-$script:sectionRva) $branch
        R15-Row $rows $cursor $branch 'jmp forbidden';$cursor+=5
    }
    if($PairCount -eq 2){
        [byte[]]$split=@(0x0f,0x84,0,0,0,0);[Int32]$splitRel=30
        R15-Put $split 2 ([BitConverter]::GetBytes($splitRel));R15-Put $pe (0x400+$cursor-$script:sectionRva) $split
        R15-Row $rows $cursor $split 'je second-dispatch';$cursor+=6
    }
    $badRowKinds=@('Movsx','Movzx','DwordLoad','Add','Jump')
    for($pair=0;$pair -lt $PairCount;$pair++){
        [UInt32]$at=$cursor;$dispatchStarts.Add($at)
        [byte[]]$a=@(0x48,0x0f,0xbe,0x85,0xc0,0x01,0,0)
        [byte[]]$b=@(0x41,0x0f,0xb6,0x84,0x07,0,0,0,0)
        [byte[]]$c=@(0x41,0x8b,0x8c,0x87,0,0,0,0)
        [byte[]]$d=@(0x49,0x03,0xcf);[byte[]]$e=@(0xff,0xe1)
        if(($Fault -like 'Bad*' -and $pair -eq 0) -or ($Fault -eq 'UnprovedPair' -and $pair -eq 1)){
            switch($Fault){
                'BadMovsxOpcode' {$a[2]=0xb6}
                'BadMovsxRegister' {$a[0]=0x49}
                'BadMovsxBase' {$a[3]=0x84}
                'BadMovzxOpcode' {$b[2]=0xbe}
                'BadMovzxDestination' {$b[3]=0x8c}
                'BadMovzxIndex' {$b[4]=0x06}
                'BadMovzxBase' {$b[0]=0x40}
                'BadDwordLoadOpcode' {$c[1]=0x89}
                'BadDwordDestination' {$c[2]=0x84}
                'BadDwordIndex' {$c[3]=0x8f}
                'BadAddRegister' {$d[2]=0xc8}
                'BadAddOpcode' {$d[1]=0x01}
                'BadJumpRegister' {$e[1]=0xe0}
                'UnprovedPair' {$a[2]=0xb6}
            }
        }
        $loadRva=[UInt32]$dwordRvas[$pair]
        $byteRva=[UInt32]$byteRvas[$pair]
        R15-Put $b 5 ([BitConverter]::GetBytes([UInt32]($byteRva+1)))
        R15-Put $c 4 ([BitConverter]::GetBytes($loadRva))
        if($Fault -eq 'BadBias' -and $pair -eq 0){R15-Put $b 5 ([BitConverter]::GetBytes([UInt32]$byteRva))}
        if($Fault -eq 'BadDwordIndex' -and $pair -eq 0){$c[3]=0x8f}
        if($Fault -eq 'BadByteIndex' -and $pair -eq 0){$b[4]=0x06}
        if($Fault -eq 'BadIndexRegister' -and $pair -eq 0){$a[0]=0x49}
        $parts=@(@{B=$a;T='movsx rax, byte ptr [rbp+1c0h]'},@{B=$b;T='movzx eax, byte ptr [r15+rax+index+1]'},
            @{B=$c;T='mov ecx, dword ptr [r15+4*rax+table]'},@{B=$d;T='add rcx, r15'},@{B=$e;T='jmp rcx'})
        foreach($part in $parts){
            R15-Put $pe (0x400+$cursor-$script:sectionRva) $part.B
            $dispatchRows.Add(@{Rva=$cursor;Bytes=[byte[]]$part.B.Clone();Text=$part.T})
            $mutation='';$wanted=''
            if($Fault -match '^(Missing|Unknown|Duplicate|Overlap|Mismatch)(Movsx|Movzx|DwordLoad|Add|Jump)$'){$mutation=$Matches[1];$wanted=$Matches[2]}
            $rowId=if($part.B.Length -eq 8 -and $part.B[0] -eq 0x48){'Movsx'}elseif($part.B.Length -eq 9){'Movzx'}elseif($part.B.Length -eq 8){'DwordLoad'}elseif($part.B.Length -eq 3){'Add'}else{'Jump'}
            if($pair -eq 0 -and $mutation -eq 'Missing' -and $wanted -eq $rowId){}
            elseif($pair -eq 0 -and $mutation -eq 'Unknown' -and $wanted -eq $rowId){R15-Row $rows $cursor $part.B '<unknown>'}
            elseif($pair -eq 0 -and $mutation -eq 'Mismatch' -and $wanted -eq $rowId){[byte[]]$wrong=[byte[]]$part.B.Clone();$wrong[$wrong.Length-1]=$wrong[$wrong.Length-1]-bxor 1;R15-Row $rows $cursor $wrong $part.T}
            else{R15-Row $rows $cursor $part.B $part.T}
            if($pair -eq 0 -and $mutation -eq 'Duplicate' -and $wanted -eq $rowId){R15-Row $rows $cursor $part.B $part.T}
            if($pair -eq 0 -and $mutation -eq 'Overlap' -and $wanted -eq $rowId){R15-Row $rows ($cursor+1) ([byte[]]@(0x90)) 'overlap'}
            $cursor+=[UInt32]$part.B.Length
        }
    }
    if($Fault -eq 'RealVex' -or $Fault -eq 'RealEvex'){
        [byte[]]$v=if($Fault -eq 'RealVex'){@(0xc5,0xf8,0x10,0x00)}else{@(0x62,0xf1,0x7c,0x48,0x58,0xc0)}
        R15-Put $pe (0x400+$cursor-$script:sectionRva) $v
        R15-Row $rows $cursor $v $(if($Fault -eq 'RealVex'){'vmovups xmm0, xmmword ptr [rax]'}else{'vaddps zmm0, zmm0, zmm0'});$cursor+=[UInt32]$v.Length
    }
    for($i=0;$i -lt $leadingPadding;$i++){
        [byte[]]$nop=@(0x90);R15-Put $pe (0x400+$cursor-$script:sectionRva) $nop;R15-Row $rows $cursor $nop 'nop';$cursor++
    }
    # The exact terminal trap and three-byte alignment NOP precede all table data.
    [byte[]]$cc=@(0xcc);[byte[]]$pad=@(0x0f,0x1f,0x00)
    R15-Put $pe (0x400+$cursor-$script:sectionRva) $cc;R15-Row $rows $cursor $cc 'int3';$cursor++
    R15-Put $pe (0x400+$cursor-$script:sectionRva) $pad;R15-Row $rows $cursor $pad 'nop dword ptr [rax]';$cursor+=3
    [UInt32]$codeEnd=$cursor
    for($pair=0;$pair -lt $PairCount;$pair++){
        [UInt32]$dw=$dwordRvas[$pair];[UInt32]$by=$byteRvas[$pair]
        if($Fault -eq 'MisalignedTable' -and $pair -eq 0){$dw++}
        for($i=0;$i -lt $DwordCount;$i++){
            [UInt32]$target=if($i -eq 0){$proc}else{[UInt32]($proc+7)}
            if($Fault -eq 'TargetOutside' -and $i -eq 1){$target=$script:sectionRva+0x2f00}
            if($Fault -eq 'TargetData' -and $i -eq 1){$target=$dw}
            if($Fault -eq 'TargetInterior' -and $i -eq 1){$target=$dispatchStarts[0]+1}
            if($Fault -eq 'TargetDecodedBoundary' -and $i -eq 1){$target=$dispatchStarts[0]+2}
            R15-SetU32 $pe (0x400+$dw-$script:sectionRva+4*$i) $target
            [byte[]]$db=[BitConverter]::GetBytes($target)
            R15-Row $rows ($dw+4*$i) $db 'opaque DWORD table data'
        }
        $extent=if($pair -eq $PairCount-1 -and $Fault -eq 'ProcedureEndAltered'){$ByteExtent+1}else{$ByteExtent}
        for($i=0;$i -lt $extent;$i++){
            [byte]$index=0
            if($Fault -eq 'IndexOutOfRange' -and $i -eq $extent-1){$index=[byte]$DwordCount}
            $pe[0x400+$by-$script:sectionRva+$i]=$index
            R15-Row $rows ($by+$i) ([byte[]]@($index)) 'opaque byte table data'
        }
        [UInt32]$byteEnd=$by+$extent
        [UInt32]$suffixEnd=if($pair -lt $PairCount-1){$dwordRvas[$pair+1]}else{$byteEnd}
        for([UInt32]$padAt=$byteEnd;$padAt -lt $suffixEnd;$padAt++){
            [byte]$padByte=0x90;if($Fault -eq 'SuffixNonNop' -and $pair -eq 0 -and $padAt -eq $byteEnd){$padByte=0x91}
            $pe[0x400+$padAt-$script:sectionRva]=$padByte;R15-Row $rows $padAt ([byte[]]@($padByte)) 'nop'
        }
    }
    if($Fault -eq 'VexLookingTarget'){
        # The first table DWORD is the VEX-looking sequence c5 f8 10 00; it names the procedure's first row.
        R15-SetU32 $pe (0x400+$dwordRvas[0]-$script:sectionRva) 0x0010f8c5
        $rows.RemoveAll([Predicate[string]]{param($x);$x.StartsWith(('{0:x}:' -f ($script:imageBase+$dwordRvas[0])))})|Out-Null
        R15-Row $rows $dwordRvas[0] ([byte[]]@(0xc5,0xf8,0x10,0x00)) 'vmovups-looking table bits'
    }
    [UInt32]$end=$byteRvas[$PairCount-1]+$(if($Fault -eq 'ProcedureEndAltered'){$ByteExtent+1}else{$ByteExtent})
    if ($Fault -in @('RealVexAfterTable','RealEvexAfterTable')) {
        [byte[]]$outside = if ($Fault -eq 'RealVexAfterTable') { @(0xc5,0xf8,0x10,0x00) } else { @(0x62,0xf1,0x7c,0x48,0x58,0xc0) }
        R15-Put $pe (0x400+$end-$script:sectionRva) $outside
        R15-Row $rows $end $outside 'real executable vector after the exact procedure/table end'
    }
    if($Fault -eq 'BadProcedureEnd'){$end++}
    if($Fault -eq 'TruncatedPe'){$cut=0x400+$dwordRvas[0]-$script:sectionRva-1;$pe=[byte[]]$pe[0..$cut]}
    $module='SyntheticR15.obj'
    $moduleHeader="  Mod 0 | ``$module``:`n  Obj: ``$module``:`n"
    $metadataText="  Age: $script:age`n  GUID: {$script:guid}`n$moduleHeader`nSection Contributions`n  SC[.text] | mod = 0, 0001:0, size = 12288, data crc = 0, reloc crc = 0`n"
    Set-Content -LiteralPath $metadata -Value $metadataText -Encoding utf8
    $sym=$moduleHeader
    if($Fault -ne 'MissingProcedure'){
        $procStart=if($Fault -eq 'ProcedureOwnerMismatch'){$proc+1}else{$proc}
        $procOwner=if($Fault -eq 'ProcedureOwnerMismatch'){1}else{0}
        $sym+="  Mod $procOwner | ``$module``:`n  0 | S_GPROC32 [size = 16] ``SyntheticOwner```n  addr = 0001:$($procStart-$script:sectionRva), code size = $($end-$procStart)`n"
    }
    for($pair=0;$pair -lt $PairCount;$pair++){
        $dw=$dwordRvas[$pair];$by=$byteRvas[$pair]
        if($Fault -eq 'MissingPair' -and $pair -eq 0){continue}
        $dwSymbol=if($Fault -eq 'DwordSymbolMismatch' -and $pair -eq 0){$dw+4}else{$dw}
        $bySymbol=if($Fault -eq 'ByteSymbolMismatch' -and $pair -eq 0){$by+1}else{$by}
        $dwOwner=if($Fault -in @('OwnerMismatch','WrongDwordOwner') -and $pair -eq 0){1}else{0}
        $byOwner=if($Fault -in @('OwnerMismatch','WrongByteOwner') -and $pair -eq 0){1}else{0}
        $dwName=if($Fault -eq 'UnknownDataSymbol' -and $pair -eq 0){'named'}else{''}
        $byName=if($Fault -eq 'UnknownDataSymbol' -and $pair -eq 0){'named'}else{''}
        if($Fault -notin @('MissingPair','MissingDwordSymbol')){$sym+="  Mod $dwOwner | ``$module``:`n  0 | S_LDATA32 [size = 16] ``$dwName```n  type = <no type>, addr = 0001:$($dwSymbol-$script:sectionRva)`n"}
        if($Fault -notin @('MissingPair','MissingByteSymbol')){$sym+="  Mod $byOwner | ``$module``:`n  0 | S_LDATA32 [size = 16] ``$byName```n  type = <no type>, addr = 0001:$($bySymbol-$script:sectionRva)`n"}
    }
    Set-Content -LiteralPath $symbols -Value $sym -Encoding utf8
    if($Fault -eq 'MissingRow'){$rows.RemoveAt(1)}
    if($Fault -eq 'UnknownRow'){$rows[1]=($rows[1] -replace '90 nop','90 <unknown>')}
    if($Fault -eq 'DuplicateRow'){$rows.Add($rows[1])}
    if($Fault -eq 'RowMismatch'){$rows[0]=$rows[0].Replace('4c 8d','48 8d')}
    if($Fault -eq 'OverlapRow'){$rows.Add(('{0:x}: 90 nop' -f ($script:imageBase+$proc+1)))}
    if($Fault -eq 'BrokenPdbGuid'){$metadataText=$metadataText.Replace($script:guid.ToString(),'00000000-0000-0000-0000-000000000000');Set-Content -LiteralPath $metadata -Value $metadataText -Encoding utf8}
    Set-Content -LiteralPath $disassembly -Value (@($rows | Sort-Object {[UInt64]::Parse(($_ -split ':',2)[0],[Globalization.NumberStyles]::HexNumber)}) -join "`n") -Encoding utf8
    [IO.File]::WriteAllBytes($binary,$pe)
    return @{Name=$Name;Binary=$binary;Metadata=$metadata;Symbols=$symbols;Disassembly=$disassembly}
}

function Invoke-R15Control([string]$Name,[string]$Fault,[bool]$ExpectedPass,[int]$Pairs=1,[int]$Count=2,[int]$Extent=31,[int]$ExpectedInline=-1,[int]$ExpectedVectors=-1){
    $case=New-R15Case $Name $Fault $Count $Pairs $Extent
    try{$result=[RedPrismBuild.IsaScanner]::Scan($case.Binary,$case.Metadata,$case.Symbols,$case.Disassembly,[string[]]@(),[string[]]@(),[string[]]@())}
    catch [IO.InvalidDataException] {
        if ($ExpectedPass -or $Fault -notin @('BrokenPdbGuid','TruncatedPe')) { throw }
        $script:r15Cases.Add($Name); Write-Host "PASS isa-r15-signed-remap.$Name"; return
    }
    if ($ExpectedPass -and $ExpectedInline -lt 0) { $ExpectedInline = $Pairs }
    if ($ExpectedPass -and $ExpectedVectors -lt 0) { $ExpectedVectors = 0 }
    if($ExpectedPass -and $result.Violations.Count){throw "${Name}: unexpected ISA rejection: $($result.Violations -join '; ')"}
    if(-not $ExpectedPass -and -not $result.Violations.Count){throw "${Name}: malformed synthetic fixture was accepted"}
    if($ExpectedInline -ge 0 -and $result.InlineTableVectorData -ne $ExpectedInline){throw "${Name}: expected $ExpectedInline proved data rows, got $($result.InlineTableVectorData)"}
    if($ExpectedVectors -ge 0 -and $result.VectorInstructions -ne $ExpectedVectors){throw "${Name}: expected $ExpectedVectors vector instructions, got $($result.VectorInstructions)"}
    $script:r15Cases.Add($Name);if($ExpectedPass){++$script:r15Accepted};Write-Host "PASS isa-r15-signed-remap.$Name"
}

Invoke-R15Control 'SinglePair' '' $true
Invoke-R15Control 'TwoPairsOneNopSuffix' '' $true 2 2 31
Invoke-R15Control 'MaximumByteExtent128' 'MaxByteExtent' $true 1 128 128
Invoke-R15Control 'BranchJoin' 'BranchJoin' $true
Invoke-R15Control 'ExternalCallAbi' 'ExternalCall' $true
Invoke-R15Control 'StandaloneF0AtomicSafe' 'AtomicSafe' $true
Invoke-R15Control 'R15ReadAsSource' 'R15Read' $true
Invoke-R15Control 'VexLookingTableTarget' 'VexLookingTarget' $true 1 2 31 1 0
Invoke-R15Control 'GenuineVexExecutableRow' 'RealVex' $false 1 2 31 1 1
Invoke-R15Control 'GenuineEvexExecutableRow' 'RealEvex' $false 1 2 31 1 1
Invoke-R15Control 'GenuineVexAfterTable' 'RealVexAfterTable' $false 1 2 31 1 1
Invoke-R15Control 'GenuineEvexAfterTable' 'RealEvexAfterTable' $false 1 2 31 1 1
foreach($fault in @('BadMovsxOpcode','BadMovsxRegister','BadMovsxBase','BadMovzxOpcode','BadMovzxDestination','BadMovzxIndex','BadMovzxBase',
    'BadDwordLoadOpcode','BadDwordDestination','BadDwordIndex','BadAddRegister','BadAddOpcode','BadJumpRegister',
    'LeaWrongOpcode','LeaWrongRegister','LeaWrongBase','LeaWrongImageBase','ClobberMov','ClobberXor','ClobberPop',
    'BranchBypassDefinition','LoopBypass','JrcxzBypass','PrefixedBranchBypass','BadBias','CountOne','Count129',
    'MissingProcedure','ProcedureOwnerMismatch','OwnerMismatch','MissingPair','UnknownDataSymbol','MissingDwordSymbol','MissingByteSymbol',
    'WrongDwordOwner','WrongByteOwner','DwordSymbolMismatch','ByteSymbolMismatch','MisalignedTable','IndexOutOfRange','TargetDecodedBoundary','TargetData','TargetOutside','TargetInterior',
    'IncomingData','IncomingProtectedInterior','SmallerRawSize','MissingRow','UnknownRow','DuplicateRow','OverlapRow','RowMismatch',
    'BrokenPdbGuid','TruncatedPe','LocalSubroutineCall')){
    Invoke-R15Control $fault $fault $false
}
Invoke-R15Control 'UnprovedPair' 'UnprovedPair' $false 2 2 31
foreach($fault in @('SuffixNonNop','TooMuchPadding')){Invoke-R15Control $fault $fault $false 2 2 31}
Invoke-R15Control 'UnnecessarySuffixPadding' 'UnnecessaryPadding' $false 2 2 32
Invoke-R15Control 'ProcedureEndChangedExtent' 'ProcedureEndAltered' $false 2 2 31
Invoke-R15Control 'ProcedureEndExtraUnlistedByte' 'BadProcedureEnd' $false 2 2 31
foreach($rowName in @('Movsx','Movzx','DwordLoad','Add','Jump')){
    foreach($mutation in @('Missing','Duplicate','Unknown','Overlap','Mismatch')){
        $fault=$mutation+$rowName;Invoke-R15Control $fault $fault $false
    }
}
Write-Host "SUMMARY isa-r15-signed-remap controls=$($script:r15Cases.Count) accepted=$script:r15Accepted refused=$($script:r15Cases.Count-$script:r15Accepted)"
