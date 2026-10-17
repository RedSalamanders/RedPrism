# Document model

Status: current normative product contract
Last reviewed: 2026-10-07
Owner: `Common/Document.h`, `Common/Document.cpp`

## Scope

This contract owns the CPU document: canvas size, the bottom-to-top layer list, group/`parentID` rules, live
clipping (`maskSourceID`), raster masks, adjustment and shape metadata, and the session-only selection.
GPU compositing is [`Rendering/Rendering_Composition.md`](../Rendering/Rendering_Composition.md). `.prism` I/O is
[`IO/IO_ProjectFormat.md`](../IO/IO_ProjectFormat.md). History is [`Document_History.md`](Document_History.md).

The requirements below and `ProjectStore` validation own document behavior.

## Limits (identical to `ProjectStore`)

| Rule | Value |
| --- | --- |
| Canvas side | 1…30 000 px |
| Layers | ≤10 000 |
| Image pixels (sum) | ≤100 M |
| Mask pixels (sum) | ≤100 M |
| Working space | sRGB |
| Format versions | 1–7 (current save is 7) |
| Layer name | non-empty after trim, ≤16 384 UTF-8 bytes |
| Group depth | ≤64 ancestors |
| Group appearance | opacity 1 and blend Normal (on disk and in the live graph) |
| Manifest (I/O) | ≤4 MiB; this contract validates the graph, not JSON |

The session enforces both 100 M sums on every edit that grows pixels (`PixelBudgetLeft`): brush and raster edits,
pastes, shapes, Layer via Copy, duplicates, masks, the floating selection's merge, filters that grow the layer's
grid, merges and distortions (a live shape past the budget keeps its pixels instead of redrawing). An edit that would
exceed them is refused and the document stays as it was; `TakeBudgetRefusal()` reports it once so the host can say
so. Decision D7 of the 2026-09-23 review: legacy enforces the sums only when it saves, where such a document is then
rejected.

## Types

`CanvasDocument` holds `id`, integer width/height, `resolution` (default 72, finite 1…9600 when set), layers
bottom-to-top, and optional `DocumentSelection`: closed loops in document pixels under the nonzero winding rule plus an
antialiasing flag ([`Document_Tools.md`](Document_Tools.md)). Selection is part of document equality so undo covers it.
It is not saved. `nullopt` means no selection; a selection enclosing no area means "touch nothing."

`ImageLayer` is a value type: `Guid` id, optional shared `Raster` asset, `LayerTransform`, name, visibility,
optional `parentID`, `isGroup`, opacity 0…1, `LayerBlendMode`, optional `LayerMask`, optional `maskSourceID`,
optional `LayerAdjustment`, optional `LayerShapeStyle`. Blank layers allocate no pixels. Groups have no image
file. Adjustment layers (v7) are not groups and have no image file. `RasterAsset::thumbnail` is optional derived
image state: it may be empty and can be produced explicitly with `Raster::MakeThumbnail` (at most 96 px on the long
side). It does not change image-layer equality. A shape layer also keeps `shapeImage`, the raster its shape drew,
outside equality: while the asset still holds it the layer is a live shape (`LiveShape`) that redraws at a new size
(C11).

### Immutable record construction foundation (P1.9 H2.2)

`Common/RecordRoot.h` provides a shared immutable sequence primitive. Its primitive qualification does not
qualify the full `CanvasDocument::layers` writer/capture integration, which remains tracked in
[`HistoryStorageAccounting`](../Plans/WIP/HistoryStorageAccounting_2026-09-30.md).

A captured root retains its immutable record vector. Copying a root never copies its records, and all reads,
including on non-const handles, remain const. Root identity is only an equality fast path: independently built
roots compare record values and sequence order, including equal empty sequences. Records and their shared children
must obey immutable ownership after publication. Record moves, assignments and destruction must be nonthrowing.

An exclusive, noncopyable builder reserves its admitted record capacity, refuses growth past that limit before
moving an incoming record, and exposes only const reads. Insert/replace/erase affect its private buffer. A capacity
larger than the vector can represent produces an invalid builder without reserving a record buffer; Clone refuses
that capacity even for an empty source. Sealing allocates the immutable root owner before moving the buffer;
allocation failure leaves the builder retryable. Successful Seal consumes it. Consumed/moved builders refuse
further mutation or publication. `use_count` never authorizes mutation of a published root.

`Builder::Adopt` transfers an exclusively owned worker vector and its allocator. All mutable borrows MUST end
before adoption. It refuses size or actual capacity above the admitted limit, or a limit above `max_size`, before
moving or reserving. A required reserve has the ordinary strong failure guarantee; account old/new buffer overlap
first. Sufficient-capacity adoption copies/moves no records and retains the buffer address. Subsequent sealing
keeps the allocation-before-move retry rule. The caller retains the source document/root through acknowledgement;
adoption never uses reference counts as permission to edit shared records.

