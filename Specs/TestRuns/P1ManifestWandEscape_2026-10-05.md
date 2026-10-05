# P1 committed-resource manifest, wand modes and sampler Escape

## Delivery checklist

- [x] Preserve the qualified mip predecessor `af7986f` and its full configuration matrix.
- [x] Prepare a CPU-only committed-resource manifest before requesting a measured GPU reservation; retain original CPU/source ownership through GPU admission failure.
- [x] Add prepared wand New/Add/Subtract and captured anti-aliasing semantics with unchanged original numeric/trace controls.
- [x] Route Escape during a physically held sampler gesture through immediate capture release while the original cancellation receipt waits for acknowledgement.
- [x] Independently review the cheaper agents' fixtures; run fresh strict private Main component proofs (142 wand / 33 manifest / 43 original plan controls).
- [x] Initial full Debug r150 and all three ARM64 r14 cross-builds pass on 829 identical inputs; independently archive every copy before source edits.
- [x] Preserve Release analysis r151 failure: test function stack 239,960 bytes exceeds the unchanged 131,072-byte analysis limit.
- [x] Split that fixture into bounded functions, retaining every expectation; fresh private wand r2 reproduces the exact 142-control registry.
- [x] Repaired identical-source full Debug/Release/real ASan, Release analysis and all ARM64 cross-builds.
- [x] Verify all predecessor controls/routes/stages, the ten actual held-Escape controls, registration and copied source/product evidence.
- [ ] Commit and push the qualified combined slice.
- [ ] Integrate complete first-display resources, post-mip consumers, host Add/Subtract, Native activation and measured G1 under the parent plan.

Status: full combined slice qualified; commit/push pending. This record does not close P1 or authorize native activation.

## Changes and references

`RasterResidencyManifestWork` lists unique committed raster textures, two mipped canvases, conservative clipping-stack surfaces, R8 layer/group coverage, each table adjustment and each hue/saturation cube. Grain has no lookup resource. CPU preparation reads immutable metadata and uploads no pixels. Additional transient preview/brush/pipeline resources and complete composition mapping remain due.

CPU admission precedes retaining source/plan owners. The GPU total includes descriptors, staging and driver margins; actual DXGI/local residency reservation is still required before any GPU creation. A manifest is not GPU admission. Existing conservative two-domain `RasterResidencyPlanWork` callers and its 43 controls are unchanged.

Prepared core wand cases cover matched union/subtraction, explicit empty/null/nonempty no-match behavior, captured antialiasing, stale authority, undo/history and composite source preparation. Normal host Add/Subtract and Clipper peak/cancellation admission remain open. Original exact trace goldens remain exact; new boolean polygon checks allow a cyclic starting vertex and remove only forward-collinear intermediate points before comparing independent literal corners.

The held sampler scenario sends real HWND Escape without releasing the original pointer first. It proves immediate gesture/capture release, original ticket/receipt retention, cancellation acknowledgement and no palette/history/source mutation or leaked CPU/GPU/opening ownership. The keyboard fix calls the existing sampler boundary seal before cancellation.

## Evidence boundary

Initial freeze: `DC12C54FC8A61BE06C381D2A77AF25BAAD24713A215C6D9F88402F4A65D785A0` (829 inputs).
Repaired freeze: `62F5449E81C89313D5DAB90141FE11AC336E2DCBC3E41B90EB567F74867F90E3` (829 inputs). Only the wand test fixture changed between these freezes; expectations are identical.

Fresh private Main wand r2: actual child 30612, exit 0, 2026-10-05 18:15:46–18:16:41 UTC, 391 owned inputs, freeze `4D68A118EBB9D74564DC64DF24BE3F5165DAC70D267C360F7D8D81382978CF1C`, stdout `0318ECD553EDD6F476952E2BDC92D4239F64F39BD1D6144D2ED6949054BD431F`.
Fresh private Main manifest r1: actual child 69856, exit 0, 17:53:46–17:54:39 UTC, 392 owned inputs, freeze `B83A5DC080766D9CC7C720A2F671F8B1783ACB33104243BE0C695D2CE1C2EAE8`, stdout `4D18235C10323E9A15382421F22F6227C0298CAA347180881DDA6D000F8F923F`.
Private strict Debug CPU probes own frozen sources/drivers/products and borrow pinned dependencies; they are not normal host/GPU/profile qualification.

Initial Debug r150: child 30932, exit 0, 17:58:55–18:13:56 UTC. Archive `p1-manifest-wand-escape-x64-Debug-r150-before-fixture-split`: 1,098 verified copies, completion `66DA3067F09D144963E945CDDB7DAFE6CE6C94355C52716FB91C3B1EA7594EFB`, 79 application stages. The actual held-Escape case contributes ten checks. Repaired full suites must reproduce them and every predecessor.

