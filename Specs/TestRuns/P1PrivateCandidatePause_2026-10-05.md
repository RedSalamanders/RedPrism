# P1 private source checkpoint — user pause, 2026-10-05

## Preservation checklist

- [x] Stop implementation and all assigned agents at the user's request.
- [x] Preserve the complete private dirty source, including untracked source/header/test files.
- [x] Archive exact source copies and the tracked binary Git diff with SHA-256 readback verification.
- [ ] Resume only after a new user instruction; review and qualify each transferred change.

**Status: HOLD / UNQUALIFIED PRIVATE WIP.** This branch is a source checkpoint. Its current contents have
not passed a linked full-suite run. The latest qualified integration source is `1936fe6` on
`codex/p1-opacity-host-proof`, not this branch. The older sampler plan and other documents here are stale;
read the current integration/native checkpoint before resuming. Do not copy this tree wholesale into either.

## Saved code and exact qualification boundary

Worktree: `C:/Users/eric/.codex/worktrees/p1-sampler-callers/RedPrism`.
Branch: `codex/p1-sampler-callers`; pre-checkpoint parent `e0360b0`.
The checkpoint retains 41 modified tracked files and nine formerly untracked source/header/test files,
plus this record and the plan pause markers. Several preceding sampler/history changes are already
qualified separately in the integration branch; this mixed private tree is not their canonical source.

Later candidates saved here:

1. `Common/Document.h`, `Document.cpp`, `DocumentHistory.cpp` and
   `Tests/DocumentTests/HistoryRetirementBoundTests.cpp`: opt-in raster residency epoch, short worker/owner
   gate, owner nonblocking permit and worker reprepare protocol; 23 intended core controls. The qualified
   history index still has a late-materialization window. No production host opts into this new protocol yet.
2. `Common/WarpStroke.h/.cpp` and the added `TestSmearWarp` controls in
   `Tests/DocumentTests/DocumentTests.cpp`: private cancellable source copy through
   `ReadRasterRegionForWork`, no shared `Data()` materialization; 14 intended controls. This does not migrate
   the complete Heal/Warp host route and does not yet cover mid-copy cancellation.
3. `Common/Document.cpp`, `RasterWork.cpp` and `Tests/DocumentTests/RasterWorkTests.cpp`: eager private
   dense thumbnail for fitting sparse Gray8 masks, preserving semantic edge-tone sampling; six intended controls.

Strict code-generation probe r6 compiled four history/document translation units and exited 0 at
`2026-10-05T05:15:00.869798Z`. It predates the final `Data()` early-return guard for an active residency lease.
It did not compile/link/run the new Warp/thumbnail controls. All these later changes remain unqualified.

## Critical next work

- Enforce same-thread residency lease ownership; a public movable lease must not permit unlocking a mutex
  from a different thread. Existing TLS/documentation alone is not an enforced contract.
- Wire successful completion through `Job::DeferCompletion` and same-record `Resume` for worker-only history
  refresh, retaining the original ticket, reservation, FIFO position, intent and revision. Do not rerun pixels.
- Admit the complete extra candidate index/container peak and retain the original reservation until terminal
  acknowledgement. Implement live-history cache-growth notification/maintenance; the current fresh diagnostic
  resamples bytes, but a previous trim decision still needs maintenance after later materialization.
- Validate permit moves/destruction, stale epoch, Busy/Refresh/failure/retry, shutdown and predecessor-root
  publication. Release every owner permit before callbacks/waits/thread transfer.
- Integrate only reviewed hunks against `1936fe6`. In particular, do not replace `ApplicationContinuations.cpp`
  or the full `DocumentTests.cpp` from this stale branch; preserve newer integration diagnostics and tests.
- Qualify linked Debug/Release/ASan suites, current analysis/ARM64 cross-builds and exact preceding registries.
  Intended new document controls total 43 (23 + 14 + 6); 6,729 + 43 = 6,772 if all are integrated unchanged.
  Count observed controls independently; this arithmetic is not a test result.

## Local preservation

Archive: `C:/Users/eric/AppData/Local/RedPrism/QualificationArchives/p1-user-pause-2026-10-05`.
977 files / 116,847,649 bytes were reread and hash/length verified at
`2026-10-05T05:31:39.955270+00:00`. Preservation manifest SHA-256:
`5BC3A3EAFA462817CE34FA2D41C0AD9C03F106B74827BED8B5709337CA958A3F`.
It includes exact dirty source, Git diffs, launchers, code-generation attempts/failures, integration receipts
and the refused sampler timing attempt. It is preservation evidence, not qualification.
Generated archives need separate transfer to another machine. Git carries source/docs only.
