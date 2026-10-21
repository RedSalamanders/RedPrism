# Paused P1 main consolidation — 21 October 2026

This is integration evidence, **not P1 acceptance**. The user requested merging P1 into main and restarting
later from that main. P1 remains HOLD; ordinary activation, Debug/ASan/native ARM64 qualification, capture,
stress and measured performance delivery gates remain open.

## Source and preservation

Canonical paused checkpoint: `85de11d481630d51a7e550f01065dbd22d6eafbd`. Main maintenance input:
`a6800c2bfdd5ab095cd1baaf1a5c770fefcaf9a1`. Reconciliation merge: `2e7b559`; satellite history join: `aeb9d69`.
The history join retained exactly tree `80e814fb68663805a01fcf1171d97bd6defd077a`; it adopted no older/private
residual production code. The [branch inventory](../Plans/WIP/P1MainIntegration_2026-10-21/satellite-source-inventory.json)
and [native rename map](../Plans/WIP/P1MainIntegration_2026-10-21/native-test-renames.json) preserve exact lookup anchors.
The original checkouts, their uncommitted work, the older stash and recovery bundles remain untouched.

## Observed checks

- Independent `validate.ps1`: passed after reconciling the main full-image PDB proof with P1's decoder-only
  controls, preserving all frozen expectations. Corrected CRLF parsing, architecture fixture imports, invalid
  timeout routing and saved-output/native reader classification. The scoped-testing fixture reports 24 passing cases.
- `format.ps1 -Check`: passed. The initial integration compiler failure exposed 25 dropped renamed includes
  and one stale viewport variable. Restored includes from the unchanged P1 checkpoint; all five project lists
  retain every original compile/header item after mapping.
- Final canonical x64 Release build: passed with build number **464**. All five linked-image ISA scans and
  real compiler-generated positive/negative controls passed. The preceding attempt compiled/scanned successfully
  but correctly refused attestation because test-routing source changed during its execution; that refusal is retained.
- `test.ps1 -Configuration Release -SkipBuild -SkipTooling -Suites Host,Document,IO,Composition,Runtime`:
  passed. All four standalone suites, failure reports, real main/worker crash probes, command-line help,
  hidden WARP application self-test and exact pinned DxUi provenance passed. The application log contains
  155 `BEGIN self-test` stages and 12,301 `PASS` lines; these counts describe this observation only.
- Routing regression: passed after removing duplicate pure ISA parser invocation from the native compiler
  gate. Independent tooling owns those controls once; every native build retains actual image scans and
  compiler-generated controls. The default full image gate and allowlist are unchanged.

The [log hashes](../Plans/WIP/P1MainIntegration_2026-10-21/validation-log-hashes.json) bind successful and failed
attempts retained under `C:/Users/eric/Documents/Codex/2026-10-21/review-remediation`. Build/test products stay in
the owned integration checkout's `.build`; none are committed. Historical failures remain historical, and
these selected Release results do not stand in for the missing Full/platform/resource/P1 acceptance gates.

The user's authorization accepts this paused source consolidation with unfinished gates; it does not change
normal PR qualification policy. Hosted checks must be reported by their actual state, including billing refusals.

## Restart

Fetch current main and create a fresh feature branch when the user resumes. Start with the opening
[P1 continuation](../Plans/WIP/UiNeverBlocks_2026-09-22.md), its private scanner/host/toolchain notes and the
satellite divergence inventory. Do not activate unfinished paths or replace current code wholesale from a
preserved satellite. The earlier checkpoint's Release ISA refusal is retained; this main integration's
Release image scans pass, without closing the remaining delivery matrix or private draft review.
