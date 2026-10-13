# P1 private owned metadata roots and allocation primitives — 2026-10-12

Status: **qualified private CPU foundation only**. Actual Application r10/r11 Release builds succeeded, but both aggregate Application runs are FAIL (exit 5); this is not full-suite qualification. Each passed `metadata-host.summary` (162 checks, 46 cases) and all four `metadata-host.owned-original-ticket.*` controls plus `metadata-owned-accounting.summary` (4 checks, 0 failures). r11 also passed `history-residency-host.summary` (41 checks, 6 cases, 2 controls, including ordered settlement) and the complete opacity-host (31 checks) and opacity-context (36 checks) scopes before its first failure at `canvas-brush-opacity.release.terminal-acknowledges-one-brush-after-opacity`, which assumes zero reservation while live metadata remains charged. Main has frozen r12: manifest SHA-256 `0EA3F6C0BC11FC5191540B6FAACF9080A6A424E721E3C15D997B9B7D3FDEEFDF`; protocol SHA-256 `9432E1D07D22E9257D5534BA2D5DDC6D45E154F4F8E25ADA43D7246BCF20EDFE`. The freeze has 11 reviewed fixtures for exact live metadata-plus-opening charge in cancelled/modal lifecycle and true zero for sealed cancelled originals after weak-observer release. Authority remains unchanged pending runtime. Actual Release build launched 11:04:32 UTC, session 85154 ongoing with no result. Numeric pixels, command ordering and history expectations remain unchanged; prior failures are preserved. No native source was changed for this checkpoint; native remains unqualified and all P1 areas remain open.

## Current r12 frozen build

Actual x64 Release build session 85154 launched at 11:04:32 UTC and remains ongoing; no result is available. The reviewed freeze contains 11 fixtures: cancelled/modal lifecycle checks exact live metadata plus opening charge; sealed cancelled originals require true zero after weak-observer release. Authority is unchanged pending runtime.

## Latest actual Application attempts — r10/r11

- r10 Release build: child 57228, exit 0, 2026-10-12 10:13:11.963371–10:22:46.962761 UTC; build-result receipt SHA-256 `EAF7E4B07F879D2E1BDE6BBC588A764B728C5B5E5F74DDAB6835D042F2E889C5`; build stdout SHA-256 `4711112BDDC1B7A08D25EF1013270F0738408FC5DE9EEA4F46A1A64AE6873B71`, stderr empty. Application child 96188, **exit 5**, 10:23:03.638530–10:23:48.984278 UTC; application-result receipt SHA-256 `17A312766686FEA34E28EF715AFD528C1C47960CA9AC830734F7562B3BFD92BD`; stdout SHA-256 `7C26D509CB9763E13F6ADDF680E687084633D1EF5C8270AD01B170112F0DEDAD`; stderr SHA-256 `EED201CB616167AEDCBDB3E8970B77008447EE7B763C716F23B16182EC8DEDB2`. First failure: `history-residency-host.ordered.actual-source-and-worker-settle`. Artifacts: `.build/P1Drafts/OwnedMetadataApplication-r10-Main/actual-startup-host-Release-r10/`.
- r11 Release build: child 28796, exit 0, 2026-10-12 10:28:18.641951–10:38:15.250024 UTC; build-result receipt SHA-256 `960437EDAFEF9BB93C0C005C24CC94722A34A368B409FCEE1517D1495F98502C`; build stdout SHA-256 `F39685E22EF594CD584EF1A65952B6A79619CACA37186FEDEECD6144C41BEF35`, stderr empty. Application child 29784, **exit 5**, 10:38:32.441663–10:39:16.351141 UTC; application-result receipt SHA-256 `578D061FAEC8846D4C93007F30EAD34BDF41105E08B7A008B05EC6D1748D73F0`; stdout SHA-256 `8EA5B4DB87BEF4602B8728B24ACE6DB5849339D11C2241E634570F1680AB12B9`; stderr SHA-256 `FDB0EFE5140BA1FB3A9B69ECB1B637D385B0CA7BA1944319C21C24A2A999D8C2`. First failure: `canvas-brush-opacity.release.terminal-acknowledges-one-brush-after-opacity`; the live-memory zero-reservation fixture assumption is being corrected. Artifacts: `.build/P1Drafts/OwnedMetadataApplication-r11-Main/actual-startup-host-Release-r11/`.

