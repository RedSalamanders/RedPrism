# Release `std::variant` destruction dispatch in the ISA gate — 2026-10-01

## Finding

After the DxUi pin moved to API revision 3 and `origin/main` (`ca51108`) was merged into `dxui/revision-3-2026-10-01`, the
unpatched x64 Release build was rejected by the ISA gate with exactly one violation: `0x1400962dc` in `ApplicationFileJobs.obj`
(linked into `RedPrism.exe`), `vpshufb xmm14, xmm14, xmmword ptr [rbp - 0x3afff69e]`, bytes `c4 62 09 00 b5 62 09 00 c5`.

It is not code. It is the first DWORD of a switch table inside the `.text` contribution of
`std::_Variant_base<HostCommandInput,...,HostPanelCancelInput>::_Destroy` (21 alternatives, RVA `0x96290`, 114 bytes). Entry 0 is
`c4 62 09 00`, the image-relative address `0x962c4` of a `ret`, and its first byte is a VEX prefix. The dispatch is

```
140096290  48 0f be 81 80 00 00 00        movsx  rax, byte ptr [rcx + 0x80]
140096298  4c 8d 05 61 9d f6 ff           lea    r8, [rip - 0x9629f]            ; 0x140000000
14009629f  41 0f b6 84 00 ed 62 09 00     movzx  eax, byte ptr [r8 + rax + 0x962ed]
1400962a8  41 8b 94 80 dc 62 09 00        mov    edx, dword ptr [r8 + 4*rax + 0x962dc]
1400962b0  49 03 d0                       add    rdx, r8
1400962b3  ff e2                          jmp    rdx
```

The index is signed (-1 is the valueless variant), so the byte table is biased by one entry: it starts at `0x962ec`, one byte before
the load's displacement, and its first byte is the entry of index -1. The DWORD table (4 entries, 16 bytes, `0x962dc`) and the
byte table (22 bytes, `0x962ec`) are the last bytes of the procedure, which ends with its executable contribution at `0x96302`.
The PDB has an anonymous untyped 16-byte `S_LDATA32` at each: section offsets 611036 and 611052. The scanner accepted an R8 base
with a RAX index only for a RAX or RCX target, and a byte table biased by one entry only for the RBP-framed RCX-base form, so
the row stayed a violation. An earlier table form of the same pin move (a switch whose first entry decoded as EVEX at
`0x140193260` in `KernelBenchmark.obj`, before the merge) is covered by the guarded RDX-base form `origin/main` already carries.

## Change

`Build/IsaScan.cs` recognizes exactly this shape (`ReadSignedBiasedByteIndexTables`, contract in
[Build_Toolchain.md](../Build/Build_Toolchain.md)): the six contiguous instructions, each equal to the PE bytes with its reported
length and overlapping no other decoded row; the exact anonymous same-owner PDB data start of the DWORD table; the exact start of
the byte table one byte before its load displacement, which is also where the DWORD table ends; the end the owning PDB procedure
shares with its executable contribution; no other data symbol inside either table; every byte below the entry count; every entry a
decoded instruction boundary of that procedure before the tables. Nothing is exempted by address, object or procedure name, and
`Build/IsaRuntimeAllowlist.psd1` is unchanged.

## Evidence

The unpatched merged-tree x64 Release image (`RedPrism.exe` SHA256
`E1BB7813E236B3E976AF175D4C0743D9E18F063C337990EC3D338D46ED9DD9FA`, PDB
`2A828F28D9B230C4E1AD034E4CBD7EAF0D235BF2D404EDFE0DAFC5F96F106C7D`, allowlist
`7870AF49195841A9B9B88DFD15378D550B127754683666C83AED83753FD80E1E`) scans as follows.

| Scan | Decoded | VEX/EVEX rows | Undecoded prefixes | Verified table data | Violations |
| --- | --- | --- | --- | --- | --- |
| Before the change | 716,383 | 460 | 9 | 1 | 1 (`0x1400962dc`) |
| After the change | 716,383 | 459 | 8 | 3 | 0 |

The three verified rows are the earlier one plus the two this table contributes (`0x1400962dc`, nine bytes, and `0x1400962e5`,
six bytes starting `62`). `HostTests.exe`, `DocumentTests.exe`, `CompositionTests.exe` and `IoTests.exe` of the same build also pass.

`Tests/BuildProcessTests/IsaInlineTableTests.ps1` now has 527 controls (434 before): 93 for this form, which rebuild the observed
bytes at the observed addresses (the section at RVA `0x96000`), with the neighbouring functions as separate contributions of
the same object. Three are accepted (the observed image, one with a leading instruction in the procedure and one relocated to
another section address); each of the other 90 changes one thing, in the groups listed in
[Testing_Validation.md](../Testing/Testing_Validation.md). Each refusal reports the table's own first row. The requested negative
controls are `DwordSymbolOneByteLate`, `DwordSymbolOneByteEarly`, `ByteSymbolOneByteLate` and `ByteSymbolOneByteEarly` (a PDB start
one byte off), `BiasNone`, `BiasTwo`, `BiasNegative` and `DwordLoadBiasedByOneEntry` (a bias that does not match),
`TargetBeforeFunction` and `TargetAfterFunction` (a target outside the function) and `RealVexInBody`, `RealVexInPad`,
`RealVexAtProcedureEnd` and `RealVexAfterFunction` (a genuine VEX instruction outside any proven table, with the two table rows
still counted as data).

To check that the controls bear on the rules, each rule was removed in turn from a scratch copy of the scanner and the new controls
run against it. Accepting a byte table start at the displacement as well failed `BiasNone`; dropping the exact DWORD record failed
six controls and the exact byte record five; allowing a record inside a table failed two; ending the byte table at an unproved
procedure end failed `ProcedureEndsEarly` and `ContributionLonger`; not requiring the procedure to contain the MOVSX failed
`ProcedureStartsAfterPrefix`; accepting entries inside or after the tables failed three; not bounding the index bytes failed two;
trusting the listing rows failed 13; and removing the signed-index requirement from both its places failed five. Three checks
(an entry inside the procedure, an entry on a decoded boundary, the byte-load opcode) are also enforced by the generic post-checks
or the state machine, so removing one alone changes no control; they stay in the form so that it does not rely on them.

## Suggestion, not part of this change

A pin move shifts the layout of every consumer image, and each shift can expose another compiler switch idiom (this is the second
table form that one pin move produced). Rather than one reviewed form per idiom, the scanner could treat PDB-proven data ranges in
`.text` generally: an anonymous 16-byte `S_LDATA32` in the owning contribution, its extent bounded by the next data symbol or the
procedure end, and every DWORD entry validated as a decoded boundary of that procedure. The per-form controls would then become
controls of that one rule.
