# Polygonal original writer: export, parked-document retirement and scheduler capacity

Status: selected component QUALIFIED and adopted; P1 remains WIP.
Date: 2026-10-18
Base: b91a7c7fc4b1cd13e638d6bb69ea0a671b14f8de (code `2cb0b64`)

## Intended adoption

- Application.cpp: documentSerial replacement/Clear cancels the existing Polygonal original; a parked original resumes once to acknowledge cancellation. No new input/job/geometry or foreign-session mutation.
- Application.Tests.PolygonalDispatch.cpp: seven new native cases extend28/242 to 35/338: two actual PNG/JPEG exports; two quiet null-selected-path picker cancellation seams; replacement/Clear with no pending raw input; real 128-job scheduler-slot exhaustion. Actual OS file picker and shared history ordinal exhaustion are not covered.
- ApplicationSamplerCallerSelfTest.cpp: split the existing composite predicate and add failure-only scalar diagnostics. Preserve every original condition and the 10-second failure bound. This is instrumentation, not a causal sampler fix.

## Independent references and review

Opaque white source has 4,096 pixels. PNG is exact; JPEG uses the existing earlier host default-quality white-matte6/255 reference (the quality 1 IoTests fixture is not a default-quality reference). Four corner receipts, original ticket, pending draft, history/generation/path/dirty marker, one Enter commit, Undo/Redo and full reservations remain exact. Picker cancellation is the actual quiet/no-selected-path branch; no OS dialog is invoked.

Parked replacement/Clear start only after all four corners/Up, receipts and host descriptors have settled onto one waiting original. No subsequent pointer input is sent. Require old ticket cancellation, original receipt success unchanged, exact foreign 4,096-center mask/absent document and zero history changes/refunds. Unchanged-product focused Release reproduces the missing wake, exit5 at document-notification-alone-joins-original-and-family-without-another-pointer-event (8checks1failure0reached). The candidate passes 34/330.

Main corrects three issues in the cheaper agent scheduler draft before use: join borrowed gate state before its stack lifetime ends on every exit; do not assume all concurrently scheduled runs have begun when the final-index event fires; accept the real constructed-but-unsubmitted scalar diagnostics state while requiring zero runs/captures/writers/parent admissions. All128 accepted jobs remain undrained during the actual owner continuation's Submit refusal. Release/join then requires128 successful completions, unchanged4096-center mask/history and exact ERROR_NOT_ENOUGH_QUOTA. Focused combined Release passes 35/338, with no full-suite claim.

## Retained failure history

ExportR1 Release fails its composite picker-state assertion after PNG/JPEG pass; original state has no scalar diagnostics, so no unique historical cause is inferred. R2 adds explicit pre-operation raw/host rendezvous, retains all post-operation conditions and adds diagnostics.

ExportR2 ASan actual25224 exits5 at inherited sampler-caller.capacity.overflow-retains-brown-one-source-and-exact-failure after all 32/308 new facts pass.59 artifact files and 2,384 exact sources are independently preserved. Historical cause is unproved. Failure-only diagnostic ASan actual101304 subsequently passes all 152stages/10890stdout assertions, both actual Save files, exports, PE/PDB and ISA. A passing repeat does not establish a causal fix; retain a final audit checkbox.

## Qualification placeholders

- Combined full Release35/338,152stages10920stdout: PASS.
- Combined full realASan35/338,152stages10920stdout: PASS.
- Both linked ISA scans: PASS; allowlist unchanged.
- Full PE/PDB/artifact/source/object/output readback: PASS.
- Current-code ARM64 Release/ASan after adoption: PASS, canonical `r1134` / `r1135` on `928e871`; serial dependency setup. See exact cross-build evidence below.
- Local gates: all 15 skills, formatting of 1,311 files and git diff whitespace check PASS. Adoption has exact 351-source / 111-header parity; the implementation is committed and pushed as `928e871`.
- Full milestone native Debug/Release/ASan/analysis, ordinary activation and G1: OPEN.

