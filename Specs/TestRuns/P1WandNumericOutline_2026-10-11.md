# P1 Wand Numeric Outline Qualification — 2026-10-11

## Qualified scope

Full x64 Debug/Release/real ASan, Release analysis and ARM64 cross-build evidence qualifies numeric transport of the displayed Wand selection outline for the bound source snapshot. It does not qualify the entire P1 change or repository.

The 1,220-input origin binding is `.build/P1Drafts/SelectionOutlineNative-r1-Main/binding-before-native-runtime.json` (SHA-256 `CC0D2666E49969177D9EACFA6E5F3A19039932FBBC53C4DF65E75EAEC2C3C5AB`). The successful build and both focused runtime receipts share source freeze SHA-256 `0DC231552233668ACA2F807685826A95AAD2A89594F4AE531D18C1D650BCE79E`.

## Implementation boundary

The worker prepares numeric `D2D1_POINT_2F` loops. It subtracts the double-precision display anchor from each double-precision source point before narrowing the relative coordinates to `float`, preserving the existing `SyncSelectionOverlay` operation order. The owner checks the current displayed selection identity before adoption; a different source is refused. Direct2D geometry creation and use remain owner-only.

The focused host evidence exercises bounded admission and output against the original ticket, output charge, and old-reference retirement lifetime. It does not qualify other selection producers or ordinary application activation.

## Evidence

| Receipt | Result |
| --- | --- |
| `.build/incremental-x64-build-Release-r890` | Preserved failed header build, exit 1: `Application.h(276,59)` reports C2061 for `PreparedSelectionOutline`. |
| `.build/incremental-x64-build-Release-r891` | x64 Release build succeeded, exit 0, 1,220 inputs; source freeze matches the binding. |
| `.build/incremental-x64-runtime-Release-r892` and `r892-Document-focused-Release.json` | Focused `DocumentTests` Release passed, exit 0. The full reader retained 9,026 original records plus 43 additions, total 9,069. |
| `.build/incremental-x64-runtime-Release-r893` and `r893-application-focused-Release.json` | Focused WARP application Release passed, exit 0. All 126 ordered stages matched. Original counts were stdout 7,399 and stderr 6,902; two expected stderr records were added, giving 7,399 and 6,904. The Wand host summaries advanced from 144 checks / 17 modes to 145 / 17, and from 199 / 21 to 200 / 21. Both mode counts remain unchanged. |

The three completed evidence archives are retained at `D:\RedPrismQualificationArchives\P1-WandNumericOutline-Release-r891-build-20261011`, `D:\RedPrismQualificationArchives\P1-WandNumericOutline-Release-r892-Document-20261011`, and `D:\RedPrismQualificationArchives\P1-WandNumericOutline-Release-r893-Application-20261011`.

The corrected Main full reader preserves the 18 existing Document additions alongside the 43 new records and uses the flat report count. Its exact archived focused comparisons also pass: Document retains the 9,008-record predecessor plus 61 additions; Application retains all original semantic records plus the two additions, with only the two literal summary rows mapped for comparison. Reader SHA-256 is `22C3B5A9DEEB7342842AF130A844BCACEE0A33D2659B09A3BA37FBADB98F75BC`.

The original include-preparation assertion, guessed Document delimiter and Application summary-count refusal remain preserved. They do not count as successful builds or runtime evidence.

## Not qualified

- Other selection producers, ordinary application activation, G1, and P1 Done qualification.
- Full owner Direct2D path construction, other selection producers and complete S5/H4 remain open. Failed r895 is preserved below; fresh r898 qualifies Release, r896 qualifies real ASan Debug and r897 qualifies Release analysis.
- Native ARM64 runtime, which was unavailable.

## Full Debug qualification

r894's actual child PID 69060 ran from 14:04:36.965065 to 14:20:55.924666 UTC and exited 0 on the same 1,220-input freeze and 20 contracts. The completed archive is `D:\RedPrismQualificationArchives\P1-WandNumericOutline-Debug-r894-full-20261011`; the Main reader report is `.build/P1Drafts/SelectionOutlineFullReader-r2-Main/r894-Debug-full-profile.json`.

The reader verifies Host 8,940, Document 9,069, Composition 6,838, Io 1,823 and Application 7,399/6,904 stdout/stderr records, with all 126 original ordered stages. It retains every qualified predecessor assertion, 60 cumulative Host additions and 61 cumulative Document additions, the exact two Application additions and literal summary bookkeeping changes. Original parser, sizing, cleanup, crash and application controls remain required. Release r895 starts only after this completed run is archived and authenticated.

## Preserved Release failure

r895's actual child PID 44596 ran from 14:21:22.888138 to 14:31:42.170403 UTC and exited 1. Its prepared cancellation smoke reports `cancel-ambient-keyboard-state` at Levels trial 0; the direct smoke had passed. This is a failed full attempt, not a successful profile. Archive `D:\RedPrismQualificationArchives\P1-WandNumericOutline-Release-r895-full-20261011` retains all 1,599 read-back source/evidence copies; completion SHA256 is `4A25626704DA0A3569AEA044E076E9039625FD4B6A6BD77F5A79F51207D59ACA`.

The later read-only diagnostic-thread snapshot has no held Ctrl/Alt/Shift/Esc high bits. It does not identify the failed benchmark thread's state. Main's separate continuation keeps the original refusal predicate, records one original standalone prepared smoke against unchanged executable bytes, and starts fresh full Release r898 only if that smoke succeeds. No failed receipt is rewritten and no automatic retry is accepted.

## Full Release qualification

