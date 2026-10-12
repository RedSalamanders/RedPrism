# P1 selection offset — private worker engine qualification

## Native exact-report qualification — 2026-10-12

- [x] Authenticate the three native full-test reports and Release analysis archive audit against the four profile references in `Matrix-r7/completed.json`; each report's SHA-256 matches its matrix reference. All four bind 1,253 inputs, source freeze `0BCC656C40903E49EE579476BD03C014031568A391D5B5C8B16894C14C9C4FCC`, and source binding `D69F9A3E11FFA6622C8C66D65D5C7E0EFB7890690F48197252DF9EE45FE84E12`.
- [x] Release and Debug each retain Host 8,940, Document 9,204, Composition 6,838, I/O 1,823, application stdout/stderr 7,399/7,556, and 131 stages; all predecessor suite assertions are retained. ASan retains Document 9,204, Composition 6,838, I/O 1,823, application stdout/stderr 7,399/7,556, and 131 stages, with Host 8,941 plus DocumentStress 1 and HostStress 9,339 sanitizer records.
- [x] Release r922 PASS — PID 68012, exit 0, 2026-10-12 01:00:07–01:15:45 UTC; stdout `D376C8AAFF15796A8FBEBCE3D9FC31179CC9EC4CA6EE7DDC8393517D59DF3011`, stderr `DC407A299ED496DD5604C7E750D7380323B079CE3D55BF0740FFA217F6D65956`; report SHA-256 `310784E3EA010626796CE056D54DC84C8A07441D3E68652BE901538BA0AB0B0A`.
- [x] Debug r923 PASS — PID 68920, exit 0, 2026-10-12 01:21:41–01:42:25 UTC; stdout `FC06AA1FB652EA57DE96DC89A33C13FEB94102A0924D7042A669848779E061CB`, stderr `E8D22350FE96DBA7C23814FB768423E443CE3E2B96A005875D99D7BCFA83076B`; report SHA-256 `32C35AB0918A827B9DA211A376EDB00E1763E45ED48B36AF1F1BEDB0E696EB07`.
- [x] ASan Debug r924 PASS — PID 13480, exit 0, 2026-10-12 01:43:01–02:10:15 UTC; stdout `82086AE580F9965D25BD4F179AB57EDAA773E09235644A2EE8B8475CC695002B`, stderr `D7C6FB297966F1E20EAF32F4579C2D807882DAB7D023F3CDEDBFF3F4BC8D348C`; report SHA-256 `0DAA99FCB9CF6F750AFA7008B041915171732DC6DD92FF1697501C48DDF979C5`. Its normal suites report Host 8,941; Document 9,204; Composition 6,838; I/O 1,823; application stdout/stderr 7,399/7,556; and 131 stages. The additional sanitizer records are DocumentStress 1 and HostStress 9,339. Release and Debug normal Host count remains 8,940.
- [x] Release analysis r925 PASS — PID 68072, exit 0, 2026-10-12 00:45:58–00:59:45 UTC; stdout `849497420E7445C22616A57A0715CD4BE2BDA67A45C69D85120E6051F6DDBA92`, empty-stderr SHA-256 `E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855`. The bounded archive audit verifies all source evidence copies; completion SHA-256 `5D323678A1E16E34F11E1849E7C87B2C1183913A66C12BA71087B71CBFD099DB`; report SHA-256 `7688FC0E1A303B7DF6BF5F9317D11C80D9F9C53179CAFF8DBC9D96CA4F7DA3F3`.
- [x] Qualify all three ARM64 r104 cross-builds: Debug child 87576 exits 0 at 02:17:55 UTC,
  Release child 58928 exits 0 at 02:22:24 UTC and ASan Debug child 61572 exits 0 at 02:25:15 UTC.
  Main's exact readback verifies every archived source/evidence copy, 15 ARM64 PE/PDB GUID/age pairs and
  five real sanitizer compile/link controls. Report SHA-256
  `F1E5E23D9CEEFFBC2F934745545C0328F518C6E8462B3AE315A1AA1464DDDD08`;
  corrected reader SHA-256 `64CCFDB607C003CD037CB73E1AFFFBA9B96229341FD4D8810078F6C512B57C96`.
  Native ARM64 runtime is unavailable.
