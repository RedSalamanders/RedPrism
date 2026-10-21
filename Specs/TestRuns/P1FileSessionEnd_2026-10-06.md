# P1 save/export handling during Windows session end

## Checklist

- [x] Verify the missing production waits and saving reason against the real host/producer paths.
- [x] Prepare reviewed producer/event and window-policy source copies under the ignored build folder.
- [x] Have a cheaper agent draft 16 protocol controls; repair the post-acknowledgement observation and invalid entry states.
- [x] Prepare 108 actual-host controls with literal output, old-file byte comparison and original ownership checks.
- [x] Integrate the complete reviewed source set after the previous frozen caller matrix finishes.
- [x] Full Debug builds/runs the new protocol and actual-message controls without changing their references.
- [x] Qualify identical-source Debug/Release/real ASan, Release analysis and ARM64 cross-builds.
- [x] Independently verify predecessor inventories, source/products/PDBs and all qualification archives.
- [x] Commit/push the qualified step and update the parent opening checklist.
- [ ] Close every remaining P1 requirement and measured G1 gate before Done.

Status: **full Debug r278 / Release r280 / real ASan r281 and all three ARM64 r40 cross-builds pass on identical source**.
Fresh Release analysis r283 and its independent archive pass after resume; r282 remains user-stopped.
The [complete caller closeout](P1CallerQualification_2026-10-07.md) qualifies this exact slice, with no complete P1/G1 claim.
Parent: [P1](../Plans/WIP/UiNeverBlocks_2026-09-22.md).

## Intended implementation

Each accepted file job and the owner share one manual acknowledgement event and atomic phase. The complete
job allowance includes fixed event/wrapper bookkeeping; event creation failure refuses admission before accepting
a writer. Project save starts queued. Export starts in readback; owner cancellation and worker entry compete
at one atomic transition so cancellation cannot permit a younger file write. Only worker scope exit acknowledges
the CPU write stage, including the scheduler's named allocation-failure unwind. Event readiness grants no receipt,
saved revision, path, document, GPU or publication authority.

An active queued/writing Save or CPU export blocks a noncritical query with a localized saving reason. Critical
queries allow. Readback/clipboard work adds no work-specific query block and preserves the existing unsaved-document
check. Confirmed end cancels readback/clipboard work and waits for queued/writing Save or an entered export writer
with one total 1,500 ms
native-wait allowance. It pumps no UI actions/completions and performs no GPU work. Timeout/failure requests
cancellation of the exact original ticket/current stamp while retaining the graph/event until ordinary
acknowledgement or shutdown join. A cancelled session cancels/waits for nothing. Ordinary completion alone applies
the captured Save path/revision; export never marks a document saved. In-flight filesystem/codec calls retain their
existing interruption limitations.

## Independent references

Twelve actual-message scenarios cover Save/PNG before and after publication, false session end, held-writer
timeout, saved and modified documents in export readback, and JPEG worker acknowledgement. Every source pixel
starts opaque `(64,64,64,255)` and Invert yields literal `(191,191,191,255)`; JPEG keeps the established three-byte
tolerance. Timeout/readback cancellation preserves the exact old destination bytes. Successful completion reloads
the literal output. The message boundary still holds the same pending receipt, ticket, history stamp and charge
after the worker event signals; no saved/path application occurs inside the wait. The helper releases the actual
producer only after the native wait-entry handshake and is joined before borrowed fixture state retires.
An earlier actual unsaved query retains its original prompt generation while an active Save reports saving;
critical/end handling clears or restores only the appropriate reason. Primary API review also replaced a
`uint32_t` pixel pointer with explicit BGRA bytes because `Raster::Make` accepts `uint8_t` storage.

The 16 protocol controls force both cancellation-before-entry and entry-before-cancellation orderings using
events. They also check queued entry, invalid/duplicate entry refusal, unacknowledged readiness and final event
readiness. These are state-machine controls, not file-writer/runtime qualification. The combined candidate has
877 frozen inputs; the earlier 872-input passing Debug and ARM64 cross-builds cannot qualify these changes.

## Integration boundary

