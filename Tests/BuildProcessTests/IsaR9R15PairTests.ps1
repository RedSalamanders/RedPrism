# Dispatch-form controls isolate the recognizers; the default image gate retains PDB-marked proof in IsaMarkedTableTests.ps1.
[CmdletBinding()]
param([string]$RepositoryRoot = '')
if (-not $RepositoryRoot) { $RepositoryRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot) }
$CandidatePath = Join-Path $RepositoryRoot 'Build/IsaScan.cs'
$OutputRoot = Join-Path $RepositoryRoot ('.build/BuildProcessTests/IsaR9R15Pair-' + [guid]::NewGuid().ToString('N'))
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Standalone private parser fixture. It writes PE/PDB-text/disassembly inputs, then invokes only the
# candidate C# parser. It never starts, loads, or executes a generated PE image.
$script:ImageBase = [UInt64]0x140000000
$script:SectionRva = [UInt32]0x0410f000
$script:RawBase = 0x400
$script:RawSize = 0x3000
$script:P = [UInt32]($script:SectionRva + 0x807)
$script:ProcedureBegin = [UInt32]($script:P - 8)
$script:AfterPair = [UInt32]($script:P + 91)
$script:Fallback = [UInt32]($script:P + 0xbe)
$script:Table1 = [UInt32](([UInt64]($script:P + 0x200) + 3) -band 0xfffffffc)
$script:Map1 = [UInt32]($script:Table1 + 8)
$script:Table2 = [UInt32]($script:Map1 + 28)
$script:Map2 = [UInt32]($script:Table2 + 8)
$script:PaddingEnd = [UInt32]($script:Map2 + 24)
$script:ContributionEnd = [UInt32]($script:PaddingEnd + 1)
$script:Guid = [guid]'9c1e2d4b-6a70-4a8b-9012-3456789abcde'
$script:Age = [UInt32]7
$script:ExpectedLines = @'
Label	ExpectedDisposition	Mutation
isa-r9-r15-pair.accept.baseline	ACCEPT	accept.baseline
isa-r9-r15-pair.accept.relocated-rva-vex	ACCEPT	accept.relocated-rva-vex
isa-r9-r15-pair.accept.relocated-rva-evex	ACCEPT	accept.relocated-rva-evex
isa-r9-r15-pair.accept.terminal-e9-nop-boundary	ACCEPT	accept.terminal-e9-nop-boundary
isa-r9-r15-pair.accept.vector-row-crosses-table1-into-map1	ACCEPT	accept.vector-row-crosses-table1-into-map1
isa-r9-r15-pair.accept.vector-row-crosses-table2-into-map2	ACCEPT	accept.vector-row-crosses-table2-into-map2
isa-r9-r15-pair.reject.wrong-producer	REJECT	reject.wrong-producer
isa-r9-r15-pair.reject.wrong-imagebase	REJECT	reject.wrong-imagebase
isa-r9-r15-pair.reject.signed-ja	REJECT	reject.signed-ja
isa-r9-r15-pair.reject.wrong-cmp-immediate	REJECT	reject.wrong-cmp-immediate
isa-r9-r15-pair.reject.wrong-cmp-register	REJECT	reject.wrong-cmp-register
isa-r9-r15-pair.reject.wrong-cmp-opcode	REJECT	reject.wrong-cmp-opcode
isa-r9-r15-pair.reject.wrong-ja-opcode	REJECT	reject.wrong-ja-opcode
isa-r9-r15-pair.reject.wrong-je-opcode	REJECT	reject.wrong-je-opcode
isa-r9-r15-pair.reject.wrong-lea-opcode	REJECT	reject.wrong-lea-opcode
isa-r9-r15-pair.reject.wrong-lea-register	REJECT	reject.wrong-lea-register
isa-r9-r15-pair.reject.wrong-lea-disp	REJECT	reject.wrong-lea-disp
isa-r9-r15-pair.reject.wrong-case-cmp-opcode	REJECT	reject.wrong-case-cmp-opcode
isa-r9-r15-pair.reject.wrong-case-cmp-immediate	REJECT	reject.wrong-case-cmp-immediate
isa-r9-r15-pair.reject.wrong-case-cmp-register	REJECT	reject.wrong-case-cmp-register
isa-r9-r15-pair.reject.wrong-default-ja	REJECT	reject.wrong-default-ja
isa-r9-r15-pair.reject.wrong-map-load-opcode	REJECT	reject.wrong-map-load-opcode
isa-r9-r15-pair.reject.wrong-map-register	REJECT	reject.wrong-map-register
isa-r9-r15-pair.reject.wrong-map-base	REJECT	reject.wrong-map-base
isa-r9-r15-pair.reject.wrong-map-index	REJECT	reject.wrong-map-index
isa-r9-r15-pair.reject.wrong-map-displacement	REJECT	reject.wrong-map-displacement
isa-r9-r15-pair.reject.wrong-table-load-opcode	REJECT	reject.wrong-table-load-opcode
isa-r9-r15-pair.reject.wrong-table-register	REJECT	reject.wrong-table-register
isa-r9-r15-pair.reject.wrong-table-base	REJECT	reject.wrong-table-base
isa-r9-r15-pair.reject.wrong-table-index	REJECT	reject.wrong-table-index
isa-r9-r15-pair.reject.wrong-table-scale	REJECT	reject.wrong-table-scale
isa-r9-r15-pair.reject.wrong-table-displacement	REJECT	reject.wrong-table-displacement
isa-r9-r15-pair.reject.wrong-add-opcode	REJECT	reject.wrong-add-opcode
isa-r9-r15-pair.reject.wrong-add-register	REJECT	reject.wrong-add-register
isa-r9-r15-pair.reject.wrong-jump-opcode	REJECT	reject.wrong-jump-opcode
isa-r9-r15-pair.reject.wrong-jump-register	REJECT	reject.wrong-jump-register
isa-r9-r15-pair.reject.gap-in-chain	REJECT	reject.gap-in-chain
isa-r9-r15-pair.reject.map1-missing	REJECT	reject.map1-missing
isa-r9-r15-pair.reject.map2-missing	REJECT	reject.map2-missing
isa-r9-r15-pair.reject.table1-missing	REJECT	reject.table1-missing
isa-r9-r15-pair.reject.table2-missing	REJECT	reject.table2-missing
isa-r9-r15-pair.reject.data-wrong-owner	REJECT	reject.data-wrong-owner
isa-r9-r15-pair.reject.procedure-wrong-owner	REJECT	reject.procedure-wrong-owner
isa-r9-r15-pair.reject.procedure-missing	REJECT	reject.procedure-missing
isa-r9-r15-pair.reject.procedure-start	REJECT	reject.procedure-start
isa-r9-r15-pair.reject.procedure-short	REJECT	reject.procedure-short
isa-r9-r15-pair.reject.procedure-long	REJECT	reject.procedure-long
isa-r9-r15-pair.reject.contribution-short	REJECT	reject.contribution-short
isa-r9-r15-pair.reject.data-named	REJECT	reject.data-named
isa-r9-r15-pair.reject.data-typed	REJECT	reject.data-typed
isa-r9-r15-pair.reject.data-start	REJECT	reject.data-start
isa-r9-r15-pair.reject.data-extent	REJECT	reject.data-extent
isa-r9-r15-pair.reject.map1-extent	REJECT	reject.map1-extent
isa-r9-r15-pair.reject.map2-extent	REJECT	reject.map2-extent
isa-r9-r15-pair.reject.table1-extent	REJECT	reject.table1-extent
isa-r9-r15-pair.reject.table2-extent	REJECT	reject.table2-extent
isa-r9-r15-pair.reject.wrong-adjacency-map1	REJECT	reject.wrong-adjacency-map1
isa-r9-r15-pair.reject.wrong-adjacency-table2	REJECT	reject.wrong-adjacency-table2
isa-r9-r15-pair.reject.wrong-adjacency-map2	REJECT	reject.wrong-adjacency-map2
isa-r9-r15-pair.reject.wrong-padding-byte	REJECT	reject.wrong-padding-byte
isa-r9-r15-pair.reject.wrong-padding-length	REJECT	reject.wrong-padding-length
isa-r9-r15-pair.reject.map1-value-out-of-range	REJECT	reject.map1-value-out-of-range
isa-r9-r15-pair.reject.map2-value-out-of-range	REJECT	reject.map2-value-out-of-range
isa-r9-r15-pair.reject.map1-case-out-of-range	REJECT	reject.map1-case-out-of-range
isa-r9-r15-pair.reject.map2-case-out-of-range	REJECT	reject.map2-case-out-of-range
isa-r9-r15-pair.reject.table1-target-interior	REJECT	reject.table1-target-interior
isa-r9-r15-pair.reject.table1-target-producer	REJECT	reject.table1-target-producer
isa-r9-r15-pair.reject.table1-target-data	REJECT	reject.table1-target-data
isa-r9-r15-pair.reject.table1-target-padding	REJECT	reject.table1-target-padding
isa-r9-r15-pair.reject.table1-target-outside	REJECT	reject.table1-target-outside
isa-r9-r15-pair.reject.table2-target-interior	REJECT	reject.table2-target-interior
isa-r9-r15-pair.reject.table2-target-producer	REJECT	reject.table2-target-producer
isa-r9-r15-pair.reject.table2-target-data	REJECT	reject.table2-target-data
isa-r9-r15-pair.reject.table2-target-padding	REJECT	reject.table2-target-padding
isa-r9-r15-pair.reject.table2-target-outside	REJECT	reject.table2-target-outside
isa-r9-r15-pair.reject.default-interior	REJECT	reject.default-interior
isa-r9-r15-pair.reject.default-data	REJECT	reject.default-data
isa-r9-r15-pair.reject.default-padding	REJECT	reject.default-padding
isa-r9-r15-pair.reject.default-outside	REJECT	reject.default-outside
isa-r9-r15-pair.reject.incoming-branch-interior	REJECT	reject.incoming-branch-interior
isa-r9-r15-pair.reject.incoming-call-interior	REJECT	reject.incoming-call-interior
isa-r9-r15-pair.reject.incoming-branch-data	REJECT	reject.incoming-branch-data
isa-r9-r15-pair.reject.incoming-call-data	REJECT	reject.incoming-call-data
isa-r9-r15-pair.reject.prefixed-jrcxz	REJECT	reject.prefixed-jrcxz
isa-r9-r15-pair.reject.loop-family	REJECT	reject.loop-family
isa-r9-r15-pair.reject.pe-listing-mismatch	REJECT	reject.pe-listing-mismatch
isa-r9-r15-pair.reject.pe-row-missing	REJECT	reject.pe-row-missing
isa-r9-r15-pair.reject.pe-row-overlap	REJECT	reject.pe-row-overlap
isa-r9-r15-pair.reject.listing-row-missing	REJECT	reject.listing-row-missing
isa-r9-r15-pair.reject.listing-row-overlap	REJECT	reject.listing-row-overlap
isa-r9-r15-pair.reject.listing-row-unknown	REJECT	reject.listing-row-unknown
isa-r9-r15-pair.reject.real-vex-before	REJECT	reject.real-vex-before
isa-r9-r15-pair.reject.real-vex-after	REJECT	reject.real-vex-after
isa-r9-r15-pair.reject.real-vex-adjacent	REJECT	reject.real-vex-adjacent
isa-r9-r15-pair.reject.real-evex-adjacent	REJECT	reject.real-evex-adjacent
isa-r9-r15-pair.reject.real-vector-crosses-target-map	REJECT	reject.real-vector-crosses-target-map
isa-r9-r15-pair.reject.terminal-wrong-opcode	REJECT	reject.terminal-wrong-opcode
isa-r9-r15-pair.reject.terminal-wrong-target	REJECT	reject.terminal-wrong-target
isa-r9-r15-pair.reject.terminal-target-producer-interior	REJECT	reject.terminal-target-producer-interior
isa-r9-r15-pair.reject.terminal-target-data	REJECT	reject.terminal-target-data
isa-r9-r15-pair.reject.terminal-target-padding	REJECT	reject.terminal-target-padding
isa-r9-r15-pair.reject.terminal-missing	REJECT	reject.terminal-missing
isa-r9-r15-pair.reject.terminal-listing-mismatch	REJECT	reject.terminal-listing-mismatch
isa-r9-r15-pair.reject.terminal-overlap	REJECT	reject.terminal-overlap
isa-r9-r15-pair.reject.code-row-gap-before-table	REJECT	reject.code-row-gap-before-table
isa-r9-r15-pair.reject.code-row-unknown-before-table	REJECT	reject.code-row-unknown-before-table
isa-r9-r15-pair.reject.code-row-overlap-before-table	REJECT	reject.code-row-overlap-before-table
isa-r9-r15-pair.reject.incoming-branch-producer-interior	REJECT	reject.incoming-branch-producer-interior
isa-r9-r15-pair.reject.incoming-call-producer-interior	REJECT	reject.incoming-call-producer-interior
isa-r9-r15-pair.reject.incoming-loop-pair-data	REJECT	reject.incoming-loop-pair-data
isa-r9-r15-pair.reject.incoming-prefixed-jrcxz-producer	REJECT	reject.incoming-prefixed-jrcxz-producer
isa-r9-r15-pair.reject.incoming-prefixed-jrcxz-data	REJECT	reject.incoming-prefixed-jrcxz-data
isa-r9-r15-pair.reject.unaligned-tables	REJECT	reject.unaligned-tables
'@ -split '\r?\n'

