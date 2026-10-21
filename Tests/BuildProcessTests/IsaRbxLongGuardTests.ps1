[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Resolve the actual checkout from this sidecar's ancestors. Build/IsaScan.cs is the
# repository sentinel; no current-directory or hard-coded-worktree assumption is used.
$repo = (Get-Item -LiteralPath $PSScriptRoot).FullName
while (-not (Test-Path -LiteralPath (Join-Path $repo 'Build/IsaScan.cs')) -or
       -not (Test-Path -LiteralPath (Join-Path $repo 'Tests/BuildProcessTests/IsaInlineTableFixtures.ps1'))) {
    $parent = Split-Path -Parent $repo
    if ([string]::IsNullOrEmpty($parent) -or $parent -eq $repo) {
        throw 'Could not find the RedPrism repository root from the sidecar location.'
    }
    $repo = $parent
}

# Freeze and display the complete case roster before importing the helper script
# (which itself executes parser-only controls) or creating any RbxLongGuard fixture.
$guardDispatchRows = @(
    'MovsxdRbxFromEdx', 'MoveRdiFromRcx', 'InitializeZeroDword', 'CallPreservingRbx',
    'InitializeOneDword', 'StoreIndexThroughRdi', 'CompareMaximum', 'JaCommonDefault',
    'LeaImageBase', 'LoadIndexedTableDword', 'AddImageBase', 'JumpThroughRcx'
)
$caseRoster = [Collections.Generic.List[object]]::new()
foreach ($maximum in @(1, 5, 127)) {
    $caseRoster.Add(@{ Name="RbxLongGuardMaximum$maximum"; Fault=''; Maximum=$maximum; Pass=$true; Inline=$maximum+1; Vectors=0 })
}
foreach ($kind in @('Missing','Duplicate','Unknown','Overlap','ListingBytes','PeMismatch')) {
    foreach ($rowName in $guardDispatchRows) {
        $caseRoster.Add(@{ Name="RbxLongGuard${kind}_$rowName"; Fault="${kind}_$rowName"; Maximum=5; Pass=$false; Inline=-1; Vectors=-1 })
    }
}
$semanticFaults = @(
    'WrongCmpOpcode', 'WrongCmpMaximum', 'ZeroMaximum', 'Maximum128', 'WrongJa',
    'Full64Initializer', 'ClobberRbx', 'WrongImageBase', 'WrongIndexRegister', 'WrongScale',
    'WrongTableRva', 'WrongAddRegisters', 'WrongJumpRegister', 'MissingRet', 'WrongRet',
    'MissingCc', 'WrongCc', 'MissingDataSymbol', 'ForeignDataSymbol', 'NamedDataSymbol',
    'TypedDataSymbol', 'InteriorDataSymbol', 'ProcedureEndsEarly', 'ProcedureEndsLate',
    'ContributionEndsEarly', 'LargerContribution', 'CaseOutside', 'DefaultOutside',
    'CaseMidInstruction', 'DefaultMidInstruction', 'AdjacentRealVex'
)
foreach ($fault in $semanticFaults) {
    $caseRoster.Add(@{ Name="RbxLongGuard$fault"; Fault=$fault; Maximum=5; Pass=($fault -eq 'LargerContribution'); Inline=$(if ($fault -in @('AdjacentRealVex','LargerContribution')) { 6 } else { -1 }); Vectors=$(if ($fault -eq 'AdjacentRealVex') { 1 } elseif ($fault -eq 'LargerContribution') { 0 } else { -1 }) })
}
# Main's independently authored extent, terminal-row and table-byte controls.
foreach ($kind in @('Missing','Duplicate','Unknown','Overlap','ListingBytes','PeMismatch')) {
    foreach ($rowName in @('Ret','Int3')) {
        $caseRoster.Add(@{ Name="RbxLongGuard${kind}_$rowName"; Fault="${kind}_$rowName"; Maximum=5; Pass=$false; Inline=-1; Vectors=-1 })
    }
}
foreach ($kind in @('Missing','Duplicate','Unknown','Overlap')) {
    foreach ($rowName in @('CaseTarget','DefaultTarget')) {
        $caseRoster.Add(@{ Name="RbxLongGuard${kind}_$rowName"; Fault="${kind}_$rowName"; Maximum=5; Pass=$false; Inline=-1; Vectors=-1 })
    }
}
foreach ($fault in @('MissingProcedure','ForeignProcedure','WrongDataAddress','CaseInTable','DefaultInTable','TableListingBytes')) {
    $caseRoster.Add(@{ Name="RbxLongGuard$fault"; Fault=$fault; Maximum=5; Pass=$false; Inline=-1; Vectors=-1 })
}
$rosterNames = @($caseRoster | ForEach-Object { $_.Name })
if ($rosterNames.Count -ne 132 -or $rosterNames.Count -ne @($rosterNames | Select-Object -Unique).Count) {
    throw "RbxLongGuard roster must contain exactly 132 unique cases; found $($rosterNames.Count)."
}
Write-Host "RbxLongGuard frozen roster: $($rosterNames.Count) cases (4 accepted, 128 refused; 72 guard-row, 12 terminal-row, 8 target-row and 37 semantic controls)"
foreach ($name in $rosterNames) { Write-Host "ROSTER $name" }

# The baseline helper loads the tracked scanner unless the caller preloads a draft type.
. (Join-Path $repo 'Tests/BuildProcessTests/IsaInlineTableFixtures.ps1')

function New-RbxLongGuardCase {
    param(
        [Parameter(Mandatory)] [string]$Name,
        [string]$Fault = '',
        [ValidateRange(0,127)] [int]$Maximum = 5
    )

    # Reuse the existing literal PE32+/CodeView image and paths. Replace the bytes,
    # contribution, procedure/data symbols, and listing independently below.
    $case = New-SyntheticCase ("RbxLongGuard_$Name") -PdbData
    [byte[]]$pe = [IO.File]::ReadAllBytes($case.Binary)
    [UInt32]$section = $script:sectionRva
    [UInt64]$imageBase = $script:imageBase
    [UInt32]$prefix = $section + 0x870
    [UInt32]$table = $section + 0x8d0
    [UInt32]$entries = [UInt32]($Maximum + 1)
    [UInt32]$tableEnd = [UInt32]($table + 4 * $entries)
    [UInt32]$ret = $table - 2
    [UInt32]$caseTarget = $section + 0x8c5
    [UInt32]$defaultTarget = $prefix + 38 + 19 + 6

    $prefixRows = [Collections.Generic.List[object]]::new()
    $prefixRows.Add(@{ Name='MovsxdRbxFromEdx'; Bytes=[byte[]]@(0x48,0x63,0xda); Text='movsxd rbx, edx' })
    $prefixRows.Add(@{ Name='MoveRdiFromRcx'; Bytes=[byte[]]@(0x48,0x8b,0xf9); Text='mov rdi, rcx' })
    $zeroInit = [byte[]]@(0xc7,0x44,0x24,0x20,0x00,0x00,0x00,0x00)
    if ($Fault -eq 'Full64Initializer') { $zeroInit = [byte[]]@(0x48) + $zeroInit }
    $prefixRows.Add(@{ Name='InitializeZeroDword'; Bytes=$zeroInit; Text='initialize [rsp+20h] to zero' })
    $prefixRows.Add(@{ Name='CallPreservingRbx'; Bytes=[byte[]]@(0xe8,0,0,0,0); Text='call ordinary x64 ABI helper' })
    if ($Fault -eq 'ClobberRbx') {
        $prefixRows.Add(@{ Name='ClobberRbx'; Bytes=[byte[]]@(0x31,0xdb); Text='xor ebx, ebx' })
    }
    $prefixRows.Add(@{ Name='InitializeOneDword'; Bytes=[byte[]]@(0xc7,0x44,0x24,0x20,0x01,0x00,0x00,0x00); Text='initialize [rsp+20h] to one' })
    $prefixRows.Add(@{ Name='StoreIndexThroughRdi'; Bytes=[byte[]]@(0x89,0x1f); Text='mov dword ptr [rdi], ebx' })
    $cmpBytes = [byte[]]@(0x83,0xfb,[byte]$Maximum)
    switch ($Fault) {
        'WrongCmpOpcode' { $cmpBytes[0] = 0x80 }
        'WrongCmpMaximum' { $cmpBytes[2] = [byte](($Maximum - 1) -band 0xff) }
        'ZeroMaximum' { $cmpBytes[2] = 0 }
        'Maximum128' { $cmpBytes[2] = 128 }
    }
    if ($Fault -eq 'WrongIndexRegister') { $prefixRows[0].Bytes[2] = 0xd9 }
    $prefixRows.Add(@{ Name='CompareMaximum'; Bytes=$cmpBytes; Text='cmp ebx, maximum' })
    $jaBytes = [byte[]]@(0x0f,0x87,0,0,0,0)
    if ($Fault -eq 'WrongJa') { $jaBytes[1] = 0x86 }
    $prefixRows.Add(@{ Name='JaCommonDefault'; Bytes=$jaBytes; Text='ja common-default' })

    $cursor = 0
    foreach ($row in $prefixRows) {
        $row.Offset = $cursor
        $cursor += $row.Bytes.Length
    }
    [UInt32]$dispatch = [UInt32]($prefix + $cursor)
    [UInt32]$afterJump = [UInt32]($dispatch + 19)
    $defaultTarget = [UInt32]($afterJump + 6)
    if ($Fault -eq 'DefaultOutside') { $defaultTarget = [UInt32]($afterJump - 1) }
    if ($Fault -eq 'DefaultMidInstruction') { $defaultTarget = [UInt32]($caseTarget + 1) }
    if ($Fault -eq 'DefaultInTable') { $defaultTarget = $table }
    if ($Fault -eq 'DefaultAtRet') { $defaultTarget = $ret }

    $dispatchRows = [Collections.Generic.List[object]]::new()
    [byte[]]$lea = @(0x48,0x8d,0x15,0,0,0,0)
    [Int32]$imageBaseDisplacement = -[Int32]$dispatch - 7
    if ($Fault -eq 'WrongImageBase') { ++$imageBaseDisplacement }
    Put-Bytes $lea 3 ([BitConverter]::GetBytes($imageBaseDisplacement))
    $dispatchRows.Add(@{ Name='LeaImageBase'; Bytes=$lea; Text='lea rdx, [rip+imagebase]' })
    [byte[]]$load = @(0x8b,0x8c,0x9a,0,0,0,0)
    if ($Fault -eq 'WrongIndexRegister') { $load[2] = 0x92 }
    if ($Fault -eq 'WrongScale') { $load[2] = 0x5a }
    [UInt32]$loadRva = $table
    if ($Fault -eq 'WrongTableRva') { $loadRva += 4 }
    Put-Bytes $load 3 ([BitConverter]::GetBytes($loadRva))
    $dispatchRows.Add(@{ Name='LoadIndexedTableDword'; Bytes=$load; Text='mov ecx, dword ptr [rdx+rbx*4+table]' })
    [byte[]]$add = @(0x48,0x03,0xca)
    if ($Fault -eq 'WrongAddRegisters') { $add[2] = 0xc2 }
    $dispatchRows.Add(@{ Name='AddImageBase'; Bytes=$add; Text='add rcx, rdx' })
    [byte[]]$jump = @(0xff,0xe1)
    if ($Fault -eq 'WrongJumpRegister') { $jump[1] = 0xe0 }
    $dispatchRows.Add(@{ Name='JumpThroughRcx'; Bytes=$jump; Text='jmp rcx' })

    # Encode relative branches only after final instruction positions are known.
    foreach ($row in $prefixRows) {
        if ($row.Name -eq 'CallPreservingRbx') {
            [UInt32]$callTarget = [UInt32]($afterJump + 7)
            [Int32]$callDisplacement = [Int32]([Int64]$callTarget - [Int64]($prefix + $row.Offset + $row.Bytes.Length))
            Put-Bytes $row.Bytes 1 ([BitConverter]::GetBytes($callDisplacement))
        }
        if ($row.Name -eq 'JaCommonDefault') {
            [Int32]$jaDisplacement = [Int32]([Int64]$defaultTarget - [Int64]($prefix + $row.Offset + $row.Bytes.Length))
            Put-Bytes $row.Bytes 2 ([BitConverter]::GetBytes($jaDisplacement))
        }
    }

    $allRows = [Collections.Generic.List[object]]::new()
    foreach ($row in $prefixRows) { $row.Rva = [UInt32]($prefix + $row.Offset); $allRows.Add($row) }
    $cursor = 0
    foreach ($row in $dispatchRows) {
        $row.Offset = $cursor
        $row.Rva = [UInt32]($dispatch + $cursor)
        $cursor += $row.Bytes.Length
        $allRows.Add($row)
    }
    if ($cursor -ne 19) { throw "Dispatch sequence must be 19 bytes; got $cursor." }

    foreach ($row in $allRows) { Put-Bytes $pe (0x400 + $row.Rva - $section) $row.Bytes }
    # The contiguous indexed cases and the common JA destination are decoded one-byte NOP boundaries.
    # A two-byte NOP can be introduced to create an interior-target negative control.
    for ([UInt32]$body = $afterJump; $body -lt $ret;) {
        if (($Fault -eq 'CaseMidInstruction' -and $body -eq $caseTarget - 1) -or
            ($Fault -eq 'DefaultMidInstruction' -and $body -eq $caseTarget)) {
            Put-Bytes $pe (0x400 + $body - $section) ([byte[]]@(0x66,0x90))
            $body += 2
        } else {
            $pe[0x400 + $body - $section] = 0x90
            ++$body
        }
    }
    $pe[0x400 + $ret - $section] = if ($Fault -eq 'MissingRet') { 0x90 } elseif ($Fault -eq 'WrongRet') { 0xcb } else { 0xc3 }
    $pe[0x400 + $ret + 1 - $section] = if ($Fault -eq 'MissingCc') { 0x90 } elseif ($Fault -eq 'WrongCc') { 0xcc -bxor 1 } else { 0xcc }

    $symbolsRows = [Collections.Generic.List[string]]::new()
    $symbolsRows.Add('  Mod 0 | `SyntheticControl.obj`:')
    $procSize = [Int64]$tableEnd - [Int64]$prefix
    if ($Fault -eq 'ProcedureEndsEarly') { $procSize -= 4 }
    if ($Fault -eq 'ProcedureEndsLate') { $procSize += 4 }
    if ($Fault -ne 'MissingProcedure') {
        if ($Fault -eq 'ForeignProcedure') { $symbolsRows.Add('  Mod 1 | `Other.obj`:') }
        $symbolsRows.Add('  0 | S_GPROC32 [size = 16] `RbxLongGuard` ')
        $symbolsRows.Add(('  addr = 0001:{0}, code size = {1}' -f ($prefix - $section), $procSize))
    }
    $dataOwner = if ($Fault -eq 'ForeignDataSymbol') { 1 } else { 0 }
    $dataObject = if ($dataOwner -eq 1) { 'Other.obj' } else { 'SyntheticControl.obj' }
    $symbolsRows.Add(('  Mod {0} | `{1}`:' -f $dataOwner, $dataObject))
    if ($Fault -ne 'MissingDataSymbol') {
        $dataName = if ($Fault -eq 'NamedDataSymbol') { 'RbxLongGuardTable' } else { '' }
        $dataType = if ($Fault -eq 'TypedDataSymbol') { '0x1000' } else { '<no type>' }
        $symbolsRows.Add(('  0 | S_LDATA32 [size = 16] `{0}`' -f $dataName))
        $symbolsRows.Add(('  type = {0}, addr = 0001:{1}' -f $dataType, ($table - $section + $(if ($Fault -eq 'WrongDataAddress') { 4 } else { 0 }))))
    }
    if ($Fault -eq 'InteriorDataSymbol') {
        $symbolsRows.Add('  Mod 0 | `SyntheticControl.obj`:')
        $symbolsRows.Add('  0 | S_LDATA32 [size = 16] ``')
        $symbolsRows.Add(('  type = <no type>, addr = 0001:{0}' -f ($table - $section + 4)))
    }

    $contributionSize = [Int64]$tableEnd - [Int64]$section
    if ($Fault -eq 'ContributionEndsEarly') { $contributionSize -= 4 }
    if ($Fault -eq 'LargerContribution') { $contributionSize += 4 }
    if ($Fault -eq 'AdjacentRealVex') { $contributionSize += 4 }
    $metadataRows = @(
        "  Age: $script:age",
        "  GUID: {$script:guid}",
        '  Mod 0 | `SyntheticControl.obj`:',
        '  Obj: `SyntheticControl.obj`:',
        '  Mod 1 | `Other.obj`:',
        '  Obj: `Other.obj`:',
        '',
        'Section Contributions',
        "  SC[.text] | mod = 0, 0001:0, size = $contributionSize, data crc = 0, reloc crc = 0"
    )
    Set-Content -LiteralPath $case.Symbols -Value ($symbolsRows -join "`n") -Encoding utf8
    Set-Content -LiteralPath $case.Metadata -Value ($metadataRows -join "`n") -Encoding utf8

    $listingRows = [Collections.Generic.List[string]]::new()
    foreach ($row in $allRows) {
        $rowFault = ''
        $rowName = ''
        if ($Fault -match '^(Missing|Duplicate|Unknown|Overlap|ListingBytes|PeMismatch)_(.+)$') {
            $rowFault = $Matches[1]
            $rowName = $Matches[2]
        }
        if ($rowFault -eq 'PeMismatch' -and $row.Name -eq $rowName) {
            $pe[0x400 + $row.Rva - $section] = $pe[0x400 + $row.Rva - $section] -bxor 1
        }
        if ($rowFault -eq 'Missing' -and $row.Name -eq $rowName) { continue }
        [byte[]]$listedBytes = $row.Bytes.Clone()
        if ($rowFault -eq 'ListingBytes' -and $row.Name -eq $rowName) {
            $listedBytes[$listedBytes.Length - 1] = $listedBytes[$listedBytes.Length - 1] -bxor 1
        }
        $text = if ($rowFault -eq 'Unknown' -and $row.Name -eq $rowName) { '<unknown>' } else { $row.Text }
        $line = '{0:x}: {1} {2}' -f ($imageBase + $row.Rva), (Hex-Bytes $listedBytes), $text
        $listingRows.Add($line)
        if ($rowFault -eq 'Duplicate' -and $row.Name -eq $rowName) { $listingRows.Add($line) }
        if ($rowFault -eq 'Overlap' -and $row.Name -eq $rowName) {
            $listingRows.Add(('{0:x}: 90 overlap' -f ($imageBase + $row.Rva + 1)))
        }
    }

    # Body boundaries and target addresses are fully represented in both PE and listing.
    for ([UInt32]$body = $afterJump; $body -lt $ret;) {
        if (($Fault -eq 'CaseMidInstruction' -and $body -eq $caseTarget - 1) -or
            ($Fault -eq 'DefaultMidInstruction' -and $body -eq $caseTarget)) {
            $listingRows.Add(('{0:x}: 66 90 xchg ax, ax' -f ($imageBase + $body)))
            $body += 2
        } else {
            $listingRows.Add(('{0:x}: 90 nop' -f ($imageBase + $body)))
            ++$body
        }
    }
    if ($Fault -eq 'AdjacentRealVex') {
        [byte[]]$adjacent = @(0xc5,0xf8,0x10,0x00)
        Put-Bytes $pe (0x400 + $tableEnd - $section) $adjacent
        $listingRows.Add(('{0:x}: {1} vmovups xmm0, xmmword ptr [rax]' -f ($imageBase + $tableEnd), (Hex-Bytes $adjacent)))
    }
    $listingRows.Add(('{0:x}: {1} ret' -f ($imageBase + $ret), (Hex-Bytes ([byte[]]@($pe[0x400 + $ret - $section])))))
    $listingRows.Add(('{0:x}: {1} int3' -f ($imageBase + $ret + 1), (Hex-Bytes ([byte[]]@($pe[0x400 + $ret + 1 - $section])))))

    [UInt32]$firstBadCase = $caseTarget
    if ($Fault -eq 'CaseOutside') { $firstBadCase = [UInt32]($afterJump - 1) }
    if ($Fault -eq 'CaseInTable') { $firstBadCase = $table }
    if ($Fault -eq 'CaseAtRet') { $firstBadCase = $ret }
    if ($Fault -eq 'CaseMidInstruction') { $firstBadCase = [UInt32]($caseTarget + 1) }
    for ($i = 0; $i -lt $entries; ++$i) {
        [UInt32]$entry = if ($i -eq 0) { $firstBadCase } else { $caseTarget }
        Set-U32 $pe (0x400 + $table - $section + 4 * $i) $entry
        [byte[]]$entryBytes = $pe[(0x400 + $table - $section + 4 * $i)..(0x400 + $table - $section + 4 * $i + 3)]
        $listingRows.Add(('{0:x}: {1} vmovups xmm0, xmmword ptr [rax]' -f ($imageBase + $table + 4 * $i), (Hex-Bytes $entryBytes)))
    }

    if ($Fault -match '^(Missing|Duplicate|Unknown|Overlap|ListingBytes|PeMismatch)_(Ret|Int3|CaseTarget|DefaultTarget)$') {
        $kind = $Matches[1]
        $targetRva = switch ($Matches[2]) {
            'Ret' { $ret }
            'Int3' { $ret + 1 }
            'CaseTarget' { $caseTarget }
            'DefaultTarget' { $defaultTarget }
        }
        $address = $imageBase + $targetRva
        $rowIndex = -1
        for ($index = 0; $index -lt $listingRows.Count; ++$index) {
            if ($listingRows[$index].StartsWith(('{0:x}:' -f $address))) { $rowIndex = $index; break }
        }
        if ($rowIndex -lt 0) { throw 'Control mutation target is absent.' }
        $original = $listingRows[$rowIndex]
        switch ($kind) {
            'Missing' { $listingRows.RemoveAt($rowIndex) }
            'Duplicate' { $listingRows.Add($original) }
            'Unknown' { $listingRows[$rowIndex] = ($original -replace '(ret|int3|nop)$','<unknown>') }
            'Overlap' {
                # A decoded predecessor extending over the target invalidates its boundary.
                $listingRows.Add(('{0:x}: 66 90 xchg ax, ax' -f ($address - 1)))
            }
            'ListingBytes' { $listingRows[$rowIndex] = ('{0:x}: 90 nop' -f $address) }
            'PeMismatch' { $pe[0x400 + $targetRva - $section] = 0x90 }
        }
    }
    if ($Fault -eq 'TableListingBytes') {
        $address = $imageBase + $table
        for ($index = 0; $index -lt $listingRows.Count; ++$index) {
            if ($listingRows[$index].StartsWith(('{0:x}:' -f $address))) {
                # Keep the PE's valid targets; corrupt only the disassembler's claimed bytes.
                $listingRows[$index] = ('{0:x}: c5 f8 10 01 vmovups xmm0, xmmword ptr [rax]' -f $address)
                break
            }
        }
    }
    [IO.File]::WriteAllBytes($case.Binary, $pe)
    Set-Content -LiteralPath $case.Disassembly -Value (@($listingRows | Sort-Object {
        [UInt64]::Parse(($_ -split ':',2)[0], [Globalization.NumberStyles]::HexNumber)
    }) -join "`n") -Encoding utf8
    $case.Name = $Name
    $case.TableEnd = $tableEnd
    $case.ExpectedInline = $entries
    return $case
}

foreach ($item in $caseRoster) {
    $case = New-RbxLongGuardCase $item.Name $item.Fault $item.Maximum
    Invoke-Control $case $item.Pass $item.Inline $item.Vectors
}
Write-Host "RbxLongGuard parser-only controls passed: $($caseRoster.Count)"
