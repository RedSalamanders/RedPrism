# Document model

Status: current normative product contract
Last reviewed: 2026-09-21
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
neither reaches. `MakeSparse` builds one; `Replacing(source, tiles, crop)` writes tiles of a working grid over a
source and crops the result, splitting older tiles around newer ones so the list stays disjoint while the base and
every untouched tile stay shared; `Crop` copies a region; `ReadRegion` samples any raster without assembling it
(thumbnails use it); `ForEachStorage` visits the base and every tile once (history counts bytes that way).
`Data()` assembles a contiguous copy on first use and keeps it (`IsMaterialized`); consumers that can read tiles
(the composition, the brush, thumbnails) MUST NOT call it. Identity is the storage itself, sparse or not.

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
ranges). Applying filters is C12.

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
- Rename (trimmed, blank ignored), visibility, place/move (`CanPlaceLayer` refuses self and descendants), move out of
  group, delete (a folder deletes its descendants; clips onto deleted sources are released; the next neighbor
  becomes active, or nothing when the document empties; the canvas size is kept).
- Opacity/blend (groups skipped), clipping link (`CanLinkMask` runs the graph validation on a copy).
- Nested `BeginEdit`/`EndEdit` follow `DocumentHistory`; every edit is one undo step.

## Validation

`Tests/DocumentTests` MUST cover cycles, missing parents, raster-as-parent, group+image, group opacity/blend, v1
groups, v4/v6 masks, adjustment-on-group, clipping cycles, canvas/name limits, mask placement helpers, collapsed
rows, clip release on delete, the legacy insertion/deletion/naming semantics, and sparse rasters (windows, fill,
`Replacing` splits, `ReadRegion`, thumbnails, lazy `Data()`; C10 `sparse.*`).

```powershell
.\test.ps1 -Platform x64 -Configuration Debug
.\test.ps1 -Platform x64 -Configuration "ASan Debug"
```
