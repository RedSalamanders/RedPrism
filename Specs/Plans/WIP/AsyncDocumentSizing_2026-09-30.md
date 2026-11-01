# Asynchronous Image Size and Canvas Size

> **HOLD** with [P1](UiNeverBlocks_2026-09-22.md) since 21 October 2026. Do not resume this file until that plan leaves HOLD. Checked boxes below are historical qualification, not permission to continue.

## Implementation checklist

- [x] P1.8.1 — Define immutable size work input/result and the validated UI apply boundary; keep all current size math and numeric expectations unchanged.
- [x] P1.8.2 — Route Image Size through an ordered, cancellable Commit job with memory admission and stale-result rejection.
- [x] P1.8.3 — Route Canvas Size, including optional colored extension fill, through the same ordered Commit path.
- [x] P1.8.4 — Extend document and host tests for copied drafts, cancellation, ordering, failure, stale results, undo/redo and existing size scenarios.
- [x] P1.8.5 — Update owning contracts, record full configuration qualification and real 4K/45 MP/WARP measurements.
- [x] P1.9 H1 size publication — Move source capture after complete admission, bind detached document/history,
  transfer retired drafts and qualify five history faults, capture refusal and stale epoch across all profiles.
- [x] Adopt selected original Size slot/device recovery and predelegate worker retirement after private actual ASan state proof, including real history allocation failure; latest full native qualification follows the parent checklist.
- [x] Add focused actual-publication and separate setup/warmup benchmark reporting; private Size/direct/sampler smoke and all six selected hardware/WARP Release large-fixture runs pass. Inclusive Apply maxima are at most554 microseconds; frame/drain overruns, initial-display costs, complete bounds, ordinary activation and latest full native qualification remain open.
- [ ] P1.8 exit — Meet the 2 ms owner-apply target after the P1.9 history/chrome/upload work; current measured failures are retained in the evidence.

Status: ACTIVE. Parent: [UiNeverBlocks_2026-09-22.md](UiNeverBlocks_2026-09-22.md), P1.8.
Implementation is authorized by the user's request to complete all P1. Functional qualification is complete;
the measured apply gate remains open. [Evidence](../../TestRuns/AsyncSizing_2026-09-30.md).

Current work: cancellable resizers and private sparse reads are integrated. The production size-card route now
submits an immutable Commit job with a whole-graph reservation, generation/card validation and one history ordinal.
The owner retains the request until final apply/drop and acknowledgement. Event-gated host checks, independent
document tests, all x64 suites and all ARM64 cross-builds pass. Hardware 4K/45 MP and WARP 4K each complete all
18 real-loop operations. The 45 MP resize requires D13's explicit 2 GiB host history limit; the earlier 256 MiB
run correctly failed because its required undo entry was trimmed. Apply times still exceed 2 ms in several
cases, and first-upload/action costs remain measured P1.9 work. P1.8 and G1 are not closed.

The H1 size migration passes the full frozen x64 suites and ARM64 clean cross-builds with 50 independent
assertions and 22 actual host scenarios in each x64 profile. It preserves resizers and numeric goldens; worker
preparation owns the complete history candidate, and owner apply validates token, active layer and epoch before
nonthrowing ownership swaps. The first ASan folder-save failure is preserved and remains F6 work; only the final
r2 matrix qualifies this slice. [Evidence](../../TestRuns/SizeHistoryPublication_2026-10-01.md).

## Current selected publication result — 2026-10-20

The selected original-ticket route now has191 Size and473 unchanged Ctrl host assertions, including
actual post-resize history allocation failure, original-worker cleanup and slot/device recovery. Private
Release and real-ASan standard Application runs pass; the complete current native matrix now also passes full x64 Debug/Release/real ASan, all three ARM64 cross-builds and canonical Release analysis.
All six selected Release fixtures keep inclusive completion Apply at most554 microseconds. The retained
owner/frame and initial-display overruns still require resolution before the P1 exit. This does not
activate the selected route in ordinary use. See the [current evidence](../../TestRuns/P1SizeRecoveryAndCost_2026-10-20.md).
The user confirms that no native ARM64 runtime host is available; cross-builds do not qualify runtime.

The initial source inventory and recommended shape below describe the September30 migration. Later H1
admission and original final-publication ownership supersede those implementation suggestions; current
behavior is governed by the owning document/history/performance contracts and the parent checklist.

## Scope and contract

