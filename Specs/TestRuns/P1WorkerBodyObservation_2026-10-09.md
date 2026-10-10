# P1 worker body observation and cancellation measurements

## Delivery checklist

Status: **Integrated; current full Debug r645/Release r647/real ASan r649, Release analysis r643 and original returned-body measurement r650 qualified. All three ARM64 r75 cross-builds qualified; commit/push due. P1 WIP.**

- [x] Independently review the optional worker observation boundary, callback lifetime, concurrent lanes,
  resumed tickets, cancellation normalization and shutdown ownership.
- [x] Prepare seven native candidates and four owning-contract updates; preserve existing goldens and the
  historical measurement values. Two new fixture files add 36 independent event-driven controls.
- [x] Review the report parsers against independent fixed models. Synthetic acceptance/refusal controls are
  parser proof only; the original rejected ordinary HostTests namespace is retained with its corrected checker.
- [x] Commit and push the independently qualified recovery checkpoint before integrating this slice: `8fc0ce6` matches the remote branch.
- [x] Verify every candidate/base hash, integrate, compile and execute original HostTests on the repaired source: r633/r634; all 36 fixed controls and 8,509 predecessor records match.
- [x] Run and independently authenticate original Release r639: all twenty operation records, source/product/machine binding, race classifications, exact empty sizing outcomes and unchanged source pass.
- [x] Complete current-source Debug r645, Release r647, real ASan r649, analysis r643 and all three ARM64 r75 cross-builds with original receipts, current source/product binding and unchanged predecessor controls.
- [ ] Update this checklist, owning contracts and the P1 plan; commit and push the qualified step.
- [ ] Measure actual Escape acceptance, restored-source presentation, body exit and external acknowledgement
  separately in the application loop. This headless diagnostic cannot close measured G1 or P1.

## Boundary and ownership

The integrated scheduler observer is immutable from initialization through joined `Close`. Its borrowed context
survives that join. Compute and recording callbacks may overlap; callbacks use bounded thread-safe storage and
cannot wait, call the scheduler, access UI/document state or authorize result application. The default observer
captures no clock, creates no event and allocates no diagnostic storage.

`BodyEntered` precedes `Run`; `BodyExited` follows its return and local destruction, or the runner's translated
`std::bad_alloc` unwind. Exit reports the raw result before cancellation normalization, worker retirement and
owner apply/drop. A dispatched cancelled stage that never calls `Run` reports `BodySkipped`. Descriptors dropped
without dispatch report no body notice. A resumed original ticket can have several body pairs. Body exit does
not acknowledge pending native I/O or GPU work.

The fixed fixture covers normal order/identity/local destruction; concurrent compute and recording lanes;
raw failure and allocation unwind; queued and running cancellation; three resumed stages; exit before held
retirement; callback-free default initialization; and owner-thread shutdown joining the borrowed observer.
Its 36 literal expectations are independent of the implementation's returned values.

## Measurement method

The integrated `HostTests.exe --bench-filter-cancel` diagnostic selects the scalar reference, uses an
8192 × 5464 fixture, and records one excluded warmup plus ten trials for each of twenty operations. After actual
body entry it waits up to 20 ms on the returned-body event. On timeout, it captures QPC immediately before the
original ticket's cancellation request. The worker observer captures the matching body's exit QPC.

Reported request-to-return time includes `Run` local cleanup and excludes subsequent worker retirement and
owner application. Already-finished work and valid-output/stop races are counted separately. Failed clocks,
failed admission, worker errors and invalid sizing results fail the diagnostic. Sizing cancellation must return
an empty detached result and preserve the source; the admitted reservation must not exceed 2 GiB. Machine and
actual worker-count collection occur outside measured intervals.

The earlier [cancellation report](FilterCancellation_2026-09-28.md) captured its clock inside `Run`. Its numbers
remain historical measurements of that earlier boundary; they cannot prove completion of final local cleanup.
The candidate owning-contract changes make this distinction explicit without changing the recorded numbers.

## Preparation provenance

