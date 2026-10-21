# P1 utility ownership integration

## Current checklist

- [x] Integrate the dormant utility owner service, exact current-context borrowing, retained command failure policy and source-capacity wake-up.
- [x] Add independent routing, joined-close, current-context, source-capacity and acquired-prefix fixtures.
- [x] Preserve and independently authenticate Debug r770's successful build and r771's original runtime failure.
- [x] Correct the joined-close fixture's missing deferred-retirement admission option; literal ownership expectations remain unchanged.
- [x] Preserve/authenticate corrected r772 build and r773 runtime failure; joined-close and current-context controls now pass.
- [x] Correct the source-capacity fixture's wait loop to honor scheduler readiness before sleeping; strengthen its worker proof to require a nonzero thread ID.
- [x] Preserve/authenticate r774 build and original r775 runtime failure; fix the held destruction stage to explicitly resume on Compute.
- [x] Integrate post-copy/partial-producer failure retention and eight actual WARP transport fixtures; Debug r777's complete composition passes with exact predecessor preservation.
- [x] Preserve/authenticate full Debug r778's startup expected-count failure before correction; all 150 PathRead assertions pass but its old 144-check guard refuses the result.
- [x] Run/authenticate full Debug r779 on the corrected candidate, including every acquired startup prefix and exact predecessor/control inventories.
- [x] Qualify full x64 Debug r788, Release r789, real ASan r787, analysis r790 and ARM64 r92 cross-builds on the same 2E5E86/1,156-input candidate.
- [ ] Migrate the production utility parent/callers and measure real cache-miss costs.

Component integration only: ordinary activation=false, G1=false, P1 Done=false. DxUi remains pinned to
`8e8994079e82f79e6765e6a885c03fc687957591`, API3.

## Original candidate

1,154 native inputs, freeze
`2020D1A1C28E22920D2F9534E2D33B9BC4DC4324CB8969D9D9B6809F7325BF8B`.

Debug build r770: actual child67024 exits0, 15:30:17.997573–15:36:58.887946 UTC.
Archive `D:\RedPrismQualificationArchives\P1-utility-ownership-1154-Debug770-build-qualified-2026-10-10`.
Completion SHA256 `3F1D38401D3A921B59F314CF6A4944479982E976827F3068AD6DF50EB02B09D9`.

Composition runtime r771: actual child43344 exits3, 15:39:03.178833–15:39:05.438394 UTC.
The last assertion is `PASS utility-joined-close.live-scheduler-cannot-refund-or-close-service`.
Archive `D:\RedPrismQualificationArchives\P1-utility-ownership-1154-Debug771-runtime-failed-2026-10-10`.
Completion SHA256 `E8212BEE17B74675345F3DCAF48D5383C8A5A154B0E2A4202DFFF6B849A6510F`.
Main authenticates all 1,154 source and 372 evidence copies and binds the original actual-child receipts.

## Repair and scope

The joined-close fixture requested retained memory without `JobOptions::deferredRetirement`.
`JobScheduler::RetainMemoryImpl` explicitly requires both admission options for a retirement transfer;
`BindForRetirement` therefore refuses the transfer, and the fixture's fatal guard terminates.
Main adds the missing option. Scheduler authority checks and the literal CPU/GPU charge, worker
retirement, wrong-ticket refusal and no-useful-completion assertions are unchanged.

The service remains dormant in ordinary Application startup. The policy-accounting fixture uses a
literal budget snapshot and does not prove production adapter admission. Empty transport command
fixtures do not prove rendered pixels. Existing region pixel goldens retain their separate role.
The following integrated queue repair retains uncertain producer/copy commands until actual GPU
acknowledgement. Real caller admission, migration and operation lifetime qualification remain open.

## Second candidate and source-capacity wait repair

Corrected Debug r772 build: child37500 exits0, 15:45:37.488356–15:52:05.596881 UTC.
Composition r773: child78544 exits3, 15:54:36.225067–15:54:43.732918 UTC.
Both have 1,154 inputs and freeze
`662104223733949AC72B6CBAA185E4838B81806D0BEB6D1E00F961E21292756B`.
All new joined-close and current-context controls pass. The new source-capacity transition assertion
fails, followed by the held worker's failure bound. That failed result remains an original failure.

