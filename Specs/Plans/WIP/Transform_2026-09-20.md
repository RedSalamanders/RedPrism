# C8 Transform

> Spawned from `Specs/Plans/Done/WindowsDirectXPort_2026-09-20.md`. Do not start until this row is `ACTIVE` in
> `Specs/Plans/WIP/README.md`. Durable behavior belongs in `Specs/Document/Document_Tools.md` (create with this plan)
> and `Specs/UI/UI_ApplicationChrome.md`.

- **Status**: ACTIVE (sequel; not started)
- **Depends on**: C6 composition, C7 shell
- **Owning specs**: `Document/Document_Tools.md` (create), `UI/UI_ApplicationChrome.md`, `Rendering/Rendering_Composition.md`

## Outcome

Move, scale, rotate, flip, free distort, multi-layer/folder transform, snapping + guides, numeric inspector,
Flip Layer / Flip Canvas. Session-only; not serialized beyond layer transforms already in `.prism`.

## Remaining work

- [ ] Pointer and keyboard transform of the active layer / selection using `LayerTransform`
- [ ] Snap to canvas, guides, and other layer bounds
- [ ] Numeric inspector: TextField + arrow nudge (C3 NumericStepper still HOLD)
- [ ] Flip Layer / Flip Canvas commands
- [ ] Spec `Document_Tools.md` + focused tests from retained numeric golden cases transform cases
- [ ] Harness screenshot of a transformed layer (application capture, not Computer Use)

## STOP

Second swap chain; GDI handles; implementing DxUi Splitter/NumericStepper in RedPrism; starting C9 from this file.

## Done

All checklist items green, spec updated, plan moved to `Specs/Plans/Done/`.