Main's candidate/base hashes and fixed 36-record model are in
`.build/P1Drafts/JobExecutionObservation-r3/Main-final-preparation.json`. The corrected namespace checker is
under that directory's `observation-checker`; the twenty-operation report parser is under
`.build/P1Drafts/JobExecutionObservation-r2/report-parser`. Main also reviews the cheaper full-profile checker in `JobExecutionObservationQualification-r2`: it adapts the actual `ArchivedPath` schema, preserves original producer identities and all unaffected source bytes, and compares complete Application records and ordered smoke operation names while retaining each original timing log hash. All 18 controls pass, including actual qualified-baseline metadata. Its first incorrectly selected focused-archive product log is preserved with its corrected receipt-bound stdout selection. These ignored files contain preparation and parser evidence, not native execution of the proposed observer. The current recovery qualification is recorded in
[the durability ledger](P1ProjectDurabilityFaults_2026-10-09.md).

Actual source, executable, runtime receipts, complete configuration results and measurement values will be
recorded here after integration. Native ARM64 runtime, RAW/Photoshop comparison and ordinary application
activation are outside this bounded diagnostic's proof.

## Integration (2026-10-10)

- [x] Main verifies the clean `8fc0ce6` checkout, every previous 1,102-source hash and all eleven base/candidate hashes before integration. Exactly five existing native files change and two fixture files are added; all other 1,097 native inputs remain byte-identical. Four owning-contract changes also match their reviewed candidates.
- [x] Debug build r631 passed on freeze `481CBD67BAD93F52712000E9E33A48213B76CFECDCBD79325F997DF010826255`. Native tests and measurements remain due. `JobExecutionObservation-r3/Main-integration.json` and `JobExecutionObservationQualification-r2/Main-source-closure.json` record the integration; the latter directory pins all current and baseline inputs.

## Preserved focused failure and repair

Actual HostTests r632 (PID 68016, exit 1, 2026-10-09 22:33:26–22:33:38 UTC) passes the other 33 observer controls and fails the second/third resumed-body pair and total-pair checks. The original fixture waits for worker notices immediately after `Resume`; scheduler admission intentionally promotes `Resuming` only in the owner drain. The repaired fixture runs that drain first. The scheduler implementation and 36 expected outcomes are unchanged.

The original 1,104-source failed run is preserved at `D:\RedPrismQualificationArchives\P1-job-body-observation-1104-Debug632-failed-2026-10-10`, with independently verified completion SHA-256 `8B3DC47B487E08FF65B56158D1F3C3536D619B93A78336DC1CF7B316C347EBC4`. Preparation r4 and `JobExecutionObservationQualification-r3/Main-resume-fixture-repair.json` record the sole fixture repair; new-source build/runtime/full qualification remains due.

## Repaired-source qualification (2026-10-10)

Source freeze: 1,104 inputs, SHA-256 `B16B2D6C2D803514C70A4E245BA77BB8D85530412138846EC2A5C1549803B77A`. The sole post-r632 change is the required owner promotion in the resumed fixture; every expected result is unchanged.

| Actual producer | Result | Independently authenticated archive completion SHA-256 |
| --- | --- | --- |
| Debug build r633, PID 90948, 22:37:59–22:44:04 UTC | PASS, including linked instruction controls | `DF3722E643BBBE250D8EB9529E82975220FB104EC29FF41020AF0FDA9EABC680` |
| Original HostTests r634, PID 68756, 22:45:17–22:45:19 UTC | PASS: 36 observation controls plus summary; exact 8,509 predecessor records, including all 15 keyboard controls | `55F20B9C01743E68E94A1C782DFE424C977383ECC56ACD1ADADF721BAF365B0D` |
| Full Debug r635, PID 87248, 22:46:10–22:56:25 UTC | PASS: all 121 Application stages, 6,909 Application records, complete predecessor suites and both smoke rosters | `95C9CB2A72C812050EAB2BE92E34E29AB8865C00BC632D846F1B09797055464F` |
| Release build r636, PID 38304, 23:03:42–23:10:34 UTC | PASS; all 1,104 source / 328 evidence copies authenticated | `451DD5819A3A145192ADD7573753B03C9D1C7031B93568624ED3967BC35401D3` |
| Full Release r637, PID 27428, 23:10:51–23:18:56 UTC | PASS: complete inventories and both original smoke rosters | `329B2E272F1CD3418077D594AC7E635D08CD2C88E86694CB12262B74BFB7D6B7` |
| Real ASan build r640, PID 68436, 23:21:55–23:28:36 UTC | PASS; all 1,104 source / 334 evidence copies authenticated | See r640-archive-check.json |
| Full real ASan r641, PID 28592, 23:28:57–23:43:46 UTC | PASS: original detection probe, both Host executions, all 121 Application stages and both smoke rosters; 1,104 source / 337 evidence copies | `FF9277C8746F42D0C9B97EEB5809C0C726663EDD4F0551B8B88C8EF44D00D980` |
| Actual Release returned-body diagnostic r639 | PASS; all twenty exact operation rows; see measurement table below | `A781FC2D19C21A544C19E7F136200833CB20B4949A56D069F6DA2EBF39168CEE` |
| Release analysis r642 / historical ARM64 r74 | Analysis FAIL C26130, preserved; r74 not launched after failure. Superseded by repaired-source r643/r75 below | See preserved failure below |

