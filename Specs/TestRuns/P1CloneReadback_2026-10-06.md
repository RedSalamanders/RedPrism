# P1 Clone All Layers source readback

## Checklist

- [x] Implement admitted tiled committed-source readback and capture pointer-time offset/tip/FIFO ownership.
- [x] Independently review the delegated core tests; repair their mutable fixture construction and strengthen
  rejected-input checks to preserve raster identity, revision and alignment.
- [x] Add actual host pixel/history, cancellation, failure, stale and device-recovery cases.
- [x] Primary lifetime review: retain the sample's original charge through live stroke and worker retirement;
  keep it through shutdown's owner graphics detach until final session/stroke retirement.
- [x] Add physical capture-loss and independent actual-owner shutdown cases at copied-worker/live-sample boundaries.
- [x] Preserve r255's original recovery-fixture failure and r256's compile diagnostic with verified archives.
- [x] Qualify the repaired source in full Debug r267, Release r268, real ASan Debug r270, Release analysis r271 and all three ARM64 r38 cross-builds.
- [x] Independently verify unchanged predecessor controls and exact tested-source equality; archive every profile.
- [ ] Commit and push this qualified source and contract update.
- [ ] Finish remaining P1 callers, resource activation and due G1 measurements before the parent moves to Done.

Status: **the current `023602…` 867-input source passes full Debug r267, Release r268 and real ASan r270 with independent predecessor/source/product verification, Release analysis r271, and all three ARM64 r38 cross-builds. Failed ASan r266 and analysis r269 are preserved.** The older `AD5015…` and 865-input receipts below qualify their exact earlier source only. This record covers Clone source
readback and shutdown ownership. It does not qualify owner stroke painting/commit, complete resource uploads,
other live-context callers, G1 latency or the entire P1 milestone.

## Implementation contract

Eligible normal All Layers Clone input joins the original continuation queue. Its Down is not consumed until
the complete committed composite is available; Move/Up/capture-loss stay in the bounded FIFO. Source offset
and brush settings are captured at press. Later source, alignment, All Layers or palette changes cannot
reinterpret accepted stroke input. A current-only completed readback transfers its original scheduler memory
reservation into a shared retained lease, with no new admission or early uncharge. The live sample graph
owns the lease before its pixel/document members, so final graph destruction releases the charge last.

The hidden owner shutdown path retains this sample until the session's final MTA retirement has released the
stroke's independent source reference. Joined utility work acknowledges its exact ticket/history stamp after
workers and mappings join; `Close` suppresses ordinary completion callbacks, so they cannot be relied on for
shutdown bookkeeping. A test-only gate waits on the actual shutdown event; production adds no such wait.

## Independent expectations

Core: supplied composite pixel `(2,1)` is literal BGRA `(11,22,33,255)`, captured offset `(-4,0)` paints target
`(6,1)`, neighbor `(7,1)` stays `(0,0,200,255)`, and exactly one `Clone Stamp` entry publishes. Missing, empty,
wrong-size, Gray8, nonfinite and integer-overflow captured inputs preserve source identity, revision, history
and alignment. These controls use explicit immutable fixture sealing and the CPU reference engine.

Actual host: red source left of x=8, blue source to its right, initially empty destination, source `(4,6)`,
press `(12,6)` and captured offset `(-8,0)` must produce BGRA `(0,0,255,255)` at the destination. Undo restores
its empty asset; Redo restores one Clone entry. Cancellation, failure and stale results publish no Clone edit.
Physical capture loss seals the accepted prefix before copy acknowledgement. View input remains responsive
while a detached worker is held. Hidden cases never Present. The original capture/ticket/charge survives a
device recovery; the third device change fails under the unchanged two-restart allowance.

Independent hidden owners shut down at the copied-worker and live-sample boundaries. They verify pending
receipts become cancelled without committed pixels/history, the live charge survives graphics detach, and
joined worker retirement leaves no sample, draft, ticket, history barrier, queue work, reservation or sink.

## Original failed attempts