- [x] Preserve runner-only refusals: the first controller expected non-ASan counts on ASan, r103's
  process helper refused already-created logs before launching a compiler, and the original r104 reader
  selected the old r6 folder rather than actual archived r7. Main corroborates the exact path and corrects
  only that selector while retaining the original archived reader/review pins and every artifact predicate.
  Failed reader receipt `0DDE989A…` and stderr `366F42BB…` remain separate failed evidence.
- [ ] Commit/push the qualified native step. Ordinary startup, outline, G1 and overall P1 qualification
  remain open; this evidence does not mark P1 Done. Earlier failures and history below remain part of the record.

## Current checklist

### Fixture-only repair and actual analysis r917 (00:10 UTC, 2026-10-12)

- [x] Move only the reference `DocumentSession` fixture to `unique_ptr`; preserve every source golden,
  semantic predicate and operation order. Genuine pre-runtime binding
  `DDEF8180236439829ABCD2EDA1D4668C7893020CAAAE07CD26031CE1CB781FA8` records 1,252 inputs and
  the same 20 contracts. Original `AF22B204…` runtime reports remain predecessor evidence.
- [x] Preserve actual analysis r917 (child 43476, exit 1, 23:53:22–23:58:58 UTC) and 1,651 read-back copies.
  Compilation and both SAL controls pass; the overall gate fails at the linked instruction scan.
  Archive completion `08C8C7D35CDFFA3E658EFDC9A2A2C476B224E95894E663D0FE2AC8CDFEDB2E2A`.
- [ ] Qualify the private exact RSI64 terminal-table recognizer against all original controls, independently
  encoded new refusal controls and the actual source-bound PE/PDB before integration. The apparent
  `vfmsub132ph` starts at `0x14029beac`, inside the anonymous 80-byte table `0x14029bea8`–`0x14029bef8`,
  after RET/three-byte NOP. Full-width CMP/JA and RCX/RSI dispatch are corroborated by the PE and PDB.
- [ ] Integrate only the qualified parser change, then run fresh actual analysis, full Debug/Release/real
  ASan and ARM64 cross-builds. The stopped r918–r920 do not provide runtime evidence.

### Native ASan r916 and analysis r914 (23:49 UTC)

- [x] Qualify full native real ASan r916 on the unchanged 1,252-input, 20-contract binding
  `AF22B204…`. Actual child 85828 exits 0
  after `2026-10-11T23:13:21Z`–`2026-10-11T23:39:13Z`; all original assertions, 92 Document and 433
  application additions, 131 stages, and the real negative sanitizer probe pass. Archive completion SHA-256
  `E43692BC7878ECBC033D1C982CECD1731F1A8A0EE07C26A1BE8112DCE6BC5DE4`; qualified report SHA-256
  `767AF14C9E809587254802DEE49F82FF8148A5B74E9BB050EDEFE8309D814D0C`. The exact freeze records 1,252
  inputs and 20 contracts. Previously qualified Debug r912 and Release r911 remain on this binding.
- [ ] Retry full Release analysis after Main's fixture-only repair: actual analysis r914 child 52552 exits 1
  at `2026-10-11T23:44:34Z` because C6262 reports an 18,428-byte stack frame at
  `Application.Tests.SelectionOffset.cpp:29` (warnings are errors). Archive 1,641 copies are read-back
  verified. Main moves the test fixture's `DocumentSession` allocation to the heap while preserving the
  same semantic assertions; the changed source requires fresh validation.
- [ ] Run the three ARM64 r102 cross-builds only after fresh analysis qualifies. No ARM64 build launched;
  the queued gate stopped.

S5 sidecars: corrected Release geometry probe passes (compile child 88864, runtime child 28284, exit 0;
source SHA-256 `9F73BC26`). The separate-factory draw returns `D2DERR_WRONG_FACTORY`. At 2048x1024 on WARP,
first/warm geometry observations are ~68.9/~72.4 ms, diagnostic only; no G1 result. WIC probe compilation
child 88632 exits 2 on a Win32 macro conflict; the failure is preserved, Main renames its local helper, and
runtime remains unrun.

