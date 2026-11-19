# RedPrism Adversarial Review

> Publication note (2026-10-21): historical, non-normative static review of the paused
> `97dd25b70e83f81c339a4b4e7e7b7589268f2510` snapshot, retained for follow-up triage.
> Index status is **RETIRED** as a work queue. Findings and line anchors below describe
> that snapshot. A 1 November 2026 check on `e79a83a` found `ComposeReadbackRows`,
> `CenterCoverage`, `LoadZip`, and `SaveContents` still present at later lines, and found
> `CompositionTests.cpp` and `HostTests.cpp` gone as single files. Do not patch from the
> line numbers in this file. Source-traced labels are not runtime reproduction, measured
> performance, or current release qualification.

Date: 2026-10-16. Branch reviewed: `codex/ui-never-blocks-paused-2026-10-03`
at HEAD `97dd25b`. Scope: Common/, RedPrism/, Tests/, build scripts,
Specs/ contracts, HLSL. The archived behavioral reference Swift tree is a behavioral
reference only and was never treated as build input.

Method: eleven scoped reviews (inventory, correctness/memory,
concurrency/jobs, GPU/rendering, document I/O security, CPU kernels,
app/Chrome/Win32, architecture/simplification, tests/build quality,
recent changes) followed by an independent skeptic pass over the
Critical and High claims. This report keeps only claims the skeptic
CONFIRMED or that carry inspected `path:line` evidence from a body
read. Claims the skeptic REFUTED are dropped (two sub-claims, named
in Section 8). PROVEN means the code path was traced in source;
SUSPECTED means the mechanism is traced but the trigger or magnitude
was not runtime-proven. No reviewer ran a build, test, profiler, or
hardware repro; every magnitude below is loop-bound or queue-bound
arithmetic from source, not a measurement.

## 1. Verdict

**Performance assurance: do not trust the green narrative; the idle
path is excellent, the busy path is not.**

RedPrism is one of the most defensively written raster applications
a reviewer is likely to meet, and its settled-document path is
genuinely first-class: `CanvasRenderer.cpp:1970-1973` proves a
settled document costs zero GPU passes, `WM_PAINT` only invalidates
(`Application.cpp:5366-5369`), and there is exactly one device, one
flip-model swap chain, and one `Present(1,0)` (`Renderer.cpp:313-363,
563`). The problem is that the same codebase blocks the UI thread on
ordinary user gestures. One eyedropper, wand, or clone click
recomposes the whole document tile by tile and blocks on a
`Map(READ)` per tile, then forces a second full recompose
(`CanvasRenderer.cpp:2222, 2351, 2253`). Every brush pointer move
dispatches GPU coverage and immediately waits for the GPU pipeline
to drain (`BrushCompute.cpp:268-289`), re-renders every layer over
the full document rectangle (`CanvasRenderer.cpp:2007, 2020`), and
regenerates full mip chains (`CanvasRenderer.cpp:1159`). The CPU
fallback that is supposed to protect WARP and correctness work is
scalar-only, single-threaded, and allocates full-canvas state for
local selections (`Selection.cpp:880-892`). None of this is hidden:
the rendering spec admits the readback debt at
`Specs/Rendering/Rendering_Composition.md:207` and the kernel table
is explicitly staged at `Kernels.cpp:107`. But "admitted debt" is
still debt the user pays on every drag. Treat any "UiNeverBlocks"
claim as aspirational until the pointer-move paths in Section 2 are
fixed and measured with ETW/PIX, which nobody has done yet.

**Code quality: outstanding discipline, punishing scale.**

The mandatory `AGENTS.md` rules are obeyed to a degree rarely seen:
no `catch(...)` anywhere, no manual COM `Release`/`AddRef` misuse,
WIL RAII throughout, untrusted-size math budget-checked in 64-bit
before allocation, `Raster::Identity` caches holding the `Raster`,
chrome strings through `STRINGTABLE`, and zero TODO/FIXME debt in
first-party code. Reviewers who came hunting for memory corruption
found no proven Critical or High memory-safety bug. The cost of that
discipline is over-engineering that now hurts reviewability:
`Application.h` is a 1,158-line header with 44 includes and 744
members, `Document.h` defines roughly 135 types in 2,732 lines, a
single menu click travels Input -> Work -> Prepared -> ApplyIfCurrent
-> Continuation across four or five files, and 17k lines of self-test
and fault-injection code compile into every configuration including
Release (`RedPrism.vcxproj:199, 255`). The branch compounds the
confusion: its last 30 commits touch zero lines of C++ or HLSL, and
TestRuns prose cites commits that are ancestors of neither HEAD nor
`main`. A new engineer cannot tell shipped behavior from narrative.
The codebase does not need a rewrite; it needs deletion, header
surgery, and a hard separation between product and test payload.

## 2. Findings, ranked

Severity reflects the skeptic's adjudication where one exists.
Nine High claims the skeptic did not reach (12-string cap) keep
their original severity and are marked "not skeptic-adjudicated".
There are no surviving Critical findings: the sole Critical claim
(scalar-only kernels) was downgraded to Medium perf debt because
it is an explicitly staged plan item, not a correctness failure.

### Critical

None proven. Reviewers hunting buffer overflows, use-after-free,
and untrusted-input corruption in Common/ and RedPrism/ found a
heavily defended surface (see the verified-clean list in Section 8).

### High — skeptic CONFIRMED

**H1. Eyedropper / wand / clone freeze the UI twice. PROVEN.**
`Application.cpp:3742` (eyedropper), `:6522` (wand), `:6662` (clone)
call `WandComposite:6545` -> `ComposeDocumentReadback:5061` ->
`ComposeReadbackRows` at `CanvasRenderer.cpp:2222`, which
recomposes every tile, blocks on `Map(READ)` per tile at `:2351`,
then sets `_documentDirty=true` at `:2253` and pays a second full
display recompose. Trigger: one click on an 8K-30K document.
Impact: seconds-long UI freeze for a single-pixel sample. The spec
admits the debt (`Specs/Rendering/Rendering_Composition.md:207`).
Fix: serve sampling from the settled canvas via a
`GpuWorkQueue`/`DocumentReadbackJob` region read; never recompose
to answer a point query, and never dirty the display composite for
a read-only tool.

