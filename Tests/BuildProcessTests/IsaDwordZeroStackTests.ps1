[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'IsaInlineTableTests.ps1')

$script:dwPrefixRva = [UInt32]($script:sectionRva + 0x89d)
$script:dwDispatchRva = [UInt32]($script:dwPrefixRva + 6)
$script:dwTableRva = [UInt32]($script:sectionRva + 0x900)
$script:dwImageBase = [UInt64]$script:imageBase
$script:dwPassed = 0
$script:dwRefused = 0
$script:dwNames = [Collections.Generic.List[string]]::new()

function New-DwordZeroStackCase([string]$Name, [string]$Fault = '', [int]$Maximum = 10) {
    if ($Maximum -lt 1 -or $Maximum -gt 255) { throw 'Fixture maximum must fit an encoded byte.' }
    $case = New-SyntheticCase ($Name + 'Seed') -PdbData
    [byte[]]$pe = [IO.File]::ReadAllBytes($case.Binary)
    [UInt32]$section = $script:sectionRva
    [UInt32]$prefix = $section + 0x89d
    [UInt32]$dispatch = $prefix + 6
    [UInt32]$table = $section + 0x900
    [UInt32]$target = $prefix + 40
    [UInt32]$end = $table + 4 * ($Maximum + 1)

    # Exact failed-image shape: CMP dword [RSP],imm8; unsigned short JA; image-base LEA;
    # zero-extending ECX=[RSP]; DWORD RVA load; ADD RCX,RAX; JMP RCX. Prefix length 6,
    # displacement-bearing table RVA at prefix+19, and afterJump at prefix+28.
    [byte[]]$bytes = @(
        0x83,0x3c,0x24,[byte]$Maximum, 0x77,[byte]($target-$dispatch),
        0x48,0x8d,0x05,0,0,0,0,
        0x8b,0x0c,0x24,
        0x8b,0x8c,0x88,0,0,0,0,
        0x48,0x03,0xc8,0xff,0xe1)
    Put-Bytes $bytes 9 ([BitConverter]::GetBytes([int32](-[int64]($dispatch + 7))))
    Set-U32 $bytes 19 $table

    [UInt32]$procBegin = $prefix
    [UInt32]$procEnd = $end
    $dataOwner = 0
    $dataName = ''
    $dataType = '<no type>'
    $rows = [Collections.Generic.List[string]]::new()
    $starts = @(0,4,6,13,16,23,26)
    $lengths = @(4,2,7,3,7,3,2)

    switch ($Fault) {
        'WrongCmpOpcode' { $bytes[0] = 0x80 }
        'WrongCmpRegister' { $bytes[1] = 0x34 }
        'WrongCmpSib' { $bytes[2] = 0x25 }
        'WrongCmpStackSlot' { $bytes[1] = 0x7c }
        'WrongJaOpcode' { $bytes[4] = 0x75 }
        'SignedJa' { $bytes[4] = 0x7f }
        'ZeroMaximum' { $bytes[3] = 0 }
        'Maximum128' { $bytes[3] = 128 }
        'Maximum255' { $bytes[3] = 255 }
        'WrongLeaOpcode' { $bytes[6] = 0x49 }
        'WrongLeaRegister' { $bytes[8] = 0x0d }
        'WrongImageBase' { $bytes[9]++ }
        'WrongStackLoadOpcode' { $bytes[13] = 0x89 }
        'WrongStackRegister' { $bytes[14] = 0x04 }
        'WrongStackSib' { $bytes[15] = 0x25 }
        'WrongIndexStackSlot' { $bytes[14] = 0x4c }
        'WrongStackWidth' { $bytes[13] = 0x48 }
        'WrongTableLoadOpcode' { $bytes[16] = 0x89 }
        'WrongTableDestination' { $bytes[17] = 0x94 }
        'WrongTableSib' { $bytes[18] = 0x89 }
        'WrongTableRva' { Set-U32 $bytes 19 ($table + 4) }
        'WrongAdd' { $bytes[25] = 0xc1 }
        'WrongJump' { $bytes[27] = 0xe0 }
        'DefaultNotTableTarget' { $bytes[5] = [byte]($target + 1 - $dispatch) }
        'DefaultIntoGuard' { $bytes[5] = 0 }
        'MissingData' { $dataName = '<missing>' }
        'WrongDataOwner' { $dataOwner = 1 }
        'WrongDataStart' { $table += 4; Set-U32 $bytes 19 $table; $end = $table + 4 * ($Maximum + 1) }
        'NamedData' { $dataName = 'NamedSwitchTable' }
        'TypedData' { $dataType = '0x1000' }
        'ProcedureEarly' { $procEnd -= 4 }
        'ProcedureLate' { $procEnd += 4 }
        'ProcedureWrongOwner' { $procBegin = $prefix - 1 }
        'ProcedureStartsAfterPrefix' { $procBegin = $prefix + 1 }
        'ProcedureMissing' { $procEnd = 0 }
        'ContributionShort' { }
        'PdbAgeMismatch' { }
        'PdbGuidMismatch' { }
        'BodyBranchIntoTable' { $procBegin = $prefix - 5 }
    }

    # The JA fallback is a real decoded body instruction and is represented by every table entry.
    # A RET plus the exact three-byte NOP is the terminal body shape under test.
    [byte[]]$terminal = @(0xc3,0x0f,0x1f,0x00)
    switch ($Fault) {
        'TerminalMissing' { $terminal = [byte[]]@(0x90,0x0f,0x1f,0x00) }
        'TerminalNopWrong' { $terminal = [byte[]]@(0xc3,0x0f,0x1f,0x01) }
        'TerminalNopMissing' { $terminal = [byte[]]@(0xc3,0x90,0x90,0x90) }
    }
    Put-Bytes $pe (0x400 + $prefix - $section) $bytes
    Put-Bytes $pe (0x400 + $table - $section - 4) $terminal
    $pe[0x400 + $target - $section] = 0x90
    $pe[0x400 + $target - $section - 1] = 0x90

    if ($Fault -eq 'PdbGuidMismatch') { $pe[0x5a4] = $pe[0x5a4] -bxor 1 }
    if ($Fault -eq 'BodyBranchIntoTable') {
        [byte[]]$branch = @(0xe9,0,0,0,0)
        Put-Bytes $branch 1 ([BitConverter]::GetBytes([int32]($table - ($procBegin + 5))))
        Put-Bytes $pe (0x400 + $procBegin - $section) $branch
        $rows.Add(('{0:x}: {1} jmp' -f ($script:dwImageBase + $procBegin),(Hex-Bytes $branch)))
    }
    if ($Fault -eq 'PdbAgeMismatch') {
        $meta = Get-Content -LiteralPath $case.Metadata -Raw
        $meta = [regex]::Replace($meta, '(?m)(Age:\s*)\d+', '${1}8', 1)
        Set-Content -LiteralPath $case.Metadata -Value $meta -Encoding utf8
    }
    if ($Fault -eq 'ContributionShort') {
        $meta = Get-Content -LiteralPath $case.Metadata -Raw
        $meta = [regex]::Replace($meta, '(?m)(size\s*=\s*)\d+', '${1}2303', 1)
        Set-Content -LiteralPath $case.Metadata -Value $meta -Encoding utf8
    }

    for ($i = 0; $i -lt 7; ++$i) {
        if ($Fault -eq "MissingRow$i") { continue }
        [byte[]]$row = $bytes[$starts[$i]..($starts[$i] + $lengths[$i] - 1)]
        if ($Fault -eq "ListingDiff$i") { $row[$row.Length - 1] = $row[$row.Length - 1] -bxor 1 }
        $rowText = if ($Fault -eq "UnknownRow$i") { '<unknown>' } else { 'guard-dispatch' }
        [UInt64]$address = $script:dwImageBase + $prefix + $starts[$i]
        if ($Fault -eq "NoncontiguousRow$i") { $address++ }
        $rows.Add(('{0:x}: {1} {2}' -f $address,(Hex-Bytes $row),$rowText))
        if ($Fault -eq "DuplicateRow$i") { $rows.Add(('{0:x}: {1} {2}' -f ($script:dwImageBase + $prefix + $starts[$i]),(Hex-Bytes $row),$rowText)) }
        if ($Fault -eq "PeDiffRow$i") {
            $raw = 0x400 + $prefix - $section + $starts[$i]
            $pe[$raw] = $pe[$raw] -bxor 1
        }
    }

    # Ensure all bytes from the post-dispatch boundary to RET are decoded, contiguous and PE-matched.
    for ([UInt32]$at = $prefix + 28; $at -lt $table - 4; ++$at) {
        if ($Fault -eq 'BodyVex' -and $at -ge $prefix + 28 -and $at -lt $prefix + 32) {
            if ($at -eq $prefix + 28) {
                Put-Bytes $pe (0x400 + $at - $section) ([byte[]]@(0xc5,0xf8,0x10,0x00))
                $rows.Add(('{0:x}: c5 f8 10 00 vmovups xmm0, xmmword ptr [rax]' -f ($script:dwImageBase + $at)))
            }
            continue
        }
        $pe[0x400 + $at - $section] = 0x90
        if ($Fault -eq 'BodyGap' -and $at -eq $prefix + 29) { continue }
        $bodyText = if (($Fault -eq 'UnknownBody' -and $at -eq $prefix + 29) -or
            ($Fault -eq 'TargetUnknown' -and $at -eq $prefix + 31) -or
            ($Fault -eq 'UnknownDefault' -and $at -eq $target)) { '<unknown>' } else { 'nop' }
        $bodyByte = if ($Fault -eq 'BodyListingDiff' -and $at -eq $prefix + 29) { '91' } else { '90' }
        $rows.Add(('{0:x}: {1} {2}' -f ($script:dwImageBase + $at),$bodyByte,$bodyText))
    }
    if ($Fault -eq 'BodyPeMismatch') {
        $pe[0x400 + $prefix - $section + 29] = 0x91
    }
    $retAddress = $table - 4
    $retBytes = if ($Fault -eq 'TerminalListingDiff') { '90' } else { 'c3' }
    $retText = if ($Fault -eq 'TerminalUnknown') { '<unknown>' } else { 'ret' }
    $rows.Add(('{0:x}: {1} {2}' -f ($script:dwImageBase + $retAddress),$retBytes,$retText))
    $nopBytes = if ($Fault -eq 'TerminalNopListingDiff') { '0f 1f 01' } else { '0f 1f 00' }
    $nopText = if ($Fault -eq 'TerminalNopUnknown') { '<unknown>' } else { 'nop dword ptr [rax]' }
    if ($Fault -ne 'TerminalNopMissing') {
        $rows.Add(('{0:x}: {1} {2}' -f ($script:dwImageBase + $table - 3),$nopBytes,$nopText))
    }

    for ($entry = 0; $entry -le $Maximum; ++$entry) {
        [UInt32]$rva = $target
        switch ($(if ($entry -eq $Maximum) { $Fault } else { '' })) {
            'TargetInGuard' { $rva = $dispatch }
            'TargetOutsideProcedure' { $rva = $prefix - 1 }
            'TargetAtTable' { $rva = $table }
            'TargetMiddleInstruction' { $rva = $table - 2 }
            'TargetUnknown' { $rva = $prefix + 31 }
            'TargetTerminalPadding' { $rva = $table - 3 }
            'TargetRetBoundary' { $rva = $table - 4 }
        }
        Set-U32 $pe (0x400 + $table - $section + 4 * $entry) $rva
        $at = 0x400 + $table - $section + 4 * $entry
        $rows.Add(('{0:x}: {1} vmovups xmm0, xmmword ptr [rax]' -f
            ($script:dwImageBase + $table + 4 * $entry),(Hex-Bytes ([byte[]]$pe[$at..($at+3)]))))
    }

    if ($Fault -in @('AdjacentVex','AdjacentEvex')) {
        [byte[]]$vector = if ($Fault -eq 'AdjacentVex') { @(0xc5,0xf8,0x10,0x00) } else { @(0x62,0xf1,0x7c,0x48,0x10,0x00) }
        Put-Bytes $pe (0x400 + $end - $section) $vector
        $mnemonic = if ($Fault -eq 'AdjacentVex') { 'vmovups xmm0, xmmword ptr [rax]' } else { 'vmovups zmm0, zmmword ptr [rax]' }
        $rows.Add(('{0:x}: {1} {2}' -f ($script:dwImageBase + $end),(Hex-Bytes $vector),$mnemonic))
    }

    $procedure = if ($Fault -eq 'ProcedureMissing') { '' } else {
        $owner = if ($Fault -eq 'ProcedureWrongOwner') { 1 } else { 0 }
        "  Mod $owner | ``SyntheticControl.obj``:`n  0 | S_GPROC32 [size = 16] ``GuardedDwordZeroStack`` `n  addr = 0001:$($procBegin-$section), code size = $($procEnd-$procBegin)`n"
    }
    if ($Fault -eq 'WrongDataOwner') { $dataOwner = 1 }
    if ($Fault -eq 'MissingData') { $dataLine = '' }
    else { $dataLine = "  Mod $dataOwner | ``SyntheticControl.obj``:`n  0 | S_LDATA32 [size = 16] ``$dataName```n  type = $dataType, addr = 0001:$($table-$section)`n" }
    $symbols = "  Mod 0 | ``SyntheticControl.obj``:`n$procedure$dataLine"
    if ($Fault -eq 'InteriorData') { $symbols += "  Mod 0 | ``SyntheticControl.obj``:`n  0 | S_LDATA32 [size = 16] `````n  type = <no type>, addr = 0001:$($table+4-$section)`n" }
    Set-Content -LiteralPath $case.Symbols -Value $symbols -Encoding utf8

    if ($Fault -in @('WrongTerminalPe','TerminalNopWrong')) { $pe[0x400 + $table - $section - 3] = 0x01 }
    Set-Content -LiteralPath $case.Disassembly -Value (@($rows | Sort-Object {
        [UInt64]::Parse(($_ -split ':',2)[0],[Globalization.NumberStyles]::HexNumber)
    }) -join "`n") -Encoding utf8
    [IO.File]::WriteAllBytes($case.Binary,$pe)
    $case.Name = $Name
    return $case
}