Clone checks cancellation before allocation, before each record copy and after the final copy. Cancellation,
record-copy failure or new-buffer allocation failure preserves the source root. The allocator supplied for a
new clone owns that clone's allocations; it does not silently inherit the source root's allocation owner.
Final root destruction belongs to the admitted retirement path. Container capacity alone does not account
record children, captured history, control blocks or complete construction/retirement peaks. Those remain
integration and H3 accounting obligations; this primitive establishes no UI timing bound.

An enabled `RecordOwnershipPolicy` measures owned record children while an exclusive builder seals. The
immutable root caches `RecordOwnershipBounds`: metadata bytes, complete conservative ownership bytes and raster
node occurrences, image-pixel occurrences and mask-pixel occurrences. Queries and handle copies allocate nothing and never revisit records. Root/vector capacity and
a control allowance are added to child totals with checked arithmetic. Disabled/unavailable policies, overflow
or a child ownership total below its metadata total yield an unavailable bound; an otherwise valid root still
seals. A consumer requiring a complete bound MUST refuse admission explicitly, never substitute zero.
Cancellation is checked before measurement, between records and before publication; refusal preserves the builder.

`ImageLayer` metadata counts retained layer/asset/mask names and all four curve-channel capacities, with allocator
and Debug proxy allowances. Ownership additionally sums cached image, mask, thumbnail and shape raster bounds by
occurrence, including possible sparse assembly. Shared occurrences intentionally overcount, and measurement never
materializes pixels. These bounds remain stable after lazy assembly. They do not replace exact history deduplication,
process headroom or complete operation admission/retirement. Application use and G1 remain separately qualified.

An enabled `RecordLookupPolicy` builds one sorted key/index vector while the exclusive builder seals. It checks
its policy's record maximum before allocation/key access and checks cancellation before keys, during duplicate
validation and before publication. Sorting is bounded by that maximum. By default, exceeding the policy maximum
refuses sealing before key allocation. A policy may explicitly retain oversized malformed snapshots without an
index; such snapshots expose no lookup or uniqueness authority. Keys, comparisons, moves and destruction
are nonthrowing. Index/root allocation failure preserves the original builder buffer and its records for retry.
The immutable root owns its lookup and records together; lookup borrows allocate nothing. Duplicate keys retain
the first source record, and `HasUniqueKeys` exposes the invalid-root condition without a second traversal.
Ownership includes the actual lookup capacity and allocator/control allowance.

`ImageLayer` enables Guid lookup for at most 10,000 records. Full-root `IndexOf`, `FindLayer` and layer lookup use
the index; span/vector/sliced queries retain indices relative to their supplied range. Oversized snapshots remain
representable for production validation and malformed-input tests, but have no index/uniqueness authority and
remain rejected by `ValidateDocument`; this does not increase the supported document layer limit. Cached pixel totals count
each layer occurrence even with shared rasters, and exclude thumbnails and shape-source storage. The bounded
initializer-list pixel-budget query subtracts unique existing exclusions from these totals. Missing/repeated IDs
do not alter the result; invalid duplicate roots or unavailable ownership bounds refuse with zero budget.
The unordered-set form retains the linear reference. Indexing does not qualify all writer/destructor or G1 paths.

`CanvasDocument::RetirementMemoryBytes` supplies a complete conservative bound for that immutable source only.
It reads the sealed layer root and construction-cached selection storage in constant time, without allocation,
record/point traversal or pixel materialization. The bound includes a 64 KiB root/rounding allowance, the document
header, complete cached layer ownership, selection retained capacities, and 64-byte outer and per-constructed-loop
control/allocator allowances. Shared occurrences may overcount. Possible sparse assembly is included before Data
publishes it. A default empty layer handle has a valid nonzero bound; a published root with unavailable child bounds
or arithmetic overflow returns zero and MUST cause admission refusal. This source bound excludes unrelated history
and display owners. Consumers MUST additionally charge their job, readback, producer and retirement peaks. It
does not replace exact history accounting, global memory admission or first-upload/G1 qualification.

Pure layer reads (`IndexOf`, hierarchy/clipping validation, hierarchy entries, descendants and effective
visibility) borrow `std::span<const ImageLayer>`. Array, sliced, vector and immutable-root ranges retain their
own order; lookup indices are relative to the supplied range. Validation refuses a required parent/clipping
source outside that range. The helper stores no span and constructs no owning adapter. A caller MUST hold the
underlying records for the entire call; longer-lived views additionally retain their document/root owner.
The const-document visibility overload forwards a borrowed span for existing callers. Mutable structural
helpers still edit their explicitly owned vector until full document record integration is complete.

