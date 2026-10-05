# P1 resumed residency, mask and Warp core qualification — 2026-10-05

## Execution checklist

- [x] Verify the saved clean branches and resume after the user's “continue until done”.
- [x] Replace movable residency leases with nonmovable scope owners and runtime thread-ownership checks.
- [x] Add five normal-suite recursion, no-shared-assembly, dense-read and release/reacquire controls.
- [x] Integrate prior 23 residency refresh controls, six semantic Gray8 thumbnail controls and Warp private-copy controls.
- [x] Review the cheaper agent's local-buffer Warp cleanup and add its valid/failed retained-capacity expectations.
- [x] Full Debug r77 and Release r78 pass on identical 771 inputs; independently verify every preceding PASS count/route and all 79 application stages, then archive every copy.
- [x] ASan Debug r79 passes on the same source; all controls/stages and its archive copies verify.
- [x] Repaired waitable core qualifies full x64 Debug r86, Release r87, ASan Debug r88, Release analysis r85 and all three ARM64 cross-builds; verify and archive every copied input.
- [x] Commit/push the qualified core slice (`ce4f8b4`) and Native evidence/checklist (`88b9b1b`).
- [ ] Qualify the combined cancellable-wait, notification, metadata-refresh and history-only maintenance host candidate.
- [ ] Wire remaining callers, live-cache maintenance, native activation and original G1 gates before P1 Done.

Status: ACTIVE resumed execution. This record qualifies only the named managed core snapshot. It does not
activate it in Native or close H3, H4, G1 or P1. Later metadata-host and Wand changes remain private/unqualified.

## Source and independent core checks

Worktree `C:/Users/eric/.codex/worktrees/p1-opacity-host-proof/RedPrism`, branch `codex/p1-opacity-host-proof`.
Parent source `1936fe6`; the pause checkpoint/docs are `e77e6e9`.
Nine reviewed source/test counterparts were transferred with a create-new receipt:
`Z:/src/RedPrism/.build/history-residency-integration-r1-20261005/receipt.json`, SHA-256
`5C496B08B2532288A7579EB80D8104AF5B48CA0FD3D03DCC454D9AFBF013CE74`.
The full `DocumentTests.cpp` was not copied: only its RasterWork include and new `TestSmearWarp` prefix were
transferred, preserving every integration diagnostic and existing golden. Formatting checks all 651 files;
all 15 skills and diff checks pass. Linked suite inputs are frozen independently of this evidence document.

Scope includes opt-in history epoch/permit/worker reprepare, explicit publication move/destruction cleanup,
private fitting sparse Gray8 mask thumbnails, and cancellable Smudge/Liquify source copy. Valid Warp buffers
are staged locally and transferred only when complete; refusal/cancellation/allocation failure releases staged
capacities and leaves zero dimensions. There is no constructor-specific deterministic allocation injection
hook, so that catch is not directly exercised. The new linked normal controls preserve source caches and
literal pixels, compare retained invalid storage, and check independent history/fault/latch expectations.
No host is opted into the residency protocol in this frozen core snapshot.

## Recorded full x64 suites

Observed: **6,778 document / 9,298 application controls**, **79 ordered application stages**. There are exactly
49 new document controls versus r74: 23 original residency, five scope-owner controls, six mask-thumbnail
controls, 14 Warp source-copy expectations and one valid retained Warp expectation. All prior multiplicities,
output routes and stages remain. All full command-line, ISA, crash, RAW offline corpus, runtime and harness
screenshot gates pass in each named successful profile. Real sanitizer qualification belongs only to r79.

Common 771-input freeze SHA-256:
`676AB623E3E18A4CAB6641D1330F61C13E6D763C2ED0646BF640B7204F7F81CA`.

### Debug r77

Actual child PID 65796, `2026-10-05T06:12:07.114044+00:00` to
`2026-10-05T06:21:10.016081+00:00`, exit 0.
Stdout `A4A14F1685858D23B350801669856F34A0D0B03661D7FACEE9C1C8B6BD53373B`;
stderr `54BD7380DCF8F63E40ED8C3125159908D076FF429B3F36F0590A422081E65321`.
Registry proof `97BB1DBE5D07CF4403AE75C159322AA02CBDE08603B827FC4764C6CC9300D6BD`.
Archive `p1-residency-core-mask-warp-x64-Debug-r77-full-qualified`, 1,041 copies reread/verified at
`2026-10-05T06:22:53.6668063+00:00`. Source manifest
`5CBE5DD9696C5353766DFD0C0AA8F462C88E8AC345342FDC983B7BC99C08F1D2`;
evidence `D6303CA5171F7D02696C5638FDBB29B4C1F502F8F7042C333AEACE4A8A40D72F`;
completion `C1D7CF04B2F05473C4DB394A52EE309F02921A129C6C81502D7F93B7A90AB8B0`.

### Release r78

