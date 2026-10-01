# Image import and project-open history publication — 2026-10-01

Status: qualified correctness and ownership slice of P1.9 H1. Full x64 suites and all ARM64 clean cross-builds
passed against the same frozen inputs. This record does not close
P1/G1, RAW, Photoshop comparisons, native ARM64 runtime or physical device-removal qualification.

## Implemented boundary

The selected file operation admits its complete capture/preparation/retirement graph before owner capture.
Its separate file-operation authority includes session, document/restore serial, generation, active/mask/crop
target and document identity. The worker never borrows live session state. It builds a complete saved baseline,
optional additional-image edit, selected ids and history publication before either live document or history changes.
Publication checks that authority, exact history epoch and selected host acquisition before nonthrowing swaps.
Saved revision remains independent for existing-document imports; replacement establishes its own saved lineage.

Replacement preserves a live panel/stroke/gradient/transform/draft on failure, and retires it only on success.
CPU preview owners, previous document/display/history/selected/collapsed metadata and paths remain in the whole
release aggregate. Borrowed brush coverage engines end on their owner before those CPU drafts reach a worker
destructor. Capture/worker/allocation/cancellation/stale outcomes retain their owners and reservation until
acknowledgement. Nested ordinary imports preserve the outer transaction's opening, depth and name.
Undo/Redo retains its separate settled-only rule.

## Proof

- Every x64 suite passes 55 `import-history.*` and 157 `import-publication.*` independent assertions. These cover
  saved baseline/additional-image history, literal pixels, Undo/Redo, no-change/nested transactions, exact
  authorities, stale epochs, moved/default/foreign/single-use results, allocation/cancellation checkpoints and
  later saved-revision updates. Eleven pending-context fixtures cover successful replacement and refusal.
- Every x64 application self-test passes 70 `project-open-host*` and 155 `image-import-host*` cases, with zero
  failures. Event gates cover real admitted capture, ordering, failure/cancellation/staleness, replacement over
  nine project contexts and new-canvas color/floating edits, exact reservation/barrier return and complete
  off-owner aggregate retirement. Hidden execution produces no Present.
- Weak-owner tests prove gradient/pixel-move graphs survive publication until aggregate destruction. Coverage
  witnesses require exactly one owner-thread end and no release-worker coverage-engine call.
- All five executable ISA scans pass in Debug, Release and ASan Debug. The 297 synthetic parser controls retain
  the existing compiled misplaced-ISA, runtime owner/function, missing/mismatched-PDB rejection controls.
  No runtime allowlist entry was added.

## Configuration and identity

| Gate | Result |
| --- | --- |
| Skills / formatting | Passed: 15 skills / 341 native files |
| x64 Debug / Release / ASan Debug | Full clean-build suites passed, including sanitizer probe and stress |
| ARM64 Debug / Release / ASan Debug | Clean cross-builds passed; native runtime unavailable |
| Frozen native/project/script inputs | Same 391 inputs verified across all six configurations and after completion |

Source-freeze SHA-256: `02937043F12761433308ED8F4C9330C7E7C1F89BAC3168A305FF9830506CB8BA`.
Release 0.1.128.0 executable SHA-256: `ED6529482E9DFBF9B4ED95BF06CC10B31A39168E095201F7CEFAB32385413834`.

Logs: `.build/p1-import-history-r4-driver.log`, its six platform/configuration logs, skills/format/freeze files
and copied x64 self-test stderr. The Release/runtime/source/binary provenance snapshot is
`.build/p1-import-history-r4-qualified/`.

## Preserved failures and parser proof

The first settled-context preflight injected a before-edit-snapshot failure into clean replacement, which creates
no edit entry. A real replacement-snapshot checkpoint now precedes baseline construction. The later live-context
fixture called the layer-transform API instead of `BeginSelectionTransform`; another reused a deliberately
preserved panel/transaction across `CreateDocument`. Corrected fixtures keep their literal expectations and
explicitly clear the previous test session. Exact binaries, DLLs, PDBs, source freezes and logs remain in:

- `.build/p1-import-history-first-Debug-project-failure/`
- `.build/p1-import-live-first-Debug-floating-fixture-failure/`
- `.build/p1-import-live-r2-Debug-fixture-state-failure/`

Three ISA gate failures were switch/remap tables and exact alignment in the pinned DxUi image, not executed VEX
instructions. Their exact images, parser source, PDB/DLLs, freeze, decoded rows, PE bytes and reports remain in:

- `.build/p1-import-history-first-Release-isa-failure/`
- `.build/p1-import-history-r2-Debug-isa-failure/`
- `.build/p1-import-history-r3-Debug-host-isa-failure/`

The scanner proves same-owner anonymous untyped data symbols, exact contiguous dispatch encodings, all DWORD
targets and complete table bounds. Backward targets require a PDB procedure containing the dispatch, complete
table and every target inside its executable contribution. Terminal byte remaps without a following symbol
require the identical exclusive procedure/contribution end. Every byte before exact one-to-three-byte trailing
NOP alignment is an in-range index. Changed/repeated/nonaligned padding, malformed final entries, unknown or
cross-owner targets and real neighboring VEX instructions remain rejected. All 15 executable preflight scans
and 297 parser controls pass before the final clean matrix. Failed attempts are not retroactively qualified.

Value metadata capture/traversal, other history callers, incremental storage accounting, chrome/first uploads
and the measured 2 ms apply target remain separate work. Event gates establish behavior, not latency bounds.
