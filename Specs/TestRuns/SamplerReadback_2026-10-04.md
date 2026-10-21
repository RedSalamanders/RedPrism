# P1 sampler repeated-pixel prerequisite — evidence

Status: latest qualified combined snapshot passes full Debug r71: cached history admission, shape-source trim and actual sampler memory-policy controls. ASan Debug r68 and Release r65 qualify their earlier snapshots. Later topology/Escape, current matrix, native activation and original G1 remain open. Hardware timing r1 is refused with display off; all failures and receipts are preserved below.
Worktree: `C:\Users\eric\.codex\worktrees\p1-sampler\RedPrism`, based on `b49e4b4` plus the assertion-preserving
six-kind test fixture isolation from color qualification. Active [plan](../Plans/WIP/SamplerReadback_2026-10-04.md).

## Existing composition runtime, before this source change

Ran the existing `.build/x64/Debug/CompositionTests.exe --warp` before editing `DocumentReadbackTests.cpp`. It exited
0 and printed `All composition tests passed.`; stderr was empty. `test.ps1` invokes `CompositionTests.exe` without
arguments, and the executable ignores `--warp` (its only recognized switch is `--failure-report-self-test`). The
readback GPU fixture creates its own WARP device. Raw stdout, stderr and actual exit code are preserved in
`.build/sampler-runtime-r2/`; [receipt](../../.build/sampler-runtime-r2/original.receipt.md).

At the time of the r2 record, the corrected between-point replacement scenario had not been built. The r3 build and runtime below subsequently exercised it.

## Reviewed implementation and controls

`ContinuePixelReadback` accepts only an acknowledged detached 1×1 request and the exact suspended ticket.
It changes coordinate/phase before resume, retains the immutable source/reservation/restart count and resets
old output on the next worker. Resume refusal restores previous request state. No extra thread, timer, polling,
scheduler reservation or GPU-owner transfer was added. Existing full/region/single-pixel consumers remain.

The cheaper agent added tests only to `DocumentReadbackTests.cpp`. Primary review moved pixel inspection/copy
from an owner capture hook to worker `ProcessReadback`, added the actual thread check and cleared borrowed
message-action pointers on final completion. The controls use independent coordinate bytes for three samples
including both document edges and a nonzero offset, exact original source retention, early/wrong/stale/OOB/
duplicate refusal, one final callback, clean detached boundaries, owner-message continuation and cancellation.
The r2 composition runtime above passes these initial continuation controls. The r3 runtime below later passed the between-point device replacement controls. Complete actual sampler/palette/picker ordering and G1 still require qualification.

Primary review corrected the added rebind fixture so replacement occurs only after the first pixel's detached
acknowledgement, before the second point resumes. It explicitly checks one captured pixel, the exact same ticket,
empty GPU/utility ownership and original source. The initial agent version posted the replacement from first preparation; that would not have established replacement between points. The corrected case passed in r3; see the runtime record below.

## Palette and picker authority source

`InputRevision` is a fixed owner-thread scalar with sticky invalid zero after exhaustion. Foreground publication
binds the session identity; copied/moved owners refuse it. Every explicit foreground/background input, swap/reset,
including equal/A–B–A values, advances authority. Checked sample success renews the caller only, without history
or generation changes. Picker publication binds chrome identity, target and input revision; open/close/detach,
manual/DxUi changes and raw sampled writes invalidate older results. Checked success renews its caller.

Thirteen independent document controls cover repeated publication, unchanged refusal, equal input, A–B–A, background input, double swap, reset, copy/move, history and counter exhaustion. The source contains six actual chrome controls covering own samples, foreign host, manual input/A–B–A, closed card and equal-target reopen. In r3, only own-samples and foreign-host passed before the manual-input check failed with the delayed-sample error below; no manual-supersession runtime pass is established. Current fixes are in an incremental compile attempt and have no runtime result. The production canvas caller, younger tool dependencies and G1 remain open.

## Normal-clean x64 Debug r2 build

The repository `build.ps1 -Platform x64 -Configuration Debug -Rebuild -MaxCpuCount 2` ran on the inherited default
desktop from 2026-10-04T19:09:59.281500+00:00 to 2026-10-04T19:18:42.504869+00:00, actual child PID 94300, exit 0. All five images compile/link;
matching-PDB ISA scans, adversarial scanner controls and exact API-revision-3 DxUi provenance pass.
The recorder verifies 754 unchanged owned inputs; freeze hash `EDE6873B170795BDE8ECB5E406CB449DC558752B6496785B3A8BE3C5CA186D35`, stdout `2AF03CA1B73EA76DA99020D831375B627774337B0A00E8C3643931E0BCDE61D2`, stderr `6B32AF0436708673541577883F2F206E8FC0F4105F813EBD5769F5B3B34ECA37`.

Archive `C:\Users\eric\AppData\Local\RedPrism\QualificationArchives\p1-sampler-pixel-continuation-x64-Debug-r2-build-only` independently verifies 976 copies, source manifest `64D99F80D07D29BB5754C343AA62A83815059CD7B727C521B94F57C6837E32D6`, evidence manifest `2226B5EC2BD8E350F7DE8DE4122B18358110A89873A1D3F624595E96BE28808F`,
completion `6296FE24697C6CFA9596F72698B80EA15917BF890A0406367593D48B69636321`, verified `2026-10-04T19:20:16.3021805+00:00`. No runtime, sanitizer detection or performance pass follows from
this build-only receipt.

The initial recorder r1 never reached a repository child or source freeze because the new worktree had no `.build`
parent. Its original launcher/error logs remain under native `.build/sampler-recorders/r1`. The separate recorder
was repaired to create parent directories; r2 is a fresh attempt, not reuse or a hidden retry. It does not alter
the main color recorder or frozen inputs.

## Normal-clean x64 Debug r3 build and runtime

The normal-clean `build.ps1 -Platform x64 -Configuration Debug -Rebuild -MaxCpuCount 2` run completed 2026-10-04 19:43:08 UTC with actual exit 0. It captured 755 inputs (freeze hash `410C60A42EFE9ECF5CB2B86B7668F5EC23B9EE2B74B1AC262968C94A33BD31A4`); stdout hash is `074062287F94F7149F1812FFF7F5DEC996C4EF016ADFE700A8B1BD1F34DCB66C`, stderr is empty. The independent build-only archive `p1-sampler-authority-device-x64-Debug-r3-build-only` verified 977 copies; source manifest hash `C188AA03B9B0FD0DC32783CE35407B99B3E7A3C55362B947AC3AD9EB20717E36`, evidence manifest hash `D79E3A6E489B8B7C327CA330784707F893BFCF05C3662CDE0481FAFF46692CE7`, completion hash `5F2C4272BB957E5F9ED626DAB557E4F56C7ADC282B2A95035B57EED37D89495D`. This is build evidence only.

The actual r3 runtime receipts in `.build/sampler-runtime-r3/` show:

- `CompositionTests`: exit 0; all composition tests passed. The repeated-pixel between-point rebind controls passed, checking literal pixels from the retained original snapshot after caller mutation, the same ticket/reservation on the replacement generation, next-worker prior-pixel retirement and empty mapping/utility ownership.
- `DocumentTests`: exit 1 on `palette-authority.sampling-and-input-authority-remain-outside-document-history`. The assertion incorrectly expected `UndoCount() == 0`; `CreateDocument` contributes a New Canvas history entry. Current source records the original undo count and checks it remains unchanged. This fix was not built in r3.
- `RedPrism`: exit 5 from the self-test failure `A delayed sample overwrote a newer picker input.` This was not a crash. The r3 run passed `sampler-authority.picker.own-samples-renew-exact-input-stamp` and `sampler-authority.picker.foreign-host-refused-without-color-change`, then failed the manual-input supersession check. It exposed a real chosen/reference swatch adapter bug: the old setter/getter path used DxUi's opening/cancellation swatch. Current source publishes through `SampleColor` (public/input revision and the normal preview callback); `ChromeHost::PickerColor` now reads the chosen value through DxUi `GetColor`, while the opening value remains available for Cancel. The fixes are in the incremental compile attempt; no runtime pass for them is established.

