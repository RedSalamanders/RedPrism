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
- [ ] N2 — Admit and activate production lifecycle callers under their existing selected host ordinal.
  Private host candidate: copied dimensions, selected ordinal/acknowledgement and path/Fit-after-success routing
  are implemented. Capture now retains only sealed authority and immutable history roots; source record copies
  are unnecessary for replacement. The original source-owning API remains a compatibility/testing wrapper.
  Admission/retirement traversal, actual caller tests and native activation are still unqualified.
  - [ ] Capture source/history only after whole-graph admission; reserve prepared documents/history and aggregate retirement.
  - [ ] Retain confirmed discard/save decisions; failed prerequisite work prevents replacement and path changes.
  - [ ] Publish document/history/targets/drafts by nonthrowing transfers, then clear the path and apply Fit once.
  - [ ] Keep the private clipboard/palette and existing settings; detach borrowed brush coverage engines on owner.
  - [ ] Retire the complete replaced CPU graph away from the owner before scheduler destruction.
- [ ] N3 — Independent primitive and real-host controls; preserve original failures before repairs.
  - [ ] Literal dimensions, names, blank layer, optional document, active/selected ids, saved state,
    New Undo/Redo and Close's empty history; exact clipboard/palette preservation.
  - [ ] Existing nested transaction, redo branch, collapsed rows, masks, retained selection/display,
    every live draft and prepaid restore owners; no dangling borrowed graphics engine on retirement.
  - [ ] Before/after-ready cancel, source/target/history/replacement staleness, repeated/younger requests,
    reached allocation/history checkpoints, admission refusal, hidden completion and shutdown.
  - [ ] New Canvas card owns the accepted dimensions despite later card edits; invalid/overflow dimensions
    cannot close the card, clear the path, lose the old document or create a history revision.
- [ ] N4 — Skills/formatting and complete x64 Debug/Release/ASan gates plus all three ARM64 cross-builds.
  Archive frozen inputs, original failures, exact images/symbols/results; reconcile contracts and commit/push.
- [ ] N5 — Measure owner admission/capture/apply/history/chrome/resource retirement and first upload;
  finish H2/H3/H4 and the full P1 G1 exit before claiming constant-time capture or a timing target.

Status: ACTIVE. Parent: [UiNeverBlocks_2026-09-22.md](UiNeverBlocks_2026-09-22.md), P1.9 H1.
Owner: `DocumentSession`, prepared history/replacement, host continuations, lifecycle jobs and their tests.

## Current code and required compatibility

`NewDocument` and New Canvas card Apply settle interactions, call `ClearDocument`, call `CreateDocument`
and mark the final history revision saved on the owner. Close Project clears the document/history/drafts on
the owner. These paths allocate history and can destroy large retained graphs synchronously.

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

An accepted lifecycle job captures the current source, exact history view and import-style session authority
after admission. Workers construct the new optional document, selected-id owner and replacement history.
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