if (-not (Test-Path -LiteralPath $CandidatePath -PathType Leaf)) { throw "CandidatePath not found: $CandidatePath" }
if (-not ('RedPrismBuild.IsaScanner' -as [type])) { Add-Type -Path (Resolve-Path -LiteralPath $CandidatePath).Path }
[void](New-Item -ItemType Directory -Path $OutputRoot -Force)

function Set-U16([byte[]]$Bytes,[int]$Offset,[UInt16]$Value) { [Array]::Copy([BitConverter]::GetBytes($Value),0,$Bytes,$Offset,2) }
function Set-U32([byte[]]$Bytes,[int]$Offset,[UInt32]$Value) { [Array]::Copy([BitConverter]::GetBytes($Value),0,$Bytes,$Offset,4) }
function Set-U64([byte[]]$Bytes,[int]$Offset,[UInt64]$Value) { [Array]::Copy([BitConverter]::GetBytes($Value),0,$Bytes,$Offset,8) }
function Put-Bytes([byte[]]$Bytes,[int]$Offset,[byte[]]$Value) { [Array]::Copy($Value,0,$Bytes,$Offset,$Value.Length) }
function Hex-Bytes([byte[]]$Bytes) { (($Bytes | ForEach-Object { $_.ToString('x2') }) -join ' ') }
function Raw-At([uint32]$Rva) {
    if ($Rva -lt $script:SectionRva -or [uint64]$Rva -ge [uint64]$script:SectionRva + $script:RawSize) { throw "Unmapped fixture RVA $Rva" }
    return [int]($script:RawBase + $Rva - $script:SectionRva)
}
function Write-Rva([byte[]]$Pe,[uint32]$Rva,[byte[]]$Bytes) { Put-Bytes $Pe (Raw-At $Rva) $Bytes }
function New-Row([uint32]$Rva,[byte[]]$Bytes,[string]$Text) { [pscustomobject]@{ Rva=$Rva; Bytes=$Bytes; Text=$Text } }
function Add-Row($Case,[uint32]$Rva,[byte[]]$Bytes,[string]$Text) {
    Write-Rva $Case.Pe $Rva $Bytes
    [void]$Case.Rows.Add((New-Row $Rva $Bytes $Text))
}
function Set-RowBytes($Case,[uint32]$Rva,[byte[]]$Bytes,[string]$Text) {
    for($i=$Case.Rows.Count-1;$i -ge 0;--$i){if($Case.Rows[$i].Rva -ge $Rva -and $Case.Rows[$i].Rva -lt $Rva+$Bytes.Length){$Case.Rows.RemoveAt($i)}}
    Add-Row $Case $Rva $Bytes $Text
}
function Patch-Byte($Case,[uint32]$Rva,[byte]$Value) { $Case.Pe[(Raw-At $Rva)]=$Value }
function Patch-U32($Case,[uint32]$Rva,[uint32]$Value) { Set-U32 $Case.Pe (Raw-At $Rva) $Value }
function Set-Rel32([byte[]]$Pe,[uint32]$Rva,[uint32]$InstructionEnd,[uint32]$Target) {
    $delta=[Int32]([Int64]$Target-[Int64]$InstructionEnd);[Array]::Copy([BitConverter]::GetBytes($delta),0,$Pe,(Raw-At $Rva),4)
}
function New-PE([uint32]$SectionRva) {
    $script:SectionRva=$SectionRva
    $pe=[byte[]]::new($script:RawBase+$script:RawSize)
    $pe[0]=0x4d; $pe[1]=0x5a
    Set-U32 $pe 0x3c 0x80
    Set-U32 $pe 0x80 0x00004550
    Set-U16 $pe 0x84 0x8664
    Set-U16 $pe 0x86 1
    Set-U16 $pe 0x94 0xf0
    Set-U16 $pe 0x98 0x20b
    Set-U64 $pe (0x98+24) $script:ImageBase
    Set-U32 $pe (0x98+56) ($SectionRva+$script:RawSize)
    Set-U32 $pe (0x98+60) 0x400
    Set-U32 $pe (0x98+112+6*8) ($SectionRva+0x180)
    Set-U32 $pe (0x98+116+6*8) 28
    $sh=0x80+24+0xf0
    Put-Bytes $pe $sh ([Text.Encoding]::ASCII.GetBytes('.text'+[char]0+[char]0+[char]0))
    Set-U32 $pe ($sh+8) $script:RawSize
    Set-U32 $pe ($sh+12) $SectionRva
    Set-U32 $pe ($sh+16) $script:RawSize
    Set-U32 $pe ($sh+20) $script:RawBase
    Set-U32 $pe ($sh+36) 0x60000020
    $db=$script:RawBase+0x180
    Set-U32 $pe ($db+12) 2
    Set-U32 $pe ($db+16) 24
    Set-U32 $pe ($db+20) ($SectionRva+0x1a0)
    Set-U32 $pe ($db+24) 0x5a0
    Put-Bytes $pe 0x5a0 ([Text.Encoding]::ASCII.GetBytes('RSDS'))
    Put-Bytes $pe 0x5a4 $script:Guid.ToByteArray()
    Set-U32 $pe 0x5b4 $script:Age
    return ,$pe
}
function New-BaseCase([string]$Name,[string]$Mutation) {
    $section=if($Mutation -eq 'accept.relocated-rva-vex'){[UInt32]0x0510f000}elseif($Mutation -eq 'accept.relocated-rva-evex'){[UInt32]0x0610f000}else{[UInt32]0x0410f000}
    $script:SectionRva=$section; $script:P=[UInt32]($section+0x807); $script:ProcedureBegin=[UInt32]($script:P-8)
    $script:AfterPair=[UInt32]($script:P+91); $script:Fallback=[UInt32]($script:P+0xbe)
    $script:Table1=[UInt32](([UInt64]($script:P+0x200)+3) -band 0xfffffffc)
    if ($Mutation -eq 'reject.unaligned-tables') { ++$script:Table1 }
    $script:Map1=[UInt32]($script:Table1+8)
    $script:Table2=[UInt32]($script:Map1+28); $script:Map2=[UInt32]($script:Table2+8)
    $script:PaddingEnd=[UInt32]($script:Map2+24); $script:ContributionEnd=[UInt32]($script:PaddingEnd+1)
    $pe=New-PE $section
    $case=@{Name=$Name;Mutation=$Mutation;Pe=$pe;Rows=[Collections.Generic.List[object]]::new();Symbols=@();ProcedureBegin=$script:ProcedureBegin;ProcedureEnd=$script:ContributionEnd;ContributionEnd=$script:ContributionEnd;Owner=0;Module=0;MissingProcedure=$false}
    for([uint32]$at=$script:ProcedureBegin;$at -lt $script:P;++$at){Add-Row $case $at ([byte[]]@(0x90)) 'nop'}
    $leaDisp=[Int32](-[Int64]($script:P+7))
    $branch1Default=[Int32]([Int64]$script:Fallback-[Int64]($script:P+31))
    $branch2Default=[Int32]([Int64]$script:Fallback-[Int64]($script:P+69))
    $chain=[byte[]]::new(91)
    $parts=@(
        @{O=0;B=[byte[]]@(0x4c,0x8d,0x0d,0,0,0,0);T='lea r9, [rip+imagebase]'},
        @{O=7;B=[byte[]]@(0x41,0x81,0xff,0x00,0x02,0x00,0x00);T='cmp r15d, 0x200'},
        @{O=14;B=[byte[]]@(0x77,0x25);T='ja second-chain'},@{O=16;B=[byte[]]@(0x74,0x49);T='je pair-end'},
        @{O=18;B=[byte[]]@(0x41,0x8d,0x47,0xfa);T='lea eax, [r15-6]'},@{O=22;B=[byte[]]@(0x83,0xf8,0x1b);T='cmp eax, 0x1b'},
        @{O=25;B=[byte[]]@(0x0f,0x87,0,0,0,0);T='ja default'},
        @{O=31;B=[byte[]]@(0x41,0x0f,0xb6,0x84,0x01,0,0,0,0);T='movzx eax, byte ptr [r9+rax+map1]'},
        @{O=40;B=[byte[]]@(0x41,0x8b,0x8c,0x81,0,0,0,0);T='mov ecx, dword ptr [r9+4*rax+table1]'},
        @{O=48;B=[byte[]]@(0x49,0x03,0xc9);T='add rcx, r9'},@{O=51;B=[byte[]]@(0xff,0xe1);T='jmp rcx'},
        @{O=53;B=[byte[]]@(0x41,0x8d,0x87,0xff,0xfd,0xff,0xff);T='lea eax, [r15-0x201]'},
        @{O=60;B=[byte[]]@(0x83,0xf8,0x14);T='cmp eax, 0x14'},@{O=63;B=[byte[]]@(0x0f,0x87,0,0,0,0);T='ja default'},
        @{O=69;B=[byte[]]@(0x41,0x0f,0xb6,0x84,0x01,0,0,0,0);T='movzx eax, byte ptr [r9+rax+map2]'},
        @{O=78;B=[byte[]]@(0x41,0x8b,0x8c,0x81,0,0,0,0);T='mov ecx, dword ptr [r9+4*rax+table2]'},
        @{O=86;B=[byte[]]@(0x49,0x03,0xc9);T='add rcx, r9'},@{O=89;B=[byte[]]@(0xff,0xe1);T='jmp rcx'}
    )
    foreach($x in $parts){[Array]::Copy($x.B,0,$chain,$x.O,$x.B.Length)}
    [Array]::Copy([BitConverter]::GetBytes([Int32]$leaDisp),0,$chain,3,4)
    [Array]::Copy([BitConverter]::GetBytes([Int32]$branch1Default),0,$chain,27,4)
    Set-U32 $chain 36 $script:Map1
    Set-U32 $chain 44 $script:Table1
    [Array]::Copy([BitConverter]::GetBytes([Int32]$branch2Default),0,$chain,65,4)
    Set-U32 $chain 74 $script:Map2
    Set-U32 $chain 82 $script:Table2
    foreach($x in $parts){$b=[byte[]]::new($x.B.Length);[Array]::Copy($chain,$x.O,$b,0,$b.Length);Add-Row $case ([UInt32]($script:P+$x.O)) $b $x.T}
    for([uint32]$at=$script:AfterPair;$at -lt $script:Table1-8;++$at){Add-Row $case $at ([byte[]]@(0x90)) 'nop'}
    $termAt=[UInt32]($script:Table1-8);$termTarget=if($Mutation -eq 'accept.terminal-e9-nop-boundary'){[UInt32]($script:P+0x100)}else{$script:AfterPair}
    $term=[byte[]]@(0xe9,0,0,0,0);[Array]::Copy([BitConverter]::GetBytes([Int32]([Int64]$termTarget-[Int64]($termAt+5))),0,$term,1,4)
    Add-Row $case $termAt $term ('jmp 0x{0:x}' -f ($script:ImageBase+$termTarget))
    Add-Row $case ([UInt32]($script:Table1-3)) ([byte[]]@(0x0f,0x1f,0x00)) 'nop dword ptr [rax]'
    $target1=[UInt32]$script:AfterPair;$targetDefault=[UInt32]$script:Fallback
    Write-Rva $case.Pe $script:Table1 ([BitConverter]::GetBytes($target1));Write-Rva $case.Pe ($script:Table1+4) ([BitConverter]::GetBytes($targetDefault))
    Write-Rva $case.Pe $script:Table2 ([BitConverter]::GetBytes($target1));Write-Rva $case.Pe ($script:Table2+4) ([BitConverter]::GetBytes($targetDefault))
    $map1=[byte[]]@(0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1)
    $map2=[byte[]]@(0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0)
    Write-Rva $case.Pe $script:Map1 $map1;Write-Rva $case.Pe $script:Map2 $map2
    $r1=Raw-At $script:Table1;$first1=[byte[]]$case.Pe[$r1..($r1+3)];Add-Row $case $script:Table1 $first1 'data dword'
$cross1=$Mutation -ne 'accept.vector-row-crosses-table2-into-map2'
    if($cross1){$v1raw=Raw-At ($script:Table1+4);$vec1=[byte[]]$case.Pe[$v1raw..($v1raw+5)];Add-Row $case ($script:Table1+4) $vec1 'vmovups xmm0, xmmword ptr [rax+rax+1]';$map1Start=2}else{$v1=[byte[]]$case.Pe[(Raw-At ($script:Table1+4))..(Raw-At ($script:Table1+7))];Add-Row $case ($script:Table1+4) $v1 'data dword';$map1Start=0}
    $r2=Raw-At $script:Table2;$first2=[byte[]]$case.Pe[$r2..($r2+3)];Add-Row $case $script:Table2 $first2 'data dword'
$cross2=$Mutation -ne 'accept.vector-row-crosses-table1-into-map1'
    if($cross2){$v2raw=Raw-At ($script:Table2+4);$vec2=[byte[]]$case.Pe[$v2raw..($v2raw+5)];Add-Row $case ($script:Table2+4) $vec2 'vmovups xmm0, xmmword ptr [rax+rax+1]';$map2Start=2}else{$v2=[byte[]]$case.Pe[(Raw-At ($script:Table2+4))..(Raw-At ($script:Table2+7))];Add-Row $case ($script:Table2+4) $v2 'data dword';$map2Start=0}
    for($i=$map1Start;$i -lt $map1.Length;++$i){Add-Row $case ([UInt32]($script:Map1+$i)) ([byte[]]@($map1[$i])) 'byte data'}
    for($i=$map2Start;$i -lt $map2.Length;++$i){Add-Row $case ([UInt32]($script:Map2+$i)) ([byte[]]@($map2[$i])) 'byte data'}
    Add-Row $case ([UInt32]($script:Map2+21)) ([byte[]]@(0x0f,0x1f,0x00)) 'nop dword ptr [rax]'
    return $case
}
function Add-BranchFault($Case,[string]$Kind,[uint32]$Destination) {
    $at=if($Destination -ge $script:Table1-3){[UInt32]($script:Table1-0x30)}else{[UInt32]($script:P-8)}
    $n=if($Kind -eq 'rel8' -or $Kind -eq 'loop' -or $Kind -eq 'jrcxz'){2}elseif($Kind -eq 'prefixed-jrcxz'){3}else{5}
    for($i=$Case.Rows.Count-1;$i -ge 0;--$i){$r=$Case.Rows[$i];if($r.Rva -ge $at -and $r.Rva -lt $at+$n){$Case.Rows.RemoveAt($i)}}
    $b=switch($Kind){'rel8'{[byte[]]@(0xeb,0)}'loop'{[byte[]]@(0xe2,0)}'jrcxz'{[byte[]]@(0xe3,0)}'prefixed-jrcxz'{[byte[]]@(0x67,0xe3,0)}'call'{[byte[]]@(0xe8,0,0,0,0)}default{[byte[]]@(0xe9,0,0,0,0)}}
    if($n -eq 2){$b[1]=[byte]([int]([Int64]$Destination-[Int64]($at+2)) -band 255)}elseif($n -eq 3){$b[2]=[byte]([int]([Int64]$Destination-[Int64]($at+3)) -band 255)}else{[Array]::Copy([BitConverter]::GetBytes([Int32]([Int64]$Destination-[Int64]($at+5))),0,$b,1,4)}
    Add-Row $Case $at $b ("jmp/call/loop control to $Destination")
}
function Apply-Fault($Case,[string]$Fault) {
    $p=$script:P
    switch -Exact ($Fault) {
        'accept.vector-row-crosses-table1-into-map1' { }
        'accept.vector-row-crosses-table2-into-map2' { }
        'accept.baseline' { }
        'reject.unaligned-tables' { }
        'reject.wrong-producer' { Patch-Byte $Case $p 0x49 }
        'reject.wrong-imagebase' { Patch-Byte $Case ($p+3) ([byte]($Case.Pe[(Raw-At ($p+3))]+1)) }
        'reject.signed-ja' { Patch-Byte $Case ($p+14) 0x7f }
        'reject.wrong-cmp-immediate' { Patch-Byte $Case ($p+10) 0x01 }
        'reject.wrong-cmp-register' { Patch-Byte $Case ($p+9) 0xfe }
        'reject.wrong-cmp-opcode' { Patch-Byte $Case ($p+8) 0x80 }
        'reject.wrong-ja-opcode' { Patch-Byte $Case ($p+14) 0x76 }
        'reject.wrong-je-opcode' { Patch-Byte $Case ($p+16) 0x75 }
        'reject.wrong-lea-opcode' { Patch-Byte $Case ($p+19) 0x89 }
        'reject.wrong-lea-register' { Patch-Byte $Case ($p+20) 0x4f }
        'reject.wrong-lea-disp' { Patch-Byte $Case ($p+21) 0xfb }
        'reject.wrong-case-cmp-opcode' { Patch-Byte $Case ($p+22) 0x81 }
        'reject.wrong-case-cmp-immediate' { Patch-Byte $Case ($p+24) 0x1a }
        'reject.wrong-case-cmp-register' { Patch-Byte $Case ($p+23) 0xf9 }
        'reject.wrong-default-ja' { Patch-Byte $Case ($p+26) 0x86 }
        'reject.wrong-map-load-opcode' { Patch-Byte $Case ($p+32) 0xbe }
        'reject.wrong-map-register' { Patch-Byte $Case ($p+34) 0x8c }
        'reject.wrong-map-base' { Patch-Byte $Case ($p+31) 0x40 }
        'reject.wrong-map-index' { Patch-Byte $Case ($p+35) 0x09 }
        'reject.wrong-map-displacement' { Patch-Byte $Case ($p+36) ([byte]($Case.Pe[(Raw-At ($p+36))]+1)) }
        'reject.wrong-table-load-opcode' { Patch-Byte $Case ($p+41) 0x89 }
        'reject.wrong-table-register' { Patch-Byte $Case ($p+42) 0x84 }
        'reject.wrong-table-base' { Patch-Byte $Case ($p+43) 0x80 }
        'reject.wrong-table-index' { Patch-Byte $Case ($p+43) 0x91 }
        'reject.wrong-table-scale' { Patch-Byte $Case ($p+43) 0x41 }
        'reject.wrong-table-displacement' { Patch-Byte $Case ($p+44) ([byte]($Case.Pe[(Raw-At ($p+44))]+1)) }
        'reject.wrong-add-opcode' { Patch-Byte $Case ($p+49) 0x01 }
        'reject.wrong-add-register' { Patch-Byte $Case ($p+50) 0xc1 }
        'reject.wrong-jump-opcode' { Patch-Byte $Case ($p+51) 0x90 }
        'reject.wrong-jump-register' { Patch-Byte $Case ($p+52) 0xe0 }
        'reject.gap-in-chain' { Patch-Byte $Case ($p+53) 0x90 }
        'reject.map1-missing' { $Case.MissingData='Map1' }
        'reject.map2-missing' { $Case.MissingData='Map2' }
        'reject.table1-missing' { $Case.MissingData='Table1' }
        'reject.table2-missing' { $Case.MissingData='Table2' }
        'reject.data-wrong-owner' { $Case.DataOwner=1 }
        'reject.procedure-wrong-owner' { $Case.ProcedureOwner=1 }
        'reject.procedure-missing' { $Case.MissingProcedure=$true }
        'reject.procedure-start' { $Case.ProcedureBegin=$p+1 }
        'reject.procedure-short' { $Case.ProcedureEnd-=8 }
        'reject.procedure-long' { $Case.ProcedureEnd+=8 }
        'reject.contribution-short' { $Case.ContributionEnd=$script:Table1 }
        'reject.data-named' { $Case.NamedData='Table1' }
        'reject.data-typed' { $Case.TypedData='Table1' }
        'reject.data-start' { $Case.DataStartFault='Table1' }
        'reject.data-extent' { $Case.ExtraSymbol=$script:PaddingEnd-1 }
        'reject.map1-extent' { $Case.ExtraSymbol=$script:Map1+14 }
        'reject.map2-extent' { $Case.ExtraSymbol=$script:Map2+10 }
        'reject.table1-extent' { $Case.ExtraSymbol=$script:Table1+4 }
        'reject.table2-extent' { $Case.ExtraSymbol=$script:Table2+4 }
        'reject.wrong-adjacency-map1' { $Case.DataStartFault='Map1' }
        'reject.wrong-adjacency-table2' { $Case.DataStartFault='Table2' }
        'reject.wrong-adjacency-map2' { $Case.DataStartFault='Map2' }
        'reject.wrong-padding-byte' { Patch-Byte $Case ($script:Map2+23) 0x01 }
        'reject.wrong-padding-length' { $Case.DataStartFault='Next';$Case.NextSymbolExtra=1 }
        'reject.map1-value-out-of-range' { Patch-Byte $Case ($script:Map1+10) 0x02 }
        'reject.map2-value-out-of-range' { Patch-Byte $Case ($script:Map2+10) 0x02 }
        'reject.map1-case-out-of-range' { Patch-Byte $Case ($p+24) 0x1c }
        'reject.map2-case-out-of-range' { Patch-Byte $Case ($p+62) 0x15 }
        'reject.table1-target-interior' { Patch-U32 $Case $script:Table1 ($p+1) }
        'reject.table1-target-producer' { Patch-U32 $Case $script:Table1 $p }
        'reject.table1-target-data' { Patch-U32 $Case $script:Table1 $script:Table1 }
        'reject.table1-target-padding' { Patch-U32 $Case $script:Table1 ($script:Table1-1) }
        'reject.table1-target-outside' { Patch-U32 $Case $script:Table1 0x7fffffff }
        'reject.table2-target-interior' { Patch-U32 $Case $script:Table2 ($p+1) }
        'reject.table2-target-producer' { Patch-U32 $Case $script:Table2 $p }
        'reject.table2-target-data' { Patch-U32 $Case $script:Table2 $script:Table2 }
        'reject.table2-target-padding' { Patch-U32 $Case $script:Table2 ($script:Table1-1) }
        'reject.table2-target-outside' { Patch-U32 $Case $script:Table2 0x7fffffff }
        'reject.default-interior' { Set-Rel32 $Case.Pe ($p+27) ($p+31) ($p+1) }
        'reject.default-data' { Set-Rel32 $Case.Pe ($p+27) ($p+31) $script:Table1; Set-Rel32 $Case.Pe ($p+65) ($p+69) $script:Table1 }
        'reject.default-padding' { Set-Rel32 $Case.Pe ($p+27) ($p+31) ($script:Table1-1); Set-Rel32 $Case.Pe ($p+65) ($p+69) ($script:Table1-1) }
        'reject.default-outside' { Set-Rel32 $Case.Pe ($p+27) ($p+31) 0x7fffffff; Set-Rel32 $Case.Pe ($p+65) ($p+69) 0x7fffffff }
        'reject.incoming-branch-interior' { Add-BranchFault $Case 'rel32' ($p+20) }
        'reject.incoming-call-interior' { Add-BranchFault $Case 'call' ($p+20) }
        'reject.incoming-branch-data' { Add-BranchFault $Case 'rel32' $script:Table1 }
        'reject.incoming-call-data' { Add-BranchFault $Case 'call' $script:Map1 }
        'reject.prefixed-jrcxz' { Add-BranchFault $Case 'prefixed-jrcxz' ($p+20) }
        'reject.loop-family' { Add-BranchFault $Case 'loop' ($p+20) }
        'reject.pe-listing-mismatch' { Patch-Byte $Case ($p+0x100) 0xcc }
        'reject.pe-row-missing' { $Case.MissingRow=[UInt32]($p+0x100);Set-RowBytes $Case ($p+0x100) ([byte[]]@(0xc5,0xf8,0x10,0x00)) 'vmovups xmm0, xmmword ptr [rax]' }
        'reject.pe-row-overlap' { $Case.SectionOverlap=$true;Set-RowBytes $Case ($p+0x100) ([byte[]]@(0xc5,0xf8,0x10,0x00)) 'vmovups xmm0, xmmword ptr [rax]' }
        'reject.listing-row-missing' { $Case.MissingRow=$p+0x100 }
        'reject.listing-row-overlap' { $Case.OverlapRow=$p+0x100 }
        'reject.listing-row-unknown' { $Case.UnknownRow=$p+0x100 }
        'reject.real-vex-before' { Set-RowBytes $Case ($p+0x100) ([byte[]]@(0xc5,0xf8,0x10,0x00)) 'vmovups xmm0, xmmword ptr [rax]' }
        'reject.real-vex-after' { $Case.VectorAfter=$true }
        'reject.real-vex-adjacent' { $Case.VectorAdjacent='vex' }
        'reject.real-evex-adjacent' { $Case.VectorAdjacent='evex' }
        'reject.real-vector-crosses-target-map' { Patch-U32 $Case $script:Table1 ([UInt32]($script:Table1+4)) }
        'reject.terminal-wrong-opcode' { Patch-Byte $Case ($script:Table1-8) 0xe8 }
        'reject.terminal-wrong-target' { for($i=$Case.Rows.Count-1;$i -ge 0;--$i){$r=$Case.Rows[$i];if($r.Rva -ge $p+0x100 -and $r.Rva -lt $p+0x102){$Case.Rows.RemoveAt($i)}};Add-Row $Case ($p+0x100) ([byte[]]@(0x66,0x90)) 'nop';Set-Rel32 $Case.Pe ($script:Table1-7) ($script:Table1-3) ($p+0x101) }
        'reject.terminal-target-producer-interior' { Set-Rel32 $Case.Pe ($script:Table1-7) ($script:Table1-3) ($p+1) }
        'reject.terminal-target-data' { Set-Rel32 $Case.Pe ($script:Table1-7) ($script:Table1-3) $script:Map1 }
        'reject.terminal-target-padding' { Set-Rel32 $Case.Pe ($script:Table1-7) ($script:Table1-3) ($script:Table1-1) }
        'reject.terminal-missing' { $Case.MissingRow=$script:Table1-8 }
        'reject.terminal-listing-mismatch' { $Case.ListingMismatch=$script:Table1-8 }
        'reject.terminal-overlap' { $Case.OverlapRow=$script:Table1-8 }
        'reject.code-row-gap-before-table' { $Case.MissingRow=$p+0x100 }
        'reject.code-row-unknown-before-table' { $Case.UnknownRow=$p+0x100 }
        'reject.code-row-overlap-before-table' { $Case.OverlapRow=$p+0x100 }
        'reject.incoming-branch-producer-interior' { Add-BranchFault $Case 'rel32' ($p+1) }
        'reject.incoming-call-producer-interior' { Add-BranchFault $Case 'call' ($p+1) }
        'reject.incoming-loop-pair-data' { Add-BranchFault $Case 'loop' $script:Table2 }
        'reject.incoming-prefixed-jrcxz-producer' { Add-BranchFault $Case 'prefixed-jrcxz' ($p+1) }
        'reject.incoming-prefixed-jrcxz-data' { Add-BranchFault $Case 'prefixed-jrcxz' $script:Map2 }
        'accept.baseline' { }
        'accept.relocated-rva-vex' { }
        'accept.relocated-rva-evex' { }
        'accept.terminal-e9-nop-boundary' { }
        default { throw "No fixture mutation implemented for frozen label: $Fault" }
    }
}
function New-SymbolText($Case) {
    $mod0='SyntheticR9R15Pair.obj';$mod1='SyntheticOther.obj';$g=[char]96
    $text="  Mod 0 | ${g}$mod0${g}:`n  Obj: ${g}$mod0${g}:`n  Mod 1 | ${g}$mod1${g}:`n  Obj: ${g}$mod1${g}:`n"
    if(-not $Case.MissingProcedure){$owner=if($Case.ContainsKey('ProcedureOwner')){$Case.ProcedureOwner}else{0};$start=$Case.ProcedureBegin-$script:SectionRva;$end=$Case.ProcedureEnd-$script:SectionRva;$text+="  Mod $owner | ${g}$mod0${g}:`n  0 | S_GPROC32 [size = 16] $g`SyntheticPair$g`n  addr = 0001:$start, code size = $($end-$start)`n"}
    $records=@(@{N='Table1';R=$script:Table1},@{N='Map1';R=$script:Map1},@{N='Table2';R=$script:Table2},@{N='Map2';R=$script:Map2},@{N='Next';R=$script:PaddingEnd})
    foreach($r in $records){
        if($Case.ContainsKey('MissingData') -and $Case.MissingData -eq $r.N){continue}
        $owner=if($Case.ContainsKey('DataOwner')){$Case.DataOwner}else{0};$at=[UInt32]$r.R
        if($Case.ContainsKey('DataStartFault') -and $Case.DataStartFault -eq $r.N){$at++}
        if($r.N -eq 'Next' -and $Case.ContainsKey('NextSymbolExtra')){$at+=$Case.NextSymbolExtra}
        $name='';$type='<no type>'
        if($Case.ContainsKey('NamedData') -and $Case.NamedData -eq $r.N){$name='named'}
        if($Case.ContainsKey('TypedData') -and $Case.TypedData -eq $r.N){$type='T_NOTYPE (0x0000)'}
        $module=if($owner -eq 1){$mod1}else{$mod0}
        $text+="  Mod $owner | ${g}$module${g}:`n  0 | S_LDATA32 [size = 16] $g$name$g`n  type = $type, addr = 0001:$($at-$script:SectionRva)`n"
    }
    if($Case.ContainsKey('ExtraSymbol')){$text+="  Mod 0 | ${g}$mod0${g}:`n  0 | S_LDATA32 [size = 16] $g$g`n  type = <no type>, addr = 0001:$($Case.ExtraSymbol-$script:SectionRva)`n"}
    return $text
}
function Materialize-Case($Case,[string]$Directory) {
    $mutation=$Case.Mutation
    $sectionEnd=if($Case.ContainsKey('ContributionEnd')){$Case.ContributionEnd}else{$script:ContributionEnd}
    $contribSize=[UInt32]($sectionEnd-$script:SectionRva)
    if($mutation -eq 'reject.contribution-short'){$contribSize=[UInt32]($script:Table1-$script:SectionRva)}
    $metadata="  Age: $script:Age`n  GUID: {$($script:Guid)}`n  Mod 0 | ``SyntheticR9R15Pair.obj``:`n  Obj: ``SyntheticR9R15Pair.obj``:`n  Mod 1 | ``SyntheticOther.obj``:`n  Obj: ``SyntheticOther.obj``:`n`nSection Contributions`n  SC[.text] | mod = 0, 0001:0, size = $contribSize, data crc = 0, reloc crc = 0`n"
    $sym=New-SymbolText $Case
    $rows=[Collections.Generic.List[string]]::new()
    foreach($r in $Case.Rows){if($Case.ContainsKey('MissingRow') -and $r.Rva -eq $Case.MissingRow){continue};$bytes=[byte[]]$r.Bytes;$txt=$r.Text;if($Case.ContainsKey('UnknownRow') -and $r.Rva -eq $Case.UnknownRow){$txt='<unknown>'};if($Case.ContainsKey('ListingMismatch') -and $r.Rva -eq $Case.ListingMismatch){$bytes=$bytes.Clone();$bytes[0]=$bytes[0]-bxor 1};$rows.Add(('{0:x}: {1} {2}' -f ($script:ImageBase+$r.Rva),(Hex-Bytes $bytes),$txt))}
    if($Case.ContainsKey('OverlapRow')){$rows.Add(('{0:x}: 90 nop' -f ($script:ImageBase+$Case.OverlapRow+1)))}
    if($Case.ContainsKey('VectorAdjacent')){$rva=$script:PaddingEnd;$vb=if($Case.VectorAdjacent -eq 'evex'){[byte[]]@(0x62,0xf1,0x7c,0x48,0x10,0)}else{[byte[]]@(0xc5,0xf8,0x10,0)};Write-Rva $Case.Pe $rva $vb;$rows.Add(('{0:x}: {1} vector adjacent' -f ($script:ImageBase+$rva),(Hex-Bytes $vb)))}
    if($Case.ContainsKey('VectorAfter')){$rva=$script:ContributionEnd+1;$vb=[byte[]]@(0xc5,0xf8,0x10,0);Write-Rva $Case.Pe $rva $vb;$rows.Add(('{0:x}: {1} vmovups xmm0, xmmword ptr [rax]' -f ($script:ImageBase+$rva),(Hex-Bytes $vb)))}
    if($mutation -eq 'reject.pe-row-overlap'){$sh2=0x1b0;Set-U16 $Case.Pe 0x86 2;Put-Bytes $Case.Pe $sh2 ([Text.Encoding]::ASCII.GetBytes('.alias'+[char]0+[char]0));Set-U32 $Case.Pe ($sh2+8) $script:RawSize;Set-U32 $Case.Pe ($sh2+12) $script:SectionRva;Set-U32 $Case.Pe ($sh2+16) $script:RawSize;Set-U32 $Case.Pe ($sh2+20) 0x100;Set-U32 $Case.Pe ($sh2+36) 0x60000020}
    [IO.File]::WriteAllBytes((Join-Path $Directory 'Synthetic.exe'),$Case.Pe)
    Set-Content -LiteralPath (Join-Path $Directory 'contributions.txt') -Value $metadata -Encoding utf8
    Set-Content -LiteralPath (Join-Path $Directory 'symbols.txt') -Value $sym -Encoding utf8
    $sortedRows=@($rows | Sort-Object { ([string]$_).Substring(0,([string]$_).IndexOf(':')) })
    Set-Content -LiteralPath (Join-Path $Directory 'disassembly.txt') -Value ($sortedRows -join "`n") -Encoding utf8
}

