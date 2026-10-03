# Wand selection cancellation and chunk boundaries

Date: 2026-10-02.

## Scope and implementation

This evidence supports S4a core/API foundation only. It qualifies cancellable core selection reads, mask construction and tracing, bounded chunk checkpoints, and their literal behavior. Direct UI wand, clone and sampler callers remain synchronous. It does not qualify job admission, Clipper integration, GPU work, history publication, 25 ms UI cancellation, G1, RAW, or Photoshop workflows.

The core uses fixed scalar chunk loops and a noinline checkpoint state. Cancellation checks bound explicit loops to 4096 units per stage. A safe scalar 64-bit zero-vertex skip uses `memcpy` within the chunk. Math, topology, and the legacy eight-million-edge limit are unchanged. The result preserves `None`, `Memory`, and `TooDetailed`; `Cancelled` is distinct. Private sparse reads do not publish a source cache. The local and shutdown owner event gates each request real cancellation after reaching step 4096; a fresh token recovers the complete literal mask. The performance token is stoppable with no request during timing. Allocator/OS/vector-growth/compaction costs have no cancellation deadline.

## Private qualification

Attempt history and retained outcomes:

| Attempt | Result retained | Evidence | Completion UTC |
| --- | --- | --- | --- |
| r1 | retained performance regression | runtime exit 0 | `2026-10-02T16:58:06.7304978+00:00` |
| r2 | retained performance regression | runtime exit 0 | `2026-10-02T17:03:14.2199592+00:00` |
| r3 | retained performance regression | runtime exit 0 | `2026-10-02T17:07:48.4644008+00:00` |
| r4 | retained performance regression | runtime exit 0 | `2026-10-02T17:11:43.2938292+00:00` |
| r5 | retained C2397 narrowing compile failure; no runtime or executable qualification | build exit 1 | `2026-10-02T17:13:01.4661501+00:00` |
| r6 | qualified private Release build, complete DocumentTests, and two performance runs | image `982C6D1616D65DCDA443FA1D536181D1227DB549FA5317B8E75A2047FAF7C5A8` | `2026-10-02T17:16:56.0416986+00:00` |

The r6 private Release image SHA-256 is `982C6D1616D65DCDA443FA1D536181D1227DB549FA5317B8E75A2047FAF7C5A8` and matches the archived build and run receipt. Its complete `DocumentTests` run contains exactly 54 `wand-cancel` and 17 `wand-chunk` named PASS checks, with no duplicate names or failure lines. The exact 71-name set was extracted from the archived r6 log and matched against each native x64 profile below. Earlier private performance regressions and the r5 C2397 narrowing compile failure are retained. R5 has no runtime qualification or executable hash.

The r1 tracking supplement records 124 compiler objects/PDB/tracking entries before optimization (manifest SHA-256 `7DEBD99EE67DBD35017B2E47EBCF30100A3AD1777672324A1547142DD6F39140`). Every tracking file's hash was verified. The baseline source snapshot is the prior `Common/Selection.cpp` for performance comparison only; it is not a new golden oracle. Existing legacy numeric expectations remain unchanged, and the new output checks use independent literal masks and rectangles.

## Performance

The r6 probe processed the 45 MP image (44,761,088 pixels; 8192 x 5464). The fragmented case used a 2048-square mask with 512 selected one-pixel-wide vertical stripes, one every four columns, and 512 literal clockwise rectangle expectations. Each run used one warmup pair and seven measured pairs with alternating baseline/candidate order, eight iterations total per workload, and a single logical processor (affinity mask 1). Validation, output hashing, and destruction were outside timing. The two machine receipts and both performance logs are archived. Shared-desktop outliers remain in the sample maxima and were not discarded.

Rows show baseline median / candidate median / candidate-to-baseline ratio for each of the two r6 runs:

| Workload | Run 1 (ms / ms / ratio) | Run 2 (ms / ms / ratio) |
| --- | ---: | ---: |
| whole-match | 39.178900 / 40.826800 / 1.042061 | 40.462700 / 41.623700 / 1.028693 |
| solid-flood | 126.745300 / 119.918400 / 0.946137 | 121.412200 / 127.152600 / 1.047280 |
| solid-trace | 77.537700 / 78.038100 / 1.006454 | 76.404900 / 77.636800 / 1.016123 |
| fragmented-stripe-trace | 7.239400 / 7.015900 / 0.969127 | 7.013100 / 7.101200 / 1.012562 |

These are paired scalar-kernel costs, not UI or whole-application speed claims.

## Native qualification

Native run stem: `p1-wand-cancellation-native-r1`. Matrix completion UTC: `2026-10-02T17:49:56.7560093Z`. Profile receipt timestamps below are preserved exactly and were parsed as UTC instants; the process start is never inferred from file timestamps.

| Profile | Exit | Completed UTC (receipt) |
| --- | ---: | --- |
| ARM64 / ASanDebug | 0 | `2026-10-02T17:20:48.1621287Z` |
| ARM64 / Release | 0 | `2026-10-02T17:23:27.4701149Z` |
| ARM64 / Debug | 0 | `2026-10-02T17:25:15.7959580Z` |
| x64 / ASanDebug | 0 | `2026-10-02T17:37:02.3823240Z` |
| x64 / Release | 0 | `2026-10-02T17:42:59.7683368Z` |
| x64 / Debug | 0 | `2026-10-02T17:49:56.7294719Z` |

The three ARM64 profiles are cross-builds only; no ARM64 runtime claim is made. Each x64 profile passed the full `test.ps1` gate, the full DocumentTests completion marker, the exact 54 + 17 wand summaries, and the same 71 unique named assertions. The native archive contains 591 frozen source entries and 328 evidence entries. Its receipt completed at `2026-10-02T17:53:32.6875331+00:00`. Every source/evidence archive hash, manifest count and manifest hash was checked; all frozen inputs still match the current working tree.

## Archive provenance

- Native archive: `C:\Users\eric\AppData\Local\RedPrism\QualificationArchives\p1-wand-cancellation-native-r1-qualified`; source manifest SHA-256 `715CACEA186884E9C472BB3AE7AF1509F5B982AEAF4F362F3E28B58218040074`; evidence manifest SHA-256 `87048A20BDBF403DE458DA79433DBF03FD43C3162FCAAB17652A433DAF19D824`.
- Qualified private r6 archive: `C:\Users\eric\AppData\Local\RedPrism\QualificationArchives\p1-wand-private-r6-Release-qualified`; source manifest SHA-256 `97204C817AA19300615E66FCDB51E4F011A072C97269B973F5C6BD36DA5AFD06`; evidence manifest SHA-256 `05DD20C5FC727FE327BEF8F22E59F7B362F24075F0A73E54FAB2C70F0A000A1E`.
- Private attempts r1-r5 were also verified against their own source/evidence manifests and freezes. The r1 tracking supplement receipt and manifest hash are retained above.
