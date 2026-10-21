# Polygonal live-draft export qualification — 2026-10-17

Scope: selected gated drawing caller, actual PNG/JPEG export and picker cancellation. P1 stays WIP.

## Code and references

`Application.Tests.PolygonalPress.cpp` adds `png-open`, `jpeg-open`, `png-cancel` and `jpeg-cancel` in each
New/Add/Subtract mode: twelve new cases and 186 assertions. All prior 45 cases / 527 assertions remain
in their source-defined order, and joined Close stays last. The complete fixture reaches 57 cases / 713
facts; the full application reaches 152 stages / 10296 stdout assertions. Main independently verifies
all predecessor stdout outside this fixture and its complete stderr through the immutable 151-stage reader.

The fixture deliberately starts export from a dirty saved marker, retains a prior project path and checks
both after actual host completion. This catches an incorrect MarkSaved that a clean fixture would miss.
Success/cancellation retain the original admitted drawing ticket, three-corner draft, committed selection
and history revision. Cancellation creates no file. Actual decoded 64×64 PNG pixels are all zero BGRA;
JPEG is opaque white within the existing `jpeg.flatten-white-matte` 6/255 tolerance. Mode-specific output
directories keep each file exclusive. Later corners/Enter, the independent 4096-center geometry reference,
one history entry, Undo/Redo and final strong/weak CPU/GPU refunds remain required.

Production C++ and existing legacy goldens are unchanged. No DxUi pin or ISA allowlist change is made.

## Actual native evidence

| Profile | Actual PID / exit | Recompiled / exact reused objects | Build archive files | Runtime archive files | Runtime completion SHA256 |
| --- | --- | --- | --- | --- | --- |
| Release | 105176 / 0 | 1 / 348 | 28 | 21 | `E1547473154956F69EAF162852425BDDB0B6112BEFB2231A522D8A4BF932D016` |
| ASanDebug | 28892 / 0 | 1 / 348 | 27 | 22 | `968696062F87467A90D749AFEAB5E375324AC346800F0E28DA9DE53B821E9E8B` |

- Release complete build archive: `61AB9C50F568E15CF3218D979267CF1073EA9A0A782B227533927E6C391B0FB9`.
- ASanDebug complete build archive: `2230225A9763B47ACF027EEBFB6077B38DB671F8D555CC1F80E3BD178987CA69`.


Both profiles compile the fixture with the original exact flags, link it with the 348 byte-identical
qualified objects and require matching x64 PE/full-PDB identity plus zero ISA violations. ASan retains
real instrumentation in all 349 application objects. Actual MSBuild object spelling/order, SDK paths,
response-file arguments and private output flags (including `/LTCGOUT`) are preserved. Complete source,
dependency, object, product and profile bookends and every build/runtime archive byte are verified.
The previously qualified 23454-fact CPU artifacts and genuine UAF diagnostic are reused only through
the already verified unchanged source/dependency/artifact/profile/scope mapping and identical ASan DLLs;
this is distinct from the two fresh complete application runs.

## Reader review and controls

Main reviews the cheaper agent's source-only reader and corrects its stage composition before actual
readback: removing the new fixture produces 151 stages, while the old 527-fact reader itself requires
152. Main verifies that old reader's exact source pins/527-row roster and calls its pinned 151-stage
predecessor directly. The new fixture bytes are pinned, assertion labels must be unique, and removing
only the twelve new export cases must reproduce all 527 old rows exactly.

The first Main full-chain control refuses stale predecessor hashes carried by the agent from R3 into
the R4 path. That reader draft/refusal is retained; Main independently checks the existing R4 review,
reader/expected/pins identities before correcting them in a new R2 reader. No application is rerun for
this control correction. The resulting reader passes four byte-preservation positives, two complete
inherited-chain profile positives and 21 negative controls, including missing export cases/fact groups,
wrong format checks, reordered cases, wrong summary, duplicate receipts, nonzero actual exit, missing raw
predecessor stderr and a changed predecessor fact with the same total count. New expectations come from
source and independent references; synthetic new fixture blocks are used only for reader controls.

## Retained proof and scope limits

Local recipes/source/build/runtime qualifications/adoption hashes are under
`.build/P1Drafts/PolygonalExportNative-Main-r1/`; Main-reviewed reader/control evidence is under
`.build/P1Drafts/PolygonalExportReader-Main-r2/`. Original agent and Main R1 reader drafts remain intact.
Complete artifacts/archives live under `D:/RedPrismBuildCaches/.build/p1-polygonal-export-{release,asan}-r1`.

The [earlier inherited held-gate ASan exit 5](P1PolygonalPhysicalHost_2026-10-17.md) remains an explicit
final qualification audit item; these passing runs do not prove a causal fix. New/nonempty/Ctrl-lift
classification, complete view/device/geometry scope, final full native/analysis/ARM64 gates, ordinary
activation and measured G1 remain open. Existing ARM64 receipts qualify earlier source; native ARM64
runtime is unavailable. No RAW/Photoshop or latency claim is made.
