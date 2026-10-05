# P1.9 H4 — Bounded raster uploads and first display

## Implementation checklist

- [x] Identify current owner costs and preserve the six qualified sampler measurements.
- [x] Specify admission, row leases, completion, cancellation, generation and retirement below.
- [ ] U1: review/register the worker row-copy helper; qualify independent pitched-row and sparse goldens.
  - [x] Registered worker row-copy helper passes 73 independent CPU controls in the full identical-source Debug/Release/real ASan matrix, Release analysis and three ARM64 cross-builds ([record](../../TestRuns/P1UploadCpuPreparation_2026-10-05.md)). Proxy reduction and consumer qualification remain open.
  - [x] Private strict CPU copy/reduction probe preserves 73 copy controls and adds 41 independent crop-grid/rounding/edge/scratch/sparse/cancellation controls. Normal registration, resource-plan admission and consumer activation remain open.
- [ ] U2: implement the bounded owner upload queue and event-driven staging reuse; qualify real WARP copies.
  - [x] Original registered transport passes 165 controls with all 746 predecessors preserved in full Debug r122, Release r126 and real ASan r124 on the same source as analysis/cross-builds (admission, crops/guards, events, cancellation, rebind, staging reuse and held-worker shutdown). The newer private fault/receiver integration and consumer activation remain open ([record](../../TestRuns/P1UploadCpuPreparation_2026-10-05.md)).
  - [x] Private strict WARP startup/fault probe preserves the 165 queue controls and adds 181 independent partial-initialization/loan/Map/pitch/rearm/signal/quarantine checks. Normal configuration and consumer qualification remain open.
- [ ] U3: prepare complete raster residency plans on workers after admission; account for every resident owner.
  - [x] Private immutable arithmetic layout passes 34 literal mip/crop/chunk/scratch/retention/sparse controls. It does not implement complete resource-plan admission or device/display publication.
  - [x] Qualify the opt-in terminal retirement acknowledgement protocol before a job can own an upload receiver across cancellation or failure.
    - [x] Private strict scheduler probe preserves the successful-stage controls and passes 88 opt-in success/failure/cancellation/default/reuse/FIFO/reservation/joined-shutdown controls (actual child 55120, source freeze B49504BE539E19C2030E5144298050D4F58EDC76A542AD5CDF0EB8C7C746296C). Normal registration and GPU leaf/consumer qualification remain open.
    - [x] Qualify the new base-chunk upload job with both producer and queue acknowledgement, including skipped mapped fills, pending map/fence cancellation, early completion, device retirement and shutdown.
      - [x] Primary-reviewed private Main WARP probe passes all 473 transport/fault/receiver controls, including 127 new receiver assertions. Full registered Debug r130, Release analysis r131 and all three ARM64 r9 cross-builds pass on the same 805 inputs; Release/real ASan and production activation remain open ([record](../../TestRuns/P1UploadLifetime_2026-10-05.md)).
  - [x] Review and qualify the separately drafted committed-document bound/enumeration plan (held immutable root, deduplicated raster layouts, separate live/retired CPU/GPU charges and declared surface/lookup/staging/driver allowances). Preview/brush/utility inputs, actual resource allocation and application admission remain open.
  - [x] Committed-document plan and six resource timing stages are primary-reviewed and registered; private CPU/WARP/ETL proofs pass. Full Debug r135 / Release r137 / real ASan r138, analysis r136 and ARM64 r10 pass on the same 811 inputs; every predecessor is preserved. Qualified step is pushed as `e0a9c67`; live consumers remain due ([record](../../TestRuns/P1ResidencyPlanAndStages_2026-10-05.md)).
- [ ] U4: stage resource creation and preserve the old display until a complete current plan can publish.
  - [ ] Primary draft: separate GPU reservations, retained capability-qualified worker texture/view creation, same-ticket base uploads and destination/source/reservation adoption. Reusable staging has its own persistent charge; uncertain copies and joined shutdown retain their graph charge until owner storage release. Private compile failures r1/r2 are preserved and repaired; actual private r4 passes 253 additions and 473 predecessors; Main integration/normal qualification is pending ([record](../../TestRuns/P1GpuCreationAndAdmission_2026-10-05.md)). Full matrix, complete document resources/mips and live activation remain open.
