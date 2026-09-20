# Document model

Status: current normative product contract
Last reviewed: 2026-09-20
Owner: `Common/Document.h`, `Common/Document.cpp`

## Scope

This contract owns the CPU document: canvas size, the bottom-to-top layer list, group/`parentID` rules, live
clipping (`maskSourceID`), raster masks, adjustment and shape metadata, and the session-only selection.
GPU compositing is C6. ZIP `.prism` I/O is C5. History is [`Document_History.md`](Document_History.md).

The requirements below and `ProjectStore` validation own document behavior.

## Limits (identical to `ProjectStore`)

| Rule | Value |
| --- | --- |
| Canvas side | 1…30 000 px |
| Layers | ≤10 000 |
| Image pixels (sum) | ≤100 M |
| Mask pixels (sum) | ≤100 M |
| Working space | sRGB |
| Format versions | 1–7 (current save is 7) |
| Layer name | non-empty after trim, ≤16 384 UTF-8 bytes |
| Group depth | ≤64 ancestors |
| Group appearance | opacity 1 and blend Normal (on disk and in the live graph) |
| Manifest (C5) | ≤4 MiB; this phase validates the graph, not JSON |

## Types

`CanvasDocument` holds `id`, integer width/height, `resolution` (default 72, finite 1…9600 when set), layers
bottom-to-top, and optional `DocumentSelection`. Selection is part of document equality so undo covers it. It is
not saved. `nil` means no selection; an empty outline means “touch nothing.”

`ImageLayer` is a value type: `Guid` id, optional shared `Raster` asset, `LayerTransform`, name, visibility,
optional `parentID`, `isGroup`, opacity 0…1, `LayerBlendMode`, optional `LayerMask`, optional `maskSourceID`,
optional `LayerAdjustment`, optional `LayerShapeStyle`. Blank layers allocate no pixels. Groups have no image
file. Adjustment layers (v7) are not groups and have no image file.

`Raster` is immutable shared storage (`std::shared_ptr`). Equality of image pixels is identity of that storage,
matching Swift `CGImage ===`. Formats: BGRA8888 (premultiplied, alpha last in memory as R,G,B,A byte order is
not required until C5/C6 goldens) and Gray8 masks.

`LayerTransform` matches archived behavioral reference: origin, size, clockwise rotation
degrees, flipX/flipY, sampling. Valid when all values are finite, size is 1…300 000 per side, and
`abs(origin) ≤ 1 000 000`.

`LayerMask` is Gray8 coverage (white reveals). Optional `placement` (unlinked or moved apart); `linked` defaults
true. Folder masks are allowed from format v6.

`LayerBlendMode` is the fourteen-mode set: Normal, Multiply, Screen, Overlay, Darken, Lighten, Difference,
Color Dodge, Color Burn, Hue, Saturation, Color, Luminosity.

`LayerAdjustment::IsValid` matches the Swift field gates (HSV ranges, four normalized level ranges, curves
2…32 strictly increasing x from 0 to 255, exposure/gradient-map/grain ranges). Applying filters is C12.

## Hierarchy and clipping

`ValidateHierarchy` rejects duplicate ids, groups that own an image, missing/non-group parents, and cycles.
`ValidateLiveMaskGraph` rejects duplicate ids, cycles in `maskSourceID`, clipping a group, clipping to a
missing/group/adjustment source. `maskSourceID` requires format ≥5. Groups/`parentID` require format ≥2.

`DocumentSession` ports the CPU edits used by group/history/appearance tests: new canvas, blank layer, folder,
rename, visibility, place/move/delete (folder deletes descendants and clears stale clipping), opacity/blend
(groups skipped), clipping link. Nested `BeginEdit`/`EndEdit` follow `DocumentHistory`.

## Validation

`ValidateDocument` implements `ProjectStore.validate` against the live graph and format version, then hierarchy
and live-mask graph. `Tests/DocumentTests` MUST cover cycles, missing parents, raster-as-parent, group+image,
group opacity/blend, v1 groups, v4/v6 masks, adjustment-on-group, clipping cycles, and canvas/name limits.

```powershell
.\test.ps1 -Platform x64 -Configuration Debug
.\test.ps1 -Platform x64 -Configuration "ASan Debug"
```
