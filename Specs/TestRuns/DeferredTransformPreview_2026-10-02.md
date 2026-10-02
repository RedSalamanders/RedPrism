# Deferred transform preview foundation — 2026-10-02

Status: all three full x64 runtime suites and all three ARM64 clean cross-builds pass against 548 frozen inputs.
This qualifies the opt-in deferred model, writer guards and preview host framework. Production ordinary-transform
policy, Alt-duplicate activation, remaining device/shutdown scenarios and measured G1 costs remain open.

## Qualification checklist

- [x] Pass 47 deferred-model and 100 writer/outline controls plus unchanged DocumentTests goldens.
- [x] Pass 90 preview host controls, the existing 344 settlement controls and complete WARP application tests.
- [x] Pass skills, formatting and six clean native profiles; ARM64 results are compilation evidence.
- [x] Preserve the original rejected Release executable before repairing its ISA data recognition.
- [x] Pass strict parser proof on that unchanged executable and 91 additional positive/negative parser controls.
- [x] Verify the 548-input freeze and archive all six images, symbols, receipts and ISA evidence.
- [ ] Activate production deferred transform policy and qualify duplicate/pointer/lifetime callers.
- [ ] Measure capture, apply, chrome, first upload and G1; complete the full P1 exit.

## Functional scope

The deferred model displays its last complete worker-prepared document and mapped selection outline. Both color
and mask distortion previews cap dimensions at 1024. The host admits one active/latest pending graph, retains its
old ticket and reservation through acknowledgement, and checks opening, settings revision, generation, source
and mailbox before complete publication. Reopening cannot accept an older completion. CPU display aggregates
retire off the owner thread.

Deferred writers refuse direct inline commit/cancel/redraw bypasses. Confirmed local discard and capture-loss
requests use ordered settlement; save-prompt Cancel preserves the draft. Direct reference APIs and existing
numeric goldens retain their behavior. Ordinary application transform openings still use the reference policy
until the remaining caller migration qualifies.

## Clean native matrix

The r2 matrix process exited 0 at `2026-10-02T03:02:48.6453347Z`, with `PASS freeze 548 files`.
Every x64 suite contains the 47 model and 100 guard passes; each WARP application log contains 90 preview host
passes and `transform-host summary: 344 checks, 0 failures`. All builds use the repository's `-Rebuild` gate.

| Profile | Result | Completed UTC |
| --- | --- | --- |
| x64 Debug | Full runtime suite passed | `2026-10-02 03:02:48` |
| x64 Release | Full runtime suite passed | `2026-10-02 02:58:11` |
| x64 ASan Debug | Full instrumented runtime suite passed | `2026-10-02 02:54:35` |
| ARM64 Debug | Clean cross-build passed | `2026-10-02 02:46:53` |
| ARM64 Release | Clean cross-build passed | `2026-10-02 02:45:12` |
| ARM64 ASan Debug | Instrumented cross-build passed | `2026-10-02 02:42:51` |

The exact archive `.build/p1-transform-deferred-r2-qualified/` contains 548 sources and 297 evidence entries,
as a junction to `C:\Users\eric\AppData\Local\RedPrism\QualificationArchives\p1-transform-deferred-r2-qualified`.
All copies were made as bytes and SHA-256 verified. Source-manifest hash:
`F263D2A3CDD52D936B261054015331563321BB0DC2A400FB2D1BE6794B11620D`.
Evidence-manifest hash: `8A3230B40D84626188151B567BFA471ACF2F9F34ACA20DA03F1A6E5F5EE91986`.

## Preserved failures and ISA proof

The clean r1 matrix passed ARM64 and x64 ASan, then rejected Release before its runtime suite. A DWORD target
in `Application::ClipboardPasteProbeJob::Run`'s terminal switch table was decoded as `vunpcklpd`. Its exact
547-source/294-evidence candidate is retained in `.build/p1-transform-deferred-r1-failure/`; Debug was not reached.
The rejected image hash is `EB9F5DC2807FE9CD3D1513D0E992AACE3E43FB750E9C6F57D3931287CBF08868` and PDB hash
`3D2D8CEFD1DCBAF6C2FBEAD40E6AAABF6258CB70AB4ABAFFF5A28EB7FD4D448A`.

Recognition requires exact MOVZX EAX from RSI+disp32, CMP AL/unsigned JA and contiguous RDX dispatch rows,
corroborated PE/listing bytes, anonymous same-owner PDB start, guard-derived end equal to the procedure end,
decoded same-procedure default/table targets and terminal RET/alignment. It grants no object or procedure
exemption and changes no runtime allowlist. Proof on the unchanged rejected image passed at
`2026-10-02T02:27:31.2505509Z`: 442 ordinary/runtime vector rows, seven verified table-data rows, zero violations.
This parser proof executed no image or synthetic instructions.

The first parser attempt omitted a required guard-row candidate; its source and failed proof are retained in
`C:\Users\eric\AppData\Local\RedPrism\QualificationArchives\p1-deferred-parser-proof-failure-r1`.
The first two new fixture revisions also remain under `isa-alguard-controls-failure-r1` and
`isa-alguard-controls-failure-r2` in that archive directory. Review corrected arbitrary displacement/default
targets being treated as invalid, preserved distinct case names and moved the adjacent real-vector fixture
outside the proved table. Final controls verify actual adjacent VEX/EVEX rejection, exact bounds, registers,
scales, PE/listing/PDB evidence, decoded targets and RET/alignment. The final standalone log is
`.build/isa-alguard-controls-r4.log` and reports 91 AL-guard controls passing; these also run in native build gates.

Earlier private model/guard/host proofs and their failed revisions are indexed in the
[execution checklist](../Plans/WIP/AsyncRasterEdits_2026-10-01.md). No native ARM64 runtime, full P1 completion,
RAW throughput or Photoshop comparison is claimed by this record.
