# P1.3 All Layers wand New — qualification ledger

## Current status

Private graph on `codex/p1-sampler`, based on `2cfabe9`. r11 compiled and passed Host/Document/Composition/Io,
including all 51 new `wand-composite-core` checks. Its application stage failed one new device fixture; it is not
a complete Debug qualification. Original assertions, numeric goldens and gates are unchanged. The repaired
fixture passes full Debug r12 and full Release r14. The shared request descriptor repair passes full ASan r16 and Release r19. The canvas-dimension fixture repair passes Debug analysis r21. The combined current runtime matrix, ARM64, shutdown and G1 remain due.

Skills: 15 pass. Formatting: 642 files pass before r11 and after the new fixture repair. No current G1 timing,
large-fixture admission or native ARM64 execution claim is made here. The r21 static-analysis result and all profile results below are tied to their frozen snapshots.

## Attempts r17–r21 — latest runtime, build and analysis receipts

These are five distinct child attempts. The freeze hash is the exact `result.json` `FreezeHash`; do not combine
configuration results into a matrix claim.

| Attempt | Scope / result | Freeze hash | Preserved archive |
| --- | --- | --- | --- |
| r17 | x64 Debug `test`, exit 1. Existing other-history readiness assertion fails; cause unassigned. | `D8F949825F1769BF6A18FDFF5175D91CD9AE2E8A7C832CD319C9D469B5A45826` | `p1-wand-composite-x64-Debug-r17-independent-history-failure` |
| r18 | x64 Release `build`, exit 1. vcpkg lock check stops before compilation. | `D8F949825F1769BF6A18FDFF5175D91CD9AE2E8A7C832CD319C9D469B5A45826` | `p1-wand-composite-x64-Release-r18-vcpkg-lock-failure` |
| r19 | Full x64 Release `test`, exit 0, repaired snapshot. | `D8F949825F1769BF6A18FDFF5175D91CD9AE2E8A7C832CD319C9D469B5A45826` | `p1-wand-composite-x64-Release-r19-repaired-full-qualified` |
| r20 | x64 Debug `analysis`, exit 1. The new constant-size assertion was tautological for its test fixture. | `D8F949825F1769BF6A18FDFF5175D91CD9AE2E8A7C832CD319C9D469B5A45826` | `p1-wand-composite-x64-Debug-r20-analysis-failure` |
| r21 | x64 Debug `analysis`, exit 0, repaired fixture. Logical byte count now uses captured canvas width and height; expected value remains literal `32`. | `1B98AA514D4105212784C41205917FE1BA449752B537821CC5FF9A7C5950CDC4` | `p1-wand-composite-x64-Debug-r21-analysis-qualified` |

The archived `result.json` records 762 frozen inputs for every attempt. Archive completion metadata records source/evidence
counts and manifest hashes as follows:

| Attempt | Source / evidence files | Source manifest SHA-256 | Evidence manifest SHA-256 |
| --- | ---: | --- | --- |
| r17 | 762 / 267 | `27495AA1DC47341D85DC2812547897663BCB3BB2899CD34437B6A584E0980E1D` | `F9F3BAB2910FD3D57828FD46656968625EFB655E9EB1F91D8D9A508E6E4C6426` |
| r18 | 762 / 257 | `9374E654984FB5CC00BBE97DE778F32EBB1E8CFF19D7A7614F9AB7BC417278BB` | `38318E3D5E7BCEA8A5E8A6B96C2D61ECE8B6887690DC21D147B3836EDFD55771` |
| r19 | 762 / 266 | `813615F328FA5FF76E6B6B88D990B5BA43A4B13A01ED5E5A386A9EC77C46794A` | `F4B860756D22FF9C527291D84C61605C03712F467FC90B7A7B4CACE5C8657169` |
| r20 | 762 / 276 | `650242C8FD5C42939CF4291916F7D4D05B402AEC8495F977DC9E51C427789239` | `31DAE5F2F1BA4656FED633788C8063649B1CE7A3748627864EA9778D0F6FA75E` |
| r21 | 762 / 283 | `6C65862327CB3D91E56CB4E603FF8AA802DCC19A5A439D67215F56AE632442AD` | `0F26835C94509D0C46C29628F6CAB6CED74596E739ED5B14E48ACE673CAB4658` |

