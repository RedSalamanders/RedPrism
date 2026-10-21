# Qualification archive storage

Status: DONE (archive tooling only)
Owner: `Build/Build_QualificationArchives.md`

- [x] Locate writers: historical private helpers copy every frozen source and product; no tracked writer exists.
- [x] Add a tracked explicit-manifest writer and immutable shared payload store.
- [x] Preserve independent run records, expected hashes, metadata, readback and fail-closed path checks.
- [x] Exercise duplicate reuse, distinct evidence, changed inputs, corruption, locks, metadata, paths and failure recovery.
- [x] Document future-run adoption; preserve frozen helpers and historical archive bytes.
- [x] Verify live references and authorized ReFS case; record checks and close the plan.

The storage change cannot grant a qualification pass. Collectors still own their exact attempt/source/output
selection, freshness intervals, stage checks, profile checks and independent binary/PDB/runtime verification.

## Result and validation

The tracked PowerShell entrypoint stores eligible sources/binaries as verified read-only NTFS objects with
per-run aliases. Run receipts, logs, captures and manifests remain independent; unsupported sharing uses verified
independent copies. No existing archive path is reused or deleted. Failed partial evidence is retained without
a completion marker. The current contract is `Specs/Build/Build_QualificationArchives.md`.

All 17 retained filesystem scenarios pass against a frozen, hash-checked copy of the final writer and tests.
Two actual archives of the tested writer sources independently verify; the repeat reuses all four payloads,
while each run keeps seven evidence files and its two raw run records independent. ReFS copy fallback passes.
Skill validation (15 skills), formatting (593 native files), PowerShell parsing and Git whitespace checks pass.

The 754 historical RedPrism archive folders remain intact. All 22 live junction target/probe identities, two
pointers and the relocated worktree HEAD verify. The receipt-gated x64 Debug selected RedSalamander ReFS case
passes with runner exit 0; the NTFS negative control skips as designed with `ALL_TESTS_SKIPPED` controller exit 1.
Two stale runner records (50 files) were preserved before cleanup. The forbidden legacy drive-root directory
remains absent. The broader sandbox disk audit still reports the same 44 historical unregistered run directories;
there is no global clean-disk claim. Those evidence directories were not deleted.

Private reproducibility receipts, frozen source/test snapshots, earlier fixture failures and the independent
readback report are retained under the local `qualification-archive-workflow-20261021` maintenance collection.
No RedPrism application/platform/performance qualification is claimed by this archive tooling change.

## Integration with the current scoped runner

The publication batch is based on current main rather than the paused P1 checkout. Existing independent tooling
discovery picks up `QualificationArchiveTests.ps1` through `validate.ps1` and `test.ps1`'s Tooling scope; the older
checkout's unconditional native-runner insertion is not replayed. Current Changes/scoped-testing guidance and the
active native qualification plan are preserved. The storage implementation and its scenario expectations remain
byte-identical to the frozen writer snapshot above; hosted checks still govern merge acceptance separately.

The isolated publication candidate passes all 17 NTFS filesystem scenarios through the current independent
tooling runner, all 24 scoped-testing regressions, complete independent validation and formatting of 564 native
files. A fresh two-attempt ReFS fixture verifies independent read-only copies, exact expected hashes and retained
failure exit status. These tooling results do not qualify native application profiles selected by conservative
scope fallback; pending or unavailable hosted execution remains separate from successful local storage tests.

## Owner-authorized publication (2026-10-21)

After reviewing the local results and being told that PR #11's hosted checks were refused before any steps by
account payments/spending limits, the owner explicitly selected "Merge #11 using the verified local results".
This authorizes an ordinary matching-head merge of the completed storage tooling despite unavailable CI. The
final publication commit records that authorization and its changelog entry only; writer/test source bytes remain
those independently reviewed and tested. The billing refusal is not successful qualification. Existing native,
platform, focus, capability and performance gates remain open under their owning plans and contracts.