## Actual evidence

- Release build child **2684**, exit 0, 08:34:34–08:38:05 UTC; zero warnings and errors under repository flags.
- DocumentTests child **604**, exit 0, 08:38:09–08:38:20 UTC; 19,000 original records preserved in order,
  plus 11 owned-root assertions and summary, 12 allocation assertions and summary: **19,025 records**.
- Main independently verifies exact literal new rosters, all predecessor records (only the established bounded
  wand allocation statistics normalization), actual receipts, and unchanged executable/PDB and source inputs.
- Source manifest: `b16be0384216daca6b812f8f46dae75d7d3fd9e0302670d9701d2273e9efc3e7`, 1,275 private inputs.
- Recipe: `556c076b7d2f90a1d2b67d0f22a831ca04e46fbf652442b476150f1518bbbf0e`.
- Runtime log: `14a426329017761556e128386202711bab342c992da70b914f3048ca6972a15b`.
- Executable: `fa39b0a81d37434b50daeba477efc12e45b261683061d6b1b4207b6c022fddb0`.
- PDB: `93e64e3334ad79791ad644a4e020eb6d7c9883cf1e989e2cb1e2966a411b4cae`.

Generated receipts remain under `.build/P1Drafts/OwnedRootQualification-r5-Main/` in the owned C worktree.
The reproducible private source, recipe, manifests, compiler/dependency identities, pre-runtime expected rosters,
actual receipts and Main readback are retained there; generated binaries are not committed.

## Behaviors checked

Owned root sealing preserves the old immutable record root and cached complete bounds. Missing owner,
cancellation, unavailable or overflowing child bounds refuse without publication. Reference construction
retains its previous behavior. New object and shared-control allocations charge only their original owner;
throwing construction and control refusal unwind the private allocation. Dead data refunds its own charge,
while a surviving weak control keeps its original allowance until actual deallocation.

The failed r4 build is preserved in `.build/P1Drafts/OwnedRootQualification-r1-agent/`: C2872 from an
unwanted scheduler enum exposed by an allocator header, and C4702 from a constructor that unconditionally
throws. Main fixes the header dependency with a standard-only ledger bridge and makes the fault constructor
conditional on a real runtime value. Existing tests, warning policy and expected values are unchanged.

## Next integration gates

- [x] Compile and execute complete selected metadata producers and immutable local budget/sealing controls.
- [x] Qualify real scheduler original-ticket capture, cancellation, failed capture, stale refusal and joined Close.
- [ ] Qualify the actual Application caller and all inherited host failure/lifetime controls.
- [ ] Complete native configuration, admission/retirement, ordinary activation and G1 acceptance.

## Complete selected producer core — r3

Release build child **15272**, exit 0, 08:56:51–09:00:45 UTC, zero warnings/errors. Actual DocumentTests
child **35408**, exit 0, 09:00:52–09:01:03 UTC. Every 19,025 predecessor record remains in order, with nine
new budget assertions and summary (19,035 total). Immutable local limits refuse before object construction;
one-way sealing still permits actual destruction refunds and preserves original weak-control lifetime.

Main independently checks all 1,278 private input copies, build/runtime receipts and unchanged executable/PDB.
Source manifest `3b47844c2617b001ea2c8f568bace54f19f734fdc300bece8dda73bb55ff375a`; recipe
`c58f13e8c4cb89526549a0a64140a16588f4c4e3fec25abec9415a515b87038f`; runtime log
`0d2d3a091b786e086349ead1920fb74d0bb8e2704184a3191c24293c659942eb`. Executable
`799142f9a07764a3c66e41d4c1e95508c4cab652b45a1516259ca3b2b14fbb53`; PDB
`a100c054014e9fbc63fcfc0f2a825a4f09d04d7f9b6e2445b77ddaea6c84e8dd`.
Generated packet and Main readback: `.build/P1Drafts/OwnedProducerQualification-r3-Main/`.

