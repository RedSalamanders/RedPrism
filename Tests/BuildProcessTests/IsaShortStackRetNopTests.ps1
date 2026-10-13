[CmdletBinding()]
param(
    [string]$ScannerSource
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repo = $PSScriptRoot
while (-not (Test-Path -LiteralPath (Join-Path $repo 'Build/IsaScan.cs')) -or
    -not (Test-Path -LiteralPath (Join-Path $repo 'Tests/BuildProcessTests/IsaInlineTableTests.ps1'))) {
    $parent = Split-Path -Parent $repo
    if (-not $parent -or $parent -eq $repo) { throw 'Repository root not found.' }
    $repo = $parent
}
$primitivePath = Join-Path $repo 'Tests/BuildProcessTests/IsaInlineTableTests.ps1'
if ($ScannerSource) {
    if (-not (Test-Path -LiteralPath $ScannerSource -PathType Leaf)) { throw "Scanner source not found: $ScannerSource" }
    if ('RedPrismBuild.IsaScanner' -as [type]) { throw 'Run in a fresh PowerShell process when -ScannerSource is specified.' }
    Add-Type -Path (Resolve-Path -LiteralPath $ScannerSource).Path
} elseif (-not ('RedPrismBuild.IsaScanner' -as [type])) {
    Add-Type -Path (Join-Path $repo 'Build/IsaScan.cs')
}

# Import only the existing PE/PDB fixture-building functions. Parsing the source AST avoids
# executing the older IsaInlineTableTests.ps1 assertions as a side effect of reusing its primitives.
$tokens = $null
$parseErrors = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile($primitivePath, [ref]$tokens, [ref]$parseErrors)
if ($parseErrors.Count) { throw "Could not parse existing ISA fixture primitives: $($parseErrors[0].Message)" }
$primitiveNames = @('Set-U16','Set-U32','Set-U64','Put-Bytes','Hex-Bytes','New-SyntheticCase')
foreach ($name in $primitiveNames) {
    $definition = $ast.FindAll({ param($node) $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $name }, $true) |
        Select-Object -First 1
    if (-not $definition) { throw "Existing fixture primitive missing: $name" }
    Invoke-Expression $definition.Extent.Text
}

$script:fixtureRoot = Join-Path $repo ('.build/BuildProcessTests/IsaShortStackRetNop-' + [guid]::NewGuid().ToString('N'))
[void](New-Item -ItemType Directory -Path $script:fixtureRoot -Force)
$script:imageBase = [UInt64]0x140000000
$script:sectionRva = [UInt32]0x0010f000
$script:dispatchRva = [UInt32]($script:sectionRva + 0x800)
$script:tableRva = [UInt32]($script:sectionRva + 0x900)
$script:targetRva = [UInt32]($script:sectionRva + 0x8c5)
$script:guid = [guid]'c7608755-8d38-4ecb-9c8c-03207c2ea174'
$script:age = [UInt32]37
$script:isaCases = [Collections.Generic.List[string]]::new()
$script:isaAccepted = 0

function Set-ShortU32([byte[]]$Bytes,[int]$Offset,[UInt32]$Value) {
    [Array]::Copy([BitConverter]::GetBytes($Value),0,$Bytes,$Offset,4)
}
function Set-ShortBytes([byte[]]$Bytes,[int]$Offset,[byte[]]$Value) {
    [Array]::Copy($Value,0,$Bytes,$Offset,$Value.Length)
}
function Get-ShortRow([UInt32]$Rva,[byte[]]$Bytes,[string]$Text) {
    return ('{0:x}: {1} {2}' -f ($script:imageBase+$Rva),(Hex-Bytes $Bytes),$Text)
}

function New-ShortStackCase {
    param(
        [Parameter(Mandatory)][string]$Name,
        [string]$Fault = '',
        [ValidateRange(1,127)][int]$Maximum = 5
    )
    $seed = New-SyntheticCase ($Name + 'Seed') -PdbData
    $pe = [IO.File]::ReadAllBytes($seed.Binary)
    [UInt32]$section = $script:sectionRva
    [UInt32]$prefix = $section + $(if ($Fault -eq 'DefaultSharesCaseTarget') { 0x890 } else { 0x800 })
    [UInt32]$dispatch = $prefix + 12
    [UInt32]$table = $section + 0x900
    [UInt32]$caseTarget = $script:targetRva # low RVA bytes c5 f8 10 00 deliberately look like VEX
    [UInt32]$defaultTarget = if ($Fault -eq 'DefaultSharesCaseTarget') { $caseTarget } else { $prefix + 0x20 }
    [UInt32]$retAt = $table - 2
    if ($Fault -eq 'MisalignedTable') { $table++ ; $retAt = $table - 2 }
    [UInt32]$procedureEnd = $table + 4 * ($Maximum + 1)
    if ($Fault -eq 'ContributionBounds') { $procedureEnd = $table + 4 * ($Maximum + 1) }
    $procBegin = if ($Fault -eq 'DirectEntryBypass') { $prefix - 5 } else { $prefix }

    [byte[]]$cmp = @(0x83,0x7c,0x24,0x28,[byte]$Maximum)
    [byte[]]$ja = @(0x77,[byte]($defaultTarget-($prefix+7)))
    [byte[]]$movsxd = @(0x48,0x63,0x44,0x24,0x28)
    [byte[]]$lea = @(0x48,0x8d,0x0d,0,0,0,0)
    [byte[]]$load = @(0x8b,0x84,0x81,0,0,0,0)
    [byte[]]$add = @(0x48,0x03,0xc1)
    [byte[]]$jump = @(0xff,0xe0)
    Set-ShortBytes $lea 3 ([BitConverter]::GetBytes([Int32](-[Int64]($dispatch+7))))
    Set-ShortBytes $load 3 ([BitConverter]::GetBytes($table))

    switch ($Fault) {
        'WrongCmpOpcode' { $cmp[0] = 0x80 }
        'WrongCmpSlot' { $cmp[3]++ }
        'WrongJaOpcode' { $ja[0] = 0x76 }
        'WrongJaCondition' { $ja[0] = 0x72 }
        'WrongDefaultTarget' { $ja[1] = [byte](($prefix-($prefix+7)) -band 0xff) }
        'WrongMovsxdOpcode' { $movsxd[1] = 0x62 }
        'WrongMovsxdRegister' { $movsxd[2] = 0x4c }
        'WrongMovsxdSlot' { $movsxd[4]++ }
        'WrongImageBase' { $lea[3]++ }
        'WrongLoadOpcode' { $load[0] = 0x89 }
        'WrongLoadBaseIndex' { $load[2] = 0x91 }
        'WrongLoadTableRva' { $load[3]++ }
        'WrongAdd' { $add[2] = 0xc2 }
        'WrongJump' { $jump[1] = 0xe1 }
        'RangeZero' { $cmp[4] = 0 }
        'Range128' { $cmp[4] = 128 }
        'RangeSigned255' { $cmp[4] = 255 }
    }

    # Make the PE code/data bytes fully deterministic over this procedure, then stamp the exact
    # six-row short-stack dispatch and its terminal RET/NOP/DWORD table.
    [UInt32]$rawBegin = 0x400 + $procBegin - $section
    [UInt32]$rawEnd = 0x400 + $procedureEnd - $section
    for ([UInt32]$offset=$rawBegin; $offset -lt $rawEnd; $offset++) { $pe[$offset] = 0x90 }
    if ($Fault -eq 'DirectEntryBypass') {
        [byte[]]$entryJump = @(0xeb,[byte](($prefix+7-($procBegin+2)) -band 0xff))
        Set-ShortBytes $pe (0x400+$procBegin-$section) $entryJump
    }
    Set-ShortBytes $pe (0x400+$prefix-$section) $cmp
    Set-ShortBytes $pe (0x400+$prefix+5-$section) $ja
    Set-ShortBytes $pe (0x400+$prefix+7-$section) $movsxd
    Set-ShortBytes $pe (0x400+$dispatch-$section) $lea
    Set-ShortBytes $pe (0x400+$dispatch+7-$section) $load
    Set-ShortBytes $pe (0x400+$dispatch+14-$section) $add
    Set-ShortBytes $pe (0x400+$dispatch+17-$section) $jump
    $pe[0x400+$defaultTarget-$section] = 0x90
    $pe[0x400+$caseTarget-$section] = 0x90
    $pe[0x400+$retAt-$section] = 0xc3
    $pe[0x400+$retAt+1-$section] = 0x90
    if ($Fault -eq 'MissingRet') { $pe[0x400+$retAt-$section] = 0x90 }
    if ($Fault -eq 'WrongTerminalNop') { $pe[0x400+$retAt+1-$section] = 0xcc }
    if ($Fault -eq 'RealVectorBody') { Set-ShortBytes $pe (0x400+$script:targetRva-$section) ([byte[]]@(0xc5,0xf8,0x10,0x00)) }
    for ($i=0; $i -le $Maximum; ++$i) {
        [UInt32]$entry = $caseTarget
        if ($i -eq $Maximum -and $Fault -eq 'TargetOutsideProcedure') { $entry = $section + 0x1f00 }
        if ($i -eq $Maximum -and $Fault -eq 'TargetIntoGuard') { $entry = $prefix }
        if ($Fault -eq 'BadFinalTarget' -and $i -eq $Maximum) { $entry = $section + 0x1f00 }
        Set-ShortU32 $pe (0x400+$table-$section+4*$i) $entry
    }

    $rows = [Collections.Generic.List[string]]::new()
    if ($Fault -eq 'DirectEntryBypass') {
        [byte[]]$entryJump = $pe[(0x400+$procBegin-$section)..(0x400+$procBegin-$section+1)]
        $rows.Add((Get-ShortRow $procBegin $entryJump 'jmp bypasses guard'))
        for ($i=2; $i -lt 5; ++$i) { $rows.Add((Get-ShortRow ($procBegin+$i) ([byte[]]@($pe[0x400+$procBegin-$section+$i])) 'nop')) }
    }
    $guard = @(@{start=0; bytes=$cmp; text='cmp dword ptr [rsp+28h], imm8'},
               @{start=5; bytes=$ja; text='ja default'},
               @{start=7; bytes=$movsxd; text='movsxd rax, dword ptr [rsp+28h]'})
    for ($i=0; $i -lt $guard.Count; ++$i) {
        if ($Fault -eq "MissingGuard$i") { continue }
        [byte[]]$shown = $guard[$i].bytes.Clone()
        if ($Fault -eq "UnknownGuard$i") { $text='<unknown>' } else { $text=$guard[$i].text }
        if ($Fault -eq "ListingGuard$i") { $shown[$shown.Length-1] = $shown[$shown.Length-1] -bxor 1 }
        $rows.Add((Get-ShortRow ($prefix+$guard[$i].start) $shown $text))
    }
    if ($Fault -eq 'OverlappingGuard') { $rows.Add((Get-ShortRow ($prefix+1) ([byte[]]@(0x90)) 'nop')) }
    $dispatchRows = @(@{offset=0;bytes=$lea;text='lea rcx, [rip+imagebase]'},
                      @{offset=7;bytes=$load;text='mov eax, dword ptr [rcx+rax*4+table]'},
                      @{offset=14;bytes=$add;text='add rax, rcx'},
                      @{offset=17;bytes=$jump;text='jmp rax'})
    for ($i=0; $i -lt $dispatchRows.Count; ++$i) {
        [byte[]]$shown = $dispatchRows[$i].bytes.Clone()
        if ($Fault -eq "ListingDispatch$i") { $shown[$shown.Length-1] = $shown[$shown.Length-1] -bxor 1 }
        $rows.Add((Get-ShortRow ($dispatch+$dispatchRows[$i].offset) $shown $dispatchRows[$i].text))
    }
    if ($Fault -eq 'OverlappingDispatch') { $rows.Add((Get-ShortRow ($dispatch+1) ([byte[]]@(0x90)) 'nop')) }

    for ([UInt32]$rva=$dispatch+19; $rva -lt $retAt; ++$rva) {
        $flowAt = if ($Fault -in @('DirectFlowLoop','DirectFlowJrcxz','DirectFlowLegacyPrefix')) { $table-32 } else { $defaultTarget }
        if ($rva -eq $flowAt -and $Fault -in @('DirectFlowJump','DirectFlowCall','DirectFlowLoop','DirectFlowJrcxz','DirectFlowLegacyPrefix')) {
            switch ($Fault) {
                'DirectFlowJump' { [byte[]]$flow=@(0xe9,0,0,0,0); Set-ShortBytes $flow 1 ([BitConverter]::GetBytes([Int32]($table-($rva+5)))) }
                'DirectFlowCall' { [byte[]]$flow=@(0xe8,0,0,0,0); Set-ShortBytes $flow 1 ([BitConverter]::GetBytes([Int32]($table-($rva+5)))) }
                'DirectFlowLoop' { [byte[]]$flow=@(0xe2,[byte](($table-($rva+2)) -band 0xff)) }
                'DirectFlowJrcxz' { [byte[]]$flow=@(0xe3,[byte](($table-($rva+2)) -band 0xff)) }
                'DirectFlowLegacyPrefix' { [byte[]]$flow=@(0x2e,0x74,[byte](($table-($rva+3)) -band 0xff)) }
            }
            Set-ShortBytes $pe (0x400+$rva-$section) $flow
            $rows.Add((Get-ShortRow $rva $flow "direct flow $Fault"))
            $rva += [UInt32]($flow.Length-1)
            continue
        }
        if ($rva -eq $script:targetRva -and $Fault -eq 'MissingBody') { continue }
        if ($rva -eq $script:targetRva -and $Fault -in @('UnknownBody','TargetUnknownBody')) {
            $pe[0x400+$rva-$section] = 0x06
            $rows.Add((Get-ShortRow $rva ([byte[]]@($pe[0x400+$rva-$section])) '<unknown>'))
            continue
        }
        if ($rva -eq $script:targetRva -and $Fault -eq 'RealVectorBody') {
            $rows.Add((Get-ShortRow $rva ([byte[]]@(0xc5,0xf8,0x10,0x00)) 'vmovups xmm0, xmmword ptr [rax]'))
            $rva += 3
            continue
        }
        $rows.Add((Get-ShortRow $rva ([byte[]]@($pe[0x400+$rva-$section])) 'nop'))
    }
    $rows.Add((Get-ShortRow $retAt ([byte[]]@($pe[0x400+$retAt-$section])) 'ret'))
    $rows.Add((Get-ShortRow ($retAt+1) ([byte[]]@($pe[0x400+$retAt+1-$section])) 'nop'))
    for ($i=0; $i -le $Maximum; ++$i) {
        [UInt32]$at = $table+4*$i
        [byte[]]$entryBytes = $pe[(0x400+$at-$section)..(0x400+$at-$section+3)]
        if ($Fault -eq 'ListingTableMismatch' -and $i -eq 0) { $entryBytes[0] = $entryBytes[0] -bxor 1 }
        $rows.Add((Get-ShortRow $at $entryBytes 'vmovups xmm0, xmmword ptr [rax]'))
    }
    if ($Fault -eq 'CcStraddle') {
        $pe[0x400+$procedureEnd-$section] = 0xcc
        [byte[]]$crossing = $pe[(0x400+$procedureEnd-$section-4)..(0x400+$procedureEnd-$section)]
        $lastAt = $table+4*$Maximum
        $rows[$rows.Count-1] = Get-ShortRow $lastAt $crossing 'alignment row straddles table into cc'
    }
    if ($Fault -eq 'NonCcStraddle') {
        $pe[0x400+$procedureEnd-$section] = 0x90
        [byte[]]$crossing = $pe[(0x400+$procedureEnd-$section-4)..(0x400+$procedureEnd-$section)]
        $lastAt = $table+4*$Maximum
        $rows[$rows.Count-1] = Get-ShortRow $lastAt $crossing 'row crosses table into non-cc'
    }
    if ($Fault -eq 'NonCcAtTableEnd') {
        [byte[]]$tail = @(0x0f,0x1f,0x00); Set-ShortBytes $pe (0x400+$procedureEnd-$section) $tail
        $lastAt = $table+4*$Maximum; [byte[]]$crossing = $pe[(0x400+$lastAt-$section)..(0x400+$procedureEnd-$section+2)]
        $rows[$rows.Count-1] = Get-ShortRow $lastAt $crossing 'row crosses table into non-cc suffix'
    }
    if ($Fault -eq 'VectorAtTableEnd') { [byte[]]$tail=@(0xc5,0xf8,0x10,0x00); Set-ShortBytes $pe (0x400+$procedureEnd-$section) $tail; $rows.Add((Get-ShortRow $procedureEnd $tail 'vmovups xmm0, xmmword ptr [rax]')) }
    if ($Fault -eq 'ListingProcedureGap') { $rows.RemoveAt(12) }
    [UInt32]$reportedEnd = $procedureEnd
    if ($Fault -eq 'ShortProcedure') { $reportedEnd-- }
    if ($Fault -eq 'LongProcedure') { $reportedEnd++ }
    $procOwner = if ($Fault -eq 'WrongProcedureOwner') { 1 } else { 0 }
    $dataOwner = if ($Fault -in @('WrongTableOwner','WrongContributionOwner')) { 1 } else { 0 }
    $symbols = "  Mod 0 | ``SyntheticControl.obj``:`n"
    if ($Fault -ne 'MissingProcedure') {
        $symbols += "  Mod $procOwner | ``SyntheticControl.obj``:`n  0 | S_GPROC32 [size = 16] ``ShortStackSwitch`` `n  addr = 0001:$($procBegin-$section), code size = $($reportedEnd-$procBegin)`n"
    }
    if ($Fault -ne 'MissingAnonymousTable') {
        [UInt32]$symTable = if ($Fault -eq 'WrongAnonymousStart') { $table+4 } elseif ($Fault -eq 'InteriorAnonymousSymbol') { $table+8 } else { $table }
        $dataName = if ($Fault -eq 'NamedTable') { 'dispatchTable' } else { '' }
        $symbols += "  Mod $dataOwner | ``SyntheticControl.obj``:`n  0 | S_LDATA32 [size = 16] ``$dataName```n  type = <no type>, addr = 0001:$($symTable-$section)`n"
    }
    # A later same-owner anonymous record blocks the generic unguarded table reader from accepting
    # malformed guard fixtures. The new proof still has the exact table start and exact procedure end.
    $sentinelOwner = if ($Fault -eq 'WrongContributionOwner') { $dataOwner } else { 0 }
    $symbols += "  Mod $sentinelOwner | ``SyntheticControl.obj``:`n  0 | S_LDATA32 [size = 16] `````n  type = <no type>, addr = 0001:$($procedureEnd+4-$section)`n"
    if ($Fault -eq 'ContributionBounds') {
        $metadataText = Get-Content -LiteralPath $seed.Metadata -Raw
        $sizeMatches = [regex]::Matches($metadataText, 'size = 8192')
        if ($sizeMatches.Count -ne 1) { throw "ContributionBounds expected one size marker, found $($sizeMatches.Count)." }
        $metadataText = [regex]::Replace($metadataText, 'size = 8192','size = 2303')
        if ([regex]::Matches($metadataText, 'size = 2303').Count -ne 1 -or [regex]::Matches($metadataText, 'size = 8192').Count -ne 0) { throw 'ContributionBounds metadata substitution failed.' }
        Set-Content -LiteralPath $seed.Metadata -Value $metadataText -Encoding utf8
    }
    if ($Fault -eq 'WrongContributionOwner') {
        $metadataText = Get-Content -LiteralPath $seed.Metadata -Raw
        $matches = [regex]::Matches($metadataText, '(?m)^  SC\[\.text\].*size = 8192, data crc = 0, reloc crc = 0$')
        if ($matches.Count -ne 1) { throw "WrongContributionOwner expected one base contribution, found $($matches.Count)." }
        $split = [int]($prefix-$section+1)
        $first = "  SC[.text]  | mod = 0, 0001:0, size = $split, data crc = 0, reloc crc = 0"
        $second = "  SC[.text]  | mod = 1, 0001:$split, size = $(8192-$split), data crc = 0, reloc crc = 0"
        $headerMatches = [regex]::Matches($metadataText, '(?m)^Section Contributions$')
        if ($headerMatches.Count -ne 1) { throw "WrongContributionOwner expected one contribution header, found $($headerMatches.Count)." }
        if ([regex]::Matches($metadataText, '(?m)^  Mod 0001 \|').Count -ne 0) { throw 'WrongContributionOwner module 1 unexpectedly already exists.' }
        $metadataText = [regex]::Replace($metadataText, '(?m)^Section Contributions$', "  Mod 0001 | ``Other.obj``:`n  Obj: ``Other.obj```n`nSection Contributions")
        if ([regex]::Matches($metadataText, '(?m)^  Mod 0001 \|').Count -ne 1) { throw 'WrongContributionOwner module declaration substitution did not occur exactly once.' }
        $scMatches = [regex]::Matches($metadataText, '(?m)^  SC\[\.text\].*size = 8192, data crc = 0, reloc crc = 0$')
        if ($scMatches.Count -ne 1) { throw "WrongContributionOwner expected one original SC record, found $($scMatches.Count)." }
        $metadataText = [regex]::Replace($metadataText, '(?m)^  SC\[\.text\].*size = 8192, data crc = 0, reloc crc = 0$', "$first`n$second")
        if ([regex]::Matches($metadataText, [regex]::Escape($first)).Count -ne 1 -or
            [regex]::Matches($metadataText, [regex]::Escape($second)).Count -ne 1 -or
            [regex]::Matches($metadataText, 'size = 8192').Count -ne 0) { throw 'WrongContributionOwner contribution split substitution did not occur exactly once.' }
        Set-Content -LiteralPath $seed.Metadata -Value $metadataText -Encoding utf8
    }
    Set-Content -LiteralPath $seed.Symbols -Value $symbols -Encoding utf8
    if ($Fault -eq 'OverlapBody') { $rows.Add((Get-ShortRow ($defaultTarget+1) ([byte[]]@(0x90)) 'overlap')) }
    if ($Fault -eq 'ListingGuardMismatch') { $index=0; $rows[$index]=$rows[$index].Replace('83 7c','83 7d') }
    if ($Fault -eq 'ListingDispatchMismatch') { $index=3; $rows[$index]=$rows[$index].Replace('48 8d','48 8e') }
    if ($Fault -eq 'DuplicateRow') { $rows.Add($rows[0]) }
    Set-Content -LiteralPath $seed.Disassembly -Value (@($rows | Sort-Object { [UInt64]::Parse(($_ -split ':',2)[0],[Globalization.NumberStyles]::HexNumber) }) -join "`n") -Encoding utf8
    [IO.File]::WriteAllBytes($seed.Binary,$pe)
    $seed.Name = $Name
    $seed.TableBegin = $table
    $seed.TableEnd = $procedureEnd
    $seed.Maximum = $Maximum
    return $seed
}

function Invoke-ShortStackControl([string]$Name,[string]$Fault,[bool]$ExpectedPass,[int]$Maximum=5) {
    $case = New-ShortStackCase $Name $Fault $Maximum
    try {
        $result = [RedPrismBuild.IsaScanner]::Scan($case.Binary,$case.Metadata,$case.Symbols,$case.Disassembly,
            [string[]]@(),[string[]]@(),[string[]]@())
    } catch [IO.InvalidDataException] {
        if ($ExpectedPass) { throw }
        $script:isaCases.Add($Name)
        Write-Host "PASS isa-short-stack-ret-nop.$Name (bounded parser refusal)"
        return
    }
    if ($ExpectedPass -and $result.Violations.Count) { throw "${Name}: unexpected ISA rejection: $($result.Violations -join '; ')" }
    if (-not $ExpectedPass -and -not $result.Violations.Count) { throw "${Name}: malformed synthetic fixture was accepted." }
    if ($ExpectedPass -and ($result.InlineTableVectorData -ne ($Maximum+1) -or $result.VectorInstructions -ne 0)) {
        throw "${Name}: inline-vector proof/count mismatch; table=$($result.InlineTableVectorData), executable=$($result.VectorInstructions)."
    }
    $script:isaCases.Add($Name)
    if ($ExpectedPass) { ++$script:isaAccepted }
    Write-Host "PASS isa-short-stack-ret-nop.$Name"
}

Invoke-ShortStackControl 'SixEntryExactTerminalTable' '' $true 5
Invoke-ShortStackControl 'MaximumOneUnsignedEdge' '' $true 1
Invoke-ShortStackControl 'DefaultMayShareCaseTarget' 'DefaultSharesCaseTarget' $true 5
Invoke-ShortStackControl 'Maximum127UnsignedEdge' '' $true 127
Invoke-ShortStackControl 'LastDwordRowStraddlesOnlyIntoCc' 'CcStraddle' $true 127

$negativeFaults = @(
    'WrongCmpOpcode','WrongCmpSlot','WrongJaOpcode','WrongJaCondition','WrongDefaultTarget',
    'WrongMovsxdOpcode','WrongMovsxdRegister','WrongMovsxdSlot','WrongImageBase',
    'WrongLoadOpcode','WrongLoadBaseIndex','WrongLoadTableRva','WrongAdd','WrongJump',
    'RangeZero','Range128','RangeSigned255','MisalignedTable','ContributionBounds','WrongContributionOwner',
    'WrongProcedureOwner','WrongTableOwner','MissingProcedure','MissingAnonymousTable','NamedTable',
    'WrongAnonymousStart','InteriorAnonymousSymbol','ShortProcedure','LongProcedure',
    'TargetOutsideProcedure','TargetIntoGuard','BadFinalTarget','TargetUnknownBody','MissingBody','UnknownBody',
    'RealVectorBody','MissingGuard0','UnknownGuard1','MissingGuard2','ListingGuardMismatch','ListingDispatchMismatch',
    'ListingTableMismatch','ListingProcedureGap','OverlapBody','OverlappingGuard','OverlappingDispatch','DuplicateRow',
    'MissingRet','WrongTerminalNop','NonCcStraddle','NonCcAtTableEnd','VectorAtTableEnd',
    'DirectEntryBypass','DirectFlowJump','DirectFlowCall','DirectFlowLoop','DirectFlowJrcxz','DirectFlowLegacyPrefix'
)
foreach ($fault in $negativeFaults) { Invoke-ShortStackControl $fault $fault $false 5 }
Write-Host "SUMMARY isa-short-stack-ret-nop controls=$($script:isaCases.Count) accepted=$script:isaAccepted refused=$($script:isaCases.Count-$script:isaAccepted)"
