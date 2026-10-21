# P1 Load Layer / Load Mask Selection — 2026-10-21

Status: complete same-source local component qualification. Full P1, current-source native ARM64 runtime and G1 remain open.

## Checklist

- [x] Trace actual ordinary handlers: settled commands call the synchronous loaders on the owner. The async
  cursor option does not guard these commands; queued execution still runs on the UI owner.
- [x] Prepare the shared immutable source kind, original command queue, source-sized complete trace/clip admission,
  worker-only sparse read/placement/intersection/history/outline and existing original-ticket retirement.
- [x] Add source-derived independent controls in the two existing registered test files: 32 core facts and
  102 actual-command host facts across fourteen cases. All are now observed in the three qualified x64 runtime profiles.
- [x] Canonical x64 Release, Debug and real ASan; preserve all predecessor assertions and independently verify
  frozen source/compiler/object/link/PE/full-PDB/runtime/ASan receipts.
- [x] ARM64 Debug, Release and genuinely instrumented ASan cross-builds; native runtime remains separate.
- [x] Same-source Release analysis, all 1,022 effective analysis command/object/link records and fresh SAL controls.
- [ ] Complete remaining ordinary caller/phase/admission/lifetime grid outside these settled load commands.
- [x] Record actual results, update the parent checklist, commit and push the qualified component.
  Published as `f4c712c21c0cfcacdeaf16db9e1e225662bd38a9`; the approved remote branch SHA is independently verified.

## Compatibility and boundaries

The numeric references are `LoadLayerSelection`, `LoadMaskSelection`, `MaskTracing` and `ApplySelection` in
`Common/Selection.cpp`, with existing legacy numeric goldens unchanged. Alpha 128 is included and 127 excluded;
mask 127 is included and 128 excluded. Tests use literal source bytes and clockwise rectangles to verify hidden
and zero-opacity layer alpha, linked/unlinked/flipped masks, a source larger than the canvas, explicit off-canvas
empty output, exact no-op/redo and saved-marker behavior, private sparse reads, stale target and cancellation.
The actual command cases retain pending source/history, original tickets/reservations and off-owner output
preparation/retirement, and test success, stale target, cancellation, capture/history/worker failure and full records.

The new host commands use New only, as their existing command handlers do. Direct Common APIs retain their
synchronous New/Add/Subtract reference behavior. Other pending gesture migrations, full display activation,
late-residency/caller phase bounds, first-frame costs and complete S5/G1 are not qualified by this step.

## Independent review

The cheaper helper's empty-trace concern is rejected against the actual reference: `TraceEdges` returns nullopt
at zero edges and again for an empty loop list; both synchronous loaders return before `ApplySelection` when
the trace is absent. This differs from a successful nonempty trace whose transformed canvas intersection is
empty. Both cases are independently exercised. The helper review is not treated as runtime evidence.

Private candidate/evidence preparation: `Z:/src/RedPrism/.build/P1Drafts/LoadedSelection-Main-r1/`.
P1 remains WIP; the preceding guarded native run measures its own published c805d5d source, not this new source.

## First candidate qualification and caller followup

`load1r1` compiles and links the new source but exits 1 at the preserved ISA scanner refusal before runtime.
`load1armd1` exits 0 and independently verifies all 1,022 compiler/object/link records and five AA64/full-PDB
pairs against the same 1,899 frozen inputs. Its 3,009-file archive manifest is
`8862E4643D7D539BCDA854F21493646DA1464A0D666F12BA9550E5DFF8A33070`. This is cross-build-only evidence
for that candidate, not the later followup source or native runtime.

Main also finds the existing 2,100-square checkerboard refusal fixture assumes synchronous load completion.
The followup waits for each original async acknowledgement while retaining every original detailed-error,
notice-count and blank-source predicate. Async load failures preserve the original error-observation counters
while posting their notice. This was a source-reviewed integration correction; the completed runtime
qualification of that followup is recorded below.

## Complete current-source local qualification

All seven attempts close with actual exit 0 on the same 1,901 frozen inputs, manifest
`AB67BC90251ACF1C8870236A39C57832F721A1C958ED6A0090F9D94CD0C42B0F`. Each independently verifies all 1,022 actual compiler/object/link records and five
matching architecture/full-PDB pairs; x64 profiles also pass the linked ISA and compiled controls.
The ASan profiles retain real instrumentation; the x64 run additionally proves real detection and stress.
Analysis retains effective `/analyze`/concurrency-plugin flags and fresh passing/failing SAL controls.

| Attempt | Configuration | Archive files | Archive manifest SHA-256 |
| --- | --- | ---: | --- |
| `load1r2` | x64 Release | 3,250 | `47F5D02D8D5C0CA138848C40CCFD889F7FE97C886B2324DC7B8537AEDB94B223` |
| `load1d1` | x64 Debug | 3,247 | `7FA212E7664F281561410DC1A9DD13F4358B5012EF9FEEEBDDA977BDEA0A3828` |
| `load1a1` | x64 ASan Debug | 3,383 | `833F4C08F92A0EA63C94C9354BAF1F3C616AAE1740A208036C0DC434902D6A2D` |
| `load1armd2` | ARM64 Debug | 3,011 | `9CCABE8A7430C114D8977A6E78EE5C6387177A0D9A22B6FDA0156082D8F49ADA` |
| `load1armr1` | ARM64 Release | 3,011 | `CC175DD064A91CA72711C2816375A0301F8A6CA6EE18876EA954C67308E13E40` |
| `load1arma1` | ARM64 ASan Debug | 3,015 | `F896C133EB68530169E30C2406EA29D348C72274F0D099FE1B9297136673EB56` |
| `load1an1` | x64 Release analysis | 3,263 | `46D1381005797C3D6556FD16D8C53C2101AAA598CFF8EC8AFF14EB448FCBF06B` |

All three x64 runs retain **12,301 Application stdout facts / 155 stages**, with zero stdout additions.
Exactly **102 new Application stderr facts / 14 actual-command cases** and **32 new Document facts**
pass. Every prior stderr byte remains after stripping only those additions and their fixed summary.
The per-profile Document totals are:

| Profile | Predecessors | Additions | Total |
| --- | ---: | ---: | ---: |
| Release | 24,003 | 32 | 24,035 |
| Debug | 24,009 | 32 | 24,041 |
| ASan Debug | 24,003 | 32 | 24,035 |

All Host, Composition and I/O predecessors, four correctness-only smoke reports, crash probes and harness
captures remain required and pass. None of these receipts establishes frame/Apply/G1 timing.
Archives/readers remain under `Z:/src/RedPrism/.build/P1Evidence/p1-loaded-layer-selection-r1/` and
`Z:/src/RedPrism/.build/P1Drafts/LoadedSelectionQualification-Main-r1/`.
The failed original Release ISA attempt and its narrower ARM64 candidate remain preserved above.
The preceding hosted c805d5d native run is a different source and does not qualify this component.

## Publication

The qualified component is committed and pushed as `f4c712c21c0cfcacdeaf16db9e1e225662bd38a9` on
`codex/p1-opacity-host-proof` in `RedSalamanders/RedPrism`. The exact destination SHA is verified.
Final `format.ps1 -Check`, `validate-skills.ps1` and staged whitespace checks pass; every frozen
non-Markdown qualification input remains unchanged. This publication closes this component only.
The remaining caller grid, full G1 and parent P1 Done gates stay open.