The 19-file integration receipt is `.build/P1Drafts/SessionEndWrites/integration-r1.json`. It verifies every
prior and prepared-source hash before copying the complete set. Skill validation passes all 15 skills and
format validation passes 750 C++ files. Full Debug r277 starts at `2026-10-06T19:23:57.783574Z`.
The first observed build error is `HostTests.cpp`'s fixed 490-element string-resource array, now containing
491 entries after adding the localized saving reason. The source stays frozen until the child finishes;
the failed attempt will be archived before correcting the capacity. Independent registry r4 explicitly expects
16 new protocol controls and two repeated string-resource controls, plus all 108 actual-message additions;
it preserves the exact qualified 867-input predecessors and makes no G1 or production-residency claim.

Debug r277's actual child PID `92124` ended at `2026-10-06T19:27:10.733358Z` with exit 1 on freeze
`CB1F3B82A080BBE6B476CED03AC47A8FD1DB47E872E3D85C127856ACB6497630`. No runtime suite was invoked.
All 1,160 archive copies verify at
`D:\RedPrismQualificationArchives\P1-file-session-877-Debug277-resource-count-failed-2026-10-06`,
completion `0E54E6071535EE436C28F877D7A2642A6DD84BD80D08558113502AF1BCBF3689`.
The sole source repair changes the resource-array capacity from 490 to 491. All protocol, pixel, file-byte,
history, message-policy and predecessor expectations remain unchanged. Debug r278 qualifies the repaired source.

## Repaired-source ARM64 cross-build qualification

All three r40 children exit 0 on the identical 877-input Debug-r278 source freeze
`14178E65857A11148E49C3D1FD548DDC39298FF2BBAFF9FFDC55244656AAABDF`. The independent r2
verifier checks each actual child interval, source equality, all five ARM64 PE images and paired PDBs.
No native ARM64 runtime result is claimed.

| Configuration | Actual child / UTC interval | Verified archive copies / completion SHA-256 |
| --- | --- | --- |
| Debug | PID 92680; 19:31:05.566528–19:33:14.729983 | 949 / `CB8BA8317B097E69116C077E8CB926308FE5E35E26B6DE90D00F90526ED14410` |
| Release | PID 109552; 19:33:23.680480–19:35:44.672012 | 949 / `9C3FA8BDB4318FDE3F3BBEFDB47D253A60D71047527B5011E34BA759CCAA4589` |
| ASan Debug | PID 113156; 19:35:51.255802–19:37:14.008313 | 953 / `D3E15C9B296B5BA31A2C0E3405B423B8F922B9F2B2E7E74D749C3F60E2B3C814` |

Archive roots are `D:\RedPrismQualificationArchives\P1-file-session-877-ARM64-<profile>40-2026-10-06`.
Recorder/helper copies, actual process receipts, immutable source manifests, compiler logs, images/PDBs and
archive hash readbacks are retained. These cross-builds do not qualify an unexecuted x64 application gate.

## Full repaired-source Debug qualification

The actual r278 child PID 106536 exits 0, `2026-10-06T19:29:45.714265Z`–
`2026-10-06T19:46:28.677744Z`, on the 877-input `14178E…` freeze above. Full canonical `test.ps1`
runs all four suites, both crash probes, command-line/WARP application controls, RAW corpus controls and
all harness screenshots. Independent registry r4 (`818C2CC74C8C9449164AE27C2FAAED0726927F946F091A71DA07F0CF650DFABF`)
checks exact source/product/PDB identity, every predecessor and each expected addition: 7,675 Host,
7,225 Document, 1,694 Composition and 1,736 I/O checks; 10,394 named application controls over 98 stages.
The 236 new application controls include 108 file session-end, 48 clipboard and 80 Clone/folder checks.
There are 16 new protocol controls plus the two repeated string-resource labels. These are functional
checks; no measured G1 responsiveness or production resource activation is implied.

Debug r278's 1,163-copy archive verifies at
`D:\RedPrismQualificationArchives\P1-file-session-877-Debug278-2026-10-06`, completion
`1E47F689FADC24068558420495B65C8A5E4D191451FD56D6589F536AA10C4F9B`, verified
`2026-10-06T19:47:01.9084336Z`. Full Release r279 starts on the unchanged source at
`2026-10-06T19:48:03.829470Z`; its result remains pending.

