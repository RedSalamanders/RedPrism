# P1 Scalar Selection Closure: Private Host Qualification — 2026-10-16

## Closeout checklist

- [x] Record the privately qualified R6 actual-host slice and its source, dependency, product, runtime, reader, and archive receipts.
- [x] Preserve the R1–R5 failed/refused attempts and their verified causes without promoting them to successful qualification.
- [ ] Wire and qualify actual physical press/branch routing.
- [ ] Adopt the R8 host source and complete the canonical native matrix, including x64 Release, Debug, real ASan Debug and ARM64 cross-builds; native ARM64 execution requires an available host.
- [x] Close the stronger worker-allocation and final zero-reserved/releasing-byte predicates on the R8 private host.
- [ ] Complete G1 and P1 closeout.

**Evidence class:** private x64 Release/WARP host evidence. This report is evidence only; it does not change product requirements or mark the P1 plan Done.

## Qualified R6 slice

The authoritative closeout receipt is [`main-qualified.json`](../../.build/P1Drafts/S5ScalarCloseLifetime-r6-Main/main-qualified.json). It records `MAIN_PRIVATE_R6_REAL_HOST_71_ALL_APPLICATION_PREDECESSORS_AND_LONG_FAULTS_QUALIFIED`. The actual private `RedPrism.exe --self-test --warp` process exited 0, started at `2026-10-16T16:50:46.353917Z`, and completed at `2026-10-16T16:52:36.170598Z`. Its stdout SHA-256 is `F717ABCE8C7D8CA9BB8215B6E83D4568895FC40292A848DBE3F058A385E39242`; stderr SHA-256 is `0931D2E62A0549FAC958D5EB08590D9198C1A4CA33DC70E26BD44868F031050A`.

The independent roster records 71 new scalar-closure assertions over 143 application stages, alongside all predecessor Application totals: 8,271 stdout assertions and 7,613 stderr assertions. The private host used two real worker threads and WARP. The new model is seeded with a worker-prepared `Begin`; it then exercises the existing production `Close` job and ownership path. It does not claim that physical pointer press or branch routing is wired.

The build receipt [`build-RedPrism-result.json`](../../.build/P1Drafts/S5ScalarCloseLifetime-r6-Main/actual-release-r1/build-RedPrism-result.json) records a successful x64 Release MSBuild from the R1 `SourceFull` tree, with stdout SHA-256 `EA381AFAB1FEB32CD5558C855AA92CC4D7C41FA385A5A45894D6B2508966139C`, empty stderr, and timestamps `2026-10-16T16:49:59.181947Z`–`2026-10-16T16:50:30.866738Z`. The qualification ties source/dependency/product bookends to 336 actual Release compile-command records. The last fixture rebuild compiled one object; 335 other objects were byte-identical. This 336-command receipt is the authoritative actual command roster; the R2 guard had previously mistaken legitimate MSBuild rebuild behavior for an unsafe build.

The executable and PDB are an exact PE/CodeView pair: executable SHA-256 `D1E70E97D62BC59E27BBDE706A93601CA977E5B6F745AEF9EB1B91279AF353D0`, PDB SHA-256 `8494B6BE92C61594C95C5C33C91554377D5F2265331BC0239644587455A7E348`, CodeView GUID `3ADE4AA6-A0D4-4C83-B8ED-BDBCFC51438B`, age `4`. The recorded exact native PE/PDB parser SHA-256 is `42CD8F86F3012FE5CA76A6B5F26CC3DA5A644B20E6B433CB7C1D2FF995328AF7`. The ISA scan exited 0 and reported zero violations ([receipt](../../.build/P1Drafts/S5ScalarCloseLifetime-r6-Main/actual-release-r1/isa-RedPrism-result.json)).

The long-path collision fixture now prepares the complete future collision path on the fixture/owner side using the existing `Win32FilePath` helper. The worker seam remains bounded and allocation-free; it does not synthesize a failure. R6 observed all four intended native creates succeeding exactly once, each with `error=0` and `status=5` (`Encode`): save/folder × end/false cases. The observations are preserved in `main-qualified.json`. All original F6 predicates passed in R6.