- [x] Verify the exact Clipper2 2.0.1 source hashes and legacy Expand/Contract tuples.
- [x] Prepare a separate bounded offset engine with complete normalization, round offset, cleanup and positive-only canvas clipping in one fixed scratch arena.
- [x] Review the cheaper agent's numerical fixtures; preserve its draft and correct the C++ newline/initializers, failure return and legacy two-step contraction sequence before compilation.
- [x] Freeze 836 private inputs and 55 independent numerical/fault records before Debug r1.
- [x] Qualify the private Debug engine: explicit r3 PASS covers all 9,112 predecessor assertions plus 55 exact additions (9,167 total); no test rebaseline.
- [x] Qualify Release r4 against the authenticated native Release r907 inventory: all 9,112 predecessor records plus 55 additions pass. Preserve the earlier reader's Debug/Release mismatch refusal; no tests change.
- [x] Qualify real ASan r5, including its actual sanitizer detection probe and all reached allocation/cancellation/capacity refusals. Actual child79716 exits0; exact reader qualifies the unchanged run against native r909. Debug engine counters: 289 allocations, 64,976-byte workspace, 4,426 work units, 56 points, 1 loop.
- [x] Qualify the immutable publication overlay in Debug r6, Release r7 and real ASan r8: all9,112 original records plus87 cumulative additions pass (32 further numeric/history/fault records).
- [x] Prepare and privately qualify settled offset caller admission, immutable publication, original-ticket output/history/outline retirement and ordered host input in all three profiles before native integration. Other selection consumers remain separate.
- [x] Qualify opt-in scratch reuse and the fixed 32,768-square Contract case in Debug, Release and real ASan, then repeat the complete private host proof. Earlier monotonic-engine qualifications do not cover this repair.
- [x] Qualify repaired CPU Debug r18: all 9,112 predecessor assertions and 92 additions pass (9,204 records), including the fixed large-grid reference within the original scratch cap. Release r19 is running; repaired ASan and host profiles remain due.
- [x] Qualify repaired CPU Release r19 on the same 843-input freeze: all 9,204 records pass; the large Contract case uses 105,694,528 scratch bytes. Real ASan r20 is running and its genuine negative detection control passes.
- [x] Qualify repaired CPU real ASan r20, including all 9,204 records and the genuine detection control. The three-profile repair summary binds each successful report; older monotonic-engine reports remain separate.
- [x] Qualify repaired actual application Debug r21: all original 7,399 stdout/7,123 stderr assertions, 128 stages and 433 additions/three stages pass. All nine fixed large-output controls pass.
- [x] Restore matching Release artifacts after r22's pre-compilation library/provenance refusal. Actual build-only r915 and its independent identity reader pass with the unchanged pin/source; this does not qualify runtime or analysis.
- [x] Qualify repaired actual application Release r24: all original assertions and 433 additions in 131 stages pass, including every large-output control. The prior r22 refusal remains separate.
- [x] Finish repaired real ASan r25 host qualification before native integration: every predecessor and 433 additions pass in 131 stages, with the genuine sanitizer detection probe.
- [x] Integrate the qualified 39-file native change, retain the old failed attempts and capture the new exact source/contract binding after formatting and skills checks.
- [ ] Retry Release analysis and qualify all ARM64 cross-builds before committing this step. Ordinary activation and G1 remain separate.
- [x] Qualify full native Release r911 without changing source/tests after the exact LF/CRLF snapshot representation is authenticated. Preserve the first controller's digest refusal separately.
- [x] Qualify full native Debug r912 on the identical binding: every original assertion, 92 Document/433 application additions and 131 stages pass. Real ASan, analysis and ARM64 remain due.
- [x] Preserve actual native ASan r913's compilation failure and all archived source/product/log copies, including an independently copied and read-back compiler PDB. This failed attempt is not a runtime qualification.
- [x] Qualify fresh full native ASan r916 on the unchanged source binding; see the exact report and archive hashes above.

### Native ASan r913 compiler failure and unchanged retry (23:15 UTC)

