# Independent PE/PDB/decoder controls for the exact 51-byte wide RCX stack remap.
# DWORD RVAs deliberately begin with C5, while every target/default remains a proved body boundary.
[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repo = $PSScriptRoot
while ($repo -and -not (Test-Path -LiteralPath (Join-Path $repo 'Build/IsaScan.cs'))) {
    $parent = Split-Path -Parent $repo
    if ($parent -eq $repo) { throw 'Could not locate repository Build/IsaScan.cs from fixture path.' }
    $repo = $parent
}
if (-not ('RedPrismBuild.IsaScanner' -as [type])) { Add-Type -Path (Join-Path $repo 'Build/IsaScan.cs') }
$fixtureRoot = Join-Path $repo ('.build/BuildProcessTests/IsaWideRcxByteRemap-' + [guid]::NewGuid().ToString('N'))
[void](New-Item -ItemType Directory -Path $fixtureRoot -Force)

# Fixed layout: guard [24 bytes] + dispatch [27 bytes], eight image-relative DWORD
# targets, then maximum+1 byte indices. Procedure end equals byte-table end. Eight
# decoded 2-byte NOP boundaries are decoded before the DWORD table. A later procedure and
# anonymous symbol exercise same-contribution continuation without rescuing owner bounds.
$script:ImageBase = [UInt64]0x140000000
$script:SectionRva = [UInt32]0x1000
$script:DispatchRva = [UInt32]0x1120
$script:GuardRva = [UInt32]($script:DispatchRva - 24)
$script:TargetRva = [UInt32]0x11c5
$script:DwordRva = [UInt32]0x1200
$script:ByteRva = [UInt32]($script:DwordRva + 32)
$script:Stack = [UInt32]0x0c98
$script:Guid = [guid]'c12611e2-4f91-4ea4-83cc-9ad48f3a6135'
$script:Age = [UInt32]7
$script:CaseCount = 0

function Set-U16([byte[]]$Bytes,[int]$Offset,[UInt16]$Value) { [Array]::Copy([BitConverter]::GetBytes($Value),0,$Bytes,$Offset,2) }
function Set-U32([byte[]]$Bytes,[int]$Offset,[UInt32]$Value) { [Array]::Copy([BitConverter]::GetBytes($Value),0,$Bytes,$Offset,4) }
function Set-U64([byte[]]$Bytes,[int]$Offset,[UInt64]$Value) { [Array]::Copy([BitConverter]::GetBytes($Value),0,$Bytes,$Offset,8) }
function Put-Bytes([byte[]]$Bytes,[int]$Offset,[byte[]]$Value) { [Array]::Copy($Value,0,$Bytes,$Offset,$Value.Length) }
function Hex-Bytes([byte[]]$Bytes) { (($Bytes | ForEach-Object { $_.ToString('x2') }) -join ' ') }
function Add-ListingRow($Rows,[UInt64]$Address,[byte[]]$Bytes,[string]$Text) {
    $Rows.Add(('{0:x}: {1} {2}' -f $Address,(Hex-Bytes $Bytes),$Text))
}
function Set-FieldBytes([byte[]]$Bytes,[int]$Start,[byte[]]$Value) { Put-Bytes $Bytes $Start $Value }

function New-WideControl([string]$Name,[string]$Fault='',[int]$Maximum=226,[UInt64]$ImageBase=$script:ImageBase,
    [UInt32]$SectionRva=$script:SectionRva,[UInt32]$StackDisplacement=$script:Stack) {
    if ($Maximum -lt 1 -or $Maximum -gt 4095) { throw "Fixture maximum outside intended accepted range: $Maximum" }
    $dispatch = [UInt32]($SectionRva + 0x120)
    $guardRva = [UInt32]($dispatch - 24)
    $target = [UInt32]($SectionRva + 0x1c5)
    $dword = [UInt32]($SectionRva + 0x200)
    $byte = [UInt32]($dword + 32)
    $procedureStart = $guardRva
    $procedureEnd = [UInt32]($byte + $Maximum + 1)
    $procedureStartOffset=[UInt32]($procedureStart-$SectionRva); $procedureEndOffset=[UInt32]($procedureEnd-$SectionRva)
    $dwordOffset=[UInt32]($dword-$SectionRva); $byteOffset=[UInt32]($byte-$SectionRva)
    $dir = Join-Path $fixtureRoot $Name
    [void](New-Item -ItemType Directory -Path $dir -Force)
    $binary=Join-Path $dir 'Synthetic.exe'; $metadata=Join-Path $dir 'contributions.txt'
    $symbols=Join-Path $dir 'symbols.txt'; $disassembly=Join-Path $dir 'disassembly.txt'

    $pe=[byte[]]::new(0x6000); $pe[0]=0x4d; $pe[1]=0x5a
    Set-U32 $pe 0x3c 0x80; Set-U32 $pe 0x80 0x00004550; Set-U16 $pe 0x84 0x8664
    Set-U16 $pe 0x86 1; Set-U16 $pe 0x94 0xf0; Set-U16 $pe 0x98 0x20b
    Set-U64 $pe (0x98+24) $ImageBase; Set-U32 $pe (0x98+56) ([UInt32]($SectionRva+0x5000)); Set-U32 $pe (0x98+60) 0x400
    $debugRva=[UInt32]($SectionRva+0x4000); Set-U32 $pe (0x98+112+6*8) $debugRva; Set-U32 $pe (0x98+116+6*8) 28
    $sectionHeader=0x80+24+0xf0
    Put-Bytes $pe $sectionHeader ([Text.Encoding]::ASCII.GetBytes('.text'+[char]0+[char]0+[char]0))
    Set-U32 $pe ($sectionHeader+8) 0x5000; Set-U32 $pe ($sectionHeader+12) $SectionRva
    Set-U32 $pe ($sectionHeader+16) 0x5000; Set-U32 $pe ($sectionHeader+20) 0x400; Set-U32 $pe ($sectionHeader+36) 0x60000020
    # CodeView debug directory and RSDS identity, independent of external PDB tooling.
    $debugFile=0x4400
    Set-U32 $pe ($debugFile+12) 2; Set-U32 $pe ($debugFile+16) 24
    Set-U32 $pe ($debugFile+20) ([UInt32]($SectionRva+0x4020)); Set-U32 $pe ($debugFile+24) 0x4420
    Put-Bytes $pe 0x4420 ([Text.Encoding]::ASCII.GetBytes('RSDS')); Put-Bytes $pe 0x4424 $script:Guid.ToByteArray()
    Set-U32 $pe 0x4434 $script:Age

    # Wide exact instruction form. cmp=11, ja=6, mov=7 bytes; rel32 starts at JA+2.
    [byte[]]$guard=[byte[]]@(0x81,0xbc,0x24,0,0,0,0,0,0,0,0, 0x0f,0x87,0,0,0,0, 0x8b,0x84,0x24,0,0,0,0)
    Put-Bytes $guard 3 ([BitConverter]::GetBytes($StackDisplacement)); Put-Bytes $guard 7 ([BitConverter]::GetBytes([UInt32]$Maximum))
    Put-Bytes $guard 20 ([BitConverter]::GetBytes($StackDisplacement))
    $defaultTarget=[UInt32]$target
    Put-Bytes $guard 13 ([BitConverter]::GetBytes([Int32]($defaultTarget-($guardRva+17))))

    # Exact 27-byte dispatch: LEA RCX,imageBase; MOVZX byte; MOV dword; ADD; JMP.
    [byte[]]$lea=[byte[]]@(0x48,0x8d,0x0d,0,0,0,0)
    Put-Bytes $lea 3 ([BitConverter]::GetBytes([Int32]($ImageBase-($ImageBase+$dispatch+7))))
    [byte[]]$byteLoad=[byte[]]@(0x0f,0xb6,0x84,0x01,0,0,0,0)
    Put-Bytes $byteLoad 4 ([BitConverter]::GetBytes($byte))
    [byte[]]$dwordLoad=[byte[]]@(0x8b,0x84,0x81,0,0,0,0)
    Put-Bytes $dwordLoad 3 ([BitConverter]::GetBytes($dword))
    [byte[]]$add=[byte[]]@(0x48,0x03,0xc1); [byte[]]$jump=[byte[]]@(0xff,0xe0)
    $fileBase=0x400-$SectionRva
    Put-Bytes $pe ($fileBase+$guardRva) $guard; Put-Bytes $pe ($fileBase+$dispatch) $lea
    Put-Bytes $pe ($fileBase+$dispatch+7) $byteLoad; Put-Bytes $pe ($fileBase+$dispatch+15) $dwordLoad
    Put-Bytes $pe ($fileBase+$dispatch+22) $add; Put-Bytes $pe ($fileBase+$dispatch+25) $jump

    # Every DWORD deliberately repeats one decoded C5-prefixed RVA; additional body boundaries remain real. The default
    # branch target is likewise a real instruction boundary inside the owning procedure.
    [byte[]]$dwords=[byte[]]::new(32); $targetRvas=[Collections.Generic.List[UInt32]]::new()
    for($i=0;$i -lt 8;$i++) { $tr=[UInt32]($target+2*$i); $targetRvas.Add($tr); Put-Bytes $dwords (4*$i) ([BitConverter]::GetBytes($target)) }
    Put-Bytes $pe ($fileBase+$dword) $dwords
    [byte[]]$remap=[byte[]]::new($Maximum+1)
    for($i=0;$i -lt $remap.Length;$i++) { $remap[$i]=[byte]($i%8) }
    Put-Bytes $pe ($fileBase+$byte) $remap
    for($i=0;$i -lt 8;$i++) { Put-Bytes $pe ($fileBase+$target+2*$i) ([byte[]]@(0x66,0x90)) }
    # Later same-contribution procedure and anonymous symbol are valid after this owner.
    Put-Bytes $pe ($fileBase+$procedureEnd) ([byte[]]@(0x66,0x90))
    if($Fault -in 'ActualVexOutside','ActualEvexOutside','ActualVexInBody','ActualEvexInBody') {
        if($Fault -in 'ActualVexOutside','ActualVexInBody') { [byte[]]$vector=[byte[]]@(0xc5,0xf8,0x10,0x00); $vectorText='vmovups xmm0, xmmword ptr [rax]' }
        else { [byte[]]$vector=[byte[]]@(0x62,0xf1,0x7c,0x48,0x10,0x00); $vectorText='vmovups zmm0, zmmword ptr [rax]' }
        $vectorRva=if($Fault -like '*InBody'){$target+0x30}else{$procedureEnd+4}; Put-Bytes $pe ($fileBase+$vectorRva) $vector
    }

    # Table sizes are native symbol starts; no owner symbol may bisect either interval.
    $byteEnd=$procedureEnd
    $module="  Mod 0000 | ``Synthetic.obj``:
  Obj: ``Synthetic.lib``
"
    $metadataText="  Age: $script:Age
  GUID: {$script:Guid}
$module
Section Contributions
  SC[.text]  | mod = 0, 0001:0, size = 20480, data crc = 0, reloc crc = 0
"
    $symbolText=$module +
        "  0 | S_GPROC32 [size = 40] ``SyntheticWideDispatch``
    addr = 0001:$procedureStartOffset, code size = $($procedureEnd-$procedureStart)
"+
        "  0 | S_LDATA32 [size = 16] ````
    type = <no type>, addr = 0001:$dwordOffset
"+
        "  0 | S_LDATA32 [size = 16] ````
    type = <no type>, addr = 0001:$byteOffset
"+
        "  0 | S_GPROC32 [size = 40] ``SyntheticLaterProcedure``
    addr = 0001:$procedureEndOffset, code size = 2
"+
        "  0 | S_LDATA32 [size = 16] ````
    type = <no type>, addr = 0001:$($procedureEndOffset+16)
"

    # One literal listing row per recognized instruction, plus apparent VEX rows over the
    # eight DWORD values. Listing bytes are independently mutable from PE bytes below.
    $rows=[Collections.Generic.List[string]]::new()
    $rowSpecs=@(
        @{Name='GuardCmp';Rva=$guardRva;Bytes=[byte[]]$guard[0..10];Text='cmp dword ptr [rsp + disp], max'},
        @{Name='GuardJa';Rva=($guardRva+11);Bytes=[byte[]]$guard[11..16];Text='ja default'},
        @{Name='GuardIndex';Rva=($guardRva+17);Bytes=[byte[]]$guard[17..23];Text='mov eax, dword ptr [rsp + disp]'},
        @{Name='Base';Rva=$dispatch;Bytes=$lea;Text='lea rcx, [rip + imagebase]'},
        @{Name='Byte';Rva=($dispatch+7);Bytes=$byteLoad;Text='movzx eax, byte ptr [rcx + rax + remap]'},
        @{Name='Dword';Rva=($dispatch+15);Bytes=$dwordLoad;Text='mov eax, dword ptr [rcx + rax*4 + targets]'},
        @{Name='Add';Rva=($dispatch+22);Bytes=$add;Text='add rax, rcx'},
        @{Name='Jump';Rva=($dispatch+25);Bytes=$jump;Text='jmp rax'})

    # Optional faults mutate exactly one recognizer field. Stack displacement mutations
    # match both PE/listing; stack mismatch controls independently change a compared/indexed slot.
    $oneByteFaults=@{
        WrongGuardOpcode=@('GuardCmp',0,0x80); WrongGuardRegister=@('GuardCmp',1,0xb4); WrongGuardSib=@('GuardCmp',2,0x25)
        WrongGuardDisp=@('GuardCmp',3,0x99); WrongJaOpcode=@('GuardJa',0,0x0e)
        WrongIndexOpcode=@('GuardIndex',0,0x89); WrongIndexRegister=@('GuardIndex',1,0x8c)
        WrongIndexSib=@('GuardIndex',2,0x25); WrongIndexDisp=@('GuardIndex',3,0x99)
        WrongLeaOpcode=@('Base',0,0x49); WrongLeaRegister=@('Base',2,0x05); WrongLeaBase=@('Base',2,0x0c)
        WrongByteOpcode=@('Byte',1,0xbf); WrongByteRegister=@('Byte',2,0x8c); WrongByteSib=@('Byte',3,0x21)
        WrongByteBase=@('Byte',3,0x09); WrongByteDisp=@('Byte',4,0x01)
        WrongDwordOpcode=@('Dword',0,0x89); WrongDwordRegister=@('Dword',1,0x8c); WrongDwordSib=@('Dword',2,0x89)
        WrongDwordScale=@('Dword',2,0x41); WrongDwordBase=@('Dword',2,0x82); WrongDwordDisp=@('Dword',3,0x01)
        WrongAddOpcode=@('Add',0,0x49); WrongAddRegister=@('Add',2,0xc2); WrongJumpOpcode=@('Jump',0,0xfe); WrongJumpRegister=@('Jump',1,0xe1)
    }
    if($oneByteFaults.ContainsKey($Fault)) {
        $faultSpec=$oneByteFaults[$Fault]; $row=$rowSpecs | Where-Object Name -eq $faultSpec[0]
        $row.Bytes[$faultSpec[1]]=[byte]$faultSpec[2]
        # Fault is in PE bytes and in listing bytes, so scanner must reject the pattern itself.
        switch($row.Name) {
            GuardCmp { Put-Bytes $pe ($fileBase+$guardRva) $row.Bytes }
            GuardJa { Put-Bytes $pe ($fileBase+$guardRva+11) $row.Bytes }
            GuardIndex { Put-Bytes $pe ($fileBase+$guardRva+17) $row.Bytes }
            Base { Put-Bytes $pe ($fileBase+$dispatch) $row.Bytes }
            Byte { Put-Bytes $pe ($fileBase+$dispatch+7) $row.Bytes }
            Dword { Put-Bytes $pe ($fileBase+$dispatch+15) $row.Bytes }
            Add { Put-Bytes $pe ($fileBase+$dispatch+22) $row.Bytes }
            Jump { Put-Bytes $pe ($fileBase+$dispatch+25) $row.Bytes }
        }
    }
    if($Fault -match '^StackHigh([123])$') {
        $at=20+[int]$Matches[1]; $guard[$at]=$guard[$at]-bxor 1; Put-Bytes $pe ($fileBase+$guardRva) $guard; $rowSpecs[2].Bytes=[byte[]]$guard[17..23]
    }
    if($Fault -eq 'StackMismatch') { $guard[20]=[byte](($guard[20]+1)-band 0xff); Put-Bytes $pe ($fileBase+$guardRva) $guard; $rowSpecs[2].Bytes=[byte[]]$guard[17..23] }
    if($Fault -like 'BadMaximum*') {
        $bad = switch($Fault) { BadMaximumZero {[UInt32]0} BadMaximum4096 {[UInt32]4096} BadMaximumHighBit {[UInt32]2147483648} default {[UInt32]4294967295} }
        Put-Bytes $pe ($fileBase+$guardRva+7) ([BitConverter]::GetBytes($bad)); Put-Bytes $rowSpecs[0].Bytes 7 ([BitConverter]::GetBytes($bad))
    }
    if($Fault -eq 'BadByteLast') { $remap[$Maximum]=8; Put-Bytes $pe ($fileBase+$byte) $remap }
    if($Fault -eq 'BadByteMiddle') { $remap[[int]($Maximum/2)]=8; Put-Bytes $pe ($fileBase+$byte) $remap }
    if($Fault -eq 'TargetMissing') { Set-U32 $pe ($fileBase+$dword) ([UInt32]($target+0x20)) }
    if($Fault -eq 'TargetMiddle') { Set-U32 $pe ($fileBase+$dword+4) ([UInt32]($target+1)) }
    if($Fault -eq 'TargetUnknown') { Set-U32 $pe ($fileBase+$dword+8) ([UInt32]($target+0x30)) }
    if($Fault -eq 'TargetOutside') { Set-U32 $pe ($fileBase+$dword+12) ([UInt32]($procedureEnd+8)) }
    if($Fault -eq 'TargetInTable') { Set-U32 $pe ($fileBase+$dword+16) $dword }
    if($Fault -eq 'DefaultTargetMiddle') { Put-Bytes $pe ($fileBase+$guardRva+13) ([BitConverter]::GetBytes([Int32](($target+1)-($guardRva+17)))) }
    if($Fault -eq 'DefaultTargetOutside') { Put-Bytes $pe ($fileBase+$guardRva+13) ([BitConverter]::GetBytes([Int32](($procedureEnd+8)-($guardRva+17)))) }
    if($Fault -eq 'DefaultTargetInTable') { Put-Bytes $pe ($fileBase+$guardRva+13) ([BitConverter]::GetBytes([Int32]($dword-($guardRva+17)))) }
    if($Fault -eq 'DefaultTargetUnknown') { Put-Bytes $pe ($fileBase+$guardRva+13) ([BitConverter]::GetBytes([Int32](($target+0x30)-($guardRva+17)))) }
    if($Fault -like 'DefaultTarget*') { $rowSpecs[1].Bytes=[byte[]]$pe[($fileBase+$guardRva+11)..($fileBase+$guardRva+16)] }

    # Eight-row fault matrix: missing, unknown mnemonic, listing/PE mismatch and overlap.
    # Deliberately independently applies to all guard and dispatch rows, not just a sample.
    $listingRows=[Collections.Generic.List[string]]::new()
    foreach($row in $rowSpecs) {
        $bytes=[byte[]]$row.Bytes.Clone(); $text=$row.Text
        if($Fault -eq ('Missing'+$row.Name)) { continue }
        if($Fault -eq ('Unknown'+$row.Name)) { $text='<unknown>' }
        if($Fault -eq ('ListingMismatch'+$row.Name)) { $bytes[0]=[byte]($bytes[0]-bxor 1) }
        if($Fault -eq ('PeMismatch'+$row.Name)) {
            $at=switch($row.Name) { GuardCmp {$guardRva} GuardJa {$guardRva+11} GuardIndex {$guardRva+17} Base {$dispatch} Byte {$dispatch+7} Dword {$dispatch+15} Add {$dispatch+22} Jump {$dispatch+25} }
            $pe[$fileBase+$at]=[byte]($pe[$fileBase+$at]-bxor 1)
        }
        Add-ListingRow $listingRows ([UInt64]($ImageBase+$row.Rva)) $bytes $text
        if($Fault -eq ('Duplicate'+$row.Name)) { Add-ListingRow $listingRows ([UInt64]($ImageBase+$row.Rva)) $bytes $text }
        if($Fault -eq ('Overlap'+$row.Name)) { Add-ListingRow $listingRows ([UInt64]($ImageBase+$row.Rva+1)) ([byte[]]@(0x90)) 'nop' }
    }
    foreach($tr in $targetRvas) { Add-ListingRow $listingRows ([UInt64]($ImageBase+$tr)) ([byte[]]@(0x66,0x90)) 'nop' }
    if($Fault -in 'TargetUnknown','DefaultTargetUnknown') {
        $pe[$fileBase+$target+0x30]=0x90
        Add-ListingRow $listingRows ([UInt64]($ImageBase+$target+0x30)) ([byte[]]@(0x90)) '<unknown>'
    }
    Add-ListingRow $listingRows ([UInt64]($ImageBase+$procedureEnd)) ([byte[]]@(0x66,0x90)) 'nop'
    # Apparent decoded VEX rows over PE DWORD table data. These exact bytes must match PE;
    # the table proof may suppress these eight apparent vector instructions only.
    for($i=0;$i -lt 8;$i++) {
        [byte[]]$fake=[BitConverter]::GetBytes([UInt32]$target)
        if($Fault -eq "VectorTablePeMismatch$i") { $fake[1]=0x12 }
        if($Fault -eq "VectorTableListingMismatch$i") { $pe[$fileBase+$dword+4*$i]=0xc4 }
        Add-ListingRow $listingRows ([UInt64]($ImageBase+$dword+4*$i)) $fake 'vmovups xmm0, xmmword ptr [rax]'
    }
    if($Fault -in 'ActualVexOutside','ActualEvexOutside','ActualVexInBody','ActualEvexInBody') {
        $vbytes=if($Fault -in 'ActualVexOutside','ActualVexInBody'){[byte[]]@(0xc5,0xf8,0x10,0x00)}else{[byte[]]@(0x62,0xf1,0x7c,0x48,0x10,0x00)}
        Add-ListingRow $listingRows ([UInt64]($ImageBase+$vectorRva)) $vbytes $vectorText
    }

    # Symbol/owner boundary controls. They are separate from PE/listing controls so each
    # failure demonstrates the PDB proof boundary. Interior and later symbols must not
    # authorize a table whose true owning procedure ends earlier.
    if($Fault -eq 'MissingProcedure') { $symbolText=$module+"  0 | S_LDATA32 [size = 16] ````
    type = <no type>, addr = 0001:$dwordOffset
" }
    if($Fault -eq 'WrongProcedureStart') { $symbolText=$symbolText.Replace("addr = 0001:$procedureStartOffset",("addr = 0001:"+($procedureStartOffset+1))) }
    if($Fault -eq 'WrongProcedureEnd') { $symbolText=$symbolText.Replace(('code size = '+($procedureEnd-$procedureStart)),('code size = '+($procedureEnd-$procedureStart-1))) }
    if($Fault -eq 'WrongProcedureOwner') { $symbolText=$symbolText.Replace("addr = 0001:$procedureStartOffset","addr = 0002:$procedureStartOffset") }
    if($Fault -eq 'MissingDwordSymbol') { $symbolText=$symbolText.Replace("  0 | S_LDATA32 [size = 16] ````
    type = <no type>, addr = 0001:$dwordOffset
",'') }
    if($Fault -eq 'MissingByteSymbol') { $symbolText=$symbolText.Replace("  0 | S_LDATA32 [size = 16] ````
    type = <no type>, addr = 0001:$byteOffset
",'') }
    if($Fault -eq 'WrongDwordSymbol') { $symbolText=$symbolText.Replace("addr = 0001:$dwordOffset",("addr = 0001:"+($dwordOffset+1))) }
    if($Fault -eq 'WrongByteSymbol') { $symbolText=$symbolText.Replace("addr = 0001:$byteOffset",("addr = 0001:"+($byteOffset+1))) }
    if($Fault -eq 'InteriorDwordSymbol') { $symbolText += "  0 | S_LDATA32 [size = 16] ````
    type = <no type>, addr = 0001:$($dwordOffset+4)
" }
    if($Fault -eq 'InteriorByteSymbol') { $symbolText += "  0 | S_LDATA32 [size = 16] ````
    type = <no type>, addr = 0001:$($byteOffset+($Maximum/2))
" }
    if($Fault -eq 'LaterSymbolRescue') { $symbolText=$symbolText.Replace(('code size = '+($procedureEnd-$procedureStart)),('code size = '+($byte-$procedureStart))) }
    if($Fault -eq 'WrongContribution') { $metadataText=$metadataText.Replace('0001:0, size = 20480','0002:0, size = 20480') }
    if($Fault -eq 'WrongOwnerContribution') { $symbolText=$symbolText.Replace("addr = 0001:$procedureStartOffset",("addr = 0002:$procedureStartOffset")) }
    if($Fault -eq 'LaterProcedureOverlaps') { $symbolText=$symbolText.Replace("addr = 0001:$procedureEndOffset, code size = 2",("addr = 0001:"+($byteOffset+1)+", code size = 2")) }
    if($Fault -eq 'MissingContribution') { $metadataText=$module+"  Age: $script:Age
  GUID: {$script:Guid}
Section Contributions
" }
    if($Fault -eq 'BadTableOwner') { $symbolText=$symbolText.Replace("addr = 0001:$byteOffset",("addr = 0002:$byteOffset")) }

    # Byte-table integrity faults at middle/last preserve lengths and symbol starts.
    if($Fault -in 'BadByteLast','BadByteMiddle') {
        $listingRows.Add(('{0:x}: {1} db {2}' -f ($ImageBase+$byte),(Hex-Bytes $remap),'remap bytes'))
    }
    if($Fault -eq 'TableRangeOverlap') { $symbolText += "  0 | S_LDATA32 [size = 16] ````
    type = <no type>, addr = 0001:$($dwordOffset+16)
" }
    if($Fault -eq 'ByteTableNotProcedureEnd') { $symbolText=$symbolText.Replace(('code size = '+($procedureEnd-$procedureStart)),('code size = '+($procedureEnd-$procedureStart+1))) }

    $rows=[Collections.Generic.List[string]]::new(); foreach($line in $listingRows){$rows.Add($line)}
    # Individual valid byte values are listed as data, keeping address/length explicit.
    if($Fault -notin 'BadByteLast','BadByteMiddle') { $rows.Add(('{0:x}: {1} db remap' -f ($ImageBase+$byte),(Hex-Bytes $remap))) }
    Set-Content -LiteralPath $metadata -Value $metadataText -Encoding utf8
    Set-Content -LiteralPath $symbols -Value $symbolText -Encoding utf8
    Set-Content -LiteralPath $disassembly -Value (@($rows | Sort-Object { [UInt64]::Parse(($_ -split ':',2)[0],[Globalization.NumberStyles]::HexNumber) }) -join "
") -Encoding utf8
    [IO.File]::WriteAllBytes($binary,$pe)
    return @{Name=$Name;Binary=$binary;Metadata=$metadata;Symbols=$symbols;Disassembly=$disassembly;Maximum=$Maximum;Fault=$Fault}
}

function Assert-WideControl($Case,[bool]$ExpectedPass,[bool]$VectorViolation=$false) {
    try {
        $result=[RedPrismBuild.IsaScanner]::Scan($Case.Binary,$Case.Metadata,$Case.Symbols,$Case.Disassembly,[string[]]@(),[string[]]@(),[string[]]@())
    } catch [IO.InvalidDataException] {
        if ($ExpectedPass -or $Case.Fault -notin 'WrongContribution','MissingContribution') { throw }
        ++$script:CaseCount; Write-Host "PASS isa-wide-rcx-byte-remap.$($Case.Name) metadata-refused"
        return
    }
    if ($Case.Fault -in 'WrongContribution','MissingContribution') { throw "$($Case.Name): malformed metadata must be refused before instruction scanning." }
    if($ExpectedPass -and ($result.Violations.Count -ne 0 -or $result.InlineTableVectorData -ne 8)) {
        throw "$($Case.Name): expected eight proven DWORD target rows and no violations; got $($result.InlineTableVectorData), $($result.Violations -join '; ')"
    }
    if(-not $ExpectedPass -and -not $VectorViolation -and $result.Violations.Count -eq 0) { throw "$($Case.Name): malformed control bypassed the ISA gate." }
    if($VectorViolation -and ($result.InlineTableVectorData -ne 8 -or $result.Violations.Count -ne 1 -or $result.EvexInstructions -ne [int]($Case.Fault -like 'ActualEvex*'))) {
        throw "$($Case.Name): only the real vector outside the tables should remain gated; eight table DWORD rows stay proven data."
    }
    ++$script:CaseCount; Write-Host "PASS isa-wide-rcx-byte-remap.$($Case.Name)"
}

# Positive range and relocation controls: requested observed maximum 226, lower boundary 1,
# upper boundary 4095, varied stack disp32 including sign-bit values, and shifted image/section.
foreach($valid in @(
    @{Name='Observed226Stack0c98';Max=226;Stack=[UInt32]0x0c98;Base=[UInt64]0x140000000;Section=[UInt32]0x1000},
    @{Name='Minimum1Shifted';Max=1;Stack=[UInt32]0x20;Base=[UInt64]0x180000000;Section=[UInt32]0x2000},
    @{Name='Maximum4095StackHigh';Max=4095;Stack=[UInt32]2147488308;Base=[UInt64]0x150000000;Section=[UInt32]0x3000},
    @{Name='InteriorMaximum127';Max=127;Stack=[UInt32]0x7fffffff;Base=[UInt64]0x160000000;Section=[UInt32]0x4000}
)) { Assert-WideControl (New-WideControl $valid.Name '' $valid.Max $valid.Base $valid.Section $valid.Stack) $true }

# Unsigned guard bound controls: zero, first out-of-contract, high-bit, and all-bits-set.
foreach($fault in @('BadMaximumZero','BadMaximum4096','BadMaximumHighBit','BadMaximumAllBits')) { Assert-WideControl (New-WideControl $fault $fault) $false }

# Exhaustive 8-row controls. Each row must be present, recognized, byte-identical to PE,
# and nonoverlapping in listing address space. Keep these generated cases fixed and visible.
$rows=@('GuardCmp','GuardJa','GuardIndex','Base','Byte','Dword','Add','Jump')
foreach($row in $rows) { foreach($kind in @('Missing','Unknown','ListingMismatch','PeMismatch','Overlap','Duplicate')) {
    $fault=$kind+$row; Assert-WideControl (New-WideControl $fault $fault) $false
} }

# Encoding-field controls cover opcode, register, SIB, scale/base, displacement, branch form,
# and all four bytes of unequal stack disp32. Every control keeps PE and listing coupled unless
# its name explicitly tests disagreement, so rejection is attributable to semantic decoding.
$encodingFaults=@('WrongGuardOpcode','WrongGuardRegister','WrongGuardSib','WrongGuardDisp','WrongJaOpcode',
    'WrongIndexOpcode','WrongIndexRegister','WrongIndexSib','WrongIndexDisp','StackMismatch','StackHigh1','StackHigh2','StackHigh3',
    'WrongLeaOpcode','WrongLeaRegister','WrongLeaBase','WrongByteOpcode','WrongByteRegister','WrongByteSib','WrongByteBase','WrongByteDisp',
    'WrongDwordOpcode','WrongDwordRegister','WrongDwordSib','WrongDwordScale','WrongDwordBase','WrongDwordDisp',
    'WrongAddOpcode','WrongAddRegister','WrongJumpOpcode','WrongJumpRegister')
foreach($fault in $encodingFaults) { Assert-WideControl (New-WideControl $fault $fault) $false }

# PDB and contribution ownership: missing/wrong table symbols, wrong owner/start/end/contribution,
# interior symbols, and a short procedure followed by later symbols/procedures must all fail.
$ownershipFaults=@('MissingProcedure','MissingDwordSymbol','MissingByteSymbol','WrongDwordSymbol','WrongByteSymbol',
    'InteriorDwordSymbol','InteriorByteSymbol','WrongProcedureStart','WrongProcedureEnd','WrongProcedureOwner',
    'WrongContribution','WrongOwnerContribution','MissingContribution','BadTableOwner','LaterSymbolRescue',
    'LaterProcedureOverlaps','TableRangeOverlap','ByteTableNotProcedureEnd')
foreach($fault in $ownershipFaults) { Assert-WideControl (New-WideControl $fault $fault) $false }

# Table contents and control-flow targets. Targets must be decoded instruction starts strictly
# within the owner and before the DWORD table; the JA default must satisfy the same boundary rule.
foreach($fault in @('BadByteLast','BadByteMiddle','TargetMissing','TargetMiddle','TargetUnknown','TargetOutside','TargetInTable',
        'DefaultTargetMiddle','DefaultTargetUnknown','DefaultTargetOutside','DefaultTargetInTable')) { Assert-WideControl (New-WideControl $fault $fault) $false }

# Apparent vector decode from the table must match PE/listing at each of eight rows; real vectors
# after the owner remain violations and must not be swallowed by the table's proven extent.
for($i=0;$i -lt 8;$i++) {
    Assert-WideControl (New-WideControl "VectorTablePeMismatch$i" "VectorTablePeMismatch$i") $false
    Assert-WideControl (New-WideControl "VectorTableListingMismatch$i" "VectorTableListingMismatch$i") $false
}
foreach($fault in @('ActualVexOutside','ActualEvexOutside','ActualVexInBody','ActualEvexInBody')) { Assert-WideControl (New-WideControl $fault $fault) $false $true }

Write-Host "PASS isa-wide-rcx-byte-remap.summary cases=$script:CaseCount positive=4 unsigned-bound-controls=4 row-matrix=8x6 encoding=$($encodingFaults.Count) ownership=$($ownershipFaults.Count) table-target-controls=11 vector-row-controls=16 actual-vector-controls=4"
