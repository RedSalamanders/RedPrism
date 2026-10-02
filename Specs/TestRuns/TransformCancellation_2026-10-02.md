# Transform cancellation qualification — 2026-10-02

Status: private core qualification and all six native-matrix profile receipts pass. x64 Debug, Release, and ASan Debug ran the full native test suites; ARM64 Debug, Release, and ASan Debug are cross-build results only. The matrix was incremental, not a clean build. Production host caller migration and G1 remain open. This is a test-run evidence record, not a completion claim for the transform extension.

## Private full-suite qualification

The complete private `DocumentTests` run passed against the 514-source freeze. The exact archive is `.build/p1-distort-draft/transform-cancellation-core-qualified-r2/` (514 source entries, 59 evidence entries). Its `Evidence/transform-cancellation-core-run-r2-result.json` reports exit code 0, executable SHA-256 `FFC9CE8711868640DB800D12F74BEFAC054A0ACAEBC6053113D8D0D3F7319A81`, and completion at `2026-10-01T23:37:02.4003068Z`. The paired process receipt also reports exit 0.

The reported 467 core controls comprise 409 existing transform/floating/history controls, 48 cancellation controls, five opening controls, and five merge controls. The full run also passes the existing `DocumentTests` cases and legacy goldens. Relevant log summaries include 48 cancellation checks with zero failures, 53 total opening checks (48 existing plus five added), 136 total merge checks (131 existing plus five added), and 43 following-history checks. No legacy golden was changed.

Ordinary cancellation drops only the private draft and preserves the document, generation, history, and clipboard. Duplicate cancellation removes the duplicate and closes only its child transaction scope. Nested floating opening, merge, and cancellation retain the parent transaction name and depth.

## Preserved failed r1 and fixture correction

The failed run remains intact at `.build/p1-distort-draft/transform-cancellation-core-failure-r1/` (514 sources, 52 evidence entries). Its run receipt reports exit code 1 and executable SHA-256 `179D045CA7B4913B380208979DF92C6B80051A51868C42EB3C5371E32AC7034B`. It contains one failure: `transform-cancellation.floating-refusal-opening-input-created`.

The failure was in fixture setup. `MakeFixture` left a mask selected, and `SelectLayer` with the same layer ID retained that mask target. The primary corrected the fixture to call `SelectLayerTarget(source, false)`. Production code and the assertion were unchanged; the corrected r2 run passes.

The folder `.build/p1-distort-draft/transform-cancellation-agent-original/` is not an exact agent-original snapshot. Its `snapshot-disclosure.json` states `ExactOriginal: false` and identifies it as a post-review snapshot containing primary review corrections. Do not cite it as an untouched original.

## Native matrix receipts

The incremental matrix completed with process exit 0 at `2026-10-01T23:55:02.2392686Z`, from `.build/p1-transform-cancellation-r1-process.json`. Each profile has its own exit-0 receipt:

| Profile | Result | Completed UTC | Evidence |
| --- | --- | --- | --- |
| x64 Debug | Native full test suite passed | `2026-10-01T23:51:24.6554412Z` | `.build/p1-transform-cancellation-r1-x64-Debug.log.result.json` |
| x64 Release | Native full test suite passed | `2026-10-01T23:47:46.2793605Z` | `.build/p1-transform-cancellation-r1-x64-Release.log.result.json` |
| x64 ASan Debug | Native full test suite passed | `2026-10-01T23:45:16.3740653Z` | `.build/p1-transform-cancellation-r1-x64-ASanDebug.log.result.json` |
| ARM64 Debug | Cross-build passed; no native runtime claim | `2026-10-01T23:55:02.1725984Z` | `.build/p1-transform-cancellation-r1-ARM64-Debug.log.result.json` |
| ARM64 Release | Cross-build passed; no native runtime claim | `2026-10-01T23:53:35.3696839Z` | `.build/p1-transform-cancellation-r1-ARM64-Release.log.result.json` |
| ARM64 ASan Debug | Cross-build passed; no native runtime claim | `2026-10-01T23:52:19.0278526Z` | `.build/p1-transform-cancellation-r1-ARM64-ASanDebug.log.result.json` |

The three x64 native Doc logs each contain `All document tests passed.` and the same zero-failure group summaries: transform commit 64, transform cancellation 48, floating opening 53, floating merge 136, and prepared-history following 43. The 467-control scope remains the inventory breakdown of 409 existing transform/floating/history controls, 48 cancellation controls, five added opening controls, and five added merge controls. The log prints group summaries rather than one aggregate `467` counter; the opening and merge summaries include their existing controls (48 + 5, and 131 + 5 respectively). These markers provide native x64 evidence for the exercised groups, while the complete Doc-suite success marker covers the full suite.

The qualified archive `.build/p1-transform-cancellation-r1-qualified/` is present and complete against its manifests: 533 source entries and 296 evidence entries, with every manifest path present. Manifest SHA-256 values are `FE01A1B7371BA7838A1764B5981461EF8B021D2B50A0B605AE97A56EF0EE24C0` (`source-manifest.json`) and `E5E19B74D49D60CC0F9F40C945AC5A670C4B288B153456CD476A52331A29CBAC` (`evidence-manifest.json`).

This was an incremental native matrix, not clean-build qualification. The ARM64 receipts establish cross-build qualification only; they are not native ARM64 runtime evidence. The native run artifacts are `.build/p1-transform-cancellation-r1-freeze.json`, `.build/p1-transform-cancellation-r1-process.json`, `.build/p1-transform-cancellation-r1-process.log`, `.build/p1-transform-cancellation-r1-process.err.log`, `.build/p1-transform-cancellation-r1-format.log`, `.build/p1-transform-cancellation-r1-skills.log`, and the per-profile logs and result receipts under `.build/`.

## Remaining scope

Production host caller integration/qualification and G1 remain open. This private core pass does not establish production transform/opening/preview/settlement routing, timing, redraw, or live producer settlement.