### Raster storage and masks

`Raster` is immutable shared storage (`std::shared_ptr`). Equality of image pixels is identity of that storage,
matching Swift `CGImage ===`. Formats: BGRA8888 (premultiplied, B,G,R,A byte order, the WIC `32bppPBGRA` layout
the codecs and the composition use) and Gray8 masks. Consumers that cache by `Raster::Identity()` MUST hold the
`Raster` (a strong reference) for as long as the cache entry lives; a freed address can be reused by another image.

A raster is contiguous or sparse (C10). A sparse raster is an unchanged *base* placed in its grid, disjoint
replacement *tiles* (`RasterTile`: a window into shared pixels at a grid position) and a *fill* byte for pixels
neither reaches. `MakeSparse` builds one; `Replacing(source, tiles, crop, fill)` writes tiles of a working grid over
a source and crops the result, splitting older tiles around newer ones so the list stays disjoint while the base and
every untouched tile stay shared. A sparse source keeps its own fill over its own extent (a hide-all mask painted
twice keeps hiding what neither stroke touched); only the area the crop adds beyond the source reads `fill`, as
windows of one shared uniform block of at most 256 × 256. `Stretched(width, height)` resamples a raster by the
nearest pixel (target pixel x reads source pixel ⌊x · own width / width⌋), which is how a mask whose pixel size
differs from its layer's is painted; `CarriedMask` moves a mask on its layer's grid onto a grown or cropped grid of
that layer (stretched first; the old area keeps its pixels and fill, only new area takes the given value). `Crop`
copies a region; `ReadRegion` samples any raster without assembling it (thumbnails use it); `ForEachStorage` visits
the base and every tile once (history counts bytes that way).
`Data()` assembles a contiguous copy on first use and keeps it (`IsMaterialized`); consumers that can read tiles
(the composition, the brush, thumbnails) MUST NOT call it. When that assembly cannot allocate, `Data()` is null and
every consumer fails its own operation (Image Size reports Memory, a warp, trace, wand or sample fails) instead of
reading a null pointer or substituting transparency. Identity is the storage itself, sparse or not.

Concurrent readers of separate `Raster` handles sharing the same storage MUST see identical immutable pixels.
The assembled byte pointer is published once with release ordering and read with acquire ordering. `Data()`
serializes first assembly with a per-storage mutex and checks the pointer again after acquiring it; failed
allocation leaves it unpublished and retryable. `ReadRegion`, `ByteSize`, `IsMaterialized` and thumbnails inspect
only the published pointer and immutable metadata, never the vector being built. Region reads of nested sparse
tile windows recursively copy only the requested pixels into the caller's existing rows; they do not call `Data()`,
allocate or acquire an assembly/residency gate. Tile source offsets, fills, later-window overrides and destination
padding keep their existing meanings. Explicit `Data()` still publishes the nested caches before its outer cache,
with unchanged pixels and identity. Publication does not change
identity or invalidate a previously returned pixel pointer. A caller retains its `Raster` for the lifetime of
any borrowed pointer. Concurrent assignment to the same handle or mutation of a document/session is not supported.

`PixelBuffer::TryAllocate` provides uniquely owned, uninitialized memory aligned to 64 bytes; zero size, overflow
or allocation failure returns an empty buffer. It uses the CRT aligned allocator so the normal ASan allocation
checks also cover large buffers. `Raster::Adopt` accepts only a complete, tightly packed buffer of exactly
width × height × bytes-per-pixel bytes and transfers it without a pixel copy. Invalid dimensions, format or size
leave ownership with the caller. A successful adoption empties the caller's buffer; writers MUST discard borrowed
mutable pointers before publication. `Raster::Make` retains its copying/zero-filling semantics for existing callers.

An admitted producer may give `Raster::Adopt` an opaque shared publication lifetime. The storage and its
control-block allocator retain that lifetime independently: actual pixel destruction on the release worker
precedes storage lifetime release, and the allocator retains admission until its last weak control block is
deallocated. A null alias with an owning control block retains the same lifetime. Copied Raster handles preserve
both pixels and accounting; invalid adoption consumes neither the pixel buffer nor caller lifetime ownership.
This creates no new byte reservation and grants no job, document or graphics publication authority. The caller
must declare all output/control overlap before allocation and bind original accounting before refunding its job.
The ordinary storage layout/factory remains unchanged. Charged storage adds two shared lifetime handles (32 bytes
on supported 64-bit targets), included in its cached self/graph retirement bounds, with no pixel copy or extra
pixel allocation. Its last owner-thread release always uses an installed deferred-release sink; workers release
inline. `WeakStorageForTest` observes control-block expiry without owning or reading dead pixels.

