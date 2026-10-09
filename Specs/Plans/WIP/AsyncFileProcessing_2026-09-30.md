# Asynchronous file processing (P1.7)

## Implementation checklist

- [x] F1 — Qualify one worker classification per owned host batch, cancellation acknowledgement and cached routing. Full configuration matrix passes; pending-IO fault injection and shared wait-pool migration remain F6.
- [x] F2 — Qualify cancellable banded WIC decode/encode without shared sparse materialization; preserve the existing EXIF/ICC policy and pixel goldens. Full configuration matrix passes; codec-call interruption, additional metadata fixtures and measured cancellation bounds remain F6.
- [x] F3 — Worker project open and ordered image import, detached results and token-checked owner application. Full configuration matrix and real-loop measurements pass; residual apply/frame targets remain F6/P1.9.
  - [x] F3a — Cancellable project loads/imports with banded file/ZIP reads and no partial publication; independent project cancellation tests and full configuration matrix pass.
  - [x] F3a.1 — Remaining aggregate image/mask budgets reach both project decoders before pixel allocation; tiny real ZIP/folder later-asset refusals, exact bytes, zero/negative budgets and no partial publication pass all x64 suites and ARM64 cross-builds ([evidence](../../TestRuns/ProjectPixelBudgets_2026-10-01.md)). Whole-graph host admission remains F3b.
  - [x] F3b — Admit and run detached open/import graphs; preserve batch/history order and validate host cancellation/staleness/failure.
    - [x] F3b.1 — Project manifest/PNG-header probe, adaptive decode ceilings, reopened-input refusals and resource declarations pass all x64 suites and ARM64 cross-builds; 69 tiny folder/ZIP and resource checks ([evidence](../../TestRuns/FileLoadPreparation_2026-10-01.md)). Host admission/reservation and independent allocation-floor controls remain F3b.3.
    - [x] F3b.2 — Detached import helper preserves first-image saved baseline, one additional entry, ignored new-canvas drop point and partial-success/cancellation policy; 31 checks and all configurations pass ([evidence](../../TestRuns/FileLoadPreparation_2026-10-01.md)).
    - [x] F3b.3 — Wire both graphs under the selected host ordinal, preserve per-project batch continuation and formatted notices outside completion, qualify real cancellation/staleness/younger-input/device-independent lifetime and record open/import UI measurements.
      - [x] F3b.3a — Project-open probe/decode jobs, validated replacement continuation, 36 phase/nine batch gates, allocation floors and 22-row smoke pass all configurations. Hardware 4K/45 MP and WARP 4K measurements recorded; apply/frame overruns leave G1 open ([evidence](../../TestRuns/ProjectOpenJobs_2026-10-01.md)).
      - [x] F3b.3b — Image-import probe/preparation/apply, 130 host cases and capped opened streams pass all configurations and 24-row hardware 4K/45 MP/100 MP and WARP 4K runs ([evidence](../../TestRuns/ImageImportJobs_2026-10-01.md)). Existing-layer apply reaches 11.403 ms at 45 MP; P1/G1 remain open.
- [ ] F4 — Worker save and asynchronous utility export graphs, whole-graph memory admission, per-path serialization, atomic destination publication and captured revision.
  - [x] F4a — Cancellable asset encoding/staged writes/libzip close preserve the external publication boundary; full configuration matrix passes.
  - [x] F4a.1 — Direct bounded PNG/JPEG file streams and staged ZIP asset-file sources remove whole-archive encoded-buffer retention. Three serial Release runs per mode at 4K/45 MP and the full matrix pass.
  - [ ] F4b — Admit and run owned project/save/export graphs with exact saved-revision and destination ordering tests.
    - [x] F4b.1 — Production project save worker owns document/path/revision and one whole-graph reservation; captured revision, publication/cancel acknowledgement and dependent action ordering pass the complete configuration matrix and real-loop measurements.
    - [x] F4b.2 — Asynchronous export readback/encoding graph; captured options/destination ordering, detached GPU lifetime and bounded additional-event recovery pass the complete configuration matrix and hardware/WARP measurements.