function Invoke-DwordControl([string]$Name,[string]$Fault,[int]$Maximum,[bool]$Accepted,[int]$Inline=-1,[int]$Vectors=-1) {
    $case = New-DwordZeroStackCase $Name $Fault $Maximum
    try {
        $result = [RedPrismBuild.IsaScanner]::Scan($case.Binary,$case.Metadata,$case.Symbols,$case.Disassembly,
            [string[]]@(),[string[]]@(),[string[]]@())
    } catch [System.Management.Automation.MethodInvocationException] {
        if ($Accepted -or $Fault -notin @('PdbAgeMismatch','PdbGuidMismatch') -or
            $_.Exception.InnerException -isnot [IO.InvalidDataException] -or
            $_.Exception.InnerException.Message -ne 'PDB GUID/age does not match the linked image.') { throw }
        ++$script:dwRefused
        $script:dwNames.Add($Name)
        Write-Host "PASS isa-dword-zero-stack.$Name refused-mismatched-identity"
        return
    }
    $bad = $result.Violations.Count -ne 0
    if ($Accepted -and $bad) { throw "$Name unexpectedly violated ISA gate: $($result.Violations -join '; ')" }
    if (-not $Accepted -and -not $bad) { throw "$Name bypassed ISA gate." }
    if ($Inline -ge 0 -and $result.InlineTableVectorData -ne $Inline) {
        throw "$Name expected inline-table vector rows $Inline; got $($result.InlineTableVectorData)."
    }
    if ($Vectors -ge 0 -and $result.VectorInstructions -ne $Vectors) {
        throw "$Name expected real vector rows $Vectors; got $($result.VectorInstructions)."
    }
    if ($Accepted) { ++$script:dwPassed } else { ++$script:dwRefused }
    $script:dwNames.Add($Name)
    Write-Host ("PASS isa-dword-zero-stack.{0} accepted={1} inline={2} vectors={3}" -f
        $Name,$Accepted,$result.InlineTableVectorData,$result.VectorInstructions)
}

