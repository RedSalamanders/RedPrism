# Selected Size recovery and owner-cost followthrough

Status: current native correctness integration qualified; ordinary activation and complete P1 performance remain open.

The candidate preserves the qualified original final-publication baseline. Private ASanR2 compiles all358
Application TUs fresh. R4 replaces only the Size test with its actual history-OOM control, retaining357 exact
same-header objects. R5 replaces only benchmark instrumentation, retaining the exact191-Size-test object.
Each linked image has independently checked actual compiler/object/link identity, matching full PE/PDB and
zero ISA violations with unchanged DxUi and runtime allowlist. The R3 XML preparation failure is retained.

R4 runs the standard155-stage Application self-test, with all 12,233 stdout facts, exactly 473 Ctrl, 191 Size and
123 row assertions. This is Application-only; it is not a test.ps1/Host/Document/composition/I/O full run.
Exact source and product bookends bind native.result.json and runtime-qualified.json under
.build/P1Evidence/p1-size-next-coherent-app-private-r4/ASanDebug. The old129Size/473Ctrl and all old
Application facts remain, except the correctly replaced row summary. No legacy numeric reference changed.

R5 separately runs affected Size, direct and focused sampler smoke with source/product/script bookends.
Six Size setup/warmup boundary pairs and36 resource rows remain separate from three real scored-publication
records. Smoke never asserts a duration or qualifies performance. Receipts live under
.build/P1Evidence/p1-size-next-coherent-app-private-r5/ASanDebug. Initial helper omissions of the external
clipboard test header/source were independently recovered from the exact passing canonical f3 source freeze;
native compilation was not repeated to erase those reader refusals.

The complete latest integrated native matrix and canonical analysis now pass as recorded below. There is no available native ARM64 runtime host, so native ARM64 runtime qualification is unavailable and is not claimed. Ordinary activation, complete phase peaks, H4 and P1 G1 remain open. P1 is WIP.

## Release owner-cost evidence (private R1)

The six completed assessments under `.build/P1Evidence/p1-size-final-handoff-bench-private-r1/Release/` report these maximum inclusive completion-Apply durations, in microseconds. Each row lists Canvas Size (accepted at 4K; refused at 45/100MP), Image Size nearest, then Image Size resolution.

| Profile | Canvas / refusal | Image nearest | Image resolution |
| --- | ---: | ---: | ---: |
| Hardware 4K | 257 | 189 | 310 |
| Hardware 45MP | 93 | 161 | 141 |
| Hardware 100MP | 71 | 160 | 183 |
| WARP 4K | 332 | 554 | 273 |
| WARP 45MP | 40 | 494 | 177 |
| WARP 100MP | 87 | 325 | 167 |

The observed Apply maximum is 554 µs, below the 2 ms Apply target for these measured samples. This does not close the full Apply gate: frame and drain maxima still exceed their targets, and complete phase bounds remain open. Initial-display and excluded-warmup boundaries are separately recorded and remain costs to assess; excluding them from scored distributions does not erase or waive them. All six assessments report `complete_G1=false` and `P1_done=false`.

The 45MP and 100MP Canvas Size cases both report the expected `canvas-size-fill-refused-pixel-budget` refusal. Neither is a published resize.

This evidence is private Release timing only. The latest code integration now passes its required full native matrix and canonical analysis, as recorded below. ARM64 was cross-built in earlier checkpoints, but no native ARM64 runtime host is available; no native runtime qualification is claimed. Ordinary activation, H4, complete G1 and P1 Done remain open.

The matching coherent private Release image passes the complete standard Application self-test
(PID 73456, exit 0):155 stages,12,233 Application facts,191 Size,473 Ctrl and123 row assertions.
Its EXE SHA256 is AB9ACDEFB9B7586CD0731E2B5D8EC59108154EAFEC0CD85A8DC207AF944EA512;
the matching full PDB is D7DE0A6B47FE40CF13D934D9E08653CF8C8167A1084D474C2EFEA63B6BB9F1EC.
Release boundary smoke passes separately. This is still Application-only proof. The measured Apply scope
includes original final CPU/history/rows/display publication, rather than only the document handle swap.

