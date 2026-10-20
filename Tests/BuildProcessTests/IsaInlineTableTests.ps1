[CmdletBinding()]
param()
# The complete baseline runs once; sidecars import the fixture builders only.
. (Join-Path $PSScriptRoot 'IsaInlineTableFixtures.ps1')
















$dataProofCount=0
foreach ($caseName in @('ValidByteIndex','MalformedByteRange','WrongByteStart','WrongByteOwner','WrongDispatch','ActualVexOutside','MalformedDwordLastEntry','MissingByteEnd','NamedByteSymbol','TypedByteSymbol','WrongByteEndOwner')) {
    $case=New-DataProofCase $caseName $(if ($caseName -eq 'ValidByteIndex') { '' } else { $caseName })
    Invoke-Control $case ($caseName -eq 'ValidByteIndex'); ++$dataProofCount
}
foreach ($caseName in @('BackwardValid','MissingProcedure','BadProcedureBounds','CrossProcedure','BackwardMiddle','UnknownTarget','ShortProcedure','WrongProcedureOwner')) {
    $case=New-DataProofCase $caseName $caseName
    Invoke-Control $case ($caseName -eq 'BackwardValid'); ++$dataProofCount
}
foreach ($caseName in @('TerminalValid','TerminalMissingProcedure','TerminalShortProcedure','TerminalWrongProcedureOwner','TerminalWrongContributionEnd','TerminalMalformedByte')) {
    $case=New-DataProofCase $caseName $caseName
    Invoke-Control $case ($caseName -eq 'TerminalValid'); ++$dataProofCount
}
foreach ($caseName in @('AlignedByteValid','AlignedByteMalformedNop','AlignedByteInvalidIndex','AlignedByteRepeatedNop')) {
    $case=New-DataProofCase $caseName $caseName
    Invoke-Control $case ($caseName -eq 'AlignedByteValid'); ++$dataProofCount
}
Write-Host "ISA data-proof synthetic controls passed: $dataProofCount"







$rdxBase = New-SyntheticCase RdxBase -RdxBase -PdbData
# Mirrors ImageCodecs' observed dispatch at 0x14009011c, its table at 0x140091138,
# and the anonymous 16-byte S_LDATA32 at 0001:590136; the synthetic PE relocates the shape.
Invoke-Control $rdxBase $true 7 0
$case = New-SyntheticCase RdxBaseWithRealVex -RdxBase -PdbData -RealVex
Invoke-Control $case $false 7 1
foreach ($fault in @('RdxWrongLea','RdxWrongLoadIndex','RdxWrongAdd','RdxWrongJump',
        'RdxNoncontiguous','RdxDoubleLoad','MissingDataSymbol','WrongDataOwner','WrongDataStart',
        'WrongDataRecordSize','NamedDataSymbol','TypedDataSymbol','CrossContribution','ContributionBounds',
        'RdxTargetMiddle','RdxTargetMissing','RdxTargetInvalid','TargetBeforeDispatch',
        'TargetAtTable','TargetPastTable','InstructionExtendsPastTable')) {
    $case = New-SyntheticCase "RdxBase$fault" -RdxBase -PdbData -Fault $fault
    Invoke-Control $case $false
}
Assert-RdxCombinationRejected RdxBaseWithOptionalByteIndex @{ OptionalByteIndex=$true }
Assert-RdxCombinationRejected RdxBaseWithDlByteIndex @{ DlByteIndex=$true }
Assert-RdxCombinationRejected RdxBaseWithSignedByteIndex @{ SignedByteIndex=$true }
Assert-RdxCombinationRejected RdxBaseWithR8Base @{ R8Base=$true }
Assert-RdxCombinationRejected RdxBaseWithHoistedR8Base @{ R8Base=$true; HoistedR8Base=$true }
Assert-RdxCombinationRejected RdxBaseWithRaxBase @{ RaxBase=$true }