- [ ] F5 — Worker clipboard decode/encode, owned immediate OS transfers, event-backed bounded retry and copied New Canvas prefill.
  - [x] F5a — Cancellable DIB byte conversion and PNG metadata inspection; 42/11 independent literal checks and all configuration gates pass ([evidence](../../TestRuns/ClipboardAndRecovery_2026-10-01.md)). Production transfer/callers remain F5b–F5d.
  - [ ] F5b — Admitted worker-owned source snapshots/global blocks and immediate OS transfer with exact sequence/publication truth.
    - [x] F5b.1 — Sealed inspected source, checked owned byte copy, fully prepared movable PNG/DIB blocks and
      one-attempt private worker adapter qualify in all six configurations. 125 native controls prove
      post-window-destruction reads and exact empty/partial/complete publication truth
      ([evidence](../../TestRuns/CopyClipboardPreparation_2026-10-01.md)). Production activation stays F5b.2.
    - [ ] F5b.2 — Whole-graph admission, worker decode/paste/private-copy publication and actual ordered host
      activation. The adapter primitive alone does not qualify Copy/Cut, Paste or New Canvas.
      - [x] F5b.2a — Registered receipt, worker payload fingerprint binding, truthful receipt/partial publication
        and native origin controls. All six configurations qualify the 456 frozen native inputs: 25 receipt,
        45 fingerprint and 34 native binding controls plus the 125 existing transfer checks. See
        [receipt binding evidence](../../TestRuns/ClipboardReceiptBinding_2026-10-01.md).
        Production host callers remain F5b.2b work.
      - [ ] F5b.2b — Actual selected Copy/Cut/Paste graphs, complete preparation/encoding/decode/history/retirement
        admission and private-origin authority at production application. Keep F5b.2 open after primitive proof.
      - [x] F5b.2b.1 — Enforce memory encoder ceilings before stream growth, exact/over-cap output proof and
        full native qualification. All six configurations pass 160 stream/34 cap controls and all 103 existing
        codec checks ([qualification record](../../TestRuns/CappedMemoryEncoding_2026-10-01.md)); whole-graph
        host admission/callers and codec-internal headroom remain F5b.2b/F5d.
      - [x] F5b.2b.2 — Detached Paste core with complete candidate document/history and atomic guarded publication.
        Private proof passes 62 controls, including exact 100 MP aggregate equality, negative floor centering,
        group descendant insertion, serial/history staleness and cancellation/allocation refusal. Preserve all 128
        inputs and seven artifacts under `.build/p1-clipboard-paste-draft/private-proof-r3/`. Native integration is
        complete. The first matrix passes Release/Debug but exposes Windows conversion of a rejected DIB in ASan;
        its 468 physical inputs and 25 artifacts are preserved. Corrected producer-order inspection passes all
        x64 suites and ARM64 clean cross-builds in the separate r2 matrix: 62 Paste, 45 availability and 125
        unchanged transfer controls. Preserve 468 physical inputs and 45 evidence files.
        See [record](../../TestRuns/ClipboardPastePreparation_2026-10-01.md).
        Host admission and ordered OS transfer remain F5b.2b/F5c.2.
      - [x] F5b.2b.3 — Qualify selected production Copy/Cut/Paste callers with complete reservations, private origin,
        acknowledged retries and worker retirement. The private host compiles with event gates at capture,
        preparation, private application, encoding, transfer and Paste publication. The draft now includes Copy
        Merged's original-authority GPU/CPU/transfer graph, 52 host scenarios and 30 passing metadata-only readback
        budget controls. A separate encoder planner passes 37 controls and actual native object-size assertions.
        The final full private WARP run exits zero: 268 command, 56 prefill, 39 actual delayed-render and 81
        publication/no-paint/retry checks pass. Primary fixes protect aliased replacement IDs, sequence fixture
        arguments, six total Paste acquisitions and prepared chrome hit layout; exact failed images are retained.
        Native callers, declarations and tests pass all three x64 gates and all three ARM64 clean cross-builds
        against 480 frozen inputs ([record](../../TestRuns/ClipboardHost_2026-10-01.md)). Corrected native
        prefill/delayed/fault suites have 61/41/88 controls. F5d/G1 remain separate.
  - [ ] F5c — Event-backed retries, ordered host callers and guarded metadata-only New Canvas prefill.
    - [x] F5c.1 — Eight persistent one-shot event/deadline waits on the existing scheduler wait pool; cancellation
      wakes the same wait, and no slot or receiver can be reused before callback acknowledgement. Native host
      routing multiplexes this source with GPU work. All six configurations, 117 retry controls and two routed
      GPU scenarios qualify ([record](../../TestRuns/RetryWaitRouting_2026-10-01.md)); actual transfers remain F5c.2.
    - [x] F5c.2 — Bind transfer retries to actual ordered Copy/Cut/Paste/New Canvas graphs and their full
      reservation; preserve six total attempts, source authority, publication truth and draft revision guards.
      New Canvas's private draft opens immediately and inspects only clipboard metadata on a worker. Card
      invocation, typed-width/height intent, session and document lifetime guard publication; close/reopen and
      copied Apply drafts cancel older suggestions. The original ticket/reservation survives acknowledged Busy
      waits. The 14 private prefill and seven real delayed-render cases pass; native integration is complete
      and all six frozen native profiles qualify. Native waits retain the source even during cancelled external render.
  - [ ] F5d — Full contention/delayed-render/cancel/shutdown gates and real-loop measurements; retire production UI codec/transfer callers.
