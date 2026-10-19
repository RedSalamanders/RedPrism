# Ctrl host, typed pixel journal and colour capture: current native matrix

## Checklist

- [x] Complete full x64 Debug, Release and real ASan validation on the same combined source.
- [x] Cross-build ARM64 Debug, Release and instrumented ASan on that exact source.
- [x] Independently verify archives, actual compiler/link records, five executable/full-PDB pairs per profile and native scope.
- [ ] Finish the original-parent final publication integration, remaining delivery gates and measured G1.
- [ ] Qualify final-source analysis and the eventual fully integrated milestone. P1 remains WIP.

## Immutable scope

The source is parent `ac17fee90050fb3c14a7abb3d1329fcbf2d705dd` plus the reviewed Ctrl/Polygonal host,
typed pixel journal, destructive/live colour target capture and immutable final-source delta. All six profiles
share 1,865 immutable inputs, source-freeze SHA-256
`D742CAE7F6D21DC4741C1CECC1DFC2FD38A954B6160A37F2F4064A0BB790F6B1`,
and working-delta SHA-256 `B4D2C2DFB9821CE1AF12AE251B14CD436507F28144372F688A315BCCBF37DC7B`.
Each profile verifies 1,011 compiler records: Application355 / Host136 / Document297 / Composition139 / I/O84.
Every profile has five independently matched complete PDBs and executable identities; x64 ISA scans have
zero violations. The pinned DxUi source/library and ISA allowlist are unchanged.

## Actual results

| Platform | Configuration | Attempt | PID / exit | Archive files | Archive manifest SHA-256 |
| --- | --- | --- | --- | --- | --- |
| x64 | ASan Debug | `j2a1` | 26660 / 0 | 3316 | `9C278F352B3D07AB4BD585E70943A5355DC44D5DDC6253619EB9762B4E1A4B79` |
| x64 | Release | `j2r1` | 6360 / 0 | 3178 | `8BCCF3A020EB368F977AD6B95C9394A7AFAFEB6854DCE13EE4399B488F4963C6` |
| x64 | Debug | `j2d1` | 85560 / 0 | 3177 | `0E44F204BB122742E37181F5403130F2CDC7843EF3263D4F5DA14B45FBBE0DB1` |
| ARM64 | Debug | `j2armd1` | 72424 / 0 | 2964 | `D989846B4F4258666F567C25B596A34530DFD524CCB9AFE882D30A589777ECB7` |
| ARM64 | Release | `j2armr1` | 74192 / 0 | 2964 | `074E44C94D2E2F05F13B40C52BB5CB78DDBF2E70AB6296CE8BB1F859521B99BA` |
| ARM64 | ASan Debug | `j2arma1` | 66400 / 0 | 2968 | `57EE021EBAE793066E49E0ED23359BF0189E11230B8191D124446C4811FEF6AD` |

Each x64 run passes all five native suites, 154 Application stages /11,814 named stdout facts,
the exact25 live-adjustment capture assertions, both crash probes, the required smoke/control suites,
the offline19-file RAW corpus/license/download controls and nine fresh harness captures. DocumentTests
passes23,973 facts in Release/ASan and23,979 in Debug; the six Debug-only differences are retained.
ASan additionally passes the actual poisoned heap-use-after-free probe,256-round raster concurrency
and64-worker stress, plus both direct/prepared cancellation smokes. Its genuine UAF output SHA-256 is
`CB135E3985EFF41FCBDE9E8B14BD5A7F9F0AC73C8AFDDEA70F080471394DD367`.

Evidence is under `Z:\src\RedPrism\.build\P1Evidence\p1-ctrl-journal-color-integration-r1`.
Each `*-archive-qualified.json`, `*-profile-qualified.json` and x64 `*-runtime-qualified.json` is
independently read back; `six-current1011-profiles-qualified.json` binds the shared source and scope.
The x64 coordinator joined at2026-10-19 22:10:35UTC. ARM64 is cross-build evidence only: no native
ARM64 runtime host was available. The newest-source full analysis remains due.

## Retained failure and limits

Earlier `j1a1` exits1 at its ambient-pointer cancellation-smoke guard after the native suites, stress,
UAF and crash probes. Its original archive and the separately passing same-binary direct/prepared
smoke retries remain intact. The complete clean `j2a1` pass establishes current validation; it does
not identify the historical input sender or establish a causal repair.

The original-ticket publication component's newer private242-fact WARP/hardware acquired-prefix/Close
proof and subsequent host/sizing prototypes are outside this actual-source matrix. Their integration,
complete ownership phase bounds, ordinary route activation, Image Size Apply cost and all nine delivery
areas retain their separate open gates. Smoke runs are functional checks; no G1 or Photoshop speed claim
is made. P2-P6 RAW/SIMD/GPU delivery remains in the separate follow-up plan.
