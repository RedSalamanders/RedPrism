# P1 project durability and session-end faults

Status: the current 1,100-input F346 checkpoint is qualified in full x64 Debug r593, Release r594, real ASan r595, Release analysis r596 and ARM64 r70 Debug/Release/ASan cross-builds. Main independently verifies exact archived sources/products, actual successful child results and original sanitizer diagnostics. Formatting (970 files), all 15 skills and diff checks pass. Earlier failed evidence is preserved. The next 22 recovery/native-error cases and separate deadline-policy draft remain uncompiled. P1.7/F6 remains active.

## Checklist

- [x] Review the optional per-save durability seam against actual manifest/ZIP flush, checked close, commit and replacement ownership. Preserve default real-operation behavior and existing stage slots.
- [x] Review all seven helper candidates, format their explicit C++ files, parse the project XML and widen every old fault visitor to the appended stage count.
- [x] Review the independent helper fixture: eight failures, two successful replacements and two pre-cancelled controls; 87 expected assertions plus its fixed summary.
- [x] Prepare the original host writer's per-job fault policy and 20 actual session-end fault cases: 12 ZIP / eight folder cases, 248 new expected assertions. Retain all 12 original cases / 108 assertions.
- [x] Independently review and rerun the cheaper agent's 13 host-verifier rejection controls, authenticate fixture/manifest pins and match its original 108 records to the actual qualified Debug r575 Application log. Parser controls are not native proof.
- [x] Integrate after the frozen frame/prepared-display source qualification and pushed checkpoint `9165ba1`; verify all eight existing native seeds, all ten candidate files and the unchanged preceding 1,098-input closure before copying. The two added fixture files make 1,100 native inputs.
- [x] Compile and execute actual I/O r584 and Application r585 on repaired Debug build r583; authenticate original receipts/source/products and preserve complete 1,737 I/O / 6,315 Application predecessor streams outside the exact new records. This is focused Debug acceptance only.
- [x] Preserve and authenticate failed full Debug r586 before repair; trace the Gradient test gate's retained manual-reset signal, which permits the second preview to complete before its owner inspects the first publication.
- [x] Qualify the auto-reset Gradient test gate in full Debug r588 with unchanged independent endpoint pixels, history/ownership checks and complete passing inventories. Remaining configuration qualification stays open below.
- [x] Main-review the next four unintegrated original-host cases: two postpublication cleanup failures and two final failed rollbacks; 58 additional expected assertions. The cheaper-agent sidecars' 18/23 parser controls pass independent main review, including the exact 356-record native predecessor and draft 384/414-record inventories. Native integration/execution remains separate.
- [x] Qualify this repaired checkpoint in full Debug593/Release594/real-ASan595, analysis596 and ARM64 r70 Debug/Release/ASan cross-builds on F346; preserve every earlier failure and actual sanitizer probe. This closes only the bounded checkpoint, not F6/P1.
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

## Native integration

Main integrates both overlays together after all r67 children join and their archives/products authenticate, then verifies the exact copied bytes. `.build/P1Drafts/ProjectHostDurability-r2/Main-native-integration.json` records all ten file identities. Native compilation and execution remain pending; the prepared inventories are not runtime proof.

Focused Debug build r581 launches only after the qualified checkpoint is pushed and the copied native bytes
are checked. Its new source freeze is `B702B77B60F755F4FAC229C55D2364B65C5E66E65285BEECAA1A4A2293C099E8`
(1,100 inputs). Main composes the independently reviewed fixed helper/host inventories with complete predecessor
streams and the existing frame/smoke/actual-sanitizer controls in `ProjectDurabilityQualification-r1`; importing
and checking the composition is checker preparation, not runtime proof.

### Preserved first native attempt and fixture repair

Debug build r581 exits zero (actual child 81432, 16:39:10.810935–16:45:50.984377 UTC); main authenticates
1,100 source / 338 evidence files in `D:\RedPrismQualificationArchives\P1-project-durability-1100-Debug581-build-qualified-2026-10-09`.
Its completion SHA-256 is `1744063760F968E9B879CD7A418EB82BC310DDDF143E10C247D73FD740E95BBB`.
Focused I/O r582 then exits one (child 27340, 16:46:34.642492–16:46:41.237203): all 12 new cases fail
`fixture-ready`, so no controlled fault is qualified. The original binary remains unchanged
(`A9EA0ECC7EA7C3E820DBC2C035496E5F132E833F6ADAD8B275C726AF917E1D5C`). Main preserves/authenticates
`D:\RedPrismQualificationArchives\P1-project-durability-1100-Debug582-Io-failed-2026-10-09` before editing;
its completion SHA-256 is `D016475C051D709C58DE92DC1E3A026DBE35C16B8D6123D76E8858FB8101CDA8`.
Archive PASS is preservation only, not a passing I/O result.

