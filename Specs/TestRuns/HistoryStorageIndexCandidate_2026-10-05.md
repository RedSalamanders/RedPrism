# History storage index integration candidate — 2026-10-05

## Status: full managed x64 Debug, Release and ASan qualification; remaining gates open

The Main candidate's integrated P1.9 H3 history-storage index passed the scoped normal x64 Debug r74 run. The
run qualifies only the index slice described here, not H3 or P1. Archive
`p1-history-unique-index-x64-Debug-r74-full-qualified` was independently verified with 1,041 copies and 79 stages
at `2026-10-05T04:44:06.2088918Z`.

Integration receipt: `.build/history-index-integration-r1-20261005 in Z:/src/RedPrism`, SHA-256
`CAC1354CA9AD5A571BE3396D139450D1A039F5A0E2719C9D48F85A6D2A6618EA`. This identifies source integration and
is separate from the scoped runtime/archive evidence below.

## Candidate contract

- A sealed immutable `DocumentHistory::StorageIndex` owns Entry/Raster/Selection membership and reference counts. Worker preparation clones the index maps and updates membership for changed entries. Distinct-owner
  resident sampling and bounds construction remain worker scans; membership diffs alone do not qualify delta cost. Undo/Redo transfer entry handles between past and future without changing
  index membership; live-document identities are excluded from retained history.
- `RetainedBytes` samples each unique retained raster's and selection's current resident bytes on query. It does
  not use the index's cached retirement total as a resident snapshot.
- The unique cached retirement bound includes `Raster::StorageRetirementMemoryBytes()` self-bounds, unique layer
  and selection storage, and index/entry metadata. Sealed history-stack metadata is calculated in O(1) from
  immutable capacity. Reference, pending, unmatched and additional sources retain occurrence-based fallback
  bounds.
- Owner publication swaps the index into the prepared publication; the retired sealed index follows the worker
  release lane. A partial index clone remains a unique producer-owned object until checked bounds are sealed.
- `AlwaysOnOwner` is used only for sealed indexes and preserves their true conservative `node.bytes`, including
  bounds below the 16 MiB default threshold. Other callers keep `SizeThreshold`; `DeferredReleaseNode` layout is
  unchanged. An empty worker TLS sink means inline destruction; only the exact registered owner sink can enqueue.

## r74 scoped Debug result

Actual child interval: `2026-10-05T04:32:56.737940Z` to `2026-10-05T04:43:16.646117Z`; process exit 0. The
771-input source freeze is `9FFD6003832CDB4ED4CD1A0CCDB87324A61BA98C2E87F8267030D4BF562D6D6B`, stdout
`D88DDBF738AAA5BB0D968F5E1C7A960BD9942B345297C5B512751DA1BF033A5A`, stderr
`F16DA879E2BCCA6E448E4B6E7E66A22F6246D3BEC03AC19ECFFABE3F226972F6`, and proof
`CADEDCF949B74E6EFD26B0DC497CD9011DAF5A68E08DA294A02E6F2C843E27E0`.

The executed registry has 6,729 DocumentTests assertions and 9,298 Application assertions, with +46 document
assertions and +0 Application assertions over the preceding registry. All 79 ordered stages and preceding
control multiplicities/routes match. The predicted counts are superseded by these observed counts. All nine new `deferred-release.always-owner-*` HostTests policy controls also pass. These are core ownership
checks, not native application retirement timing.

Independent archive receipt: source manifest SHA-256
`8114BE8AE3B5CB225EC6D401248B6B9F9729A24F8ACE855DB0544F0A2D308AFD`, evidence SHA-256
`67C32431C9CEC4DBBF6138F08531CB7CE4016C1EE6774A2B465776A861FE7D83`, completion SHA-256
`5F1285BF02F287D28D45E3783746F27644F6980F71A4B8C2C6D70149DEFD0EEE`.