Artifacts live in D:\RedPrismBuildCaches\.build\p1-polygonal-lifetime-export-native-r1 plus preserved focused sibling directories, original-export-native-r1/r2, and sampler-capacity-diagnostic-native-r1. Native ARM64 execution remains unavailable. DxUi pin8e8994079e82f79e6765e6a885c03fc687957591/API3 and ISA allowlist unchanged.
## Exact completed products

| Profile | Artifact files | Artifact index SHA-256 | PE/PDB GUID / age | EXE SHA-256 | PDB SHA-256 |
| --- | --- | --- | --- | --- | --- |
| Release | 66 | 0EE9DAA3C28A3C3EF82D48993A5920F9A34845B9A7C94FEE42CE8CF95410F588 | CF7F1B09-870E-4C08-833E-EC3028150C7A / 1 | BC91A0864C46ADD533ED7CB35A84904226A85B871DDC4A1DCDEB1D573243E362 | 9CC560BB9E0563585521F415ACCC1080BC65065563A61C02AF6D15B547195589 |
| ASanDebug | 68 | A72863DC186617A0AC9C0EE5631754B04223CD05EE05FF3328AE24E5718DFE11 | 32F1F544-7A12-4880-AA45-BCCE50650A7A / 1 | BCAB0E96B6E9B317397C212027CD910F999BF371C50C7F5AA9E98B68ECFAE2C8 | BC4F1A2EA6A0B0A17B000344F80475554106B4DA627E8623B690C1C7E9B28108 |

Both builds use three fresh application objects plus 348 byte-exact matching-profile objects. Unchanged CPU/kernel/header/shader/project/DxUi/ISA policy inputs are retained. Every completed runtime is serialized. Source blobs and per-profile source indexes preserve all 2,385 exact source inputs per profile; the final Root adoption matches every 351 compiled source and 111 copied headers byte-for-byte.

## Adopted-source ARM64 Release and ASan cross-builds

Both canonical Build attempts qualify exact committed `928e871509f85025c90812122fd3be5a51dbdde2`. Source-only expectations pin 1,485 inputs / 1,481 root project, code, policy, domain and toolchain inputs and all 994 translation units: Application 351, Host 136, Document 285, Composition 138 and I/O 84. Four positive and 22 negative reader controls pass before native launch. The shared source reference SHA-256 is `40A3F048CA3C079BB714241CFA408D20D6EB2A484313DC88D753904B761147F1`.

| Profile / attempt | Actual PID / exit | Fresh / exact inherited objects | Archived files | Archive completion SHA-256 |
| --- | --- | --- | --- | --- |
| Release / r1134 | 77624 / 0 | 3 / 991 | 1558 | 19C145126A26705DF4CB4E77AE26124AF728E297C40128B0EC02AD9B20F1A0F7 |
| ASan Debug / r1135 | 23196 / 0 | 3 / 991 | 1562 | 785B7A06ABA7C74D66DBCB6DDB5A802720A9DCCDE4D21F55B35F7CA00C0234BB |

Every one of the 994 actual command records has the required W4/WX/SDL/stdcpplatest/conformance/UTF-8 profile, exact CRT/optimization/defines, and real ASan instrumentation plus the required forced header in the ASan profile. Each new source and object differs from its preserved current `2cb0b64` baseline; every other 991 source/object pair is byte-exact. The actual link consumes all 351 Application objects. All archived bytes and five AA64 PE/full-PDB identities per profile pass independent readback. No failed native build was retried and no policy/profile/source gate was relaxed.

Evidence: `D:\RedPrismBuildCaches\.build\p1-polygonal-lifetime-export-arm-native-r1`, including preflight/freeze, source-only controls, per-attempt archive/artifact/PE-PDB/command-profile/object-lineage records, preserved old objects and new current-object maps. Dependency setup/builds are serialized to avoid the earlier shared DxUi vcpkg lock failure. DxUi and the x64 ISA allowlist remain unchanged.

This is compilation/link/source/artifact qualification. Native ARM64 runtime is unavailable; no ARM64 UAF detection, functional runtime, ordinary activation or G1 result is claimed. Latest Debug/full native/analysis and the nine remaining delivery areas still gate P1 Done. The retained sampler/inverse-selection intermittent failures remain in the final audit checklist.
