# P1 stroke resource prerequisites and analysis correction

## Checklist

- [x] Preserve the failed Release analysis r292 with its actual exit, source, responses and diagnostics.
- [x] Move 19 large healing fixture arrays to heap owners; preserve every kernel and expected byte.
- [x] Review and integrate producer-cached tile ownership and sparse replacement-grid workspace.
- [x] Review and integrate GPU density clear and fixed-slot readiness before final density dispatch.
- [x] Review and integrate the private, cancellable worker mask sample with independent literal controls.
- [x] Register all ten added test files; format/check 771 C++ files and validate 15 skills.
- [ ] Require exact expanded controls and every predecessor in fresh canonical x64 Debug/Release/real ASan.
- [ ] Complete Release analysis and all three ARM64 cross-builds on the same source; archive exact products.
- [ ] Update owning contracts, commit and push the qualified source.
- [ ] Connect the original final stroke ticket to the actual application; finish the remaining P1/G1 gates.

Status: **Original 898-input Debug r294, Release r295, Release analysis r297 and ARM64 r42 builds independently verified and archived. ASan r296 failed a sampler terminal check. Diagnostic-only ASan r298 passes independent verification and archive readback. The intermittent failure has no established cause. No complete identical-source current matrix, asynchronous host activation or G1 claim**.
Parent: [P1](../Plans/WIP/UiNeverBlocks_2026-09-22.md).
Foundation: [earlier qualification and scope](P1StrokeFinishFoundation_2026-10-07.md).
Actual application closure: [opening, finalization and lifetime work](P1StrokeFinishHostWork_2026-10-07.md).

## Preserved analysis failure

Release analysis r292 runs at `2026-10-07T08:09:03.288875Z`â€“`08:29:13.893959Z`, actual child PID `68316`,
exit **1**, 888 inputs, original freeze
`38C098CB8975D6EAB9FF7120007FA9FF661DB2B9F7F66331E40030ADBB8262CC`.
`Document.Tests.HealWork.cpp:135` has C6262: `RunHealWorkChecks` uses 222,088 bytes of stack, followed by
C2220 because warnings are errors. No kernel diagnostic or changed expectation is implicated.
Two copied actual compiler responses prove `/MP2` and `/analyze`. This attempt is not an analysis pass.

Its independently verified archive is
`D:\RedPrismQualificationArchives\P1-stroke-foundation-888-Analysis292-stack-warning-2026-10-07`:
1,185 copies, source manifest `74029DC1995F6C12D99E2C390FECD9CBE17D6FE02068293F0C33B8D5A60E9FBA`,
evidence manifest `2569ACFFBA02F0F00D8ECC768710A9B21850EECE23A76E1F0EE82C851D27F2A8`,
completion `C55CC9597BC87DBC3CB8B46D8C5E6A01E15BBF7BA848C9F0608A9363B9B781C0`,
verified `2026-10-07T08:30:48.0744114Z`.

The primary replaces stack fixture declarations with `make_unique<array>` and local references. Array types,
initialization, spans, analytic constants and all 33 controls stay unchanged. The executed repair helper r1
expects 18 declarations and refuses **before any write**; r2 checks the actual 19 declarations and applies them.
Pre-format source hash becomes `9563D04BCA8598D71A13A464F91E98DDEDD4A789D40FCAEBA7EF78723A5BCE23`.
Fresh attempts freeze the formatted bytes; old functional results remain evidence for their original source.

## Resource implementation

Each brush tile producer updates its complete buffer/raster ownership after successful allocation or replacement.
The owner retirement query reads this aggregate and fixed metadata capacities, avoiding a tile traversal.
Sharing remains conservatively counted; unknown children or arithmetic overflow return unavailable. This cache
does not itself implement whole-draft destruction or GPU retirement; those remain application closure work.

Raster construction caches the previous `AddSparseWindows` sum for one uniform 256-pixel replacement grid:
`(ceil(width/256)+2)*(ceil(height/256)+2)*8*sizeof(RasterTile)` for each clipped source window. Empty/contiguous
rasters report zero workspace. Sparse metadata is immutable; overflow/invalid windows report unavailable.
The raster-edit admission query reads this cache. It is not an arbitrary-rectangle splitting declaration.

GPU density initialization records `ClearUnorderedAccessViewUint` with four zeros after creating the structured
buffer's UAV. It removes a tile-sized owner float vector and initial CPU upload. Existing density stays intact;
a new stroke clears a reused key. The shader and coverage values are unchanged. The dedicated WARP tests request
the D3D11 debug layer, report whether it is available, and inspect warnings/errors when it is active.

