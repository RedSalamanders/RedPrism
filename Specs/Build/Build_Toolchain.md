# RedPrism build toolchain

Status: current normative repository contract
Last reviewed: 2026-09-20
Owner: root MSBuild projects, `vcpkg.json`, `vcpkg-tool.json`, `build.ps1`, `Build/BuildPresentation.psm1`, `vcpkg-install.ps1`, `format.ps1`

## Scope

This contract owns the Windows C++ toolchain, configurations, generated-output layout, vcpkg pin, and the root
build entrypoints. Runtime windowing lives in [`UI/UI_Windowing.md`](../UI/UI_Windowing.md). Architecture ownership
lives in [`Core/Core_Architecture.md`](../Core/Core_Architecture.md). Test orchestration lives in
[`Testing/Testing_Validation.md`](../Testing/Testing_Validation.md).

DxUi restore is C2 work: `restore-dxui.ps1` clones the lock in `Dependencies/DxUi.lock.json` into
`.build/dependencies/DxUi/` and never resets `Z:\src\DxUi`. See [`Core/Core_DxUiIntegration.md`](../Core/Core_DxUiIntegration.md).

## Toolchain

| Item | Required value |
| --- | --- |
| Generator | MSBuild via `RedPrism.sln`. CMake, MinGW, and a second C++ standard are prohibited. |
| IDE / toolset | Visual Studio 2026 (18.x), `v145` |
| Windows SDK | `10.0.26100.0` |
| Language | MSVC `stdcpplatest`, Unicode Win32 APIs |
| Warnings | `/W4`, `/permissive-`, SDL, warnings as errors, `/utf-8`, `/Zc:preprocessor` |
| CRT | `/MDd` Debug and ASan Debug, `/MD` Release |
| Minimum OS | Windows 11 build 22000.2600 |

First-party translation units compile as C++.

## Configurations and platforms

Every native project MUST map Debug, Release, and ASan Debug on x64 and ARM64 (six profiles).

ASan Debug MUST use `/MDd`, disabled optimization, program-database debug information, and real AddressSanitizer
instrumentation. `Common/RequireAddressSanitizer.h` is a forced include so an unsanitized ASan Debug compilation is a
build error.

ARM64 **runtime** claims require a native ARM64 host. Cross-compilation from x64 is allowed; `test.ps1 -Platform ARM64`
MUST refuse to run on a non-ARM64 host.

## Outputs

| Kind | Path |
| --- | --- |
| Binaries | `.build/<Platform>/<Configuration>/` |
| Intermediates | `.build/Intermediate/<Platform>/<Configuration>/<Project>/` |
| vcpkg install tree | `.build/vcpkg_installed/<x64\|arm64\|x64-asan>/` |
| vcpkg tool checkout | `.build/vcpkg-tool/` |
| MSBuild logs | `.build/logs/` |

`.build/` MUST NOT be committed. `OutDir` / `IntDir` are set in `Directory.Build.props`.

## Build presentation

`build.ps1` MUST begin with the framed RedPrism pixel-art banner before any target, preflight, or MSBuild line. The
banner is a raster prism mark: incoming beam, glass triangle, dispersed spectrum, composition layer chips, and a canvas
checker, then a spectrum `REDPRISM` wordmark. It MUST include the tagline `REDPRISM // RASTER COMPOSITION // BUILD LOCKED`.
On color hosts the spectrum is painted per pixel (24-bit when the host advertises truecolor or Windows Terminal);
plain and redirected capture keep the same block geometry without terminal control sequences. This mark is RedPrism's,
not a copy of another product banner.

`Tests/BuildProcessTests/BuildPresentationTests.ps1` MUST prove banner identity, even scene geometry, interactive-host
selection, and that a CI-style redirected console is not treated as a color host.

## Exact-output process preflight

Before dependency installation, cleaning, rebuilding, or building, `build.ps1` MUST inspect running `RedPrism.exe`
processes. A build invocation does not own an independently launched process and MUST NOT terminate one. It MUST reject
only a process whose `Win32_Process.ExecutablePath`, after absolute-path normalization, equals the selected
`.build/<Platform>/<Configuration>/RedPrism.exe`.

- An exact-path match MUST fail the build and name the process identifier and path so the user can close it and retry.
- Same-name processes from another checkout, platform, configuration, or path MUST remain running and MUST NOT block
  the build.
- Enumeration failure MUST fail the build with an explicit statement that no process was terminated.
- `-Run` performs the same preflight, builds, then launches the new target.

## vcpkg

