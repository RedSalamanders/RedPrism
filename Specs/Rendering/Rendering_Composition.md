# Canvas composition

Status: current normative product contract
Last reviewed: 2026-09-23
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
`ComposeReadback(document)`: the committed document (`DocumentSession::Document()`) flattened at full resolution,
never the display composite, a panel's preview raster or the stroke preview (`EndStroke`'s working texture stands in
for its layer only in the display). It marks the display dirty, so the next frame composes the display document
again (legacy `sampleCompositeColor` draws the document itself).

The host takes the `ComposeReadback(document, Raster&)` overload: staging rows are written into one 64-byte-aligned
`PixelBuffer` and adopted directly as BGRA8888, with no second full-image CPU copy. The vector overload remains for
diagnostic consumers and shares the same composition/readback routine. Failure clears either output; partial pixels
are never published. Readback is still synchronous at this stage; fenced completion belongs to pipeline slice P1.3.

## GPU readback queue foundation (P1.3)

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

Only the owner records Copy, fence Signal/event registration/Flush or EnqueueSetEvent, Map READ DO_NOT_WAIT and
Unmap. EnqueueSetEvent already flushes. Registered waits publish an atomic ready flag and wake the shared job event.
Map occurs once after completion; WAS_STILL_DRAWING is a reported failure, never a retry. Successful delivery
lends immutable rows with an exact lease generation. Workers may copy them, and the owner unmaps exactly once on
acknowledgement. Duplicate/stale releases fail. A device rebind retires old pending/in-flight work; lent rows keep
their old context/texture/fence alive until worker acknowledgement, and their bytes remain charged against the
same staging cap. No command is recorded on a retired context. Consumers validate returned device generations
before publication. Shutdown disarms/joins wait producers, joins copying workers, then unmaps/releases leftovers.

This foundation is exercised without a window or Present. Production readback callers still use the synchronous
path above until P1.3's utility-composition and host migrations land; it does not establish application-wide
nonblocking readbacks or automatic restart after device loss.

## Failures

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