CopyPixelsInput may carry an explicitly supplied opaque output lifetime. Newly allocated Copy pixels
retain it in their Raster storage and control-block allocator, never implicitly inheriting source
accounting. The ordinary empty-lifetime path retains its numeric behavior and storage layout.
Closing a document preserves its private clipboard for later Paste; a later successful private Copy
replaces that clipboard. Those operations cannot refund pixels still owned by another copied Raster
or weak storage control. Final application CPU retirement also releases the private clipboard.

Large last-reference storage releases can be transferred through the owning thread's deferred-release sink.
The intrusive node remains alive until its worker destroys it; workers with no sink release inline. Directly
owned `PixelBuffer` results follow the same rule, including unpublished/cancelled results. Shutdown retains the
sink through producer join and owner wrapper/resource detachment, then clears it before final worker retirement
and scheduler teardown. Sparse bases and tiles retain independent
shared ownership; dropping a wrapper never frees a still-referenced base or tile.

Production pixel moves may retain one opaque immutable opening with source, selection, coverage and lifted pixels.
It owns each identity it uses for validation, including the selection's antialiasing and source grid/placement.
Offset and duplicate intent remain per-request values. Fresh private worker strokes install only these immutable
owners; mutable tiles, landing indexes, callbacks, cancellation and GPU engines are never shared between jobs.
The opening is session-only, outside history and the project schema. Its complete deferred-release node caches
a conservative ownership bound so last-reference retirement does not traverse or materialize its rasters.

Each newly constructed raster caches a conservative bound for its complete held storage graph and node count.
The capacity bound includes its possible contiguous sparse assembly, storage/control overhead, tile capacity
and the cached bounds of directly held base/tile owners. It sums occurrences, so repeated shared owners can be
overcounted. Bound reads are constant-time and never assemble pixels or recursively expand a shared graph.
An overflow or graph depth above 128 produces an unavailable bound (zero), which migrated whole-operation
admission refuses. These immutable construction caches are separate from history's retained-byte accounting;
they do not change pixel equality, history trim goldens or the raster's published assembly lifetime.

`GridReplacementWorkspaceBytes` additionally caches the clipped-window splitting allowance used by one uniform
256-pixel replacement grid. For each kept window it sums
`(ceil(width/256)+2)*(ceil(height/256)+2)*8*sizeof(RasterTile)` with checked arithmetic. Empty and contiguous
rasters return zero; unavailable sparse metadata returns absence and admission MUST refuse it. The immutable
query allocates nothing and visits no windows. This allowance does not cover arbitrary overlapping rectangles,
new pixel buffers, full source/result ownership or other operation peaks.

Basic raster-edit jobs retain input/token/result handles in one aggregate deferred-release owner. A stale or
canceled sparse frame can consist of many individually small tiles; retirement MUST NOT free that entire graph
on the UI thread merely because each tile is below the per-storage threshold. Worker accounting deduplicates
reachable raster identities and includes allocation-rounded contiguous pixel bounds, tile-vector capacities, conservative storage overhead,
names and retained selection capacity. This retirement bound includes held immutable sources and may overcount
sources still referenced by the live document; it never copies their pixels. Unaccounted allocation-failure
paths remain eligible for aggregate deferral. Clearing the sink only after joined owner cleanup preserves the
final worker drain and prevents any retired node from outliving its consumer.

`PixelBuffer` uses a separately allocated 64-byte ownership record and aligned CRT pixels. Requested size is
distinct from rounded capacity; buffers of at least 16 MiB use 16 MiB classes for bounded recycling. Consumers
see only the requested size. `RequiredBytes` provides the complete allocation estimate for memory admission.
ASan poisons cached payloads and unused capacity; reuse unpoisons only the requested bytes. A detached pool never
owns an outstanding buffer, so closing the pool while an image remains alive preserves that image's lifetime.

`LayerTransform` matches archived behavioral reference: origin, size, clockwise rotation
degrees, flipX/flipY, sampling. Valid when all values are finite, size is 1…300 000 per side, and
`abs(origin) ≤ 1 000 000`. Its geometry helpers (`Point`, `Contains`, `Corners`, `Following`, `Mirrored`, …), the
`Affine2` map and the session's transform editing are specified in [`Document_Tools.md`](Document_Tools.md).