Build archive `D:\RedPrismQualificationArchives\P1-utility-ownership-1154-Debug772-build-qualified-2026-10-10`,
completion `BE7F572858B2E541DC8FFEA0812944DDF0B3EAED80B74CB2E362A7076BB5E266`.
Runtime archive `D:\RedPrismQualificationArchives\P1-utility-ownership-1154-Debug773-runtime-failed-2026-10-10`,
completion `A791DC48BF9FA30F78CDA69E711ACBC4374A2C5B3C4683888EC2E0CFE173CBC2`.
Main independently authenticates both archives before further changes.

The fixture's `Until` helper sleeps after one rotated owner pass without checking `HasUiWork`.
The canonical `SettleJobs` loop explicitly continues while ready work remains: one capacity wake
does not promise another event for an already-ready rotated leaf. Main applies that predicate to
the fixture, adds a concrete transition diagnostic and requires a nonzero recorder thread ID;
equal zero record/finish IDs no longer pass the worker assertion. Fresh native proof is due.

## Recorder-lane deadlock and copy retention

r774 build child47260 exits0, 15:58:06.699108–16:04:44.643814 UTC.
r775 Composition child42320 exits3, 16:05:29.439506–16:05:36.937297 UTC.
Both have 1,154 inputs, freeze `9BDA2566334EC96659E782B94D7D32BCD5F40206A64A255BA6EFE9E4B6ECEB17`.
Archives `D:\RedPrismQualificationArchives\P1-utility-ownership-1154-Debug774-build-qualified-2026-10-10`
and `D:\RedPrismQualificationArchives\P1-utility-ownership-1154-Debug775-runtime-failed-2026-10-10` have
completion SHA256 `316E3CED610E217831F474976095F09DD209CDD7429511194BB51FEF55340C6D` and
`74C759139298625DA48922B808E5BBF0C60ACA8C061DEF595BA1C8E63E15D07A`.
Main independently authenticates both before further repair.

The wait-loop repair alone does not fix the fixture. Its `DetachAndRetire` calls the Resume overload
that preserves the previous GPU recording lane. The held destruction stage occupies that sole recorder
while waiting for the third recorder to join. Main explicitly resumes destruction on Compute and
strengthens the final assertion to require its retirement thread to differ from the overlapping third
recorder. The original failures remain failures; no deadline or ownership expectation is relaxed.

Main also integrates the production queue's missing post-copy retention. Before producer/copy commands,
the existing slot holds its original device/context/completion route. Failed signaling or partial producer
failure retains the source, staging, receiver, original ticket and in-flight charge. One real captured
retirement barrier is offered; healthy refusal remains dormant until a device transition or Close.
Actual event delivery returns the original failure without Map/rows/recopy. Joined Close drops callbacks.
The eight independent WARP fixtures include both texture and structured-buffer paths; no production
caller migration, performance result or rendered-pixel golden is inferred from these transport cases.

## Current composition proof and full Debug bookkeeping failure

The 1,156-input source freeze is
`218765BCEF84D1AE7620E6A58FAA91E169ACCB90A4402F0C7B6FC045ADBD02BC`.
Debug r776 build child7584 exits0, 16:09:50.586012–16:16:21.253359 UTC.
Complete Composition r777 child54744 exits0, 16:17:09.168726–16:17:20.483155 UTC.
Main verifies all 195 new ownership/recovery controls and 87 copy-retirement controls against their
source-derived inventories. Removing only these controls and the explicit source-capacity transition
diagnostic preserves all 6,140 predecessor records in their original order.

Build archive `D:\RedPrismQualificationArchives\P1-utility-copy-retention-1156-Debug776-build-qualified-2026-10-10`,
completion `67F3DFBC8FCBB0F5A6401A9C6DEBD5C6AD21FB35F2BB7F4FD1EFFF7C7E5AAA4A`.
Runtime archive `D:\RedPrismQualificationArchives\P1-utility-copy-retention-1156-Debug777-runtime-qualified-2026-10-10`,
completion `2F2890BD46126F9E7931170A05FD8D888881075BB81E54C1BB9B669F63218302`.