**H2. GPU brush preview blocks the calling thread every move.**
PROVEN. `GpuBrushCoverage::Render` dispatches, copies, then maps
with flags 0 and no fence or `DO_NOT_WAIT`
(`BrushCompute.cpp:268, 271, 289`), called synchronously from
`BrushStroke::Render` (`BrushStroke.cpp:816`) on pointer move
(`Application.cpp:6769`). The preview buffer is also `uint`/px
(`BrushCoverage.hlsl:17`) versus legacy `uchar`
(archived behavioral reference), plus
a CPU narrowing loop at `BrushCompute.cpp:295-298`. Impact: every
stroke update waits for the GPU pipeline to drain, defeating the
event-based design used by `GpuWorkQueue`. Fix: fenced,
double-buffered readback one move behind; typed R8 preview UAV.

**H3. WM_SIZE does GPU reallocation inside the message handler.**
Code CONFIRMED, visual impact SUSPECTED. `Application.cpp:5108,
5426, 5434` calls `OnSize`, which synchronously runs
`ResizeBuffers` (`Renderer.cpp:434`) plus chrome `Layout`+`Prepare`
(`Application.cpp:2924, 3007`), while `Present` only happens in the
frame loop (`Application.cpp:445`) that is blocked inside the
caption-drag modal loop. The same-size early-out at
`Renderer.cpp:425` still clears `_occluded/_suspended` and does a
full `SyncChrome` because `OnSize` discards `wParam`
(`Application.cpp:5107-5108, 5432-5435`). Impact: dragging a window
border repeatedly reallocates GPU resources and prepares chrome
without presenting; hidden windows do layout work. Fix: `OnSize`
records size and invalidates only; run `ResizeBuffers` and render
in the frame loop, ignore `SIZE_MAXHIDE`/`SIZE_MAXSHOW`, and never
clear occlusion on a no-op resize.

**H4. HSL blend golden grades the shader with its CPU twin.**
CONFIRMED as a test-hygiene gap. Expected pixels derive from
`Adjust::BlendStraight`, the product-code twin of the HLSL under
test (`CompositionTests.cpp:1248, 1277-1279`), violating the
AGENTS.md rule that goldens are numeric expectations from
retained numeric golden cases. Impact: a formula mis-ported identically
into CPU and HLSL passes forever. The parity check still has value
as a cross-implementation guard. Fix: keep the parity check, add
literal expected pixel values taken from archived behavioral reference worked
examples as the normative oracle.

**H5. Self-test and fault-injection code ship in every config.**
PROVEN. Roughly 39 `RedPrism/*SelfTest*`/`*Tests.cpp` files
(about 17k lines) are plain `ClCompile` entries inside an
unconditioned `ItemGroup` (`RedPrism.vcxproj:199, 255`), plus 71
`Run*SelfTest` declarations and 24 `*TestGate.h` headers reachable
from `Application.h:55` with entry at `Application.cpp:578`.
Recent branch work added 2,525 lines of the same kind plus
production branches that throw `bad_alloc`, post thread messages,
and wait up to 10s gated only by a test pointer
(`ApplicationLayerMetadataJobs.cpp:72-112`). Impact: Release
carries harness surface, bloats rebuilds, and gives an attacker or
curious user a `--crash-probe`/`--self-test` surface in the shipped
binary. Whether the linker retains it all was not verified.
Fix: `ExcludedFromBuild` for Release at minimum; properly, move
self-tests to `Tests/` against a host library.

### High — not skeptic-adjudicated (inspected evidence stands)

**H6. Full-frame CPU filters use one core. PROVEN.** Every Adjust
and filter kernel is a single serial y-loop
(`Kernels_Scalar_Adjust.cpp:89, 127, 183, 247, 301, 346`;
`ImageFilters.cpp:63, 105`; `Adjust.cpp:1371, 1517`), and no
`Executor`/`WorkerPool` use exists in those files. Impact: a
24 MP filter saturates one core while the pool idles. Fix: split
rows into Executor bands with per-band cancel checks; blur's
vertical pass bands on columns.

**H7. ComposeTile copies every brush tile twice per move.**
PROVEN. `BrushStroke.cpp:869` copies the 256 KB base tile into a
vector, `Raster::Make` copies again at `:961-962`, for every dirty
tile in `Render :822-832`; the inner loop also pays `%`/`/` per
pixel (`:886-887`), a full `Affine2::Apply` (`:888`), a
`SelectionAt()` lookup (`:892-894`), and `lround` per channel
(`:877-878`). Fix: compose into a pooled tile-owned buffer adopted
without copy; nested row loops; hoisted selection row pointer.

**H8. Selection coverage is O(height x edges) with redundant
affines. PROVEN.** `CenterCoverage` (`Selection.cpp:360-417`)
rescans every loop edge for every row, heapsorts crossings per
row (`:318-359, 422-425`), and re-applies the affine to both
endpoints per edge per row (`:390-391`). Trigger: a 10k-point
lasso on a 4000-row canvas means roughly 40M edge tests and 80M
affine applies for one rebuild. Fix: pre-transform loops once,
bucket edges by y-range into an active-edge table.

**H9. Selection coverage ignores the loop bounding box.** PROVEN.
The antialiased path allocates two full-canvas float grids, about
8 B/px — 192 MB at 24 MP, near 7 GB at the 30000-px canvas limit
(`Selection.cpp:880-892`; `Document.h:27`) — then scans all pixels
(`:266-287`); `CenterCoverage` likewise allocates the full frame
(`:296, 368-375`). Impact: a 100x100 selection on a huge canvas
pays full-canvas allocate/zero/scan. Fix: rasterize into a
loop-bounds sub-rect and composite at offset, or tile coverage.

**H10. Application and Document are god objects. PROVEN (counts);
split lines are opinion.** `Application.h:3`: 44 includes, 1,158
lines, 744 member declarations, 34 nested `*Job` classes;
`Application.cpp:1` is 8,094 lines. `Document.h:131-1730` defines
about 135 types in 2,732 lines and is included by 48 files.
Trigger: touching any `*Work.h` recompiles all 44 `Application*.cpp`
files; editing `WandSettings` (`Document.h:806`) rebuilds half the
tree. Fix: extract a HostJobs coordinator, forward-declare Work
payloads behind `shared_ptr`, split `Raster.h`, `AdjustTypes.h`,
`SelectionTypes.h`, `Session.h`.