$expected=@($script:ExpectedLines | Where-Object { $_.Length -gt 0 })
if($expected.Count -ne 114 -or $expected[0] -ne "Label`tExpectedDisposition`tMutation"){throw 'Frozen Expected.tsv schema/count changed; expected header plus exactly 113 cases.'}
$accepted=0;$refused=0;$seen=[Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
foreach($line in $expected | Select-Object -Skip 1){
    $fields=$line.Split([char]9);if($fields.Count -ne 3){throw "Malformed Expected.tsv row: $line"}
    $label=$fields[0];$disposition=$fields[1];$mutation=$fields[2]
    if(-not $seen.Add($label)){throw "Duplicate frozen label: $label"}
    if(($disposition -ne 'ACCEPT' -and $disposition -ne 'REJECT') -or $mutation -ne $label.Substring('isa-r9-r15-pair.'.Length)){throw "Invalid frozen expectation row: $line"}
    $case=New-BaseCase $label $mutation
    Apply-Fault $case $mutation
    if($mutation -ne 'reject.pe-listing-mismatch'){foreach($row in $case.Rows){$raw=Raw-At $row.Rva;$row.Bytes=[byte[]]$case.Pe[$raw..($raw+$row.Bytes.Length-1)]}}
    $directory=Join-Path $OutputRoot $mutation
    [void](New-Item -ItemType Directory -Path $directory -Force)
    Materialize-Case $case $directory
    $binary=Join-Path $directory 'Synthetic.exe';$metadata=Join-Path $directory 'contributions.txt';$symbols=Join-Path $directory 'symbols.txt';$listing=Join-Path $directory 'disassembly.txt'
    $result=[RedPrismBuild.IsaScanner]::Scan($binary,$metadata,$symbols,$listing,[string[]]@(),[string[]]@(),[string[]]@(), $false)
    $passed=if($disposition -eq 'ACCEPT'){$result.Violations.Count -eq 0}else{$result.Violations.Count -gt 0}
    if(-not $passed){throw "$label expected $disposition, got violations=$($result.Violations.Count): $($result.Violations -join '; ')"}
    if($disposition -eq 'ACCEPT'){++$accepted}else{++$refused}
    Write-Host "PASS $label $disposition"
}
if($seen.Count -ne 113 -or $accepted -ne 6 -or $refused -ne 107){throw "Unexpected final roster counts: cases=$($seen.Count), accepted=$accepted, rejected=$refused"}
Write-Host "SUMMARY isa-r9-r15-pair cases=$($seen.Count) accepted=$accepted rejected=$refused"
