# Hue/Wand integration: canonical analysis — 2026-10-16

## Verified scope

Actual r1045 canonical x64 Release analysis exits 0: 11:04:41–12:43:07 UTC. Main independently reads
all 1,794 archived files, 943 native compiler command records, five linked executable/PDB CodeView pairs,
the five image ISA scans, concurrency SAL controls and eight expected scanner rejections. The linked
DxUi pin remains `8e8994079e82f79e6765e6a885c03fc687957591` / API 3.

- Exact freeze: 1,377 inputs, including 1,373 repository and four pinned wrapper inputs.
- Freeze SHA-256: `1D0B173CDF5ECA8016FD27B194140039463A15B5B68B6ACF3DCEF6996F0B9B5B`.
- Archive completion SHA-256: `FC468D3CAEAE83FEC2CDFD3F048630B9FD701716D6886BEDF050D91D23704C8F`.
- Source manifest: `BB9E3D990F1FBA6E8ECB450016808CFF94F91DC1DE4F935FF8EE0216FB7F97E1`.
- Evidence manifest: `0D1199F61F47C1CD9101484E532BDEF6016F8B2329CA50FF311D391AA97EED97`.

The coordinator then exits 1 at the deliberately changed next-dispatch admission marker. Main verifies
that this marker is the only changed recipe pin; the completed analysis itself passes and its source
bookend is intact. No subsequent r1046–r1051 profile was launched. Compilers and applications were not
terminated. The source freeze, rather than a clean Git commit, identifies this then-dirty 35-path integration.

## Corrected-source follow-up

Main's independent review finds that preserving-panel Wand changes the document selection while fresh
color jobs still carry the old selection witness. The reviewed correction preserves public panel opening
coverage/token state, binds only the acknowledged Wand selection for newly captured jobs, refuses older
results without closing the valid panel, and rearms a stale ready histogram after its original acknowledgement.

After the analysis archive and coordinator join, Main selectively adopts the 14-path correction and four
contract amendments. Together with the previous integration, 43 code/test/project paths are pending runtime
qualification. Skills (15), formatting (1,228 files) and diff checks pass. The new CPU and actual hidden WARP
host suites declare 28 and 29 assertions; source-only verifier compatibility controls are not runtime evidence.

The fresh complete sequence starts actual Debug r1052 at 12:49:16 UTC, followed by analysis r1053, Release
r1054, real ASan r1055 and ARM64 Debug/Release/instrumented-ASan cross-builds r1056–r1058. Its actual
first 1,380-input freeze matches the predicted identity:
`2EC35CD837CC6873F9A1666655E0C4B5F42A3C9B3C08FBB1FECEECD611867CBF`.
These runs have not completed at this checkpoint. This report does not qualify the corrected source,
ordinary activation, native ARM64 runtime, measured G1 or P1 Done. The complete runtime-qualified
predecessor remains [67a77dd](P1CombinedStartupIoLassoX64_2026-10-16.md).
