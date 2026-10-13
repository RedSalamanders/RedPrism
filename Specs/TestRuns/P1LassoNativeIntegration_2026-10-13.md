# P1 lasso host integration — native qualification in progress

## Checklist

- [x] Preserve r1012's two fixture diagnostics and repair storage/exact HRESULT type only.
- [x] Independently prove all 80 input-adapter predicates and the exact error equality unchanged.
- [x] Integrate the qualified private 160-fact host ordering/kind-toggle/wakeup repair and CPU/WARP fixtures.
- [x] Qualify the freshly rebuilt native Release DocumentTests: all 21,720 records retain the 21,673 predecessors.
- [x] Preserve failed whole analysis r1019 and the reader's ordering/boundary refusals.
- [x] Qualify latest-source canonical Release analysis and independently verify its complete archive and scans.
- [x] Qualify complete latest-source full Release, including all 160 host/29 display facts and unchanged predecessors.
- [x] Qualify complete latest-source full Debug with exact rosters, source and artifact/profile identity.
- [x] Qualify complete latest-source real ASan with real detection, all instrumented commands and exact rosters.
- [ ] Qualify complete latest-source ARM64 cross-builds; native ARM64 runtime remains unavailable.
- [ ] Qualify actual native host/display, complete admission/retirement, ordinary activation and measured S5/G1.

## Native source and fixture repair

The 1,356-input source freeze is `BFA03023B57A19AD6BF4082FA54ABBD1A71DA32C462794A657615AD6CFA56C76`.
The preceding r1012 analysis reports C6262 (147,388-byte test frame) and C6221 (different integer types in
an exact HRESULT equality). Its complete archive receipt is `92316A1FD1ECAA03660E182D718297F140D5585B60D18D05117C8B62EA1FB672`.
Twenty fixture-owned HostLassoInput instances move to the heap; only the expected HRESULT is cast to int32.
Whole-file normalization independently proves unchanged predicates and lifetimes, including all 80 adapter facts.
The private current-only host source now includes 160 facts; its separate private receipt remains unchanged.

## Actual focused native Release result

The canonical r1019 rebuild successfully links DocumentTests at 19:04:40 UTC before other projects fail.
The focused r1027 child is PID60224, exit0, 19:20:13.258871–19:20:25.824650 UTC. Its executable hash is
`B018467885510EBF092FD9EB958947F5D0139B71B8438EBC9628797F5234322B`; it remains unchanged across runtime.
Stdout is `F96725F95332327BF77553BA3E7982CCBA5C4537E7352033A32A99368AD87426`; stderr is empty.
Exact ordered readback preserves all 21,673 native predecessors and adds 23 conditional facts, their one PASS
summary, and 23 draft-raster facts: 21,720 records total. The separate raster summary is also required once.

The archive is `Z:\CodexArtifacts\RedPrism-P1\2026-10-12\P1-LassoHost-x64-Release-Document-focused-r1027-passed-20261013`, independently verified completion
`6E40A7D7F2451491B26CE3790F89D0457A9F42156F9C20DE0A9120BE815D528E`. Source, output/PDB and build tracking are preserved.
Ancillary captured application/composition products do not qualify those scopes; this is DocumentTests only.
The runner's generic scope text says rebuilt; its explicit Mode=runtime and Suite=DocumentTests define this
focused run, whose compile provenance is the successful document target inside failed r1019.

## Whole analysis and reader refusals

Whole Release analysis r1019 is PID78912, exit1, 18:50:51–19:10:55 UTC. With project cap2 it reports C1060
compiler heap exhaustion, secondary C1001 internal compiler errors and D8040 child communication errors.
The complete failure archive receipt is `FF82046870684E1A2AD4FEA3397D0B7BB646DCF5D470C69E316D953BC6320EE3`.
No r1020–r1025 full runtime/cross-build follows it. The replacement uses project cap1. CL_MPCount alone is
not an asserted compiler /MP cap; no system pagefile or independently running application is changed.

