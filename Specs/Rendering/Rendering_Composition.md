# Canvas composition

Status: current normative product contract
Last reviewed: 2026-10-08
Owner: `RedPrism/CanvasRenderer.*`, `RedPrism/CanvasViewport.h`, HLSL under `RedPrism/Shaders/`, the frame loop's
failure handling in `RedPrism/Renderer.*` and `Application::RenderFrame`

## Scope

This contract owns document-space GPU compositing and the view transform. The swap chain and Present stay in
`Renderer`. The numeric expectations and tolerances below, together with the independent rendering tests,
own layer, mask and clipping behavior.

## Viewport

`CanvasViewport` maps the canvas hole to document space. View coordinates are **device pixels** relative to the hole
(the Win32 client area is physical pixels); `backingScale` is the window's DPI / 96. **Zoom is device pixels per
document pixel**: zoom 1 shows actual pixels on any display, as the legacy "zoom 1 means actual display pixels", and
the status bar's percentage is that zoom. Chrome constants are DIPs and convert through `DipToView` (× backing scale)
or `DipToDocument` (× backing scale ÷ zoom): the Move tool's hit radius (10 DIP) and rotation knob offset (28 DIP),
snap distances, the gradient handle reach (10 DIP) and the polygonal lasso's closing distance (8 DIP), so a handle
hits where it is drawn at every scale. Opening a document fits the canvas (`followsFit`); `Fit` keeps a margin of
96 DIP across each axis (legacy `CanvasViewport.fit`: 96 points, at least 1 point left). A new hole size or display
scale keeps the zoom and the pan in device pixels, so the document point at the center stays put. Zoom is clamped to
**0.001–32**. Setting zoom exactly 1 puts the document's origin on a whole device pixel (the anchor moves less than
half a pixel), so actual pixels are neither blurred nor a click between two document pixels.

Space or the Hand tool pans; View menu commands fit (Ctrl+0), show actual pixels (Ctrl+1, zoom 1), and step by 1.25
(Ctrl+= / Ctrl+-) around the hole center. The wheel zooms around the cursor by 1.1 per notch (`WHEEL_DELTA` 120) and
by that fraction for a partial delta (`1.1^(delta / 120)`), so a precision touchpad's small deltas or a pinch add up
to the notches they sum to and a zero delta does nothing. The Zoom tool zooms around its press by `2^(dx / 100)` for a
horizontal drag of `dx` DIPs (right in, left out, legacy `EditorCanvas`), measured from the press whatever the number
of pointer moves; a press released within 3 DIP zooms in twice (Alt: out).

The blit samples the canvas with point filtering from **200 %** (legacy `crispZoom`: hard-edged document pixels) and
draws the pixel grid from **800 %**; both decisions come from the same zoom (`SamplesCrisp`, `ShowsPixelGrid`), never
from a threshold in HLSL. Zoomed out, the canvas reduces sharply through its own mip chain: the two canvases carry
mips, generated once per changed composite when the blit first needs them (zoom × canvas scale < 1), and the blit
samples at the level the zoom gives (`canvasLod`, computed on the CPU because the scale is uniform and implicit
derivatives are undefined across the hole's edge). Layer textures are mipmapped too (`GenerateMips` when the device
allows; otherwise a single-level fallback). `LayerSampling::Nearest` samples with point filtering; `Smooth` and
`High` sample linearly.

Layer and mask `SampleGrad` footprints come from the exact affine inverse placement, never lane derivatives.
For size `s`, rotation `(sin, cos)`, flips `f`, window scale `v` and regional texture scale `r = rect.zw`, the
normalized texture rates are `dx = (cos, -sin) × v × f × r / max(s, 1e-5)` and
`dy = (sin, cos) × v × f × r / max(s, 1e-5)`. An unplaced raster mask uses the layer's placement; a placed mask
and folder coverage use the mask's placement. This preserves point/linear filtering and minification mips at
thin targets, clipped edges and recording scissor boundaries. Computing `ddx`/`ddy` before a source-level branch
is insufficient: optimized FXC can sink those instructions into divergent unit-square/alpha branches, as the
archived Release r460 shader shows. Exact affine rates avoid that dependency; the constant-buffer ABI is unchanged.

`samplingfootprint.*` uses an independent pixel-center reference for this existing sampling contract: a two-texel
row `[31,219]` stretched to four pixels reads `[31,31,219,219]` with nearest sampling and `[31,78,172,219]` with
linear clamp (±1 for UNORM rounding). Placed/clipped masks read `[0,31,219,0]`; flips reverse the row; a 90-degree
rotation gives the same row as a column; a uniform mask remains 128. Minifying `[0,255,0,255]` to two pixels
selects the reduced pair averages `[128,128]` (±1). These checks cover source images, unplaced/placed raster masks
and folder masks without changing any legacy golden or tolerance.

## Document-space model

Document space has its origin at the top-left, +Y down, one unit per document pixel. A `LayerTransform` places the
unit square of a raster: rotation is clockwise around the center, `flipX`/`flipY` mirror the unit square, and the
shader inverts that mapping per pixel (`DocumentToUnit`), so pixels outside the transform rectangle are transparent.

The canvas is a **window** of the document: canvas pixel (i, j) covers the document square of side `viewScale` at
`viewOrigin + (i, j) × viewScale`. The display's window is the whole document at scale 1, or the reduced proxy of
"Documents larger than the device"; a readback's windows are tiles at scale 1. Coverage and stack surfaces are the
canvas's size and share its window. Pass modes, layer flags, adjustment kernels and blit flags are constants shared
by the C++ and the HLSL (`Shaders/CompositionShared.h`); every `AdjustmentKind` maps to its kernel explicitly, so the
enum's order never reaches the shader.

Every visible pixel layer draws with one fullscreen pass (`CompositionPixel.hlsl`, pass `Layer`) into ping-pong
BGRA8 canvas textures. The premultiplied layer color is:

```text
color = sample(image, layerUnit) × opacity × rasterMask × liveCoverage × folderCoverage
```

- **Raster mask**: a Gray8 image sampled at the layer's unit coordinates. A mask moved apart from its layer
  (`LayerMask::placement`) samples at the placement's unit coordinates instead; outside the placement it shows
  its edge tone (`MaskEdgeBackground` of the thumbnail: 1 reveal or 0 hide, legacy `LayerMask.background`).
  A disabled mask contributes nothing.
- **Live coverage** (`maskSourceID`): the source layer's own coverage in document space — its image alpha through
  its transform, times its opacity, its raster mask and its own upstream live coverage — independent of the
  source's visibility and color (legacy `LiveMaskRenderer.coverage`). A source without pixels covers nothing, so
  the clipped layer is hidden. Coverage textures are R8, document sized, rendered by pass `LayerCoverage` and
  cached per composition by source id.
- **Folder coverage**: an enabled folder mask is a Gray8 image placed by the folder's mask transform (placement
  or the folder transform); outside its rectangle the coverage is 0. Nested folder masks multiply. Pass
  `MaskCoverage` renders one R8 texture per masked folder chain (`GroupCoverage`), cached per composition.

## Clipping stacks

