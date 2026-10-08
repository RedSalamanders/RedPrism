# Brush rendering

Status: current normative contract (created with C10)
Last reviewed: 2026-10-07
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
- **Tile composition** (CPU, only the tiles the segments touch, 256 × 256 of the grid): paint is the foreground
  color (`brushSettings` red, green and blue, copied when the stroke begins) times coverage over the tile's base
  pixels (the layer's pixels, or the fill beyond them), erase is
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
placement, resized to the crop); a semantic thumbnail is sampled for a painted mask through `ReadRegion`, never
through `Data()`; an image thumbnail may remain empty. A painted
sparse mask keeps its own fill over its own extent (`Replacing` pads only the area the crop adds), so a hide-all
mask painted any number of times still hides everything no stroke touched; a mask whose pixel size differs from its
layer's (stretched over it) is resampled onto the layer's grid by the nearest pixel (`Raster::Stretched`), as its
tiles were read, the first time a stroke commits on it, while a uniform 1 × 1 mask stays one value. A mask on the
layer's grid grows with the layer (`CarriedMask`): its old area keeps its pixels and fill and only the new area
reveals (255). The commit is refused, and nothing recorded, when the layer's pixels or transform changed under the
stroke. History names:
`Brush Stroke`, `Erase`, `Paint Mask`. Undo, Redo and the next stroke read the tiles directly; the first
`Data()` call (export, a filter) assembles a contiguous copy once and keeps it.

### Detached final coverage foundation (P1)

`StrokeOpeningWork` captures immutable document/history authority and event-time brush/Heal/mask/Clone options
after complete caller admission. Its worker constructs a CPU draft, selection coverage and Blur sample; image
Blur preserves transparent finite edges, mask Blur clamps edges, and both retain the existing source-over rule.
Folder and adjustment masks remain supported; disabled masks and image-only Heal/Clone on masks refuse.
Owner publication verifies the exact source, selection, history epoch and active target before attaching the
real coverage engine to an empty draft. It produces no pointer sample or pixel/history edit. The first accepted
sample records its target-bound Shift-line endpoint. These APIs are staged prerequisites; application activation
and measured deadlines remain separate P1 obligations.

`BrushStroke::MakeForWork` owns the whole private CPU graph through a preallocated deferred owner, including
small metadata graphs. Its last owner release reads only the cached complete bound. Before off-owner transfer,
detach the graphics borrow on its owner; failed drafts also have a detach path. The caller takes and acknowledges
the complete GPU epoch before ending the engine. A later CPU destructor cannot reenter that engine. Allocation
failure constructing the shared control invokes the transferred deleter exactly once. Normal application use
must retain the complete opening/live/retirement declaration through every reader and acknowledgement.

`PlanFinishMemory` reads cached tile/tail ownership and at most the last three samples before exclusive transfer.
It uses the final centripetal Catmull-Rom segment's cubic convex hull, including brush reach and numerical padding,
to conservatively bound potentially new tiles. It declares simultaneous curve/key/tile/coverage/compose buffers,
sparse metadata and possible assembled-cache size classes, stretched/carried masks and eager semantic thumbnails,
healing input/coverage/float pyramids and new GPU density/coverage/segment storage. Queue staging, complete
document/history publication, GPU epochs and final residency remain caller admission obligations. Unknown cached
children, arithmetic overflow or unsupported projected commit dimensions/pixels refuse before transfer.
This declaration alone establishes no host admission or timing qualification.

Construction validates finite blank dimensions before rounding/narrowing and computes inverse canvas/source
union in 64 bits. `TileKeys` preflights tile-aligned projected dimensions and the pixel budget before enumerating
its hash keys. A failed/cancelled preparation publishes no draft; it never truncates unsupported inverse geometry
into an apparently valid smaller paint grid. Existing curve and coverage numeric goldens remain unchanged.

