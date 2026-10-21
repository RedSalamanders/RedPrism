# P1 private Raster residency journal and history delta adapter — 2026-10-12

Status: **PASS for the private actual-Raster serial/journal and history-delta adapter controls only. This is not native integration or production qualification.**

## Scope

The primary-authored private source adds an immutable nonzero Raster storage serial, a bounded global residency change journal and history charge-delta preparation. Agent-reviewed Raster fixture controls were independently registered and checked against a frozen expected roster. The adapter observes the existing exclusive residency lease. Journal events contain value data (storage serial, epoch and resident self-byte charge), not Raster owners.

The journal has 4,096 fixed slots. Serials and epochs do not wrap: exhaustion refuses further allocation/publication. Cache publication and its numeric event complete under the same lease before the new witness is exposed. The append is validated before the nonthrowing pixel-buffer move and release publication. History maintenance consumes a complete suffix from its captured epoch and uses one serial lookup per event. A stale owner witness returns Refresh before publication. Cancellation preserves the already published history. An overwritten 4,097-event window falls back to a worker rescan of all 33 records.

## Fresh Release r3 result

Evidence packet: `.build/P1Drafts/HistoryJournalAdapter-r3-Main/qualified.json`
SHA-256: `6CA2C169F1CE02D26AF108447FA6BA13BFF0D006005F50613B595D94DFB31DC8`

The qualified manifest reports PASS. It preserves all 18,733 predecessor records in their original order and verifies 52 appended roster records: 50 PASS control records and two summaries. The separate raw Raster integration summary reports `assertions=18 failures=0`; it was checked separately from the roster count.

| Stage | Process | UTC interval | Exit | Evidence |
| --- | ---: | --- | ---: | --- |
| x64 Release `DocumentTests.vcxproj` build | 29100 | 05:21:44.567848–05:24:15.521798 | 0 | stdout SHA-256 `210F9BACE06309C51AD4CFF865A96CA2FA06EA1A89DE4D97DFE4983505A5F26A`; stderr empty; zero compiler warning/error diagnostic lines |
| x64 Release `DocumentTests.exe` | 27740 | 05:24:16.920048–05:24:25.758268 | 0 | stdout SHA-256 `F10F1F38061F16A5C3BD5487DA8E53DDB314340BD49AE7BD419EE30C68B6ACFE`; stderr empty |

The manifest also records expected roster SHA-256 `6EDD6B07A8A8CC79636A49FE51667F63C5135950DB901A12ED68737288D3CD8D` and reader SHA-256 `D09FB09D4B6C4ACB464F965528F063098C3BC1D795E6AE8EBEEB3DC2AB9C5553`.

### Added controls

- 20 `raster-residency-journal` controls cover literal ring ordering, exact-capacity suffixes, overwritten and invalid witnesses, cancellation, and permanent serial/epoch exhaustion without wrap. Four worker threads produce an exact continuous nonzero serial range.
- 18 Raster integration assertions cover copied-handle identity, distinct-storage identity, exclusive lease behavior, successful complete materialization publication, exact resident bytes, cancellation without partial output, and value-only journal lifetime after Raster destruction.
- 12 `history-residency-delta` controls cover 33-record bootstrap and unchanged-epoch skip, exact sparse residency charge, one-event lookup, stale witness refusal, unrelated/freed storage, cancellation preservation, and overwrite fallback. The 4,097-event case falls back to a 33-record worker rescan while preserving reference bytes, revision and history.

## Attempt history

- r1 preparation refused before starting any build or runtime because the runner expected five temporary journal arrays while the independently reviewed fixture contained four. This was a preparation inventory assertion, not a failed product control.
- r2's build failed with two fixture `int`-to-`double` narrowing errors and a separate C1090 PDB API error 23 (`0x00000006`, invalid handle). No runtime started.
- r3 uses explicit dimension casts for those fixture values and fresh build/output paths. The assertions, flags and frozen predecessor roster were retained; the fresh build and complete runtime pass as recorded above.

## Limits and remaining gates

This qualifies a private candidate. The private source is not integrated into Native RedPrism. No timing or production qualification is claimed. Native admission—including the 96 KiB worker scratch reservation—full original-ticket ownership and bounded retirement, fixed journal global resource accounting in the native resource contract, changed-owner topology preparation, actual host consumers, ordinary activation, G1, H3 closure and P1 completion remain open. The native implementation/completion item remains unchecked in the WIP plan.

The native x64 Debug/Release/real-ASan, Release analysis and ARM64 cross-build matrix was not run for this private packet. No normative domain contract or parent `UiNeverBlocks` plan was changed.

## Packet files and hashes

The filenames below are relative to `.build/P1Drafts/HistoryJournalAdapter-r3-Main/`.

| File | SHA-256 |
| --- | --- |
| `qualified.json` | `6CA2C169F1CE02D26AF108447FA6BA13BFF0D006005F50613B595D94DFB31DC8` |
| `before.json` (expected roster) | `6EDD6B07A8A8CC79636A49FE51667F63C5135950DB901A12ED68737288D3CD8D` |
| `build.stdout.log` | `210F9BACE06309C51AD4CFF865A96CA2FA06EA1A89DE4D97DFE4983505A5F26A` |
| `build.stderr.log` | `E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855` |
| `document.stdout.log` | `F10F1F38061F16A5C3BD5487DA8E53DDB314340BD49AE7BD419EE30C68B6ACFE` |
| `document.stderr.log` | `E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855` |
| `fixture-repair.json` | `7EDED56BBE340B3CDAEBBADB414B6E43A0D83833DB21BA0BAF350FECFF74644F` |
| `failed-r2-build-result.json` | `FA630FFB7885AB33B0930078455F9B53A0E24783C5C642B1853830DD30FD581A` |
