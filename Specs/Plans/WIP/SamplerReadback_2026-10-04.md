# P1.3 — asynchronous canvas color sampler

## Delivery checklist

- [x] Audit the synchronous press/move/release path and its committed-source, palette, picker and shutdown consumers.
- [x] Implement repeated-pixel continuation; full Debug r10 passes its WARP controls, including between-point device replacement. Other profiles remain due.
- [x] Implement fixed scalar session/picker input authority; full Debug r10 passes all 13 palette and eight picker controls. Other profiles remain due.
- [x] Implement bounded accepted-coordinate FIFO input and qualify its 2,653 controls in full Debug r10, preserving opaque-prefix, exact acknowledgement, wrap, cancellation and capacity behavior.
- [x] Full Debug r10 qualifies repeated 1×1 readbacks on one acknowledged ticket, source and reservation; exact pixels, refusal, cancellation, device replacement and retirement. Final required matrix remains due.
- [ ] Integrate and runtime-qualify the actual Eyedropper and Alt Brush/Heal/Gradient callers with bounded accepted-coordinate FIFO input and one admitted source capture.
- [ ] Bind publication to the exact document session, gesture opening and foreground/picker input authority, including equal-value reopen and A–B–A changes.
- [ ] Preserve older sample / younger palette-dependent brush, gradient and shape ordering through terminal acknowledgement.
- [ ] Qualify actual press/move/Up, pointer leave/loss, Escape, failure, memory wait, shutdown and device fallback controls with unchanged numeric goldens.
- [ ] Measure sampler owner capture, utility record/first upload, completion/chrome and invalidated-frame G1 rows on A/B; fix overruns.
- [ ] Complete required x64 suites, analysis, ARM64 cross-builds, contracts/guide and native integration; commit/push the qualified slice.

Status: ACTIVE prerequisite and caller work under [P1](UiNeverBlocks_2026-09-22.md). Full Debug r10 passed the repaired combined snapshot, including all eight picker controls, 13 palette controls, 2,653 FIFO checks and repeated readback controls. r8 analysis predates clipboard integration; Release, ASan, current combined analysis/ARM64, actual sampler callers and G1 remain due.
Baseline: managed `b49e4b4`; the main color qualification runs independently in its frozen worktree.
Owners: `Application`, `DocumentReadbackJob`, `GpuWorkQueue`, palette/picker input and selected host continuations.
Contracts: `Document_Tools`, `Rendering_Composition`, `UI_Windowing`, `UI_ApplicationChrome`, `Core_PerformanceAndResources`, `Testing_Validation`.

## Existing behavior to preserve

`PressCanvas` constructs `ColorSampling`, calls `WandComposite` synchronously for the complete committed document,
then `SampleColorAt` reads the held raster for each accepted move. `EndColorSampling` hides the ring and discards
that owner; ordinary Up does not sample its release coordinate. The source excludes adjustment and stroke previews.
Inside bounds, coordinates are floored. Transparent pixels/outside points leave color unchanged. Premultiplied
BGRA is unpremultiplied, clamped and rounded by `DocumentSession::SampleCompositeColor`; no pixel formula changes.
An open picker receives its sampled preview color; otherwise foreground changes only between brush strokes.
The ring's lower half retains the original press color. Existing `eyedropper.*` and actual 0xFF3366CC host golden stay.
The existing large sole-raster shutdown fixture remains a retirement requirement even after the production path
no longer keeps a complete composite.

## Acknowledged repeated-pixel readback prerequisite

A derived job starts with an admitted 1×1 region and retains one immutable source snapshot. After each GPU copy,
worker copy/adoption and detached worker processing, the owner acknowledges Map/Unmap and releases utility targets.
Only then may `ContinuePixelReadback` resume the exact suspended ticket at another in-bounds pixel. Its region
remains 1×1: no additional output peak, snapshot capture, job sequence, admission or reservation is introduced.
The previous pixel/result retires in the next worker preparation, not the pointer input callback. Total device
restart allowance remains two for this record; point changes do not reset it. Unsupported capability, repeated
Map-busy and persistent device failure retain existing local failure behavior; no synchronous readback fallback.