The failed r1 build and r2 runtime remain preserved. Main restores the unchanged reference stack layout and
bounds, and its existing intrusive StorageIndex retirement node. Only the explicitly owned producer uses
the new accounted wrapper. All old numeric expectations remain unchanged. This document-core evidence does
not qualify the actual scheduler/Application route or the separate native r951 mixed-reference failure.

## Actual scheduler and mixed reference closure — r7

Release build child **49088**, exit 0, 09:41:05.256408–09:44:35.419224 UTC, zero warnings/errors. Actual
DocumentTests child **21180**, exit 0, 09:44:45.507101–09:44:56.143442 UTC. Main independently verifies
1,282 private input hashes, actual receipts, unchanged executable/PDB and exact ordered predecessor records.
Every 19,035 predecessor remains, plus 1,692 owned metadata JobScheduler/DocumentSession assertions and
summary, and 17 mixed managed/reference history assertions and summary: **20,746 records**.

Original-ticket controls exercise 110 distinct edits with the existing 100-entry cap, retained independent
readers, revision restoration with epoch advancement, cancellation, refused capture, below-headroom admission,
exact original output/temporary partition, actual off-owner retirement, final weak controls and joined Close.
The mixed-history regression preserves literal four-byte sparse-leaf accounting across reference End/Undo/Redo,
nested parent aliases and twelve-byte late-cache growth that trims at the eight-byte cap. No preparation or
accounting query materializes the source; only the explicit late-cache test calls Data.

Source manifest `0153e1263c4da963768df0aee58d9ea38a3a7f245017b07a690d9c7dbc4d71c2`; recipe
`454c7d2325d1dc7afed210695471a3202fee109c998ce849f1113a3d7d2c2114`; runtime log
`1a23afc3af4dc779c8677d2c27944955c393b9731f0a42adbcec26b045ab3a0d`. Executable
`f3a77f5b72ba4ca1255ab61db828b571e748f93ca47e372864cbd3e4a10548f8`; PDB
`494303eb5fdb7f281222df1698cea7237e79db8adeb3cd1aad07291407a9a707`.
Generated packet/Main readback: `.build/P1Drafts/OwnedProducerQualification-r7-Main/`.

Failed r4 compilation exposed fixture API/error-code conversions; Main uses the actual base Job owning pointer
and an explicit signed diagnostic width without suppressing warnings. Failed r5 runtime preserves an unset
same-ticket observation and a final output without a retained reader. Main records actual callback/original-ticket
equality and captures a separate post-publication reader for every output, while preserving pre-edit stale-authority
witnesses and every old predicate. r6 compilation stops on disk exhaustion; r7 uses fresh compiler output after
verified preservation of old generated intermediates. Actual Application and native qualification remain open.

## Actual Application attempts r8/r9 — failed, retained

Both Release builds pass; actual hidden WARP application r8 child 82320 exits 5
(09:52:51.133775–09:53:51.349089 UTC). All 36 original history restore modes pass before
`metadata-host.ordered.complete-retirement` fails. The ordered input intentionally clears its test gate
before younger operations; Main restores only the original accounting observation after all jobs settle.
The live immutable graph continues to own its original charge.

Fresh r9 child **50596** exits 5 (10:07:11.501382–10:07:55.303585 UTC), after the ordered case and all
original metadata failure/cancellation/staleness/residency modes pass, at
`metadata-host.admission.fresh-input-recovers`. Its stdout hash is
`81A26B5BD7B67C0B5021669C9FBDCF362BB650E81F172561D60832C8FB72AAE0`; stderr
`D10ABE1EC76988C4FD7F748291D2CC8599DC9141DF768BF83C52ED027BA6C597`.
Main supplies a nonholding original-ticket accounting observer for recovery and keeps the existing context
observer through cleanup. Predicates, literal state, input ordering and production code are unchanged.
Fresh r10 is compiling; no runtime or native qualification is claimed. Failed packets remain in their
original `.build/P1Drafts/OwnedMetadataApplication-r8-Main/` and `r9-Main/` directories.