Actual child PID 63204, `2026-10-05T06:23:21.438154+00:00` to
`2026-10-05T06:29:14.417270+00:00`, exit 0. Identical freeze and complete registry.
Stdout `A502B0326AE4D6BBE7AEFF09C4098171011C4F2DE69B687B2ED18AA4F1B37319`;
stderr `9BECC521776735F46100B89B4FC33D7CED0F1D71A6F842E7C8B7D09F1A9A75AB`.
Registry proof `D0CFA785B00B7B6339E8DE84BD5A1A76A3ACF79F73DDF19D92A6C033C7DAEBC1`.
Archive `p1-residency-core-mask-warp-x64-Release-r78-full-qualified`, 1,041 copies reread/verified at
`2026-10-05T06:34:37.5019821+00:00`. Source manifest
`AB56B7E8E652203D41D04A0888B5802D599C1EBD15CC44643EA228C08790C380`;
evidence `21F4A71998FE1AD7701253B6C3334D0CF173C168BA5EB4701A887B1CED230446`;
completion `C4093EF1089B70C15811332ECBFE45E3FA75D9F2CEFF860436006210B474FCDA`.

### ASan Debug r79

Actual child PID 3180, `2026-10-05T06:34:40.795019+00:00` to
`2026-10-05T06:48:08.846883+00:00`, exit 0. Identical freeze and complete registry.
Stdout `F919865AAD730D3EAD1016C1B11C9BDC9310369EE9878DD3E4466CEA21E7DBE8`;
stderr `48A1AD6BC6CEA654E82D90750ACAC902DE347DC65760B8DA6C813D145FD2E1AA`.
Registry proof `D0CFA785B00B7B6339E8DE84BD5A1A76A3ACF79F73DDF19D92A6C033C7DAEBC1`.
Archive `p1-residency-core-mask-warp-x64-ASanDebug-r79-full-qualified`, 1,048 copies reread/verified at
`2026-10-05T06:49:36.5953946+00:00`. Source manifest
`097FE54632C4AE9046B209A69B9DFCD22C34DC94A0C8091F49B054F1157C6BDC`;
evidence `C029734328B5828643CCE3B2635AEDB8935298F04239140BCF38F2E248870FDB`;
completion `34640C1D5A8BC6626E66AEAEB291CE4C29B0E5E37CFAAE77081B4B96304BCE64`.
Real sanitizer execution is qualified for this core snapshot. Later analysis repairs require fresh runtime
qualification; these three runs are not evidence for a later source snapshot.

### Release analysis r80 failure and repair

Actual child PID 73960, `2026-10-05T06:50:09.925934+00:00` to
`2026-10-05T06:54:05.208678+00:00`, exit 1, on the original 771-input freeze.
The gate found C26135 on the conditional residency-lock constructor, C6262 (164,656 bytes versus
the unchanged 131,072-byte test limit) in `TestPaletteIntentAuthority`, and C6001 on the refresh test's
conditional `intendedRevision` initialization. No runtime golden or warning/stack threshold was changed.
The wrapper now describes its actual conditional acquire and lock identity with SAL; palette fixtures
use heap-owned sessions with identical operations/assertions; the Guid is initialized explicitly before
assignment. Primary review also corrected two unqualified agent wrapper tests before integration: a
publication must keep its permit through apply, and the undo/redo result accumulator must be mutable.
The claimed failure is one oversized test function and one initialization diagnostic.

Failed archive `p1-residency-core-x64-Release-r80-analysis-failed` preserves all 992 copied inputs/evidence,
verified `2026-10-05T06:54:47.4071061+00:00`. Source manifest
`44C21C026CA38802781451C383803354CC786ED4A6528F5918DB3AEC4573AE07`;
evidence `9953FFC3630304BFC2587007CE6C1B2B369308ACB8F1EDF9D93A9EF08E32609F`;
completion `031B74BCED7DBCFCB62CF14990038FAE8FD959488822E71F2F0EA8CC6C32007B`.
Stdout `0D71EAAABC9C62132388C9B1A43591D8814B20FE226E30A09663D0CE6C30305C`;
stderr `A8A4252C62957836AF4D50437F1F3B8C24BB1945D8E3E2F5B7FD1DA6346E00C7`.
### Later analysis diagnosis and waitable-gate decision

Every subsequent failed analysis attempt is preserved with all copied inputs and evidence. No diagnostics,
concurrency negative controls, ISA gates, stack limits or legacy expectations were disabled.

| Attempt | Actual child / UTC span | Freeze / result | Diagnosis / archive completion hash |
| --- | --- | --- | --- |
| r81 | 65188 / 06:56:42.654078–06:59:46.595414 | `011867E5F0E5DC937B753B79E8667B19F38D369FDB628C6C5C1A4C5B78F178A2`, exit 1 | SAL cannot parse a member-function call in the conditional annotation; two sampler assertion helpers do not prove their pointer prerequisites. `p1-residency-core-x64-Release-r81-analysis-failed`, `1BACFFEC28046AF49AECA37188A02EE477CE94BA84C62863B2C97A5A856A4A18` |
| r82 | 78296 / 07:02:27.526448–07:07:13.944028 | `F4976DEB222990EC21E52AD51C112BC4EA1DEBBCE3C507E95BDFC68F82C8F44F`, exit 1 | Conditional constructor witness compiles, but the unique-lock move/try path introduces C26115. `p1-residency-core-x64-Release-r82-analysis-failed`, `39158A08346087CCAE8838C1477F4D815B089B6DA89D2BA81455C0E8E3CEB4C2` |
| r83 | 63444 / 07:09:27.295157–07:12:06.108008 | `1E09F5580836A1D14DD9F8EBB5171E8B95272144EECCC685EFB2A91A770D2225`, exit 1 | Scoped acquire/release annotations cannot prove the composite permit's second destructor/reset release (C26167/C26160). `p1-residency-core-x64-Release-r83-analysis-failed`, `41E604994B2FD40C0FD3C47D01B36197AA417122D9B4E8E2903F8598020BF4B1` |
| r84 | 22724 / 07:18:01.107533–07:20:39.871267 | Same r83 freeze, exit 1 | Accidental duplicate launch after a helper's exact-text anchor refused before any edit. It supplies no new qualification. `p1-residency-core-x64-Release-r84-analysis-failed`, `373AA34A4F6C7DB53F6CEAA9FBB77594439203CBE707EAC433FEC209F56A6971` |