$case = New-R9R8DispatchCase R9R8Dispatch
Invoke-Control $case $true 7 0
$case = New-R9R8DispatchCase R9R8DispatchWithAdjacentRealVex -RealVex
Invoke-Control $case $false 7 1
foreach ($fault in @(
        'R9WrongLeaDestination','R9WrongLeaOpcode','R9WrongLeaPrefix','R9WrongImageBase',
        'R9WrongIndexOpcode','R9WrongIndexDestination','R9WrongIndexBase','R9WrongIndexRegister',
        'R9WrongIndexScale','R9WrongIndexPrefix','R9MissingByteIndex',
        'R9WrongLoadOpcode','R9WrongLoadDestination','R9WrongLoadBase','R9WrongLoadIndex',
        'R9WrongLoadScale','R9WrongLoadPrefix','R9WrongAddOpcode','R9WrongAddDestination',
        'R9WrongAddSource','R9WrongAddPrefix','R9WrongJumpOpcode','R9WrongJumpTarget','R9WrongJumpPrefix',
        'R9NoncontiguousAfterLea','R9NoncontiguousAfterIndex','R9NoncontiguousAfterLoad','R9NoncontiguousAfterAdd',
        'MissingDataSymbol','WrongDataOwner','WrongDataStart','WrongDataRecordSize','NamedDataSymbol','TypedDataSymbol',
        'CrossContribution','ContributionBounds','TargetBeforeDispatch','TargetAtTable','TargetPastTable',
        'R9TargetMiddle','R9TargetMissing','R9TargetInvalid','R9TruncatedTable')) {
    $case = New-R9R8DispatchCase "R9R8Dispatch$fault" -Fault $fault
    Invoke-Control $case $false
}

$dlByteIndex = New-SyntheticCase DlByteIndex -DlByteIndex
Invoke-Control $dlByteIndex $true 4 0
$case = New-SyntheticCase DlByteIndexWithRealVex -DlByteIndex -RealVex
Invoke-Control $case $false 4 1
foreach ($fault in @('WrongDlDestination','WrongDlSource','WrongDlOpcode','DlNoncontiguous','DlDuplicate',
        'MissingDataSymbol','WrongDataOwner','WrongDataStart','WrongDataRecordSize','NamedDataSymbol',
        'TypedDataSymbol','CrossContribution','ContributionBounds','TargetBeforeDispatch','TargetAtTable',
        'TargetPastTable','MiddleOfInstruction','UndecodedTarget','UnknownTargetInstruction',
        'SignedLastEntryMiddle','InstructionExtendsPastTable')) {
    $case = New-SyntheticCase "DlByteIndex$fault" -DlByteIndex -Fault $fault
    Invoke-Control $case $false
}
Assert-DlCombinationRejected DlByteIndexWithOptionalIndex @{ OptionalByteIndex=$true }
Assert-DlCombinationRejected DlByteIndexWithSignedIndex @{ SignedByteIndex=$true }
Assert-DlCombinationRejected DlByteIndexWithR8Base @{ R8Base=$true }
Assert-DlCombinationRejected DlByteIndexWithHoistedR8Base @{ R8Base=$true; HoistedR8Base=$true; PdbData=$true }
Assert-DlCombinationRejected DlByteIndexWithRaxBase @{ RaxBase=$true; PdbData=$true }

$good = New-SyntheticCase Positive -OptionalByteIndex -ShortNop
Invoke-Control $good $true 2 0
$optionalOff = New-SyntheticCase PositiveWithoutByteIndex
Invoke-Control $optionalOff $true 2 0
$tailPaddingCase = New-TailPaddingCase TailPaddingVex
Set-TailPaddingVexArtifact $tailPaddingCase
Invoke-Control $tailPaddingCase $true 1 0
$case = New-TailPaddingCase TailPaddingVexNonCc
Set-TailPaddingVexArtifact $case -NonCcTail
Invoke-Control $case $false
$case = New-TailPaddingCase TailPaddingVexAtTableEnd
Set-TailPaddingVexArtifact $case -PeTailMismatch
Invoke-Control $case $false
$case = New-TailPaddingCase TailPaddingVexAtTableEndReal
Set-TailPaddingVexArtifact $case -VexAtTableEnd
Invoke-Control $case $false
$case = New-TailPaddingCase TailPaddingVexOverlong
Set-TailPaddingVexArtifact $case -OverlongInstruction
Invoke-Control $case $false
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

