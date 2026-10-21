# P1 combined capture/preflight canonical matrix

## Completed step

- [x] Integrate worker target capture for RasterEdit, Gradient and Pixel Move; keep full mask/name semantics.
- [x] Replace allocation-producing raster preflight with the existing scalar grid plan; preserve pixel math.
- [x] Validate native clipboard handoff and session-end publication with deterministic OS controls.
- [x] Correct only the composition fixture's observation of release bookkeeping; preserve exact charge/pixels.
- [x] Qualify full x64 Debug, Release and real ASan; qualify all three ARM64 cross-build profiles.
- [ ] Resolve historical panel/Wand failure; complete ordinary activation and the remaining P1 delivery gates.

## Source and evidence identity

Base commit: `99d5c8eeb7a79cf88126484201d9c426fed3ad45` on `codex/p1-opacity-host-proof`.
All six current attempts bind the same 1,831-input freeze:
`90DF76D528C867A0A09D698DCDFDCBD5AE8FC00BED1C83409254CEC891AC8B29`.
Working patch SHA-256: `7965C4F61ED14BA10A7E4C244F7F5E5F00728CC40EA303496FDF6854A0F49101`.
The source bookends include HEAD, status, exact patch and every captured file. This closeout updates Markdown;
the separate post-document-update check verifies that all other frozen inputs are byte-identical.

Evidence root: `Z:\src\RedPrism\.build\P1Evidence\p1-capture-combined-native-r4`.
Each `*-archive-qualified.json`, `*-profile-qualified.json` and x64 `*-runtime-qualified.json` is independently
read back. Every archive row, original compiler record, linked object and five PE/full-PDB pairs verifies.

| Profile / attempt | Actual result | Independently checked runtime scope | Archive files / manifest SHA-256 |
| --- | --- | --- | --- |
| x64 Debug c4d1 | PID64060 exit0, 13:58:07 UTC | All five suites;152 Application stages /11,527 facts;23,647 Document facts;10 calibrated Debug preflight facts; two crash probes; nine fresh harness captures | 3,122 / `55DCD2A7360E36AAB8BC88140FDF1AB7ACFF1689969835ABEC1A87A9516DFDB8` |
| x64 Release c4r1 | PID17172 exit0, 13:19:29 UTC | All five suites; same152/11,527 Application roster;23,641 Document facts; four preflight facts; two crash probes; nine fresh captures | 3,123 / `803B71AE8D94AF2809FB74206F5F30CD9F75FC7EA76A8B551A95FEA11B8F8FDB` |
| x64 ASan Debug c4a1 | PID101044 exit0, 13:00:06 UTC | All five suites; same152/11,527 roster;23,641 Document facts; real isolated heap-use-after-free diagnosis;256-round raster /64-worker stress; both crash probes and nine fresh captures | 3,261 / `B582C73A736848D048FC1CEE60790BE50526CAB68478976CED3DB91C70FB34E3` |
| ARM64 Debug c4armd1 | Build exit0 | Cross-build only:994 compiler records, one fresh changed fixture /993 exact inherited objects, five AA64/full-PDB pairs | 2,913 / `3F8BEEF04AB014C94DAA774633917ACE85F8A03BE69908E3324938F56892292A` |
| ARM64 Release c4armr1 | Build exit0 | Same cross-build/profile/object scope | 2,913 / `5008203120A67C7F71E0D2FD8ED47A2A8BA59C9745EC6EF9AFCA0D48B129C93E` |
| ARM64 ASan Debug c4arma1 | Build exit0 | Same cross-build scope, with real sanitizer instrumentation | 2,917 / `E5F76BEBE758C7E6A5E47CF7F4A5CE89F2987EF66F4B2BEADD4658AD1B0875E6` |

Each profile has994 actual compiler records. The x64 instruction scans and all14,133 default scanner controls
pass; the ISA allowlist and DxUi pin are unchanged. Release and Debug do not borrow the ASan probe or stress
receipts. All three Application fact multisets match the independently qualified capture candidate exactly,
including64 Polygonal dispatcher cases /729 facts. The calibrated allocation observer is Debug-specific.

## Retained failures and reader corrections

The c2 composition charge failure is retained; its deterministic held-release control and fixture-only repair
are documented in [P1PublicationParentRetirement](P1PublicationParentRetirement_2026-10-19.md).
The c3 Release panel/Wand completion failure remains unresolved. Current passing runs and failure-only numeric
logging establish no historical cause or causal repair. The original predicate and15000ms bound are unchanged.
See [capture/preflight evidence](P1RasterCaptureAndPreflight_2026-10-19.md).

The first c4 Release reader still required c3r1. Its next version wrongly assumed every Application addition
was a dispatcher fact, omitting the qualified capture/clipboard/session-end additions. Both refusals remain
retained. Reader r4 checks all predecessor facts and the complete11,527-fact roster against the independently
qualified canonical ASan archive with the identical source freeze. It qualifies the original Release archive;
no native test or expectation was changed or replayed for these reader corrections.

## Limits and next delivery

Native ARM64 execution is unavailable; no ARM64 runtime claim is made. Full Release analysis p1an1 remains
proof for the preceding Pixel Move source, not fresh analysis of this combined patch. The layer-row, kernel,
UI and prepared-display benchmark runs here are correctness smoke tests, not measured G1 qualification.

P1 remains ACTIVE/WIP. Initial Ctrl/lift, ordinary display/tool activation, typed journal pixel continuation,
complete production admission/retirement, Image Size apply and all remaining delivery gates stay open.
Separate private Ctrl/pixel CPU/GPU and weak-owner work is not included in this canonical source or activated.