The fixture's long root label plus full GUID produces nested asset codec temporary names of 262–274 characters.
Main shortens only that exclusively created root to `PD-` plus eight GUID digits, retaining bounded exclusive
collision retries and exact canonical-root cleanup validation. The I/O harness lacks the application manifest's
long-path declaration. This is the setup failure hypothesis; repaired native execution is still required.
Literal old/new document IDs, metadata, 3×2 pixels, every 87 assertion and the summary remain unchanged; no backend,
production path policy or legacy golden is modified. The new reviewed helper overlay is `ProjectDurabilityFaults-r3`,
fixture SHA-256 `C4039A96020F17F35D71F6C615E017B9C2858DB70BF60293530100C153F4B8E6` and manifest
SHA-256 `79EBD09EEED7C1BFDA08B306CD397BB6C6EF2243EE7E10EDBEC45BC3BECD2E47`.

Debug build r583 is running on repaired 1,100-input freeze
`1615FD80D40F7A6B9D756D2B04DA95ED4CEEA805E6A44432E6D2DC79A452B88E`.
`ProjectDurabilityInventory-r2` changes source/manifest/registry pins only, preserves the exact independently
fixed 87 assertions and summary, and passes its 13 parser controls. `ProjectDurabilityQualification-r2`
retains the old profile/focused checker mechanics and adds only the repaired freeze and native fixture binding.
These are checker preparation, not runtime or F6 closure.

### Repaired focused I/O acceptance

Debug r583 exits zero (actual child 37268, 16:49:48.682662–16:55:39.993761 UTC), on the repaired 1,100-input
freeze `1615FD80D40F7A6B9D756D2B04DA95ED4CEEA805E6A44432E6D2DC79A452B88E`. Main authenticates its
1,100 source / 338 evidence files in `D:\RedPrismQualificationArchives\P1-project-durability-1100-Debug583-build-qualified-2026-10-09`;
completion SHA-256 `AA964EF0F6FCD144D1004893E1E71E95A2262E71CB7191EBC105B2F7F5A6B257`.
Focused I/O r584 exits zero (child 83724, 16:56:39.941525–16:56:46.508195 UTC) with unchanged actual binary
`B7E6E8E6EC1DE7DD073A140B38EA8DC89732D70C2C5FDF4D02FACE46C043B59A`.

Main's focused checker authenticates both build/runtime archives, matches their actual binary and identical
source freeze, validates every 87 helper assertion plus summary and preserves the complete original
1,737-record I/O stream (1,825 current records). All 12 setups now pass after shortening only the exclusive
scratch root; this supports the path-headroom repair without altering production or independent expectations.
Focused runtime archive: `D:\RedPrismQualificationArchives\P1-project-durability-1100-Debug584-Io-qualified-2026-10-09`;
completion SHA-256 `06C80F0702B261316AA1716FA296D7EB05BF4FEE3D5A1C0A358DF999E7896CEB`.
Report: `.build/P1Drafts/ProjectDurabilityQualification-r2/Main-Debug584-Io-profile.json`.
Actual Application r585 is now executing on the same frozen build. The 248 new host assertions, complete
source configuration matrix, broader F6 and P1 Done remain separate and open.

### Focused original Application acceptance

Actual Application r585 exits zero (child 85924, 16:58:14.870190–17:01:01.978550 UTC) on the same repaired
freeze and unchanged preserved r583 binary `90D72DB1A809BCCD4FF6F077EEF24C55FBDCAB714A28BEC35B2F96C87E901880`.
Main independently authenticates the runtime and build archives, then checks every 356 file-end-session record
(108 original / 248 new), the complete original 6,315-record Application stream (6,563 current) and all 121
unchanged stages. Worker-only waits, TRUE/FALSE behavior, exact native observer faults, ordinary failure receipts,
localized notices, old literal ZIP/folder content and original ticket/resource retirement pass in actual host calls.