$case = New-SyntheticCase HoistedR8Base -R8Base -HoistedR8Base -PdbData -TargetRegister 1
Invoke-Control $case $true 2 0
$case = New-SyntheticCase HoistedR8BaseWithRealVex -R8Base -HoistedR8Base -PdbData -TargetRegister 1 -RealVex
Invoke-Control $case $false 2 1
foreach ($fault in @('ClobberedR8','BrokenHoistedPrelude','PartialHoistedPrelude','WrongHoistedTest',
        'WrongHoistedBounds','BranchBeforeDispatch','BranchIntoTable','BranchPastTable','BranchTargetMiddle',
        'SecondBranchBeforeDispatch','FirstBranchPastTable','UnknownBranchTarget',
        'MissingDataSymbol','MissingDataWithRet','WrongDataOwner','WrongDataStart','CrossContribution')) {
    $case = New-SyntheticCase "HoistedR8Base$fault" -R8Base -HoistedR8Base -PdbData -TargetRegister 1 -Fault $fault
    Invoke-Control $case $false
}

$case = New-SyntheticCase RaxBase -RaxBase -PdbData
Invoke-Control $case $true 2 0
$case = New-SyntheticCase RaxBaseWithRealVex -RaxBase -PdbData -RealVex
Invoke-Control $case $false 2 1
foreach ($fault in @('WrongImageBase','CorruptedIndexLoad','ClobberedIndex','MismatchedTableBase',
        'MismatchedTableIndex','MismatchedTableDestination','WrongRaxAdd','WrongRaxJump',
        'MissingDataSymbol','WrongDataOwner','WrongDataStart','WrongDataRecordSize','NamedDataSymbol',
        'TypedDataSymbol','CrossContribution','ContributionBounds','MiddleOfInstruction',
        'UndecodedTarget','UnknownTargetInstruction','TargetBeforeDispatch','TargetAtTable','TargetPastTable',
        'InstructionExtendsPastTable')) {
    $case = New-SyntheticCase "RaxBase$fault" -RaxBase -PdbData -Fault $fault
    Invoke-Control $case $false
}

# Observed MSVC ASan std::visit form: LEA RAX,imagebase; MOV RCX,[RSP+20h];
# MOV ECX,[RAX+4*RCX+table]; ADD RCX,RAX; JMP RCX. For this fixture, the
# LEA begins at VA 0x14010f000, the stack load at 0x14010f007, and the table
# lookup at 0x14010f00c (RVA 0x0010f00c).
$case = New-SyntheticCase RaxRspIndex -RaxBase -RaxRspIndex -PdbData
Invoke-Control $case $true 2 0
$fixtureBytes = [IO.File]::ReadAllBytes($case.Binary)
$expectedRspLoad = [byte[]]@(0x48,0x8b,0x4c,0x24,0x20)
$actualRspLoad = [byte[]]$fixtureBytes[0x407..0x40b]
if ((Hex-Bytes $actualRspLoad) -ne (Hex-Bytes $expectedRspLoad)) {
    throw 'RaxRspIndex: PE bytes at RVA 0x0010f007 do not match the literal RSP qword load.'
}
$listingRows = [IO.File]::ReadAllLines($case.Disassembly)
$expectedRspListing = '14010f007: 48 8b 4c 24 20 mov rcx, qword ptr [rsp+20h]'
$expectedTableListing = '14010f00c: 8b 8c 88 00 00 11 00 mov ecx, dword ptr [rax+rcx*4+table]'
if ($expectedRspListing -cnotin $listingRows -or $expectedTableListing -cnotin $listingRows) {
    throw 'RaxRspIndex: listing bytes or instruction addresses do not match the literal PE layout.'
}
$case = New-SyntheticCase RaxRspIndexWithAdjacentRealVex -RaxBase -RaxRspIndex -PdbData -RealVex
Invoke-Control $case $false 2 1
foreach ($fault in @(
        'RaxRspWrongRexDestination','RaxRspWrongOpcode','RaxRspWrongLoadSize',
        'RaxRspWrongFrameBase','RaxRspWrongSib','RaxRspNoncontiguous','RaxRspClobberedIndex',
        'WrongImageBase','MismatchedTableBase','MismatchedTableIndex','MismatchedTableDestination',
        'WrongRaxAdd','WrongRaxJump','MissingDataSymbol','WrongDataOwner','WrongDataStart',
        'WrongDataRecordSize','NamedDataSymbol','TypedDataSymbol','CrossContribution','ContributionBounds',
        'MiddleOfInstruction','UndecodedTarget','UnknownTargetInstruction','TargetBeforeDispatch',
        'TargetAtTable','TargetPastTable','InstructionExtendsPastTable')) {
    $case = New-SyntheticCase "RaxRspIndex$fault" -RaxBase -RaxRspIndex -PdbData -Fault $fault
    Invoke-Control $case $false
}

