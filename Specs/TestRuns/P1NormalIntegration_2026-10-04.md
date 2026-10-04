# P1 normal integration — 2026-10-04

## Qualification checklist

- [x] Preserve the first incremental normal Debug run and independently verify all 801 source/evidence copies; separately preserve its 49 command-tracking logs.
- [x] Reject that run for application qualification: it executed 75 stages instead of the integrated candidate's 78. Copying source retained older timestamps, so incremental compilation reused the previous application registry.
- [x] Force `-Rebuild` for subsequent normal tests and cross-builds; capture actual child process handles, exits and frozen inputs.
- [x] Preserve the first clean Debug failure and independently verify all 850 source/evidence copies before changing an input.
- [x] Complete and archive the diagnostic clean Debug run: actual script exit 0, all 78 application stages and exactly the preceding 8,962 named controls; all 850 copies independently verified.
- [x] Complete and archive clean normal Release: actual script exit 0, the identical 78 ordered stages / 8,962 named controls; all 849 copies independently verified.
- [ ] Diagnose and resolve the clean run's Magic Wand failure while retaining the literal expected selection and history.
- [ ] Complete fresh normal x64 Debug/Release/ASan suites, compare the executed stage/assertion registry, verify every archive copy and complete ARM64 cross-builds.
- [ ] Activate the qualified candidate on the native branch and close the remaining P1 ownership and measured G1 rows.

## Scope and source

Candidate: `codex/p1-opacity-host-proof`, based on `b22f974`, with the privately qualified combined
lifetime source, portable project entries, normal DxUi output-path repair and immutable test fixtures.
Private application qualification is recorded in [P1DeferredCanvasLifetime](P1DeferredCanvasLifetime_2026-10-04.md).
It does not substitute for this normal solution matrix or the [P1 milestone](../Plans/WIP/UiNeverBlocks_2026-09-22.md).

## Preserved normal runs

Archives are under `%LocalAppData%\RedPrism\QualificationArchives\`.

| Attempt | Actual result | Independently verified archive | Completed receipt SHA-256 |
| --- | --- | --- | --- |
| `normal-test-Debug-r1` | Script exit 0, but stale 75-stage application registry; rejected qualification | `p1-lifetime-normal-Debug-r1-stale-application-registry`, 736 sources + 65 evidence files | `3EB9EDE05E973A86AB3CA033FDCD17D93F5E9252D424418F9D91BE67016BE136` |
| `normal-clean-x64-test-Debug-r1` | Script exit 1; application self-test exit 5 at the Magic Wand selection fixture | `p1-lifetime-normal-clean-Debug-r1-wand-failure`, 736 sources + 114 evidence files | `088BF189A5495A3514503216533EC996ACAD995D47FBDBACF91949E78DA14D2F` |
| `normal-clean-x64-test-Debug-r2` | Script exit 0; 78 stages / 8,962 named controls match the private qualified predecessor exactly | `p1-lifetime-normal-clean-Debug-r2-diagnostic-pass`, 736 sources + 114 evidence files | `97BB097BA6FDDAF5517DFA5E9AA04F77D86A6FA241AEE009AAB0BB132ACB16D4` |
| `normal-clean-x64-test-Release-r3` | Script exit 0; identical 78 ordered stages / 8,962 named controls | `p1-lifetime-normal-clean-Release-r3-pass`, 736 sources + 113 evidence files | `F1B605FC39A035FF8921D76001D9A64CDFA500E5618618D822C1D061339A4866` |

The clean run built all five images and passed the build/ISA controls and standalone suites. Its new standalone
lifetime shutdown stage executed 104 controls. The subsequent application fixture failed before completing
the full self-test: selecting the painted swatch must still produce bounds `{40,40,160,120}` and history
`Magic Wand`. This requirement has not been relaxed.

The second clean Debug attempt adds diagnostic state before the pointer event and to the failure message.
It changes no product branch or selection expectation. Before the successful pointer input, the Wand tool
was active with no queued/active host request, history work or opacity edit; all-layers sampling was off and
tolerance was 32. Its full normal build/test, ISA/provenance, corpus acquisition controls and nine harness
screenshots passed. Named application controls are parsed from both stdout and stderr and match the private
predecessor exactly. A successful diagnostic run alone does not explain the preserved failure; input ordering
and repeatability must be resolved before declaring the candidate complete. Clean normal Release also passes;
clean normal ASan is in progress. A focused read-only audit found no proven cause in viewport, generation or
publication ownership; the remaining evidence gap is the synthetic pointer's effective modifiers and exact
admission/capture/apply route. No delay or relaxed selection expectation has been added.

## Validation discipline

Freeze source, scripts, build inputs and dependencies during an attempt. Record actual process exits and
the executed registry, rather than inferring success from a wrapper or aggregate count. Preserve executable,
PDB, DLL, logs, ISA/provenance evidence and command-tracking logs before another build can overwrite them.
Independently read back every archived source/evidence copy before editing or starting another attempt.
ASan requires real compiler instrumentation; ARM64 cross-builds establish no native runtime or timing result.