The actual Host executable SHA-256 is `41600E2D2E644301DD0F0EE45FB9C1958AC5BC64A43C212BD665C6D4460104CC`, identical to the archived r633 build and unchanged during execution. `JobExecutionObservationQualification-r3/Main-Debug634-host-qualified.json` verifies the complete source closure, exact binary binding and original receipt-bound stdout; broad focused-archive product logs are not substituted for this original execution. Archives are under `D:\RedPrismQualificationArchives\P1-job-body-observation-1104-*`.

Main repairs the delegated benchmark launcher in `FilterBodyBenchmarkRunner-r3`: the real single-profile report fields and source/archive hashes are authoritative; the generic recorder's historical “Incremental focus” label does not override its exact unfiltered `test.ps1` arguments. All 18 profile-parser and six benchmark-parser controls pass, including actual predecessor metadata refusals. These are parser checks only. The launcher remains blocked until the actual current-source full Release report is pinned.

### Original Debug635 metadata refusal and qualification

The actual native producer exited zero. The first postprocessor refused the intentionally changed `Common/Jobs.cpp` because its earlier callback-order guard required all sixteen protocol files to be unchanged. Main preserves that refusal and qualifies the original archive with `JobExecutionObservationQualification-r4`: fourteen GPU protocol files are unchanged; only the two scheduler files differ at their exact reviewed baseline/current hashes. The same established two callback partial orders, numeric goldens, multiplicities and complete record inventories remain required. All nineteen parser controls pass, including the original actual Composition records and a refused mutation. No native rerun or reconstructed output is substituted. `Main-Debug635-profile.json` binds every current/baseline source, product and archive hash.

## Actual returned-body measurements (2026-10-10)

Original HostTests PID 26864 exited zero at 2026-10-09T23:20:06.855785+00:00–2026-10-09T23:20:14.883883+00:00. The exact Release637 executable SHA-256 is `6DE5B5922E777ABC7A44444650439A3AEC85C14770389B961F4C47827A0B572C`; its PDB and all three runtime DLLs remain byte-identical before and after this run. Source freeze is the same 1,104-input B16 candidate.

Machine: AMD Ryzen 9 9950X3D (16 physical / 32 logical processors), 66,155,405,312 bytes RAM, Windows build 26300, Balanced power scheme, x64 Release, scalar dispatch, 32 scheduler workers. GPU/driver/version metadata is reported unknown by this headless host; no GPU result is inferred. One excluded warmup plus ten trial slots per operation use the declared 20 ms body-entry delay. This run occurred after the Release producer joined and before the ASan producer launched.

