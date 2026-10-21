# Dispatch-form controls isolate the recognizers; the default image gate retains PDB-marked proof in IsaMarkedTableTests.ps1.
[CmdletBinding()]
param(
    [string]$ScannerSourcePath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if (-not (Test-Path -LiteralPath (Join-Path $repo 'Build/IsaScan.cs'))) {
    throw "RedPrism test repository root not found: $repo"
}
if ($ScannerSourcePath) {
    if ('RedPrismBuild.IsaScanner' -as [type]) {
        throw 'Use a fresh PowerShell process when specifying ScannerSourcePath.'
    }
    Add-Type -Path ([IO.Path]::GetFullPath($ScannerSourcePath))
}

# Reuse the repository's PE32+/RSDS/PDB synthetic builder and scanner invocation.
. (Join-Path $repo 'Tests/BuildProcessTests/IsaInlineTableFixtures.ps1')

$script:r13Root = Join-Path $repo ('.build/BuildProcessTests/IsaR13Terminal-' + [guid]::NewGuid().ToString('N'))
[void](New-Item -ItemType Directory -Path $script:r13Root -Force)
$script:r13Names = [Collections.Generic.List[string]]::new()
$script:r13Failures = [Collections.Generic.List[string]]::new()
$script:r13Accepted = 0

function R13-SetU32([byte[]]$Bytes, [int]$Offset, [UInt32]$Value) {
    [Array]::Copy([BitConverter]::GetBytes($Value), 0, $Bytes, $Offset, 4)
}

function R13-Put([byte[]]$Bytes, [int]$Offset, [byte[]]$Value) {
    [Array]::Copy($Value, 0, $Bytes, $Offset, $Value.Length)
}

function R13-Row([Collections.Generic.List[string]]$Rows, [UInt32]$Rva, [byte[]]$Bytes, [string]$Text) {
    $Rows.Add(('{0:x}: {1} {2}' -f ($script:imageBase + $Rva), (Hex-Bytes $Bytes), $Text))
}

function New-R13TerminalCase([string]$Name, [string]$Fault = '', [int]$Maximum = 19) {
    if ($Maximum -lt 0 -or $Maximum -gt 255) { throw "Fixture maximum outside byte range: $Maximum" }
    $dir = Join-Path $script:r13Root $Name
    [void](New-Item -ItemType Directory -Path $dir -Force)
    $seed = New-SyntheticCase ($Name + 'Seed') -PdbData
    $pe = [IO.File]::ReadAllBytes($seed.Binary)
    [UInt32]$section = $script:sectionRva
    [UInt32]$procedure = $section + 0x800
    [UInt32]$prefix = $procedure + 0x20
    [UInt32]$afterJump = $prefix + 30
    [UInt32]$target = $section + 0x8c5 # RVA bytes c5 f8 10 00 exercise data-only VEX classification.
    [UInt32]$firstData = $section + 0x900
    [UInt32]$priorBytes = if ($Fault -in @('AdjacentTables','AdjacentRetNop','PriorTargetGuard',
        'MissingPriorData','WrongPriorDataOwner','WrongPriorDataStart','NamedPriorData','TypedPriorData')) { 8 } else { 0 }
    [UInt32]$table = if ($Fault -eq 'TablePastContribution') { $section + 0x1ffc } else { $firstData + $priorBytes }
    if ($Fault -eq 'TablePastContribution') { $firstData = $table; $priorBytes = 0 }
    [UInt32]$end = $table + 4 * ($Maximum + 1)
    [UInt32]$codeEnd = if ($priorBytes -gt 0) { $firstData } else { $table }
    [UInt32]$tableRva = $table

    [byte[]]$cmp = @(0x41, 0x83, 0xfd, [byte]$Maximum)
    [byte[]]$ja = @(0x0f, 0x87, 0, 0, 0, 0)
    [byte[]]$lea = @(0x48, 0x8d, 0x15, 0, 0, 0, 0)
    [byte[]]$load = @(0x42, 0x8b, 0x8c, 0xaa, 0, 0, 0, 0)
    [byte[]]$add = @(0x48, 0x03, 0xca)
    [byte[]]$jump = @(0xff, 0xe1)
    $fallback = [UInt32]$target
    switch ($Fault) {
        'WrongCmpRex' { $cmp[0] = 0x40 }
        'WrongCmpOpcode' { $cmp[1] = 0x81 }
        'WrongCmpRegister' { $cmp[2] = 0xfc }
        'MaximumZero' { $cmp[3] = 0 }
        'MaximumNegative' { $cmp[3] = 255 }
        'Maximum128' { $cmp[3] = 128 }
        'WrongJa' { $ja[1] = 0x86 }
        'WrongLeaRex' { $lea[0] = 0x49 }
        'WrongLeaOpcode' { $lea[1] = 0x89 }
        'WrongLeaRegister' { $lea[2] = 0x0d }
        'WrongLoadRex' { $load[0] = 0x41 }
        'WrongLoadOpcode' { $load[1] = 0x89 }
        'WrongLoadRegister' { $load[2] = 0x84 }
        'WrongLoadSib' { $load[3] = 0xa2 }
        'WrongTableRva' { $tableRva += 4 }
        'WrongAddRex' { $add[0] = 0x49 }
        'WrongAddRegister' { $add[2] = 0xc2 }
        'WrongJumpRegister' { $jump[1] = 0xe2 }
        'DefaultBeforeJump' { $fallback = $prefix + 28 }
        'DefaultInGuardInterior' { $fallback = $prefix + 1 }
        'DefaultAtData' { $fallback = $table }
    }
    [Array]::Copy([BitConverter]::GetBytes([Int32]([Int64]$fallback - [Int64]($prefix + 10))), 0, $ja, 2, 4)
    [Array]::Copy([BitConverter]::GetBytes([Int32](-[Int64]($prefix + 17))), 0, $lea, 3, 4)
    if ($Fault -eq 'WrongImageBase') { $lea[3] = $lea[3] -bxor 1 }
    R13-SetU32 $load 4 $tableRva

    # Keep decoded code contiguous through the first data symbol. A terminal RET is
    # required before any optional earlier DWORD table span.
    for ([UInt32]$rva = $procedure; $rva -lt $firstData; ++$rva) {
        $pe[0x400 + $rva - $section] = 0x90
    }
    $terminalRet = $codeEnd - 1
    if ($Fault -eq 'AdjacentRetNop') {
        $terminalRet = $codeEnd - 4
        $pe[0x400 + $terminalRet - $section] = 0xc3
        R13-Put $pe (0x400 + $terminalRet + 1 - $section) ([byte[]]@(0x0f, 0x1f, 0x00))
    } else {
        $pe[0x400 + $terminalRet - $section] = 0xc3
    }
    R13-Put $pe (0x400 + $prefix - $section) $cmp
    R13-Put $pe (0x400 + $prefix + 4 - $section) $ja
    R13-Put $pe (0x400 + $prefix + 10 - $section) $lea
    R13-Put $pe (0x400 + $prefix + 17 - $section) $load
    R13-Put $pe (0x400 + $prefix + 25 - $section) $add
    R13-Put $pe (0x400 + $prefix + 28 - $section) $jump
    R13-Put $pe (0x400 + $target - $section) ([byte[]]@(0x0f, 0x1f, 0x40, 0x00))

    if ($Fault -eq 'RealVexBeforeData') {
        R13-Put $pe (0x400 + $target - $section) ([byte[]]@(0xc5, 0xf8, 0x10, 0x00))
    }
    if ($Fault -eq 'RealVexAdjacent') {
        R13-Put $pe (0x400 + $end - $section) ([byte[]]@(0xc5, 0xf8, 0x10, 0x00))
    }
    if ($Fault -eq 'RealEvexAdjacent') {
        R13-Put $pe (0x400 + $end - $section) ([byte[]]@(0x62, 0xf1, 0x7c, 0x48, 0x10, 0x00))
    }

    $branchFaults = @(
        'ShortJmpGuard', 'ShortJccGuard', 'NearJccGuard', 'NearCallGuard', 'NearJmpGuard',
        'LoopE0Guard', 'LoopE1Guard', 'LoopE2Guard', 'LoopE3Guard', 'PrefixedLoopGuard',
        'NearCallTable', 'NearJmpTable', 'NearJccTable', 'ShortJmpTable', 'PrefixedLoopTable'
    )
    [byte[]]$branch = @()
    [UInt32]$branchSource = 0
    if ($Fault -in $branchFaults) {
        $toTable = $Fault.EndsWith('Table', [StringComparison]::Ordinal)
        $branchSource = if ($Fault.EndsWith('Table', [StringComparison]::Ordinal) -and $Fault -ne 'NearCallTable' -and
            $Fault -ne 'NearJmpTable' -and $Fault -ne 'NearJccTable') { $table - 10 } else { $procedure }
        [UInt32]$destination = if ($toTable) { $table } else { $prefix + 1 }
        $branch = switch -Regex ($Fault) {
            '^ShortJmp' { [byte[]]@(0xeb, 0); break }
            '^ShortJcc' { [byte[]]@(0x74, 0); break }
            '^NearJcc' { [byte[]]@(0x0f, 0x85, 0, 0, 0, 0); break }
            '^NearCall' { [byte[]]@(0xe8, 0, 0, 0, 0); break }
            '^NearJmp' { [byte[]]@(0xe9, 0, 0, 0, 0); break }
            '^LoopE0' { [byte[]]@(0xe0, 0); break }
            '^LoopE1' { [byte[]]@(0xe1, 0); break }
            '^LoopE2' { [byte[]]@(0xe2, 0); break }
            '^LoopE3' { [byte[]]@(0xe3, 0); break }
            '^PrefixedLoop' { [byte[]]@(0x67, 0xe3, 0); break }
        }
        if ($branch.Length -eq 2) {
            $branch[1] = [byte]([Int64]$destination - [Int64]($branchSource + 2))
        } elseif ($branch.Length -eq 3) {
            $branch[2] = [byte]([Int64]$destination - [Int64]($branchSource + 3))
        } elseif ($branch.Length -eq 5) {
            R13-SetU32 $branch 1 ([UInt32]([Int64]$destination - [Int64]($branchSource + 5)))
        } else {
            R13-SetU32 $branch 2 ([UInt32]([Int64]$destination - [Int64]($branchSource + 6)))
        }
        R13-Put $pe (0x400 + $branchSource - $section) $branch
    }

    $parts = @(
        @{Rva=$prefix; Bytes=$cmp; Text='cmp r13d, maximum'},
        @{Rva=$prefix+4; Bytes=$ja; Text='ja default'},
        @{Rva=$prefix+10; Bytes=$lea; Text='lea rdx, [rip + imagebase]'},
        @{Rva=$prefix+17; Bytes=$load; Text='mov ecx, dword ptr [rdx + 4*r13 + table]'},
        @{Rva=$prefix+25; Bytes=$add; Text='add rcx, rdx'},
        @{Rva=$prefix+28; Bytes=$jump; Text='jmp rcx'}
    )
    $rows = [Collections.Generic.List[string]]::new()
    for ([UInt32]$rva = $procedure; $rva -lt $firstData;) {
        $part = $null
        $rowIndex = -1
        for ($i = 0; $i -lt $parts.Count; ++$i) {
            if ($parts[$i].Rva -eq $rva) { $part = $parts[$i]; $rowIndex = $i; break }
        }
        if ($null -ne $part) {
            [byte[]]$shown = [byte[]]$part.Bytes.Clone()
            if ($Fault -eq "MissingGuard$rowIndex") { $rva += $shown.Length; continue }
            if ($Fault -eq "MismatchGuard$rowIndex") { $shown[$shown.Length - 1] = $shown[$shown.Length - 1] -bxor 1 }
            if ($Fault -eq "UnknownGuard$rowIndex") { $part.Text = '<unknown>' }
            R13-Row $rows $rva $shown $part.Text
            if ($Fault -eq "OverlapGuard$rowIndex") { R13-Row $rows ($rva + 1) ([byte[]]@(0x90)) 'nop' }
            if ($Fault -eq "DuplicateGuard$rowIndex") { R13-Row $rows $rva $shown $part.Text }
            if ($Fault -eq "CorruptPeGuard$rowIndex") { $pe[0x400 + $rva - $section] = $pe[0x400 + $rva - $section] -bxor 1 }
            $rva += $part.Bytes.Length
            continue
        }
        if ($Fault -in @('UnknownBody','TargetUnknownBody') -and $rva -eq $procedure + 5) {
            R13-Row $rows $rva ([byte[]]@(0x90)) '<unknown>'
            ++$rva
            continue
        }
        if ($Fault -eq 'MissingBody' -and $rva -eq $procedure + 5) { ++$rva; continue }
        if ($Fault -in $branchFaults -and $rva -eq $branchSource) {
            # The branch bytes were written into PE above. Recreate their literal row from PE.
            $branchBytes = [byte[]]$pe[(0x400 + $rva - $section)..(0x400 + $rva - $section + $branch.Length - 1)]
            R13-Row $rows $rva $branchBytes 'direct branch control'
            $rva += $branch.Length
            continue
        }
        if ($rva -eq $target -and $Fault -eq 'RealVexBeforeData') {
            R13-Row $rows $rva ([byte[]]@(0xc5, 0xf8, 0x10, 0x00)) 'vmovups xmm0, xmmword ptr [rax]'
            $rva += 4
            continue
        }
        if ($rva -eq $target) {
            R13-Row $rows $rva ([byte[]]@(0x0f, 0x1f, 0x40, 0x00)) 'nop dword ptr [rax]'
            $rva += 4
            continue
        }
        if ($rva -eq $terminalRet) {
            R13-Row $rows $rva ([byte[]]@(0xc3)) 'ret'
            ++$rva
            continue
        }
        if ($Fault -eq 'AdjacentRetNop' -and $rva -eq $codeEnd - 3) {
            R13-Row $rows $rva ([byte[]]@(0x0f, 0x1f, 0x00)) 'nop dword ptr [rax]'
            $rva += 3
            continue
        }
        if ($rva -eq $procedure + 5 -and $Fault -eq 'MismatchBody') {
            R13-Row $rows $rva ([byte[]]@(0x91)) 'xchg ecx, eax'
            ++$rva
            continue
        }
        R13-Row $rows $rva ([byte[]]@(0x90)) 'nop'
        ++$rva
    }

    if ($Fault -eq 'RealVexAdjacent') {
        R13-Row $rows $end ([byte[]]@(0xc5, 0xf8, 0x10, 0x00)) 'vmovups xmm0, xmmword ptr [rax]'
    }
    if ($Fault -eq 'RealEvexAdjacent') {
        R13-Row $rows $end ([byte[]]@(0x62, 0xf1, 0x7c, 0x48, 0x10, 0x00)) 'vmovups zmm0, zmmword ptr [rax]'
    }

    [UInt32]$entryTarget = $target
    switch ($Fault) {
        'TargetBeforeJump' { $entryTarget = $prefix + 28 }
        'TargetGuardInterior' { $entryTarget = $prefix + 1 }
        'TargetAtData' { $entryTarget = $table }
        'TargetOutsideProcedure' { $entryTarget = $section + 0x1500 }
        'TargetUnknownBody' { $entryTarget = $procedure + 5 }
    }
    $targetRva = $target

    if ($priorBytes -gt 0) {
        [UInt32]$priorTarget = $section + 0x890
        for ($i = 0; $i -lt ($priorBytes / 4); ++$i) {
            R13-SetU32 $pe (0x400 + $firstData - $section + 4 * $i) $priorTarget
            R13-Row $rows ($firstData + 4 * $i) ([BitConverter]::GetBytes($priorTarget)) '<unknown>'
        }
        if ($Fault -eq 'PriorTargetGuard') {
            R13-SetU32 $pe (0x400 + $firstData - $section) ($prefix + 1)
        }
    }

    for ($i = 0; $i -le $Maximum; ++$i) {
        [UInt32]$entry = $targetRva
        if ($i -eq $Maximum) { $entry = $entryTarget }
        $entryOffset = 0x400 + $table - $section + 4 * $i
        if ($entryOffset -le $pe.Length - 4) { R13-SetU32 $pe $entryOffset $entry }
        [byte[]]$entryBytes = [BitConverter]::GetBytes($entry)
        if ($Fault -eq 'MismatchTableRow' -and $i -eq 0) { $entryBytes[0] = $entryBytes[0] -bxor 1 }
        if ($Fault -eq 'UnknownTableRow' -and $i -eq 0) {
            R13-Row $rows ($table + 4 * $i) $entryBytes '<unknown>'
        } else {
            R13-Row $rows ($table + 4 * $i) $entryBytes 'vmovups xmm0, xmmword ptr [rax]'
        }
    }
    if ($Fault -eq 'OverlapTableRow') { R13-Row $rows ($table + 1) ([byte[]]@(0x90)) 'nop' }
    if ($Fault -eq 'DuplicateTableRow') { R13-Row $rows $table ([BitConverter]::GetBytes($targetRva)) 'vmovups xmm0, xmmword ptr [rax]' }
    if ($Fault -eq 'CorruptTablePe') { $pe[0x400 + $table - $section] = $pe[0x400 + $table - $section] -bxor 1 }

    $symbolText = "  Mod 0 | ``SyntheticControl.obj``:`n" +
        "  0 | S_GPROC32 [size = 16] ``R13Terminal`` `n  addr = 0001:$($procedure-$section), code size = $($end-$procedure)`n"
    if ($Fault -eq 'MissingProcedure') { $symbolText = '' }
    if ($Fault -eq 'WrongProcedureOwner') { $symbolText = $symbolText.Replace('Mod 0 |', 'Mod 1 |') }
    if ($Fault -eq 'WrongProcedureStart') { $symbolText = $symbolText.Replace("addr = 0001:$($procedure-$section)", "addr = 0001:$($procedure-$section+1)") }
    if ($Fault -eq 'ShortProcedure') { $symbolText = $symbolText.Replace("code size = $($end-$procedure)", "code size = $($end-$procedure-4)") }
    if ($Fault -eq 'LongProcedure') { $symbolText = $symbolText.Replace("code size = $($end-$procedure)", "code size = $($end-$procedure+4)") }
    if ($Fault -eq 'NamedData') { $dataName = 'dispatch' } else { $dataName = '' }
    if ($Fault -eq 'TypedData') { $dataType = 'T_NOTYPE (0x0000)' } else { $dataType = '<no type>' }
    [UInt32]$dataSymbol = if ($Fault -eq 'WrongDataStart') { $table + 4 } else { $table }
    if ($Fault -ne 'MissingData') {
        $dataOwner = if ($Fault -eq 'WrongDataOwner') { 1 } else { 0 }
        $encodedName = if ($dataName) { '``' + $dataName + '``' } else { '``' }
        $symbolText += "  Mod $dataOwner | ``SyntheticControl.obj``:`n  0 | S_LDATA32 [size = 16] $encodedName`n  type = $dataType, addr = 0001:$($dataSymbol-$section)`n"
    }
    if ($priorBytes -gt 0 -and $Fault -ne 'MissingPriorData') {
        $priorOwner = if ($Fault -eq 'WrongPriorDataOwner') { 1 } else { 0 }
        $priorName = if ($Fault -eq 'NamedPriorData') { 'prior' } else { '' }
        $priorType = if ($Fault -eq 'TypedPriorData') { 'T_NOTYPE (0x0000)' } else { '<no type>' }
        $priorStart = if ($Fault -eq 'WrongPriorDataStart') { $firstData + 4 } else { $firstData }
        $encodedPriorName = if ($priorName) { '``' + $priorName + '``' } else { '``' }
        $symbolText += "  Mod $priorOwner | ``SyntheticControl.obj``:`n  0 | S_LDATA32 [size = 16] $encodedPriorName`n  type = $priorType, addr = 0001:$($priorStart-$section)`n"
    }
    if ($Fault -eq 'InternalDataSymbol') {
        $anonymousName = '``'
        $symbolText += "  Mod 0 | ``SyntheticControl.obj``:`n  0 | S_LDATA32 [size = 16] $anonymousName`n  type = <no type>, addr = 0001:$($table+4-$section)`n"
    }
    if ($Fault -eq 'EarlyFirstData') {
        $anonymousName = '``'
        $symbolText += "  Mod 0 | ``SyntheticControl.obj``:`n  0 | S_LDATA32 [size = 16] $anonymousName`n  type = <no type>, addr = 0001:$($target+4-$section)`n"
    }
    if ($Fault -eq 'DuplicateData') {
        $anonymousName = '``'
        $symbolText += "  Mod 0 | ``SyntheticControl.obj``:`n  0 | S_LDATA32 [size = 16] $anonymousName`n  type = <no type>, addr = 0001:$($table-$section)`n"
    }
    if ($Fault -eq 'WrongContributionOwner') {
        $metadataText = Get-Content -LiteralPath $seed.Metadata -Raw
        $metadataText = $metadataText.Replace('mod = 0, 0001:0', 'mod = 1, 0001:0')
        Set-Content -LiteralPath $seed.Metadata -Value $metadataText -Encoding utf8
    }
    if ($Fault -eq 'MissingContribution') {
        $metadataText = Get-Content -LiteralPath $seed.Metadata -Raw
        $metadataText = [regex]::Replace($metadataText, '(?m)^\s*SC\[.*\r?\n', '')
        Set-Content -LiteralPath $seed.Metadata -Value $metadataText -Encoding utf8
    }
    if ($Fault -eq 'OverlapContribution') {
        $metadataText = Get-Content -LiteralPath $seed.Metadata -Raw
        $line = [regex]::Match($metadataText, '(?m)^\s*SC\[.*$').Value
        $metadataText += "`n$line`n"
        Set-Content -LiteralPath $seed.Metadata -Value $metadataText -Encoding utf8
    }

    if ($Fault -in @('CorruptPeGuard0','CorruptPeGuard1','CorruptPeGuard2','CorruptPeGuard3','CorruptPeGuard4','CorruptPeGuard5')) {
        # The row generator corrupts PE after the independent listing was recorded.
    }
    $rows = [Collections.Generic.List[string]]@($rows | Sort-Object {
        [UInt64]::Parse(($_ -split ':', 2)[0], [Globalization.NumberStyles]::HexNumber)
    })
    Set-Content -LiteralPath $seed.Symbols -Value $symbolText -Encoding utf8
    Set-Content -LiteralPath $seed.Disassembly -Value ($rows -join "`n") -Encoding utf8
    [IO.File]::WriteAllBytes($seed.Binary, $pe)
    return @{Name=$Name; Binary=$seed.Binary; Metadata=$seed.Metadata; Symbols=$seed.Symbols; Disassembly=$seed.Disassembly}
}

function Invoke-R13Control([string]$Name, [string]$Fault, [int]$Maximum, [bool]$ExpectedPass,
    [int]$ExpectedInline = -1, [int]$ExpectedVectors = -1) {
    $case = $null
    $failure = $null
    try {
        $case = New-R13TerminalCase $Name $Fault $Maximum
        $result = [RedPrismBuild.IsaScanner]::Scan($case.Binary, $case.Metadata, $case.Symbols, $case.Disassembly,
            [string[]]@(), [string[]]@(), [string[]]@(), $false)
    } catch {
        $expectedError = switch ($Fault) {
            'MissingContribution' { 'Missing PDB module/contribution data.' }
            'WrongContributionOwner' { 'Contribution refers to an absent module.' }
            'OverlapContribution' { 'Overlapping PDB contributions cannot be attributed safely.' }
            default { $null }
        }
        $cause = $_.Exception
        while ($cause.InnerException) { $cause = $cause.InnerException }
        if (-not $ExpectedPass -and $expectedError -and $cause -is [IO.InvalidDataException] -and
            $cause.Message -eq $expectedError) {
            $script:r13Names.Add($Name)
            Write-Host "PASS isa-r13-terminal.$Name (exact InvalidDataException refusal)"
            return
        }
        $failure = "scanner threw while parsing the literal control: $($_.Exception.Message)"
    }
    if ($null -eq $failure) {
        if ($ExpectedPass -and $result.Violations.Count -ne 0) {
            $failure = "expected parser acceptance, got $($result.Violations -join '; ')"
        } elseif (-not $ExpectedPass -and $result.Violations.Count -eq 0) {
            $failure = 'malformed or vector-containing parser control was accepted'
        } elseif ($ExpectedInline -ge 0 -and $result.InlineTableVectorData -ne $ExpectedInline) {
            $failure = "expected $ExpectedInline inline-table vector rows, got $($result.InlineTableVectorData)"
        } elseif ($ExpectedVectors -ge 0 -and $result.VectorInstructions -ne $ExpectedVectors) {
            $failure = "expected $ExpectedVectors vector rows, got $($result.VectorInstructions)"
        }
    }
    $script:r13Names.Add($Name)
    if ($null -eq $failure) {
        if ($ExpectedPass) { ++$script:r13Accepted }
        Write-Host "PASS isa-r13-terminal.$Name"
    } else {
        $script:r13Failures.Add("${Name}: $failure")
        Write-Host "FAIL isa-r13-terminal.$Name - $failure"
    }
}

# Independent literal accepted boundaries, with hard-coded expected row totals.
Invoke-R13Control 'Maximum1' '' 1 $true 2 0
Invoke-R13Control 'ObservedMaximum19' '' 19 $true 20 0
Invoke-R13Control 'Maximum127' '' 127 $true 128 0
Invoke-R13Control 'SecondAdjacentTable' 'AdjacentTables' 19 $true 20 0
Invoke-R13Control 'SecondAdjacentTableRetNop' 'AdjacentRetNop' 19 $true 20 0

# Corrupt each of the six independently reported terminal rows in five different ways.
foreach ($i in 0..5) {
    foreach ($kind in 'MissingGuard', 'MismatchGuard', 'UnknownGuard', 'OverlapGuard', 'DuplicateGuard') {
        $fault = "$kind$i"
        Invoke-R13Control $fault $fault 19 $false
    }
}
foreach ($i in 0..5) { Invoke-R13Control "CorruptPeGuard$i" "CorruptPeGuard$i" 19 $false }

$refused = @(
    'WrongCmpRex','WrongCmpOpcode','WrongCmpRegister','MaximumZero','MaximumNegative','Maximum128',
    'WrongJa','WrongLeaRex','WrongLeaOpcode','WrongLeaRegister','WrongImageBase',
    'WrongLoadRex','WrongLoadOpcode','WrongLoadRegister','WrongLoadSib','WrongTableRva',
    'WrongAddRex','WrongAddRegister','WrongJumpRegister','DefaultBeforeJump','DefaultInGuardInterior','DefaultAtData',
    'TargetBeforeJump','TargetGuardInterior','TargetAtData','TargetOutsideProcedure','TargetUnknownBody',
    'MissingProcedure','WrongProcedureOwner','ShortProcedure','LongProcedure',
    'MissingData','WrongDataOwner','WrongDataStart','NamedData','TypedData','InternalDataSymbol','EarlyFirstData',
    'MissingContribution','WrongContributionOwner','OverlapContribution',
    'TablePastContribution','MissingBody','UnknownBody','MismatchBody','MismatchTableRow','UnknownTableRow',
    'OverlapTableRow','DuplicateTableRow','CorruptTablePe',
    'PriorTargetGuard','MissingPriorData','WrongPriorDataOwner',
    'WrongPriorDataStart','NamedPriorData','TypedPriorData'
)
foreach ($fault in $refused) { Invoke-R13Control $fault $fault 19 $false }

# Direct branches, calls, legacy-prefixed branches, and LOOP-family branches may not
# enter the guard interior or either the selected or same-procedure table range.
foreach ($fault in @(
    'ShortJmpGuard','ShortJccGuard','NearJccGuard','NearCallGuard','NearJmpGuard',
    'LoopE0Guard','LoopE1Guard','LoopE2Guard','LoopE3Guard','PrefixedLoopGuard',
    'NearCallTable','NearJmpTable','NearJccTable','ShortJmpTable','PrefixedLoopTable'
)) { Invoke-R13Control $fault $fault 19 $false }

# Real vector encodings remain ordinary violations next to the table and in code before data.
Invoke-R13Control 'RealVexBeforeData' 'RealVexBeforeData' 19 $false 20 1
Invoke-R13Control 'RealVexAdjacent' 'RealVexAdjacent' 19 $false 20 1
Invoke-R13Control 'RealEvexAdjacent' 'RealEvexAdjacent' 19 $false 20 1

Write-Host "SUMMARY isa-r13-terminal controls=$($script:r13Names.Count) accepted=$script:r13Accepted refused=$($script:r13Names.Count-$script:r13Accepted) failures=$($script:r13Failures.Count)"
if ($script:r13Failures.Count -gt 0) { throw ($script:r13Failures -join "`n") }