| Attempt | Actual result | Evidence and correction |
| --- | --- | --- |
| Full Debug r255 | Exit 1 after Clone recovery pixel/Undo/Redo passed | Fixture counted only two prepares for one recovery. The shared readback contract calls prepare once initially and twice per successful restart (recovery callback, then new-attempt preparation). Corrected gate triggers pre-copy changes only on `S_OK`, counts recoveries separately, and expects 3/5 preparations for 1/2 recoveries. The two-restart and exact-pixel controls are unchanged. |
| Full Debug r256 | Exit 1 during compilation; no fresh application runtime | New standalone test passed `const MSG` to the established mutable dispatch API. Corrected the borrowed message reference. |
| Full Debug r257 | Exit 1; application exited `0xC0000005` after all seven held-readback shutdown checks passed | The new fixture explicitly reset its unique Application owner, then its scope cleanup unconditionally called `Shutdown` on the null pointer. Corrected cleanup to guard the owner, matching the existing standalone fixtures. This is a fixture lifetime error; no live-sample qualification follows from this attempt. |
| Full Debug r261 | Exit 1 during compilation; no fresh application runtime | New pan test compared signed history counts with unsigned constants, producing C4389 under warnings as errors. Use signed integer literals; expectations are unchanged. |
| Full Debug r262 | Exit 1; all 81 readback/pan controls pass, five policy checks fail | WM mouse coordinates are whole client pixels; an odd canvas extent positioned the new fixture's document grid half a pixel off that grid. Align the test viewport after layout before sending actual host pointer input. Keep exact source, offset, pixel, history and capture assertions. |

r255 freeze: 865 inputs, SHA256
`BA13B6DE3397EE715FCAF099261FE11FAA5CA4E85AC6D1F104B390F487BCFE06`.
Actual runner interval: 2026-10-06 15:03:35.007647–15:11:45.139608 UTC. Archive
`D:\RedPrismQualificationArchives\P1-clone-readback-Debug255-failed-2026-10-06` independently verifies all
1,140 copies; completion SHA256 `B14E3F70864A492AAB64F84F4A41F70CC612CF5EEFEF81E20F1BFD1FC33C88BE`.

r256 freeze: 865 inputs, SHA256
`74B2A1D0B851E175DA4E09DC3816A9991C0E66930993731CA034C8BFCE74EE18`.
Actual runner interval: 2026-10-06 15:14:43.622518–15:15:29.336308 UTC. Archive
`D:\RedPrismQualificationArchives\P1-clone-readback-Debug256-compile-failed-2026-10-06` independently verifies
all 1,140 copies; completion SHA256 `79495B6D5C80ED00BFDF7B0F89249200E6D9E7CC2ED47903DD48C83712BC450A`.
Its old output-directory application log is excluded from runtime qualification.

r257 freeze: 865 inputs, SHA256
`F423878F7E9C134DB4D89E8CFEF043D70C0FD3AB738E7995245F2D097DC647EB`.
Actual runner interval: 2026-10-06 15:16:26.927074–15:21:58.723236 UTC. Archive
`D:\RedPrismQualificationArchives\P1-clone-readback-Debug257-fixture-crash-2026-10-06` independently verifies
all 1,140 copies; completion SHA256 `D785887395CA9F691B15D9DE5E903F344FAADDAEB12D7302B54AEE72C7D6CEA0`.

Sampler diagnostics print only on the unchanged transparent-tail literal failure. No cause for the earlier
intermittent failure is established here. Full application/clipboard runtime configurations run sequentially.

## Current qualification

The completed full Release r268 child PID 5752 ran 2026-10-06
17:45:58.139347–17:58:31.927463 UTC and exited 0 on the same 867-input freeze
`0236021DC5A19194AA4AC527FF628A7980A131A78D2A7337C7E40A5D157E75E4`.
Independent registry verifier r4 matches real ASan r270's exact source and preserves every predecessor:
10,158 named application controls / 92 stages, 110 new host controls, 12 core controls and 119 scanner controls.
All suites, crash probes and application harness screenshots pass. Archive
`D:\RedPrismQualificationArchives\P1-clone-pan-867-Release268-2026-10-06` independently verifies
1,149 copies; completion SHA256 `E19AEE3785C7DD935C7A259CC6B6FE6AB245A15D1FCB91C15BB4127B5CA98302`.
The complete current matrix now passes. Repository skills validate 15 entries; formatting checks 740 files;
`git diff --check` passes. This functional matrix is not a P1/G1 latency or production-residency exit.