$case = New-SyntheticCase SignedByteIndex -SignedByteIndex
Invoke-Control $case $true 4 0
$case = New-SyntheticCase SignedByteIndexWithVexAfterTable -SignedByteIndex -RealVex
Invoke-Control $case $false 4 1
foreach ($fault in @('SignedBadMovsxDestination','SignedBadFrame','SignedBadOpcode','SignedBadSize',
        'SignedNoncontiguous','SignedBiasTwoEntries','MissingDataSymbol','WrongDataOwner','WrongDataStart',
        'WrongDataRecordSize','NamedDataSymbol','TypedDataSymbol','CrossContribution','ContributionBounds',
        'TargetBeforeDispatch','TargetAtTable','SignedDefaultMiddle','SignedDefaultMissing',
        'SignedDefaultPastContribution','SignedLastEntryMiddle','InstructionExtendsPastTable')) {
    $case = New-SyntheticCase "SignedByteIndex$fault" -SignedByteIndex -Fault $fault
    Invoke-Control $case $false
}

# Observed HostPathReadTests form: movsx rax, byte ptr [rbp+1c0h] with a disp32,
# followed contiguously by the existing image-base/table-load/add/jump dispatch.
$case = New-SyntheticCase SignedWidePositiveDisp32WithDefaultBeforeTable -SignedWideFrame
Invoke-Control $case $true 4 0
$case = New-SyntheticCase SignedWideFrameWithRealVex -SignedWideFrame -RealVex
Invoke-Control $case $false 4 1
foreach ($fault in @('SignedWideWrongRexDestination','SignedWideWrongOpcode','SignedWideWrongFrameBase',
        'SignedWideWrongFrameSib','SignedWideNoncontiguous','MissingDataSymbol','WrongDataOwner',
        'WrongDataStart','WrongDataRecordSize','NamedDataSymbol','TypedDataSymbol','CrossContribution',
        'ContributionBounds','TargetBeforeDispatch','TargetAtTable','TargetPastTable','SignedDefaultMiddle',
        'SignedDefaultMissing','SignedDefaultPastContribution','SignedLastEntryMiddle','UndecodedTarget',
        'InstructionExtendsPastTable')) {
    $case = New-SyntheticCase "SignedWideFrame$fault" -SignedWideFrame -Fault $fault
    Invoke-Control $case $false
}

foreach ($name in @('WrongImageBase','BrokenSequence','CrossContribution','ContributionBounds',
        'MiddleOfInstruction','UndecodedTarget','UnknownTargetInstruction','TargetBeforeDispatch','NoRet','InstructionExtendsPastTable')) {
    $case = New-SyntheticCase $name -Fault $name
    Invoke-Control $case $false
}



$adjacent = New-AdjacentTablesCase AdjacentProvedTables
Invoke-Control $adjacent $true 1 0
$adjacent = New-AdjacentTablesCase AdjacentProvedTablesWithRealVex -RealVex
Invoke-Control $adjacent $false 1 1
foreach ($fault in 'SecondDispatchBroken','SecondNoncontiguous','SecondTargetMiddle','SecondTargetUnknown',
                    'FirstTargetMiddle','Gap','ByteMismatch','Overlong','OutsideSuffix','StartsAtEnd') {
    $adjacent = New-AdjacentTablesCase "AdjacentTables$fault" -Fault $fault
    Invoke-Control $adjacent $false
}

Write-Host "Synthetic ISA parser fixtures: $fixtureRoot"
$global:LASTEXITCODE = 0