The nine original runtime records are retained and hash-verified at `C:\Users\eric\AppData\Local\RedPrism\QualificationArchives\p1-sampler-authority-device-x64-Debug-r3-runtime-original\verified.json`. The executable images are preserved in the separate r3 build archive; the runtime result JSON files record the executable hashes. Preserve these failed records as evidence.

## Incremental x64 Debug r4 status

The incremental build at `.build/incremental-x64-build-Debug-r4/` started at 2026-10-04 20:02:53 UTC from 755 frozen inputs and completed at 20:07:31 UTC with actual exit 0. `result.json` records freeze hash `863DD561014CD4AFF9E73A1CD99712673E21A61EEDC4D7D704F712B3BB9FBB88`; stdout hash `BCD64E2EE0740D14DF79EE2012A7127AA340DE669EB2B3001603852E0BE46ADB`; stderr is empty. The independent archive `C:\Users\eric\AppData\Local\RedPrism\QualificationArchives\p1-sampler-chosen-color-x64-Debug-r4-incremental-build` verifies 977 copies, source manifest `D0AEB27777DD77C9F4EEB76E8373422A091B390F3EE6C96B5CC726C08434DBCC`, evidence manifest `FA9454E682D7F797119D98EBBB5061ED71122866ED7BD0E3DA268ACC046FE2CA`, completion `43E37994624C928E68BD99C5965DC4C0289B12722561B24E0A79E7317C6F277C`. Build-only; no runtime or G1 claim.

## Focused x64 Debug r5 DocumentTests

`.build/incremental-x64-runtime-Debug-r5/result.json` records the actual `DocumentTests.exe` exit 0 at 20:09:28 UTC against the same 755-input freeze (`863DD561014CD4AFF9E73A1CD99712673E21A61EEDC4D7D704F712B3BB9FBB88`). The focused suite contained 6,504 controls: 6,491 preceding plus 13 Document sampler-authority checks. Verified archive: `C:\Users\eric\AppData\Local\RedPrism\QualificationArchives\p1-sampler-authority-x64-Debug-r5-document-qualified`; source manifest `21D29EBD0B24A6BC3727E226DCD2F878552AE0D15F0914041476840661DACEA1`, evidence manifest `D984D1292748D1D1508CD0A5E2EF26F952BF001BC336FB664D6812C98821EC98`, completion `9DC7454A0425D039E19C5CFEC9C9E828D303F3241583DD4D730DDD0032E0DB38`. This is scoped DocumentTests evidence, not a qualification of current host picker controls, manual routing, FIFO or production sampler.

## Focused x64 Debug r6 Application

`.build/incremental-x64-runtime-Debug-r6/result.json` records the actual `RedPrism.exe` exit 0 at 20:13:34 UTC against the same 755-input freeze. The verified sampler registry `.build/incremental-x64-runtime-Debug-r6/verified-sampler-registry.json` reports 8,986 controls = 8,980 preceding + six older sampler picker controls, with the original registry identical. The archived application run completed 78 stages. Verified archive: `C:\Users\eric\AppData\Local\RedPrism\QualificationArchives\p1-sampler-chosen-color-x64-Debug-r6-application-qualified`; source manifest `6F9E8A7E6E9C05B24010E6517577477A79310643866DDC8CF8B5B6CF15FCAEE9`, evidence manifest `E6A72EF3BFEF5522F9A98915930525D7798F5E8CDD40AA05131A8836CC78309E`, completion `6A1971B2BFC3271DE5BFDE6E8ADEED63E60D9C969E388722D03A57755AA4BC03`. The six older picker controls do not qualify the current eight controls or manual routing/FIFO.

## Failed r7 analysis and scoped fixture proof

`.build/incremental-x64-analysis-Debug-r7/result.json` records analysis exit 1. The diagnostics in `stdout.log` identify C6326 potential constant-versus-constant comparisons at `Tests/DocumentTests/DocumentTests.cpp` lines 5439 and 5510; warnings-as-errors produce C2220. Preserve the failed run and its verified archive `C:\Users\eric\AppData\Local\RedPrism\QualificationArchives\p1-sampler-x64-Debug-r7-analysis-constant-switch-failure`, source manifest `CD04E2FEC65A93A6ABC598845F8C5C7F0EDBABAE1648680EDFFB908E7C5E08C0`, evidence manifest `26DCC27D6ED09E7A3D9228CE7F4F54EDC9BCF9BDD9871BF2F9C7A2FACF533003`, completion `D138313226D05AD9A00C8D58F5F36CC918A19EF502627A35F9E9263D8265ED42`.

The current `.build/adjustment-constexpr-isolation-proof.json` records 996 lexer-recognized `Expect` call expressions and `ExactMultisetIdentical: true` between the r7 frozen fixture and current fixture. Its scope is the switch-to-`if constexpr` test-fixture repair, with no production, expectation, scanner or suppression changes. It is not a successful analysis receipt or a completed gate and does not establish 1,087 expressions.

## r8 analysis — qualified, analysis only

`.build/incremental-x64-analysis-Debug-r8/result.json` records x64 Debug analysis exit 0 from 2026-10-04 20:42:23.612791 UTC to 20:50:34.380260 UTC over 758 inputs. Freeze hash `50D0F59348CAEF45379B1FEB6E32B6A47D50C6835F749274D325EC2882618C26`; stdout hash `172F975362B3068B61561011D2AE9E4A3BBEBD9D39662C4DF67F8D9366437022`; stderr empty. The independently verified archive `C:\Users\eric\AppData\Local\RedPrism\QualificationArchives\p1-sampler-x64-Debug-r8-analysis-qualified` contains 1,005 copies; source manifest `EF167CA7AECDFB0CB62770DCDCED2F9A763BB31C57FCC59F240E90D38892DD9D`, evidence manifest `11B85C429DAAF9AEB76199D07F99F21C2CE8A6EEC184170F42AB29F4686CDC9B`, completion `D9052405E5BD77C6D0661CC8E6C48F2F98B1A0C85D98E3F7EF66AAC19440DE7B`. This is an analysis-only pass on the pre-integration snapshot; it does not qualify the combined source, runtime behavior or full validation gates.

## Clipboard-fix integration and full Debug r9

After r8 archive verification, seven clipboard-fix code/contract files from main r51 were integrated, covering `ClipboardTransfer.*`, `ApplicationClipboardJobs`/delayed tests, `IoClipboardTests`, `Core_PerformanceAndResources` and `Testing_Validation`, plus a new `Document_Tools` scope block. The clipboard fix has a separate main-branch full ASan r51 pass; it is not qualification of the combined sampler snapshot, and no analysis result for the combined snapshot is claimed.

The full Debug r9 recorder is `.build/incremental-x64-test-Debug-r9/`. Its `started.json` records a 758-input x64 Debug `test.ps1` run beginning at 2026-10-04 20:52:16.672409 UTC; the recorder was started at 20:52:12 UTC with PID 103568. Its actual failure and source repair are recorded below.

## r9 actual failure and sequence-fixture repair

r9 ended at `2026-10-04T20:59:06.834291Z`, actual exit 1, 758 unchanged inputs, freeze hash
`4452113F1BF272675C7703E1798BF81CF16787FA1BF5396997B8390F6EA1E9AA`. HostTests failed only
`sampler-input.up-while-active-selects-prefix-point` and
`sampler-input.sealed-accepted-prefix-completes-without-release-point`. Cancellation had consumed sequences
1–3, so the next opening's literal expectations must be 4/5, not 3/4. The source repair changes those two new
fixture literals and adds a comment; production logic and original controls remain unchanged. Later suites and
the application did not run in this attempt (zero application stages).

The initial archive helper stopped because no screenshot folder existed after the early failure; preserve that
partial archive. A separate helper guards the missing folder and independently verifies 980 copies in
`p1-sampler-x64-Debug-r9-new-sequence-fixture-failure-complete`. Completion SHA-256:
`4ABAC468289E43731D496158AB2DDA811CE34502450320DA4B101D4A98260654`; source manifest:
`88861FAB51B710164DBD21F253C56B741405E630E8C324CB50B36ED9722C0E01`; evidence manifest:
`56CF82EE6AC1D98C1346B553A73FD34128CB391D69080DEE172FF16FA440DDC5`.

