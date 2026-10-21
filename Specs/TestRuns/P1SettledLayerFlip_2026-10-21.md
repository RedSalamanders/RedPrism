# P1 settled single-layer flip migration

Status: saved WIP. Focused DocumentTests and the corrected hidden Application pass; the current Release build
fails its mandatory ISA gate. The unqualified scanner/test repair is saved privately. P1 remains WIP.

## Scope and reference

The ordinary horizontal/vertical layer commands enter the existing ordered host command queue. Eligible settled
single-layer work uses the existing metadata admission, post-admission immutable capture, worker document/mask/
history preparation, original publication owner and deferred retirement. Failed admission cannot fall through
to synchronous layer cloning. Group/canvas flips, live edits and transform-header actions keep their separate
reference behavior and remain outside this migration.

The numeric oracle is archived behavioral reference and `LayerMask.swift`: center reflection,
negated rotation, the selected flip toggle, unchanged sampling, linked mask movement and unlinked placement
preservation. The existing in-process tests retain every predecessor assertion and legacy golden.

## Reviewed authoring packet

`Z:\src\RedPrism\.build\P1Drafts\LayerFlip-Main-r1` binds seven original worktree hashes. All seven match before
integration after compiler-host publication `d3fb3dc`. Main reviews the complete helper core-test diff and adds
ordinary-command and joined-Close controls to the existing fixtures. Static review identifies a missing
own-command terminal-receipt assertion; the corrected host fixture captures the original queued receipt and
ordinal, then checks the exact terminal outcome/HRESULT. The reviewer rechecks the fix.

Authored expectations, not observed runtime results:

- 266 core assertions: H/V flag combinations, literal origin/size/rotation/sampling, mask placements and owners,
  history/Undo/Redo/involution, guarded and no-op outcomes, move/stale rules, and existing cancel/allocation stages.
- 28 ordinary `OnCommand` cases with 158 checks and a summary: admission before capture, held source, original
  ordinal/receipt, younger Undo, cancel/fault/stale outcomes and original strong/weak accounting.
- 22 joined-Close ownership assertions and a summary through `OwnedMetadataJob`. This is fixture-level Close
  evidence; actual public-command Application Close still requires its own integration proof.

The tests extend `LayerMetadataWorkTests.cpp`, `ApplicationLayerMetadataSelfTest.cpp` and
`Document.Tests.OwnedMetadataJobsR4.cpp`. They introduce no runner or qualification framework.

## Validation

Use the existing canonical build and affected DocumentTests/hidden WARP Application executables for development.
Retain all failures and check complete logs. Full current-source Debug/Release/real ASan, Release analysis,
ARM64 cross profiles, native runtime and final P1 gates remain due after a coherent integration batch.
No speed, ordinary prepared-row/display readiness, G1, H3 or P1 completion follows from static review.

### Initial development build

`flipdevr1` exits 1 before runtime. Main reads the complete stdout/stderr and preserves the same-attempt
3,248-file archive, manifest SHA-256 `6A6DF583028CAE2ECC27A3EBEAD1B12F1609A856DCB8A2BB4685E16A72992FF7`.
The 1,905-input freeze is `CEBFB3ECE215FA2CDD746C66632FCFE75F54CC795AC1CA86AFE879604DE8783F`.
The new Main-authored host fixture calls the nonexistent `ToggleLayerVisibility` instead of `ToggleVisibility`
and shadows its preceding `graphDestroyed` local. Both fixture errors are corrected without changing test
expectations or production behavior. Successful DocumentTests linking in this failed solution build is no
runtime or complete-build qualification. The failed receipt and available possibly stale products stay retained.

`flipdevr2` compiles and links the repaired source, then exits 1 during mandatory ISA parser controls:
`WriteAllBytes` reports insufficient space. Main reads the full stderr and every stdout line, including all
1,911 PASS records; no scanner assertion failure is observed. The 3,247-file failed archive has manifest
`3A1BEFD88F9511545F6B83B2E4E45A5217550BDCE826674F2FC5EA4965448E6B` and source freeze
`1C6FE7661F436FC9A7DEA225024B63718C19B6BAC545759F3BE5E587A47D0943`.
This remains a failed mandatory build gate, with no runtime qualification inferred.

