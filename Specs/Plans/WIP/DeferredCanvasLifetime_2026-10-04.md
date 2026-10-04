# Deferred canvas lifetime closure (P1.9 H2.2)

## Implementation checklist

- [x] Identify the deadlock: an admitted live brush owns its ordinal until Up/Lost, while a later close waits behind it.
- [x] Read the existing discard-prompt, output-selection, ordered-continuation and retirement contracts.
- [x] Draft bounded exact-key pending detachment; primary owns its implementation, cheaper agent drafts independent queue tests.
- [x] Review and qualify detachment ownership, FIFO, wraparound, capacity and preserved admission barriers in the isolated draft: 21 exact controls in Debug/Release/ASan.
- [x] Draft the private preflight, explicit output owner, same-ordinal selection and modal cancellation guard.
- [x] Qualify 160 original-stream cancellation and 80 accepted Save/discard/write-failure controls with all predecessor application checks in Debug and Release; independently verify both archives.
- [x] Qualify the same frozen lifetime inputs in ASan Debug: all 8,358 controls/74 stages; 792 independently verified copies and actual instrumentation flag.
- [x] Preserve and push the basic preflight source checkpoint (`ffc16df`); all 515 owned inputs match across profiles.
- [x] Prepare 192 modal pointer-loss/reentrancy checks across the same sixteen cancellation scenarios.
- [ ] Qualify the modal candidate with every predecessor application check and all required profiles.
- [ ] Implement nonmutating prompt preflight with scalar document/ordinal/opening authority and reentrancy protection.
- [ ] Preserve the stroke when either the discard prompt or Save output selection is cancelled.
- [ ] Acknowledge Save/Don't Save's original stream before executing the dependent lifetime action.
- [ ] Integrate exact pending-close cancellation and shutdown/critical-session producer retirement.
- [ ] Qualify held/live/modal/failure cases, every predecessor suite and all required profiles; commit/push qualified slices.
- [ ] Complete remaining native caller migration and measure G1 before marking P1 complete.

Status: ACTIVE private implementation. The prior caller (`a0ab3f0`) and basic lifetime preflight (`ffc16df`) are
pushed on `codex/p1-opacity-host-proof`. The lifetime preflight passes private Debug/Release/ASan; its modal
extension is unqualified. No native lifetime route has changed. The [canvas input plan](DeferredCanvasInput_2026-10-04.md)
remains the parent. Exact archive hashes and scope limits are in the
[lifetime record](../../TestRuns/P1DeferredCanvasLifetime_2026-10-04.md).
Owners: `Common/ContinuationQueue.h`, `RedPrism/ContinuationRequests.*`, `ApplicationContinuations.cpp`,
`ApplicationFiles.cpp`, `Application.cpp`, and the acknowledged shutdown/CPU retirement path.
Contracts: `Document_History.md`, `UI_ApplicationChrome.md`, `UI_Windowing.md`,
`Core_PerformanceAndResources.md` and `Testing_Validation.md`.

## Prompt and ordering rules

1. An admitted close/new/end-session input can request a nonmutating preflight while an older canvas stream owns
   its ordinal. Asking must not synthesize Lost or end/cancel a core brush. Older independently admitted opacity
   work can acknowledge while the modal loop pumps; its history must not be rolled back to simulate preservation.
2. Retain scalar document serial, host ordinal, stream opening and end-session generation. Do not retain a
   `PendingAt` reference across a prompt. Modal messages can mutate queues, finish the original stream or replace
   a document; reacquire and validate authority after every dialog. No authority can cancel a new core opening.
3. Cancel finishes only the requesting lifetime receipt and releases its input ownership. The original stream,
   settings and core owner can accept its later release. Do not clear the generic queue or cancel unrelated work.
4. Save requires output selection before settling the stream. Existing `HostStage::Output` precedes settlement;
   cancelling that picker must also preserve the pending edit. Refactor selection to accept an explicit owned
   request where needed; never temporarily overwrite the older selected host request to borrow its helper.
5. After valid Save/output acceptance, seal the exact accepted prefix and await its opacity/stroke acknowledgement.
   Write that resulting revision once, then run the originally admitted close/replacement without asking again.
6. Don't Save stops sampling and cancels only the matching owned brush. Await retirement before the dependent
   close/replacement publishes. A failed/stale lifetime request cannot dispose of another session's graph.
7. A detached accepted lifetime input retains its original ordinal and must participate in the existing earliest
   work selection. Younger history/host input cannot pass it. Re-enqueueing at the tail with a new ordinal is invalid.
   Explicitly bound/count any extra preflight slot and its input budget; keep the normal queue's capacity eight.
