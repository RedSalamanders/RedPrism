# Deferred color display primitive — 2026-10-04

## Qualification checklist

- [x] Primary implements and reviews metadata-only preparation, constant-time checked cached planning and source/preview validation.
- [x] Cheaper agent writes independent bounded tests; primary reviews all code, fixes fixture mistakes, adds hierarchy/adjustment/uniform-mask cases and registers the suite.
- [x] Preserve the failed first Debug attempt and independently verify all 1,483 archive copies before repair.
- [x] Full private Debug build/run exit 0: 6,313 existing named controls unchanged plus 79 new controls; all 1,484 archive copies independently verified.
- [x] Full private Release/ASan document suites pass the same 6,392 controls; all 631 owned inputs match and every archive copy is verified. The actual ASan compiler tracking includes `/fsanitize=address`.
- [ ] Actual session/host publication, complete admission and retirement, source/settings authority and full normal/native integration.
- [ ] Original P1 G1 measurements; this primitive establishes no application timing or milestone completion.

Plan: [DeferredColorDisplay](../Plans/WIP/DeferredColorDisplay_2026-10-04.md), part of
[P1](../Plans/WIP/UiNeverBlocks_2026-09-22.md). Private source:
`%LocalAppData%\RedPrism\PrivateWork\p1-color-display-proof`.
Archives are under `%LocalAppData%\RedPrism\QualificationArchives\`.

| Attempt | Actual scope/result | Archive | Completed receipt SHA-256 |
| --- | --- | --- | --- |
| r1 Debug | Build 0, runtime 1: boundary fixture incorrectly expected rejection at exactly 100 M pixels | `p1-color-display-r1-Debug-failure` | `ED36ECCFE4DD38548F6708D37B3B69D651081CA180C970DAAA3E1E8AF047A4B9` |
| r2 Debug | Build/runtime 0; 6,392 named controls, including 79 new independent display controls | `p1-color-display-r2-Debug-qualified-private-document-suite` | `1D23AE87612667495F4ACF196301D683FF06C7399C5A89B149824515D57DD813` |
| r3 Release | Build/runtime 0; same 6,392 controls; 1,482 copies verified | `p1-color-display-r3-Release-qualified-private-document-suite` | `B43299577273A433023F74D4C29C280FBB400775A59AFF324C6960B711693E12` |
| r4 ASan Debug | Build/runtime 0; same 6,392 controls; 1,487 copies verified; real instrumentation | `p1-color-display-r4-ASanDebug-qualified-private-document-suite` | `E154380CBDACF5F58159061CFF9C953E9D316C9AF408874673FF26CD3889CB48` |

Primary review corrected the selection-loop retirement expectation and replaced a prohibited sparse-base
chain with nested sparse replacement tiles. The worker now explicitly refuses a non-BGRA target source;
manifest validation alone does not establish that image format. The combined-pixel budget test preserves
the inclusive 100 M contract: exactly 100 M succeeds; 100,000,100 pixels refuses without assembling its
99,990,000-pixel sparse source. Existing literal pixel/history expectations are unchanged.

Independent cases cover exact target replacement and untouched document/record fields (including fields
omitted from layer equality), implicit/explicit/linked/unlinked/uniform mask placement, semantic thumbnails,
hierarchy, clipping and unrelated curves, selection/raster sharing and sparse non-materialization; invalid
source/preview/target refusals; literal planning totals and each overflow/unavailable bound; cancellation
before construction and at every preparation stage/per-record checkpoint; and reached `bad_alloc` observers
with unchanged source and successful retry.

The worker clones/seals metadata and validates the complete resulting document; sealing alone is not validation.
It runs no pixel kernel, `Data`, history mutation or GPU operation. Cached planning is partial: host capture,
settings/render allocation and replaced-display ownership must be included before actual admission. The
existing full document validator remains an unsliced worker call; cancellation is checked around it, and
this suite does not establish the original 1 ms cancellation target for that call or the UI/apply/frame targets.

## Session publication work in progress

Private continuation: `%LocalAppData%\RedPrism\PrivateWork\p1-color-publication-proof`. Primary implemented
move-only capture/publication, complete source and scalar history authority, and transfer of replaced preview
and display owners. History authority holds no undo stack: display preparation does not read that stack.
Borrowed planning now covers render scratch, settings/target copies, immutable metadata construction, the
old display and panel owners, with additional allocation and conservative retirement reported separately.
Host job/payload records must still be added before admission.

The cheaper agent supplied 60 independent session controls; primary corrected move-only test handling and
a missing selection include. The first compile failure (`p1-color-publication-r1-Debug-failure`, completed
receipt `2BFCF30BD03BD0968190FFD351553B98C40A12B2E5041F88BDE26296F42735A9`) retains 1,475 verified copies.
The second build and full runtime both exited 0 with 6,452 controls, but its archival wrapper rejected a wrong
new-test prefix in registry comparison. That qualification failure is preserved as
`p1-color-publication-r2-Debug-failure`, completed receipt
`3D9A8681DD63FF0CF317292CB13FEC49FA5CB5E98FFE56ECFC017BAC0625F3A1`, all 1,489 copies verified.
The wrapper is repaired. Those executions preceded the scalar-history/planning changes and do not qualify
the subsequent source. Isolated history invalidation, moved publication, failed-display invalidation and
borrowed-planning cases are being added before the fresh three-profile matrix. No host or timing claim follows.

## Qualified private publication core

Fresh forced builds and full document suites each execute 6,392 unchanged preceding controls plus 96 independent
publication/planning/owner-retention controls. All 635 owned `.cpp/.h/.hlsl/.rc` inputs match by individual
SHA-256. The ASan compiler command includes `/fsanitize=address` (tracking hash
`BACAE1165358AC96F0C36B3F4EEA117499BB80DA2677579321A3AB8280279974`).

| Attempt | Actual build/runtime result | Archive and independently verified copies | Completed receipt SHA-256 |
| --- | --- | --- | --- |
| r3 Debug | 0 / 0; 6,488 controls | `p1-color-publication-r3-Debug-qualified-private-document-suite`, 1,490 | `F3DB3EA1353347BBF4214DE45CAC386374FC4DFF9DA489ADB127057FE779D12E` |
| r4 Release | 0 / 0; identical controls | `p1-color-publication-r4-Release-qualified-private-document-suite`, 1,488 | `B3C000E2634824B684A432FE2E4C306364EC8ABCB027761166137F2050BE2D5D` |
| r5 ASan Debug | 0 / 0; identical controls, actual instrumentation | `p1-color-publication-r5-ASanDebug-qualified-private-document-suite`, 1,493 | `ADDBDD7DED54C673BB4EBE98A5D87D68A4D747A160219618F3E5C9256C9518FB` |

Independent cases separate history-only structural invalidation from complete-source invalidation; changing the
save marker alone keeps publication valid. Moved-from/consumed, copied/foreign session, replaced document,
changed source/history, out-of-order and intermediate settings cases preserve current data and controls.
An allocation-free failure factory transfers captured owners without copying; only a current latest failure
invalidates the shown preview. Worker cancellation/allocation observers preserve source/history and permit retry.

Primary found and fixed another final-owner path: opt-in settings ticks formerly reset grown/preview caches.
Off/identity now retain invisible handles; growth moves the old grown cache into one fixed slot, repeated growth
retains that same owner, and a matching publication moves it into the prepared retirement aggregate. Visibility
checks exclude those stale handles. A weak lookup owner remains alive across invalidation and publication, then
expires with the prepared aggregate. Default inline behavior and all existing goldens remain unchanged.

The complete CPU planner covers render scratch, settings/token copies, record construction and old display/panel
ownership without traversing undo history or materializing pixels. It reports resident retirement separately from
new allocation. Actual host job/payload records, reservation-before-capture/resume, mailbox descriptors, shader
visibility wiring and worker retirement still require integration and host tests. Direct test destruction of a
prepared aggregate does not prove retirement off the application owner. No application/G1/native activation is claimed.

## Normal integration in progress

The managed candidate now includes the core and actual host wiring. Its pending mailbox holds scalar identities;
borrowed planning precedes owning capture, an admitted record resumes worker preparation, and owner publication
retains replaced display/panel owners in the retirement payload. Cancel/off/reopen keep the exact ticket until
acknowledgement. Histogram capture uses the same admission and retirement boundary. These source changes have
not yet passed the complete normal matrix or G1 measurements.

Static analysis r14 exited 1 without runtime. All 966 source/evidence copies were independently verified at
2026-10-04T13:06:09.8653910Z in `p1-display-normal-clean-x64-Debug-r14-analysis-failure`;
completion SHA-256 `A7642BC473095890C02D80828F25C5F7D033E9D7FF9625B7F860A921541C6229`.
The run exposed further exact HRESULT comparison warnings, nullable test resources and stack-frame diagnostics.
The primary reviewed the cheaper agents' bounded comparison/test patches; pending reruns must establish which
diagnostics are cleared. Existing numeric goldens and error values are unchanged.

A primary correction also bounds failure publication: a separately reconstructed selection cannot trigger
owner-side geometric equality. It is conservatively refused unless it shares the captured selection root;
successful workers still prove equality and bind the root before publication. Three independent document
controls exercise this failure case. Fresh normal Debug r15 is in progress with sources frozen; no pass is claimed.

Normal r15 exited 1 during compilation, without runtime. The new host test called a void session API as a
predicate, and a const descriptor could not transfer into the mailbox. Every one of the 1,005 archive copies was
verified before correcting either source. Archive `p1-color-host-normal-clean-x64-Debug-r15-compile-failure`,
completion SHA-256 `AA22E929A78A4A7939A99841A157638F19B6084E023C9B35529B1857C4EFA133`.
The calls now verify `HasColorEdit` after opening; the descriptor is mutable for move admission. Normal Debug
r16 is running from a new frozen snapshot. No runtime or completed milestone is claimed by the failed attempt.

Normal r16 compiled, linked, passed the ISA/build controls and all standalone suites, including the three new
failure-root controls. The hidden application exited 5 in the first new full-display case (the wrapper exited 1),
after 57 stages. All 1,015 copied inputs and evidence files were verified before changes. Archive
`p1-color-host-normal-clean-x64-Debug-r16-full-display-failure`, completion SHA-256
`192F4393D97BC32E7F37AEF98EAAEC0E2CD69FFB2C904CAC46BBC611F32BDE95`.

The new test's reference incorrectly doubled encoded gray bytes. Primary checked
retained numeric golden cases: Exposure doubles linear light and its +1-stop gray
expectation is 176 within 2. The fixture now selects the legacy gray-128 sample and uses that existing expectation;
the sibling and unselected pixels remain literal. Failure output now reports pixels, publication and retirement
facts separately. No existing golden or processing arithmetic changed. The independent clipboard-owner fixture
also retains the Application's opt-in publication policy after resetting its session. Fresh normal Debug r17 is
running from another frozen snapshot; the new host route remains unqualified until its actual gates pass.

Normal r17 passed all standalone suites and all ten initial `color-display-host.*` cases, then failed the later
clipboard compatibility fixture. The full application reached 71 stages and exited 5; the wrapper exited 1.
Every one of the 1,015 copied source/evidence files was verified at 2026-10-04T13:56:06.9836484Z before edits.
Archive `p1-color-host-normal-clean-x64-Debug-r17-clipboard-failure`, completion SHA-256
`89594BD8975941C98F4AA6FD6DD67217672B7D3EEAAB7D4B0C316BC182410586`.
Independent standalone registry verification again confirms 6,392 preceding plus 99 publication controls;
this scoped success does not qualify the failed overall run.

Primary reproduced the clipboard fixture defect with a read-only native probe (no clipboard contents changed):
with a null holder and null second reader, both `OpenClipboard` calls succeeded, the second close released the
hold, and the first close failed. A distinct message-only holder window refused both null and distinct-window
second opens with error 5 and retained its own successful close. The product's serial Io lane still closes each
scope before returning; the fixture's null opener allowed asynchronous prefill to release the intended hold.
The fixture now owns a message-only window, verifies the opener at entry and checks its release/close. This
models the documented [window-owned clipboard exclusion](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-openclipboard)
and preserves the original private-newer-copy expectation and all numeric goldens.

Additional actual host cases are being integrated before the next snapshot: device recovery while preparation
is running and while ready publication is held, all 10,000 legal metadata records, shutdown before/after
preparation, and real memory-admission wait/cancellation. Shutdown now clears retained color preview/histogram
tickets after the scheduler's join, which suppresses `Completed` callbacks. These changes remain unqualified
until fresh normal runtime, analysis and cross-builds pass. The original P1/G1 exit criteria remain open.

Before r18, primary integrated the current native baseline `024380a` into the managed branch (local merge
`6b5175e`, qualification pending). Both scanner proofs are retained: the candidate's guarded RSI table and the
native hoisted R15 remap. Native pointer controls and their project registrations are included. The saved
unqualified color work was reapplied without conflict; its source remains separate from the qualified prior
checkpoint. The merged menu guard retains the native dispatch-only timer scope and the candidate's brush
Escape handling. This is source integration, not a runtime qualification claim.

The cheaper agent drafted the two standalone admission cases. Primary moved private-member access into an
Application member, removed redundant termination checks, changed cancellation to the actual queued panel
Cancel receipt, added source/panel guards and retained shutdown-aware gate cleanup. Production admission uses
the existing optional `MemorySource` through the startup test probe; ordinary startup supplies the OS source.
The blocker owns a declared 64 MiB reservation, preventing both partial reservation and owning capture of the
preview. Resume requires the original ticket; cancellation requires no preview capture or worker entry.

Fresh full Debug r18 began at 2026-10-04T14:17:45.2178348Z with 752 frozen inputs, 17 targeted display host
controls and the unchanged 78-stage registry. Skills pass all 15 entries and formatting passes 634 files.
The build includes both scanner control families and the native pointer tests. Sources remain frozen through
actual child exit and independently verified archive completion. No pass is claimed before that boundary.

While r18 is frozen, primary found an additional flaw in the new fault tests: the callback captured a layer id
before `stalePreparedCase` rebuilt its fixture. The source-identity and sibling mutations could therefore miss
the intended record; the same-id replacement did not preserve the current fixture's target id. The recorded
r17 pass lines remain execution evidence, but they do not establish those intended mutations. After r18 is
archived, callbacks must resolve current fixture ids at the actual mutation boundary and assert the new raster
or metadata was installed. Add a separate held-source-change case requiring a fresh descriptor and complete
display with the current sibling fields, not merely eventual rejection of an old generation. No old golden or
P1 exit gate changes. Current host coverage remains open until the corrected controls are rerun.

Review also found that the waiting-cancel harness retained the preview instrumentation as the owner gate for
the new dismissal job. Dismissal has its own admitted capture and may legitimately set that gate's capture
flag. Before queueing Cancel, detach the owner test gate while the already submitted preview keeps its immutable
shared gate; then assert that preview never captured. This separates two actual jobs' observations without
changing either production cancellation path. The correction waits for the frozen r18 archive boundary.

Normal r18 exited 1 at the identified waiting-cancel instrumentation case, before the main Application registry.
Both new shutdown cases and admission resume executed successfully; the other shared-owner new cases did not
run in this attempt. All standalone suites and both scanner families passed. Every one of the 1,019 copies was
verified at 2026-10-04T14:28:57.2805887Z. Archive
`p1-color-host-combined-x64-Debug-r18-admission-instrumentation-failure`, completion SHA-256
`A896B530AA976FA9254C1DF724122EB6658B9950DDA1D4D1FBD536104868ADFC`.
The successful standalone DocumentTests registry remains exactly 6,392 preceding plus 99 publication controls.

After that boundary, the preview retained its submitted immutable gate while the owner detached it before
starting dismissal. Source-identity and sibling callbacks now resolve the current freshly built target and
prove the new raster/fields were installed. Same-id replacement preserves every document field and raster/mask
identity and checks a changed session serial. A new held source-change case requires one pending descriptor,
the old exact ticket until acknowledgement, two worker visits and a complete display with the new sibling
name/opacity and the original opening/settings. Fresh Debug r19 is running from a new frozen snapshot with
18 targeted color controls and 634 formatted files. No overall pass is claimed yet.

Normal r19 compiled/linked and passed the ISA controls, then stopped in the failure-report startup probe:
`DocumentTests.exe --failure-report-self-test` exited `-1073741502` (`0xC0000142`), before its expected CRT
report and before any application stage. All 1,019 source/evidence copies were verified at
2026-10-04T14:43:39.0032368Z. Archive
`p1-color-host-combined-x64-Debug-r19-process-initialization-failure`, completion SHA-256
`09552D86532E1A188753D65B78301021B8E487A20780937F00848AEDD579B1C4`.
The unchanged binary (SHA-256 `5095E0B8C6C2357FB65312A38B24A168C63477FD005C00C815CC48268A5FA60A`)
was launched separately at 14:44:42Z and produced its required CRT report and exit 3. This narrow retry
establishes no full-suite pass and does not explain the original initialization failure.

Normal r20's PowerShell child itself exited `0xC0000142` before producing any output or starting a build/test.
All 1,019 copies were verified at 2026-10-04T14:45:44.0594929Z; older output-directory products are retained
as evidence only, not counted as runtime from this attempt. Archive
`p1-color-host-combined-x64-Debug-r20-powershell-initialization-failure`, completion SHA-256
`94DE5AA79D8AFD7F740EB0EF06B747B986325648CE0D4E26B523AB3625B94343`.
No product source, expected exit, golden or timeout was changed. Fresh Debug r21 is running with that same
product snapshot. The process-start failures remain unexplained; qualification remains open.

Normal r21 passed compilation, linked-image/scanner controls, all failure-report probes and the Host,
Document and Composition suites, then exited 1 in IoTests before any application stage. The clipboard
transfer group reported 25 failed assertions, including NativeFailure/error 8 before clipboard mutation;
its overall suite result is one failed test group, not one failed assertion. Later availability/settings
controls ran and the fixture restored the user's clipboard. Every one of the 1,019 copies was verified at
2026-10-04T14:53:42.7922202Z. Archive
`p1-color-host-combined-x64-Debug-r21-io-failure`, completion SHA-256
`FCC661FD4AEE3DD7C126ED53C76F5AFF809D54DAF31E7E67525FBD5EA3897374`.
These failures are retained and do not qualify the application color route. Fresh native static analysis r22
is next; it does not run the Windows/clipboard fixtures. The runtime host also intermittently fails to start
PowerShell threads. The user has been asked to free unused application/build resources; no user process
has been terminated, and no environmental cause is claimed as proven.

The r22 PowerShell child exited `-532462766` (`0xE0434352`) in `LocalPipeline.InvokeThreadProc`, with
`Thread failed to start` and an unhandled `NullReferenceException`, before MSBuild or product runtime.
All 1,009 retained copies were verified at 2026-10-04T14:56:35.2340345Z. Archive
`p1-color-host-combined-x64-Debug-r22-powershell-failure`, completion SHA-256
`474D8D4EC79B87591D1ADF1545A7302E2419764EFD74BF451E4C4CBA49F911AB`.

After that boundary, primary reviewed the cheaper agent's four fixture-buffer fixes. Exact patterns,
`no_vector`, cancellation checkpoints, control names and assertions are unchanged; large fixture arrays
are heap-owned. Production image-import/project-open path buffers are bounded heap construction with
whole-operation OutOfMemory refusal, and classification reuses one constant-initialized 64 KiB buffer per
executing thread. It never pumps messages/reenters classification and acknowledges each read before reuse.
The existing 96 KiB classification scratch allowance is unchanged. The benchmark's temporary path buffer
is heap-owned within its existing recoverable setup boundary. The lookup failure result is populated in
its already admitted payload instead of creating a large stack temporary. Skills pass 15 entries and
formatting passes 634 files; these new changes await compiler/runtime qualification.

The next analysis attempt uses an ignored Python launcher with `CREATE_NO_WINDOW` to avoid allocating
another hidden console for the PowerShell child. It still calls the identical repository entrypoint with
forced Rebuild, freezes/copies all inputs, records the actual child PID/exit and performs no retry. This
launch change does not establish the cause of the resource failures or bypass any build/test gate.

Analysis r23 reached repository tool discovery, which reported no MSBuild, and exited 1 before compilation.
A separate unchanged `vswhere` invocation subsequently returned the installed Visual Studio 18.x instance
and exit 0. No installation, pin or toolchain setting was changed, and the failed discovery remains retained.
All 1,009 copied files were verified at 2026-10-04T15:06:36.7518248Z. Archive
`p1-color-host-combined-x64-Debug-r23-tool-discovery-failure`, completion SHA-256
`DDA5517B5C18360B00ECC6077140A520AC16B5234A7717BD9BE3FE5BC1533F21`.

Analysis r24 discovered the correct existing Visual Studio/toolset but stopped in dependency preflight:
vcpkg's `vswhere` child returned `0x80070008`, explicitly reporting insufficient memory resources.
The wrapper exited 1 before compilation. All 1,009 copies were verified at
2026-10-04T15:09:23.1365108Z. Archive
`p1-color-host-combined-x64-Debug-r24-dependency-launch-failure`, completion SHA-256
`E3229D6AEF420B6C905C7841C01C1998B1AFE24896FA7DFD3D35584DADDE6AF5`.
Further full qualification waits for recovery of the host's process/resource failures. The failed runs
are not treated as product passes, and no dependency/ISA/provenance gate is skipped.

Primary review replaced the intermediate thread-local classification scratch proposal: a large native TLS
buffer would add persistent storage to unrelated threads. The current candidate allocates one fixed 64 KiB
buffer only inside a nonempty, validated classification call, within the already admitted 96 KiB scratch
allowance. It reuses the buffer for all paths and releases it on the worker after every read is acknowledged.
Allocation refusal returns `OutOfMemory` with no partial output; the actual host job maps it to failed
`E_OUTOFMEMORY`. Empty, invalid and pre-cancelled batches allocate no scratch. A controlled fixed-buffer
factory supports independent refusal/cancellation/positive tests. This change and its new tests still require
fresh compiler/runtime qualification after the host recovers; no P1 exit or legacy golden is weakened.

The cheaper agent added six scratch-source scenarios with 12 new independent controls. Primary reviewed each
assertion and strengthened the successful case to a counted `.PRISM` subview whose backing string continues
with a non-comp suffix. It must terminate the view privately and leave that backing string unchanged.
The cases cover whole-batch refusal/default output, cancellation winning over refusal, empty/invalid/pre-
cancelled batches skipping allocation, and successful custom fixed-size storage. No existing test name,
numeric golden or runner order changes. Formatting still passes all 634 files and skills all 15 entries.
These controls have not run on the resource-failing host.

## Pushed checkpoint and resource boundary

The reviewed source is preserved and pushed as WIP commit `92bd2ae` on `codex/p1-opacity-host-proof`.
This commit includes the corrected 18-case host route, complete publication core, static-analysis repairs and
12 new scratch-allocation controls. It is an unqualified checkpoint, not native application activation or
P1 completion. The guide update is reviewed against actual progress and save-before-close callers; it does
not promise G1 timings or future RAW/SIMD behavior.

Analysis r25's actual PowerShell child (PID 64876) exited `-1073741502` (`0xC0000142`) before compilation,
with empty stdout/stderr. All 1,009 copies were independently verified at
2026-10-04T15:41:00.2914138Z. Archive
`p1-color-host-combined-x64-Debug-r25-powershell-initialization-failure`, completion SHA-256
`D6E68E11BD91A02A6F2560BE399242945A5DB12CCDBCE67DF6D5D930009D2B46`.

Analysis r26 used an ignored in-process wrapper to avoid starting a second PowerShell host. It invoked the
identical repository `build.ps1 -Rebuild -Analyze -MaxCpuCount 2` entrypoint, preserved all owned inputs and
still failed tool discovery before compilation. The tool's actual terminal exit 1 agrees with the wrapper
receipt; this terminal observation is retained separately. All 1,010 copied source/evidence files were verified
at 2026-10-04T15:43:01.8709340Z. Archive
`p1-color-host-combined-x64-Debug-r26-inprocess-tool-discovery-failure`, completion SHA-256
`A4721374A69881123A4CC555B9DA8AECD4C3733E716DE0399EB15AB7E014F207`.
This is not proof that Visual Studio is absent or that a particular resource pool caused the failure.
The installed prerelease Visual Studio 18.x was returned by a separate unchanged `vswhere -prerelease`
query. No dependency, ISA, provenance, expected exit, legacy golden or timeout gate was bypassed.

## Analysis r27 — private-desktop compiler diagnostics failure

The actual child was PID 49684, started `2026-10-04T17:15:45.534773Z` and completed
`2026-10-04T17:19:25.538478Z` with exit 1. It ran the unchanged repository entrypoint from the worktree:
`build.ps1 -Platform x64 -Configuration Debug -Rebuild -MaxCpuCount 2 -Analyze`. To contain compiler/build
processes, the launcher used a private temporary desktop `WinSta0\RedPrismQualification_53384_191545535792`;
the desktop handle was closed after the child exited. `MSBUILDDISABLENODEREUSE=1` was scoped to that process.
This launch reached compiler diagnostics, unlike r25's PowerShell initialization failure and r26's
pre-compilation tool-discovery failure. No thresholds or qualification gates were changed: production stack
diagnostics remain at 16 KiB and test diagnostics at 128 KiB.

The build did not compile successfully. Diagnostics include C6262 stack use in `ColorDisplayPublication.cpp`
and other production/test functions, C28182 null-dereference warnings from nothrow scratch allocations in
`PathClassificationWork.cpp` and related files, constant-comparison warnings, integer/HRESULT comparison
warnings, and test assertions that trigger analyzer findings. The build log ends with the actual MSBuild exit
1; no runtime stages ran. This is useful compiler evidence, not a runtime pass and not P1 completion.

The run receipt records 753 frozen source inputs. Archive completion records 980 independently verified
source/evidence copies at `2026-10-04T17:22:45.3114011Z`, completion SHA-256
`B4FB4DAA4999B29CE66BFBF8974538CE13452C021BABFCE896397834630B7588`. Its recorded archive root is
`C:\Users\eric\AppData\Local\RedPrism\QualificationArchives\p1-color-host-private-desktop-x64-Debug-r27-native-analysis-failure`;
the worktree's `.build\normal-clean-x64-analysis-Debug-r27` contains the run receipt and logs. The
verified frozen-input count is 753 and verified archive-copy count is 980.

Reviewed candidate repairs remain unqualified: in-place color results, heap path scratch, test splitting and
array bounds, and exact HRESULT comparisons. No r28 result is available in this record. Active-desktop G1 /
Release ETW qualification remains separate from the private-desktop compiler run.

## Analysis r28 — interrupted diagnostics only

The tool session ended before its recorder wrote the actual child exit. No recorder/compiler child remained
when checked; the exit is unavailable and is recorded as null. The frozen source remained unchanged. Compiler
diagnostics are retained as evidence for further stack and integer-comparison repairs, with no completed
analysis or runtime claim. The independently verified archive has 1,024 source/evidence copies, completion
SHA-256 `B992C30AB592C606D6C06FEEB5E6D05FD0008ED88F18EAC9EC7AFBAD27D6EE4D`, verified
`2026-10-04T17:47:40.7588302Z`, under
`C:\Users\eric\AppData\Local\RedPrism\QualificationArchives\p1-color-host-private-desktop-x64-Debug-r28-interrupted-analysis-diagnostics`.
A hidden recorder outside the chat tool's process job now owns r29; it retains the actual process handle,
waits without polling, closes its private desktop and checks all frozen inputs before writing its receipt.
No r29 outcome is asserted here. The exact existing pixel and state expectations remain required.

## Analysis r29 — production repairs compile; one test stack warning remains

The persistent recorder captured actual child PID 47768, start `2026-10-04T17:52:14.323547Z`, end
`2026-10-04T17:55:29.575902Z`, exit 1. The same forced Debug analysis reached compilation and linked the
application, IoTests and CompositionTests. DocumentTests failed C6262 in
`FailedPublicationFactoryHandlesLatestAndStaleResults` (159,912 bytes); the production diagnostics reported
by preceding attempts were absent. This remains an overall analysis failure with no runtime or ISA-gate
success claim. Frozen input count is 754, including the persistent recorder launcher.

The independently verified archive has 1,005 source/evidence copies, completion SHA-256
`DB902C97C33C5446F9086CD240381C017FE1F6D178D095B1B56FA36F993B8770`, verified
`2026-10-04T17:56:13.9209981Z`, under
`C:\Users\eric\AppData\Local\RedPrism\QualificationArchives\p1-color-host-private-desktop-x64-Debug-r29-one-test-stack-failure`.
The remaining test cases were split in their original order. Follow-up r30 runs on the inherited default
desktop; its actual desktop receipt takes precedence over the launcher's historical private-desktop label.
Tool discovery on that desktop now returns the installed MSBuild and actual vswhere exit 0. No cause for the
previous system resource failures is inferred from recovery. Full runtime and active-desktop G1/ETW remain open.

## Analysis r30 — recovered default desktop; further test stack repair

Actual child PID 14468 ran the unchanged forced Debug analysis on the inherited default desktop from
`2026-10-04T17:58:08.017991Z` to `2026-10-04T18:01:30.254470Z`, exit 1. Its recorded desktop is
`Inherited default desktop`; the launcher label still named the previous private-desktop workaround and has
since been corrected for future receipts. C6262 moved to `LookupAndMaskPublicationKeepTheirSourceSemantics`
(135,656 bytes). The application again compiled and linked without the preceding production diagnostics,
but DocumentTests and the overall analysis gate remain failed. No runtime stage ran.

All 754 frozen inputs remained unchanged. The independently verified archive contains 1,005 source/evidence
copies, completion SHA-256 `4D14192918F2405C26B8189FFB51E16EB8AC73F3C16C0AA4ACEA2E407E05E0C9`, verified
`2026-10-04T18:01:56.7464229Z`, under
`C:\Users\eric\AppData\Local\RedPrism\QualificationArchives\p1-color-host-default-desktop-x64-Debug-r30-test-stack-failure`.
A cheaper agent is repairing collocated large test outputs; primary review and fresh analysis precede the full
current runtime matrix. Existing numeric goldens, assertion names and runner order remain required.

## Analysis r31 — qualified compiler and ISA gate

Actual child PID 83176 ran the unchanged forced Debug analysis on the inherited default desktop from
`2026-10-04T18:09:56.732235Z` to `2026-10-04T18:17:30.319236Z`, exit 0. All five linked images, native
positive/negative concurrency SAL controls, linked-image ISA scans, matching PDB checks, scanner controls and
DxUi provenance passed. Production/test stack thresholds remain 16 KiB/128 KiB. Every existing publication
`Expect` expression, including numeric literals, was mechanically compared with the pre-repair source;
all 99 control expressions and the helper signature remain unchanged. No runtime qualification follows from
this analysis build.

All 754 owned inputs remained unchanged. The independently verified archive contains 1,038 source/evidence
copies, including the actual positive/negative SAL logs, completion SHA-256
`C6BE5C689E833FFCBB679415779C9909397C9F0D482A535B7FAF48F794DE4C4A`, verified
`2026-10-04T18:19:03.3384071Z`, under
`C:\Users\eric\AppData\Local\RedPrism\QualificationArchives\p1-color-host-default-desktop-x64-Debug-r31-analysis-qualified`.
The full Debug suite r32 and ARM64 Debug cross-build r33 began after archive verification; their frozen source
manifests match r31 exactly (`634E78D32E2D2175881274C52432FE708A68DE81763402D1BA6A8514B43C06F9`).
They remain pending; no application registry, ASan, ARM64 runtime or due G1 claim is made here. Skills passed
all 15 entries and formatting passed all 634 files. The plan-split verifier still preserves the original P1
exit, G1 table, operation scope and all 13 future roadmap sections.

## First complete current Debug runtime and ARM64 cross-builds

All four attempts use the exact r31 owned-source manifest
`634E78D32E2D2175881274C52432FE708A68DE81763402D1BA6A8514B43C06F9` (754 inputs).
The Debug suite ran on the inherited default desktop from `2026-10-04T18:19:21.313277Z` to
`2026-10-04T18:30:57.906753Z`, actual PID 54060, exit 0. Host/document/I/O/composition suites, command-line
and real main/worker crash probes, hidden WARP application, benchmark smoke and all harness screenshots passed.
Independent archived-log verification retains exactly 6,392 preceding document controls plus 99 publication
controls (6,491), and 8,962 preceding application controls plus 18 color host controls (8,980), with the unchanged
78 ordered stages. All 12 new `pathwork.scratch.*` controls and the existing scratch-creation control passed.

| Attempt | Actual result | Verified archive copies | Completion SHA-256 |
| --- | --- | --- | --- |
| x64 Debug r32 | Full `test.ps1 -Rebuild`, exit 0; exact registries above | 1,022 | `1ADE2D720FC6F62C986C7B751B60A00A1AE2A47E29C6413D6ED26ADDB026E8AE` |
| ARM64 Debug r33 | Cross-build, exit 0; no native runtime claim | 956 | `ABFB2F524D53EEBDA6167F1BDA40972EC70BE4AC42E123CD488FF32DCE36F94B` |
| ARM64 Release r34 | Cross-build, exit 0; no native runtime claim | 956 | `296806D4BEED7924BB8CA04F3CCDFAF333B53881BA05F178D86404340A841EAB` |
| ARM64 ASan Debug r35 | Cross-build, exit 0; no native sanitizer runtime claim | 960 | `91BF900110D72E47D5D0E9DADFE252805ECFEA172BA970F6DB6ED34FD9A96DA8` |

Every copied source/product was independently read back and hashed before overwrite. Their archive roots under
`%LocalAppData%\RedPrism\QualificationArchives` end respectively in
`p1-color-host-default-desktop-x64-Debug-r32-full-suite-qualified`,
`p1-color-host-default-desktop-ARM64-Debug-r33-cross-build-qualified`,
`p1-color-host-default-desktop-ARM64-Release-r34-cross-build-qualified`, and
`p1-color-host-default-desktop-ARM64-ASanDebug-r35-cross-build-qualified`.
Release and actual x64 ASan runtime still require their fresh full suites before closing the combined source
qualification. Native integration, original due G1 timing and the remaining P1 caller/resource routes stay open.

## Current continuation boundary

P1 remains WIP. r27/r29/r30 have actual failed terminal receipts; r28 retains interrupted diagnostics
with an unavailable exit. r31 passes analysis. Every archive is independently verified; the current full
runtime matrix has its first Debug pass and all three ARM64 cross-builds; Release and x64 ASan remain open. The last overall successful matrix predates the current color route. r21's exact
6,491-control document success is scoped to its earlier frozen source; it cannot qualify the subsequently
reviewed stack/scratch changes or the corrected host cases.

Complete fresh full x64 Debug/Release/ASan and all ARM64 cross-builds from the identical owned source.
Preserve and independently verify each attempt before overwriting its products or repairing any failure.
Application qualification requires the unchanged 78 ordered stages / 8,962 preceding literal controls plus
the exact 18 new color controls, and DocumentTests requires its unchanged 6,392 preceding plus 99 publication
controls. IoTests must also execute the 12 new scratch controls alongside every previous control. Real ASan
instrumentation/detection remains required; ARM64 cross-builds do not establish native runtime.

Native activation and the remaining sampler/stroke/selection/file/history/upload/chrome routes and original
G1 measurements remain P1 requirements. Do not move P1 to Done after only this color-display qualification.

## Managed Release r36 — ISA scan failure

The fresh normal-clean x64 Release test attempt ran on the inherited default desktop from
`2026-10-04T18:39:39.697607Z` to `2026-10-04T18:42:38.201517Z`, actual child PID 58180, and exited 1
without retry. The Release images compiled and linked. The ISA scan passed for `RedPrism.exe` (459 decoded
VEX/EVEX instructions, 0 tier and 459 runtime; 6 undecoded data prefixes and 5 verified table-data regions)
and `HostTests.exe` (260 decoded, 0 tier and 260 runtime; 1 undecoded data prefix and 0 verified table-data
regions), then rejected `DocumentTests.exe` at `0x140016024` and `0x14001602c` in `DocumentTests.obj`,
reporting `vpshufb xmm8, xmm15, xmm12` and `vpshufb xmm11, xmm15, xmmword ptr [r14]`. The scan stopped
at DocumentTests; no Release test runtime started. Disassembly review shows candidate inline-table bytes, but
the cause and proof are unresolved. This does not establish actual executable AVX instructions or a scanner
false positive.

The exact recorder receipt is `.build/normal-clean-x64-test-Release-r36/result.json`: exit 1, 754 inputs,
freeze hash `634E78D32E2D2175881274C52432FE708A68DE81763402D1BA6A8514B43C06F9`, stdout hash
`27099E812BFD33E7DAE62500E965DDB8A867F37B6118A95461EC8CA4E82640E8`, stderr hash
`0DAF5BDC8AAD2C43F58ED7DEF79E2E3E0739501FB97B8A407250A8022AB2E4FC`; mode `test`, platform x64,
configuration Release, launcher `Hidden persistent recorder; inherited default desktop; actual child exit, no retry`.
The independent receipt `.build/normal-clean-x64-test-Release-r36/independently-verified-archive.json`
verifies 1,021 copies in
`C:\Users\eric\AppData\Local\RedPrism\QualificationArchives\p1-color-host-default-desktop-x64-Release-r36-isa-failure`;
source-manifest hash `6DD90EF4E9840BA32F6B034631350BC666CA589AA5FE7EE5B08A72E5A9AF0BEA`, evidence-manifest
hash `50DD4800067387276CB5A4214F0C3C30AD7E62002F2E87FD6CF2A8A65F687FD7`, completion hash
`47C0CAF71D89F7A9106748358050BB639F3D7ADB4A3814014926935DA27909D7`, actual exit 1, zero stages,
verified `2026-10-04T18:51:51.4334045Z`.

This is a preserved qualification failure, not a Release runtime pass. Release and ASan runtime remain
unqualified, and this evidence does not complete the deferred-display child or P1.

## Managed Release r37 — full suite qualified

The r36 disassembly places the two reported vector matches inside adjacent DWORD switch tables owned by
`TestAdjustmentLayers`, reached through its RSI-based enum dispatch. The conservative scanner did not prove that
layout. Instead of broadening its instruction/data exemptions, the six legacy shared-editor cases now instantiate
one fixture per explicit kind in the original order. A balanced-call comparison preserves all 1,074 `Expect`
expressions in `DocumentTests.cpp`; every control and pixel expectation remains unchanged. No production source,
ISA floor, scanner, runtime allowlist or golden changes in this repair.

The fresh repository `test.ps1 -Platform x64 -Configuration Release -Rebuild` ran on the inherited default desktop
from 2026-10-04T18:54:40.394917+00:00 to 2026-10-04T19:02:56.196051+00:00, actual child PID 3116, exit 0, without retry.
All five image scans and adversarial scanner controls pass. Host/Document/I/O/Composition, isolated crash probes,
hidden WARP application, command line, benchmark smoke, corpus controls and all harness screenshots pass.
Independent registry comparison verifies 6,491 document controls (6,392 preceding + 99 publication), 8,980
application controls (8,962 preceding + 18 color host) and the exact 78-stage application sequence.

The source freeze covers 754 owned inputs with hash `2B31D9B6E599A373D4CED54B772C12F7377947CE6B3DDE50A6AC0061FBDC8556`. stdout hash `45EFE43E171EF4DA1BB36BA1EBC178F9D21C9C63F2F8E79ED5F19B449E65F859`;
stderr hash `F7F4639A7C712E422AFB52D28B9BC9190A86CBE0864B5540373C8263601A0782`. Archive `C:\Users\eric\AppData\Local\RedPrism\QualificationArchives\p1-color-host-default-desktop-x64-Release-r37-full-suite-qualified` independently verifies 1021 copies;
source-manifest hash `182CA95A4165034BA10DD13824F72B09E40D79720C0BF1A5AD7F95E7C26B38CD`, evidence-manifest hash `BC9AE60A8C14E07D9908BEE88C9E3C86C732AB32B9FC8A7E93070CC513C92954`, completion hash `43059FD4BF908E7E1AAEECD9BD63178E524A7711102BD6170DC04565EF99AD27`;
verified `2026-10-04T19:05:11.5687828+00:00`. The archive preserves the exact failed r36 attempt separately.

This qualifies current managed Release functional behavior. The earlier Debug/analysis/ARM64 records precede the
fixture isolation and are retained with that limitation. Fresh x64 ASan, current remaining-profile validation,
native integration, remaining P1 callers and original G1 measurements still precede milestone completion.

## ASan r38 failure and ARM64 cross-builds r39–r41

The fresh normal-clean x64 ASan Debug test r38 used the same 754-input freeze as r37
(`2B31D9B6E599A373D4CED54B772C12F7377947CE6B3DDE50A6AC0061FBDC8556`). The hidden WARP application failed at
the `clipboard-delayed` self-test stage: 45 controls ran and three failed, the application exited 5, and the
overall test exited 1. The recorder ran from `2026-10-04T19:05:18.687776Z` to `2026-10-04T19:20:52.814264Z`
(PID 59004). This establishes a functional test failure; no AddressSanitizer violation is established by the
captured result. The original failure archive is preserved at
`C:\Users\eric\AppData\Local\RedPrism\QualificationArchives\p1-color-host-explicit-fixtures-x64-ASanDebug-r38-clipboard-failure`.
Its independently verified receipt records 1,028 copies, actual exit 1, 74 stages, and verification at
`2026-10-04T19:21:33.0079055Z` (completion SHA-256
`90F7780BDD97B33870A73B4D03B4BB88B3E1EBC63C284A79FB9EF8FDC7FF6389`).

| Attempt | Actual result | Inputs | Independently verified archive copies | Completion SHA-256 |
| --- | --- | ---: | ---: | --- |
| x64 ASan Debug r38 | Full test exit 1; `clipboard-delayed` had 3 failures out of 45; no sanitizer violation established | 754 | 1,028 | `90F7780BDD97B33870A73B4D03B4BB88B3E1EBC63C284A79FB9EF8FDC7FF6389` |
| ARM64 Debug r39 | Cross-build exit 0; no runtime claim | 754 | 956 | `CC9E5A9E4EBBFA58E8251B57C67958F2900E290D0AB88CEC95B43FE68EF05CFF` |
| ARM64 Release r40 | Cross-build exit 0; no runtime claim | 754 | 956 | `FA9F681678F898DE1357C19BECFD097D8A4C57D1E6B7525A6EB2452394AA6B3E` |
| ARM64 ASan Debug r41 | Cross-build exit 0; no native sanitizer runtime claim | 754 | 960 | `1843E80DFC775310EE7A16D3A64CE64E736D48EB7DC9EE58A9C83E0E6E55919E` |

The r39–r41 builds share the r38 freeze hash above. Their independently verified archives record actual exit 0
and zero runtime stages. Earlier Debug r32 and ARM64 r33–r35, as well as analysis r31, precede the fixture
isolation represented by Release r37; they do not qualify that repaired fixture source.

A fresh x64 ASan Debug run, r42, used 754 frozen inputs with freeze hash
`C884F7F4AE83CCCC46936FFC964ADCE6F5BBE90BAA7E33A70AFE068BF183AC00`. Child PID 94924 started at
`2026-10-04T19:29:32.979858Z`, ended at `2026-10-04T19:39:09.917947Z`, and exited 1. Build,
HostTests, DocumentTests and CompositionTests passed. `IoTests.exe` then exited 1: its clipboard-transfer group
ran 139 checks with 11 failures, and the suite stopped before the application phase (zero application stages).
The older application self-test output under `.build` belongs to r38 and is not r42 evidence. The overall r42
result is a failure, not a pass; the scalar native timestamp and `Wait`/`SetClipboardData` error diagnostics
establish neither a functional repair nor an environmental cause. Clipboard-holder records show later retries
that succeeded, which also does not identify the cause.

The existing archive `p1-color-host-native-diagnostics-x64-ASanDebug-r42-host-failure` is retained under
`%LocalAppData%\RedPrism\QualificationArchives\` with its original name. Independent verification completed
at `2026-10-04T19:41:11.8592012Z`: all 754 frozen inputs and 274 evidence files (1,028 copies total) were
verified. Source-manifest SHA-256 is
`FF984228E380E7A89CDA25BF38452C37772484CC2F11BE9A7E3F399B92250F74`, evidence-manifest SHA-256 is
`1F567121734BCD96FA90EEBB3DBD281E3DE15C9FCC8E75C70852DF8B3E3DC997`, and completion SHA-256 is
`935510E0F81C3A3B88CE35180971343F772E39F9B776B87151D2F5DF4FD0E8EB`.

Follow-up isolated-station attempts r43, r44 and r45 are diagnostics against the exact existing `IoTests.exe`,
not qualification runs. They failed before starting their child: `CreateWindowStationW` returned errors 5, 5
and 183 respectively. These outcomes do not establish an environmental cause for r42. Primary native-call
diagnostics continue in parallel; no functional repair or changed assertion/deadline is established. Native
activation, remaining P1 callers and original G1 measurements remain open.

## Clipboard owner-scope investigation and current candidate

The next incremental x64 ASan Debug build, r46, is build-only: `result.json` records `Mode: build` and exit 0.
It is not a static-analysis or runtime result. Focused runtime r47 then ran `IoTests.exe` and exited 1 with
three failed assertions in 139 clipboard-transfer checks. The failure was `CloseClipboard` error 1418. The
1,028-copy archive was independently verified; neither the actor holding the native clipboard scope nor an
environmental cause was established. Focused runtime r48 ran the actual application, reached 73 stages and
exited 5 after one native prefill-DIB failure among 61 checks; `EnumClipboardFormats` returned error 1418.
Its separately named 1,028-copy archive was also independently verified. These focused failures do not
qualify the full application or establish an external actor as their cause. Exact local records are linked:
[r46 build receipt](../../.build/incremental-x64-build-ASanDebug-r46/result.json), [r47 runtime receipt](../../.build/incremental-x64-runtime-ASanDebug-r47/result.json), [r47 independent archive receipt](../../.build/incremental-x64-runtime-ASanDebug-r47/independently-verified-archive.json), [r48 runtime receipt](../../.build/incremental-x64-runtime-ASanDebug-r48/result.json), and [r48 independent archive receipt](../../.build/incremental-x64-runtime-ASanDebug-r48/independently-verified-archive.json). The verified archive names are `p1-color-clipboard-native-calls-x64-ASanDebug-r47-io-close-failure` (completion SHA-256 `8EF66827E8DED2A25270CEA4EBF66F10250D4F00F552367ED71D3AB4B00C0CDC`) and `p1-color-clipboard-native-calls-x64-ASanDebug-r48-prefill-enumeration-failure` (completion SHA-256 `CF6C8FB6A89D06FC0553A7B6F9914FC428849258B519F0C80D49439D37E89654`).

Read-only native diagnostic r49 opened and closed the current Unicode clipboard successfully in all 100
attempts, with both null and distinct owner-window forms. This does not establish application qualification
or universal stability. See the [r49 local diagnostic receipt](../../.build/clipboard-native-scope-r49/result.json).
Read-only concurrent diagnostic r50 reproduced the null-owner scope hazard: with two
concurrent null-owner opens, both opens succeeded, the second close succeeded, and the first scope's next
enumeration and close returned 1418. When either scope used a distinct owner HWND, the second open failed
busy with error 5 and the first scope enumerated and closed successfully. These diagnostics identify the
reproducible native scope behavior; they do not identify the actor in r47 or r48. See the [r50 local diagnostic receipt](../../.build/clipboard-native-scope-r50/result.json).

The current candidate now opens native clipboard scopes for Inspect and Copy with a fresh worker-owned
`STATIC` message HWND and RAII scope; it does not open with a null owner. Window creation faults are reported
as local worker-only native failures, with no automatic retry; busy keeps the existing bounded retry budget,
and failed results remain empty. The actual two-MTA-worker regression holds the first owner scope open while the second worker
attempts Inspect and Copy, expects one busy attempt for each, validates the owner thread and HWND, then
releases the first scope and checks unchanged 2x1 clipboard metadata. Invalid worker windows are refused
before `OpenClipboard`; existing clipboard goldens remain unchanged.

The fresh full x64 ASan Debug `test.ps1` run r51 exited 0 on its 754-input freeze. Clipboard transfer passed
all 146 checks (the prior 139 plus seven new checks). The hidden WARP application registry records the
unchanged 8,980 controls and all 78 ordered stages; the document registry records 6,491 controls. The full
test also passed its crash probes, ASan detection probe, ISA scan and other gates. The independent archive
receipt verifies 1,028 copies and 78 stages with completion SHA-256
`AC69CB61894D66FC58A4ED78AA3856CE6D48110270FFB7EDF435FC55A9BDA2CE`. See the [r51 result](../../.build/incremental-x64-test-ASanDebug-r51/result.json),
[independent archive receipt](../../.build/incremental-x64-test-ASanDebug-r51/independently-verified-archive.json),
[application registry](../../.build/incremental-x64-test-ASanDebug-r51/verified-color-host-application-registry.json),
and [document registry](../../.build/incremental-x64-test-ASanDebug-r51/verified-color-document-registry.json).
Prior failed and diagnostic receipts remain preserved above. r51 qualifies this x64 ASan suite and its
recorded registries, not native activation, the remaining configuration matrix or G1.

## Current Debug analysis r52

The sampler's r7 analysis exposed C6326 on constant template switches in the shared-editor fixture. The
same test-only repair is now applied here: explicit `if constexpr` branches, with no original assertion,
production path, scanner rule, threshold or suppression change. The independent lexical proof preserves
all 17 fixture `Expect` expressions and all 983 whole-file expressions as identical multisets; these are
source-expression counts, not runtime assertion counts.

Current `build.ps1 -Platform x64 -Configuration Debug -Analyze -MaxCpuCount 2` completed successfully from
`2026-10-04T20:53:24.502436Z` to `2026-10-04T21:00:58.949256Z`, actual child PID 31352, exit 0.
The analysis entrypoint rebuilt all five images and passed the SAL controls, matching-PDB ISA/scanner gates
and pinned DxUi provenance. Its 754 inputs remained unchanged; freeze SHA-256
`D5572E2B598E83AF61B57CEE66DA3DDF1198F8405546158626247202BC71D1F3`; stdout
`64A66718151ECBC6261C25C8B99CF477B621E3D131ECD59B60F947D2AE3559D9`; stderr is empty.
The [actual receipt](../../.build/incremental-x64-analysis-Debug-r52/result.json) is analysis-only; r51's full
ASan runtime precedes this test-dispatch repair. Current full Debug/Release and ARM64 builds, native activation,
remaining actual callers and G1 stay separate requirements.

Independent archive `p1-color-clipboard-read-window-x64-Debug-r52-analysis-qualified` verifies 1,037 copies,
zero runtime stages and completion SHA-256 `7E6A2B72555C9B402C0534C810B53014C3A5D0556ED2DA5B86B69F7F24CF6D98`.
Source-manifest SHA-256 is `EC6BD145C9DCBAC159A9F1507B120671B176A85BAEDF9F38E2732F329F0DF0A5`; evidence
manifest is `37E5EDEE9B73B83513E515ED86E71397E3F58B135989F84B09F2EC932FF7759D`.