The original standalone prepared smoke's actual child PID 32760 exits 0 at 14:35:47.121342 UTC. Its application executable SHA256 remains `AFA3DEDB56BA029B5237C04DF3064C19D44DD32A5FF59E5F558C68F6753367A6` before and after execution; the native source and contracts remain unchanged.

Fresh full Release r898's actual child PID 92472 ran from 14:35:54.846506 to 14:47:30.379584 UTC and exited 0 on the same source freeze and contracts. Archive `D:\RedPrismQualificationArchives\P1-WandNumericOutline-Release-r898-full-20261011` and Main report `.build/P1Drafts/SelectionOutlineFullReader-r2-Main/r898-Release-full-profile.json` authenticate the complete original inventories, intended additions and all 126 application stages. Both original cancellation smoke routes pass in this complete profile; r895 remains failed.

## Full real ASan qualification

r896's actual child PID 64520 ran from 14:47:53.054087 to 15:09:38.190139 UTC and exited 0 on the same 1,220 inputs, source freeze and 20 contracts. Archive `D:\RedPrismQualificationArchives\P1-WandNumericOutline-ASan-r896-full-20261011` and Main report `.build/P1Drafts/SelectionOutlineFullReader-r2-Main/r896-ASan-full-profile.json` authenticate all original records and intended additions, all 126 application stages, the actual heap-use-after-free detection probe, stress scopes and original parser/sizing/cleanup controls.

The inventories are Host 8,941 (8,881 original plus 60), Document 9,069 (9,008 original plus 61), HostStress 9,339 (9,279 original plus 60), DocumentStress 1, Composition 6,838, Io 1,823 and Application 7,399/6,904 stdout/stderr records. This is full ASan qualification of the bound numeric-outline source, with the implementation limits above retained.

## Release analysis and ARM64 admission

r897's actual child PID 4832 ran from 15:10:03.838748 to 15:22:10.377301 UTC and exited 0 on the identical source freeze and contracts. Main's independent archive audit verifies all 1,220 source and 395 evidence copies, including the actual successful analysis receipt and unchanged current files. Archive completion SHA256 is `D77CCCE0CCA2870FFB8DE0004DC2BF20037FA44912EED3CFBFB3D9D7DEE496A2`; the archive is `D:\RedPrismQualificationArchives\P1-WandNumericOutline-Release-r897-analysis-20261011`.

Main reviews the exact Debug/Release/ASan suite inventories, all 126 ordered application stages and the actual serialized intervals before authorizing ARM64 r99 at 15:23:48 UTC. The direct launcher starts controller PID 43432 at 15:24:12 UTC; Debug/Release/ASan cross-build results remain due. Authorization SHA256 is `2642DAA68E28F71FD22AA523A76577C288E7232E48B6AE0D16BD39297E77DD5B`. Cross-build admission is not ARM64 runtime proof.

## Preserved ARM64 checker refusals

The original ARM64 Debug child PID 81652 runs from 15:24:30.292859 to 15:25:41.952844 UTC and exits 0. Its checker stops at a stale gate-module pin retained from the earlier Release draft. Main preserves that refusal and the original checker, then corrects the exact reviewed gate pin. The next checker invocation refuses the equality between the archived original reader and the now-corrected reader; that refusal is also preserved.

Main binds the corrected authentication to the one exact successful Debug completion (`27778D349BAEB7D06119374E1FD0FF35991475C326646F3579E6D2E371395207`), PID 81652 and the original archived reader bytes. Every source, contract, archive, product and recorded helper identity remains required. The report separately records the original verifier and the current authentication reader; new profiles must match the current reader. All five Debug products authenticate without rebuilding Debug or changing native source or test expectations.

Fresh controller PID 69468 launches at 15:31:14 UTC into a distinct receipt folder for only the two unstarted Release/ASan profiles. The successful original Debug child and both checker refusals remain distinct. All 15 cross-profile PE/PDB matches and real ARM64 ASan build controls remain due.

## Completed ARM64 qualification

All three r99 profiles pass on the identical source and contracts. Debug PID 81652 exits 0 at 15:25:41.952844 UTC; Release PID 48696 runs from 15:31:33.372712 to 15:33:16.244754 UTC and exits 0; ASan Debug PID 63624 runs from 15:33:49.315208 to 15:34:53.418244 UTC and exits 0. Each profile authenticates its five ARM64 products. All 15 cross-profile executable/PDB pairs and the five real ASan compiler/image controls pass.

Main's final review verifies current source and product bookends, actual serial child intervals, archive completions and every matching product hash. Its initial lookup used the short `ASan` label for the pair report, which stores the literal configuration `ASan Debug`; that refused checker version remains preserved. Main corrects only this exact label lookup, retaining all product and instrumentation predicates. The successful review is `.build/P1Drafts/ArmWandOutline1220-r99-Main/final-cross-profile-main-review.json`, completed at 15:38:41 UTC.

Formatting checks all 1,078 C++ files and all 15 repository skills validate. This completes qualification of the numeric-outline step, with S5/H4, ordinary activation, full G1 and native ARM64 runtime limits retained above.

## Recovery

After all current producers finish, recovery `D:\RedPrismQualificationArchives\P1-recorded-callers-source-recovery-20261011T153936Z` saves and reads back 17,831 files from 114 draft folders. Manifest SHA256 is `683628AFE29C6AFC947EEF1B94DB8A1CCEC65D49E36CE8AF81F56D4F919DE8E3`; working-patch SHA256 is `FC52732FF836BF039F7A39B366B9BB416C91518930C07506025C9BFC38DEC30E`. It preserves all earlier refusals, current receipts and private Inverse preparation. This is recovery evidence, separate from the native qualification above.