Actual test child 2964 exits 1 after `23:01:17.686053Z`–`23:04:07.990977Z`. Compilation fails at
`ApplicationShutdownTests.cpp(1318,1)` with C1090, PDB API error 23 / `0x00000006`; no runtime
suite or sanitizer detection probe runs. No cause is established. The original matrix stops rather
than treating the failure as a pass or continuing to analysis.

Archive `P1-SelectionOffset-ASanDebug-r913-test-20261011` reads back all 1,614 copies. Completion SHA-256
is `3688FCE68BF7516FBEBD2D20CA6F67B790D1948144CF6C89134C500653852059`. A separate preserved
51,965,952-byte compiler PDB reads back with SHA-256
`4A766EF789B949BA9813974B7F260B761AAEE2854B29CAF8E69B8B7447938F4B`.

Fresh forced full ASan r916 runs on the unchanged 1,252 inputs, 20 contracts and genuine `AF22B204…`
binding. Compiler flags, source, tests, thresholds and dependency pin are unchanged. The successful
same-source Release r911 and Debug r912 reports remain authenticated. Analysis r914 and ARM64 r102
remain required; the reviewed ARM64 protocol changes only its failed-r913 gate to the fresh-r916 gate.

### Full native Debug r912 (23:02 UTC)

Actual test child 10676 exits 0 after `22:41:58.149868Z`–`23:00:39.864829Z`, on the same 1,252 source inputs,
20 contracts and genuine `AF22B204…` origin. All original suites, ISA/parser/refusal controls, crash probes,
WARP application, benchmark/parser smokes and harness captures pass. The exact reader authenticates the
already-proved LF/CRLF snapshot representation and preserves every predecessor expectation and order.
Counts are Host 8,940, Document 9,204, Composition 6,838, I/O 1,823 and application 7,399 stdout/7,556 stderr,
with 131 stages. The native reader verifies 63 short-parser, 132 long-guard, 59 RBX-disp8, sizing and cleanup controls.

Archive `P1-SelectionOffset-Debug-r912-test-20261011` reads back all 1,661 copies. Completion SHA-256 is
`C944DA31CEF0350CC0EC588FC5B7AD0D24A37D408A858965D1F5574B290CF08F`;
qualified report SHA-256 is `D3F1DE33D6DA2D545086E916D764344F4B915306912D26517BE9063EEFC0BF91`.
Real ASan r913, Release analysis r914 and dedicated ARM64 r102 remain due. These are functional validations;
no S5, ordinary activation, measured G1 or P1 Done claim follows.

### Full native Release r911 (22:42 UTC)

Actual test child 82124 exits 0 after `22:21:50.062999Z`–`22:36:28.532236Z`. All tests, ISA controls,
crash probes, hidden WARP application and harness screenshots pass. The initial controller stops before
archival because the authoring snapshot hash (`1F387831…`, LF) differs from the actual runner snapshot
(`4EF66CF7…`, CRLF). The two raw manifests and refusal remain preserved.

Main proves exact LF-to-CRLF bytes, identical ordered JSON rows, unchanged file lengths/hashes for all
1,252 inputs, unchanged 20 contracts and the genuine earlier binding `AF22B204…` (created 22:21:04 UTC).
Projection receipt SHA-256 is `CC428781CD88AFD6B707AF26A7EBBA3E7B71968F9BBAD6E8BC56ADC72EAB9D58`.
The corrected reader authenticates that receipt and the exact archived native snapshot bytes while retaining
every source, runtime, parser, sizing, cleanup, predecessor and assertion-order predicate. No code, tests,
thresholds or origin timestamp changes; no rebuild follows from an encoding-only metadata mismatch.

Archive `P1-SelectionOffset-Release-r911-test-20261011` reads back every copy; completion SHA-256 is
`077445EDF90803AF999E5DED54147B435E71218DAB244E01FF132C9D9E9AE65C`. Qualified report SHA-256 is
`A40E03E7C3E6E496EB85EDD145B8E0BEF7D47DC1989A625E1669E7F80E70EE7A`. Counts are Host 8,940,
Document 9,204, Composition 6,838, I/O 1,823 and application 7,399 stdout/7,556 stderr in 131 stages.
Fresh forced Debug r912, real ASan r913 and analysis r914 remain sequenced; ARM64's own original snapshot
stays exact while its native x64 gates authenticate the proven CRLF representation. P1 remains WIP.

