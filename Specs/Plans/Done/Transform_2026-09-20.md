# C8 Transform

> Spawned from `Specs/Plans/Done/WindowsDirectXPort_2026-09-20.md`. Do not start until this row is `ACTIVE` in
> `Specs/Plans/WIP/README.md`. Durable behavior belongs in `Specs/Document/Document_Tools.md` (create with this plan)
> and `Specs/UI/UI_ApplicationChrome.md`.

- **Status**: DONE (2026-09-21)
- **Depends on**: C6 composition, C7 shell
- **Owning specs**: `Document/Document_Tools.md` (create), `UI/UI_ApplicationChrome.md`, `Rendering/Rendering_Composition.md`

## Outcome

Move, scale, rotate, flip, free distort, multi-layer/folder transform, snapping + guides, numeric inspector,
Flip Layer / Flip Canvas. Session-only; not serialized beyond layer transforms already in `.prism`.

## Remaining work

- [x] Pointer and keyboard transform of the active layer / selection using `LayerTransform` (drag modes, group and mask edits, duplicate, nudge)
- [x] Snap to canvas, guides, and other layer bounds (accent guides drawn by `CanvasOverlay`)
- [x] Numeric inspector: `DxUi::NumericStepper` header (the C3 fallback is retired; the library shipped in `7e3be94`)
- [x] Flip Layer / Flip Canvas commands (Layer and Image menus)
- [x] Spec `Document_Tools.md` + focused tests from retained numeric golden cases transform cases (DocumentTests `transform.*`, `drag.*`, `snap.*`, `duplicate.*`, `group.*`, `mask.*`, `flip.*`, `distort.*`; HostTests `overlay.*`)
- [x] Harness screenshot of a transformed layer (`--screenshot --demo transform`, `docs/screenshots/transform-96.png`)

## STOP

Second swap chain; GDI handles; implementing DxUi Splitter/NumericStepper in RedPrism; starting C9 from this file.

## Done

All checklist items green, spec updated, plan moved to `Specs/Plans/Done/`.

## Closeout (2026-09-21)

Shipped on the corrected composition and the DxUi editor controls. Deviations from the legacy app, recorded in
`Document_Tools.md` and `UI_ApplicationChrome.md`: header numbers for X/Y/W/H are whole pixels (the stepper formats
a fixed number of decimals); mac Command maps to Ctrl (pick a layer, distort a handle), Option to Alt (duplicate,
resize about the center), and Ctrl during a move drags without snapping; Undo during a pending edit cancels it.
Gates: `test.ps1` Debug, Release and ASan Debug, `build.ps1 -Platform ARM64 -Configuration Debug`, `format.ps1`,
`validate-skills.ps1`.