A base layer with no `maskSourceID` and no adjustment, followed in render order by clipped siblings whose
`maskSourceID` is that base and whose `parentID` matches, forms a **clipping stack** (legacy
`LiveMaskRenderer.prepareStacks`). The stack composites through a scratch surface:

1. `StackBegin` writes the base as straight color plus its alpha (opacity applied).
2. `StackChild` blends each clipped layer over the stack's straight color with the child's blend mode and opacity
   while keeping the base alpha, so a soft base edge never darkens (no black fringe).
3. `StackResolve` premultiplies the stack, applies the base's folder coverage, and composites it onto the canvas
   with the **base's** blend mode.

Clipped layers that are not directly above their source (or sit in another folder) draw as ordinary layers
multiplied by the source's live coverage.

## Blend modes

Modes 1–8 (Multiply, Screen, Overlay, Darken, Lighten, Difference, Color Dodge, Color Burn) are the separable PDF
formulas on straight colors, composited as `Co = (1−αs)·Cb + (1−αb)·Cs + αs·αb·B(cb,cs)`. Color Dodge and Color
Burn follow the PDF / Core Image definition, never Core Graphics. Hue, Saturation, Color and Luminosity (modes
9–12) are the PDF non-separable mixes on straight colors (`Lum` 0.3/0.59/0.11, `ClipColor`, `SetLum`, `Sat`,
`SetSat`): Hue takes the source hue at the backdrop's saturation and luminosity, Saturation the source saturation
on the backdrop's hue, Color the source color at the backdrop's luminosity, Luminosity the backdrop's color at the
source luminosity. `Adjust::BlendStraight` is the CPU reference every mode is checked against. Groups are
pass-through.

## Adjustment layers (C12)

An adjustment layer (`ImageLayer::adjustment`, no pixels of its own) reworks the composite beneath it, as the legacy
`LiveMaskRenderer.adjust` did on the whole canvas so far:

- Pass `Adjust` (6) reads the current canvas as the destination, unpremultiplies each pixel, runs the kind's math
  on the straight color, blends the result over the original straight color with the layer's blend mode when it is
  not Normal, and mixes it back at the layer's coverage (opacity × its own raster mask through its placement, with
  the mask's edge tone beyond a placed mask × its folders' coverage), keeping the pixel's alpha; a transparent pixel
  is left alone. The layer's transform only places its mask.
- A clipped adjustment (`maskSourceID`) applies inside its base's clipping stack through pass `StackAdjust` (7): the
  stack's straight base color is reworked at the child's coverage and the stack alpha kept, so a soft base edge is
  adjusted like the color it is. A clipped adjustment that is not directly above its base draws nothing, as in
  the legacy renderer. A hidden adjustment (or a hidden folder above it) draws nothing.
- The kinds: Levels, Curves and Exposure are three 256-entry tables (one RGBA32F row, `g_lut`, sampled linearly
  between neighboring entries exactly like the CPU `levels_apply`); Gradient Map is a 256-entry color table
  indexed by the rounded Rec. 709 luminance of the straight color; Hue/Saturation is the 33³ color cube
  (`g_cube`, RGBA32F `Texture3D`, trilinear like `CIColorCube`, built by `Adjust::ColorCube`); Grain is
  procedural in the shader, the legacy lattice hash at the document pixel center with the layer's seed, so the
  pattern is fixed in document space however the canvas is drawn. `Common/Adjust.cpp` holds the CPU reference for
  each (`ApplyAdjustment`) and `CompositionTests` proves the GPU stays within its tolerance.
- Textures per adjustment layer are cached by layer id (`AdjustTextures`), rebuilt when the settings change (an
  adjustment edited live in the panel rebuilds per change), released when a composition no longer uses them or
  with the document surfaces, and counted in `CanvasSurfaceBytes`.


## Composition lifetime and residency

- `Compose` re-renders only when the document changed (`InvalidateDocument`, called by `Application::DocumentChanged`
  after every session edit, undo/redo, open and import) or the canvas surfaces were rebuilt (document size, device
  loss, hidden→visible). A settled document composites zero times per frame; `CompositeCount()` proves it and
  `--self-test` asserts a second frame does not re-composite.
- Uploaded rasters are cached by `Raster::Identity()` and keep their storage alive while cached, so a freed address
  can never alias a different image. Every composition releases entries it did not use, which bounds GPU residency to
  the rasters of the current document.
- Sparse rasters (C10) upload through GPU assembly (`CreateSparseTexture`: clear to the fill, copy the base's cached
  texture, write the tiles, generate mips), so their CPU copy stays unassembled. A brush stroke in progress draws
  from a working texture updated tile by tile (`BeginStroke` / `UpdateStrokeTiles`) and adopted into the cache on
  commit (`EndStroke`), as [`Rendering_Brush.md`](Rendering_Brush.md) specifies; it counts in `CanvasSurfaceBytes`
  and is dropped with the surfaces.
- Adjustment layers and GPU adjustment previews keep lookup textures cached by layer id and released like rasters.
  A 256 × 1 RGBA32F DYNAMIC LUT uses `Map` DISCARD; a DEFAULT 33³ Hue/Saturation cube uses `UpdateSubresource`.
  Settings changes reuse these resources. Worker-prepared values supply live panel ticks; ordinary document
  reconstruction can build a missing lookup from its committed adjustment metadata.
- Destructive GPU previews use pass `LayerAdjust` (8) into a reusable BGRA8 target on the prepared layer grid.
  It reads source pixels directly, unpremultiplies, applies the lookup/grain, rounds the premultiplied result to
  bytes, mixes original/result through edit selection coverage, and keeps source alpha. Destructive grain uses
  `(pixel + 0.5) / previewScale` in layer coordinates. The ordinary layer pass then places this target with the
  layer's transform, opacity, blend mode, real mask, live coverage, clipping stack and folder coverage. Passes 6/7
  retain adjustment-layer semantics. The CPU reference is `RunFilter` plus `BlendThrough` followed by the same
  ordinary composition (tables/Gradient Map 1/255, cube 4/255, grain 2/255). GPU previews keep P1's existing
  prepared-grid limits; view-resolution refinement belongs to P3.
- `SetAdjustmentPreview` retains owning CPU handles. Hidden release destroys its target and lookup textures;
  visible/device recreation rebuilds them from those handles. Settled blits record no extra adjustment pass.
  `ComposeReadback` ignores this transient preview and reads the committed document. A lookup/target failure uses
  the existing canvas failure path, leaves the document intact and never publishes a partial committed raster.
- Canvas surfaces: two BGRA8 canvases with mip chains, two BGRA8 stack surfaces (allocated on the first clipping
  stack), and a pool of R8 coverage textures reused across compositions. `CanvasSurfaceBytes()` reports all of them.
  Hidden, minimized (`WM_SIZE` 0 × 0), occluded (`Present` reported `DXGI_STATUS_OCCLUDED`) or display-off windows
  drop them (`ReleaseDocumentSurfaces`, `CanvasSurfaceBytes() == 0`) and MUST NOT Present; the first frame that can
  reach the display composes them again. A `--screenshot` capture keeps them.
- Device loss rebuilds shaders, surfaces and the raster cache as one operation. The host rebinds the composition and
  the brush engine when the renderer's device generation changes, so a rebuilt device at the old one's address is
  never mistaken for it.