Tile producers maintain the aggregate ownership of permanent/coverage capacities and base/image raster graphs
after every successful allocation or replacement, including compatibility healing and lifted-pixel moves.
`RetirementMemoryBytes` reads this cache plus fixed metadata capacities and retained source/selection/sample
bounds without traversing tiles or pixels. Unknown children and arithmetic overflow return unavailable (zero).
The cache does not itself route whole-draft destruction or reserve CPU/GPU memory.

The pinned MSVC hash-directory bound charges two iterator slots per bucket, including retained empty capacity.
`ExpectedFinishCoverageEngine` and `CanTakeStrokeEpoch` provide non-mutating owner preflight for the exact
attached draft, original engine and empty worker-constructed epoch. Worker-opened drafts remain eligible after
their owner attachment; a detached, failed or worker-only draft exposes no engine borrow. Preflight confers no
admission, handoff or completion authority. The owner must preserve serialized access through the actual Take.
The optional `GpuBrushCoverage::StrokeEpoch` prepares an empty replacement map on a worker. `TakeStrokeEpoch`
swaps the complete tile directory and segment buffer/view owners without owner iteration or allocation. Only
after every copy/map user and recorded GPU work acknowledges may a worker reset the detached epoch; reset destroys
the directory as well as its nodes. Cached CPU and GPU charges cover metadata capacities, density/uint payloads,
optional staging and resource allowances. The epoch owns no immediate context or reservation. Its caller keeps
the original owner-only GPU reservation until worker retirement acknowledges, or until joined shutdown returns.
This primitive does not activate application stroke finalization or establish whole-graph admission/G1.

Detached final recording retains the original device identity and the complete density/segment graph.
`RecordEpochCoverage` is owner-only and requires an empty live engine on that original healthy device. It swaps
the captured graph into the engine for one bounded recording call, restores the complete graph and cached
charges on success or failure, and performs no pixel readback. An active live graph or replacement device
refuses without consuming the detached graph. `EpochCoverageBuffer` borrows only the exact key and dimensions
on the original device. The caller keeps this graph and its separate owner GPU reservation through every
copy/map user and actual original-stream completion, then resets the whole epoch on a worker. A failed
recording call alone is insufficient retirement authority. These requirements also apply to failed or
cancelled finalization; they do not activate the application mouse-up path.

Fresh compute density uses `ClearUnorderedAccessViewUint` with zero bits on its structured UAV. It creates no
tile-sized CPU zero vector and performs no initial pixel upload. Reusing an existing tile preserves settled
density; a reused key in a new stroke starts at zero. The coverage shader and legacy density equations stay
unchanged. GPU recording, resource creation, epoch retirement and their measured owner costs remain separate.

`RecordEpochForWork` supplements owner recording with an exclusive recorder-lane path. It validates the actual
captured device, private deferred context and recording thread before creating its private pipeline. Tile keys
are a sorted exact union; unsupported extents, duplicate keys and unrepresentable geometry refuse before mutation.
Captured buffers and their cached charges return to the epoch even on failure. The 80-byte constant layout adds
a disjoint dispatch origin/end; indices and floating positions remain on the original complete tile. Every pixel
processes all segments in their original order. Dynamic segment maps and full compute bindings belong to each
command list. CPU composition/readback and useful owner publication remain outside recording. The caller holds
complete reservations, joins the producer and acknowledges every original use before borrowing or resetting
the epoch. This API alone does not activate application Append/Up or establish a timing bound.

The worker completion API supplements the synchronous reference API above. `DetachCoverageForFinish` transfers
exclusive CPU draft ownership and removes its graphics pointer. It leaves the borrowed engine's density alive;
the owner must retain each final GPU copy source before calling `EndStroke`. A second detach, pointer Append,
or compatibility Flush on that detached draft refuses. No worker calls or destroys a graphics engine.