## Actual Application r12 — failed, retained

Release build child **98908** exits 0, 11:04:32.086053–11:15:08.200858 UTC, zero warnings/errors;
linked ISA child **70212** exits 0. Actual hidden WARP Application child **36760** exits 5,
11:15:27.419597–11:16:14.258143 UTC. Stdout SHA-256
`7683C7A045DF9BCEC7711733BA7AED58C2668115CDDA16678A1049EE7434056F`; stderr
`6D8620EAEDC71A336435F2B76C620FE170CD7DF37749AB99E909C750F03CA2E6`.

The 162 original metadata assertions, four owned-original-ticket controls plus summary, history-residency,
both opacity suites and brush-opacity's 52 controls pass before the cancellation replacement-stroke case
fails `old-input-retires-once-without-owned-budget`. The fixture proves one successfully recorded opacity
entry remains and then replaces only the brush, so its old global-zero condition is incorrect. Main corrects
its original-ticket settlement and preserves the replacement-document zero condition.

Main's independent lifecycle caller trace additionally distinguishes successful typed closure in live
cases from source-preview retention during held modal document replacement. R13 separately observes both
original tickets and checks their exact remaining charges plus any original live brush opening. Only test
weak observers are released; literal pixels, state, history, receipt and ordinal expectations are preserved.
R13 manifest is `8F88E1DC7A25BB35D11968C10FF58F069EA0B7B31D2BBA094CFE73EA9E4D8A0A`;
its build is running. This is not aggregate or native qualification. Failed r12 source/products/logs remain
in `.build/P1Drafts/OwnedMetadataApplication-r12-Main/`.

## Actual Application r13 boundary failure and fresh r14

R13 Release MSBuild child 79884 exits 0, 11:22:02.117717–11:32:36.711457 UTC, zero warnings/errors.
The linked ISA child passes. Actual Application WARP child 16280 exits 5, 11:32:55.984131–11:33:42.647152
UTC. Stdout SHA-256 is `3DD9AD391BEFD9DA5E85F36D93D7B2E32E4F484F3992F3EA7CC39F8D8499BB37`;
stderr is `594033FFDDF6F8233239A6896BEA0092CC961B9AF4D29072195B7695FCEFA995`.
The failure is `canvas-brush-boundary.tool-held.stroke-and-dependent-receipts-retire-without-budget`.
All prior metadata/ownership, opacity, cancellation, palette, competing and mask-palette suites pass in
that child. Later lifetime suites do not run; no aggregate qualification is claimed.

Main reads all twelve held/live/refused Tool/Layer/Mask/Save branches. Their final guard explicitly requires
`UndoCount == 2` and literal `.25` opacity plus accepted brush pixels; neither live history entry is destroyed.
R14 corrects only the global-zero assumption, requiring a bound original metadata ticket, positive immutable
output bytes and exact temporary/endpoint settlement. All prefix/capture, receipt, pixel, saved-file and
history expectations remain unchanged. The private frozen manifest is
`266B8C771E20909C0E29136DD5CD9CFB05BBC974BEA7F73FA9E1E53AB3BF0DAA` (1,282 inputs).
R14 is building; its independent full predecessor/new-record readback remains due. Native source and ordinary
activation are unchanged. Native profiles, complete costs, H3/G1 and P1 remain open.

## Actual r14 later authority failure; reference acknowledgement and save correction r15

R14 MSBuild child 43960 exits 0, 11:38:53.975300–11:49:16.381923 UTC, zero warnings/errors; ISA passes.
Actual WARP Application child 48104 exits 5, 11:49:40.473917–11:50:30.001543 UTC. Stdout hash is
`72F0C954637FB8D29F4DCE7A17EE5E8E3D5598812566143F7D10893BD2FB89C5`; stderr is
`944541CD7C9BAA1BD6BDF1E8A9C5A482B9D4FF6B80A67B4B9845688C25336231`.
All twelve boundary cases and complete cancellation/accepted-close/modal lifecycle suites pass. The authority
suite stops at `core-live-save.all-input-and-worker-reservations-retire`; later cases and project-save do not run.

