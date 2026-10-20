[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$fixtureRepo = (Get-Item -LiteralPath $PSScriptRoot).FullName
while (-not (Test-Path -LiteralPath (Join-Path $fixtureRepo 'Build/IsaScan.cs'))) {
    $parent = Split-Path -Parent $fixtureRepo
    if ([string]::IsNullOrEmpty($parent) -or $parent -eq $fixtureRepo) {
        throw 'Cannot find RedPrism repository from the parser fixture.'
    }
    $fixtureRepo = $parent
}
. (Join-Path $fixtureRepo 'Tests/BuildProcessTests/IsaInlineTableFixtures.ps1')

# The caller preloads its private scanner type. The imported helper provides PE setup,
# byte-writing, listing formatting, and Invoke-Control assertions.
$script:rsi64Base = [UInt64]0x140000000
$script:rsi64Section = [UInt32]$script:sectionRva
$script:rsi64Prefix = [UInt32]($script:rsi64Section + 0x780)
$script:rsi64Table = [UInt32]($script:rsi64Section + 0x900)

function New-Rsi64Case {
    param([Parameter(Mandatory)][string]$Name, [string]$Fault = '', [ValidateRange(1,127)][int]$Maximum = 5)
    $case = New-SyntheticCase ($Name + 'Seed') -PdbData
    [byte[]]$pe = [IO.File]::ReadAllBytes($case.Binary)
    [UInt32]$p = $script:rsi64Prefix; [UInt32]$t = $script:rsi64Table
    [UInt32]$after = $p + 29; [UInt32]$retAt = $t - 4; [UInt32]$nopAt = $t - 3
    [UInt32]$end = $t + 4 * ($Maximum + 1)
    [UInt32]$caseTarget = $script:rsi64Section + 0x8c5; [UInt32]$defaultTarget = $after + 2

    # Exact independently encoded rows: lengths 4, 6, 7, 7, 3, 2.
    [byte[]]$cmp = @(0x48,0x83,0xfe,[byte]$Maximum)
    [byte[]]$ja = @(0x0f,0x87,0,0,0,0)
    [byte[]]$lea = @(0x48,0x8d,0x0d,0,0,0,0)
    [byte[]]$load = @(0x8b,0x84,0xb1,0,0,0,0)
    [byte[]]$add = @(0x48,0x03,0xc1); [byte[]]$jmp = @(0xff,0xe0)
    Put-Bytes $lea 3 ([BitConverter]::GetBytes([Int32](-[Int64]($p+17))))
    Set-U32 $load 3 $t
    switch ($Fault) {
        WrongCompareOpcode { $cmp[1]=0x82 } Guard32Bit { $cmp[0]=0x40 } WrongMaximum { $cmp[3]=[byte]($Maximum-1) }
        ZeroMaximum { $cmp[3]=0 } Maximum128 { $cmp[3]=128 }
        WrongJa { $ja[1]=0x86 } WrongBase { $lea[2]=0x15 } WrongImageBase { $lea[3]++ }
        WrongLoadOpcode { $load[0]=0x89 } WrongLoadDestination { $load[1]=0x8c } WrongScale { $load[2]=0x71 }
        WrongIndex { $load[2]=0xa9 } WrongLoadBase { $load[2]=0xb2 } WrongLoadAddress { $load[3]++ }
        WrongAddOpcode { $add[1]=0x01 } WrongAddRegisters { $add[2]=0xc2 } WrongJumpOpcode { $jmp[1]=0xe1 }
        CaseBeforeBody { $caseTarget=$after-1 } CaseRetBoundary { $caseTarget=$retAt }
        CasePadding { $caseTarget=$nopAt } CaseTable { $caseTarget=$t } CaseMiddleInstruction { $caseTarget=$after+1 }
        DefaultBeforeBody { $defaultTarget=$after-1 } DefaultRetBoundary { $defaultTarget=$retAt }
        DefaultPadding { $defaultTarget=$nopAt } DefaultTable { $defaultTarget=$t }
        DefaultMiddleInstruction { $defaultTarget=$after+1 }
    }
    Put-Bytes $ja 2 ([BitConverter]::GetBytes([Int32]([Int64]$defaultTarget-[Int64]($p+10))))
    Put-Bytes $pe (0x400+$p-$script:rsi64Section) $cmp
    Put-Bytes $pe (0x400+$p+4-$script:rsi64Section) $ja
    Put-Bytes $pe (0x400+$p+10-$script:rsi64Section) $lea
    Put-Bytes $pe (0x400+$p+17-$script:rsi64Section) $load
    Put-Bytes $pe (0x400+$p+24-$script:rsi64Section) $add
    Put-Bytes $pe (0x400+$p+27-$script:rsi64Section) $jmp

    # Entire post-jump region through RET and the 0f 1f 00 NOP has known PE/listing rows.
    for ([UInt32]$r=$after; $r -lt $retAt; ++$r) { $pe[0x400+$r-$script:rsi64Section]=0x90 }
    if ($Fault -in @('CaseMiddleInstruction','DefaultMiddleInstruction')) {
        $pe[0x400+$after-$script:rsi64Section]=0x66
        $pe[0x400+$after+1-$script:rsi64Section]=0x90
    }
    if ($Fault -in @('BodyBranchIntoGuard','BodyBranchIntoTable')) {
        [byte[]]$edge=@(0xe9,0,0,0,0)
        [Int64]$dest=if($Fault -eq 'BodyBranchIntoGuard'){$p+11}else{$t}
        Put-Bytes $edge 1 ([BitConverter]::GetBytes([Int32]($dest-[Int64]($after+5))))
        Put-Bytes $pe (0x400+$after-$script:rsi64Section) $edge
    }
    [byte[]]$ret=@(0xc3); [byte[]]$padding=@(0x0f,0x1f,0x00)
    if($Fault -eq 'WrongRet'){$ret[0]=0x90}; if($Fault -eq 'WrongPadding'){$padding[2]=1}
    Put-Bytes $pe (0x400+$retAt-$script:rsi64Section) $ret
    Put-Bytes $pe (0x400+$nopAt-$script:rsi64Section) $padding
    for($i=0;$i -le $Maximum;++$i) {
        [UInt32]$entry=$script:rsi64Section + 0x8c5
        if($i -eq $Maximum){$entry=$caseTarget}
        if($i -eq $Maximum -and $Fault -eq 'HighIndexTarget'){$entry=$p+1}
        Set-U32 $pe (0x400+$t-$script:rsi64Section+4*$i) $entry
    }

    $rows=[Collections.Generic.List[string]]::new()
    $bytes=@($cmp,$ja,$lea,$load,$add,$jmp); $offs=@(0,4,10,17,24,27)
    $names=@('Cmp','Ja','Lea','Load','Add','Jump')
    for($i=0;$i -lt 6;++$i) {
        $n=$names[$i]; if($Fault -eq "Missing$n"){continue}
        [byte[]]$actual=$bytes[$i].Clone(); $file=0x400+$p+$offs[$i]-$script:rsi64Section
        if($Fault -eq "Pe$n"){$pe[$file]=$pe[$file] -bxor 1}
        [byte[]]$shown=$actual.Clone()
        if($Fault -eq "Listing$n"){$shown[-1]=$shown[-1] -bxor 1}
        $text=if($Fault -eq "Unknown$n"){'<unknown>'}else{'guard-dispatch'}
        $line='{0:x}: {1} {2}' -f ($script:rsi64Base+$p+$offs[$i]),(Hex-Bytes $shown),$text
        $rows.Add($line)
        if($Fault -eq "Duplicate$n"){$rows.Add($line)}
        if($Fault -eq "Overlap$n"){$rows.Add(('{0:x}: {1:x2} overlap' -f ($script:rsi64Base+$p+$offs[$i]+1),$pe[$file+1]))}
    }
    for([UInt32]$r=$after;$r -lt $retAt;++$r) {
        if($Fault -eq 'MissingBodyRow' -and $r -eq $after+10){continue}
        if($Fault -in @('BodyBranchIntoGuard','BodyBranchIntoTable') -and $r -eq $after) {
            [byte[]]$shown=$pe[(0x400+$r-$script:rsi64Section)..(0x400+$r-$script:rsi64Section+4)]
            $rows.Add(('{0:x}: {1} jmp external-entry-control' -f ($script:rsi64Base+$r),(Hex-Bytes $shown)))
            $r+=4; continue
        }
        if($Fault -in @('CaseMiddleInstruction','DefaultMiddleInstruction') -and $r -eq $after) {
            $rows.Add(('{0:x}: 66 90 nop word ptr' -f ($script:rsi64Base+$r))); ++$r; continue
        }
        [byte]$b=$pe[0x400+$r-$script:rsi64Section]; [byte[]]$shown=@($b)
        if($Fault -eq 'BodyListingMismatch' -and $r -eq $after+10){$shown[0]=$shown[0] -bxor 1}
        $text=if($Fault -eq 'UnknownBodyRow' -and $r -eq $after+10){'<unknown>'}else{'nop'}
        $line='{0:x}: {1} {2}' -f ($script:rsi64Base+$r),(Hex-Bytes $shown),$text
        $rows.Add($line); if($Fault -eq 'DuplicateBodyRow' -and $r -eq $after+10){$rows.Add($line)}
    }
    if($Fault -ne 'MissingRet') {
        $text=if($Fault -eq 'UnknownRet'){'<unknown>'}else{'ret'}
        $rows.Add(('{0:x}: {1} {2}' -f ($script:rsi64Base+$retAt),(Hex-Bytes $ret),$text))
    }
    if($Fault -ne 'MissingPadding') {
        [byte[]]$shown=$padding.Clone(); if($Fault -eq 'PaddingListingMismatch'){$shown[2]=$shown[2] -bxor 1}
        $rows.Add(('{0:x}: {1} nop' -f ($script:rsi64Base+$nopAt),(Hex-Bytes $shown)))
    }
    for($i=0;$i -le $Maximum;++$i) {
        [byte[]]$data=$pe[(0x400+$t-$script:rsi64Section+4*$i)..(0x400+$t-$script:rsi64Section+4*$i+3)]
        if($Fault -eq 'TableListingMismatch' -and $i -eq $Maximum){$data[0]=$data[0] -bxor 1}
        $rows.Add(('{0:x}: {1} vmovups xmm0, xmmword ptr [rax]' -f ($script:rsi64Base+$t+4*$i),(Hex-Bytes $data)))
    }
    if($Fault -eq 'AdjacentVex') {
        [byte[]]$vex=@(0xc5,0xf8,0x10,0x00); Put-Bytes $pe (0x400+$end-$script:rsi64Section) $vex
        $rows.Add(('{0:x}: {1} vmovups xmm0, xmmword ptr [rax]' -f ($script:rsi64Base+$end),(Hex-Bytes $vex)))
    }

    $nl=[Environment]::NewLine; $q=[string][char]96
    $po=if($Fault -eq 'WrongProcedureOwner'){1}else{0}; $do=if($Fault -eq 'WrongDataOwner'){1}else{0}
    [UInt32]$procEnd=$end; if($Fault -eq 'ProcedureEndsEarly'){$procEnd-=4}; if($Fault -eq 'ProcedureEndsLate'){$procEnd+=4}
    $symbols='  Mod {0} | {1}SyntheticControl.obj{1}:{2}' -f $po,$q,$nl
    if($Fault -ne 'MissingProcedure'){$symbols+='  0 | S_GPROC32 [size = 16] {0}Rsi64Terminal{0} {1}  addr = 0001:{2}, code size = {3}{4}' -f $q,$nl,($p-$script:rsi64Section),($procEnd-$p),$nl}
    $symbols+='  Mod {0} | {1}SyntheticControl.obj{1}:{2}' -f $do,$q,$nl
    if($Fault -ne 'MissingData') {
        [UInt32]$start=if($Fault -eq 'WrongDataStart'){$t+4}else{$t}
        $symbolName=if($Fault -eq 'NamedData'){'named-table'}else{''}
        $type=if($Fault -eq 'TypedData'){'type = 0x1000, '}else{'type = <no type>, '}
        $symbols+='  0 | S_LDATA32 [size = 16] {0}{1}{0}{4}  {2}addr = 0001:{3}{4}' -f $q,$symbolName,$type,($start-$script:rsi64Section),$nl
    }
    if($Fault -in @('InteriorData','DuplicateData')) {
        [UInt32]$extra=if($Fault -eq 'InteriorData'){$t+4}else{$t}
        $symbols+='  0 | S_LDATA32 [size = 16] {0}{0}{1}  type = <no type>, addr = 0001:{2}{3}' -f $q,$nl,($extra-$script:rsi64Section),$nl
    }
    if($Fault -eq 'UnownedForeignData') {
        $symbols+='  Mod 1 | {0}Foreign.obj{0}:{1}  0 | S_LDATA32 [size = 16] {0}{0}{1}  type = <no type>, addr = 0001:{2}{1}' -f $q,$nl,($t-$script:rsi64Section)
    }
    Set-Content -LiteralPath $case.Symbols -Value $symbols -Encoding utf8
    Set-Content -LiteralPath $case.Disassembly -Value (@($rows | Sort-Object {[UInt64]::Parse(($_ -split ':',2)[0],[Globalization.NumberStyles]::HexNumber)}) -join $nl) -Encoding utf8
    [IO.File]::WriteAllBytes($case.Binary,$pe); $case.Name=$Name; return $case
}

# Fixed expected outcomes precede all calls to the private scanner.
$accepted=@(@{Name='Rsi64TerminalExact';Maximum=5;Fault=''},@{Name='Rsi64TerminalMaximum1';Maximum=1;Fault=''},@{Name='Rsi64TerminalMaximum127';Maximum=127;Fault=''},
    @{Name='Rsi64TerminalCaseRetBoundary';Maximum=5;Fault='CaseRetBoundary'},@{Name='Rsi64TerminalDefaultRetBoundary';Maximum=5;Fault='DefaultRetBoundary'},
    @{Name='Rsi64TerminalDuplicateData';Maximum=5;Fault='DuplicateData'},
    @{Name='Rsi64TerminalUnownedForeignData';Maximum=5;Fault='UnownedForeignData'})
$faults=@(
 'WrongCompareOpcode','Guard32Bit','WrongMaximum','ZeroMaximum','Maximum128','WrongJa','WrongBase','WrongImageBase','WrongLoadOpcode','WrongLoadDestination',
 'WrongScale','WrongIndex','WrongLoadBase','WrongLoadAddress','WrongAddOpcode','WrongAddRegisters','WrongJumpOpcode','HighIndexTarget',
 'CaseBeforeBody','CasePadding','CaseTable','CaseMiddleInstruction','DefaultBeforeBody',
 'DefaultPadding','DefaultTable','DefaultMiddleInstruction','WrongRet','WrongPadding','MissingRet','MissingPadding','UnknownRet',
 'PaddingListingMismatch','TableListingMismatch','MissingProcedure','WrongProcedureOwner','ProcedureEndsEarly','ProcedureEndsLate',
 'MissingData','WrongDataOwner','WrongDataStart','NamedData','TypedData','InteriorData','MissingBodyRow',
 'DuplicateBodyRow','UnknownBodyRow','BodyListingMismatch','BodyBranchIntoGuard','BodyBranchIntoTable','AdjacentVex'
)
$guardKinds=@('Missing','Duplicate','Unknown','Pe','Listing','Overlap'); $guardNames=@('Cmp','Ja','Lea','Load','Add','Jump')
$total=$accepted.Count+$faults.Count+$guardKinds.Count*$guardNames.Count
if($total -ne 93){throw "IsaRsi64Terminal roster changed: expected 93, got $total."}
$ran=0
foreach($item in $accepted){
    # Only the last CaseRetBoundary cell is scalar (FC...), while all other cells begin C5 F8 10 00.
    # Keep genuine vector-looking table cells so invalid-target refusals cannot pass without recognition.
    $inline=if($item.Fault -eq 'CaseRetBoundary'){[int]$item.Maximum}else{[int]$item.Maximum+1}
    Invoke-Control (New-Rsi64Case $item.Name $item.Fault $item.Maximum) $true $inline 0
    ++$ran
}
foreach($fault in $faults){
    if($fault -eq 'AdjacentVex'){Invoke-Control (New-Rsi64Case ("Rsi64Terminal$fault") $fault) $false 6 1}
    else{Invoke-Control (New-Rsi64Case ("Rsi64Terminal$fault") $fault) $false}
    ++$ran
}
foreach($kind in $guardKinds){foreach($guard in $guardNames){$fault=$kind+$guard; Invoke-Control (New-Rsi64Case ("Rsi64Terminal$fault") $fault) $false; ++$ran}}
if($ran -ne $total){throw "IsaRsi64Terminal ran $ran controls; roster declares $total."}
Write-Host "SUMMARY isa-rsi64-terminal controls=$ran accepted=$($accepted.Count) refused=$($ran-$accepted.Count)"
$global:LASTEXITCODE=0