**H11. ARM64 ASan gate never builds ARM64. PROVEN.**
`AsanArm64Tests.ps1:5-6` admits it "does not install or build
anything" and only regex-matches text at `:20-42`; `test.ps1:21-23`
refuses ARM64 runtime on x64; the mandatory gate (`AGENTS.md`
validation block) never builds ARM64 despite the
real-instrumentation requirement. Impact: an ARM64-only compile or
ASan break passes the full gate. Fix: add `build.ps1 -Platform
ARM64` for all configurations to the gate; run the ASan probe on a
native ARM64 host before any ARM64 runtime claim.

**H12. Branch narrative does not describe branch code. PROVEN.**
The last 30 commits (`97dd25b..ad99d7b`) touch zero `.cpp/.h/.hlsl`
files — only `UiNeverBlocks_2026-09-22.md` and TestRuns prose —
while `P1GpuMipChain_2026-10-05.md:13` claims code "committed/
pushed as `af7986f`" that is an ancestor of neither HEAD nor
`main`, and `git diff main...HEAD` contains no mip or `GpuWork`
code. Only 7 of the 44 branch-ahead commits carry code at all.
Impact: anyone trusting HEAD TestRuns as qualification of HEAD
code qualifies a phantom. Fix: land the private candidates or
strike/qualify the TestRuns entries as external experiments; never
cite them as HEAD evidence.

### Medium

**M1. No SIMD tier ships (downgraded from Critical). PROVEN debt.**
`Kernels.cpp:67, 108` always selects Scalar despite AVX2 CPUID
decode (`:24-32`); all six Adjust kernels are scalar float with
divides, `exp`, `sqrt/log/cos`, and 24 gathers per pixel
(`Kernels_Scalar_Adjust.cpp:111-116, 207-221, 275-278, 380-386`).
The skeptic downgraded this because it is explicitly staged
(`Kernels.cpp:107`, P0.8/P2) with no correctness failure. Fix:
land AVX2/NEON tables behind the existing dispatch when P2 is
scheduled; add an integer LUT path for ApplyTables/GradientMap.

**M2. Every edit re-renders every layer full-frame (downgraded
from High). PROVEN design cost.** `InvalidateDocument`
(`CanvasRenderer.h:44-48`) leads to `ComposeView`
(`CanvasRenderer.cpp:1985, 2007, 2020`) running a fullscreen
pass per layer, including per brush move
(`UpdateStrokeTiles` dirty at `:1162`) and per adjustment tick;
the shader shades and loads the destination at every pixel
(`CompositionPixel.hlsl:294-321`). Downgraded because the
settled-zero contract holds (`CanvasRenderer.cpp:1970`) and no
spec mandates a dirty rect. Fix (optional, highest-value GPU
optimization available): cache below/above-edit composites and/or
scissor passes to the layer/dirty AABB.

**M3. Brush CPU fallback rescans all segments per tile
(downgraded from High; "62M full evals/move" REFUTED).**
`RenderTile` (`BrushStroke.cpp:218-276, 261`) tests every settled
and tail segment at every pixel with no per-tile segment culling.
The original cost claim was refuted: `SegmentDensity` early-outs
before quadrature (`:191, 198`), so most tests are cheap rejects.
Residual: per-segment call overhead on the CPU/WARP path only.
Fix: bin segments per tile; squared-distance reject first.

**M4. GaussianBlur is naive and unbounded in the API
(downgraded from High).** Separable double loops, O(W*H*(2r+1))
(`ImageFilters.cpp:63, 76-97, 105, 117-139`), with
`kMaxSigma ~357M` at `:186` as an overflow guard. Downgraded
because the alarming sigma=100 trigger is unreachable: the
product caller clamps 1.5-30 (`BrushStroke.cpp:1940`). Fix: only
if large-sigma UI is ever added, switch to box/IIR approximation,
float math, and band parallelism.

**M5. Shutdown can hang on clipboard delayed render
(downgraded from High; one leg REFUTED). SUSPECTED.**
`WorkerPool::Close` joins running callbacks
(`WorkerPool.cpp:220`); a worker can block inside
`GetClipboardData` delayed render
(`ClipboardTransfer.cpp:198, 205`) if another process's clipboard
owner hangs, blocking exit forever. The second leg — that
`GetOverlappedResult(TRUE)` in PathClassification can hang — is
REFUTED: it follows `CancelIoEx` as a mandatory acknowledgment
(`PathClassificationWork.cpp:91`). Fix: isolate the clipboard
read on a sacrificial non-joined thread, or spec the hang.

**M6. Mips regenerate eagerly, and autogen support is assumed.**
PROVEN (eager part) / SUSPECTED (missing probe impact).
`UpdateStrokeTiles` regenerates the full mip chain after every
tile batch (`CanvasRenderer.cpp:1159`); uploads do the same for
all rasters and masks (`:803, 957, 762`), unlike lazy canvas mips
(`:2268-2272`). No `FORMAT_SUPPORT/MIP_AUTOGEN` probe exists
(`GpuCapabilities.cpp:130-136` probes only UAV store) despite the
spec promise at `Specs/Rendering/Rendering_Composition.md:40-41`.
Impact: full-texture work per pointer move; on a driver/format
without autogen, uninitialized mips get sampled. Fix: per-texture
stale flag generated only when the sampled footprint is < 1, and
probe `D3D11_FORMAT_SUPPORT_MIP_AUTOGEN` at device creation.

**M7. Hot paths allocate per move, per compose, per paint.**
PROVEN. `BrushStroke::Render` builds two `unordered_set`s plus
vectors per point (`BrushStroke.cpp:793-814`); `BuffersFor`
allocates zero-fill vectors and three `CreateBuffer`s per new tile
mid-stroke (`BrushCompute.cpp:128-172`); `ComposeView` allocates
layer-id lists per compose (`Document.cpp:1277-1288` via
`CanvasRenderer.cpp:2007`); `LevelsBar::HandlePositions` is
`noexcept` yet returns a heap vector on every paint and curve drag
copies point vectors twice per move
(`AdjustControls.cpp:280-297, 313-321, 379`; `.h:127`). Fix:
reusable member scratch, pooled tile buffers, `ClearUAV` zeroing,
small fixed arrays with out-params for control geometry.

