# P1 sampler repeated-pixel prerequisite — evidence

Status: full Debug r10 passed the repaired combined prerequisite snapshot. Other required profiles, combined analysis/ARM64, production sampler integration and G1 remain open; all original failures are preserved below.
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