$guardedProofCount = 0
foreach ($name in 'LargerContribution','LaterProcedureDataSymbol') {
    $fault = if ($name -eq 'LaterProcedureDataSymbol') { $name } else { '' }
    $case = New-GuardedTerminalByteRemapCase $name $fault
    Invoke-Control $case $true 1 0
    ++$guardedProofCount
}
foreach ($fault in 'ChangedCmp','MissingCmp','UnknownCmp','OverlappingCmp','WrongLengthCmp',
    'ListingCmpDisplacementMismatch','ListingCmpMaximumMismatch',
    'ChangedJa','MissingJa','UnknownJa','OverlappingJa','WrongLengthJa','ListingJaTargetMismatch',
    'ChangedMovsxd','MissingMovsxd','UnknownMovsxd','OverlappingMovsxd','WrongLengthMovsxd',
    'MismatchedStackDisplacement','RangeTooShort','RangeTooLong','RangeOverflow',
    'MissingProcedure','ShortProcedure','LongProcedure','WrongProcedureOwner','WrongDefaultTarget',
    'GuardOutsideProcedure','TargetOutsideProcedure','MalformedByte','EarlyDataSymbol','VexAfterProcedure') {
    $case = New-GuardedTerminalByteRemapCase ("Guarded$fault") $fault
    Invoke-Control $case $false
    ++$guardedProofCount
}
Write-Host "Guarded terminal byte-remap synthetic controls passed: $guardedProofCount"


$terminalDwordCount = 0
Invoke-Control (New-GuardedTerminalDwordCase 'TerminalDwordValid') $true 6 0
++$terminalDwordCount
Invoke-Control (New-GuardedTerminalDwordCase 'TerminalDwordRealVectorAfter' 'RealVectorAfter') $false 6 1
++$terminalDwordCount
foreach ($fault in 'WrongCmp','WrongJa','WrongMovsxd','DifferentStackSlot','ShortRange','LongRange','SignedRange','WrongDefault',
    'WrongLoad','WrongAdd','WrongJump','MissingGuard0','MissingGuard1','MissingGuard2','ListingGuard0','ListingGuard1','ListingGuard2',
    'UnknownGuard0','UnknownGuard1','UnknownGuard2','OverlappingGuard','ListingDispatch0','ListingDispatch1','ListingDispatch2','ListingDispatch3',
    'BadLastTarget','OutsideTarget','ShortProcedure','LongProcedure','WrongProcedureOwner','MissingProcedure','WrongDataOwner','MissingData','WrongDataStart','EarlyData') {
    Invoke-Control (New-GuardedTerminalDwordCase ("TerminalDword$fault") $fault) $false
    ++$terminalDwordCount
}
Write-Host "Guarded terminal DWORD synthetic controls passed: $terminalDwordCount"


$terminalDwordLongJaCount = 0
Invoke-Control (New-GuardedTerminalDwordLongJaCase 'TerminalDwordLongJaSeparateDefault' 'SeparateDefault' 10) $true 11 0
++$terminalDwordLongJaCount
foreach ($maximum in 1,10,127) {
    $expectedRows = $maximum + 1
    Invoke-Control (New-GuardedTerminalDwordLongJaCase "TerminalDwordLongJaMaximum$maximum" '' $maximum) $true $expectedRows 0
    ++$terminalDwordLongJaCount
}
Invoke-Control (New-GuardedTerminalDwordLongJaCase 'TerminalDwordLongJaAdjacentVex' 'AdjacentVex' 10) $false
++ $terminalDwordLongJaCount
foreach ($fault in 'WrongCmp','WrongJa','WrongJaCondition','WrongMovsxd','WrongMovsxdRegister','DifferentStackSlot','WrongCmpStackSlot',
    'ZeroRange','Range128','Range255','WrongDefault','WrongImageBase','WrongLoad','WrongLoadIndex','WrongAdd','WrongJump',
    'MissingGuard0','MissingGuard1','MissingGuard2','ListingGuard0','ListingGuard1','ListingGuard2','UnknownGuard0','UnknownGuard1','UnknownGuard2',
    'OverlappingGuard','OverlappingDispatch','ListingDispatch0','ListingDispatch1','ListingDispatch2','ListingDispatch3','BadLastTarget','OutsideTarget','MiddleTarget',
    'ShortProcedure','LongProcedure','WrongProcedureOwner','MissingProcedure','WrongDataOwner','MissingData','WrongDataStart','EarlyDataSymbol',
    'LaterUnrelatedData') {
    Invoke-Control (New-GuardedTerminalDwordLongJaCase ("TerminalDwordLongJa$fault") $fault 10) $false
    ++$terminalDwordLongJaCount
}
Write-Host "Guarded terminal DWORD long-JA synthetic controls passed: $terminalDwordLongJaCount"

# Bounded independent control for MOVZX ECX, byte [RAX]; CMP CL,imm8; JA rel8;
# LEA RDX,imagebase; MOV EAX,[RDX+4*RCX+tableRva]; ADD RAX,RDX; JMP RAX.
# Keep instruction bytes as numeric literals: these controls qualify parser evidence,
# including when a disassembly mnemonic is deliberately generic or misleading.


