# C9 Selection

> Spawned from `Specs/Plans/Done/WindowsDirectXPort_2026-09-20.md`. Index before coding.

- **Status**: ACTIVE (sequel; not started)
- **Depends on**: C6
- **Owning specs**: `Document/Document_Tools.md`, `Document/Document_Model.md` (`DocumentSelection`)

## Outcome

Marquee (rect/ellipse), freehand/polygonal lasso, magic wand (`WandPixels`), add/subtract, move outline,
expand/contract, inverse, load layer/mask as selection. Selection is session-only.

## Remaining work

- [ ] Port wand numeric goldens from the C kernel / Swift tests
- [ ] Outline move and boolean ops
- [ ] Load layer/mask as selection
- [ ] Spec + `Tests/` goldens; no UI-driver scripts

## STOP

CPU compositing into a DIB; starting C10 clipped paint before wand goldens exist.

## Done

Checklist green, spec updated, plan moved to Done.