The final core candidate uses a standard waitable atomic gate, with the same nonmovable same-thread scope
owners. An owner performs exactly one acquire CAS and returns Busy on contention. Workers park with
`atomic::wait`; release publishes with release ordering and `notify_one`. There is no timer/spin source, heap
allocation or owner wait. Recursion is refused and foreign-thread release remains fatal before the gate changes.
Per-storage assembly retains its independent standard mutex. Existing cache-publication/epoch, concurrent Busy,
move/drop/refresh and literal-budget controls are preserved. This change needs fresh full runtime qualification;
it is not justified by the earlier mutex candidate's green runs. Cancellation time and G1 remain measured exits.

### Release analysis r85 — repaired waitable gate

Actual child PID 40576, `2026-10-05T07:21:52.017991+00:00` to
`2026-10-05T07:28:44.938042+00:00`, exit 0. All five images, unchanged analysis/concurrency controls,
matching-PDB ISA/scanner controls and pinned DxUi provenance pass. 771-input freeze
`B1F0B1F1CF31248D7F058A2CD1651F1963E302F035B987115B97679D001C754F`.
Stdout `68821421042762D3ECA305782637AD8392EF6E9EF234A4AEE01163E08C860B85`;
empty stderr `E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855`.
Archive `p1-residency-waitable-gate-x64-Release-r85-analysis-qualified`, 1,054 copies reread/verified at
`2026-10-05T07:29:37.1721719+00:00`. Source manifest
`ED6E095A8EB6DE302738FEE0C137A9DCD4D264ABC46FA8249E342875C6D905B3`;
evidence `F84A2909A8EA0C4EF94631AE5E4FE862FA5BC121B1A3E9C87AA16BC32235DAEC`;
completion `6812632E1DD912CE8E09E8A8FB06CF3475D58ECCE0133E54287C6B4FDC1FE174`.

Current repaired core passes Debug r86, Release r87 and real ASan Debug r88 on the same 771-input freeze.
Every preceding control, route and all 79 stages remain intact: 6,778 document / 9,298 application checks.
ARM64 Debug/Release/ASan Debug cross-builds also pass; every archive copy has been reread and verified.
There is no native ARM64 runtime, Native activation or G1 claim.

### Repaired core runtime and cross-build evidence

| Configuration / attempt | Actual child / exit | UTC start to finish | Verified archive / copies | Completion SHA-256 |
| --- | --- | --- | --- | --- |

| Debug / `incremental-x64-test-Debug-r86` | 71996 / 0 | 2026-10-05T07:29:41.097913+00:00 to 2026-10-05T07:40:30.986302+00:00 | `p1-residency-waitable-gate-x64-Debug-r86-full-qualified` / 1041 | `377E373363B89ECCF00971CDF31DE5E239A23F2F5BCCFC22F4BDD74826FFD398` |
| Release / `incremental-x64-test-Release-r87` | 65820 / 0 | 2026-10-05T07:42:21.639898+00:00 to 2026-10-05T07:48:35.519443+00:00 | `p1-residency-waitable-gate-x64-Release-r87-full-qualified` / 1041 | `2EAEB932FAF9D4824476054B34C3A383971439401B66823E6A83BA92F85F4754` |
| ASan Debug / `incremental-x64-test-ASanDebug-r88` | 75620 / 0 | 2026-10-05T07:49:23.698011+00:00 to 2026-10-05T08:01:34.229208+00:00 | `p1-residency-waitable-gate-x64-ASanDebug-r88-full-qualified` / 1048 | `3ADFBC0EEE63DD1B352B59DE454B178B3917F7DF0B82D61B7CED5B8877A5EB31` |
| ARM64 Debug / `residency-arm-builds-r1-20261005/Debug` | 65800 / 0 | 2026-10-05T07:29:41.499586+00:00 to 2026-10-05T07:31:59.341958+00:00 | `p1-residency-waitable-gate-ARM64-Debug-r1-cross-qualified` / 973 | `55ABF6C64B24AF7A9196AF66C1F1C141299D2EFE315B1D2F1945E2BD511DC76E` |
| ARM64 Release / `residency-arm-builds-r1-20261005/Release` | 79380 / 0 | 2026-10-05T07:32:03.955104+00:00 to 2026-10-05T07:33:53.913114+00:00 | `p1-residency-waitable-gate-ARM64-Release-r1-cross-qualified` / 973 | `9B1E2931DBE2A6AB057D29875A6A9C2E600B9D49637B7A77ACA61E9A09232B0F` |
| ARM64 ASanDebug / `residency-arm-builds-r1-20261005/ASanDebug` | 3040 / 0 | 2026-10-05T07:33:56.795809+00:00 to 2026-10-05T07:35:07.664240+00:00 | `p1-residency-waitable-gate-ARM64-ASanDebug-r1-cross-qualified` / 977 | `B572100C3604CDF090CD874E0589FE3F43C4EB8EB7E53094FD32D6E816CED09D` |