The apparent C: fixture path is an existing junction to the full Z: generated-test root. C: has 37.9 GB and
S: has 2 TB available; Z: has about 5 MB. New fixture directories now route through the same C: access path
to `S:\CodexScratch\RedPrism\P1_2026-10-21\BuildProcessTests`. All 1,588 legacy directory targets are individually
checked and retained through child junctions; the original C: junction is retained as
`BuildProcessTests-before-S-20261021`. No fixture data is moved or deleted. The routing receipt and complete
legacy-link map live in `P1Evidence/p1-settled-layer-flips-r1` through its S: junction.

`flipdevr3` passes the complete Release build and ISA gates after routing recovery. The independent profile
reader verifies all 1,022 compiler records and five PE/full-PDB pairs. The 3,255-file archive manifest is
`E67D4E983EDA0728172A7858B0E802BBBAC6BD67A8E14ABCCDFBB7F46FBEEDC6`; its 1,905-input freeze is
`34E4E646CEDF711D1C9E9E3710F3D67C076CC2C0EF3682968DEFDF1D057BB4FB`.
Focused DocumentTests exits 0 with 24,324 PASS records, including the authored core flip and joined-Close checks.
The hidden Application exits 5 at the first new ordinary flip's final state/history predicate. Main reads both
complete logs and verifies unchanged source and executable/PDB bookends after the failure. Its queue,
original receipt, admission and off-owner retirement predicates pass; no runtime qualification is inferred.

The new host fixture uses `ImageLayer::Blank`, which deliberately has no raster asset. The worker correctly
returns a successful no-op under the unchanged `CanTransform` rule; the fixture incorrectly expects a flip.
The corrected ordinary cases use a real raster and a literal nonzero rotation (12.5 to -12.5 degrees). Two
explicit ordinary assetless no-op cases retain the original refusal behavior. Production behavior and legacy
goldens are unchanged by this fixture repair; the corrected 28-case/158-check run remains pending.

### Corrected focused runtime and retained ISA refusal

`flipdevr4` compiles/links, then build PID 66628 exits 1 at the unchanged mandatory ISA gate. Main reads its
complete stdout/stderr and retains the 3,247-file archive, manifest
`9ABB0BD8F6FE12AA49943F0AACA332FB051966D3CBD9F06C6C5BC98FA19D49F9`, source freeze
`98DD15FB0F804D2F8B950DCD7CF861C8D92F34368A714CE7104E6B5C473C1B61` (1,905 inputs).
The flagged bytes are an anonymous 37-entry DWORD table in `Application::OnAdjustAction`, decoded as
`vcvtsi2sd` at `0x1402a1cb8`. Its exact producer/SIL guard/RSI index/EAX dispatch form is outside the old
scanner proof. No allowlist or compiler-flag relaxation is applied.

For focused behavior diagnosis only, the same image runs the hidden WARP Application: PID 75032 exits 0
at 12:05:30 UTC, including all 158 new ordinary-flip checks / 28 cases and the explicit assetless no-ops.
Source inputs and executable/full-PDB bytes remain unchanged before/after. The image SHA-256 is
`70DDADBEDC9C06A0EB34164D0C8E2508AC5A9345D529C604F116E3D115ECF9F4`; PDB
`A94E146C090C3CEECB9940FB6A960F92438D700C6CE80BD37B2E26791268CAE5`.
Raw streams, actual started/result records and `focused-bookends.json` live under `flipdevr4/Focused`.
The earlier `flipdevr3` DocumentTests Counter comparison separately proves every 24,035 predecessor occurrence
and exactly 289 additions; `main-document-counter.json` retains the full delta.

This passing focused runtime does not repair the failed build, establish a full profile, qualify actual
public-command joined Close or close P1. At the user's checkpoint request, Main's incomplete strict parser
and the helper's 323 authored controls are copied/hash-verified into the saved checkpoint. They have not
compiled/executed together. The source scanner is restored to its exact original bytes after backup; the
existing failing gate remains unchanged. Follow the saved continuation in the parent WIP plan before resuming.
