# P1 bounded raster mip recording — 2026-10-05

## Qualification checklist

- [x] Preserve the original base-upload and creation controls and failed mip attempts.
- [x] Primary implement worker-created parent SRV/child RTV pairs and a separate mip output shader.
- [x] Primary implement one owner draw per pump, real completion acknowledgement and original-ticket continuation.
- [x] Preserve whole admitted raster charges through failure, cancellation, quarantine and joined shutdown.
- [x] Private strict WARP r7 passes 173 new mip controls and the exact 726 preceding controls.
- [ ] Fresh Main source integration and private reference, normal registration and independent reviewer findings.
- [ ] Identical-source Debug/Release/real ASan, analysis, ARM64 cross-builds and predecessor registry proof.
- [ ] Independently archive qualification, reconcile contracts, commit and push this step.
- [ ] Complete document graphs, generation/consumer/fallback activation and the original largest-fixture G1 gates.
  - [ ] Qualify actual composition/chrome/brush rendering after a mip pump on the shared context.

**Status: private Caller draft qualified; Main integration and normal qualification pending.**

Parent: [bounded upload and first display](../Plans/WIP/BoundedRasterUploads_2026-10-05.md), U4.
This is one admitted raster's chain. It does not qualify an entire document's resources or first composition.

## Ownership and recording

An opt-in creation request prepares all fixed-capacity mip parent views, child render targets, shaders, sampler,
rasterizer and depth state on a capability-qualified worker holding the exact device. Each parent SRV exposes
one level, each child RTV its next level. Partial pipeline/view creation retains its owners until joined owner
retirement. Cancellation is observed between resource creation calls; no context is passed to the worker.

The existing queue records one draw into the next level per owner pump. The viewport matches that child, linear
clamp sampling reads only the completed parent, blending/depth/scissor state is explicitly established, and the
draw preserves premultiplied BGRA or the R8 mask channel. Shader compilation is build-time strict FXC, reusing the
full-screen composition vertex output contract and a separate pixel shader for this different target contract.

Only real GPU completion resumes the original scheduler ticket to advance another level. The original CPU and
GPU reservations remain held. Final successful callback alone may adopt the source/texture/view/reservation;
the MipsComplete authority moves with that owner and clears on reset. A base-only request still leaves lower
levels undefined. This route has no document/display publication. Pumping must occur outside renderer frame
recording; subsequent rendering must establish its pipeline. It does not save/restore arbitrary external state.

The queue retains independent view/target/pipeline/context references after uncertain signaling and through
joined shutdown. Their release precedes the graph reservation. Cancellation before pending mip recording is
checked through the original cancellation token; cancellation after a draw waits for acknowledgement and cannot
record another level. No mip stage maps or recopies pixels, polls GPU status, or executes a CPU pixel loop.

## Pixel and fault references

Independent literals include BGRA 2x2 opaque {0,64,128,192} -> 96, Gray 8x8 constant 128 through four levels,
and a one-level singleton 213 requiring zero mip draws. Odd Gray 3x5 -> 1x2 samples parent centers x=1,
y=.75/3.25, yielding [32,112] -> 72. Gray 5x3 -> 2x1 samples x=.75/3.25, y=1, yielding [38,58] -> 48.
A varying Gray 4x4 yields [24,56,88,120] -> 72. Premultiplied BGRA/alpha yields [48,24,12,96]. Base bytes,
intermediate and final levels are read independently after completion. Installed zero-write blend, empty scissor
and unrelated viewport states must not suppress the draw.

Faults cover each worker pipeline prefix, partial view creation, pre-record refusal, post-draw signal quarantine,
cancellation before/after a mip and shutdown after recording. They preserve exact HRESULT/ticket, suppress
partial adoption and account for all charge lifetimes. These are deterministic state/pixel expectations, not
wall-time or driver-memory measurements.

## Retained attempts and current private proof

Sliced GenerateMips initially passed BGRA and failed the deeper Gray chain: r1 actual PID 98176 exited 2;
r2 PID 94536 and r3 PID 43972 exited 1 on final Gray 0 instead of 128. Cancellation was corrected between
r1 and r2; r3 repeated the unchanged failing source after a diagnostic edit assertion prevented that edit.
Literal expectations were retained. A separate D3D debug-layer WARP probe reproduced both R8 and BGRA deeper
levels left zero by adjacent-range GenerateMips calls, while a single full-chain call initialized every level.
No universal hardware-driver conclusion is inferred from that observation. Explicit child-level rendering
replaces that attempt.

r4 corrupted the nested Python FXC path with string escapes. The child rejected the filename, then driver
product hashing failed before its actual process receipt was saved. This attempt qualifies nothing. Its freeze,
command and streams are retained. The repaired helper uses a forward-slash path and persists actual-process.json
before later hashing. r5 passes 81 mip controls; r6 passes 143 after odd/alpha/state cases; r7 adds 30 pipeline
prefix failure controls, with all 726 predecessors exactly preserved in each successful attempt.

r7 actual PID 18188, exit 0, 2026-10-05T16:54:55.429598Z -> 16:56:07.600699Z, 409 inputs;
freeze `5EEF606A7B47CF1D05929CA6EAAF614848950A5DA6D2EF1979A0BDCF1DBE23A7`, stdout
`7A7A1B9067050D1DE69C97380B0C97D3146EDEB788E152FC5B810E85579A63F2`.
Pixel shader bytecode SHA-256 `DEAEAC564AE8A9966135E318B003CE61F906ECB678319EC8435D4892C41F3691`;
FXC SHA-256 `005EFF830845789C7EFB2831A0B41950EE6954E9BCD93BAF50DE67AD537728B2`.
Receipts and owned sources/products remain in `Z:\src\RedPrism\.build\private-gpu-mips-runtime-r7-20261005`.
Primary readback verifies each frozen copy and the exact predecessor counters in independent-mip-proof.json.
The old generic Scope field underdescribes this new mip suite; that receipt is preserved and the independent
proof states the tested scope. Header comments subsequently clarified this contract; fresh Main qualification
must bind the final integrated bytes. This private result is neither normal qualification nor application activation.

## Independent review

The bounded cheaper-agent pass found no demonstrated pixel/lifetime/cancellation/generation defect and identified
the changed immediate-context pipeline as an integration obligation. Primary verified Application drains owner work
before RenderFrame, CanvasRenderer RunPass and BlitToTarget establish their viewport/shaders/samplers, and
Renderer passes its window viewport to ChromeHost Composite. The mip API now explicitly requires pumping outside
frame recording and complete state binding by following consumers. Arbitrary state preservation is not promised.
This does not close the unchecked actual post-mip composition/chrome/brush validation before live activation.