The attempt-root `independently-verified-archive.json` receipts were checked against each archive's
`archive-completed.json`, `source-manifest.json` and `evidence-manifest.json`. For all five, archive path, copy count,
source/evidence manifest hashes, completion hash (SHA-256 of `archive-completed.json`) and actual exit match. The
verified copy totals include each archive's source and evidence files; attempt stages distinguish executed runtime
stages from build/analysis-only attempts.

| Attempt | Verified copies | Stages | Completion SHA-256 | Verified UTC |
| --- | ---: | ---: | --- | --- |
| r17 | 1,029 | 64 | `4C47BDBA238D55A710F005FA8AF47E940175A98ABEC411CB3FC0DF36C3A0198C` | 2026-10-04T22:39:47.5039727+00:00 |
| r18 | 1,019 | 0 | `06A3201F95D77038701CFAF463C7C25D7E9CE2675D1D92DA71C971F252CDEABD` | 2026-10-04T22:35:02.0436530+00:00 |
| r19 | 1,028 | 79 | `7F4D9FA68C05F97C7DD8EA8D1C1F7AC0EFD83AB74F079C440302EBCB602E28D4` | 2026-10-04T22:54:22.3007673+00:00 |
| r20 | 1,038 | 0 | `0543BCC93269D2A072B9D1652D0E00373B01A23D7A5B0F14EB3459C9DC91D1BA` | 2026-10-04T23:06:31.1225635+00:00 |
| r21 | 1,045 | 0 | `F89B0CB71A56FEF51877B239E6E54B0FA0289D430616AA5900804CCE0E61EF40` | 2026-10-04T23:19:39.1294900+00:00 |

The independent archive verification confirms preservation and identity only; it does not upgrade a failed attempt or
combine the separate r19 Release and r21 analysis runs into a full matrix qualification.

## Full Debug r11 — original new-fixture failure

- Child 78284: `test.ps1 -Platform x64 -Configuration Debug`, 2026-10-04 21:38:00.770608Z to
  21:45:28.666893Z, actual exit **1**; no automatic retry. All 762 frozen owned inputs remain identical.
- Freeze SHA-256 `DAB3500CEE25A525EECF9A62961ABC18CADF2E16B75EC49C02E7062C1AE444B3`;
  stdout `E878061B59A6D05746BF5A1B96F16F2130F6985193C765DA0E7CBD9D4D66EC3C`;
  stderr `012E192A99F7AF0757F2E6F3223AA74F99AD84AEB4E755885B7067D8A5F7B9E1`.
- All five images build/link; matching-PDB ISA/scanner controls and DxUi provenance pass. Host, document,
  composition and I/O suites pass. The 51 composite core checks report zero failures. Crash probes and CLI help pass.
- Application reaches 43 stages and 111 new composite host checks, with 110 passing and one failure:
  `device-after-copy.copied-bytes-survive-retired-device`. The fixture assumed `GpuWorkQueue::RetireDevice`
  increments generation. Code inspection confirms it clears device/context/completion route without changing
  generation; `Bind` increments generation. The fixture boolean was therefore false even with successful
  publication and retirement. The preceding host mode checks pass, including cancellation, source replacement,
  exact literal selection, captured settings/AA, source/history preservation, ordinal barriers, window-message
  dispatch, actual full-record-table refusal, map/utility release and off-owner aggregate retirement.
- Archive before any output overwrite:
  `C:\Users\eric\AppData\Local\RedPrism\QualificationArchives\p1-wand-composite-x64-Debug-r11-retirement-fixture-failure`.
  All **1,029** copied files independently verify in length and SHA-256, at 21:45:56.0543075Z.
  Source manifest `8FFD486C81E546AB83B99CE79545DE15C9AA53209A484F194695D14BDD4C4970`;
  evidence manifest `C47534586FDF84F2F053871ACDD88CD57B922CA53A5DC580FE1EF3CB6546C271`;
  completion `97B84EADB0BF17847B5CBB31F3266A774254AA29A12EB438CB0B6BA1F3FCD555`.