## Full Debug r10 result

Fresh full Debug r10 ran from `2026-10-04T21:01:43.384704Z` to `2026-10-04T21:09:38.134690Z`, actual child
PID 115172, exit 0. All 758 frozen inputs remained unchanged; freeze SHA-256
`7EDA6C1C327ACD0ACBB72F29564596D11CFB22BC9A4BA4E4325A4049C7353B88`. The full repository suite passed,
including all 2,653 sampler FIFO checks, repeated-pixel composition controls, 146 clipboard-transfer controls,
the actual application, crash probes, command-line gates and application-harness screenshots.

The exact preceding registries are preserved: 6,504 document controls = 6,491 preceding + 13 palette controls;
8,988 application controls = 8,980 preceding + eight picker controls; all 78 original ordered stages match.
See [registry verification](../../.build/incremental-x64-test-Debug-r10/verified-sampler-registries.json).
Independent archive `p1-sampler-x64-Debug-r10-full-qualified` verifies 1,025 copies, completion SHA-256
`64819CFFFC0925A5C9341B25E7EFCB770D79B58965938DE29DBE8C80AE899266`; source manifest
`7786E7666CFB8C1CFEEDC82EBA3282608C1801D066AD97EA8F684C1176EDDCD9`; evidence manifest
`8B0B251597334E9A35CC3CFF09402CCC210B8C78859E5A1443816DD9A2A16836`.
This full Debug prerequisite result does not establish Release/ASan, current combined analysis/ARM64,
actual asynchronous sampler callers, native activation, owner-latency targets or G1.

Full Debug r10 qualifies its eight picker controls, manual routing and bounded accepted-coordinate FIFO. Production sampling still uses synchronous `WandComposite`; younger palette-dependent tools remain unintegrated. No G1 or end-to-end sampler result is established.


## Explicit intent sidecar — r53/r54 builds and full Debug r55

Primary reviewed the separate palette/picker manual counters, sampled-preview scope and intent-sensitive fixed
FIFO. A cheap-agent fixture compared foreign/wrong-target mutated stamps with an unmutated reference; primary
repaired only that new assertion to compare each candidate with its own pre-call snapshot. Existing controls,
legacy numeric expectations, scanner settings and stack limits are unchanged.

Build r53: actual child 47336, 22:34:10.883880Z–22:40:05.922416Z, exit 0, 766 owned inputs. Build r54 adds
read-only readiness diagnostics to the existing host-ordering failure seam; its original condition is unchanged.
Child 118032, 22:43:46.876765Z–22:48:33.050954Z, exit 0. All five images, matching-PDB ISA controls and pinned
DxUi provenance pass. Build-only archives verify 1,024 copies each; completion hashes respectively
`0601902D6961F9CDC4197CB18725BCF3EA51CB404AF2430B759E54448F34DBEB` and
`C5EBDF017A95C8FF3ADCE71BD29DD9DFA190BBE68C0C4B17FBE90C48570B73CF`.

Full Debug r55: child 43536, 22:55:06.004619Z–23:04:38.953218Z, actual exit **0**. All 766 inputs are unchanged,
freeze `3AE094D37EC8DBC0B1DA5F84E966B467E320DB0CB6FF4F804E31111BB242A473`.
Stdout `85A8CF1B2512AA51DE27606561798AFDF6517F8ECCD933AB0B20136B9920C0CC`,
stderr `C6C4848C9A0D1E835ACED6892A5C8593C78B4448518E9344A6F911D15DAB2AAC`.
Complete suites, all five images, ISA/PDB/scanner controls, WARP app, crash/CLI/build/RAW corpus and harness
screenshots pass. Exact archived literal registries preserve all preceding controls: **6,571 document**
(6,555 + 16 palette intent), **9,122 application** (9,099 + 23 picker intent), same ordered **79 stages**.
New intent FIFO: **1,757** checks, zero failures. Original FIFO 2,653 and clipboard transfer 146 remain passing.
Registry proof SHA-256 `4B9A253FDC5EB3C627B5950622CB8DD5AE255A3E00A31E6E417E1AB0B82B4672`.

Independent archive `p1-sampler-intent-x64-Debug-r55-full-qualified` under the local qualification archive root:
all 1,033 copies verify at 23:06:30.9103708Z; completion
`181609C28CBA6A9E599008D911CD44EF7179026167D584377A01A2CDCF340094`, source manifest
`B4937F338042473C76300F0639B58CE900762714A9F074C089B19ED1EC7C06E8`, evidence manifest
`D27639ACB04F58E037A261956E8B93175E6B973AF63E823CA97ADB3A6A31D1A8`.

The separate wand Debug r17 failure does not reproduce in r55; its cause remains unassigned. Read-only
`UiReadinessForTest` distinguishes source readiness from memory/owner/completion notifications if it recurs.
This pass qualifies the intent prerequisites on the frozen snapshot, not the newer caller checkout's source
and opening APIs, admitted sampler graph, physical callers, younger consumers, final matrix or G1.


## Admitted sampler graph — r56 failure, r57 full Debug

r56 failed to compile the new self-test: a named lambda was passed to `wil::scope_exit`; the WIL contract
requires a directly constructed scope lambda. Primary repaired this new fixture only. Actual child 85044,
23:23:01.443349Z–23:24:11.247766Z, exit 1; 770 frozen inputs, freeze
`BD830A1C981F940C06CDF7D929EE1A12EFDEA2F521516C13355D747D16DC2836`.
Independent archive `p1-sampler-graph-x64-Debug-r56-build-failure`: 1,028 copies, zero runtime stages, completion
`9F99737FEF5C96680D9CB4CA2A80AA908D46F8EEEA32EEF472732B306D9B5553`.

Full Debug r57 ran child 43124, 2026-10-04T23:27:36.566161Z–23:36:23.126801Z, actual exit 0.
770 inputs match freeze `F9B7A1059AE065052792550C322032633BCAB187BD555EBD32E6C0260EB321BC`.
Stdout `2ABF7AA13FB5AB340A8BDA09A82CE62F4902D153269F2C950E0F650F2D2F47D5`,
stderr `7FFC5661AE790C8FF17F4D9FD29DA3D246284CE82156E718E63AD010EBCB8EF5`.
All full runtime suites, build, ISA/PDB/scanner controls, WARP application, crash/CLI fixtures and screenshots pass.
The formatting check failed before this run on a copied self-test; this result does not qualify that gate.

Independent literal-registry comparison verifies **6,588 document** (6,571 preceding + 17 document-read),
**9,147 application** (9,122 preceding + eight picker-opening + 17 graph), and unchanged **79 stages**.
Proof `verified-graph-registries.json` SHA-256 `D47D29A842EF0C9FAE975D2854CA8A0417A8B136162657F47510B34B717CAE8A`.
This corrects a preliminary count of 19 document-read checks; the archived literal registry contains 17.
Archive `p1-sampler-graph-x64-Debug-r57-full-qualified` verifies 1,037 copies at 23:38:44.1536587Z; completion
`B920DA8D0C023A8197DA5FB81CFBBA4CDDDA30A70BEE837689D011DD9C4091D6`, source manifest
`D20FA620BB19B30B66513965FA7253C7142CD4D36DA79105ACEDD24108975733`, evidence manifest
`F5E1883F89D8371AA07CDD49889DB47356E6B5142F4475F4955BCB16480E4CC5`.

Physical sampling was still synchronous in r57. Follow-up primary review fixed the selected sampler's terminal
cancel/failure outcome being inferred as success from an exhausted FIFO. The newer r58 candidate adds exact
selected/live cancellation controls, physical Eyedropper/Alt routing, accepted-color dependencies for Brush,
Gradient and Shape, and captured shape-preview color. A cheap agent wrote independent actual-pointer caller
controls; primary reviewed and registered them. Original numeric expectations and ordering assertions remain.

Primary also found a scheduler readiness publication gap in current code: a final release/trim count reached
zero before its memory notification. The candidate publishes readiness before decrement and wakes after it;
idle includes undelivered owner readiness. A deterministic NotifyMemoryChanged/Settle regression is added.
This is a source-backed race correction, not a confirmed diagnosis of the earlier wand r17 failure.