Archive: `D:\RedPrismQualificationArchives\P1-project-durability-1100-Debug585-App-qualified-2026-10-09`;
completion SHA-256 `1C3CE454E1E61DA69E41580298B811579056F9B34167E7DC18242EC1C7222C47`.
Profile report: `.build/P1Drafts/ProjectDurabilityQualification-r2/Main-Debug585-App-profile.json`.
Full Debug/Release/real-ASan, analysis and all ARM64 cross-builds remain due on this exact source before commit.
The main-reviewed r68 ARM scripts are prepared but unlaunched; they differ from qualified r67 only in explicit
source/count/run identity literals. Main independently rehashes/normalizes all three scripts and all 1,100 live
inputs. Broader F6, ordinary activation, measured G1 and P1 Done remain open.

### Preserved full Debug failure and Gradient test synchronization repair

Full Debug r586 exits one (actual child 49496, 17:03:53.344664–17:14:06.968108 UTC) on freeze `1615FD80D40F7A6B9D756D2B04DA95ED4CEEA805E6A44432E6D2DC79A452B88E`. Build/ISA and Host/Document/Composition/I/O gates pass, then the actual Application exits five after `gradient-host.dependent-invert-cancel`, before `gradient-host.latest-preview`. No diagnostic identifies which old compound predicate failed; preceding Shape residency PASS diagnostics are not the failure.

Main authenticates all 1,100 sources / 347 evidence files in `D:\RedPrismQualificationArchives\P1-project-durability-1100-Debug586-full-failed-2026-10-09`; completion SHA-256 `215D332D9907194AA61179F910F38FE463BA85FCD08B5D3F20093A0689969916`. This is preserved failure, not qualification.

Code review establishes that the manual-reset Gradient release stays signaled after the first held preview, allowing the second worker to finish before the owner inspects the first publication. Main makes the harness event auto-reset: each visit consumes its own release under the existing single-worker mailbox. Cancellation/shutdown retain their separate event/acknowledgement protocol. Failure-only diagnostics now identify every coalesced state predicate and latest output bytes. Independent 85/170 intermediate and 128 latest endpoint bytes, history/ownership conditions and all passing records remain unchanged. Actual repair qualification is still pending.

The revised 1,100-input freeze is `A9FA8B5A78ED54037432587DA0B9724A6FBC4A0C55923906AD8F1E2F24449442`. Main compares all source entries: only `GradientWorkTestGate.h` and `ApplicationGradientTests.cpp` differ from r583. The r3 profile/focused checkers retain the exact preceding parser, expected stream and predecessor mechanics, changing only this freeze and adding the two reviewed native bindings. Earlier r2/r68/matrix scripts are preserved and must not launch on this changed source.

Debug build r587 exits zero (actual child 22228, 17:21:14.912342–17:26:45.640962 UTC), including the linked ISA
and guard rejection controls. Main authenticates all 1,100 sources / 338 evidence files in
`D:\RedPrismQualificationArchives\P1-project-durability-1100-Debug587-build-qualified-2026-10-09`;
completion SHA-256 `DF6FD46920B0E687014F21787D29B971FE3982923C2DE0898DBB45C324E9187D`.
The reviewed r2 serial driver starts full Debug r588 before Release r589, real ASan r590 and analysis r591.
Each phase joins, archives, authenticates and validates the complete predecessor/profile before the next phase;
any actual failure is preserved and stops this sequence. The r69 ARM64 drafts independently reverse-normalize
to the previously reviewed r68 scripts and bind all 1,100 live inputs; they remain unlaunched until x64 joins
and passes. Cross-builds do not establish native ARM64 runtime.

### Next original-host cleanup and failed-rollback cases — unintegrated

Main prepares `.build/P1Drafts/ProjectCleanupRollbackHost-r3` with three candidate files and exact native seed
pins. Two TRUE/FALSE postpublication cleanup cases deny only the second backup-cleanup operation; they require
one Published fact, a succeeded original receipt, the literal new project, an intact reloadable old backup,
saved authority and no false failure notice (28 expected assertions). Two TRUE/FALSE failed-rollback cases deny
staging publication followed by rollback; they require both exact ordered errors, no publication/recovery timer,
the byte-exact reloadable old backup, truthful destination absence, owned staging retirement, unchanged dirty
source/path/history and one localized failure notice (30 expected assertions). Ordinary policy stays empty;
all preceding case meanings and the first 356 current records remain unchanged.

