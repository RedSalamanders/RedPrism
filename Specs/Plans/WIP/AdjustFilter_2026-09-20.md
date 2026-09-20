# C12 Adjust and filter

> Spawned from `Specs/Plans/Done/WindowsDirectXPort_2026-09-20.md`. Index before coding.

- **Status**: ACTIVE (sequel; not started)
- **Depends on**: C6, C4 adjustment records
- **Owning specs**: `Rendering/Rendering_Composition.md`, `Document/Document_Model.md`

## Outcome

Apply adjustment layers that C6 currently pass-through: Levels (Auto), Curves, Hue/Saturation, Exposure, Gradient
Map, Grain. Filters: Invert, Gaussian Blur, Motion Blur, Add Noise, Lens Correction. Live preview; selection-limited
when a selection exists. HSL blend modes (Hue/Saturation/Color/Luminosity) that currently pass source color.

## Remaining work

- [ ] GPU or tiled CPU preview that matches legacy goldens
- [ ] Commit to layer pixels or keep live adjustment records as in v7
- [ ] Spec the preview vs bake contract
- [ ] Tests from retained numeric golden cases adjustment/filter cases

## STOP

Changing `.prism` to v8 without a spec; “improving” blend math without goldens.

## Done

Checklist green, composition spec updated, plan moved to Done.