Full Debug r778 child46364 exits1, 16:17:58.464228–16:25:30.354582 UTC.
Archive `D:\RedPrismQualificationArchives\P1-utility-copy-retention-1156-Debug778-full-failed-2026-10-10`,
completion `82096A3B3A475A560CA65E99EEB03EA627461B5A63ECFEC2A8AC859511A90DE4`.
Main authenticates all 1,156 source and 381 evidence copies before correction. The PathRead startup
fixture reports `checks=150 expected=144 failures=0`: its two added controls run in each of three
startup cases. Main updates the literal summary to150 and the total guard from178 to184, retaining
all 34 boundary controls. Other extended startup fixtures return their failure count and have no
literal count guard. No assertion, numeric golden, timeout or ownership expectation changes.
Fresh full configuration qualification follows this bookkeeping correction.

## Corrected full Debug qualification

Full Debug r779 child5132 exits0, 16:34:09.505843–16:45:05.392833 UTC, all 124 Application stages.
Its 1,156 inputs have freeze
`E4241DF87B27B7B19CFD1ABB38458C713C5880DF7F5F54D3BC8201A4A6EDA16B`.
Only the PathRead literal summary/count guard differs from the preceding 218765 candidate.
Archive `D:\RedPrismQualificationArchives\P1-utility-copy-retention-1156-Debug779-qualified-2026-10-10`,
completion `D49A7214D68E65F3D550281D4F9E14E48D1E144D87C27447677F4AE4A87324FE`.
Main authenticates 1,156 source and 383 evidence copies and the original unfiltered `test.ps1` arguments.

| Profile | Current records | Preserved predecessor | Added controls |
| --- | ---: | ---: | ---: |
| Host | 8,788 | 8,786 | 2 |
| Document | 8,782 | 8,782 | 0 |
| Composition | 6,423 | 6,140 | 195 ownership/recovery + 87 copy-retirement + one explicit transition diagnostic |
| I/O | 1,825 | 1,825 | 0 |
| Application | 7,393 | 7,275 | 118 |

Every predecessor sequence is preserved exactly. The four startup summaries change only by the
source-derived additions: Responsive217→231, History Residency36→44, PathRead144→150 and
Processing405→495. All 34 PathRead boundary controls remain unchanged. Direct/prepared smoke routes
and the sampler smoke preserve their original operation contracts; smoke timings do not close G1.
Skills15, formatting1,019 files and `git diff --check` pass.

Main's first qualification reader inherited a classifier that omitted the History Residency summary
from both transcripts. The original reader and failure are preserved. Main explicitly includes that
source-defined summary prefix and verifies its old/new literal counts; no native output or assertion
changes. The revised reader SHA256 is
`9EA07F44E3CBA8E6AB3646BA3FD1A075384F3C268A7762EACBA0A3B2345DD8A1`.
Release r780 and real ASan r781 pass on that same corrected candidate. Main authenticates each
complete archive and exact predecessor/control inventories before starting the next configuration.

| Profile | Actual child and UTC interval | Archive completion SHA256 | Main profile SHA256 |
| --- | --- | --- | --- |
| Release r780 | 85416; 16:46:35.665574–16:56:25.718325 | `C568D0CF84F2563D893C8E9E7E8C59C1B65727263A3751E892FDC1BEF805BBAC` | `E286A8795126E6C8CD445EFFD1F1DD257EED7560443D854BE061084A9A7D1E4E` |
| ASan r781 | 51164; 16:59:28.782174–17:18:04.541874 | `0C128A1E461F179633B9B23956AB900D16468D50FA624552A958FFEDA85EDFD4` | `1174B019223D10D17B8D51925DAF970DA9256F5E40ADE7A845A168E306F53DAB` |