- [ ] U5: integrate display and utility consumers, sparse/proxy/mask/preview sources and borrowed brush adoption.
- [ ] U6: bound layer/chrome synchronization and retire replaced CPU/GPU owners through their correct lanes.
- [ ] U7: run the fault/lifetime matrix, full required configurations and unchanged predecessor registries.
  - [x] Dormant foundation matrix passes full Debug r130, Release r133, real ASan r134, Release analysis r131 and all three ARM64 r9 cross-builds on identical 805 inputs; 75 CPU/88 scheduler/308 GPU additions and every predecessor route/stage are verified ([record](../../TestRuns/P1UploadLifetime_2026-10-05.md)). Complete consumer/device/resource/retirement faults remain open.
- [ ] U8: fix first-display, warmup and large-fixture owner/apply/frame overruns; retain Release/ETW evidence.
  - [x] Fixed-storage texture/view/mip/upload/composition/chrome timing and real WARP/ETW controls qualify in the full matrix (`e0a9c67`); reporting preserves inclusive first/warmup stages and absence semantics. Actual benchmark rows and G1 fixes remain due.
- [ ] Reconcile owning contracts, commit/push qualified steps, and close only after all H4 requirements pass.

**Status: ACTIVE; original row-copy/transport foundations qualified and pushed (`74c327d`); lifetime foundations pass the complete normal matrix; production activation remains open.**
Parent: [P1 delivery milestone](UiNeverBlocks_2026-09-22.md), P1.9 H4.
Owners: Application, Renderer, CanvasRenderer, GPU upload queue, JobScheduler and worker preparation.
Contracts: Rendering_Composition, Core_Architecture, Core_PerformanceAndResources, UI_Windowing,
Document_Model, Testing_Validation and Testing_Benchmarks. This plan does not authorize new blend/color math,
replacement of mask edge semantics or postponement of a P1 latency requirement to P3.

## 1. Evidence and scope

The managed source at `8ab921d` still uploads a dense raster in one owner `UpdateSubresource` call;
`CanvasRenderer::UploadRaster` calls `Data`, creates a whole mipmapped texture and generates its mips.
`UploadRows` uses 8 MiB source bands but loops through every band in one owner invocation, allocates row/texel
buffers and performs `ReadRegion`/`ReduceBlocks` there. Merely using bands does not bound a frame.
Sparse GPU assembly and existing brush texture adoption already avoid many complete CPU assemblies; preserve them.

[Sampler measurements](../../TestRuns/P1SamplerMeasurements_2026-10-05.md) bind six hardware/WARP runs
to the qualified binary and show bounded steady sampling alongside first-display/warmup overruns. They do not
qualify the full G1 fixture or exclude startup from H4. Texture creation, canvas/stack/coverage allocation,
mips, chrome publication, final owner release and composition must each have their own recorded duration.

The final target remains the parent's original P1 G1 contract: admitted inputs, completion drain/actions and
frame CPU p99 <= 4 ms and max <= 16 ms, completed-result apply <= 2 ms, no owner pixel loops or waits.
Measure the specified 45 MP two-image/mask/adjustment fixture and 100 MP fixture, hardware R1 and the original
WARP rows. Retain the parent's explicit P3 exceptions; do not invent additional exceptions here.

## 2. Admission and immutable plan

1. The selected command keeps its original ordinal, ticket, document serial, generation, target identity and
   cancellation state. A residency stage neither repeats the edit nor creates a second undo entry.
2. Owner admission reads checked sealed root bounds. The worker enumerates the immutable layer records,
   deduplicates held raster identities and prepares required format/region/scale/mip/coverage inputs. No owner
   traversal of thousands of layer records, selection points or sparse tiles is hidden inside capture/admission.
3. Charge existing and replacement residents concurrently, source handles, plan containers/capacities, all
   staging slots, CPU row buffers, mip/stack/coverage targets, driver headroom and the complete retirement graph.
   CPU and VRAM reservations are separate; overflow, unknown bounds and budget refusal fail before capture.
4. Cache keys hold their Raster, including pending residents. Include format, crop/region, proxy scale,
   preview invocation and device generation where applicable. A raw pointer is not authority or ownership.
5. Fixed queue records and reusable slots limit outstanding work. Initial prototype limits: eight requests,
   two lent/in-flight chunks, <= 1 MiB logical bytes per chunk, <= 128 KiB per row, <= 16 MiB total staging
   allocation including pitch/class rounding. These are admission/measurement inputs, not timing guarantees;
   any adjustment requires recorded CPU/GPU/peak-memory evidence and unchanged G1 acceptance thresholds.