The original cleanup-only sidecar's fixed 384-record draft inventory retains the exact 356-record native r585
prefix; main independently runs all 18 parser/rejection controls and verifies candidate/manifest/baseline hashes
and byte-preserving passthrough. These are checker controls, not 28 new native assertions. An extended sidecar
for the combined 414-record expectation is delegated to a cheaper agent for main review. None of the four new
host cases is integrated, compiled or executed yet. Failed draft-creation anchors in `ProjectCleanupRollbackHost-r1`
and `-r2` are preserved; only `-r3` has a complete candidate/hash manifest. Native recovery exhaustion/timer
failures, codec failures, combined deadlines and faulted late completions remain open.

### Full repaired-source Debug acceptance

Full Debug r588 exits zero (actual child 37196, 17:27:28.224840–17:39:09.792121 UTC) on the repaired
`A9FA8B5A78ED54037432587DA0B9724A6FBC4A0C55923906AD8F1E2F24449442` freeze. Main authenticates all
1,100 sources / 349 evidence files and independently runs the complete profile projection: Host 8,494,
Document 8,778, Composition 5,963, I/O 1,825 (1,737 original plus 87 assertions and summary), Application
6,563 (6,315 original plus 248 host assertions), the full 356-record session-end namespace, all 121 stages
and the actual direct/prepared 27-operation and sampler smoke rosters. `gradient-host.latest-preview` passes
with the unchanged intermediate/latest literal bytes and ownership predicates; no existing golden or passing
record is changed. This qualifies the identified gate race repair on Debug; wider configuration proof remains due.

Archive: `D:\RedPrismQualificationArchives\P1-project-durability-1100-Debug588-qualified-2026-10-09`;
completion SHA-256 `2E66031A88988B5987EA32B6CF8836A9FA232065B1439C59FBEE83978D01E1DF`.
Profile report: `.build/P1Drafts/ProjectDurabilityQualification-r3/Main-Debug588-profile.json`.

The first serial controller failed *after* native Debug passed: capturing launcher stdout with an anonymous pipe
kept its EOF open in the hidden recorder; `OpenProcess` then returned error 87 after that recorder exited.
Its driver, launch logs and failure explanation are preserved in `ProjectDurabilityMatrix-r2`. This is a controller
failure, not a native test failure. Main archives/authenticates the original r588 result and prepares r3 with
file-backed launcher stdout/stderr, the same exact native receipt/profile gates and an already-joined-receipt
fallback. Remaining Release r589, ASan r590 and analysis r591 start serially from the authenticated Debug proof;
Debug is not rerun. Each next actual failure is preserved before repair or advance.

Main independently reviews the extended cleanup/rollback sidecar and runs all 23 parser controls (actual exit
zero), authenticating all three candidate sources/manifest and the exact r585 stdout. Its 414-record inventory
retains the draft 384-record prefix and native-qualified 356-record prefix; the 58 appended assertions remain
uncompiled expectations. Parser stderr SHA-256 `EEACC1043CD8C24373EFE98DBC6C6318EF3FEA89D3533DEB9FD4F2D764BCEB8D`.
Broader F6, ordinary activation, measured G1 and P1 Done remain open.

### Release acceptance, preserved ASan ISA failure and strict scanner repair

