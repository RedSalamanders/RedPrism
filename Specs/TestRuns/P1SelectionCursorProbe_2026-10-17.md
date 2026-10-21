# P1 cursor geometry capture prerequisite — 2026-10-17

## Scope and correction

Seven code/test paths add a cached scalar plan and an exact post-admission immutable geometry capture.
The plan owns no geometry/history root. The shared result keeps the original root plus scalar offset;
existing worker containment adds the offset per vertex to preserve large-coordinate rounding.
Three domain contracts describe authority, bounds and validation. The Application worker remains private;
no ordinary routing, latency, G1 or latest-source native-matrix qualification follows. P1 remains WIP.

The first full Release runtime (PID 74588, exit 1) retained two new fixture failures: `ImageLayer::Blank`
has no raster asset, so it correctly cannot begin a transform. The corrected transform-only fixture
constructs a real asset. Production behavior, numeric expectations, labels and all predecessors remain
unchanged. The failed 14-file runtime archive is retained at
`.build/P1Drafts/S5SelectionCursorCpuRelease-r1-Main/Storage/RuntimeArchive/`.

## Exact successful scope

Each profile compiles 280 actual project TUs, with zero warnings/errors and no ISA violations.
All 33 new facts and all 23,006 unchanged predecessors pass, in exact order. The source-only reader
retains 4 positive and 22 refusal controls and delegates every predecessor byte unchanged. Main reviewed
and reran the complete independent source/product/archive and actual-roster checks. Archives have exact
recipe-derived membership, unique contained paths, matching sizes/SHA-256, and matching full PE/PDBs.
The new facts cover empty/ineligible sources, literal geometry/edge results, tampering, session/reopen/
generation authority, retained-root survival, deferred and huge-coordinate offsets, rounding collapse,
and a 65,540-point source. Three static traits constrain ownership.

| Profile | Actual runtime PID | Exit | Assertions | Build archive | Runtime archive |
| --- | ---: | ---: | ---: | ---: | ---: |
| Release | 50744 | 0 | 23,039 | 896 | 19 |
| ASan Debug | 69296 | 0 | 23,039 | 901 | 23 |

The real ASan probe (PID 28996, exit 1) emits `AddressSanitizer: heap-use-after-free` from the exact
linked image. All 280 ASan compiler records require instrumentation, the guard header, `/MDd`, `/Od`
and `/Zi`. Complete successful runtime stderr is empty in both profiles.

## Local receipts

- Release: build completion `71437ABEAE1B5409100310E77D63F206FB1A837BA9036409533C82677EF7A074`; runtime completion `8CF6C8CA302F55227D342A4C41989090CBF4D618DCE84D611E8E0B3694F98BB9`; independent report `E510E30DEC45F5EC64049F56762C412DFDCE10A2D9B9D91809C9887C0F5543A4`.
- ASan Debug: build completion `9EA7FB0A6B5F16A0E35B64153975800A86A77C96D572D956DFF4A02BF5BCD716`; runtime completion `6CF1860514A88C62320EA1EF43C516F3E73D96A0F063576426B4DBD682D2CD73`; independent report `76F89F323030E5173693B6765149DFA96AC1FE9A48CFCC64B39EB5EF4EF3F8DA`.

Complete recipes, records and independently rerun verifiers are under
`.build/P1Drafts/S5SelectionCursorCpuRelease-r2-Main/` and
`.build/P1Drafts/S5SelectionCursorCpuAsan-r1-Main/`; storage roots are recorded in their receipts.
The ASan build journal retains a copied uppercase `RELEASE` descriptive literal. Its actual argv,
all instrumented command records, ASan product path, ISA target and real UAF control establish the
actual profile; the metadata follow-through is retained without rewriting the original journal.

## Remaining work

The actual cursor consumer, complete HostTests/native profiles, native ARM64 runtime availability,
ordinary activation and measured G1 remain separate gates. Older `6a7afac` native receipts cannot
qualify the new source. No private focused or scoped run substitutes for those obligations.
