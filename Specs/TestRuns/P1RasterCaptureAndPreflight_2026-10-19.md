# Raster target capture, scalar preflight and clipboard retry regression

## Delivery checklist

- [x] Privately verify scalar preflight in native Debug, Release and real ASan.
- [x] Retain the old Debug allocation failures and prove the event observer sees a known allocation.
- [x] Privately verify prepared Gradient target capture with 446 previous and 20 new assertions.
- [x] Force the real native Paste inspection/read contention handoff and verify the permitted ticket transition.
- [x] Retain an old-predicate negative control that fails this valid native transition.
- [x] Qualify the combined full real-ASan Application candidate and integrate the exact reviewed sources:152 stages /11,527 assertions preserve all11,323 predecessors; eightfresh/343 inherited objects, actual instrumentation, full PE/PDB identity and zero ISA violations independently pass.
- [x] Complete affected canonical Debug/Release/real-ASan and all three ARM64 cross-build profiles; [current matrix](P1CaptureMatrix_2026-10-19.md).
- [ ] Complete the nine P1 delivery areas and measured G1 before milestone Done.

## Code changes and preserved behavior

`RasterEditWork.cpp` uses `BrushStroke::PlanGrid` for extent validation and memory planning instead of
constructing BrushStroke objects whose hash containers allocate before admission. Grid arithmetic, overflow
checks, byte budgets, status values and pixel kernels are preserved.

RasterEdit commits and Gradient commits/prepared previews capture immutable document roots and targetless
tokens after admission. Workers copy the full layer/target metadata. Commits retain their history source;
prepared previews drop the additional root on the worker. Assetless/mask-only validation and the existing
image/mask name policy are preserved. Unprepared preview capture and ordinary display activation remain open.

The clipboard change is a fixture correction. Production already admits metadata inspection and full
read/decode separately under one selected ordinal/history stamp. Its native acquisition counter includes
successful inspection. The old fixture incorrectly equated all waits with the first ticket and counted only
armed waits. The correction verifies each original admitted phase and the one permitted handoff.

## Independent private evidence