r58 skills validate all 15 skills; formatting/check pass 651 files. Full Debug started 2026-10-05T00:02:33Z,
result pending. No r58 runtime, final matrix, native activation or G1 claim is established yet.


## Physical caller fixture compile repairs — r58/r59

r58: new caller lambda omitted its pixel and scalar-slot helper captures (C3493/C2326). Actual child 31140, 2026-10-05T00:02:40.203282+00:00–2026-10-05T00:03:52.020481+00:00, exit 1; 771 inputs, freeze `3EFE909D449415602AA5ECAAFBE5C17665298EF6E2F6B664E8B82299CFC336E5`. Primary repaired only the new fixture. Archive `p1-sampler-callers-x64-Debug-r58-build-failure` verifies 1,038 copies / zero runtime stages; completion `B9A2B335AA1A56C3FB4F3A164AAEA8A5AD75EB2913648B78CDD0B7E271F5F9C2`.

r59: new fixture vector fill forwarded unsigned int to uint8_t under warnings-as-errors (C2220/C4244). Actual child 108292, 2026-10-05T00:06:38.722933+00:00–2026-10-05T00:06:59.089853+00:00, exit 1; 771 inputs, freeze `619672692C72C3A1E9AA61D8381BEE413AB96ACC0B3D53458237E23AFFC6E24D`. Primary repaired only the new fixture. Archive `p1-sampler-callers-x64-Debug-r59-byte-construction-failure` verifies 1,038 copies / zero runtime stages; completion `32E12FFCCE6BCBF0AA6DE05572678B426BADD454D57037DDDD9F90AE51C1B9BC`.

r60 started full Debug at 2026-10-05T00:07:47Z after the exact byte constructor repair; result pending. The caller-root follow-up additionally preserves the accepted opaque color across ordered sampler openings of unchanged manual intent; transparent later openings cannot revert a younger brush to an unpublished baseline. This newer fix and its independent controls are not in r60 and remain unqualified.


## Actual sampler callers — full Debug r60

Full Debug r60: child 50388, 2026-10-05T00:07:52.545831Z–00:17:29.545837Z, exit **0**.
771 inputs match freeze `9BD23DD13F9C30CEE000C483A167D4F86DE2E18DB64E39991E3A17738FFBF6CA`.
Stdout `6DE4A593ECDB1C55BE479DA9B5C9337BED29F5D2F0D37F23B4DD6FB8A824D816`, stderr
`6934544EBD7758169735434A50F4FAFB9B2E006152665F522FCAE30ED06F187E`.
All suites, five images, unchanged ISA/PDB/scanner controls, WARP host, crash/CLI/build/RAW fixtures and harness
screenshots pass. Skills validate 15 skills and format/check pass 651 files before the freeze.

Exact archived literal registry comparison preserves all r57 controls and 79 ordered stages:
**6,604 document** (6,588 + 16 captured-tool), **9,178 application** (9,147 + five cancellation + 26 physical caller).
Proof `verified-caller-registries.json` SHA-256 `53416A4E4CD66A22E08FDB837F8BBE626CD8395A2B4249247BF21F389C41732C`.
Original 2,653 sampler FIFO and 146 clipboard controls remain passing. Actual physical caller controls verify
blue Down/brown Move/transparent tail/Up without a release sample, queued tool selection before sample ack,
manual magenta supersession, exact brown Brush/Shape pixels, captured Gradient settings/endpoints and one entry.
Selected and acknowledged idle cancellation retain the original ticket through exact cancelled acknowledgement.

Independent archive `p1-sampler-callers-x64-Debug-r60-full-qualified` verifies 1,038 copies / 79 stages at
00:18:17.9420134Z; completion `C635050EC66DAD182D8BC23594DD078246DA9387E33E5D25B8DFD2564FA82D40`,
source manifest `4213EFB7C9E1EC62C9C084F0C8DE4A201D5E2F4C3AA8A8D7ACF77658DD2F6FF4`, evidence manifest
`074C14EB3D43B01EA6F92B1E46470FED11D0869140BFB7FFAC68E7CD6AFDAF4F`.

The next candidate includes eight cross-opening opaque-prefix controls and six actual Alt-tool controls;
they are absent from r60. Review also refines release/trim readiness publication: a guarded publication counter
keeps idle false until readiness is published after the completed count decrement, preserving recycle-cap
restoration while issuing one wake. The first flag-before-count version in r60 could consume the notification
before trim completion; the next version requires its own qualification. Release/ASan/analysis/ARM64, native
activation, broader live contexts, largest-fixture admission, H3/H4 and G1 remain open. This is a WIP checkpoint.


## Cross-opening prefix, actual Alt and cleanup readiness - full Debug r61

Full Debug r61: child 83976, 2026-10-05T00:22:21.362724Z-00:32:20.405969Z, actual exit **0**.
771 unchanged inputs match freeze `200A0071422868F02D7DD1B5FADA5156202CFCBACF13CCE8C14B4A3247EA96E8`.
Stdout `5835FA702FA02983831BE818179894CC8331ACBB0B584EA3127BF55A2189AE9C`, stderr
`7774B7E409D65346BD7A9117BECD6801CBB2C4569972AB4D80A12FA931C7BB18`.
All full suites, ISA/PDB/scanner controls, WARP application, crash/CLI/build/RAW fixtures and harness screenshots
pass. Skills validate 15 skills and format/check pass 651 files before the freeze.

Independent literal comparison preserves all **6,604 document** controls, all preceding application controls and
**79 ordered stages**. The application registry now has **9,192** controls: eight cross-opening prefix and six
physical Alt Brush/Heal/Gradient controls added to r60's 9,178. Proof `verified-prefix-alt-registries.json` SHA-256
`92B95A753A178953ADF36B7396093F46E01E805377B2A0DF018B54C60E4CC4D7`.

The prefix cache contains only owner/session, document serial, manual intent and ARGB. An ordered transparent
opening of unchanged intent inherits the preceding accepted opaque color; equal manual input severs inheritance.
Actual Alt callers end their scalar prefix before pumping and publish the literal brown result without opening
brush/gradient/shape work or changing document revision, undo count or generation.

Release/trim completion now enters a publication guard, decrements its completed count, publishes readiness,
leaves the guard and wakes once. Idle includes this guard and undelivered readiness. This fixes both the original
late-notification idle gap and the intermediate version's early notification consumption before recycle-cap
restoration. The change is not a confirmed diagnosis of the wand r17 ordering failure.

Independent archive `p1-sampler-prefix-alt-x64-Debug-r61-full-qualified` verifies **1,038 copies / 79 stages** at
2026-10-05T00:33:46.0757913Z; completion `98F92B6F9A09DA25367AA96C709B264E3A8738BC74DCA2821C56CBAFA75A86D8`,
source manifest `E39F625FB31200D5400FE47E3059E08D7845134A255453373C06ACA317DCDC47`, evidence manifest
`2B61F4E72B25DCC6979D1E5197BDCD17A41CAF2656014FCC04929BAEDE022E25`.

Newer physical leave/capture-loss/Cancel Mode/Escape and partial-alpha fixtures are absent from r61.
Release/ASan/current analysis/ARM64, native activation, broader picker/lifetime/admission cases, H3/H4 and G1 remain
open. This qualifies its exact Debug snapshot only and remains a WIP checkpoint.


## Physical terminals and partial alpha - r62 pending

The reviewed source adds real captured/captureless pointer leave, WM_CAPTURECHANGED, WM_CANCELMODE and Escape
controls. A capture-held leave accepts another Move; other loss paths seal accepted points, while Escape cancels
without late palette publication. Tests require exact receipt, original source/history and complete retirement.
Retiring or sealing a matching physical sampler drag now releases this HWND's capture under the existing
reentrancy guard. No synthetic Up sample, rollback or replacement gesture is introduced.