- [x] Full Release r589 on the Gradient-repaired A9FA freeze: actual child 51888, exit zero, all 121 stages, 87 helper assertions plus summary, 248 added Application assertions, exact 356 session-end records and complete predecessor/direct/prepared smoke inventories. Archive completion `8EBD89CDAFD5ABDFFD1160BCAFB14050C8E21E9FB99724F5B989004E81CF6CE7`; independently read back 1100 sources and 333 evidence files. This is Release acceptance, not full-matrix/P1 completion.
- [x] Preserve actual ASan r590 pre-runtime failure: child 82704, exit one, zero stages. Archive completion `C78D69AAD4F3650061FC0C5E458D5258DA6E00C239C4AF33F4C312E3EDF127B3`, 1100 sources / 330 evidence files. Original image `DEFB410C768C994C8838FCB2543024887AB154CA9C44FAC428D809E5A1323A4A`, PDB `D36ECC81BAAFBB0794F1AC5A5F4CBA59E55D5233BE4D650F19A674247811521F` and original WindowHost COFF object `7E4579A2BB1DF2F2EA7F454E3A1F643482C0CD16915A59BA7335D4E191FD1ED0` remain retained. Six DWORD RVAs `015C25C5` decode as VEX but point to a scalar branch in the exact owning procedure; no runtime test or sanitizer probe ran.
- [x] Main's bounded scanner repair: observed disp8 DWORD CMP/unsigned short JA/zero-extending MOV EAX from the same stack slot, exact four-row RCX dispatch, actual PE/listing/nonoverlap agreement, known target/default boundaries, anonymous same-owner PDB data, exact terminal procedure end and a matched RET immediately before the table. Existing forms stay unchanged. Sixty independent parser controls include range limits, damaged/missing/unknown/overlapping encodings, wrong slots/registers/targets/defaults/PDB ownership/extents, missing/damaged return and genuine adjacent VEX rejection. Every preceding inline-table control also passes.
- [x] Rescan original retained ASan image/PDB/listings: 6,093,782 decoded rows, 11,690 runtime vectors unchanged, zero violations; inline-table rows increase only from 16 to 22. Parser SHA-256 `60BFBE4FE8835AE7A0BE2253125776CE7316EA178AD997B243033B1AFC9E3764`; ISA allowlist stays `7870AF49195841A9B9B88DFD15378D550B127754683666C83AED83753FD80E1E`. This is parser proof, not ASan runtime qualification. DxUi source and API3 pin are unchanged.
- [ ] Qualify fresh native full Debug r593, Release r594, real ASan r595 and analysis r596 after Debug build r592 joins/authenticates. Revised freeze `F34690DD470BEC8F7A09AA1C96B3B170F877A7232B01FD3029B4342A0E29264D` retains the same 1,100 ordered inputs; only `Build/IsaScan.cs` and the existing `IsaInlineTableTests.ps1` differ from r587. Current full-profile checker expectations and goldens are unchanged; all 14 integrated native bindings are pinned. Earlier A9FA qualification remains historical evidence.
- [ ] Qualify revised-source ARM64 cross-builds after the x64 matrix joins; native ARM64 runtime remains unavailable. Commit/push only the qualified checkpoint. Remaining F6, ordinary activation, admission/lifetime costs and measured G1 still block P1 Done.

Archive roots: `D:\RedPrismQualificationArchives\P1-project-durability-1100-Release589-qualified-2026-10-09` and `D:\RedPrismQualificationArchives\P1-project-durability-1100-ASan590-failed-2026-10-09`. Main's preserved-image proof and parser logs live in `.build/P1Drafts/IsaUnsignedStack-r1/`; checker/controller adaptations in `ProjectDurabilityQualification-r4` / `ProjectDurabilityMatrix-r4` are preparation only until actual receipts and authenticated reports exist.

### Revised build and next recovery cases