The R6 archived source is bound by its manifest. The working [`SourceFull`](../../.build/P1Drafts/S5ScalarCloseLifetime-r1-Main/SourceFull/) and the long-collision fixture draft in [`P1F6LongCollision-r1-Main/Source`](../../.build/P1Drafts/P1F6LongCollision-r1-Main/Source/) are navigation pointers; SourceFull is reused for later candidates and is not a current R6 source bookend. The R6 runtime product and raw stdout/stderr are under [`actual-release-r1`](../../.build/P1Drafts/S5ScalarCloseLifetime-r6-Main/actual-release-r1/). The source, dependency, and product bookends are recorded as true in the qualification receipt.

### Reader correction and archive

The first R6 reader process exited 1 after the successful runtime because its stage parser expected a `SUMMARY`-prefixed Panel/Wand summary. The actual predecessor output has a valid summary line without that prefix; this was a reader grammar mismatch, not a host assertion failure. The original runtime logs and first refusal are retained in [`reader-RedPrism-result.json`](../../.build/P1Drafts/S5ScalarCloseLifetime-r6-Main/actual-release-r1/reader-RedPrism-result.json) and [`reader-RedPrism.stderr.log`](../../.build/P1Drafts/S5ScalarCloseLifetime-r6-Main/actual-release-r1/reader-RedPrism.stderr.log).

The corrected reader is [`verify-private-app.py`](../../.build/P1Drafts/S5ScalarCloseReader-r2-Main/verify-private-app.py), SHA-256 `44C7D482316C5C74E4DA87289196A003C540E72A9852D5BCC6445B272FF9DEAB` as recorded in `main-qualified.json`. It changed source-only grammar, passed 2 positive and 12 negative controls, and reread the original R6 runtime output only. There was no new native build, source/golden/expectation edit, or runtime retry.

The completed archive receipt is [`archive-receipt.json`](../../.build/P1Drafts/S5ScalarCloseLifetime-r6-Main/archive-receipt.json): 2,009 archive copies, manifest SHA-256 `212F20ECBCF6C4D1AB7C238D21E25F922693A176CA49FADE37C10AA748A4D552`, completed `2026-10-16T16:52:48.825601Z`, completion SHA-256 `539FAEBA8F0D8786A5DE85A8660C27AFD7520246F2AD6EC68A8A905BFD2FDCCE`. The archive is at `Z:\CodexArtifacts\RedPrism-P1\2026-10-16\P1-ScalarSelectionClosure-x64-Release-private-r6`.

## Attempt history and preserved evidence

Each attempt's receipts and archive record are linked below. Earlier failures remain part of the record; R6 success does not erase or reinterpret them.

