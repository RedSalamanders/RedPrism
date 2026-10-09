# P1 project durability and session-end faults

Status: main-reviewed preparations only; not integrated or compiled. P1.7/F6 remains active.

## Checklist

- [x] Review the optional per-save durability seam against actual manifest/ZIP flush, checked close, commit and replacement ownership. Preserve default real-operation behavior and existing stage slots.
- [x] Review all seven helper candidates, format their explicit C++ files, parse the project XML and widen every old fault visitor to the appended stage count.
- [x] Review the independent helper fixture: eight failures, two successful replacements and two pre-cancelled controls; 87 expected assertions plus its fixed summary.
- [x] Prepare the original host writer's per-job fault policy and 20 actual session-end fault cases: 12 ZIP / eight folder cases, 248 new expected assertions. Retain all 12 original cases / 108 assertions.
- [x] Independently review and rerun the cheaper agent's 13 host-verifier rejection controls, authenticate fixture/manifest pins and match its original 108 records to the actual qualified Debug r575 Application log. Parser controls are not native proof.
- [ ] Integrate after the frozen frame/prepared-display source qualification and checkpoint commit; verify every existing native seed and candidate byte before copying.
- [ ] Compile and execute actual I/O and Application fixtures, authenticate original receipts/source/products and check complete predecessor streams outside the exact new records.
- [ ] Qualify the final repaired source in required configurations, including the actual sanitizer probe, analysis and ARM64 cross-builds; preserve failures before any repair.
- [ ] Complete the other F6 obligations: asset/export codec faults, host failed rollback/recovery and post-publication cleanup, combined writer/session budget, faulted late completion/timeouts, phase peaks and measured stop-to-return/G1. This bounded slice cannot close F6 or P1.

## Durability ownership and failure oracle

The existing per-call `ProjectSaveFaultPolicy` appends six gates after its stable slots 0–13:
`ManifestFlush`, `ManifestClose`, `ArchiveCommit`, `ArchiveFlush`, `ArchiveClose`, `ArchiveReplace`.
`Count` becomes 20. The callback returns an immediate Win32 error or zero to execute the original operation;
there is no process-wide injection state. Manifest gates apply to ZIP and folder projects; ZIP gates apply only
to ZIP projects. Existing directory rename, recovery and cleanup gates retain their numeric values and policy.

An injected gate denies its named operation before execution. RAII still closes/discards the exclusively owned
resource before temporary cleanup. The observer captures `ERROR_WRITE_FAULT` before cleanup can alter LastError.
Flush reports `FlushFailed`, checked close/ZIP commit reports `CloseFailed`, and ZIP replacement reports
`StagingToDestinationFailed`. Before replacement, no `Published` event is permitted. Only replacement failure
has a preceding `Staged` event among the six ZIP durability cases. Injected errors are final; the existing
bounded retries apply to real native rename refusals only.

The helper fixture uses different fixed document/layer IDs, names, resolutions, opacities and literal 3×2 pixels
for the old and replacement projects. Failure must preserve the entire old path/byte/directory snapshot, its
loaded metadata/pixels and an unrelated sibling, and leave no owned temporary. Success must load the exact new
document and follow the new layer ID's PNG path; preserving the old asset path set would be incorrect. Tree
inspection includes empty directories and refuses I/O or reparse errors. Cleanup validates the exact exclusively
created GUID root. Pre-cancelled controls must leave the original snapshot untouched and visit no fault gate.

## Original writer and session-end oracle

The harness request is immutable before submission. Its job retains the gate and creates a per-job policy on
the worker. Only the worker writes fixed-capacity fault/observer counters; the harness reads them after the
original acknowledgement. Ordinary saves retain the default empty policy. The host's current public receipt
continues to report the existing coarse `E_FAIL` for an unpublished `Encode` result; exact native error facts
remain in the worker observer.

Every new case starts with a valid literal old project and a different dirty current document. The original
asynchronous writer is held before the fault. Actual `WM_QUERYENDSESSION` checks the localized saving block
and critical-query allowance. Actual `WM_ENDSESSION(TRUE)` waits only for the original writer event; a helper
releases that worker after entry to the existing wait. `WM_ENDSESSION(FALSE)` performs no worker wait or cancel.
The event grants no owner receipt, path, saved revision or history authority. Ordinary completion later reports
one failed receipt/notice and retains the dirty current document and old path. Tickets, events, budgets and
utility owners retire without a hidden Present.

ZIP cases cover all six new gates with TRUE and FALSE, giving 12 cases / 144 assertions. Folder cases cover
manifest flush/close and destination/staging rename refusal with TRUE and FALSE, giving eight cases / 104
assertions. Folder staging-publication refusal must perform the original successful rollback; other folder
cases perform no rollback. No ZIP gate is consulted by a folder writer. Whole old trees, old literal loaded
pixels/IDs and the absence of owned staging/backup siblings are required. Rename failures have exactly one
`Staged` before their named error, with no `Published`.

## Preparation identities and qualification boundary

Main's reviewed helper overlay is `.build/P1Drafts/ProjectDurabilityFaults-r2`; its seven candidates and all
old visitor bounds are authenticated in `Main-review-addendum.json`. The independently reviewed helper inventory
is `ProjectDurabilityInventory-r1`: 87 exact ordered assertions and one summary, not a runtime result.

Main's host overlay is `.build/P1Drafts/ProjectHostDurability-r2`; the original r1 is preserved. Host fixture
SHA-256: `19D7C588A4D6240C87A498C2E0A794025BAB851D20A81A0621C93D6CF2B948DF`.
Candidate-manifest SHA-256: `2B150CEC4DAC4EB487A4E213079E8DEE5F45BED5E286388480C25B1E5D39A539`.

The fixed host inventory in `ProjectHostDurabilityInventory-r1` contains 356 records: 108 original, 144 ZIP
fault and 104 folder fault records. Registry SHA-256:
`F2EC846BD8E95F442812D04C4169E15926D0BE52E07359109FCB560D0F34D834`.
Main reads the complete verifier, reruns its 13 parser controls and binds the original 108 records to
`D:\RedPrismQualificationArchives\P1-frame-prepared-1098-Debug575-qualified-2026-10-09`'s actual Application
log (`0BE27048E3682A78FE14AC9D3DA267AD9EA8A045569D56037724AA8A7342B3EB`). This validates the inventory baseline;
none of the 248 new records has executed yet. Actual child exits, complete source/dependency/product identity,
fixed new records and all remaining predecessor streams must qualify together after integration.