- Constants are updated with `Map` `DISCARD`; steady-state frames that only blit do not allocate.

## Documents larger than the device (Decision D1)

The model keeps the legacy limit of 30 000 pixels a side (`kMaxCanvasSide`); Direct3D 11 textures stop at
16 384 (`D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION`). The composition never asks for a texture beyond that limit:

- **Display proxy**: a document with a side above the limit composes at a power-of-two reduction (`CanvasScale()`
  2, 4, … document pixels per canvas pixel) so both canvas sides fit; the blit maps the proxy onto the document's
  extent, so the view, the overlays and pointer mapping are unchanged and zooming in shows the proxy's pixels.
- **Large rasters**: an image, mask or stroke grid with a side above the limit uploads only the part the canvas
  window shows (with a filtering margin of twice a canvas pixel's footprint plus two pixels), reduced by a power of
  two when that part is still too large, read through `Raster::ReadRegion` band by band (at most 8 MiB of rows at a
  time), so a sparse raster is never assembled. The shader remaps the raster's unit square onto those texels
  (`sourceRect`, `maskRect`). These textures are cached per raster, part and reduction and released when a
  composition no longer uses them, like whole textures. A reduced stroke working texture takes its tiles reduced on
  the CPU and is never adopted at commit; the committed raster uploads on its own.
- **Readbacks at full resolution**: `ComposeReadback` composes a document above the limit in tiles of at most 4 096
  pixels (never more than half the limit, so a tile's part of a raster fits at full resolution) at scale 1 and
  stitches them. Nothing is refused and nothing is reduced: an export, Copy Merged, the wand or the eyedroppers of
  a 20 000 × 3 000 panorama read every pixel. `Readback` (the display canvas) fails while the display draws a
  proxy, so no caller can mistake the proxy for the document.

Limits: the display of a document above the limit is the proxy's resolution (a 30 000 pixel side shows at half
resolution even at 100 %); a stroke on a grid above the limit uploads its reduced working texture from the layer
once when it starts and its commit uploads once; a readback of such a document composes every tile in turn.

## Readbacks

Export PNG/JPEG, Copy Merged, the wand's All Layers, Clone Stamp's All Layers and the eyedroppers read
the committed document (`DocumentSession::Document()`) flattened at full resolution,
never the display composite, a panel's preview raster or the stroke preview (`EndStroke`'s working texture stands in
for its layer only in the display). The remaining synchronous `ComposeReadback` callers mark the display dirty,
so the next frame composes the display document again (legacy `sampleCompositeColor` draws the document itself).
The separate utility path below preserves the settled display composite.

Synchronous callers take the `ComposeReadback(document, Raster&)` overload: staging rows are written into one 64-byte-aligned
`PixelBuffer` and adopted directly as BGRA8888, with no second full-image CPU copy. The vector overload remains for
diagnostic consumers and shares the same composition/readback routine. Failure clears either output; partial pixels
are never published. Merge, adjustment opening and PNG/JPEG export use the asynchronous utility path below. Settled
New-mode All Layers wand uses the admitted `DocumentReadbackJob` graph below; Add/Subtract and live-context wand
requests retain their existing synchronous composition callers. Eligible Clone All Layers uses the admitted
readback graph; eligible single-layer Clone uses a worker placement sample without GPU composition. The P1 canvas color sampler uses
one admitted source and repeated 1×1 requests with no owner pixel copy; current qualification is scoped below. Clipboard
actions use worker and readback graphs; prefill/inspection retries and final transfer/publication sequencing retain
their separate clipboard contracts.

## GPU readback queue and utility composition (P1.3)

`GpuCapabilities::Probe` borrows a device and its immediate context, verifies their COM identity and rejects
null, deferred or mismatched contexts. Optional feature/interface queries retain both availability and HRESULT;
unsupported queries never imply a capability. The value report covers Device5/Context4/CreateFence, Adapter4
monitored fences, IDXGIDevice2, options2, concurrent resource creation, four required typed UAV store formats,
minimum precision and local/nonlocal video-memory budgets. Monitored completion requires all fence interfaces,
fence creation and the adapter's monitored-fence flag. Otherwise a successful IDXGIDevice2 query selects
EnqueueSetEvent; no supported completion route fails locally.

`GpuWorkQueue` is an optional owner pump, with eight fixed slots and at most two GPU copies in flight. Its BGRA8
mip-0 reads accept regions up to 2048 squared; larger consumers must traverse tiles. It pools staging textures by
power-of-two width/height classes under a 64 MiB texel-storage cap, including lent and retired textures. Driver
allocation/row-padding overhead remains outside this declared texel cap; mapped row pitch is validated and used
by the copy worker. When retained staging prevents a new class allocation, pending work waits for acknowledgement
without claiming readiness or polling. A 1×1 sampler stages one texel.

The same slots also accept exact structured uint32 buffers through `EnqueueBufferReadback`: a nonempty shape
up to 2048 squared, four-byte structure stride, matching `ByteWidth`, supported default/immutable usage and the
bound device identity are required before admission. Buffer staging is byte-exact; texture staging keeps its
power-of-two classes. A slot changing kind drops the incompatible reusable staging owner before replacement,
and idle, lent and retired storage share the same 64 MiB cap. `GpuBufferReadbackValues` carries an immutable
uint32 span, exact width/height, lease and device generation to a typed receiver. The owner never converts or
copies those pixels. Callback, nonblocking Map, event rearm, cancellation, retirement and exact Unmap rules
below apply equally to buffers. A successful enqueue retains its source even when the coverage engine ends.

`HasReadbackSlot` reads the eight fixed owner metadata slots and returns false when stopped or unbound.
It reports readiness only; it neither reserves a slot nor admits CPU/GPU memory. A final-density producer MUST
wait for readiness before depositing the next settled segments, then retain the successful copy source once.
Capacity release and device retirement notify the owner through the existing event route; a producer MUST NOT
poll or re-dispatch settled density after an admission failure.

Only the owner records Copy, fence Signal/event registration/Flush or EnqueueSetEvent, Map READ DO_NOT_WAIT and
Unmap. EnqueueSetEvent already flushes. Registered waits publish an atomic ready flag and wake the shared job event.
Map is nonblocking after completion. If it returns WAS_STILL_DRAWING, the queue may retain the original copy,
slot, generation and in-flight charge through exactly one additional EnqueueSetEvent barrier when supported.
Only that new event permits a second nonblocking Map; a second busy result or barrier failure is a local failure.
There is no recopy, timer, polling loop or blocking Map. Device retirement is checked before either attempt.
Successful delivery
lends immutable rows with an exact lease generation. Workers may copy them, and the owner unmaps exactly once on
acknowledgement. Duplicate/stale releases fail. A device rebind retires old pending/in-flight work; lent rows keep
their old context/texture/fence alive until worker acknowledgement, and their bytes remain charged against the
same staging cap. No useful image command is recorded on a retired context. An explicit owner-captured
retirement barrier may signal/flush its original healthy context solely to acknowledge preceding commands;
it shares the fixed queue/event limits, reports its captured generation and grants no current publication.
Its strong origin rejects a foreign or closed/reinitialized queue. Fence serials never reset at rebind.
Admission/recording failure is not a GPU acknowledgement. Consumers validate returned device generations
before publication. Shutdown disarms/joins wait producers, joins copying workers, then unmaps/releases leftovers.