Release archive: `D:\RedPrismQualificationArchives\P1-utility-copy-retention-1156-Release780-qualified-2026-10-10`.
ASan archive: `D:\RedPrismQualificationArchives\P1-utility-copy-retention-1156-ASan781-qualified-2026-10-10`.
Each has 1,156 exact source copies and all 124 Application stages. Release preserves predecessor order
exactly. ASan preserves the complete predecessor multiset with only its already pinned callback partial
order canonicalization; all 195 ownership and 87 copy-retirement controls pass unchanged. The actual
ASan-probe diagnostic SHA256 is `2AE3B041E8B6F547D6A7639C20902A79E3153698C024CD14DA32F2964D9F450F`;
concurrent raster stress, direct/prepared smoke contracts and both actual Host executions also pass.
This is functional component qualification, with no new timing or activation claim.
Release analysis r782 fails before a qualified matrix. Actual child3324 exits1,17:23:07.728568–
17:27:49.081454 UTC, on the same E4241D source. Archive
`D:\RedPrismQualificationArchives\P1-utility-copy-retention-1156-analysis782-failed-2026-10-10`,
completion `73B94FE3346900A63D0C783AD88DF8DEA00CDD4EB12CF2C262115E335E0807F8`.
Main authenticates all1,156 sources and365 evidence copies before changing source. Three test literal
capacity comparisons trigger C6326; the int32/HRESULT comparison triggers C6221. Main adds compile-time
literal assertions and volatile runtime observations for the same capacity PASS controls, and an explicit
int32 conversion for the same exact error comparison. No expectation, tolerance or production behavior changes.

The following combined candidate integrates shared recorder startup, checkpoint10/full-route rollback,
independent device/Stop/Close guards and actual first/second display-child attachment refusals. Partly
acquired display controllers are one-shot, while their independent common pump remains usable. The combined source has1,156 inputs and freeze
`7E552212CCD78CE223DDAFC5E8C7FF74AD148AB22C197DAD7FA4312BACE1CD7D`;
binding `EEE24CEB9F0E90904F82842E8BD5EB6C1169E96631B473B30EF7D1D52676CFCB`.
Exactly ten native files differ from E4241D: six startup/fixture files and four analysis-only test repairs.

Release analysis r783 passes: actual child63892 exits0,17:31:56.986863–17:42:25.913110 UTC.
Archive `D:\RedPrismQualificationArchives\P1-utility-common-startup-1156-analysis783-qualified-2026-10-10`,
completion `6B02D191021747D36A8F5659428152F73DCBDF9D5814428BE5395EF6FE100F71`.
Main verifies all1,156 source and379 evidence copies, the actual child/argv/configuration and immutable
manifest bytes. ISA and concurrency controls pass; this is not runtime or timing qualification.

Main reviews the cheaper agent's source inventory and finds its omitted pending-case failed-startup path.
The original120-entry draft is preserved before correction. The reviewed startup delta is121:98 processing
and23 responsive controls, with summaries495→593 and231→254. Combined Application/Host additions vs the
qualified1144 predecessor are241 (239 Application,2 Host). Names, case multiplicities and all three source
hashes are independently checked. Projection and ARM launch-gate parsers each pass ten synthetic controls;
those checks do not qualify native behavior. Full Debug r784 and Release r785 pass and are independently
authenticated on the combined source. Real ASan r786 stops at an instruction-parser jump-table refusal before runtime, and is preserved/authenticated. A narrowly proved parser extension passes the saved binary and all inherited plus63 new controls; fresh full source/profile qualification remains due.
r779/r780/r781 remain evidence for their earlier candidate only.

Full Debug r784: actual child51424 exits0,17:43:34.414532–17:55:36.591478 UTC.
Archive `D:\RedPrismQualificationArchives\P1-utility-common-startup-1156-Debug784-qualified-2026-10-10`,
completion `D4D47CC5E447A8C17E3697956454DF8FE13C97383C765B1925A36FF11A3E07BD`.
Main authenticates1,156 source/385 evidence copies, all124 stages and the exact predecessor/control inventories.
Profile report SHA256 `D7FFA13C8820D0322620F4F8B1D08E0C3558F8FFE35A34D3F19D21FA2C9567E3`.

Full Release r785: actual child58664 exits0,17:57:30.257956–18:07:51.041674 UTC.
Archive `D:\RedPrismQualificationArchives\P1-utility-common-startup-1156-Release785-qualified-2026-10-10`,
completion `2DB3E28F6D1E8AE54454F7270C62967EBB392538EFC15B1FC4BF7B97126A59F7`.
Main authenticates1,156 source/367 evidence copies, all124 stages and the exact predecessor/control inventories.
Profile report SHA256 `C4AD36546CF61529A94A37289546EF80D4933CE11212CE3A8E08973087345658`.