`HasReadbackSlot` examines only eight owner metadata slots and refuses stopped/unbound queues. It is readiness,
not memory or GPU reservation. The application must check it before settling density and use event notifications
when no slot is available; this helper alone establishes no terminal caller integration.

The worker mask sampler validates format, dimensions, 100M output pixels and cancellation, reads sparse sources
privately, preserves mask edge background and exact nearest/bilinear taps, and collapses High to Smooth.
Invalid placement yields background only after a successful source read. The shared sparse cache stays absent.

## Reviewed tests and current attempt

Expected additional controls: **110 document** (48 retirement, 44 sparse workspace, 18 mask samples),
**71 composition** with the debug layer active (40 density, 31 slot controls), or 69 if the debug layer is unavailable.
The delegated slot receipt lists 30 distinct names but `setup.bind-warp-device` executes twice; the expanded
normal path therefore has 31 records. Fresh verification must include that duplicate, not deduplicate evidence.
The primary caught and corrected its missing setup failure assertions, early receiver destruction and waiting
after failed admission. Every ordinary return now closes queue borrowers before receiver destruction.

Retirement tests preserve the actual contiguous working-tile representation, separately test sparse snapshots,
and cover compatibility healing and joined-worker MoveLifted. Mask tests use literal bilinear expectations,
including byte 72 at the fractional center versus nearest byte 0. No golden is rebaselined.

The primary integration helper stages all 24 mutations before writing, checks original raw source hashes,
registers every actual sidecar function, and adds ten files. Final memory planning and actual application opening/
finish drafts remain ignored and unactivated. Debug r293 starts the canonical `test.ps1` at
`2026-10-07T08:34:35.067646Z`, freezing 898 inputs. Compilation/runtime, source/product identity, analysis,
cross-builds and complete P1 owner deadlines remain due.

Debug r293 compiles the corrected heap fixture and production sources, but fails C4244 in the density test:
`std::fill` receives an int sentinel `0xA5` for byte iterators. Actual PID `78252`,
`2026-10-07T08:34:35.067646Z`â€“`08:43:27.108003Z`, exit 1, freeze
`CE4B0FFDB217E56E232B8E5B6A2A939B833F1B6C2F64D6D4D892E80F11727B88`.
No suite runs. The primary makes the sentinel's `uint8_t` type explicit; the sentinel, assertions and production
shader/clear stay unchanged. The 1,185-copy failed archive
`P1-stroke-resources-898-Debug293-narrowing-warning-2026-10-07` independently verifies at
`2026-10-07T08:45:43.6816974Z`: source `E41EC68C2BB541136A2B1DD328943DAF775632A7FCEE8738495FCC35C3CFAED6`,
evidence `F6205DBED459A6343F8DA0F02B449D33AF90C99E2A2F5BCA48BA364672764CA5`,
completion `F2A0BE2872FA22977A98E99A94620FB73FC2D54F7FA46F5532F5ECDDBE0B5DD3`.
Fresh Debug r294 executes the canonical complete entrypoint on the corrected source and exits 0.

## Debug r294 qualification

Actual child PID `78420`, `2026-10-07T08:46:20.708102Z`â€“`08:57:16.178686Z`, exit 0, 898 inputs,
freeze `6358FB57C725E348C079A1C5027FFC3B5D33842C860E1E2CEB9C137D4C7ACCA7`.
Independent verification requires every previous control plus the exact ordered 181 additions:
Host 7,675, Document 7,702, Composition 1,833, I/O 1,736; application 10,430 named controls and 99 stages.
The D3D11 debug layer is active. All five fresh linked images/PDBs and the 119 ISA scanner controls verify.

The primary reviews the delegated static inventory and catches a duplicated printed prefix in its absent-debug
branch. The same source helper prints one prefix in both branches; corrected inventory r2 preserves all active
records and removes only that erroneous duplication. Its SHA-256 is
`EA4974E940EB542C4F4626D2C42A7C33F7D3BCF06AB11D4481010F94D2C1B62F`.
Full verifier r1 is `02EA7563C5DBCABAC9AC23B7F0252F272CF05BE7BA9541C4EF5B57970C76EA2C`.

Archive `D:\RedPrismQualificationArchives\P1-stroke-resources-898-Debug294-qualified-2026-10-07`
independently verifies 1,188 copies at `2026-10-07T09:05:15.0149954Z`:
source manifest `E93E5201E5F35954859F1E201C482A0C4796EED23F42A23F50506D74EB9EC55B`,
evidence manifest `79EC97D20408BA0419FF88684CD7E9839DB92D29C8132BA85465B80978EFDEEE`,
completion `C47FAD849E9E0452CAFE0BCBD3D7755AB6B4FCC1AA540CCEC957A308F4B6369A`.
Release r295, Release analysis r297 and three ARM64 r42 builds qualify this original source below. The diagnostic-only ASan source is identified separately.