Four physical numeric controls cover BGRA 31/63/127/128 -> 0xFFFD7E3E and above-alpha clamp -> 0xFFFFFFFF.
They release over an opaque blue pixel to prove Up cannot replace the accepted Down result.
Skills validate 15 skills and format/check pass 651 files. Full Release r62 began at
2026-10-05T00:43:54.220693Z (recorder 95964); actual result, source freeze and archive are pending.
This source is frozen through completion. No Release runtime, current matrix or G1 claim is established yet.


## Physical terminals and alpha - full Release r62

Actual child 29472, 2026-10-05T00:43:59.435227Z-00:50:37.524131Z, exit **0**; 771 unchanged inputs,
freeze `CB8CC88E46FF41A3A58DF9F9801BFBE8868C36FCF08E4C52578530F116CAF3A4`.
Stdout `1F4D483ECE49E5B95ED42F4616C8BF6DF96C7D3E68D5B36631C070A55C5A02C1`, stderr
`1B33E0DBF238AA9B9AB197009995026A2062FF311AAE0DC43B3BF40D7B846B27`.
All full runtime suites, unchanged instruction/PDB/scanner controls, WARP application, crash probes, CLI/build/RAW
fixtures and screenshots pass. Skills/format passed before the freeze.

The independent archived stdout-plus-stderr comparison preserves literal control multiplicities and all
**79 ordered stages**: **6,604 document**, **9,207 application** (9,192 + 11 terminal + four alpha).
The initial comparison helper incorrectly assumed every application control was unique and only on stdout;
that verification assertion failed without changing source or rerunning runtime. The corrected helper reads
both archived streams and compares counters, as the existing qualification convention requires.
Proof `verified-terminal-alpha-registries.json` SHA-256
`33C15C3E026A0B2544210513270008D6E2AED5E276AEFE05DAA0DDBC95167F86`.

Archive `p1-sampler-terminals-alpha-x64-Release-r62-full-qualified` independently verifies **1,038 copies / 79 stages**
at 2026-10-05T00:51:42.1303922Z. Completion `282FFDD1F7B15427D0D84BC5987C684DF9362AB4D6E8DB9D900793C563B69F7E`,
source manifest `A4B45232441AFE3E8C2BD956A3822AB5C283EB3BDF756654D89291DEA700806C`, evidence manifest
`4E3231AF20910F883F400BC90E560D2C6B5B737FC61BA1715AF6173EE371FAD2`.

The capture-held leave stays live; captureless leave, capture change and Cancel Mode seal exactly their accepted
prefix and resolve brown. Escape returns Cancelled and releases capture without late publication. Alpha controls
retain the independent literal expectations and prove the opaque release location is never sampled. Source,
history and generation stay exact and all sampler resources retire.

This is Release correctness qualification, not a latency measurement. Current combined Debug/ASan/analysis/ARM64,
actual picker target/lifetime/admission breadth, native activation, H3/H4 and original G1 remain open.


## Actual picker targets and sampler benchmark - r63 pending

The cheaper agent adds 16 actual foreground/background picker controls: successful preview, equal manual input,
A-B-A and same-color reopen. Primary reviews receipt/binding capture, chosen-color/cancel behavior and unchanged
palette/history/source. Primary adds three Canvas Size custom-extension controls, six destructive Gradient Map
endpoint controls and six live adjustment endpoint controls. The live cases wait on an owner-only acknowledgement
event, require generation renewal after their own Preview, then send a real Move. Original identity-map blue and
brown luminances are independently fixed at bytes 99 and 75. The second sample must read that admitted original
root; using the changed adjustment produces a different byte and fails.

A new `sampler-accepted-prefix` real-loop row drives actual Down/Move/end producers at exact document coordinates
(0.5,0.5), (1.5,0.5), (0.5,0.5), without client-coordinate rounding at Fit zoom. It validates original history/image,
receipt, three GPU copies, released reservation/utility and literal gray 55 on A (masked composite gray 50 plus
linear-sRGB exposure 0.25), or gray 29 on ceiling B. Timing covers posted owner dispatch, capture, readback,
publication and the final invalidated frame. The smoke contract retains all 26 prior operations and adds one;
its counter requires six point copies across two trials. No timing thresholds are placed in tests.

Skills validate 15 skills and format/check pass 651 files. Full Debug r63 began at
2026-10-05T01:01:03.824631Z, recorder 85192. Actual result and archive are pending. Source stays frozen.
Newer caller-root opening/FIFO capacity and submission-refusal controls are not in r63. No new matrix/G1 claim.


## Actual picker targets and sampler smoke - full Debug r63

Actual child 19744, 2026-10-05T01:01:08.467024Z-01:09:02.685416Z, exit **0**; 771 unchanged inputs,
freeze `A667299643C63B91C6A5DDD4680D1EB1B0CCBC49717606B5FEBCE5D5FBD2C6ED`.
Stdout `5379DA4C5E6F3EC344D08B3693C85175C028DAF7FCD0A9C92631DEDEAFC362EC`, stderr
`75B25DD34B2017262D670B82A2B1B5F1AF16C43252DB45AF941629B2D8D5A28C`.
All full suites, unchanged instruction/PDB/scanner controls, WARP host, crash probes, CLI/build/RAW fixtures,
27-row UI smoke and screenshots pass. Skills/format pass before the freeze.

Independent archived literal counters preserve **6,604 document / 9,238 application** controls (9,207 + 31 picker),
all **79 stages** and the exact preceding 26 benchmark rows plus `sampler-accepted-prefix`.
Proof `verified-picker-benchmark-registries.json` SHA-256
`896756B95E1C83CD5BB013E2689006F90A2718FD3EBD22C4310A30406496776A`.
The new smoke row reports six copies/maps, zero busy failures and two presentations. Its Debug 64x48 durations
are correctness diagnostics only and cannot qualify Release A/B or G1.

Archive `p1-sampler-picker-benchmark-x64-Debug-r63-full-qualified` verifies **1,038 copies / 79 stages** at
2026-10-05T01:09:55.9573878Z; completion `0A7620477E56653AEFFB1882DF6FC3C98211FB09E0D525C6BAB3AFFD7CE518B0`,
source manifest `A09FD9D9C1A46B048FC15E1D2B241837964BC3733917D5C8A3D6481C98C156BA`, evidence manifest
`2052A812DACA2FD381607E9402EB4C5FDBE78CAB29DA295ACAA57CC007D0AFE4`.

Secondary review found the new fixture should explicitly reject a pre-existing size card or color edit and clear
those states on failure cleanup. That repair is in newer caller-root source, absent from r63. Its reviewed
capacity/refusal/overflow controls and owner-only source-capture counter are likewise newer and unqualified.
Current combined Release/ASan/analysis/ARM64, native activation, broader lifetime/admission, H3/H4 and G1 remain open.

## Actual admission and physical FIFO overflow - full ASan Debug r64

Actual child 76532, 2026-10-05T01:13:25.273055Z-01:26:03.740941Z, exit **0**; 771 unchanged inputs,
freeze `428BE42FF87DEFFB6AE759A9213835BB936FA88D41E2B2C7F762370D987759B7`.
Stdout `31BB36D5A08404477737CD55F09068A29605AAB298785B39DFED37B17AFAD44F`, stderr
`5698AF0EC364CB8BBC4C3DA43DBD4DC025947813D36F5BC4B6B6472DAFC2F7C5`.
All full suites, unchanged ISA/PDB/scanner controls, actual ASan detection, WARP host, crash probes, CLI/build/RAW
fixtures, 27-row smoke and screenshots pass. Skills/format pass before the freeze.

Four host controls fill all nine actual scalar openings, cancel every exact receipt, fill the real 128-record
scheduler table to refuse before capture, then recover after draining. Two physical pointer controls fill 512
distinct accepted points, reject the next blue point and seal without a release sample. The accepted brown
endpoint remains selected; the original receipt reports quota failure and all graph owners retire. An owner-only
borrowed counter proves exactly one immutable document capture per admitted prefix and zero on refusal. It is
cleared before its harness storage dies. The fixture now refuses pre-existing size/color cards and clears its own
cards on failure cleanup. Secondary review confirmed actual capacity and checked pointer/source lifetimes.

