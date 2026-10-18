[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$fixtureRoot = Join-Path $repo ('.build/BuildProcessTests/IsaSplitTailUnsigned-' + [guid]::NewGuid().ToString('N'))
[void](New-Item -ItemType Directory -Path $fixtureRoot -Force)
if (-not ('RedPrismBuild.IsaScanner' -as [type])) {
    Add-Type -Path (Join-Path $repo 'Build/IsaScan.cs')
}

# Encoded parser controls; none of these bytes execute as native instructions.
$script:ImageBase = [UInt64]0x140000000
$script:SectionRva = [UInt32]0x0010f000
$script:ProcBegin = [UInt32]0x0010f800
$script:Dispatch1 = [UInt32]($script:ProcBegin + 0x4b)
$script:Dispatch1After = [UInt32]($script:ProcBegin + 0x66)
$script:Dispatch2Prefix = [UInt32]($script:ProcBegin + 0x73)
$script:Dispatch2 = [UInt32]($script:ProcBegin + 0x82)
$script:Dispatch2After = [UInt32]($script:ProcBegin + 0x95)
$script:TerminalRet = [UInt32]($script:ProcBegin + 0x142)
$script:TerminalNop = [UInt32]($script:ProcBegin + 0x143)
$script:Table1 = [UInt32]($script:ProcBegin + 0x144)
$script:Remap = [UInt32]($script:ProcBegin + 0x164)
$script:Table2 = [UInt32]($script:ProcBegin + 0x180)
$script:Table2End = [UInt32]($script:ProcBegin + 0x1d4)
$script:LaterAnonymous = [UInt32]($script:Table2End + 0x10)
$script:LaterProcedure = [UInt32]($script:Table2End + 0x08)
$script:Target = [UInt32]($script:ProcBegin + 0xc5)
$script:Default1 = $script:Target
$script:Default2 = [UInt32]($script:ProcBegin + 0x137)
$script:Guid = [guid]'47a7f951-2db8-45d5-9068-2a8e5fa150bc'
$script:Age = [UInt32]3

function Set-U16([byte[]]$Bytes, [int]$Offset, [UInt16]$Value) {
    [Array]::Copy([BitConverter]::GetBytes($Value), 0, $Bytes, $Offset, 2)
}
function Set-U32([byte[]]$Bytes, [int]$Offset, [UInt32]$Value) {
    [Array]::Copy([BitConverter]::GetBytes($Value), 0, $Bytes, $Offset, 4)
}
function Put-Bytes([byte[]]$Bytes, [int]$Offset, [byte[]]$Value) {
    [Array]::Copy($Value, 0, $Bytes, $Offset, $Value.Length)
}
function Hex-Bytes([byte[]]$Bytes) {
    return (($Bytes | ForEach-Object { $_.ToString('x2') }) -join ' ')
}

function New-Case {
    param(
        [Parameter(Mandatory)][string]$Name,
        [string]$Fault = ''
    )
    $dir = Join-Path $fixtureRoot $Name
    [void](New-Item -ItemType Directory -Path $dir -Force)
    $binary = Join-Path $dir 'SyntheticIsaSplitTail.exe'
    $metadata = Join-Path $dir 'contributions.txt'
    $symbols = Join-Path $dir 'symbols.txt'
    $disassembly = Join-Path $dir 'disassembly.txt'

    # PE32+ with one executable section. Bytes are parser input only; no code executes.
    $pe = [byte[]]::new(0x2400)
    $pe[0] = 0x4d; $pe[1] = 0x5a
    Set-U32 $pe 0x3c 0x80
    Set-U32 $pe 0x80 0x00004550
    Set-U16 $pe 0x84 0x8664
    Set-U16 $pe 0x86 1
    Set-U16 $pe 0x94 0xf0
    Set-U16 $pe 0x98 0x20b
    [Array]::Copy([BitConverter]::GetBytes($script:ImageBase), 0, $pe, (0x98 + 24), 8)
    Set-U32 $pe (0x98 + 56) ($script:SectionRva + 0x2000)
    Set-U32 $pe (0x98 + 60) 0x400
    Set-U32 $pe (0x98 + 112 + 6*8) ($script:SectionRva + 0x180)
    Set-U32 $pe (0x98 + 116 + 6*8) 28
    $section = 0x80 + 24 + 0xf0
    Put-Bytes $pe $section ([Text.Encoding]::ASCII.GetBytes('.text' + [char]0 + [char]0 + [char]0))
    Set-U32 $pe ($section+8) 0x2000
    Set-U32 $pe ($section+12) $script:SectionRva
    Set-U32 $pe ($section+16) 0x2000
    Set-U32 $pe ($section+20) 0x400
    Set-U32 $pe ($section+36) 0x60000020
    Set-U32 $pe (0x400+0x180+12) 2
    Set-U32 $pe (0x400+0x180+16) 24
    Set-U32 $pe (0x400+0x180+20) ($script:SectionRva + 0x1a0)
    Set-U32 $pe (0x400+0x180+24) 0x5a0
    Put-Bytes $pe 0x5a0 ([Text.Encoding]::ASCII.GetBytes('RSDS'))
    Put-Bytes $pe 0x5a4 $script:Guid.ToByteArray()
    Set-U32 $pe 0x5b4 $script:Age

    $raw = { param([UInt32]$Rva) [int](0x400 + $Rva - $script:SectionRva) }
    $rows = [Collections.Generic.List[string]]::new()

    # First 42-byte RCX remap dispatcher: CMP DWORD [RSP+20h],1bh;
    # near JA; zero-extending MOV EAX; LEA RCX,imagebase; MOVZX remap;
    # DWORD table load; ADD RAX,RCX; JMP RAX. This is covered only by
    # the joint pair proof; the standalone byte-index proof rejects it.
    [UInt32]$firstPrefix = $script:ProcBegin + 0x3c
    [byte[]]$firstCmp = @(0x83,0x7c,0x24,0x20,0x1b)
    [byte[]]$firstJa = @(0x0f,0x87,0,0,0,0)
    [Int32]$firstJaDisp = [Int32]([Int64]$script:Default1 - [Int64]($firstPrefix+11))
    if ($Fault -eq 'FirstDefault') {
        [Int32]$firstJaDisp = [Int32]([Int64]($script:ProcBegin+0x70) - [Int64]($firstPrefix+11))
    }
    Put-Bytes $firstJa 2 ([BitConverter]::GetBytes($firstJaDisp))
    [byte[]]$firstIndex = @(0x8b,0x44,0x24,0x20)
    [byte[]]$firstLeaBase = @(0x48,0x8d,0x0d,0,0,0,0)
    [Int32]$firstLeaDisp = [Int32](-[Int64]($script:Dispatch1+7))
    Put-Bytes $firstLeaBase 3 ([BitConverter]::GetBytes($firstLeaDisp))
    [byte[]]$firstRemapLoad = @(0x0f,0xb6,0x84,0x01,0,0,0,0)
    Set-U32 $firstRemapLoad 4 $script:Remap
    [byte[]]$firstDwordLoad = @(0x8b,0x84,0x81,0,0,0,0)
    Set-U32 $firstDwordLoad 3 $script:Table1
    switch ($Fault) {
        'FirstCount' { $firstCmp[4] = 0x1a }
        'FirstSlot' { $firstCmp[3] = 0x28 }
        'FirstBase' { $firstLeaBase[3] = $firstLeaBase[3] -bxor 1 }
        'FirstLoad' { $firstRemapLoad[1] = 0xb7 }
        'FirstTable' { Set-U32 $firstDwordLoad 3 ($script:Table1+4) }
    }
    [byte[]]$firstAdd = @(0x48,0x03,0xc1)
    [byte[]]$firstJump = @(0xff,0xe0)
    [string]$firstCmpText = if ($Fault -eq 'FirstCount') { 'cmp dword ptr [rsp+20h], 1ah' }
        else { 'cmp dword ptr [rsp+20h], 1bh' }
    $firstSegments = @(
        @{ Rva=$firstPrefix; Bytes=$firstCmp; Text=$firstCmpText },
        @{ Rva=($firstPrefix+5); Bytes=$firstJa; Text='ja first-default' },
        @{ Rva=($firstPrefix+11); Bytes=$firstIndex; Text='mov eax, dword ptr [rsp+20h]' },
        @{ Rva=$script:Dispatch1; Bytes=$firstLeaBase; Text='lea rcx, [imagebase]' },
        @{ Rva=($script:Dispatch1+7); Bytes=$firstRemapLoad; Text='movzx eax, byte ptr [rcx+rax+remap]' },
        @{ Rva=($script:Dispatch1+15); Bytes=$firstDwordLoad; Text='mov eax, dword ptr [rcx+rax*4+table1]' },
        @{ Rva=($script:Dispatch1+22); Bytes=$firstAdd; Text='add rax, rcx' },
        @{ Rva=($script:Dispatch1+25); Bytes=$firstJump; Text='jmp rax' }
    )
    for ($rowIndex=0; $rowIndex -lt $firstSegments.Count; ++$rowIndex) {
        $segment = $firstSegments[$rowIndex]
        [byte[]]$bytes = $segment.Bytes
        Put-Bytes $pe (& $raw $segment.Rva) $bytes
        [string]$text = $segment.Text
        $rowFaultPrefix = "FirstDispatchRow${rowIndex}"
        if ($Fault -eq ($rowFaultPrefix + 'Unknown')) { $text = '<unknown>' }
        [byte[]]$reported = $bytes
        if ($Fault -eq ($rowFaultPrefix + 'Listing')) {
            $reported = [byte[]]$bytes.Clone()
            $reported[$reported.Length-1] = $reported[$reported.Length-1] -bxor 1
        }
        $line = '{0:x}: {1} {2}' -f ($script:ImageBase+$segment.Rva), (Hex-Bytes $reported), $text
        if ($Fault -ne ($rowFaultPrefix + 'Missing')) { $rows.Add($line) }
        if ($Fault -eq ($rowFaultPrefix + 'Duplicate')) { $rows.Add($line) }
    }
    if (($script:Dispatch1+27) -ne $script:Dispatch1After) { throw 'First remap dispatcher offset drifted.' }

    # Seven-row unsigned stack DWORD candidate from the guarded pair recognizer.
    $dispatches = @(@{ Start=[UInt32]$script:Dispatch2; Table=[UInt32]$script:Table2; Max=[byte]0x14; Near=$true })
    foreach ($item in $dispatches) {
        [UInt32]$start = $item.Start
        [UInt32]$table = $item.Table
        [byte]$maximum = $item.Max
        [UInt32]$guard = $start - $(if ($item.Near) { 15 } else { 11 })
        [byte[]]$cmp = @(0x83,0x7c,0x24,0x20,$maximum)
        [byte[]]$branch = if ($item.Near) { @(0x0f,0x87,0,0,0,0) } else { @(0x77,0) }
        [UInt32]$indexRva = $guard + [UInt32]$cmp.Length + [UInt32]$branch.Length
        [byte[]]$index = @(0x8b,0x44,0x24,0x20)
        [UInt32]$leaRva = $indexRva + [UInt32]$index.Length
        [byte[]]$lea = @(0x48,0x8d,0x0d,0,0,0,0)
        [Int32]$leaDisp = [Int32](-[Int64]($leaRva + 7))
        Put-Bytes $lea 3 ([BitConverter]::GetBytes($leaDisp))
        [UInt32]$loadRva = $leaRva + $(if ($item.Near) { 7 } else { 15 })
        [byte[]]$load = @(0x8b,0x84,0x81,0,0,0,0)
        Set-U32 $load 3 $table
        [UInt32]$addRva = $loadRva + 7
        [byte[]]$add = @(0x48,0x03,0xc1)
        [UInt32]$jumpRva = $addRva + 3
        [byte[]]$jump = @(0xff,0xe0)
        [UInt32]$afterJump = $jumpRva + 2
        [UInt32]$default = if ($item.Near) { $script:Default2 } else { $script:Default1 }

        if ($item.Near) {
            [Int32]$branchDisp = [Int32]([Int64]$default - [Int64]($guard + 11))
            Put-Bytes $branch 2 ([BitConverter]::GetBytes($branchDisp))
        } else {
            [Int32]$branchDisp = [Int32]([Int64]$default - [Int64]($guard + 7))
            $branch[1] = [byte]($branchDisp -band 0xff)
        }
        if ($item.Near) {
            switch ($Fault) {
                'GuardCompare' { $cmp[1] = 0x7d }
                'GuardMaximum' { $cmp[4] = 0x13 }
                'IndexSlot' { $index[3] = 0x28 }
                'BypassBranch' { if ($item.Near) { $branch[1] = 0x86 } else { $branch[0] = 0x76 } }
                'UnprovenIndirectFlow' { $jump[1] = 0xe1 }
            }
        }

        $segments = @(
            @{ Rva=$guard; Bytes=$cmp; Text='cmp dword ptr [rsp+20h], imm8' },
            @{ Rva=($guard+[UInt32]$cmp.Length); Bytes=$branch; Text=$(if ($item.Near) {'ja default'} else {'ja short default'}) },
            @{ Rva=$indexRva; Bytes=$index; Text='mov eax, dword ptr [rsp+20h]' },
            @{ Rva=$leaRva; Bytes=$lea; Text='lea rcx, [imagebase]' }
        )
        $segments += @(
            @{ Rva=$loadRva; Bytes=$load; Text='mov eax, dword ptr [rcx+rax*4+table]' },
            @{ Rva=$addRva; Bytes=$add; Text='add rax, rcx' },
            @{ Rva=$jumpRva; Bytes=$jump; Text=$(if ($Fault -eq 'UnprovenIndirectFlow' -and $item.Near) {'jmp rcx'} else {'jmp rax'}) }
        )
        if ($item.Near -and $afterJump -ne $script:Dispatch2After) { throw 'Second dispatch fixture offset drifted.' }
        if (-not $item.Near -and $afterJump -ne $script:Dispatch1After) { throw 'First remap dispatch fixture offset drifted.' }
        for ($rowIndex=0; $rowIndex -lt $segments.Count; ++$rowIndex) {
            $segment = $segments[$rowIndex]
            [byte[]]$bytes = $segment.Bytes
            Put-Bytes $pe (& $raw $segment.Rva) $bytes
            [string]$text = $segment.Text
            if ($Fault -eq 'BypassBranch' -and $item.Near -and $segment.Rva -eq ($guard+[UInt32]$cmp.Length)) { $text = 'jbe default' }
            $rowFaultPrefix = "DispatchRow${rowIndex}"
            if ($item.Near -and $Fault -eq ($rowFaultPrefix + 'Listing')) { $bytes[$bytes.Length-1] = $bytes[$bytes.Length-1] -bxor 1 }
            if ($item.Near -and $Fault -eq ($rowFaultPrefix + 'Unknown')) { $text = '<unknown>' }
            $line = '{0:x}: {1} {2}' -f ($script:ImageBase+$segment.Rva), (Hex-Bytes $bytes), $text
            if (-not ($item.Near -and $Fault -eq ($rowFaultPrefix + 'Missing'))) {
                $rows.Add($line)
                if ($item.Near -and $Fault -eq ($rowFaultPrefix + 'Duplicate')) { $rows.Add($line) }
            }
        }
    }

    # All indexed case targets lie after dispatch 2 and before the terminal RET.
    [byte[]]$targetBytes = @(0x66,0x90)
    Put-Bytes $pe (& $raw $script:Target) $targetBytes
    $rows.Add(('{0:x}: 66 90 two-byte nop' -f ($script:ImageBase+$script:Target)))
    foreach ($defaultRva in @($script:Default2)) {
        Put-Bytes $pe (& $raw $defaultRva) ([byte[]]@(0x90))
        $rows.Add(('{0:x}: 90 nop' -f ($script:ImageBase+$defaultRva)))
    }

    # Fill a contiguous decoded body through RET/NOP. There are no data bytes
    # before the terminal instructions, and no gaps in the disassembly rows.
    [UInt32]$bodyRowAddress = $script:ProcBegin
    [UInt32]$bodyOverrideStart = $script:ProcBegin + 0x10
    [UInt32]$bodyOverrideTarget = 0
    [byte[]]$bodyOverrideBytes = @()
    [string]$bodyOverrideText = ''
    switch ($Fault) {
        'BodyJmpFirstIndex' { $bodyOverrideTarget = $firstPrefix + 11 }
        'BodyJmpFirstDispatch' { $bodyOverrideTarget = $script:Dispatch1 }
        'BodyJmpSecondIndex' { $bodyOverrideTarget = $script:Dispatch2Prefix + 11 }
        'BodyJmpSecondDispatch' { $bodyOverrideTarget = $script:Dispatch2 }
        'BodyVector' {
            $bodyOverrideBytes = [byte[]]@(0xc5,0xf8,0x10,0x00)
            $bodyOverrideText = 'vmovups xmm0, xmmword ptr [rax]'
        }
    }
    if ($bodyOverrideTarget -ne 0) {
        [Int32]$bodyJumpDisp = [Int32]([Int64]$bodyOverrideTarget - [Int64]($bodyOverrideStart+5))
        $bodyOverrideBytes = [byte[]]@(0xe9,0,0,0,0)
        Put-Bytes $bodyOverrideBytes 1 ([BitConverter]::GetBytes($bodyJumpDisp))
        $bodyOverrideText = ('jmp 0x{0:x}' -f ($script:ImageBase+$bodyOverrideTarget))
    }
    for ([UInt32]$at=$script:ProcBegin; $at -le $script:TerminalNop; $at++) {
        $isCovered = $false
        foreach ($dispatch in $dispatches) {
            $g = [UInt32]$dispatch.Start - $(if ($dispatch.Near) {15} else {11})
            $limit = [UInt32]$dispatch.Start + $(if ($dispatch.Near) {19} else {27})
            if ($at -ge $g -and $at -lt $limit) { $isCovered=$true; break }
        }
        if ($at -ge $firstPrefix -and $at -lt $script:Dispatch1After) { $isCovered=$true }
        if ($at -eq $script:TerminalRet -or $at -eq $script:TerminalNop) { $isCovered=$true }
        if ($at -ge $script:Target -and $at -lt ($script:Target+2)) { $isCovered=$true }
        if ($at -eq $script:Default1 -or $at -eq $script:Default2) { $isCovered=$true }
        if ($bodyOverrideBytes.Length -gt 0 -and $at -ge $bodyOverrideStart -and
            $at -lt ($bodyOverrideStart+[UInt32]$bodyOverrideBytes.Length)) { $isCovered=$true }
        if (-not $isCovered) {
            $pe[(& $raw $at)] = 0x90
            $line = '{0:x}: 90 nop' -f ($script:ImageBase+$at)
            if ($at -eq $bodyRowAddress) {
                if ($Fault -eq 'BodyRowUnknown') { $line = '{0:x}: 90 <unknown>' -f ($script:ImageBase+$at) }
                if ($Fault -eq 'BodyRowListing') { $line = '{0:x}: 91 nop' -f ($script:ImageBase+$at) }
                if ($Fault -ne 'BodyRowMissing') { $rows.Add($line) }
                if ($Fault -eq 'BodyRowDuplicate') { $rows.Add($line) }
            } else {
                $rows.Add($line)
            }
        }
        if ($bodyOverrideBytes.Length -gt 0 -and $at -eq $bodyOverrideStart) {
            Put-Bytes $pe (& $raw $at) $bodyOverrideBytes
            $rows.Add(('{0:x}: {1} {2}' -f ($script:ImageBase+$at), (Hex-Bytes $bodyOverrideBytes), $bodyOverrideText))
        }
    }

    [byte[]]$retBytes = if ($Fault -eq 'RetChanged') { @(0xcc) } else { @(0xc3) }
    [byte[]]$nopBytes = if ($Fault -eq 'NopChanged') { @(0x91) } else { @(0x90) }
    Put-Bytes $pe (& $raw $script:TerminalRet) $retBytes
    Put-Bytes $pe (& $raw $script:TerminalNop) $nopBytes
    if ($Fault -ne 'RetMissing') { $rows.Add(('{0:x}: {1} ret' -f ($script:ImageBase+$script:TerminalRet), (Hex-Bytes $retBytes))) }
    if ($Fault -ne 'NopMissing') { $rows.Add(('{0:x}: {1} nop' -f ($script:ImageBase+$script:TerminalNop), (Hex-Bytes $nopBytes))) }

    # Tail1 is eight DWORD RVAs, followed by the 28-byte remap and 21 DWORD RVAs.
    foreach ($tableInfo in @(@{Rva=$script:Table1; Count=8}, @{Rva=$script:Table2; Count=21})) {
        for ($i=0; $i -lt $tableInfo.Count; $i++) {
            [UInt32]$entry = $script:Target
            if ($Fault -eq 'EarlierTargetIntoCandidateGuard' -and
                $tableInfo.Rva -eq $script:Table1 -and $i -eq 0) {
                $entry = $script:Dispatch2Prefix + 5
            }
            if ($Fault -eq 'TargetMidInstruction' -and $tableInfo.Rva -eq $script:Table2 -and $i -eq 20) { $entry++ }
            Set-U32 $pe ((& $raw $tableInfo.Rva)+4*$i) $entry
            [byte[]]$entryBytes = $pe[((& $raw $tableInfo.Rva)+4*$i)..((& $raw $tableInfo.Rva)+4*$i+3)]
            $rows.Add(('{0:x}: {1} vmovups xmm0, xmmword ptr [rax]' -f ($script:ImageBase+$tableInfo.Rva+4*$i), (Hex-Bytes $entryBytes)))
        }
    }
    # Materialize the post-RET remap bytes as anonymous data, not instructions.
    [byte[]]$remapBytes = [byte[]]::new(28)
    for ($i=0; $i -lt $remapBytes.Length; $i++) { $remapBytes[$i] = [byte]($i % 8) }
    if ($Fault -eq 'RemapIndexOutOfRange') { $remapBytes[0] = 8 }
    Put-Bytes $pe (& $raw $script:Remap) $remapBytes

    # Keep the exact end of the candidate table observable: INT3 padding follows,
    # then a same-owner anonymous symbol inside a distinct later procedure. This
    # prevents generic terminal-table parsing from consuming malformed CC data.
    [byte[]]$int3Padding = [byte[]]::new(32)
    for ($i=0; $i -lt $int3Padding.Length; $i++) { $int3Padding[$i] = 0xcc }
    Put-Bytes $pe (& $raw $script:Table2End) $int3Padding
    for ($i=0; $i -lt $int3Padding.Length; $i++) {
        $rows.Add(('{0:x}: cc int3' -f ($script:ImageBase+$script:Table2End+$i)))
    }
    if ($Fault -eq 'RealVectorAdjacent') {
        [UInt32]$vectorRva = $script:LaterAnonymous
        [byte[]]$vector = @(0xc5,0xf8,0x10,0x00)
        Put-Bytes $pe (& $raw $vectorRva) $vector
        $rows.RemoveAt($rows.Count-16)
        $rows.Add(('{0:x}: {1} vmovups xmm0, xmmword ptr [rax]' -f ($script:ImageBase+$vectorRva), (Hex-Bytes $vector)))
    }
    if ($Fault -eq 'RealEvexAdjacent') {
        [UInt32]$vectorRva = $script:LaterAnonymous
        [byte[]]$vector = @(0x62,0xf1,0x7c,0x08,0x10,0x00)
        Put-Bytes $pe (& $raw $vectorRva) $vector
        $rows.RemoveAt($rows.Count-16)
        $rows.Add(('{0:x}: {1} vmovups xmm0, xmmword ptr [rax]' -f ($script:ImageBase+$vectorRva), (Hex-Bytes $vector)))
    }

    $procedureEnd = $script:Table2End
    if ($Fault -eq 'ProcedureExtent') { $procedureEnd += 4 }
    $procedureOwner = if ($Fault -eq 'ProcedureOwner') { 1 } else { 0 }
    $table2Owner = if ($Fault -eq 'ContributionMismatch') { 1 } else { 0 }
    $contributions = if ($Fault -eq 'ContributionMismatch') {
        "  SC[.text]  | mod = 0, 0001:0, size = $($script:Table2-$script:SectionRva), data crc = 0, reloc crc = 0`n" +
        "  SC[.text]  | mod = 1, 0001:$($script:Table2-$script:SectionRva), size = 4096, data crc = 0, reloc crc = 0`n"
    } else {
        "  SC[.text]  | mod = 0, 0001:0, size = 8192, data crc = 0, reloc crc = 0`n"
    }
    $metadataText = "  Age: $($script:Age)`n  GUID: {$($script:Guid)}`n" +
        "  Mod 0000 | ``SyntheticControl.obj``:`n  Obj: ``SyntheticControl.obj```n" +
        "  Mod 0001 | ``OtherControl.obj``:`n  Obj: ``OtherControl.obj```n`nSection Contributions`n" +
        $contributions
    $symbolsText = "  Mod $procedureOwner | ``SyntheticControl.obj``:`n" +
        "  0 | S_GPROC32 [size = 16] ``TraceWindowHostMessageName`` `n" +
        "  addr = 0001:$($script:ProcBegin-$script:SectionRva), code size = $($procedureEnd-$script:ProcBegin)`n" +
        "  Mod 0 | ``SyntheticControl.obj``:`n" +
        "  0 | S_LDATA32 [size = 16] ```` `n  type = <no type>, addr = 0001:$($script:Table1-$script:SectionRva)`n" +
        "  Mod $table2Owner | ``$(if ($table2Owner -eq 1) {'OtherControl.obj'} else {'SyntheticControl.obj'})``:`n" +
        "  0 | S_LDATA32 [size = 16] ```` `n  type = <no type>, addr = 0001:$($script:Table2-$script:SectionRva)`n" +
        "  0 | S_LDATA32 [size = 16] ```` `n  type = <no type>, addr = 0001:$($script:Remap-$script:SectionRva)`n" +
        "  0 | S_LDATA32 [size = 16] ```` `n  type = <no type>, addr = 0001:$($script:LaterAnonymous-$script:SectionRva)`n" +
        "  Mod 0 | ``SyntheticControl.obj``:`n" +
        "  0 | S_GPROC32 [size = 16] ``LaterControlProcedure```n" +
        "  addr = 0001:$($script:LaterProcedure-$script:SectionRva), code size = 24`n"
    if ($Fault -eq 'TableExtent') {
        $symbolsText = $symbolsText.Replace("addr = 0001:$($script:Table2-$script:SectionRva)`n", "addr = 0001:$($script:Table2-$script:SectionRva+4)`n")
    }
    Set-Content -LiteralPath $metadata -Value $metadataText -Encoding utf8
    Set-Content -LiteralPath $symbols -Value $symbolsText -Encoding utf8
    Set-Content -LiteralPath $disassembly -Value (@($rows | Sort-Object {
        [UInt64]::Parse(($_ -split ':',2)[0], [Globalization.NumberStyles]::HexNumber)
    }) -join "`n") -Encoding utf8
    [IO.File]::WriteAllBytes($binary, $pe)
    return [pscustomobject]@{ Name=$Name; Binary=$binary; Metadata=$metadata; Symbols=$symbols; Disassembly=$disassembly }
}

function Assert-Outcome([string]$Name, [string]$Fault, [bool]$ExpectedPass,
    [int]$ExpectedInlineRows, [int]$ExpectedVectorViolations, [int]$ExpectedEvex = 0) {
    $case = New-Case $Name $Fault
    $result = [RedPrismBuild.IsaScanner]::Scan($case.Binary, $case.Metadata, $case.Symbols, $case.Disassembly,
        [string[]]@(), [string[]]@(), [string[]]@())
    if ($ExpectedPass -and $result.Violations.Count -ne 0) {
        throw "$Name expected pass, got: $($result.Violations -join '; ')"
    }
    if (-not $ExpectedPass -and $result.Violations.Count -eq 0) {
        throw "$Name expected a vector violation, but the scan passed."
    }
    if ($result.InlineTableVectorData -ne $ExpectedInlineRows) {
        throw "$Name expected $ExpectedInlineRows inline-table rows, got $($result.InlineTableVectorData)."
    }
    if ($result.VectorInstructions -ne $ExpectedVectorViolations -or
        $result.Violations.Count -ne $ExpectedVectorViolations) {
        throw "$Name expected $ExpectedVectorViolations vector instructions/violations, got $($result.VectorInstructions)/$($result.Violations.Count)."
    }
    if ($result.EvexInstructions -ne $ExpectedEvex) {
        throw "$Name expected $ExpectedEvex EVEX instructions, got $($result.EvexInstructions)."
    }
    Write-Host "PASS isa-split-tail-unsigned.$Name"
}
function Run-Control([string]$Name, [string]$Fault, [bool]$ExpectedPass,
    [int]$ExpectedInlineRows, [int]$ExpectedVectorViolations, [int]$ExpectedEvex = 0) {
    $script:ControlCount++
    try {
        Assert-Outcome $Name $Fault $ExpectedPass $ExpectedInlineRows $ExpectedVectorViolations $ExpectedEvex
    } catch {
        $script:Failures.Add("$Name :: $($_.Exception.Message)")
        Write-Host "FAIL isa-split-tail-unsigned.$Name :: $($_.Exception.Message)"
    }
}

# Independent expectations exercise the joint proof of the first
# dispatcher/table/remap and second guarded table. No hostile pair case assumes
# that the first table or remap has an independent proof.
$script:Failures = [Collections.Generic.List[string]]::new()
$script:ControlCount = 0
Run-Control 'ValidNearJaStackDword21' '' $true 29 0
$hostile = @(
    'GuardCompare', 'GuardMaximum', 'IndexSlot', 'BypassBranch',
    'ProcedureExtent', 'ProcedureOwner', 'ContributionMismatch',
    'TableExtent', 'TargetMidInstruction', 'UnprovenIndirectFlow',
    'EarlierTargetIntoCandidateGuard', 'RealVectorAdjacent'
)
foreach ($fault in $hostile) {
    $inlineExpected = if ($fault -eq 'RealVectorAdjacent') { 29 }
        else { 0 }
    $vectorExpected = if ($fault -eq 'RealVectorAdjacent') { 1 }
        elseif ($fault -in @('TargetMidInstruction','EarlierTargetIntoCandidateGuard')) { 28 }
        else { 29 }
    Run-Control ("Reject" + $fault) $fault $false $inlineExpected $vectorExpected
}
foreach ($fault in 'FirstCount','FirstSlot','FirstBase','FirstLoad','FirstTable','FirstDefault',
        'RemapIndexOutOfRange') {
    Run-Control ("Reject" + $fault) $fault $false 0 29
}
foreach ($rowIndex in 0..7) {
    foreach ($mutation in 'Missing','Duplicate','Unknown','Listing') {
        $fault = "FirstDispatchRow${rowIndex}${mutation}"
        Run-Control ("Reject" + $fault) $fault $false 0 29
    }
}
foreach ($fault in 'BodyJmpFirstIndex','BodyJmpFirstDispatch','BodyJmpSecondIndex','BodyJmpSecondDispatch',
        'BodyRowMissing','BodyRowDuplicate','BodyRowUnknown','BodyRowListing',
        'RetMissing','RetChanged','NopMissing','NopChanged') {
    Run-Control ("Reject" + $fault) $fault $false 0 29
}
foreach ($rowIndex in 0..6) {
    foreach ($mutation in 'Missing','Duplicate','Unknown','Listing') {
        $fault = "DispatchRow${rowIndex}${mutation}"
        Run-Control ("Reject" + $fault) $fault $false 0 29
    }
}
Run-Control 'GateBodyVector' 'BodyVector' $false 29 1
Run-Control 'GateRealEvexAdjacent' 'RealEvexAdjacent' $false 29 1 1
Write-Host "IsaSplitTailUnsigned controls executed: $($script:ControlCount) total; failures=$($script:Failures.Count)."
if ($script:ControlCount -ne 94) { throw 'Split unsigned tail control roster differs.' }
if ($script:Failures.Count -gt 0) { throw ($script:Failures -join "`n") }
