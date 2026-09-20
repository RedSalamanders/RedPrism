# C15 Packaging

> Spawned from `Specs/Plans/Done/WindowsDirectXPort_2026-09-20.md`. Index before coding.

- **Status**: ACTIVE (sequel; not started)
- **Depends on**: C14
- **Owning specs**: `Build/Build_Packaging.md` (create)

## Outcome

Portable ZIP, version stamp (`major.minor` + commit count), later winget. Sparkle-class auto-update is out of v1.

## Remaining work

- [ ] `package.ps1` staging rules, CRT, clean-extraction smoke
- [ ] `Specs/Build/Build_Packaging.md`
- [ ] Version identity already in `Version.h` stays authoritative
- [ ] Winget is a follow-on, not a C15 blocker unless this plan expands

## STOP

Committing `.build/`; killing `RedPrism.exe` to package; silently changing RedXe/RedSalamander pins.

## Done

Checklist green, packaging spec updated, plan moved to Done.