The original r261 compile failure is independently archived with 1,144 copies, completion SHA256
`7202854224714D66FE9AB844EC32EC234999A0C68EEBDD6BE452B3B5C8677E15`, under
`D:\RedPrismQualificationArchives\P1-clone-pan-Debug261-compile-failed-2026-10-06`.
Its 867-input freeze is `1CBCFD4F42B26639869B5ED76860957944D07E6ECD96C9D7DB74916F61758520`.
Actual runner: PID 106524, 2026-10-06 16:00:27.370226–16:01:12.329851 UTC, exit 1.

The original r262 policy failure is independently archived with 1,144 copies, completion SHA256
`382636C23697FD573A79048E2F42FA472F10D8568BBDE98B1BC445CBFA32C51D`, under
`D:\RedPrismQualificationArchives\P1-clone-pan-Debug262-policy-failed-2026-10-06`.
Its 867-input freeze is `94891E52181B40EA918BD80BCA487C31ABED396F52B87A9EF6DAD7563CC9ADB9`.
Actual runner: PID 104620, 2026-10-06 16:02:21.723546–16:09:00.569315 UTC, exit 1.
All earlier suites and all 81 readback/pan controls pass, but the new policy stage fails; this is no full
qualification. The corrected source starts a fresh full Debug attempt r263.

### Revised 867-input source

Freeze SHA256: `AD50152361F49A30A7C94A90FB6EFCA01E04767F3B4E54A7AE05A6FDD21DBCD4`.
Full x64 Debug r263 exits 0: actual PID 69268, 2026-10-06 16:11:16.041557–16:22:40.556738 UTC.
Primary-reviewed independent registry r3 verifies 10,158 named application controls / 92 stages, all preceding
controls, all 110 new host checks, 12 core checks and 105 scanner controls. Suites retain 7,657 Host / 7,225
Document / 1,694 Composition / 1,736 I/O checks. All 15 skills and 740 formatted files pass. Its archive,
`D:\RedPrismQualificationArchives\P1-clone-pan-867-Debug263-2026-10-06`, independently verifies 1,147 copies;
completion SHA256 `0E434CF62C12D3559848F4489D85CCCB03A0206C50149D1C7D87F948DED7BE7E`.

ARM64 r37 cross-builds pass on the identical freeze:

| Configuration | Actual child / UTC interval | Archive completion SHA256 / verified copies |
| --- | --- | --- |
| Debug | PID 108284; 16:15:01.379188–16:16:04.571198 | `B91E231F17772E0B42EEB479073673BDDDCA56A7C4BA2EE84A5721C04D758B18` / 938 |
| Release | PID 90756; 16:16:09.234736–16:17:36.549710 | `D39376468589A37522C6E5A70E45A7A59D19CD866881C82BFABB84A1D32D56D7` / 938 |
| ASan Debug | PID 60520; 16:17:41.944481–16:18:43.699396 | `B7C642B3D868715586356C9D184B1C9C746470C4167658FD69D34E883AD37B72` / 942 |

Archive names under `D:\RedPrismQualificationArchives` are
`P1-clone-pan-867-ARM64-Debug37-2026-10-06`, `P1-clone-pan-867-ARM64-Release37-2026-10-06` and
`P1-clone-pan-867-ARM64-ASanDebug37-2026-10-06`. These are cross-builds, not native ARM64 runtime evidence.
Release analysis r264 and real x64 ASan Debug r265 used this same freeze; their results and archive receipts
are recorded below and do not qualify the newer `023602…` candidate.

### Focused Release analysis, failed ASan scan, and new candidate