Release analysis r151: child 18260, exit 1, 18:00:10–18:05:27 UTC. Archive `p1-manifest-wand-escape-x64-Release-r151-analysis-failed`: 1,105 verified copies, completion `A1B911C8364CA00215D3AA02BB8CB39C1CD35DC1A0428BF8E248C42061F66DF9`. C6262 on the enlarged test fixture; no warning suppression or stack threshold change. The original failure remains preserved.

Initial ARM64 r14 Debug/Release/ASan Debug: actual children 98188/92188/88348, all exit 0. Independent archive completions `D2D7A54F31C15D86455E20D9D07DBCA04B220D6894330568D596216F359ED8F8`, `9D64647ACED9351B66997BFC490DC46E6556FB4F8A9755A090E99D2F9BE604C3`, `1C7791B4EBD4818CFF52F8F61E895EE128FC6D07159F0869BFF11E88B7DB5A49`. These are cross compilation only, with no native ARM64 runtime claim.

## Repaired qualification matrix

All rows use the same 829-input repaired freeze above. Every child exited 0. Each archive was copied and independently read back before source changes or same-profile product overwrite. ARM64 rows prove cross compilation only.

| Configuration | Actual child | UTC interval | Independent archive completion SHA-256 |
| --- | ---: | --- | --- |
| Debug | 46652 | 18:17:23–18:28:14 | `2D79D2236EA305903A28A152841611660E4FEB5545DBE739AA706A65672F3D90` |
| Release analysis | 93736 | 18:17:58–18:25:40 | `03330E0B95BC2C8FB72D800343A64171A5F15598461039700EE00263DABCA0D5` |
| Release | 99444 | 18:29:42–18:36:02 | `A7F9BE4AA5978C252DB2DC4007F1B5F962B1824C71CC9DCB9B6144A2281E457A` |
| ASan Debug | 35072 | 18:46:37–18:58:51 | `2ECE083B6DFB017807F452C21601B55CD9C00CA0BB28C62CE7815C9D7658B483` |
| ARM64 Debug cross-build | 97252 | 18:17:59–18:19:42 | `3049D5C86FA62F0E053D3467315CF71A2955AFFAD6E8CB2349ED6D40C2800CD2` |
| ARM64 Release cross-build | 94168 | 18:20:00–18:20:24 | `19FB85C0C81D3D662C588BDF19B033CCFDBCD17E8AC3A3CB9D4F7D8C8D668826` |
| ARM64 ASan Debug cross-build | 93248 | 18:20:31–18:22:10 | `FD9B74F8087A81526FAB3680C99448B74C5BB9928AB6C3A882B418D942D0F6AF` |

Archives are beneath `%LocalAppData%\RedPrism\QualificationArchives\p1-manifest-wand-escape-*`. Full runtime registry verification preserves all original routes and 79 application stages: DocumentTests 7,081 → 7,139 (+33 manifest, +25 wand), CompositionTests 1,668 unchanged, HostTests 7,524 unchanged in Debug/Release and 7,525 unchanged in ASan. Application controls 9,744 → 9,754 (+10 held-Escape). All 7,922 ASan stress controls are preserved exactly.

The real ASan expected-failure diagnostic is independently archived as a supplement. Diagnostic hash `42E063631482B1EDD422BB9100D07D14A2B34665378F09335F027A50C3C5B1D9`; receipt `B7A572ED122A9592495409793654C8874DED597CE678A22382DE3508BBE037C0`. Its timestamp belongs to child 35072’s actual interval and it contains `AddressSanitizer: heap-use-after-free`. Original archive receipts remain unchanged.

`validate-skills.ps1`: 15 skills pass. `format.ps1 -Check`: 708 files pass. The full test paths include ISA controls, crash probes and harness screenshots; a successful build alone was not used as runtime qualification.

## Remaining integration

The next host slice moves hierarchy/name work to an admitted worker and publishes prepared rows with bounded lookup. It must retain the original memory/source owners through active chrome use and worker retirement, validate document/collapse/locale authority, and qualify actual pinned-DxUi notification/layout/accessibility costs. A prepared CPU leaf alone cannot satisfy H4 or G1.

[Parent P1 plan](../Plans/WIP/UiNeverBlocks_2026-09-22.md), [bounded upload/first-display plan](../Plans/WIP/BoundedRasterUploads_2026-10-05.md), [sampler plan](../Plans/WIP/SamplerReadback_2026-10-04.md).
