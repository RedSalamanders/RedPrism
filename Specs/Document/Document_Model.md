# Document model

Status: current normative product contract
Last reviewed: 2026-09-27
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
file. Adjustment layers (v7) are not groups and have no image file. A shape layer also keeps `shapeImage`, the
raster its shape drew, outside equality: while the asset still holds it the layer is a live shape (`LiveShape`) that
redraws at a new size (C11).

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
only the published pointer and immutable metadata, never the vector being built. Publication does not change
identity or invalidate a previously returned pixel pointer. A caller retains its `Raster` for the lifetime of
any borrowed pointer. Concurrent assignment to the same handle or mutation of a document/session is not supported.

`PixelBuffer::TryAllocate` provides uniquely owned, uninitialized memory aligned to 64 bytes; zero size, overflow
or allocation failure returns an empty buffer. It uses the CRT aligned allocator so the normal ASan allocation
checks also cover large buffers. `Raster::Adopt` accepts only a complete, tightly packed buffer of exactly
width × height × bytes-per-pixel bytes and transfers it without a pixel copy. Invalid dimensions, format or size
leave ownership with the caller. A successful adoption empties the caller's buffer; writers MUST discard borrowed
mutable pointers before publication. `Raster::Make` retains its copying/zero-filling semantics for existing callers.

Large last-reference storage releases can be transferred through the owning thread's deferred-release sink.
The intrusive node remains alive until its worker destroys it; workers with no sink release inline. Directly
owned `PixelBuffer` results follow the same rule, including unpublished/cancelled results. Clearing the sink
before joining/draining prevents callbacks into a destroyed scheduler. Sparse bases and tiles retain independent
shared ownership; dropping a wrapper never frees a still-referenced base or tile.

Basic raster-edit jobs retain input/token/result handles in one aggregate deferred-release owner. A stale or
canceled sparse frame can consist of many individually small tiles; retirement MUST NOT free that entire graph
on the UI thread merely because each tile is below the per-storage threshold. Worker accounting deduplicates
reachable raster identities and includes allocation-rounded contiguous pixel bounds, tile-vector capacities, conservative storage overhead,
names and retained selection capacity. This retirement bound includes held immutable sources and may overcount
sources still referenced by the live document; it never copies their pixels. Unaccounted allocation-failure
paths remain eligible for aggregate deferral. Clearing the sink before shutdown join preserves the existing
join/drain ownership rule.

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

## Session edits

### Mutation ownership and result tokens

The UI thread owns `DocumentSession`. `Document()` and `ActiveLayer()` expose const views only. Production code
mutates through session operations, including `ApplyImportedDocument` for a validated same-document import and
`PreviewLayerOpacity` for the typed opacity preview/restore. Import installation checks the full document graph
and pixel budgets before replacing any state. Loaded replacements and size results also validate before install.
`EditForTest(fn)` is a scoped fixture hook for tests and the application harness; the callback receives a document
reference that MUST NOT escape. It invalidates the generation before the callback, including if construction fails.

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
  The asynchronous host uses worker-safe `PrepareMerge(plan, composite, cancellation)` to create the trimmed image and eager
  thumbnail. Cancellation reaches the trim scan and crop; a cancelled preparation publishes no result.
  Its privately constructed `PreparedMerge` owns the source plan and canvas dimensions.
  `ApplyPreparedMerge` checks those dimensions, plan metadata, live ids, pixel budget and resulting graphs before
  the same history insertion/clip repair. It refuses an unsettled gradient; the selected host continuation
  settles earlier edits before taking the snapshot and validates its complete document token before apply.
- `ClearDocument` drops the document, the active layer and selection, the collapsed set, every draft and pending
  edit, and the history (marked saved): the state after Close Project. `CreateDocument` starts the next one.
  `ReplaceDocument(document, active)` (File › Open, a new project from images) drops what `ClearDocument` drops and
  installs the document unmodified, `active` (when it names a layer) the only selected layer with its pixels, not
  its mask, targeted.
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

`DocumentTests` also races eight event-gated readers through first assembly, whole-buffer reads, padded region
reads, thumbnails and byte accounting for Gray8 and BGRA8888, on one sparse storage and on two sparse storages
sharing a base. Expected bytes are independently computed from the base offset, tile window and fill values.
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