6. Utility inputs remain committed full-resolution sources. A display proxy never silently substitutes for
   export, merge, sampler, wand, clone or the brush's source. Hidden utility work owns separate targets.

## 3. Worker row contract (U1)

`CopyRasterRowsForUpload` accepts an owned immutable Raster plus an in-bounds source rectangle and borrowed
writable rows. Validate positive dimensions, nonnegative offsets, Gray8/Bgra8888, nonnull destination,
pitch >= logical row bytes, pitch representable by ReadRegion, and checked capacity
`(height - 1) * pitch + rowBytes` before writing. Invalid input and precancellation write no bytes.

The worker calls `ReadRegion` one row at a time and checks cancellation at each row boundary. It never calls
`Data` or `Materialize`, allocates a full raster, touches UI, maps/unmaps, flushes or receives a GPU context.
Padding remains untouched. Cancellation can leave a prefix in unpublished staging; the owner must discard that
chunk. Cancellation after the last copy still suppresses submission. A deterministic observer permits tests to
cancel after a named row; production leaves it empty. Mask thumbnails/edge tone remain atomically prepared
with the mask, using the existing nearest-sample semantics.

Keep exact proxy block averaging/rounding from ReduceBlocks on a worker, including cropped origins, nonmultiple
edges and Gray8. Qualify that separately against literal/serial expectations; the simple copy helper alone
does not implement proxies or sparse GPU assembly planning.

## 4. Owner upload queue (U2)

The queue is an optional leaf of OwnerWorkMultiplexer and uses the scheduler's existing event wait pool.
All immediate-context calls, Map, Unmap, copy recording, completion registration and target publication remain
on the owner. Workers receive only data, pitch, bounds and an exact slot/device generation lease.

| State | Owner/worker transition | Required ownership |
| --- | --- | --- |
| Free | Owner accepts an admitted bounded chunk | Original job, held source and destination generation |
| Pending | Owner prepares/reuses a bounded staging class and attempts nonblocking WRITE Map | Queue owns texture/context |
| Lent | Worker fills the mapped rows, then publishes its result with release/acquire ordering | Mapped resource/context survive cancellation and device retirement |
| Filled | Owner validates exact lease, acknowledges Unmap and records a bounded copy only for Ready/current work | Cancelled, failed or retired rows never copy |
| Waiting | Owner arms a real completion event after copy | Slot cannot be overwritten/reused until completion |
| Free/Retired | Joined notification marks readiness; owner releases/reuses, or drops retired storage | No expired callback or receiver |

Use a STAGING CPU_WRITE texture and Map WRITE with DO_NOT_WAIT. Never combine DO_NOT_WAIT with WRITE_DISCARD
or WRITE_NO_OVERWRITE. Treat WAS_STILL_DRAWING as a wait state, not a polling loop: arm one supported completion
barrier, retry once when signalled, then fail locally if still busy. Do not read write-combined mapped memory.
Copy only an exact valid source box to the destination mip/offset after Unmap; source and destination must be
unmapped and format-compatible. No full-image UpdateSubresource fallback.

Waiting/lent work counts as pending but not ready; no periodic timer, HasReady spin or repeated empty callback.
Pump records a fixed count/byte-limited slice and returns to messages. Fence registration uses a real event;
the supported EnqueueSetEvent route is explicitly tested. Observe the established flush behavior of each route,
avoiding a redundant flush. The completion callback only sets atomic readiness and wakes the scheduler.

Device/slot generations cannot wrap/reuse into stale authority. Duplicate acknowledgements, foreign slots,
wrong generations and acknowledgements before worker publication refuse without Unmap/copy of another lease.

## 5. Resource creation, composition and publication (U3–U5)

Moving row copying does not establish a bounded whole-texture CreateTexture2D/CreateSRV/GenerateMips or a bounded
first Compose. Instrument these calls before and after implementation. D3D11's device supports concurrent use
by default, but DriverConcurrentCreates=false can serialize resource creation with drawing. A worker call alone
therefore cannot prove owner responsiveness. Keep device ownership in the GPU subsystem, never Common kernels.