## Release compiler-service failure preserved

Release r279 actual child PID 83012 exits 1, `19:48:03.829470Z`–`19:51:09.896858Z`, on the unchanged
877-input freeze. Compilation stops at C1090 PDB API error 23 / `0x000006BA` in `Renderer.cpp` and `Main.cpp`;
no runtime suite starts. The failure does not establish a product-code defect or a runtime pass. The
1,158-copy archive verifies at `D:\RedPrismQualificationArchives\P1-file-session-877-Release279-PDB-API-failed-2026-10-06`,
completion `374D889D5169B24078D8BD06FBF396DF3D24CC76F0275AC3C33D60422AC8AF9F`, verified
`2026-10-06T19:52:56.3673574Z`. No compiler/build process remained in the inspected process inventory.
Fresh Release r280 uses the same source with compiler parallelism reduced to two. There is no source or
assertion change and no retrospective claim for r279.

## Full repaired-source Release qualification

Release r280 actual child PID 91668 exits 0, `2026-10-06T19:53:19.254376Z`–
`2026-10-06T20:00:18.631187Z`, on the unchanged 877-input freeze. The full canonical runtime, crash,
RAW corpus and harness screenshot gates pass. Independent registry r4 verifies the same 10,394 named
application controls / 98 stages, 236 expected additions and exact predecessor inventories as Debug.
Its 1,161-copy archive independently verifies at
`D:\RedPrismQualificationArchives\P1-file-session-877-Release280-2026-10-06`, completion
`22DAF5DB8716F67CA2DD8285B1B3294B9270DB9C21D9B9777498748B6D67C8C3`, verified
`2026-10-06T20:00:59.0177598Z`. The r279 compiler-service failure remains preserved.
Real ASan r281 is next on the unchanged source.

## Full repaired-source ASan qualification

Actual child PID 72324 exits 0, `2026-10-06T20:01:23.688072Z`–`20:17:24.044878Z`,
on the same 877-input `14178E…` freeze. The full canonical gates pass, including the real
`AddressSanitizer: heap-use-after-free` detection probe, concurrent raster/worker stress, crash probes,
10,394 named application controls / 98 stages, RAW corpus controls and harness screenshots.
The ordinary suite counts are 7,676 Host (including the existing ASan-only control), 7,225 Document,
1,694 Composition and 1,736 I/O. Raster stress retains its one control; worker stress has 8,073 controls.

Independent registry r4 initially refused the worker-stress inventory because it omitted the new Host
controls when checking `HostTests --stress`, which reruns the complete Host registry. Direct comparison
finds zero removed controls and exactly the same 16 worker-write protocol and two resource additions.
The preserved r4 failure receipt and r5 verifier (`752ECA8DEB1744AE43E35B82F96AD39B150E0BD1588C8C76225EB59C5D22F965`)
require those exact additions and all original controls; no product source or runtime expectation changes.
Registry r5 passes source, product/PDB, predecessor, stress and application inventory verification.

All 1,165 archive copies independently verify at
`D:\RedPrismQualificationArchives\P1-file-session-877-ASanDebug281-2026-10-06`, completion
`4A9FA6CB9D524C2D8CD3D29C87DDA4D3A5C21D32028A6B0C02B7787F5A94FF47`, verified
`2026-10-06T20:18:51.4850663Z`. Release analysis r282 starts on this unchanged source at
`2026-10-06T20:19:33.294658Z`, alone with compiler parallelism two. Two actual descendant compiler
response files containing `/MP2` and `/analyze` are captured with hashes and process identity.
The user subsequently requests all work stop. Actual analysis child PID 67664 exits -1 at
`2026-10-06T20:43:28.106340Z`; the interrupted run is preserved as
`P1-file-session-877-Analysis282-user-stopped-2026-10-06`. It is not an analysis pass.
The [restart checkpoint](P1Pause_2026-10-06.md) records the remaining qualification and pending drafts.
