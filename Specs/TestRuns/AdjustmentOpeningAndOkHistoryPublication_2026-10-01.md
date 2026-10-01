# Adjustment opening and metadata OK history publication — 2026-10-01

Status: qualified P1.9 H1.6a/H1.6c functional slice. All six clean configurations pass against 408 unchanged
native/build inputs. Production Cancel routing, remaining H1 callers, shared document capture, incremental accounting,
first uploads and measured G1 are open. ARM64 runtime, RAW and Photoshop comparisons are not qualified here.

## Boundary

Opening submits a cheap descriptor before admitted source/history capture. Worker preparation retains original
records and changes only its private sampling visibility snapshot. GPU tiles and mapped-row acknowledgement stay
on the owner. Final copied pixels detach from GPU lifetime before worker panel/original/Begin construction. Apply
validates document lifetime/generation/sequence, pending id, active/mask/crop and exact history epoch before swaps.

Metadata OK owns the whole document, original/settings and history only after complete reservation. The worker
prepares final records and End, reusing the opening snapshot. Apply swaps old document/panel/original/display/history
ownership into its preallocated aggregate. All large CPU owners retire off the owner; graphics cleanup stays there.
Owner document copy and conservative admission traversal remain measured H2/H3 work.

True stored-record no-op keeps revision and redo. Existing promotion of omitted HSV/Exposure settings to explicit
defaults still records its real metadata change. Every acknowledged End expires prior whole-document tokens by
advancing the opaque mutation stamp. Pixel bytes, global filter templates, nested outer name and source visibility
remain unchanged. Production cancellation routing is a separate caller gate.

## Qualified proof

- 515 independent closure assertions cover all six kinds, preview on/off, no-op/redo, literal parameters/pixels,
  single-use/moved/foreign and stale authority, and reached allocation/cancellation at five history stages.
- 210 independent opening assertions cover literal source/settings/kind/placement/seed/deferred state, Begin,
  generation/revision/redo retention, prepared Cancel and reached three-stage allocation/cancellation refusal.
- 58 reviewed nested assertions cover named outer Begin/End, preview on/off OK and Cancel, exact combined entry,
  Undo/Redo, global template, mutation generation and refusal before preparation. The closure suite independently
  proves that an unchanged End expires a token that was current immediately before acknowledgement.
- Closure also refuses altered same-sequence parameters and retains a later saved revision; opening refuses an
  absent document/id and preserves a later saved revision independently of prepared history state.
- The actual WARP host adds 74 closure and 72 opening cases. They require admission/reservation, exact terminal
  history lineage, hidden Present count unchanged, old-owner worker retirement, GPU lease/utility release,
  delayed cancellation drop, reached history/capture failure, epoch staleness and device recovery after detached
  copied pixels. Younger rename cannot execute before opening and subsequent OK acknowledgement.

## Preserved preflight failures

- r2: the default-HSV no-op fixture assumed absent and explicit settings were equal. Its exact executable/PDB/DLLs,
  freeze and log are in `.build/p1-adjustment-first-Debug-hsv-noop-failure/`; executable SHA-256
  `19028BDBD6FA7827B7A2D01C5D24B67C333AB84D339FA13F79FA48A0A8D70C12`.
- r3: a missing direct ChromeHost header stopped compilation; the diagnostic log remains.
- r4: the failure fixture used `CanRedo()` during a retained open transaction. Redo handles must be preserved,
  while Undo/Redo commands remain disabled at nonzero depth. Exact failed binary/PDB/dependencies/freeze/log are
  in `.build/p1-adjustment-second-Debug-open-redo-failure/`; executable SHA-256
  `F9579867352CF08E89E80B76E2232679D7A90FE2C21511AB2D958B820F1DB742`.
- r5: the three-layer GPU opening fixture used reversed storage order. RedPrism stores layers bottom-first.
  The corrected fixture still requires identical literal grayscale bytes below the adjustment and unchanged live
  visibility. Exact failed inputs and image are in `.build/p1-adjustment-third-Debug-layer-order-failure/`;
  executable SHA-256 `8DF608957BAEFA031F4493B31CF7C08F1FF7EE86755E6D5A229D4071BEE4E932`.

Original logs are retained; old failures are not successful qualification.

## Frozen configuration matrix

| Profile | Result |
| --- | --- |
| x64 Debug | Full test suite, crash/worker probes, ISA controls, WARP and harness screenshots pass |
| x64 Release | Same full suite passes |
| x64 ASan Debug | Same full suite plus real sanitizer detection probe and instrumented stress pass |
| ARM64 Debug | Clean cross-build passes |
| ARM64 Release | Clean cross-build passes |
| ARM64 ASan Debug | Clean instrumented cross-build passes; native runtime unavailable |

`validate-skills.ps1` validates 15 skills; `format.ps1 -Check` checks 358 files. The qualification driver runs
`test.ps1 -Rebuild` for each x64 profile, then `build.ps1 -Rebuild` for each ARM64 profile, checking all source
hashes before every run and after completion. Each x64 host stderr has exactly 74 closure and 72 opening rows.
The three document groups contain 515 + 210 + 58 = 783 assertions.

Logs and source freeze: `.build/p1-adjustment-r1-*`; verified qualified Release images/PDBs/DLLs and source,
binary and DxUi provenance: `.build/p1-adjustment-r1-qualified/`. Parent commit is `47d67f9`.

- Source-freeze SHA-256: `F2F16C8550ACF6164A02CAC83193CE7BC64077BFBB00E4A52E79C0FA1BE7FB15`.
- Release version: `0.1.130.0`; executable SHA-256:
  `778FD263129D4EEBE9F26D1AAF5F0F143B2072F0E4B3B2F9C8F7BB063C08EE63`.
- DxUi pin: `c403052bad26c97ba23995e136672c32c85ccbfc`; runtime ISA allowlist is unchanged,
  SHA-256 `6F55A40AB0CE23E98BB10839EC68765531129EF375B00DD29B298C3DC97EE560`.

These are functional and ownership results. No G1 latency, native ARM64 runtime, RAW decode or Photoshop
performance result is inferred from compilation or hidden WARP execution.