### Release artifact repair r915 (22:00 UTC)

Actual build child 22228 exits 0 at `2026-10-11T21:59:30.585069Z` after starting at 21:49:54 UTC,
on the unchanged 1,228-input native freeze `6354B916…` and 20 contracts. The repaired Release
provenance names the actual linked library hash `7C0DDD56…`; provenance SHA-256 is `7297BFBC…`.
The independent reader `AF87A74D…` authenticates the actual logs, clean pinned dependency source,
module bytes, native source/contracts and every retained Debug private input/product. The dependency
pin remains `8e8994079e82f79e6765e6a885c03fc687957591`, API revision 3. Preserved pre-repair copies
remain exact. This is artifact repair only; the earlier analysis failure and r22 preflight refusal
remain recorded. Fresh host Release r24 and ASan r25 are launched in that order on the unchanged
1,237-input private host r4 candidate and original runner/reader protocol.

### Repaired Release host r24 (22:08 UTC)

Private build child 89744 exits 0 with zero warnings/errors at 22:06:44 UTC. Actual application child
41928 exits 0 after `22:06:59.325322Z`–`22:08:17.539762Z`. The unchanged exact reader `C719C296…`
qualifies all 7,399 original stdout and 7,123 original stderr assertions, plus 433 additions, in the
original 128 stages plus three new stages. Source/dependency and product books remain exact; before-book
SHA-256 is `20022294…`. Real ASan r25 follows on the same private candidate. These receipts do not qualify
native activation, first-paint Direct2D construction or G1.

### Repaired ASan host r25 and native integration (22:22 UTC)

Private ASan build child 41400 exits 0 with zero warnings/errors at 22:11:54 UTC. The genuine negative
control child 9520 exits 1 with the required heap-use-after-free diagnostic. Actual application child
81232 exits 0 after `22:11:59.117808Z`–`22:19:17.029940Z`. The exact reader qualifies all original
assertions and 433 additions/three stages; source/dependency and product books remain exact. Before-book
SHA-256 is `46EA81CC…`; successful report SHA-256 is `D4372F57…`. The three-profile host summary binds
Debug `CD34C1EE…`, Release `2B6C859E…` and ASan `D4372F57…`.

Main's gated native integration applies 39 files after verifying all CPU/host reports, source books,
artifact books and historical engine/core reports. The canonical formatter changes none of the prepared
C++ bytes. `format.ps1 -Check` checks 1,106 files; all 15 skills validate. The new native snapshot contains
1,252 inputs and 20 contracts, source-freeze SHA-256 `1F387831…` and binding SHA-256 `AF22B204…`.
Forced native Release r911, Debug r912, real ASan r913 and Release analysis r914 are sequenced; all
ARM64 r102 builds remain behind their exact successful reports and independent archive readers.
The cheaper ARM64 draft is retained but not activated; Main adapts the existing dedicated ARM64
controller, preserves its PE/PDB and sanitizer predicates, adds forced rebuild and freezes its gate reports.
This is integrated code awaiting native qualification, with no commit, G1, ordinary-route or P1 Done claim.

## Historical initial prepared scope

This is an isolated worker-engine candidate under `.build/P1Drafts/SelectionOffset-r1-Main` in the
`codex/p1-opacity-host-proof` checkout. Native source remains frozen at the Inverse/scanner r3 binding
`2B0DD89C7B208183729ADD25FD8515EEAF4C239FD5F74BC84FD2234F0F67AAD9`.
No ordinary application offset route has changed.

Normalization retains `ClipperD(2)` with binary scale 128 and NonZero winding. The round offset retains
`InflatePaths`' decimal scale 100, miter limit 2 and arc tolerance 0, with the original integer cleanup union.
Positive amounts clip to the canvas through the original precision-2 intersection; negative amounts do not
add that clip. The signed command amount is bounded to ±1–500. All internal scratch uses the existing fixed
arena, including containers and temporary paths; final ordinary vectors require separate caller admission.
Generated source preserves the upstream copyright and license. This preparation proves no latency deadline.