8. While a prompt is on the stack, repeated owner actions must neither open another prompt nor release its record
   beneath a borrowed reference. Record cancellation intent and finish after the presenter unwinds. End-session
   cancellation clears only the matching generation's block reason; critical end/shutdown closes producer admission
   before joining producers and destroying sinks.

## Detachment primitive

The prototype `DetachPending(documentSerial, ordinal)` removes only an exact pending entry. It moves at most
eight fixed slots, retains surviving FIFO order and the last accepted ordinal, and invokes no callback. Its
caller owns the detached request's receipt, budget and eventual retirement. A selected/taken entry is absent;
refusal changes no owner. Every pending reference expires on successful detachment. Removed admitted barriers
still create an ordinal gap, preventing preview/Wand coalescence across the earlier input.

Qualify independent expected ordinal/value sequences and move-only owner counts for empty/missing/zero/wrong
document refusal, front/middle/tail/single and wrapped/full removal, repeated removal, caller-held retirement,
unrelated owner preservation, later Clear, selected-entry exclusion, reused ordinal refusal and barrier gaps.
This primitive alone does not establish prompt/lifetime behavior or authorize early document snapshots.

The isolated draft archive is `p1-canvas-detach-draft-r1-qualified-isolated-draft` under
`%LocalAppData%\RedPrism\QualificationArchives`. All 10 inputs and 70 evidence copies were independently verified.
Completion SHA-256: `7E1E54D9FD6C9A4A73E5780909674D91196BCAD46294B52691B71CC723F11BAF`;
source manifest: `8193D0554D0B5603D903747B4279AB5BFDB5C3A6DAB425D7AC4FCE28166D8A24`;
evidence manifest: `D73C1B08A46D05D1E71421C7E7229AA8C8268821826CCD4FE9E71788C247D6FA`.
The actual ASan compile command contains `/fsanitize=address`. These are independent queue-draft runs, not
qualification of a native or application lifetime route.

## Private implementation under qualification

`%LocalAppData%\RedPrism\PrivateWork\p1-canvas-lifetime-proof` starts from 634 exact frozen boundary-ASan inputs.
It adds one bounded detached lifetime owner in addition to the normal eight queue slots, retaining that request's
input-budget ticket throughout the prompt, output picker and acknowledgement. Successful detachment preserves
the global admission watermark. Earliest-ordinal selection considers the detached entry before younger inputs;
completion rearms this owner even when the ordinary queue is empty.

The preflight runs before an older history prerequisite can disarm the source. It treats an unreplayed admitted
brush as unsaved work. Explicit request-based output selection avoids replacing the older active request. Cancel
and picker cancellation retire only the lifetime request; Save seals the prefix and Don't Save requests exact
opening cancellation. Publication waits for the original stream receipt. Failed/stale prerequisites suppress
the dependent action. Modal pointer loss is guarded during presentation, and cancellation/shutdown intent retains
the presenter-owned request until it unwinds. Shutdown retires fixed input values while leaving the core CPU
graph for the existing joined shutdown release path.

Primary review rejected the first cancellation-test draft: it expected the brush to be cancelled by the close
prompt and then opened a new stroke. The corrected independent expectation must retain the original pending
brush receipt/opening, then finish that same stroke on later Move/Up. No product expectation is weakened to fit
the incorrect draft. Full application runs, modal/reentrancy and shutdown scenario coverage remain required.

## Required application cases

Run each close project, New, window close and noncritical end-session route both before brush replay (held opacity
closure) and after the core brush opens. Cover Cancel, Save to an existing path, Save As success, Save picker
Cancel, Don't Save, failed write, construction/quota refusal and stale document/core/end-session authority.

During each prompt, independently inject older opacity acknowledgement, original Move/Up, capture loss, replacement
core/document, repeated close and end-session cancellation. Check exact receipt/error, original field/history,
captured pixel prefix, reopened saved document, and zero leftover input/worker/GPU row/event reservations.
Cancellation cases must keep the same pending owner available to continue; accepted cases must publish at most
once and retain ordinal order. Shut down during held, replayed and detached-preflight states, including failed
initialization. No worker can reach a destroyed HWND/chrome/document sink or leave a borrowed compute engine attached.

All original legacy expectations remain unchanged. Timing comes from measured owner-loop/ETW evidence; a large
assertion count or passing WARP suite is not a G1 latency result.