`PrepareFinalCoverage` builds the final Catmull-Rom curve, the sorted exact union of final and old provisional
tail keys, and private tile metadata. CPU coverage executes on the worker with row cancellation. Compute
coverage preserves the shader's uint values: `RecordCoverage` records dispatch without compatibility readback,
and the typed buffer queue lends the result to a worker. `InstallFinalCoverage` requires the exact key and
value count, clamps uint values to 255, and rejects duplicate deliveries. `ComposeFinalCoverage` requires all
coverage acknowledgements and exact tile origins, dimensions and borrowed preview pointers. Failed, cancelled,
or incompletely composed drafts cannot expose a snapshot.

`HealForFinish` uses the original pixels and unchanged legacy search, membrane, grain and rounding equations.
It checks the complete supported patch region before allocating and checks cancellation between bounds,
read, coverage, kernel and output rows. Partial private results are discarded as one graph; they cannot become
publishable until every touched tile and selection blend completes. `HealForWork` validates actual BGRA and
coverage spans, last-row extent, stride arithmetic, dimensions, finite opacity and mode before allocating or
writing. Its checkpoints distinguish role, ring rows/columns, patch search, statistics, recursive coarse solve,
solve and output. Cancellation during output can leave a private prefix; callers must discard the whole result.

This foundation does not activate asynchronous application mouse-up. The application's existing pointer update
and compatibility finish remain governed by the sections above until the original-ticket host path, complete
admission, final preview/residency and G1 gates qualify together.

The experimental application final-stroke parent keeps the admitted physical gesture's host ordinal and one
`Stroke` scheduler record through exclusive draft capture, final coverage, mapped-buffer conversion, healing,
sparse document/history preparation, row preparation and private display recording. Descriptor admission and
fresh reservation growth precede the corresponding captures/allocations. WARP retains the compute engine;
only a stroke opened with the CPU fallback uses CPU coverage. Borrowed engines never cross to workers.

Publication must preflight the exact current edit/history authority, original display slot, device/context,
GPU reservation and complete prepared final document before changing any visible root. History/document,
prepared rows and display install in one nonreentrant owner boundary. Notifications follow that boundary.
The same original CPU charge survives through shared display/row owners and weak control blocks; GPU charges
survive each original recorder/copy/frame acknowledgement. Failed or cancelled drafts never publish a prefix.
Joined shutdown first joins the actual producers, detaches mapped/seed/controller borrows on the owner, then
destroys complete CPU/epoch/context owners on workers before releasing their admissions.