Independent counters preserve **6,604 document / 9,244 application** controls (9,238 + six) and all **79 stages**.
The first proof assertion excluded the original parameterized `sparse.concurrent-readers` PASS line from the
document count; the corrected helper preserves that line and multiplicity. No source or runtime was changed.
Proof `verified-admission-overflow-registries.json` SHA-256
`71842E08E55D6033926FCB6B78CE83A236313442DFBA95BFBA027D342C70BB94`.

Archive `p1-sampler-admission-overflow-x64-ASanDebug-r64-full-qualified` independently verifies **1,046 copies / 79
stages** at 2026-10-05T01:28:32.4388838Z. Completion
`4198170DDD916FE416EECCC3DB14EF35CDC1D6AABFDDEB26168F16A441476261`, source manifest
`7A811B14089503DBE89B82C1901B04A5CC1A8FC1B9F43B5E32263796A01134B7`, evidence manifest
`7BE423AD81D7E1A9245BF333AE09F664E54D6EC88CEC04D7140A3D2B4B16E43F`.

Later physical Map-busy/recovery controls and focused sampler benchmark modes are absent from r64. Current
combined Debug/Release/analysis/ARM64, native activation, broader memory/device/shutdown, H3/H4 and G1 remain due.

## Physical Map failure/recovery and focused sampler smoke - full Release r65

Actual child 25828, 2026-10-05T01:31:30.898435Z-01:37:36.570286Z, exit **0**; 771 unchanged inputs,
freeze `87DA5565ABFA0383550B4C01DE35004CEF46EE73C6E58D16AA4639EC14187C0D`.
Stdout `78CB73FDC7A9B85FF0ADFDEA7A1D598B28175F689DC75F5AE4CC31FFF0DAC291`, stderr
`4055458730DF784F72B52880CAF36881A1BD6D79A44BF1ECBC317D479B365CB2`.
All full suites, unchanged instruction/PDB/scanner controls, WARP host, crash probes, CLI/build/RAW fixtures,
27-operation UI smoke, focused one-operation smoke and screenshots pass. Skills/format pass before the freeze.

The physical sampler cases inject one busy Map or two persistent busy Maps after the first real successful Map.
The persistent case retains blue and fails locally after two copies, three Map attempts and one event barrier;
the recovery case proceeds through brown/transparent with three copies, four Maps and one barrier. Each captures
the original source exactly once and retires all graph ownership. Successful retry uses the real D3D context.
These are deterministic policy controls, not observations of naturally occurring driver busy behavior. Secondary
review finds no concrete defect in their counter, source, color or lifetime expectations.

The cheaper agent adds exact focused CLI aliases, malformed-value controls and one-operation smoke validation.
Primary implements runtime selection, unchanged fixtures, and separate initial-display/excluded-warmup rows.
Review preserves the production allowance of one event barrier per copy instead of requiring zero real busy Maps.
The smoke requires two boundaries with frames/valid clocks, six recorded copies and a single completion report;
the original 27-operation table remains intact. Its 64x48 durations are correctness diagnostics, not A/B timing.

Independent counters preserve **6,604 document / 9,248 application** controls (9,244 + four) and all **79 stages**.
Proof `verified-map-focused-benchmark-registries.json` SHA-256
`546A50736F45BE3CAB867106AC55D8D328919863EC518D6AA71F75FBD153D38B`.
Archive `p1-sampler-map-focused-benchmark-x64-Release-r65-full-qualified` independently verifies **1,041 copies / 79
stages** at 2026-10-05T01:40:05.5562835Z. Completion
`1C790C862E000CD1398FCBA36A40D57555D5AE75CFCE0FF9C838950835D7CCC5`, source manifest
`F8A9C6FAE574C289014D2FFAC28FDE7130D6759DF04AF1A19BBD01FEDB170868`, evidence manifest
`0732E1E98AB1AC177F0228AE380441164161CC7319CA4600A97971711EFE02DE`.

## Focused Release latency - r1 presentation refusal

The serial recorder copies the exact r65 EXE/PDB/runtime DLLs from that verified archive into a fresh immutable
snapshot and checks every hash before/after each run. Native `.build/p1-sampler-perf-r1-20261005/` preserves its
source/helper, qualification binding, raw logs and observed results. First 4K hardware case reports Ryzen 9 9950X3D,
64 GB physical memory, NVIDIA RTX 5080 driver 32.0.16.1714, scalar dispatch and 32 workers. It exits **5** at trial 0:
`presentation-unavailable`, visible=1, displayOn=0, suspended=0, occluded=0, zero Presents and no frame/canvas error.
The recorder stops without launching remaining cases. No timing row or G1 result exists. The user is asked to
wake/unlock the desktop; correctness and shutdown work can continue independently. Preserve the refused attempt
and use a fresh output directory after the display state changes. Do not bypass the guard.

The newer shutdown audit identifies ticket/history-stamp fields that relied on a completion callback suppressed
by shutdown. A post-join cleanup and three actual host phases are prepared separately and absent from r65.
Final matrix, native activation, broader memory/device/shutdown, H3/H4 and original G1 remain due.

## Sampler shutdown acknowledgement - full Debug r66

Actual child 57048, 2026-10-05T01:46:35.509493Z-01:55:58.556964Z, exit **0**, 771 unchanged inputs.
Freeze `A172E8DC60099FC09183BE1897CA2D7271928865EDF95BC15262B7C2FDC43DD3`; stdout
`E7FD8685771EC007F4BB3B978C7C9078DE38688BF49283DD1CAD93E1F6AEAA58`, stderr
`7C27BE48C8C666FE9428C1092067FE429697E5F71E4FC0334FD9DB2101E08BE3`. Skills/format pass before freeze.
All full suites, ISA/PDB/scanner controls, WARP, crash probes, CLI/build/RAW fixtures, both UI smoke paths and
harness screenshots pass. The proof preserves every preceding literal application control and document PASS
line, including parameterized names, with **6,604 document / 9,266 application** controls and **79 ordered stages**.

Eighteen new checks use actual hidden WARP application hosts before source capture, at an acknowledged live
prefix, and during a held Map-recovery event. They retain the original receipt/source/reservation until join,
then require cancelled acknowledgement, cleared sampler ticket/history stamp, no capture/device/worker owners,
one final MTA CPU-root retirement, idempotent second shutdown and consumed fixture quit. Shutdown clears the
ticket and stamp after Close joins; suppressed Completed callbacks cannot leave the session busy. Secondary
review finds no concrete lifetime or order defect. Existing standalone shutdown tests and stage order remain.

Proof `verified-sampler-shutdown-registries.json`: SHA-256
`F84B8F013D45ADF3452AA08C45C0E5427F1CE5DA54C3446130CB400F7D6C3422`.
Archive `p1-sampler-shutdown-x64-Debug-r66-full-qualified`: **1,041 copies / 79 stages**, independently
verified 2026-10-05T02:02:33.3150033Z. Completion
`F77926C742C07DE70E34B8A4FF4D6D73D9CF1A02B23AA2ECD2363980C4A59FD3`; source manifest
`9B1E843CC8BF95CFEDD76839C070969757662DF68A76282B85364BB5B1A40C5C`; evidence manifest
`E043A93E8613B147C2A2FFF6AD785EDCA3503782079AAB1129C303B9BE60C28D`.

The first proof script mistakenly expected lowercase UI smoke text; correcting that diagnostic assertion
changes neither source nor runtime. Cached source-only document admission is absent from r66. Combined
Release/ASan/analysis/ARM64, native integration, broader callers and original G1 remain open.

## Cached source admission - ASan Debug r67 build refusal

The source-only document bound and 20 new ownership checks enter full ASan Debug qualification. Actual child
19528, 2026-10-05T02:05:43.064484Z-02:06:52.007003Z, exit **1**. The new test's inner `ImageLayer layer`
hides its earlier full-child fixture variable (C4456, treated as error); runtime is not reached. Primary repairs
the inner variable name to `record` without changing compiler gates or expectations. The secondary review missed
that warning. Skills and format had passed; those do not establish compilation.