$movzxTerminalCount = 0
Invoke-Control (New-MovzxGuardedTerminalDwordCase 'MovzxTerminalDwordValid') $true 6 0
++$movzxTerminalCount
Invoke-Control (New-MovzxGuardedTerminalDwordCase 'MovzxTerminalDefaultInEntries' 'DefaultInTable') $true 6 0
++$movzxTerminalCount
Invoke-Control (New-MovzxGuardedTerminalDwordCase 'MovzxTerminalMaximumOne' '' 1) $true 2 0
++$movzxTerminalCount
Invoke-Control (New-MovzxGuardedTerminalDwordCase 'MovzxTerminalMaximum127' '' 127) $true 128 0
++$movzxTerminalCount
# Keep a real VEX instruction directly after the terminal table rejected.
$case = New-MovzxGuardedTerminalDwordCase 'MovzxTerminalAdjacentVex'
$pe = [IO.File]::ReadAllBytes($case.Binary)
[UInt32]$vexAt = [UInt32]0x0010f900
[byte[]]$vex = @(0xc5,0xf8,0x10,0x00)
Put-Bytes $pe (0x400+$vexAt-[UInt32]0x0010f000) $vex
$rows = [IO.File]::ReadAllText($case.Disassembly)
$rows += "`n$('{0:x}: {1} vmovups xmm0, xmmword ptr [rax]' -f ([UInt64]0x140000000+$vexAt),(Hex-Bytes $vex))"
[IO.File]::WriteAllBytes($case.Binary,$pe)
Set-Content -LiteralPath $case.Disassembly -Value $rows -Encoding utf8
Invoke-Control $case $false 6 1
++$movzxTerminalCount

foreach ($fault in 'WrongMovzxDestination','WrongMovzxSource','SignedMovsx','WrongCmpRegister','RangeTooSmall','RangeTooLarge',
    'WrongJa','DefaultMidInstruction','DefaultOutsideProcedure','DefaultInsideTable','WrongImageBase','WrongLeaOpcode','WrongLeaDestination',
    'WrongLoadOpcode','WrongLoadDestination','WrongLoadScale','WrongLoadIndex','WrongLoadBase','WrongLoadAddress',
    'WrongAddOpcode','WrongAddRegisters','WrongJumpOpcode',
    'MissingPrefix0','MissingPrefix1','MissingPrefix2','ListingMismatchPrefix0','ListingMismatchPrefix1','ListingMismatchPrefix2',
    'UnknownPrefix0','UnknownPrefix1','UnknownPrefix2','OverlapPrefix0','OverlapPrefix1','OverlapPrefix2',
    'MissingDispatch0','MissingDispatch1','MissingDispatch2','MissingDispatch3',
    'ListingMismatchDispatch0','ListingMismatchDispatch1','ListingMismatchDispatch2','ListingMismatchDispatch3',
    'UnknownDispatch0','UnknownDispatch1','UnknownDispatch2','UnknownDispatch3',
    'OverlapDispatch0','OverlapDispatch1','OverlapDispatch2','OverlapDispatch3',
    'MissingProcedure','WrongProcedureOwner','ProcedureEndsEarly','ProcedureEndsLate',
    'MissingData','WrongDataOwner','WrongDataStart','EarlyDataSymbol',
    'BadLastTarget','OutsideTarget','MidTableTarget') {
    Invoke-Control (New-MovzxGuardedTerminalDwordCase ("MovzxTerminal$fault") $fault) $false
    ++$movzxTerminalCount
}
Write-Host "MOVZX guarded terminal DWORD synthetic controls passed: $movzxTerminalCount"

# The observed Release std::variant destruction dispatch (ApplicationFileJobs.obj in RedPrism.exe, 2026-10-01): the signed byte
# index makes the byte table biased by one entry. At the observed addresses:
#   140096290 MOVSX RAX,byte [RCX+0x80]          14009629f MOVZX EAX,byte [R8+RAX+0x962ed]   1400962b0 ADD RDX,R8
#   140096298 LEA   R8,imagebase                  1400962a8 MOV EDX,[R8+4*RAX+0x962dc]        1400962b3 JMP RDX
# A four-entry DWORD table at 0x962dc and the 22-byte index table at 0x962ec (index -1 first) end the 114-byte procedure and
# its contribution. The DWORD table's first entry, 0x962c4, is the byte sequence c4 62 09 00 that the linear decoder reads as
# VPSHUFB. This control rebuilds those bytes at those addresses (the section is placed at the observed RVA 0x96000), with the
# neighbouring functions as separate contributions of the same object, and the objdump rows around the tables. Every fault
# changes one thing about this otherwise accepted image.