`Application.Tests.StrokeFinish.cpp` exercises this opted-in path through the real hidden application. Normal
startup is still disabled pending asynchronous Down/Append/Up integration, full fault/driver qualification
and the G1 measurements. Its structural tests cannot establish a performance pass.

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
  once per pointer update and never per frame otherwise. When the preview's grid grew past the layer (the grid
  covers the canvas, a warp covers the document), an unplaced mask of more than one pixel stays on the layer's own
  pixels (placed at the layer's transform) and paint beyond them is revealed, exactly as the commit grows the mask
  with 255; a uniform 1 × 1 mask stays uniform (legacy `TiledLayerRenderer.drawStroke`: "paint beyond the layer's
  old bounds is revealed, not masked").
- Mouse-up first settles the provisional tail (`BrushStroke::Flush`, the final Catmull-Rom segment) and sends the
  tiles it changed to the working texture, then commits: the adopted texture holds the curve the raster holds, never
  the chord the tail drew, so the canvas, a PNG export and the next stroke's base agree with the saved pixels.
- `EndStroke(committed, cropX, cropY, committedLayerID)` adopts the working texture: the committed raster's crop is
  copied on the GPU (`CopySubresourceRegion`) into a raster-cache entry keyed by that raster's identity, so the first
  composition after mouse-up finds its texture and the committed raster is never materialized for upload. The host
  names the layer the committed raster was installed on (`Application::EndStrokePreview` finds it by identity), and
  the adoption happens only when that is the preview's layer, the raster's format is the target's (Gray8 for a
  mask), the crop lies in the working texture and nothing was reduced; otherwise the committed raster uploads on its
  own. `EndStroke()` drops the preview. `CanvasSurfaceBytes` counts the working texture; `ReleaseDocumentSurfaces`
  drops it.
- A gradient or pixel move shows through one preview per raster: when the pending raster changes (another layer, the
  mask instead of the image, or a new line after the old one went) the host ends the old preview, adopting its
  texture only for its own commit, and begins a new one, so a new gradient never writes into another layer's
  texture.
- A grid larger than the device's textures previews through a reduced working texture of the part the display
  shows (Rendering_Composition.md "Documents larger than the device"); its tiles are reduced on the CPU and it is
  never adopted.

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
`MoveLifted(offset, duplicate)` renders the move from the original: the selection becomes a hole (the pixels times
one minus the coverage, unless duplicating) and the lifted pixels land `offset` document pixels away, source-over
(exact for whole pixels on an unscaled layer, bilinear otherwise), so a preview never accumulates and the pixels
along a soft edge add back to the original. It rebuilds only what changed: an offset that rounds to the one already
shown rebuilds nothing, and a new offset rebuilds the tiles the pixels landed on last time and land on now (the
hole's tiles on the first update, or when duplicating switches), each from its original content (review fix
RF-216). The session's `PixelMove` keeps the raster; the host shows it through the stroke preview like a gradient
(`BeginStroke` from `PreviewFor`, the changed tiles per pointer update) and the commit adopts the working texture,
so a move never re-uploads the layer per pointer update. Synchronous session pixel edits upload through the
sparse path. Production Fill, Clear and Invert use `RasterEditWork`: a detached CPU `BrushStroke` mode with no
segment/permanent-density/coverage-engine buffers, cancellation between rows/tiles and private sparse reads.
Worker Clear clips temporary tile rectangles to the existing source grid: clearing cannot grow the committed
image. The document pixel limit still checks that retained grid, while complete admission covers temporary
coverage and patch storage. Canvas tile padding must not cause an otherwise valid near-cap clear to refuse.
The source mapping, selection coverage, destination-out rounding and sparse replacement bytes remain unchanged.
The pointer brush's compute engine, including the required WARP compute path, is unchanged. Prepared sparse
handles and eager mask thumbnails publish once through the token-checked owner API in `Document_Tools`.
Production gradients use that same private CPU path, with metadata-only owner updates and one active/latest
pending worker preview. No worker mutates a published stroke. Preview/source/tile/coverage owners retire as one
graph; progress/cancellation borrows end before publication. A commit can adopt the uploaded preview only for
its exact settings/source generation and target, with worker-prepared crop coordinates; GPU pixel equality and
adoption counts remain required. The direct synchronous gradient API retains its numeric oracle behavior.

## Budgets

| Budget | Limit |
| --- | --- |
| Tile | 256 × 256 grid pixels; only touched tiles allocate (CPU image + coverage, GPU density + preview) |
| Mouse-up | MUST NOT materialize a contiguous bitmap: `IsMaterialized()` stays false through commit, thumbnail, display and the next stroke |
| History | A stroke retains only the tiles it replaced plus the previous thumbnail when present (`RetainedBytes`), never the shared tiles; the session's history limit applies (`Document_History.md`, D13) |
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
size / hardness / opacity keys), `expand.*` (paint beyond the layer grows it), `clip.*` (selection-clipped paint),
`maskfill.*` (a hide-all mask painted on two tiles keeps hiding the rest, also once the layer grows) and
`stretchmask.*` (a mask stretched over a layer of another pixel size keeps its place when painted and when the layer
grows); the pixel edit cases (`edit.*`, `pixelmove.*`, `movetiles.*`, `clipboard.*`, `floating.*`) are listed in
[`Document_Tools.md`](../Document/Document_Tools.md).