Move the expensive preparation for the existing Image Size and Canvas Size actions off the UI thread. Both remain one user action and one history entry (`Image Size` or `Canvas Size`) after any older history work has completed. Preserve every existing dimension, resolution, anchor, fill, sampling, mask, selection, active-layer, error, no-op, undo and redo behavior. `New Canvas` and Crop are outside this slice; Crop continues to use its current `ResizeCanvas` path.

The master plan assigns P1.8 to “Image Size and the Canvas Size fill as Commit jobs.” Its requirements include the shared history barrier and copied-draft and cancellation tests. The P1 exit still requires the master plan's applicable R1/W evidence, every due self-test and legacy async test, and the measured G1 criteria; completing this child alone does not close P1 or G1. Record unavailable hardware qualification separately.

## Baseline implementation and anchors (before this slice)

- `Common/CanvasSize.h` defines `CanvasSizeDraft`, `ImageSizeDraft`, `CanvasSizeOptions`, `ImageSizeOptions`, `ResizeStatus`, `ResizeCanvas(const CanvasDocument&, const CanvasSizeOptions&, CanvasDocument&)`, and `ResizeImage(const CanvasDocument&, const ImageSizeOptions&, CanvasDocument&)`.
- `Common/CanvasSize.cpp` performs the existing preparation. Both functions read a const source and write a detached output. Canvas fill allocates and clears the full extension raster; Image Size rasterizes each image/mask and creates thumbnails. The existing routines catch `std::bad_alloc` and return `ResizeStatus::Memory`; they currently have no cancellation input.
- `RedPrism/ApplicationSize.cpp`, `Application::ApplySizeCard`, validates the live draft, calls `SettleInteractions(false)`, builds options from the draft and current palette colors, calls the resizer synchronously, closes the card, and applies success using `_session.ApplyDocumentSize(std::move(resized), name)`. Failures are translated to the existing Too Large or Resize notice; `Unchanged` leaves document history alone.
- `RedPrism/ContinuationRequests.h` has `HostSizeApplyInput { HostSizeDraft draft; uint64_t invocation; }`. `ApplySizeCard` copies `_sizeDialog` and `_sizeInvocation` into this queued input. `ExecuteHostInput` rejects a mismatched invocation or closed card, then restores the copied draft before applying. Reopening a card increments `_sizeInvocation` (with overflow refusal), so an old queued action cannot apply to a newer card.
- `RedPrism/ApplicationContinuations.cpp`, `RunHostContinuation`, selects the oldest host request, settles a pending color edit through `StartColorCommit`, and runs the selected action only after successful prerequisite completion. A failure/cancel/stale prerequisite finishes the host request without running the dependent action. `_activeHostRequest` retains the copied draft while work is pending.
- `Common/HistoryWork.h` provides `HistoryWork::Begin`, `Continue`, `IsCurrent`, `CancelRequested`, and `Finish`. `Finish` is required after UI apply/drop, never on worker completion alone. `Application::ResumeHostHistory` publishes the prerequisite result and rearms the existing continuation source. `ApplicationReadbackJobs.cpp::StartUtilityReadback` demonstrates beginning or continuing the selected request's ordinal and holding `_hostCommitStamp` until completion.
- `Common/Document.h` exposes `DocumentSession::CaptureJobToken` and `CanApplyJobResult`; a token without a target validates session identity, document serial, generation, restore serial and canvas dimensions. `DocumentSession::ApplyDocumentSize` in `Common/Document.cpp` validates same document id and document graph, clears outline drafts, installs one named history edit and preserves the active layer, but currently returns `void` and does not take a token.
- Existing numeric coverage is in `Tests/DocumentTests/DocumentTests.cpp`: `canvasdraft.*`, `canvas.*`, `imagedraft.*`, and `imagesize.*`. The real size-card path is in `Application::RunSizeSelfTest` in `RedPrism/ApplicationSize.cpp`; it already checks Canvas Size fill and anchor behavior, Image Size nearest sampling and 300 ppi, Escape, and three successive size undos. The pending-gradient case proves a preceding edit is committed before Canvas Size and remains one history entry below it.

## Recommended implementation shape

### 1. Freeze the request before asynchronous work

Keep `HostSizeApplyInput` as the owner of the exact `HostSizeDraft` and invocation selected by Apply. Keep its lifetime through the existing host continuation and receipt. Do not let the worker read `_sizeDialog`, `_session`, palette state, `_viewport`, the window, or chrome.

