# Active plan index

Files in this directory are non-normative execution or decision records. Current requirements remain in the
owning domain spec under `Specs/<Domain>/` once those contracts exist. Domain contracts created with C0/C1 live under `Specs/<Domain>/`. This index remains routing only.

Add every direct WIP plan to this table exactly once. Use `ACTIVE` for executable work, `HOLD` for paused work,
`DECISION` when product intent is unresolved, and `RETIRED` for a bounded tombstone.

| Plan | Status | Owning domain spec | Purpose |
| --- | --- | --- | --- |
| [`WindowsDirectXPort_2026-09-20.md`](WindowsDirectXPort_2026-09-20.md) | `ACTIVE` | [`Build_Toolchain`](../../Build/Build_Toolchain.md), [`Core_Architecture`](../../Core/Core_Architecture.md), [`Core_PerformanceAndResources`](../../Core/Core_PerformanceAndResources.md), [`Core_DxUiIntegration`](../../Core/Core_DxUiIntegration.md), [`UI_Windowing`](../../UI/UI_Windowing.md), [`Testing_Validation`](../../Testing/Testing_Validation.md); later Document/Rendering/IO | Port the Windows raster editor under archived behavioral reference to a Windows-native C++ Direct3D 11 application hosted with DxUi. |

On completion of a plan, merge durable requirements into the owning domain spec, move the plan to
`Specs/Plans/Done/`, and remove its row here. Child phase plans spawned from the master program must be
indexed here before they authorize implementation.
