[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Independent parser fixture for the exact RET + 90 + aligned DWORD-table layout.
# These byte arrays and listings are controls; nothing is executed as native code.
$isaRetNopRepo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if (-not (Test-Path -LiteralPath (Join-Path $isaRetNopRepo 'Build\IsaScan.cs'))) {
    throw "Managed RedPrism root not found from sidecar: $isaRetNopRepo"
}
$isaRetNopFixtureRoot = Join-Path $isaRetNopRepo ('.build/BuildProcessTests/IsaQwordRetNop-r1-' + [guid]::NewGuid().ToString('N'))
[void](New-Item -ItemType Directory -Path $isaRetNopFixtureRoot -Force)
if (-not ('RedPrismBuild.IsaScanner' -as [type])) {
    Add-Type -Path (Join-Path $isaRetNopRepo 'Build/IsaScan.cs')
}
$script:isaRetNopImageBase = [UInt64]0x140000000
$script:isaRetNopSectionRva = [UInt32]0x006a2000
$script:isaRetNopGuid = [guid]'9c1e2d4b-6a70-4a8b-9012-3456789abcde'
$script:isaRetNopAge = [UInt32]7

function Set-IsaRetNopU16([byte[]]$Bytes,[int]$Offset,[UInt16]$Value) {
    [Array]::Copy([BitConverter]::GetBytes($Value),0,$Bytes,$Offset,2)
}
function Set-IsaRetNopU32([byte[]]$Bytes,[int]$Offset,[UInt32]$Value) {
    [Array]::Copy([BitConverter]::GetBytes($Value),0,$Bytes,$Offset,4)
}
function Set-IsaRetNopU64([byte[]]$Bytes,[int]$Offset,[UInt64]$Value) {
    [Array]::Copy([BitConverter]::GetBytes($Value),0,$Bytes,$Offset,8)
}
function Put-IsaRetNopBytes([byte[]]$Bytes,[int]$Offset,[byte[]]$Value) {
    [Array]::Copy($Value,0,$Bytes,$Offset,$Value.Length)
}
function Format-IsaRetNopBytes([byte[]]$Bytes) {
    return (($Bytes | ForEach-Object { $_.ToString('x2') }) -join ' ')
}

function New-IsaQwordRetNopCase([string]$Name,[string]$Fault='',[int]$Maximum=16) {
    if ($Maximum -lt 1 -or $Maximum -gt 127) { throw 'Maximum must be in [1,127].' }
    $directory=Join-Path $isaRetNopFixtureRoot $Name
    [void](New-Item -ItemType Directory -Path $directory -Force)
    $binary=Join-Path $directory 'SyntheticIsaQwordRetNop.exe'
    $metadata=Join-Path $directory 'contributions.txt'
    $symbols=Join-Path $directory 'symbols.txt'
    $disassembly=Join-Path $directory 'disassembly.txt'
    $s=[UInt32]$script:isaRetNopSectionRva
    [UInt32]$prefix=$s+0x880
    [UInt32]$dispatch=$prefix+15
    [UInt32]$table=$s+0xc2c
    [UInt32]$ret=$table-2
    [UInt32]$padding=$ret+1
    [UInt32]$target=$s+0xbec
    [UInt32]$vectorTarget=$s+0x9c5
    [UInt32]$end=$table+4*($Maximum+1)

    # PE32+ image with one executable section and an RSDS record, matching the scanner fixture contract.
    [byte[]]$pe=[byte[]]::new(0x2400)
    $pe[0]=0x4d; $pe[1]=0x5a
    Set-IsaRetNopU32 $pe 0x3c 0x80
    Set-IsaRetNopU32 $pe 0x80 0x00004550
    Set-IsaRetNopU16 $pe 0x84 0x8664
    Set-IsaRetNopU16 $pe 0x86 1
    Set-IsaRetNopU16 $pe 0x94 0xf0
    Set-IsaRetNopU16 $pe 0x98 0x20b
    Set-IsaRetNopU64 $pe (0x98+24) $script:isaRetNopImageBase
    Set-IsaRetNopU32 $pe (0x98+56) ($s+0x2000)
    Set-IsaRetNopU32 $pe (0x98+60) 0x400
    Set-IsaRetNopU32 $pe (0x98+112+6*8) ($s+0x180)
    Set-IsaRetNopU32 $pe (0x98+116+6*8) 28
    $section=0x80+24+0xf0
    Put-IsaRetNopBytes $pe $section ([Text.Encoding]::ASCII.GetBytes('.text'+[char]0+[char]0+[char]0))
    Set-IsaRetNopU32 $pe ($section+8) 0x2000
    Set-IsaRetNopU32 $pe ($section+12) $s
    Set-IsaRetNopU32 $pe ($section+16) 0x2000
    Set-IsaRetNopU32 $pe ($section+20) 0x400
    Set-IsaRetNopU32 $pe ($section+36) 0x60000020
    Set-IsaRetNopU32 $pe (0x400+0x180+12) 2
    Set-IsaRetNopU32 $pe (0x400+0x180+16) 24
    Set-IsaRetNopU32 $pe (0x400+0x180+20) ($s+0x1a0)
    Set-IsaRetNopU32 $pe (0x400+0x180+24) 0x5a0
    Put-IsaRetNopBytes $pe 0x5a0 ([Text.Encoding]::ASCII.GetBytes('RSDS'))
    Put-IsaRetNopBytes $pe 0x5a4 $script:isaRetNopGuid.ToByteArray()
    Set-IsaRetNopU32 $pe 0x5b4 $script:isaRetNopAge

    # The dispatch bytes use the exact seven decoded guard/dispatch instructions from QwordStackRax.
    # Exact terminal RVAs are RET 0x006a2c2a, NOP 0x006a2c2b, table 0x006a2c2c.
    [byte[]]$code=@(0x48,0x83,0xbc,0x24,0x28,0x01,0,0,[byte]$Maximum,
        0x0f,0x87,0,0,0,0,0x48,0x8d,0x05,0,0,0,0,
        0x48,0x8b,0x8c,0x24,0x28,0x01,0,0,0x8b,0x8c,0x88,0,0,0,0,0x48,0x03,0xc8,0xff,0xe1)
    Put-IsaRetNopBytes $code 11 ([BitConverter]::GetBytes([int32]($target-($prefix+15))))
    Put-IsaRetNopBytes $code 18 ([BitConverter]::GetBytes([int32](-[int64]($dispatch+7))))
    Set-IsaRetNopU32 $code 33 $table
    [byte[]]$paddingBytes=@(0x90)
    switch ($Fault) {
        'DwordCmp' {$code[0]=0x40}
        'WrongCmp' {$code[2]=0xbd}
        'WrongCmpSib' {$code[3]=0x25}
        'WrongJa' {$code[10]=0x86}
        'SignedJa' {$code[10]=0x8f}
        'WrongBase' {$code[17]=0x0d}
        'WrongImageBase' {$code[18]++}
        'WrongIndexRegister' {$code[24]=0x84}
        'DwordIndex' {$code[22]=0x40}
        'WrongIndexSib' {$code[25]=0x25}
        'StackByte0' {$code[26]++}
        'StackByte1' {$code[27]++}
        'StackByte2' {$code[28]++}
        'StackByte3' {$code[29]++}
        'WrongLoad' {$code[30]=0x89}
        'WrongLoadSib' {$code[32]=0x89}
        'WrongAdd' {$code[39]=0xc1}
        'WrongJump' {$code[41]=0xe0}
        'ZeroRange' {$code[8]=0}
        'Range128' {$code[8]=128}
        'Range255' {$code[8]=255}
        'WrongNop' {$paddingBytes[0]=0x91}
        'NopLengthTwo' {$paddingBytes=[byte[]]@(0x0f,0x1f)}
        'NopLengthFour' {$paddingBytes=[byte[]]@(0x0f,0x1f,0x00,0x90)}
        'VexInPadding' {$paddingBytes=[byte[]]@(0xc5,0xf8,0x77)}
        'MissingPadding' {$paddingBytes=[byte[]]::new(0)}
        'MissingNop' {$paddingBytes=[byte[]]::new(0)}
        'ExtraPadding' {$paddingBytes=[byte[]]@(0x90,0x90)}
        'UnmodeledTerminalBranch' {$paddingBytes=[byte[]]@(0xe0)}
        'DefaultNotInTable' {Put-IsaRetNopBytes $code 11 ([BitConverter]::GetBytes([int32]($target-1-($prefix+15))))}
        'DefaultInGuard' {Put-IsaRetNopBytes $code 11 ([BitConverter]::GetBytes([int32]($dispatch-($prefix+15))))}
    }
    if ($Fault -eq 'RetWrongOffset') { $ret=$ret-1; $padding=$ret+1 }
    if ($Fault -eq 'ExtraPadding') { $ret=$table-3; $padding=$ret+1 }
    if ($Fault -in @('MissingRet','MissingTerminal','WrongRetPe')) { $pe[0x400+$ret-$s]=0x90 }
    elseif ($Fault -eq 'UnmodeledTerminalBranch') { $pe[0x400+$ret-$s]=0xff }
    else { $pe[0x400+$ret-$s]=0xc3 }
    Put-IsaRetNopBytes $pe (0x400+$prefix-$s) $code
    if ($paddingBytes.Length -gt 0) { Put-IsaRetNopBytes $pe (0x400+$padding-$s) $paddingBytes }
    $pe[0x400+$target-$s]=0x90
    $pe[0x400+$target-1-$s]=0x90
    # Only the target RVA's encoded DWORD resembles VEX. The target itself is ordinary scalar code.
    [byte[]]$vectorCode=@(0x90)
    Put-IsaRetNopBytes $pe (0x400+$vectorTarget-$s) $vectorCode

    $rows=[Collections.Generic.List[string]]::new()
    $starts=@(0,9,15,22,30,37,40); $lengths=@(9,6,7,8,7,3,2)
    for($i=0;$i -lt 7;++$i) {
        if($Fault -eq "MissingRow$i"){continue}
        [byte[]]$row=$code[$starts[$i]..($starts[$i]+$lengths[$i]-1)]
        if($Fault -eq "ListingRow$i"){$row[$row.Length-1]=$row[$row.Length-1] -bxor 1}
        $text=if($Fault -eq "UnknownRow$i"){'<unknown>'}else{'guard-dispatch'}
        $rows.Add(('{0:x}: {1} {2}' -f ($script:isaRetNopImageBase+$prefix+$starts[$i]),(Format-IsaRetNopBytes $row),$text))
    }
    if($Fault -notin @('MissingRet','MissingTerminal','UnmodeledTerminalBranch')) {
        $retBytes=if($Fault -eq 'WrongRetListing' -or $Fault -eq 'WrongTerminalListing'){'90'}else{'c3'}
        $retText=if($Fault -eq 'UnknownTerminal'){'<unknown>'}else{'ret'}
        $rows.Add(('{0:x}: {1} {2}' -f ($script:isaRetNopImageBase+$ret),$retBytes,$retText))
    }
    if ($Fault -ne 'MissingNopRow' -and $Fault -ne 'MissingNop') {
        [byte[]]$shownNop=$paddingBytes
        if($Fault -eq 'NopListingMismatch'){$shownNop=[byte[]]@(0x91)}
        if($Fault -eq 'OverlapNopIntoTable'){$shownNop=[byte[]]@(0x90,0xc5)}
        $nopText=if($Fault -eq 'UnknownNopRow'){'<unknown>'}else{'nop'}
        if ($Fault -eq 'UnmodeledTerminalBranch') {
            $rows.Add(('{0:x}: ff e0 jmp rax' -f ($script:isaRetNopImageBase+$ret)))
        }
        elseif ($Fault -eq 'ExtraPadding') {
            $rows.Add(('{0:x}: 90 nop' -f ($script:isaRetNopImageBase+$padding)))
            $rows.Add(('{0:x}: 90 nop' -f ($script:isaRetNopImageBase+$padding+1)))
        }
        elseif ($shownNop.Length -gt 0) {
            $rows.Add(('{0:x}: {1} {2}' -f ($script:isaRetNopImageBase+$padding),(Format-IsaRetNopBytes $shownNop),$nopText))
        }
    }
    if($Fault -eq 'DuplicateNopRow'){$rows.Add(('{0:x}: 90 nop' -f ($script:isaRetNopImageBase+$padding)))}
    if($Fault -eq 'OverlapNopFirst'){$rows.Add(('{0:x}: 90 nop' -f ($script:isaRetNopImageBase+$padding)))}
    if($Fault -eq 'OverlapRetIntoNop'){$rows.Add(('{0:x}: c3 90 overlapping-ret-nop' -f ($script:isaRetNopImageBase+$ret)))}
    if($Fault -eq 'OverlapGuard'){$rows.Add(('{0:x}: 90 nop' -f ($script:isaRetNopImageBase+$dispatch+1)))}
    if($Fault -eq 'OverlapDispatch'){$rows.Add(('{0:x}: 90 nop' -f ($script:isaRetNopImageBase+$dispatch+8)))}
    $targetText=if($Fault -eq 'UnknownDefault'){'<unknown>'}else{'nop'}
    $bodyText=if($Fault -eq 'UnknownBodyTarget'){'<unknown>'}else{'nop'}
    $rows.Add(('{0:x}: 90 {1}' -f ($script:isaRetNopImageBase+$target),$targetText))
    $rows.Add(('{0:x}: 90 {1}' -f ($script:isaRetNopImageBase+$target-1),$bodyText))
    $rows.Add(('{0:x}: {1} nop' -f
        ($script:isaRetNopImageBase+$vectorTarget),(Format-IsaRetNopBytes $vectorCode)))
    if($Fault -in @('CrossProcedureTarget','CrossModuleTarget')) {
        $pe[0x400+$prefix-2-$s]=0x90; $pe[0x400+$prefix-1-$s]=0x90
        $rows.Add(('{0:x}: 90 nop' -f ($script:isaRetNopImageBase+$prefix-2)))
        $rows.Add(('{0:x}: 90 nop' -f ($script:isaRetNopImageBase+$prefix-1)))
    }

    for($i=0;$i -le $Maximum;++$i) {
        [uint32]$entry=$vectorTarget
        if($i -eq $Maximum) { $entry=$target }
        if($i -eq 0 -and $Fault -in @('KnownBodyTarget','UnknownBodyTarget')){$entry=$target-1}
        if($i -eq $Maximum) {
            switch($Fault) {
                'MiddleTarget' {$entry=$prefix+1}
                'OutsideTarget' {$entry=$prefix-1}
                'GuardTarget' {$entry=$dispatch}
                'TableTarget' {$entry=$table}
                'MissingTarget' {$entry=$target+1}
                'TargetIntoPadByte0' {$entry=$padding}
                'TargetIntoPadByte1' {$entry=$padding+1}
                'TargetIntoPadByte2' {$entry=$padding+2}
                'CrossProcedureTarget' {$entry=$prefix-1}
                'CrossModuleTarget' {$entry=$prefix-1}
            }
        }
        $at=0x400+$table-$s+4*$i
        Set-IsaRetNopU32 $pe $at $entry
        $tableRow=Format-IsaRetNopBytes ([byte[]]$pe[$at..($at+3)])
        $tableMnemonic=if($entry -eq $vectorTarget){'vpunpckhdq xmm8, xmm10, xmmword ptr [rax]'}else{'.long 0x{0:x8}' -f $entry}
        $rows.Add(('{0:x}: {1} {2}' -f ($script:isaRetNopImageBase+$table+4*$i),$tableRow,$tableMnemonic))
    }
    [uint32]$procBegin=$prefix
    [uint32]$contributionBegin=$procBegin
    if($Fault -eq 'CrossProcedureTarget') { $contributionBegin=$prefix-2 }
    [uint32]$procEnd=$end
    [uint32]$branchAt=0
    [byte[]]$branch=@()
    $directFault=$Fault -match '^(Direct|Call|Loop|Jrcxz|Legacy)'
    $outsideEntry=$Fault -eq 'OutsideEntryToGuard'
    if($Fault -in @('EntryToIndex','EntryToTable','EntryToJa','EntryToGuard','EntryIntoPadByte0','EntryIntoPadByte1','EntryIntoPadByte2') -or $outsideEntry -or $directFault) {
        $procBegin=$prefix-8
        if($outsideEntry){$procBegin=$prefix-16}
        [uint32]$into=switch($Fault) {
            'EntryToIndex' {$dispatch+7}; 'EntryToTable' {$table}; 'EntryToJa' {$prefix+9}
            'EntryToGuard' {$prefix+1}; 'OutsideEntryToGuard' {$prefix}
            'Legacy66ToBody' {$target}
            default {
                if($Fault -match 'Byte0$'){$padding}
                elseif($Fault -match 'Byte1$'){$padding+1}
                elseif($Fault -match 'Byte2$'){$padding+2}
                elseif($Fault -match 'TableStart$'){$table}
                elseif($Fault -match 'TableInterior$'){$table+4}
                else {$padding}
            }
        }
        [byte[]]$branch=switch($Fault) {
            {$_ -match '^Call'} {[byte[]]@(0xe8,0,0,0,0)}
            {$_ -match '^LoopE0' -or $_ -match '^DirectE0'} {[byte[]]@(0xe0,0)}
            {$_ -match '^LoopE1' -or $_ -match '^DirectE1'} {[byte[]]@(0xe1,0)}
            {$_ -match '^LoopE2' -or $_ -match '^DirectE2'} {[byte[]]@(0xe2,0)}
            {$_ -match '^Jrcxz' -or $_ -match '^DirectE3'} {[byte[]]@(0xe3,0)}
            {$_ -match '^Legacy66'} {[byte[]]@(0x66,0xe9,0,0,0,0)}
            {$_ -match '^Legacy2E'} {[byte[]]@(0x2e,0xe9,0,0,0,0)}
            default {[byte[]]@(0xe9,0,0,0,0)}
        }
        [uint32]$branchAt=if($outsideEntry){$prefix-16}else{$procBegin}
        # LOOP-family rel8 edges originate after the protected dispatch so every padding byte is reachable.
        if($branch.Length -eq 2){$branchAt=$ret-32;$procBegin=$prefix}
        if($branch.Length -eq 2){$branch[1]=[byte]($into-($branchAt+2))}
        elseif($Fault -match '^Legacy66' -or $Fault -match '^Legacy2E'){Put-IsaRetNopBytes $branch 2 ([BitConverter]::GetBytes([int32]($into-($branchAt+$branch.Length))))}
        else {Put-IsaRetNopBytes $branch ($branch.Length-4) ([BitConverter]::GetBytes([int32]($into-($branchAt+$branch.Length))))}
        Put-IsaRetNopBytes $pe (0x400+$branchAt-$s) $branch
        $branchText=if($Fault -eq 'Legacy66ToBody'){'jmp'}else{'direct-control'}
        $rows.Add(('{0:x}: {1} {2}' -f ($script:isaRetNopImageBase+$branchAt),(Format-IsaRetNopBytes $branch),$branchText))
        if(-not $outsideEntry -and $branchAt+$branch.Length -lt $prefix) {
            for([uint32]$fill=$branchAt+$branch.Length;$fill -lt $prefix;++$fill) {
                $pe[0x400+$fill-$s]=0x90
                $rows.Add(('{0:x}: 90 nop' -f ($script:isaRetNopImageBase+$fill)))
            }
        }
    }
    for([uint32]$fill=$prefix+42;$fill -lt $ret;++$fill) {
        if($fill -ge $branchAt -and $fill -lt $branchAt+$branch.Length){continue}
        if(($fill -ge $vectorTarget -and $fill -lt $vectorTarget+$vectorCode.Length) -or
           $fill -eq $target -or $fill -eq $target-1){continue}
        $pe[0x400+$fill-$s]=0x90
        if(($Fault -eq 'MissingTarget' -and $fill -eq $target+1) -or
           ($Fault -eq 'MissingBodyRow' -and $fill -eq $target+2)){continue}
        $bodyByte=if($Fault -eq 'BodyListingMismatch' -and $fill -eq $target+2){'91'}else{'90'}
        $bodyName=if($Fault -eq 'UnknownBodyRow' -and $fill -eq $target+2){'<unknown>'}else{'nop'}
        $rows.Add(('{0:x}: {1} {2}' -f ($script:isaRetNopImageBase+$fill),$bodyByte,$bodyName))
        if($Fault -eq 'DuplicateBodyRow' -and $fill -eq $target+2){$rows.Add(('{0:x}: 90 nop' -f ($script:isaRetNopImageBase+$fill)))}
    }
    if($Fault -eq 'ShortProcedure'){$procEnd-=4}
    if($Fault -eq 'LongProcedure'){$procEnd+=4}
    if($outsideEntry){$procBegin=$prefix}
    if($Fault -eq 'CrossProcedureTarget') { $contributionBegin=$prefix-2 }
    else { $contributionBegin=$procBegin }
    $procOwner=if($Fault -eq 'WrongProcedureOwner'){1}else{0}
    $sym="  Mod $procOwner | ``SyntheticControl.obj``:`n"
    if($Fault -ne 'MissingProcedure'){$sym+="  0 | S_GPROC32 [size = 16] ``GuardedQwordStackRaxRetNop```n  addr = 0001:$($procBegin-$s), code size = $($procEnd-$procBegin)`n"}
    if($Fault -eq 'CrossProcedureTarget'){$sym+="  0 | S_GPROC32 [size = 16] ``OtherProcedure```n  addr = 0001:$($prefix-2-$s), code size = 2`n"}
    if($Fault -eq 'CrossModuleTarget'){$sym+="  Mod 1 | ``SyntheticOther.obj``:`n  0 | S_GPROC32 [size = 16] ``OtherModuleProcedure```n  addr = 0001:$($prefix-2-$s), code size = 2`n"}
    $dataOwner=if($Fault -eq 'WrongDataOwner'){1}else{0}
    $sym+="  Mod $dataOwner | ``SyntheticControl.obj``:`n"
    if($Fault -ne 'MissingData') {
        $data=if($Fault -eq 'WrongDataStart'){$table+4}else{$table}
        $sym+="  0 | S_LDATA32 [size = 16] `````n  type = <no type>, addr = 0001:$($data-$s)`n"
    }
    if($Fault -eq 'InteriorData'){$sym+="  0 | S_LDATA32 [size = 16] `````n  type = <no type>, addr = 0001:$($table+4-$s)`n"}
    $sym+="  Mod 0 | ``SyntheticControl.obj``:`n  0 | S_LDATA32 [size = 16] `````n  type = <no type>, addr = 0001:4096`n"
    if($Fault -eq 'AdjacentVex') {
        [byte[]]$vex=@(0xc5,0xf8,0x10,0x00)
        Put-IsaRetNopBytes $pe (0x400+$end-$s) $vex
        $rows.Add(('{0:x}: {1} vmovups xmm0, xmmword ptr [rax]' -f ($script:isaRetNopImageBase+$end),(Format-IsaRetNopBytes $vex)))
    }
    $contributionEnd=$end
    if($Fault -eq 'LongProcedure'){$contributionEnd=$end+4}
    $contributions="  Age: $script:isaRetNopAge`n  GUID: {$script:isaRetNopGuid}`n  Mod 0 | ``SyntheticControl.obj``:`n"
    if($Fault -eq 'CrossModuleTarget'){$contributions+="  Mod 1 | ``SyntheticOther.obj``:`n"}
    $contributions+="`nSection Contributions`n"
    if($Fault -eq 'CrossModuleTarget'){$contributions+="  SC[.text] | mod = 1, 0001:$($prefix-2-$s), size = 2, data crc = 0, reloc crc = 0`n"}
    $contributions+="  SC[.text] | mod = 0, 0001:$($contributionBegin-$s), size = $($contributionEnd-$contributionBegin), data crc = 0, reloc crc = 0`n"
    Set-Content -LiteralPath $metadata -Value $contributions -Encoding utf8
    Set-Content -LiteralPath $symbols -Value $sym -Encoding utf8
    Set-Content -LiteralPath $disassembly -Value (@($rows | Sort-Object { [UInt64]::Parse(($_ -split ':',2)[0],[Globalization.NumberStyles]::HexNumber) }) -join "`n") -Encoding utf8
    [IO.File]::WriteAllBytes($binary,$pe)
    return @{Name=$Name;Binary=$binary;Metadata=$metadata;Symbols=$symbols;Disassembly=$disassembly}
}

$script:isaRetNopControlNames=[Collections.Generic.List[string]]::new()
function Invoke-IsaQwordRetNopControl([string]$Name,[string]$Fault,[int]$Maximum,[bool]$Accepted,[int]$Tables=0,[int]$Violations=-1) {
    $case=New-IsaQwordRetNopCase $Name $Fault $Maximum
    $result=[RedPrismBuild.IsaScanner]::Scan($case.Binary,$case.Metadata,$case.Symbols,$case.Disassembly,[string[]]@(),[string[]]@(),[string[]]@())
    $expected=if($Accepted){'accept'}else{'refuse'}
    $observed=if($result.Violations.Count -eq 0){'accept'}else{'refuse'}
    if($expected -ne $observed){Write-Host "FAIL isa-qword-retnop.$Name expected=$expected observed=$observed"; throw "$Name ISA gate mismatch: $($result.Violations -join '; ')"}
    if($Tables -ge 0 -and $result.InlineTableVectorData -ne $Tables){throw "$Name expected $Tables inline table rows, got $($result.InlineTableVectorData)."}
    if($Violations -ge 0 -and $result.Violations.Count -ne $Violations){throw "$Name expected $Violations violations, got $($result.Violations.Count)."}
    $script:isaRetNopControlNames.Add($Name)
    Write-Host "PASS isa-qword-retnop.$Name expected=$expected observed=$observed"
}

# Sixteen c5-prefixed entries plus the exact default RVA 0x006a2bec make all 17 targets explicit.
# The scanner's InlineTableVectorData metric therefore reports 16 for the accepted exact-shape controls.
Invoke-IsaQwordRetNopControl 'ActualMaximum16Ret90' '' 16 $true 16 0
Invoke-IsaQwordRetNopControl 'KnownBodyTarget' 'KnownBodyTarget' 16 $true 15 0
Invoke-IsaQwordRetNopControl 'Legacy66DecodedBranchToBody' 'Legacy66ToBody' 16 $true 16 0
Invoke-IsaQwordRetNopControl 'AdjacentVex' 'AdjacentVex' 16 $false 16 1
$isaRetNopFaults=@(
    'DwordCmp','WrongCmp','WrongCmpSib','WrongJa','SignedJa','WrongBase','WrongImageBase','WrongIndexRegister','DwordIndex','WrongIndexSib',
    'StackByte0','StackByte1','StackByte2','StackByte3','WrongLoad','WrongLoadSib','WrongAdd','WrongJump','ZeroRange','Range128','Range255',
    'DefaultNotInTable','DefaultInGuard','OverlapGuard','OverlapDispatch','UnknownDefault','UnknownBodyTarget','WrongRetPe','MissingRet','WrongRetListing',
    'MissingTerminal','UnknownTerminal','WrongTerminalListing','UnmodeledTerminalBranch','MiddleTarget','OutsideTarget','GuardTarget','TableTarget','MissingTarget',
    'ShortProcedure','LongProcedure','WrongProcedureOwner','MissingProcedure','WrongDataOwner','MissingData','WrongDataStart','InteriorData',
    'EntryToIndex','EntryToTable','EntryToJa','MissingPadding','MissingNop','WrongNop','ExtraPadding','RetWrongOffset',
    'NopLengthTwo','NopLengthFour','NopListingMismatch','MissingNopRow','UnknownNopRow','DuplicateNopRow','OverlapNopFirst','OverlapNopIntoTable','OverlapRetIntoNop',
    'VexInPadding','TargetIntoPadByte0','TargetIntoPadByte1','TargetIntoPadByte2','EntryIntoPadByte0','EntryIntoPadByte1','EntryIntoPadByte2',
    'EntryToGuard','DirectE0ToByte0','DirectE1ToByte1','DirectE2ToByte2','DirectE3ToTableStart',
    'CallToByte0','CallToTableInterior','LoopE0ToByte0','LoopE1ToByte1','LoopE2ToByte2','JrcxzToByte0',
    'Legacy66ToByte1','Legacy2EToTableInterior','CrossProcedureTarget','CrossModuleTarget',
    'MissingBodyRow','BodyListingMismatch','UnknownBodyRow','DuplicateBodyRow'
)
foreach($fault in $isaRetNopFaults){Invoke-IsaQwordRetNopControl $fault $fault 16 $false}
foreach($row in 0..6){foreach($kind in 'Missing','Listing','Unknown'){Invoke-IsaQwordRetNopControl "$($kind)Row$row" "$($kind)Row$row" 16 $false}}
$acceptedNames=@('ActualMaximum16Ret90','KnownBodyTarget','Legacy66DecodedBranchToBody')
$acceptedCount=@($script:isaRetNopControlNames | Where-Object { $_ -in $acceptedNames }).Count
Write-Host "SUMMARY isa-qword-retnop controls=$($script:isaRetNopControlNames.Count) accepted=$acceptedCount refused=$($script:isaRetNopControlNames.Count-$acceptedCount)"