$biasedR8Count = 0
$case = New-SignedBiasedR8Case 'SignedBiasedR8Valid'
Invoke-Control $case $true 2 0
++$biasedR8Count
# The observed layout is not the only one the proof allows: the dispatch need not be the procedure's first instruction.
$case = New-SignedBiasedR8Case 'SignedBiasedR8LeadingInstruction' 'LeadingInstruction'
Invoke-Control $case $true 2 0
++$biasedR8Count
# Nothing depends on the observed addresses: the same image in a section at another RVA (its first table row now reads as VEX,
# its second as a legacy prefix) is accepted too.
$case = New-SignedBiasedR8Case 'SignedBiasedR8Relocated' '' 0x0010f000
Invoke-Control $case $true 1 0
++$biasedR8Count
foreach ($fault in 'MovsxUnsigned','MovsxWord','MovsxWrongDestination','MovsxWrongBase','MovsxWrongRex','MovsxShortDisplacement',
        'LeaWrongDestination','LeaWrongBase','LeaWrongImageBase',
        'ByteLoadWrongDestination','ByteLoadWrongIndex','ByteLoadSigned','ByteLoadWrongPrefix','ByteLoadMissing',
        'DwordLoadWrongDestination','DwordLoadWrongBase','DwordLoadWrongScale','DwordLoadWrongOpcode',
        'AddWrongRegister','AddWrongOpcode','JumpWrongRegister','ConsistentEcxTarget','UnsignedUnbiasedEdx',
        'GapAfterMovsx','GapAfterLea','GapAfterMovzx','GapAfterMov','GapAfterAdd') {
    $case = New-SignedBiasedR8Case "SignedBiasedR8$fault" $fault
    Invoke-Control $case $false
    ++$biasedR8Count
}
foreach ($kind in 'ListingMismatch','Unknown','Overlap','Missing') {
    foreach ($index in 0..5) {
        $case = New-SignedBiasedR8Case "SignedBiasedR8$kind$index" "$kind$index"
        Invoke-Control $case $false
        ++$biasedR8Count
    }
}
# A table whose PDB start is one byte off, in either direction and for either table, and a byte table whose bias does not match.
foreach ($fault in 'MissingDwordSymbol','MissingByteSymbol',
        'DwordSymbolOneByteLate','DwordSymbolOneByteEarly','ByteSymbolOneByteLate','ByteSymbolOneByteEarly',
        'DwordSymbolWrongOwner','ByteSymbolWrongOwner','NamedDwordSymbol','NamedByteSymbol','TypedDwordSymbol','TypedByteSymbol',
        'DwordSymbolWrongSize','ByteSymbolWrongSize','SymbolInsideDwordTable','SymbolInsideByteTable',
        'BiasNone','BiasTwo','BiasNegative','DwordLoadBiasedByOneEntry') {
    $case = New-SignedBiasedR8Case "SignedBiasedR8$fault" $fault
    Invoke-Control $case $false
    ++$biasedR8Count
}
# Procedure and contribution bounds, index values, and targets outside the function or not on a decoded instruction.
foreach ($fault in 'MissingProcedure','ProcedureWrongOwner','ProcedureEndsEarly','ProcedureEndsLate','ProcedureStartsAfterPrefix','ContributionLonger',
        'ByteValueTooLarge','ByteValueTooLargeLast',
        'TargetBeforeFunction','TargetAfterFunction','TargetInsideInstruction','TargetInDwordTable','TargetInByteTable','TargetUnknown') {
    $case = New-SignedBiasedR8Case "SignedBiasedR8$fault" $fault
    Invoke-Control $case $false
    ++$biasedR8Count
}
# A genuine VEX instruction outside any proven table stays a violation while the two table rows stay data: inside the
# procedure, right before the DWORD table, on the first byte after the procedure and in the next function.
foreach ($fault in 'RealVexInBody','RealVexInPad','RealVexAtProcedureEnd','RealVexAfterFunction') {
    $case = New-SignedBiasedR8Case "SignedBiasedR8$fault" $fault
    Invoke-Control $case $false 2 1
    ++$biasedR8Count
}
Write-Host "Signed biased R8 byte-index synthetic controls passed: $biasedR8Count"


