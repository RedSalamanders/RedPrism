[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$candidate = Join-Path $repo 'Build/IsaScan.cs'
$runRoot = Join-Path $repo ('.build/BuildProcessTests/IsaR8RaxRdx-' + [guid]::NewGuid().ToString('N'))
[void](New-Item -ItemType Directory -Path $runRoot -Force)
Start-Transcript -LiteralPath (Join-Path $runRoot 'controls.log') | Out-Null
try {
$candidateHash = (Get-FileHash -LiteralPath $candidate -Algorithm SHA256).Hash
$helperPath = Join-Path $repo 'Tests/BuildProcessTests/IsaInlineTableTests.ps1'
$helperHash = (Get-FileHash -LiteralPath $helperPath -Algorithm SHA256).Hash
Write-Host "PRIVATE CANDIDATE $candidate SHA256=$candidateHash"
Write-Host "HELPER SOURCE $helperPath SHA256=$helperHash"
# Reuse only these unchanged function definitions. Never execute the Root script's control roster or loader.
$helperNames = @('Set-U16','Set-U32','Set-U64','Put-Bytes','Hex-Bytes','New-SyntheticCase','Invoke-Control')
$tokens = $null; $parseErrors = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile($helperPath, [ref]$tokens, [ref]$parseErrors)
if ($parseErrors.Count) { throw 'Helper source has parse errors.' }
$definitions = @($ast.FindAll({ param($node) $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $helperNames -contains $node.Name }, $false))
if ($definitions.Count -ne $helperNames.Count) { throw 'Required unchanged helper function roster differs.' }
. ([scriptblock]::Create(($definitions | ForEach-Object { $_.Extent.Text }) -join "`n"))
# This loads only the managed private parser for synthetic controls. No native compiler/toolchain or PE execution.
if (-not ('RedPrismBuild.IsaScanner' -as [type])) { Add-Type -Path $candidate }
$fixtureRoot = Join-Path $runRoot 'fixtures'
[void](New-Item -ItemType Directory -Path $fixtureRoot)
$imageBase = [uint64]0x140000000
$sectionRva = [uint32]0x0010f000
$tableRva = [uint32]($sectionRva + 0x1000)
$dispatchRva = $sectionRva
$targetRva = [uint32]0x0010f8c5
$guid = [guid]'9c1e2d4b-6a70-4a8b-9012-3456789abcde'
$age = [uint32]7

function New-R8RaxRdxCase {
    param(
        [Parameter(Mandatory)][string]$Name,
        [string]$Fault = '',
        [ValidateRange(1,127)][int]$Maximum = 5,
        [switch]$BackwardTerminal,
        [ValidateRange(0,3)][int]$TerminalPadding = 0
    )
    $case = New-SyntheticCase ($Name + 'Envelope') -PdbData
    $pe = [IO.File]::ReadAllBytes($case.Binary)
    [uint32]$section = $script:sectionRva
    [uint64]$imageBase = $script:imageBase
    [uint32]$prefix = $section + 0x840
    [uint32]$procBegin = $section + 0x820
    [uint32]$table = $section + 0x900
    [uint32]$target = $section + 0x8c5 # RVA bytes begin C5 F8 10 00, like the motivating six repeated targets.
    [uint32]$count = [uint32]($Maximum + 1)
    [uint32]$tableEnd = [uint32]($table + 4 * $count)
    $rawBase = 0x400
    [uint32]$contributionEnd = $section + 0x1800
    $metadataText = [IO.File]::ReadAllText($case.Metadata).Replace('size = 8192,', 'size = 6144,')

    # Replace the entire procedure body with known one-byte NOPs, then write the dispatch and owned data.
    for ([uint32]$rva = $procBegin; $rva -lt $table; ++$rva) { $pe[$rawBase + $rva - $section] = 0x90 }
    [byte[]]$dispatch = @(
        0x41,0x8d,0x87,0,0,0,0,             # lea eax,[r15+bias]
        0x83,0xf8,[byte]$Maximum,             # cmp eax,max
        0x0f,0x87,0,0,0,0,                   # ja fallback
        0x4c,0x8d,0x05,0,0,0,0,             # lea r8,[rip+imagebase]
        0x41,0x8b,0x94,0x80,0,0,0,0,        # mov edx,[r8+rax*4+table]
        0x49,0x03,0xd0,                     # add rdx,r8
        0xff,0xe2                           # jmp rdx
    )
    Put-Bytes $dispatch 3 ([BitConverter]::GetBytes([int32]-5))
    Put-Bytes $dispatch 12 ([BitConverter]::GetBytes([int32]($target - ($prefix + 16))))
    Put-Bytes $dispatch 19 ([BitConverter]::GetBytes([int32]($imageBase - ($imageBase + $prefix + 23))))
    Set-U32 $dispatch 27 $table
    switch ($Fault) {
        'WrongPrefixRex' { $dispatch[0] = 0x49 }
        'WrongPrefixOpcode' { $dispatch[1] = 0x89 }
        'WrongPrefixModRm' { $dispatch[2] = 0x86 }
        'AlternateBias' { $dispatch[3] = 0 } # -256 is another valid signed bias.
        'WrongCmpOpcode' { $dispatch[7] = 0x81 }
        'WrongCmpRegister' { $dispatch[8] = 0xf9 }
        'WrongCmpRange' { $dispatch[9] = [byte]($Maximum - 1) }
        'WrongJaOpcode' { $dispatch[10] = 0x72 }
        'WrongJaCondition' { $dispatch[11] = 0x86 }
        'WrongDefaultGuard' { Put-Bytes $dispatch 12 ([BitConverter]::GetBytes([int32](($prefix + 8) - ($prefix + 16)))) }
        'WrongDefaultTable' { Put-Bytes $dispatch 12 ([BitConverter]::GetBytes([int32]($table - ($prefix + 16)))) }
        'WrongDefaultMiddle' { Put-Bytes $dispatch 12 ([BitConverter]::GetBytes([int32](($target + 1) - ($prefix + 16)))) }
        'WrongImageBaseLeaRex' { $dispatch[16] = 0x49 }
        'WrongImageBaseLeaOpcode' { $dispatch[17] = 0x89 }
        'WrongImageBaseLeaModRm' { $dispatch[18] = 0x0d }
        'WrongImageBase' { $dispatch[19]++ }
        'WrongLoadRex' { $dispatch[23] = 0x40 }
        'WrongLoadOpcode' { $dispatch[24] = 0x89 }
        'WrongLoadModRm' { $dispatch[25] = 0x9c }
        'WrongLoadSib' { $dispatch[26] = 0x88 }
        'WrongLoadTable' { $dispatch[27]++ }
        'WrongAddRex' { $dispatch[31] = 0x48 }
        'WrongAddOpcode' { $dispatch[32] = 0x01 }
        'WrongAddModRm' { $dispatch[33] = 0xc2 }
        'WrongJumpRegister' { $dispatch[35] = 0xe1 }
        'ZeroMaximum' { $dispatch[9] = 0 }
        'Maximum128' { $dispatch[9] = 128 }
        'Maximum255' { $dispatch[9] = 255 }
        'WrongDefaultOutsideProc' { Put-Bytes $dispatch 12 ([BitConverter]::GetBytes([int32](($procBegin - 1) - ($prefix + 16)))) }
    }
    Put-Bytes $pe ($rawBase + $prefix - $section) $dispatch

    Put-Bytes $pe ($rawBase + $target - $section) ([byte[]]@(0x66,0x90))
    if ($Fault -eq 'UnknownTarget') { $pe[$rawBase + $target - $section] = 0x06 }
    if ($Fault -eq 'RealVexTarget') { Put-Bytes $pe ($rawBase + $target - $section) ([byte[]]@(0xc5,0xf8,0x10,0x00)) }
    if ($Fault -eq 'RealEvexTarget') { Put-Bytes $pe ($rawBase + $target - $section) ([byte[]]@(0x62,0xf1,0x7c,0x48,0x10,0x00)) }
    if ($Fault -eq 'RealVexBody') { Put-Bytes $pe ($rawBase + $prefix - $section - 8) ([byte[]]@(0xc5,0xf8,0x10,0x00)) }
    if ($Fault -eq 'RealEvexBody') { Put-Bytes $pe ($rawBase + $prefix - $section - 8) ([byte[]]@(0x62,0xf1,0x7c,0x48,0x10,0x00)) }

    $effectivePadding = if ($Fault -eq 'TooMuchTerminalPadding') { 4 } else { $TerminalPadding }
    [uint32]$terminalEnd = $table - $effectivePadding
    [uint32]$terminal = if ($BackwardTerminal) { $terminalEnd - 5 } else { $terminalEnd - 1 }
    [byte[]]$terminalBytes = if ($BackwardTerminal) { @(0xe9,0,0,0,0) } else { @(0xc3) }
    if ($BackwardTerminal) { Put-Bytes $terminalBytes 1 ([BitConverter]::GetBytes([int32]($procBegin - $terminalEnd))) }
    if ($Fault -eq 'WrongTerminal') { $terminalBytes[0] = 0x90 }
    Put-Bytes $pe ($rawBase + $terminal - $section) $terminalBytes
    # The containing PDB procedure includes the primary table and eight bytes of sibling data.
    Put-Bytes $pe ($rawBase + $tableEnd - $section) ([byte[]]::new(8))
    for ($i = 0; $i -lt $count; ++$i) {
        [uint32]$entry = $target
        if ($Fault -eq 'TargetInterior' -and $i -eq ($count - 1)) { $entry = $target + 1 }
        if ($Fault -eq 'TargetGuard' -and $i -eq ($count - 1)) { $entry = $prefix + 8 }
        if ($Fault -eq 'TargetTable' -and $i -eq ($count - 1)) { $entry = $table }
        if ($Fault -eq 'TargetOutsideProc' -and $i -eq ($count - 1)) { $entry = $procBegin - 1 }
        if ($Fault -eq 'TargetDataSibling' -and $i -eq ($count - 1)) { $entry = $tableEnd }
        Set-U32 $pe ($rawBase + $table - $section + 4 * $i) $entry
    }

    $rows = [Collections.Generic.List[string]]::new()
    $vectorAt = $prefix - 8
    for ([uint32]$rva = $procBegin; $rva -lt $table; ++$rva) {
        if ($rva -eq $vectorAt -and $Fault -in @('RealVexBody','RealEvexBody')) {
            [byte[]]$vb = if ($Fault -eq 'RealVexBody') { @(0xc5,0xf8,0x10,0x00) } else { @(0x62,0xf1,0x7c,0x48,0x10,0x00) }
            $rows.Add(('{0:x}: {1} vector-body' -f ($imageBase + $rva), (Hex-Bytes $vb)))
            $rva += [uint32]($vb.Length - 1)
            continue
        }
        if ($rva -eq $prefix) {
            $starts = @(0,7,10,16,23,31,34); $lengths = @(7,3,6,7,8,3,2)
            $names = @('lea eax,[r15+bias]','cmp eax,max','ja fallback','lea r8,[rip+imagebase]','mov edx,[r8+rax*4+table]','add rdx,r8','jmp rdx')
            for ($k = 0; $k -lt 7; ++$k) {
                [byte[]]$part = $dispatch[$starts[$k]..($starts[$k] + $lengths[$k] - 1)]
                $rows.Add(('{0:x}: {1} {2}' -f ($imageBase + $prefix + $starts[$k]), (Hex-Bytes $part), $names[$k]))
            }
            $rva += 35
            continue
        }
        if ($rva -eq $target) {
            $len = if ($Fault -eq 'RealVexTarget') { 4 } elseif ($Fault -eq 'RealEvexTarget') { 6 } else { 2 }
            [byte[]]$tb = [byte[]]$pe[($rawBase + $rva - $section)..($rawBase + $rva - $section + $len - 1)]
            $txt = if ($Fault -eq 'UnknownTarget') { '<unknown>' } elseif ($Fault -in @('RealVexTarget','RealEvexTarget')) { 'vector-target' } else { 'nop' }
            $rows.Add(('{0:x}: {1} {2}' -f ($imageBase + $rva), (Hex-Bytes $tb), $txt))
            $rva += [uint32]($len - 1)
            continue
        }
        if ($rva -eq $terminal) {
            $terminalText = if ($BackwardTerminal) { 'jmp backward' } else { 'ret' }
            $rows.Add(('{0:x}: {1} {2}' -f ($imageBase + $rva), (Hex-Bytes $terminalBytes), $terminalText))
            $rva += [uint32]($terminalBytes.Length - 1)
            continue
        }
        if ($rva -eq $procBegin -and $Fault -in @('IncomingGuard','IncomingGuardInterior','IncomingTable','IncomingSiblingData')) {
            [uint32]$dest = switch ($Fault) { 'IncomingGuard' {$prefix}; 'IncomingGuardInterior' {$prefix + 1}; 'IncomingTable' {$table}; default {$tableEnd} }
            [byte[]]$branch = @(0xe9,0,0,0,0)
            Put-Bytes $branch 1 ([BitConverter]::GetBytes([int32]($dest - ($rva + 5))))
            Put-Bytes $pe ($rawBase + $rva - $section) $branch
            $rows.Add(('{0:x}: {1} jmp direct' -f ($imageBase + $rva), (Hex-Bytes $branch)))
            $rva += 4
            continue
        }
        if ($rva -eq ($procBegin + 8) -and $Fault.StartsWith('SplitLock', [StringComparison]::Ordinal)) {
            [byte[]]$atomic = switch ($Fault) {
                'SplitLockOrphan' { @() }
                'SplitLockDec' { @(0xff,0x0b) }
                'SplitLockXadd' { @(0x0f,0xc1,0x03) }
                'SplitLockRexInc' { @(0x48,0xff,0x03) }
                'SplitLockRexDec' { @(0x48,0xff,0x0b) }
                'SplitLockRexXadd' { @(0x48,0x0f,0xc1,0x03) }
                'SplitLockRegisterInc' { @(0xff,0xc0) }
                'SplitLockRegisterDec' { @(0xff,0xc8) }
                'SplitLockRegisterXadd' { @(0x0f,0xc1,0xc0) }
                'SplitLockNonAtomic' { @(0x8b,0x03) }
                'SplitLockNonAtomicGroup' { @(0xff,0x13) }
                default { @(0xff,0x03) }
            }
            if ($null -eq $atomic) { $atomic = [byte[]]::new(0) }
            $gap = if ($Fault -eq 'SplitLockGap') { 1 } else { 0 }
            $prefixByte = if ($Fault -eq 'SplitLockPePrefixMismatch') { 0xf2 } else { 0xf0 }
            $pe[$rawBase + $rva - $section] = [byte]$prefixByte
            if ($Fault -ne 'SplitLockMissingPrefix') { $rows.Add(('{0:x}: f0 lock' -f ($imageBase + $rva))) }
            if ($gap) { $rows.Add(('{0:x}: 90 nop' -f ($imageBase + $rva + 1))) }
            if ($atomic.Length) {
                Put-Bytes $pe ($rawBase + $rva - $section + 1 + $gap) $atomic
                [byte[]]$reported = $atomic.Clone()
                if ($Fault -eq 'SplitLockMismatching') { $reported[1] = 0x0b }
                $atomicText = if ($Fault -eq 'SplitLockUnknownSuffix') { '<unknown>' } else { 'atomic-control' }
                if ($Fault -ne 'SplitLockMissingSuffix') {
                    $rows.Add(('{0:x}: {1} {2}' -f ($imageBase + $rva + 1 + $gap), (Hex-Bytes $reported), $atomicText))
                }
            }
            $rva += [uint32]($atomic.Length + $gap)
            continue
        }
        if ($Fault -eq 'UnknownBody' -and $rva -eq ($procBegin + 8)) {
            $pe[$rawBase + $rva - $section] = 0x06
            $rows.Add(('{0:x}: 06 <unknown>' -f ($imageBase + $rva)))
            continue
        }
        $rows.Add(('{0:x}: 90 nop' -f ($imageBase + $rva)))
    }
    for ($i = 0; $i -lt $count; ++$i) {
        $at = $rawBase + $table - $section + 4 * $i
        [byte[]]$entryBytes = [byte[]]$pe[$at..($at + 3)]
        $rows.Add(('{0:x}: {1} vmovups xmm0,xmmword ptr [rax]' -f ($imageBase + $table + 4 * $i), (Hex-Bytes $entryBytes)))
    }
    if ($Fault -eq 'TableListingMismatch') { $rows[$rows.Count - 1] = $rows[$rows.Count - 1] -replace 'c5 f8 10 00','c5 f8 10 01' }
    if ($Fault -eq 'PeGuardMismatch') { $pe[$rawBase + $prefix - $section + 7] = 0x81 }
    if ($Fault -eq 'PeTableMismatch') { $pe[$rawBase + $table - $section] = $pe[$rawBase + $table - $section] -bxor 1 }
    if ($Fault -eq 'MissingGuardRow') {
        $missingAddress = '{0:x}:' -f ($imageBase + $prefix + 7)
        for ($ri = 0; $ri -lt $rows.Count; ++$ri) { if ($rows[$ri].StartsWith($missingAddress, [StringComparison]::Ordinal)) { $rows.RemoveAt($ri); break } }
    }
    if ($Fault -eq 'DuplicateGuardRow') { $rows.Add(('{0:x}: 90 nop' -f ($imageBase + $prefix + 7))) }
    if ($Fault -eq 'OverlapGuardRow') { $rows.Add(('{0:x}: 90 nop' -f ($imageBase + $prefix + 1))) }
    if ($Fault -eq 'DuplicateTableRow') { $rows.Add($rows[$rows.Count - 1]) }
    if ($Fault -eq 'MissingTableRow') { $rows.RemoveAt($rows.Count - 1) }
    if ($Fault -eq 'UnknownTableRow') { $rows[$rows.Count - 1] = $rows[$rows.Count - 1] -replace 'vmovups','<unknown>' }
    if ($Fault -eq 'ListingBodyMismatch') { $rows[0] = $rows[0].Replace(': 90 nop', ': 91 nop') }

    $procEnd = $tableEnd + 8
    if ($Fault -eq 'ShortProcedure') { $procEnd = $tableEnd - 4 }
    if ($Fault -eq 'LongProcedure') { $procEnd = $contributionEnd + 1 }
    $procModule = if ($Fault -eq 'WrongProcedureOwner') { 1 } else { 0 }
    $dataModule = if ($Fault -eq 'WrongDataOwner') { 1 } else { 0 }
    $dataOffset = if ($Fault -eq 'WrongDataStart') { $table + 4 - $section } else { $table - $section }
    $endOffset = if ($Fault -eq 'WrongDataEnd') { $tableEnd + 4 - $section } else { $tableEnd - $section }
    $procText = "  Mod $procModule | ``SyntheticControl.obj``:`n  0 | S_GPROC32 [size = 16] ``GuardedR8RaxRdx`` `n  addr = 0001:$($procBegin - $section), code size = $($procEnd - $procBegin)`n"
    if ($Fault -eq 'MissingProcedure') { $procText = '' }
    if ($Fault -eq 'DuplicateProcedure') { $procText += $procText }
    if ($Fault -eq 'TruncatedProcedure') { $procText = $procText -replace 'code size = \d+','code size =' }
    $dataText = "  Mod $dataModule | ``SyntheticControl.obj``:`n  0 | S_LDATA32 [size = 16] `````n  type = <no type>, addr = 0001:$dataOffset`n"
    if ($Fault -eq 'MissingStartSymbol') { $dataText = '' }
    if ($Fault -eq 'NamedStartSymbol') { $dataText = $dataText.Replace('S_LDATA32 [size = 16] ``','S_LDATA32 [size = 16] ``named') }
    if ($Fault -eq 'TypedStartSymbol') { $dataText = $dataText.Replace('<no type>','unsigned long') }
    if ($Fault -eq 'DuplicateStartSymbol') { $dataText += $dataText }
    if ($Fault -ne 'MissingEndSymbol') { $dataText += "  Mod $dataModule | ``SyntheticControl.obj``:`n  0 | S_LDATA32 [size = 16] `````n  type = <no type>, addr = 0001:$endOffset`n" }
    if ($Fault -eq 'InteriorSiblingSymbol') { $dataText += "  Mod $dataModule | ``SyntheticControl.obj``:`n  0 | S_LDATA32 [size = 16] `````n  type = <no type>, addr = 0001:$($table + 4 - $section)`n" }
    if ($Fault -eq 'DuplicateEndSymbol') { $dataText += "  Mod $dataModule | ``SyntheticControl.obj``:`n  0 | S_LDATA32 [size = 16] `````n  type = <no type>, addr = 0001:$endOffset`n" }

    $symbols = $procText + $dataText



    if ($Fault -eq 'TableEdgeGuard') {
        Set-U32 $pe ($rawBase + $table - $section) ($prefix + 1)
        $rows[$rows.Count - $count] = '{0:x}: {1} vmovups xmm0,xmmword ptr [rax]' -f ($imageBase+$table), (Hex-Bytes ([byte[]]$pe[($rawBase+$table-$section)..($rawBase+$table-$section+3)]))
    }
    if ($Fault -eq 'TruncatedListing') { $rows.RemoveAt($rows.Count - 1) }

    if ($Fault -eq 'AmbiguousOwner') {
        $metadataText = $metadataText.Replace('Section Contributions', "  Mod 0001 | ``ConflictingControl.obj``:`n  Obj: ``ConflictingControl.obj``:`nSection Contributions")
        $metadataText += "`n  SC[.text]  | mod = 1, 0001:$($table - $section), size = 4, data crc = 0, reloc crc = 0`n"
    }
    if ($Fault -eq 'MissingOwnerMetadata') {
        $metadataText = (@($metadataText -split '\r?\n' | Where-Object { $_ -notmatch '^\s*(Mod \d+ \||Obj:)' }) -join "`n")
    }
    if ($Fault -eq 'TruncatedOwnerMetadata') {
        $metadataText = $metadataText -replace '(?m)^\s*Mod \d+ \| .+\r?$', '  Mod 0 |'
    }
    Set-Content -LiteralPath $case.Metadata -Value $metadataText -Encoding utf8
    Set-Content -LiteralPath $case.Symbols -Value $symbols -Encoding utf8
    Set-Content -LiteralPath $case.Disassembly -Value (@($rows | Sort-Object { [uint64]::Parse(($_ -split ':',2)[0], [Globalization.NumberStyles]::HexNumber) }) -join "`n") -Encoding utf8
    [IO.File]::WriteAllBytes($case.Binary, $pe)
    $case.Name = $Name
    $case.TableRva = $table
    $case.TableEnd = $tableEnd
    return $case
}


$results = [Collections.Generic.List[object]]::new()
function Invoke-R8RaxRdxControl {
    param([string]$Name, [string]$Fault='', [int]$Maximum=5, [bool]$ExpectedPass=$false,
        [int]$ExpectedInline=-1, [int]$ExpectedVectors=-1, [string]$ExpectedError='',
        [switch]$BackwardTerminal, [int]$TerminalPadding=0)
    $passed = $false; $detail = ''; $case = $null
    try {
        $case = New-R8RaxRdxCase $Name $Fault $Maximum -BackwardTerminal:$BackwardTerminal -TerminalPadding $TerminalPadding
        if ($ExpectedPass -and $ExpectedInline -lt 0) { $ExpectedInline = $Maximum + 1 }
        # Keep the old helper and its optional expectations intact. Ordinary refusals do not require zero vectors.
        Invoke-Control $case $ExpectedPass $ExpectedInline $ExpectedVectors
        if ($ExpectedError) { throw "Expected metadata exception: $ExpectedError" }
        $passed = $true
    } catch {
        $rootError = $_.Exception.GetBaseException()
        $detail = $_.Exception.Message
        if ($ExpectedError -and $rootError -is [IO.InvalidDataException] -and $rootError.Message -eq $ExpectedError) {
            $passed = $true
            Write-Host "PASS isa-r8-rax-rdx.$Name expected metadata refusal: $($rootError.Message)"
        } else { Write-Host "FAIL isa-r8-rax-rdx.$Name $detail" }
    }
    $results.Add([pscustomobject]@{Name=$Name;Fault=$Fault;Maximum=$Maximum;ExpectedPass=$ExpectedPass;
        ExpectedInline=$ExpectedInline;ExpectedVectors=$ExpectedVectors;ExpectedError=$ExpectedError;
        Passed=$passed;Detail=$detail;Fixture=if($null -ne $case){Split-Path -Parent $case.Binary}else{''}})
}
foreach ($maximum in 1,5,12,127) { Invoke-R8RaxRdxControl -Name "CanonicalMaximum$maximum" -Maximum $maximum -ExpectedPass $true -ExpectedVectors 0 }
foreach ($positive in 'AlternateBias','IncomingGuard','DuplicateStartSymbol','DuplicateEndSymbol') {
    Invoke-R8RaxRdxControl -Name $positive -Fault $positive -ExpectedPass $true -ExpectedVectors 0
}
foreach ($padding in 0..3) {
    Invoke-R8RaxRdxControl -Name "RetPadding$padding" -ExpectedPass $true -ExpectedVectors 0 -TerminalPadding $padding
    Invoke-R8RaxRdxControl -Name "BackwardJmpPadding$padding" -ExpectedPass $true -ExpectedVectors 0 -BackwardTerminal -TerminalPadding $padding
}
foreach ($positive in 'SplitLockInc','SplitLockDec','SplitLockXadd','SplitLockRexInc','SplitLockRexDec','SplitLockRexXadd') {
    Invoke-R8RaxRdxControl -Name $positive -Fault $positive -ExpectedPass $true -ExpectedVectors 0
}
$faults = @(
    'WrongPrefixRex','WrongPrefixOpcode','WrongPrefixModRm',
    'WrongCmpOpcode','WrongCmpRegister','WrongCmpRange','WrongJaOpcode','WrongJaCondition',
    'WrongImageBaseLeaRex','WrongImageBaseLeaOpcode','WrongImageBaseLeaModRm','WrongImageBase',
    'WrongLoadRex','WrongLoadOpcode','WrongLoadModRm','WrongLoadSib','WrongLoadTable',
    'WrongAddRex','WrongAddOpcode','WrongAddModRm','WrongJumpRegister',
    'ZeroMaximum','Maximum128','Maximum255','WrongDefaultGuard','WrongDefaultTable','WrongDefaultMiddle','WrongDefaultOutsideProc',
    'WrongTerminal','TooMuchTerminalPadding','TargetInterior','TargetGuard','TargetTable','TargetOutsideProc','TargetDataSibling','UnknownTarget',
    'WrongProcedureOwner','WrongDataOwner','MissingProcedure','TruncatedProcedure','DuplicateProcedure',
    'ShortProcedure','LongProcedure','MissingStartSymbol','MissingEndSymbol','WrongDataStart','WrongDataEnd',
    'NamedStartSymbol','TypedStartSymbol','InteriorSiblingSymbol',
    'TableListingMismatch','ListingBodyMismatch','PeGuardMismatch','PeTableMismatch','MissingGuardRow','DuplicateGuardRow','OverlapGuardRow',
    'MissingTableRow','DuplicateTableRow','UnknownTableRow','UnknownBody','TruncatedListing',
    'IncomingGuardInterior','IncomingTable','IncomingSiblingData','TableEdgeGuard',
    'SplitLockOrphan','SplitLockMissingPrefix','SplitLockMissingSuffix','SplitLockMismatching',
    'SplitLockRegisterInc','SplitLockRegisterDec','SplitLockRegisterXadd','SplitLockNonAtomic','SplitLockNonAtomicGroup',
    'SplitLockUnknownSuffix','SplitLockGap','SplitLockPePrefixMismatch'
)
foreach ($fault in $faults) { Invoke-R8RaxRdxControl -Name $fault -Fault $fault }
foreach ($vector in 'RealVexBody','RealEvexBody','RealVexTarget','RealEvexTarget') {
    Invoke-R8RaxRdxControl -Name $vector -Fault $vector -ExpectedInline 6 -ExpectedVectors 1
}
Invoke-R8RaxRdxControl -Name 'AmbiguousOwner' -Fault 'AmbiguousOwner' -ExpectedError 'Overlapping PDB contributions cannot be attributed safely.'
foreach ($fault in 'MissingOwnerMetadata','TruncatedOwnerMetadata') {
    Invoke-R8RaxRdxControl -Name $fault -Fault $fault -ExpectedError 'Missing PDB module/contribution data.'
}
$failures = @($results | Where-Object { -not $_.Passed })
$accepted = @($results | Where-Object ExpectedPass).Count
$receipt = [pscustomobject]@{Candidate=$candidate;CandidateSha256=$candidateHash;CandidateSha256After=(Get-FileHash -LiteralPath $candidate -Algorithm SHA256).Hash;
    Helper=$helperPath;HelperSha256=$helperHash;Controls=$results.Count;Accepted=$accepted;Refused=$results.Count-$accepted;
    Passed=$results.Count-$failures.Count;Failed=$failures.Count;Results=@($results)}
$receipt | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $runRoot 'results.json') -Encoding utf8
Write-Host "SUMMARY isa-r8-rax-rdx controls=$($receipt.Controls) accepted=$accepted refused=$($receipt.Refused) passed=$($receipt.Passed) failed=$($receipt.Failed)"
Write-Host "RECEIPTS $runRoot"
if ($receipt.CandidateSha256After -ne $candidateHash) { throw 'Candidate changed during this run; receipts are not a stable-source qualification.' }
if ($failures.Count) { throw "$($failures.Count) bounded parser controls failed; all fixtures and logs retained at $runRoot" }
} finally { Stop-Transcript | Out-Null }