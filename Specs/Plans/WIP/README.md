# Active plan index

Files in this directory are non-normative execution or decision records. Current requirements remain in the
owning domain spec under `Specs/<Domain>/` once those contracts exist. Domain contracts created with C0–C7 live under
`Specs/<Domain>/`. This index remains routing only.

Add every direct WIP plan to this table exactly once. Use `ACTIVE` for executable work, `HOLD` for paused work,
`DECISION` when product intent is unresolved, and `RETIRED` for a bounded tombstone.

The Windows DirectX port master program is historical:
[`WindowsDirectXPort_2026-09-20.md`](../Done/WindowsDirectXPort_2026-09-20.md).

| Plan | Status | Owning domain spec | Purpose |
| --- | --- | --- | --- |
| [`Transform_2026-09-20.md`](Transform_2026-09-20.md) | `ACTIVE` | [`UI_ApplicationChrome`](../../UI/UI_ApplicationChrome.md); `Document_Tools` (create in C8) | Move/scale/rotate/flip/distort, snapping, guides, numeric inspector |
| [`Selection_2026-09-20.md`](Selection_2026-09-20.md) | `ACTIVE` | [`Document_Model`](../../Document/Document_Model.md); `Document_Tools` | Marquee, lasso, wand, boolean ops, outline |
| [`Brush_2026-09-20.md`](Brush_2026-09-20.md) | `ACTIVE` | [`Core_PerformanceAndResources`](../../Core/Core_PerformanceAndResources.md); `Document_Tools` | Compute brush + eraser, RasterSnapshot budgets |
| [`Retouch_2026-09-20.md`](Retouch_2026-09-20.md) | `ACTIVE` | `Document_Tools` | Clone, heal, smear, gradient, shape, eyedropper |
| [`AdjustFilter_2026-09-20.md`](AdjustFilter_2026-09-20.md) | `ACTIVE` | [`Rendering_Composition`](../../Rendering/Rendering_Composition.md) | Adjustment layers and filters; HSL blends |
| [`SmartOps_2026-09-20.md`](SmartOps_2026-09-20.md) | `ACTIVE` | `Document_Tools` | Content-aware fill; Remove Background decision |
| [`ProductHardening_2026-09-20.md`](ProductHardening_2026-09-20.md) | `ACTIVE` | settings/crash specs (create in C14); [`UI_ApplicationChrome`](../../UI/UI_ApplicationChrome.md) | Settings, minidumps, localization, UIA |
| [`Packaging_2026-09-20.md`](Packaging_2026-09-20.md) | `ACTIVE` | `Build_Packaging` (create in C15) | Portable ZIP and version stamp |

On completion of a plan, merge durable requirements into the owning domain spec, move the plan to
`Specs/Plans/Done/`, and remove its row here. Child phase plans spawned from the master program must be
indexed here before they authorize implementation.