`LayerMask` is Gray8 coverage (white reveals). Optional `placement` (the mask moved apart from its layer); `linked`
defaults true. `MaskTransform(layer)` is where the mask pixels sit: the placement, else the layer transform.
`MaskEdgeBackground(thumbnail)` is what a placed mask shows beyond its pixels: 1 (reveal) when at least half of the
thumbnail's edge intensity is white, else 0; a missing or non-gray thumbnail reveals (legacy `LayerMask.background`).
Folder masks are allowed from format v6.

`LayerBlendMode` is the fourteen-mode set: Normal, Multiply, Screen, Overlay, Darken, Lighten, Difference,
Color Dodge, Color Burn, Hue, Saturation, Color, Luminosity.

`LayerAdjustment` mirrors the Swift record: kind, legacy HSV scalars, optional `hsvSettings`, `levels`, `curves`,
optional `exposureSettings`, `gradientMapSettings`, `grainSettings`. `IsValid` matches the Swift field gates (HSV
ranges, four normalized level ranges, curves 2…32 strictly increasing x from 0 to 255, exposure/gradient-map/grain
ranges). Adjustment layers apply live in the composition and the Image menu applies the same kinds to a layer's
pixels (C12, [`Document_Tools.md`](Document_Tools.md), [`Rendering_Composition.md`](../Rendering/Rendering_Composition.md)).

### Direct distortion source lifetime

`DistortWarp::Warp` and `WarpMask` retain a contiguous sampling owner for their complete read. An already
published contiguous source may be shared; an unmaterialized sparse source is copied privately with the
bounded worker raster reader. The source identity, nested caches and residency epoch are not changed by that
private read. The uniform 1x1 mask shortcut retains its exact source identity without an assembly. Format,
corner, sampling and output-byte rules are unchanged. Explicit caller `Data()` keeps its separate publication
contract; this does not activate an asynchronous caller or establish whole-operation/G1 bounds.

## Hierarchy and clipping

`ValidateHierarchy` rejects duplicate ids, groups that own an image, missing/non-group parents, and cycles.
`ValidateLiveMaskGraph` rejects duplicate ids, cycles in `maskSourceID`, clipping a group, clipping to a
missing/group/adjustment source. `maskSourceID` requires format ≥5. Groups/`parentID` require format ≥2.

`ValidateDocument` implements `ProjectStore.validate` against the live graph and format version, then hierarchy
and live-mask graph. Mask records may exist without pixels while a project loads (assets attach after the manifest
validates); the Gray8 format rule applies once a mask has storage, and the decoder enforces it on the way in.

`HierarchyEntries` lists rows depth-first with inherited visibility; a collapsed group (in the caller's collapsed
set) lists itself with `collapsed = true` and hides its subtree. A row carries its layer's mask state for the layers
panel's badge (`hasMask`, and `maskEnabled`, true without a mask; RF-907). `RenderLayerIds` is the visible, non-group
subset in bottom-to-top order.

Collapsed IDs are session-only panel state, outside document equality, the project format and history. The
read-only set has a new bound of 10,000 IDs independently of live layer count, including remembered deleted
folders. Adding at capacity is ignored; removal remains allowed. Explicit `SetGroupCollapsed` and
`ClearCollapsedGroups` advance a separate monotonic `CollapseRevision` only on membership change. Clearing or
replacement that discards a nonempty set also advances it. No-op requests leave document generation and history
unchanged. Exhaustion invalidates this authority rather than wrapping. Returning to an earlier collapsed set
cannot authorize a delayed prepared result. Copied/moved sessions renew their document read authority, so even
equal roots, IDs and counters cannot authorize another session's row publication.

## Session edits

### Mutation ownership and result tokens

The UI thread owns `DocumentSession`. `Document()` and `ActiveLayer()` expose const views only. Production code
mutates through session operations, including `ApplyImportedDocument` for a validated same-document import and
`PreviewLayerOpacity` for the typed opacity preview/restore. Import installation checks the full document graph
and pixel budgets before replacing any state. Loaded replacements and size results also validate before install.
`EditForTest(fn)` is a scoped fixture hook for tests and the application harness; the callback receives a document
reference that MUST NOT escape. It invalidates the generation before the callback, including if construction fails.

Settled Rename, visibility, opacity button/key commits and blend-mode commands use sealed `LayerMetadataInput`.
Capture follows full job admission and binds the saved target id, settled session authority, whole-document token
and immutable history view. Workers construct and validate the complete after-document and named history entry.
The owner applies only a current result through nonthrowing document/display/history swaps; the result retains
all replaced owners for worker retirement. Rename uses its card's original target even when another layer is
active, trims whitespace and ignores blank/unchanged/missing targets. Groups permit rename/visibility and ignore
opacity/blend. A true no-op leaves generation, revision and redo unchanged. Typed opacity preview/closure remains
a separate session path. An open brush, warp, shape, lasso/outline gesture or outer history transaction retains
the reference command route and its gesture/nested history behavior until preserving worker authority is added;
it must not become a rejected edit merely because the settled worker input is ineligible.
Native capture still copies vector records until the H2.2 document root integration;
this worker boundary does not close capture, admission traversal or measured G1.