**M8. GPU readback and partial init have no failure exit.**
PROVEN. Readback rearms with timeout 0, meaning wait forever
(`GpuWork.cpp:198`; `Jobs.h:192`; `WorkerPool.cpp:384`): a hung or
removed GPU wedges `DocumentReadbackJob` in Suspended
(`DocumentReadbackJob.cpp:268`) holding its slot, and export never
finishes. Partial `Initialize` failures leave non-retryable
zombies (`GpuWork.cpp:296`; `RetryWaitQueue.cpp:89`;
`Jobs.cpp:517, 568`; `WorkerPool.cpp:103`): one transient quota
failure in the shared 32-slot wait pool bricks readback for the
process lifetime. `StopNotifications` discards `RemoveEventWait`
failure (`GpuWork.cpp:427`) although removal is refused inside a
UI callback (`Jobs.cpp:835`), then frees slots at
`GpuWork.cpp:452` the wait pool may still reference — a
use-after-free route if stop is ever requested from a callback.
Fix: bounded timeout then `GetDeviceRemovedReason` +
`DeliverFailure`; unwind and reset on failed init; check removal
and forbid stop-during-callback as `RetryWaitQueue.cpp:233` does.

**M9. Message pump starves presenting; render failure sleeps
forever. PROVEN.** `PumpMessages` drains `PeekMessage` without a
cap or mouse-move coalescing (`Jobs.cpp:1085-1105`;
`Application.cpp:326-332`), so a 1000 Hz brush drag or posted-
message flood starves `Present` and job completions. A failed
`RenderFrame` clears `_frameInvalidated` and the loop then waits
infinitely with no retry wake (`Application.cpp:423-426,
480-492`): one transient HRESULT freezes the canvas until the next
input. Fix: cap dispatches per iteration and coalesce moves; on
failure keep invalidated with bounded backoff until success or
the failed-frame close threshold.

**M10. Chrome arranges three times per frame; shutdown can
overwrite a settings edit. PROVEN.** `SyncChrome` calls `Layout`
then `Prepare` (which lays out again), then `Render` prepares a
third time, with no dirty check in `Arrange`
(`Application.cpp:2924, 3007`; `ChromeHost.cpp:3122-3142`;
`Renderer.cpp:519-522`). Separately, shutdown `SaveSettings`
writes in-memory settings without reloading first
(`ApplicationSettings.cpp:233-262`; `Application.cpp:3418-3419`),
silently discarding an external edit made within the 250 ms watch
window (`ApplicationSettings.cpp:196`). Fix: dirty-flag arrange
on size/DPI/structure change and drop `Prepare` from `SyncChrome`;
merge live file state at the top of `SaveSettings`.

**M11. .prism zip load has no aggregate budget. PROVEN.** Each zip
entry is capped at 512 MiB (`ProjectStore.h:24`) but nothing caps
the total: `LoadZip` (`ProjectStore.cpp:1411-1420`) fully inflates
every entry via `ReadZipEntry :1250-1279` before PNG dimensions
are checked, and `AttachAssets :1293-1308` invokes the loader
even when the pixel budget is already spent. Trigger: a project
with thousands of 1-px PNGs padded toward the per-entry cap burns
unbounded inflate CPU and allocator churn. Fix: cumulative
encoded-bytes budget, skip the loader when `remainingPixels==0`,
probe the PNG header (as `ReadProjectAssetHeader:1957` does)
before inflating an entry.

**M12. Wrong-typed manifest fields silently delete layer pixels.
PROVEN.** `ParseLayer` discards typed-read results at
`ProjectStore.cpp:712, 722, 723, 734`, so a present-but-wrong-typed
`imageFile`/`maskFile` loads as an empty layer with `Ok`, and the
next save permanently drops the asset — violating the MUST at
`IO_ProjectFormat.md:120`. Fix: when the key is present, a failed
typed read returns false, matching `maskEnabled` at `:736-740`.

**M13. Clipboard save copies without a cap. PROVEN.**
`SaveContents` (`SystemClipboard.cpp:440`) copies every memory
format at full `GlobalSize` with no per-format or aggregate limit
(callers `Application.cpp:1877`, `ApplicationFiles.cpp:1561`).
Trigger: another app places a multi-GB custom format; the UI
thread allocates it while holding the clipboard open. Fix: enforce
a budget and fail the save instead of copying oversized formats.

**M14. Layer-metadata "worker" still copies the document twice
on the UI thread — and misses half its call sites. PROVEN.**
`PrepareLayerMetadataInput` copies the whole document on the
owner thread (`LayerMetadataWork.cpp:39`;
`ApplicationLayerMetadataJobs.cpp:133`), the worker copies again
(`:110-116`), and any non-settled document falls back to
synchronous legacy edits (`:238-254`). Keyboard opacity digits
(`ApplicationKeyboard.cpp:504`) and typed opacity commit
(`ApplicationLayerEdits.cpp:132-134, 188-190`) never use the
worker at all, while `DocumentSession::StepBlendMode`
(`AdjustEdit.cpp:653`) is now dead product code whose math and
history titles were copy-pasted into two other files
(`Application.cpp:4483-4486`; `LayerMetadataWork.cpp:83-99` vs
`Document.cpp:1779-2877`). Worst, the same rename succeeds or
fails by gesture state: the worker rejects over-long names
(`LayerMetadataWork.cpp:81, 141-142`), the legacy fallback does
not (`Document.cpp:1767-1782`). Fix: capture a record view, not a
document copy; route all opacity entries through one admission
path; enforce name validity once in `RenameLayer`; delete or call
`StepBlendMode` and share title constants.

**M15. Full-surface clears and per-pass state churn. PROVEN.**
`ComposeView` clears both ping-pong canvases
(`CanvasRenderer.cpp:2015-2016`) although the first pass
overwrites one fully; `DrawStack` clears before `StackBegin`
overwrites (`:1854`; `CompositionPixel.hlsl:511-516`); `RunPass`
rebinds all state, unbinds 7 SRVs, and DISCARD-maps a single
constant buffer per pass (`:1218-1247`); nearest-neighbor sampling
samples twice (`CompositionPixel.hlsl:261-279`). Fix: clear only
what survives, hoist invariant state per compose, single sampler
selection, scissor the blit to its hole.