Prototype worker resource creation only with a retained exact device generation and the reported concurrency
capability. For the unsupported/coarsely synchronized route, design bounded resident allocation/assembly or
another measured nonblocking route before activation. Do not fall back to a 400 MiB owner allocation/upload.
If bounded raster backing requires a new sampling representation, document and prove transform/filter/mask/
clipping parity; preserve document-space canvas and pan/zoom blit-only semantics. P3's tiled canvas/LOD remains
its own later feature, not a substitute for completing this P1 allocation gate.

Mip creation/updates and first canvas/stack/coverage composition also require a sliced/prepared protocol with
an explicit complete result. Present the previous settled canvas while a new plan is pending; show normal
localized job progress. Never sample a half-initialized texture, combine old/new document epochs or publish a
complete plan with missing mask/coverage resources. On first open with no settled canvas, use the ordinary
empty canvas/chrome until the complete result is current. Pan/zoom continues to blit the held settled result.

Refresh authority before publication, then swap completed owners without allocation/traversal. An old live
document can remain editable only under the parent's ordered input rules; it cannot commit against new ids.
Publish one display epoch per complete plan and sweep only owners no longer used by display, utility or brush.
No settled document gets new upload/composition work from a frame alone. Account for visible chrome/layer rows,
UI Automation and preview lookup resources under the same first-display measurements.

## 6. Cancellation, replacement, shutdown and retirement

Cancel suppresses unpublished chunks/plan publication; the selected receipt remains pending through worker and
mapped-row acknowledgement. A mapped pointer never expires merely because a device was removed. Retired device
contexts/resources remain held until the copying worker stops and owner Unmap acknowledgement is possible;
no recording or copy occurs on a retired context. A new generation uses separate resources/reservations.
Restart from the original immutable source only under the existing bounded restart/ordinal rules.

Shutdown: request stop and suppress new submissions; stop/join notification producers while their contexts
remain alive; join compute and retirement workers; acknowledge remaining mapped rows on their retained owner
context; detach the empty source; then destroy queue/device/chrome/window. No borrowed receiver outlives its
job. Test cancellation at Pending, Lent, Filled and Waiting, and actual shutdown with a lent worker held.

Large CPU graphs retire on the admitted worker release lane. GPU/context resources are released on their
correct owner lane in bounded batches after fence completion. Include retired resident bytes in the cap;
replacement cannot allocate a second unlimited graph while the first is still lent/in-flight.

## 7. Qualification matrix and phase exits

- CPU literals: BGRA/Gray subsets, offsets, sparse base/tile/fill overlap, padded pitch, guards, exact-capacity,
  overflow/invalid/default/zero inputs, precancel/midrow cancel, observer ordering and unchanged residency epoch.
- Real WARP queue: literal pixels after staging copies, both completion routes, capacity/in-flight caps, reusable
  classes, Busy retry/failure, duplicate/wrong-generation leases, cancel each state, device retire/rebind,
  hidden/no-Present completion, every allocation/wait registration failure and actual shutdown/join.
- Composition: all existing goldens unchanged; dense/sparse masks, eager edge tone, transformed sampling,
  clipping/folders/adjustment previews, brush adoption, utility/display concurrency, oversized crop/proxy edges,
  pan/zoom zero recomposition and zero hidden display surfaces. Never rebaseline a legacy golden.
- Actual host: open/import/resize/filter replacement, stale target/document, rapid queued commands, cancel,
  device removal, hide/show and shutdown. Keep the old display valid until atomic complete publication.
- Full Debug/Release/real ASan Debug tests, Release analysis, three ARM64 cross-builds, skills/formatting and
  strict preservation of every predecessor literal control/route/stage. Native ARM64 runtime stays explicitly
  unavailable unless executed on a native host. Source/hash/archive receipts bind each result.
- Release/ETW G1 fixtures record per-stage wall time, rows/bytes/allocations/copies/submissions/Map retries,
  queue/resident/retired peaks, input-to-frame and first/warmup samples. Retain failed observations. Fix overruns
  before claiming H4/P1 completion; a CPU helper test or warm-cache sampler run cannot qualify this exit.

## 8. API references

Design constraints above follow the primary Microsoft contracts for
[Map flags](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ne-d3d11-d3d11_map_flag),
[Map and writable memory](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-map),
[CopySubresourceRegion](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-copysubresourceregion),
[driver threading capabilities](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ns-d3d11-d3d11_feature_data_threading)
and [multithreaded resource creation](https://learn.microsoft.com/en-us/windows/win32/direct3d11/overviews-direct3d-11-render-multi-thread-create).
Capability reports and actual measurements select the route; hardware names do not.
