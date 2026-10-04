# P1.3 — All Layers wand New-selection readback

## Latest receipts (r17–r21)

- [x] Full x64 Release r19 passes on freeze `D8F949825F1769BF6A18FDFF5175D91CD9AE2E8A7C832CD319C9D469B5A45826`.
- [x] x64 Debug analysis r21 passes on repaired-fixture freeze `1B98AA514D4105212784C41205917FE1BA449752B537821CC5FF9A7C5950CDC4`.
- [x] Independent archive receipts for r17–r21 match each archived run's completion, source/evidence manifests, copy count and actual exit.
- [ ] Debug r17 runtime fails the existing other-history readiness assertion; cause remains unassigned.
- [x] Preserve Release r18's vcpkg lock failure before compilation; the later serial Release r19 run passes.
- [x] Preserve Debug analysis r20's tautological-fixture failure; r21 repairs it by deriving logical bytes from captured canvas width and height while retaining expected literal `32`.
- [ ] Full current Debug/Release/ASan matrix, G1, and parent P1 qualification remain open. The r19 Release and r21 analysis receipts are separate runs on different freezes; they do not qualify a combined matrix.

## Delivery checklist

- [x] Audit the settled New-selection command, active-layer worker publication, utility readback and ordered completion owners.
- [x] Implement distinct immutable composite input/publication APIs and a conservative complete-graph memory plan; review the independent core fixtures.
- [x] Implement the actual New command's descriptor-only admission, asynchronous GPU source, detached worker selection/history preparation and checked publication; Debug r12, Release r14 and ASan r16 pass their complete suites on their recorded snapshots.
- [x] Full Debug r12 qualifies the real command, captured settings, exact visible composite, cancellation, stale authority, failures, ordinal barriers, no-work inputs and actual record-capacity refusal; 51 core and 111 host checks pass.
- [x] Full Debug r12 proves Map/utility release before the prepared CPU gate and off-owner readback/completed-composite aggregate retirement; actual device recovery after copy also passes.
- [ ] Extend graph-specific shutdown/lifetime coverage before capture, while rows are lent and after-ready, including complete last-owner retirement.
- [ ] Run the full current x64 Debug/Release/ASan matrix and ARM64 cross-build gates; preserve preceding controls and failed receipts. Release r19 and analysis r21 are separate receipts, not a combined matrix.
- [ ] Measure the original P1 G1 rows on A/B and fix overruns; qualify the whole P1 caller integration before native activation.
- [ ] Commit and push the qualified bounded slice; integrate durable contracts and update the parent ledger.

Status: ACTIVE under [P1](UiNeverBlocks_2026-09-22.md), private candidate on `codex/p1-sampler` after `2cfabe9`.
Full Debug r11 passed the 51 core checks and failed one new device fixture; its verified failure archive is retained.
The repaired actual device-recovery fixture passes full Debug r12 and full Release r14. ASan build r15 fails the
unchanged ISA scanner on dispatch-table bytes; the shared request descriptor repair passes full ASan r16.
Later attempts are listed above and in the receipt ledger. Debug r17 and analysis r20 failures remain recorded;
Release r19 and analysis r21 pass only their respective configurations/scopes. ARM64 cross-builds,
shutdown/lifetime and G1 remain due.
[Receipts and scope](../../TestRuns/WandCompositeReadback_2026-10-04.md).
This slice covers settled **New** with All Layers enabled. Add/Subtract and coexisting live tool contexts remain
on their current synchronous paths and stay in P1's remaining work. No large-image or G1 qualification is claimed.

## Source and order

`RunMagicWand` queues the shared wand descriptor (`HostWandCompositeNewInput` aliases `HostWandLayerNewInput`) with
event-time point, sample size, tolerance, connectivity and
antialiasing. The selected original host ordinal captures the settled committed document after older producers
complete. A fixed descriptor alone is built before whole admission. One scheduler record/ticket/reservation then
owns source capture, utility composition/readback, worker flood/trace, prepared document/history and retirement.

New requests use the existing adjacent-New coalescing policy only when shared ordinals are adjacent. Commands,
strokes and ordinal gaps remain barriers. Deselect/Undo behind the request execute after its acknowledgement.
Cancel leaves the receipt/ticket pending until worker acknowledgement, with no partial selection/history edit.

## Immutable worker and publication

The composite entrypoint requires `sampleAllLayers`, canvas-sized BGRA storage and finite in-bounds coordinates.
The active-layer entrypoint still rejects All Layers. Crossed publication APIs refuse. An absent active image
does not imply a transparent visible composite. Source pixels include normal visibility, opacity, masks, blend,
clipping and adjustment-layer composition; transient stroke/destructive-panel previews are excluded.

Owner recording and Map/Unmap follow `DocumentReadbackJob`; workers copy/adopt the rows. Its detached boundary
acknowledges the final map and releases utility targets before flood/trace and history preparation. Subsequent
device replacement cannot invalidate the already copied composite. The worker reads only the admitted immutable
graph. Exact session/source generation, active-target/history authority and publication epoch guard the owner
swap. An unchanged outline preserves redo/revision; a successful changed outline creates one Magic Wand entry.

The derived aggregate has separate empty destinations for base document/buffer/image transfer and a separate
completed composite field. Moving the completion image into a base transfer destination would swap pixels back
onto the owner destructor and is prohibited. The aggregate retires off the owner after acknowledgement, including
cancel, capture failure, worker/history failure and stale completion. GPU cleanup stays on its owner.

## Admission and qualification

Reserve the composite pixel allocation, mask/vertex/flood/trace/geometry bounds, complete capture/history graph,
base readback working sets, immutable source-retirement ownership and descriptor/payload metadata before capture.
Checked arithmetic refuses impossible bounds. Record/capacity refusal captures zero sources and publishes zero
edits. Conservative reservation size alone does not establish that A/B will be admitted; capacity and original
owner/apply/frame/cancellation budgets must be measured and closed under the parent P1 contract.

Independent core tests use literal two-color bytes and exact rectangle vertices, not a production-render oracle.
Host tests use a hidden active image and a different visible two-color composite; this distinguishes All Layers
from active-layer sampling. Event gates observe pending receipts, unchanged live state, original reservation,
utility/map release, real window-message dispatch and retirement. Faults are actual construction/history stages
and actual scheduler record exhaustion. Timeouts are failure bounds, never performance assertions.

The sampler prerequisites remain independently tracked in [SamplerReadback](SamplerReadback_2026-10-04.md).