These receipts are separate from the new `023602…` candidate. Full Debug r263, ARM64 cross-build r37, and
Release analysis r264 all use the `AD50152361F49A30A7C94A90FB6EFCA01E04767F3B4E54A7AE05A6FDD21DBCD4`
freeze (867 inputs); they do not qualify the new source.

Release analysis r264 exits 0, x64, PID 63756, from 2026-10-06 16:20:33.532699 to 16:29:03.228855 UTC.
The `.build` result and archived attempt agree on exit, timestamps, and freeze. This was analysis mode with no
test stages or runtime log. The archive
`D:\RedPrismQualificationArchives\P1-clone-pan-867-ReleaseAnalysis264-2026-10-06` has 867 source inputs,
293 evidence files, and 1,160 independently verified copies; its source/evidence manifest SHA256 values match
the archive receipt. Completion SHA256: `916E8B9BB44086495C3772E2F7D4390F7A8F3A89191F0255D457A9CA695F827`.

Real x64 ASan Debug r265 exits 1 before runtime, PID 33412, from 2026-10-06 16:23:43.349442 to
16:25:52.405020 UTC, on that same old `AD5015…` freeze. Linked instruction scanning rejects
`vsqrtsd xmm0, xmm7, qword ptr [rcx]` at `0x141521870` in `DxUi.Accessibility.obj`; no runtime stages ran.
The archive `D:\RedPrismQualificationArchives\P1-clone-pan-ASan265-scan-failed-2026-10-06` has 867 source
inputs, 280 evidence files, and 1,147 independently verified copies; both manifest hashes match the archive
receipt. Completion SHA256: `F58C461A204856E0B05617953B575BD680FD4C66013FCD0EE823ED2441297970`.

Independent archived PE/PDB/listing analysis establishes that the rejected instruction lies inside the
accessibility switch target table: 8 guard rows, unsigned maximum 76, 8 DWORD targets, and 77 byte indices
were verified. The proof report is
`D:\RedPrismQualificationArchives\P1-clone-pan-ASan265-scan-failed-2026-10-06\independent-interior-switch-byte-proof-r1.json`;
the helper executed was copied into that archive. The supplemental receipt
`independent-proof-supplement-r1.json` verifies helper SHA256
`2691CAE2AD9A09055D437C640A8E2334C811F2D8697BCE97541941D43BE15D34` and report SHA256
`9A86546A164AD1E566A2BBBE4C99C806F0EC7D61BE8CBFDBF5F5CFFD8FA55878`. This explains the scanner
false positive for this archived binary only. It is not a scanner replay, sanitizer runtime, or qualification
pass.

ARM64 r38 cross-builds all exit 0 on the separate `0236021DC5A19194AA4AC527FF628A7980A131A78D2A7337C7E40A5D157E75E4`
freeze (867 inputs). Each `.build` result and independently verified archive receipt agree on exit and freeze;
all three archives were verified under `D:\RedPrismQualificationArchives`:

| Configuration | Actual child / UTC interval | Archive; verified copies; completion SHA256 |
| --- | --- | --- |
| Debug | 16:41:09.992509–16:41:33.085997 | `P1-clone-pan-867-ARM64-Debug38-2026-10-06`; 938; `1AFD78312ED8B254B72695F057F627B017F5200F3589B012254C13C67F8400D9` |
| Release | 16:41:37.737813–16:41:57.907211 | `P1-clone-pan-867-ARM64-Release38-2026-10-06`; 938; `46870054868EA72AD6D69480DE565FB7A769CB675A0E497DDF9F0CDFA9819E34` |
| ASan Debug | 16:42:02.897491–16:42:18.037187 | `P1-clone-pan-867-ARM64-ASanDebug38-2026-10-06`; 942; `58F254EE969DBC0BDFB3F6A1E7F2A814031FFEE781278B39C3C3DE3755715829` |

These are cross-builds only; they provide no native ARM64 runtime evidence.

