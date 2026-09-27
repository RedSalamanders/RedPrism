# RedPrism build toolchain

Status: current normative repository contract
Last reviewed: 2026-09-27
Owner: root MSBuild projects, `vcpkg.json`, `vcpkg-tool.json`, `build.ps1`, `Build/BuildPresentation.psm1`, `vcpkg-install.ps1`, `format.ps1`

## Scope

This contract owns the Windows C++ toolchain, configurations, generated-output layout, vcpkg pin, and the root
build entrypoints. Runtime windowing lives in [`UI/UI_Windowing.md`](../UI/UI_Windowing.md). Architecture ownership
lives in [`Core/Core_Architecture.md`](../Core/Core_Architecture.md). Test orchestration lives in
[`Testing/Testing_Validation.md`](../Testing/Testing_Validation.md).

DxUi restore is C2 work: `restore-dxui.ps1` clones the lock in `Dependencies/DxUi.lock.json` into
`.build/dependencies/DxUi/` and never resets `Z:\src\DxUi`. See [`Core/Core_DxUiIntegration.md`](../Core/Core_DxUiIntegration.md).

Lucide is the tool-rail picture source. It is not a vcpkg port and not an icon font. `Dependencies/Lucide.lock.json`
names `https://github.com/lucide-icons/lucide` and one commit. `ThirdParty/Lucide/icons/` holds only the tool SVGs
copied from that commit, and `ThirdParty/Lucide/LICENSE` is the ISC license plus the MIT notice for icons derived
from Feather. The app does not clone the Lucide repository, does not take its npm packages, and does not build a
private icon font. The rail strokes those SVGs with Direct2D (`LucideToolButton`) at stroke width 1.25 and rotates
the bandage 45°. The palette hot zones use `rotate-ccw` and `move-diagonal-2` from the same pin. Gradient and Shape
use authored SVGs under `RedPrism/Icons/`. Segoe Fluent Icons remain the fallback glyph. A newer Lucide commit is
adopted by replacing the locked files and the lock together.

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
| vcpkg install tree | `.build/vcpkg_installed/<x64\|arm64\|x64-asan\|arm64-asan>/` |
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
platform-scoped tree. `ASan Debug` links its own tree per platform (`vcpkg-install.ps1 -Asan`,
`.build/vcpkg_installed/x64-asan/` or `-Platform ARM64 -Asan`, `.build/vcpkg_installed/arm64-asan/`): the overlay
triplet `Build/vcpkg-triplets/x64-windows-asan.cmake` (ARM64: `arm64-windows-asan.cmake`, mirrored, sharing the same
`asan-toolchain.cmake`) builds every dependency with `/fsanitize=address` (its chainloaded toolchain drops vcpkg's
`/RTC1`), so a static library such as Clipper2 carries the same STL container annotations as the sanitized
application and DxUi objects and links cleanly. The MSBuild vcpkg integration is disabled (`VcpkgEnabled=false`); include and library directories
are wired from that tree. `Directory.Build.targets` copies `yyjson.dll`, `zip.dll`, and the zlib runtime (`z.dll` /
`zd.dll` or `zlib1.dll`) next to `RedPrism.exe`, `HostTests.exe`, `IoTests.exe`, and `CompositionTests.exe`.

`build.ps1` (except `-Clean`) MUST run `vcpkg-install.ps1` for the selected platform before MSBuild, then
`restore-dxui.ps1 -CheckUpdates`. That check compares the lock with DxUi `main` and prints one advisory. It does
not change the pin and does not fail a restore whose pin is valid. A newer `main` commit with a successful completed
`ci.yml` push run is yellow (`DxUi update available: pinned <old>, available <new>`). A newer `main` commit without
that validation is red. A pin ahead of or diverged from `main`, and a lookup that cannot run, are dark yellow. The
same pin is quiet. Colors are presentation only.

## Version stamp