| Operation | Measured / early / overlap | Median (ms) | p95 / maximum (ms) |
| --- | ---: | ---: | ---: |
| levels | 10 / 0 / 0 | 3.835 | 5.636 / 5.636 |
| hsv | 10 / 0 / 0 | 3.677 | 6.740 / 6.740 |
| curves | 10 / 0 / 0 | 3.933 | 7.947 / 7.947 |
| exposure | 10 / 0 / 0 | 3.953 | 6.839 / 6.839 |
| gradient-map | 10 / 0 / 0 | 3.673 | 4.216 / 4.216 |
| grain | 10 / 0 / 0 | 3.830 | 6.274 / 6.274 |
| gaussian-250 | 10 / 0 / 0 | 0.055 | 0.102 / 0.102 |
| motion-2000 | 10 / 0 / 0 | 0.055 | 0.073 / 0.073 |
| noise | 10 / 0 / 0 | 3.933 | 7.125 / 7.125 |
| content-fill | 10 / 0 / 0 | 4.563 | 11.130 / 11.130 |
| lens | 10 / 0 / 0 | 3.950 | 7.942 / 7.942 |
| histogram | 10 / 0 / 0 | 0.008 | 0.020 / 0.020 |
| coverage-aa | 10 / 0 / 0 | 5.816 | 11.097 / 11.097 |
| coverage-hard | 2 / 7 / 1 | 1.054 | 1.060 / 1.060 |
| downscale | 10 / 0 / 0 | 4.145 | 4.627 / 4.627 |
| carried-mask | 10 / 0 / 0 | 0.988 | 1.530 / 1.530 |
| sparse-copy | 10 / 0 / 0 | 3.939 | 6.894 / 6.894 |
| sizing-canvas-fill | 10 / 0 / 0 | 1.370 | 2.567 / 2.567 |
| sizing-image-nearest | 10 / 0 / 0 | 2.297 | 3.328 / 3.328 |
| sizing-image-high | 10 / 0 / 0 | 2.175 | 3.714 / 3.714 |

All 192 measured cancellations include actual Run-local destruction; seven hard-coverage trials finish early and one overlaps stop with valid completion. Those eight slots are separate from the measured tail. Sizing reports ten empty safe cancelled outputs per case; admitted peaks are 201,405,120 bytes for Canvas Size fill and 117,519,040 bytes for each Image Size case, below 2 GiB. Original source remains unchanged. The maximum observed 11.130 ms belongs to content-aware fill. These samples establish this fixture/delay only; they do not measure actual Escape dispatch, frame feedback, later worker retirement, owner application or native/GPU acknowledgement, and do not close G1/P1 or qualify RAW/Photoshop speed.

Archive: `D:\RedPrismQualificationArchives\P1-job-body-observation-1104-Benchmark639-qualified-2026-10-10`. Completion SHA-256 `A781FC2D19C21A544C19E7F136200833CB20B4949A56D069F6DA2EBF39168CEE`; original stdout SHA-256 `9E2B264FFDDC0AEB93E0EDA943910CE81A1DC3915080486285B75493DC51A05E`. `JobExecutionObservationQualification-r4/Main-Benchmark639-qualified.json` binds the original receipt, 20 exact method/report rows, complete machine fields, current full Release report and independently authenticated source/product copies.

### Historical B16 full x64 functional matrix

Main independently verifies all 1,104 live native input hashes and binds the original Debug635 / Release637 / ASan641 reports, archive manifests and child receipts in `JobExecutionObservationQualification-r4/Main-current-x64-three-profiles.json`. The real ASan profile report SHA-256 is `71909992A5FAED7EB531B4C005B562F0CCAE0F3450BBD7B3D24D94C641AB5902`; original stdout SHA-256 `38B6143E0E92887F2888471F28F03FA234608782A81BADB5DF1DD7D2636C77F6`. The original detection-probe hash is `C222F99A33882769E92C214F43CBFAA9A3D00279AF38D3BB678B142751F208C6`. This full functional matrix does not close analysis, cross-build, actual Escape/Present, G1 or P1 delivery.

## Original analysis failure and immutable identity repair

Actual Release analysis r642 (PID 28764, exit 1, 2026-10-09 23:44:14–23:47:56 UTC) fails C26130 in Common/Jobs.cpp::ObserveExecution. The helper derives the slot using the lock-protected record array outside its lock. Record stage ownership is retained, but this container access violates the concurrency-analysis contract. The original attempt is preserved at `D:\RedPrismQualificationArchives\P1-job-body-observation-1104-Analysis642-failed-2026-10-10`: completion SHA-256 `1BD4166507AF65614D20CA8F89309DA3D99F6ACDD9A954A44E1CD284C46F27EE`, 1,104 source and 315 evidence copies authenticated. No follow-on producer or automatic retry was launched by that driver.

