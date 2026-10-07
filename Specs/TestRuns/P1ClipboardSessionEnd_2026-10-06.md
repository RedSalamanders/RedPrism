# P1 clipboard cancellation on Windows session end

## Checklist

- [x] Confirm the production session-end omission against the actual worker and window paths.
- [x] Have a cheaper agent prepare the bounded six-scenario test draft; independently review and repair it.
- [x] Implement exact-ticket/current-stamp cancellation on confirmed session end, preserving acknowledgement.
- [x] Register 48 real-message controls; use one existing harness gate event and a collision-free message.
- [x] Keep irreversible native publication, private Copy/Cut, all preceding goldens and retry policy unchanged.
- [x] Full identical-source Debug/Release/real ASan, Release analysis and ARM64 cross-build qualification.
  - [x] Full Debug r275 and independent registry r3 pass all 48 new checks and every predecessor.
  - [x] Repair/qualify the precise post-Empty oracle; retain the failed Release r276 result.
  - [x] Release, real ASan, analysis and all three ARM64 cross-builds on the repaired source.
- [x] Independently verify all predecessor registries and actual images/PDBs; archive every receipt.
- [x] Commit/push the qualified caller step and update the parent checklist.
- [ ] Finish the remaining P1/G1 gates before Done; Save/export session-end wiring is now qualified separately.

Status: **qualified on the combined 877-input source**. The
[complete caller closeout](P1CallerQualification_2026-10-07.md) binds Debug r278, Release r280,
real ASan r281, Release analysis r283 and all three ARM64 r40 cross-builds and archive proofs.
Parent: [P1](../Plans/WIP/UiNeverBlocks_2026-09-22.md).

## Reference and boundaries

Actual `WM_ENDSESSION(TRUE)` cancels the active clipboard scheduler ticket and requests cancellation only
of its exact current history stamp. It does not clear the receipt, ticket, charge or unfinished graph and
does not wait for, retry or perform a native clipboard operation. A false ending flag preserves active work.
Private Copy/Cut already applied remains completed. Cancellation before Empty preserves foreign contents;
once Empty succeeds, the existing adapter attempts receipt, PNG and DIB placements once despite late stop.

The six actual-host scenarios cover Copy and Cut before Empty, after Empty, and cancelled session end.
Independent source bytes are `(11,22,33,255)`, `(20,40,80,255)`, `(99,100,101,255)`; only the middle pixel
is selected. Cut retains literal outer pixels and makes the middle transparent, with one `Clear` history
entry. Copy preserves document/history. The private clipboard owns the selected middle pixel at `(1,0)`
and advances its serial once. Foreign OS contents are separately `(17,33,65,255)`.

Each scenario checks the actual pending window-message boundary, original ticket/stamp/reservation through
acknowledgement, true/false cancellation, completed private receipt, literal private pixels/history/serial,
zero or exactly one native publication, OS pixels/content-bound receipt, and complete worker retirement.
The new observer hook exists only when a harness gate is configured. It shares the original release event,
uses borrowed shutdown readiness and a ten-second failure bound; normal production allocates/posts/waits
nothing for this hook. A single bounded harness acquisition copies receipt and image formats together before
the tiny test-only digest comparison. No later global clipboard sequence is origin authority.

Primary review removed duplicated dispatch branches, private API assumptions, an existing message collision,
an unnecessary gate event, optional dereferences, narrowing stride, predictable/pre-deleted scratch paths
and separate receipt acquisition races. The user clipboard and settings path are restored after ordinary
acknowledgement. A settlement failure exits without destroying state still borrowed by a worker.

The first combined candidate contains 872 frozen inputs, 80 CPU Clone/folder additions plus 48 clipboard
expectations and five new application stages relative to the qualified 867-input predecessor. Full Debug
r275 passes on freeze `2D7C0359BDE500A7A59969718748110C579F83BE9A36EDFE138155CFA8F36372`.
Its actual child PID `69204` ran `18:36:23.006756Z`–`18:50:01.205495Z` and exited 0. Independent
registry r3 verifies all 128 additions, 10,286 named application controls / 97 stages and unchanged
Host/Document/Composition/I/O predecessors. All 1,156 copies in
`D:\RedPrismQualificationArchives\P1-clone-clipboard-872-Debug275-2026-10-06` verify; completion
`AFDD29BF36B12F64E0F20CD56F0553DB3B53AAB301B00BD7D99E0B8413FBF2A8`.
Release r276 failed as recorded below. Save/export completion events and saving block reasons
are now integrated in the combined 877-input candidate; no claim of overall
P1, native/resource activation, frame latency or G1 is made here.

## Release r276 retained failure and review

Actual child PID `97868` ran `18:51:34.544314Z`–`19:01:17.775559Z`, exited 1, on the same 872-input freeze.
Compilation, all four suites and both crash probes passed. The new application matrix failed only
`clipboard-end-session.copy-cancelled-session.irreversible-formats-published-once-with-no-retry`.
Private pixels/history, successful receipt, literal OS pixels/content binding and final retirement passed.
The combined counter assertion logged no individual values, so its exact failing condition is unproven.

Source review confirms the assertion's one-total-open requirement excludes the normative six-attempt
pre-Empty Busy retry. The integrated repair observes successful Open, Empty, receipt/PNG/DIB placements and
the open-attempt count at Empty; final attempts must equal that snapshot. It preserves exact single
publication/placement and proves no retry after the irreversible boundary. The cancelled-session Copy
case deliberately injects one pre-Empty Busy result. Extra failure diagnostics print each counter and
the captured sequence; production retry/adapter behavior is unchanged. This is a reviewed oracle repair,
not a claimed reproduction or established root cause of the archived failure.

All 1,153 copies in
`D:\RedPrismQualificationArchives\P1-clone-clipboard-872-Release276-publication-counter-failed-2026-10-06`
verify; completion `DF44D76BB00A7D13A43951BD208464F64CC06DD4A73E82C0EA33AE91134032F8`.

## Preserved unchanged-source ARM64 cross-builds

All three r39 children cross-compile the same 872-input freeze used by Debug r275 and Release r276.
Their independently checked PE architecture is ARM64 (`0xAA64`); source, actual child exits and linked image/PDB
hashes match. No native ARM64 runtime was executed. Archives verify:

| Configuration | Actual child / UTC interval | Copies | Completion hash |
| --- | --- | --- | --- |
| Debug | `66612`, `18:53:34.045480`–`18:59:43.763122`, exit 0 | 944 | `0261B3512E5FB9E9369C6CF484214DF3A2098B3448905F27909E1E903A1BE46C` |
| Release | `19860`, `18:59:49.560473`–`19:05:48.515988`, exit 0 | 944 | `06BF1C67D1F7BFE29E2BAF50709DC8AAA6C347A448834E80F8DE0C76F25EF319` |
| ASan Debug | `76104`, `19:05:53.001494`–`19:11:34.143104`, exit 0 | 948 | `9657DDEBAE8AE46FEED58E9729CD7628B5A6B4912BDEDE6F27431447DEA51CC1` |

Archive names are `P1-clone-clipboard-872-ARM64-{Debug,Release,ASanDebug}39-2026-10-06` under
`D:\RedPrismQualificationArchives`. These builds cannot qualify the repaired 877-input source.