Main observes direct reference `FinishBrush`, composition EndStroke and preview release occur after that
branch's last settlement. `JobScheduler::IsIdle` includes memory retirement/trim/releasing acknowledgements
and pending owner work; the final test helper refuses before draining when not idle. R15 pumps and settles
this actual reference retirement before preserving the same exact metadata/preview residual equation.
Failure-only diagnostics expose residual, releasing, expected source/output, idle and input state. No helper,
production operation, authority, pixel, receipt, history or shutdown-zero predicate changes.

The cheaper agent's complete 207-translation-unit opacity/reservation sweep identifies only Boundary and
PendingOpacity Save as conflicting global-zero assertions. Main directly verifies the latter's saved `.5`
opacity and retained live history. The isolated save correction installs an unheld observer and verifies
current original ticket provenance, immutable output/local bounds, expired temporary and endpoint balance.
Preview and save keep their original ordering; twelve non-opacity modes retain explicit zero.

Frozen r15 manifest: `32DDBFDA9A2CDDADA588C933160DD6A51BF4C489E9184CA41E5E97B8C8FD25EE`,
1,282 inputs. It is building; independent complete predecessor/new-record qualification remains due. Native
source/contracts and ordinary activation are unchanged. Every P1 delivery gate remains open.

## Actual r15 cancellation accounting failure and fresh r16 fixture correction

R15 build child 93652 exits 0, 11:54:24.611367–12:04:46.581835 UTC, zero warnings/errors; linked ISA passes.
Actual WARP child 95984 exits 5, 12:05:05.516071–12:05:52.834796 UTC. Stdout SHA256:
`C4AA3F07C0BB4BC3588FF74456EFBC02BE79C50BE2D6BC34D1CCB095BB3F1863`; stderr:
`6D28C390F925A81BDE6CACD7CD40A83E5DCF05B88387F322362F9426D5DBE643`.
The reference-retirement correction passes; failure diagnostic is
`Held OK pixels escaped cancellation or a younger rename crossed close acknowledgement.`

Main traces the guard: after real Cancel acknowledgement a younger rename must succeed, change the revision
and retain its single undo entry, yet the old guard requires zero global reserved memory. R16 observes that
rename's unheld original-ticket publication, positive immutable output, expired temporary and settled endpoint
balance. Literal source pixels, canceled OK, repeated Cancel identity, younger ordering, revision change and
exact Undo-only-rename document equality are preserved. Other cancellation/failed/cleared graph zeros remain.
No production operation/helper/native source changes. Manifest:
`CDA6A5561E8D4CDBCDB5C4577FB8B1043C56EBDEC94461ED4FA2DFA5EFCD8DF3`, 1,282 Source inputs.
Complete runtime and independent original/new record qualification remain due.

Main rejects all four additional audit findings as stale duplicates: current r15 private source already uses
original-ticket accounting for metadata general/recovery/context and ordered history residency. Their actual
stage records complete before this failure. Audit is retained at
`.build/P1Drafts/MetadataAllKindZeroAudit-r1-agent/AUDIT.md`; it supplies no new qualification evidence.

## Actual r16 later opening failure and fresh r17 correction

Build child 98524 exits 0, 12:14:50.046978–12:24:04.805166 UTC, zero warnings/errors; linked ISA passes.
Actual WARP child 44848 exits 5, 12:24:23.001695–12:25:12.199646 UTC. Stdout SHA256:
`61F255A09E9C562E31345AD38587318FD12BF2C263D6D9DA0198DC6A8EDF9AB9`; stderr:
`520FB6744EC22AE0F0246C2586664A80A106FCC766F337476074050D572FB74A`.
All color-cancellation checks pass, including all four held-OK/rename cases and their unchanged Undo equality.
Adjustment opening Ordered passes its four copied/weak lifetime controls. Younger then fails its terminal guard.

