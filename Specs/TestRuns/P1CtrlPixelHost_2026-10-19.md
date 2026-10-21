# Initial Ctrl host, procedural frames and display opening retirement

## Checklist

- [x] Keep physical Down/Move/Up, modifiers and history on the original admitted selection parent.
- [x] Build immutable pending pixel frames from the original lift without a per-tick CPU preview job.
- [x] Construct display roots on the opening worker and retire undrained, cancelled and stale roots on workers.
- [x] Reproduce the missing final-frame notification; fix the successful terminal notification.
- [x] Check independent literal CPU/WARP pixels, device rebuild, Undo/Redo and strong/weak accounting.
- [x] Reproduce and repair the Ctrl fixture's Win32 modifier leak without changing existing assertions.
- [x] Pass the complete private real-ASan Application run: 154 stages, 11,675 named stdout facts.
- [x] Pass all 286 existing clipboard facts plus six controlled replacement-before-inspection facts.
- [x] Adopt the reviewed candidate/project registrations and qualify the combined actual source in all six profiles; [current matrix](P1CtrlCaptureMatrix_2026-10-20.md).
- [ ] Keep final CPU/history/rows/GPU publication under the original ticket; finish journal/Wand and bounds.
- [ ] Qualify ordinary activation and measured G1. P1 remains WIP.

## Private host evidence

The private source lineage is `PixelMoveHost-Main-r1`, the two-source
`PixelMoveHostNotifications-Main-r1` refinement, and the fixture-only
`PixelMoveHostKeyCleanup-Main-r1` correction under `Z:\src\RedPrism\.build\P1Drafts`.
All evidence folders below are under `Z:\src\RedPrism\.build\P1Evidence`.

The original 355-TU ASan build compiled every source but failed its resource step because its isolated
tree lacked Lucide SVGs. Resource repair independently verifies all 355 actual objects, compiler flags
and immutable sources, then links those objects against the unchanged canonical resources. This does
not turn the failed original build into a successful build. The exact copied DxUi library is the pinned
`8e8994079e82f79e6765e6a885c03fc687957591`, API 3, SHA-256
`24B7B8ABD15E88A6A3DDD529BF6832FBA164E5EBCD504C6F400631702BA25E11`.

The event-driven native control fails exactly at
`application-ctrl-pixel-press.move.after-up-committed-WARP-display-matches-independent-literal`:
held preview and final CPU pixels pass, while the committed GPU frame still contains the previous pixels.
The successful pixel terminal now calls `DocumentChanged`; outline-only terminals keep their selection
notification. The fixture performs actual message dispatch and observes production work readiness; it
does not repeatedly request a replacement frame to hide a missing notification.

The initial fixed fixture incorrectly expected a refund while its diagnostic expired weak reference to
`SharedPublicationMemory` still owned the accounting control block. Its corrected assertions first prove
that exact retained charge, release that last weak observer, and then require the exact refund after the
real release acknowledgment. No production accounting predicate or legacy golden was relaxed.

The next full private run preserves an existing early-Up failure. A separate two-source diagnostic
reproduces the exact failure, with Ctrl still held on the test thread and `E_INVALIDARG` after one capture
and one hit. The new Ctrl fixture's final memory-refusal branch had bypassed `SetKeys(false, false, false)`.
`ScopedSynthetic` restores its previous synthetic state; it does not restore the Win32 thread state written
for DefWindowProc. A scope-exit cleanup now releases those synthetic modifiers on every return/continue.

The corrected full private run is
`p1-ctrl-pixel-host-key-cleanup-private-r1/ASanDebug/runtime-observed.json`:

| Evidence | Actual result |
| --- | --- |
| Native Application | PID 50400, exit 0, 2026-10-19 16:41:11 UTC |
| Scope | 154 stages; all 11,527 original facts plus 128 Ctrl and 20 opening-retirement facts |
| Stdout SHA-256 | `C21257540BD1169B7B5A883847ECD089351F05D55EA22C17AF241CC9E033672F` |
| Stderr SHA-256 | `F3E99BD67ACDFE80D7C48C880E5A1CCE62BC4060C833E943549B544039CBF132` |
| Executable SHA-256 | `6DB61B8700DC631BD191FE1DD7F81C5F2271D6514A160BF387EC305361C07501` |
| Full PDB SHA-256 | `5F7DAA280844FE70C15F5699249E2A225448E989F14B80F6419CA2908FE62F65` |
| Compilation | One fresh fixture TU and 354 independently verified predecessor objects |
| Artifact checks | PE/RSDS/full PDB match, unchanged allowlist, actual ISA scan with zero violations |

