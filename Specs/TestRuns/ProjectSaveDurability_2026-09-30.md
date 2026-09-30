# Project save durability qualification — 2026-09-30

## Result and scope

The bounded D17 save-staging slice passes the complete repository matrix. ZIP and exploded projects use
unique exclusively created sibling staging, check writes/flush/close before publication and remove only owned
staging. PNG ZIP entries use STORE; manifest entries use DEFLATE. Independently rewritten older DEFLATE
assets continue to load. The `.prism` schema and exploded destination/`.old` rotation are preserved.

Independent tests prove first save and overwrite/rotation round trips, method selection, unrelated same-prefix
file/directory preservation and owned cleanup. A locked archive replacement uses distinguishable old/new
document resolutions and verifies the prior bytes and document remain intact after failure.

One initial Debug array-writer save failed in the earlier run. Failure-only status/path/OS diagnostics were
added; the failure was not reproduced in the repeat or the final three x64 suites. Its cause is unconfirmed.

## Validation

`validate-skills.ps1` (15 skills), `format.ps1 -Check` (230 files), all x64 Debug/Release/ASan Debug suites and
all ARM64 Debug/Release/ASan Debug cross-builds pass. ASan executed the actual detection probe. Existing
legacy numeric expectations, hostile-project tests, WARP host checks, crash probes, UI smoke and offline
19-file RAW corpus provenance checks remain green. Native ARM64 runtime is unavailable.

Logs are `.build/p1-sizing-history-{x64,ARM64}-{Debug,Release,ASanDebug}.log`, plus
`.build/p1-sizing-history-skills.log` and `.build/p1-sizing-history-format-check.log`. The 270-file combined
sizing/save/history source freeze was checked before every configuration and after qualification; SHA256:
`FB3AF47A088889F262F15E717AD5D0FB0F30C1F739F5A4879C832CAC1DC37933`.

## Remaining P1.7 work

Save still runs synchronously until worker migration lands. Parallel/banded encoding, per-path scheduling,
cancellation and external-publication coordination remain open. Flush failure and the second exploded rename
are not fault-injected by this slice; the existing rollback path is retained. These tests do not establish
power-loss durability for directory metadata or close overall P1.7/G1.