`Common/Version.h` is `0.1.<build>.0`. `build.ps1` passes `/p:RedPrismBuildNumber=` as the Git commit count of `HEAD`,
or `0` when the repository has no commits. Only `ResourceCompile` sees that number so a new stamp does not rebuild C++.

The version resource MUST keep `OriginalFilename=RedPrism.exe` and `ProductName=RedPrism`.

## Entry points

| Script | Duty |
| --- | --- |
| `.\vcpkg-install.ps1 [-Platform x64\|ARM64\|All] [-Asan]` | Restore WIL, yyjson, libzip, and Clipper2 (`-Asan`: the sanitized x64 tree) |
| `.\restore-dxui.ps1 [-Platform x64\|ARM64] [-CheckUpdates]` | Restore the exact DxUi pin; `-CheckUpdates` prints one advisory when DxUi `main` is not the pin |
| `.\build.ps1 [-Platform] [-Configuration] [-Clean\|-Rebuild] [-Analyze] [-Run]` | Preflight, restore, MSBuild, provenance; `-Analyze` runs native MSVC code analysis |
| `.\test.ps1` | Build, then the validation in `Testing_Validation.md` |
| `.\format.ps1 [-Check]` | clang-format over `Common/`, `RedPrism/`, `Tests/` |
| `.\package.ps1 [-Platform] [-SkipBuild] [-SkipSmoke]` | Release build, the portable ZIP with its `.sha256`, the clean-extraction smoke ([`Build_Packaging.md`](Build_Packaging.md)) |
| `.\validate-skills.ps1` | Repository skill frontmatter and body checks |

`build.ps1` locates MSBuild from `MSBUILD_EXE_PATH`, `PATH`, or vswhere (including prerelease VS 2026). Every MSBuild
invocation writes a UTF-8 log under `.build/logs/`.

`build.ps1 -Analyze` passes `RedPrismAnalyze=true`; the root `Directory.Build.targets` enables MSVC native C++ code
analysis (`/analyze`) and loads `EspXEngine.dll` with its `ConcurrencyCheck.dll` extension only for RedPrism projects.
The pinned DxUi project does not receive the opt-in or the compiler plugin.
The analysis marks the selected RedPrism vcpkg include root, the pinned DxUi source and consumer vcpkg include roots,
and the compiler's `INCLUDE` paths (including the Windows SDK) as external, then uses `/analyze:external-` to skip
diagnostics originating in those headers. The DxUi roots come from the resolved pinned consumer properties; analysis
does not modify or float that dependency. It validates RedPrism's repository code, not pinned vendor implementations;
repository diagnostics remain enabled.
MSVC's default C6262 stack threshold remains 16 KiB for `Common/` and `RedPrism/`. Test-owned translation units whose
full paths are under the repository's `Tests/` directory use `/analyze:stacksize 131072` (128 KiB). Several existing
test entry functions keep multiple roughly 13 KiB `DocumentSession` fixtures alive, producing suite-only frames from
26 KiB to about 92 KiB; the test-specific budget accommodates those fixture frames while retaining stack
analysis and leaving the production threshold unchanged. C6262 is not globally disabled.
The switch forces the solution `Rebuild` target, even when `-Rebuild` was not specified, so changed and new translation
units are analyzed instead of reusing ordinary build objects. It retains the selected platform, configuration, worker
count, and other normal build options. Analysis diagnostics remain enabled and are reported in a separate `-Analyze`
MSBuild log under `.build/logs/`; `-Analyze` cannot be combined with `-Clean`.
After a successful solution rebuild, `-Analyze` compiles two small static-library probes with the same MSBuild
executable, platform, and configuration. The probe projects opt in through `RedPrismAnalysisProbe=true`. The guarded
`WorkerResources::ExclusiveLock` access must pass native concurrency SAL analysis; a separately rebuilt unguarded
access must fail with a C261xx diagnostic at `ConcurrencyAnalysisProbe::Unsafe`. Probe projects,
objects, and retained per-case logs live under `.build/AnalysisProbe/`; regular `test.ps1` does not run these controls.