The 35 numerical records include literal bounds and coverage from
retained numeric golden cases: Expand5 on a 20-pixel rectangle, Contract8 on the expanded
result, full-canvas Expand10, Contract10 edge/center coverage, then Contract45 yielding empty geometry.
Twenty-four ordered-vector comparisons use the unchanged pinned reference for disjoint, holed, concave and
fractional inputs at six signed amounts. The 20 fault records cover invalid inputs, output ceilings, every
reached allocation prefix and 64-unit cancellation checkpoint, smaller 64-byte arena capacities, unchanged
source geometry and successful reuse after refusal. No result is inferred from the expected roster.

## Actual private qualification history

Receipts below are from the isolated private profile. Failed attempts remain in the record; Debug r3 and
Release r4 have exact-record qualification receipts.

- **Debug r1 — genuine compile failure.** Build PID 21824 exited 1, `2026-10-11T19:45:00.958325Z` to
  `19:47:42.499736Z`. The compiler reported undeclared `kSelectionModifyLimit` at
  `BoundedSelectionOffset.cpp:68` and failed default construction of `ClipperOffset::Group`
  (`std::construct_at`, C2672): three errors, zero warnings. Build stdout SHA-256:
  `E734BE0D7B6BCDDC21F84785F173DB44F1095C172670A922D33032B2505D0512`.
- **Debug r2 — genuine runtime failure.** Build PID 80760 exited 0 (`19:49:30.061115Z`–
  `19:52:16.090665Z`; stdout SHA-256
  `73E47A88FEBFEF24645DC41CC05154B7CE3CA28C9041E7F4DEC34168D29D9CF0`). Document child PID 70864
  exited 1 (`19:52:17.703645Z`–`19:54:49.963826Z`) with 19 failed document tests. The cleanup `else if`
  was bound to the prior `Execute` failure branch, selecting the wrong cleanup behavior. The r3 correction
  uses distinct reversed/non-reversed branches; its receipt records unchanged predicates and no native edits.
- **Debug r3 — qualified PASS.** Build PID 5928 exited 0 (`19:57:29.644701Z`–`20:00:09.818204Z`;
  stdout SHA-256 `29E2AE2CCC9BC18B1643000F8170468142E76E7FB2E8C7BB9EF64DE79363A49E`). Actual document
  child PID 27024 exited 0 (`20:00:11.307268Z`–`20:02:12.933546Z`), desktop handle closed. The
  independent `qualified.json` says PASS: 9,167 records, all 9,112 predecessor assertions retained,
  55 added records. Native binding SHA-256 is
  `2B0DD89C7B208183729ADD25FD8515EEAF4C239FD5F74BC84FD2234F0F67AAD9`; private manifest SHA-256 is
  `5B5442A4BE8DD476FD2814C01C7C28DB9CF9B84D053BA2FC5DEC69E139EB43D7`; exact-record reader SHA-256 is
  `F8CD3DBDBB76793C835FDB7E1577BE8B45524EE36A45CEE0174321192EBD1A92`. No test was rebaselined.
- **Release r4 — original reader failure preserved; corrected reader qualifies.** The actual document child PID 80472 exited 0
  (`2026-10-11T20:07:46.270302Z`–`20:07:55.049130Z`), but the independent exact-record reader FAILED:
  it compared Release allocation/cancellation prefix statistics against Debug statistics. Preserve this
  reader failure as evidence; do not treat the child exit as qualification. The corrected reader is being
  prepared against a native predecessor from the same configuration and exact source binding, retaining
  exact counts and predicates without adding normalization or changing tests. Main's corrected reader
  (`B97C36AC1C299FAE8AE3AC644810F3DD1C695681EE9C8025E45AC68E22D9AB75`) authenticates the matching
  native Release r907 archive on the same source binding, then accepts all 9,167 records. Release engine
  counters are 224 allocations, 56,624-byte workspace, 4,361 work units, 56 points and 1 loop.
- **ASan r5 — fresh standalone execution.** The original serial controller stopped after the Release reader
  refusal. After the same-profile reader accepts Release, Main starts a separate actual ASan r5 controller;
  compilation, sanitizer detection and runtime qualification remain pending.

