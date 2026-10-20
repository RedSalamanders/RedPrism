# Display authority and ordinary cursor integration

Status: corrected integration passes all three native x64 profiles, all three ARM64 cross-builds and canonical Release analysis on the same source freeze. Parent P1 remains WIP; this evidence does not close G1 or qualify P1 Done.

## Preserved first native failure

Full canonical x64 Release m1r1 compiled and linked, passed all five linked-image ISA scans and build controls, and completed Host, Document, composition and I/O suites. Application then failed its two original application-shutdown.before/after.explicit-overlay-selection-before-shutdown assertions. The sixteen new display-message controls passed. Full Application completion, subsequent smoke and captures were not qualified.

The real-message fixture sent DPI/display messages after preparing the special shutdown overlay. Normal chrome synchronization refreshes that overlay from the document. The correction sends those messages before constructing the shutdown draft/overlay and leaves the original overlay assertions intact. No product pixel, history or display handler was changed to accommodate this fixture. The retained original attempt has actual parent 33124 / exit 1, source bookends, 1,884 frozen inputs and 3,208 independently verified files. Archive manifest SHA256: 689D82389860CF8D7697AF4F636A876DD30079C233338FA88154EA84E7E890E7. Source-freeze SHA256: 3709820380E19720C851E35C9E3EA8E57C92193F58EFACF43F37BBC16BA51078.

## Corrected integration: common source identity

The Release m1r2 and real-ASan m1a1 profile receipts both identify source freeze SHA256 A9390781134537A6CFE7341F67D4CD9824A0EA1D35DDD80926BB898D2C92CFED, working-delta SHA256 72A4CA4484B8F0E98BCE91EF885FEE83D23379678C1E10D60C7759E100EA366E, and the same 1,885 frozen source inputs and 1,022 compile records. Each profile receipt verifies all artifact rows and five PE/full-PDB pairs.

| Native profile | Attempt | Profile receipt | Runtime receipt | Exact runtime counts |
| --- | --- | --- | --- | --- |
| x64 Release | m1r2 | pass, exit 0 | pass, exit 0 | Host: 12,524 predecessors + 24 additions; Document: 24,003 + 0; Composition: 8,415 + 0; I/O: 1,953 + 0. Application: 12,233 predecessors + 18 additions = 12,251 facts / 155 stages. |
| x64 real ASan Debug | m1a1 | pass, exit 0 | pass, exit 0 | Host: 12,525 predecessors + 24 additions; Document: 24,003 + 0; Composition: 8,415 + 0; I/O: 1,953 + 0. Application: 12,233 predecessors + 18 additions = 12,251 facts / 155 stages. |

Both runtime receipts report 24 new host facts, 50 existing cursor host facts, two new cursor-startup facts and four correctness-smoke reports. The one-fact difference in Host predecessor counts is retained as recorded by the per-profile receipts; it does not change the stated addition or Application counts. These are the receipt arrays, not inferred suite totals.

The implementation proof covers actual benchmark-window monitor selection and exact rational configured refresh metadata, continuity invalidation from window messages, the shared final endpoint query for normal and cancellation benchmarks, and independent CPU cursor-worker admission during ordinary Run startup. Configured mode metadata is not measured scanout or Present cadence. The startup facts do not qualify visible cursor latency or activate every prepared consumer.

## ASan reader footer refusal and recovery

The m1a1 native run exited 0. Its first reader attempt exited 1 because the reader expected one “All host tests passed” footer while the actual ASan normal suite plus worker stress produced two. The receipt records both the independent prior f4a1 count and current m1a1 count as 2. This is a reader predicate refusal, not a native test failure; the refusal receipt explicitly says no native rerun occurred.

The recovered reader exited 0 against the same archive manifest F4A381F531CE1DE01250853CF9317A527EF0F7882023B8BCDD869F3BA54B450E, with no native rerun. Preserve both receipts and the chronology.

## Complete corrected local qualification

Every profile below uses the same 1,885-input source freeze and 1,022 translation units.
Main independently checks archived source/product bytes, actual commands, five matching PE/full-PDB pairs,
linked-image ISA policy, unchanged predecessors and configuration-specific controls.

| Profile | Attempt / actual parent / exit | Qualified scope | Archived files / manifest SHA256 |
| --- | --- | --- | --- |
| x64 Release | `m1r2` / 78636 / 0 | Full native runtime, correctness smoke and harness capture | 3,221 / `9031147A73BE8CFCFA37773937D537009015D18BA5AC2AFBCB6F2558CA787678` |
| x64 ASan Debug | `m1a1` / 101108 / 0 | Full native runtime, correctness smoke and harness capture | 3,356 / `F4A381F531CE1DE01250853CF9317A527EF0F7882023B8BCDD869F3BA54B450E` |
| ARM64 Debug | `m1armd1` / 95028 / 0 | Cross-build only; no native ARM64 execution | 2,995 / `2BC9F672947D70506CD524200EAFCCA740CED50819A9B235F95D3FC2D94A1134` |
| ARM64 Release | `m1armr1` / 86768 / 0 | Cross-build only; no native ARM64 execution | 2,995 / `C212A3104AB6A846B214174413B060D02934DE06A95CF28299C68D062168870C` |
| ARM64 ASan Debug | `m1arma1` / 92716 / 0 | Cross-build only; no native ARM64 execution | 2,999 / `D32E591437BC381BD0979254D882A3AA2EFD9B025111288C7A83CE5A3A1F3452` |
| x64 Debug | `m1d1` / 74232 / 0 | Full native runtime, correctness smoke and harness capture | 3,220 / `38D19EDD2F54D9FE07B0057702138BF6392A22FAA526458367928CA571C30A78` |
| x64 Release | `m1an2` / 105676 / 0 | Canonical Rebuild/Analyze and positive/negative SAL controls | 3,235 / `CE43DA26E57B4437131ADD1BE7E87D79CA21C1F576E62E8FEB6C739812FD888B` |