- [ ] F6 — Faults, late cancellation, shutdown save coordination, complete validation and measured UI/apply/IO bounds; update master checklist.
  - [x] F6a.4 — Bounded native staging-publication recovery with truthful cancellation/publication and rollback:
    at most three attempts after native error 5/32, 25/100 ms event waits, separate timer resource faults and final
    injected rename errors. Independent real staging-child/directory readers cover first saves and replacements.
    Combined r6 failed the initial destination-retry fixture before fault installation; its exact 577 inputs and
    294 evidence entries are archived. Focused Release r4 passes 269 publication controls and the complete I/O suite;
    fresh r9 full-matrix qualification passes all six profiles
    ([record](../../TestRuns/P1LifecycleShutdown_2026-10-02.md)). Unexpected destination races retain exact foreign bytes and the old
    backup after rollback refusal. External cause, IO-call interruption and measured cancellation limits remain open.
  - [x] F6a.2 — Timer creation/arming/wait resource faults preserve native rollback errors, backup and publication
    truth; healthy/final-injected rollback paths skip timer hooks. All configurations and 40 rotation checks pass
    after correcting the directory fixture to lock the directory itself ([evidence](../../TestRuns/LiftedPixelsPreparation_2026-10-01.md)).
  - [x] F6a.1 — Qualify bounded native rollback recovery: three attempts, 25/100 ms event waits, exact native errors,
    late cancellation and complete backup retention. Real child-reader controls reproduce error 5 and exercise
    release/exhaustion without timing assertions. ASan reproduced the prior immediate rollback failure; an
    independent later diagnostic rename succeeded and no project-tree handles remained in the test process.
    All x64 suites and ARM64 cross-builds pass, with 33 rotation checks ([evidence](../../TestRuns/RasterEdits_2026-10-01.md)).
    The responsible external reader/filter and original first-save cause remain unproven. Timer resource faults
    are tracked at F6a.2; original failures are retained.
  - [x] F6a — Directory rotation/rollback/cleanup faults, backup-only retry recovery and truthful publication/errors; 24 injected/real filesystem checks and all configurations pass ([evidence](../../TestRuns/ClipboardAndRecovery_2026-10-01.md)). Cleanup allocates its bounded scratch before staging and never follows reparse targets. Intermittent real save cause, flush/close faults and overall F6 remain open.
  - [x] Qualify a bounded recovery policy for real hardware `DXGI_ERROR_WAS_STILL_DRAWING` after its GPU event: retain the original copy through one additional event and nonblocking Map, then fail locally if still busy. Full configuration gates, injected failures/later-tile refusal and 4K/45 MP hardware exports pass. The exact driver cause remains unknown; this policy bounds attempts, not driver delay.
  - [ ] Establish the cause of intermittent exploded save/rotation failures. Save-worker qualification preparation saw `save-durability.folder-first-save` and a later `project-cancel.publish-exploded` failure; twelve runs after adding failure-only stage/error diagnostics passed without reproducing either. Keep the original failures, distinguish immediate observer error values from cleanup LastError, and cover the final recovery/cleanup paths with injected faults.

    Selection r2 qualification passes Debug/Release, then ASan fails one `destination-retry.baseline` initial
    fixture save. Its complete 423-input/image/log snapshot is preserved under
    `.build/p1-selection-r2-ASan-baseline-failure/`. This happens before the fault-injected replacement and is
    not evidence of a failed native retry. The fixture now retains 32 immediate stage events and prints mode,
    status and those values only on failure. Production policy and the exact assertion remain unchanged;
    the original cause and F6 stay open.

    Sizing's first frozen ASan run also fails the real host folder save-in-place case after its initial fixture
    write/open succeed. Preserve `.build/p1-size-history-first-ASan-failure*` and its executable/source hashes.
    A fixed-capacity trace-only save gate observes the actual asynchronous write rather than only the initial
    synchronous fixture, and reports receipt/IO/reload status and stage-native values on failure. The rerun passes
    this scenario without identifying its original cause; F6 stays open and failed evidence is retained.

    Merge's r3 matrix reproduces `rotation-fault.timer-create-failure-keeps-native-child-lock-error-and-backup`
    in Debug IoTests. Preserve `.build/p1-merge-history-r3-Debug-rotation-failure/` and its exact image/freeze/logs.
    The recovery policy and assertion remain unchanged; failure-only diagnostics now report each status, captured
    reader-open error, fault hook, stage-native value and destination/backup/staging fact before a focused rerun.
    All 12 focused full I/O runs pass without reproducing that predicate. Its cause remains open; the distinct
    r4 matrix stops on an unknown nested UI-smoke exit code. The r5 matrix passes all three x64 gates and ARM64
    cross-builds after process-handle retention. These results do not explain or retroactively
    qualify the failed r3 image.

Status: ACTIVE, executable P1.7 child; no completion claim.
Owner: `RedPrism/ApplicationFileJobs.cpp`, `ApplicationProjectSaveJobs.cpp`, file continuations,
`Common/PathClassificationWork.*`, `ProjectSaveWork.h`, codecs/project store and clipboard.

F1/F2 qualification: [FileFoundation_2026-09-30.md](../../TestRuns/FileFoundation_2026-09-30.md).
These bounded foundations leave F4–F6 and overall P1.7 active.

F3a/F4a/F4a.1 qualification: [ProjectStreams_2026-09-30.md](../../TestRuns/ProjectStreams_2026-09-30.md).
The completed helper/stream slice leaves production open/import at F3b. Project-save and utility-export workers
are qualified at F4b.1/F4b.2 ([ProjectWriters_2026-10-01.md](../../TestRuns/ProjectWriters_2026-10-01.md)); F4, F6,
overall P1.7 and G1 remain open for IO faults, remaining callers and measured scope overruns.

## Ownership and ordering

The selected host request retains its ordinal, receipt and input reservation until every worker acknowledges and
the owner applies/drops the result. Each worker independently owns immutable paths/options; shutdown may retire
the selected host input before joining jobs. Admission accounts for any additional path copy. Work never borrows
`DocumentSession`, a window, chrome or GPU state. Owner completion posts notices or rearms an owned UI action;
it does not open a modal, wait for work or recursively drain the scheduler.

Classify an Open batch once before deciding project/image routing. Preserve directories, case-insensitive `.prism`
and the three ZIP signatures; preserve existing best-effort image classification for unreadable/short headers.
Explicit Images As Project/Layer input retains its explicit action. Empty host entries remain skipped. Invalid
helper input publishes no partial batch. Classification is nonmutating and does not hold a history transaction,
but its active host request prevents younger mutations from starting. An externally replaced document makes its
completion stale. Escape retains the active request until cancellation acknowledgement, including late Finished.

