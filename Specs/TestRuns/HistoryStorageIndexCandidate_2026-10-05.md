# History storage index integration candidate — 2026-10-05

## Status: scoped x64 Debug qualification; remaining gates open

The Main candidate's integrated P1.9 H3 history-storage index passed the scoped normal x64 Debug r74 run. The
run qualifies only the index slice described here, not H3 or P1. Archive
`p1-history-unique-index-x64-Debug-r74-full-qualified` was independently verified with 1,041 copies and 79 stages
at `2026-10-05T04:44:06.2088918Z`.

Integration receipt: `Native.build/history-index-integration-r1-20261005`, SHA-256
`CAC1354CA9AD5A571BE3396D139450D1A039F5A0E2719C9D48F85A6D2A6618EA`. This identifies source integration and
is separate from the scoped runtime/archive evidence below.

## Candidate contract

- A sealed immutable `HistoryStorageIndex` owns Entry/Raster/Selection membership and reference counts. Prepared
  changes update only changed entries. Undo/Redo transfer entry handles between past and future without changing
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
control multiplicities/routes match. The predicted counts are superseded by these observed counts. No separate
HostTests policy count was supplied.

Independent archive receipt: source manifest SHA-256
`8114BE8AE3B5CB225EC6D401248B6B9F9729A24F8ACE855DB0544F0A2D308AFD`, evidence SHA-256
`67C32431C9CEC4DBBF6138F08531CB7CE4016C1EE6774A2B465776A861FE7D83`, completion SHA-256
`5F1285BF02F287D28D45E3783746F27644F6980F71A4B8C2C6D70149DEFD0EEE`.

Private frozen-probe history: r1 failed compilation on a lambda-capture error and C4458 and is preserved; r2–r4
two-translation-unit probes and r5 four-translation-unit probe each reported zero issues before the latest owner
policy and late-budget changes. These probes do not qualify the integrated candidate.

## Open qualification

Concurrent late sparse-cache publication and history trim/reprepare synchronization remain unresolved. A private
WARP copy candidate is still under preparation and is not integrated in Main. Full x64 Release/ASan, ARM64
cross-builds, native activation, G1 timing, caller/delta cost and final H3 gates remain open. No legacy golden has
been changed, and this scoped Debug result does not close P1.9 H3 or P1.
