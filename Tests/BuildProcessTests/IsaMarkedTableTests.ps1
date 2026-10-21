[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$fixtureRoot = Join-Path $repo ('.build/BuildProcessTests/IsaMarkedTable-' + [guid]::NewGuid().ToString('N'))
[void](New-Item -ItemType Directory -Path $fixtureRoot -Force)
if (-not ('RedPrismBuild.IsaScanner' -as [type])) {
    Add-Type -Path (Join-Path $repo 'Build/IsaScan.cs')
}
Import-Module (Join-Path $repo 'Build/IsaScan.psm1') -Force
$objdump = (Find-RedPrismIsaTools).Objdump

# Controls of the PDB-marked table proof (ReadPdbProvenTables in Build/IsaScan.cs). Each fixture is a small PE32+ image with one
# contribution per procedure, its PDB records in llvm-pdbutil's text form, and its listing from the llvm-objdump the gate runs,
# so a table decodes in each layout as it would in a real scan. They are parser controls, not CPU execution tests.
#
# The switch is DxUi's MenuPointerKindFromMessage as linked into RedPrism.exe on 2026-10-02 (Release x64, DxUi 271bd54): the
# dispatch (R8 image base, RDX index, EDX target), six cases, the default, a three-byte NOP and fifteen image-relative entries.
# At 0x1402a4a70 the default's address ends in C5, so default entries decode as vcvtsi2ss and no dispatch recognizer proves
# the table: that image failed the gate.
$imageBase = [UInt64]0x140000000
$guid = [guid]'9c1e2d4b-6a70-4a8b-9012-3456789abcde'
$age = [UInt32]7
$caseOffsets = @(0x1f, 0x28, 0x31, 0x3a, 0x43, 0x4c)
$defaultOffset = 0x55
$entryOffsets = @(0x1f, 0x28, 0x31, 0x55, 0x3a, 0x43, 0x55, 0x55, 0x55, 0x55, 0x4c, 0x55, 0x55, 0x55, 0x4c)
$nextOffset = 0xb0
# In the observed layout the linear decoder reads CWDE at the table's second DWORD: a decoded boundary that is table data.
$rowInTable = 0x64

function Set-U16([byte[]]$Bytes, [int]$Offset, [UInt16]$Value) { [Array]::Copy([BitConverter]::GetBytes($Value), 0, $Bytes, $Offset, 2) }
function Set-U32([byte[]]$Bytes, [int]$Offset, [UInt32]$Value) { [Array]::Copy([BitConverter]::GetBytes($Value), 0, $Bytes, $Offset, 4) }
function Set-U64([byte[]]$Bytes, [int]$Offset, [UInt64]$Value) { [Array]::Copy([BitConverter]::GetBytes($Value), 0, $Bytes, $Offset, 8) }
function Put-Bytes([byte[]]$Bytes, [int]$Offset, [byte[]]$Value) { [Array]::Copy($Value, 0, $Bytes, $Offset, $Value.Length) }

# A PE32+ image with 512-byte section and file alignment: .text from `$TextRva` (INT3 filled, the code at file offset 0x400) and
# .rdata holding the CodeView record.
function New-Image([object[]]$Code, [UInt32]$TextRva, [UInt32]$TextSize) {
    $rdataRva = [UInt32]($TextRva + $TextSize)
    $pe = [byte[]]::new(0x400 + $TextSize + 0x200)
    $pe[0] = 0x4d; $pe[1] = 0x5a; Set-U32 $pe 0x3c 0x80; Set-U32 $pe 0x80 0x4550
    Set-U16 $pe 0x84 0x8664; Set-U16 $pe 0x86 2; Set-U16 $pe 0x94 0xf0; Set-U16 $pe 0x96 0x22
    $optional = 0x98
    Set-U16 $pe $optional 0x20b; Set-U32 $pe ($optional + 4) $TextSize; Set-U32 $pe ($optional + 16) $TextRva
    Set-U32 $pe ($optional + 20) $TextRva; Set-U64 $pe ($optional + 24) $imageBase; Set-U32 $pe ($optional + 32) 0x200
    Set-U32 $pe ($optional + 36) 0x200; Set-U16 $pe ($optional + 40) 6; Set-U16 $pe ($optional + 48) 6
    Set-U32 $pe ($optional + 56) ($rdataRva + 0x200); Set-U32 $pe ($optional + 60) 0x400; Set-U16 $pe ($optional + 68) 3
    Set-U32 $pe ($optional + 108) 16; Set-U32 $pe ($optional + 112 + 6 * 8) $rdataRva; Set-U32 $pe ($optional + 116 + 6 * 8) 28
    $section = 0x80 + 24 + 0xf0
    Put-Bytes $pe $section ([Text.Encoding]::ASCII.GetBytes(".text`0`0`0"))
    Set-U32 $pe ($section + 8) $TextSize; Set-U32 $pe ($section + 12) $TextRva; Set-U32 $pe ($section + 16) $TextSize
    Set-U32 $pe ($section + 20) 0x400; Set-U32 $pe ($section + 36) 0x60000020
    $section += 40
    Put-Bytes $pe $section ([Text.Encoding]::ASCII.GetBytes(".rdata`0`0"))
    Set-U32 $pe ($section + 8) 0x200; Set-U32 $pe ($section + 12) $rdataRva; Set-U32 $pe ($section + 16) 0x200
    Set-U32 $pe ($section + 20) (0x400 + $TextSize); Set-U32 $pe ($section + 36) 0x40000040
    $rdata = [int](0x400 + $TextSize)
    Set-U32 $pe ($rdata + 12) 2; Set-U32 $pe ($rdata + 16) 24; Set-U32 $pe ($rdata + 20) ($rdataRva + 0x40)
    Set-U32 $pe ($rdata + 24) ($rdata + 0x40)
    Put-Bytes $pe ($rdata + 0x40) ([Text.Encoding]::ASCII.GetBytes('RSDS')); Put-Bytes $pe ($rdata + 0x44) $guid.ToByteArray()
    Set-U32 $pe ($rdata + 0x54) $age
    for ($i = 0; $i -lt $TextSize; ++$i) { $pe[0x400 + $i] = 0xcc }
    foreach ($block in $Code) { Put-Bytes $pe (0x400 + [int]($block.Rva - $TextRva)) ([byte[]]$block.Bytes) }
    return , $pe
}

# Writes the image, its contributions and symbols, and the llvm-objdump listing. `$Spec` holds Code (@{ Rva; Bytes }), Procedures
# (@{ Name; Rva; Size; Module } with optional ContributionSize, NoContribution and NoRecord) and Marks (@{ Rva; Module } with
# optional Name and Type). .text covers the code blocks, from the 512-byte boundary before the first to at least 32 bytes after the
# last, so the listing holds only them and INT3 rows around them.
function New-Case([string]$Name, [hashtable]$Spec) {
    $first = [UInt32]::MaxValue; $last = [UInt32]0
    foreach ($block in $Spec.Code) {
        $first = [Math]::Min($first, [UInt32]$block.Rva); $last = [Math]::Max($last, [UInt32]($block.Rva + $block.Bytes.Length))
    }
    $text = [UInt32]($first -band 0xfffffe00)
    $directory = Join-Path $fixtureRoot $Name
    [void](New-Item -ItemType Directory -Path $directory -Force)
    $binary = Join-Path $directory 'MarkedTableControl.exe'
    [IO.File]::WriteAllBytes($binary, (New-Image $Spec.Code $text ([UInt32](($last + 0x21f) -band 0xfffffe00) - $text)))
    $objects = @('DxUi.Menu.obj', 'Other.obj')
    $metadata = [Collections.Generic.List[string]]::new()
    $metadata.AddRange([string[]]@("  Age: $age", "  GUID: {$guid}"))
    foreach ($module in 0, 1) { $metadata.AddRange([string[]]@("  Mod $module | ``$($objects[$module])``:", '  Obj: `DxUi.lib`:')) }
    $metadata.AddRange([string[]]@('', 'Section Contributions'))
    foreach ($procedure in @($Spec.Procedures | Where-Object { -not $_.ContainsKey('NoContribution') } | Sort-Object { $_.Rva })) {
        $size = if ($procedure.ContainsKey('ContributionSize')) { $procedure.ContributionSize } else { $procedure.Size }
        $metadata.Add("  SC[.text] | mod = 0, 0001:$($procedure.Rva - $text), size = $size, data crc = 0, reloc crc = 0")
    }
    $symbols = [Collections.Generic.List[string]]::new()
    foreach ($module in 0, 1) {
        $symbols.Add("  Mod $module | ``$($objects[$module])``:")
        foreach ($procedure in @($Spec.Procedures | Where-Object { $_.Module -eq $module -and -not $_.ContainsKey('NoRecord') })) {
            $symbols.Add("  4 | S_LPROC32 [size = 96] ``$($procedure.Name)``")
            $symbols.Add("      parent = 0, end = 0, addr = 0001:$($procedure.Rva - $text), code size = $($procedure.Size)")
        }
        foreach ($mark in @($Spec.Marks | Where-Object { $_.Module -eq $module })) {
            $label = if ($mark.ContainsKey('Name')) { $mark.Name } else { '' }
            $type = if ($mark.ContainsKey('Type')) { $mark.Type } else { '<no type>' }
            $symbols.Add("  8 | S_LDATA32 [size = 16] ``$label``")
            $symbols.Add("      type = $type, addr = 0001:$($mark.Rva - $text)")
        }
    }
    $case = @{ Name = $Name; Binary = $binary; Metadata = (Join-Path $directory 'contributions.txt')
        Symbols = (Join-Path $directory 'symbols.txt'); Disassembly = (Join-Path $directory 'disassembly.txt') }
    Set-Content -LiteralPath $case.Metadata -Value $metadata -Encoding utf8
    Set-Content -LiteralPath $case.Symbols -Value $symbols -Encoding utf8
    & $objdump -d --x86-asm-syntax=intel $binary > $case.Disassembly
    if ($LASTEXITCODE -ne 0) { throw "llvm-objdump failed on the $Name control." }
    return $case
}

# The observed switch at `$At` (an RVA), its LEA and table displacements computed for that address.
function New-SwitchBytes([UInt32]$At, [int[]]$Entries, [byte[]]$Alignment) {
    $table = 0x5d + $Alignment.Length
    $bytes = [Collections.Generic.List[byte]]::new()
    $bytes.AddRange([byte[]]@(0x81, 0xc2, 0x00, 0xfe, 0xff, 0xff, 0x83, 0xfa, 0x0e, 0x77, 0x4a, 0x4c, 0x8d, 0x05))
    $bytes.AddRange([BitConverter]::GetBytes([Int32](-([Int64]$At + 0x12))))
    $bytes.AddRange([byte[]]@(0x41, 0x8b, 0x94, 0x90)); $bytes.AddRange([BitConverter]::GetBytes([UInt32]($At + $table)))
    $bytes.AddRange([byte[]]@(0x49, 0x03, 0xd0, 0xff, 0xe2))
    for ($case = 0; $case -lt 6; ++$case) { $bytes.AddRange([byte[]]@(0x66, 0xc7, 0x01, [byte]$case, 0x01, 0x48, 0x8b, 0xc1, 0xc3)) }
    $bytes.AddRange([byte[]]@(0xc6, 0x41, 0x01, 0x00, 0x48, 0x8b, 0xc1, 0xc3))
    $bytes.AddRange($Alignment)
    foreach ($entry in $Entries) { $bytes.AddRange([BitConverter]::GetBytes([UInt32]($At + $entry))) }
    return , $bytes.ToArray()
}

# The switch at `$At` between a previous and a next procedure of the same object. `$Fault` changes exactly one thing.
function New-SwitchSpec([UInt32]$At, [string]$Fault = '') {
    $entries = [int[]]$entryOffsets.Clone()
    $alignment = [byte[]]@(0x0f, 0x1f, 0x00)
    switch ($Fault) {
        'EntryBeforeProcedure' { $entries[7] = -0x10 }
        'EntryInNextProcedure' { $entries[7] = $nextOffset }
        'EntryIntoTable' { $entries[7] = $rowInTable }
        'EntryMidInstruction' { $entries[7] = $caseOffsets[0] + 1 }
        'TableNotDwordAligned' { $alignment = [byte[]]@(0x90) }
    }
    $table = 0x5d + $alignment.Length
    $code = New-SwitchBytes $At $entries $alignment
    if ($Fault -eq 'RealVexInsteadOfEntry') { Put-Bytes $code ($table + 4 * 7) ([byte[]]@(0xc5, 0xf8, 0x77, 0x90)) }
    if ($Fault -eq 'RealVexBeforeTable') { Put-Bytes $code $defaultOffset ([byte[]]@(0xc5, 0xf8, 0x77, 0x90)) }
    if ($Fault -in 'SecondTable', 'EntryIntoEarlierTable') {
        # A second table after the first; its third entry leads into code, or into the first table. In this layout the decoder
        # reads its second entry as vcvtsi2ss.
        $last = if ($Fault -eq 'SecondTable') { $caseOffsets[0] } else { $rowInTable }
        foreach ($entry in @($defaultOffset, $defaultOffset, $last)) { $code = [byte[]]($code + [BitConverter]::GetBytes([UInt32]($At + $entry))) }
    }
    $size = $code.Length
    $next = [byte[]]@(0x0f, 0x1f, 0x00, 0x48, 0x83, 0xec, 0x28, 0x48, 0x83, 0xc4, 0x28, 0xc3)
    if ($Fault -eq 'RealVexAfterProcedure') { Put-Bytes $next 0 ([byte[]]@(0xc5, 0xf8, 0x77)) }
    $recorded = $size
    if ($Fault -eq 'ProcedureEndsOneByteEarly') { $recorded = $size - 1 }
    if ($Fault -eq 'ProcedureEndsOneByteLate') { $recorded = $size + 1 }
    if ($Fault -eq 'SingleEntryRange') { $recorded = $size - 4 }
    $switchProcedure = @{ Name = 'DxUi::`anonymous namespace''::MenuPointerKindFromMessage'; Rva = $At; Size = $recorded
        Module = 0; ContributionSize = $size + 1 }
    if ($Fault -eq 'NoProcedure') { $switchProcedure.NoRecord = $true }
    if ($Fault -eq 'ProcedureOtherModule') { $switchProcedure.Module = 1 }
    $procedures = @(
        @{ Name = 'PreviousProcedure'; Rva = [UInt32]($At - 0x10); Size = 3; Module = 0 },
        $switchProcedure,
        @{ Name = 'NextProcedure'; Rva = [UInt32]($At + $nextOffset); Size = $next.Length; Module = 0 })
    if ($Fault -eq 'TwoProcedures') {
        # A second procedure record over the table inside the same contribution: the table has no single holder.
        $procedures += @{ Name = 'Overlapping'; Rva = [UInt32]($At + $caseOffsets[0]); Size = $size - $caseOffsets[0]; Module = 0
            NoContribution = $true }
    }
    $marks = @(@{ Rva = [UInt32]($At + $table); Module = 0 })
    switch ($Fault) {
        'NoMark' { $marks = @() }
        'NamedMark' { $marks[0].Name = 'Table' }
        'TypedMark' { $marks[0].Type = '0x1000' }
        'MarkOneByteEarly' { $marks[0].Rva-- }
        'MarkOneByteLate' { $marks[0].Rva++ }
        'MarkOtherModule' { $marks[0].Module = 1 }
        'SingleEntryRange' { $marks += @{ Rva = [UInt32]($At + $table + 4 * 13); Module = 0 } }
    }
    if ($Fault -in 'SecondTable', 'EntryIntoEarlierTable') { $marks += @{ Rva = [UInt32]($At + $table + 4 * $entries.Length); Module = 0 } }
    return @{
        Code = @(@{ Rva = [UInt32]($At - 0x10); Bytes = [byte[]]@(0x33, 0xc0, 0xc3) }, @{ Rva = $At; Bytes = $code },
            @{ Rva = [UInt32]($At + $nextOffset); Bytes = $next })
        Procedures = $procedures; Marks = $marks }
}

# A procedure at `$At` of two-byte NOPs (the branch targets), a DWORD table of `$Entries` entries and the byte index table after it.
function New-IndexedSpec([int]$Entries, [byte[]]$Indices, [string]$Fault = '', [UInt32]$At = 0x2a5000) {
    $bytes = [Collections.Generic.List[byte]]::new()
    for ($i = 0; $i -lt 4; ++$i) { $bytes.AddRange([byte[]]@(0x66, 0x90)) }
    $bytes.Add(0xc3); while ($bytes.Count -lt 0x10) { $bytes.Add(0xcc) }
    for ($i = 0; $i -lt $Entries; ++$i) { $bytes.AddRange([BitConverter]::GetBytes([UInt32]($At + 2 * ($i % 4)))) }
    $marks = @(@{ Rva = [UInt32]($At + 0x10); Module = 0 })
    if ($Fault -eq 'MarkedGapBeforeIndices') {
        $marks += @{ Rva = [UInt32]($At + $bytes.Count); Module = 0 }
        $bytes.AddRange([byte[]]@(0xff, 0xff, 0xff, 0xff))
    }
    if ($Fault -ne 'UnmarkedIndices') { $marks += @{ Rva = [UInt32]($At + $bytes.Count); Module = 0 } }
    if ($Fault -eq 'IndicesMarkedInOtherModule') { $marks[-1].Module = 1 }
    $bytes.AddRange($Indices)
    $procedures = @(@{ Name = 'IndexedSwitch'; Rva = $At; Size = $bytes.Count; Module = 0 })
    if ($Fault -eq 'IndicesStartNextProcedure') {
        # The DWORD table ends its procedure; the next procedure of the same contribution begins with the marked bytes.
        $procedures[0].Size = 0x10 + 4 * $Entries
        $procedures[0].ContributionSize = $bytes.Count
        $procedures += @{ Name = 'Following'; Rva = [UInt32]($At + 0x10 + 4 * $Entries); Size = $Indices.Length; Module = 0
            NoContribution = $true }
    }
    return @{ Code = @(@{ Rva = $At; Bytes = $bytes.ToArray() }); Procedures = $procedures; Marks = $marks }
}

# At RVA 0xc5c000 every entry of a twenty-entry table reads 12 C0 C5 00: the decoder reads ADC AL,AL and then VEX rows that each
# start in one entry and end in the next (C5 00 12 C0, vmovhlps), so the last one ends in the index table (C5 00 12 00, vmovlps).
# Such a row is table data only when the index table it reaches is proved too.
function New-StraddleSpec([byte[]]$Indices) {
    $at = [UInt32]0xc5c000
    $bytes = [Collections.Generic.List[byte]]::new()
    for ($i = 0; $i -lt 0x43; ++$i) { $bytes.Add(0x90) }
    $bytes.Add(0xc3)
    for ($i = 0; $i -lt 20; ++$i) { $bytes.AddRange([BitConverter]::GetBytes([UInt32]($at + 0x12))) }
    $indexTable = [UInt32]($at + $bytes.Count)
    $bytes.AddRange($Indices)
    return @{ Code = @(@{ Rva = $at; Bytes = $bytes.ToArray() })
        Procedures = @(@{ Name = 'StraddledSwitch'; Rva = $at; Size = $bytes.Count; Module = 0 })
        Marks = @(@{ Rva = [UInt32]($at + 0x44); Module = 0 }, @{ Rva = $indexTable; Module = 0 }) }
}

function Invoke-Control([string]$Name, [hashtable]$Spec, [bool]$ExpectedPass, [int]$ExpectedTableRows = -1,
    [switch]$UnprovedByRecognizers) {
    $case = New-Case $Name $Spec
    $gate = [RedPrismBuild.IsaScanner]::Scan($case.Binary, $case.Metadata, $case.Symbols, $case.Disassembly,
        [string[]]@(), [string[]]@(), [string[]]@())
    if ($ExpectedPass -and $gate.Violations.Count -ne 0) {
        throw "$Name violated the ISA gate: $($gate.Violations -join '; ')"
    }
    if (-not $ExpectedPass -and $gate.Violations.Count -eq 0) { throw "$Name bypassed the ISA gate." }
    if ($ExpectedTableRows -ge 0 -and $gate.InlineTableVectorData -ne $ExpectedTableRows) {
        throw "${Name}: expected $ExpectedTableRows proved table rows that decode as vector instructions, got $($gate.InlineTableVectorData)."
    }
    if ($UnprovedByRecognizers) {
        $alone = [RedPrismBuild.IsaScanner]::Scan($case.Binary, $case.Metadata, $case.Symbols, $case.Disassembly,
            [string[]]@(), [string[]]@(), [string[]]@(), $false)
        if ($alone.Violations.Count -eq 0) { throw "${Name}: the dispatch recognizers alone already prove this table." }
    }
    Write-Host "PASS isa-marked-table.$Name$(if (-not $ExpectedPass) { ' (expected rejection)' })"
    $script:controls++
}

$controls = 0
$observed = [UInt32]0x2a4a70
# At this address no entry decodes as a vector instruction, so a genuine one is the only vector row.
$plain = [UInt32]0x2a4a80
# With 200 DWORD entries the indices C5 4A 2A 00, C4 41 7A 2A 00 and 62 61 7C 48 58 00 are valid and decode as vcvtsi2ss,
# vcvtsi2ss and an EVEX vaddps.
$vector = [byte[]]@(0xc5, 0x4a, 0x2a, 0x00, 0xc4, 0x41, 0x7a, 0x2a, 0x00, 0x62, 0x61, 0x7c, 0x48, 0x58, 0x00, 0x01)

# The observed image: default entries decode as vcvtsi2ss, the table is proved from its mark and content, and the dispatch
# recognizers alone do not prove it (the failure this proof closes). Wherever the linker puts the switch it stays proved; the
# number of rows the decoder reads as vector instructions depends on the address.
Invoke-Control 'Observed' (New-SwitchSpec $observed) $true 6 -UnprovedByRecognizers
Invoke-Control 'MovedSameLowByte' (New-SwitchSpec ([UInt32]0x2a5b70)) $true 7 -UnprovedByRecognizers
Invoke-Control 'MovedEarlier' (New-SwitchSpec ([UInt32]0x2a4270)) $true 7 -UnprovedByRecognizers
Invoke-Control 'MovedNoVectorBytes' (New-SwitchSpec $plain) $true 0
Invoke-Control 'SecondTable' (New-SwitchSpec $observed 'SecondTable') $true 7 -UnprovedByRecognizers
$menuSwitch = New-SwitchSpec $observed
$indexed = New-IndexedSpec 200 $vector
Invoke-Control 'TablesInTwoProcedures' @{ Code = @($menuSwitch.Code + $indexed.Code)
    Procedures = @($menuSwitch.Procedures + $indexed.Procedures); Marks = @($menuSwitch.Marks + $indexed.Marks) } $true 9
foreach ($fault in @('NoMark', 'NamedMark', 'TypedMark', 'MarkOneByteEarly', 'MarkOneByteLate', 'MarkOtherModule',
        'NoProcedure', 'ProcedureOtherModule', 'ProcedureEndsOneByteEarly', 'ProcedureEndsOneByteLate', 'TwoProcedures',
        'SingleEntryRange', 'TableNotDwordAligned', 'EntryBeforeProcedure', 'EntryInNextProcedure', 'EntryIntoTable',
        'EntryIntoEarlierTable', 'EntryMidInstruction')) {
    Invoke-Control $fault (New-SwitchSpec $observed $fault) $false
}
# Genuine vector instructions: in the marked range, in the procedure's code, after it.
foreach ($fault in @('RealVexInsteadOfEntry', 'RealVexBeforeTable', 'RealVexAfterProcedure')) {
    Invoke-Control $fault (New-SwitchSpec $plain $fault) $false
}

# Byte index tables.
Invoke-Control 'Indices' (New-IndexedSpec 200 $vector) $true 3
$tooLarge = [byte[]]$vector.Clone(); $tooLarge[15] = 200
Invoke-Control 'IndexAtEntryCount' (New-IndexedSpec 200 $tooLarge) $false
foreach ($fault in @('UnmarkedIndices', 'IndicesMarkedInOtherModule', 'MarkedGapBeforeIndices', 'IndicesStartNextProcedure')) {
    Invoke-Control $fault (New-IndexedSpec 200 $vector $fault) $false
}
# The alignment that ends a DWORD-aligned index range is not an index; each form was observed in RedPrism.exe. Any other trailing
# byte, or a NOP that does not end on a DWORD boundary, is an index. The EVEX indices 62 61 74 48 58 00 (vaddps) are valid with
# 120 entries and 62 61 04 48 58 00 with 100, where 66 is not; 90 is valid with neither.
$evex = [byte[]]@(0x62, 0x61, 0x74, 0x48, 0x58, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06)
$evexBelow100 = [byte[]]@(0x62, 0x61, 0x04, 0x48, 0x58, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06)
Invoke-Control 'AlignedByNop' (New-IndexedSpec 120 ([byte[]]($evex + [byte[]]@(0x07, 0x08, 0x09, 0x90)))) $true 1
Invoke-Control 'AlignedByTwoByteNop' (New-IndexedSpec 100 ([byte[]]($evexBelow100 + [byte[]]@(0x07, 0x08, 0x66, 0x90)))) $true 1
Invoke-Control 'TrailingByteNotNop' (New-IndexedSpec 120 ([byte[]]($evex + [byte[]]@(0x07, 0x08, 0x09, 0x91)))) $false
Invoke-Control 'NopNotAtDwordEnd' (New-IndexedSpec 120 ([byte[]]($evex + [byte[]]@(0x07, 0x08, 0x90)))) $false
# With twenty entries 1F is not a valid index: only the three-byte alignment rule proves this index table, and without that
# proof the row that reaches into it is a violation.
$straddled = [byte[]]@(0x12, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x0f, 0x1f, 0x00)
Invoke-Control 'AlignedByThreeByteNop' (New-StraddleSpec $straddled) $true 20 -UnprovedByRecognizers
$straddledInvalid = [byte[]]$straddled.Clone(); $straddledInvalid[2] = 20
Invoke-Control 'RowIntoUnprovedIndices' (New-StraddleSpec $straddledInvalid) $false
Write-Host "PASS isa-marked-table: $controls controls"
$global:LASTEXITCODE = 0
