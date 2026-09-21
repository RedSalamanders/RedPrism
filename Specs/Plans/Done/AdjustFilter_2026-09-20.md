# C12 Adjust and filter

> Spawned from `Specs/Plans/Done/WindowsDirectXPort_2026-09-20.md`. Index before coding.

- **Status**: DONE
- **Depends on**: C6, C4 adjustment records
- **Owning specs**: `Rendering/Rendering_Composition.md`, `Document/Document_Model.md`

## Outcome

Apply adjustment layers that C6 currently pass-through: Levels (Auto), Curves, Hue/Saturation, Exposure, Gradient
Map, Grain. Filters: Invert, Gaussian Blur, Motion Blur, Add Noise, Lens Correction. Live preview; selection-limited
when a selection exists. HSL blend modes (Hue/Saturation/Color/Luminosity) that currently pass source color.

## Remaining work

- [x] GPU or tiled CPU preview that matches legacy goldens
- [x] Commit to layer pixels or keep live adjustment records as in v7
- [x] Spec the preview vs bake contract
- [x] Tests from retained numeric golden cases adjustment/filter cases

## STOP

Changing `.prism` to v8 without a spec; “improving” blend math without goldens.

## Done

Checklist green, composition spec updated, plan moved to Done.

## Closeout (2026-09-21)

Shipped: `Common/Adjust.*` ports the legacy kernels number for number (levels tables and histogram, Hermite curves,
the exposure table, the gradient map, grain, the Hue/Saturation cube and band math, noise, lens correction) plus an
own even-streak motion blur and the PDF HSL blends; `Common/AdjustEdit.cpp` holds the panel sessions (filters with
grown blur grids, Levels with its histogram, auto modes and eyedroppers, Hue/Saturation with its eyedroppers and
targeted drag, adjustment layers edited over the composite beneath them, the blend-mode step) with synchronous
previews shown through `DisplayDocument`; the composition applies adjustment layers live through passes 6 and 7
(tables, the color cube and procedural grain, with the layer's blend mode, opacity, mask and folder coverage, clipped
ones inside the stack) and blends Hue, Saturation, Color and Luminosity; the host adds the Image and Filter menus,
the Layer menu's adjustment submenu and Edit Adjustment, Ctrl+M / Ctrl+L / Ctrl+U, Shift+= / Shift+-, the floating
adjustment panel with its drawn curve, levels, spectrum and ramp controls, the eyedroppers and targeted drag on the
canvas, and `--demo adjust`. The `.prism` format stays at v7; adjustment records already round-tripped.

Deviations from the legacy app, recorded in the owning specs: previews render synchronously on the CPU from a copy
no larger than 4096 px (tables, cube) or 2048 px (blurs, lens) instead of the legacy asynchronous 8000 / 2048 px
previews; Motion Blur is an even streak sampled up to 96 times rather than Core Image's Gaussian-tapered one;
Gaussian Blur follows the legacy implementation (the layer grows and its border softens into the room), not the
legacy test's assertion of an unfaded border, and the ported test says so; the panels are one floating card with
Enter / Escape instead of modal sheets, a tool switch applies the open one, and an adjustment layer is edited from
the Layer menu (`Edit Adjustment…`) rather than a double-click in the layers panel; the Levels histogram is ready
at once (no loading state); Remove Background and Content-Aware Fill stay in C13. Test mains now report Debug
assertions on stderr and abort instead of waiting on a dialog, and flush every verdict. Gates: `test.ps1` Debug,
Release and ASan Debug, `build.ps1 -Platform ARM64 -Configuration Debug`, `format.ps1 -Check`,
`validate-skills.ps1`.
