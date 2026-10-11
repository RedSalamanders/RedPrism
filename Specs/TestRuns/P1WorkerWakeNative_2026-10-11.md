# P1 native owner wake — 2026-10-11

## Checklist

- [x] Review the private counter, broker, actual WorkerPool, original JobScheduler and injected-failure proofs.
- [x] Integrate the endpoint into scheduler compute/recording pools, preserving default pools and foreign producers.
- [x] Qualify Release build r879, focused Host r880 and all WARP application stages r881.
- [x] Preserve three actual uninstrumented cancellation rosters and correct the two action samples' budget classification.
- [x] Preserve the separate instrumented repeat and the original parser contiguity refusal.
- [x] Qualify full native Debug, Release and real ASan on this source.
- [ ] Complete Release analysis r889 and ARM64 cross-build r98.
- [ ] Investigate earlier actual Apply failures; complete the original G1 operation set and ordinary activation.

P1 remains WIP; all nine P1 parent areas remain open. Full Debug, Release and real ASan pass on this source; Release analysis r889 is in progress and ARM64 r98 is unlaunched. G1 ordinary activation and Done are not claimed. The earlier ledger compared two
UI action samples to the wrong budget. The correction below preserves the original timings and targets.

## Source and functional acceptance

The earlier sparse-read and optional-attribution checkpoint is committed/pushed as `e6b6bf8`, with its own
complete native/cross-build evidence. Later owner-wake changes are separate. Their 1,216-input source freeze is
`AC6284262CAAFEAD9004D80E5A355FCB670A1F4F7C27E9DB8BE55B409E8C5A85`. The genuine binding was persisted
at 11:35:10.223598 UTC before runtime, SHA256
`E8656FC3CA9034D9DDFFA54D2D6A32D41C0BCEAB72A66D3F024CB533E2EDD97D`; it binds all inputs, 20 owning
contracts and the exact Release products. DxUi remains pinned and unchanged at `8e8994079e82f79e6765e6a885c03fc687957591`.

Release build r879 passes. Actual Host r880 (PID 64668, exit 0, 11:35:16–11:35:18 UTC) preserves all 8,923
predecessor records and adds 17 literal capacity/reset/actual-pool records. WARP application r881 preserves
all 126 ordered stages and all 7,399/6,902 stdout/stderr assertions. Main authenticates source, contracts,
products, actual process results and the complete predecessor inventories. This does not replace the full matrix.

The scheduler opts in only its compute and recording pools. Three inline counters and checked startup event/wait
resources move SRW acquisition/native work submission off owner. The wait shares the existing pool. Joined
Close precedes context release; startup failures unwind before acceptance. Signal failure after publication
uses the installed terminate handler. The private fault children validate actual dump/marker/exception codes.
Native callback entry additionally installs that handler before accessing state. No per-request payload,
allocation, second pool or polling loop is introduced. Kernel signaling latency still requires measurement.

## Actual uninstrumented cancellation rosters

No build or native test producer runs concurrently with these timing children. Each actual source/product/
contract identity and active power scheme stays unchanged; the benchmark owns its normal presentation guards
and display-power request. Optional attribution is explicitly disabled. The original saved-output reader passes.

| Profile | Actual PID / exit | Interval UTC | Maximum Apply µs | Maximum action µs | Measured / refusal rows |
| --- | --- | --- | ---: | ---: | ---: |
| 4K WARP | 71088 / 0 | 11:40:18–11:41:23 | 273 | **2,305** | 82 / 0 |
| 45 MP hardware | 48872 / 0 | 11:41:25–11:42:43 | 229 | 174 | 70 / 22 |
| 100 MP hardware | 65992 / 0 | 11:42:44–11:44:36 | 454 | **2,064** | 70 / 22 |