Both full profiles preserve Host8786+2, Document8782, Io1825, Composition6140+195+87+one INFO,
and Application7275+239 records. Recorded processing593/responsive254 summaries replace only their
source-defined predecessor405/217 summaries; PathRead150 and History44 keep their existing projection.
Direct/prepared smoke contracts and real sanitizer obligations remain unchanged.
Ordinary activation=false, G1=false and P1 Done=false.

## Production caller migration checklist

Main reviews the cheaper agent's seven-caller inventory against the actual constructors, hooks,
submission sites and all eleven recorded source hashes. This is source evidence only.

- [x] Identify all seven production consumers and their existing detached boundaries.
- [ ] Register the shared command pump independently of prepared display activation; keep one recording
  lane and add acquired-prefix refusal/rollback proof. Prepared display initialization must reuse it.
- [ ] Implement the original admitted recorded readback parent with one captured immutable source,
  serial regions, mapped-row copy on Compute and stream retirement on Compute. Owner callbacks only
  borrow already recorded pixels, acknowledge loans and resume bounded work.
- [ ] Qualify normal, empty/sparse/masked/clipping/edge pixels against existing numeric goldens;
  include real WARP and available hardware, source-capacity contention and hidden completion.
- [ ] Qualify cancel before opening/recording/command acknowledgement/copy/map/CPU handoff; failures
  retain the original graph until actual acknowledgement and final worker destruction.
- [ ] Qualify actual device replacement, same-pointer new generation, refused fresh budget query and
  the two-restart limit. Replacement cannot acknowledge the old commands or change the original source.
- [ ] Qualify joined Close before first capture, during recording, command quarantine, mapped rows and
  CPU processing. No useful callback survives Close; GPU refunds follow actual graph retirement. The receiver
  and stream live in a separate preallocated root: actual scheduler Close destroys job wrappers before
  the Application GPU cleanup hook. Exact per-source joined detach must precede wrapper destruction,
  while root destruction and refunds follow the later queue/final-worker acknowledgements.
- [ ] Migrate Export first, then Sampler, Clone All Layers, Merge, Copy Merged, Wand Composite and
  Adjustment Opening. Preserve every existing caller-specific authority, failure and pixel control.
- [ ] Qualify each complete CPU/GPU/weak-owner budget and cache-miss owner cost before ordinary activation.

Compatibility requirements:

| Caller | Required behavior |
| --- | --- |
| Export | Capture after complete admission; clear selection on the export snapshot. Begin encoding only after final GPU/map and graph retirement. Never restart an external write after this CPU boundary. |
| Sampler | Keep one original source/ticket/FIFO reservation and the total restart limit across successive 1×1 points. Idle input remains suspended without a timer. Preserve exact point acknowledgement and terminal ordering. |
| Clone All Layers | Preserve clone opening/source authority and successful sample adoption. Transfer one complete accounting owner to the adopted sample; do not transfer the same scheduler reservation twice. |
| Merge | Retain the worker-built subset and history authority; final detached trim/history preparation remains on a worker. |
| Copy Merged | Preserve cropped/selected CPU capture, acknowledged clipboard retry and actual OS publication truth. Device recovery ends before encoding/publication starts. |
| Wand Composite | Preserve the prepared input, current history-restore authority and detached selection/history preparation. |
| Adjustment Opening | Preprocess visibility on a worker while retaining the original input. Reuse that prepared immutable source on restart and preserve the named Begin/panel authority. |

All seven current submissions use the default Compute lane with no retained-slot preclaim and no
deferred-retirement option. Migration must update those options together with each caller's accounting
owner and admission formula. The new parent cannot bind the whole reservation and then invoke Clone's
old `RetainMemory` again. Existing deferred release aggregates also need the original charge through
their last strong/weak owner. These are implementation requirements, not grandfathered qualifications.

## ASan instruction-scan failure, parser correction, and current candidate matrix

This addendum records the original ASan r786 failure and the subsequent narrowly scoped parser correction. It does not replace or reinterpret any preceding result, failure, or control inventory in this TestRun.

ASan r786 instruction-scan failure (preserved): the 1,156-input candidate freeze was `7E552212CCD78CE223DDAFC5E8C7FF74AD148AB22C197DAD7FA4312BACE1CD7D`. The actual child/archive is `D:\RedPrismQualificationArchives\P1-utility-common-startup-1156-ASan786-isa-failed-2026-10-10`; its actual child exited 1 before runtime qualification. The instruction scanner parsed bytes from the pinned DxUi `TraceButtonVariantName` table as instructions and refused the scan. This is a failed original attempt and remains in the evidence history.

