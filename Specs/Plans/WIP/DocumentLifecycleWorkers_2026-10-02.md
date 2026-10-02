# Document lifecycle workers (P1.9 H1)

## Implementation checklist

- [x] N1 — Prepare optional New/Close document/history publication primitives; all six native profiles qualify.
  - [x] Private primitive and independent state/cancellation/fault controls: complete DocumentTests passes
    65 lifecycle controls against 542 frozen private inputs. Sources, exact image/symbols and results are
    archived in `.build/p1-host-draft/lifecycle-core-qualified-r2/` on the qualification archive volume.
    Native qualification passes the same 65 controls in every x64 suite, all three full x64 gates and all three
    ARM64 clean cross-builds against 561 frozen inputs ([record](../../TestRuns/DocumentLifecycleCore_2026-10-02.md)).
    Production lifecycle callers remain N2.
  - [x] Extend prepared replacement to represent an absent document; preallocate every retired draft/selection owner.
  - [x] Preserve New's one `New Canvas` entry with an absent before-document, mark the final revision saved,
    and preserve Close's empty saved history. Reuse the current direct methods as compatibility references.
  - [x] Validate dimensions, active id, source/session/generation/structural identity and exact history epoch;
    refuse invalid, stale, cancelled and moved-from publications before changing any live state.
- [x] N2 — Admit and activate production lifecycle callers under their existing selected host ordinal.
  The complete private WARP application run passes 258 controls (115 lifecycle, 58 accepted-dimension/refusal,
  85 Close cancellation/fault), with existing application goldens unchanged. Its exact 547 inputs, image/symbols
  and 91 evidence entries are retained in `.build/p1-host-draft/lifecycle-host-qualified-r5/`.
  The reviewed production candidate qualifies in the six-profile r9 matrix at N4; broader N3 and timing remain open.
  Capture retains only sealed authority and immutable history roots after admission; replacement needs no copy
  of the old document records. The source-owning API remains a compatibility/testing wrapper. Admission and
  retirement traversal remain H2 costs. The first receipt fixture failure and the later genuine typed-opacity
  replacement regression are preserved before repair. Replacement now retains the old field's acknowledged
  value until its late commit is consumed, as the existing opacity contract requires.
  - [x] Private host proof: capture authority/history after whole-graph admission; reserve prepared documents/history and aggregate retirement.
  - [x] Private host proof: retain discard/save decisions; failed prerequisite work prevents replacement and path changes.
  - [x] Private host proof: publish by nonthrowing transfers, then clear the path and apply Fit once.
  - [x] Private host proof: keep private clipboard/palette/settings and detach borrowed coverage engines on owner.
  - [x] Private host proof: retire replaced CPU payloads away from owner after apply/drop acknowledgement.
- [ ] N3 — Independent primitive and real-host controls; preserve original failures before repairs.
  - [x] Private host proof: literal dimensions, names, blank layer, optional document, active/selected ids, saved state,
    New Undo/Redo and Close's empty history; exact clipboard/palette preservation.
  - [ ] Existing nested transaction, redo branch, collapsed rows, masks, retained selection/display,
    every live draft and prepaid restore owners; no dangling borrowed graphics engine on retirement.
  - [ ] Before/after-ready cancel, source/target/history/replacement staleness, repeated/younger requests,
    reached allocation/history checkpoints, admission refusal, hidden completion and shutdown.
  - [x] Private host proof: New Canvas card owns accepted dimensions despite later edits; invalid/overflow dimensions
    cannot close the card, clear the path, lose the old document or create a history revision.
