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
[`Selection_2026-09-20.md`](../Done/Selection_2026-09-20.md), C10 Brush in
[`Brush_2026-09-20.md`](../Done/Brush_2026-09-20.md), C11 Retouch in
[`Retouch_2026-09-20.md`](../Done/Retouch_2026-09-20.md), C9b Pixel edits in
[`PixelEdits_2026-09-21.md`](../Done/PixelEdits_2026-09-21.md), C12 Adjust and filter in
[`AdjustFilter_2026-09-20.md`](../Done/AdjustFilter_2026-09-20.md) C13 Smart operations (Remove Background
rejected for v1) in [`SmartOps_2026-09-20.md`](../Done/SmartOps_2026-09-20.md) and C14 Product hardening in
[`ProductHardening_2026-09-20.md`](../Done/ProductHardening_2026-09-20.md).

| Plan | Status | Owning domain spec | Purpose |
| --- | --- | --- | --- |
| [`Packaging_2026-09-20.md`](Packaging_2026-09-20.md) | `ACTIVE` | `Build_Packaging` (create in C15) | Portable ZIP and version stamp |

On completion of a plan, merge durable requirements into the owning domain spec, move the plan to
`Specs/Plans/Done/`, and remove its row here. Child phase plans spawned from the master program must be
indexed here before they authorize implementation.