The new x64 ASan Debug candidate r266 freezes 867 inputs at SHA256
`0236021DC5A19194AA4AC527FF628A7980A131A78D2A7337C7E40A5D157E75E4`, with 119 controls. PID 42908 exits 1
over 2026-10-06 16:35:00.868133–16:50:34.663895 UTC. The runtime suites, real ASan diagnostic, crash probes and
all 92 application stages complete, including the 81 Clone/pan and 15 input-policy controls. The full gate
fails when `empty-shell-96` screenshot exits 9; no screenshot HRESULT was emitted. This is not a full pass.
Its independently verified archive `P1-clone-pan-ASan266-screenshot-failed-2026-10-06` has 1,149 copies and
completion SHA256 `CFF0B4453FAECEF8E7EE9682E8938C6302378A93D2264C847F3498F1EE9D44AF`.

Isolated screenshot reproduction r1 uses the exact unchanged image (SHA256
`C1B42740CD728C86617B836C9A1D044981069F6323068B7125F023BA7F32A7ED`), PID 108996, and exits 0 with a PNG.
The result is under `.build/clone-screenshot-reproduction-r1`. This establishes nonreproduction only;
the original failure has no established root cause. A fresh full ASan run r270 is started alone on the same source.

That full ASan r270 rerun exits 0, PID 4144, over 2026-10-06 16:54:18.307769–17:05:30.441256 UTC, on the exact
867-input `023602…` freeze. The primary-reviewed independent registry r4 preserves all predecessor controls and
verifies 10,158 named application controls / 92 stages, 110 new host checks, 12 core checks and all 119 scanner
controls. The actual ASan probe, 8,055 worker stress controls and concurrent raster stress pass unchanged.
Every harness screenshot passes. Its archive `P1-clone-pan-867-ASan270-2026-10-06` independently verifies
1,152 copies; completion SHA256 `A348BB30508EBC3F3162CA2F6BCB3727373DF762FCFE23BE0B4D945B5B2F77FB`.
This successful rerun does not establish the root cause of r266's original screenshot failure.

Full x64 Debug r267 exits 0, PID 64816, over 2026-10-06 17:07:25.533790–17:16:32.688185 UTC,
on the same 867-input `023602…` freeze. Independent registry r4 compares it with the successful ASan r270
source and preserves every predecessor control, the 92 stages, all 110 new host and 12 new core checks,
and all 119 compact signed scanner controls. Its archive
`P1-clone-pan-867-Debug267-2026-10-06` independently verifies 1,149 copies; completion SHA256
`DA2F4EAC6FDE026B832FC44DA77BD9FBF14FC3DAB91237624A03EF1332B6E87F`.

Release analysis r271 exits 0, PID 10264, over 2026-10-06 17:21:43.696882–17:44:47.556256 UTC on
the same 867-input `023602…` freeze. The unchanged canonical `build.ps1 -Analyze -MaxCpuCount 2` runs
alone with process-scoped `CL_MPCount=2`; two retained compiler response files independently prove `/MP2`
and `/analyze`. The helper `verify-analysis-r1.py` verifies source equality against successful ASan r270,
all five fresh linked-image scans and matching product/PDB/allowlist hashes, and the 119-control scanner
summary. This is analysis evidence, not a runtime run. Its archive
`P1-clone-pan-867-ReleaseAnalysis271-2026-10-06` independently verifies 1,166 copies; completion SHA256
`36D08DD400160355430658E0DBDA6A33B9834BA5F584FD10D76027C5207F4BF6`.

Release analysis r269 on the same new 867-input freeze exits 1, PID 97432, from 2026-10-06
16:43:12.680527 to 16:49:09.953550 UTC. MSBuild reports `MSB4018` with an inner
`System.OutOfMemoryException` while parsing compiler output (`VCToolTask.ParseLine`); this is no successful
analysis result. The compiler first reports `C1060: compiler is out of heap space` in
`ApplicationLayerMetadataSelfTest.cpp`; concurrent runtime/build work is stopped before the next analysis
attempt. The failed archive `P1-clone-pan-ReleaseAnalysis269-heap-failed-2026-10-06` verifies 1,150 copies,
completion SHA256 `D9D9B9ED1303A416AB88948FD1C0025B8417253657401778BB249955FAF00371`.
No runtime was executed. The `.build` `result.json` records the exact
`023602…` freeze and exit code; later matrix evidence may supersede this failed attempt without altering it.

