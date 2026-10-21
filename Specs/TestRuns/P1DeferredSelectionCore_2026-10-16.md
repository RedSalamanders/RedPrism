# P1 deferred selection core — private CPU Release qualification

Date: 2026-10-16. Scope: isolated S5 core and descriptor tests. **All results below are private CPU qualification;
none is adopted into the native checkout.** No actual Application route, GPU route, ordinary activation, complete
phase-bound, performance/G1, native ARM64 runtime, or P1 Done claim follows.

## Latest private qualification state (2026-10-16)

- `.build/P1Drafts/S5CoreQualification-r3-Main/main-qualified.json` independently qualifies the private x64
  Release candidate: 146 new assertions plus all 21,863 predecessors (22,009 total), exact source roster,
  262 compiler records, ISA and PE/PDB checks, and all 1,848 archived files. Archive completion SHA-256:
  `EB96EDCE41202B2E688DEC5D6847E770E580500572E1F8D21D9E603E1BD5B4F4`.
- `.build/P1Drafts/S5CoreAsan-r1-Main/main-qualified.json` qualifies the corresponding private x64 ASan Debug
  candidate with the same 22,009 assertions and a real ASan detection probe. The expected negative probe exits 1
  and its diagnostic hash is recorded in the receipt; this is not full-application ASan qualification. Archive
  completion SHA-256: `76D07B9DE369EF2108C279FD14EFD7E1DC4905E9602A09E1BEF48A5F241A5266`.
- `.build/P1Drafts/S5CollapsedHitBefore-r1-Main/main-reproduced.json` retains the controlled pre-fix failure:
  exactly `deferred-selection-move.collapsed-finite-offset-extent-is-not-movable-like-numeric-reference` fails;
  all other 22,009 records pass. Its 1,846-file archive is verified, completion SHA-256
  `384F5C1FB66947E1F48EE09D3EC38B0F38B10C3B3384EDBDECBC918C3DAD387D`.
- `.build/P1Drafts/S5CollapsedHitAfter-r1-Main/main-qualified.json` qualifies the corrected private Release
  candidate with 147 new assertions plus all 21,863 predecessors (22,010 total), while retaining the pre-fix
  failure and its evidence. The 1,850-file archive is verified, completion SHA-256
  `B5AA5B26825FE40DD38C43027610AF2CB7C5F2CB5772A9AE7F0F8669F37B2E95`.
- `.build/P1Drafts/S5CollapsedHitAsan-r2-Main/main-qualified.json` qualifies the corrected private real-ASan
  candidate with all 22,010 assertions and a real negative use-after-free probe (expected exit 1). The 1,857-file
  archive is verified, completion SHA-256
  `38D605DFA39D938146621C89464046B5718472231F56D18E33B932269A2CD26C`.

The private CPU results cover scalar outline movement; prepared opening, closure and publication; exact
cancellable hit traversal; bounded physical-press-prefix capture; selected translated-output geometry and weak
control lifetime. The bounded physical prefix and all other S5 pieces remain unwired to actual host dispatch.
Preserve every earlier failure and its archive; these receipts establish isolated candidate behavior only.

Native integration remains pending. Analysis r1053 failed on C6262 (Application frame 16,464 bytes; Clipboard
frame 131,752 bytes). Native Release r1054, real ASan r1055, and ARM64 Debug/Release/instrumented-ASan r1056–r1058
were not launched. Main subsequently adopts 29 reviewed source/test/project paths with three owning contracts; fresh canonical
analysis r1059 is running on 1,393 inputs. The private receipts establish no fresh-matrix pass. Ordinary host activation, native ARM64
runtime, G1 and P1 Done remain open.


## Implementation and checks

The isolated candidate keeps original outline geometry immutable during movement and stores only a finite,
rounded display offset. End builds translated geometry and prepared history; current-only publication swaps
roots. Cancel/no-op preserve original geometry and redo. Existing numeric APIs remain references.
A prepared opening constructs Begin history privately before owner publication. Exact cancellable containment
uses translated vertices, preserving boundary rounding, and checks both loops and edges within 1,024 work steps.
The fixed 512-event press descriptor retains event-time coordinates, settings, modifiers and ordinals, with
separate Up/Lost and overflow terminals and explicit loss sealing at ordinal exhaustion. It is not host-wired.

