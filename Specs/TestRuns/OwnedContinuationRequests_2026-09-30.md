# Owned continuation inputs and receipts — 2026-09-30

Status: owned input/receipt model qualified; production continuation activation remains open

## Changes

`ContinuationReceipt` retains one UI-owned result without a borrowed host/document pointer. A unique movable
`ContinuationCompletion` accepts only the first terminal outcome/error. Dropping or overwriting its pending
ownership publishes Cancelled; explicit stale/failure publication takes precedence. Read/copy/move/publication
allocate no storage. Request construction creates one shared state; an eventual owning admission boundary must
translate allocation failure into a failed request using the allocation-free immediate failure value.

`HostRequest` owns typed event-time input: a whole ordered Unicode path batch, write arguments, complete size-card
draft, layer/tool values and panel/session-end lifetime keys. Inputs own no pixel/session snapshot or callable.
Publisher retirement precedes owned input release. The existing Application size draft and outcome/image-choice
enums now use these same definitions; production producers and the staged runner are still inactive.

The parent RAW plan now requires real ARW, CR3, NEF, RAF and tiled DNG decode/reference tests, hostile-file and
cancel/recovery scenarios, and actual editor bake/save/reopen/export round trips. ViewerImgRaw source inspection
supplied validation/cancellation/resource patterns. No RAW decode or RedSalamander runtime test ran in this step.

## Evidence

The primary implemented the ownership model and typed payloads. A cheaper agent supplied receipt tests; primary
review added moved-from destruction before destination publication, literal retirement trace outcomes and no
callback on refused admission. The request tests use original literal Unicode names, paths, numeric draft fields
and lifetime keys after mutating the event source. Both new public headers compile first in their test translation
units, without a Windows header. Tests exercise actual receipt state through retirement-aware queues.

Independent state expectations cover every terminal outcome/error, invalid/Pending publication refusal, retained
observers, unique move ownership, first-terminal preservation, abandonment, stale/superseded/Clear retirement,
full/invalid admission retaining input, active input surviving Clear and explicit failed publication.
Existing legacy numeric goldens remain unchanged.

Each x64 configuration passed 85 receipt assertions and 21 typed-input assertions. A separate cheaper-agent RAW
matrix review found that P4's old exit did not explicitly require the new real-file matrix. Primary verified the
gap and made §6.9 mandatory at P4/P5 exit, preserving ordinary-suite missing-corpus SKIP reporting separately.

## Validation

| Gate | Result | Evidence under `.build/logs/` |
| --- | --- | --- |
| Skill validation and formatting | Passed: 15 skills, 207 source files | Matrix preflight |
| x64 Debug full tests | All tests passed, including WARP | `owned-continuation-x64-Debug-tests.log` |
| x64 Release full tests | All tests passed | `owned-continuation-x64-Release-tests.log` |
| x64 ASan Debug full tests | All tests passed, including actual sanitizer detection and WARP | `owned-continuation-x64-ASan-Debug-tests.log` |
| ARM64 Debug cross-build | Passed, pinned dependency provenance verified | `owned-continuation-arm64-Debug-build.log` |
| ARM64 Release cross-build | Passed, pinned dependency provenance verified | `owned-continuation-arm64-Release-build.log` |
| ARM64 ASan Debug cross-build | Passed, pinned dependency provenance verified | `owned-continuation-arm64-ASan-Debug-build.log` |

No compiled source changed during the final configuration matrix, and no benchmark ran concurrently with builds.

## Limits

These tests qualify input/result ownership, not actual Save/Close/import effects or the producer's construction
allocation-failure boundary. Variable-length payload admission/accounting, Application source registration, staged
settlement/write/discard and production OK activation remain P1.1c.3c work. Explicit queue retirement is required;
destructor cancellation is a fallback, not a replacement for stale/failure outcomes. The RAW matrix is a plan;
licensed-file integrity evidence does not qualify unpack/develop/persistence or performance. ARM64 builds do not
prove native runtime behavior, and no Photoshop comparison or whole-host responsiveness claim is made.
