# Continuation scheduling primitives — 2026-09-30

Status: scheduling and ordering primitives qualified; production continuation activation remains open

## Changes

`JobScheduler` accepts one stable, borrowed owner-thread UI-action source. Requests coalesce into one readiness
flag, independently of its eight ordinary action slots. Dispatch consumes readiness before the callback and
alternates source/ordinary work when both are ready. The source uses the existing event and the one-action-per-pass
bound; waiting continuations must rearm on prerequisite completion. Registration/removal refuse callback-time
changes. Stop drops readiness and Close removes the pointer without invoking or destroying the source.

`HistoryWork::Continue` admits another operation of the same selected input, only from the exact latest finished
stamp in the same document. Every acquisition gets a distinct generation, including the same ordinal and kind.
Initial Begin still rejects a reused ordinal. This prevents a late prerequisite completion releasing the dependent
stage. Prerequisite outcome and live document validation remain host policy.

The Release matrix also exposed a scanner false positive after unrelated link-layout changes: DWORD switch-table
targets in `SystemClipboard.obj` decoded as eight `vmovlps` instructions. The matching anonymous PDB symbols,
actual PE entries and decoded body targets establish that span as data. The scanner now proves the observed
hoisted R8 image-base prelude, in addition to its original contiguous forms. Only its exact preserving instruction
sequence and valid branch/table destinations are recognized, with a mandatory matching data symbol. The runtime
allowlist is unchanged; neighboring code remains gated. The same Release binary passes with eight additional
verified table-data rows and its original 414 runtime VEX/EVEX instructions.

## Evidence

HostTests uses literal dispatch order, ownership, event and state expectations: a full ordinary queue and retained
ninth action; repeated coalesced requests; source first then alternating FIFO work; a source rearming for a later
pass; reentrant drains and callback-time removal refused; pending removal without invocation; replacement source
identity; stop and Close without callback or borrowed destruction. The primary reviewed a cheaper agent's test
draft and strengthened pending removal, replacement, failure diagnostics and exact identity checks.

The initial focused run's broad idle assertion included an independent OS memory notification. Source tests now
use the existing deterministic memory seam, so their wake/idle observations isolate this feature. Real pressure
notification coverage remains in the existing scheduler/memory tests.

History tests cover unfinished, other-owner and replaced-document refusal, sequential stages before a younger
stroke, late same-kind completions and expired prerequisite stamps. DocumentTests commits a numeric inversion,
then Canvas Size under one request while a younger Undo remains queued. The first Undo removes size while
retaining inverted pixels; the next restores the original bytes. These are actual document/history operations,
without changing legacy goldens.

Final review strengthened the existing document-serial fixture to preserve the acquisition stamp, isolating the
changed serial. Cross-owner cases now create matching ordinal, document and acquisition values on both owners,
so owner identity is the sole refusal reason; the foreign request also leaves the other owner's own work usable.
All final x64 suites include these corrected fixtures.

The scanner controls prove the exact hoisted prelude, incomplete/wrong instructions, changed base, both branch
bounds and decoded destinations, exact data ownership and missing data even beside RET. Neighboring real VEX
is still rejected. The primary corrected the agent draft's encoding, address placement and byte/listing alignment;
LLVM also decodes the positive fixture's two table rows as the intended legal VEX sequences. Synthetic parser
controls are never executed as native code. All existing compiled ISA controls remain required.

## Validation

| Gate | Result | Evidence under `.build/logs/` |
| --- | --- | --- |
| Skill validation and formatting | Passed: 15 skills, 201 source files | Final matrix output |
| x64 Debug full tests | All tests passed | `continuation-source-x64-Debug-tests.log` |
| x64 Release full tests | All tests passed | `continuation-source-x64-Release-tests.log` |
| x64 ASan Debug full tests | All tests passed, including actual sanitizer detection and stress | `continuation-source-x64-ASan-Debug-tests.log` |
| ARM64 Debug cross-build | Passed, pinned dependency provenance verified | `continuation-source-arm64-Debug-build.log` |
| ARM64 Release cross-build | Passed, pinned dependency provenance verified | `continuation-source-arm64-Release-build.log` |
| ARM64 ASan Debug cross-build | Passed, pinned dependency provenance verified | `continuation-source-arm64-ASan-Debug-build.log` |
| Synthetic ISA parser controls | All controls passed | `continuation-source-isa-parser.log` |

No compiled source changed during the final x64 matrix. ARM64 object timestamps were checked against the final
history fixture; Debug and Release were rebuilt after the last fixture edit, and ASan Debug already contained it.
No benchmark ran concurrently with builds. No elapsed-time assertion or image-speed claim is made.

## Limits

Application has not yet registered a production source or migrated its owned write/discard flows. These changes
qualify scheduling and ordering primitives, not production OK/Save/Close/import or the whole-host G1 target.
Native ARM64, GPU/IO migration, SIMD/RAW work and Photoshop comparison remain outstanding.