Independent literal CPU assertions cover geometry/holes/negative-half rounding, capture and publication state,
undo/redo, source/opening/offset/generation/session refusal, panel refusal, cancellation and history allocation
faults, unchanged geometry identity, exact boundary tolerance, empty-loop/edge checkpoints, and a -2^48 floating
rounding fixture. Physical-prefix assertions cover all 512 accepted events, refusal, terminal preservation,
acknowledgement and nonwrapping clocks. Main corrects delegated fixture API, include, checkpoint-count,
immutable-identity and even-ordinal modifier mistakes before accepting the final candidate.

These are CPU model/static-preparation tests; they do not demonstrate actual Application worker ownership,
GPU rendering, actual pointer dispatch, view/DPI parity or complete gesture admission/settlement.

## Actual successful candidate r2

- Source/output root: `.build/P1Drafts/S5CoreQualification-r2-Main/`; separate private products/intermediates.
- Complete Release rebuild PID 82240, exit 0, 14:11:11.156937–14:17:57.895116 UTC.
- Actual DocumentTests PID 34864, exit 0, 14:18:10.534056–14:18:21.918094 UTC.
- All **21,863 predecessors + 56 move/opening + 32 hit + 37 press-input = 21,988 assertions** pass.
- Actual stdout: `523BE2A01992FB6D884CC5B38AF9C2469E553D7AE89BA38C936D5E1ADC4741C3`.
- The complete strict reader exits zero; its LF/CRLF positives and eight rejection controls passed before build.
- Main compares all **261 actual compiler commands** with the exact project source roster and checks the
  actual Release `/MD`, `/O2`, `/W4`, `/WX`, language/conformance and precise-floating-point flags.
- The linked ISA scan has no violations, using the unchanged runtime allowlist.
- No dependency or DxUi project is built. All 155 files of the previously qualified immutable vcpkg snapshot
  remain byte-identical. All source/dependency/product bookends pass before/after execution.

Executable SHA-256: `F0CEEE630868E9FDD4FA06A77633D673267FA68F6B2C98823EE085DA7587B579`.
PDB SHA-256: `57DE49D80FA725C5E43B48C61B7DD12BD0982FA13D184AFBF11DDAFE78FB63EB`.
Main compares actual CodeView GUID `30072AA9-EED7-48FC-8BCB-4EEFAD640566`, age 1, with the linked full PDB.

Main independently verifies all **1,845 archived files**:
`Z:\CodexArtifacts\RedPrism-P1\2026-10-16\P1-S5Core-x64-Release-private-r2`.
Manifest SHA-256: `27A7499BE3D55F1090675260588493A59C3FFCCAD52F19834209B04BE96C051D`.
Completion SHA-256: `B6CCB539E04AEF4EAC368C13DA1306B1B4332A2EAA8DCA17D765BB3E4EA18B4B`.
The local `main-qualified.json` binds actual exits, all inputs, archive, compiler records, ISA and PE/PDB evidence.

## Original failure retained

Private r1 rebuild PID 85944 exits 1 at 14:08:33 UTC with eight fixture compile diagnostics: two attempts to
mutate immutable `SelectionLoops` via `emplace_back`, plus six namespace/name diagnostics from missing
`Selection.h`. No runtime starts. Main verifies all **1,815 failed archive files** before the fresh r2 candidate.
It uses the public immutable initializer-list constructor and explicit include, retaining literal vertices
and every original expectation. Original source, objects and logs stay intact.

Failed archive completion: `AF41799D192B32C33BDAD9970D460449ECBBD6EA0B4EC50A9C122BEF55EB1035`.
Failed manifest: `64443C7ECD371441CE8E2C57EEA597675F335162EE4CA9D2B656C92EF28BE30E`.

## Remaining S5 work

Original-gesture reservation and physical-prefix host integration, all branch/close/display/cursor consumers,
preserving-panel authority, view/device correctness and complete operation bounds remain open. The private r3
selected translated-output geometry and weak-control lifetime extension is now qualified in Release and real
ASan, including the corrected collapsed-finite-offset hit-parity case; see the receipts above. The actual host
route remains unwired. Main adopts the reviewed core and fixture heap repairs; the fresh combined-source native matrix remains due. Ordinary activation, native ARM64 runtime, complete phase bounds and G1 remain unqualified.