On the UI thread, after the existing required settle succeeds and before submission:

1. Recheck the request invocation/card lifetime and current document presence.
2. Derive `CanvasSizeOptions` or `ImageSizeOptions` from the copied draft using the same conversions and option choices as `ApplySizeCard` today. Canvas extension color is captured from foreground/background/custom ARGB at this point. Image Size with resampling off captures current document width/height and the copied draft's resolution; sampling remains the selected `LayerSampling`.
3. Capture the immutable `CanvasDocument` value (its raster handles share immutable storage) and a no-target `JobToken` from `CaptureJobToken()` after preceding history work has applied. The snapshot and token must describe the same generation.
4. Submit a Commit job keyed by the current document serial. Its owned input is only the document snapshot, options, token and request identity needed for UI completion. Its output is `ResizeStatus` plus the detached resized document on `Ok`.

The existing `ResizeCanvas` and `ResizeImage` functions are the exact preparation entry points and should remain the single source of resize arithmetic. Call them on the worker with the captured source/options. Preserve `ResizeStatus::Unchanged`, `TooLarge`, `Invalid` and `Memory` distinctions. Do not create a second implementation of the size math.

### 2. Add an explicit, checked UI apply boundary

Add a `DocumentSession` apply entry point alongside `ApplyDocumentSize`, recommended as `bool ApplyDocumentSizeIfCurrent(CanvasDocument, std::wstring, const JobToken&)`. It must check `CanApplyJobResult(token, token.sequence)`, same document id, and `ValidateDocument` before mutating anything; on success it must retain the current `ApplyDocumentSize` behavior (drop outline drafts, one named history transaction, active layer retained). Return false without mutation on stale/invalid input. Alternatively, keep the existing method and make the UI completion perform the token check immediately before it, but then completion must treat any failed apply as a failed/stale job rather than assuming success. Prefer the checked API so validation and mutation are one session operation.

On completion, the UI thread checks the exact active history stamp/request and cancellation state, then checks the token and applies only a successful current result. Only a successful changed `Ok` result sets `followsFit` and calls `DocumentChanged`. `Unchanged` and each existing resize failure keep their current UI behavior and create no history entry. A stale result is dropped without applying it. Clear the active size ticket/state, call `HistoryWork::Finish` only after apply/drop, then call `ResumeHostHistory(stamp, outcome, error)` so a younger request cannot snapshot before this decision.

The size commit must continue the currently selected host ordinal if the same host action first committed a pending panel. Use the same `Begin`/`Continue` discipline shown by `StartUtilityReadback`: start a fresh Commit stamp when there is no finished prerequisite for this ordinal; otherwise continue the exact `_hostFinishedStamp` in the same document. Do not allocate a second ordinal for one Apply action. Mark the host prerequisite active (`_hostCommitStamp`) before returning to the outer pump, and retain the active request until the job's UI completion releases it.

### 3. Cancellation and admission

Add size-job-owned state/ticket parallel to the existing commit jobs; do not reuse the color editor's preview state. Escape or the applicable host cancellation requests cancellation through `HistoryWork::RequestCancel` and `JobScheduler::Cancel`, but keeps the history barrier, active request and all snapshot/result ownership until the worker acknowledges and UI completion drops the result. A late `Finished` result after cancellation must never apply. Queue replacement, card close/reopen, document replacement, undo/redo restore and shutdown must all resolve through stale/cancel/drop paths without releasing the barrier early.

Give the job an explicit `JobOptions::memoryBytes` reservation for its complete additional allocation peak; the captured immutable source snapshot is already resident and is not charged again. Canvas fill can declare its extension raster and thumbnail peak from the requested dimensions. Image Size must account for the aggregate output image and mask pixel limits plus thumbnails and resize bookkeeping. Use checked 64-bit arithmetic and fail admission locally when the reservation cannot be represented or granted. Record any unavoidable conservative over-reservation here with measured evidence; do not submit a zero-byte reservation for work that allocates substantial outputs.

The current resizers have no cancellation checkpoints. Add a cancellable worker-facing overload/helper that preserves the existing pixel and iteration order while checking cancellation at bounded work intervals (per layer and within long row/pixel loops such as Canvas Extension fill and image/mask rasterization). Keep existing non-job callers, including Crop, source-compatible and behavior-identical. If cancellation cannot be observed inside a current rendering kernel, the job must still retain its barrier and discard the completed output after acknowledgement; document and measure that limit rather than claiming prompt cancellation.