Final application shutdown has one exclusive retirement boundary after all producers have joined. The owner
calls `DetachCoverageForShutdown`, severing borrowed graphics engines without destroying the CPU drafts.
The scheduler's final MTA callback then calls `RetireCpuForShutdown` to release document, display, history, drafts,
private clipboard and other large CPU roots without constructing a replacement session. No UI read or mutation
may overlap that callback. This is destruction only; it publishes no edit or completion. Empty selected/collapsed
hash bucket capacity remains until host destruction; no general concurrent session access is supported.

`Generation()` is a session-local monotonic `uint64_t` mutation stamp, distinct from the history revision and the
display-preview revision. Every `EndEdit` advances it, even inside an outer history transaction. Restore, document
replacement/clear, live adjustment changes, typed opacity previews/restores, outline moves/cancels, and insertion
of a floating selection advance it before returning. It may advance for a no-op transaction; callers test equality,
never a count of edits. Pure reads, active-target changes and drafts that leave the document untouched do not advance
it. A new document, clear or replacement advances `DocumentSerial()`, including reopening the same persisted id.
Undo/Redo across document ids also advances that serial. Every history restore advances a separate restore serial
so Undo/Redo cannot revive a previously captured result even when its raster handles return.

`CaptureJobToken` owns the captured image and complete mask record, including its raster, transform, linking and
enablement, plus immutable selection handles. No cache/result identity is just a borrowed storage address.
`CanApplyJobResult(token, latestSequence)` requires the capturing session identity, the same document and restore
serials and the caller's current
sequence. A whole-document token also requires the same generation. A layer token requires the target to exist
with the same image, transform, mask, layer kind/adjustment and selection handles, on the same canvas dimensions;
an unrelated layer's rename does not invalidate it.
Selection validation compares immutable handle identity and antialiasing in constant time, conservatively refusing
a separately rebuilt outline. Copies and moves receive a new session identity; a move also retires the source's
identity. Tokens cannot cross sessions even when every counter and document field matches. A scheduler caller must additionally
check cancellation and whether a preview's edit is still open; these token helpers do not run jobs or apply pixels.

`ApplyDocumentSizeIfCurrent` is the atomic owner boundary for a worker-prepared resized document. It requires a
current whole-document token, the same document id and a valid graph before clearing outline drafts and applying
one named history transaction. Refused stale/invalid input changes neither the document nor its history/active
layer. The host additionally checks its exact history acquisition and size-card invocation. Direct Crop retains
`ApplyDocumentSize`; size arithmetic and cancellation are specified in `Document_Tools.md`.

### Immutable selection snapshots

`DocumentSelection::loops` is a `SelectionLoops` value backed by `shared_ptr<const vector<SelectionLoop>>`.
Copying a document, history snapshot, pending edit or token shares its points. Readers get only const vectors,
loops and points; a geometry operation builds a new vector and publishes it as a new value. Empty outlines need
no heap allocation. Selection equality keeps its previous value semantics (antialiasing and all points), with a
fast path for shared storage, so equal geometry preserves redo. Flipping, translating or distorting a selection
must leave earlier snapshots untouched. History counts retained immutable selection capacity by held storage
identity, excluding the live document's identity. Deferred destruction remains subsequent resource work in the
active performance plan.

The geometry producer caches its total point count and finite-point bounds. `PointCount()`, `Bounds()` and
`DocumentSelection::IsEmpty()` are constant-time for snapshot, panel and admission readers. Copies preserve the
cache with shared storage; a moved-from empty handle reports zero points and no bounds. Bounds retain the same
finite-point filtering and degenerate-selection rules as `LoopsBounds`. Geometry equality and vector access are
unchanged. This avoids outline traversal when opening a panel or reserving a hard-coverage worker's crossing array.
The same producer caches `StorageBytes()`: the vector object and retained outer/inner capacities, including spare
capacity, with opaque allocator/control-block overhead covered by global headroom. Copies and moves preserve
this cache; empty/moved-from handles report zero. History byte queries do not traverse the outline.

### Operations

`DocumentSession` ports the CPU edits used by the legacy group/history/appearance/layer tests:

- New canvas (rejects invalid sizes without touching the document), blank layer, folder: names are the lowest
  unused `Layer N` / `Folder N`; a new layer goes directly above the active layer, or on top of the active folder's
  contents, and inherits the active layer's parent.
