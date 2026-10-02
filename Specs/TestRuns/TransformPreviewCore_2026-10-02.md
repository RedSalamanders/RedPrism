# Transform preview core qualification

Status: all three native x64 test suites and all three ARM64 cross-builds pass against 537 frozen inputs.
This record covers `Common/TransformPreviewWork.*` and `Tests/DocumentTests/TransformPreviewWorkTests.*` only.
Production host activation, transform settlement and G1/performance remain open.

## Qualification checklist

- [x] Preserve the private core proof and the original native failure before rerunning.
- [x] Pass skills and formatting validation.
- [x] Pass complete x64 Debug, Release and ASan Debug suites, including unchanged numeric goldens and ISA gates.
- [x] Cross-build ARM64 Debug, Release and ASan Debug; no native ARM64 runtime claim.
- [x] Verify frozen sources and archive all six profiles before further compiled-source edits.
- [ ] Qualify production preview coalescing, caller settlement and measured G1 in A6.3/A6.4.

## Core behavior and private run

The private `DocumentTests` run reports **44 preview assertions, 0 failures**, followed by `All document tests passed.` The exact aggregate is printed in `transform-preview-core-run-r1.log`; it is not inferred from the number of `Expect` call sites. Each native x64 profile also reports `transform-preview: 44 assertions, 0 failures` alongside the prior 467 core controls.

The 44 assertions cover:

- Distorted color and mask output bounded to 1024 pixels on the longest side, with transparent bounds retained and source rasters left immutable. Affine previews share the original color and mask pixels.
- Ordinary and group transforms, including carried group geometry, sparse source assembly without materializing the source, and independent literal references for selected mask distortion output.
- Linked unplaced masks, linked placed masks, uniform linked masks, standalone mask transforms, and unlinked masks. Checks include placement following, implicit placement preservation, binary dark edge tone, nearest-grid mask bytes, and source immutability.
- Invalid corner fallback to the affine result; malformed edits, missing or duplicate group members, missing targets, and unsupported raster formats are refused.
- Snapshot, layer, publication, pre-cancel, and executor-shutdown cancellation. Cancellation and allocation failure do not publish partial documents or alter the owning session.
- Display-only publication, single-use consumption, and refusal of stale drafts, closed/reopened transforms, changed documents, changed targets, changed history, same-ID replacement, and foreign-session publications. The opening document, history, generation, clipboard, and mask target remain unchanged.

The earlier private run snapshot is `.build/p1-distort-draft/transform-preview-core-qualified-r1/`: its `source-freeze.json` has 519 entries, `evidence-manifest.json` has 66 entries, and `Evidence/transform-preview-core-run-r1-result.json` records exit 0, executable SHA-256 `D441C30F40DC34C6E1B50E91C19F91499D09744AA27D7FB21DB5E05D100858D5`, and completion at `2026-10-01T23:57:54Z`. The run log contains the 44-assertion summary and full `DocumentTests` success marker.

The existing 467 transform/floating/history core controls and legacy numeric goldens remain unchanged; no legacy golden was rebaselined.

## Preserved native r1 failure and rerun

The exact native r1 source/evidence archive is `.build/p1-transform-preview-core-r1-failure/` (537 source entries, 289 evidence entries). The matrix receipt `.build/p1-transform-preview-core-r1-process.json` records exit 1 because x64 Debug `IoTests.exe` returned 1. The Debug log reports exactly two failures:

- `clipboardavailability.both-valid-dibs-inspect-successfully`
- `clipboardavailability.valid-v5-is-preferred-over-different-cf-dib`

The exact failed Debug `IoTests.exe` was rerun without rebuilding. `.build/p1-transform-preview-core-r1-io-recheck.json` records exit 0 and executable SHA-256 `157CB892A2318F1C69902857F2967BD6BBE699AB9206DB280F08683C991272C7`; its log shows both assertions passing and `All I/O tests passed.` The root cause of the original failure is not established by these results.

In r1, x64 Release and x64 ASan Debug passed, with exit-0 profile receipts. x64 Debug remains a failed matrix profile despite the successful isolated IoTests rerun; that rerun does not replace a complete passing Debug matrix receipt.

## Native matrix checklist

| Matrix profile | Current record |
| --- | --- |
| r1 x64 Debug | Failed: two IoTests clipboard-availability assertions listed above |
| r1 x64 Release | Passed |
| r1 x64 ASan Debug | Passed |
| r2 x64 Debug | Passed; completed 2026-10-02 00:37:25 UTC |
| r2 x64 Release | Passed; completed 2026-10-02 00:33:26 UTC |
| r2 x64 ASan Debug | Passed; completed 2026-10-02 00:31:35 UTC |
| r2 ARM64 Debug | Passed; cross-build only, completed 2026-10-02 00:24:08 UTC |
| r2 ARM64 Release | Passed; cross-build only, completed 2026-10-02 00:22:59 UTC |
| r2 ARM64 ASan Debug | Passed; cross-build only, completed 2026-10-02 00:21:41 UTC |

The r2 process records exit 0 at `2026-10-02T00:37:25.6926480Z`, following all six exit-0 profile receipts
and the final 537-file freeze verification. `.build/p1-transform-preview-core-r2-qualified/` retains 537 source
entries and 296 evidence entries, including exact binaries, PDBs, logs, receipts, instruction scans and runners.
This matrix used incremental builds; it does not claim a clean rebuild or native ARM64 execution.

Archive SHA-256 values:

- Source manifest: `861D70084BFB14EBA37D738C9CA81FC13300B4935184750BB5ADA40C0E04F217`.
- Evidence manifest: `7F3FDB5C6D6D0386554B7FB9B5C83BF33649CB63D2276A69018E798C5FCB4E10`.
- Frozen-input list: `031620780DA719A8F64F420FA9055B6C1B6B2A7254FD62808542E701AA864ECC`.

## Scope boundary

This record qualifies detached preview preparation and owner-side publication in the native core. It makes no
claim that a production host caller activates, routes, settles or redraws transform previews, and it contains
no G1 or performance qualification. The two original clipboard failures remain documented; the successful
fresh matrix and isolated rerun do not establish their cause.