## IO cancellation limits

Header reads use an owned overlapped operation. Register local and shutdown stop callbacks after issue, so an
already-requested stop signals the owned cancellation event. The worker cancels the exact pending read via
`CancelIoEx`; the stop callback performs only `SetEvent`, including when cancellation is requested on the UI thread.
Retain handles, event, buffer, callbacks and
`OVERLAPPED` until the operation acknowledges. Attribute lookup and file open are synchronous OS calls on the
worker; the initial helper's acknowledged header wait also occupies a compute lane. Measure these limits and
replace waits with the shared event pool before F6/overall P1.7 closes; no UI-thread IO is permitted by that exit.

WIC band checkpoints cannot cancel a codec COM call already in flight. Neither a stop request nor a returned
partial decoder result authorizes publishing pixels. Record measured stop-to-return distributions separately
from structural band bounds. Preserve imported orientation and ICC policy and PNG/JPEG numeric expectations.

### F6 shared native-read continuation — implementation sequence (2026-10-09)

Implementation status (2026-10-09): The shared native-read checkpoint is qualified on freeze `1125320A...2FA421E` (1,093 inputs): focused Debug556/Host557/actual WARP App558, full x64 Release560/Debug561/real-ASan562, Release analysis559 and ARM64 r63 Debug/Release/ASan cross-builds. Main verifies exact archived copies/receipts, 44 primitive/26 transfer/144 host controls, 121 stages and all predecessors. Both ASan stress executions and the separately preserved actual probe pass. Preserved checker refusals change no native expectations. Skills15/format964/diff-check pass; native ARM64 runtime is unavailable. Identity exhaustion, complete phase peaks, writer/session-end and measured G1 remain open. [Evidence](../../TestRuns/P1NativePathRead_2026-10-09.md).

Every read, including immediate completion and cancellation before issue, consumes its original registered callback before event reuse. Readiness cannot resume a running issue worker; Await arms only after joined return. A stop request invokes actual worker CancelIoEx without faking hEvent. GetOverlappedResult acknowledges before reuse/free, with reset/recheck for premature readiness. Shutdown joins readiness callbacks, detaches borrowed wrappers while retaining the event, and closes it after final worker native acknowledgement and scheduler Close. The ninth route preserves fair rotation and explicit nine-source/tenth-refusal controls.

The fixed native inventories are eight primitive cases/44 controls, 26 transfer controls and nine hidden-host scenarios plus three startup prefixes/144 controls. The early-ready case holds the actual issue worker until a readiness callback occurs before Await arms; only subsequent actual native acknowledgement can satisfy its controls. Other cases cover three read cycles, premature signal, cancel, replacement, late cancel, shutdown and premature-signal cancellation/shutdown. A separate job on the same one-worker pool supplies the pending read bytes. The independently reviewed output checker passes fourteen explicitly synthetic controls; those are checker tests only. All existing host classification and predecessor gates remain required. Unbudgeted fabricated path roots refuse before capture; the owner retains original receipt/input budget while its unchanged vector swaps into and back from the private worker payload. Large input refusal retirement, complete phase bounds, OS stop-to-return, writer durability/session-end faults and F5d/G1 remain open.


- [x] Create one stable Application-owned header-read event/source at processing startup. Register its wait
  with JobScheduler outside callbacks; RegisterEventWait/RemoveEventWait are startup/teardown APIs and cannot
  be called from a job completion. The callback only publishes readiness and wakes the owner. Rearm the
  existing registration for the exact suspended original classification job; never create a new job per file.
- [x] Split worker attribute/open/issue from pending completion. A pending ReadFile yields its compute lane.
  Keep file, four-byte buffer, OVERLAPPED, path scratch and exact original ticket/ordinal alive. On readiness,
  resume the original stage and use GetOverlappedResult without waiting. Incomplete/old readiness yields back
  to the same event route. Synchronous attribute/open calls remain private worker calls with measured limits.
- [x] Preserve cancellation authority on skipped worker stages: the retirement worker requests CancelIoEx
  for the exact operation; a stop request never acknowledges it. Pending cancellation keeps the same event,
  inputs and reservation until actual native completion, then retires the whole private payload on a worker.
  Close joins event callbacks before workers. Its final worker cleanup must cancel/acknowledge any remaining
  native operation before releasing its file/event/buffer; report uninterruptible OS stop-to-return separately.
- [x] Remove the current owner path-vector copy before Submit. HostInputBudget caps aggregate input storage
  at 1 MiB. Admit complete cached path-storage/scratch/metadata bounds first, allocate the private payload on
  the original worker, then transfer the selected request's vector in constant time at the exact joined owner
  callback. Keep the host receipt/reservation on the owner; its owner-affine budget ticket must never be freed
  by the worker. Restore paths to that same selected request after successful classification so its existing
  ordered project/image continuation works. Failure/Close retains any transferred paths through worker release.
- [x] Add real pending-operation fixtures (an admitted test file source may supply a real overlapped pipe),
  plus immediate file success/short header/API refusal, stop-before-issue, pending cancel, already-completed
  cancel, late signal, repeated rearm, replacement, original FIFO, hidden shutdown and joined Close. Require
  actual GetOverlappedResult acknowledgement; synthetic readiness is never completion proof. Keep existing
  .prism-extension/directory/ZIP-signature/empty-host-path ordering semantics and independent literal bytes.