| Attempt | Verified outcome |
| --- | --- |
| R1 | [`main-failed-closeout.json`](../../.build/P1Drafts/S5ScalarCloseLifetime-r1-Main/main-failed-closeout.json): Release rebuild failed before runtime. The callback fixture treated `MessageDispatch` as `bool` although it returns `void` (C3487/C2562). The 1,974-copy archive is recorded in [`archive-receipt.json`](../../.build/P1Drafts/S5ScalarCloseLifetime-r1-Main/archive-receipt.json). |
| R2 | [`main-build-qualified-runtime-due.json`](../../.build/P1Drafts/S5ScalarCloseLifetime-r2-Main/main-build-qualified-runtime-due.json): actual MSBuild exited 0, but the overstrict one-object reuse guard refused before runtime. MSBuild had rebuilt 16 translation units: 15 existing plus the new fixture; 320 other objects were byte-identical. The source/dependency bookend was true, and the 336-command roster was subsequently verified by Main. The 1,994-copy archive is in [`archive-receipt.json`](../../.build/P1Drafts/S5ScalarCloseLifetime-r2-Main/archive-receipt.json). |
| R3 | [`main-failed-closeout.json`](../../.build/P1Drafts/S5ScalarCloseLifetime-r3-Main/main-failed-closeout.json): one new fault-classification fixture assertion failed. Capture/history prepare faults must classify as `Failed`; a fixture ternary classified only `WorkerFailure` as `Failed` and incorrectly fell through to `Stale`. No production or legacy golden change was made. The 2,002-copy archive is in [`archive-receipt.json`](../../.build/P1Drafts/S5ScalarCloseLifetime-r3-Main/archive-receipt.json). |
| R4 | [`main-failed-closeout.json`](../../.build/P1Drafts/S5ScalarCloseLifetime-r4-Main/main-failed-closeout.json): all 71 new scalar assertions passed, but 32 inherited F6 file-fault assertions failed. At that point the cause was explicitly unclaimed. The 2,006-copy archive is in [`archive-receipt.json`](../../.build/P1Drafts/S5ScalarCloseLifetime-r4-Main/archive-receipt.json). |
| R5 | [`main-failed-closeout.json`](../../.build/P1Drafts/S5ScalarCloseLifetime-r5-Main/main-failed-closeout.json): each native staging search matched once, but unprefixed collision-directory creation returned Win32 error 3 (`ERROR_PATH_NOT_FOUND`); the real save returned status 0. The observed system had `LongPathsEnabled=0`; the longer compiler TEMP was inherited by the runtime instead of restoring the ordinary short TEMP. These observations explain that attempt's intended fault not being reached; they do not establish a cause for historical save failures. The 2,006-copy archive is in [`archive-receipt.json`](../../.build/P1Drafts/S5ScalarCloseLifetime-r5-Main/archive-receipt.json). |
| R6 | Qualified private host evidence described above. R6 corrected the fixture by preparing the full future collision path on the owner side through `Win32FilePath`, with the worker seam bounded and allocation-free. All four actual creates succeeded, and all original F6 predicates passed. |

## Scope and remaining gates

This is a privately qualified actual-host slice: x64 Release, two real workers, WARP, seeded worker-prepared `Begin`, production `Close` job ownership, 71 new scalar-closure assertions, the full predecessor Application stdout/stderr assertion totals, and the four R6 native collision cases. It is not ordinary application activation, physical pointer press/branch routing, G1, canonical native-matrix qualification for the R6 source, native ARM64 runtime qualification, or P1 Done. No new host ASan run has been performed.

The existing native matrix receipts under [`S5NativeMatrix-r2-Main`](../../.build/P1Drafts/S5NativeMatrix-r2-Main/) are for the older S5 core, not the R6 host source. They include analysis `r1066`, Release `r1067`, Debug `r1068`, ASan Debug `r1069`, and ARM64 work `r1070`–`r1072`; they cannot be used as qualification of the R6 additions. The matrix's current progress receipt itself states `NATIVE_MATRIX_IN_PROGRESS` and `P1_done: false` ([`progress.json`](../../.build/P1Drafts/S5NativeMatrix-r2-Main/actual-r1/progress.json)).

At R6 closeout, the separate strengthening work was still pending in [`S5ScalarCloseStrength-r1-Main/review.json`](../../.build/P1Drafts/S5ScalarCloseStrength-r1-Main/review.json). R8 below closes those stronger predicates on its private host. That does not add ASan qualification or adopt the R8 source into the canonical native matrix. Ordinary activation/physical routing, the native matrix, ARM64 cross-builds, G1, and P1 closeout remain open. Native ARM64 runtime is unavailable and is not claimed.


## R7 source qualification and R8 Main qualification

R7's build/guard refusal remains as recorded: dependency-log paths were uppercase while object filenames
retained source case. Main independently verified the exact ten recompiled dependencies, 326 unchanged objects,
and the complete failed/refused archive. R8 used the frozen R7 image for runtime qualification; it did not
replay compilation or alter source/expectations. The R1–R7 attempt records above remain unchanged, including
their failures and refusals.