Freeze `9923CF2B59D9D224AFE95AF0E4112C94A44587CECDFA63CC36D5DDB1E24694CF`; stdout
`35326A657C754276D669D3E3D7FAA4E36EDA426D386CB55112C4F0CDA22340C7`; stderr
`A8A4252C62957836AF4D50437F1F3B8C24BB1945D8E3E2F5B7FD1DA6346E00C7`.
Archive `p1-sampler-cached-source-x64-ASanDebug-r67-failed` independently verifies **1,045 copies**, zero
current runtime stages, at 2026-10-05T02:10:27.2601350Z. Completion
`83C71F777FE34456B4BC198CE3F1C9A8856F600559946109677187D409822761`; source manifest
`9938DE60CD144ED4F8DCD02279DC32FAEF71093B23CE46794D0362182D47D515`; evidence manifest
`879327A3EF488CF60E96D3AF2D3D9DFCDC227A40BA72DF61280500763EA8C94B`.
Old products/logs included for preservation do not qualify this failed attempt. Fresh r68 tests the repaired
snapshot; source-only admission, the final matrix and G1 remain pending.

## Cached complete source-only admission - full ASan Debug r68

Actual child 36388, 2026-10-05T02:11:45.349099Z-02:24:54.642310Z, exit **0**, 771 unchanged inputs.
Freeze `F04A8580A1D0815AE1E2A9112A020F70E55191F500AA6CB82647BC23D3680196`; stdout
`EE5CE8B923566265930DF915387504A0A04B27806E16F06BA8B3A9163A485B78`; stderr
`3C1F935C1792B6B87F348036A14BB1ABE557E71E7DA84C90FFD83664EB450218`. Skills/format pass before freeze.
Full suites, unchanged ISA/PDB/scanner controls, real isolated ASan diagnostic, WARP/crash probes, CLI/build/RAW
fixtures, both UI smoke paths and harness screenshots pass. No legacy numeric expectation is changed.

Sampler admission now reads the complete immutable document source bound in constant time. It includes cached
layer/name/curve/raster ownership, selection capacity/control allowances and potential sparse assembly, while
excluding history the sampler does not capture. Missing/overflowed child bounds refuse before capture. Twenty
new controls cover full child owners, default/published empty roots, copy/replacement, selection immutability,
late assembly, an unmeasurable nested child and repeated 10,000-record queries. Primary reviews the agent tests
and source; secondary review reports no concrete undercount/lifetime issue, but misses the archived r67 shadow
warning. The repaired compiled tests pass. Source admission alone does not close exact history accounting or uploads.

Independent proof preserves all preceding application PASS lines, **9,266 named application** controls and all
**79 stages**. Document PASS lines equal the complete preceding registry plus precisely twenty new source-bound
controls: **6,624** total. Proof `verified-cached-source-registries.json` SHA-256
`93909BE94E7347EF2110DA8E4EFA67BDB0058A1CD74B4F0C25E8ACD2094EEC8E`.
Archive `p1-sampler-cached-source-x64-ASanDebug-r68-full-qualified`: **1,048 copies / 79 stages**, independently
verified 2026-10-05T02:26:31.2968689Z. Completion
`BFD40D5B8B022E210C9FF7B7093E9EEC470CBAC3795948F786B1F0BC8CDD1AC2`; source manifest
`3993DDF679EBE977FDAE07EA546F6BF7DAF9BAC499765C650D7B63F1A7C57357`; evidence manifest
`A0FD123DD43FF77EE4D3CE90726E754F3DDC7D1E6747DA9234E61C7D7798CD14`.

Cached history-stack admission and actual sampler memory-wait/refusal controls are absent from r68. Current
Debug/Release/analysis/ARM64, native activation, broader callers/H3/H4 and original G1 remain due.

## Combined cached history and sampler memory-pressure candidate - r69 compilation failure

Primary implements immutable history-stack ownership caches and replaces layer/entry walks in edit/restore/
retirement admission with cached queries. The complete conservative bound intentionally overcounts sharing and
potential sparse assembly; it does not close unique retained-storage accounting or late-residency enforcement.
Secondary review finds no separate cache undercount/lifetime defect and identifies the existing shape-source
trim omission. Primary verifies both walks, adds that field, and writes four independent 1 MiB / half-MiB checks.
Nineteen agent root-cache tests are reviewed and corrected before integration: a mismatched shared-pointer type,
erasing the wrong expected child/capacity, and iterator dereference. Private syntax r2 preserves C2039; repaired
r3 passes all three TUs under stdcpplatest, W4/WX and permissive-. These probes are not full runtime qualification.

The cheaper agent writes a fresh actual WARP sampler memory fixture using the existing startup MemorySource
probe. Primary catches unsupported Guid boolean and JobStatus enum use; the agent repairs them. Primary removes
a first-pump refusal assumption because recycling may require acknowledgement. The idle check is
`!RunUiActions && !HasUiWork`; scoped source readiness was checked by the r66 shutdown fixture, not this test.
Three cases have 23 assertions: waiting then recovery, exact-ticket
cancel while waiting, and hard memory refusal. Literal blue/brown, original receipt/ticket/history, zero captures
before admission, one on recovery, complete retirement and joined shutdown are required. These are injected
memory-policy cases, not observations of system-wide low memory. Existing stages and numeric goldens remain.

Actual child 23348, 2026-10-05T02:39:28.516033Z-02:40:37.143011Z, exit **1**, 771 unchanged inputs.
The full Debug build reports C4702/C2220 in `HistoryEntries::Publish`: unsupported ownership policies
instantiate a loop whose branch always breaks. Runtime is not reached. Primary moves the compile-time policy
branch outside the loop, preserving empty-root bounds, unavailable nonempty policies and immutable publication.
Warnings and expectations remain unchanged; private syntax-only success did not cover code generation.

Freeze `AF2A5C7B053507502971AFA9F65FE6200F32674334254B724F72076BB6B62CFE`; stdout
`6894070368502C47BEE7597907F53106FF4D4E4E1B792761B62F1D114FBFBBCE`; stderr
`A8A4252C62957836AF4D50437F1F3B8C24BB1945D8E3E2F5B7FD1DA6346E00C7`.
Archive `p1-history-cache-sampler-memory-x64-Debug-r69-failed` independently verifies **1,040 copies**, zero
current runtime stages, at 2026-10-05T02:48:46.4141267Z. Completion
`67D88A8FD3F6B001D5A9BFA6378605C74ECF3AF90BDBCF5A5B07946D896AD560`; source manifest
`8CF551DF341E970F7CE31743DE3275BEF47E9350A1BC7B002DCB7606B154C665`; evidence manifest
`A7C90DDEC81933A2282645FA428BDEB9F8FD2698E3BEAFF048BB962644933B95`.
Older output logs are preserved and do not qualify this attempt. Fresh full Debug r70 tests the repaired
candidate; current matrix, exact accounting, native activation and G1 remain open.

## Combined history cache - r70 document fixture failure

Actual child 21716, 2026-10-05T02:50:21.365986Z-02:56:39.343250Z, exit **1**, 771 unchanged inputs.
Compilation and ISA/PDB/scanner gates pass. DocumentTests fails one new assertion,
`history-retirement-bound.shape-source-only-history-owner-counts-one-literal-mib`. The fixture clears only
`shapeImage`; existing layer equality deliberately excludes this derived cache, so no history entry is created.
Primary changes the fixture to perform a real rename while clearing the cache. The original one-MiB charge,
live exclusion and half-MiB trim expectations stay unchanged. Production equality and all legacy goldens stay unchanged.
The application runtime and sampler memory fixture are not reached.

Freeze `B4AF0FAA4EC5BD542A53E8D46CCC9FDE63CCA3D72F1C895289F95100EA83FEF8`; stdout
`094E60DBFF1AF49439E4BFA3E514494A61B0498ABD70105CC62DE9A52CD31E2F`; stderr
`D7A6F383AB79B73F669914EDB26859DF411BE623D0C9B9F27EBD57C1FFD9D479`.
Archive `p1-history-cache-sampler-memory-x64-Debug-r70-document-failed` independently verifies **1,040 copies**,
zero current application stages, at 2026-10-05T02:57:26.2169207Z. Completion
`EF53B99D4D7087C2D62CCC4E0411D87644458DA168A877D068626DA51831494A`; source manifest
`5B7FBA392AF92BED18DBEDC91B58A0F4236B4F0E1874BB04A975954ED579DB68`; evidence manifest
`E9D1E23B5BC820366CEE0E0FA6C6EB5A89EA8AAC4AFFC7BF321EAE6C928C3220`.
Fresh r71 tests this repaired candidate; later topology source is private and absent from this freeze.