The agent r5 reader initially requires a success footer from intentionally failed historical ASan witness
r971; Main refuses it. Corrected r6 retains the partial 60-row statistics scope and requires hash-verified
manifest copies; it never qualifies that failed suite. Main supplies exact archived copies and its controls pass.
Focused r1027 then exposes a private predecessor order mismatch at record20782, despite equal 21,720 Counters.
Main's r8 reader uses previously independently qualified native r1006 registration order and the same reference
facts; no actual assertion changes. It also preserves all 7,328 native composition predecessors: the legacy
readback success message precedes 815 newer controls, so complete composition extraction ends at the next I/O
harness suite. Native full success and exact order/multiplicity remain mandatory. All refused readers remain.

## Replacement canonical analysis

Release analysis r1026, PID100680, exits0 at 21:11:02.592594 UTC after starting 19:31:11.226217 UTC.
It uses project cap1 and exactly the r1019 compiled-source freeze above. Main independently hashes all
1,356 archived source files and 413 evidence files (1,769 total), checks every live source bookend,
and binds all five linked image/PDB pairs to their zero-violation ISA reports. The allowlist remains
`7870AF49195841A9B9B88DFD15378D550B127754683666C83AED83753FD80E1E`.
The canonical positive/negative concurrency SAL controls and eight linked rejection controls pass.

Archive: `Z:\CodexArtifacts\RedPrism-P1\2026-10-12\P1-LassoHost-x64-Release-analysis-r1026-passed-20261013`.
Completion SHA256: `64E00930EDC37ECADDE28A35CA9F9BF4C2E5AE9C549A8C8A4F6F17DAB55902A6`.
Stdout: `DDF564088893630780F298ACDF66152C3530D1CC277353220916D6B5A1104269`; stderr empty.
The first two independent reader attempts refuse an assumed UTF8 banner and a verbose English MSBuild
summary absent in the canonical minimal output. Both remain preserved. Byte-preserving log reading and
the actual canonical control lines resolve only those reader assumptions; no runtime or product predicate changes.

The full Release r1028 gate now runs. Debug/real ASan and all ARM64 cross-builds follow serially and are
not yet qualified by this record.

## Complete native Release

Full canonical Release r1028, PID103284, exits0 at 21:50:13.360659 UTC after starting 21:11:17.985463 UTC.
The 1,356 compiled-source freeze is unchanged. Independent Main readback requires all 138 Application
stages, 160 lasso host facts, 29 draft-display facts, 21,720 document records and 7,366 composition records.
All existing independent numeric predicates and record multiplicities remain required.
A separate Main artifact reader verifies all 1,757 archived files, 1,352 identical compiled/project/toolchain
inputs, five x64 image/full-PDB pairs with matching zero-violation ISA reports, and 930 actual native
compiler command records with Release instrumentation disabled. The four runner/helper inputs remain separately
pinned in the full source freeze. Skills validate all 15 entries; formatting checks all 1,204 files.

Archive: `Z:\CodexArtifacts\RedPrism-P1\2026-10-12\P1-LassoHost-x64-Release-r1028-passed-20261013`.
Completion SHA256: `66F8B8527820DB5B35CFFCFF5F644028A73F1DDB04A4E45238DC7678D39AF7B9`.
Parent stdout: `274A56E753FD9122C224AE5905635107123C08E5728944031F3F11715B5007D4`.
Application stdout: `D51D0DE97ACFB32CD05D7592FCC24D62DB236FF03BAE3E9EF1F9D7828B587BAD`.
Application stderr: `18446AEAA47082BC5EDF89700F4F38CCD7512D93E8BF82868BE54E04978485A4`.

Two strict reader refusals remain. The unchanged GPU recording fixture allows its original acknowledgement
callback between the failure notice and the later all-uses acknowledgement; a historical complete output
order was too strict. Main proves the rule from the same current/frozen/r1006 fixture source. The replacement
requires eight exact original cases, their literal 3/1/0/0/1/1/0/0 acknowledgement counts, atomic callback fact
order and source-defined boundaries; every other record stays fully ordered. It accepts four legal callback
positions and rejects eight mutations (missing/duplicate/reversed/early/late callbacks, reordered owner facts,
reordered new pixel facts and changed predicates). A second refusal identifies two new pixel labels whose
private expected order differs from the native fixture's call order: the SRV-release assertion at source288
precedes the overlay-order assertion at298. All thirteen new pixel expectations are now derived from the
frozen source call order; values/counts do not change. The original expected files remain preserved.