`Application` owns this queue and binds it when the renderer's exact generation changes. Stop event producers
before joining scheduler workers, then release queue owners before composition/device teardown.

Utility composition has separate two-canvas, stack and coverage targets. It shares the owning raster/region and
adjustment caches with display composition; residency is the union of the latest utility tile and settled display.
Utility recording saves/restores view bookkeeping without invalidating the display or incrementing its compose
count. It ignores stroke and color-panel previews and records no mips. `AcquireUtility` retains those targets;
the last matching release drops utility-only owners. Hidden display release still reports zero display surface
bytes and never presents, while accepted utility jobs may finish with their separate targets.

`EnqueueProducedReadback` invokes a job-owned producer inside the owner recording stage, immediately followed by
the staging copy. Another request cannot overwrite the reusable utility target between those commands. The
producer returns a borrowed source only for that call; all pending/waiting COM owners stay in the queue.

`DocumentReadbackJob` owns one immutable document/region and reservation through its complete tiled graph. Worker
stages allocate an aligned output, copy rows using the validated pitch, check cancellation per row and adopt only
the finished raster. Owner stages acknowledge each lease before the next tile and publish once. Full-image,
region and 1×1 requests compose only their requested bounds, in tiles at most 2048 or half the lower device/test
texture limit, retaining the synchronous traversal's guard room for transformed sampling footprints.
An optional final worker step prepares Merge's trim and complete survivor/history publication under the
same reservation. Production merge admits and captures metadata first, then builds the plan/composition subset
on a worker; a missing plan finishes without pixel/readback allocation. A ready plan keeps the same history ordinal
through separate full-graph admission and history capture. Merge and export detach the final mapping and utility
targets before their CPU publication/encoding stages. Device authority is validated before this detached boundary;
later device changes cannot invalidate copied bytes or restart CPU history preparation/an external writer.
A generation/device
failure restarts the whole graph at most twice, releasing the previous attempt on a worker; it never publishes
partial pixels. Unsupported completion, capacity, memory or persistent device failures fail locally.

For settled New-mode All Layers wand, complete graph admission covers the conservative selection plan, full tiled
readback, source snapshot, job metadata and aggregate retirement before the owner captures its immutable document.
Each tile is composed from that snapshot; only the owner records/maps/unmaps GPU resources. Workers copy borrowed
rows into the completed BGRA image, and the final lease and utility targets are released before detached CPU wand
sampling, tracing and history preparation. Current-only owner publication performs prepared history publication
and nonthrowing document/display swaps. The completed composite is held separately from the empty base
`DocumentReadbackJob` transfer destination, so the derived release aggregate can receive base-owned document/pixel/
image owners without displacing the completed image and retire the whole payload off owner. This route uses event
completion and does not poll; Add/Subtract and live-context wand requests retain the synchronous composition path.

A detached 1×1 consumer may continue at another in-bounds pixel with `ContinuePixelReadback` only after
the previous worker and mapped lease have been acknowledged. It retains the exact suspended ticket,
original immutable document, reservation and total two-restart allowance. Refused early, stale, duplicate
or out-of-bounds requests change no state. Previous CPU output retires on the next worker; utility targets
are released before that detached boundary. Input may leave the record suspended until a message arrives,
without polling, a timer or a dummy worker turn. This primitive does not activate the production sampler;
the canvas color sampler uses this continuation with exact gesture/source/manual/picker-opening authority and
terminal acknowledgement. Current private Debug qualification is recorded in
[SamplerReadback](../TestRuns/SamplerReadback_2026-10-04.md); broader profiles, native activation and G1 remain due.

Merge, adjustment-editor opening and PNG/JPEG export use this path, retain their selected history acquisition
through apply/drop, and validate complete snapshot and device generations before use or CPU detachment. Settled
New-mode All Layers wand also uses this path as specified above. Eligible settled Clone All Layers input uses
the detached full-document readback with captured offset and settings; its original reservation survives in
the adopted live sample until worker retirement. Eligible single-layer Clone uses the captured image/placement
worker source with the same input and retained-charge protocol; it does not prepare a GPU source. Unsupported
host Clone input refuses without synchronous fallback. Add/Subtract and live-context wand retain synchronous
paths. Eyedropper and Alt-tool sampling use the admitted 1×1 graph,
with current actual caller/profile qualification and remaining work scoped in the sampler record. Clipboard readback and transfer use their worker
graphs; prefill/inspection retries and final transfer/publication sequencing remain governed by the clipboard
contract. Synchronous readback remains the diagnostic numeric reference; application-wide nonblocking readbacks
and G1 are not yet established.

## Failures

### Bounded private recording and original command completion

`GpuCommandStream` transports two immutable published command lists between one recording producer and its
original immediate-context owner. The producer's incoming command/private context is a third shadow owner;
the parent's complete CPU/GPU admission MUST include all three plus source, device, result and retirement peaks.
The descriptor remains on one explicitly opted-in scheduler record. Transport metadata is not admission.
Only exact original job/device/ordinal/slot leases may execute or acknowledge. Full capacity waits on events
on the dedicated recorder; cancellation/shutdown wake that wait without releasing executing commands.

`GpuRecordingCommands` requires successful actual DriverConcurrentCreates and a private deferred Context1.
It records upload bands at most 1 MiB, clear/copy/draw windows at most 512², and eager linear-clamp mip draws,
checkpointing and fully rebinding state after FinishCommandList(FALSE). UpdateSubresource1 preserves software
command-list source offsets on WARP. Parent-owned initialized source spans remain alive through recording.
Supported P1 formats are R8, premultiplied BGRA8 and RGBA32F lookup inputs. Invalid device/layout/pitch/box
refuses the preparation. Cancellation checks occur between bounded commands and resource creation calls.
These are command-size bounds; opaque driver memory and elapsed time still require admission and measurement.

`GpuCommandPump` is one stable startup-registered owner source with two fixed borrowers. It executes at most
one batch per owner pump, outside frame recording, using the queue-captured original context. It first checks
completion capacity and the command list's actual device against the captured device before execution. A foreign
device refuses without Execute or an acknowledgement, even when numeric queue generations match. Its original
producer and full charge remain until joined worker retirement. The actual original GPU callback (including
retired E_ABORT), confirmed original-device
removal, or joined shutdown acknowledges a submitted use. Failure to enqueue/signal and requested cancellation
never acknowledge it. A post-execute failure stops the source and retains the exact command/context/charge.
One bounded captured recovery barrier may resolve it. Failed healthy recovery stays dormant and quarantined;
an actual device change permits one further attempt, otherwise joined shutdown resolves destruction ownership.
No timer, polling or unchanged self-rearm is permitted. Failure is reported once per source.