## Release r295 qualification

Actual child PID `69660`, `2026-10-07T08:59:55.913931Z`–`09:15:48.447140Z`, exit 0.
The same 898 inputs and freeze as Debug r294 are independently checked by resource verifier r1.
Every predecessor and all 181 ordered additions pass: Host 7,675, Document 7,702, Composition 1,833,
I/O 1,736; application 10,430 named controls and 99 stages. The D3D11 debug layer is active.
Five fresh images/PDBs, linked ISA reports and all 119 scanner controls verify.

Archive `D:\RedPrismQualificationArchives\P1-stroke-resources-898-Release295-qualified-2026-10-07`
verifies 1,186 copies at `2026-10-07T09:22:52.9693043Z`:
source `1F6FDCABE874879499D07126F02DF27DE5F15CE752C2C779227DE37130AD04A1`,
evidence `82424A831F8B1B2135D782A6346506CA6A2905555DF6A4A258168AE94676D4BE`,
completion `9B9ED832B10F989A1AB9177DDF548B825242A4266CFAB31823310E081D4BD51C`.
ASan, corrected Release analysis, ARM64 evidence and actual application closure remain separate gates.

## ARM64 r42 cross-build qualification

The unchanged 898-input freeze is checked before/after each actual child. All five fresh AA64 images/PDBs
verify in Debug, Release and ASan Debug. The ASan images contain sanitizer imports, and their fresh compiler logs
require `/fsanitize=address` plus the instrumentation guard. This is cross-compilation evidence, not native runtime.
Verifier SHA-256 `E392BE464C27EF77EF72269E6E569BD817AD08BFAAA9790BB72779D2513ED443`.

- **Debug**: actual PID `70424`, `2026-10-07T09:00:44.879612+00:00`–`2026-10-07T09:09:07.568064+00:00`, exit 0. Archive `D:\RedPrismQualificationArchives\P1-stroke-resources-898-ARM64-Debug42-qualified-2026-10-07`; completion `33FD51C083129E4F3C33BA680E405286ABFDB78E10BF176F09B28F51FCF959FD`.
- **Release**: actual PID `83552`, `2026-10-07T09:09:13.356428+00:00`–`2026-10-07T09:16:54.781982+00:00`, exit 0. Archive `D:\RedPrismQualificationArchives\P1-stroke-resources-898-ARM64-Release42-qualified-2026-10-07`; completion `F0512BC51942E4E3849FBEB9FBA20DE922B2E109CF144B81B070461244761B98`.
- **ASan Debug**: actual PID `83300`, `2026-10-07T09:16:59.813960+00:00`–`2026-10-07T09:24:14.773966+00:00`, exit 0. Archive `D:\RedPrismQualificationArchives\P1-stroke-resources-898-ARM64-ASan42-qualified-2026-10-07`; completion `42B8F4850BA69A47ADDE95E93E314E16D179A430464B3011B7786A5C67D8E8F6`.

## Preserved ASan r296 failure

Actual canonical test child PID `79220`, `2026-10-07T09:19:55.248670Z`–`09:37:29.399248Z`, exit **1**,
898 inputs and unchanged freeze. The host suite, sanitizer detection and both crash probes pass. The actual
hidden WARP application exits 5 at `sampler-caller.terminal.captured-leave-prefix-acknowledged`. The immediate
terminal/prefix control passes. Its combined post-settlement predicate does not identify which outcome, palette,
authority or retained-resource condition fails; inspection is required before assigning a cause. The resource
Document/Composition suites ran before the application: the preserved actual suite sections contain 7,676 Host, 7,702 Document, 1,833 Composition and 1,736 I/O PASS controls, with no FAIL in those four sections. The earlier entrypoint-order note was incorrect and is corrected here. Their successful execution does not change the full command's failure; this is not a complete ASan pass.

Archive `D:\RedPrismQualificationArchives\P1-stroke-resources-898-ASan296-sampler-terminal-failure-2026-10-07`
verifies 1,186 copies at `2026-10-07T09:40:12.3911574Z`: source
`16B381F67BF44D050D704C80C7EA1706AF98F8248C6677EACBF896D7F8687902`, evidence
`1631BEC164205B97A924007B14B117A472E476C30B0438EFC552ED224A361F25`, completion
`BD11A5D115EF013A31B33682AB326C0756E476A68C5FA9A4DB4874D28B790098`.
Corrected Release analysis r297 captures two actual descendant compiler responses proving `/MP2` and `/analyze`.

