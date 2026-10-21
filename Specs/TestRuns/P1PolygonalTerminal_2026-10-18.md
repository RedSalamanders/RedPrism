# Polygonal terminal qualification — 2026-10-18

Scope: selected gated Win32 caller; P1 remains WIP. Production C++ and legacy goldens are unchanged.

## Code and independent references

Eighteen new cases / 208 assertions preserve all 63 cases / 791 predecessors: 81 cases / 999 facts total.
Both complete Application runs pass 152 stages / 10,582 stdout assertions. The six new scenarios run
in New, Add and Subtract: Escape, Backspace-to-zero, two-corner near-first, short double-click, captured
header-mode callback and harness DPI message handling. Joined Close stays last.

Escape is Cancelled with no publication/history. Last-corner Backspace is Finished/endedByBackspace with
no publication/history. Near-first with two stored corners records a third point and stays live before
Escape. A true short double-click clears the New case's present empty selection once, while Add/Subtract
keep the original sixteen covered centers and no history entry. The accepted physical sample is checked
for PolygonPress/double-click and not-near-first. Existing numeric coverage, Undo/Redo and strong/weak
retirement references stay unchanged. Mode is captured at Down; the later direct callback changes the
header setting without retargeting the original writer. The DPI case uses a screenshot-DPI override and
native `WM_DPICHANGED`, restores bounds/override, and keeps the same writer and committed state.
These two cases establish callback/harness behavior; actual widgets and OS monitor transitions remain open.

## Review and retained failures

Main corrects the cheaper fixture draft's inherited corner removal, joined-Close order, cancelled-versus-
Finished semantics, early asynchronous assertions and unsupported DPI/widget claims. The source-only
reader is pinned before execution and preserves all 527/713/791 predecessors plus untouched 151-stage
stdout and full stderr outside this fixture. Four local positives, two full-chain profile positives and
39 refusals pass; expected output is derived from source and independent references.

Original Release R1 reaches 294 checks / 23 cases, then fails its double-click join expectation.
R2's failure-only diagnostic hits a retained C4477 format error; R3 fixes only the format, compiles/links,
then encounters the retained interior-table ISA refusal repaired in `03908d3`. The unchanged R3 diagnostic
image then reproduces the join failure: no active host request, no queued input, no closing ordinal and
a live original writer parked for input. Source inspection confirms native double-click detection requires
both timestamp and previous-point position. The fixture had updated only the timestamp and clicked
(2,2) after (8,2), correctly selecting a non-closing near-first point. R4 clicks the prior corner and
asserts the actual captured flags. Production behavior and legacy goldens are not changed to fit the test.

Original failed-runtime archive completions are `06AA22E3B674AE6F2ABBCB97B12A7866154ED8562BBB8478BA8214F57E080DA1`
(R1) and `E5C9CF18A1E8C5060DBA46CB3FF4839E3F348B76FDC7CB07057FA62A5DC0A790` (diagnostic R3).
Compile/ISA failures and both source-only/preflight refusals remain separately retained.

## Actual profile evidence

| Profile | Actual PID / exit | Recompiled / exact reused objects | Build files | Runtime files | Runtime completion SHA256 |
| --- | --- | --- | --- | --- | --- |
| Release | 69876 / 0 | 1 / 348 | 28 | 21 | `1FF924C42FF4EEA207EF85C030ADF66CD5F84836A07DFA6B66D74E0DC27941DA` |
| ASanDebug | 102656 / 0 | 1 / 348 | 27 | 22 | `346ED737879EB70239B96AA6A7553BCC33430A18CBC1B763A5FAC8134A68A561` |

- Release build archive completion: `7691FEA3A648EE7A419300CC8E58C3155F176D7A6689C943D1A00195303E720C`.
- ASanDebug build archive completion: `21EAF690B06E199B9F29B1FF7126B790B72C611ABFC1699CF8766F0C6B3A64B1`.

Main independently checks every archive byte, all 348 unchanged object identities, the one new fixture TU,
actual profile flags, matching full PDB/PE identity and zero ISA violations. ASan instrumentation remains
real across all 349 Application objects. The committed scanner update is an explicit tool-only substitution;
no native compiler/dependency scope is relaxed. Exact unchanged CPU/UAF artifacts and ASan DLL closure
retain their recorded reuse mapping, separately from these two fresh full Application runs.

## Retained evidence and remaining scope

Private code/recipes/readback are under `.build/P1Drafts/PolygonalTerminalNative-Main-r4/`; source review
and source-only reader controls are under `PolygonalTerminalFixture-Main-r3` and `PolygonalTerminalReader-Main-r3`.
Prior candidate and diagnostic directories remain immutable. Complete cache archives are under
`D:/RedPrismBuildCaches/.build/p1-polygonal-terminal-{release,asan}-r4`. Main reviews and corrects the cheaper
documentation draft's commit hash and near-first wording before adoption.

Ordinary activation, the exact-hit New/nonempty/Ctrl-lift dispatcher, remaining device/display scope, full
latest native/analysis/ARM64 qualification and measured G1 remain open. Native ARM64 runtime is unavailable.
The unrelated inherited inverse-selection held-gate ASan exit 5 remains an audit item; later passes do not
prove a causal repair. This evidence makes no RAW/Photoshop or presentation-latency claim and does not
move P1 to Done or authorize a main merge.