The final reader SHA256 is `D74FFA4F0BA7F8AE794953E65E3D94309F9F81D237B09D5BE2BEB72E26131719`;
its policy is `09C09EE5FAB1456083BC090C9FFE968344782504F38531A0235EDC9DF71DA560`.
Debug r1029 is running; real ASan and ARM64 cross-builds are not yet qualified by this record.

## Complete native Debug

Full canonical Debug r1029, PID80104, exits0 at 22:31:12.008417 UTC after starting 21:50:26.627150 UTC.
The same 1,356-input source passes the exact 138-stage/160-host/29-draft-display/21,720-document/
7,366-composition roster, preserving the source-defined GPU callback rule and all independent values.
Main verifies all 1,777 archived files, the same 1,352 compiled/project/toolchain inputs, 930 native compile
commands with sanitizer instrumentation disabled, and five x64 image/full-PDB scan bindings.

Archive: `Z:\CodexArtifacts\RedPrism-P1\2026-10-12\P1-LassoHost-x64-Debug-r1029-passed-20261013`.
Completion SHA256: `474299B03E65B931475766C362ED2EE57278E7973F32ABF233667F19346B07BB`.
Parent stdout: `D8D62CDC5D406BDD056BF42BFFA94037B4181A10353AE8740908669DC7E1A260`.
Application stdout: `990A1B195BA341BDC7A6FEDA185629568E28B9D80577D406674EC828D25ADE89`.
Application stderr: `B5B4C96BB599AC41F8B6D414462D9A3BE02374238414F1C06E3E0D4AD19B5508`.
Real ASan r1030 now runs; ARM64 cross-builds follow serially. Neither is qualified here yet.

## Complete real ASan

Full canonical ASan Debug r1030, PID92808, exits0 at 23:17:02.224692 UTC after starting 22:31:27.079998 UTC.
Its source remains the same 1,356-input freeze. Main independently requires the complete 138-stage/
160-host/29-draft-display/21,720-document/7,366-composition roster, actual heap-use-after-free detection,
all 1,741 archived files, 1,352 unchanged compiled/project/toolchain inputs, 930 actual native compiler
records with `/fsanitize=address`, and five exact x64 PE/CodeView/full-PDB GUID/age pairs.

Archive: `Z:\CodexArtifacts\RedPrism-P1\2026-10-12\P1-LassoHost-x64-ASanDebug-r1030-passed-20261013`.
Completion SHA256: `1970B9F3ADD64A8D6B212F73E37C188C4E2DC9D27763D33FBDDCD727CEA07121`.
Parent stdout: `C3A25D35D4F72DD86A88A1E5FCBFBDB2084712EE29459519A70EAD316F56AAAD`.
Application stdout: `D89AE2C256D4CC7DF76B6D4BB1633C262B38FB69A19ACFE0C0A55BB5DF6B69E0`.
Application stderr: `B5B4C96BB599AC41F8B6D414462D9A3BE02374238414F1C06E3E0D4AD19B5508`.
Actual UAF probe: `9033F83A1475378DCEBA49819A03FB865221EE32A2092647223F07D759EB887A`.
The exact runtime allowlist remains unchanged. Actual runtime passes the same predicates as Release/Debug.

Before queued ARM64 launches, Main finds its separate helper still defaults to project cap2. The helper is
absent from every active x64 source-freeze input and no ARM job has launched; Main preserves its old bytes
and changes only that project cap to1. This matches the successful r1026 retry and changes no compiler ISA,
sanitizer or product requirement. Current ARM64 r1031 runs; r1032/r1033 follow serially. Their own freezes
must retain the same 1,352 compiled/project/toolchain inputs while pinning the changed runner separately.

## Limits

Complete native Release/Debug/real ASan host/display correctness and analysis pass. ARM64 cross-builds, ordinary activation and performance qualification remain due. The scanner/core
checkpoint remains qualified by its own earlier record. P1 remains WIP, ordinary activation stays cold, and
no broad delivery checkbox or Done/merge state changes on this focused result.