### 4. Test slices

- Keep all existing `DocumentTests` resize goldens and assertions unchanged. Add focused cancellation tests for the worker preparation path proving no partial result is published and the original document remains unchanged.
- Extend `RunSizeSelfTest` with an event-gated size worker. While held, assert the live document/history are unchanged, the history barrier remains active, and the request owns the exact submitted draft/options even if the card is edited or closed afterward. Release the gate and assert the requested values, fill color, sampling and history name are the ones applied.
- Prove order: a pending color edit commits before sizing; the size is one entry directly above it; a younger queued history mutation does not start/snapshot until size apply/drop and `Finish`; size refusal/failure prevents dependent continuation from running.
- Prove cancellation before worker start, during worker preparation, and after preparation but before UI completion. In each case the original pixels/dimensions/history remain intact, late success is dropped, the barrier remains until acknowledgement, and the next queued request resumes exactly once.
- Prove stale rejection after document replacement/restore and after closing/reopening the size card. Check no stale result changes a new document or the newer card, and the request receipt reports the appropriate terminal outcome.
- Cover Canvas Size transparent grow/shrink across anchors, colored fill cutout, fill budget refusal, unchanged size, invalid options and memory/admission failure. Cover Image Size resampling on/off, resolution-only update, nearest/smooth/high choices, rotated and hidden layers, masks including placed/uniform masks, limits, failure and one-entry undo/redo. Existing numeric legacy goldens remain the pixel oracle; do not rebaseline them.
- Check status/notice mapping remains Too Large versus Resize failure, active layer and selection-outline behavior match current semantics, `followsFit` changes only on successful application, and every terminal path releases the job reservation and history barrier.

## Behavior requiring care during implementation

`ApplySizeCard` currently calls `SettleInteractions(false)` immediately before its synchronous snapshot. `RunHostContinuation` also settles a pending color edit before executing a host size request. The existing `SettleInteractions(false)` ends document-bound interactions, and `EndCanvasEdit` may commit some of them synchronously. P1.8 must snapshot only after that same settle behavior and must not capture the source earlier when the host request is queued. The exact product policy for a failed pending interaction settle is not represented as a return value from `SettleInteractions`; inspect the affected `EndCanvasEdit` branches when implementing and preserve existing behavior unless the owning P1.5 decision has already changed it. Do not broaden this size slice into transform/warp migration.

`ResizeCanvas` and `ResizeImage` catch `std::bad_alloc` internally and translate it to `ResizeStatus::Memory`, while scheduler-level failures arrive as `JobResult::Failed`. Keep these failure channels distinct through the UI mapping and guarantee neither can create a history entry. The existing `ApplyDocumentSize` is `void`; this is why stale validation and checked apply are important.

## Exit gates

- Existing direct numeric size goldens remain unchanged and pass in `DocumentTests`.
- The real WARP host self-test covers the asynchronous production route, event-gated ordering, copied draft/token lifetime, cancellation acknowledgement, stale completion, each terminal status and undo/redo.
- No size preparation, full extension fill, layer/mask resampling or thumbnail creation runs on the UI thread. UI apply stays within the master plan's apply budget; record the measured operation and apply timings in the P1 evidence.
- No busy polling or added timer/thread/queue is introduced. Hidden/minimized completion continues through the shared scheduler event and outer pump.
- Run the repository validation matrix: `validate-skills.ps1`, `format.ps1 -Check`, `test.ps1` x64 Debug, Release and ASan Debug, and `build.ps1 -Platform ARM64` for Debug, Release and ASan Debug. Native ARM64 runtime claims require a native ARM64 host.
- Update the owning document/tool and performance contracts for durable behavior, cancellation bounds, allocation estimates and validation requirements. Keep the WIP index, this checklist and the master checklist current during implementation.

## Source documents

- `AGENTS.md`
- `Specs/README.md`
- `Specs/Core/Core_PerformanceAndResources.md`
- `Specs/Document/Document_Model.md`
- `Specs/Document/Document_History.md`
- `Specs/Document/Document_Tools.md`, “Canvas size, image size and crop (C16)” and “Validation / Sizes”
- `Specs/Plans/WIP/UiNeverBlocks_2026-09-22.md`, P1.8 and P1 exit (G1)
- `.agents/skills/spec-workflow/SKILL.md`
- `.agents/skills/performance-resources/SKILL.md`
