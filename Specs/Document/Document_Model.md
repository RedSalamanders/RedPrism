# Document model

Status: current normative product contract
Last reviewed: 2026-09-23
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
set) lists itself with `collapsed = true` and hides its subtree. `RenderLayerIds` is the visible, non-group subset in
bottom-to-top order.

## Session edits

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
rows, clip release on delete, the legacy `LiveMaskTests` shared clipping stack (`clip.stack-*`: releasing the top
layer leaves the one below clipped, releasing the lower one releases both, a layer dragged below its base stops
clipping), the active-layer rules (`active.*`), the document pixel budget (`docbudget.*`: a paste, a duplicate, a
blur's grown grid, the floating merge, masks, a merge and a distortion refused past 100 M while an edit that fits
goes through), the legacy insertion/deletion/naming semantics, and sparse rasters (windows, fill, `Replacing`
splits and fills, `ReadRegion`, thumbnails, lazy `Data()`; C10 `sparse.*`).

```powershell
.\test.ps1 -Platform x64 -Configuration Debug
.\test.ps1 -Platform x64 -Configuration "ASan Debug"
```