Main confirms Younger closes the panel and then successfully renames, retaining one undo entry; the old guard
requires only panel bytes (zero here), omitting the metadata output. R17 observes the unheld original ticket and
requires its positive immutable output, expired temporary, settled endpoint balance and exact residual equal
to the retained panel bytes. Eleven other modes keep their old exact panel/zero equations. No production,
helper, pixel, history, visibility, receipt, hidden-rendering or GPU predicate changes.

Main scans all seven current frozen metadata-action / reservation intersections, including Jobs.cpp embedded
fixtures. General metadata, history residency and color cancellation are already corrected; Application and
Files matches are production dispatch/diagnostics. No further uncorrected matching fixture remains in that
bounded syntax sweep. It does not infer acceptance for later unexecuted suites.

R17 manifest `EB7E0F587D34D2E107F6DE196C1AA3CE1ED8E9EF28F24386DDB4037FBC5635F0`, 1,282 inputs.
Complete runtime and independent record qualification remain due. All nine P1 delivery areas stay open.
Closed r14/r15 generated intermediates are preserved with per-file hash equality and old-path junctions at the
explicit P1 artifact archive; no evidence was deleted and no native source changed.

## Actual r17 early shutdown timeout and fresh r18

Build child 92136 exits 0, 12:31:15.612498-12:40:55.399749 UTC, zero warnings/errors; linked ISA passes.
Actual WARP child 62016 exits 5, 12:41:14.065928-12:41:29.555190 UTC. Stdout SHA256:
`6672B4B8AB196F0DA3EB62EBC64228E4F3894F0B2F46FFB7844C5601AC203EA1`; stderr is empty.
Last record is `canvas-brush-lifetime-shutdown.stream-held.typed-opacity-preview-before-original-down`.
The driver then times out before its held original metadata gate; no state diagnosis was emitted.
The r17 Younger opening repair never executes, so this result does not qualify it.

The standalone driver precedes RunSelfTest's keyboard scope and calls SamplesColor/CanDeferCanvasBrush,
which read KeyboardState. Testing_Validation already requires each nested driver to own ScopedSynthetic.
Main adds that existing all-up/restore scope in r18 plus failure-only gate-state diagnostics with fflush before
the unchanged _Exit(5) deadline. This is a verified harness contract defect; physical Alt as the actual
r17 timeout cause remains unestablished. No production input or visible benchmark input policy changes.
All event, pointer, original-ticket, pixel, history, receipt, lifetime and exact-zero controls remain unchanged.

R18 manifest `F9E23C53989EAADDEBA38C19871BE6ED7C45DAF95C8E61E8F9885A2C695DB6C3`, 1,282 frozen inputs.
Selective integration is gated on its complete actual Application PASS and independent readback; pending.
P1 remains WIP.

## Actual r18 sizing-baseline failure and fresh r19

Build child 27088 exits 0, 12:57:28.895032-13:07:53.591983 UTC, zero warnings/errors; linked ISA passes.
Actual WARP child 93880 exits 5, 13:08:41.677939-13:09:32.659781 UTC. Stdout SHA256:
`60BB2ED8100E7DD1E93BBDBE6269E6B692A8FA6B1EC4BD7C4106D8B3C31C267D`; stderr:
`ABB3BE86B89B9310A96ED94897A003A3AAA21AB7E38AD040199AF459E926784D`.
Earlier repaired shutdown/opening/cancellation fixtures and all 22 sizing modes pass. Final
`sizing-clipboard-retirement.actual-private-replacement-keeps-baseline-reader-charge` fails; the unchanged
last-baseline-reader release/refund check passes. No failure component values were logged.

Main confirms the final reference Copy/Clear can defer real metadata destruction and acknowledgements through
AccountedPublicationObject and DocumentHistory reset. R19 keeps the original copied clipboard reader held while
requiring SettleJobs to consume those events before observing the same exact baseline. Every literal pixel,
origin/size/serial/source identity, empty document/history and original baseline/final-zero value stays unchanged.
It adds failure-only component diagnostics, with bounds before literal diagnostic reads. The exact r18 failure
component remains unestablished; this is a corrected observation boundary, not a rebaselined expected charge.