- [x] Revised Debug build r592: actual child 75712 exits zero (18:11:15.800495–18:16:59.286528 UTC) on the F346 freeze. All linked scans and original/new rejection controls pass. Main independently authenticates the completed archive before launching r593; completion `F0CB2F167725F252F163B1A6BF8FDFDDBCB76C81AFBC28BC6D526864A9BB8A96`, source manifest `AA4BC5A8B610607145370E560372D479D12F5E14FC26816B40032FCD15AE56D1`, evidence manifest `B492BA5EDC8ED4045A8EE53BF85F989A5A6407237EA2A6E9FE2B36736A0FB758`. Archive: `D:\RedPrismQualificationArchives\P1-project-durability-1100-Debug592-build-qualified-2026-10-09`. Full Debug r593 is running; this build is not the runtime matrix.
- [x] Prepare and review eight additional original-host recovery cases in `.build/P1Drafts/ProjectRecoveryHost-r2/`, after the four cleanup/failed-rollback drafts. A worker-local real backup manifest reader omits DELETE sharing; actual native rollback must fail through two retries or selected timer create/arm/wait refusal. The reader retires before acknowledgement. An independent postack backup directory rename/restore proves retirement; old tree bytes/IDs/literal pixels, exact retry/error ordering, failed original receipt, one localized notice, destination absence, dirty source/path/history and all ticket/resource owners are checked. Healthy policy remains empty. No native backend edits or execution yet.
- [x] Main review of the cheaper inventory sidecar: exact preceding 414-record draft prefix, eight new cases / 136 expected assertions / 550 total records in 44 cases. All 20 supplied parser controls pass in main's fresh execution; 11 additional main rejection controls cover unknown/embedded/duplicated/misordered records, baseline/registry/model mutations, missing newline and substitution of each actual secondary source. Byte-preserving passthrough also passes. The existing 356-record baseline is the only native-proven prefix; the later 194 records remain draft expectations. This is parser proof only.
- [ ] Integrate and execute the next reviewed cleanup/rollback/recovery candidate after this checkpoint qualifies and commits. The first recovery draft is preserved with a use-before-declaration found in review; r2 puts the immutable mode at loop start and remains uncompiled. Candidate hashes: gate `B6400C03191BD2A55186599185990E9A66828F7391CC364698F3CEDD47E61624`, job `CFF055F3125E05C75C17D1AA5D5DD0B762FDA48988A4D1C95DEBC753B108338A`, fixture `32EC2163392DFC851310EBD114FD57D76DEA5EBF417B674A60B939EA2AA6F249`. Main's sidecar review is `.build/P1Drafts/ProjectRecoveryHostInventory-r1/Main-review.json`; registry `9EECDF43C594E075A97EABB556BCCAA346362D8E714E3676BE8B4CFC0F2693A9` and checker `75CB86A3A91DB443C9423DE3B33A706B65AACF8891EDAEA34CC591716CD845B0` stay pinned.

Remaining F6 follow-ups retain their owning policy: a timed-out original writer has a cancelled stamp, and an unpublished late error must not acquire saved/path authority. Validate the cancellation/failure race and its notice policy against the actual owner before choosing new expected records. Current continuations serialize original writers behind one active ordinal/history stamp; establish actual two-writer reachability or label the shared 1500 ms deadline test as policy-only. Actual host asset/export errors and these late-completion/deadline cases remain open. No P1/F6 closure is claimed.

### Revised-source runtime progress and late-completion draft

- [x] Full Debug r593: actual child 6684, `2026-10-09T18:17:27.686252+00:00`–`2026-10-09T18:28:40.927307+00:00`, exit zero, 1100 source / 351 evidence copies authenticated, 121 stages and exact current/predecessor records. Completion `BCE10FA8AC167FFC6F5F3C1FD5EBE8D155FB5ADC25CE3A20E70AE55F809CFB87`. Both direct/prepared 27-operation and sampler smoke routes pass. Profile: `ProjectDurabilityQualification-r4/Main-Debug593-profile.json`.
- [x] Full Release r594: actual child 92008, `2026-10-09T18:29:09.176185+00:00`–`2026-10-09T18:36:45.139620+00:00`, exit zero, 1100 source / 335 evidence copies authenticated, 121 stages and exact current/predecessor records. Completion `3E998D9C46E679B6DDB25D96761FB87DCD578894A12F0C670E87B4761322FD15`. Both direct/prepared 27-operation and sampler smoke routes pass. Profile: `ProjectDurabilityQualification-r4/Main-Release594-profile.json`.

