# Pixel-move opening reuse — 2026-10-01

Status: qualified correctness and ownership slice of P1.5 A5.2a. Complete P1/G1 performance, outline display,
first uploads and other A5 callers remain open. This run establishes no RAW or Photoshop comparison.

## Implemented boundary

One opaque immutable opening retains source, coverage, lifted pixels and selection provenance. Reuse validates
the exact source/grid/placement, target, selection storage and antialiasing. Each worker builds a fresh private
stroke: mutable tiles/indexes, callbacks and cancellation are never shared. The host caches only acknowledged
ready work for the current document serial, gesture opening and source generation. It retains that cache through
admitted commit capture and clears it on cancellation, replacement, reopening, source invalidation or completion.

The original full preparation peak remains reserved on both cache misses and reuse. Cached retirement bounds
are conservative constant-time declarations. The complete source/coverage/lift/selection/node owner releases
off the owner thread; its bound exists before owning capture and remains above the release threshold.

## Proof

- 33 independent document assertions cover literal whole/partial-alpha pixels, placed/scaled/sparse inputs,
  unmaterialized sources, exact grid/source/selection/AA/target authority, fresh/moved-from stroke refusal,
  cancellation, cached capacity and last-owner deferred retirement.
- Actual host scenarios prove one opening preparation for two coalesced previews and the latest commit,
  literal final pixels, the moved outline and one `Move Pixels` entry. Undo restores source raster identity.
  Reopening requires fresh preparation; cancelling retires the cache. Existing stale/failure/ordered-nudge,
  whole admission, cancellation acknowledgement and large-graph retirement controls pass unchanged.
- Primary review corrected the agent test's nonexistent history accessor before compilation. Native preflight
  and the distinct final frozen matrix both passed; no failed native matrix is concealed or excluded here.

## Configuration and identity

| Gate | Result |
| --- | --- |
| Skills / formatting | Passed: 15 skills / 324 native files |
| x64 Debug / Release / ASan Debug | Full clean-build suites passed, including sanitizer probe and stress |
| ARM64 Debug / Release / ASan Debug | Clean cross-builds passed; native runtime unavailable |
| Frozen native/project/script inputs | Same 374 inputs verified across all six configurations |

Source-freeze SHA-256: `09378F9545D8280D836B252970A6EDABF7C4678A4B2FB1554E38848C490C0396`.
Release 0.1.125.0 executable SHA-256: `13C9F678B6A861A2F3CFB5A30C16C500EE89BA0257ACA41374C3F04463CCD5EF`.

Final logs: `.build/p1-pixelmove-opening-r1-driver.log`,
`.build/p1-pixelmove-opening-r1-{x64,ARM64}-{Debug,Release,ASanDebug}.log`, skills/format/source-freeze files.
The preserved Release snapshot is `.build/p1-pixelmove-opening-r1-qualified/`, with exact binary hashes,
version and qualification provenance. The snapshot is evidence for this source only; later binaries need
their own qualification.

## Remaining work

Copy/Cut, floating transactions, distortion, shapes/masks and complete caller migration remain open. P1.6 owns
displayed selection geometry; P1.9 owns other prepared-history callers, shared metadata/indexes, incremental
accounting and thumbnail/upload/chrome costs. No drag latency, reduced allocation peak or 2 ms apply/G1 target
is claimed from functional cache tests.
