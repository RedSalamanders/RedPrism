# Owned color-commit continuations

## Implementation checklist

- [x] Audit the current write, discard, import, size, layer and window entry points against the owning contracts.
- [x] Finish qualification of the busy controls and ordered commit engine in the parent P1.1c.3a–b record: all x64 test configurations and ARM64 cross-builds passed.
- [x] Add and qualify the scheduler's allocation-free, coalesced UI-action source with bounded scheduling and borrowed shutdown ownership. Application registration and its initial bridge are tracked below; production producers remain inactive.
- [x] Qualify sequential same-ordinal history acquisitions, including late same-kind completions and commit-then-size before queued Undo. Full x64 gates and all ARM64 cross-builds passed.
- [x] Define owned request payloads, completion receipts and explicit outcomes; preserve relative input order.
  - [x] Qualify the owning observer/publisher model and typed event-time paths, full size drafts and layer/lifetime inputs. All x64 configurations and ARM64 cross-builds passed; producer/runner activation remains open.
  - [x] Qualify capacity-based request admission, the combined queued/active byte budget, immutable retained payloads and reservation retirement. All x64 gates and ARM64 cross-builds passed; borrowed-input preflight and staged-state admission remain open.
  - [x] Bound borrowed path/name values before copying them and reserve later selected output/state before activating production producers.
    - [x] Add bounded, allocation-free borrowed-input preflight and copy only after whole-input admission; preserve ordered Unicode batches and optional values. All x64 gates and ARM64 cross-builds passed.
    - [x] Own later selected output paths under the same combined budget, including replacement peak and publication/destruction ordering. All x64 gates and ARM64 cross-builds passed.
    - [x] Qualify refusal before copying, partial-copy/receipt allocation failures, queue/active moves and later-path replacement; run all x64 gates and ARM64 cross-builds. 77 request checks per normal run, 154 with ASan stress.
    - [x] Adopt preflight at each producer's first copy, add direct bounded population for drop/read sources, and admit any new variable stage state as part of the atomic runner/producer migration.
      - [x] Direct admitted path-buffer population and bounded dialog adapters: qualified in all x64 tests and ARM64 cross-builds.
      - [x] Staged production runner and all required producer activation: qualified: 84 event cases plus owned Shape release; full six-configuration matrix passed.
- [x] Publish outcomes for refused, stale and cleared queue entries before releasing their owned receipts.
  - [x] Qualify per-entry retirement callbacks for superseded, stale and cleared entries, including retained observer state and FIFO ring wrap. Full x64 gates and all ARM64 cross-builds passed; production publication remains part of the runner.
- [x] Implement the staged settlement/write/discard runner and document/session-end lifetime checks.
  - [x] Qualify the Application source bridge with numeric panel requests, deferred terminal publication, exact acquisition wake-up, full queue, failure, cancellation and shutdown. All x64 configurations and ARM64 cross-builds passed; production producers remain inactive.
- [x] Migrate Save, Save As, PNG/JPEG export and every replacement/close caller together with production OK.
- [x] Migrate ordered file batches, copied size drafts, panel/tool/layer interactions and busy callback guards.
- [x] Prove each caller's success, failure, cancellation, stale result and queue refusal with actual event gates.
- [x] Run the complete x64 gates and ARM64 cross-builds; record the remaining GPU/IO/history blocking paths.
- [x] Update domain contracts and the parent checklist, commit/push, and move this child plan to Done.

Status: COMPLETE. Parent: [`UiNeverBlocks_2026-09-22.md`](../WIP/UiNeverBlocks_2026-09-22.md), P1.1c.3c–d.
The user authorized implementation through P6 and commit/push at verified steps. This child records the concrete
host migration; the domain contracts remain authority. The staged production runner and typed producers are
implemented and qualified in all six configurations. GPU readback, file encoding/decoding and size kernels move in later
P1 slices.

Owners: `Application*`, `RedPrism/ContinuationRequests.h`, `Common/ContinuationReceipt.h`, `Common/Jobs.*`,
`Common/HistoryWork.h`, `Common/ContinuationQueue.h`.
Contracts: `Core_Architecture`, `Core_PerformanceAndResources`, `Document_Tools`, `Document_History`, `UI_ApplicationChrome`,
`UI_Windowing`, `IO_ProjectFormat`, `IO_ImageCodecs`, `Testing_Validation`.

## Production activation, 2026-09-30

The runner and all listed production producers passed all x64 test configurations and ARM64 cross-builds.
[Activation evidence](../../TestRuns/HostContinuationActivation_2026-09-30.md) records 14 direct-read checks per
normal run, 84 caller outcome cases, the owned Shape-release gate and session-end message tests. Release's real
4K loop completed all 15 operations; remaining raster/history/GPU/IO stalls keep P1 and G1 open. Source qualification
was frozen. The model/primitive notes below describe historical checkpoints before this activation.

