# P1 Polygonal DPI fixture portability — 2026-10-21

Status: complete same-source local runtime/build matrix qualified; publication and renewed native validation pending. P1 remains WIP.

## Checklist

- [x] Preserve and independently verify the native ARM64 assertion failure and its exact source/products.
- [x] Reproduce the generic maximum-tracking clamp with an independently owned hidden Win32 reference.
- [x] Review every retained assertion; add a contained rectangle and live minimum-size precondition.
- [x] Adopt the candidate after the preceding source-frozen shutdown qualification closes.
- [x] Run full x64 Debug, Release and genuine ASan; preserve all 12,298 predecessors plus three new facts.
- [x] Cross-build ARM64 Debug, Release and ASan Debug.
- [ ] Qualify native Release separately on the published source.
- [ ] Commit/push each qualified step and verify the destination SHA; retain every failed attempt.

## Observed failure

[Native run 39511716098](https://github.com/RedSalamanders/RedPrism/actions/runs/39511716098) tests
`43725c68817eb303b519f9837064d03bb77a148c`. Actual Application PID 1580 exits 5 and canonical parent
4492 exits 1. Its sole failed predicate is
`polygonal-press-host.new.harness-dpi-live.harness-dpi-override-native-message-rebuilds-live-chrome-not-os-transition`.
The group reports 316 checks, one failure, reached 25. Closing source/environment receipts and five
AA64/full-PDB pairs are verified. The failure remains separate from later local candidate results.

The original assertion combines chrome DPI, unchanged native window DPI, the exact suggested bounds,
the retained drawing writer and original ticket, draft point count, selection and history revision.
Its single failure does not identify which term failed. The fixture enlarges an already accepted native
rectangle by 32 by 24 physical pixels; that requested size need not be accepted by every host.

## Independent reference and candidate

The private Main reference creates and owns one hidden PMv2 Win32 HWND with an explicit 640 by 480
maximum tracking size. Its actual manually delivered WM_DPICHANGED expansion to 672 by 504 is clamped,
while the contained 608 by 456 rectangle is applied exactly. Native DPI stays 144 despite the synthetic
120-DPI message. Actual exit is 0; the app and desktop are untouched. This establishes a generic fixture
failure mode, without establishing the failed ARM64 term. Reference source SHA256:
`8C7673A5D3398E89F38152B798434EFA23FE2A34A390D32F5C239BCD03B4C50E`;
output SHA256 `472663957942E9CD1AE0BFCEB32F5C0D98F8550AC472871799E4D8C3023A6B29`.

The candidate insets the accepted native rectangle by 16 horizontal and 12 vertical pixels on each edge.
Before dispatch, it queries the actual owner minimum at the proposed harness scale, restores the old
override, and asserts containment, positive extent and both minimum dimensions. This adds one named
precondition in each of New/Add/Subtract. An unsuitable setup fails explicitly.

All original DPI, native bounds, writer/ticket, point-count, selection/revision, current-viewport click
and restoration predicates and labels remain. Unexpected writer loss becomes a failed predicate through
explicit guards. Failure-only typed diagnostics print every combined term and the requested/actual bounds.
The ordinary application handler, timeouts, legacy selection/pixel goldens and dependency pin remain unchanged.

A cheaper agent verifies original predicate preservation and diagnostic types, and identifies the residual
minimum-tracking assumption. Main traces the actual WM_GETMINMAXINFO handler and adds the live minimum
precondition and failure-path guards. Final private candidate SHA256:
`DD7917E25A9139DCFD52DF0F46594E39FBB8A1A3A979D486670711EDCF85A3EE`.

## Required qualification

The full same-profile x64 comparison must retain all 12,298 predecessor Application PASS records, all 155
BEGIN stages, all four suite rosters, unchanged Application stderr and the existing 36 shutdown controls.
Its only PASS delta is the three new DPI setup facts, giving 12,301 Application facts. Genuine ASan probes,
stress, canonical correctness smokes and captures remain required. ARM64 cross-builds prove compilation
only; a native hosted result must close the actual assertion before native runtime is claimed.

Static analysis for the preceding production shutdown repair remains scoped to its frozen source. Renew
full analysis on the final P1 source; this fixture-only change does not itself establish complete G1,
ordinary activation, native runtime or P1 Done. No Photoshop speed comparison follows from these tests.

## Complete same-source local qualification

All six canonical commands exit 0 against the same 1,898-input freeze:
`6982A7739A3C7AD9A10E1621B55608C5A82FA33412A20671AACB6D163D8D474E`.
Working patch SHA256: `3B34FFAAD2F428D896F6CAAFC1F44F3FB45810F04AF586C2834E03021D3C91CA`.
Full x64 tests use `test.ps1`; ARM64 cross-builds use `build.ps1`, with `-MaxCpuCount 4`.
The serial shutdown analysis finishes before this fixture is adopted; its tested source is retained separately.

| Profile | Attempt | Actual exit | Read-back archive manifest SHA256 |
| --- | --- | --- | --- |
| x64 Release | `dpi1r1` | 0 | `B4DD113638B9EF03A564AC017CF7E90F550F2AE539AB7DC742E17E2882FB70D5` |
| x64 Debug | `dpi1d1` | 0 | `489DB94D8D05A7D3825689BC3281C054FB31762B5AA543678A62FB990C63D506` |
| x64 ASan Debug | `dpi1a1` | 0 | `24A415D80B6A88C4F84F4C9BDD073E61F7F6AE5316AC6A129B86CD6E402A483A` |
| ARM64 Debug | `dpi1armd1` | 0 | `D2A7CDD3E4D3BDE8CD90CD87B2B8020501004FFB7F2FB2A869BC8685B656D3A3` |
| ARM64 Release | `dpi1armr1` | 0 | `4A3725B7C42432D99AE1261DC6C952553AB6006081E56A4E6284425267983E84` |
| ARM64 ASan Debug | `dpi1arma1` | 0 | `AB08C0A88C7F90546DEC47CA2EDD970336B32E38FB6C39C5078318C75BE666A6` |

Main independently verifies all 1,022 source/compiler/object/link records and five executable/full-PDB
pairs per configuration, exact archive bytes and source bookends, required effective configuration flags,
and each x64 linked ISA gate. Every x64 runtime preserves all 12,298 same-profile predecessor facts and
155 BEGIN stages, adds exactly three passing DPI setup facts, and preserves all four suite rosters and
Application stderr bytes. Existing 36 shutdown checks remain. Genuine ASan detection/stress, canonical
correctness smokes and nine captures pass. Formatting checks 1,354 files and 15 skills validate.

Every New/Add/Subtract DPI case passes containment/live minimum, exact chrome/native DPI and requested
rectangle, original writer/ticket, selection/history, five-point current-view click mapping and restoration.
This local proof does not identify which combined term failed on the preceding native runner. Renew native
Release using the published fixture before claiming that assertion closed. ARM64 builds are compilation
and instrumentation proof only. Final-source analysis, broader P1 admission/activation and G1 remain open.
