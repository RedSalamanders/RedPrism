# Existing Polygonal draft Ctrl corners — 2026-10-19

**Evidence date:** 2026-10-19
**Scope:** private focused x64 polygonal-dispatch evidence for Ctrl corner operations.

## Result

The Main cohort reports 64 cases and 729 native assertions in both Release and ASan Debug; all 534 predecessor dispatcher facts are retained. The runtime receipts are `PRIVATE_FOCUSED64_ONLY_NOT_FULL_APPLICATION_RUNTIME_PASS`. The focused source/PE/PDB/ISA/save-readback receipts report 729 facts, 67 verified artifacts, and zero Application stages. Runtime output includes Ctrl-corner add/subtract modifier cases, four-corner receipt ordering, Enter acknowledgement, undo/redo, and joined-shutdown ownership checks.

This evidence supports the Ctrl polygonal-corner scope represented by these cases. **Ctrl/lift is not supported.** The receipts do not establish physical-keyboard behavior, menu-widget dispatch, a full Application run, ordinary activation, ARM64 runtime, or P1 completion.

## Qualification and limits

- Release runtime and independent qualification: `D:\RedPrismBuildCaches\.build\p1-polygonal-ctrl-corners-main-r1\Release\runtime-qualified.json` and `main-independent-qualified.json`.
- ASan Debug runtime and independent qualification: corresponding files under `ASanDebug\`.
- Main Release and ASan Debug runtime processes exited 0. The pre-ISA control Release runtime in `main-source-preservation.json` exited 5; it is preserved and not counted as a pass. The control cohort’s Release build receipt reports an exact-profile build pass.
- The original Main Release `isa.result.json` remains exit 1. `main-release-isa-correction.json` binds a corrected scanner qualification to the same Release image and PDB; `native_Application_replayed` is false. This is scanner correction evidence, not a runtime replay.
- Main source-preservation receipt records the control fixture as byte-identical and retains source maps and runtime results. The independent receipts explicitly say `ordinary_activation: false` and `P1_done: false`.

## Identity anchors

- Release image SHA-256: `F27322DBF26A4C04D38CF32F7C02BA4B363F2BA598DF27AACDE2307511C194A8`; PDB SHA-256: `458727AC51549E66DA2D513993C4813E66847EA4CA3CF3D4D67FA3D84A2B6405`.
- Release independent-receipt manifest SHA-256: `9647ED2D0B27119E97540AB99F7EBE1A09B3846B1DABF6253D1ECD52C742DEAE`.
- ASan Debug independent-receipt manifest SHA-256: `7C9C4E4F5BBEF58AD36B1B3AD91E26275FF52FA85706F5BA097AB69F1DDC103A`.
- Main source-preservation receipt SHA-256: `453C83EB594A6A83D7A9974E1BFBC1262BFBD183708E881DD08A4D460F36FF60`.
- Original failed Release ISA report SHA-256: `E7C7B150194473B417B8C7FBDC1629C94C0BDC99FF5F434FDAE069A301CFBA37`. Correction receipt SHA-256: `3B8FCBCE109579D49A6924BF203D8EFA99CADDA981EF132A6BD3D8F8F574906C`.

## Sources consulted

- `D:\RedPrismBuildCaches\.build\p1-polygonal-ctrl-corners-main-r1\{Release,ASanDebug}\runtime-qualified.json`, `main-independent-qualified.json`, runtime result/log, and `main-source-preservation.json`.
- `D:\RedPrismBuildCaches\.build\p1-polygonal-ctrl-corners-main-r1\Release\main-release-isa-correction.json`, retained `isa.result.json`, and `IsaScan\report.json`.
- `D:\RedPrismBuildCaches\.build\p1-polygonal-ctrl-corners-control-r1\Release\build-qualified.json`.
- `Z:\src\RedPrism\Specs\Build\Build_Toolchain.md` (current ISA scanner qualification rules).

## Exact scenario and control

Fifteen added cases cover New/Add/Subtract with Ctrl, Ctrl+Shift, Ctrl+Alt and Ctrl+Shift+Alt, plus
Ctrl+Shift+Alt corners accepted before controller admission. Each contributes 13 facts. After an existing
original parent is admitted, its raw journal accepts the later Ctrl corner and the worker retains the
legacy captured drawing mode. Distinct Pending corner receipts, one original/capture, no pixel writer,
all 4,096 selection centers, one history entry, Undo/Redo and complete refunds are checked.
The unchanged Release control exits 5 at the first Ctrl-corner admission assertion: 538 checks, one
failure, 49 completed predecessor cases. The fixed profiles use two fresh TUs plus 349 exact matching
profile objects. Main preserves all three source maps as 2,022 distinct content-addressed blobs in
`original-ctrl-corners-source-blobs.zip`, SHA256
`D573813A01915BA1CFD0AD0672C1DEC2CC4A5784E4EDCF5F252B85249570D7EE`.
That earlier functional source receipt was created before the scanner qualification; the later Release
ISA correction and independent artifact receipt complete qualification without replacing the old failure.