## CPU ISA boundary

Every first-party translation unit, including intrinsic tier files, uses the platform's default `/arch`.
Do not compile shared STL/implicit COMDAT code at a higher `/arch`. Each tier entry is an extern noinline function
named `Kernels_<Tier>_<Module>_<Name>` in `Kernels_<Tier>_<Module>.cpp`, called through `KernelTable`.

After every x64 build, `build.ps1` scans all five first-party linked executables with `llvm-objdump` and
`llvm-pdbutil` from the Visual Studio C++ Clang tools component (or a complete standalone LLVM installation).
`Build/IsaScan.cs` inspects decoded executable instructions for C4/C5/62 VEX/EVEX prefixes, including a preceding
legacy address/segment prefix. It checks the PE CodeView GUID and age against the full PDB, maps decimal section
contributions to their object, and fails on missing/ambiguous attribution or a missing/mismatched PDB.
AVX2 objects allow VEX; AVX-512 objects allow VEX and EVEX. Other objects must match the checked-in
`Build/IsaRuntimeAllowlist.psd1`. No broad first-party object exception is permitted.

The runtime exceptions are MSVC `vector_algorithms.obj` from `msvcprt[d].lib`, and the exact PDB procedure range
of the runtime-dispatched `wmemchr` intrinsic in the observed `ImageCodecs.obj`, `HostTests.obj` or `IoTests.obj`.
MSVC can emit that intrinsic inside a consumer object; surrounding functions remain scanned. The function name
alone is insufficient. Undecoded prefixes in inline data are counted separately,
not accepted as decoded instructions. The scan is a VEX/EVEX boundary gate; it does not prove safety of arbitrary
third-party DLL dispatch or replace execution on baseline hardware. Those libraries need their own qualification.

MSVC can put a switch table inside an executable contribution, and its DWORD RVAs can decode as VEX instructions.
The scanner recognizes only the observed image-relative dispatch: contiguous decoded instructions load the image
base into RCX, optionally remap a byte index, load an RVA through RCX + 4×RAX, add the base and jump through RAX.
It also recognizes the observed R8-base forms with RAX/RCX index and RAX/RCX/RDX target, checking that remap, load,
add and jump use the same registers. The aligned table must follow RET plus at most three alignment bytes, or
have an exact anonymous `S_LDATA32` start in the matching PDB and owning contribution. The latter covers multiple
tables after one function. A following data symbol bounds the table; it is not itself an exempt range. Every entry must
target a decoded instruction after the dispatch and before the table. Only complete instructions inside that
verified DWORD span are counted separately as `inlineTableVectorData`; other code in the function remains gated.
Unrecognized table/dispatch forms receive no exemption. The synthetic parser controls are distinct from the
compiled ISA controls and cover broken dispatch, bounds, targets, trailing return and adjacent real VEX code.

Every x64 configuration also compiles scalar, AVX2 and AVX-512 scan controls. Controls are never executed, so they
work on a baseline host. Deliberate AVX2 in a baseline object and EVEX in an AVX2 object MUST fail; correctly
placed tier code MUST pass, with a separately counted EVEX instruction in the AVX-512 control. A runtime-named
object from an unapproved library and a same-named function in an unapproved object MUST fail. Missing and
mismatched PDB controls MUST fail. Reports, binary/PDB/allowlist hashes, disassembly, full
PDB records and control logs live under `.build/IsaScan/`. `-Clean` and ARM64 builds do not invoke this x64 gate.

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
.\build.ps1 -Platform x64 -Configuration Debug -Analyze
.\test.ps1 -Platform x64 -Configuration Debug
.\format.ps1 -Check
```

Expected: zero MSBuild warnings or errors; `.build\x64\<Configuration>\RedPrism.exe` exists; ASan Debug binaries are
really instrumented.
