# C9 Selection

> Spawned from `Specs/Plans/Done/WindowsDirectXPort_2026-09-20.md`. Durable behavior lives in
> `Specs/Document/Document_Tools.md` (selection model, session and wand) and `Specs/UI/UI_ApplicationChrome.md`
> (selection tools, header, menu and keys).

- **Status**: DONE (2026-09-21)
- **Depends on**: C6 composition, C8 transform (pending-edit rules, overlay)
- **Owning specs**: `Document/Document_Tools.md`, `Document/Document_Model.md` (`DocumentSelection`),
  `UI/UI_ApplicationChrome.md`

## Outcome

Marquee (rectangle/ellipse), freehand/polygonal lasso, magic wand (`WandPixels` kernel), add/subtract, move
outline, expand/contract, inverse, load layer/mask as selection. Selection is session-only (in the document for
history, never saved).

## Remaining work

- [x] Port wand numeric goldens from the C kernel / Swift tests (DocumentTests `wand.*`, `wandsession.*`)
- [x] Outline move and boolean ops (Clipper2 from vcpkg; DocumentTests `geometry.*`, `coverage.*`, `selection.*`,
      `move.*`, `modify.*`, `marquee.*`)
- [x] Load layer/mask as selection (`MaskTracing`; DocumentTests `load.*`)
- [x] Spec + `Tests/` goldens; no UI-driver scripts (the WARP self-test drives `Application` directly)

## STOP

CPU compositing into a DIB; starting C10 clipped paint before wand goldens exist.

## Done

Checklist green, spec updated, plan moved to Done.

## Closeout (2026-09-21)

Shipped on Clipper2 2.0.1 (vcpkg, static library) for the boolean operations and round offsets, with an own
exact-area coverage rasterizer replacing Core Graphics fills, and the legacy `WandPixels.c` flood fill and
pixel-edge tracer ported to `Common/Selection.cpp`. Deviations from the legacy app, recorded in
`Document_Tools.md` and `UI_ApplicationChrome.md`: mac Option maps to Alt (subtract) and Command to Ctrl; the
selection cursor is the system crosshair (four-way arrow over a movable outline) without the add/subtract badges;
marching ants are static (no animation timer yet); the Magic Wand runs on the UI thread (the legacy detached task
is not needed for the canvas sizes covered by the tests); "All Layers" sampling reads the composition's flattened
readback rather than a CPU composite; Cmd-drag pixel moves, Cmd-arrow pixel nudges, Delete-clears-pixels, fills
and the floating selection transform wait on C10 (they need raster edits). Gates: `test.ps1` Debug, Release and
ASan Debug, `build.ps1 -Platform ARM64 -Configuration Debug`, `format.ps1`, `validate-skills.ps1`.