The archive receipts bind the source/evidence manifests, actual child exits and logs. Registry proof for each
x64 runtime is `D0CFA785B00B7B6339E8DE84BD5A1A76A3ACF79F73DDF19D92A6C033C7DAEBC1`.
The next candidate adds cancellation while parked, bounded growth notifications and a real history-only
maintenance source; it has strict nine-TU code generation only, without linked runtime qualification.

All archives are beneath `C:/Users/eric/AppData/Local/RedPrism/QualificationArchives`.

## Separate next candidates and review findings

Private Caller now contains an admitted same-ticket metadata-host refresh: one completed edit, one input
capture, unchanged reservation/FIFO position, worker-only index refresh and owner permit release before every
callback/chrome operation. Extra clone/stack/root overlap is conservatively reserved before capture. Three
actual-host success/allocation-failure/cancel cases are drafted. They have no linked runtime result yet.
An independent live-cache notification/maintenance route remains necessary: failed or cancelled edits preserve
the original history/revision even if a separate consumer has grown its resident cache.

The cheaper Wand core sidecar is private in `p1-sampler`; host routing and complete checked Clipper admission
are untouched. Primary review corrected matched Add/Sub AA against legacy `Selection.swift:227` and current
`Document_Tools`: results use the captured current AA option; no-match Add/Sub preserve the exact old selection
and AA, and New no-match deselects. Three new draft expectations were corrected before qualification; existing
legacy goldens were unchanged. The candidate now has 15 new labels including invalid-mode refusal.
Do not qualify or activate this candidate until complete admission and actual callers are integrated/tested.

## Combined host candidate — qualification in progress

The next managed slice has 774 owned inputs and three new normal application source/header files.
The cancellable gate changes its private wake sequence on local/shutdown stop while retaining the ownership
bit, allowing a waiter to leave before the holder releases. Stop notices leave the residency epoch unchanged.
A worker retry with an unchanged witness now skips private index reconstruction; two controls inject the
construction failure seam and verify the original edit still publishes once.

The real metadata job retains its ticket, capture, prepared user revision and memory reservation through
worker refresh. The separate source consumes bounded growth readiness, defers behind accepted/live edits,
and prepares history-only trimming without changing document, generation, user revision or saved marker.
Its startup remains self-test scoped. Six literal host cases cover success, younger ordering, cancellation,
capture failure, worker failure and stale authority; transaction deferral and full scheduler refusal are added.
Primary review corrected the sidecar's field/type mistakes, literal grown retained charge (12 bytes, not 4),
and borrowed worker-counter lifetime. Full queue tests use completed-but-undrained records, without blocking
worker threads. Original controls and Main continuation diagnostics remain intact.

Debug r89 failed at compilation on two `const` cleanup guards whose new tests dismiss them; they are mutable
in the repaired candidate. No product threshold or expected behavior changed. Actual child 69708, exit 1,
08:12:41.093413–08:13:45.836483 UTC; freeze
`A64E4ECC9DC47508A16A84F33807DF8F2AB3F056FDC7295DABB87362DA7A3A99`.
Archive `p1-residency-host-x64-Debug-r89-build-failed`: 1,043 verified copies, completion
`1A8C5A68957A8FBF82DE7D7295E42CFF1DBBF2D6871A4146BF9543245D29A130`.
The repaired full Debug run is next. This slice does not qualify live source startup, every history writer,
Native activation, H3 completion or G1.

### Repaired actual-host diagnostic qualification

Debug r90 passed the linked build and CPU suites, then failed at the first new metadata fixture because the
blank layer had no raster asset. That test now explicitly constructs `RasterAsset` instead of dereferencing
an empty optional. The failed actual child 83216 ran 08:17:16.151115–08:24:04.818266 UTC, exit 1, freeze
`6B5E5A0CA6CD26C7E72391ECECA333A4D655C01BA7D52622B190A9BF07235DE7`.
Archive `p1-residency-host-x64-Debug-r90-metadata-fixture-failed`: 1,043 verified copies, completion
`09308EF209CFB7FD899D232D4357A0F1E64D04A979736B30D070B45D2730B7A9`.

Repaired build r91 passes: child 84020, 08:25:40.718977–08:30:18.628394 UTC, exit 0. All five linked images,
unchanged ISA/scanner controls and pinned-library provenance pass. Archive
`p1-residency-host-x64-Debug-r91-build-qualified`: 1,034 verified copies; completion
`10668931357F70C79421A8A296EC4D87D1DF8773FCC18DF1B22152F67910D408`.

