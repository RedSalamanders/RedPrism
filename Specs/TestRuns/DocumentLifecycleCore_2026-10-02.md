# Prepared New/Close publication primitive

Date: 2026-10-02. Branch: `main`. Scope: P1.9 H1 lifecycle preparation; production callers remain open.

## Change and independent expectations

`PrepareSavedReplacement` preserves New's one `New Canvas` entry from no document, with a saved final revision.
`PrepareDocumentLifecyclePublication` prepares optional document/targets and complete replacement/history
retirement holders privately. Applying an absent output is a real Close publication. Closing an already
absent document preserves its existing redo lineage. Application validates sealed session authority and
exact history before nonthrowing swaps. Direct New/Close application callers are unchanged in this slice.

Each x64 DocumentTests run passes **65 lifecycle controls**, with the existing numeric goldens unchanged:

- Literal 5×7 blank `Layer 1`, targets, saved New entry, Undo to no document and Redo to the saved canvas.
- Close's empty document/targets/history/saved state; palette and private clipboard preserved by both operations.
- Optional output, invalid dimensions/active ids/authority, moved-from/single-use refusal, session/generation,
  replacement and history staleness, including a deferred transform.
- Reached allocation failures at all seven New history checkpoints and Close's reset checkpoint.
- Cancellation at all seven reached New checkpoints and both reached Close checkpoints: exact old document,
  history/saved/redo/targets/private state and lifetime authority remain unchanged.

The test additions from simpler agents were reviewed and refined by the primary agent. Original sidecars
are retained under the local qualification archive volume. The final private DocumentTests proof contains
542 inputs; native qualification below uses the repository projects, source registration and build gates.

## Frozen native qualification

Runner: `.build/qualify-p1-lifecycle-core-native-r1.ps1`; **561 frozen inputs**. Skills and formatting pass.
Every profile is a clean rebuild. Runtime claims apply to x64; ARM64 results are cross-builds.

| Profile | Gate | Result | Completed UTC |
| --- | --- | --- | --- |
| ARM64 ASan Debug | Instrumented cross-build | 0 | 04:55:22 |
| ARM64 Release | Cross-build | 0 | 04:57:54 |
| ARM64 Debug | Cross-build | 0 | 04:59:42 |
| x64 ASan Debug | Full suite, sanitizer detection/crash probes and WARP harness | 0 | 05:07:32 |
| x64 Release | Full suite and WARP harness | 0 | 05:11:26 |
| x64 Debug | Full suite and WARP harness | 0 | 05:16:29 |

All five linked-image ISA scans and their controls pass in every x64 profile. The merged DxUi API revision 3
pin remains `271bd54be24eadf3f03ba5221d1be7be069860f2`. The final freeze assertion and runner receipt pass at
`2026-10-02T05:16:29.3670689Z`.

## Preserved proof and limits

`.build/p1-lifecycle-core-native-r1-qualified/` links to the local qualification archive volume. It contains
**561 source entries and 297 evidence entries**, with byte-copy/SHA-256 verification, exact executables,
symbols, DLLs, ISA proof/controls, logs, frozen inputs and per-profile/final receipts.

- Source manifest SHA-256: `25A460569E107B8B29C117C6D11C594BEEE795594E589C7C7FB61ACD7A726435`.
- Evidence manifest SHA-256: `6E46ECF50236F807CB22CA46EB6C912BAF6E50F11E1AD40B24436C13A0346BF4`.

This qualifies the primitive and its compatibility/refusal behavior. It does not qualify production lifecycle
jobs, their capture/retirement under shutdown, constant-time document/admission/history accounting, owner apply
or first-upload timing, P1 G1, RAW decode/development or comparison with Photoshop. The broader private transform
policy failures and its eventual private full-run pass are tracked separately in the raster worker child plan.