R19 manifest `4BBE1A908C9C706F37F47329E5B81893DCFF6DCDE1D03504F0C9058C88D67617`, 1,282 inputs.
It requires complete actual runtime and independent record qualification before selective native integration.
Its first preparation stops at the explicit 4 GiB free-space preflight before making a candidate directory.
Closed r16/r17/r18 generated intermediates are then preserved with all 336 files per packet hash-equal and
old-path junctions in the established P1 archive. No evidence is deleted or active compiler/process terminated.
Fresh preparation succeeds; no runtime retry is hidden. All nine P1 delivery areas remain open.

## Actual r19 later save-admission failure and diagnostic r20

Build child 89544 exits 0, 13:19:13.267296-13:29:31.326730 UTC, zero warnings/errors; linked ISA passes.
Actual WARP child 86296 exits 5, 13:29:58.560930-13:31:12.893292 UTC. Stdout SHA256:
`752BC15C6D5165DCD9C21481113B5022130F93C69F0B47C3B49AB325C21770A7`; stderr:
`4801617333B716C1E779D5303181D317A52B0A5F0ED69CC2F83822518906990B`.
Sizing replacement/last-reader retirement now passes with the original literals, baseline and zero refund
expectations. Save cases0-10, including PendingOpacity, pass. Admission case11 then fails its compound guard.
The failed component is unlogged and its cause remains unestablished.

R20 changes only failure diagnostics before that unchanged failure return: actual outcome/error, held/visited
state, terminal tickets/ordinal, baseline memory/idle, path/revision/modified/error identity and original metadata
plus prepared display/row state. No predicate, literal, error code, timeout or production behavior changes.
Manifest `B1CBE27A076708E64EBCA5623CD0ECC0B99C8D0A0E2397B0A4EFD2B4697DB422`, 1,282 inputs.
Complete actual runtime/readback remain due. Native integration and all parent delivery gates remain open.

## Actual r20 exact retained-charge failure and r21 isolation repair

Build child 62852 exits 0, 13:40:26.158398-13:50:02.063693 UTC; linked ISA passes.
Actual WARP child 102792 exits 5, 13:50:39.770726-13:51:31.409482 UTC. Stdout SHA256:
`459728CE8CA7CE43976DD7E102223BF9B806375A5C728B0E92235007BCAC36CA`; stderr:
`8090FF89E058893590BDD1610F28B105189A0721E3C55CA03406CFABA7B6FF44`.
Case11 reports settled/idle, exact Failed/E_OUTOFMEMORY, no visit/publication, retired tickets/stamp/barrier,
zero input charge, unchanged path/revision/modified state and one correct localized error. Accounting alone
fails: reserved=114664, releasing=0. Display/rows are disabled; the previous metadata gate is absent.

Code inspection supplies the retained graph: CreateDocument calls BeginEdit/EndEdit, preserving the preceding
PendingOpacity document in Undo. The per-case comment claiming graph replacement retired the old fixture was
incorrect. ClearDocument resets document, editing sessions, display copy and history. R21 uses that existing
API, consumer refresh and SettleJobs before fresh fixture admission, then requires no document/Undo/Redo and
exact zero memory. No production behavior, per-case literal/output/history/error/receipt or timeout changes.
Source review then identifies the newly authored nonexistent RedoCount API in this cleanup; the current
actual build is retained. Its replacement must use existing CanRedo with the same empty-history expectation.
R21 manifest `3E4BBA3F09D207057C5C060D1C05AE9C461214C6FC520B57ABD4513168BE498A`, 1,282 inputs.

Closed r19/r20 intermediates are preserved in the existing archive with identical old-path junction bytes
(336 files per packet). No source, log or evidence is deleted; no active process is terminated.
Complete application qualification, native integration, measured phase/caller costs and P1 remain open.

