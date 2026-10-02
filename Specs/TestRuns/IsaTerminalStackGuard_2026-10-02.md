# ISA terminal stack-guard qualification — 2026-10-02

Status: the strict x64 ISA parser proof passes synthetic controls and the exact retained-image rescan. Layer via Copy native r3 qualification also passes; its runtime evidence lives in the separate LayerCopy record.

## Checklist

- [x] Keep the runtime allowlist unchanged; qualify only exact decoded/PDB/PE table evidence.
- [x] Pass all recorded inline-table controls, including 48 long-JA controls.
- [x] Rescan the exact retained failed ASan executable; zero violations.
- [x] Complete native r3 x64 Debug, Release and ASan Debug suites plus all ARM64 cross-builds; see [LayerCopyWorkers_2026-10-01.md](LayerCopyWorkers_2026-10-01.md) for the separate runtime proof and retained failures.

## Finding and accepted proof

The retained ASan scan initially reported one EVEX instruction at `0x14065bcd8` in `Application.obj`, identified in the PDB as `Application::EditUnderWay`. The bytes are 11 inline DWORD RVAs in its terminal switch table, not executable EVEX code. The original report had 4,115,277 decoded instructions, 11,691 vector instructions, one EVEX, five inline-table vector-data rows and one violation.

`Build/IsaScan.cs` now recognizes only the observed terminal RCX dispatch with its exact 19-byte sequence and either the 12-byte short-JA or 16-byte long-JA stack guard. The guard is `CMP dword [RSP+disp8], imm8` with maximum 1–127, `JA rel8` or `JA rel32`, and `MOVSXD RAX,dword [RSP+the same disp8]`. Each reported guard/dispatch row must have its expected decoded length and match PE bytes at known, nonoverlapping instruction boundaries. The aligned DWORD table requires its exact anonymous same-owner PDB data start; `tableStart + 4*(maximum+1)` must equal the exact owning procedure end within the contribution. Every table RVA and the default must resolve to decoded boundaries in that procedure before the table. For long JA, the default may be a separate decoded boundary in the same procedure before the table. Other symbols or procedure bounds cannot extend the proof. No object, procedure or runtime allowlist exception was added.

## Evidence

The complete parser-control run `.build/p1-layer-copy-isa-controls-r7.log` exited 0 at `2026-10-01T22:21:25.1138715Z` (`.build/p1-layer-copy-isa-controls-r7.json`). Its named proof groups report 29 data-proof, 34 terminal byte-remap, 37 short-form terminal DWORD, 48 long-JA terminal DWORD and 66 MOVZX terminal DWORD controls passed. Thus all 166 controls in the preexisting named groups and all 48 new long-JA controls pass. The log contains 482 `PASS isa-inline-table.*` rows overall.

The exact archived image was independently rescanned using the retained ISA inputs under `.build/p1-layer-copy-r1-ASan-failure/Evidence/.build/IsaScan/x64/ASanDebug/`. The report `.build/p1-layer-copy-failed-binary-rescan-r4.json`, completed `2026-10-01T22:21:36.3088849Z`, records 4,115,277 decoded instructions, 11,690 vector/runtime instructions, zero EVEX, four undecoded prefixes, six inline-table vector-data rows and zero violations. This is the same executable hash as the original failing report.

SHA-256 provenance:

- Archived `Evidence/.build/x64/ASan Debug/RedPrism.exe`: `B034ED1D231C8DF7FC3F5FB0A683F0AFFD96779C526AC8E5407D84664FA75606`
- `Build/IsaScan.cs` used for r7 and the passing rescan: `15F83C2EF26F575FECF051B3672809FF0EBF6FA53F2616A69CF63509C2BAB363`
- `Build/IsaRuntimeAllowlist.psd1`: `6F55A40AB0CE23E98BB10839EC68765531129EF375B00DD29B298C3DC97EE560`
- Archived PDB (from its retained ISA report): `6E83F3E629FF9F453E89F0A3E4A70FF6F29638B89FE957EABDD9F09CF4A13C30`

The original image and PDB, disassembly, contribution map, symbols and failed report are retained under `.build/p1-layer-copy-r1-ASan-failure/Evidence/`. The passing exact-image rescan receipt and control log/receipt are retained at the paths above. No native build, application run, or LayerCopy runtime result is established by these parser controls.

## Retained failed iterations

These failures remain useful negative evidence and are not counted as passes:

- `.build/p1-layer-copy-isa-controls-failure-r2/` / `.build/p1-layer-copy-isa-controls-r2.log`: `TerminalDwordLongJaAdjacentVex` bypassed the gate. The initial adjacent-VEX fixture admitted generic recognition and did not isolate the new proof.
- `.build/p1-layer-copy-isa-controls-failure-r3/` / `.build/p1-layer-copy-isa-controls-r3.log`: the positive `TerminalDwordLongJaMaximum1` fixture was rejected as two `vmovups` violations.
- `.build/p1-layer-copy-isa-controls-failure-r4/` / `.build/p1-layer-copy-isa-controls-r4.log`: `TerminalDwordLongJaLaterUnrelatedData` bypassed the gate.
- `.build/p1-layer-copy-isa-controls-failure-r5/` / `.build/p1-layer-copy-isa-controls-r5.log`: `TerminalDwordLongJaMissingDefault` bypassed the gate. The original missing-default byte was `0xC7`, which could not trigger the VEX-prefix gate; the corrected control uses a separate default target.
- `.build/p1-layer-copy-isa-rescan-failure-r3/`: the exact-image rescan still reported the original EVEX-shaped table bytes before the separate-default proof correction. Its report retains the same binary and allowlist hashes, parser hash `9712B1A7BC3E1F605380FABF56D454E001B02933A97E074B683ED972769A945F`, and the one violation at `0x14065bcd8`.

The parser fixes resolve bounded table-data classification. They do not establish Layer via Copy native matrix completion, runtime behavior, performance, or native ARM64 execution. The WIP checklist remains authoritative for those open gates.