`CompositionTests` (WARP): `sparse.*` (a sparse raster uploads through the GPU assembly path and matches the
assembled bytes), `stroke.*` (preview texture creation from source and fill, tile updates, mask previews, adoption
on commit, rebuild after a device reset), `strokemask.*` (an unplaced mask in a grown preview), `adopt.*` (adoption
only for the preview's own layer and target), `oversize.stroke-*` (a reduced working texture) and `gpubrush.*` (the
compute kernel against the CPU kernel within 2/255 per pixel, a session stroke through the GPU engine matching the
CPU engine, buffers released after `EndStroke`). The self-test proves the mouse-up upload: a curved three-sample
stroke's adopted texture equals its committed raster within 2/255.

`RedPrism.exe --self-test --warp` paints a stroke through the pointer path with the compute engine, proves the live
preview shows the paint, that mouse-up commits one sparse `Brush Stroke` entry that never materializes and draws
from the adopted texture, that the Eraser clears it, that `[` `]` Shift-`[` and `5` change the tip and Escape drops
a stroke. `--screenshot ... --demo brush` writes `docs/screenshots/brush-96.png`. `HostTests` covers the
`--bench` and `--demo brush` switches and the brush strings.

### Application opening

Ordinary Paint/Eraser, Spot Healing, Blur and Clone presses capture target, mask, kind, healing mode and brush settings at event time. After descriptor and complete CPU admission, a worker constructs the private draft, selection coverage and Blur sample. Clone first acknowledges its admitted All Layers readback or Single Layer sample, then admits and captures that immutable sample and the press's Clone offset into the same ordered opening continuation. Its opening declaration includes the sample's complete cached retirement bound; Clone on masks, absent or incorrectly sized/formatted samples refuse before capture. The owner attaches the empty coverage engine only after exact source, selection, target and history checks; the original worker stage retires the captured source before replay. The live draft owns the transferred reservation until its final CPU reader retires. Delayed replay cannot substitute newer palette, tip or Clone-source settings. Zoom and deferred Space pan remain view-only while opening work is pending. Pointer samples and mouse-up finalization are tracked separately in the active P1 plan; opening alone establishes no finalization or G1 result.

For an admitted ordinary brush stream, Escape while physical sampling remains open cancels its draft. Accepted
Up, capture loss and overflow loss seal the recorded prefix: subsequent Escape MUST preserve its original
request, receipt and worker authority while cancelling other queued inputs. This includes a stroke still
queued behind an older opacity or sampling request. Undo/Redo after sealing enters the ordered continuation
stream and observes the stroke's acknowledged history at dequeue. Explicit discard, document teardown and
shutdown retain their cancellation and joined-retirement paths. No keyboard callback waits for an opening,
coverage, commit or retirement acknowledgement.

Overflow loss commits only the accepted prefix and returns exactly `ERROR_NOT_ENOUGH_QUOTA` in its original
host receipt, including through the experimental final parent. The refused point and a later Up cannot extend
that prefix. A prior finalization failure, cancellation or stale authority keeps its own terminal outcome.

### Exclusive pointer sample preparation

`PlanSampleMemory` reads cached ownership and at most three previous samples. The unchanged centripetal
segment's cubic convex hull plus provisional tail and tip reach bound the new tile region before any
sample/key/pixel allocation. A monotonically growing bounding tile rectangle excludes the untouched source;
revisiting tiles does not accumulate a new persistent charge per event. Declare the original source graph,
complete persistent tile/density/coverage/metadata owners and simultaneous curve/key/compose copies.
Actual epoch/pipeline/command shadows, staging/maps, immutable preview/display and rows remain caller charges.
Unknown cached storage, overflow, nonfinite input or unsupported projected geometry/pixel limits refuse.

A live retained opening lease grows only on its serialized owner, through a fresh physical/commit query,
before new tile allocations. It keeps its original slot, generation and final reader acknowledgement.
`DetachCoverageForSample` transfers the draft exclusively and removes its engine borrow without ending or
resetting density. No live session or graphics context moves to the CPU worker. While detached, synchronous
Append/Flush/final handoff and PaintSnapshot refuse. A duplicate accepted point advances its sequence but
prepares no segments/tiles/pixels; the host may acknowledge that no-op without useful GPU work.