Progress, 2026-09-30: busy controls and the ordered commit engine are qualified in the
[engine record](../../TestRuns/OrderedColorCommit_2026-09-30.md). Scheduler/source and repeated-acquisition
primitives are qualified in the [continuation record](../../TestRuns/ContinuationSources_2026-09-30.md).
Application registration and the initial owned panel bridge passed all x64 configurations and ARM64 cross-builds below.
These primitive checks do not qualify the child plan's integrated production activation matrix.
Per-entry retirement is qualified in the [retirement record](../../TestRuns/ContinuationRetirement_2026-09-30.md).
The queue supplies every dropped entry before release. The typed host request/receipt model passed all x64
configurations and ARM64 cross-builds in the [ownership record](../../TestRuns/OwnedContinuationRequests_2026-09-30.md);
production producer admission and staged publication remain open.

Initial bridge progress is recorded in [HostContinuationBridge_2026-09-30.md](../../TestRuns/HostContinuationBridge_2026-09-30.md).
It registers the source, bounds numeric requests to eight pending plus one active, preserves real worker errors,
and publishes after acknowledged apply/drop. Each future asynchronous history finisher must call the common
`ResumeHostHistory` hook after barrier release. Capacity-based request accounting now follows inputs through queue,
active work and retirement; qualification is recorded below. Actual selection/stroke/IO migration, borrowed-input
preflight and later-stage storage admission remain open. Cancel after acknowledged panel success/failure preserves that result; multi-stage
write/discard actions need an explicit policy for cancelling their remaining stages.

Request admission is qualified in [HostInputAdmission_2026-09-30.md](../../TestRuns/HostInputAdmission_2026-09-30.md):
38 checks per normal HostTests invocation, 76 with ASan stress, actual host quota/receipt-allocation controls,
full x64 Debug/Release/ASan gates and all three ARM64 cross-builds. This does not activate production file callers.

Borrowed-input implementation, 2026-09-30: the builders and request-owned later output paths are implemented;
all x64 gates and ARM64 cross-builds passed in [BorrowedHostInputs_2026-09-30.md](../../TestRuns/BorrowedHostInputs_2026-09-30.md).
Preflight inspects bounded lengths/counts without copying, reserves before payload/result allocation and returns
every partial-construction charge on failure. Later output replacement retains the previous path until the new
path is fully admitted and owned. The staged runner and atomic production caller migration remain open; §4.1
names each first-copy boundary and the direct-population requirement for drop/read sources.

## 1. Outcomes and ownership

Each accepted request has an owning payload, a session document serial, an input ordinal and an owning completion
receipt. Outcomes are `Pending`, `Succeeded`, `Cancelled`, `Failed` and `Stale`. Admission and successful completion
are distinct. A returned pending receipt cannot authorize a write, import, replacement or close.

Use a typed request variant for command/tool/layer events, writes, ordered paths/import choice/drop point, size
draft, New, Close Project, Close Window and session end. Copy event arguments before returning from their callback.
Queued requests contain no document or pixel snapshot. Take immutable work only when the shared history barrier
permits the oldest request to start. A multi-file drop is one request in the eight-entry continuation ring.

Store only event-time arguments in queued entries. The active request additionally owns its stage, selected path,
prompt answer, panel opening, outstanding work stamp, saved revision and any dependent action. Avoid generic
callbacks that can capture a borrowed path, draft, drop message or temporary document.

Completion receipts are owned values and remain readable after the request leaves the queue. Worker code never
accesses them; the owner publishes their outcome after apply/drop. Tests retain a receipt while pumping the shared
harness. Production never waits, polls a future, calls `SettleJobs` or makes quiet mode synchronous.

Queue retirement must use the retirement-aware overloads to receive each owned entry before dropping it, so
stale/replaced entries, Escape clearing and shutdown publish their final receipt outcomes. Counts remain available
for notices. `HostRequest::Retire` maps stale to Stale and replaced/cleared to Cancelled; dropping a still-pending
unique publisher is a Cancelled fallback. Only the first terminal outcome is published.
Queue refusal leaves the caller's payload owned and publishes a failed admission receipt. Prove that none of these
paths strands an externally retained receipt in Pending.

## 2. Guaranteed UI wake-up

The existing scheduler's ordinary UI-action queue has eight slots. A completion cannot lose its dependent action
because those slots happen to be occupied. Add one registered, stable host `UiAction` source, requested with a
coalesced owner-thread ready flag. Its object belongs to Application; workers cannot invoke it or borrow its state.

`RunUiActions` runs at most one source/queued action per call, outside message/control/completion dispatch. When
both kinds are ready, alternate so neither starves. The ready flag participates in `HasUiWork`, `IsIdle` and event
reset/wake accounting. Reentrant drains remain no-ops. Requesting a source that is already ready allocates nothing.
The host disarms a request that is waiting for a worker and rearms it on the specific completion; it never retries
an unchanged waiting state in an idle loop.

