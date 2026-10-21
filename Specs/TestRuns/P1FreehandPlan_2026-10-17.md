# P1 Freehand admission prerequisites — private CPU scope, 2026-10-17

## Boundary

Main implements and qualifies the proposed Freehand down-time geometry resource plan in a private source
snapshot. The root application does not yet adopt these APIs or enable the Freehand physical parent.
Parent/host integration, its complete phase bounds, ordinary activation and G1 remain due. P1 is WIP.

## Implemented and checked

`FreehandLimits` and `PlanFreehand` independently bound a complete gesture to 65,536 accepted vertices:
1,023 immutable sealed 64-point blocks plus the inline tail. The 512-event ring bounds pending input;
it does not bound total gesture length. The plan includes the full arena, traversal, outline, bounded
boolean scratch, output/control owners and construction overlap. New excludes old geometry from its
boolean input; preserving modes include cached source counts. Arithmetic and cap refusal happen before
source capture. These are enforced resource ceilings, not a guarantee that every complex path fits.

The 75 new CPU facts cover tabulated New/Add/Subtract limits and refusal boundaries, multi-block traversal,
exact 65,536/65,537 capacity, retained snapshot stability, cancellation at all seven geometry stages,
mid-copy cancellation, actual bounded-engine refusals and independent literal geometry/coverage.
The two 6×4 rectangles overlap over 3×3, giving replacement/union/subtraction areas 24/39/15;
all 96 pixel centers match an independent coordinate predicate. Nine additional input facts preserve
Down-time kind/mode/point/modifiers/AA across header changes and physical release AA for all three modes.

Main reviews the delegated tests and routes all three deliberate engine-allocation failures through the
real scheduler. The checks require `E_OUTOFMEMORY`, no partial result, completion and final memory refund.
The source has no new direct allocation-failure catch in that fixture. Existing expectations remain intact.

## Exact successful runs

| Profile | Actual runtime | Complete checks | Archive completion SHA-256 |
| --- | --- | --- | --- |
| Release | PID 38292, exit 0; 12:31:51–12:32:02 UTC | All 23,123 assertions, including every 23,039 predecessor; 281 compiler records/objects; 894 build and 20 original runtime archive files | Build `A503E7E5C81D6464365A174B6E563D4FE47E79B665E55EAFFD017D540CD0D220`; runtime `B2037D7443794A0DBA81490F5BAA0DF7E2BC1D91B3800580ACE79019A63BB4B7` |
| Real ASan | PID 28880, exit 0; 12:53:44–12:57:13 UTC | All 23,123 assertions, 281 actual instrumented compiler records/objects, 899 build and 25 runtime archive files; its own genuine UAF detection | Build `BBEFBB54A9E138E354AE36A2AE4371380F2BC2D33F5A62D65F9E104D63CDDDA1`; runtime `2C1AAD2E155767D0404FD1340565125E2649E3F7CAF094457DACDB61C8143818` |

Main independently verifies complete archive inventories and hashes, raw compiler commands, all actual
owned compiler dependencies, source freezes, exact full executable/PDB identity and zero ISA violations.
ASan's actual `/MDd`, `/Od`, `/Zi`, `/fsanitize=address` and forced include belong to the private source root.
Probe PID 80596 exits 1 with a genuine heap-use-after-free diagnostic, SHA-256
`17620C02684708DE1603BF07B6BBBEF9A0AAC854EE81C6DEBEB4D68E100D6298`.

The source, recipes and Main qualifications are under `.build/P1Drafts/S5Freehand-r1-Main/`,
`S5FreehandCpuRelease-r1-Main/` and `S5FreehandCpuAsan-r1-Main/` in the native checkout.
Their receipts identify physical cache and archive storage. These are scoped CPU results, not native
Application, ARM64 or physical caller qualification for the proposed feature.

## Preserved reader failure

The first Release binary passes every assertion. The original transcript reader (PID 98196, exit 2)
incorrectly expands two `foreach(case)` loops in label-major order. Main first preserves and independently
checks both original archives and every object hash. `DocReader-Main-r2` changes only that expansion to the
case-major order required by the test source; no C++ predicate, label, binary or runtime is changed or replayed.
The complete unchanged predecessor reader chain remains required. Source-only controls include four positive
and 23 negative cases plus seven Main controls; both original erroneous groupings are explicitly refused.
The original failed reader and original archive completion remain unchanged. ASan uses the corrected reader.

## Next component

The separate private host prototype adds a typed captured drawing plan, exact original arena-block count,
prepared-writer authority and a bounded Freehand input continuation. It is uncompiled and unqualified.
The proposed owner metadata bound must be reconciled with the WIP sequence and measured against the existing
phase target; all full-path geometry, history preparation and retirement stay on workers. No phase target is
relaxed by this CPU prerequisite evidence.