The 4K Lens trial 1 action reaches 2,305 µs while Apply is 134 µs. The 100 MP Exposure trial 3 action
reaches 2,064 µs while Apply is 64 µs. The earlier ledger incorrectly treated these as failures against a
2 ms action target. §1.2 applies 2 ms to `ResultApply`; `UiAction` has p99 ≤ 4 ms / max ≤ 16 ms. The benchmark
reads those two timing kinds separately from their original inclusive histograms. Reconciliation of all
100/80/80 recorded active trials (including excluded cancellation outcomes) matches every value in the
unchanged reader's owner distributions: action p99 bounds are 4,000/250/4,000 µs, action maxima are the
values above, and Apply maxima are 273/229/454 µs. These current recorded samples meet their respective
budgets. No native/parser threshold or raw record changes, and this does not qualify the full G1 operation set.
The 4,036 µs Curves Apply remains an actual historical failure; no cause or repair is established by the
current repeat. Refusal rows carry no kernel/cancellation/owner timing claim.

The separate audit is `NativeWorkerWake-r1-Main/timing-budget-reconciliation.json`; its reader SHA256 is
`935F29B8CDA7B956E680D692317D02DA3261D7E33420A822C46AAF7E0D2E0BAF`. The audit binds the genuine
pre-runtime source, raw/parser receipts and the actual timing-kind getters. Its initial refusal correctly
stopped at the large-profile active-row count; Main then matches the original 88 active / 22 refusal roster
and keeps warmups and refusals distinct. This corrects the ledger, not the earlier source or evidence.

Archive: `D:\RedPrismQualificationArchives\P1-WorkerWake-native-Release-r879-focused-and-visible-20261011T115334Z`.
All 1,323 copied evidence/source files read back; manifest SHA256
`EE1F1898D6D4292FA0EA8F5BE82AEC79029D6129FC8C165A2D98D7CCD9212379`.

## Separate instrumented repeat and reader review

The instrumented 4K WARP child (PID 57500, exit 0) records Apply/action maxima of 439/249 µs, 110 valid
summaries, no phase at or above the 1,000 µs threshold, no overflow and no optional clock failure. This repeat
does not explain either preceding action sample. The original cancellation reader first refuses interleaved
diagnostic rows because its namespace must be contiguous. That actual refusal stays intact. Main verifies
the independent attribution reader and a separate projection removing only the 110 validated summary lines;
every original cancellation row remains byte-identical and the unchanged cancellation reader then passes.
Raw/projection SHA256:
`CEA20370AEE37BB599B1DFC5C75E9451CAECB4AAA80AFC971A7CB9633EE354C2` /
`3A96D1745808C5F4ED57C3C92C6AB737B1A9B03483DC9FB4DC8D2A6DF02DFAF8`.

Main also reviews the cheaper agent's full-matrix reader. Its original qualification predicates remain intact.
The agent's PowerShell normalization changed the genuine UTC timestamp spelling into local `+02:00`, which
would break the existing literal time-order comparison; Main restores the persisted UTC spelling and corrects
a stale scope description. Later metadata has its real authored time and references the earlier binding digest.
No authorization Boolean or timestamp is invented. Complete native Debug now passes; real ASan r888 is now qualified below.

The first reviewed controller's nested r882 recorder launch returns `CreateProcessW` error 5 before any
native test starts; its launcher PID 14772 exits 1 and its original streams remain preserved. Main's next
helper keeps the child in its existing Windows job and retains the actual-process wait, source/contract pins,
archive and reader predicates. Fresh r886 Debug starts successfully, followed only after success by r887
Release, r888 real ASan and r889 Release analysis.

## Complete native qualification

Full Debug r886 runs as actual child PID 91336, exit 0, 12:10:02.186304–12:24:09.531948 UTC, on
the exact 1,216 inputs and 20 contracts above. The independent reader preserves all 8,880 original Host
records plus 60 cumulative additions (43 earlier attribution and 17 wake assertions), all 9,008 original
Document records plus 18 earlier sparse-read assertions, unchanged 6,838 Composition and 1,823 Io records,
all 7,399/6,902 application records and all 126 ordered stages. Original parser, sizing, cleanup, build-process
and actual crash-probe controls remain intact. This is correctness/ownership qualification; it does not close G1.