**M16. MotionBlur and Heal are unbounded per-pixel loops.**
PROVEN. MotionBlur runs up to 96 double bilinear taps per pixel
(`Adjust.cpp:1356, 1371-1397`). Heal takes no cancel token
(`Heal.h:31-32`) and runs fixed 300/40 SOR iterations over the
whole work box (`Heal.cpp:85-211`) after up to 169 full-ring
scores — roughly 1.2B cell visits for a 2000x2000 spot. Fix:
prefix-sum line integration; residual-epsilon termination,
hole-index lists, threaded cancel token.

**M17. Warp and sparse paths pay whole-image copies.** PROVEN.
`WarpStroke` copies the entire layer in its constructor
(`WarpStroke.cpp:38-44`) and re-copies per dab into 4x float
scratch (`:250-269`); `Raster::Data()` silently materializes and
retains the full frame forever (`Document.cpp:434-447, 463-485`),
voiding sparsity; `Replacing` is O(old x new tiles) with per-pair
allocations (`:735-772`) and unmaterialized `ReadRegion` rescans
all tiles per row (`:549-552, 821`); `Materialize` holds
`assemblyMutex` across a whole-image build (`:458`). Fix: tile
storage for warps, spatial tile index, build-outside-lock publish,
evict the assembled cache for sparse storage.

**M18. Adjust pipeline is copy-bound; ContentFill allocates
before its empty check. PROVEN.** Every `ApplyAdjustment` kind
first copies the full frame (`Adjust.cpp:1137-1188`), `BlendThrough`
adds two more copies (`:1506-1537`), and `ContentFillUnchecked`
allocates seven full-frame arrays (16 B/px, 384 MB at 24 MP,
`:1758-1764`) before the `missing==0` early-out (`:1805-1808`).
Fix: fuse coverage blend into kernels, band over region reads,
scan mask bounds before allocating fill state.

**M19. Owner-thread discipline compiles away in Release.**
PROVEN. `CheckOwner` is assert-only (`Jobs.cpp:228`;
`GpuWork.cpp:76`; `RetryWaitQueue.cpp:39`) while `liveCount`,
`nextSequence` (`Jobs.cpp:97`), and `MemoryBudget::_reserved`
(`MemoryBudget.h:57`) are unsynchronized owner-only state. Impact:
any future worker-side Submit/Cancel is a silent data race.
Fix: runtime thread-id check returning an error on mutating APIs.

**M20. WarpStroke step count is uncapped. SUSPECTED.**
`WarpStroke.cpp:127` computes `(int)ceil(distance/spacing)` after
only an `isfinite` check (`:107`) and pushes every step (`:141`)
with no cancellation; callers never clamp near the canvas
(`BrushStroke.cpp:1997, 2009`). Trigger: a teleporting pointer at
low zoom yields 1e5-1e9 iterations, and beyond 2^31 the cast is
undefined behavior. Fix: clamp to an expanded canvas rect, cap
steps. Related signed-overflow shapes, currently gated by
`LayerTransform::IsValid` (`Document.cpp:848-854`), sit at
`BrushStroke.cpp:95-112` and `Document.cpp:359, 406`; compute in
int64 there as defense in depth.

**M21. Test-gate holes below High. PROVEN unless noted.** The
9-mode blend golden asserts one channel of one gray pixel
(`CompositionTests.cpp:366-386`), so channel swaps pass in 7 modes.
The kernel smoke only cross-checks two rows of the same run
(`test.ps1:194-225`), so self-consistent wrong math passes forever.
The screenshot gate passes with all nine cases skipped
(`test.ps1:396-401, 419`). `/analyze` never gates
(`build.ps1:95-99`). Legacy downsample quality thresholds
(`DownsampleTests.swift:46-64`) are unported (SUSPECTED gap).
See Section 6 for fixes.

### Low (grouped; each with its fix)

- **WindowCapture creates a second device (downgraded from
  High).** `WindowCapture.cpp:115` creates a HARDWARE device in
  Common, the never-GPU owner; skeptic confirmed harness-only with
  no swap chain, hence Low. Fix when touched: move to RedPrism/,
  wrap the Renderer device, delete the duplicate `WritePng`
  (`:40`) in favor of `ImageCodecs.cpp:470`. The same file
  suppresses seven warnings across 220 lines of own code
  (`:31-32` to `:251`): scope the pragma to WinRT classes.
- **Dead primitives (RecordRoot downgraded from High).**
  `RecordRoot.h:16`, `PointerInputStream.h:11`,
  `OpacityInputOrder.h:11`, `ContinuationQueue.h:112`, and
  `AddressSanitizerProbe.h` have zero product includers (tests
  only); `DocumentSession::StepBlendMode` is dead product code
  (see M14); `MergeLayers`/`PreparedMerge` (`Document.h:1828,
  1879, 1912`) have zero product callers. Runtime cost is zero;
  the cost is narrative confusion. Fix: wire in or move to
  Tests/Support; delete the dead merge generation.
- **Validation overflows before it validates.** `ValidateDocument`
  accumulates `int` pixel totals and checks after adding
  (`Document.cpp:1157-1189, 1201-1205`); about 22 legal max-size
  layers overflow before the check. Fix: int64 accumulators.
- **Reparse defense is fail-open and raceable.** `ReadFileBytes`
  rejects reparse points only when the info call succeeds
  (`ProjectStore.cpp:130-135`); `CodecFileStream::Open
  :36-48` never checks the handle. Fix: fail closed, verify the
  opened handle.
- **Embedded-NUL enum keys truncate.** `ProjectStore.cpp:470, 501`
  build a `string_view` from a C string, so `"reds\0junk"` matches
  `reds`. Fix: length-aware view, reject NULs.
- **Crash marker is trusted into Explorer.** `TakePending
  :226-249` returns file contents unvalidated into an
  `explorer /select` argument (`Main.cpp:316-317, 310-318`); the
  prompt is also unowned and can hide behind other windows. Fix:
  accept only `RedPrism-*.dmp` names in the crash directory; own
  the prompt by the main HWND.