- [x] Qualify this 1,093-input source checkpoint in all full x64 profiles, Release analysis and all three ARM64 cross-builds. Actual results and independent inventories are recorded above; this does not close the remaining phase/stop-to-return/G1 requirements or the later exhaustion repair.
- [ ] Qualify every current-source profile, exact native resource/retirement bounds and the F6 stop-to-return
  measurements before activation/closure. The existing blocking helper may remain a reference test surface;
  ordinary classification must use the shared continuation. Writer flush/close/rename/cleanup failures and
  F5d/G1 remain separate open gates, including the preserved unexplained exploded-save failures.
- [ ] Close native-read identity exhaustion without losing a pending OS owner. The qualified predecessor increments
  for a new issue and for rearm after premature readiness; at the final identity, the latter refuses and the
  pending-read host branch terminates. The integrated follow-up advances only a new native operation; its
  same-ticket rearm retains the final identity. Focused Debug563/Host564/App565 qualify 20/34
  additions and all predecessors. Analysis566 fails the fixture's error-type comparison and is preserved;
  main casts only that comparison, with unchanged expected error bits. Current-source qualification is due. A rearm must retain the same native operation identity and exact original
  ticket, leaving the event unreset. Only a new operation consumes an identity and may refuse before issue.
  Prove real last-identity premature readiness, actual incomplete/native acknowledgements, same-pool progress,
  next-issue refusal, truthful original error/younger continuation and complete worker retirement. Also prove an
  initially exhausted source and zero/foreign-ticket rearm refusal. The main-reviewed candidate and 20-control Host / 34-control actual Application
  proofs are integrated after committing/pushing c8e253f. Main corrected the agent's API-seam misunderstanding,
  startup-callback race, atomic observation, forbidden setup catch and 19-to-20 inventory count. No unrun
  qualification is claimed. This finding remains open even if those profiles pass.

## Preparation and application

For project open, the worker loads and validates a detached document, active id and format version. On current
completion, one owner replacement resets session/history/mask target, uses the captured path, and follows Fit.
Failure leaves the existing document intact; the batch continues its remaining project paths in original order.

For image import, snapshot only after earlier work settles. The first successful image establishes a new canvas;
failed inputs before it do not prevent later inputs. Additional successful inputs form one named import history
entry, preserving placement, grouping, active id, reported batch failure and the existing partial-success policy.
Cancellation/staleness publishes no partial prepared document. Prepare both the initial new document and the
additional import result off-thread so the owner need not perform any per-image decoding or thumbnail work.

### F3b.3b execution detail

Execution checklist (2026-10-01; bounded slice qualified):

- [x] Implement opened-stream decode caps, signed/hard pixel guards and metadata-only inspection; add independent codec tests.
- [x] Implement enforced per-path/aggregate preparation ceilings and whole-graph memory plans; add detached batch tests.
- [x] Implement selected host import probe/preparation/apply ordering, cancellation/staleness/failure gates and mixed batches. Capture occurs only after reservation admission.
- [x] Qualify import benchmark rows, full configurations and measurements; commit/push this slice with its evidence.

All x64 suites and ARM64 cross-builds passed, including 130 host import cases in each x64 configuration. All four
24-row runs passed; the 100 MP run additionally exercises exact pixel-budget refusal. The
[import record](../../TestRuns/ImageImportJobs_2026-10-01.md) retains frozen-source identities and measured overruns.

Qualification corrections: the first Debug run passed I/O but failed a new host placement expectation. The legacy
`EditorSession.swift:656–657` floors centered placement; the new literal expectations were corrected without
changing production placement or existing goldens. Original failure logs remain under `.build/p1-image-import-original-placement-*`.
The next host run caught a new device assertion storing an HRESULT in a Boolean (`S_OK` became false). The control
now retains the HRESULT and checks `SUCCEEDED`; its original log is `.build/p1-image-import-original-device-*`.
The direct host run then found a real continuation bug: a completed project left `FileComplete` as the next
prerequisite stage, so a following image group was skipped. `PathImages` now selects `Execute` before settling;
the mixed-project/image test requires the image job, imported pixels and one additional history entry. The original
failed control is `.build/p1-image-import-original-mixed-selftest.err.log`.

Use a metadata inspection job followed by separately admitted bounded preparation. Inspect frame zero on an owned
WIC stream, retain standard per-path status/pixel/encoded-length values, and never call CopyPixels or create a
Raster in the inspection helper. Orientation may swap sides but preserves the pixel count used for admission.
PNG/JPEG/TIFF/HEIF policy remains the existing WIC import policy; RAW corpus decoding is P4. Decoder metadata
calls can block internally on a worker and are not claimed interruptible. Inspect and cap the opened source's
encoded length, rather than assuming a separate pathname stat describes the decoder's stream.

