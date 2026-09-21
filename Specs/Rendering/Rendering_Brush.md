# Brush rendering

Status: current normative contract (created with C10)
Last reviewed: 2026-09-21
Owner: `Common/BrushStroke.*` (stroke model, CPU kernel, session brush API), `RedPrism/BrushCompute.*` and
`RedPrism/Shaders/BrushCoverage.hlsl` (compute engine), the sparse-raster and stroke-preview code in
`RedPrism/CanvasRenderer.*`, the brush code in `RedPrism/Application.cpp`

## Scope

This contract owns how a brush or eraser stroke turns pointer samples into layer pixels: the continuous-tip model,
the 256-pixel tile grid, the coverage engines (Direct3D 11 compute in the application, a CPU kernel everywhere
else), how a stroke commits without ever assembling the layer's pixels, how the composition shows a stroke while it
is drawn and adopts it afterwards, and the budgets and evidence that go with it. The header, keys and cursor live in
[`UI_ApplicationChrome.md`](../UI/UI_ApplicationChrome.md); the session API and history names in
[`Document_Tools.md`](../Document/Document_Tools.md); sparse rasters in
[`Document_Model.md`](../Document/Document_Model.md).

## Stroke model

A `BrushStroke` edits one layer's image, or its mask, on a pixel grid of its own:

- **Grid**: the layer's pixels (a mask on the layer's own grid uses the layer image's size, so a uniform 1×1 mask
  is painted at layer resolution; a mask with a placement uses its own pixels), grown to the smallest rectangle
  that also covers the canvas, so paint can leave the layer's rectangle and the layer grows with it. Grid pixel
  (x, y) maps to the document through `PixelToDocument` of the placing transform (`PaintTransform`); the grid is
  never larger than the layer plus the canvas, and nothing outside the touched tiles is allocated.
- **Samples**: the pointer path is smoothed by a centripetal Catmull-Rom curve through the last four samples,
  subdivided until it is straight to a fifth of a pixel (depth ≤ 10). Segments older than the newest pair are
  *settled*; the newest pair is the *provisional tail*, re-rendered from scratch on every sample so replacing it
  leaves nothing behind. `Flush` settles the tail (mouse-up); `Append` of a repeated point is a no-op.
- **Tip**: a round tip of `diameter` document pixels (1–2000), `hardness` 0 soft … 1 hard. A hard tip is the
  antialiased silhouette of the swept segment (edge width = one grid pixel in document units). A soft tip
  deposits optical density along the path at a spacing of 2.5 % of the diameter (1.5 % for hard tips, the same
  constants the legacy dab fallback used), integrated per segment (8-point Gauss–Legendre in the CPU kernel, the
  same sum in the shader), capped at a density of 20 and converted to coverage, so self-crossings and corners blend
  without ridges or creases and the result does not depend on the number of pointer events.
- **Opacity** caps the whole stroke: coverage never exceeds `opacity`, however often the path crosses itself.
- **Selection clip**: while a selection exists the stroke multiplies its coverage by the selection's coverage
  raster (antialiased or hard, `SelectionGeometry::Coverage`), sampled at the grid pixel's document position; an
  empty selection paints nothing.
- **Tile composition** (CPU, only the tiles the segments touch, 256 × 256 of the grid): paint is
  `color × coverage` over the tile's base pixels (the layer's pixels, or the fill beyond them), erase is
  `base × (1 − coverage)`, and a mask is painted toward white or black (`maskPaintWhite`) in Gray8. Each tile keeps
  its base, its settled density, its 0–255 preview coverage and its composed image; `TakeChanged` hands the host the
  tiles whose image changed since the last call.
- **Budget**: a stroke that would take the layer past the document's pixel limit (100 M image pixels and 100 M
  mask pixels across the document, `RasterEditPixelLimit`) fails at `Append`; the session drops it and the host
  reports `IDS_ERROR_BRUSH_BUDGET`. Tile allocation failure fails the same way.

## Coverage engines

`BrushCoverageEngine::Render(tiles, settled, tail, params)` fills each tile's permanent density (settled paint) and
preview coverage (settled plus tail). Two engines exist and MUST agree within 2/255 per pixel:

- `CpuBrushCoverage::Shared()` keeps the permanent density in the stroke's own float buffers. `DocumentTests`
  run on it; the application falls back to it when the compute pipeline cannot be created.
- `GpuBrushCoverage` (`BrushCoverage.hlsl`, `cs_5_0`, one thread per grid pixel in 16 × 16 groups, one dispatch
  per tile) keeps the permanent density on the GPU for the stroke's lifetime (a float structured buffer per tile),
  writes the preview into a `uint` structured buffer and reads only that back through a staging buffer. Segments
  go through one dynamic structured buffer (grown by doubling from 64), constants through `Map` `DISCARD` per tile.
  It borrows the application device, owns no canvas-sized allocation, and `EndStroke` releases the tile buffers.
  The shader compiles at build time (`FxCompile` in `RedPrism.vcxproj` and `CompositionTests.vcxproj`).

