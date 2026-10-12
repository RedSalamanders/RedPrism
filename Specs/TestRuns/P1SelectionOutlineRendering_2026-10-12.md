# P1 selection outline rendering experiments

## Checklist

- [x] Run corrected independent Direct2D geometry/factory diagnostics in isolated Release.
- [x] Verify CPU WIC premultiplied pixels transfer exactly into owner-created GPU bitmaps.
- [x] Verify one solid/eight dash A8 planes with independent numerical/source-over expectations.
- [x] Qualify the private actual application CPU diagnostic in Release r4: three fixed contour sets and 15 first/warm,
  phase, pan and zoom observations separate path creation/append/Close and solid/dash drawing from inclusive
  `RenderFrame`. Private Release r1's C1090 compiler refusal remains separate historical evidence. Unchanged-source
  private Release r2 compiles with zero warnings/errors and its linked ISA scan passes, but actual WARP application
  child 92040 exits 5 from 2026-10-12T02:48:51.571755Z to 02:49:33.196972Z. It fails
  `selection-outline-cost.actual-WARP-renderer-and-chrome-initialize` before any actual-outline diagnostics or guard
  test completes. No G1 result or P1 closure is claimed.
- [ ] Qualify the diagnostic on the integrated source and remaining configurations; long-contour and
  G1 document observations remain due. The private r4 success below does not qualify a replacement route.
- [ ] Add view/DPI/clipping/anchor, long contours/holes and source/device races to the candidate route.
- [ ] Implement and qualify complete original-ticket admission, bounded uploads and CPU/GPU retirement.
- [ ] Integrate every producer/ordinary display consumer and measure inclusive owner/apply/frame G1.

These are standalone WARP experiments under ignored `.build/P1Drafts/`; no production route, full configuration
matrix, ARM64 runtime or P1/S5 closure follows. Main authors the C++ and independently reviews the cheaper
runner adaptations. Native source and DxUi pin remain unchanged by these experiments.

## Geometry and factory observations

### Actual application diagnostic — Release r4

The new independently owning fixtures now run in the existing standalone startup phase, before the main
application installs its one-per-thread release sink and registers the window class. The original r2 runtime
failure is retained. R3 exposes missing `KeyboardState.h` includes in the two new fixture translation units;
its compiler result remains failed. R4 adds those declarations, retaining scoped synthetic input and every
original/new assertion, source expectation, compiler flag and pin.

R4 compile child 45160 exits 0 with zero warnings/errors and linked ISA passes. Actual WARP child 63304 exits 0
at 03:11:33.650660 UTC after starting 03:10:09.076823 UTC. Independent exact reader verifies all original
7,399 stdout and 7,556 stderr assertions in order, 131 original stages plus two new standalone stages,
34 timing assertions, 18 opacity refusal assertions and all 15 predeclared observations. Qualification SHA-256
`C81BF5008A90B1ACC01A0295709598701FBB3B74A6829170092B96D66E66FE95`; private source manifest
`32A584F23595A1EEB96C260CA0437FC905FCB3E481046F8C48FB448A95327034`.

The fixed grid has 32,768 closed four-point contours. Its first path append is 7.0578 ms; creation/Open and
Close are 0.0029/0.0006 ms. Solid/dash call intervals are at most 0.005/0.001 ms across the five grid frames.
Inclusive `RenderFrame` is 70.5600 ms first, 50.9925 ms next phase, 61.7269 ms cached phase zero,
57.1592 ms after pan and 141.9893 ms after zoom. Most observed cost therefore follows the draw-call returns;
the diagnostic does not attribute it to a particular internal Direct2D/GPU operation. Rectangle/hole frames
are 1.4741–3.2555 ms. All phase/view changes reuse one path per source as required by the current contract.

These hidden WARP wall-clock intervals include the actual renderer/chrome path but do not establish GPU
completion, scanout, visible presentation or G1. They justify further work on prepared coverage with bounded
uploads and inexpensive animation. View/DPI parity, original-ticket ownership, opaque memory/cancellation
intervals and every producer remain required before selecting or activating the replacement.

The initial hollow-figure `GetBounds` expectation fails after successful compilation and remains preserved.
Main corrects only the diagnostic to independent solid one-unit widened stroke bounds; custom fixed strokes
remain unchanged. Corrected source SHA-256
`9F73BC26D41D5E7B309187A55599A730493AE7950CB32E195901EC4EC5D4A20E` passes: compile child 88864
and runtime child 28284 exit 0, receipt `SelectionGeometryInterop-r1-Main/.out/d839f15ffbf64842acff21750b144ff6`.
Line length 48 and triangle length `48 + 2 sqrt(24² + 48²)` pass; the fixed 32,768-square case has 131,072
segments and length 2,621,440. Same-factory draws succeed; separate worker single/multithreaded factories return
`D2DERR_WRONG_FACTORY`. Large first/warm draw observations are 68.9/72.4 ms, not application latency gates.

## CPU raster transport

Preserve the initial `near` Win32 macro compilation failure. The local helper rename retains every expected value.
Fresh Release compile child 62124 and runtime child 31336 exit 0; receipt
`SelectionOutlineRasterInterop-r1-Main/.out/ac7b431e5be843f2afff44642257ea80` freezes source/runner/dependencies.
Joined worker pixels transfer byte-for-byte through owner-created bitmaps. Fixed line boundary RGB/alpha
64/192, white gap 128/128 and transparent origin pass. Full line, triangle and large CPU/WARP reference images
also have zero observed differing bytes on both factory types; this does not establish untested view/phase parity.

## Nine-plane animation transport

Actual Release compile child 63616 and runtime child 83768 exit 0 at 00:00:23–00:00:29 UTC. Frozen source
`95386678D42A91B2D23DC47E5859008886EE930A8801C3D5047D913A928190CF`, runner
`41A0053A366ED96640251483F288BF7EA3028946342CE9FE129B6B59D86FDBDD`; receipt
`SelectionOutlineMaskInterop-r1-Main/.out/04fb07949e3448e6885fe10098b550f5/run-receipt.json` SHA-256
`E9791CBEB149430097FB2D14DC99DD0887E77AE0D8A9052F2DE4DB2949EBDE8C`.

Each worker rasterizes one white solid coverage and eight dash coverages with one reused WIC surface. Owner-created
A8 bitmaps need only two opacity-mask fills per phase. The independent integer source-over reference was declared
before execution, allowing at most one byte of quantization error: all 48 phase/image comparisons have maximum
delta zero. All 16 literal line phase/transparent-origin checks pass; earlier geometry/raster controls remain passing.

At 2048 by 1024, nine A8 planes retain 18,874,368 bytes per CPU/GPU copy, versus 67,108,864 bytes for eight composed
BGRA frames. Observed worker preparation is 880.5/791.1 ms; raw upload submissions are 1.34/1.21 ms. Readback Map
waits remain in the diagnostic. These measurements do not include the application's ownership, first display,
history/chrome synchronization or presentation and cannot satisfy G1. View/DPI changes must regenerate fixed-DIP
coverage rather than scale cached dash lengths; original-ticket admission and retirement remain mandatory.

The aliased rendering mode required by [Direct2D opacity-mask fills](https://learn.microsoft.com/en-us/windows/win32/api/d2d1_1/nn-d2d1_1-id2d1devicecontext)
is restored after the experiment's already-rasterized masks are painted. Production integration remains pending.