## Release analysis r297 and sampler diagnostic follow-up

- [x] Independently verify and archive r297 Release `build.ps1 -Analyze`: exit 0, identical 898-input freeze `6358FB57C725E348C079A1C5027FFC3B5D33842C860E1E2CEB9C137D4C7ACCA7`, concurrency SAL positive/negative controls, five linked image/PDB identities and ISA controls. Recorder interval: 2026-10-07 09:36:49.297602 to 10:03:19.620199 UTC; actual owned child PID 65532. Actual compiler response copies both contain `/MP2` and `/analyze`. Verifier r2 SHA-256 `856BBC3410A946663445A2E87E47B9F4323DDCD68E51DB675AB0851203E86775`. This is static analysis, not runtime qualification.
- [x] Preserve `D:\RedPrismQualificationArchives\P1-stroke-resources-898-Analysis297-qualified-2026-10-07`: 1,205 independently read-back copies; source manifest `B64FF27DB673D728DE5AA9D82F46D0D65EECBD954F8D1EE0BDA8E051763C9CF3`, evidence manifest `76AD8E24F02553497E910626376D9FA5E3F9390ADD7E97A9345A2384274BE9B9`, completion `C4B1A884D23DFE617FFE9357C3ADDCC21818311211BAF857010ED2641E6E0042`.
- [x] Add failure-only sampler terminal diagnostics after r297 archive. Exactly one frozen source changes: `ApplicationSamplerCallerSelfTest.cpp`, SHA-256 `49F3D231EF7BB34AC93CFD65243B3B2385504E4FE1A10EC7B9195F06A98C39C2` to `63090EBE9EC19D7C31C4A7B978AA9A48D9323B00049D407029C5E00E13CCE0B4`. The same SettleJobs call occurs once with its unchanged 10-second failure bound; receipt outcome/error, actual color, authority, history and every resource predicate print only on failure. No PASS name, golden, expected outcome or cleanup requirement changes. Skills 15 and format 771 pass before rebuild.
- [x] Complete and independently verify ASan r298 against this diagnostic source. The passing investigation preserves r296; it does not establish the unresolved sampler failure's cause.

The original 898-input Debug/Release/analysis/ARM64 evidence predates the diagnostic source change and retains its exact scope. P1 and actual asynchronous stroke activation remain open.

## Diagnostic-only ASan r298 qualification

Actual child PID `52316`, `2026-10-07T10:05:38.785105Z`–`10:18:38.755092Z`, exit 0.
898 frozen inputs, hash `36553441A79E81E0D33999B891EA1833F7AF799CB2B705FD4132AD668E50BCBA`.
The only change from the original resource source is the failure-only sampler diagnostic identified above.
Independent verifier r2 (`EAB6B968706A4AE2AF229B499E56BC612588D104F26E87080CB6D8A4A5342359`)
requires that exact source difference, every predecessor and all 181 ordered resource additions, matching
source/product/PDB/allowlist identities and all five fresh linked-image ISA reports. The four unchanged suite
images/PDBs are byte-identical to their archived r296 products; only the application is rebuilt in this interval.
This is explicit unchanged-source artifact reuse, not a claim that all five images were rebuilt in r298.

Host 7,676, Document 7,702, Composition 1,833 and I/O 1,736 controls pass. The D3D11 debug layer is active.
The application passes all 10,430 named controls and 99 stages, including the unchanged sampler terminal checks.
Worker stress 8,073, raster stress, actual sanitizer diagnostic, both crash probes, benchmark smokes, build-process
controls and harness captures pass. The missing-tree Git stderr belongs to the expected DxUi restore failure
fixture. The intermittent sampler failure is not reproduced and remains unresolved.

Archive `D:\RedPrismQualificationArchives\P1-stroke-resources-898-ASan298-diagnostic-qualified-2026-10-07`
verifies 1,189 copies at `2026-10-07T10:21:44.5462044Z`:
source manifest `B2AE0E7D8F1612E9240D727048CA36C8166EDF3F902A0369246B36F85DDE72D5`,
evidence manifest `3F5ABA9EF9DA734950B7C4A07654D552185541564E63E34FAA988DD70B67B7CB`,
completion `F4E15D1F2B8AE52328F2C835C96F0DCFEE90272F58A1B48ADEEAD98ACE42E5AE`.

The qualified foundation/resource checkpoint can be preserved without declaring P1 complete. Connecting the
actual application, diagnosing intermittent failures and the remaining exact-source final matrix/G1 gates
remain mandatory.
