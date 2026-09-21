# Active plan index

Files in this directory are non-normative execution or decision records. Current requirements remain in the
owning domain spec under `Specs/<Domain>/` once those contracts exist. Domain contracts created with C0–C7 live under
`Specs/<Domain>/`. This index remains routing only.

Add every direct WIP plan to this table exactly once. Use `ACTIVE` for executable work, `HOLD` for paused work,
`DECISION` when product intent is unresolved, and `RETIRED` for a bounded tombstone.

The Windows DirectX port master program is historical:
[`WindowsDirectXPort_2026-09-20.md`](../Done/WindowsDirectXPort_2026-09-20.md). The C0–C7 review and the DxUi editor-control adoption are
recorded in [`PortReview_2026-09-21.md`](../Done/PortReview_2026-09-21.md). C8 Transform is recorded in
[`Transform_2026-09-20.md`](../Done/Transform_2026-09-20.md), C9 Selection in
[`Selection_2026-09-20.md`](../Done/Selection_2026-09-20.md) and C10 Brush in
[`Brush_2026-09-20.md`](../Done/Brush_2026-09-20.md).

| Plan | Status | Owning domain spec | Purpose |
| --- | --- | --- | --- |
| [`Retouch_2026-09-20.md`](Retouch_2026-09-20.md) | `ACTIVE` | [`Document_Tools`](../../Document/Document_Tools.md) | Clone, heal, smear, gradient, shape, eyedropper |
| [`AdjustFilter_2026-09-20.md`](AdjustFilter_2026-09-20.md) | `ACTIVE` | [`Rendering_Composition`](../../Rendering/Rendering_Composition.md) | Adjustment layers and filters; HSL blends |
| [`SmartOps_2026-09-20.md`](SmartOps_2026-09-20.md) | `ACTIVE` | [`Document_Tools`](../../Document/Document_Tools.md) | Content-aware fill; Remove Background decision |
| [`ProductHardening_2026-09-20.md`](ProductHardening_2026-09-20.md) | `ACTIVE` | settings/crash specs (create in C14); [`UI_ApplicationChrome`](../../UI/UI_ApplicationChrome.md) | Settings, minidumps, localization, UIA |
| [`Packaging_2026-09-20.md`](Packaging_2026-09-20.md) | `ACTIVE` | `Build_Packaging` (create in C15) | Portable ZIP and version stamp |

On completion of a plan, merge durable requirements into the owning domain spec, move the plan to
`Specs/Plans/Done/`, and remove its row here. Child phase plans spawned from the master program must be
indexed here before they authorize implementation.