Normal detachment requires recorder callback join and all submitted GPU uses acknowledged. Captured device/context
references transfer into an empty worker-retired owner; the UI never silently drops their last reference.
Shutdown stops future use, joins command/wait producers, acknowledges uncertain slots for destruction only,
then retires complete CPU/graphics graphs on the joined worker before returning admission. Ordinary owner
callbacks and useful pixel publication remain suppressed in that shutdown phase. Receiver callbacks cannot
detach/destroy their own receiver or recursively pump/close. Stable pump registration ends after every borrower.

Canonical tests use independent exact lease/state expectations, real WARP completion callbacks, same-record
reservation/FIFO and actual Execute followed by injected completion refusals. A recovery success uses the real
captured barrier; a healthy failed recovery has no acknowledgement before joined shutdown. This mechanism does
not yet activate full display/stroke graphs. Complete supported-image admission, immutable published display,
fallback, source reuse, final layer rows and actual P1/G1 timings remain required before application activation.

### Dormant bounded upload leaf

`GpuUploadQueue` transports admitted base-level chunks through owner WRITE/DO_NOT_WAIT Map, worker-only row
filling, owner Unmap/copy and a real completion event. Its fixed records and staging charges remain owned through
acknowledgement. Startup failure must unwind acquired event/wait/route prefixes. A failed completion signal
after recording, including a Map-recovery barrier, quarantines the slot and retained context until joined
shutdown; uncertain completion never permits reuse. Stale or foreign lease publication cannot acknowledge a
new request. Only the owner records or releases graphics objects; workers receive writable rows, never a context.

`RasterUploadLayout` is immutable arithmetic for an owned crop and exact proxy scale. Every chunk retains the
same reduction grid and clipped-edge rounding. Its logical mip bytes and bounded reduction scratch are inputs
to complete admission, not a VRAM reservation or a resource creation plan. `GpuRasterUploadJob` serially fills
base chunks under the same opted-in scheduler record and complete reservation. Completion may precede producer
return; the receiver remains alive until both acknowledge. Cancellation suppresses remaining chunks and holds
pending map/fence receivers through final acknowledgement. The application still uses its current composition
upload paths until complete resource admission, mip/display publication and measured first-display gates pass.

A frame never closes the window on its own (RF-401):

- **Device removal at any stage**: when chrome `Prepare`, the composition, the blit, chrome `Composite` or `Present`
  fails and either the result or `GetDeviceRemovedReason` says the device is gone, the renderer rebuilds the device,
  swap chain and target (the chrome follows through `ReplaceDevice`), returns `S_FALSE`, and the host rebinds the
  composition and the brush engine and draws again on the new device.
- **Canvas failure** (the composition or the blit fails without device loss, for example out of memory): the frame
  still presents the chrome with the hole empty, and a notice says the canvas could not be drawn and that the
  document is intact (`IDS_ERROR_CANVAS_DRAW`, once per failure run). The next frame tries the canvas again.
- **Persistent failure**: three failed frames in a row (a chrome stage, a rebuild that fails, a wait that fails, an
  adapter change or a resize that fails) stop drawing and route through the normal close (`WM_CLOSE`), so a modified
  document asks to be saved first; if that close is cancelled the window stays, undrawn, and can still save.

## Present order

Clear → chrome `Prepare` (layout, hole) → `Compose` (only when dirty) + `BlitToTarget` (discard outside the hole,
checkerboard + optional pixel grid; canvas mips generated first when zoomed out and the composite changed) → DxUi
`Composite` over the canvas (its surface is transparent where no control paints, so tooltips and popups can overlap
the canvas) → one `Present`. Occlusion stays on the C1 scheduler; when DXGI's occlusion message could not be
registered, an occluded window probes (`Present` `DXGI_PRESENT_TEST`) on each wake and retries the registration
instead of waiting for a message that never comes.

## Validation

`Tests/CompositionTests` runs on WARP with nearest sampling on 2×2 documents unless stated; tolerances are named
here so goldens are not silently rebaselined:

| Golden (legacy source) | Expected |
| --- | --- |
| Blend modes, 0.4 gray under 0.8 gray (`LayerAppearanceTests`) | Normal 0.8, Multiply 0.32, Screen 0.88, Overlay 0.64, Darken 0.4, Lighten 0.8, Difference 0.4, Dodge 1, Burn 0.25 (±5/255); opacity 0.5 → 0.6; opacity 0 → 0.4 |
| Multiply red over white; Dodge red over 50 % gray | `(0,0,255,255)` ±2; `(128,128,255,255)` ±3 |
| Order, hidden layer, blank layer, empty document (`LayerTests`) | top wins; hidden top reveals red; blank layer transparent; empty document alpha 0 |
| Raster masks (`LayerMaskTests`) | reveal-all 255; hide-all 0; disabled 255; coverage `[255,0,128,255]` ±1; ×opacity 0.5 `[128,0,64,128]` ±1; flipX `[0,255,255,128]`; rotation keeps the coverage total ±4; placement +1 px `[255,255,255,128]`; placed hide-all stays 0 |
| Live masks (`LiveMaskTests`) | hidden black source `[255,0,128,255]`; source opacity 0.5 `[128,0,64,128]`; target mask 0.5 lowers alpha; moved source `[0,255,0,128]`; chain `[0,0,128,255]`; blank source hides |
| Clipping stack (`clippingColorPreservesSoftBaseAlphaWithoutBlackFringe`) | alpha stays `[255,128,32,0]`, RGB equals alpha (white at base coverage); child opacity keeps alpha; over a white background fully white and opaque; base Multiply applies to the stack (200 gray × 128 gray ≈ 100 ±3) |
| Folder masks (`folderMasksClipEveryLayerInside…`) | hide-all 0; disabled 255; coverage `[255,0,128,255]`; × layer mask `[255,0,64,255]` ±1; nested outer hide-all 0; coverage pool ≤ 2 textures; hidden parent 0 |
| Transform (`renderingRotatesScalesAndFlipsWithoutReplacingPixels`) | 8×8 red/blue at (8,8) 16×16 rot 90: (16,12) red, (16,20) blue; flipX → (16,12) blue; 4× scale and flipY keep columns |
| Residency and caching | settled document not re-composited; `InvalidateDocument` re-composites; hidden surfaces 0; device-loss recreate; resize rebuilds |
| Viewport (`CompositionTests.swift`; the legacy view is points, RedPrism's device pixels, so its 800×600 view at 2× is 1600×1200 here) | round trip at zoom 0.25/1/3.75 and backing 1/2 within 1e-6; the document is `width × zoom` device pixels wide at backing 1 and 2 (legacy `documentRect.width × backing == width × zoom`, `viewport.actual-pixels`); cursor pixel fixed under zoom; fit ≤ 1408×1008 device pixels (704×504 points) centered in 1600×1200 at 2×; resize keeps the center; limits 0.001 and 32 |
| Viewport scale (RF-403, RF-404, `viewport.*`, `overlay.*`, `wheel.*`, `zoomtool.*`) | 10 DIP is 22.5 device pixels at 2.25 and 45 document pixels at zoom 0.5 there; the fit margin is 96 DIP at 1.5; crisp from zoom 2 and the grid from 8 at backing 1, 1.5 and 2; zoom 1 puts the origin on whole device pixels; a point 15 device pixels off a corner handle hits at 225 % and misses at 100 %, the knob 28 DIP up; wheel 1.1 per 120, 1.21 per 240, ten 12s make 1.1, 0 does nothing; the Zoom tool doubles per 100 DIP |
| Blit (RF-403, RF-412, `blit.*`) | a 2×1 document at zoom 2 on a 2× display covers device pixels 6–9 of a 16-pixel hole; from 200 % the boundary pixel is pure black, at 150 % blended; black and white one-pixel columns at 25 % read mid gray through the canvas mips (100–155), not black |
| Oversize (D1, RF-401, `oversize.*`; texture limit lowered to 64, then the device's 16 384) | a 100×10 document composes through a 2× proxy with one region texture; `Readback` of the proxy fails; the display blit shows red and blue halves; `ComposeReadback` returns the exact 100×10 bytes in tiles, a 100×10 mask cuts alpha at column 30, and a stroke on a 100×10 grid previews reduced, is not adopted and reads back exactly; a 17 000×10 document composes at scale 2 and reads back byte for byte |
| Readback (RF-402, `readback.*`) | with a red stroke preview over a white layer the display shows red while `ComposeReadback` returns white; the next display composition shows red again |
| Adopted readback (`readback.adopted-*`, `oversize.device-limit-adopted-readback`) | the raster overload returns the vector overload's exact committed bytes, including the 17 000×10 tiled case, with aligned storage; invalid dimensions or a missing device return the exact error and an empty output |
| Previews (RF-406, RF-407, `strokemask.*`, `adopt.*`) | a 4×4 layer's unplaced mask stays on its pixels in an 8×8 grown preview (column 1 shown, column 2 hidden) and paint beyond them shows; a uniform 1×1 mask stays uniform; a working texture is not adopted for another layer's raster or for a mask raster, and is adopted for its own |
| Sparse upload and stroke preview (C10, `sparse.*`, `stroke.*`) | an assembled sparse raster renders like its contiguous twin; the preview texture builds from source and fill, takes tile updates, previews masks, is adopted on commit and rebuilt after a reset, and the rebuilt texture draws the tile again (`stroke.rebuilt-preview-drawn`) |
| Compute brush (C10, `gpubrush.*`) | kernel tiles and a session stroke through `GpuBrushCoverage` match the CPU engine within 2/255 per pixel; tile buffers are released after `EndStroke` |
| Adjustment layers (C12, `adjust.*`, legacy `AdjustmentLayerTests`) | a Levels layer with output white 0 blackens the white beneath but not the red above, stays live when the base changes and does nothing hidden; a clipped inverted master curve turns the green base magenta and leaves the blue alone until unclipped (then yellow); Hue 120 turns red green (±8), opacity 0 and a black mask keep the pixels; Multiply keeps the coverage `[255,128,32,0]` exactly and a 128 mask mixes half red half green at the same alphas; Levels, Curves, Exposure and Gradient Map match `Adjust::ApplyAdjustment` within 1/255, Hue/Saturation within 4, Grain within 2; tables and the cube count in `CanvasSurfaceBytes` and go with the surfaces |
| GPU adjustment previews (`adjust-preview.*`, `adjust.reuse.*`) | all six kinds match CPU filter + selection mix + normal composition at the above tolerances; translated/scaled layer, partial alpha, selection, mask, opacity, clipping, folder and upper/background records stay correct; committed readback ignores the transient preview; hidden/device reconstruction, clear and settled zero-pass frames work; settings reuse LUT/cube storage and kind switches release the previous lookup |
| HSL blends (C12, `blend.hsl-golden`) | Hue, Saturation, Color and Luminosity at opacity 1 and 0.5 match `Adjust::BlendStraight` composited over the opaque backdrop within 2/255 |
| Frame loop (RF-408, RF-409, RF-410, `ants.*`, `frame.*`) | the ants' dash steps every 120 ms through 8 steps and the loop waits exactly until the next step; occluded without the occlusion message probes, with it waits; a capture draws with the display off, the window hidden and occluded |

`CompositionTests` compiles the shaders with the application's flags (`/Ges /O3`, stripped), so it runs the same
bytecode. `RedPrism.exe --self-test --warp` proves the host side (`RunRenderingSelfTest`): Actual Pixels is 1:1 on
whole device pixels at the machine's DPI; ten 12-unit wheel deltas zoom 1.1 and a zero delta nothing; the Zoom
tool's drag zooms by its distance over eight moves and a click zooms in twice; a device removed during chrome
Prepare, the composition, the blit, chrome Composite or Present is rebuilt, rebound and drawn on, the window open; a
canvas that fails without device loss leaves the chrome presenting and notices once; a 17 000 × 300 document draws
through its proxy and the wand's composite reads it back at full resolution, a 17 000 pixel layer included; the
wand's composite reads the document under a live stroke preview; a curved three-sample stroke's adopted texture
equals its committed raster within 2/255 (RF-405); a gradient started on another layer while a stale preview remains
previews on that layer; a gradient left pending on one layer when another layer is chosen commits, its preview ends
by adopting its texture for that layer, the next layer's line previews on a texture of its own and Enter adopts that
one, and each layer then draws exactly as its committed raster (within 2/255, the other layer hidden; RF-1105); the
ants follow the outline; minimizing and occlusion drop the surfaces and the next frame
restores them; the window cannot shrink below the chrome around a minimum canvas and the crop shade of inverted
bounds draws nothing; the screenshot exit codes tell 8 (unsupported) from 9 (failed or never captured); three failed
frames stop drawing and post one `WM_CLOSE` that prompts, the window still open.


### Dormant worker-created raster residency

The optional worker creation route holds an owner-captured exact device/generation and permits resource creation
only when its successful actual threading feature query advertises DriverConcurrentCreates. A worker never
receives an immediate context. Texture/view failure and cancellation retain partial owners until owner retirement;
no resource may be borrowed before producer join and owner acknowledgement. Unsupported concurrency refuses
without an owner allocation fallback. It requires live whole CPU/GPU admission and a production fallback before activation.

GPU admission uses fixed unique nonwrapping reservation records, conservative overlap and explicit driver headroom.
The admitting domain outlives its reservations. Persistent reusable staging retains an independent charge; uncertain
copies and joined mapped shutdown retain the exact request's charge until owner graphics/context release.
Only a final successful original-ticket callback may transfer the created destination, held source Raster and
reservation. All old job graphics references are removed before charge transfer. A base-only request leaves
lower mips undefined;
no sampling or display publication precedes the consumer's admitted complete mip/fence stage. Full document
resources, actual DXGI snapshots, consumer/fallback integration and first-display G1 remain unqualified.

### Dormant explicit raster mip recording

An optional complete-chain request creates its bounded parent SRV/next-level RTV pairs and
shader/sampler/rasterizer/depth objects on the capability-qualified worker, observing cancellation between
creation calls. An owner pump records at most one linear-clamp premultiplied BGRA/R8 parent-to-child draw and
arms a real completion event. Only acknowledgement resumes the original ticket to advance another level. No
mip stage copies pixels on the owner, maps, polls or creates resources. It changes the immediate-context
state; pump outside frame recording and fully bind following consumers.

Partial/faulted/cancelled chains cannot be adopted as complete. All raster CPU/GPU reservations remain held
through the last level and uncertain signal/shutdown references. Final successful adoption carries
MipsComplete authority, which moves and clears with the owner. Future graphics users still require their own
completion before retirement. This is one raster chain, not complete document/display qualification. Whole
graph admission, shared-context consumer/fallback tests, live activation and original first-display/G1 remain
required.

### Dormant committed-resource description before GPU admission

A separate CPU-only planning admission MUST reserve the immutable source/root, complete CPU plan/descriptor/hash
capacities and serial upload scratch before capture. Worker preparation deduplicates owned raster layouts and
live coverage keys, describes two mipped canvases, possible clipping stacks, enabled group coverage and per-kind
adjustment resources. Missing live-source IDs need blank coverage. Tables are 256x1 RGBA32F; HSV uses a 33-cubed
RGBA32F texture; grain needs no lookup. Distinct group/live roles retain their ID even when they name the same
layer. Hidden inputs and unused potential stacks may conservatively overcount. Preparation neither reads or
materializes source pixels, computes adjustment lookups, nor creates/records/publishes GPU work.

The completed scalar requirement includes these logical textures and declared staging/driver allowances.
Before any GPU creation the graphics owner MUST reserve the complete described graph against actual adapter
usage and live/retired reservations. CPU admission/source ownership survives both phases, cancellation, refusal,
partial output and deferred worker retirement. This description cannot approve driver usage or replace the
separate preview/brush/utility/pipeline graph and its CPU/device metadata. Original conservative two-domain
RasterResidencyAdmission/Prepare remains available with its original contract. This dormant description alone
does not qualify live display integration, oversized mapping, complete resource creation/fallback or G1.

## Opt-in recorded committed display

`CommittedDisplayController` has two fixed owner cells: one published cache and an admitted replacement or
retiring graph. This route is enabled only by the hidden application qualification fixtures. A separate pending controller
is described below. Normal application display, pending previews, strokes and utilities retain their existing
consumers until their separate admission
and scenario gates close. Helper integration does not qualify H4/P1 or a first-display latency.

The original scheduler record admits CPU source/manifest/scratch before capture, plans resources on a compute
worker, then grows its complete reservation before graph containers, command shadows and software textures.
The container declaration is 4,096 bytes per current/seed layer plus eight MiB fixed scratch; its production peak
still requires measurement. Software logical textures are additionally charged to physical/commit admission.
The complete GPU description requires a fresh successful actual DXGI segment query, including current usage,
old reservations and headroom. Unknown/zero budgets refuse; no guessed segment admits. Hardware selects local;
software selects a positive available nonlocal budget, otherwise local.

`RecordedDisplayGraph` owns its immutable source, composition resources, recording policy and private context.
Preparation requires actual `DriverConcurrentCreates`; command batches remain bounded through the original
two-slot transport. Every batch executes on the captured original context and acknowledges its actual GPU use.
The recording callback must join before transport retirement, publication and seed retirement. Mips are eager
before sealing. Published graphs admit only owner blits on their actual device/context; pan/zoom does no Compose
or mip generation. A blit installs the full target scissor after tiled recording. The renderer's opt-in route
never falls back to owner composition while a replacement waits or refuses.

Publication transfers the preclaimed CPU retention slot before pointer adoption. A different document/device
or a cleared document excludes old pixels immediately, even while an unfinished replacement holds a seed borrow.
Same-document revisions can retain the previous complete display until replacement. Retiring a cache first
excludes future frames, then records an original-context barrier after its last blit. Failed healthy signals
remain charged and dormant; an actual device change arms one recovery attempt. Actual device removal permits
destruction only. Graph/source/context destruction runs on the existing noncancellable worker release sink;
both CPU and GPU charges survive its fixed-cell acknowledgement.

Shutdown stops new uses, joins actual producers, acknowledges submitted original uses for destruction, detaches
borrowed owner routes and retires roots on the final MTA worker before refund. An initialized source whose
recorder never started has a separate empty-join path: stopped, no producer thread, no command slot owner and
no prior finish/retirement authority. Only the actual post-join owner hook may use it; it publishes cancelled
destruction and supplies no successful pixels or useful receiver callback.
### Opt-in committed-display visibility and startup

The opt-in application route binds the actual device/context queue before its first immutable source request.
An unavailable initial context cannot count as a displayed or successfully prepared graph. The normal startup
route remains unchanged until the complete pending-edit, stroke, utility and row consumers are qualified.

Hidden, minimized, display-off or occluded state immediately removes the published display pointer and
cancels useful graph preparation. Existing graphs keep their original reservations through real command and
last-use acknowledgements, producer joins and worker destruction. Repeated requests while suspended admit
no replacement and create no ready loop. One actual visible-state transition requests the current document;
there is no synchronous composition fallback. A qualification fixture may explicitly enable hidden rendering.

### Opt-in original stroke preview parent

The experimental sample parent retains one original scheduler ticket and physical input ordinal through the
live gesture and final publication. A private graph owns its complete working grid, canvas/mips, immutable
source, recording context and command transport. Admit current/replacement overlap before creation. The new
grid copies the acknowledged previous grid and updates only changed tiles; the previous graph is immutable.
Publish only after the actual recorder joins and every original batch acknowledges. Frames then blit this
acknowledged preview; pan/zoom performs no composition. The committed-only controller cannot adopt a stroke
preview as its committed document cache.

Explicit source width/height describe a nearest stretch onto the stroke grid. An unplaced mask of different
pixel dimensions uses `floor(localCoordinate * sourceDimension / placedDimension)`, matching the brush's
existing nearest reference. Source/fill assembly remains worker work. A previous preview is compatible only
when its exact source identity, placement dimensions, grid, target and transform agree.

Before retiring a previous preview, exclude later frames and acknowledge the original last-frame barrier.
Keep the original completion context through final coverage and final display publication: retiring density
does not acknowledge a preview frame. Cancel and device replacement stop useful work, retain original owners
through real joins/acknowledgements, and retire complete roots off the owner. Ordinary startup remains disabled
pending the actual mode/fault matrix, full qualification and measured G1.

### Private recorded Warp source previews

An optional owning `StrokePreview::warpSource` represents complete immutable document-grid bytes: the exact
placed original source plus complete touched tiles prepared by the worker factory. Its completeness certificate
cannot be copied into a mutable descriptor. Source identity, original layer placement, document dimensions,
image target, transparent fill and identity document-grid placement must agree before private resource creation.
Mask-target, incomplete, mismatched and pre-cancelled descriptors refuse; an owner call cannot sample this source.

The first private grid samples the placed original through bounded upload bands on the recording worker, with
cancellation checked between rectangles of at most 256 by 256 pixels. It never materializes a full CPU image or
an unmaterialized sparse source. A compatible next graph inherits the exact acknowledged previous grid and writes
only changed tiles; capture changed tiles before the complete snapshot factory consumes the draft's dirty flags.
Each graph retains its immutable source, including the old source held by the published seed.

The original parent separately admits source/draft/snapshot ownership, source windows, upload scratch, both
private grids and mip chains, complete document/stack graphs, command shadows and all last-frame retirement
overlap. Logical WARP allocations also remain charged to physical/commit memory. The fixture's 128 MiB peak
is a declared test reservation, not a production admission estimate or a responsiveness measurement.

The recorded-parent fixture uses literal translated, scaled, 180-degree rotated, unmaterialized sparse and blank
sources in both Liquify and Smudge modes. It reads actual first/touched/committed GPU composites at document
resolution. Initial pixels match the independently placed literal bytes exactly; touched and committed pixels
match the original contiguous scalar Warp exactly. The old published grid must remain unchanged. Every graph
publishes after actual original command acknowledgement and retires on the worker after the real last-use barrier.
WARP is required; unavailable hardware is recorded explicitly without a hardware pass claim. Ordinary live
Warp routing and measured G1 remain separate acceptance gates.

### Private recorded adjustment previews

`RecordedDisplayGraph::PrepareAdjustmentForWork` prepares only on the admitted recording worker.
The graph copies owning source, selection and lookup handles; no caller stack metadata survives recording.
The original parent joins recording, acknowledges every original command batch and retires transport before
publication. It excludes subsequent frame/seed borrows and acknowledges the last original-context use before
worker destruction. The committed-only controller MUST NOT adopt this pending graph as a committed cache.

Destructive previews require the exact source raster identity in the target layer, BGRA8 pixels, a supported
lookup kind, finite scale in (0,1], and optional same-grid Gray8 selection. Pass 8 runs on the layer grid before
ordinary transform, opacity, mask and blend passes. Grain uses layer-pixel positions divided by preview scale.
Live adjustment previews require the retained original adjustment and source target to agree; they retain
passes 6/7 and document-space grain. Unknown kinds, incomplete HSV cubes, invalid targets/grids/scales and
pre-cancellation refuse before private graphics creation. Owner calls refuse before metadata traversal.

The parent separately admits the private target and its mips, selection texture/mips, lookup, upload scratch,
containers, command shadows and all current/seed/replacement owners. A committed-only manifest is insufficient.
Software logical graphics allocations also remain charged to physical/commit memory. The fixed 128 MiB test
reservation is a harness declaration and cannot serve as production admission or a responsiveness measurement.
Ordinary pending-edit routing remains disabled until host admission, retirement and G1 qualification close.


### Opt-in pending-display controller

The Application qualification route owns separate two-cell committed and pending controllers, sharing one
GPU reservation ledger and one startup command pump. The Application owns that pump's shutdown; an individual
controller cannot stop or close the shared transport. Each controller permits one active replacement;
a cached graph has already detached its recording source. Existing two-borrower transport capacity still
requires actual next-operation coexistence qualification before ordinary startup activation.

`DescribePendingDisplaySource` borrows only a completed deferred color/adjustment or transform source.
Description MUST NOT build pixels, lookups, materialized rasters or replacement documents. It supplies complete
document authority, kind, opening, publication, retained layer-root identity and optional prepared lookup/source/
coverage/original-adjustment fields. Refusal leaves its output unchanged. The controller revalidates those
borrows immediately before admitted owning capture. Unsupported draft kinds refuse; they cannot silently use
the committed document as a substitute pending preview.

Pending keys and owners remain distinct from committed keys. They cannot lend a committed seed or accept final
document publication. Worker color ticks keep the committed generation/root/history unchanged; a compatible
acknowledged preview may remain visible while newer settings prepare. Preview off, a different opening,
document/device replacement or suspension excludes incompatible pixels before the next frame. Accepted Cancel
also excludes pending pixels immediately, keyed by its exact document/opening and pending receipt. The closure
still retains its original panel/source until worker acknowledgement. Refused admission or a completed/stale
receipt cannot hide a new opening. Notify the pending controller to stop useful work and retire acknowledged
roots; exclusion alone does not authorize destruction or refund. An earlier
failure is reported only while its exact pending key remains current. Lookup-only destructive previews use
pass 8 before normal layer composition; live adjustments retain passes 6/7 and their original adjustment.

Before owning capture, reserve the base source/manifest and additional preview envelope: retained raster and
lookup capacities, original/copy curve storage, BGRA target and optional Gray8 coverage with complete mips,
lookup texture, bounded upload/reduction rows, container/view/driver allowances and command shadows. WARP
logical graphics storage is additionally charged to physical/commit admission. Fresh successful actual segment
admission covers old, replacement and retiring GPU reservations; a fixed fixture budget is insufficient.
Publication requires the actual original recorder join and every submitted GPU acknowledgement. Retirement
keeps the original owners and charges through last frame barriers and final worker destruction.

The opt-in renderer selects acknowledged stroke, then current pending, then committed display, and records only
a blit in the frame. It does not run owner composition as a fallback for this route. Shutdown excludes future
frames and preparation, joins both controllers' producers and waits, acknowledges destruction-only original
uses, retires graph/source/lookup/context owners on the worker, then detaches both controllers and closes the
one shared pump. Hidden fixtures and arithmetic controls alone do not qualify production peaks or G1.

### Experimental completed Gradient and Pixel Move display

The explicit display opt-in also accepts completed Gradient and Pixel Move CPU previews. Their pending descriptor
borrows the exact immutable `PreparedRasterPreview` handle and validates its bound original CPU accounting owner,
target, document authority, opening and acknowledged publication. The key holds a separate metadata identity;
the owning graph retains that metadata so storage addresses cannot alias. Ordinary startup remains disabled.

The CPU producer reserves the complete preview/source/cache peak and preclaims its retained-memory slot before
capture. Only its successful original completion binds that reservation, before session publication or lifted
opening-cache retention. Replacement previews refuse local admission rather than waiting on their own retained
cache. Failed or cancelled opted-in payloads also retire as complete worker-owned graphs, including small outputs.

A recorder reconstructs a complete snapshot from the immutable original source, fill and every current touched
tile. It MUST NOT seed these complete snapshots with the preceding stroke grid: a moved selection may omit a former
landing tile, which must return to the original source. The graphics stage separately admits its source/metadata,
directory/construction overlap, texture and full mip bound, bounded upload/reduction scratch, view/container/driver
allowances and command shadows. WARP additionally charges logical graphics storage to CPU admission. Actual fresh
segment admission and original recording/GPU/frame acknowledgements remain mandatory.

A real pending image may supply the first pixels of an otherwise empty layer; ordinary empty layers still cover
nothing. Mask pixels retain their original placed/stretched/uniform geometry and semantic thumbnail while previewing.
Commit publishes the worker's new eager semantic thumbnail, so a placed mask's exterior can change at its existing
edge threshold. This preserves the direct composition and commit rules. Returning a gradient to zero length or a
move to zero offset clears both CPU preview and metadata, excludes the old frame and requires fresh acknowledgement
before a resumed input becomes visible. A new opening cannot publish through a cancelled original ticket.

The actual hidden Application WARP stage uses independent image, mask and three-tile move literals, real Apply,
Cancel, Undo, zero/redrag, failure and reopen controls. The source-bound qualification and earlier failed fixture
are in `Specs/TestRuns/P1RasterPreview_2026-10-08.md`. These controls do not qualify large production phase peaks,
all remaining consumers, ordinary activation or measured G1.