### Earlier 865-input source (partial matrix)

The repaired source freezes 865 inputs at SHA256
`6C8B3AC4ADB319FB8E011092DF84F11E08707B281B8D8FF816795AD9C49C9084`.

Full x64 Debug r258 exits 0 over the actual child interval
2026-10-06 15:23:46.231984–15:32:23.568468 UTC. Independent registry verification preserves every predecessor
control and stage and verifies 10,116 named application controls / 91 stages, 68 new actual-host expectations
and 12 new core expectations. Suite counts: Host 7,657; Document 7,225; Composition 1,694; I/O 1,736.
Its independently verified archive contains 1,143 copies at
`D:\RedPrismQualificationArchives\P1-clone-readback-865-Debug258-2026-10-06`; completion SHA256
`05EA217FE1C2F1AE05C0AED815DAFB19EBE7F3D5B17D0393323B2AF93E6A9674`.

All ARM64 r36 cross-builds exit 0 on the identical source freeze. These establish compilation only;
this machine provides no native ARM64 runtime qualification. Each source/product archive was independently
read back before any products could be replaced:

| Configuration | Actual child interval, UTC | Verified copies | Archive completion SHA256 |
| --- | --- | ---: | --- |
| Debug | 15:31:03.320309–15:32:33.388091 | 936 | `724339A7341327509FEAE50FC0571AAE328241CE7976FB12EB39681EFC0EC4C7` |
| Release | 15:32:37.401846–15:34:34.382788 | 936 | `AC992BC5C898BAC8F4AD82D5E892B545041591061D6AD5B9B6EB6D9C8E439887` |
| ASan Debug | 15:34:39.309535–15:35:56.549926 | 940 | `B03263680101402F3E9F6FF4A303CD4326758DB1039621A208F80F5E06CEA679` |

Archives use `D:\RedPrismQualificationArchives\P1-clone-readback-865-ARM64-{Debug,Release,ASanDebug}36-2026-10-06`.
Full x64 Release r259 exits 0 over 15:36:28.308677–15:43:21.705695 UTC on the same 865-input freeze.
Independent verification preserves every predecessor and the same 10,116 controls / 91 stages. Its archive
`P1-clone-readback-865-Release259-2026-10-06` verifies 1,145 copies; completion SHA256
`5505BC12EE43FA92145B6FB9BDB5FF965BC9862F778B008FA20D2B59C6E73208`.

### ASan r260 linked scan refusal and follow-up

The actual r260 child exits 1 over 15:43:53.868034–15:45:33.644329 UTC before runtime. The instruction checker
rejects a decoded EVEX row at `0x141517829` inside the pinned DxUi accessibility object's switch target table.
The failed source, executable, matching PDB, decoder listing and contribution metadata are preserved in
`P1-clone-readback-ASan260-scan-failed-2026-10-06` (1,145 independently verified copies; completion SHA256
`67BD683AB4A974C337AD209C515F729355AF20CCD6DC6695884212F328013E94`). This is no sanitizer runtime pass.

Independent archived-PE analysis verifies the complete eight-row guard/dispatch, 19 target boundaries,
68 remap indices, maximum 67, and exact owning procedure end. The rejected row lies wholly inside the DWORD
target table. The missing scanner encoding is `CMP [RSP+disp32],imm8; JA; MOVSXD RAX,[RSP+same disp32]`
followed by the RCX byte-remap dispatch. The product repair requires the exact guard, PE/listing agreement,
same contribution/procedure and symbols, valid targets/remap, and reported table bytes. Malformed forms
cannot fall back to an unrelated later data symbol. Neither DxUi source nor its pin changes.

The revised candidate also removes the synchronous Clone host fallback, adds Alt/mask/Shift policy controls,
and separates a new physical Space pan from the sealed original paint FIFO. Its complete new source matrix
is due; prior 865-input receipts qualify their exact earlier source only.