Actual hidden WARP runtime r92 passes: child 86956, 08:31:33.544670–08:33:37.833894 UTC, exit 0.
The executable stays unchanged at SHA-256
`7F74AB5B4AE40117004050AC073EE99E756AFC17890F21D16449EA4821A0E5DF`.
It preserves all previous named controls, multiplicities, output routes and 79 ordered stages: **9,362 application
controls**, 64 added (23 metadata refresh and 41 maintenance controls). The aggregate metadata summary necessarily
changes from 139 checks / 43 cases to exactly 162 / 46, once on the same stderr route. No individual previous
expectation is removed or rebaselined. The helper explicitly validates that aggregate replacement and every
preceding named/literal control. Archive `p1-residency-host-x64-Debug-r92-runtime-qualified`: 1,034 verified copies;
completion `C5480BCA95EBD393E9D64D89B55E8619EC2EDA59D3BD3CF5B45C1EE824B15741`.

r91 and r92 share 774 owned inputs, freeze
`F773A1EC601C3F94091CF20398E08A13EAE4F4A585F2A32521B9879F604FB3A7`.
Full Debug r93 and Release analysis r94 are running on this source. Full Release/ASan, cross-builds and
comparative memory/cost gates still remain. The extra candidate/index/container overlap is conservatively
reserved before capture; its cost has not been justified by a measured speed or memory improvement.

The independent Undo/Redo and Select All/Deselect drafts stay in the private Caller worktree. Restore adds six
actual host late-growth/fault/cancel cases and ten unit controls; its repaired three-TU strict code-generation
probe passes (r3). Selection adds three actual Select All host cases and unit controls for both commands;
primary review corrected COW fixture writes, the two-pixel outline and the redo storage budget expectation.
These drafts have no linked runtime result and cannot be counted as integrated or activated P1 exits.

### Combined host matrix through r95; ASan r96 in progress

All completed rows below bind the same 774-input freeze `F773A1EC601C3F94091CF20398E08A13EAE4F4A585F2A32521B9879F604FB3A7`.
The full Debug and Release runtimes preserve every previous named control, route and ordered stage: 6,809 document / 9,362 application checks and 79 stages.
Only the exact metadata aggregate changes from 139/43 to 162/46. Registry proof SHA-256 `A1A33B8430D9126DCD01454F895C243236FFCAD838DBDED46168E9C486434BBF`; checker `9DF840D1C7A0F314C3423CB3D767534E8AC72ED985C6AF442E88005FC0DF9126`.

| Gate | Actual child / exit | UTC start to finish | Verified archive / copies | Completion SHA-256 |
| --- | --- | --- | --- | --- |
| Debug full | 56152 / 0 | 2026-10-05T08:36:29.814595+00:00 to 2026-10-05T08:45:49.919120+00:00 | `p1-residency-host-x64-Debug-r93-full-qualified` / 1044 | `4B187BFC3CC8253CD2A79B5E3AE401339FC7E5123CED8282166CC5B3E8D58DCF` |
| Release analysis | 15500 / 0 | 2026-10-05T08:37:05.760818+00:00 to 2026-10-05T08:43:50.883023+00:00 | `p1-residency-host-x64-Release-r94-analysis-qualified` / 1057 | `90069AC6C64305769882527EFF06F3D26A492112EB6551F9A865D77B5BA08BBD` |
| Release full | 79160 / 0 | 2026-10-05T08:51:07.235306+00:00 to 2026-10-05T08:59:59.739130+00:00 | `p1-residency-host-x64-Release-r95-full-qualified` / 1044 | `32D764DC7223619FF53B8EBAD82F6B92E05C65A266DEE5AFBB4E503E06098EA7` |
| ARM64 Debug cross | 7384 / 0 | 2026-10-05T08:51:08.073570+00:00 to 2026-10-05T08:53:45.402042+00:00 | `p1-residency-host-ARM64-Debug-r2-cross-qualified` / 976 | `F3C1A5F7091701CA3E7C2A42879FCB7FFB7BAD40D178C782E94B8584DE714D86` |
| ARM64 Release cross | 440 / 0 | 2026-10-05T08:53:48.559440+00:00 to 2026-10-05T08:55:35.672942+00:00 | `p1-residency-host-ARM64-Release-r2-cross-qualified` / 976 | `4DAC4C4123BC4E9A9AF73872780D2162C9E8365D9CC5444CDF918527F42BDA40` |
| ARM64 ASan Debug cross | 80076 / 0 | 2026-10-05T08:55:39.570199+00:00 to 2026-10-05T08:56:53.742791+00:00 | `p1-residency-host-ARM64-ASanDebug-r2-cross-qualified` / 980 | `BA09BC75AD860F5137D78095CE948666AD7D10E5333DE03C78B6018FF2B912AB` |

15 skills, formatting of all 654 files and `git diff --check` pass for the combined host candidate. ASan r96 still has no completion receipt and must finish before this source slice is committed. Cross-compilation supplies no native ARM64 runtime or G1 evidence.