R8's [`independent-runtime-roster.json`](../../.build/P1Drafts/S5ScalarCloseLifetime-r8-Main/independent-runtime-roster.json)
reports `PASS_PRIVATE_REAL_SCALAR_CLOSE_HOST_71_AND_ALL_PREDECESSORS`: 71 new assertions over 143 Application
stages, plus all predecessor Application totals (8,271 stdout and 7,613 stderr assertions). The actual private
x64 Release `RedPrism.exe --self-test --warp` process ran on the inherited default desktop with WARP and exited
0 (PID 90392; `2026-10-16T17:01:50.149210Z`–`2026-10-16T17:03:24.476612Z`). Its stdout SHA-256 is
`CC2CD1A1387662A78AAB77B82675C145AF3448F4EFDEAAA33AE5350C355818D0`; stderr SHA-256 is
`0931D2E62A0549FAC958D5EB08590D9198C1A4CA33DC70E26BD44868F031050A`. The stronger assertions include the
actual worker-thread allocation trace and require zero final reserved and releasing bytes. The stronger R7 source adds the test gate allocation flag and its fixture checks; production job behavior
remains unchanged from R6. R8 retains the same 71 expected facts and labels and runs the frozen R7 image.
The inherited repair-scope receipt describes the earlier fixture correction, not the complete R7 source delta.

Main completed independent archive readback and qualification in
[`main-qualified.json`](../../.build/P1Drafts/S5ScalarCloseLifetime-r8-Main/main-qualified.json), status
`MAIN_PRIVATE_R8_STRONG_REAL_HOST_71_ALL_PREDECESSORS_QUALIFIED`. It verified all 2,006 archive copies,
the 336 frozen compile objects, source/dependency/product bookends, exact PE/PDB identity (CodeView GUID
`3ADE4AA6-A0D4-4C83-B8ED-BDBCFC51438B`, age 5), zero ISA violations, the 71 stronger new facts, and all
predecessor Application assertions. The receipt binds the archive completion SHA-256 to
`A44392A08FC71C5B8971A8652DDB06520254E4FDE8BBB10461786E97CEA5D080`, matching
[`archive-receipt.json`](../../.build/P1Drafts/S5ScalarCloseLifetime-r8-Main/archive-receipt.json), which records
the attempt archive at
`Z:\CodexArtifacts\RedPrism-P1\2026-10-16\P1-ScalarSelectionClosure-x64-Release-private-r8`, and
`actual_result_present: true`. This is private x64 Release/WARP host qualification only. The receipt explicitly
records `native_matrix: false`, `ordinary_activation: false`, `native_ARM64_runtime: false`, `G1: false`, and
`P1_done: false`. Native Debug/Release/ASan matrix adoption, ARM64 cross-build/runtime qualification, ordinary
activation and physical press/branch routing, G1, and P1 Done remain due.

## Real-ASan host continuation

The isolated stronger-source ASan Debug build is running under
[`S5ScalarCloseAsan-r1-Main`](../../.build/P1Drafts/S5ScalarCloseAsan-r1-Main/). Its fresh source copy, vcpkg dependencies and pinned DxUi ASan library are frozen before compilation; the library hash matches the existing native provenance. The runner requires actual ASan compiler commands, two linked ISA checks, the deliberate HostTests UAF diagnostic and the complete 143-stage Application roster. No ASan success is claimed before those processes and independent archive checks finish.

## Preserved real-ASan attempts

ASan R1 successfully builds the application (PID 91256, exit 0). Main verifies every one of 336 actual compiler commands uses `/fsanitize=address`, `/MDd`, `/Od`, `/Zi` and the exact Common forced-include guard, plus exact executable/PDB identity. Its following HostTests helper build records C1033; ENOSPC prevents its joined numeric child result from being persisted. Main retains all 2,028 archived files, completion `7CD6C11E4FD462805986E9E829DEBB5F8FE1C7F5D83803876162C338F07A7DD8`. The earlier path-case reader refusal remains retained; only exact canonical Windows path comparison changes, with all instrumentation/source-set requirements kept.