Before preparation, declare the additional output, thumbnail, owned path, document record/name/curve and temporary
working-copy peak. Shared source rasters/selections already resident are not charged twice. Per-path metadata and
aggregate added-pixel ceilings must reach the reopened decoder before output allocation; changed input can only
succeed within the declared limits. Cache failed inspection statuses in their original order. Refusal never
submits an under-reserved preparation job. Keep probe and preparation handles/codecs confined to their respective
workers. Both reservation charges may coexist at submission; FIFO prevents preparation before probe retirement.
The decoder boundary must explicitly reject negative budgets and enforce the hard 100 M per-image limit even
when a caller supplies a larger count. Current `CopyFrame` casts the signed remaining count to unsigned; the
import slice must correct that gap rather than preserving it in the new probe's tests. Valid current callers and
legacy pixel goldens remain unchanged. Opened-stream length caps must also be enforced at decode, not by checking
a path before a separate filename reopen.

Only a current serial/generation/ordinal/acquisition may apply. Prepare the new saved baseline and optional
additional import document together; cancellation/staleness/allocation failure applies neither. A completed
ordinary partial-success batch keeps its successful images and reports its failure, preserving the legacy policy.
An existing document gets one Import Image(s) entry; a new canvas first becomes a saved baseline and additional
images get one entry. Validate the complete prepared pair before owner replacement; a baseline replacement must
use the explicit same-input lineage continuation rule. Existing-document younger Undo may run after import;
younger input captured on a replaced document must be stale. Keep the first batch error across mixed project/image
paths, post one localized import summary, and retire all ownership only after apply/drop acknowledgement.

Qualify no-document/new/existing/grouped targets, placement and ignored new-canvas drop points, first and later
failures, baseline plus one additional history entry/Undo, copied paths, after-probe source growth, exact pixel
ceilings, complete pair publication, probe/preparation cancellation and actual allocation/admission failures,
generation changes and reused persisted IDs, mixed batches, and hidden/device-independent completion. Add actual
image-import benchmark rows to the host loop without timed synchronous file checks. Full configurations and
measured UI/apply costs remain the exit gate; this plan section is not implementation evidence.

The production project-save worker captures the immutable settled document, active id, resolved output path and
exact saved revision. It marks only that revision after checked publication; it never marks a later edit saved.
Utility export uses asynchronous document readback followed by worker encode/write under the selected global
host ordinal, unique exclusive sibling temporary outputs and checked write/flush/close/replacement. The last
GPU mapping and utility targets are released before the detached CPU-only encoding stage. After that boundary,
device changes cannot repeat encoding or external publication. Cancellation before publication preserves the
destination; late cancellation records the independent publication fact and suppresses dependent actions.

The production project-save worker now owns the settled document snapshot (with selection removed), active id,
resolved path and revision, and submits one complete memory reservation with its Io job. The reservation includes
the worker and resolved path, fixed 16 MiB manifest/JSON/codec allowance, per-layer/per-asset metadata, one
largest-asset frame allowance, and path-derived source/staging/member names. It refuses invalid limits or overflow
before submission, does not multiply the frame allowance per asset, and does not materialize sparse inputs.
Its host request keeps the input ordinal, receipt and input charge until acknowledged completion; current lineage
records the published path and captured revision, while stale lineage cannot update either. A published file is
recorded even if cancellation changes the generic job result, but a dependent close/replacement proceeds only on
success. Project opening uses the two-job probe/decode graph described by `IO_ProjectFormat.md`; image import
and clipboard file processing remain synchronous until their later slices.

Retain an independent published flag in writer results. The scheduler may classify a job Cancelled after its
worker published successfully; owner completion still records the actual captured saved revision/path, while
cancellation suppresses a dependent replacement/close. Never infer external rollback from the generic job status.

F4b.1 implementation evidence is the thirteen-case `project-save-host.case=*` matrix: success, younger
mutation ordering, cancellation before publication, cancellation after publication, cancelled close after
publication, returned failure, actual allocation failure, stale document lineage, and a post-publication later
edit, pending transform settlement, pending typed-opacity settlement, owning admission allocation failure and
resolved path refusal before Run when nested staging/codec suffixes would exceed the owned buffer.
The worker cases use owned events; admission fails before Run. `project-save-memory.*` covers reservation bounds,
limits, shared and sparse raster accounting. The 21-row
UI smoke now includes `project-save`, `png-export` and `jpeg-export`. F4b.1/F4b.2 gates and 21-row hardware
4K/45 MP and WARP 4K measurements pass; keep F4b/F4/P1.7 open for the remaining fault/IO qualification.

F4b.2 adds nineteen `export-readback-memory.*` arithmetic/metadata/sparse-source checks and fifteen
`export-host.case=*` scenarios: PNG/JPEG literal output, younger edit ordering, cancellation before/after
publication, returned failure, worker/admission allocation failure, stale lineage, device changes before encoding
and after publication, captured encoder options, output-pixel refusal before any readback allocation, and
resolved path refusal before Run when the exclusive codec sibling suffix would exceed the owned buffer.
The fifteenth case fails the second readback tile persistently after one tile copied and requires no encode visit,
no published output, one exact failure receipt/notice and unchanged document/history.
Worker gates require zero lent GPU mappings and zero utility-surface bytes before encoding; recovery cases
require one encode attempt. All current failure cases require exactly one localized notice. These tests need
full configuration qualification and real-loop measurements, now recorded in the writer evidence above.

### F5 execution detail

Implementation checklist (start here; each qualified slice is committed and pushed):