A refused request (wrong ticket, pre-acknowledgement, out-of-bounds coordinate or duplicate resume) changes no
request state. A successfully resumed hook must retain the record. While no input is available the record may
stay suspended: no event polling, periodic timer, self-rearming action or dummy worker turn. Owner input or a
terminal can resume it. Cancellation/shutdown joins workers before releasing document/result owners and slots.
Tests use independent coordinate bytes, exact four-byte output, retained reservation, no lent/utility owners at
the detached boundary and one final callback; a sequence includes both document edges and a nonzero offset.
This prerequisite alone does not activate a sampler, qualify owner uploads, or prove performance.

## Actual caller and input authority

The guarded foreground and picker APIs are now implemented. Both bind an owner lifetime and monotonic input
revision, renew a successful caller's stamp, and permanently invalidate exhausted counters. Existing inline
palette/picker paths preserve their color semantics. Independent core controls and actual picker calls are in the
candidate; a fresh build/runtime matrix is required. They are prerequisites only: the current canvas sampler still
uses synchronous `WandComposite`, and selected younger tool dependencies below are not yet migrated.

1. Press owns a trivial descriptor: session/document serial, generation, exact gesture opening, document pixel,
   original ring color and intended palette/picker target. No raster or document graph is captured before whole
   admission. If this source authority changed while waiting, refuse locally instead of sampling a newer document.
2. First admitted owner continuation captures the immutable committed root once. Every point and bounded device
   restart uses that same root through terminal acknowledgement. Capture and retirement bounds include complete
   source metadata, derived wrappers, pixel output and existing utility/residency costs; H4 upload bounds remain due.
3. Keep one immutable active coordinate/sequence and a fixed FIFO of at most 512 accepted pending coordinates (implemented; runtime qualification pending).
   Coalesce only adjacent pending coordinates that address the same pixel on this immutable source. A single
   latest-point mailbox is insufficient: an opaque middle point followed by a transparent last point must leave
   the middle color selected, as the synchronous path does. Preserve differing coordinates until their result
   acknowledges; transparent results retain the last acknowledged opaque prefix. Outside/nonfinite points move
   or hide the ring but enqueue no copy. The worker reads only its selected immutable coordinate/result. Up seals
   the accepted prefix, not a release coordinate. Capacity exhaustion seals and reports local refusal without
   silently accepting a dropped point or discarding already accepted points. Pending indices alone add no wake:
   progress resumes through real input or a preceding readback acknowledgement. Batched GPU evaluation may later
   reduce round trips only with identical last-opaque semantics, complete admission and numeric controls.
4. Picker publication binds a monotonic opening and manual-edit authority, not only `ColorPickerOpen` or color
   equality. Closing/reopening the same target/color and a manual A–B–A edit must refuse older samples. Foreground
   publication similarly respects a later explicit palette intent; document generation alone is insufficient
   because the palette lives outside history. Sampling updates its own expected authority after publication.
5. Up seals the last accepted move without inventing a release-point sample. Hide the drag/ring immediately, retain
   the pending palette dependency until the last accepted sample acknowledges, then retire once. Loss/leave/Escape
   retain their existing tool policy; define and prove pending-prefix handling before migration, with no rollback
   of already published foreground and no late update into a replacement picker or live brush.
6. A younger accepted brush/gradient/shape must use the color selected by an older sample when no newer explicit
   palette intent intervened. Its captured tool/settings remain event-time values. Bind this dependency to the
   original sampler opening/result under the shared selected-input order; never read a later unrelated foreground
   or reject ordinary accepted input merely to avoid resolving the dependency. Close/new/open/Undo, palette reset
   and swap, tool change and picker completion must seal/cancel/acknowledge the exact sampling opening.
7. Keep publication outside WndProc/control callbacks: checked scalar palette/picker update plus ring/chrome
   invalidation only. Sampler creates no document revision or history entry. Off-owner aggregate retirement owns
   the captured document through failure, cancellation, replacement and shutdown acknowledgement.

## Qualification matrix