Main's repair snapshots exact ticket/class/lane during Take's existing locked selection, then delivers the borrowed immutable identity outside the lock. There is no additional lock, clock, event, heap allocation, warning suppression or changed expected result. Only Common/Jobs.cpp changes (SHA-256 `EA79A15723C8826ED11853750CFE6F63B76DB05B25AF24519C0F37BEFFF57E30`); the other 1,103 native inputs stay identical. `JobExecutionObservation-r5/Main-analysis-repair.json` and `Main-source-closure.json` bind that change.

Fresh source freeze: `689AEE1858B1D0EBC7627FB439BDF3DBD728DF79FE67888AC5373BFA944F28EB`, 1,104 inputs. Analysis r643 starts first to verify the repaired concurrency contract. New-source Debug/Release/real ASan, original measurement, analysis archive and ARM64 qualification remain due. Q-r5 preserves the same 36 native expectations and nineteen parser controls; historical B16 evidence is refused as current repaired-source proof.

### Snapshot repair: actual analysis qualification

Original actual Release analysis r643 (PID 9116, exit zero, 2026-10-09 23:50:52–23:59:37 UTC) passes the native concurrency controls and complete linked instruction/ISA controls. `JobExecutionObservationMatrix-r5/Main-Analysis643-archive-check.json` independently authenticates all 1,104 source / 353 evidence copies in `D:\RedPrismQualificationArchives\P1-job-body-observation-snapshot-1104-Analysis643-qualified-2026-10-10`: completion SHA-256 `8FE8EE8E455F1D8092C1585C5A81506D372DD0565930B59902F48D0912F84F92`, source manifest `7130E1128C3ED97C72430D0290F270E81792C6B69737CA7992CFD25FAFC7669A`, evidence manifest `4EC9C71050FC214D11C213272AE77F59C3BDAF8957510AAB519F4346E4A0A1B4`.

The new Q-r5 profile checker is hash-pinned (`4B16269B16C2A0B954E4766CE4498FFDE1B436E2E9F970DCD59463C479E693FC`), preserves all 36 controls and passes nineteen parser controls. Formatting (974 native files) and all fifteen skill checks pass read-only on the same source, with pre/post native hashes verified. The new serial driver starts only after this original analysis joins/authenticates; it stops and archives an actual failed child before launching any follow-on. Fresh full native profiles, measurements and ARM64 builds remain due.

### Immutable snapshot current Debug r645 and source-proven recovery parser repair

Actual full Debug r645 exits zero (`PID 49584`, 2026-10-10 00:07:57.339553–00:18:55.917821 UTC) on the unchanged 1,104-input `689AEE1858B1D0EBC7627FB439BDF3DBD728DF79FE67888AC5373BFA944F28EB` source. The completed archive authenticates 1,104 sources and 357 evidence copies. All 36 original observer controls, complete Host/Document/Composition/Io predecessor inventories, all 6,909 Application records/6,747 assertions, 121 stages, 694 writer controls, eight policy controls and both 27-operation/direct-prepared plus sampler smoke routes qualify.

The original r5 postprocessor refused `Missing or ambiguous exact fail-first recovery group`; that refusal and producer stdout remain preserved. Source `Composition.Tests.GpuCommandPump.cpp::Run` permits the actual callback pair during the final `Until(CanRetireOwner)` after the independent CPU job completed. Main r6 accepts exactly three complete five-record sequences between the original failure and final all-submitted-use acknowledgement. Callback and joined/charge pairs stay adjacent and ordered; all other source bindings, controls, goldens, ordering and multiplicities remain fixed. Its 21 parser controls include all three patterns, invalid mutations and original r645 evidence. No native source, native expected outcome or historical failure is changed; no native test rerun was performed.

Archive: `D:\RedPrismQualificationArchives\P1-job-body-observation-snapshot-1104-Debug645-qualified-2026-10-10`.

- Completion SHA-256: `D816CCD319F2C4FFBBB8038815E2E23C9969C0E6FD5FC904121A61C627D20086`.
- Source manifest: `888F9E543FDAEF81943D3CACA68464E583962FE84AFB3D7256778405DDC674D9`.
- Evidence manifest: `97DAF88F11AF67D5A7F157C513A0166E468E4DE2AA91A5153751C9D4FCF0C6C5`.
- Original child stdout: `5789D86F40DD69A3495A21846BBDCF6100668CBD8333BC4137BCD93B02984DA0`.
- Main r6 current Debug profile: `7711832BD1A9EC929C7FAF5C74E0567CEE4C266D4B398F7B43BB2BDD5F5B0CB3`.

