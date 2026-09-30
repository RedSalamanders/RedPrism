# Asynchronous file processing (P1.7)

## Implementation checklist

- [x] F1 — Qualify one worker classification per owned host batch, cancellation acknowledgement and cached routing. Full configuration matrix passes; pending-IO fault injection and shared wait-pool migration remain F6.
- [x] F2 — Qualify cancellable banded WIC decode/encode without shared sparse materialization; preserve the existing EXIF/ICC policy and pixel goldens. Full configuration matrix passes; codec-call interruption, additional metadata fixtures and measured cancellation bounds remain F6.
- [ ] F3 — Worker project open and ordered image import, detached results and token-checked owner application.
  - [x] F3a — Cancellable project loads/imports with banded file/ZIP reads and no partial publication; independent project cancellation tests and full configuration matrix pass.
  - [ ] F3b — Admit and run detached open/import graphs; preserve batch/history order and validate host cancellation/staleness/failure.
- [ ] F4 — Worker save and asynchronous utility export graphs, whole-graph memory admission, per-path serialization, atomic destination publication and captured revision.
  - [x] F4a — Cancellable asset encoding/staged writes/libzip close preserve the external publication boundary; full configuration matrix passes.
  - [x] F4a.1 — Direct bounded PNG/JPEG file streams and staged ZIP asset-file sources remove whole-archive encoded-buffer retention. Three serial Release runs per mode at 4K/45 MP and the full matrix pass.
  - [ ] F4b — Admit and run owned project/save/export graphs with exact saved-revision and destination ordering tests.
    - [ ] F4b.1 — Production project save worker owns document/path/revision and one whole-graph reservation; retain the input ordinal through publication/cancel acknowledgement, mark the published revision, and resume a dependent action only on success.
    - [ ] F4b.2 — Asynchronous export readback/encoding graph; prevent device restart after external publication and preserve captured options/destination ordering.
- [ ] F5 — Worker clipboard decode/encode, owned immediate OS transfers, event-backed bounded retry and copied New Canvas prefill.
- [ ] F6 — Faults, late cancellation, shutdown save coordination, complete validation and measured UI/apply/IO bounds; update master checklist.

Status: ACTIVE, executable P1.7 child; no completion claim.
Owner: `ApplicationFileJobs.cpp`, file continuations, `Common/PathClassificationWork.*`, codecs/project store and clipboard.

F1/F2 qualification: [FileFoundation_2026-09-30.md](../../TestRuns/FileFoundation_2026-09-30.md).
These bounded foundations leave F3–F6 and overall P1.7 active.

F3a/F4a/F4a.1 qualification: [ProjectStreams_2026-09-30.md](../../TestRuns/ProjectStreams_2026-09-30.md).
The completed helper/stream slice leaves production open/import/save/export graphs at F3b/F4b.

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

Save captures the immutable document, active id, chosen output path, format/options and exact saved revision
after settle. Mark only that revision saved after checked publication; never mark a later edit saved. Export
uses the asynchronous document readback graph, then worker encode/write. Queue destination work using a
collision-free path key. Use unique exclusive sibling temporary outputs, checked write/flush/close and rename;
cancel before publication leaves the destination intact. Define the publication boundary so a late cancellation
cannot report that an already committed external file was rolled back.

Retain an independent published flag in writer results. The scheduler may classify a job Cancelled after its
worker published successfully; owner completion still records the actual captured saved revision/path, while
cancellation suppresses a dependent replacement/close. Never infer external rollback from the generic job status.

## Required evidence

Independent classification rules, bounded counted paths, no partial cancelled output, both stop registrations,
event-gated owner ordering and acknowledged cancellation. Retain all existing file/session/clipboard and codec
goldens. Add fault injection for pending IO and second-rename/flush failures where not yet covered. Run all three
x64 test configurations, all three ARM64 cross-builds, format and skill validation. Native ARM64 remains a separate
runtime qualification. Measure real application commands and slow/locked paths without timing assertions in
ordinary correctness tests. F1/F2 implementation alone does not close the file migration or G1.