Recorded owner/frame misses include hardware100MP resolution frame p99 upper bucket8 ms; WARP4K
frame p99 buckets8/8/16 ms; and a WARP100MP nearest frame maximum117.940 ms. Initial-display
boundaries additionally retain hardware4K fill drain18.298 ms, WARP45MP nearest drain54.889 ms,
WARP45MP resolution frame78.584 ms, WARP100MP nearest drain135.947 ms and WARP100MP resolution
frame92.877 ms. These remain failed or unqualified costs; later passing repetitions cannot establish
their cause. Diagnostic attribution is in progress. Actual monitor refresh must be verified before the
two-refresh frame-latency gate is assessed.

## Current integrated native qualification — 2026-10-20

The complete current Size recovery implementation passes full canonical x64 Debug, Release and genuinely
instrumented ASan tests, all three ARM64 cross-builds, and canonical full Release analysis. Every profile
contains the same 1,883 frozen inputs, 1,022 actual translation units and five matching complete PE/PDB pairs.
The source-freeze SHA256 is `1435B7A7302040A69A1D998C2792691EB124D89182C997A7A6C365DCE762976C`; its starting HEAD is `aa142f9` and its frozen working delta is
`DF960A043E2150E72D944E94161EDA42AD431728EDBBEB9EE2BCB384FA3F39C3`.

| Profile | Actual parent / exit | Translation units | Archive SHA256 |
| --- | --- | ---: | --- |
| x64 ASan Debug (`f4a1`) | 82852 / 0 | 1,022 | `D97D8C0B13D3B9B9DD92413A49FB26FFCA7DD4FDAF0A2FE233F8C87852379935` |
| x64 Debug (`f4d1`) | 95520 / 0 | 1,022 | `48FD9EA6AE0E06DF4D543534A2C66CCE5C6515F9FD8516AE61840FBFAACAA1EC` |
| x64 Release (`f4r1`) | 66788 / 0 | 1,022 | `C34E780C568780BD9651B11AF35DB7AEC35A9EE972D0BEDC3E812940719159BC` |
| ARM64 Debug (`f4ad1`) | 86084 / 0 | 1,022 | `EE8014858CC1ADA7E070E3FC61BF2A3B39339D2FF302A4717B63798CAC9DD0EB` |
| ARM64 Release (`f4armr1`) | 3912 / 0 | 1,022 | `BB3C0F012B11446E2261C8A2D26D5337316C15A67EEFBDB0B400CA503750D141` |
| ARM64 ASan Debug (`f4arma1`) | 70248 / 0 | 1,022 | `16870061E783BE2AE0781A5481E3ACE8FA428C1D5AD64D09D3580F57E3AA6A88` |
| x64 Release analysis (`f4an1`) | 68728 / 0 | 1,022 | `F147BDC641EB7AB68DB02010E3681C5D41DD824090D81938E8EEDA70C0CA78F3` |

Each x64 runtime retains all 155 Application stages and 12,233 Application PASS records, including the exact
191 Size, 473 Ctrl and 123 row assertions. Current Document totals are configuration-specific; the strict
readers check the complete predecessor/delta roster, rather than asserting one shared total. Both crash probes,
the focused Size/direct/sampler smoke, layer-row smoke and all nine integrity-checked harness captures pass.
ASan additionally proves a real UAF diagnostic, 256 raster stress rounds and saturated-worker stress.
The analysis verifies all 1,022 actual compiler commands, the clean positive SAL control and the real negative
C26130 control. Warning limits, pixel goldens, runtime allowlist and DxUi pin are unchanged.

The ARM64 Debug attempt's original reader-name refusal is retained and repaired by an exact name/platform/
configuration check on the same successful build. No compiler/runtime rerun erased that refusal. Main also
corrected the private aggregate reader to require sanitizer-only proofs only from ASan and the exact Release
conditional roster from Release; original reader sources and receipts remain preserved.
The original Release verification invocation omitted its required conditional-roster argument. The retained
invocation refusal is separate from the successful native test. Recovery verifies that both defining sources
are unchanged from the previously qualified roster, binds that roster to the current freeze and qualifies the
same archived run. No native tests were repeated to recover this verification error.

Evidence and actual process/archive/runtime receipts live under
`.build/P1Evidence/p1-size-recovery-native-integration-r1/`. The independent aggregate and strict SAL readers
are retained under `.build/P1Drafts/SizeRecoveryMatrixCloseout-Luna-r1/`, with Main's review corrections.
No native ARM64 host is available. Cross-compilation establishes no ARM64 runtime or timing result.

This completes the Size recovery correctness integration step. Ordinary activation, complete phase admission,
the measured frame/drain misses, complete H4/G1 and the parent P1 milestone remain open.