- [ ] Finish actual real ASan r595, analysis r596 and reviewed ARM64 r70 cross-builds on this freeze, then commit/push the qualified checkpoint. ASan has cleared the exact failing-image scanner and Host suite; this is progress, not sanitizer qualification.
- [x] Prepare and review two original post-timeout fault cases: ZIP manifest close and folder staging publication. The existing immutable-thread message/release gate holds the real operation boundary; release occurs only after WM_ENDSESSION returns and has cancelled the original stamp. Worker facts record cancellation before delivering the exact I/O error. Expected ordinary receipt is quiet Cancelled/E_ABORT with no late path/saved authority. All previous 550 draft records are unchanged; two cases add 28 expected records (578 / 46 cases). Main reruns all 24 cheaper parser controls plus 11 independent rejection controls and byte-preserving passthrough. No native integration/execution yet.
- [ ] Integrate/qualify this reviewed 14-case candidate after the current checkpoint commits. `.build/P1Drafts/ProjectLateFaultHost-r1/candidate-hashes.json` is `741ECAF33368554D21CEEF7E5EF43865A05FEF4E002850197DE7DACBD7AFD19D`; fixture `70DC560BD07A9D90978BCC8A687021FA3D48DD365961149088A1B97588F0DF6E`. Registry `EF5106003C9474095D331FC5DB041278D7A1B50BE6489AC5FE46A1E72DBCAD0B` / checker `97A4AE4640AAE1C5978BA8D1F92C92EF69E19C65297D82426861B69A2F1A34C8` and main review in `ProjectLateFaultHostInventory-r1` remain draft proof. The pinned 356 baseline alone has native execution; later 222 expected assertions are not native coverage.
- [ ] Main-review the four new real locked PNG/JPEG output candidates in `.build/P1Drafts/ExportNativeFailureHost-r1`. A real reader denies DELETE sharing through the original admitted export/readback/encoder graph; no synthetic error is returned. Both TRUE/FALSE session-end variants must preserve old bytes/reloadable pixels, one codec attempt, failed receipt/format notice, dirty source/history/path and exact ticket/GPU/CPU retirement. Five candidate sources are prepared; expected 60 additions / 638 records in 50 cases are uncompiled and unqualified. Actual project-asset errors and the separately labelled shared-deadline policy/reachability work remain open.

### Current sanitizer qualification and next native candidate

- [x] Real ASan r595: actual child 37424, `2026-10-09T18:37:06.497758+00:00`–`2026-10-09T18:57:34.026959+00:00`, exit zero; all 1100 source and 333 evidence copies authenticated. Completion `B60879102B5B75CA55B5E0566E32301FD4CA7E195A46D9C380A0B1AFB2CB3647`, source manifest `88F7F3D6D2B70D833E73E151C7A6ACA9CE15D401EC95CEB941999A1FB3B82ECA`, evidence manifest `6A547C0906321A854919D8B54565415D4CBF4B5CE8B825A611FC045634CD93C4`. Main independently verifies both Host executions, original sanitizer probe diagnostic, 121 application stages, 6,563 application records, all 356 session-end records, raster stress and direct/prepared functional smoke routes. `ProjectDurabilityQualification-r4/Main-ASan595-profile.json` records the source-bound predecessor projection. This is functional sanitizer qualification, not F6 closure or G1.
- [ ] Release analysis r596 is running; all three reviewed ARM64 r70 cross-builds remain due before committing this checkpoint.
- [x] Main independently reviews the complete next five-source native candidate and all 24 cheaper parser controls plus 13 additional refusal controls, including actual substitution of each secondary source. The exact preceding 638-record prefix is preserved; four native project-asset collision cases add 56 expected records. `ProjectNativeAssetFailureHostInventory-r1/Main-independent-review.json` is main-owned; the agent's earlier `Main-review.json` remains separately preserved. Final registry `665155D86F4053AF59D42711B5145900AD3B19CC55B898A9458D6F8C042766B6`, verifier `5B5A97847204B13B99DF31FB9CFD1B9106605F699C7D94C971A6CE2A446B96A3`, candidate manifest `469F4F6D0A7FCF60F3A3970289474FCC66DB60C0398987D622D51B64C6FE258C`.
- [ ] Integrate and execute all 22 next cases only after the current checkpoint commits: published cleanup refusal, failed rollback, real rollback readers and timer refusals, late original timeout errors, actual locked PNG/JPEG destinations, and actual first-asset destination directories. The current 356-record baseline alone is native proof; all later 338 expected records remain uncompiled drafts. Shared-deadline policy and original two-writer reachability remain separate open work.

### Current analysis qualification and remaining cross-builds

