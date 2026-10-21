# Polygonal finite plan and guarded metadata — 2026-10-17

Status: qualified scoped Release/real-ASan CPU prerequisites, adopted. P1 remains WIP.

## Scope

Four model files implement finite Polygonal limits/planning and exact own-key point/cursor/removal APIs.
The Freehand wrapper retains its kind restriction; generic drag-Up `PlanDrawing` still refuses Polygonal.
Quarter-pixel no-op, cursor-only updates, nonfinite refusal, foreign writer/history exclusion and retained
snapshot behavior preserve the numeric reference. Last-corner Pop resets only its prevalidated own draft
and active marker; its prepared owner keeps arena/control lifetime after the live draft disappears.

The finite policy remains 65,536 live corners/1,023 sealed blocks plus inline tail. Backspace/refill never
recycles sealed nodes while snapshots exist; earlier capacity exhaustion refuses without partial output.

## Independent expectations and actual results

- 39 source-defined own-key/metadata/history/capacity/lifetime facts.
- 35 independently tabulated plan/refusal/geometry facts. Literal 12-by-10 masks check every triangle
  and self-intersection pixel, with 28/24 covered centers. No Freehand API supplies expected values.
- All 23,233 predecessor Document facts preserved: complete **23,307** assertion roster in Release
  and instrumented ASan, both actual exits 0.
- Fresh 282-TU builds per profile; all 896 Release and 901 ASan archive files, 282 objects, actual
  compiler/dependency closures, source/product bookends, exact executable/full-PDB identities and
  zero ISA violations independently checked.
- Actual ASan UAF control PID 92480, expected exit 1. Diagnostic hash
  `D85386E894B940017A8C6C041C20598429B46A6BAEF00E3E383476D5BA7C72E5`.
- Runtime archives: all 18 Release and 22 ASan files independently checked, including every raw
  ordered assertion and summary. Main's source-derived adapter has four isolated positive and
  fourteen refusal controls and forwards all predecessor bytes unchanged. No predecessor success
  transcript is synthesized and no runtime output defines expected facts.

The first ASan build reader retained a Release product subdirectory. All 901 archive files, objects,
compiler records and input hashes had passed before that reader-only refusal. The old reader and
profile-path correction remain retained; no compiler or test was rerun for it.

## Receipt identities

| Receipt | SHA-256 |
| --- | --- |
| Release build | `C0DB5E0C11D8DF5F3461ECB4DFB127A0426516E54C6A4670371F6AD66A26A49D` |
| ASan build | `12CEA2F9629D89260721869FD4E71A5DE0C0E70A8B6ACBB8AB2E61C7B78B9CE9` |
| Release runtime | `B121BDFBDD9900BC9DDAD8B0856100F380FC6FE47AB85B7D34B623CFF877280E` |
| ASan runtime | `B05DE2D566D60752F942AE1178246A0161F33D9805B52AFE0F378662C7E6CF9D` |

Private recipes/readback live under `.build/P1Drafts/S5PolygonalCpuRelease-r1-Main/`,
`S5PolygonalCpuAsan-r1-Main/` and `S5Polygonal-r1-Main/`. Main reviews the delegated 35-fact fixture
before integration and independently qualifies its complete actual executions. The attempted delegated
reader produced no files; Main writes and verifies the source-derived adapter directly.

Root formatting initially refused a mixed final line terminator in the new declaration header.
Normalizing it preserves every C++ token; the original qualified build source and formatting
correction receipt remain retained. Full root formatting and skill checks pass after correction.

## Limits

These are CPU prerequisites. The original multi-click parent, New inside-hit choice, physical Up/hover/
conditional-close ordering, Save/tool/lifetime arbitration, complete host admission/retirement, ordinary
activation, latest full native matrix and measured G1 remain open. Native ARM64 runtime is unavailable.
The isolated next drawing-parent prototype is uncompiled and is not included in this adoption.
