# Basic raster edits and bounded save recovery — 2026-10-01

Status: P1.5 A1/A2 and P1.7 F6a.1 functional qualification passed. A3 performance, remaining P1 and G1 are open.

## Implemented boundary

Production Invert, foreground/background Fill, Clear and selected Delete prepare coverage, sparse tile pixels,
growing masks and semantic mask thumbnails on a private CPU worker. Capture follows complete admission under
the selected host ordinal. Exact source/transform/mask/selection/session tokens guard one named history edit;
late cancellation and stale results add no history. Workers borrow no session, HWND, chrome or GPU engine.
The existing pointer brush and WARP compute engine retain their behavior.

Admission includes snapshot metadata, history entry growth and trim bookkeeping in addition to worker planes,
tile windows and coverage. History preflight still traverses records and Begin/End still allocate on the owner.
Prepared allocation-safe history, incremental accounting, bounded capture/chrome and first upload remain P1.9.
Gradient and distortion prerequisite settlement remain synchronous until their separate A4/A6 migration.

Exploded-save rollback now retries only actual native Access Denied or Sharing Violation: at most three native
attempts, with lazily created one-shot event waits at 25/100 ms and a 1 s failure bound on each wait. Healthy
saves allocate no timer. Injected faults remain final. Late cancellation cannot abandon restoration; exhausted
recovery preserves the complete backup and the original publication/rollback errors, without false Published.

## Independent correctness evidence

- 75 `rastereditwork.*` checks: literal image/mask output, selection exterior, placed/transformed masks, sparse
  inputs without materialization, exact/lower pixel budgets, independent memory floors and cancellation.
- 372 `rasteredit-host.*` cases cover the five commands on image/mask targets before/after preparation, actual
  admission/capture/worker OOM, early/late cancellation, returned errors, stale image/transform/mask/selection/
  generation/restore, document replacement and younger Undo. They assert literal pixels, one history entry,
  view input while held, ownership until acknowledgement and terminal zero tickets/barriers/charges.
- Five `history-accounting.edit-admission*` checks independently cover large layer/name/history/pending floors.
  The existing 24 `rasterwork.*` family checks also pass, including added bounded sparse mask/thumbnail paths.
- All 33 `rotation-fault.*` checks pass: existing injected boundaries, exact old/backup trees and cleanup,
  explicit read-only/deep-fixture teardown assertions, real child-reader error 5 and directory-reader error 32,
  release on retry, bounded exhaustion and subsequent backup-only recovery. Deadlines are failure bounds.
- Existing numeric legacy goldens, host/document/I/O/composition suites, WARP self-test, ISA gates, crash probes,
  UI smoke and harness screenshots pass. ASan extended stress and the actual detection probe pass.

## Qualification corrections

The original ASan save rollback failed with native error 5 after injected publication failure and late cancel.
No project-tree handles remained in that test process at the diagnostic inspection; a later independent native
rename succeeded. The external reader/filter is not identified. Controlled child-reader tests reproduce a
possible native mechanism and qualify the bounded policy; they do not identify the original culprit.
Windows documents the open-child restriction for [directory rename](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/ns-ntifs-_file_rename_information).
Keep original failure logs `.build/p1-raster-edit-initial-asan-rollback-failure.log`,
`.build/p1-raster-edit-rollback-diagnostic.log` and `.build/p1-raster-edit-handle-diagnostic.log`.
Timer creation/arming/wait resource-fault injection, flush/close faults and the original first-save cause remain F6.

The initial Release ISA gate decoded an anonymous jump-table data byte as EVEX. The guarded parser now accepts
the actual exact RBP disp32 MOVSX form as well as disp8, retaining same-owner/PDB/contiguity/default/alignment/
target checks. All 226 parser controls pass, including wide-frame acceptance, adjacent real VEX and rejection
controls. No ISA allowlist exemption was added. Original report/log remain under
`.build/p1-raster-edit-initial-isa-report.json` and `.build/p1-raster-edit-initial-release-isa-failure.log`.
Three new draft pixel expectations were corrected against the independent rounding formula before qualification;
existing legacy goldens were not changed.

## Configuration gates and identities

| Gate | Result |
| --- | --- |
| Skills / format | Passed: 15 skills / 283 native files |
| x64 Debug / Release full suites | Passed |
| x64 ASan Debug full suites, extended stress and detection probe | Passed |
| ARM64 Debug / Release / ASan Debug | Cross-builds passed; native execution unavailable |
| Frozen inputs through all six configurations | Passed: 331 source/project/script inputs |

Driver: `.build/qualify-raster-edit.ps1`; logs: `.build/p1-raster-edit-driver.log`,
`.build/p1-raster-edit-{x64,ARM64}-{Debug,Release,ASanDebug}.log` and skills/format logs.
Targeted host output `.build/p1-raster-edit-host-target7.out.log` records the 372 host cases; the complete suite
also runs them but its GUI self-test stdout is not redirected into the matrix log.

- Source-freeze manifest SHA-256: `AC34FB04D023EA522B7F9E8E33491076D86B2FEA12D7817B196D4E022CB777B2`.
- x64 Release 0.1.118.0 executable SHA-256: `5C145531CCDBD133EFA606CA50538434F2E2D90164E5B846430268BC4BDCA559`.
- Qualified executable/PDB/DLLs and both source/binary manifests are preserved in `.build/p1-raster-edit-qualified`.

## Performance qualification remains pending

The actual hardware 4K benchmark refused presentation before operation 0 (`zoom-actual`): visible=1,
displayOn=0, suspended=0, occluded=0, presents=0. No timing rows were produced. The guard remains enabled.
Failure evidence: `.build/p1-raster-edit-hardware-4k.log` and benchmark driver log.
The detected machine is Ryzen 9 9950X3D (16 physical/32 logical cores), RTX 5080 driver 32.0.16.1714,
Windows build 26200, 66,155,405,312 bytes RAM, balanced power scheme, scalar ISA/32 workers.

Retry the preserved qualified binary on an active desktop for hardware 4K/45 MP/100 MP and WARP 4K, retaining
raw trials and message/action/apply/drain/frame scopes. Use valid current G1 fixtures: 45 MP with two full image
layers, one mask and one adjustment; 100 MP with one image layer. No timing or Photoshop speed claim is made.