## Actual r21 compile refusal and corrected r22

Build child 86932 exits 1, 2026-10-12T13:59:10.311088+00:00 to 2026-10-12T14:07:59.377962+00:00.
C2039 refuses the cleanup's two RedoCount references (no such DocumentHistory API); no ISA/runtime starts.
The single compiler warning and both errors are preserved. Stdout SHA256 `AD8838C5160134022E73FF01BCFD1FA1FEF6E33EAC73A2CF28304AAB4FBFA36E`.
R22 uses CanRedo for the same empty-history guard and diagnostic. Its freshly frozen 1,282-input manifest is
`7341C1AA62B3335098F857BB204416C00D35897175B6F490A9A44CA1D362D60F`; candidate save source SHA256
`193113B832C9C90500F1C5C6EEA10D3E4BCCB52E7107011F00DA8F50150A0CE5`.
Full actual Release/WARP qualification is running; private/native integration remains gated. The prior
114,664-byte charge is not waived: each new case first requires complete prior-fixture retirement and zero.

## Actual r22 successful compilation, later ISA refusal

Build child 65800 exits 0, 14:09:13.625942-14:20:25.912920 UTC, zero warnings/errors.
Build stdout SHA256 `F5AC8C18D3EADF316931EE9A2F2529E414534DB3EDCB994A7A6B5961FDA826BB`.
ISA child 70832 exits 1, 14:20:42.151777-14:20:57.710680 UTC; no application runtime launches.
The two refused rows, 0x1404605b8/0x1404605dc, decode compiler DWORD target plus adjacent byte-map data as
vpshufb. Anonymous same-owner PDB symbols, exact paired R15 guards/R9 producer and decoded branch destinations
identify these as data. Main authors a narrow private complete PE/PDB/listing/data-range proof; its actual
saved-image scan passes without changing any artifact or the original rejection. Synthetic mutation controls
and native adoption remain due. No allowlist, DxUi pin/library/source or executable code is changed.

Main also drafts a cached immutable save-planning projection in a separate private packet. It moves the exact
old per-record reserve formula to root Seal; planning reads cached scalars in constant time before admission.
Independent old-loop reference tests, full private/runtime/native proofs and measurement remain due.

## Actual r22 application after the private scanner correction

The original successful build and ISA refusal are preserved. A new rescan/runtime packet explicitly reuses
that unchanged C++ source, dependency closure and executable; it claims no new compilation. Build receipt
SHA256 is E9BBEA30074BE5D73F53CF04F987E2E4BC14722DCB2B43BC513F5E0106BAA737;
executable SHA256 is 4A17FED2A705E2B33E5C3F36828A5CFF41DC2766A46A75A57604C8BA6560FB19.
Private actual scan proves the two rows are table data; the original 488 executable vector instructions stay classified.
The allowlist and DxUi pin/source/library do not change. Synthetic scanner mutation proof remains due.

Actual inherited-desktop WARP application child 105936 exits 0, 2026-10-12T14:46:22.349747+00:00 to
2026-10-12T14:47:40.388088+00:00. Stdout SHA256 F981132530608835E8B3334ED0322088EF42BEF96409861E110195FF8BD7F330;
stderr FAE9F386C2186C79EDB71991AAD8D769722AB6E07A6A2025AE2FDEC016002225.
Main's independent ordered reader passes all 133 stages, 7,410 stdout and 7,613 stderr records, preserving every
predecessor record and the exact five owned metadata controls. Source/dependency/product bookends remain equal.
Report 4B479CF588E3F779B8DD6D1D27A2ACA8CDA8A4BBC9DBB91065325F1C71496E98,
`.build/P1Drafts/OwnedMetadataApplication-r22-Rescan-r1-Main/actual-rescan-r1/qualified.json`.

This establishes the explicit independent save-fixture retirement correction with all original assertions.
It remains private Release/WARP evidence. Native scanner adoption/configuration gates, complete caller and
consumer phase bounds, ordinary responsive activation, G1 and P1 Done remain open.