## Repair and pending follow-up

The new test now exercises `Renderer::RecoverDeviceForTest` followed by actual composition/queue rebinding while
the detached prepared CPU selection is held. It requires both renderer and queue generation to advance, current
device binding, the original single source capture and successful exact selection after acknowledgement. No
application algorithm, legacy expectation, scanner allowance, stack threshold or numeric tolerance changed.

## Full Debug r12 — pass

Child 54460 runs the complete `test.ps1` entrypoint, 21:46:41.009836Z to 21:55:25.916607Z, actual exit **0**.
All 762 inputs remain unchanged, freeze `F2B50CD0EE9A60D95E28F37078FE0BD2521F4025122FF658B53E89CA535C68B1`.
Stdout `F94B8FAE42FDFC6ADAFA2AEA40B4DD206BA14158E18BF4811BB2CDEE3D0C0ECE`,
stderr `E12F7C584FEE4E3F1A89BE07397B56038B0C5ACDD8E5EB76999DF9453A8B70E8`.

All five images, matching-PDB ISA/scanner controls, Host/Document/Composition/Io, WARP application, crash probes,
CLI, build-process/deep-restore controls and harness screenshots pass. Independently compare archived literal
registries with preceding r10: every original document/application control is unchanged; exactly 51 core and
111 explicitly enumerated host checks are added. Counts are **6,555 document / 9,099 application controls**.
The original 78 stages remain in the same order, with the single new stage giving **79**. Existing sampler FIFO
2,653 and clipboard-transfer 146 checks still report zero failures. Registry proof SHA-256
`C5BB4CB6BCF33829E7B2483B8EE53E106A6A8C8F9FD3FED977D62CC82EFFAC46`.

Archive `C:\Users\eric\AppData\Local\RedPrism\QualificationArchives\p1-wand-composite-x64-Debug-r12-full-qualified`:
all **1,029** copied files independently verify, at 21:56:32.2987849Z. Source manifest
`616D0F8337F2646DD7023D8B90E21658DDACD25DD15B8BF2D4B86DF81D94D9C3`, evidence manifest
`5369CA4A68E3654191950C1D6BB799A19E2847340420EAA5A7DD16B47EE4CE22`, completion
`10015EC3DE4BDD9CE654156E5FBB55985E58D7AD8DBC2D9C067215DBABEE2151`.

## Release r13 — build-only pass

Child 48404: `build.ps1 -Platform x64 -Configuration Release -MaxCpuCount 2`, 21:47:39.792804Z to
21:53:17.952641Z, actual exit **0**. Same 762-input freeze as r12. All five images, ISA/scanner and DxUi
provenance pass. No runtime stage is counted. Stdout
`E1BE7B57570D5CF95E9017E1A12A145668C9883E2F7E8F2885D68FE05D072551`; stderr is empty.

Archive `C:\Users\eric\AppData\Local\RedPrism\QualificationArchives\p1-wand-composite-x64-Release-r13-build-qualified`:
all **983** copied files independently verify, at 21:54:49.1943833Z. Source manifest
`805F17653E2697B96979BC849C15BBAB479FA0657B1313B852AE1921D247B5AC`, evidence manifest
`C01E371F3349C012E116CC14D51EADCAE6249B68FB74C69731E2DFAE4E95E83E`, completion
`35699FAF31AA81802F5536D17F247BB207D30CD31BA9B47515D7C52A0056915A`.

## Full Release r14 — pass

Child 76692: complete `test.ps1 -Platform x64 -Configuration Release`, 21:58:30.622783Z to
22:04:51.798324Z, actual exit **0**, same 762-input freeze as r12. All five images, ISA/PDB controls,
Host/Document/Composition/Io, WARP application, crash/CLI/build-process/RAW corpus and screenshots pass.
The independently verified literal registries preserve all preceding controls: 6,555 document / 9,099 application,
79 stages; 51 new core, 111 new host, 2,653 sampler FIFO and 146 clipboard checks.
Stdout `85E8773656AE1E27CE0A25DBBA09ABC1F9C7026537B856CF816942272AA55DED`,
stderr `E40CB1087E34C013D1ED3D1C334AE9D1A42F0D05CF833A13948340B03E1B7756`.
Archive `p1-wand-composite-x64-Release-r14-full-qualified` under the same local qualification archive root:
1,028 copies independently verified at 22:06:41.6871149Z; completion
`0F38C60EE9F83E73F35B4E9ED93F73F8F55EB0F765074EFDD296E253E4DBFFDF`, source manifest
`FC4D2CFD5B5E26553070251BDF5A90FE94D16F1D5887E9C2377EEB623F99CFE4`, evidence manifest
`FE780CD91E7CAB3EC13F7CA861A844E8FB0F90FE072699EC248CB182ED95EE81`.