$unsignedStackCount = 0
foreach ($maximum in 1,5,127) {
    Invoke-Control (New-UnsignedStackTerminalCase "UnsignedStackTerminalMaximum$maximum" '' $maximum) $true ($maximum+1) 0
    ++$unsignedStackCount
}
Invoke-Control (New-UnsignedStackTerminalCase 'UnsignedStackTerminalAdjacentVex' 'AdjacentVex') $false 6 1
++ $unsignedStackCount
$unsignedStackFaults = @('WrongCmp','WrongJa','WrongIndexOpcode','WrongIndexRegister','WrongIndexSib','DifferentStackSlot',
    'ZeroRange','Range128','Range255','WrongDefault','BackwardDefault','WrongImageBase','WrongLoad','WrongLoadIndex','WrongAdd','WrongJump',
    'OverlappingGuard','OverlappingDispatch','DuplicateGuard','UnknownTarget','WrongRet','MissingRet','ListingRet','UnknownRet','OverlappingRet',
    'MiddleTarget','OutsideTarget','ShortProcedure','LongProcedure','WrongProcedureOwner','MissingProcedure','WrongDataOwner','MissingData',
    'WrongDataStart','EarlyDataSymbol')
foreach ($i in 0..6) { $unsignedStackFaults += "MissingRow$i", "ListingRow$i", "UnknownRow$i" }
foreach ($fault in $unsignedStackFaults) {
    Invoke-Control (New-UnsignedStackTerminalCase ("UnsignedStackTerminal$fault") $fault) $false
    ++$unsignedStackCount
}
Write-Host "Unsigned stack terminal DWORD synthetic controls passed: $unsignedStackCount"

# Independent short unsigned JA with signed stack reload: the default is a separate decoded body entry.
# Eight little-endian RVA entries also decode as real VEX rows. Prove data from guard/PDB/body evidence,
# while actual body/adjacent vector instructions continue to fail. No native instructions are executed.


$separateShortCount = 0
Invoke-Control (New-SeparateShortDefaultCase 'SeparateShortDefaultSlot20') $true 8 0; ++$separateShortCount
Invoke-Control (New-SeparateShortDefaultCase 'SeparateShortDefaultSlot40' '' 0x40) $true 8 0; ++$separateShortCount
Invoke-Control (New-SeparateShortDefaultCase 'SeparateShortDefaultRetCase' 'CaseRetBoundary') $true 7 0; ++$separateShortCount
$separateShortFaults = @('DefaultInGuard','DefaultInDispatch','DefaultInTable','DefaultMiddleInstruction',
    'DifferentStackSlot','WrongJa','SignedMaximum','ZeroMaximum','WrongImageBase','BodyBranchIntoTable',
    'BodyBranchIntoPadding','BodyListingMismatch','UnknownCaseTarget','MissingBodyRow','WrongRet','WrongPadding',
    'UnknownRet','MissingRet','MissingPadding','CaseInGuard','CaseInPadding','CaseInTable','TableListingMismatch',
    'ShortProcedure','LongProcedure','WrongProcedureOwner','WrongDataOwner','MissingProcedure','MissingData','InteriorData')
foreach ($i in 0..6) { $separateShortFaults += "MissingRow$i", "ListingRow$i", "UnknownRow$i", "DuplicateRow$i" }
foreach ($fault in $separateShortFaults) {
    Invoke-Control (New-SeparateShortDefaultCase ("SeparateShortDefault$fault") $fault) $false
    ++$separateShortCount
}
Invoke-Control (New-SeparateShortDefaultCase 'SeparateShortDefaultActualBodyVex' 'ActualBodyVex') $false 8 1
++ $separateShortCount
Invoke-Control (New-SeparateShortDefaultCase 'SeparateShortDefaultActualAdjacentEvex' 'ActualAdjacentEvex') $false 8 1
++ $separateShortCount
if ($separateShortCount -ne 63) { throw 'Separate short-default parser control roster differs.' }
Write-Host 'Separate short-default terminal DWORD synthetic controls passed: 63 (3 accepted, 60 refused)'