- [x] Release analysis r596: actual child 50864, `2026-10-09T18:58:07.986353+00:00`–`2026-10-09T19:09:23.538401+00:00`, exit zero. All 1100 source/349 evidence copies authenticate; completion `4C5549C7F4C1653F0FE462038557EE79E03FAB4E1A7B4CC2296447AE0A9E2E78`, source `B61C5C7BCF0DC19F99B4A8C930B7E0657521C5334C27F4C8D8976431AF1370A7`, evidence `5268D4ACB9BA4AA19382D624E86FC7DBB6EB4155F90976650EAE38E5F5930AA6`. Original source/pin/allowlist bounds are unchanged. The recorded full x64 matrix supervisor joined successfully after this analysis archive.
- [ ] ARM64 r70 serial cross-builds are launched on the exact F346 source only after all actual x64 profiles/analysis have passed, archived and joined. Main rehashes all 1,100 inputs and all three reviewed ARM scripts before launch. Runtime on native ARM64 remains unavailable.
- [x] Main independently reruns the cheaper complete-application projection checker and ten additional refusal controls. Its five-source bytes, 694 namespace records, fixed 338 additions and unchanged 6,563-record actual same-profile predecessor are bound. The synthetic expected total is 6,901 records; `ProjectSessionRecoveryProjection-r1/Main-independent-review.json` explicitly records preparation only. No new native execution follows from these parser controls.
- [ ] Main has prepared a separate eight-control shared-deadline policy draft. It labels its two completion records and invalid/unregistered tickets as synthetic and exercises actual WM_ENDSESSION TRUE/FALSE plus the same absolute deadline forwarded to both native waits. Ordinary save/export admission serializes original writers; the draft cannot establish two simultaneous original writer graphs or G1. `.build/P1Drafts/SharedEndSessionDeadline-r1/Main-preparation.json` remains unintegrated and uncompiled.

### Current source: complete configuration qualification

- [x] ARM64 Debug r70: actual child 40540, `2026-10-09T19:10:52.244337+00:00`–`2026-10-09T19:12:02.068605+00:00`, exit zero; 1,100 source copies and 75 evidence copies independently read back. Five ARM64 PE/PDB pairs, 0 real sanitizer compiler/image controls. Completion `E9E25705885520F9237DBB82AF32908E40F1A6E4C8AD0176CFD1C7F40590FBB9`, source `BDEB0FC9DAA6321A663EC8F04CB8875E96C2FFB8883342750B737AECA92D2043`, evidence `CBBE7D1CB5CEC613826F0BC8FE1CE3EF33C7A1A67FE443301E5BFFEFCC121B08`. Main reruns the unchanged detailed verifier in `Arm1100-r70/Main-independent` after the driver's own verification. Native ARM64 runtime is unavailable.
- [x] ARM64 Release r70: actual child 66556, `2026-10-09T19:13:10.669036+00:00`–`2026-10-09T19:15:07.863459+00:00`, exit zero; 1,100 source copies and 75 evidence copies independently read back. Five ARM64 PE/PDB pairs, 0 real sanitizer compiler/image controls. Completion `F2D9B09E7AB945638130A3984ED7DBECA0223F689E6B304D826A9A4243B6FC54`, source `BB241679196488290ED337E7D5204ECEC06083C1657905C76B0A02A8752E16AA`, evidence `5316D04A675EE8E906CDC286319A5CEA7BC48B99012B0894CA81555A71C087E8`. Main reruns the unchanged detailed verifier in `Arm1100-r70/Main-independent` after the driver's own verification. Native ARM64 runtime is unavailable.
- [x] ARM64 ASan r70: actual child 90228, `2026-10-09T19:16:21.095439+00:00`–`2026-10-09T19:17:23.943984+00:00`, exit zero; 1,100 source copies and 79 evidence copies independently read back. Five ARM64 PE/PDB pairs, 5 real sanitizer compiler/image controls. Completion `8104BE4BC7419E347FABC0F3EB3B095D5DD330A256B906BA54D7C3B5A86761C9`, source `6CFA336D7F6BA15E3A69875CF6DD9A0C31D7DCA827D04BEB05DF490C2A2773D3`, evidence `A4F70C2C6A23B4F8F84F0D4AFBD7607EF89C5EFB2DC2DE1EF53242CBB9F5A6FC`. Main reruns the unchanged detailed verifier in `Arm1100-r70/Main-independent` after the driver's own verification. Native ARM64 runtime is unavailable.
- [x] Complete the bounded durability/scanner checkpoint on F346: all required x64 profiles, actual original sanitizer diagnostic, Release analysis, all three ARM64 cross-builds, 970-file format, 15 skills and diff checks pass. `Arm1100-r70/Main-independent/Main-closeout.json` binds all actual results and explicitly leaves P1/F6/G1 open.
- [ ] Continue with the five-source 22-case native candidate after committing/pushing this qualified checkpoint. The separate deadline policy remains a later, explicitly synthetic overlapping-state test. No native expectations or legacy goldens have been rebaselined.