- **Settings write can orphan the file; temp name is predictable.**
  `Save :510-519` moves the file aside before writing, so a failed
  write leaves defaults; the temp uses `+.tmp-<pid>` with
  `CREATE_ALWAYS` (`SettingsStore.cpp:579-582`, SUSPECTED).
  Fix: publish first, back up after; GUID + `CREATE_NEW` (needs
  `Core_Settings.md:78` amended).
- **Crash artifacts are not durable.** No `FlushFileBuffers`;
  second-resolution `CREATE_ALWAYS` names can truncate
  (`CrashDumps.cpp:166-186, 215-223`). Fix: flush, millisecond names.
- **Homography NaNs reach UB casts.** `TransformEdit.cpp:56-75,
  174-179` (same at `DistortWork.cpp:87, 93`): `clamp(NaN)` stays
  NaN into `(int)floor`. Memory-safe under MSVC, garbage pixels.
  Fix: reject non-finite centers before casting.
- **History retention under-charges nested sparse storage.**
  `TrimPrepared :758-777` admits the gap (`:767-768`). Fix: charge
  graph closure like `EditMemoryBytes :538-547`.
- **Renderer hard-requires an unused interface.** `CreateDevice`
  fails without `ID3D11DeviceContext1` (`Renderer.cpp:248-255`)
  although `_context1` is never used; recovery waits 1000 ms and
  flushes a possibly-removed context (`:638-652`). Fix: drop the
  requirement, skip the wait on removed devices.
