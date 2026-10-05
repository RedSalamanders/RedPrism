# P1 bounded raster mip recording — 2026-10-05

## Qualification checklist

- [x] Preserve the original base-upload and creation controls and failed mip attempts.
- [x] Primary implement worker-created parent SRV/child RTV pairs and a separate mip output shader.
- [x] Primary implement one owner draw per pump, real completion acknowledgement and original-ticket continuation.
- [x] Preserve whole admitted raster charges through failure, cancellation, quarantine and joined shutdown.
- [x] Private strict WARP r7 passes 173 new mip controls and the exact 726 preceding controls.
- [x] Fresh Main source integration/private reference, exact normal registration and independent reviewer findings are reviewed; actual shared-context consumer qualification remains below.
- [x] Identical-source Debug/Release/real ASan, analysis, ARM64 cross-builds and predecessor registry proof.
- [x] Independently archive qualification and reconcile contracts.
- [x] Commit and push this qualified step (`af7986f`).
- [ ] Complete document graphs, generation/consumer/fallback activation and the original largest-fixture G1 gates.
  - [ ] Qualify actual composition/chrome/brush rendering after a mip pump on the shared context.

**Status: managed normal qualification passes on the identical 825-input source; committed/pushed as `af7986f`.**

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

## Fresh Main reference and running normal qualification

Main private r1 actual child PID 76656 exits 0, 2026-10-05T17:10:07.441826Z -> 17:11:19.006217Z; 407 frozen inputs, SHA-256 `008C46D84FD015BB2DE0DA7223194BB37073033DA933E427D8D667B0012AE4F8`. Stdout `78DE691BDF914C9F2BA852FE2E59DDD3392E18D32C135A8E58729E2290C76841`. The fresh driver owns frozen sources, compiled strict shaders and actual process receipt before later hashing; scope explicitly covers mip draws. All 726 predecessors and 173 new mip controls pass. Agent registrations were independently reviewed for exact uniqueness, complete FXC metadata and preserved entries; no whole Caller project was copied. Skills validate 15 and formatting checks 704 files.

## Complete normal qualification

All gates below pass on the identical 825-input freeze `87EF15D69384FDF174B89616E2D957D467562C3D644BDB98110B2A821CC0E18F`. Strict source/registry proof preserves every predecessor: Document 7,081, Host 7,525, application 9,744 controls and 79 ordered stages; Composition 1,495 → 1,668 adds exactly the 173 private mip controls. ASan's independent stress registry remains 7,922. Skills (15) and formatting (704) pass. These are the repository entrypoint runs; incremental product reuse is scoped in their receipts, not claimed as clean full rebuilds.

| Gate | Actual child / exit | UTC interval | Verified archive completion SHA-256 |
| --- | --- | --- | --- |
| x64 Debug test r146 | 82276 / 0 | 2026-10-05T17:13:59.475872+00:00 → 2026-10-05T17:24:22.934606+00:00 | `2CFE32B8CD5154A20B63F61BCEA7C8E9EE7612644526CFAC1DD654B62289AF76` |
| x64 Release analysis r147 | 97064 / 0 | 2026-10-05T17:14:53.989121+00:00 → 2026-10-05T17:22:05.632849+00:00 | `E4125E149D498B3A633323EEE6C80BF946443CF071CF0567EEA011CB11298BAE` |
| x64 Release test r148 | 83084 / 0 | 2026-10-05T17:28:25.645117+00:00 → 2026-10-05T17:34:52.827613+00:00 | `65197FB392D169A1E054B05D80A1A52D549222222BDF0E87CE33637D91EDB3FA` |
| x64 ASan Debug test r149 | 100764 / 0 | 2026-10-05T17:35:27.969620+00:00 → 2026-10-05T17:47:41.110541+00:00 | `B63AA9B7DC5ED56CECD2A0D3E71E422751BAADE5B97D1860CDC560510EFACC14` |
| ARM64 Debug r13 cross-build | 61364 / 0 | 2026-10-05T17:14:28.596809+00:00 → 2026-10-05T17:14:54.420879+00:00 | `AFBA23C99F5C5DD0EA0FA11C761873BA88731F4FFDFCB3B8241154BFBC73922C` |
| ARM64 Release r13 cross-build | 92320 / 0 | 2026-10-05T17:14:58.394408+00:00 → 2026-10-05T17:17:09.830415+00:00 | `863792F830618A703009213DCB75BBAEDE5BFF061F4E605739C47028816B3D52` |
| ARM64 ASan Debug r13 cross-build | 96764 / 0 | 2026-10-05T17:17:18.830820+00:00 → 2026-10-05T17:18:55.625345+00:00 | `383B55E7F254E2652524F359A86C00F729CFB97D2D54CC92F49D178F12B6E504` |

Archives are under `C:\Users\eric\AppData\Local\RedPrism\QualificationArchives`, named by gate above. Sources, products/PDB/provenance, logs, screenshots and registry proof were independently copied/read back after actual child exit. The current real sanitizer probe diagnosed expected heap-use-after-free; its diagnostic hash is `1F82C180A1710DE2D10C760EC9F879DC76724B50DF04AD4AF3B31BBF811B05DC`, supplement receipt `4E909841D39F0596DDFD0EEB5E9674B1ECB5800D0290D49B07FBDCB7B170C42E`. ARM64 results are compilation only. Complete resource graphs, shared-context actual consumers, fallback/native activation and original first-display G1 remain unchecked requirements.