| Family | Required evidence |
| --- | --- |
| Pixels | Opaque literal 0xFF3366CC; partial alpha BGRA 31/63/127/128 -> 0xFFFD7E3E; channels above alpha clamp to 255; alpha 0 no change; floor/fractional/boundary/outside; transformed mask/clipping/adjustment composition |
| Coalescing | Gated first copy, several moves, same/different pixel, invalid point, latest sequence, move then Up before ready; exact source retained after intervening edits |
| Palette/picker | Foreground vs open foreground/background/gradient/size picker, closed/reopened same target, manual A–B–A, reset/swap, mask target, younger brush/gradient/shape color capture |
| Lifetime | Escape, leave/loss, tool/history boundary, same document ID replaced, close/new/open, late success/failure, admitted/waiting cancellation, shutdown before capture/copy/after-ready |
| GPU/admission | Monitored fence and EnqueueSetEvent, one busy retry/second busy failure, rebind before copy/while mapped/between points, persistent failure/two-restart cap, no capability, exact memory pressure/fault boundary |
| Structure | Owner messages/view input dispatch while gated; no owner pixel copying/Map wait/full readback; exact one reservation/ticket/source; no idle wake or utility leak; complete aggregate retires off owner |
| Timing | Release A/B owner/apply and invalidation-to-Present evidence on R1 and W as the original P1 G1 contract requires; no wall-clock thresholds in tests |

Only after these caller and measurement rows qualify can this child and the P1.3 parent be closed. Wand All Layers
and Clone All Layers remain separate full-region consumers; this 1×1 path does not migrate or reduce their output.

## Qualification update — 2026-10-04, r3

The normal-clean x64 Debug r3 build passed with exit 0 and was independently archived with 977 verified copies. Its captured source predates the follow-up fixes described here. In the actual r3 runtime, all `CompositionTests` passed, including the repeated-pixel between-point device-rebind controls: exact original-source pixels, same ticket/reservation, device generation advance, prior-pixel retirement, and no outstanding utility ownership.

`DocumentTests` exited 1 on `palette-authority.sampling-and-input-authority-remain-outside-document-history`. The fixture expected undo count zero, but `CreateDocument` records the New Canvas operation. The source now captures the initial undo count and checks it remains unchanged; that correction was not built in r3. The original failing result and logs remain in the verified runtime archive.

The r3 `RedPrism` self-test exited 5 on the delayed-picker-sample assertion; this was a test failure, not a crash. The run passed the own-samples and foreign-host picker checks, then failed manual-input supersession. It exposed that the picker adapter used DxUi's opening/reference swatch accessor for the chosen value. Current source routes sampled publication through `SampleColor` (advancing public/input revision and invoking the preview callback); `ChromeHost::PickerColor` now reads the chosen value through DxUi `GetColor`, while the opening value remains for Cancel. These fixes are in the incremental compile attempt; no runtime pass is established.

The r3 failures and their source fixes remain as recorded above. Subsequent evidence is summarized in the r4–r8 update below; those later focused receipts do not retroactively change r3 outcomes. Production sampler activation, younger palette-dependent tool ordering, controlled actual-caller tests, performance/G1, and the broader required matrix remain open.

## Qualification update — 2026-10-04, r4–r8