Main reviews the cheaper agent's 16 core numeric fixtures before compilation: private include paths are fixed,
and full-canvas Expand10 compares with the unchanged original session operation rather than assuming an undo
no-op. Main's 16 fault controls retain the complete document, history identity/revision, Undo/Redo titles,
future count and generation, and cover absent/empty no-op, one-time consumption, cancellation, stale input,
off-owner preparation, actual arena refusal and successful reuse. The source project freezes 842 private
inputs and 87 cumulative additions before the first core Debug compile. The engine's earlier source and
products remain unchanged. Core compilation and its independent reader are still pending.

This evidence makes no native offset activation, caller/host
admission, host ordering, or G1 claim. The ordinary application route remains unchanged.


## Three-profile engine/publication qualification (20:40 UTC)

The engine's same-profile reader accepts Debug r3, Release r4 and real ASan r5. The original failed serial
matrix and wrong-profile reader remain preserved. ASan actual child79716 exits0 at20:16:13 UTC; its separate
negative detection child83988 exits1 with the required heap-use-after-free and named probe source.
All9,167 records and55 additions match; bounded ASan counters are289 allocations,64,976 workspace bytes,
4,426 work units,56 points and1 loop. The summary `engine-three-profile-qualified-r1.json` binds all reports.

Publication core Debug child81164, Release child49664 and ASan child76292 each exit0, respectively at
20:17:48,20:22:52 and20:28:45 UTC. The exact reader accepts9,199 records/all9,112 predecessors plus87
additions per profile. Its earlier ASan refusal for missing native r909 remains preserved; the same unchanged
r8 source/products/run now qualifies after native r909's actual report is authenticated. The summary
`core-three-profile-qualified-r1.json` binds the three reports and actual children. No tests are rebaselined.

The private host r2 candidate freezes1,236 inputs (manifest `6F113275…`) and424 independent additions:
16 numeric/event-time,368 two-sign fault/state and40 joined-Close controls. It preserves the original128
application stages and adds exactly two ordered stages. Main reviews the inherited host protocol against
current native Release/Debug/ASan reports and matching compiler/SDK/pinned DxUi identity; the historical
Debug repair receipt is removed while current artifact identity checks remain. Debug r9 is building;
no host pass, large-output bound, native offset activation or G1 claim follows from launch.

## Host Debug r9 — original asynchronous caller gap

The complete private host compiles with zero warnings/errors (build90864, exit0,20:38:13–20:42:52 UTC).
Actual app87972 exits5 at20:45:06 UTC: the original header Expand test checks its five-pixel golden
immediately after an action that now queues a job. The original failure is preserved, along with all logs,
source/product snapshots and the stopped serial controller. Release r10 and ASan r11 do not launch.
No host qualification follows from the successful compile or partial tests.

Main will add real message-pump settlement at that existing caller, preserving its bounds/title predicate,
and explicitly type both Inverse/offset fault error expectations as int32. An independent large contract
fixture is being prepared:32,768 disjoint20-pixel squares contract by5 into10-pixel squares, with literal
count/area/bounds and complete original-ticket copied/weak-output ownership. Existing engine/publication
qualifications remain separate. The authoring candidate reproduces all eight private engine/base files
byte-for-byte from exact pinned upstream sources; `native-authoring-replay-r1.json` records this preparation.

## Large Contract refusal and scratch repair preparation

Private host r3 compiles, and actual Debug r12 child 77004 exits 5 at 20:57:23 UTC.
Its 384 command/fault and 40 joined-Close records all pass. Five of the nine large Contract records
fail: literal geometry, worker-buffer identity, >16 MiB output charge, copied-reader retention and
last-outline-weak retention. The original header Expand golden passes after real pump settlement.
Release r13 and ASan r14 never launch. This is a failed host attempt, with partial controls preserved.

Diagnostic r15 child 88852 and r16 child 15072 each exit 0, retaining the existing CPU controls.
Both runners refuse the intentional INFO stderr afterward; neither run is qualification.
The r16 stage report identifies normalization. Capacity is 134,217,728 bytes, consumed bytes
134,217,680, allocation requests 685,873 and work units 32,270,834 before safe refusal.