Evidence is retained under `Z:\src\RedPrism\.build\P1Evidence\`.

| Candidate | Native result | Scope |
| --- | --- | --- |
| `p1-raster-preflight-private-r1/ControlDebug` | exit 5, exactly five allocation assertions fail | Known vector allocation is observed; functional/golden assertions pass |
| `p1-raster-preflight-private-r2/Debug` | exit 0, 174 assertions | Six calibrated Debug-only allocation observer assertions plus 168 functional assertions |
| `p1-raster-preflight-private-r2/ASanDebug` | exit 0, 168 assertions | Functional/golden proof with actual ASan instrumentation |
| `p1-raster-preflight-release-private-r3/Release` | exit 0, same 168 assertions | Release code path; no CRT allocation event claim |
| `p1-raster-gradient-capture-focus-private-r2/ControlASanDebug` | exit 5, exactly ten new worker-capture assertions fail | All 446 original prepared-raster assertions pass |
| `p1-raster-gradient-capture-focus-private-r3/ASanDebug` | exit 0, 466 assertions | All 446 originals plus ten setup and ten worker-capture assertions |
| `p1-clipboard-retry-witness-private-r2b/ASanDebug` | exit 0, 77 assertions | 55 previous native assertions, 19 witness controls and three actual second-holder handoff assertions |
| `p1-clipboard-retry-witness-control-r3/ASanDebug` | expected exit 5 | Reinstated old predicate and its aggregate fail; all three new native handoff assertions pass |

All three focused preflight profiles independently verify four fresh/281 exact inherited objects, actual
compiler profiles, full PDB metadata, PE RSDS GUID/age pairing and zero ISA violations. Release and ASan
functional assertion multisets are identical; Debug differs only by its six named observer assertions.
Receipt: `p1-raster-preflight-release-private-r3/main-qualified.json`.

The corrected clipboard candidate independently verifies two fresh/349 exact inherited objects, real ASan
flags, full matching PDB and zero violations using the active checkout's scanner. Main corrected the cheaper
helper's stale scanner path and added an explicit PE RSDS GUID/age comparison before accepting the result.
Receipt: `p1-clipboard-retry-witness-private-r2b/ASanDebug/clipboard-image-verified.json`.

## Retained refusals and scope limits

The original full capture control stopped before its new target tests at the old clipboard assertion; that
run is retained and supplies no target-capture negative control. The focused target control above supplies
that separate evidence. The first r2 clipboard build was denied read-only Git access to the pinned dependency;
its failure is retained, and r2b retries with the required access. No dependency code or pin changed.

The first preflight ASan allocation observer did not see even its known vector allocation. Its refusal is
retained; the corrected fixture limits allocation event claims to native Debug and preserves ASan functional
checks. No allocation-event claim is inferred from an uncalibrated zero count.

Native ARM64 runtime testing is unavailable here. These private component results do not complete the latest
canonical matrix, ordinary activation, initial physical Ctrl/lift, the remaining capture/display bounds or G1.


## Native session-end deadline correction and combined integration

The first combined ASan candidate r4 reaches149/152 stages and stops with exactly three failures in
`save-with-earlier-unsaved-query`: expected event wait, cancel flag and receipt. All128 new capture/clipboard
assertions and all other predecessor assertions pass. The published literal pixels and saved-revision checks
also pass. Original stdout/stderr and the byte-verified retained filesystem fixture are preserved under
`p1-raster-gradient-capture-integrated-private-r4/ASanDebug/retained-failure.json`.

A focused diagnostic repeat passes702 assertions and establishes no historical cause. A deterministic native
counterexample holds the actual post-publication worker until WM_ENDSESSION returns from its real deadline:
waitResult258 (WAIT_TIMEOUT), remaining1500, exact cancellation and E_ABORT receipt, correct new file/saved
revision. It fails exactly the same three old assertions. No sleep, fabricated result or changed deadline is
used. Its source/logs are retained under `p1-file-end-session-deadline-control-private-r3/ASanDebug`.

The fixture now validates the actual bounded event-or-deadline outcome separately from publication truth read
only after original worker acknowledgement. Literal old/new pixels and history/path truth remain independent
checks. Two explicit native deadline cases cover before and after publication with the earlier unsaved query.
The corrected focused scope passes778 assertions: all702 predecessors plus76 facts across56 native cases.
All producer, writer, cancellation and1500ms deadline source files are unchanged. The focused image verifies
ten actual ASan compiler records,341 baseline-exact objects, full matching PDB and zero ISA violations.
The earlier private diagnostic harness compile refusal (nonexistent _selfTest field) remains retained.
Receipt: `p1-file-end-session-deadline-fix-private-r4/ASanDebug/runtime-qualified.json`.

The final combined private r5 Application passes152 stages /11,527 assertions: all11,323 prior facts plus80
RasterEdit, six Gradient commit,20 prepared Gradient,19 clipboard witness, three native clipboard handoff
and76 deadline/publication facts. Eight fresh compiler records,343 inherited objects, source/object bookends,
real instrumentation, PE RSDS/full-PDB identity and zero ISA violations pass independent verification.
This is the full Application self-test, not a fresh complete five-suite canonical run or an ASan UAF probe.

Exact source adoption copies11 code/test/header paths and four owning-contract/evidence paths. Before/after
copies and identities are retained at `p1-raster-gradient-capture-integrated-private-r5/Adoption/source-adoption.json`.
Skills15, formatting1311 files and whitespace checks pass after adoption. Canonical latest-source x64/ARM64
qualification and the completed-step commit/push remain due. All nine milestone gates remain open.

## Combined canonical continuation

Combined-source ARM64 Debug c2armd1, Release c2armr1 and real-instrumented ASan c2arma1 cross-builds exit0.
Each independently qualifies994 compiler records and five AA64/full-PDB pairs; the same source bridge has
13 fresh/981 exact inherited objects/profile. Native ARM64 execution is unavailable. This covers source freeze
3A23B05EA2297E9AE84B9B5516A0BF9327405E85BE1C7242E545D2951ED70F95 before the following fixture correction.

Canonical x64 Release c2r1 exits1 at one composition exact-charge assertion after Host and Document pass;
application/I/O later gates were not reached. Its2958-file archive and994 compiler/five PE-PDB metadata
records are verified, with runtime explicitly failed. The failed-run archive omits ISA products, so no archived
ISA qualification is inferred from that compiler reader. The original missing-artifact reader refusal remains.
The event-held cache release control and fixture correction are recorded separately in
[P1PublicationParentRetirement](P1PublicationParentRetirement_2026-10-19.md). Current canonical matrix remains due.

## Corrected composition fixture and subsequent Release failure

The corrected-source c3 ARM64 Debug/Release/instrumented-ASan matrix independently verifies994
compiler records/profile, one fresh composition test /993 exact inherited objects and five AA64/full-PDB
pairs per configuration. Source freeze is A1CC48E8CEC6B5590282510C9BFFAC02D7E53FD55F2FB8AD961B9EE92062CD21.
No native ARM64 runtime claim is made. Receipt: `p1-capture-combined-native-r3/arm64-source-object-bridge-qualified.json`.

Canonical Release c3r1 passes Host, Document, Composition (including the corrected exact-charge observation),
I/O and crash probes, then exits1 when the hidden Application returns5 at
`panel-wand-host.levels.layer.add.original-request-joins-and-finishes`. Its3111-file archive,994 compiler
records, five PE/full-PDB pairs and archived ISA artifacts verify; runtime qualification is explicitly false.
Archive manifest DF2B102146D5BE1B428A93A8605B18802A6083E3B051A86E829217D6EBD2D87D.

A separate full-context diagnostic image changes only the panel/Wand fixture: one fresh CPP /350 exact
inherited Application objects. It passes152 stages /11,527 facts (PID61296, exit0), with all30 panel/Wand
combinations passing. This repeat establishes no historical cause or causal fix. Its extra unconditional
tracing stays private. Root adopts only failure-only numeric diagnostics for settled/receipt/history/queue
state, preserving the exact original predicate and15000ms failure bound. Current canonical x64 validation
and the unresolved historical failure remain open; P1 is WIP.


## Latest completed canonical matrix

The combined current-source c4 Debug/Release/real-ASan and all three ARM64 cross-build profiles independently pass. The [matrix record](P1CaptureMatrix_2026-10-19.md) supersedes pending validation statements in the historical sections above. The c3 panel/Wand cause, native ARM64 runtime, fresh final analysis, ordinary activation and remaining P1 delivery gates stay open.