Private next drafts include Undo/Redo, Select All and Deselect late-growth/fault/cancel host paths; selection nudge refresh and zero-offset preservation; and actual standalone shutdown at a pending notice or held prepared maintenance worker. Primary review repaired the nudge after-document witness, reversed Deselect budget arguments, reused younger failure gate and saved-revision assertions. The first combined 13-TU probe failed on a nonexistent test checkpoint enumerator; the failed source/logs are preserved. No next draft has linked qualification yet.

### Completed combined host matrix

Full real ASan Debug r96 also passes on the unchanged 774-input freeze: actual child 19968, 2026-10-05T09:02:44.085555+00:00 to 2026-10-05T09:14:35.359450+00:00, exit 0. The original controls/routes/stages and the same 6,809 / 9,362 / 79 registries verify with proof `A1A33B8430D9126DCD01454F895C243236FFCAD838DBDED46168E9C486434BBF`.
Archive `p1-residency-host-x64-ASanDebug-r96-full-qualified`: 1051 copies reread and verified; completion `31BEE63DE352A3511E6E3B86A700E8DAD34A504A2DEB833766B6AF4467C40FDB`. Source manifest `C4FE4719E9BF92BC9E05E25A5554D62A07C545FB1241C70992E4552E6B022491`; evidence manifest `21B9F93BEB4CC60B47D7CC050DE79786CE98141458AAA3D8E0A0F861AF6A8CA2`.

This qualifies the combined cancellation, growth notification, metadata opt-in and scoped maintenance host slice in Debug/Release/ASan, Release analysis and all three ARM64 cross-builds. Normal maintenance startup, remaining writers, Native activation, delta cost and G1 remain open. The next 13-TU draft probe r1's nonexistent checkpoint was corrected to `StorageIndexClone`; r2 passes 319 immutable inputs, freeze `1643C0EAB9F8A88963B214BA31E2C95DA7A66FB64DC37FFECDDD41F97D6290D2`, with no linked/runtime claim.

### Integrated restore/selection/nudge and maintenance shutdown candidate

The next 21-file source group is integrated narrowly, preserving Main's newer continuation diagnostics and
no-growth refresh fast path. It adds same-ticket history refresh to Undo/Redo, Select All/Deselect and nudge,
complete candidate/index overlap reservation, single-use/moved/no-op checks and actual standalone shutdown
with a pending growth notice or a held prepared maintenance worker. It is not yet qualified or committed.

The first full Debug attempt passed the CPU suites and new restore cases, then found that the restore fixture
leaked its 8-byte history budget into the preceding Select All control. The repair restores the original
budget on test exit and starts each case with it. The next Debug attempt preserved the original selection
controls but exposed the new Deselect fixture's historical outline sharing the current selection identity;
live-identity exclusion correctly removed its byte charge. The repaired private fixture uses a distinct
literal rectangle. These are fixture corrections; trimming/selection goldens and latency thresholds are unchanged.

Release analysis r98 passes on the original 775-input candidate; it does not qualify the later fixture repair.
r100 was refused by vcpkg's exclusive install lock before compilation while Debug dependency preparation
ran. Its failure is retained. A fresh analysis r101 starts after the Debug build stage; no lock is removed
and no running application is terminated. Source inputs remain frozen until that attempt finishes/archives.

| Attempt | Actual child / exit | UTC interval | Inputs / source freeze | Verified archive completion SHA-256 |
| --- | --- | --- | --- | --- |
| incremental-x64-test-Debug-r97: Debug full; fixture failure | 10684 / 1 | 2026-10-05T09:17:26.318080+00:00 to 2026-10-05T09:26:46.219048+00:00 | 775 / `93662A6DC698BC352631ADD22FDF238DCDBA8C0E5DF2D7F40A132AD592E52362` | `p1-history-callers-x64-Debug-r97-runtime-failure` / `50F1321BC63E1CE587BF4316BDCCB0C1E7DD96D4D6A8044BD1A45990FC0B8794` |
| incremental-x64-analysis-Release-r98: Release analysis; original candidate | 30444 / 0 | 2026-10-05T09:18:11.113360+00:00 to 2026-10-05T09:25:02.295458+00:00 | 775 / `93662A6DC698BC352631ADD22FDF238DCDBA8C0E5DF2D7F40A132AD592E52362` | `p1-history-callers-x64-Release-r98-analysis-qualified` / `1411BF4433B92A08A260CF15D563B7ACA550D81401FDE0706D8DF061DC4268CA` |
| incremental-x64-test-Debug-r99: Debug full; second fixture failure | 5552 / 1 | 2026-10-05T09:32:53.315042+00:00 to 2026-10-05T09:40:12.764113+00:00 | 775 / `55178994ED11207EDA1E253EB7707733C77EBDFB1947BDB9B00C44EA0171325A` | `p1-history-callers-x64-Debug-r99-fixture-failure` / `74EB52973F8AB03062E3CB019D88E47B029DEDAA02FCB0CC70CEC4B9531F15EC` |
| incremental-x64-analysis-Release-r100: Release analysis; dependency lock refusal | 48824 / 1 | 2026-10-05T09:32:54.121709+00:00 to 2026-10-05T09:32:57.065669+00:00 | 775 / `55178994ED11207EDA1E253EB7707733C77EBDFB1947BDB9B00C44EA0171325A` | `p1-history-callers-x64-Release-r100-analysis-start-failure` / `EA6E46C36CBBC7E140FD634AD5DB2DD6B3C4037DF639D05B4CF618ED96A2978A` |