Remaining current-source Release r646/r647 and real ASan r648/r649 run serially under the reviewed r6 driver (`000AC36E6BE4BE107C1C199388ED5DB620BDF71E39B45A3972E2842C040B2C76`, PID 68232). The original r643 analysis and r645 full Debug prerequisites are hash-bound before follow-on production. Current measurement r650 and ARM64 r75 are due; this is no G1/activation/P1 Done claim.

### Current Release r647

The original full Release r647 exits zero (`PID 34480`, 2026-10-10 00:29:26.391859–00:36:56.634704 UTC) on the unchanged 1,104-input `689AEE18...944F28EB` source. Main r6 authenticates its complete source/evidence archive and preserves every predecessor golden/control, all 36 observer controls, 121 Application stages, 6,909 Application records, 694 writer/eight policy records and direct/prepared/sampler smoke contracts. Profile SHA-256: `603026ED9E5E7476FAA54726E8FABB7EA2D89AD25CF3780B0B52DDCAE064749E`.

Archive: `D:\RedPrismQualificationArchives\P1-job-body-observation-snapshot-1104-Release647-qualified-2026-10-10`. Its original stdout SHA-256 is `5E943637041948CE1C4D3925D9854ACAA0D94A4D0D3C9FC471820B83EA81259B`. Current real ASan r648/r649, non-concurrent current-source benchmark r650 and ARM64 r75 remain due; the producer sequence stays serial.

### Current real ASan r649

Original full real ASan r649 exits zero (PID 47308, 2026-10-10T00:42:42.996048+00:00–2026-10-10T00:57:04.885235+00:00) on the unchanged 1,104-input `689AEE18...944F28EB` source. All 36 observer controls in both Host executions, concurrent raster/worker stress, 121 Application stages, complete predecessor inventories, both 27-operation direct/prepared smoke rosters and the sampler smoke qualify. The deliberate detection probe is original runtime evidence, SHA-256 `DFB4851F8D8E89B64BB6F13F9D02C4D7D04F1EAD87EF32F5E257B6228B7E8370`. No sanitizer expectation or pixel golden changes.

Archive: `D:\RedPrismQualificationArchives\P1-job-body-observation-snapshot-1104-ASan649-qualified-2026-10-10`. Completion SHA-256 `B2F8CDDBA8655EA787E67A5455652B40C4F56A405673801CE0A930EAC5C66DD0`; qualified profile SHA-256 `924E7D1820601796B83774F31EA1045D4044BFD900E6A149A39DBCA0DD1221DC`. Main rehashes all 1,104 live source bytes and binds all three current native profiles plus actual analysis r643 in Q-r6/Main-current-x64-three-profiles.json before the next producer.

### Current returned-body measurement r650

Original HostTests PID 62568 exits zero at 2026-10-10T02:58:05.923568+02:00–2026-10-10T02:58:13.068617+02:00, after every x64 producer joins. This measures the current immutable-snapshot source with the exact full-Release647 EXE/PDB and three runtime DLLs unchanged. The declared method remains scalar 8192×5464, 20 ms after actual body entry, one excluded warmup and ten trial slots per case; machine is Ryzen 9 9950X3D / 16 physical / 32 logical / 32 workers / Windows 26300 / Balanced power / 66,155,405,312 bytes RAM. These are headless returned-body measurements, with no Escape, presentation, owner-apply, GPU acknowledgement, active-tier cancel-interval, G1 or RAW/Photoshop claim.