- [x] F5a — Separate cancellable DIB byte conversion and PNG metadata inspection from Win32 transfer; 53 literal byte/metadata controls and all configurations pass ([evidence](../../TestRuns/ClipboardAndRecovery_2026-10-01.md)).
- [ ] F5b — Own prepared global blocks, inspected source limits and immediate OS transfer on workers; preserve PNG-first fallback, sequence truth, private-copy preference and acknowledged cancellation.
- [ ] F5c — Add bounded event-backed clipboard retries and ordered Copy/Cut/Paste continuations, plus metadata-only New Canvas prefill guarded against typing, replacement and clipboard changes.
- [ ] F5d — Qualify publication/cancellation/late failure, delayed rendering, contention, shutdown and full real-loop measurements; retire production UI-thread codec/clipboard access before closing F5.

F5a preserves the current integer BGRA premultiplication rules and accepted DIB layouts. Parse and serialize
explicit little-endian bytes, check offsets before pointer arithmetic and enforce 30,000-side, 100 M pixel and
512 MiB encoded limits independently of caller ceilings. Decode into an adopted PixelBuffer. Encode reads
bounded rows privately from sparse rasters; no shared Data() assembly and no additional full-source buffer.
Metadata probes must neither CopyPixels nor create thumbnails. Failed/cancelled outputs are empty. Independent
literal DIB and PNG fixtures cover top/bottom rows, padding, alpha, masks, malformed sizes and exact budget edges.

F5b must inspect before large allocation and reserve the complete copy/decode/paste graph. Reopened clipboard
input must match the inspected sequence and fit inspected encoded/pixel limits before copying or allocating;
failed PNG can fall back only to a separately inspected DIB. Never retain borrowed clipboard handles/locked
pointers after CloseClipboard. Worker-owned resources survive cancellation until the OS call returns.
Outgoing global blocks are prepared before EmptyClipboard; SetClipboardData transfers only successful blocks.
Record whether the clipboard was emptied, which formats were placed and its sequence while held. Late
cancellation cannot undo or repeat a published external change. An outgoing snapshot checks the source sequence
before EmptyClipboard so encoding cannot overwrite a later foreign copy. The session's private copy remains
usable when mirroring fails; remember the sequence at the private copy, not a later unrelated sequence.

The qualified F5b.1 adapter passes 125 native Win32 checks: literal PNG/DIB bytes, independent straight
versus premultiplied alpha, post-owner-destruction reads, reached allocation/native faults, complete/partial/empty
publication, early/late cancellation, sealed stale retries, independent DIB fallback and an externally gated
delayed renderer. All x64 suites and ARM64 clean cross-builds pass
([evidence](../../TestRuns/CopyClipboardPreparation_2026-10-01.md)); production callers remain open.
One measured control records held sequence 16016 and later-open sequence 16018 without a foreign replacement.
Do not assume closing/destroying an immediate owner preserves the held value. The result records the exact
sequence after the last placement while open, independently observed at that stage; a later sequence is not
proof that a foreign copy occurred or authority to relabel our old copy. F5b.2 must explicitly resolve this
post-close authority for private-origin Paste, including a foreign replacement between close and any later
observation, before host activation. Native test waits are bounded test-only readiness controls. The shared
event/deadline framework qualifies at F5c.1; actual clipboard retry jobs remain F5c.2.

F5b.2 implementation sequence (qualified primitives and remaining host work):

- [x] Seal private Copy/Cut pixels/history authority with a separate session copy serial; direct successful
  copies also expire older workers, and a failed Cut restores both prior pixels and serial.
  A5.4b passes all six configurations and 92 independent controls
  ([record](../../TestRuns/ClipboardCommandPublication_2026-10-01.md)); production callers remain F5b.2b.
- [x] Add an immediate registered receipt format binding the process/session and successful private copy serial.
  Prepare its small movable block before Empty and place it first. Receipt placement failure places no image
  format and reports exact emptied failure; after a successful receipt, still attempt PNG and DIB despite late stop.
  Use a registered format, not CF_PRIVATEFIRST (whose handles Windows does not automatically free).
- [x] Bind receipt authority to local immutable PNG/DIB sizes and SHA-256 fingerprints produced on the encoding
  worker. The receipt alone is insufficient: an owned incoming payload must match the bound bytes before using
  the private copy/origin. Copying back the identical receipt and identical payload retains that content's origin;
  replacing any readable image format, retaining an old receipt with newer bytes, or changing the private serial
  refuses preference. No bare HGLOBAL address or later fresh sequence is authority.
- [x] Inspect/copy receipt and bytes at the exact sealed source sequence, with all borrowed pointers confined to
  the open scope. Digest work runs on bounded cancellable worker chunks. PNG-first/DIB fallback remains independent
  from receipt preference; absent/invalid receipts do not prevent foreign image decode.
- [ ] Retain captured-sequence private preference on a pre-Empty mirror failure, as the current host does. Never
  relabel a later sequence as ours. No-image foreign clipboard still permits the existing internal Paste fallback;
  an image-format decode failure retains the current no-private-fallback rule.
- [ ] Prove complete/partial/empty receipt publication, native receipt errors, receipt destruction survival,
  payload tampering, stale serials, identical copy-back, failed mirroring and foreign replacement between close
  and owner completion. Activate real callers only after these and whole-graph admission qualify.