The fixture uses independent 5×3 BGRA literal images for Move and Duplicate. It checks Down-Ctrl/Alt/Shift
precedence, ignored later modifiers, Up before pumping, zero offset, capture loss and memory refusal.
It also checks zero/redrag exclusion of old landings, actual device rebuilding/replay, one-entry Undo/Redo,
and separate original lift/frame charges through independent strong and expired weak readers. Three
additional cases hold an actual undrained opening completion after its worker hook and exercise late
cancellation, stale source and actual joined Close before refund.

## Clipboard contract regression

The canonical foundation ASan failure is retained separately. Its retry fixture expected foreign 1×1
BGRA `{17,33,65,255}` on a 9×7 canvas at `(4,3)`. The clipboard sequence changed before successful
inspection. The unexpected payload's dimensions and publisher were not captured; neither is inferred.
A tracing-only full Application run passes all 152 stages / 11,527 original facts, with stable sequence,
self-owned clipboard and the expected 1×1 payload. That pass does not establish a historical cause.

`ClipboardReplacementProof-Main-r1` adds one test-only scenario. An actual OS clipboard write replaces
payload A with independent 1×1 BGRA `{9,19,39,255}` while the original PasteProbe is held. After two injected
Busy acquisitions, the existing operation must inspect/paste B under its original six-acquisition budget,
while its private copied pixel remains `{20,40,80,255}`. All original cases retain their expectations.

The focused native ASan run is PID 98156, exit 0, 2026-10-19 16:29:16 UTC: 286 original clipboard facts and
six new facts. Stdout SHA-256 is `E708C807E566334042DB4CB0DAA2CA207A7AE1C15BFDFAC73F84777CA90E0553`;
stderr is empty. Four fresh TUs and 349 exact predecessor objects, full PE/PDB and ISA0 qualify. The private
Main/Application selectors are excluded from adoption. The first reader incorrectly expected seven new
facts and one stage; corrected readback verifies the same runtime bytes, six facts and both actual stages
(the existing JobShutdown prelude plus the focused clipboard driver), without replaying runtime.

## Adoption and limits

The partial review tree `CtrlHostAdoptionCandidate-Main-r1` is not compilable by itself: quoted relative
includes would mix model ABIs. Adoption must copy the reviewed sources into the actual worktree and register
both new fixtures in its real application project before building.

The candidate also removes the unusable default owner argument from the display-root constructor, refuses
a zero procedural ownership bound, isolates the existing standalone selection driver from physical keys,
and formats the changed sources. These follow-ups require combined-source validation; the private receipt
above is not presented as proof of those later bytes.

Pending frames have their own complete admission and retain the original lift. The current final CPU/history
closure still requests a separate committed frame. It does not satisfy original-parent final rows/GPU
publication, typed journal/Wand support, complete measured operation bounds, ordinary activation, G1, or
the P1 delivery milestone. No native ARM64 runtime or Photoshop/RAW speed claim is made.

## Full private Polygonal/journal extension

The subsequent complete355-fresh-TU ASan image uses the corrected Win32 key cleanup, selection driver
isolation, clipboard replacement controls and typed journal implementation. PID103492 exits0 at17:48:41UTC:
154 Application stages /11,814 named facts preserve all11,675 preceding private host facts, add125 mirrored
Polygonal facts, eight actual held-first-closure/queued-successor facts and six clipboard replacement facts.
Executable SHA-256 `B24BB6F6B65DDF57ECD4AD640291BF18854D3C341CC02EC44D34D7E4D8A37457`;
full PDB `F4E086E8C769EF400CD42E960AE5BB705489AB9668A3F0988262F0EEF791B82A`.
Actual compiler flags, source bookends, matching full PDB and unchanged-allowlist ISA0 independently pass.
The separate CPU journal image passes993 facts, including42 new journal assertions and all selected predecessors.

These code/test registrations are now adopted with the colour capture/final-source API. Formatting and
their combined model ABI require fresh canonical qualification; the preceding private native result is not
that qualification. Final original-parent rows/GPU publication, remaining journal/Wand grid, phase bounds,
ordinary activation and all nine P1 delivery gates stay open.