- Rename (trimmed, blank ignored), visibility, place/move (`CanPlaceLayer` refuses self and descendants; `before`
  inserts in front of the named sibling, which is visually below it in the top-first list — the layers drag uses
  this), move out of
  group, delete (a folder deletes its descendants; clips onto deleted sources are released; the next neighbor
  becomes active, or nothing when the document empties; the canvas size is kept).
- Opacity/blend (groups skipped), clipping link (`CanLinkMask` runs the graph validation on a copy).
- Move up/down (`MoveActiveLayer(±1)`, legacy `EditorSession.moveActiveLayer`): the active layer swaps places with
  its next sibling in the same parent as one `Reorder Layers` entry; `CanMoveActiveLayer` says whether that sibling
  exists. Group the selection (`GroupSelectedLayers`, legacy `LayerGroups.groupSelectedLayers`): the selected roots
  (a selected folder carries its subtree) move into a new `Folder N` placed at the topmost selected branch of their
  common parent, an empty selection makes an empty folder, the folder becomes active and its parent uncollapses;
  one `Group Layers` entry, refused when the hierarchy would not validate. Clipping toggle (`ToggleClippingMask`,
  legacy `LiveLayerMask.toggleClippingMask`): a clipped layer releases (`Release Clipping Mask`) together with the
  run of clipped layers stacked on it that share its base, the ones below staying clipped (legacy
  `removeLiveMask`); otherwise a non-folder layer clips to the sibling below it, sharing that sibling's base when it
  is clipped itself (`Create Clipping Mask`); `CanToggleClippingMask` mirrors the legacy gate.
- Merge (`PlanMerge`, `MergeLayers`, legacy `LayerMerge.swift`): the plan lists the layers to merge in stacking
  order, the ids that go, the result's name (the lower layer's, the folder's or the top selected one's), its
  parent, the anchor whose slot it takes and the action name (`Merge Down`, `Merge Group`, `Merge Layers`); no plan
  for the bottom layer of a folder, a folder without pixel layers or a selection without one. `MergeLayers(plan,
  composite)` takes the document-sized composite the host rendered, trims it to its pixels (`TrimmedToAlpha`),
  places the result at the anchor's slot with clips onto merged layers re-targeted, validates the hierarchy and
  the clip graph, and records one entry named by the plan; a plan whose layers are gone is refused.
  The asynchronous host uses worker-safe `PrepareMerge(plan, composite, cancellation)` to create the trimmed image;
  an image thumbnail is optional derived state and is not required for merge preparation. Cancellation reaches the
  trim scan and crop; a cancelled preparation publishes no result.
  Its privately constructed `PreparedMerge` owns the source plan and canvas dimensions.
  `ApplyPreparedMerge` checks those dimensions, plan metadata, live ids, pixel budget and resulting graphs before
  the same history insertion/clip repair. It refuses an unsettled gradient; the selected host continuation
  settles earlier edits before taking the snapshot and validates its complete document token before apply.
  Production merge first admits a metadata-only record, captures immutable source/active/selected owners and
  plans on its worker. A missing plan succeeds without canvas pixel allocation or GPU work. A ready plan retains
  that source and worker-built composition subset while a separate full readback/history record retains the same
  history acquisition. The probe aggregate is allocated on its original admitted Compute opening, before
  owner capture; its original accounting survives the complete handoff and aggregate/control retirement.
  The dependent readback refuses insufficient memory once: its owned probe cannot free while that phase
  retains it. Ordinary independent admissions keep event-driven waiting. History capture follows the complete
  readback reservation. `PrepareMergePublication` builds the
  survivor records, clipping rewrites, the trimmed layer, active/selected ids and detached history before
  either live state changes. Newly allocated crop pixels explicitly retain the readback's original lifetime
  owner, including its charged metadata in admission. Full-bounds/transparent trims preserve source identity;
  crop never implicitly inherits another source job's accounting owner. `ApplyPreparedMergeIfCurrent` validates whole document token, active id, settled
  edit state and exact history epoch, then publishes and swaps replaced document/selection/display owners.
  Failure/cancellation/staleness preserves the existing document, revision and redo. The original direct methods
  remain numeric references. This integration is undergoing native qualification under P1.9 H1.
- `ClearDocument` drops the document, the active layer and selection, the collapsed set, every draft and pending
  edit, and the history (marked saved): the state after Close Project. `CreateDocument` starts the next one.
  `ReplaceDocument(document, active)` (File › Open, a new project from images) drops what `ClearDocument` drops and
  installs the document unmodified, `active` (when it names a layer) the only selected layer with its pixels, not
  its mask, targeted. Capture the requested active ID before clearing the old session: `active` may reference
  `ActiveLayerID()` on that same session. Replacement starts a fresh document lifetime even when its persisted
  document and layer IDs match the old ones, and leaves history marked saved with no undo entries.