Parser correction and new source: Main implements narrowly constrained handling for a separate short default in the instruction parser. Its 63 encoded controls comprise 3 accepted and 60 refused cases; inherited controls pass. The correction does not change the DxUi pin, DxUi source, or the ISA runtime allowlist. The new 1,156-input source freeze is `2E5E865973894DF3BBEA876DF5A2126A4A2B41E2B7F0ACAA1BCD285A15875FC7`, bound by `.build/P1Drafts/UtilityOwnershipQualification-r4-Main/source-binding.json`. Relative to 7E5522, the two changed build/parser inputs are `Build/IsaScan.cs` and `Tests/BuildProcessTests/IsaInlineTableTests.ps1`; native C++ source is unchanged. `Specs/Testing/Testing_Validation.md` records the parser policy. The parser controls prove parser acceptance/refusal behavior only; they do not independently prove native execution or timing.

ASan r787 on the 2E5… source completed with actual child 66288, exit 0, from 2026-10-10 18:26:57.852308 through 18:44:44.686663 UTC. The archive is `D:\RedPrismQualificationArchives\P1-utility-common-startup-1156-ASan787-qualified-2026-10-10`, with completion SHA256 `FC22D7BAAF0F37D9082753E900AFD4E68ED4F2B783520529553DADB97AD8C5E2`; it contains 1,515 evidence copies and all 124 Application stages. The r4 Main profile report at `.build/P1Drafts/UtilityOwnershipQualification-r4-Main/r787-ownership-profile.json` is present and records `qualified_native_profile: true`, authenticated archive and baseline, the exact 2E5… source freeze, and the preserved profile inventories. This qualifies that ASan profile only; it does not close the broader qualification or delivery checklist.

### Current candidate matrix (2026-10-10, qualified component)

All rows refer to the same 1,156-input freeze `2E5E865973894DF3BBEA876DF5A2126A4A2B41E2B7F0ACAA1BCD285A15875FC7`. Source binding `.build/P1Drafts/UtilityOwnershipQualification-r4-Main/source-binding.json` has SHA256 `25DF1969FF4EFA0AAB03D999F21997A1465BBF35CFDFAC0BD85D3627FEE0E10E`. Earlier results remain historical for their own source candidates. Every archive below is under `D:\RedPrismQualificationArchives`.