Worker preparation uses the unchanged settled Catmull-Rom pieces and straight provisional tail, including
previous tail tiles in the exact ordered changed set. CPU permanent density uses the existing scalar kernel.
The compute path requires one exact uint coverage acknowledgement per required key/dimension, rejects
missing/duplicate/foreign delivery and clamps values to 255. Complete sequence/key/grid/buffer validation
precedes tile composition. Failed or cancelled preparation remains permanently unpublishable; no private
prefix can become a snapshot. Temporary metadata is released on the worker.

Only after actual worker/recorder/map/copy joins may the original owner restore the GPU epoch and reattach
the exact engine to the completed CPU draft. Neither operation calls BeginStroke/EndStroke or constructs
new pixel buffers. `RestoreStrokeEpoch` refuses a foreign thread/device, removed device, nonempty live engine
or already returned epoch. Empty directory/device owners still retire on a worker under the parent charge.
`StrokeSampleInput` retains immutable document/history/selection/target authority. The session exposes only
its opening scalar while the private draft runs; Undo authority is invalid then. A current complete apply
restores the draft and Shift-line endpoint without committing document/history. Cancel, replacement, restore,
changed source/selection/history/target and moved/consumed inputs refuse stale publication.
Successful apply consumes the input's mutable draft reference on the owner. The remaining immutable
document/history wrappers may then retire on a worker; later owner cancellation must end the engine exactly
once on that owner, without waiting for or transferring an engine-owning last reference to the input's worker.
Draft borrows from the consumed input are invalid after apply.

These APIs are P1 prerequisites. Actual application Append/Up/preview/display consumers, complete peak
admission, fault scenarios and measured G1 still require qualification. Direct scalar APIs/goldens retain
their existing behavior.

The experimental final parent quarantines a healthy-device retirement-event refusal without polling or refund.
Only a changed queue device generation or completion route grants one new offer on its held original context;
repeated unchanged binding notifications do not retry. Another refusal returns to dormant quarantine and keeps
the first terminal cause. Cancellation acknowledges its original skipped worker stage but retains quarantined
CPU/GPU owners until original event recovery or joined shutdown. Cancellation wins the unpublished receipt
after that acknowledgement. Device replacement itself never establishes GPU completion or successful image work.

### Opt-in application sample parent

The experimental application route starts its original processing parent after worker opening and before
consuming Down. It keeps that ticket, physical ordinal and history work stamp through every accepted sample
and final publication. Between samples it blocks on existing input/cancellation/resource events; no polling
or timer retries are permitted. Shift's previous endpoint is a separate first sample. Up processes its last
point before final coverage; Lost finalizes only the accepted prefix. Duplicate points advance the sequence
without replacing an acknowledged preview.

Before each exclusive transfer, grow the original live opening lease and parent CPU/GPU admissions to their
complete overlapping maxima. Retire each original command stream on a worker before restarting it under the
same ticket. Exact mapped uint coverage is installed on the worker; owner Unmap follows its actual join.
Preview metadata and changed immutable tiles are built and recorded off the owner. Restore the original
epoch and apply the private sample only after those joins and acknowledgements. No sample publishes history.

A returned live draft may coexist with retirement of its consumed input wrapper. Before replacing the device
or dropping the engine, capture that live CPU/GPU draft in a separate worker-preallocated emergency owner;
do not mutate fields an active producer uses or release live density on the owner. Cancellation/replacement
preserves every original context and reservation until its actual acknowledgement or joined shutdown.
Final publication reuses the admitted final coverage/history/display/rows path. This experimental route remains
disabled in ordinary startup until actual mode/fault coverage, the complete validation matrix and G1 pass.
