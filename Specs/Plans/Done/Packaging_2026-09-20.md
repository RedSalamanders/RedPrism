# C15 Packaging

> Spawned from `Specs/Plans/Done/WindowsDirectXPort_2026-09-20.md`. Index before coding.

- **Status**: DONE
- **Depends on**: C14
- **Owning specs**: `Build/Build_Packaging.md`

## Outcome

Portable ZIP, version stamp (`major.minor` + commit count), later winget. Sparkle-class auto-update is out of v1.

## Remaining work

- [x] `package.ps1` staging rules, CRT, clean-extraction smoke
- [x] `Specs/Build/Build_Packaging.md`
- [x] Version identity already in `Version.h` stays authoritative
- [x] Winget is a follow-on, not a C15 blocker unless this plan expands

## STOP

Committing `.build/`; killing `RedPrism.exe` to package; silently changing RedXe/RedSalamander pins.

## Done

Checklist green, packaging spec updated, plan moved to Done.

## Closeout (2026-09-21)

Shipped: `package.ps1` builds Release through `build.ps1` (the same preflight; a running `RedPrism.exe` blocks
and is never terminated), reads the version from the executable's identity (`0.1.<commit count>`, `Version.h`
untouched), stages `RedPrism-<version>-<platform>\` with the executable, the DLLs its import table names (walked
with `dumpbin`: vcpkg's `yyjson.dll`, `zip.dll` and `z.dll`, and the app-local Visual C++ CRT `vcruntime140.dll`,
`vcruntime140_1.dll`, `msvcp140.dll` and `msvcp140_atomic_wait.dll` from the installed redistributable), the vcpkg
copyright notices for yyjson, libzip, zlib, Clipper2 and WIL, and the two docs; zips it under `.build\packages`
with a `.sha256`; and proves a clean extraction with a system-only `Path` (`--help`, `--self-test --warp`, the
packaged version identity). `Specs/Build/Build_Packaging.md` is the contract; the toolchain spec's entry points,
the validation gates, the docs and the build-redprism skill point at it. Run at this closeout:
`RedPrism-0.1.33-x64.zip` (1593 KB, smoke passed) and `RedPrism-0.1.33-ARM64.zip` (1891 KB, smoke skipped on the
x64 host); `validate-skills.ps1` passed.

Decisions: a package is always Release (the Debug CRT is not redistributable); the Universal CRT is not staged
(Windows 10 and later carry it); the executable is unsigned in v1 (no certificate) and the `.sha256` beside the
download is the integrity check; winget is a follow-on that needs a public download URL and changes nothing in
the package; MSIX or an installer only if `.prism` associations or Start-menu integration are wanted; no
auto-update (the legacy Sparkle stays out of v1 by decision). No pins changed; `.build/` stays uncommitted.