R2 continues with the exact successful instrumented application and a freshly executed, authenticated earlier DocumentTests UAF control using a byte-identical ASan runtime DLL. Linked ISA passes and the real UAF control diagnoses correctly (exit 1). The application exits 5, PID 77612, at 17:52:38.567586 UTC. All 71 new scalar facts pass; the run reaches 137 stages and fails exactly `clipboard-delayed.paste.unrelated-read-before-worker-cannot-enter-event-gate`. The complete predecessor roster therefore does not qualify. Main verifies all 35 supplemental archive files and the original source/dependency/product reference; completion `E0388165CEA95733A6E09410886EF509A4CAB06A2954A6E643AC68D444EAC469`. [Failure receipt](../../.build/P1Drafts/S5ScalarCloseAsan-r2-Main/main-failed-closeout.json).

The clipboard failure's cause is unestablished. Native Debug overlapped, but that does not establish causation. Private R3 adds only native owner/window, handle/error, sequence and render/worker-counter diagnostics. Reversing that insertion reproduces the exact original source text; every assertion and label remains unchanged. Its build/ISA run is followed by a wait for both native Debug and ASan parent results, preventing another overlap of these clipboard suites. No passing repeat will be treated as a cause for the older failure.

## Diagnostic ASan R3 build-only and serialized R4 runtime

R3 incrementally rebuilds exactly `ApplicationClipboardDelayedTests.obj`, with 335 original instrumented
objects unchanged. Build and ISA exit 0. Its unchanged-assertion fixture diagnostic records the clipboard
owner/sequence, worker-read flag and unrelated/render counts around the deliberate early read. The driver
then refuses before runtime because native Debug r1073 failed. All 34 supplemental copies are verified;
completion `51EFD7A2CBA2E9799DB4DCFA417E24775BA9D4D967349AFB1D78057574F476FB`.
No R3 runtime or detection probe is claimed.

Fresh R4 runs that frozen diagnostic image without compilation and with no other Application clipboard
suite active. It retains the original early-read assertion and every predecessor expectation. Its actual
result remains pending; a passing repeat alone would not establish the cause of R2's failure. Native
successful-publication gate validation waits for this private Application child to join before launching.

## Serialized real-ASan R4 independently qualified

The actual frozen diagnostic Application exits 0 (PID 10244; 18:33:19.760674–18:40:49.872492 UTC).
All 71 stronger scalar-closure facts and all predecessor assertions pass over 143 stages. The deliberate
unrelated clipboard read reports the intended real owner/reader, no returned handle, one unrelated-render
callback, zero gated renders and a false worker-read flag. Its original negative assertion passes. This
passing serialized repeat does not establish the cause of R2's retained failure.

Main verifies the 39-file R4 archive and 34-file diagnostic-build archive, all source/dependency/product
bookends, 336 exact instrumented command records and 335 unchanged original objects. Only the diagnostic
fixture object was rebuilt. The full-stream reader passes twelve independent rejection controls; ISA has
zero violations and the actual UAF probe is detected. Linked GUID `0C9EEAA9-08BC-4A32-ABA2-5E7CE1D244B4`, age 2;
executable `8B0DB5E931D6FFB42BCF6E6BB920220BD5FA4109301ADD40B0A4F48BD015C42A`;
PDB `669E770239833360F938FF779B97946B29CECA0FE734FB70450EC06105200FE8`.
Completion: `388F372F0548F38659E919F946ACAC5A0F8B75B7604EECBF745B58857791F44D`.
[Main qualification](../../.build/P1Drafts/S5ScalarCloseAsan-r4-Main/main-qualified.json).

This qualifies the stronger private scalar host in Release and real ASan. Native adoption/contracts and
actual physical-press routing remain due. The current native clipboard-phase repair is independent:
canonical Debug r1074 precedes real ASan r1075, ARM64 r1076–r1078, Release analysis r1079 and full Release r1080.
Native ARM64 runtime, ordinary activation, complete P1 delivery and G1 remain open.
