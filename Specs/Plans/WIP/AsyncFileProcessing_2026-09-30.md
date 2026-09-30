# Asynchronous file processing (P1.7)

## Implementation checklist

- [x] F1 — Qualify one worker classification per owned host batch, cancellation acknowledgement and cached routing. Full configuration matrix passes; pending-IO fault injection and shared wait-pool migration remain F6.
- [x] F2 — Qualify cancellable banded WIC decode/encode without shared sparse materialization; preserve the existing EXIF/ICC policy and pixel goldens. Full configuration matrix passes; codec-call interruption, additional metadata fixtures and measured cancellation bounds remain F6.
- [ ] F3 — Worker project open and ordered image import, detached results and token-checked owner application.
  - [x] F3a — Cancellable project loads/imports with banded file/ZIP reads and no partial publication; independent project cancellation tests and full configuration matrix pass.
  - [x] F3a.1 — Remaining aggregate image/mask budgets reach both project decoders before pixel allocation; tiny real ZIP/folder later-asset refusals, exact bytes, zero/negative budgets and no partial publication pass all x64 suites and ARM64 cross-builds ([evidence](../../TestRuns/ProjectPixelBudgets_2026-10-01.md)). Whole-graph host admission remains F3b.
  - [ ] F3b — Admit and run detached open/import graphs; preserve batch/history order and validate host cancellation/staleness/failure.
    - [x] F3b.1 — Project manifest/PNG-header probe, adaptive decode ceilings, reopened-input refusals and resource declarations pass all x64 suites and ARM64 cross-builds; 69 tiny folder/ZIP and resource checks ([evidence](../../TestRuns/FileLoadPreparation_2026-10-01.md)). Host admission/reservation and independent allocation-floor controls remain F3b.3.
    - [x] F3b.2 — Detached import helper preserves first-image saved baseline, one additional entry, ignored new-canvas drop point and partial-success/cancellation policy; 31 checks and all configurations pass ([evidence](../../TestRuns/FileLoadPreparation_2026-10-01.md)).
    - [ ] F3b.3 — Wire both graphs under the selected host ordinal, preserve per-project batch continuation and formatted notices outside completion, qualify real cancellation/staleness/younger-input/device-independent lifetime and record open/import UI measurements.
      - [x] F3b.3a — Project-open probe/decode jobs, validated replacement continuation, 36 phase/nine batch gates, allocation floors and 22-row smoke pass all configurations. Hardware 4K/45 MP and WARP 4K measurements recorded; apply/frame overruns leave G1 open ([evidence](../../TestRuns/ProjectOpenJobs_2026-10-01.md)).
      - [ ] F3b.3b — Activate detached image-import worker and qualify saved baseline, one additional history entry, partial-success and ordered owner application.
- [ ] F4 — Worker save and asynchronous utility export graphs, whole-graph memory admission, per-path serialization, atomic destination publication and captured revision.
  - [x] F4a — Cancellable asset encoding/staged writes/libzip close preserve the external publication boundary; full configuration matrix passes.
  - [x] F4a.1 — Direct bounded PNG/JPEG file streams and staged ZIP asset-file sources remove whole-archive encoded-buffer retention. Three serial Release runs per mode at 4K/45 MP and the full matrix pass.
  - [ ] F4b — Admit and run owned project/save/export graphs with exact saved-revision and destination ordering tests.
    - [x] F4b.1 — Production project save worker owns document/path/revision and one whole-graph reservation; captured revision, publication/cancel acknowledgement and dependent action ordering pass the complete configuration matrix and real-loop measurements.
    - [x] F4b.2 — Asynchronous export readback/encoding graph; captured options/destination ordering, detached GPU lifetime and bounded additional-event recovery pass the complete configuration matrix and hardware/WARP measurements.
- [ ] F5 — Worker clipboard decode/encode, owned immediate OS transfers, event-backed bounded retry and copied New Canvas prefill.
- [ ] F6 — Faults, late cancellation, shutdown save coordination, complete validation and measured UI/apply/IO bounds; update master checklist.
  - [x] Qualify a bounded recovery policy for real hardware `DXGI_ERROR_WAS_STILL_DRAWING` after its GPU event: retain the original copy through one additional event and nonblocking Map, then fail locally if still busy. Full configuration gates, injected failures/later-tile refusal and 4K/45 MP hardware exports pass. The exact driver cause remains unknown; this policy bounds attempts, not driver delay.
  - [ ] Establish the cause of intermittent exploded save/rotation failures. Save-worker qualification preparation saw `save-durability.folder-first-save` and a later `project-cancel.publish-exploded` failure; twelve runs after adding failure-only stage/error diagnostics passed without reproducing either. Keep the original failures, distinguish immediate observer error values from cleanup LastError, and cover the final recovery/cleanup paths with injected faults.

Status: ACTIVE, executable P1.7 child; no completion claim.
Owner: `RedPrism/ApplicationFileJobs.cpp`, `ApplicationProjectSaveJobs.cpp`, file continuations,
`Common/PathClassificationWork.*`, `ProjectSaveWork.h`, codecs/project store and clipboard.

F1/F2 qualification: [FileFoundation_2026-09-30.md](../../TestRuns/FileFoundation_2026-09-30.md).
These bounded foundations leave F3–F6 and overall P1.7 active.

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

## Required evidence

Independent classification rules, bounded counted paths, no partial cancelled output, both stop registrations,
event-gated owner ordering and acknowledged cancellation. Retain all existing file/session/clipboard and codec
goldens. Add fault injection for pending IO and second-rename/flush failures where not yet covered. Run all three
x64 test configurations, all three ARM64 cross-builds, format and skill validation. Native ARM64 remains a separate
runtime qualification. Measure real application commands and slow/locked paths without timing assertions in
ordinary correctness tests. F1/F2 implementation alone does not close the file migration or G1.