## Commit

Mouse-up (`DocumentSession::FinishBrush`) settles the tail, then installs one raster without assembling the layer:
`PaintSnapshot` finds the painted bounds (the alpha bounds of the touched tiles plus the original pixels; a mask
keeps its whole grid) and builds `Raster::Replacing(source, tiles, bounds)`, a sparse raster that shares the
layer's previous storage and every untouched tile. The layer's transform becomes `TransformFor(bounds)` (the same
placement, resized to the crop); the thumbnail is sampled through `ReadRegion`, never through `Data()`. A mask on
the layer's grid grows with the layer, revealing (255) the new area; a uniform mask stays uniform. The commit is
refused, and nothing recorded, when the layer's pixels or transform changed under the stroke. History names:
`Brush Stroke`, `Erase`, `Paint Mask`. Undo, Redo and the next stroke read the tiles directly; the first
`Data()` call (export, a filter) assembles a contiguous copy once and keeps it.

Shift-click paints a straight line from the end of the previous stroke on the same layer or mask
(`ShiftLineStart`); switching tools or layers, or a stroke on another target, forgets that point.

## Live preview in the composition

While a stroke is drawn the composition shows it without a document edit:

- `BeginStroke(StrokePreview)` names the layer (or its mask), the grid size, the grid's placing transform and the
  pixels the stroke started from (`source` at `sourceX, sourceY`; a uniform 1×1 mask becomes its one value as
  `fill`).
- `UpdateStrokeTiles(tiles)` writes changed tiles with `UpdateSubresource` into one working texture of the grid's
  size (BGRA8 for images, R8 for masks) and regenerates its mips. The texture is created on the first call from
  the fill, the source's own cached texture (copied on the GPU, never re-uploaded) and every touched tile; while
  `StrokeTextureMissing()` the host sends every touched tile, afterwards only `TakeChanged`.
- `Compose` draws the working texture in place of the layer's image (or as its mask) with the preview's transform,
  including for a layer that has no pixels yet; a tile update marks the document dirty, so the frame composites
  once per pointer update and never per frame otherwise.
- `EndStroke(committed, cropX, cropY)` adopts the working texture: the committed raster's crop is copied on the GPU
  (`CopySubresourceRegion`) into a raster-cache entry keyed by that raster's identity, so the first composition
  after mouse-up finds its texture and the committed raster is never materialized for upload. `EndStroke(nullptr)`
  drops the preview. `CanvasSurfaceBytes` counts the working texture; `ReleaseDocumentSurfaces` drops it.

