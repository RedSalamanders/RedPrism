---
name: build-redprism
description: Build, clean, run, or smoke-test the RedPrism Visual Studio solution. Use for compilation failures, output locations, build configurations, or validating a change; do not use for graphics implementation decisions.
---

# Build RedPrism

Read `Specs/Build/Build_Toolchain.md` and the validation section of the owning domain spec before choosing the
final build matrix.

Use the repository entrypoints instead of assembling ad hoc MSBuild commands:

```powershell
.\vcpkg-install.ps1 -Platform x64
.\build.ps1
.\build.ps1 -Configuration Release
.\build.ps1 -Configuration "ASan Debug"
.\build.ps1 -Platform ARM64
.\build.ps1 -Rebuild
.\test.ps1
.\package.ps1 -Platform x64
```

Outputs are always `.build/<Platform>/<Configuration>/`. Intermediates are under `.build/Intermediate/`.
Every x64 build scans all five linked images for misplaced VEX/EVEX instructions and compiles passing/failing
controls. It requires `llvm-objdump` and `llvm-pdbutil` (VS C++ Clang tools) plus matching full PDBs. Keep tier TUs
at default `/arch`; runtime exceptions belong only in the reviewed `Build/IsaRuntimeAllowlist.psd1`.
Pinned vcpkg state stays beneath `.build/`. Do not substitute a developer-global install. vcpkg builds with the Visual Studio
installation and default MSVC toolset of the MSBuild `build.ps1` found (`vcpkg-install.ps1 -MSBuildPath`), never the newest
toolset it finds, through pinned copies of every triplet in `.build/vcpkg-triplets/`; see `Specs/Build/Build_Toolchain.md`.

`package.ps1` builds Release and stages the portable ZIP (`.build/packages/`, never committed) with the app-local
Visual C++ CRT and the vcpkg DLLs the import table names, then proves a clean extraction runs `--help` and
`--self-test --warp` with a system-only `Path`. It never packages Debug (`Specs/Build/Build_Packaging.md`).

`build.ps1` discovers Visual Studio, including prerelease instances. Do not hardcode a developer installation path.
It begins with the framed RedPrism pixel-art banner owned by `Build/BuildPresentation.psm1`.

Every build, clean, and rebuild preflights the selected `RedPrism.exe` output. A running process whose normalized
path exactly matches that output blocks the build with diagnostics and is never terminated.

When diagnosing a failure:

1. For dependency failures, run `vcpkg-install.ps1` for the exact platform.
2. Re-run `build.ps1` with the failing configuration and platform.
3. Fix the first project-code diagnostic; warnings are errors.
4. Run `test.ps1` after a successful host-architecture build. ARM64 runtime tests require a native ARM64 host.
5. Build Debug, Release, and ASan Debug when changing project properties. ASan Debug tests must produce a real
   AddressSanitizer diagnostic from the isolated probe.

DxUi restore is C2 work. Do not add an unpinned sibling checkout to the build. A normal build asks DxUi whether
`main` is newer than the lock and prints that advisory without changing the pin. The restore is a sparse checkout with Git
long paths (no `Measurements/`, `docs/gallery` or `Specs/`); if `.build/dependencies/DxUi/source/<commit>` is damaged,
delete that directory and restore again.