| File | Value |
| --- | --- |
| `vcpkg-tool.json` repository | `https://github.com/microsoft/vcpkg` |
| `vcpkg-tool.json` commit and `vcpkg.json` `builtin-baseline` | `9e593bb18ea69cc5095e012465dcd675a822ed0d` |
| WIL | `version>="1.0.260126.7"` |
| yyjson | `version>="0.12.0"` |
| libzip | `default-features: false` (zlib is restored as its dependency) |
| Clipper2 | `version>="2.0.1"` (static `Clipper2.lib`; no runtime DLL) |

C0–C4 used WIL and yyjson only. C5 adds libzip; C9 adds Clipper2 for selection booleans and offsets. onnxruntime is a later product decision. `curl`, `webview2`,
`aws-sdk-cpp`, `sqlite3`, `7zip`, `ftxui`, `pe-parse`, `libjpeg-turbo`, `libpng`, and `libraw` MUST NOT be added by
this contract.

`vcpkg-install.ps1` clones the pinned tooling under `.build/vcpkg-tool/`, bootstraps it, and installs into the
platform-scoped tree. `ASan Debug` links its own tree (`vcpkg-install.ps1 -Asan`, `.build/vcpkg_installed/x64-asan/`):
the overlay triplet `Build/vcpkg-triplets/x64-windows-asan.cmake` builds every dependency with `/fsanitize=address`
(its chainloaded toolchain drops vcpkg's `/RTC1`), so a static library such as Clipper2 carries the same STL container
annotations as the sanitized application and DxUi objects and links cleanly. The MSBuild vcpkg integration is disabled (`VcpkgEnabled=false`); include and library directories
are wired from that tree. `Directory.Build.targets` copies `yyjson.dll`, `zip.dll`, and the zlib runtime (`z.dll` /
`zd.dll` or `zlib1.dll`) next to `RedPrism.exe`, `HostTests.exe`, `IoTests.exe`, and `CompositionTests.exe`.

`build.ps1` (except `-Clean`) MUST run `vcpkg-install.ps1` for the selected platform before MSBuild.

## Version stamp

`Common/Version.h` is `0.1.<build>.0`. `build.ps1` passes `/p:RedPrismBuildNumber=` as the Git commit count of `HEAD`,
or `0` when the repository has no commits. Only `ResourceCompile` sees that number so a new stamp does not rebuild C++.

The version resource MUST keep `OriginalFilename=RedPrism.exe` and `ProductName=RedPrism`.

## Entry points

| Script | Duty |
| --- | --- |
| `.\vcpkg-install.ps1 [-Platform x64\|ARM64\|All] [-Asan]` | Restore WIL, yyjson, libzip, and Clipper2 (`-Asan`: the sanitized x64 tree) |
| `.\restore-dxui.ps1 [-Platform x64\|ARM64]` | Restore the exact DxUi pin |
| `.\build.ps1 [-Platform] [-Configuration] [-Clean\|-Rebuild] [-Run]` | Preflight, restore, MSBuild, provenance |
| `.\test.ps1` | Build, then the validation in `Testing_Validation.md` |
| `.\format.ps1 [-Check]` | clang-format over `Common/`, `RedPrism/`, `Tests/` |
| `.\package.ps1 [-Platform] [-SkipBuild] [-SkipSmoke]` | Release build, the portable ZIP with its `.sha256`, the clean-extraction smoke ([`Build_Packaging.md`](Build_Packaging.md)) |
| `.\validate-skills.ps1` | Repository skill frontmatter and body checks |

`build.ps1` locates MSBuild from `MSBUILD_EXE_PATH`, `PATH`, or vswhere (including prerelease VS 2026). Every MSBuild
invocation writes a UTF-8 log under `.build/logs/`.

## Solution shape

`RedPrism.sln` contains `RedPrism` (Win32 GUI), `HostTests`, `DocumentTests`, `IoTests`, and `CompositionTests`.
`Common/` sources compile into those test and app projects. HLSL under `RedPrism/Shaders/` is compiled with FXC into
headers consumed by `CanvasRenderer`.

## Validation

```powershell
.\validate-skills.ps1
.\build.ps1 -Platform x64 -Configuration Debug
.\build.ps1 -Platform x64 -Configuration Release
.\build.ps1 -Platform x64 -Configuration "ASan Debug"
.\test.ps1 -Platform x64 -Configuration Debug
.\format.ps1 -Check
```

Expected: zero MSBuild warnings or errors; `.build\x64\<Configuration>\RedPrism.exe` exists; ASan Debug binaries are
really instrumented.
