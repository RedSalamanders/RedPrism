# P1 normal integration — 2026-10-04

## Qualification checklist

- [x] Preserve the first incremental normal Debug run and independently verify all 801 source/evidence copies; separately preserve its 49 command-tracking logs.
- [x] Reject that run for application qualification: it executed 75 stages instead of the integrated candidate's 78. Copying source retained older timestamps, so incremental compilation reused the previous application registry.
- [x] Force `-Rebuild` for subsequent normal tests and cross-builds; capture actual child process handles, exits and frozen inputs.
- [x] Preserve the first clean Debug failure and independently verify all 850 source/evidence copies before changing an input.
- [x] Complete and archive the diagnostic clean Debug run: actual script exit 0, all 78 application stages and exactly the preceding 8,962 named controls; all 850 copies independently verified.
- [x] Complete and archive clean normal Release: actual script exit 0, the identical 78 ordered stages / 8,962 named controls; all 849 copies independently verified.
- [x] Preserve the first normal ASan attempt's actual build failure: 819 source/evidence copies and all 130 ISA fixture files independently verified. It reached no application runtime.
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
| `normal-clean-x64-test-ASanDebug-r4` | Script exit 1 in the mismatched-PDB ISA control; no application runtime | `p1-lifetime-normal-clean-ASanDebug-r4-isa-mismatched-pdb-failure`, 736 sources + 83 evidence files | `1DB6DD4E98D328AE5735DDCE5E0C8EB09FD6DBAD8B559C33BDD18FCCF8AF8F2A` |
| `normal-clean-x64-test-Debug-r5` | Latest combined lifetime/bounds/scanner candidate, script exit 0; 78 stages / 8,962 application controls and 6,313 document controls | `p1-lifetime-bounds-normal-clean-Debug-r5-pass`, all 852 copies verified | `01A7A3B6BBD19DDC2E94ED2701CEA02ED2A1135710D12EC8E9C3CBE0CABECB64` |
| `normal-clean-x64-test-Release-r6` | Script exit 1 before runtime: decoded BF16 bytes in a six-entry switch table require a new exact data proof | `p1-lifetime-bounds-normal-clean-Release-r6-isa-rejection`, all 851 copies verified | `D2CC8EF21ED87D34B8FC041B424909EFFBB7120261D65324C205B94E265A527A` |
| `normal-clean-x64-test-Release-r7` | Fresh combined candidate, script exit 0; exact 78 stages / 8,962 application controls; all ISA controls and screenshots pass | `p1-lifetime-bounds-normal-clean-Release-r7-pass`, all 1,004 copies verified, including scanner extractions/fixtures | `3D389DB2E71765E6167D6A89C65BE325EB64749B04C0D3DE49BE2AC175FEDB31` |
| `normal-clean-x64-test-Debug-r8` | Same combined source, actual script exit 0; exact 78 ordered stages / 8,962 application controls, all ISA controls and nine screenshots pass | `p1-lifetime-bounds-normal-clean-Debug-r8-pass`, all 1,005 copies independently verified | `F7D5794569F077172C137EF447DA5F31591CF3FE2800DBFDDF8E73EF54413D12` |

Debug r8's archive includes current scanner extraction and negative fixture products. Its source/evidence
manifest hashes are `D1C2FB924F15782B8AD52245491380FCBFBE810E8A5E2EC6EBA5D62877EA36A4` and
`A172C4F1EE27FB4302FCDA3204FA487A2C96077100F27BA96705806D83D6A59A`; independent copy verification
completed at `12:05:54.3041974Z`, followed by exact application registry verification at `12:06:23.6360112Z`.
Fresh ASan r9 is running against the frozen combined candidate; ARM64 cross-builds and native activation
remain open. Successful repeated Wand fixtures still do not establish the first failure's cause.

The Release r6 output directory retained the preceding Release self-test logs; its completed archive lists
their 78 stages as preserved products. This attempt actually executed **zero** application stages, confirmed
by the fresh attempt stdout/stderr and its build failure. Those older logs are not runtime qualification.
The archiver now counts a self-test log only when it was written within the actual attempt interval; old output
logs do not count. Release r7's log was written at `11:44:28.6078114Z`, within its actual
`11:36:16.1545412Z`–`11:45:01.7908182Z` interval, and independently proves 78 ordered stages / 8,962 controls.
Its preserved completed archive's initial stream-only stage field is zero because the script retains stage
markers in the self-test files. The separate registry receipt records the exact executed set. All four failing scanner extraction
files were separately copied and verified in `p1-lifetime-bounds-normal-clean-Release-r6-isa-extraction`, manifest
SHA-256 `6139712AD31A8B650AC2F8AAE31361FE1120EDC8CD1B9A98357C25E11E5DC0BC`.

Debug r5's scoped Wand trace confirms pointer `{100,80}` with no modifiers, New/active-layer route, successful
admission/capture, prepared publication and current completion. It preserves the original swatch/history golden.
This successful trace does not establish the cause of the earlier intermittent failure.

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
and repeatability must be resolved before declaring the candidate complete. Clean normal Release also passes.
A focused read-only audit found no proven cause in viewport, generation or
publication ownership; the remaining evidence gap is the synthetic pointer's effective modifiers and exact
admission/capture/apply route. No delay or relaxed selection expectation has been added.

The ASan build exposed the scanner's order of validation: it interpreted an unrelated PDB's contribution offsets
before checking its GUID/age against the image. That negative fixture can have different PE section sizes and
throw the wrong diagnostic. The candidate now checks identity first. Focused ASan scanner tests pass the original
mismatched-PDB rejection and two independent wrong-GUID/wrong-age controls with impossible section offsets.
The ISA allowlist and contribution/instruction checks are unchanged. The 130-file failure supplement is
`p1-lifetime-normal-clean-ASanDebug-r4-isa-fixtures`, manifest SHA-256
`DFA877CABD0901A7E0D10E13DCA312EB30BCDF315ECB2ED3E2B1A3D7E118C89B`.

After all preceding actual attempts and copies completed, the eight privately qualified ownership-cache files
were integrated and hash-checked. A scoped pointer/admission/capture/worker/completion trace was added to the
Wand fixture; it reads only captured values on workers and changes no selection branch or golden.
The combined candidate passes formatting on 622 files and is undergoing a new forced-rebuild Debug matrix.
Earlier Debug/Release results qualify their earlier source, not this subsequent ownership-cache integration.

## Validation discipline

Freeze source, scripts, build inputs and dependencies during an attempt. Record actual process exits and
the executed registry, rather than inferring success from a wrapper or aggregate count. Preserve executable,
PDB, DLL, logs, ISA/provenance evidence and command-tracking logs before another build can overwrite them.
Independently read back every archived source/evidence copy before editing or starting another attempt.
ASan requires real compiler instrumentation; ARM64 cross-builds establish no native runtime or timing result.