The full current Debug/Release/ASan runtime matrix, repaired-source analysis, all ARM64 cross-builds,
predecessor registry proof and measured costs remain required. Original failed receipts and copied
source/product evidence remain under the named QualificationArchives folders. No native activation,
normal maintenance startup, remaining-writer coverage, H3 completion or G1 result follows from these attempts.
The separately indexed H4 upload plan and private row-copy/helper tests have no linked production qualification.

### Sparse fixture diagnosis corrected; charged leaf repair

r101 analysis passes after budget isolation. r102 then disproves the initial attribution of Deselect's failure
to its outline: constructing another SelectionLoops already gives a distinct held identity, even for equal
coordinates. The explicit different rectangle remains useful as a literal fixture, but did not fix the charge.
The actual error is `lateResidencySource.ByteSize()`: an unmaterialized sparse root owns zero resident pixel
bytes, while its retained leaf owns four. The fixture used zero instead of four in its budget and separately
asserted the root was four. The repaired fixture charges `leaf.ByteSize()==4`, asserts root bytes zero and
retains the original selection charge. It changes no production accounting rule or golden threshold.

r103 analysis and three ARM64 cross-builds pass on the distinct-outline source before this charged-leaf repair;
they do not qualify its later source. Build-only r104 is running on the repaired candidate before another
actual-host diagnostic runtime. Full required configuration/analysis/cross-build qualification still follows.

| Attempt | Actual child / exit | UTC interval | Source freeze | Verified archive completion SHA-256 |
| --- | --- | --- | --- | --- |
| `incremental-x64-analysis-Release-r101` | 86504 / 0 | 2026-10-05T09:38:21.489321+00:00 to 2026-10-05T09:45:13.671883+00:00 | `55178994ED11207EDA1E253EB7707733C77EBDFB1947BDB9B00C44EA0171325A` | `p1-history-callers-x64-Release-r101-budget-repair-analysis-qualified` / `29645DACE4E2E91BA5F600C475DC96B4D0458EC8FD381B1BB003BA7BF78A7EE5` |
| `incremental-x64-test-Debug-r102` | 30424 / 1 | 2026-10-05T09:46:23.034657+00:00 to 2026-10-05T09:57:13.816907+00:00 | `20A81E079147F79AE4A23A39E3D14A2513325E23C3BFA0AE5E0CF6EDCB7C951F` | `p1-history-callers-x64-Debug-r102-selection-fixture-failure` / `0B1E44F282248448E8DFE6B3033657BAA7EF729B1962AF7EF980386606AF9A7C` |
| `incremental-x64-analysis-Release-r103` | 72644 / 0 | 2026-10-05T09:49:32.917809+00:00 to 2026-10-05T09:58:06.881385+00:00 | `20A81E079147F79AE4A23A39E3D14A2513325E23C3BFA0AE5E0CF6EDCB7C951F` | `p1-history-callers-x64-Release-r103-distinct-outline-analysis-qualified` / `87F02C61D39E1BE47A6CAA4972B7A066B8ABACEB47FEE396590B4D89B2D68BC2` |
| `residency-arm-builds-r3-callers/Debug` | 87548 / 0 | 2026-10-05T09:49:34.256917+00:00 to 2026-10-05T09:52:05.466805+00:00 | `20A81E079147F79AE4A23A39E3D14A2513325E23C3BFA0AE5E0CF6EDCB7C951F` | `p1-history-callers-ARM64-Debug-r3-cross-qualified` / `D709E392A8DF31434DFDC7CBA55D9D3FEA626C152414AF26C2B4456CFE26E98B` |
| `residency-arm-builds-r3-callers/Release` | 39592 / 0 | 2026-10-05T09:52:50.329061+00:00 to 2026-10-05T09:55:20.739223+00:00 | `20A81E079147F79AE4A23A39E3D14A2513325E23C3BFA0AE5E0CF6EDCB7C951F` | `p1-history-callers-ARM64-Release-r3-cross-qualified` / `C8784146D2D54B3692652060A2812C377D3E6122C9A3E2C3434F4C05297EA8C8` |
| `residency-arm-builds-r3-callers/ASanDebug` | 83548 / 0 | 2026-10-05T09:55:23.778548+00:00 to 2026-10-05T09:56:14.649964+00:00 | `20A81E079147F79AE4A23A39E3D14A2513325E23C3BFA0AE5E0CF6EDCB7C951F` | `p1-history-callers-ARM64-ASanDebug-r3-cross-qualified` / `D59F4E25AE477D4A83543F3ED01A25E315AA7E188E09617F46118A9FA2A02C46` |

The current integrated writer/shutdown group remains uncommitted pending complete runtime qualification.

### Repaired leaf charge: runtime, analysis, cross-build and Release qualification

