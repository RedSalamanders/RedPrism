# P1 panel/Wand follow-through — full Debug qualification

Date: 2026-10-16. Scope: the selectively adopted 43-path Hue, preserving-panel Wand, hard Paint parity,
private distortion-read and post-Wand panel follow-through integration. This is a Debug checkpoint.
The remaining analysis/Release/ASan/ARM64 matrix, ordinary activation, measured G1 and P1 Done stay open.

## Source and actual execution

- Repository: `C:\Users\eric\.codex\worktrees\p1-opacity-host-proof\RedPrism`, branch `codex/p1-opacity-host-proof`.
- Frozen inputs: 1,376 repository inputs plus four pinned runner inputs (1,380 total).
- Source-freeze SHA-256: `2EC35CD837CC6873F9A1666655E0C4B5F42A3C9B3C08FBB1FECEECD611867CBF`.
- Actual full canonical x64 Debug test/rebuild attempt: r1052, PID 83108, exit **0**.
- UTC start/end: 12:49:16.505673 / 13:34:56.800894.
- Parent stdout SHA-256: `94290C65DBA520D7C27DF606349C80B7328835A4FEBFF3316E8E8057C20FF2A2`.
- Parent stderr SHA-256: `62DD3CF8536851CB45FF1E38C8DFB2B3589757A22E7C40627247A6B1DDBE14A1`.
- All 15 skills, formatting of 1,228 files and diff checks pass before the native attempt.
- DxUi remains pinned to `8e8994079e82f79e6765e6a885c03fc687957591`, API 3. The normal rebuild
  compiles its pinned cached library; no DxUi source or pin changes are included.

## Result and independent checks

All canonical suites, crash probes, help and harness screenshot gates pass. The exact source-derived roster
contains 21,863 Document facts; 142 Application stages; 9,023 Host PASS records plus its Hue summary;
7,391 composition records; and 1,953 I/O records. The newly added follow-through families contain **28 CPU**
and **29 actual hidden WARP host** assertions, with zero failures. The Application stdout assertion count
is 8,271 and stderr assertion count is 7,613; non-PASS summary records are counted separately.

These cover both Wand sources, original opening coverage for preview/OK, Cancel and reopen, rejection of
an arbitrary later selection, old-result refusal without closing the current valid panel, and delayed
histogram completion retaining its original ticket until acknowledgement before one replacement attempt.
They do not establish ordinary activation or the complete phase performance budgets.

Main independently hashes all **1,805 archive files**, verifies every live source bookend, **945 actual compiler
command records**, five actual x64 images and their matching CodeView PDB GUID/age pairs. Archive:
`Z:\CodexArtifacts\RedPrism-P1\2026-10-12\P1-LassoHost-x64-Debug-r1052-passed-20261016`.

- Source manifest: `F47A7D1C8900A91F6578844E6EF0C33E9DA7358899061EC939FFAE7B5BD6EA53`.
- Evidence manifest: `9CC5FAC3520037FB81C79B42CEC5CA8F2092748EE64E2FEB52AD7F8951CA9FE1`.
- Archive completion: `F484BB371E7D0E81AB941BCF153FDF26C1AE90E3362CC4594F27B5CB1B9199AD`.

Local independent receipts are under `.build/P1Integration/LassoHost-r1-Main/`:
`r1052-independent-roster.json`, `r1052-independent-artifacts.json`,
`r1052-independent-pe-pdb.json` and `r1052-main-closeout.json`.

## Retained verifier refusal and correction

The original r2 coordinator stops after the successful native command because its strict Host comparator
expects `bench-machine.host-configuration-release`; actual Debug correctly emits
`bench-machine.host-configuration-debug`. All 9,024 ordered Host events otherwise match exactly.
The original failure, raw streams, archive and recipe pins remain preserved under
`.build/P1Drafts/PanelWandNativeMatrix-r2-Main/`.

`Tests/HostTests/BenchmarkMachineTests.cpp:76` and `Common/BenchmarkMachine.cpp:422` define the
configuration witnesses from `__SANITIZE_ADDRESS__`, `_DEBUG` and the Release branch. The fresh r3
reader pins both source files, requires exactly one raw witness matching the requested profile, and only
then maps that verified label to the historical Release comparator. Three positive profile controls and
six wrong/duplicate/missing/failed/unknown rejection controls pass. All other expected records, exits,
ordering, source/artifact identities, ISA checks and real sanitizer-probe requirements remain enforced.
This correction changes no production source, test expectation or legacy golden and reruns no native child.
The fresh reader accepts the preserved archive, then the independent artifact and PDB checks pass.

## Continuation

The fresh r3 coordinator admits only unused r1053–r1058 attempt roots, pins its corrected reader and the
Debug closeout, and preserves the same source throughout. It stops and archives the first actual failure.
The remaining gates are Release static analysis, full Release, real x64 ASan, and all three ARM64
cross-builds. Native ARM64 runtime is unavailable and is not claimed. The parent P1 delivery checklist
remains open until implementation, ordinary use, complete bounds and the specified G1 measurements close.