| Operation | Measured / early / overlap | Median (ms) | p95 / maximum (ms) |
| --- | ---: | ---: | ---: |
| levels | 10 / 0 / 0 | 5.134 | 8.002 / 8.002 |
| hsv | 10 / 0 / 0 | 4.078 | 5.559 / 5.559 |
| curves | 10 / 0 / 0 | 5.376 | 6.084 / 6.084 |
| exposure | 10 / 0 / 0 | 4.802 | 6.458 / 6.458 |
| gradient-map | 10 / 0 / 0 | 4.872 | 12.787 / 12.787 |
| grain | 10 / 0 / 0 | 4.923 | 6.018 / 6.018 |
| gaussian-250 | 10 / 0 / 0 | 0.045 | 0.060 / 0.060 |
| motion-2000 | 10 / 0 / 0 | 0.037 | 0.048 / 0.048 |
| noise | 10 / 0 / 0 | 6.387 | 8.080 / 8.080 |
| content-fill | 10 / 0 / 0 | 5.024 | 7.343 / 7.343 |
| lens | 10 / 0 / 0 | 6.357 | 11.964 / 11.964 |
| histogram | 10 / 0 / 0 | 0.009 | 0.114 / 0.114 |
| coverage-aa | 10 / 0 / 0 | 5.181 | 7.443 / 7.443 |
| coverage-hard | 4 / 5 / 1 | 1.258 | 1.428 / 1.428 |
| downscale | 10 / 0 / 0 | 5.137 | 6.802 / 6.802 |
| carried-mask | 10 / 0 / 0 | 1.393 | 1.901 / 1.901 |
| sparse-copy | 10 / 0 / 0 | 5.431 | 9.379 / 9.379 |
| sizing-canvas-fill | 10 / 0 / 0 | 2.211 | 3.308 / 3.308 |
| sizing-image-nearest | 10 / 0 / 0 | 3.207 | 4.635 / 4.635 |
| sizing-image-high | 10 / 0 / 0 | 3.951 | 6.142 / 6.142 |

All 194 measured returns include Run-local destruction. Five hard-coverage trials finish early and one overlaps acceptance; neither enters the measured tail. All three sizing cases retain ten empty safe cancelled outputs, unchanged original sources and admitted peaks below 2 GiB (201,405,120 bytes canvas fill; 117,519,040 bytes each image size). The maximum 12.787 ms belongs to gradient map; no algorithm changed between the historical B16 and current snapshot measurements, and these two finite sample sets do not establish a throughput regression or improvement.

Archive: `D:\RedPrismQualificationArchives\P1-job-body-observation-snapshot-1104-Benchmark650-qualified-2026-10-10`. Completion SHA-256 `D25C823845EA6ED09288426E511E55A6AACBB20C4DB6585A6211A7CDB3182E6D`; original stdout SHA-256 `8DF518F0455E836670AE6AA0926E85184F1AC4AFDB73975815B5CD8423EF89AD`. Q-r6/Main-Benchmark650-qualified.json verifies every method/report row, exact actual child receipt, current full Release binding and all 1,104 source / 332 evidence copies. ARM64 r75 remains the serial active producer; commit follows its actual qualification.

### Final current-source qualification and cross-builds

Main authenticates all three actual ARM64 r75 children against their complete archives, x64/analysis prerequisites and unchanged 1,104 live inputs. All 15 ARM64 PE/PDB pairs and five actual ASan compiler controls pass. The original process intervals below come from owned child handles after signalled exit; no native ARM64 host is available.

| Configuration | Actual PID / interval UTC | Archive completion SHA-256 |
| --- | --- | --- |
| Debug | 41404 / 2026-10-10T00:59:27.692098+00:00–2026-10-10T01:01:01.881713+00:00 | `389F25047ACE1A846A433A20A43A1493A55456A7568454C26E90B24F94662601` |
| Release | 19460 / 2026-10-10T01:01:54.733086+00:00–2026-10-10T01:03:45.924742+00:00 | `6CCE4F2DE0C4AA40DB59DEA7236258F3CE97ED412AB8D21E02D4007676DA6BAD` |
| ASan | 53024 / 2026-10-10T01:04:29.596785+00:00–2026-10-10T01:05:40.432324+00:00 | `B0AB42863F492B20032BDDFB5C1411B68A24CCBAA6C49F0055456C5C2B538A13` |

Q-r6/Main-current-qualified-closeout.json binds current full x64 Debug/Release/real ASan, actual analysis r643, original returned-body measurement r650, all cross-builds and the same-source formatting/skills checks. Native inputs are rehashed after the last producer joins. DxUi API3 pin remains `8e8994079e82f79e6765e6a885c03fc687957591`; no DxUi source changed. This closes the observer step only. Actual Escape acceptance/restored-source presentation, ordinary activation, measured G1 and the nine remaining P1 delivery areas remain open.