Private frozen-probe history: r1 failed compilation on a lambda-capture error and C4458 and is preserved; r2–r4
two-translation-unit probes and r5 four-translation-unit probe each exited 0 for code generation before the latest owner
policy and late-budget changes. These probes do not qualify the integrated candidate.

## Open qualification

Concurrent late sparse-cache publication and history trim/reprepare synchronization remain unresolved. A private
Smudge/Liquify private source-copy candidate is still under preparation and is not integrated in Main. Current analysis, ARM64
cross-builds, native activation, G1 timing, caller/delta cost and final H3 gates remain open. No legacy golden has
been changed, and this scoped Debug result does not close P1.9 H3 or P1.

## r75 full Release result on identical source

Actual child PID 83256: `2026-10-05T04:46:01.923837Z` to `2026-10-05T04:53:00.984530Z`, exit 0.
All 771 inputs have the same freeze hash as r74. The observed registry is 6,729 document / 9,298 application
controls, unchanged 79 stages and every preceding control multiplicity and output route; all nine new owner-policy
HostTests controls pass. Registry proof: `618D8FCE5DE428EC2DA12300F594BA9868A99E67CE7503486A481E60359AAEC2`.
Stdout: `332BF9D94E2AF9F7D11E422D7BABA23BB0B4D0154B2F187DAA121C541D1F0245`;
stderr: `504C2F8EC5874BB10A20AE11D10594182962ED9FA86963CC63CA0A307E446010`.
Archive `p1-history-unique-index-x64-Release-r75-full-qualified`: 1,041 copies independently verified at
`2026-10-05T04:53:59.4809193Z`. Source manifest:
`4420E850A602F86EEE33D5471C37B409BC15EF7CF03C5777AA22EC408CA3F0EC`;
evidence: `AFC3BBFEBC3A5EE1C97D64E56FEBF4668B5C89A7D24442D45B99EAB6DC26597A`;
completion: `8A2214E1769A7B66BA2C96D86F085E091F284AA69098580FFA549749DFE81D27`.

ASan Debug r76 passes on this same source; its exact receipt follows. This report refers to the managed candidate at
`C:/Users/eric/.codex/worktrees/p1-opacity-host-proof/RedPrism`; it records no activation of that source in the
native `Z:/src/RedPrism` application checkout. Published r74/r75 source archives remain authoritative.

## r76 full ASan Debug result on identical source

Actual child PID 47476: `2026-10-05T04:54:15.975864Z` to `2026-10-05T05:10:33.392854Z`, exit 0.
All 771 inputs match r74/r75. The complete observed registry remains 6,729 document / 9,298 application controls,
79 ordered stages and preceding multiplicities/output routes; all nine owner-policy HostTests controls pass.
The real sanitizer probe and full in-process/runtime/screenshot gates pass. Registry proof:
`618D8FCE5DE428EC2DA12300F594BA9868A99E67CE7503486A481E60359AAEC2`.
Stdout: `15BA61D49A49C635758B7D874B2BC7E5EB189CFE7D834329079631B1469CA995`;
stderr: `BAA11F5ADF585D950136E39048178258C1BC8945BFB5A16BF0EF5BF915B034B5`.
Archive `p1-history-unique-index-x64-ASanDebug-r76-full-qualified`: 1,048 independently verified copies at
`2026-10-05T05:15:26.0823496Z`. Source manifest:
`4C6DB9D5BD067DFB0050D9DF1F500B48B1E622CFAC934BB939FE2DC7FE362772`;
evidence: `6962802B347F1D02AB8CED442012DD02C4F37D38AB197DC4B37C204C8781FE4F`;
completion: `4136663CFD7D88D370AA5167277450BE996F219AF8A06769029F0CA55432D0C5`.

A later private residency-epoch/owner-permit/worker-refresh protocol, eager fitting sparse Gray8 thumbnails and
Smudge/Liquify source-copy candidate are not in this freeze. Strict private four-TU code-generation probe r6
exits 0; it is not linked/runtime qualification. Host same-ticket refresh, complete admission, live-history
notification/maintenance and all remaining H3/P1 gates remain open.