Archive: `D:\RedPrismQualificationArchives\P1-WorkerWake-Debug-r886-full-20261011`.
Completion receipt SHA256: `25BF85C0D824C87920623F370CE7787FB1ACC416AC58C8B12802745D5CF4A612`.
The current full reader is pinned at `CDAD1B5795E8FC413D45A46A9F3FC5381C74EEA7AF752F8DBCFDAF302DAC3ADF`.
Full Release r887 also passes on the identical source: actual child PID 67332, exit 0,
12:24:34.869423–12:34:05.770447 UTC, with the same complete inventories and 126 application stages.
Archive: `D:\RedPrismQualificationArchives\P1-WorkerWake-Release-r887-full-20261011`.
Completion receipt SHA256: `CB2BE36D61A5322EBF0E48030DAD3295A1AAB3E5BC2A4C6890E0A802EF7D0031`.
Release analysis r889 passes as actual child PID 52700, exit 0, 12:54:05.932775–13:05:02.466522 UTC.
Main independently reads back all 1,216 source and 393 evidence files against the earlier binding and current
20 contracts. Archive: `D:\RedPrismQualificationArchives\P1-WorkerWake-Release-r889-analysis-20261011`;
completion receipt SHA256 `997A06929FC394813E9F19EA034292DE3E954EFE6BFE194EC220323497D32D0A`.
The independent audit SHA256 is `1441DFBBFC465E6459D3B81E4FEDCB4B3F430F28E488A51FD3097C98D9E14CA1`.
Main authenticates the exact three x64 profiles, original literal inventories and serialized child intervals,
then starts ARM64 r98 controller PID 66996 at 13:06:35 UTC. These remain cross-builds, with native ARM64
runtime unavailable; all nine P1 delivery areas, ordinary activation and G1 remain open.
Main corrects the cheaper agent's gate to read its actual supplied config, reject DRAFT before source/report
access, and bind the exact r889 archive, completion receipt and independent audit-reader identity. The
renamed process, archive, PE/PDB and ASan predicates match reviewed r97 after exact identity substitution;
remaining changes are a named hash constant and descriptive strings. The DRAFT refusal is tested without
launching or creating native outputs. The earlier ARM64 r97 result remains evidence only for the 1,211-input checkpoint. DxUi remains unchanged at `8e8994079e82f79e6765e6a885c03fc687957591`.

Real ASan r888 passes on the identical 1,216-input freeze `AC6284262CAAFEAD9004D80E5A355FCB670A1F4F7C27E9DB8BE55B409E8C5A85`: actual x64 `ASan Debug` test child PID 86792, exit 0, 12:34:22.799598–12:53:42.866975 UTC. Stdout/stderr SHA256 are `19E96EC7F5BD281F45B3D75BF9ED6105AB3963C6B83B68EECBFC9E811F1F3FE2` / `3C079EEB2A4ECF77122F102F3DCA83E102DE0F9F3B68BAF1E9C1F10F4514CE73`; source binding is `35223462EB567D4D1804867F59AF273DC9A7D4D16F307B50FBE55DB39C10502D`, qualification reader `CDAD1B5795E8FC413D45A46A9F3FC5381C74EEA7AF752F8DBCFDAF302DAC3ADF`. It preserves Host 8,881 baseline records and adds 60 (8,941 total), Document 9,008 plus 18 (9,026), Composition 6,838 and Io 1,823 unchanged; HostStress is 9,279 plus 60 (9,339), DocumentStress 1, and application stdout/stderr 7,399/6,902 across 126 stages. The actual ASan probe hash is `48838C8857D707A5FE8D56F77435D7574369C5860762176BCEE794F93F0F6C3E`; archived probe stderr (probe PID 27840) confirms the intentional heap-use-after-free was detected and aborted. The parser controls remain 3 accepted / 60 refused across 63 unique controls with 13 roster repeats; sizing/clipboard coexistence and 132 RBX parser controls pass. Archive `D:\RedPrismQualificationArchives\P1-WorkerWake-ASan-r888-full-20261011` contains 369 evidence files; source-manifest SHA256 `4C9CA573C4D4F688C65AE8051D16CC492E8E5F5A81F5E722FD125BD61DA920F9`, evidence-manifest SHA256 `3CEEAA3E5D5112E2B3B9C7B707B9FBB4F854696E828AE83BAF8ACBA85265B50F`, completion receipt SHA256 `D3177534DDE7C00718B07D744ED5E40868CACC3DBD6BC4F13E66800857C9E4EF`. This is full ASan qualification, not G1 closure.