# Positive range boundaries and the minimum procedure begin at the six-byte prefix.
foreach ($maximum in 1,64,127) { Invoke-DwordControl "Maximum$maximum" '' $maximum $true ($maximum+1) 0 }
Invoke-DwordControl 'MinimumProcedureStartsAtPrefixWithRetNop3' '' 10 $true 11 0
Invoke-DwordControl 'TargetRetBoundary' 'TargetRetBoundary' 10 $true 10 0

$faults = @(
    'WrongCmpOpcode','WrongCmpRegister','WrongCmpSib','WrongCmpStackSlot','WrongJaOpcode','SignedJa','ZeroMaximum','Maximum128','Maximum255',
    'WrongLeaOpcode','WrongLeaRegister','WrongImageBase','WrongStackLoadOpcode','WrongStackRegister','WrongStackSib',
    'WrongStackWidth','WrongIndexStackSlot','WrongTableLoadOpcode','WrongTableDestination','WrongTableSib','WrongTableRva','WrongAdd','WrongJump',
    'DefaultNotTableTarget','DefaultIntoGuard','TargetInGuard','TargetOutsideProcedure','TargetAtTable','TargetMiddleInstruction',
    'TargetUnknown','TargetTerminalPadding','MissingData','WrongDataOwner','WrongDataStart','NamedData','TypedData','InteriorData',
    'ProcedureEarly','ProcedureLate','ProcedureWrongOwner','ProcedureStartsAfterPrefix','ProcedureMissing',
    'ContributionShort','PdbAgeMismatch','PdbGuidMismatch','BodyBranchIntoTable','BodyGap','UnknownBody','BodyPeMismatch','BodyListingDiff',
    'TerminalMissing','TerminalNopWrong','TerminalNopMissing','TerminalListingDiff','TerminalUnknown','TerminalNopListingDiff',
    'TerminalNopUnknown','WrongTerminalPe','UnknownDefault','AdjacentVex','AdjacentEvex','BodyVex'
)
foreach ($fault in $faults) {
    $inline = if ($fault -in @('AdjacentVex','AdjacentEvex','BodyVex')) { 11 } else { -1 }
    $vectors = if ($fault -in @('AdjacentVex','AdjacentEvex','BodyVex')) { 1 } else { -1 }
    Invoke-DwordControl $fault $fault 10 $false $inline $vectors
}
foreach ($row in 0..6) {
    foreach ($kind in 'MissingRow','UnknownRow','ListingDiff','NoncontiguousRow','DuplicateRow','PeDiffRow') {
        Invoke-DwordControl "$kind$row" "$kind$row" 10 $false
    }
}

if ($script:dwPassed -ne 5 -or $script:dwRefused -ne 104 -or $script:dwNames.Count -ne 109) {
    throw 'Exact DWORD zero-stack control roster differs.'
}
Write-Host 'PASS isa-dword-zero-stack: 109 controls (5 accepted, 104 refused)'