Registration and ready-flag lifetime follow the scheduler's owner/lock rules. Shutdown stops host admission,
invalidates intents, unregisters the source outside callbacks, then cancels/joins workers before destroying owners.
Prove full ordinary-action queue, coalescing, fairness, self-rearm, event-driven idle, modal reentrancy and teardown
with HostTests. Normal `PostUiAction` callers keep their existing ownership-on-refusal behavior.

## 3. Settlement stages

| Stage | Required work | Next condition |
| --- | --- | --- |
| Select oldest | Drop requests from a replaced document with localized status; retain the selected payload | No earlier history operation is pending |
| Confirm discard | Evaluate current unsaved work and ask Save / Don't Save / Cancel once for this stage | Cancel ends without settling drafts; Don't Save explicitly permits discard; Save selects the write path |
| Select output | Own the chosen/current project or export path and format | Cancellation ends the request before committing pixels |
| Settle edits | Settle typed opacity and appropriate canvas edits; admit immutable color commit under its ordinal | `Pending` retains the active stage; only `Succeeded` advances |
| Apply/drop | Validate opening, sequence, serial and target; finish history before releasing the barrier | Failed/cancelled/stale prerequisite suppresses its dependent action |
| Write | Snapshot the post-settlement document/revision and serialize to the retained path | Write failure/cancellation suppresses replacement/close; mark only the written revision saved |
| Execute dependent action | Revalidate the same document/request/session-end lifetime, then import, resize, replace or close | A stale lifetime never inherits an earlier Save answer |
| Finish | Publish the owning receipt and wake the next ready input | No retained input or resource is silently discarded |

A discard request first waits for existing admitted history work. It does not newly commit a merely open panel
before asking: the current domain contract requires Cancel to preserve every draft. The Save answer then settles
what will be written. A failed donorless fill retains its panel and aborts the required write/import/resize.

History ordinals order accepted inputs across classes. Some requests require more than one sequential history
operation (panel commit followed by import/resize). Preserve the request's relative order for these suboperations.
If extending `HistoryWork` to continue an ordinal, give each acquisition a distinct generation and require the
exact previous finished stamp; retain strict initial Begin ordering. A late suboperation completion cannot finish
a later acquisition with the same ordinal/kind. Prove this combined path before activation.

Save currently performs synchronous encoding, and export performs synchronous composition readback/encoding.
This slice fixes ordering/ownership; P1.3/P1.7 replace those waits. Do not claim G1 from a continuation alone.

## 4. Caller inventory and payloads

| Caller | Owned values and policy | Required proof |
| --- | --- | --- |
| Panel OK / Enter | Panel opening and selected ordinal; freeze mutation controls only after admission | Actual worker apply once; Cancel/view input; competing ordinal; late/stale/failed result |
| Save / Save As | Selected/current path, save-as choice and eventual written revision | No serialization before successful settlement; failed commit creates no output and preserves panel |
| PNG / JPEG export | Selected path, format and post-settlement source/resolution | No readback or encoding after failed settlement; both formats independently |
| New | Action and old document lifetime | Save, Don't Save, Cancel and failed Save independently |
| New Canvas Apply | Complete copied dimensions/draft and card invocation | Cancel/failed Save preserves card and document; later typing cannot change the accepted request |
| Project open | Owned path and old document lifetime | Failed load preserves document; only a successful prerequisite permits replacement |
| Image as new project | Whole ordered batch, first-success position and chosen policy | Initial missing file, Save/Discard/Cancel, no replacement when every file fails |
| Close Project | Old document/request lifetime | Failed/cancelled prerequisite keeps document; successful flow empties shell |
| WM_CLOSE / Exit | One close request, confirmation state and callback-safe window lifetime | Window stays usable while waiting; no duplicate prompt or destruction inside callback |
| Windows session end | Request generation, block reason and close intent | `WM_ENDSESSION(FALSE)` invalidates late close; block reason persists until this specific flow completes |
| Image as layer / Import Images | Whole batch, chosen choice and optional document-space point | Actual failed panel commit aborts import; accepted batch remains one queue entry |
| OpenPaths / drop | Whole original path ordering, project/image classification and point | Projects first, then one image question; project+image drop centers images; active batch alone rebinds after its replacement |
| Canvas / Image Size Apply | Complete typed draft and old serial | Explicit panel-dismissal/settlement policy; no resize after failed required commit |
| Tool / new panel / adjustment creation | Tool/command identity | Failed prerequisite cannot cancel a panel and fall through into the requested action |
| Layer select/target/invoke/reorder/rename/opacity | IDs, target flags, reorder placement and edit phase/value | No history/target mutation beneath an admitted snapshot; revalidate IDs at execution |