The receipt design follows the distinction between registered/private formats and immediate/synthesized contents
in [Microsoft's clipboard format contract](https://learn.microsoft.com/en-us/windows/win32/dataxchg/clipboard-formats).
The [sequence contract](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getclipboardsequencenumber)
tracks content/empty changes and delayed rendering; it does not establish content ownership. Existing F5b.1
sequence and pixel controls remain unchanged. This design adds a content binding, not a security authentication
claim, and its encoding/digest/owned-byte memory and elapsed cost must be included in F5d measurements.

Primary review refinement: fingerprint the complete exposed movable block, including deterministic zeroed
padding if Windows exposes more bytes than requested. Incoming owned copies include that full block, so hashing
only the encoder vector length could refuse a legitimate private-origin copy. Keep the SHA hash object's fixed
4096-byte storage on its worker stack, check the provider's required object length before creation, and cancel
between 64 KiB bands. No owner-side payload hashing is allowed. The private prototype's first compile failure
(missing WIL prerequisite header and a sidecar's nonexistent CancelSource method) is byte-preserved; the corrected
prototype passes. Positive receipt controls now discriminate malformed receipt refusal, and native tamper controls
change exactly one byte of the original encoded DIB while keeping its header and matching PNG unchanged.

Qualified F5b.2b.1 replaces the growable encoder memory destination with a stream enforcing a derived/lower caller cap
before growth. Its 160 stream/34 codec controls and 103 unchanged codec checks pass in all three x64 suites;
all ARM64 clean cross-builds pass against the unchanged 462-input freeze. This still leaves simultaneous stream/vector/global/source/history
peaks to F5b.2b whole-graph admission. Opaque codec allocations and digest throughput remain measured headroom
and F5d/G1 work; do not credit the sink cap as complete host admission.

The master table's short synchronous owner publication is insufficient as a guarantee: EmptyClipboard sends
WM_DESTROYCLIPBOARD to the preceding owner and GetClipboardData can trigger delayed rendering. Execute these
calls on a worker inside the private Win32 clipboard adapter. Reading can use a null owner. Publishing uses an
adapter-owned message-only window on that worker; never borrow the Application HWND or call session/chrome.
Use already rendered PNG/DIB global blocks, never delayed rendering. Verify that destruction of the immediate
format owner retains readable contents and sequence truth. This is an explicit private platform-adapter
exception to the generic job prohibition on window access, not permission to call application UI from Run.
Update the owning architecture/performance/clipboard contracts and the master table with F5b implementation.

Retry failures without occupying a compute callback or sleeping on the owner. The F5c.1 prototype uses a
relative one-shot deadline on the existing thread-pool event wait, whose event can also signal cancellation.
This avoids replacing a timer after its old callback has queued. It is an implementation refinement of the
one-shot timer requirement, with the same 50/100/200/400/800 ms delays and no separate timer pool. See
[SetThreadpoolWait](https://learn.microsoft.com/en-us/windows/win32/api/threadpoolapiset/nf-threadpoolapiset-setthreadpoolwait).
A bounded owner work source publishes readiness and resumes the same suspended
job/ticket/reservation. Five retries follow the initial attempt at 50, 100, 200, 400 and 800 ms; six total attempts,
no polling or recurring timer. Cancellation signals the existing event immediately and retains the suspended
job until the callback acknowledges; signal resource failure retains its error and owner until the original
deadline acknowledges, with no failed-signal polling. Rearm/reuse is allowed only after the prior callback has
published and will no longer touch request metadata. Unregister/join wait callbacks before
destroying the timer or job. The Application owner pump can multiplex GPU and retry sources while preserving
the scheduler's single borrowed owner-work-source contract. Shutdown stops timer producers before closing workers.

The selected Copy/Cut/Paste request retains its history ordinal until its admitted stages acknowledge.
Clipboard replacement has a separate copy stamp so an older encode cannot publish over a newer private copy.
Cut's private copy and clear remain atomic under the existing numeric golden; selection rendering, clear and
Copy Merged readback migrate in P1.5/P1.3 rather than being called synchronously as part of F5's final exit.
Foreign Paste prepares the inserted layer/document privately and validates the settled serial/generation before
one owner history entry. Internal Paste preserves the stored origin; foreign Paste uses floored centering.
New Canvas opens immediately at the last size. Its probe result applies only to the same card invocation,
unedited draft revision and clipboard sequence; any Width/Height edit, even back to the previous value, wins.

Keep ordinary correctness tests event gated, not latency assertions. Separate measured codec row checkpoints,
clipboard retry deadlines and stop-to-return distributions from any claim about an in-flight OS/WIC call.
An external delayed renderer can block that worker; it must never block the interactive application thread.
F6 still owns measured slow/hung-source and hidden shutdown qualification. No G1 claim follows a helper alone.

Primary platform references: [OpenClipboard](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-openclipboard),
[Clipboard operations](https://learn.microsoft.com/en-us/windows/win32/dataxchg/clipboard-operations),
[SetClipboardData](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setclipboarddata).


## Required evidence

Independent classification rules, bounded counted paths, no partial cancelled output, both stop registrations,
event-gated owner ordering and acknowledged cancellation. Retain all existing file/session/clipboard and codec
goldens. Add fault injection for pending IO and second-rename/flush failures where not yet covered. Run all three
x64 test configurations, all three ARM64 cross-builds, format and skill validation. Native ARM64 remains a separate
runtime qualification. Measure real application commands and slow/locked paths without timing assertions in
ordinary correctness tests. F1/F2 implementation alone does not close the file migration or G1.