Sparse rasters that reach `TextureFor` without an adopted texture (Undo, Redo, a reopened stroke) are assembled on
the GPU the same way (`CreateSparseTexture`: clear to the fill, copy the base's texture, upload the tiles), which
keeps the CPU copy unassembled; a device without render-target support for the format falls back to `Data()`.

## Retouch strokes (C11)

The same stroke composes from a *sample* when one is set (`SetSample`: a document-size premultiplied BGRA raster, or
Gray8 on a mask, read bilinearly at the grid pixel's document position shifted by a whole-pixel offset): Clone Stamp
paints the sample over the pixels under the tip through the coverage and opacity; Blur does the same with the layer
softened (`ImageFilters::GaussianBlur`: separable, radius three sigma, transparent beyond an image's edge and clamped
for a mask); a warp result (Liquify, Smudge) replaces the pixels under a hard tip. Outside the sample nothing is
copied. Spot Healing paints a dark wash (12 % gray at a fixed 45 %) while the stroke is drawn and, at mouse-up,
`Heal` runs the legacy kernel (`Common/Heal.*`) over the layer's original pixels within the painted bounds grown by
3.2 × (the spot's larger side + 32), with the stroke coverage as the mask, and writes the result into the tiles
within the selection before the usual commit. Because that rewrite follows the last preview update, the host does
not adopt the working texture for a healing stroke; the committed raster uploads on its own, as a warp's does (its
working copy lived on the document grid, not the layer's).

`PaintCanvas` fills tiles without segments: every tile the canvas and the selection's bounds cover starts from its
original content and takes a per-pixel shade at the pixel's document center, clipped to the canvas and the selection
coverage (`FillGradient` for the Gradient tool; `Fill`, `ClearPixels` and `Invert` for the pixel edits). Redrawing
restarts from the original, so a moved line never accumulates.

Smudge and Liquify work on `WarpStroke`, a document-size copy of the layer as shown, dab by dab (the weight follows
the hardness, at most 0.98, with a smoothstep rim); the host shows the copy through the stroke preview on the
document grid (256² tiles marked dirty per dab) and the session paints the result into the layer along the dabs.

## Pixel edits (C9b)

`PaintCanvas` takes a mode: `Over` (the shade over the pixels: fills, gradients), `DestinationOut` (clears through
the coverage) and `Invert` (premultiplied: each color becomes alpha − color, a mask 255 − value; without a selection
it covers every pixel the layer has, on or off the canvas). `Fill`, `ClearPixels` and `Invert` wrap them.

`LiftSelection` copies the layer's pixels under the selection out of the original, faded by the coverage;
`MoveLifted(offset, duplicate)` rebuilds every tile the pixels leave or land on (and any tile an earlier preview
changed) from the original: the selection becomes a hole (the pixels times one minus the coverage, unless
duplicating) and the lifted pixels land `offset` document pixels away, source-over (exact for whole pixels on an
unscaled layer, bilinear otherwise), so a preview never accumulates and the pixels along a soft edge add back to the
original. The session's `PixelMove` keeps the raster; the host shows it through the stroke preview like a gradient
(`BeginStroke` from `PreviewFor`, the changed tiles per pointer update) and the commit adopts the working texture,
so a move never re-uploads the layer per pointer update. The session's other edits (fills, Clear, Invert, nudges)
commit at once and upload through the sparse path.

## Budgets

| Budget | Limit |
| --- | --- |
| Tile | 256 × 256 grid pixels; only touched tiles allocate (CPU image + coverage, GPU density + preview) |
| Mouse-up | MUST NOT materialize a contiguous bitmap: `IsMaterialized()` stays false through commit, thumbnail, display and the next stroke |
| History | A stroke retains only the tiles it replaced plus the previous thumbnail (`RetainedBytes`), never the shared tiles; the 256 MiB history cap applies |
| Pixel limit | 100 M image / 100 M mask pixels per document, checked before a stroke grows the layer |
| Preview upload | Changed tiles only; never a full-canvas upload per pointer update |
| Frame | One composition per pointer update (the tile update dirties the document); a settled document composites 0 times |

Performance evidence is recorded per run under `Specs/TestRuns/` (`--bench brush`, see
[`Brush_2026-09-21.md`](../TestRuns/Brush_2026-09-21.md)): the legacy scenario of a 4000 × 4000 document, 40 px and
800 px soft white strokes of 120 updates 40 pixels apart, blank and opaque layers, two passes each. `update` is the
pointer update alone (curve, kernel, readback, tile composition and upload); `frame` adds the composition, blit and
Present, which waits for the display's refresh; `commit` is mouse-up without the following frame. Numbers are
compared with archived brush benchmark notes as an order of magnitude, not as a parity claim.

## Validation

`DocumentTests` (CPU engine): `sparse.*` (windows, fill, `Replacing` splits, `ReadRegion`, thumbnails, lazy
`Data()`), `kernel.*` (tip coverage and density, segment distance, the 8-point integral), `brush.*` (a stroke's
pixels, hard edges, soft falloff, cancel), `mask.*` brush cases (paint white/black, uniform masks, growth), `opacity.*`
/ `accumulate.*` / `ripple.*` (the opacity cap, dense vs sparse sampling within tolerance, no stamp ridges),
`curve.*` / `tail.*` (Catmull-Rom subdivision, tail replacement), `crossing.*` (self-intersections against
source-over coverage, repeated flushes), `snapshot.*` / `ridges.*` (a 2000² document with a 400 px tip: mouse-up and
the next stroke never materialize, history retains only replaced tiles), `bounds.*` / `budget.*` (alpha bounds,
transform after growth, the pixel limit), `reject.*` / `line.*` / `keys.*` (invalid settings and points, Shift lines,
size / hardness / opacity keys), `expand.*` (paint beyond the layer grows it) and `clip.*` (selection-clipped paint);
the pixel edit cases (`edit.*`, `pixelmove.*`, `clipboard.*`, `floating.*`) are listed in
[`Document_Tools.md`](../Document/Document_Tools.md).

`CompositionTests` (WARP): `sparse.*` (a sparse raster uploads through the GPU assembly path and matches the
assembled bytes), `stroke.*` (preview texture creation from source and fill, tile updates, mask previews, adoption
on commit, rebuild after a device reset) and `gpubrush.*` (the compute kernel against the CPU kernel within 2/255
per pixel, a session stroke through the GPU engine matching the CPU engine, buffers released after `EndStroke`).

`RedPrism.exe --self-test --warp` paints a stroke through the pointer path with the compute engine, proves the live
preview shows the paint, that mouse-up commits one sparse `Brush Stroke` entry that never materializes and draws
from the adopted texture, that the Eraser clears it, that `[` `]` Shift-`[` and `5` change the tip and Escape drops
a stroke. `--screenshot ... --demo brush` writes `docs/screenshots/brush-96.png`. `HostTests` covers the
`--bench` and `--demo brush` switches and the brush strings.