- **r4 build:** incremental x64 Debug build exited 0 from 755 frozen inputs. Independently verified archive `C:\Users\eric\AppData\Local\RedPrism\QualificationArchives\p1-sampler-chosen-color-x64-Debug-r4-incremental-build` contains 977 verified copies; source manifest `D0AEB27777DD77C9F4EEB76E8373422A091B390F3EE6C96B5CC726C08434DBCC`, evidence manifest `FA9454E682D7F797119D98EBBB5061ED71122866ED7BD0E3DA268ACC046FE2CA`, completion `43E37994624C928E68BD99C5965DC4C0289B12722561B24E0A79E7317C6F277C`. This is build-only evidence.
- **r5 DocumentTests:** the focused x64 Debug `DocumentTests.exe` run exited 0. It recorded 6,504 controls (6,491 preceding plus 13 Document sampler-authority checks); the independent archive is `C:\Users\eric\AppData\Local\RedPrism\QualificationArchives\p1-sampler-authority-x64-Debug-r5-document-qualified`, completion `9DC7454A0425D039E19C5CFEC9C9E828D303F3241583DD4D730DDD0032E0DB38`. This qualifies those Document checks only; it does not qualify current host picker controls, manual routing, FIFO behavior or the production sampler.
- **r6 Application:** the focused x64 Debug `RedPrism.exe` run exited 0 with 78 stages. The verified sampler registry reports 8,986 controls (8,980 preceding plus six older sampler picker controls); the registry confirms the preceding registry is identical. Archive `C:\Users\eric\AppData\Local\RedPrism\QualificationArchives\p1-sampler-chosen-color-x64-Debug-r6-application-qualified`, completion `6A1971B2BFC3271DE5BFDE6E8ADEED63E60D9C969E388722D03A57755AA4BC03`. Those six older controls do not establish runtime passage for the current eight picker controls or manual routing/FIFO.
- **r7 analysis:** x64 Debug analysis exited 1. `Tests/DocumentTests/DocumentTests.cpp` triggered C6326 constant-comparison warnings at lines 5439 and 5510, promoted to errors. Preserve this failed result and archive: `C:\Users\eric\AppData\Local\RedPrism\QualificationArchives\p1-sampler-x64-Debug-r7-analysis-constant-switch-failure`, completion `D138313226D05AD9A00C8D58F5F36CC918A19EF502627A35F9E9263D8265ED42`.
- **Fixture repair proof:** `.build/adjustment-constexpr-isolation-proof.json` records that the r7 frozen and current `DocumentTests.cpp` contain an identical multiset of 996 lexer-recognized `Expect` call expressions after the switch-to-`if constexpr` fixture repair. This is a scoped assertion-preservation proof, not a successful analysis result or a completed validation gate; it does not establish 1,087 expressions.
- **r8 analysis:** x64 Debug analysis exited 0 at 2026-10-04 20:50:34 UTC from 758 frozen inputs. The result receipt records freeze hash `50D0F59348CAEF45379B1FEB6E32B6A47D50C6835F749274D325EC2882618C26`, stdout hash `172F975362B3068B61561011D2AE9E4A3BBEBD9D39662C4DF67F8D9366437022`, and empty stderr. Independently verified archive `C:\Users\eric\AppData\Local\RedPrism\QualificationArchives\p1-sampler-x64-Debug-r8-analysis-qualified` contains 1,005 copies; source manifest `EF167CA7AECDFB0CB62770DCDCED2F9A763BB31C57FCC59F240E90D38892DD9D`, evidence manifest `11B85C429DAAF9AEB76199D07F99F21C2CE8A6EEC184170F42AB29F4686CDC9B`, completion `D9052405E5BD77C6D0661CC8E6C48F2F98B1A0C85D98E3F7EF66AAC19440DE7B`. This is an analysis-only pass on the pre-integration snapshot, not a runtime or full-gate result.

After the r8 archive was verified, the primary integrated seven clipboard-fix code/contract files from main r51, covering `ClipboardTransfer.*`, `ApplicationClipboardJobs`/delayed tests, `IoClipboardTests`, `Core_PerformanceAndResources` and `Testing_Validation`, plus a new scope block in `Document_Tools`. The clipboard fix has a separate main-branch full ASan r51 pass; that result does not qualify this combined sampler snapshot. Full Debug r9 was started after archive verification. Its 758-input freeze includes the integration, and source is frozen for the run. No analysis result exists for the combined snapshot yet.

The current eight picker controls, manual routing and accepted-coordinate FIFO passed full Debug r10 below.
The actual canvas sampler still uses synchronous `WandComposite`, and younger palette-dependent tools remain
unintegrated. No G1 or end-to-end sampler qualification is implied by these prerequisites.

## Qualification update — r9 failure and r10

Full Debug r9 exited 1 in HostTests before document, composition, I/O or application execution. Two new
`sampler-input` assertions expected sequences 3/4 after cancelling an opening that had already consumed 1–3.
The correct independent sequence reference is 4/5: cancellation must not reuse its discarded pending stamp.
Only those two new fixture literals and their explanatory comment changed; production sequence logic, original
controls and goldens did not. The 980-copy original failure archive is independently verified with completion
SHA-256 `4ABAC468289E43731D496158AB2DDA811CE34502450320DA4B101D4A98260654`.
The first archive helper stopped before completion because this early failure had created no screenshot folder;
that partial archive is preserved. A separate helper guards the absent folder and produced the complete receipt.
Fresh full Debug r10 passed at `2026-10-04T21:09:38.134690Z`, actual exit 0. All 758 frozen inputs and
1,025 archived copies are independently verified (completion SHA-256
`64819CFFFC0925A5C9341B25E7EFCB770D79B58965938DE29DBE8C80AE899266`). Its unchanged preceding registries
plus new controls are 6,504 document and 8,988 application controls, with the same 78 ordered application stages.
This is the combined Debug prerequisite result, not actual asynchronous sampler activation or G1 compliance.
