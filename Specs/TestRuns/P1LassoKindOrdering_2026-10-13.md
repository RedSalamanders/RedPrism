# P1 lasso cross-stream ordering and kind-toggle proof — 2026-10-13

Status: **private actual x64 Release scoped PASS**. Native integration/profile qualification, complete
caller/admission/lifetime coverage, ordinary activation and measured S5/G1 remain open.

## Repairs and independent expectations

The earlier 96 host facts remain unchanged. Thirty-two new facts exercise Point, Cursor, Backspace and
Marquee against a tool command in both admission orders. Each physical producer reserves a scalar ordinal
from the existing nonwrapping HistoryWork clock. Replay selects an older queued request/canvas lifetime
before a later sample; fixed ring order preserves earlier samples. No extra queue, owner or UI source.

Eight boundary facts place the actual shared clock at uint64 maximum. Live sampling fails before mutation;
late Move/Backspace/polygon press cannot consume a counter or cancel an accepted terminal. The original
selected terminal still publishes a literal area-800 triangle and exactly one entry. The fixture-only
friend adds no production member/API; restoration cannot predate a started operation.

Twenty-four kind-toggle facts cover both toggle commands during opening, after acquired idle sampling,
and after a selected physical Finish. The command cancels the exact paired input/session draft before
changing the kind, preserves selected-Finish order and literal triangle/rectangle geometry, adds no
cancellation history, and permits a new opening using the changed kind.

The first actual runtime exposes a lost wake: draining the final sample returns from RunHostContinuation
without rearming an already accepted command. Diagnostics show original opening Succeeded, unchanged
history, busy draft/input, old kind and one remaining queued request. The repair rearms existing work after
stale/lost/drained replay only when a sample, accepted host request or canvas lifetime remains, and excludes
shutdown. An empty live gesture remains disarmed. The original failing predicate is unchanged.

## Actual run and independent readback

Replacement build PID 64976 exits 0 with zero warnings/errors, 17:47:53–17:48:19 UTC; linked ISA exits 0.
Application PID 32816 exits 0, 17:48:36–17:50:14 UTC, `--self-test --warp`. Main independently verifies
**138 application stages, 160 host assertions, 7,765 stdout PASS records, 7,613 stderr PASS records and
55 summaries**, preserving all predecessor assertions, order and duplicates. All 729 declared source/
dependency inputs and 360 products retain exact bookends; complete archived products include intermediates.

Stdout SHA-256: `ED6535802E1CC9310D61FF91359446CD253EE8A2DFE75FFE98D0A115656298FB`.
Stderr SHA-256: `18446AEAA47082BC5EDF89700F4F38CCD7512D93E8BF82868BE54E04978485A4`.
Independent ordered-report SHA-256: `71658624154D06B83261E63F44FCD781C2D9CAD5D4EB8DEDACBF0EDBCC147C93`.
Archive: `Z:\CodexArtifacts\RedPrism-P1\2026-10-12\P1-LassoKindInput-r4-passed-20261013`.
Manifest SHA-256: `9095BC6E349349D9765449FE9666A109AEDF6CA5CA6B4DEAFEF0414484215C6B`.

## Retained refusals

- r1 actual application exit 5 at the first queued-toggle acknowledgement after all 32 ordering facts pass.
  Complete source/products/logs archive manifest: `6139FB7FEA1195D67C238CF587EB72229C768EF5C0FFB18CF99A03D2ABD5D8EC`.
- r2 diagnostic-only compilation refuses two `%zu` arguments of type int (C4477/C2220); no runtime.
  Manifest: `787689DB7857D585711E730220D15B2A34FA6BCD7955DCCB9A6313D746C77BFE`.
- r3 unchanged runtime predicate fails with the diagnostics above; its stdout is byte-identical to r1.
  Manifest: `6B301558EBC9B06D3B2024585FE97E44EF682AF768D360F6410CA5E4370D5610`.
- Main's first result extractor omits 27 predecessor lowercase `summary:` lines. Its refusal is retained;
  corrected extraction restores that existing event class without changing expected records or rerunning.

This result is distinct from the running native r1012 analysis on the preceding source. That analysis and
this private PASS do not qualify a later combined source, current native profiles, visible latency or the
remaining [lasso delivery checklist](../Plans/WIP/LassoWorkers_2026-10-12.md).
