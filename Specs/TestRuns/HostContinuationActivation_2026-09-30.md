# Production color-commit continuations — 2026-09-30

## Implemented boundary

Production panel OK and dependent Save/Save As, PNG/JPEG, New/New Canvas, project/image open/import, size Apply,
Close Project/window/session end and tool/layer actions use owned typed requests. Accepted input is Pending;
successful color apply/drop acknowledgement precedes writes and dependent actions. A failed/cancelled/stale
prerequisite preserves the panel and suppresses its dependent action. Prompt cancellation preserves all drafts.
Output selection precedes pixel settlement/readback; the written revision is captured after settlement.

The stable UI-action source advances bounded fixed stages outside callbacks. Ordered project/image batches retain
their original paths, policy and drop point; only the active batch rebinds after its intentional replacement.
Size Apply owns its full draft and card invocation. Session end validates its generation in both the posted message
and selected request. Critical queries are allowed while an ordinary request waits; failed admission removes
only that request's block reason. No production caller waits or uses `SettleJobs`.

Shell/drop/rename input copies now begin at the admitted boundary. Whole drop lengths are queried before buffer
allocation; filling occurs under the reserved ticket while the HDROP lives. Shell strings remain WIL-owned until
bounded preflight/copy. Input storage remains charged through terminal publication and release.

Header callbacks and opacity settlement cannot mutate beneath admitted work. Shape pointer release/ordinary leave
captures the draft and color in the ordered queue; worker Shape rasterization remains P1.5.

## Proof

- `host-path-read.*`: 14 direct-population checks for preflight refusal before reads, partial read/input/result
  failures, Unicode ownership/order, bounds and reservation lifetime.
- `host-flow.*`: 21 selected inputs × four event-gated outcomes (84 cases). Actual donorless fill fails; Gamma 2
  succeeds; active Cancel and reused persisted document IDs/session-end cancellation reject dependent actions.
  Saved Gamma 2 bytes, no premature/failed output, surviving panel/history, distinct layer effects and budget
  retirement are observed. View input remains usable while the worker is gated.
- An additional actual Shape release checks two separate ordered undo entries and captured red pixels after the
  palette changes to green. Session-end message tests check stale generations, critical queries, full-queue
  admission, block-reason cleanup and posted close.
- Existing direct numeric goldens remain unchanged. File/drop/size/key harnesses observe completion through the
  shared event pump after their real producer returns.

Cheaper agents implemented the bounded dialog adapters, reader tests and session-end checks and audited callback
mutation paths. The primary reviewed their changes, owned the staged runner/caller migration and fixed the
confirmed Shape-release bypass. One proposed opacity-import finding was rejected after checking the existing
`SettleInteractions(true)` path.

## Qualification

| Gate | Result |
| --- | --- |
| Skills / format check | Passed |
| x64 Debug complete `test.ps1` | Passed |
| x64 Release complete `test.ps1` | Passed |
| x64 ASan Debug complete `test.ps1` | Passed, including stress and the deliberate sanitizer detection probe |
| ARM64 Debug / Release / ASan Debug | Cross-builds passed; no native runtime claim |
| Real-loop remeasurement | Release hardware `--bench ui` completed all 15 operations, 10 measured trials each |

All existing legacy goldens, WARP checks, ISA controls, crash probes and harness captures passed in the applicable
complete gates. Reader checks ran 14 times per normal HostTests invocation and 28 including ASan stress. Each x64
gate verified all 19 local RAW files and exact license/manifest bytes without downloading or skipping the corpus.

The benchmark used Ryzen 9 9950X3D, 32 logical processors, 64 GiB RAM, RTX 5080 driver 32.0.16.1714, Windows build
26200, Balanced power, scalar kernels and 32 workers. Fixture checksum: `04c311cabfc8b925`.

| 4K operation | Median through Present submission | Max UI dispatch | Max completion apply | Max UI action |
| --- | ---: | ---: | ---: | ---: |
| Levels open | 60.511 ms | 3.645 ms | 4.348 ms | 0 |
| Levels update | 78.856 ms | 2.750 ms | 0.063 ms | 0 |
| Levels commit | 8.661 ms | 0.083 ms | 3.658 ms | 5.898 ms |
| Invert | 166.402 ms | 187.123 ms | 0 | 0 |
| Fill | 212.440 ms | 243.115 ms | 0 | 0 |
| Clear | 251.392 ms | 270.051 ms | 0 | 0 |

The action/apply residuals and synchronous raster rows prevent a G1 claim. These are operation measurements,
not isolated kernels or display scanout; GPU work preceding a sample is not fenced in this benchmark yet.

Ignored evidence uses `.build/p1-activation-*.log`, `.build/p1-debug-self-test.*.log` and
`.build/p1-activation-source-hashes.json`. All 269 recorded validation inputs were unchanged through qualification.
The benchmark log is `.build/p1-activation-ui-release.log`, ending `UI BENCH REPORT status=ok`.

## Remaining performance work

This validates ordering and ownership, not all P1 or G1. File classification/decode/encode, GPU readback, several
tool/size kernels, history accounting and first uploads still execute synchronously. Their migrations remain
P1.2–P1.9. RAW decoding, SIMD speed, unavailable hardware and Photoshop comparisons retain their own phase gates.