- [x] N4 — Skills/formatting and complete x64 Debug/Release/ASan gates plus all three ARM64 cross-builds.
  Exact frozen inputs, original failures and images/symbols/results are archived; contracts are reconciled for
  the integration commit. Fresh r9 passes all six profiles against 582 inputs
  ([record](../../TestRuns/P1LifecycleShutdown_2026-10-02.md)).
  The first combined matrix stops on ARM64 ASan compilation: the 485-entry string catalog still declared a
  484-element array. Its 572 inputs and 284 evidence entries are preserved on the qualification archive volume
  as `p1-lifecycle-policy-shutdown-native-r1-ARM64-compile-failure`. Correcting the explicit count changes no
  catalog expectation. No profile qualifies in that run; the fresh r2 matrix must complete every gate.
  The r2 matrix passes all three ARM64 profiles and x64 ASan build/ISA/host/document/composition gates, then stops
  at IoTests' delayed clipboard inspection/copy/decode assertions. Its 572 inputs and 292 evidence entries are
  retained as `p1-lifecycle-policy-shutdown-native-r2-ASan-io-failure`. The cause is unconfirmed; failure-only
  native render/sequence/format diagnostics precede a fresh matrix without changing expected pixels or status.
  The r3 matrix passes all three ARM64 builds and x64 ASan through IoTests, then fails in the application
  `clipboard-work` stage before its first reported host control. Its 572 inputs and 292 evidence entries are
  retained as `p1-lifecycle-policy-shutdown-native-r3-ASan-clipboard-host-failure`. Diagnostic-only early-return
  reporting now distinguishes user-clipboard preservation from fixture private-copy failure; the cause remains
  unconfirmed and no complete combined matrix is claimed.
  R4 passes all three clean ARM64 builds and both full x64 ASan/Release suites. Debug's build, ISA, host,
  document and composition gates pass, but IoTests fails the exact foreign clipboard-content preservation
  observation; sequence diagnostics show a later value differs from the value held during publication.
  Its 572 inputs and 295 evidence entries are retained as
  `p1-lifecycle-policy-shutdown-native-r4-Debug-clipboard-preservation-failure`. Failure-only fixture-read
  diagnostics now identify Inspect/Copy/Decode failure without changing the expected bytes or retry policy.
  No six-profile qualification or production clipboard fix is claimed by this partial run.
  The fixture's verification helper now uses the independent C9b reader, keeping one clipboard open through
  decode instead of releasing between Inspect and Copy. Direct primitive status/refusal tests, literal expected
  bytes and product behavior are unchanged. The focused Debug I/O run passes; the original intermittent causes
  remain unconfirmed. Reviewed actual Application shutdown hooks are integrated. Combined clean r5 passes all
  three ARM64 builds, then stops before x64 ASan runtime at a decoded EVEX row in the pinned DxUi menu's inline
  switch table. Its exact 576 inputs and 292 evidence entries are retained as
  `p1-lifecycle-policy-shutdown-native-r5-ASan-isa-failure`. An exact compact stack-guard proof passes 56 new
  independent controls and all existing parser controls; it rechecks the original archived image without a
  rebuild or runtime allowlist change. Combined r6 passes every ARM64 build and the complete x64 ASan suite,
  then stops at Release IoTests' `destination-retry.baseline`: mode 4, Encode, native error 5 at staging
  publication, before the fixture opens its replacement reader or installs faults. Its exact 577 inputs and
  294 evidence entries are retained as `p1-lifecycle-policy-shutdown-native-r6-Release-destination-retry-failure`.
  New bounded staging-publication recovery and independent source-reader controls remain under qualification;
  the original external cause is unproven. No full native qualification is claimed by r6. The focused Release r4
  build/ISA/HostTests/IoTests pass 34 suspended/prequeued retirement and 269 publication controls after exact R10/AL
  switch-table proof. Its 582 inputs and 292 evidence entries are retained. R7 then stops in ASan IoTests at the
  clipboard replacement refusal; r8 passes all ARM64 builds and the complete x64 ASan suite, then Release stops
  at clipboard source-sequence fixture acquisition and its dependent publication assertion. Debug does not run.
  The exact 582-input r7/r8 failures retain 292/294 evidence entries before further edits/rebuilds
  ([record](../../TestRuns/P1LifecycleShutdownFailure_2026-10-02.md)). Native six-profile qualification stays open;
  fixture diagnostics retain the original retry budget and expected assertions.
- [ ] N5 — Measure owner admission/capture/apply/history/chrome/resource retirement and first upload;
  finish H2/H3/H4 and the full P1 G1 exit before claiming constant-time capture or a timing target.

Status: ACTIVE. Parent: [UiNeverBlocks_2026-09-22.md](UiNeverBlocks_2026-09-22.md), P1.9 H1.
Owner: `DocumentSession`, prepared history/replacement, host continuations, lifecycle jobs and their tests.

## Current code and required compatibility

The direct `ClearDocument`/`CreateDocument` methods remain compatibility references. The activated host New,
New Canvas Apply and Close Project routes prepare optional replacement documents/history on a Commit worker
under the selected host ordinal; their combined native qualification passes
([record](../../TestRuns/P1LifecycleShutdown_2026-10-02.md)).

The compatibility reference is `Document_Model.md` and the direct `ClearDocument`/`CreateDocument` behavior:
New has one blank `Layer 1`, the requested dimensions, no pixels until painting, one `New Canvas` entry from
an absent before-document and a saved final revision. Undo returns to no document; Redo returns that canvas.
Close has no document, no active/selected layer, no collapsed rows, drafts or history, and is saved.
The private clipboard, palette and settings survive both operations. A new lifetime must advance session
serial/generation authority; exact arithmetic deltas are internal and do not identify a persisted document.

Existing `PreparedImageImport` replacement already owns retired transform/lasso/selection-move/brush/warp,
gradient/shape/pixel-move/color drafts, collapsed rows, display, adjustment, last brush information and warp
source. Extend its optional-document publication rather than constructing another incomplete retirement bundle.
Replacement publication must select an absent document too; a null output is a real Close result, not a no-op.

## Preparation and application

An accepted lifecycle job captures the exact immutable history view and sealed import-style session authority
after admission. It does not copy old document records; publication transfers their live owner into retirement.
Workers construct the new optional document, selected-id owner and replacement history.
New uses an absent baseline, one named entry and a saved after-revision. Close uses an absent baseline/after
and no entry. Closing an already absent document remains a successful no-op with no new authority or history.
Every replacement allocates its retirement holders before publication, including an empty Close.

Application first validates the complete authority and history witness. Only nonthrowing swaps follow; no
validation, snapshot copy, selection insertion, trim, allocation or full graph destruction follows the first
live mutation. The prepared CPU payload retains the previous document/history/display/draft owners until
the release lane acknowledges destruction. Graphics coverage engines are detached on owner before transfer.
Failure, cancellation or stale authority keeps the original document, path, history, saved marker and drafts.

The host retains the existing save/discard prompt and selected request ordinal. Accepted dimensions are copied
from the card. On successful New/Close publication it clears the path and enables Fit; later requests observe
the new document lifetime. Failed replacement does not authorize a younger write against an uninstalled source.

## Limits and validation

This slice removes allocating publication/destruction from these lifecycle callers. Value capture of 10,000
layer records and admission traversal remain explicit H2 costs; incremental trim remains H3; chrome rebuilding
and first upload remain H4. Reservations include both captured and privately prepared metadata/history plus
replaced-owner headroom. No timing guarantee follows from moving preparation to a worker.

Functional tests use literal state-machine expectations above and preserve all existing numeric goldens.
Event gates establish acknowledgement/order/lifetime; their failure bounds detect missing progress and never
assert performance. Measured timing requires the current Release binary and qualified active desktop/device.