## ASan build r15 — ISA rejection and descriptor repair

Child 92252: ASan build, 21:58:31.482821Z to 22:00:55.046739Z, actual exit **1**.
All five images link; the unchanged scanner rejects bytes at `RedPrism.exe` VA `0x1405a6f98`, attributed to
`ApplicationLayerMetadataSelfTest.obj`, interpreted as `vcvtsd2ss`. No runtime executes and no ASan finding is implied.
Disassembly and matching PDB identify these bytes as a compiler-generated dispatch table DWORD `0x005A63C5`,
after the function return at `0x1405a6f65`. The enclosing STL raw-variant construction visitor starts at
`0x1405a5b00`, size 5,484. Adding a 32nd variant alternative moves its dispatch table into the 64-entry tier.
Both wand events have the same captured fields and coalescing family. The repair shares their descriptor type and
selects the separate worker graph using captured `settings.sampleAllLayers`, restoring 31 top-level alternatives.
Core composite/layer validation remains distinct; the actual queue test also checks the captured All Layers flag.
No scanner change, suppression, ISA allowance, stack threshold, golden or older assertion changes.

The original failure archive `p1-wand-composite-x64-ASanDebug-r15-build-failure` verifies all 850 copies,
completion `EF608720676C2A71E3FF7231A7A45BEB92B2BC69309EB237819C2BA768161561`, source manifest
`B9864BD18FFA0B849CE34218556E20607A0827C3E26F3F65771387606A5FDB0F`, evidence manifest
`83D6DCFD82A893D0DF1A8EA6630A2D8AED91ED7534636CE5DE294DA9FE06BA74`, verified 22:02:38.1860268Z.

## Full ASan r16 — pass on the repaired descriptor

Child 40512: complete `test.ps1 -Platform x64 -Configuration "ASan Debug"`,
22:11:59.071869Z to 22:24:39.532302Z, actual exit **0**. All 762 owned inputs remain unchanged,
freeze `D8F949825F1769BF6A18FDFF5175D91CD9AE2E8A7C832CD319C9D469B5A45826`.
Stdout `0FE1698CB5FECD53F41A4A2410740DEB6368EC790AB6C8170AC781DCFEB6FA70`,
stderr `3D4ED4113A5B039EC9627C3C1F8E9E9B2CCDAAD466116BE592CEFC63E5CDF2F7`.
All five images, unchanged ISA/PDB controls, real sanitizer probe, complete suites, WARP app, crash probes,
CLI/build/RAW corpus and harness screenshots pass. Exact original literal registries and original ordered stages
are independently preserved: 6,555 document / 9,099 application controls, 79 stages, 51 new core / 111 new host,
2,653 sampler FIFO and 146 clipboard checks.
Archive `p1-wand-composite-x64-ASanDebug-r16-full-qualified`: all 1,035 copied files independently verify at
22:25:28.4269401Z; completion `3A03D9AD81E528DAA043D89C028CFA074C4C2DE0580774080A10A0EE2D9DA289`,
source manifest `419C46952D37EE759895B12280D02526C34633EB7B79FC9E45CF5267506C21A4`, evidence manifest
`C6CAC108ECB329EAF1F5B15D8312C85A5E1A52ABDE09C4B1960D5B5A516EDAFA`.

## Current remaining qualification

The full combined current runtime/analysis matrix, ARM64 cross-builds, graph-specific shutdown,
A/B capacity/G1 and complete parent P1 integration remain due. No profile result is generalized across a changed
snapshot. The actual sampler remains synchronous; its separate prerequisite/caller work is still active.
