# Transform worker core — 2026-10-02

Status: native clean-matrix qualification completed with process Exit 0 at `2026-10-01T23:30:11.1759728Z`. All six profile result receipts report Exit 0. x64 runs executed the frozen test suite; ARM64 results establish cross-builds only, with no ARM64 runtime claim. This record separates that clean matrix from the earlier private core qualification.

## Scope

The native frozen run covers the six detached core helpers—Distort, TransformPixels, TransformCommit, FloatingPixels, FloatingOpening and FloatingMerge—plus FollowingHistory and the production `workerClear` patch. The patch is active in the frozen scope. Each x64 profile passed 409 independent core checks, with the assertion groups listed below; existing legacy goldens remain unchanged. These core tests do not establish migration or qualification of the production transform/opening/preview/settlement host callers.

The archived private r1 core run contains 409 passing assertions:

| Group | Assertions |
| --- | ---: |
| Distort / warp | 56 |
| TransformPixels | 33 |
| TransformCommit | 64 |
| FloatingOpening | 48 |
| FloatingMerge | 131 |
| FloatingPixels | 34 |
| FollowingHistory | 43 |
| **Total** | **409** |

The final archived log has exactly these group counts. The original inline oracle and all legacy goldens are unchanged. Group D7 reserves later members' old rasters as part of the group budget; it does not waive those retained owners.

## Archived private qualification and budget correction

The preserved qualification is `.build/p1-distort-draft/floating-merge-core-qualified-r1/`. Its source freeze contains 510 sources; its evidence manifest contains 45 entries. The final `floating-merge-core-run-r1` receipt reports exit code 0 at `2026-10-01T23:05:36.4315666Z`, with executable SHA-256 `B4FFC8A9E5406D610E0F1F16098F14AC3FDC2BDCADE29CE2C16A165FA7059415`. The successful log contains all 409 assertions above.

The earlier `.build/p1-distort-draft/floating-core-failure-r1/` receipt is retained as a failure: FloatingPixels' 512-by-512 canvas-budget assertion failed (`floating-work.planner.budget-case-open-prepared`). The qualified r2 run used the smaller 8-by-8 workaround and passed its 506-source run; it did not establish the 512-by-512 case. The subsequent merged r1 restores the exact 512-by-512 source with source-clipped Clear and passes that budget case. It also passes a positive Clear crossing a tile boundary with a four-pixel committed limit, while retaining the negative control that refuses a committed source grid beyond its caller limit. The original failed receipt is not hidden or replaced.

The original agent test snapshot is preserved at `.build/p1-distort-draft/floating-merge-agent-original/`, before the primary const moved-input correction. Primary review added mask and budget-boundary assertions; the final counts above include them, with no assertion weakening. The earlier floating-opening qualification requires settled history at depth zero. Later nested-floating and cancellation changes in the private current tree are outside this native freeze and are not used as proof here.

## Native clean matrix status

The frozen input manifest `.build/p1-transform-core-r1-freeze.json` contains 529 entries. The process receipt `.build/p1-transform-core-r1-process.json` reports Exit 0 at `2026-10-01T23:30:11.1759728Z`; the process log records all six profiles passing and confirms the 529-file freeze. Matching `.log.result.json` receipts report Exit 0:

| Platform / configuration | State at capture |
| --- | --- |
| x64 ASan Debug | Passed, `2026-10-01T23:15:33.9647319Z` |
| x64 Release | Passed, `2026-10-01T23:19:33.1127421Z` |
| x64 Debug | Passed, `2026-10-01T23:24:19.4497579Z` |
| ARM64 ASan Debug | Cross-build passed, `2026-10-01T23:26:06.8548637Z` |
| ARM64 Release | Cross-build passed, `2026-10-01T23:28:27.8305987Z` |
| ARM64 Debug | Cross-build passed, `2026-10-01T23:30:11.1308223Z` |

The receipts and logs are `.build/p1-transform-core-r1-{x64,ARM64}-{ASanDebug,Release,Debug}.log` and the corresponding `.log.result.json` files. The completed qualified archive is `.build/p1-transform-core-r1-qualified/`: its source manifest lists 529 sources and its evidence manifest lists 296 files. Verification found all manifest-listed files present and matching their recorded SHA-256 values. The manifest SHA-256 values are:

| Archive manifest | SHA-256 |
| --- | --- |
| `source-manifest.json` | `1A2C7B786241E9432E8CB7AACF59A4B8FD413B0C4B57BF933851A6EBE266105B` |
| `evidence-manifest.json` | `E2B1A0EF711A1829B9738963FF6131EFAF73A66983581EADA8AA6E779BC97A4B` |

The earlier private archived qualification above is a separate run.

## Limits

Production transform host callers and G1/performance remain open. This record makes no claim for Photoshop comparison or native ARM64 execution. The separately archived private core run is evidence only for its exact 510-source executable; it does not qualify later binaries. ARM64 results are cross-build qualification only.