## Cached history and sampler memory policy - full Debug r71

Actual child 10300, 2026-10-05T02:59:00.008441Z-03:08:18.431111Z, exit **0**, 771 unchanged inputs.
Freeze `F4C557B79769A2C76E0EB7E687C95612AB2A7A9587753E1B99D4EF8CFAA37C2A`; stdout
`034AA58019AA7C58FE704CF75BFBD27237EDA748B8F6758291B729A2DF0E47AB`; stderr
`EE58B58EF4D73C62CBB27E4C01476FDB347C0D14C6A63D006F90753C13B2DFB6`.
Skills/format, all full suites, unchanged ISA/PDB/scanner controls, WARP/crash/CLI/build/RAW probes,
27-operation/focused sampler smoke and harness screenshots pass. No legacy golden or warning gate changes.

Exactly 34 new document controls (19 cache + 11 snapshot/history + four shape budget) and 23 sampler-memory
application controls pass: **6,658 document / 9,289 application**, all original PASS-line multiplicities and
ordered **79 stages** preserved. The first independent proof script uses the wrong exact document-stage marker
and fails before writing its proof; repaired extraction matches the observed `Running document model tests...`
marker. This changes neither runtime nor source. Proof SHA-256
`D9AC4AC19B113B7FABDE427EF6B3A6488C9518CE840E056F648FB3745AD410BB`.

Archive `p1-history-cache-sampler-memory-x64-Debug-r71-full-qualified` independently verifies **1,040 copies / 79
stages** at 2026-10-05T03:10:50.1510965Z. Completion
`B69C7F629A271548C80B789260BE9EFED3F679A52369118EAF06F5B6641E5314`; source manifest
`E248B4171414C905EC8AD283718612911048041E3FD32A664EA52F8E657B3208`; evidence manifest
`97D29B7B76A12A74317E5FD12D49D06DBF3BE739E474EFFDCD7912423EF379AA`.
The registry proof and its eight exact inputs are independently copied/read back in
`p1-history-cache-sampler-memory-r71-registry-proof`, verified 2026-10-05T03:12:35.359605Z, input manifest
`4DDFB5216C61D3CBB557B8ABE268479FB7AB68DF701BCE789BD02B619C2BF096`, receipt
`930E730791D2CA904D57B99313AE4B6363FB462D1B30A34CC0B663F52449383C`.

Secondary reviews find no separate cache undercount/lifetime or memory-fixture false-success defect. They confirm
that nested closure/late sparse residency and physical Escape during admission waiting remain unproven by r71.
Later topology and Escape source is absent. Current Release/ASan/analysis/ARM64, exact H3/H4, native activation
and original G1 remain required; this slice is a qualified WIP checkpoint, not P1 completion.

## Complete storage topology and physical sampler Escape - full ASan Debug r72

Primary integrates four source files only after preserving and committing r71. No older Caller specifications
or continuation diagnostics are copied. Skills validate all 15, formatting checks all 651 and diff checks pass.
Strict private frozen Common-header/DocumentHistory Debug code-generation probes r1/r2 pass without linked
runtime claims. Their output/source receipts stay under native `.build/history-topology-codegen-r*-20261005`.

Cheaper-agent tests are reviewed: primary requires literal nested charge equality, required PrepareEdit names
and topology-only lifetime after releasing the separate snapshot. Twenty-one topology controls cover complete
nested/shared owners, capacity, source lifetime, stale matching, late Data without cached totals, absent/empty
states, cancellation and selection/live exclusion. Primary adds four reference/mixed-path checks: exactly forty
bytes, one-byte-over-budget refusal and complete live nested exclusion at a zero budget. The review also prevents
temporary topology construction in reference End; its existing scan now uses complete iterative closure. These
changes do not solve concurrent late-cache growth, incremental unique accounting or native caller migration.

The physical Escape fixture sends real hidden-window key messages during memory waiting after pointer Up; the
existing exact-ticket cancellation case remains unchanged. Nine new fourth-case evaluations pass for cancellation
authority, original pending receipt, Cancelled/E_ABORT, zero capture, unchanged source/history/palette and joined
retirement. Pointer-held waiting remains unproven. The same run qualifies 25 new topology/reference checks.

Actual child **108668**, 2026-10-05T03:22:01.321444Z–03:36:49.015623Z, exit **0**, 771 unchanged inputs.
Full ASan Debug passes **6,683 document / 9,298 application controls** and all original **79 ordered stages**;
relative to r71, this adds 25 topology/reference controls and nine physical Escape evaluations. Archive
`p1-history-topology-sampler-escape-x64-ASanDebug-r72-full-qualified` independently verifies **1,048 copies / 79
stages** at 2026-10-05T03:41:16.6295970Z.

Freeze `4E4C78B06D8AB39C6B28BEF479F2F42DEE9ABF265AE07DC00AF101EDCA51CC23`; stdout
`0BFE5579EC374D7B9AFE979746408601ABCEF9F2C3AA932E7F8C17508AE49C86`; stderr
`4D8A3B4F0FA75DBB2108CFBBEB8B38B738AA5EA58F4FEF263F5EFE9DB5C0EC98`.
Proof `EF6F09A69B12B71701DAB9632632C336F1B31E9D60D1AE4C004FE6132DAAFB5F`; source manifest
`2E128FF93C9C0C5D951AF7BC4F07C301BB02667510E149306BBA033861BF26D2`; evidence manifest
`67B2ACD6E3B4CA2FE5B067B93144C3A92A388675E2BE6F1EC6A216882F79A602`; completion
`40E3795F6ADCA97A955AE6DAE17D26F11379294FCD757F47520C2A96DDDD52C6`.

This is a qualified ASan Debug snapshot only. Current full Debug/Release/analysis/ARM64 matrix and native
activation remain due. H3 exact unique accounting, late-residency synchronization, reference-path cost, pointer-held
Escape and G1 remain open; r72 does not close the P1 phase.

## Same-source full Release qualification - r73

Actual child **102972**, 2026-10-05T03:56:35.738302Z–04:03:51.049489Z, exit **0**, same **771-input freeze** as r72:
`4E4C78B06D8AB39C6B28BEF479F2F42DEE9ABF265AE07DC00AF101EDCA51CC23`. Full Release preserves the exact **6,683
document / 9,298 application controls**, all **79 ordered stages**, and all PASS counters/routes.

The first verifier expected the two command outputs to contain identical per-route registries and refused before
writing proof. After fixing collection to keep both route registries separate and compare their combined counts,
verification succeeded with no source or runtime changes. Proof `7314BDD8A6B4A8EABB0845E1EDD85D957C1A8326B2EA09EFE95F1FEFEE0CC1F7`; helper
`D9FC9F256F45C06401FAE9CDF6E6F289A27AB2AE14B70A4DEF8B20B465FDD082`.

Stdout `2EF4E4400D5658B7C21BB1B5237A807854B991ACAD5B0A98D6AF30AD25B6A563`; stderr
`D80E52C0C7A6B24FE8166997E33EEFD2148A7FB1F7269B06EA542FFC443FE4BF`. Archive
`p1-history-topology-sampler-escape-x64-Release-r73-full-qualified` independently verifies **1,041 copies / 79
stages** at 2026-10-05T04:07:07.1088436Z. Source manifest
`26EFFDC0B6E73FB0D7E7053D12F7AE047CF061C32FA687E829D9BA4DE3524F68`; evidence manifest
`A56F1E289F6998DFE68FBFFDEF56C45FCEC0B74885BB30521F7E756CA9FB1C52`; completion
`5F16165584D23F273395E4376BE2CE57B4EA8FE433C7FC9AFD986BCA1979496F`.

This independently qualifies Release on r72's frozen source only. Full Debug, analysis, ARM64 cross-builds, native
activation, H3 unique accounting, late-cache/reprepare synchronization, reference-path cost and G1 remain open.