The current 775-input repaired source has freeze
`7C8011BCFE89E48D6C4423B431C56B39DC912A8D49026103231380D072B48073`.
r104 builds; r105 actual application runtime passes 9,446 named controls and all 79 stages; r106 Release analysis
and all three r4 ARM64 cross-builds pass. r108 full Release passes 6,841 document and 9,446 application controls,
preserving all preceding literal controls, multiplicities, output routes and ordered stages. Its only aggregate
adapter changes selection-host's summary from 162 checks/28 modes to 208 checks/34 modes; the preceding individual
controls remain mandatory. The 32 new document controls cover Restore, selection commands and nudge. The 84 new
named host controls include 14 actual shutdown checks, 46 selection checks and 24 nudge checks; six coarse Restore
mode reports are additionally retained. Registry helper hash is
`DD3E8ED56C596577EEA39D298A15707DF5105568EA94378E5186A9DD9EF5172E`; r108 proof hash is
`786A4B995A935A085B1103E06AB619A39BD85A37FE2288021FFCCABB803A093A`.

r107 full Debug stops in the existing clipboard-preservation fixture: "The self-test could not set the user's
clipboard aside." Its actual run overlapped r108 on the default desktop. This does not qualify Debug or prove
a production defect; no fixture or clipboard guard was weakened. Both attempts are archived. The fresh r109
full Debug attempt is now isolated from another application test. Real ASan Debug follows sequentially.

| Attempt | Actual child / exit | UTC interval | Source freeze | Verified archive completion SHA-256 |
| --- | --- | --- | --- | --- |
| `incremental-x64-build-Debug-r104` | 50096 / 0 | 2026-10-05T09:59:38.331887+00:00 to 2026-10-05T10:03:36.818241+00:00 | `7C8011BCFE89E48D6C4423B431C56B39DC912A8D49026103231380D072B48073` | `p1-history-callers-x64-Debug-r104-leaf-repair-build-qualified` / `8513EC1BC01812AC243431936F66960D5C553B293F981144AE0A1BADDBDF945B` |
| `incremental-x64-runtime-Debug-r105` | 87896 / 0 | 2026-10-05T10:04:42.381942+00:00 to 2026-10-05T10:08:42.010740+00:00 | `7C8011BCFE89E48D6C4423B431C56B39DC912A8D49026103231380D072B48073` | `p1-history-callers-x64-Debug-r105-runtime-qualified` / `0000E1E9B4B3F4FEE996A57E9A8D29C9B5BB809F1AFA0576A0ED2E54F39B499F` |
| `incremental-x64-analysis-Release-r106` | 3040 / 0 | 2026-10-05T10:04:43.412383+00:00 to 2026-10-05T10:11:34.301970+00:00 | `7C8011BCFE89E48D6C4423B431C56B39DC912A8D49026103231380D072B48073` | `p1-history-callers-x64-Release-r106-leaf-repair-analysis-qualified` / `64FB19F4A9BCC88024A5A44C154E514771626D4E79B90EFA8CD8D4D7339AB74B` |
| `residency-arm-builds-r4-leaf/Debug` | 90780 / 0 | 2026-10-05T10:05:55.051530+00:00 to 2026-10-05T10:06:42.114465+00:00 | `7C8011BCFE89E48D6C4423B431C56B39DC912A8D49026103231380D072B48073` | `p1-history-callers-ARM64-Debug-r4-leaf-cross-qualified` / `9F939E92B052D402B9A18A99168078C3CAB970712846BF3D2660998E839C4BDA` |
| `residency-arm-builds-r4-leaf/Release` | 65964 / 0 | 2026-10-05T10:06:51.694175+00:00 to 2026-10-05T10:08:50.844961+00:00 | `7C8011BCFE89E48D6C4423B431C56B39DC912A8D49026103231380D072B48073` | `p1-history-callers-ARM64-Release-r4-leaf-cross-qualified` / `D2087BB7B450F76ED8F1C6438385B8FE568203249D25F141DEFDE05FE437B555` |
| `residency-arm-builds-r4-leaf/ASanDebug` | 46412 / 0 | 2026-10-05T10:08:53.913512+00:00 to 2026-10-05T10:09:19.020114+00:00 | `7C8011BCFE89E48D6C4423B431C56B39DC912A8D49026103231380D072B48073` | `p1-history-callers-ARM64-ASanDebug-r4-leaf-cross-qualified` / `100257C05BF2B15A93F151F17FB53A7A867D3F514FEFE8F236B93B0B90FA98FC` |
| `incremental-x64-test-Debug-r107` | 21704 / 1 | 2026-10-05T10:16:42.228064+00:00 to 2026-10-05T10:24:28.162137+00:00 | `7C8011BCFE89E48D6C4423B431C56B39DC912A8D49026103231380D072B48073` | `p1-history-callers-x64-Debug-r107-clipboard-start-failure` / `C8D9D4CEA1C10928C6C2EB71ABA11EA7BA6796398683BD6DD81014CC9B0AE4F9` |
| `incremental-x64-test-Release-r108` | 91260 / 0 | 2026-10-05T10:18:10.849652+00:00 to 2026-10-05T10:24:59.716817+00:00 | `7C8011BCFE89E48D6C4423B431C56B39DC912A8D49026103231380D072B48073` | `p1-history-callers-x64-Release-r108-full-qualified` / `F3B1DF2F63982B7BCB4007EECCE33C55903E4B96FE1410E1441246CEC4383F6F` |

Native activation, normal maintenance startup, remaining-writer/delta accounting and G1 remain open.