- The active layer changes in one place (legacy `EditorSession.activeLayerID` didSet): every edit that makes another
  layer active (a new layer, folder, shape, paste or duplicate, a moved layer, a group, a merge, a deletion, the
  floating selection, Undo and Redo) drops the mask target and makes the selected set that layer alone, so a new
  layer paints and `GroupSelectedLayers` / the Move tool act on it; a mask stays targeted only while the active
  layer has one.
- Nested `BeginEdit`/`EndEdit` follow `DocumentHistory`; every edit is one undo step.

## Validation

`Tests/DocumentTests` MUST cover cycles, missing parents, raster-as-parent, group+image, group opacity/blend, v1
groups, v4/v6 masks, adjustment-on-group, clipping cycles, canvas/name limits, mask placement helpers, collapsed
rows and their mask state, clip release on delete, the legacy `LiveMaskTests` shared clipping stack (`clip.stack-*`:
releasing the top layer leaves the one below clipped, releasing the lower one releases both, a layer dragged below
its base stops clipping), the active-layer rules (`active.*`), the document pixel budget (`docbudget.*`: a paste, a
duplicate, a blur's grown grid, the floating merge, masks, a merge and a distortion refused past 100 M while an edit
that fits goes through), the legacy insertion/deletion/naming semantics, and sparse rasters (windows, fill,
`Replacing` splits and fills, `ReadRegion`, thumbnails, lazy `Data()`; C10 `sparse.*`).

`DocumentTests` also covers an empty optional image thumbnail, explicit thumbnail derivation without changing image
pixels or image-layer equality, and eager Gray8 semantic mask thumbnails with the existing `MaskEdgeBackground`
result. It races eight event-gated readers through first assembly, whole-buffer reads, padded region reads,
thumbnails and byte accounting for Gray8 and BGRA8888, on one sparse storage and on two sparse storages sharing a
base. Expected bytes are independently computed from the base offset, tile window and fill values.
Every materializer of a storage must receive the same stable pointer; separate caches remain distinct. The
normal suite runs eight rounds; `DocumentTests.exe --stress` runs 256 rounds and MUST pass in ASan Debug before
the concurrency foundation closes. This is stress coverage of ownership and publication, not a race detector.

The `buffer.*` cases prove alignment, empty and overflowing allocation requests, move ownership, invalid adoption
without consumption, zero-copy publication, shared lifetime and an adopted buffer used as a sparse base.

The `generation.*` cases prove that mutating session operations invalidate whole-document tokens, including nested
transactions, typed opacity, live adjustment editing, outline movement and floating/duplicate transforms. They
cover Undo/Redo, same-id reopen, stale sequences, image/transform/mask/selection changes, retained input lifetime,
refused invalid documents, and shared immutable selection snapshots with unchanged geometry/redo semantics.
Compile-time assertions require const document/layer readers and non-throwing selection-handle copies.

```powershell
.\test.ps1 -Platform x64 -Configuration Debug
.\test.ps1 -Platform x64 -Configuration "ASan Debug"
```

## Immutable worker geometry adoption

`SelectionLoops::AdoptForWork` adopts worker-owned loop/point vector buffers without copying points. It
computes cached point count, capacity bytes and finite-point bounds with cancellation checks every64 work
units. Bounds ignore non-finite points, as the existing geometry reference does; point count still includes
them. Cancellation returns no published handle. Empty geometry retains no allocation or opaque lifetime.
The caller admits vector/control/ownership bounds before allocation and explicitly supplies a charged output
lifetime when needed. `WorkOwnershipAllowance` covers the supported node, control block and per-vector
bookkeeping in addition to cached vector capacity bytes; zero refuses overflow. It is an admission allowance,
not a physical-residency measurement.

Copied/moved readers share one immutable identity and cached metadata. On the scheduler owner, the final
strong geometry release queues the complete node for worker destruction regardless of its size. Geometry
buffers die before their opaque lifetime. A surviving weak storage control also retains that lifetime until
its own control block dies. This does not change synchronous selection arithmetic or activate a worker
boolean caller by itself. See the separate phase-retention contract in `Core_PerformanceAndResources.md`.

## Preserving marquee source metadata

Opted-in preserving marquee preparation uses the original immutable selection's cached point/loop counts
for its admitted plan. The drawing payload retains that exact plan while holding the corresponding source
root. A worker cannot substitute the default zero-source limits or mutate/materialize the shared source.
Output and allocator/control owners remain charged through their final strong and weak reader release.
This does not change the synchronous reference API or enable ordinary host activation.