| Profile | Actual child and UTC interval | Archive and completion SHA256 | Main qualification |
| --- | --- | --- | --- |
| x64 Debug r788 | child92104, exit0; 2026-10-10T18:46:58.939712+00:00 through 2026-10-10T18:59:04.130556+00:00 | `P1-utility-common-startup-1156-Debug788-qualified-2026-10-10`; 387 evidence copies; completion `25BDD5BD1C7BA999A9AC4D2CD337B2956640A6B4152F17D2915D558C6CB587E1` | PASS; Main report `F46F5765B628B3F11F1FBEEEECE8603AD44D216EEA9EC14828B6B4A788351AEC` |
| x64 Release r789 | child11004, exit0; 2026-10-10T19:00:02.391435+00:00 through 2026-10-10T19:08:42.089099+00:00 | `P1-utility-common-startup-1156-Release789-qualified-2026-10-10`; 369 evidence copies; completion `08459D2F253F08C8737A4951937263CB01BF5FF1A88F9488087F59C45A261F12` | PASS; Main report `17BD328339D0EF09470DC5FB2032C1CBE6CD21F71FE184E686051A5DF6229599` |
| x64 ASan Debug r787 | child66288, exit0; 2026-10-10T18:26:57.852308+00:00 through 2026-10-10T18:44:44.686663+00:00 | `P1-utility-common-startup-1156-ASan787-qualified-2026-10-10`; 359 evidence copies; completion `FC22D7BAAF0F37D9082753E900AFD4E68ED4F2B783520529553DADB97AD8C5E2` | PASS; Main report `EDCE9AC78C4A7E4D9934402C91AE7822A64FD6ED2A03E34BCFCC54A0D4F8895F` |
| x64 Release analysis r790 | child42316, exit0; 2026-10-10T19:10:17.701266+00:00 through 2026-10-10T19:21:09.783787+00:00 | `P1-utility-common-startup-1156-analysis790-qualified-2026-10-10`; 383 evidence copies; completion `84EDADC3E31466287E18E835B59B1D00EECAD478016FFC071BDCD2B8738BF2C2` | PASS; Main report `1E55F6234F696D5F9D2ACDB508C54A7E094749F0D2CD4D61E77E1206E47AF665` |
| ARM64 Debug r92 | child48908, exit0; 2026-10-10T19:24:37.603505+00:00 through 2026-10-10T19:25:44.001618+00:00 | `P1-utility-recording-1156-ARM64-Debug-r92-qualified-2026-10-10`; completion `2BB692D66D851D67DDAEE662244349E16DAC67F4E29F9B83C17DFB0B0D4EB4C6` | Cross-build PASS; 5 EXE/PDB pairs, 0 ASan controls; profile report `0392B3D1D3833FA99176B74A66278C1C5DDB711569A0E90B3303B10061C32DEE` |
| ARM64 Release r92 | child58664, exit0; 2026-10-10T19:26:35.591614+00:00 through 2026-10-10T19:28:06.994663+00:00 | `P1-utility-recording-1156-ARM64-Release-r92-qualified-2026-10-10`; completion `FF530895524C426B129BC3B638460FAA612600800293F0EEC905AA65C022779A` | Cross-build PASS; 5 EXE/PDB pairs, 0 ASan controls; profile report `8626BBD063FE892453451D071EA617624B9C95870C374D46218B5A75D8653008` |
| ARM64 ASan Debug r92 | child87440, exit0; 2026-10-10T19:28:50.579041+00:00 through 2026-10-10T19:29:49.121017+00:00 | `P1-utility-recording-1156-ARM64-ASan-r92-qualified-2026-10-10`; completion `5672FAB18FEA2697459FF4992BE843744D4A594587E55C4899E485E7AA67BC87` | Cross-build PASS; 5 EXE/PDB pairs, 5 ASan controls; profile report `BC1EC8AE33C686FD7F07F520D00338D8A73F52EB0568FF3FEE178FDA1840B7C7` |

The three full x64 runtime profiles preserve all 124 Application stages and their original smoke contracts: 239 Application additions (7,514 records), 195 ownership/recovery and 87 copy-retirement controls (6,423 composition records), 8,782 Document and 1,825 I/O records. Host totals are 8,788 for Debug/Release and 8,789 for ASan, preserving their respective predecessors plus two controls. Debug/Release composition predecessor order is exact; ASan retains the pinned callback partial-order policy. Each full profile authenticates all 63 parser controls (3 accepted, 60 refused), their multiplicities and summary; no inherited expectation or allowlist was changed. Release analysis passes its actual analysis child and instruction controls; it is not a runtime or timing result.

Main joins the actual three ARM64 child/archive/source/product receipts in `.build/P1Drafts/ArmUtilityOwnership1156-r92-Main/Main-cross-matrix-joined.json`, SHA256 `FEEC5EC0524E2177B2302FF21DEBC737B3D2E7FB43806426610571C052264B49`. A separate read of PE CodeView GUID/age and actual PDB summaries confirms all 15 archived executable/PDB identities in `Main-pdb-pairs.json`, SHA256 `B0B194C080D672E2D4B215EC03A80266328A50D9202234DE768954E0512AD43D`. All five ASan compiler commands and images prove instrumentation. These are cross-build results; native ARM64 runtime remains unavailable.

Main rejects the cheaper documentation draft's copied Release completion hash, analysis report/completion mislabel and ARM archive/report mislabels. This corrected table is generated from actual archived receipts, not manually copied labels. Its preceding failure history is preserved byte for byte.

## Open status

This closes the component's exact-source profile qualification checklist. Ordinary activation=false, G1=false, P1 Done=false. Nine delivery areas remain open and seven production utility callers have not migrated. Prepared4K-WARP's recorded 2.227 ms Apply result still exceeds the unchanged 2 ms gate. Parent/caller migration, complete admission and joined lifetime, cancellation/recovery/hidden behavior and real cache-miss costs remain required before P1 Done.
