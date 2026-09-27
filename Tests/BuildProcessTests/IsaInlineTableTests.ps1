[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$fixtureRoot = Join-Path $repo ('.build/BuildProcessTests/IsaInlineTable-' + [guid]::NewGuid().ToString('N'))
[void](New-Item -ItemType Directory -Path $fixtureRoot -Force)
if (-not ('RedPrismBuild.IsaScanner' -as [type])) {
    Add-Type -Path (Join-Path $repo 'Build/IsaScan.cs')
}

# These byte arrays and listings are parser controls, not native code or CPU execution tests.
$imageBase = [UInt64]0x140000000
$sectionRva = [UInt32]0x0010f000
$tableRva = [UInt32]($sectionRva + 0x1000)
$dispatchRva = $sectionRva
$targetRva = [UInt32]0x0010f8c5
$guid = [guid]'9c1e2d4b-6a70-4a8b-9012-3456789abcde'
$age = [UInt32]7

function Set-U16([byte[]]$Bytes, [int]$Offset, [UInt16]$Value) {
    [Array]::Copy([BitConverter]::GetBytes($Value), 0, $Bytes, $Offset, 2)
}
function Set-U32([byte[]]$Bytes, [int]$Offset, [UInt32]$Value) {
    [Array]::Copy([BitConverter]::GetBytes($Value), 0, $Bytes, $Offset, 4)
}
function Set-U64([byte[]]$Bytes, [int]$Offset, [UInt64]$Value) {
    [Array]::Copy([BitConverter]::GetBytes($Value), 0, $Bytes, $Offset, 8)
}
function Put-Bytes([byte[]]$Bytes, [int]$Offset, [byte[]]$Value) {
    [Array]::Copy($Value, 0, $Bytes, $Offset, $Value.Length)
}
function Hex-Bytes([byte[]]$Bytes) {
    return (($Bytes | ForEach-Object { $_.ToString('x2') }) -join ' ')
}

function New-SyntheticCase {
    param(
        [Parameter(Mandatory)] [string]$Name,
        [string]$Fault = '',
        [switch]$OptionalByteIndex,
        [switch]$RealVex,
        [switch]$ShortNop,
        [switch]$R8Base,
        [ValidateSet(0,1,2)] [int]$TargetRegister = 0,
        [switch]$PdbData
    )
    $directory = Join-Path $fixtureRoot $Name
    [void](New-Item -ItemType Directory -Path $directory -Force)
    $binary = Join-Path $directory 'SyntheticIsaParserControl.exe'
    $metadata = Join-Path $directory 'contributions.txt'
    $symbols = Join-Path $directory 'symbols.txt'
    $disassembly = Join-Path $directory 'disassembly.txt'

    # One small PE32+ image, with a single executable section and an embedded RSDS identity.
    $pe = [byte[]]::new(0x2400)
    $pe[0] = 0x4d; $pe[1] = 0x5a
    Set-U32 $pe 0x3c 0x80
    Set-U32 $pe 0x80 0x00004550
    Set-U16 $pe 0x84 0x8664
    Set-U16 $pe 0x86 1
    Set-U16 $pe 0x94 0xf0
    Set-U16 $pe 0x98 0x20b
    Set-U64 $pe (0x98+24) $imageBase
    Set-U32 $pe (0x98+56) ($sectionRva + 0x2000)
    Set-U32 $pe (0x98+60) 0x400
    Set-U32 $pe (0x98+112+6*8) ($sectionRva + 0x180)
    Set-U32 $pe (0x98+116+6*8) 28
    $section = 0x80 + 24 + 0xf0
    Put-Bytes $pe $section ([Text.Encoding]::ASCII.GetBytes('.text' + [char]0 + [char]0 + [char]0))
    Set-U32 $pe ($section+8) 0x2000
    Set-U32 $pe ($section+12) $sectionRva
    Set-U32 $pe ($section+16) 0x2000
    Set-U32 $pe ($section+20) 0x400
    Set-U32 $pe ($section+36) 0x60000020
    # IMAGE_DEBUG_DIRECTORY: type CodeView, size 24, raw pointer 0x5a0.
    Set-U32 $pe (0x400+0x180+12) 2
    Set-U32 $pe (0x400+0x180+16) 24
    Set-U32 $pe (0x400+0x180+20) ($sectionRva + 0x1a0)
    Set-U32 $pe (0x400+0x180+24) 0x5a0
    Put-Bytes $pe 0x5a0 ([Text.Encoding]::ASCII.GetBytes('RSDS'))
    Put-Bytes $pe 0x5a4 $guid.ToByteArray()
    Set-U32 $pe 0x5b4 $age

    $lea = [byte[]]@(0x48,0x8d,0x0d,0,0,0,0)
    if ($R8Base) { $lea[0] = 0x4c; $lea[2] = 0x05 }
    if ($Fault -eq 'WrongImageBase') { $leaDisp = [Int32]($imageBase - ($imageBase + $dispatchRva + 7) + 1) }
    else { $leaDisp = [Int32]($imageBase - ($imageBase + $dispatchRva + 7)) }
    Put-Bytes $lea 3 ([BitConverter]::GetBytes($leaDisp))
    Put-Bytes $pe (0x400+$dispatchRva-$sectionRva) $lea
    $cursor = [UInt32]($dispatchRva + 7)
    $instructions = [Collections.Generic.List[string]]::new()
    $instructions.Add(('{0:x}: {1} lea rcx, [rip+imagebase]' -f ($imageBase+$dispatchRva), (Hex-Bytes $lea)))
    if ($OptionalByteIndex) {
        $indexLoad = [byte[]]@(0x0f,0xb6,0x84,0x01,0,0,0,0)
        Put-Bytes $indexLoad 4 ([BitConverter]::GetBytes([UInt32]0))
        if ($R8Base) {
            $indexLoad = [byte[]]@(0x41,0x0f,0xb6,0x84,0x00,0,0,0,0)
            if ($TargetRegister -eq 2) { $indexLoad[3] = 0x8c; $indexLoad[4] = 0x08 }
        }
        Put-Bytes $pe (0x400+$cursor-$sectionRva) $indexLoad
        $instructions.Add(('{0:x}: {1} movzx eax, byte ptr [rcx+rax]' -f ($imageBase+$cursor), (Hex-Bytes $indexLoad)))
        $cursor += $indexLoad.Length
    }
    $loadDisp = [UInt32]($tableRva -band 0xffffffff)
    $tableLoad = [byte[]]@(0x8b,0x84,0x81,0,0,0,0)
    Put-Bytes $tableLoad 3 ([BitConverter]::GetBytes($loadDisp))
    if ($R8Base) {
        $tableLoad = [byte[]]@(0x41,0x8b,(0x84+8*$TargetRegister),0x80,0,0,0,0)
        if ($TargetRegister -eq 2) { $tableLoad[3] = 0x88 }
        Put-Bytes $tableLoad 4 ([BitConverter]::GetBytes($loadDisp))
    }
    if ($Fault -eq 'BrokenSequence') { $tableLoad[0] = 0x89 }
    Put-Bytes $pe (0x400+$cursor-$sectionRva) $tableLoad
    $instructions.Add(('{0:x}: {1} mov eax, dword ptr [rcx+rax*4+table]' -f ($imageBase+$cursor), (Hex-Bytes $tableLoad)))
    $cursor += $tableLoad.Length
    $add = [byte[]]@(0x48,0x03,0xc1)
    if ($R8Base) { $add = [byte[]]@(0x49,0x03,(0xc0+8*$TargetRegister)) }
    if ($Fault -eq 'WrongAddRegister') { $add[2] = 0xd8 }
    Put-Bytes $pe (0x400+$cursor-$sectionRva) $add
    $instructions.Add(('{0:x}: {1} add rax, rcx' -f ($imageBase+$cursor), (Hex-Bytes $add)))
    $cursor += 3
    $jump = [byte[]]@(0xff,0xe0)
    if ($R8Base) { $jump[1] += $TargetRegister }
    if ($Fault -eq 'WrongJumpRegister') { $jump[1] = 0xe3 }
    Put-Bytes $pe (0x400+$cursor-$sectionRva) $jump
    $instructions.Add(('{0:x}: {1} jmp rax' -f ($imageBase+$cursor), (Hex-Bytes $jump)))
    $afterJump = [UInt32]($cursor + 2)

    $actualTableRva = $tableRva
    $target = $targetRva
    if ($Fault -eq 'TargetBeforeDispatch') { $target = $dispatchRva }
    $tableBytes = [byte[]]::new(8)
    Put-Bytes $tableBytes 0 ([BitConverter]::GetBytes($target))
    Put-Bytes $tableBytes 4 ([BitConverter]::GetBytes($targetRva))
    if ($Fault -eq 'NoRet' -or $PdbData) { $pe[0x400+$actualTableRva-$sectionRva-1] = 0x90 }
    elseif ($ShortNop) {
        $pe[0x400+$actualTableRva-$sectionRva-3] = 0xc3
        $pe[0x400+$actualTableRva-$sectionRva-2] = 0x66
        $pe[0x400+$actualTableRva-$sectionRva-1] = 0x90
    } else { $pe[0x400+$actualTableRva-$sectionRva-1] = 0xc3 }
    Put-Bytes $pe (0x400+$actualTableRva-$sectionRva) $tableBytes

    $dataEnd = [UInt32]($actualTableRva + 8)
    $contributionEnd = [UInt32]0x2000
    $splitTableContribution = $Fault -eq 'CrossContribution'
    if ($Fault -eq 'ContributionBounds') { $contributionEnd = [UInt32]($actualTableRva - $sectionRva + 4) }
    $contributions = if ($splitTableContribution) {
        "  SC[.text]  | mod = 0, 0001:0, size = $($actualTableRva - $sectionRva), data crc = 0, reloc crc = 0`n" +
        "  SC[.text]  | mod = 1, 0001:$($actualTableRva - $sectionRva), size = 4096, data crc = 0, reloc crc = 0`n"
    } else {
        "  SC[.text]  | mod = 0, 0001:0, size = $contributionEnd, data crc = 0, reloc crc = 0`n"
    }
    $moduleHeader = "  Mod 0000 | ``SyntheticControl.obj``:`n  Obj: ``SyntheticControl.obj``:`n"
    if ($splitTableContribution) { $moduleHeader += "  Mod 0001 | ``SyntheticTable.obj``:`n  Obj: ``SyntheticTable.obj``:`n" }
    $metadataText = "  Age: $age`n  GUID: {$guid}`n$moduleHeader`nSection Contributions`n$contributions"
    Set-Content -LiteralPath $metadata -Value $metadataText -Encoding utf8
    $symbolText = ''
    if ($PdbData -and $Fault -ne 'MissingDataSymbol') {
        $dataModule = if ($Fault -eq 'WrongDataOwner') { 1 } else { 0 }
        $dataOffset = $actualTableRva - $sectionRva
        if ($Fault -eq 'WrongDataStart') { $dataOffset += 4 }
        $symbolText = "  Mod $dataModule | ``SyntheticControl.obj``:`n  0 | S_LDATA32 [size = 16] ```` `n".TrimEnd() +
            "`n  type = <no type>, addr = 0001:$dataOffset`n"
    }
    Set-Content -LiteralPath $symbols -Value $symbolText -Encoding utf8

    # The target is a decoded boundary only in the valid case; middle-target deliberately lands inside it.
    $targetInstruction = [byte[]]@(0x66,0x90)
    $targetStart = if ($Fault -eq 'MiddleOfInstruction') { $targetRva - 1 } else { $targetRva }
    Put-Bytes $pe (0x400+$targetStart-$sectionRva) $targetInstruction
    if ($Fault -eq 'UnknownTargetInstruction') { $pe[0x400+$targetStart-$sectionRva] = 0x06 }
    if ($RealVex) { Put-Bytes $pe (0x400+0x20) ([byte[]]@(0xc5,0xf8,0x10,0x00)) }
    $rows = [Collections.Generic.List[string]]::new()
    foreach ($row in $instructions) { $rows.Add($row) }
    if ($RealVex) { $rows.Add(('{0:x}: c5 f8 10 00 vmovups xmm0, xmmword ptr [rax]' -f ($imageBase+$dispatchRva+0x20))) }
    if ($Fault -eq 'UnknownTargetInstruction') {
        $rows.Add(('{0:x}: 06 <unknown>' -f ($imageBase+$targetRva)))
    } elseif ($Fault -ne 'UndecodedTarget' -and $Fault -ne 'MiddleOfInstruction') {
        $rows.Add(('{0:x}: 66 90 nop' -f ($imageBase+$targetRva)))
    }
    if ($Fault -eq 'MiddleOfInstruction') {
        $rows.Add(('{0:x}: 66 90 nop' -f ($imageBase+$targetRva-1)))
    }
    $rows.Add(('{0:x}: {1} vmovups xmm0, xmmword ptr [rax]' -f ($imageBase+$tableRva), (Hex-Bytes $tableBytes[0..3])))
    $rows.Add(('{0:x}: {1} vmovups xmm0, xmmword ptr [rax]' -f ($imageBase+$tableRva+4), (Hex-Bytes $tableBytes[4..7])))
    if ($Fault -eq 'InstructionExtendsPastTable') {
        # Deliberately substitute a synthetic decoded span; the scanner must bound the entire row.
        $pe[0x400+$actualTableRva-$sectionRva+8] = 0xc4
        $rows[$rows.Count-1] = ('{0:x}: c4 e1 78 58 c4 vaddps xmm0, xmm0, xmm4' -f ($imageBase+$tableRva+4))
    }
    Set-Content -LiteralPath $disassembly -Value ($rows -join "`n") -Encoding utf8
    [IO.File]::WriteAllBytes($binary, $pe)
    return @{ Name=$Name; Binary=$binary; Metadata=$metadata; Symbols=$symbols; Disassembly=$disassembly; Target=$targetRva; TableEnd=$dataEnd }
}

function Invoke-Control($Case, [bool]$ExpectedPass, [int]$ExpectedInline = -1, [int]$ExpectedVectors = -1) {
    $result = [RedPrismBuild.IsaScanner]::Scan($Case.Binary, $Case.Metadata, $Case.Symbols, $Case.Disassembly,
        [string[]]@(), [string[]]@(), [string[]]@())
    if ($ExpectedPass -and $result.Violations.Count -ne 0) {
        throw "$($Case.Name) unexpectedly violated the ISA gate: $($result.Violations -join '; ')"
    }
    if (-not $ExpectedPass -and $result.Violations.Count -eq 0) {
        throw "$($Case.Name) bypassed the ISA gate."
    }
    if ($ExpectedInline -ge 0 -and $result.InlineTableVectorData -ne $ExpectedInline) {
        throw "$($Case.Name): expected $ExpectedInline inline-table vector rows, got $($result.InlineTableVectorData)."
    }
    if ($ExpectedVectors -ge 0 -and $result.VectorInstructions -ne $ExpectedVectors) {
        throw "$($Case.Name): expected $ExpectedVectors ordinary vector rows, got $($result.VectorInstructions)."
    }
    Write-Host "PASS isa-inline-table.$($Case.Name)"
}

$good = New-SyntheticCase Positive -OptionalByteIndex -ShortNop
Invoke-Control $good $true 2 0
$optionalOff = New-SyntheticCase PositiveWithoutByteIndex
Invoke-Control $optionalOff $true 2 0
$mixed = New-SyntheticCase RealVexBesideTable -RealVex
Invoke-Control $mixed $false 2 1

foreach ($targetRegister in @(0,1,2)) {
    $case = New-SyntheticCase "R8DataTarget$targetRegister" -R8Base -PdbData -OptionalByteIndex -TargetRegister $targetRegister
    Invoke-Control $case $true 2 0
}
$case = New-SyntheticCase R8DataNoRemap -R8Base -PdbData
Invoke-Control $case $true 2 0
$case = New-SyntheticCase R8DataWithRealVex -R8Base -PdbData -RealVex
Invoke-Control $case $false 2 1
foreach ($fault in @('MissingDataSymbol','WrongDataOwner','WrongDataStart','WrongAddRegister','WrongJumpRegister',
        'CrossContribution','ContributionBounds','MiddleOfInstruction','UnknownTargetInstruction','InstructionExtendsPastTable')) {
    $case = New-SyntheticCase "R8Data$fault" -R8Base -PdbData -OptionalByteIndex -TargetRegister 1 -Fault $fault
    Invoke-Control $case $false
}

foreach ($name in @('WrongImageBase','BrokenSequence','CrossContribution','ContributionBounds',
        'MiddleOfInstruction','UndecodedTarget','UnknownTargetInstruction','TargetBeforeDispatch','NoRet','InstructionExtendsPastTable')) {
    $case = New-SyntheticCase $name -Fault $name
    Invoke-Control $case $false
}

Write-Host "Synthetic ISA parser fixtures: $fixtureRoot"
$global:LASTEXITCODE = 0