- **Occlusion purges the raster cache.** `Application.cpp:541-552`
  plus `CanvasRenderer.cpp:155-160` erase all raster textures on
  cover/sleep (spec'd RF-411), so the first visible frame re-uploads
  everything (SUSPECTED hitch size). Fix: budget-retain rasters,
  drop canvas/stack promptly.
- **HLSL drift risks.** Double byte quantization
  (`CompositionPixel.hlsl:464-470`), float grain lattice at 30000-px
  coords (`:338-375` vs double CPU), blend mode as raw enum ordinal
  (`CanvasRenderer.cpp:1295`). All SUSPECTED. Fix: large-coord
  golden, shared constants with `static_assert`.
- **Build-script frailty.** `format.ps1:22-37` is ~35 files from
  the argv ceiling and excludes HLSL; `package.ps1:97-99, 150, 175,
  202` packages stale or incomplete zips green; `validate_skills.py
  :23-53` checks envelope, not content; vcpkg reproducibility rests
  on one baseline pin (`vcpkg.json:6, 10-22`, SUSPECTED). Fixes in
  Section 6.

## 3. Architecture assessment vs AGENTS.md ownership

The ownership table in AGENTS.md is mostly real, which is why the
violations stand out. Renderer owns the single device, swap chain,
and Present (`Renderer.cpp:313-363, 563`); CanvasRenderer borrows
the device and owns document-space passes; ChromeHost holds no
DocumentSession and lays out `Toolbar` by manual arrange
(`ChromeHost.cpp:1381`) without abusing `Toggle`
(`ChromeHost.cpp:832-836`); JobScheduler's public header stays
standard-only. Document code never owns a GPU device — with one
exception.

Violations and strains, in order of consequence:

1. **Common creates a GPU device.** `WindowCapture.cpp:115` calls
   `D3D11CreateDevice(HARDWARE)` inside Common, the owner AGENTS.md
   defines as "never GPU device ownership", for its sole caller
   `Application.cpp:5336`. It also duplicates the PNG encoder
   (`WindowCapture.cpp:40` vs `ImageCodecs.cpp:470`). Practical harm
   today is harness-only (hence Low), but it breaks the one-device
   mental model: on a forced-WARP host, capture can fail or use a
   different adapter than the renderer. Move the file to RedPrism/
   and wrap the renderer's device.
2. **Application owns everything, so ownership means little.**
   `Application.h:100` owns window, renderer, session, scheduler,
   clipboard, and benchmark, with 34 nested Job classes and 744
   members. AGENTS.md assigns Application the widest remit, but the
   header turns that remit into a compile-time and review-time
   monopoly: every feature lands in the same TU graph (80 touches
   in the last 150 commits for `Application.cpp`, 75 for `.h`).
3. **Document.h collapses the Document owner's internal seams.**
   Raster, adjustments (`:387`), selection (`:653`), brush
   (`:1037`), history (`:1419`), and session (`:1730`) share one
   header included by 48 files. The AGENTS.md split into
   `Document*`, `Selection`, `BrushStroke`, `Adjust*`, `ProjectStore`
   translation units exists, but the single header forces them to
   co-evolve anyway.
4. **Test code lives in the product owner.** Self-tests, 24
   `*TestGate.h` headers, and fault hooks in production files
   (H5) invert the intended direction: product code knows about
   tests. Four headers in Common/ are test-only as well
   (`RecordRoot.h`, `OpacityInputOrder.h`, `PointerInputStream.h`,
   `AddressSanitizerProbe.h`).
5. **Two GPU upload/readback disciplines coexist.** `GpuWorkQueue`
   does fenced, event-based transfers correctly, while the brush
   engine and the sampling tools block synchronously (H1, H2). The
   architecture has the right owner for this work; two callers
   bypass it.
6. **Header hygiene breaches.** `Utf8.h:5` includes `<windows.h>`
   (AGENTS.md bars Win32 typedefs in non-Win32 headers) yet five
   sites hand-roll the same conversion anyway;
   `ChromeHost.h:8, 15, 27` pulls `Document.h`, `<d3d11.h>`, and
   four DxUi headers into a 1,474-line header, and duplicates
   `BrushSettings` as `BrushInspectorState :116` in different units,
   requiring manual two-way sync.

What deserves credit because it makes the above fixable: WIL RAII
and `com_ptr_nothrow` discipline held everywhere reviewed; the
`Raster::Identity` rule (caches hold the Raster) held at every
inspected site (`CanvasRenderer.cpp:824, 958, 1194`); untrusted
input is budget-checked before allocation in clipboard, codec,
zip, and JSON paths; and the HLSL blend formulas were verified to
match the CPU reference including Dodge/Burn thresholds
(`Adjust.cpp:193-217, 1034-1108`). The seams exist; the headers and
test placement are what erode them.

## 4. Simplification plan

Ordered by payoff per unit risk. Estimates are from reviewer line
counts, not measurements.

1. **Evict test payload from the product (H5).** Exclude 39
   self-test TUs (~17k lines) from Release now; move them under
   `Tests/` against a host library next. Payoff: smaller shipped
   surface, faster incremental builds, honest product inventory.
   Risk: low; tests keep running from their new home.
2. **Collapse the 24 TestGates into one.** Each redeclares
   entered/release events, `JobResult`, and fault atomics
   (`ColorWorkTestGate.h:24` vs `HistoryRestoreTestGate.h:14`) and
   drags `Document.h` + `Jobs.h` + `windows.h` into the product.
   One templated `TestGate<Kind>` in Tests/Support deletes 23
   headers and their include weight.
3. **Split Document.h along existing TU seams.** `Raster.h`,
   `AdjustTypes.h`, `SelectionTypes.h`, `Session.h`. Payoff: most
   incremental rebuilds stop paying for all ~135 types. Risk:
   mechanical, compiler-checked.
4. **Extract HostJobs from Application; pimpl the Work payloads.**
   Forward-declare the 35 `*Work.h` types behind `shared_ptr` so
   feature work stops rebuilding 44 TUs. Move per-job stamp/ticket
   state out of `Application.h` (the branch's `_layerMetadataTestGate`
   field at `Application.h:820` is the latest offender).
5. **One generic Prepared<T>.** Sixty `Prepared*` types and 30
   `Host*Input` structs implement the same
   Input -> Work -> Prepared -> ApplyIfCurrent ceremony
   (`ContinuationRequests.h:59`). A generic wrapper plus a
   token-check helper in `Jobs.h` removes an estimated 30% of the
   ~10k Work/Continuation boilerplate lines. Opinion, with counts
   measured; do it feature-by-feature, not as a big-bang rewrite.
6. **Delete proven-dead code.** `MergeLayers`/`PreparedMerge`
   (zero product callers), `DocumentSession::StepBlendMode` (or
   call it), the four test-only Common/ headers (move to Tests/),
   the duplicate `WritePng`, five hand-rolled UTF-8 conversions
   (~75 lines), and the empty `newTests/` directory. Small lines,
   large clarity: Common/ means product code again.
7. **Freeze the kernel ISA surface until SIMD lands.**
   `Kernels.h:17` advertises five tiers with CPUID/XCR0 decode and
   CLI parsing for a Scalar-only table (`Kernels.cpp:67`). Keep
   `KernelTable`; delete `ParseKernelIsa`/`ValidateKernelIsa` and
   capability decode until the first AVX2/NEON kernel exists.
   Payoff ~100 lines plus their tests; prevents "we have SIMD"
   misreading. (Design opinion; deadness is proven.)
8. **Close the WIP drawer.** 19 plans sit in `Specs/Plans/WIP/`
   (non-normative per `Specs/README.md:18`) against 17 in Done,
   and the uncommitted index already links two untracked plans,
   one dated after HEAD. Archive or finish them; require each WIP
   row to name its implementing commit so narrative cannot drift
   from code again (H12).

## 5. Performance risks ranked by steady-state cost

Idle cost is not the problem: a settled document costs zero passes
and the loop blocks on events. The ranking below is by cost during
active editing, which is where every serious charge sits. All
magnitudes are code-derived; no ETW/PIX measurement exists yet,
which is itself a gap (Section 6).

1. **Pointer-move brush stack (H2 + H7 + M2 + M6).** Per move: GPU
   dispatch with a blocking readback, a full-document pass per
   layer, full mip regeneration, and (on the CPU path) two 256 KB
   heap copies per dirty tile. This is the single most expensive
   interaction in the product and it is the most frequent.
2. **Sampling clicks (H1).** Rarer than brush moves but worse per
   event: two full-document recomposes plus a blocking map per
   tile for one pixel of output.
3. **Full-frame CPU filters and adjustments (H6 + M1 + M18).**
   Scalar, single-threaded, copy-first. A 24 MP Levels/Grain/Noise
   pays tens of millions of transcendentals on one core while the
   worker pool idles.
4. **Selection rebuilds (H8 + H9).** O(height x edges) with
   full-canvas float allocation: 192 MB transient at 24 MP for
   coverage that may describe 10k pixels.
5. **Retouch solvers (M16 + M17).** Heal's fixed-iteration SOR,
   MotionBlur's 96 taps/px, WarpStroke's whole-layer copies:
   seconds-scale, uncancellable (Heal has no token at all).
6. **Sparse-raster materialization traps (M17).** First `Data()`
   touch retains a full-frame copy beside the tiles; long sessions
   pay O(tiles^2) merge and O(rows x tiles) reads as fragmentation
   grows — a slow leak in steady-state memory and time.
7. **Per-frame chrome and pump overhead (M9 + M10 + M7).** Triple
   arrange, uncapped message drain without coalescing, per-sync
   `LayerRows` construction (`ChromeHost.cpp:3085-3098`). Small
   each; constant.
8. **Clear/state churn inside each compose (M15).** Redundant
   full-surface clears and per-pass rebinding multiply risk 1 by a
   constant factor on every layer of every compose.
9. **Occlusion cache purge (Low).** Zero while hidden, then a
   re-upload storm on the first visible frame after alt-tab or
   monitor sleep.

## 6. Test and quality-gate gaps

- **Self-oracle goldens (H4, M21).** HSL expectations come from
  product code; the 9-mode golden checks one gray channel; kernel
  smoke checks a run against itself. Fix: literal archived behavioral reference pixel
  values, distinct R/G/B inputs asserting all bytes, committed
  checksum tables. Until then, green composition tests prove parity,
  not correctness.
- **ARM64 is ungated (H11).** Text-regex checks substitute for
  builds. Fix: build all configs in the gate; probe ASan natively.
- **WARP-only GPU coverage.** All composition tests run WARP FL11_0
  (`CompositionTests.cpp:134-141`), so hardware mip autogen and the
  `MonitoredFence` route never execute; brush parity covers only
  identity mapping on one 64x64 tile (`:864-904`). Fix: rotated/
  scaled mapping and multi-tile goldens; an optional hardware pass.
- **Skippable screenshot gate.** Exit 8 skips all nine cases and
  the run still prints "All tests passed" (`test.ps1:396-401, 419`).
  Fix: fail when zero screenshots pass.
- **Missing gates and timeouts.** `/analyze` is opt-in only
  (`build.ps1:95-99`); the four test exes run with no per-suite
  timeout (`test.ps1:61-64`), so a deadlock hangs the gate forever;
  `test.ps1:393` kills a process its own rationale says never to
  kill (`:161-163`); two suites silently run everything on unknown
  switches (`HostTests.cpp:1791`; `CompositionTests.cpp:1927-1931`).
- **Unported legacy goldens.** Downsample quality thresholds have
  no ported counterpart; 13 legacy suite names are uncited, only
  two traced (Brush ported, Downsample not). The DocumentTests
  suite itself is a 7,319-line monolith that hides what is and is
  not covered.
- **Script frailty.** `format.ps1` nears the argv ceiling and
  skips HLSL; `package.ps1` warns instead of failing on missing
  dumpbin/notices and packages stale binaries; `validate-skills`
  never checks that skill bodies or the AGENTS table still resolve.
- **Clean bills worth keeping:** warnings-as-errors, SDL, and
  permissive settings apply uniformly (`Directory.Build.props
  :37-43`); no orphan `.cpp` exists outside a project; all four
  suite mains aggregate failures to nonzero exits.

## 7. Top 10 actions in priority order

1. Route eyedropper/wand/clone sampling through `GpuWorkQueue`
   region reads on the settled canvas; delete the recompose-to-
   sample path (H1).
2. Make brush preview readback fenced and double-buffered; switch
   the preview format to R8 (H2).
3. Exclude self-test/fault TUs from Release today; schedule the
   move to `Tests/` (H5).
4. Replace the HSL self-oracle and single-channel blend goldens
   with literal legacy numeric expectations (H4, M21).
5. Add ARM64 builds for all configurations to the mandatory gate;
   stop claiming ARM64 ASan from regex checks (H11).
6. Defer `ResizeBuffers` out of `WM_SIZE` into the frame loop;
   pass and honor `wParam` (H3).
7. Parallelize row-independent kernels into Executor bands; this
   is worth more now than SIMD and makes later SIMD trivial to
   schedule (H6; then M1).
8. Bounding-box and active-edge selection coverage (H8, H9).
9. Zero-copy ComposeTile with pooled buffers and hoisted row
   loops (H7).
10. Reconcile the branch: land or withdraw the TestRuns-claimed
    commits, commit or drop the two untracked WIP plans, and fix
    the metadata-worker copies and missed call sites so
    "UiNeverBlocks" describes the code (H12, M14).

Close behind: bounded GpuWork timeouts and init unwinding (M8),
pump caps with move coalescing (M9), zip aggregate budget and
strict ParseLayer (M11, M12), clipboard caps (M13), and the
Document.h/Application.h splits (H10).

## 8. Coverage limits and unresolved items

The workflow recorded the following scope-level gaps verbatim;
each scoped review in fact returned body-read evidence used above,
but several returned `complete=false`, so these remain honest
limits rather than executed coverage claims:

- `[inventory] inventory: missing complete evidence result`
- `[correctness-memory] correctness-memory: missing complete evidence result`
- `[concurrency-jobs] concurrency-jobs: missing complete evidence result`
- `[gpu-rendering] gpu-rendering: missing complete evidence result`
- `[document-io-security] document-io-security: missing complete evidence result`
- `[cpu-kernels-perf] cpu-kernels-perf: missing complete evidence result`
- `[app-chrome-win32] app-chrome-win32: missing complete evidence result`
- `[architecture-simplification] architecture-simplification: missing complete evidence result`
- `[tests-build-quality] tests-build-quality: missing complete evidence result`
- `[recent-changes] recent-changes: missing complete evidence result`
- `[skeptic] skeptic-verifier: missing complete evidence result`

Specific limits behind those labels:

- **Nothing was executed.** No reviewer ran `build.ps1`,
  `test.ps1`, ASan, ETW, or PIX. Every stall magnitude is a
  source-traced loop/queue bound, and the current red/green state
  of the mandatory gate is unverified. Run the full AGENTS.md
  validation block before acting on severity where it matters.
- **Skeptic cap.** Only 12 of 21 Critical/High claims were
  adjudicated. Unadjudicated and carried at original severity:
  H6-H12 (parallelism, ComposeTile, coverage complexity, god
  headers, ARM64 gate, branch narrative). Verdict tally on the
  adjudicated set: 5 confirmed (H1-H5), 7 downgraded (M1-M5,
  WindowCapture, dead primitives), 2 sub-claims refuted and
  dropped from this report — the "62M full density evals per move"
  brush quantification (M3) and the PathClassification hang leg
  (M5).
- **Bodies sampled, not exhausted.** `Application.cpp` (8,094
  lines), `ChromeHost.cpp`, `BrushStroke.cpp`, `Adjust.cpp`,
  `Selection.cpp` tracing, and 36 `Application*Jobs.cpp` Run
  bodies were spot-read; a job dereferencing session state on a
  worker would be a race this review did not exhaustively exclude.
- **Pinned and hardware unknowns.** DxUi EmbeddedHost internals,
  chrome GPU cost, and `RequestFrameThunk` thread affinity were
  not inspected (pin rules). Whether any shipping driver lacks
  MIP autogen for the used formats is unproven (WARP-only
  evidence), as is premultiplied-invariant enforcement on load.
- **Untraced inputs.** Utf8/Guid acceptance breadth, extreme
  manifest doubles through adjustment kernels, libzip encrypted/
  multi-disk behavior, every edit path's aggregate-pixel
  enforcement, and 11 of 13 uncited legacy suite ports were not
  verified. Crash handler terminate coverage (severity input for
  the `Main.cpp:162-295` noexcept-allocation note) was not read.
- **Verified clean (so the gaps above are not mistaken for open
  suspicion):** no `catch(...)` tree-wide; no COM ownership misuse;
  HWND owned via `wil::unique_hwnd` (`Application.cpp:3444, 5081,
  5405`); STRINGTABLE discipline in sampled chrome; single device/
  swap chain/Present; `WM_PAINT` never renders; blend formulas
  match between HLSL and CPU; DecodeDib and untrusted-size gates
  hold at every inspected entry.

End of report. Only this file was written by the synthesis step.