The next optional host-action diagnostic draft is saved outside native source. It keeps original inclusive
timing scopes, thresholds, capacity and job ancestry, and adds lexical subscopes for preflight, sampler
cleanup/readback/replay, preview starts, dispatch-stage and settlement. It is unapplied and uncompiled.
The two preparation refusals occurred before any native mutation or draft output; corrected preparation
matches the actual function boundaries and call counts. The earlier ledger's action-budget mistake is corrected
above. Actual historical Apply failures and the complete G1 operation set remain open; this draft does not repair them.

## ARM64 cross-build closeout

The reviewed r98 controller completes all three cross-builds on the same 1,216 source inputs, 20 contracts
and unchanged DxUi pin. Each actual child exits 0; its successful archive verifies against its actual receipt.

| Profile | Actual PID | Child interval UTC | Completion receipt SHA256 |
| --- | ---: | --- | --- |
| Debug | 22196 | 13:06:53-13:07:23 | `11FBAEB7C1DCCE61EEB82BC3E3F0D52ACDFCF116570F29426C0610C856FE4B69` |
| Release | 52148 | 13:07:53-13:08:47 | `6E23DA7D25982E3254ED3DEC67510B939DAA2460FB0B9A209B7290AB05061773` |
| ASan Debug | 75160 | 13:09:17-13:09:45 | `F8B766F6A4195498DA7D84F91182979CDC22ECF815A8848A321D8E1CD0F9D6B4` |

Archive root: `D:\RedPrismQualificationArchives\P1-WorkerWake1216-r98`.
All 15 cross-profile PE/PDB pairs and five ASan compiler/image controls pass. These are cross-build claims;
no native ARM64 runtime was available. The launcher/runner, Main authorization, three independently
verified profile reports and PE/PDB verifier report remain in `ArmWorkerWake1216-r98-Main` and
`.build/armworkerwake1216-builds-r98`. Main checks actual successful child intervals and every profile
inventory before closeout. Formatting checks 1,074 files; all 15 repository skills validate; `git diff --check`
passes. P1's nine parent delivery areas, ordinary activation and complete G1 remain open.

## Recovery

After every current producer exits, Main executes the reviewed additive recovery roster. Recovery root:
`D:\RedPrismQualificationArchives\P1-recorded-callers-source-recovery-20261011T131308Z`.
All 16,731 copied files across 102 draft folders read back. Manifest SHA256:
`616ABAE87EE044DBCF275888404B09803009A344B92854C976ED672DEF427B06`;
patch SHA256 `B3182BD908F0F59A6C0FB96B3AF30EF35BB9BF4B0D86671F2E07B074BAB72444`.
This saves the actual qualified source, new diagnostics and review scripts, retained failures and process
receipts from base `e6b6bf8`; it is recovery evidence rather than an additional qualification claim.

Qualified implementation commit: `0393d2b` (pushed to `origin/codex/p1-opacity-host-proof`). P1 remains WIP; no merge to main.