Main's next candidate reuses fully retired power-of-two slots inside the same admitted arena.
The default Boolean policy and header layout are retained; only offset operations opt in.
Allocation-count refusal and 64-unit cancellation stay global to the operation. Redzones and
unreachable-object cleanup remain required. Five new independent grid assertions retain 32,768
four-point squares, 131,072 vertices, signed twice-area 6,553,600 and bounds {13,13,8183,4087};
they require success within the original 128 MiB cap and unchanged input. The earlier private
authoring-path refusal and r17 copied-project include-root compile failure remain preserved.
The corrected isolated candidate freezes 843 inputs and 92 cumulative additions before execution.
No native activation or performance qualification follows from this preparation.

## Repaired CPU Debug r18 qualification

Actual document child 79076 exits 0 at 21:22:40 UTC; build 82220 exits 0 with no warnings/errors.
The independent reader verifies the exact 843-input source/dependency/product bookends, all 9,112
same-profile predecessor assertions and 92 additions in their original order, including every reached
allocation/cancellation/capacity refusal. The fixed large Contract case passes all five controls with
122,473,664 scratch bytes, 1,705,213 allocation requests and 173,651,989 work units. The small offset
reference retains its exact geometry/work counts while scratch falls from 64,976 to 28,064 bytes.
The reader SHA256 is `53625A6FAF42B6ADCC9D5393602995D6FC86DB123BD21BA3F3612F0358AA199A`;
private manifest SHA256 is `919323699B106DBBA782F048EBEC87DDE91933E1E2A5B31108627FB5D81F9C57`.
Source replay also reproduces all eight generated/base files byte-for-byte, including the new workspace.
These are CPU and authoring results; repaired host ownership, Release/ASan, native activation and G1 remain due.

Repaired Release r19 actual child 57712 exits 0 at 21:27:00 UTC. Its independent reader retains all
9,112 predecessor assertions and 92 additions. Large counters are 1,180,384 allocation requests,
105,694,528 scratch bytes and 173,127,160 work units. Small reference scratch falls from 56,624 to
25,888 bytes while geometry/allocation/work counts remain unchanged. Debug and Release report hashes
are `17211406…` and `D0A65931…`. ASan r20 compiles without warnings/errors and its actual negative
probe child 32228 exits 1 with the required heap-use-after-free; the positive suite remains running.

Main independently reviews the cheaper agent's 1,237-input host r4 preparation: only the two repaired
engine files and isolated project/solution identities change; all 433 host records and 131 stages are
retained. The inherited reader's absent `golden_records` lookup is corrected to the already frozen
16-entry `numeric_records_in_runtime_order` field, retaining the same exact order predicate. No
runtime qualification follows. A 39-file native activation patch is prepared, including independent
large-grid tests and owning behavior/validation/authoring contracts; all three repaired CPU and host
reports are required before that patch can write any native source.

## Repaired ASan and actual Debug host qualification

Repaired CPU ASan r20 child 92136 exits 0 at 21:34:06 UTC. Its exact reader accepts 9,204 records
on the same 843-input freeze and the genuine negative detection child 32228. Large statistics match
Debug (122,473,664 scratch bytes); `reuse-three-profile-qualified-r1.json` binds all three reports.

Actual WARP application Debug r21 child 50032 exits 0 at 21:42:51 UTC. Its independent reader accepts
all 7,399 original stdout and 7,123 original stderr assertions, 433 predeclared additions and 131
ordered stages. The large fixture passes exact geometry, buffer identity, >16 MiB original-ticket
output charge, copied/weak retention and exact final refund with no host barrier. Debug build 55560
has zero warnings/errors. This is private host proof, with native integration and G1 still due.

The serial controller then stops on Release r22 before compilation: runner child 15624 exits 1
at 21:42:56 UTC because the library hash differs from `DxUi.provenance.json`. Current library
`7C0DDD56…` was written at 20:38:39 UTC during the earlier analysis interval; recorded provenance
expects `D72B1125…`. Source pin remains `8e899407…`. The mismatch, library/PDB/provenance copies
and original refusal are preserved under `ReleaseArtifactRepair-r1` before native build-only r915.
This repair supplies no new runtime qualification. Original ASan r23 has not launched.