All three x64 runtime profiles preserve 12,233 Application predecessors and add exactly 18 facts:
16 actual window-message facts and two ordinary Run cursor-startup facts. All 155 stages pass;
each runtime includes the 50 prior cursor-host facts and both new startup facts. Host adds exactly
24 monitor/rational-refresh/continuity facts to the same-configuration predecessor roster. Numeric
Document, composition and I/O predecessors remain unchanged. Real ASan includes the genuine detection
probe and concurrent raster/worker stress. Both crash probes and all nine harness captures pass.
Canonical Release analysis retains every warning policy and the real negative C26130 control.

## Qualification limits

Local ARM64 rows establish cross-compilation only. No local native ARM64 machine is available.
The separate hosted ARM64 feasibility job tests the earlier committed source plus its two CI files;
it cannot qualify this frozen integration. Its preserved timeout is described below.

Configured monitor metadata is not measured scanout or Present cadence. Monitor continuity and the
shared final benchmark endpoint check prevent a stale display authority from qualifying full timing;
smoke remains explicitly correctness only. The independent CPU cursor startup does not enable the
complete prepared display/stroke/selection route. Complete operation-phase admission, the remaining
P1 delivery areas and measured G1 are still open. No P1 Done or merge qualification follows here.
DxUi, numeric goldens, warning limits and the ISA runtime allowlist are unchanged.


## Preserved hosted ARM64 feasibility timeout

[Run 39403365977](https://github.com/RedSalamanders/RedPrism/actions/runs/39403365977) tested commit
`a10ca32cc410aa78d9ea135cf55522ef8f0f7af2`: the preceding committed C++ baseline plus two CI files.
Its environment confirms native Windows ARM64 on Cobalt 100, two logical processors, 8 GiB RAM,
Visual Studio 2026 and default MSVC 14.51.36231. Canonical discovery selected AMD64 MSBuild.
The child reached compilation but exceeded the 85-minute bound. Its exit remains null, not a fabricated
failure/success code. The helper did not terminate it. No native suite result or final source bookend exists.

Main verifies all 17 downloaded artifact files and the three partial AA64 images (Host, composition and I/O).
Those partial products do not qualify the build, tests or this A939 integration. Captured streams show no
compiler error diagnostic; the timeout's cause is not established by this absence. The first readback's strict
UTF-8 console decoder refusal is retained. Exact ASCII protocol checks over the same unchanged raw bytes
recover that reader without rerunning native work or changing the captured streams.

The next CI candidate selects installed ARM64-hosted MSBuild through the canonical supported override and
retains bounded progress/free-memory evidence. It is a separate followthrough, with no successful result
claimed here. This hosted CPU/WARP environment also establishes no Snapdragon/Adreno R3 performance result.


## Preserved failed analysis and recovery boundary

The original `m1an1` analysis attempt has actual parent 87304, exit 1, matching source bookends, and
source freeze `A9390781134537A6CFE7341F67D4CD9824A0EA1D35DDD80926BB898D2C92CFED`. Compilation,
linking and scans completed before C: ran out of space during the final ISA fixture `WriteAllBytes`.
The failed attempt remains preserved in its 3,235-file archive with
manifest SHA256 `518A601C5B4DEC496AB42E2C8B3F21C45C2592F4D61713757D9377459937EB9D`; the 323-file
failed fixture archive retains backup SHA256
`6006907A109129D4999114FA70C221FECFC2DE12482C85CE5EDAB8B0EF496FC1`.

The same-source `m1an2` retry now passes with actual parent 105676 / exit 0. Main verifies its
3,235-file archive, all 1,022 compiler records, five PE/full-PDB pairs and strict SAL controls. Its archive
manifest SHA256 is `CE43DA26E57B4437131ADD1BE7E87D79CA21C1F576E62E8FEB6C739812FD888B`.

The approved storage transition preserves all 16,814 original fixture folders in the C: sibling
`BuildProcessTests-preserved-m1an1-20261020` and redirects new fixtures to
`Z:\src\RedPrism\.build\P1GeneratedTests-M1AnalysisRecovery-r1`. The actual transition receipt records
`GENERATED_FIXTURES_REDIRECTED_ORIGINALS_RETAINED`, `filesDeleted: 0`, and `sourceCodeChanged: false`.
The original verification, branch/diff, CI-only fast-forward, tested C++ and ISA allowlist guards remain
required for publication. The hosted timeout remains unqualified; G1 and P1 Done remain open.


## Qualified code publication

The code/test/spec step is committed as `76b7af0bea0f5f4a1a2f087baf6b65fa0bb65866` and pushed to `codex/p1-opacity-host-proof`. Main verified the destination ref and unchanged tested C++ bytes after the push. This publication closes this integration step; the opening P1 delivery gates remain open.