Opening a size card deliberately dismisses a panel today. Keep that decision explicit in its contract and test;
an Apply request must not reinterpret a failed required commit as consent to resize. Window resize/DPI and view
commands do not settle edits. There is no Recent/Revert or positional-file-open command to invent in this slice.

### 4.1. Producer migration order after the borrowed-input gate

The next runner/producer change must address the first allocating copy, not only its Application endpoint:

| Existing source | Concrete boundary to migrate | Admission/result requirement |
| --- | --- | --- |
| `OpenPath(const wchar_t*)` | Bound the terminated scan, form one borrowed view on the stack, and construct the owned batch through admission | Null/empty policy stays explicit; overlong input fails before `wstring`/vector construction |
| `WM_DROPFILES` | Check `DragQueryFileW` count and all lengths before allocating path buffers; retain the drop owner through bounded reads | Whole-batch admission before copying; exact queried/read lengths; no truncation or partial queue entry |
| `ReadDialogResult` / `PickImagePaths` | Keep shell-owned strings alive while preflighting, and bound result count before constructing host strings | Distinguish cancellation, read failure and quota/OOM; avoid a duplicate uncharged Application copy |
| `ChromeHost::CloseRenameCard` / `OnRename` | Pass a borrowed text view through the immediate callback and copy it into an admitted request before closing/reusing the field | Capture the rename target/card lifetime at the event; keep the existing document trim/UTF-8 name checks at apply |
| `SaveDocument` / `ExportFlattened` | Select/retain the output through the active request before settlement or readback, then snapshot the post-settlement revision | Quota/OOM/cancel performs no write/readback; publish success only after the requested file operation succeeds |
| Discard/replacement/window/session end | Carry the prompt answer and lifetime as bounded stage state; perform Save as a prerequisite on the same selected ordinal | A pending/failed/stale write cannot authorize replacement or destruction; cancelled session end invalidates its late close |

The view-copy builders handle already readable borrowed buffers. A producer such as `WM_DROPFILES`, whose names
require a length query and a fill into a destination, also needs reservation transfer/direct bounded population
under the admitted ticket; creating a temporary uncharged string batch first is not an implementation of that
producer's gate. Keep immediate borrowed readers/callbacks out of queued payloads. Any new variable stage state
must gain admission at its first allocating copy. The runner and producer activation matrix remain one migration.

## 5. Cancellation, replacement and later stroke integration

Cancel/Escape cancels Commit/Selection work and clears queued commands, while preserving the barrier until the
completion drops. A pointer-up stroke's recording is retained under the parent plan; this command queue does not
own or discard stroke input. Mutating commands/Undo/Redo queue, other canvas gestures follow the existing busy
refusal policy, and view/layout/menu input remains available. Guard callbacks and keyboard paths in addition to
disabling controls; disabled numeric focus loss must not change frozen settings.

An active ordered file batch can intentionally replace the document and rebind its own remaining stages to the
new session serial. Other queued requests retain their original serial and become stale. Do not use the persisted
CanvasDocument ID as a lifetime token.

Before a delayed destructive replacement/close, revalidate saved revision, document lifetime and retained stroke
input. P1.4 must integrate the rule that a younger recorded stroke cannot be lost because an earlier Save answer
authorized a close. Revoking that destructive continuation leaves the surviving document/input available; an
already running save may finish only its captured revision. Implement and test the chosen resolution with stroke
replay before claiming the complete host gate.

Session end has its own generation. A cancelled session end cannot borrow a later request's block reason or close
confirmation. Maintain the existing critical-end policy; add saving-specific behavior with P1.7's IO stage.

## 6. Validation and activation

First prove scheduler/source ownership and stamp continuation independently. Then gate actual commits and assert
each inventory row's observable effects: file existence/content, history entries, surviving pixels/draft/window,
prompt count, completion receipt and queued-input order. Use a forced three-way answer seam for Save as well as
Don't Save and Cancel; a Boolean forced answer cannot cover all branches.

Cover admission/allocation failure, actual runner `bad_alloc`, no-source fill, late successful pixels before Cancel,
stale Unchanged, document replacement with reused persisted IDs, eight pending entries plus ninth refusal, command
arguments changed after enqueue, hidden completion priority and shutdown. Stale completions cannot authorize
dependent writes. Keep existing numeric goldens unchanged; migrate harness observations with shared event pumping.

Activate production OK and all required dependent callers in the same tested migration. Remove obsolete
settle/discard Boolean APIs after converting every caller. Complete skills/format checks, all three x64 test
configurations and all ARM64 cross-builds. Re-measure relevant real-loop operations separately from builds and
record the remaining GPU/IO/history costs. Native ARM64, R2 and Photoshop comparisons remain separate gates.
