# RedPrism build toolchain

Status: current normative repository contract
Last reviewed: 2026-10-21
Owner: root MSBuild projects, `vcpkg.json`, `vcpkg-tool.json`, `build.ps1`, `Build/BuildPresentation.psm1`, `vcpkg-install.ps1`, `Build/VcpkgToolset.psm1`, `Build/VisualStudioDiscovery.psm1`, `format.ps1`

## Scope

Local MSBuild launches, including ordinary, analysis and actual SDK-profile preflight,
use the exact pinned engine's `Invoke-CiNativeCommand`. Each owned child receives a
fresh private compiler metadata-service endpoint; the parent's environment, literal
arguments, live output, original native exit, compiler flags and warning policy remain
unchanged. Restore the clean `.ci-engine` pin before native builds. This launch policy
does not itself grant a native, SDK or performance result.

This contract owns the Windows C++ toolchain, configurations, generated-output layout, vcpkg pin, and the root
build entrypoints. Runtime windowing lives in [`UI/UI_Windowing.md`](../UI/UI_Windowing.md). Architecture ownership
lives in [`Core/Core_Architecture.md`](../Core/Core_Architecture.md). Test orchestration lives in
[`Testing/Testing_Validation.md`](../Testing/Testing_Validation.md).

DxUi restore is C2 work: `restore-dxui.ps1` clones the lock in `Dependencies/DxUi.lock.json` into
`.build/dependencies/DxUi/` and never resets `Z:\src\DxUi`. The clone keeps Git long paths and leaves out the directories a
build never reads (`Measurements/`, `docs/gallery`, `Specs/`), so a deep checkout root restores. See
[`Core/Core_DxUiIntegration.md`](../Core/Core_DxUiIntegration.md).

DxUi consumer output folders use a 16-character fingerprint prefix with the complete identity retained and
validated in a marker. This also bounds vcpkg's native temporary-directory path; Git long-path support alone
does not make those directory operations work. Both MSBuild and product provenance use the resolved output
root, with full identity comparison before cache reuse, as required by `Core_DxUiIntegration.md`.

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
| Warnings | `/Wall`, `/permissive-`, SDL, /WX disabled, zero warnings, `/utf-8`, `/Zc:preprocessor` |
| CRT | `/MDd` Debug and ASan Debug, `/MD` Release |
| Minimum OS | Windows 11 build 22000.2600 |

First-party translation units compile as C++.

On an X64 process, `build.ps1` MUST default an unset `PreferredToolArchitecture` to `x64` for dependency
identity/restore, the solution build and its validation/provenance. An explicit value is preserved, including
the native ARM64 wrapper's `arm64`. The caller's environment MUST be restored on success and every failure.
MSBuild discovery keeps its existing `MSBUILD_EXE_PATH`, PATH and version-ranged installation precedence.
An amd64 MSBuild process alone does not select x64-hosted tools when cross-targeting ARM64. Qualification
records the actual compiler/linker and matching dependency identity; earlier tool-host receipts remain historical.
`Tests/BuildProcessTests/ToolArchitectureTests.ps1` executes unchanged entrypoint bytes with isolated dependency
and solution stand-ins to cover these boundaries, failures, clean builds and caller environment restoration.

The current P1 native ARM64 feasibility wrapper requires default MSVC `14.52.37025` in the selected installation.
It records required/actual versions in `toolset-requirement.json` and refuses a mismatch before changing the
canonical discovery environment or starting work. Matching this declaration alone does not qualify compiler,
dependency or runtime products. Earlier 14.51 native evidence cannot qualify the 14.52 environment.

The reviewed disposable native CI job provisions BuildTools catalog `18.12.12316.0` side by side at
`C:\P1NativeTools1452`. Fixed official bootstrapper/channel/catalog URLs and SHA-256 values bind the input;
the bootstrapper also requires a valid Microsoft publisher signature. Local pinned manifests select native
ARM64 MSBuild, ARM64-hosted compiler tools and SDK 26100. An existing target, input mismatch, installer failure
or restart requirement refuses the job. Installed MSBuild/compiler bytes must match independently inspected
AA64 package members before the unchanged default-version/host-selection guard can mutate the environment.
Installer results, fresh installer logs and observed product hashes are retained, including mismatch facts.
Log collection cannot replace the primary installer error. This CI setup never installs tools on the user's
computer. The artifact retains the selected ARM64 Release intermediates and DxUi library/identity for
independent compiler/dependency readback. Provisioning alone grants no runtime or P1 qualification.

`format.ps1` prefers the C++ Clang tools bundled with the discovered supported Visual Studio installation,
then falls back to PATH and standalone LLVM. It reports the actual formatter and version. This prevents an
unrelated global LLVM formatter from overriding the supported toolchain; tooling evidence includes the selected
formatter's path and binary hash. Formatting uses bounded argument batches so deep checkout paths still cover
every first-party source file within Windows process command-line limits.

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
| vcpkg overlay triplets | `.build/vcpkg-triplets/` |
| vcpkg tool checkout | `.build/vcpkg-tool/` |
| MSBuild logs | `.build/logs/` |

`.build/` MUST NOT be committed. `OutDir` / `IntDir` are set in `Directory.Build.props`.

`format.ps1` MUST process every discovered C++ source even in a deep worktree. It submits bounded filename
batches below the Windows process command-line limit and fails on the first formatter error; splitting the
invocations MUST NOT change formatting options, omit files or change the final discovered-file count.

Builds sharing a mutable dependency output MUST be serialized, including private candidate builds. A separate
application `OutDir`/`IntDir` does not isolate a pinned DxUi rebuild that writes the same resolved library path.
Parallel compilation requires isolated dependency outputs or an already-built immutable dependency; preserve
failed source/object/link evidence before retrying a candidate affected by shared-output rebuilds.

## Explicit build concurrency

`build.ps1 -MaxCpuCount N` and `test.ps1 -MaxCpuCount N` accept 0–256. Zero preserves the automatic
MSBuild/compiler choice. A positive value caps MSBuild project concurrency (`/m:N`) and forwards
`CL_MPCount=N` for toolsets that use the compiler multi-tool scheduler. The test entrypoint forwards the
same value without omitting any gate. The installed toolset may disable compiler `/MP` or use a separate
task scheduler; qualification MUST inspect actual evaluated settings and MUST NOT infer an emitted `/MPN`
switch from this property alone. Memory-constrained qualification SHOULD use an explicit small project cap
and record the compiler/task settings. A single compiler batch can contain multiple translation units.
No system pagefile setting or independently running application is changed by this option.

Persistent qualification storage follows [Build_QualificationArchives.md](Build_QualificationArchives.md).
Collectors use `archive-qualification.ps1` with explicit frozen hashes and retain all existing semantic checks.

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
| `vcpkg-tool.json` commit and `vcpkg.json` `builtin-baseline` | `9eec002df1cd109f8f104cd966a763daf9751489` |
| WIL | `version>="1.0.260126.7"` |
| yyjson | `version>="0.13.0"` |
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

vcpkg builds with the Visual Studio installation and the default MSVC toolset that MSBuild compiles RedPrism with, never with
the newest toolset it finds. `vcpkg-install.ps1` takes the installation from the MSBuild that `build.ps1` discovered
(`-MSBuildPath`; the installation holding its `MSBuild\Current\Bin`, so `MSBUILD_EXE_PATH` and `PATH` are honored, and a
standalone run finds the MSBuild `build.ps1` would), and the toolset from that installation's
`VC\Auxiliary\Build\Microsoft.VCToolsVersion.default.txt` (MSBuild reaches the same version for `v145` through
`Microsoft.VCToolsVersion.v145.default.props`; the two agree on the installation this was written against, and the choice
follows DxUi's `Get-DxUiDefaultToolset`). Neither is hard-coded, and a missing or malformed version file
fails before anything is cloned, naming the file and the repair. The two differ when a newer toolset is installed beside the
default: in the historical 14.51-default installation, the side-by-side 14.52 preview lacked an x64-hosted ARM64 compiler, so every fresh ARM64 restore failed
while configuring its first library (`LNK1104` on `MSVCRTD.lib`), and compiled libraries from a toolset newer than the one that
links them are a link hazard.

Every triplet vcpkg can build here (`x64-windows` and `arm64-windows`, as target or as host, and the two sanitizer triplets)
gets an overlay in `.build/vcpkg-triplets/` (`Build/VcpkgToolset.psm1`): the pinned vcpkg checkout's triplet, or RedPrism's own,
copied unchanged, then `set(VCPKG_VISUAL_STUDIO_PATH "<installation>")` and `set(VCPKG_PLATFORM_TOOLSET_VERSION "<major.minor>")`.
vcpkg receives only that directory as `--overlay-triplets`. The sanitizer overlays bring `asan-toolchain.cmake` beside them,
which reaches the managed checkout through `..\..\.build\vcpkg-tool`, so the overlay directory stays two levels below the
repository root (`vcpkg-install.ps1` refuses a layout where it does not). A file is rewritten only when its bytes change. A
different triplet is a different package ABI hash, so the first restore after a change of the installation, the default
major.minor, the vcpkg pin or a triplet rebuilds the packages; vcpkg's per-user binary cache serves a repeat. The logic mirrors
DxUi's `Tools/VcpkgTriplet.psm1`, which pins DxUi's own restore the same way, but depends on neither a restored DxUi checkout
nor its pin, because `vcpkg-install.ps1` runs before `restore-dxui.ps1` and on its own.
`Tests/BuildProcessTests/VcpkgToolsetTests.ps1` covers it, and requires the same overlay text and the same toolset (and
failures) as the pinned DxUi modules whenever the pin is restored, so a drift shows at the next pin move.

`build.ps1` (except `-Clean`) MUST run `vcpkg-install.ps1` for the selected platform, with the MSBuild it found, before
MSBuild, then `restore-dxui.ps1 -CheckUpdates`. That check compares the lock with DxUi `main` and prints one advisory. It does
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
| `.\vcpkg-install.ps1 [-Platform x64\|ARM64\|All] [-Asan] [-MSBuildPath <path>]` | Restore WIL, yyjson, libzip, and Clipper2 with the Visual Studio installation and default toolset of the MSBuild in use (`-Asan`: the sanitized tree of the platform) |
| `.\restore-dxui.ps1 [-Platform x64\|ARM64] [-CheckUpdates]` | Restore the exact DxUi pin as a sparse, long-path-safe checkout; `-CheckUpdates` prints one advisory when DxUi `main` is not the pin |
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
GUID and age MUST match before PDB contribution offsets are interpreted in the image's sections. Independent
wrong-GUID and wrong-age controls with impossible contribution offsets prove this ordering; a matching PDB
still undergoes every existing contribution, instruction and runtime-allowlist check.
AVX2 objects allow VEX; AVX-512 objects allow VEX and EVEX. Other objects must match the checked-in
`Build/IsaRuntimeAllowlist.psd1`. No broad first-party object exception is permitted.
The R15 signed variant-remap data proof checks the complete PE-backed body and encoded control flow,
nonvolatile image-base reaching definition, anonymous same-owner DWORD/byte pairs, exact procedure end,
uniform remap extent, decoded targets and literal alignment NOPs. Unknown effects and unmodelled transfers
refuse that proof. This data recognition adds no runtime, object or function allowance.

The runtime exceptions are MSVC `vector_algorithms.obj` from `msvcprt[d].lib`, and the exact PDB procedure range
of the runtime-dispatched `wmemchr` intrinsic in the observed `ImageCodecs.obj`, `Host.Tests.Runner.obj`, `IO.Tests.Runner.obj` or
`SettingsStore.obj` (the CompositionTests worker/runtime link).
MSVC can emit that intrinsic inside a consumer object; surrounding functions remain scanned. The function name
alone is insufficient. Undecoded prefixes in inline data are counted separately,
not accepted as decoded instructions. The scan is a VEX/EVEX boundary gate; it does not prove safety of arbitrary
third-party DLL dispatch or replace execution on baseline hardware. Those libraries need their own qualification.

MSVC can put a switch table inside an executable contribution, and its DWORD RVAs or byte indices can decode as VEX
or EVEX instructions. Whether they do depends only on the address at which the linker places the procedure, so any relink
can turn an accepted table into a violation. MSVC emits each such table after all of its procedure's code and marks it
in the PDB with an anonymous untyped 16-byte `S_LDATA32`. The scanner proves every marked range from that record and
its content, whatever dispatch reads it:

- The range runs from the record to the next such record of the same module, or to the end of the one PDB procedure of
  that module and contribution that contains the record. A record in no procedure, or in more than one, proves nothing.
- A DWORD table starts on a DWORD boundary, spans a whole number of DWORDs and holds at least two. Each DWORD is the
  image-relative address of a known decoded instruction boundary of that procedure before its first marked range.
- A byte index table directly follows such a proved DWORD table of the same procedure. Each of its bytes is an index
  below that table's entry count, except the exact trailing alignment `90`, `66 90` or `0F 1F 00` of a range that ends
  on a DWORD boundary.

A range that is neither proves nothing. A range over 64 KiB is not examined. Every byte outside a proved range stays
gated, including real vector code before, between or after the tables. A proved range takes part in the row
checks below (an `INT3` suffix, directly touching proved tables) like a recognized table. The proof exempts no object,
procedure or address, and the runtime allowlist is unchanged
([record](../TestRuns/IsaMarkedDataTables_2026-10-02.md)).

The dispatch recognizers below prove a table from its observed dispatch form, independently of the marked-range
proof; their parser controls are evaluated with that proof both enabled and disabled. The basic form is the
observed image-relative dispatch: contiguous decoded instructions load the image
base into RCX, optionally remap a byte index, load an RVA through RCX + 4×RAX, add the base and jump through RAX.
It also recognizes the observed R8-base forms with RAX/RCX index and RAX/RCX/RDX target, checking that remap, load,
add and jump use the same registers. The aligned table must follow RET plus at most three alignment bytes, or
have an exact anonymous `S_LDATA32` start in the matching PDB and owning contribution. The latter covers multiple
tables after one function. A following data symbol bounds the table; it is not itself an exempt range. Every entry must
target a known decoded instruction boundary before the table, in the same executable contribution. A target before
the dispatch additionally requires one exact same-owner PDB procedure containing the dispatch, the backward
targets and the entire DWORD table; that procedure must fit its executable contribution and section. Unknown
instructions, overlapping boundaries and cross-procedure targets do not prove data. When the next same-owner
anonymous data symbol bounds a DWORD table, every entry through that exclusive boundary must pass; an invalid
last entry cannot silently shorten the span. Only complete instructions inside that
verified DWORD span are counted separately as `inlineTableVectorData`. An observed linear-disassembly row starting
inside that span may consume adjacent `INT3` alignment: the row must be at most 15 bytes, match the PE byte for
byte, and have only `0xCC` outside the proved table. A row may cross directly touching tables only when each table
has independently passed the dispatch, PDB, PE and decoded-target proof, belongs to the exact same owning module
and contribution, and has no unresolved targets. Gaps and merely neighboring data symbols do not extend the
proved span. A row starting at or after the end, a nonpadding suffix or an unproved table stays gated. Other code
in the function remains gated.
An observed memory byte-index remap also requires the exact anonymous, untyped same-owner PDB data symbol at
its encoded start, followed by an exclusive same-owner anonymous data-symbol boundary. A terminal byte span
may instead end at the exact shared end of its owning PDB procedure and executable contribution when no
following data symbol exists. That same procedure must contain the dispatch, DWORD table and byte-table start;
a missing, shorter, cross-owner or differently bounded procedure cannot prove this terminal span.
The observed Release RDX-image-base remap requires its exact contiguous LEA EAX,[RDX+disp32], CMP EAX,imm32,
unsigned JA and CDQE guard immediately before the image-base LEA. The JA default is a proved DWORD-table target;
maximum+1 bounds the byte table within the same exact owning procedure and contribution. Every guard and
dispatch row must agree with PE bytes, decoded length and nonoverlapping known instruction boundaries. A
missing/malformed guard cannot fall back to another data symbol or procedure end. The 39 independent RDX
controls include corrupt bytes/targets/bounds/PDB records, duplicate/unknown/overlapping dispatch rows,
listing-versus-PE mismatches and real vector code outside the proved range. This is data proof, not a runtime
allowlist entry.

The observed Release RBX-index variant uses the same RDX image-base dispatch, with exactly
`LEA EAX,[RBX+disp8]; CMP EAX,imm32; JA rel32; CDQE` immediately before the image-base LEA.
Its guard is 16 bytes; the original RDX disp32 guard remains 19 bytes. All four known guard rows
must match their PE bytes, decoded lengths and nonoverlapping boundaries. Both forms require
the same procedure/contribution ownership, complete DWORD-target proof, proved JA default and
`maximum + 1` byte-table bound (maximum below 4,096). A missing or malformed guard grants no
fallback extent. `IsaRbxDisp8RemapTests.ps1` ports the original 39 remap controls to the RBX form
and adds 20 guard/operand refusals: one accepted, 58 refused. The original RDX fixture remains
unchanged. These encoded fixtures do not execute their bytes; the runtime allowlist is unchanged.

The observed Release host-flow switch uses a full-width unsigned RSI guard followed by the exact contiguous
`CMP RSI,imm8; JA rel32; LEA RCX,imagebase; MOV EAX,[RCX+4*RSI+table]; ADD RAX,RCX; JMP RAX`.
Its 29 bytes and six decoded rows must match the PE; the maximum is 1–127. The anonymous untyped same-owner
PDB table starts on a DWORD boundary and ends at exactly `tableStart + 4*(maximum+1)`, the procedure end.
The complete procedure body before the table is capped at 16 KiB and must be known, contiguous, nonoverlapping
and PE-matching, including the final one-byte RET and exact three-byte NOP. Every case/default target is a
body boundary after the dispatch and before the NOP; the RET itself is valid. Direct branches bypassing the
guard or entering the table refuse. Reported vector bytes must match the PE before data classification.
`IsaRsi64TerminalTests.ps1` supplies 93 controls (seven accepted, 86 refused), including a genuine adjacent VEX
instruction that remains rejected. Identical owner records and irrelevant foreign records leave valid proof
intact; a missing or solely foreign owner does not prove the table. Existing recognizers and the runtime
allowlist remain unchanged. Synthetic fixture bytes are never executed.

The Release adjustment-construction table uses an exact nonvolatile RBX index: `MOVSXD RBX,EDX`,
`MOV RDI,RCX`, a zero DWORD stack initializer, an ordinary x64 ABI call, a one DWORD initializer,
`MOV [RDI],EBX`, `CMP EBX,max`, unsigned `JA rel32`, then `LEA RDX,imagebase`,
`MOV ECX,[RDX+4*RBX+table]`, `ADD RCX,RDX`, `JMP RCX`. All twelve contiguous decoded rows and
57 bytes must match the PE; the full-width initializer and unsigned maximum 1–127 bound the RBX index.
The exact same-owner anonymous untyped symbol starts an aligned DWORD table whose
`tableStart + 4*(max+1)` equals the owning PDB procedure end inside its contribution. The preceding
one-byte RET and INT3, all case targets and the separate JA default require decoded boundaries before
the table in that procedure. A larger contribution is allowed; changed procedure extent, symbols,
guards, row ownership or bytes withhold proof. This form has no fallback to another data proof.
Reported vector-row bytes must also match the PE before being classified as table data; adjacent real
vector code remains gated. Duplicate decoded rows retain their longest claimed extent and become
unknown, so a short first row cannot hide a later overlapping row. `IsaRbxLongGuardTests.ps1` supplies
132 parser controls (four accepted, 128 refused); their bytes are never executed and the runtime
allowlist is unchanged.

The observed terminal RDX DWORD table has a different exact guard: `MOVZX ECX,byte [RAX]`, `CMP CL,max`,
`JA rel8`, followed contiguously by `LEA RDX,imagebase`, `MOV EAX,[RDX+4*RCX+table]`, `ADD RAX,RDX`, `JMP RAX`.
The unsigned byte maximum is 1–127. The aligned table has an exact same-owner anonymous data symbol and
`tableStart + 4*(max+1)` equals the exact owning PDB procedure end inside its executable contribution. Every
entry and the default target must be decoded boundaries in that procedure before the table; the default may
be separate from the table entries. Guard/dispatch bytes and decoded lengths must match the PE without unknown
or overlapping boundaries. An earlier same-owner data symbol contradicts the table count; a later procedure's
symbol cannot extend it. This form cannot fall back to a looser symbol/procedure-end proof. All 66 controls pass,
including a real adjacent vector instruction that stays rejected. No runtime allowlist entry is added.

For the observed ASan RCX/RAX remap, a procedure end inside a larger contribution requires an additional exact
25-byte guard immediately before LEA: `CMP dword [RSP+disp32],max`, `JA rel32`, then
`MOVSXD RAX,dword [RSP+the same disp32]`. The three decoded boundaries, lengths, owners and reported PE bytes must agree;
unknown or overlapping rows fail. The unsigned maximum is below 4096, the JA default is a proved DWORD-table
target, and `byteStart + max + 1` equals that exact owning procedure's end. The guard, dispatch, DWORD span and
every target fit that procedure and contribution. An earlier anonymous data symbol contradicts the count;
a later function's symbol cannot extend it. Every encoded index is validated, with no padding subtracted from
this guarded count. A bare procedure end does not prove this form. Existing terminal and symbol-bounded proofs
retain their original rules.
The complete PE byte range must fit the same contribution, contain at most 4096 bytes, follow the associated DWORD table and contain
only indices below that table's entry count. Register/immediate remaps do not create a byte-data range. The
exact trailing alignment `90`, `66 90` or `0F 1F 00` may occupy one to three bytes ending at the proved DWORD-aligned
boundary. Those complete bytes are padding; every preceding byte still passes the index bound. A changed suffix,
repeated padding, nonaligned end or empty index range receives no exemption. No other instruction is padding.
The byte table becomes proved data only after its associated DWORD table and every branch target pass. Rows crossing
directly touching DWORD and byte-index spans keep the same 15-byte, byte-for-byte PE and contribution checks.
No object, library or function runtime allowlist entry is added by either proof.
A table of an unrecognized dispatch form is proved only by its PDB record and content, as described above.

The observed Release OnCommand remap has exactly nine contiguous rows (43 bytes):
`LEA EAX,[RBX-0x64]; CMP EAX,imm32; JA rel32; CDQE; LEA RDI,imagebase; MOVZX EAX,[RDI+RAX+byteTable];
MOV ECX,[RDI+4*RAX+dwordTable]; ADD RCX,RDI; JMP RCX`. Every row must match PE bytes and known,
nonoverlapping decoded boundaries. The unsigned maximum is 1…4095. Same-owner anonymous PDB starts bound
adjacent DWORD/byte spans; the DWORD table has 2…4096 entries and the byte span is exactly max+1 bytes.
Its exclusive boundary is the next same-owner data symbol or owning procedure end, with at most three bytes
of canonical alignment (`90`/`CC`, `66 90` or `0F 1F 00`). That alignment receives no table exemption.
The guard/dispatch, both spans and all decoded targets fit the exact same procedure and contribution.
Every byte is below the DWORD entry count. Every DWORD target is a decoded body boundary before the tables,
outside the protected guard/dispatch; the JA default must be one of those targets. Direct body entry into the
protected sequence or either data span rejects the proof. Missing/corrupt bounds, owner, PE/listing rows or
targets receive no fallback. Other vector code remains gated; the runtime allowlist is unchanged.

The observed terminal CL/R8 dispatch controls live in `IsaClGuardTests.ps1`. That exact form is contiguous
`MOVZX ECX,byte [RBX+disp8]; CMP CL,max; JA rel32; MOVZX ECX,CL; LEA R8,imagebase;
MOV ECX,[R8+4*RCX+table]; ADD RCX,R8; JMP RCX`. Every row's exact bytes/length agree with the PE, with no unknown
or overlapping boundaries. The unsigned maximum is 1…127. The aligned same-owner anonymous PDB table starts at
the load displacement, and its maximum+1 DWORDs end exactly at the owning PDB procedure end. No intervening data
symbol is allowed. Every entry and the JA default target is a decoded boundary of that procedure before the
table. The observed `JMP rel8; NOP` suffix immediately before the table is also corroborated against the PE and
its jump target. The 99 independent controls prove accepted bounds and displacements and refusal of malformed
guard/register/PE/listing/PDB/target/suffix rows, plus real adjacent VEX/EVEX instructions. This adds no runtime
allowlist exception. The synthetic parser controls are distinct from the compiled ISA controls. They cover broken
dispatch, bounds, targets, trailing return, the marked-range rules and adjacent real VEX code.

  The compact ASan menu byte remap permits exactly `CMP dword [RSP+disp32],imm8; JA rel32;
  MOV EAX,[RSP+the same disp32]` followed by the contiguous RCX image-base byte/DWORD dispatch, ADD and JMP RAX.
  All eight rows, 48 bytes, must match PE bytes and decoded nonoverlapping boundaries. The sign-extended imm8
  maximum is 1…127. The adjacent same-owner anonymous PDB DWORD/byte tables have max+1 byte indices ending exactly
  at their owning PDB procedure end inside the larger contribution. Every index is below the DWORD entry count;
  every DWORD/default target is a decoded body boundary before the tables. A later data symbol cannot rescue a
  missing/malformed compact guard. The 56 independent controls include extreme valid bounds/displacements,
  PE/listing/PDB/target/guard mismatches and adjacent real VEX/EVEX. This adds no runtime allowlist exemption.

The observed Release continuation switch uses an image-base LEA into R10, 83 bytes of exact non-clobbering
prelude rows, and `MOVZX EAX,byte [RBX+disp32]; CMP AL,imm8; JA rel32; MOV EAX,[R10+4*RAX+table]; ADD RAX,R10;
JMP RAX`. All 24 prelude/guard/dispatch rows must match PE bytes, decoded lengths and nonoverlapping boundaries;
the unsigned maximum is 1…127. Prelude branches must reach decoded procedure boundaries, and direct entries
cannot bypass the base definition or guard. The same-owner anonymous DWORD table ends exactly at its PDB
procedure end, with no intervening data symbol. Every target, including backward body targets and the separate
default, is a decoded boundary before the table; table entries cannot enter the protected prelude/guard.
The preceding RET/NOP or RET/security-failure-CALL/INT3/NOP rows are also corroborated against PE and listing.
The 183 independent controls include every protected row's missing/duplicate/unknown/overlapping/PE/listing
refusal, extreme bounds, direct-entry and backward-target cases, metadata/target/tail refusals, and real VEX/EVEX
in the body or adjacent to the table. This proof adds no runtime instruction, module or allowlist exemption.

The observed Release `std::format::_Visit` terminal switch instead uses exactly 40 contiguous bytes:
`MOVZX EAX,byte [RCX]`; three fixed register-preserving moves; `CMP AL,imm8`; `JA rel32`;
`LEA R10,imagebase`; `MOV ECX,[R10+4*RAX+table]`; `ADD RCX,R10`; `JMP RCX`.
Full EAX zero extension, all ten exact rows and the complete known procedure body must agree with PE bytes and
nonoverlapping decoded boundaries. The unsigned maximum is 1…127. Anonymous untyped same-owner PDB data begins
at the DWORD load's displacement, and max+1 DWORDs end exactly at that procedure's end. No intervening symbol
may alter the count. Every table/default target is a known body boundary after dispatch, before the exact cold
CALL/INT3/NOP tail. Direct branches may not bypass the protected guard, enter tail alignment or target table
bytes. Reported table bytes must match PE bytes too. The 141 independent controls include every protected/body/
tail-row proof failure, ownership/count/target corruption, bounds and genuine body/adjacent vector rejection.
This proof recognizes data only; ordinary instructions and the runtime allowlist remain gated.

The observed ASan menu tracing remap uses a different exact 45-byte form: `CMP dword [RSP+disp8],imm32`;
`JA rel32`; zero-extending `MOV EAX,[RSP+the same disp8]`; the contiguous RCX image-base LEA, byte remap load,
DWORD target load, ADD RAX,RCX and JMP RAX. All eight rows, their lengths and PE/listing bytes must agree without
unknown/duplicate/overlapping boundaries. The unsigned maximum is 1…4095; max+1 byte indices end at the exact
same-owner procedure end inside its contribution. Anonymous untyped same-owner symbols bound both tables;
the byte table begins at the DWORD end, every byte index is below the DWORD count, and every target/default is
a decoded body boundary before the DWORD table. Earlier contradictory or unrelated later symbols cannot rescue
a malformed/missing guard. Reported DWORD rows must agree with PE bytes; genuine body/adjacent VEX/EVEX remain
instructions. The 115 independent controls cover 15 valid bound/displacement combinations, all eight row-proof
failure classes, operand/owner/range/count corruption and genuine body/adjacent vector rejection. This adds no
module or allowlist exemption.

The observed ASan color-picker remap also emits the exact 42-byte compact form with opcode `83`:
`CMP dword [RSP+disp8],imm8; JA rel32; MOV EAX,[RSP+the same disp8]`, followed by the same contiguous
RCX image-base LEA, byte-remap load, DWORD target load, ADD RAX,RCX and JMP RAX. Its sign-extended
immediate must be positive, 1…127; encoded negative values and zero do not prove an unsigned bound.
The compact comparison row must be retained as evidence. All eight rows, PE/listing agreement,
nonoverlapping known boundaries, anonymous same-owner symbols, contiguous DWORD/byte spans,
exact owning procedure end, remap indices and decoded body/default targets satisfy the same proof
as the imm32 form. No missing or contradictory evidence receives a fallback exemption. The 115
imm32 controls remain unchanged; `IsaDisp8RcxImm8ByteRemapTests.ps1` adds 116 independent controls,
including the observed maximum 27 / stack displacement 40, invalid signed maxima, every row-evidence
fault and genuine body/adjacent VEX/EVEX rejection. Only proved data receives data status; runtime
and tier allowlists are unchanged. The [unchanged-image record](../TestRuns/P1WandReadbackRebind_2026-10-06.md)
preserves the original false instruction report and exact failed binary/PDB.

The observed ASan DxUi message switch uses the exact 51-byte zero-extending wide form:
`CMP dword [RSP+disp32],imm32; JA rel32; MOV EAX,[RSP+the same disp32]`, followed by the contiguous
RCX image-base LEA, byte-remap load, DWORD target load, ADD RAX,RCX and JMP RAX. The unsigned maximum
is 1…4095. All four stack-displacement bytes agree and all eight rows match PE bytes, decoded lengths
and nonoverlapping known boundaries. Anonymous same-owner PDB symbols bound the adjacent DWORD and
byte spans. Exactly max+1 byte indices end at the owning procedure end, every index is below the
DWORD count, and every target/default is a decoded body boundary before the DWORD table. Missing,
malformed or contradictory evidence cannot be rescued by a later symbol or broader contribution.
No other same-contribution procedure entry may begin inside these proved tables. Reported vector rows
must match PE bytes. Genuine body/adjacent VEX/EVEX remains gated; this proof
adds no object/function/runtime allowance and changes neither the pinned DxUi code nor its pin.
`IsaWideRcxByteRemapTests.ps1` owns 136 independent encoded acceptance/refusal controls, including
the observed maximum 226/stack 0xc98, shifted image/section, all eight row proof failures, full
displacements, metadata/content/target boundaries and genuine vectors in/after the body.

The ASan `Application::EndCanvasEdit` terminal switch uses exactly `CMP dword [RSP+disp32],imm8;
JA rel32; MOVSXD RAX,dword [RSP+the same disp32]`, followed by the contiguous RCX image-base DWORD dispatch,
ADD and JMP RAX. All seven guard/dispatch rows must match their PE bytes, decoded lengths and nonoverlapping
boundaries. All four displacement bytes must agree; the unsigned maximum is 1…127. The aligned anonymous
same-owner PDB table starts at the load displacement, and max+1 DWORDs end exactly at the containing procedure
end. Every table/default target is a decoded boundary within that procedure before the table. Intervening
data symbols or unrelated later symbols cannot supply a larger extent. The 54 independent controls cover
accepted bounds/separate default, malformed registers/SIB/opcodes, high-byte displacement mismatches,
missing/unknown/overlapping/PE-mismatched rows, metadata/bounds/targets and adjacent real vector rejection.
Existing disp8 terminal proofs remain required. This is data recognition with no runtime allowlist exemption.

  The observed DxUi typography form permits exactly one contiguous `MOVZX EAX,DL` between LEA RCX of image base
and the DWORD EAX table load. RCX stays unchanged. This form requires the exact same-owner anonymous 16-byte
untyped `S_LDATA32` at the aligned table start, even if RET/alignment precede it; decoded targets and full bounds
still apply. A changed byte register, opcode, repeated move, intervening instruction or invalid PDB symbol gets
no exemption. Real instructions next to the table remain gated, and the runtime allowlist is unchanged.

The observed WIC orientation dispatch permits exactly `LEA RDX,imagebase; MOV ECX,[RDX+4*RAX+table];
ADD RCX,RDX; JMP RCX`, contiguous and without optional index loads. This form also requires the exact anonymous
same-owner untyped 16-byte `S_LDATA32` record at the table start. Contribution, alignment, PE-entry and decoded
target bounds remain required; malformed registers, extra instructions, invalid symbols or adjacent real
vector instructions remain violations. Seven DWORD entries in the observed Release image are data, not EVEX code.

The observed DxUi Menu dispatch permits exactly contiguous `LEA R9,imagebase; MOVZX EAX,[R9+RAX+byteTable];
MOV R8D,[R9+4*RAX+table]; ADD R8,R9; JMP R8`. Both indexed loads must have the observed byte encodings;
the byte-index load is mandatory and no prelude is allowed. The aligned DWORD table requires the exact
anonymous same-owner untyped 16-byte PDB data record and the same contribution/decoded-target proof as above.
Other bases, indices, targets, opcodes, intervening instructions or invalid PDB evidence receive no exemption.
Adjacent executable vector code remains gated; the runtime allowlist is unchanged.

The observed Debug/ASan `std::variant` RAX-base form requires exactly LEA RAX of image base,
one frame-relative RCX index load, MOV ECX through RAX + 4×RCX, ADD RCX/RAX, and JMP RCX. The index load is exactly
`48 8B 8D <disp32>` from RBP or `48 8B 4C 24 <disp8>` from RSP; these contiguous byte forms alone are recognized.
It additionally requires the same contribution's exact anonymous 16-byte `S_LDATA32`, `<no type>` and aligned
table start; all existing
decoded-target and table-bound checks apply. Corrupted registers, sequences, symbols, targets, and real vector
instructions beside the table remain refused. This adds no object/function runtime exemption.

The separately observed ASan QWORD-stack/RAX terminal form requires exactly seven contiguous instructions:
`CMP qword [RSP+disp32],imm8; JA rel32; LEA RAX,imagebase; MOV RCX,[RSP+disp32];
MOV ECX,[RAX+4*RCX+tableRva]; ADD RCX,RAX; JMP RCX`. Both full stack displacements must agree and the unsigned
bound must be 1…127. All seven rows must match the PE bytes and decoded boundaries without overlap. The aligned
table starts at an anonymous same-owner untyped PDB data record; max+1 DWORDs end exactly at its owning
procedure's end, with no interior same-owner data record. A decoded single-byte RET immediately precedes it,
or is followed by exactly one decoded `0f 1f 00` alignment NOP immediately before the table. In the padded form,
both terminal rows and every procedure-body row before the table must be known, contiguous, nonoverlapping and
byte-identical to the PE. Table/default targets cannot enter the padding. Direct transfers, including LOOP-family
and legacy-prefixed branches/calls, cannot enter the padding/table or protected guard interior from elsewhere.
Every target is a decoded body boundary in that procedure before the table and outside the protected guard/
dispatch; the guarded fallback must be among them. Direct branches/calls elsewhere in that procedure cannot
enter the protected interior or the table. Missing or changed proof evidence leaves the bytes subject to the
ordinary ISA gate; real vector code adjacent to the table remains gated. The runtime allowlist is unchanged
([73-control proof and unchanged-image rechecks](../TestRuns/IsaQwordStackRax_2026-10-03.md)).
The padded extension retains those controls and all other predecessor parser suites; 113 additional controls
and four unchanged archived images pass the [terminal-padding proof](../TestRuns/IsaQwordPadding_2026-10-03.md).
Native qualification remains separately required for its activation.

The additionally observed ASan DWORD zero-stack form is exactly `CMP dword [RSP],imm8; JA rel8;
LEA RAX,imagebase; MOV ECX,[RSP]; MOV ECX,[RAX+4*RCX+tableRva]; ADD RCX,RAX; JMP RCX`.
Its unsigned bound is 1…127, its ECX stack load zero-extends, and its seven PE/listing boundaries are contiguous
and nonoverlapping. The same QWORD-form terminal symbol/count/target and complete padded-body proof above
applies: anonymous same-owner untyped data, exact owning procedure end, all max+1 entries, a proved fallback,
known PE-matched body rows and no outside transfer into guard interior or table/padding. The exact zero-stack
form cannot infer another stack displacement or operand width. Missing, duplicate, unknown, contradictory or
changed evidence withholds the exemption. Genuine body/adjacent VEX and EVEX instructions stay gated; no
runtime exception is added. See [109 parser controls and the original failed-image proof](../TestRuns/IsaDwordZeroStack_2026-10-10.md).

The observed Release variant form loads a signed byte into RAX from RBP+disp8 or RBP+disp32 immediately before
LEA RCX of image base. Only `48 0F BE 45 <disp8>` or `48 0F BE 85 <disp32>` proves that index; a different
destination, REX prefix, frame base, SIB or load width does not. Its DWORD load can point exactly one entry after
the anonymous PDB table start: index -1 addresses
the first/default entry. Only the exact contiguous MOVSX/LEA/load/ADD/JMP form and same-owner anonymous data
symbol prove that bias. The complete table, including its default entry, must target decoded body instructions
after the jump and before the table. Different registers, noncontiguity, greater bias, invalid PDB records and
invalid default/other targets receive no exemption. This adds no runtime allowlist entry.

The observed Release signed variant move also uses exactly five contiguous instructions, 27 bytes:
`MOVSX RAX,byte [RBP+disp32]`, `LEA RDX,imagebase`, `MOV EAX,[RDX+4*RAX+tableRva]`,
`ADD RAX,RDX`, `JMP RAX`. Every decoded row, length, nonoverlapping boundary and byte MUST match the PE.
The exact anonymous same-owner untyped PDB data record MUST begin one DWORD before the load displacement;
that first entry belongs to the signed index -1 default. The complete default and ordinary entries MUST target
known decoded body instructions after the jump and before the table, with the existing contribution and
next-data-symbol bounds. Another register, REX/frame/SIB/width, unsigned or disp8 index, bias, gap, unknown or
conflicting row, symbol/owner or target receives no proof. A real vector instruction adjacent to this data
remains a violation. This changes no runtime, object, function or address allowlist. Its 62 independent pure
parser controls and same-image readback are separate from native profile qualification.

The observed Release `std::variant` destruction dispatch (`_Variant_base<...>::_Destroy` of 21 alternatives) is exactly six
contiguous instructions, 37 bytes: `MOVSX RAX,byte [RCX+disp32]`, `LEA R8,imagebase`, `MOVZX EAX,byte [R8+RAX+disp32]`,
`MOV EDX,[R8+4*RAX+disp32]`, `ADD RDX,R8` and `JMP RDX`. The index is signed (-1 is the valueless state), so the byte load's
displacement is the table start plus one. Each row must equal the PE bytes, have its reported length and overlap no other decoded
row. The DWORD table starts at the exact anonymous same-owner untyped 16-byte `S_LDATA32` of its load. The byte table starts at
the exact such record one byte before its load displacement (a record at the displacement, or anywhere else, proves nothing),
which is also where the DWORD table ends. The byte table ends at the end its owning PDB procedure shares with its executable
contribution, no other data symbol lies inside either table, every byte is an index below the DWORD entry count, and every entry
is a decoded instruction boundary of that procedure before the tables. The procedure may begin before the MOVSX. Another bias,
register, displacement width, index extension, row, symbol, bound or target receives no exemption, and nothing is exempted by
address, object or procedure name. This adds no runtime allowlist entry
([record](../TestRuns/IsaSignedBiasedByteTable_2026-10-01.md)).

The observed Release variant destructor also emits `MOVSX RAX,byte [RBP+disp32]`, contiguous `LEA RDX,imagebase`,
`MOVZX EAX,byte [RDX+RAX+disp32]`, `MOV ECX,[RDX+4*RAX+disp32]`, `ADD RCX,RDX`, `JMP RCX`.
This specific RDX form uses the same one-byte valueless bias and terminal DWORD/byte-table proof as the R8
form above. Every instruction boundary and byte must agree with the PE; the anonymous same-owner PDB records
bound both tables, every remap byte is below the exact DWORD entry count, every target is a decoded boundary
inside the same procedure before its tables, and the procedure and contribution share the exact byte-table end.
Reported table bytes must agree with the PE too. A different bias, register, width, owner, symbol, extent,
decoded row or target receives no data exemption. `IsaSignedRdxRemapTests.ps1` preserves 62 independent controls,
including malformed/missing/duplicated/overlapping rows, PE/listing contradictions, last-byte remap corruption,
and real VEX/EVEX instructions in code. The runtime allowlist is unchanged. Full normal matrix qualification
remains required before the scanner extension is complete.

For the observed hoisted R8 setup in the inlined clipboard switch, only the exact contiguous eight-instruction
prelude is recognized: move EAX to EDX, store EAX at an RSP+disp8 address, move EAX to R12D, test EAX, forward JE,
LEA EAX with disp8, compare EAX with imm8, then forward JA. Every instruction preserves R8. Both branch destinations
must decode after the computed jump and before the table. This form additionally requires the exact anonymous
PDB data symbol in the owning contribution. A partial sequence, clobbered base, wrong instruction, invalid branch
or absent data symbol receives no exemption. Adjacent real VEX remains rejected. No generic instruction skipping
or new runtime allowlist entry is permitted.

The observed Release color-settings switch has a second exact terminal form: `MOVSXD RSI,EDX`, `MOV RDI,RCX`,
an RSP+32 DWORD zero store, a direct call, the same slot set to one, unsigned `CMP ESI,imm8` / long `JA`,
`LEA RDX,imagebase`, `MOV ECX,[RDX+4*RSI+table]`, `ADD RCX,RDX`, `JMP RCX`. The producer and prelude are
mandatory because a 32-bit guard alone does not bound an arbitrary 64-bit RSI. Every row must decode and match
the PE; the nonnegative maximum is 1–127. The exact same-owner anonymous data symbol begins the DWORD table;
its guard-derived end is the owning PDB procedure end. Every target and the default decode after the dispatch
and before the table. Interior symbols and outside branches entering the prelude or table invalidate proof.
The terminal INT3 and two-byte NOP must match both PE and listing. Only the proved table receives data status;
the existing checked INT3 suffix rule applies to a decoded row that crosses its end. Wrong producer, guard,
base/index, rows, symbols, targets, boundaries or padding and adjacent real VEX remain rejected. Independent
parser controls cover each refusal. Runtime and tier allowlists are unchanged.

The observed Release terminal enum switch uses exactly six contiguous rows: `CMP R13D,imm8`;
`JA rel32`; `LEA RDX,imagebase`; `MOV ECX,[RDX+4*R13+tableRva]`; `ADD RCX,RDX`; `JMP RCX`.
Only these 30 bytes and a positive unsigned maximum 1–127 establish its table extent. The anonymous,
untyped, same-owner PDB symbol starts exactly at the aligned table RVA; maximum+1 DWORDs end at the
owning procedure end inside the same executable contribution. Each row must match the PE and decoded
boundaries. The default and every table target must be decoded body boundaries after dispatch and before
that procedure's first data symbol. Complete preceding code must be known, contiguous and PE-matching,
ending in RET or RET followed by `0f 1f 00`. An earlier contiguous anonymous DWORD span requires valid
same-procedure body targets outside the protected guard. Direct branches/calls, including legacy prefixes
and LOOP-family branches, cannot enter the guard interior, alignment or either data span. The selected
table's reported rows must also be known, contiguous, nonoverlapping and PE-matching; duplicate/missing
rows provide no proof. A final scalar listing row may consume matching bytes after the logical table end;
a crossing vector row remains subject to the existing checked suffix rule. This proves data only, not a
general upper-bit invariant for R13, and adds no object/function or runtime-allowlist exemption. Parser
controls and unchanged failed-image verification are separate from full native profile qualification.

The observed Release worker-priority loop hoists `R14=imagebase` across exactly 35 contiguous rows (132 bytes).
Only its complete fixed producer, two stack loads, predicates, zero-extended EAX index, unsigned guard of five,
DWORD load, ADD/JMP, three case bodies, call, candidate update and bounded loop back edge prove this form.
Every row and boundary MUST match the PE. Its one loop call targets outside the procedure and relies on the
Windows x64 nonvolatile-register ABI; there is no general instruction skipping or same-procedure LEA proof.
Outside direct branches/calls, including legacy-prefixed and LOOP-family transfers, cannot enter the protected
loop interior. Other indirect/far jumps, far calls/returns and same-procedure direct calls invalidate proof.
The six DWORDs start at an aligned, anonymous, untyped same-owner PDB record and end exactly at the owning
procedure end. Each target MUST be one of the two independently decoded ordinary case starts. The full preceding
procedure is known, contiguous, nonoverlapping and PE-matching, ending in a decoded backward JMP to a body
boundary before the producer followed by exactly `66 90` alignment. No other same-owner data record lies in
the procedure. Direct transfers cannot enter that alignment or table. The table listing also MUST be known,
contiguous, nonoverlapping and byte-identical; a final row may consume only the existing checked `INT3` suffix
when receiving vector-data status. Wrong or missing producer, loop, guard, row, symbol, extent, target or suffix
provides no exemption. Real adjacent vector code remains gated. This changes no runtime, object, function,
address or tier allowlist; parser/readback proof is separate from native profile qualification.

The observed ASan Menu switch also has an unsigned wide-stack form: eight-byte `CMP [RSP+disp32],imm8`,
six-byte unsigned `JA`, seven-byte zero-extending `MOV EAX,[RSP+same disp32]`, then the exact contiguous
`LEA RCX,imagebase; MOV EAX,[RCX+4*RAX+tableRva]; ADD RAX,RCX; JMP RAX`. The nonnegative maximum 1–127
bounds maximum+1 aligned DWORDs inside the same executable contribution and containing PDB procedure.
The complete guard, dispatch and decoded body through the immediately preceding RET MUST match the PE.
Default/case targets are known body instruction boundaries after dispatch. Interior anonymous symbols,
overlapping/unknown/missing rows, bad targets and direct body transfers into the table, including archived behavioral reference prefixes and LOOP-family branches, refuse proof.
An anonymous symbol at the table start is optional for this exact form; a later unrelated data symbol
cannot extend its guard-derived extent. Only the exact table bytes receive data status and must match
the listing. Real adjacent vector instructions remain gated. Collecting candidate body rows grants no
exemption. `IsaWideUnsignedGuardTests.ps1` retains prior controls and checks three accepted forms and
27 independent refusals. No runtime, object, function, tier or address allowlist changes.

The observed pinned Menu immediate-R9 switch is a separate exact terminal form: `MOVZX EAX,[RDI+1]`,
`CMP AL,max`, unsigned `JA rel32`, immediate `LEA R9,imagebase`, `MOV EAX,[R9+4*RAX+tableRva]`,
`ADD RAX,R9`, `JMP RAX`. All seven PE/listing rows, same-contribution guard/dispatch, anonymous data
start, exact maximum+1 DWORD terminal PDB extent, known immediately preceding RET, and forward
case/default decoded boundaries are required. Maximum is 1–127. Only those proved data bytes are
exempt, and their reported bytes must match the PE. Real vector code in the body, target or adjacent
range remains gated. `IsaR9DirectTests.ps1` provides 64 independent controls; neither DxUi nor the
runtime/object/function/tier/address allowlists change. [Exact readback](../TestRuns/IsaR9Direct_2026-10-17.md).

Every x64 configuration also compiles scalar, AVX2 and AVX-512 scan controls. Controls are never executed, so they
work on a baseline host. Deliberate AVX2 in a baseline object and EVEX in an AVX2 object MUST fail; correctly
placed tier code MUST pass, with a separately counted EVEX instruction in the AVX-512 control. A runtime-named
object from an unapproved library and a same-named function in an unapproved object MUST fail. Missing and
mismatched PDB controls MUST fail. Reports, binary/PDB/allowlist hashes, disassembly, full
PDB records and control logs live under `.build/IsaScan/`. `-Clean` and ARM64 builds do not invoke this x64 gate.

## Bounded selection offset source authoring

`Build/GenerateBoundedSelectionOffset.py` reproduces the isolated pinned Clipper2 2.0.1 offset fork.
It requires explicit `--source`, fresh `--output`, `--clang-format`, `--format-style`, `--base-engine`
and `--boolean-generator` paths. Exact upstream hashes, the existing base generation manifest and
the shared lexical loop rewriter are verified. The base workspace/license are copied verbatim; the
offset source retains normalization/offset/cleanup precision and ordering, scoped arena allocation,
explicit Debug container initialization and cancellation checkpoints. The output provenance manifest
records authoring only. Reproduction does not establish runtime, admission, lifetime or G1 qualification.

## Solution shape

`RedPrism.sln` contains `RedPrism` (Win32 GUI), `HostTests`, `DocumentTests`, `IoTests`, and `CompositionTests`.
`Common/` sources compile into those test and app projects. HLSL under `RedPrism/Shaders/` is compiled with FXC into
headers consumed by `CanvasRenderer`.

## Validation

Activating a private candidate must produce current source timestamps and use `-Rebuild` for every qualification
profile that may contain older objects. Copying files with their old timestamps can leave an object compiled
against an earlier class layout even when the candidate's source hashes are correct. A passing source freeze
alone does not prove that all linked objects were rebuilt from those inputs. Preserve failed images, matching
PDBs, build tracking and source timestamps before repairing or rebuilding a mixed image. Ordinary incremental
development builds remain available; a copied candidate with uncertain object provenance requires forced rebuilds.

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


The observed ASan short-stack Menu form MUST use a complete proof: DWORD `CMP [RSP+disp8],imm8`
with maximum 1–127, short unsigned `JA`, signed `MOVSXD RAX,[RSP+same disp8]`, and exact contiguous
RCX image-base/load/add/jump dispatch. The anonymous same-owner DWORD table MUST end at the exact
PDB procedure end, immediately follow `RET; NOP` with one-byte NOP, and contain only known PE-matching
case boundaries after dispatch and before the RET. The default MUST also be a known case-body boundary.
Every row from procedure start through terminal padding MUST be contiguous, known, nonoverlapping and
PE-matching. Direct branch/call/LOOP-family transfers, with legacy prefixes, MUST NOT bypass the guard
or enter padding/data. A short-stack one-byte NOP candidate MUST be selected before the generic forward reader, and a refused proof MUST NOT fall back to either the generic or older target-only reader. The distinct long-JA form keeps its existing proof. Unmapped candidate probing supplies no terminal evidence.
Only the proved table receives data status; real vector instructions in its body stay gated. The existing
PE-matching INT3 suffix rule still governs a decoder row that crosses the exact table end.

The independent short-stack suite MUST retain five accepted controls (six-entry exact layout, shared-case
default, bounds 1/127, and a PE-matching INT3 straddle) and 58 refusals. These include malformed guard/index/
base/load/add/jump, PDB/table/procedure ownership and extents, unknown/missing/overlapping or contradictory
rows, wrong terminal, out-of-body or interior targets, prefixed and LOOP-family entries, and real vector
instructions in the body or directly after the exact table end. A genuine following instruction MUST NOT be
consumed as another target RVA even when its byte encoding equals a valid case address.


### R9 byte-switch two-byte alignment sibling

The existing exact R9B-guarded switch may end with `RET; CALL rel32; INT3; 66 90`
instead of the one-byte NOP tail. Both tail encodings require complete PE-matching decoded rows,
the same anonymous terminal PDB table extent, complete preceding body and valid case/default targets.
Only the two-byte sibling additionally accepts a short JNE into the shared JA when its immediately
preceding R9B comparison uses exactly the same maximum. Other incoming guard transfers refuse proof.
Its reported vector-data bytes must match the PE. The existing one-byte form stays covered.
`IsaR9ByteSwitchTests.ps1` adds 109 independent controls (7 accepted, 102 refused); actual vector code
in the body, target or adjacent range remains gated. No DxUi or allowlist change is involved.
[Original failed-image and scanner regression evidence](../TestRuns/IsaR9AlignedTail_2026-10-17.md).


### R8/RAX/RDX guarded interior DWORD table

The observed pinned WindowHost switch adds only this exact contiguous seven-row, 36-byte sequence:
`LEA EAX,[R15+disp32]; CMP EAX,max; JA rel32; LEA R8,imagebase; MOV EDX,[R8+4*RAX+tableRva]; ADD RDX,R8; JMP RDX`.
Maximum is 1–127. Complete PE/listing agreement, one owning executable contribution and exact anonymous
same-owner PDB start/end MUST prove maximum+1 DWORDs. A unique containing procedure supplies complete
known, contiguous code before its first anonymous data symbol; code/default/case routing and no incoming
guard-interior/data transfers remain required. Its code ends with RET or a backward JMP plus at most three
alignment bytes. The table can follow earlier anonymous tail data; later data is not thereby proved.

A separate LLVM LOCK row requires an adjacent PE-matching memory INC/DEC/XADD row. Orphan, unknown,
register-only, missing, mismatching or non-atomic suffixes refuse this proof. Only verified table bytes receive
data status; actual vector instructions in code remain gated. `IsaR8RaxRdxTests.ps1` adds 107 encoded controls
(22 accepted, 85 refused) to every x64 scanner regression. No DxUi or allowlist change is involved.
[Exact failed-image and regression evidence](../TestRuns/IsaR8RaxRdx_2026-10-18.md).

## Joint unsigned stack switch tail data

The observed ASan unsigned switch pair may end its exact PDB procedure with a DWORD table, a byte remap, and a second DWORD table after one RET/NOP. Recognize only the first contiguous 42-byte CMP DWORD [RSP+disp8],imm8 / unsigned near JA / zero-extending MOV EAX from the same slot / RCX image-base LEA / MOVZX EAX byte remap / DWORD RVA load / ADD RAX,RCX / JMP RAX and the second contiguous 34-byte guarded direct DWORD dispatch. Both maxima are 1–127. The remap has exactly first maximum+1 bytes, each below the first DWORD entry count; all three spans have exact anonymous same-owner PDB starts. The second maximum+1 DWORDs end exactly at the containing PDB procedure end within its contribution.

Require known nonoverlapping decoded rows and matching PE bytes throughout the complete body through RET and its one-byte NOP. Both defaults and every table target stay after the second indirect jump and before RET. Direct edges cannot enter either guard/dispatch interior from outside, padding/data, or an instruction interior. Only the two proven indirect jumps are accepted; unproved indirect transfers refuse the data proof. A bounded candidate-procedure collection of at most 1 MiB grants no exemption by itself. Every claimed table-data row must match its actual PE bytes. Other vector instructions in the body, after the tables, or at a later anonymous symbol remain subject to the ordinary ISA gate. No object/function/runtime allowance changes.

`IsaSplitTailUnsignedTests.ps1`, registered in `IsaScanTests.ps1`, has 94 encoded controls: valid joint proof, both dispatches' missing/duplicate/unknown/PE-mismatched rows, remap bounds and operands, PDB ownership/extents, actual direct guard-bypass edges, body/RET/NOP integrity, malformed targets and real VEX/EVEX gating. Synthetic inputs include the observed INT3 padding and later same-owner symbol in a distinct procedure; the old scanner rejects the valid form. Controls execute no encoded native instructions. [Qualification](../TestRuns/IsaSplitUnsignedTail_2026-10-18.md).


## Direct EBX-guarded RBX terminal switch data

A second observed Release form uses the contiguous 28-byte `CMP EBX,max; JA rel32; LEA RDX,imagebase;
MOV ECX,[RDX+4*RBX+table]; ADD RCX,RDX; JMP RCX`. The unsigned maximum is 1–127. The DWORD-aligned
anonymous same-owner table ends exactly at the containing PDB procedure end, within its contribution.
Its executable body ends with an external direct CALL and INT3. All case/default targets are known decoded
boundaries after the dispatch and before that CALL. The complete body is known, contiguous, nonoverlapping,
PE-matched and bounded to 1 MiB; all six dispatcher rows and table bytes agree with the PE.

The low-DWORD comparison alone MUST NOT prove the full-width RBX index. Require a reaching exact
`MOVSXD RBX,[RBP+disp8]` within 4 KiB before CMP. Every later encoded effect through the dispatch must
preserve a sign/zero-extended DWORD in RBX. Explicitly modeled 32-bit writes, byte/word writes, reads and
ordinary Windows x64 ABI calls preserve that relation; a possible full-width clobber or unmodeled opcode
refuses proof. No direct edge may bypass the reaching producer or enter the dispatcher after CMP. Known
entries at the producer itself or from its protected region to CMP remain valid. Local calls, other unproved
indirect jumps, far/system transfers and edges into data, trap padding or instruction interiors refuse.

`IsaRbxDirectGuardTests.ps1` runs 120 encoded parser controls by default in the canonical ISA gate, including
full-RBX clobbers, unmodeled register effects, actual guard-bypass branches, valid alternate producers and
branch targets, exact row/PE/PDB/body/terminal refusals and genuine adjacent/body VEX/EVEX. Only table data
is classified; executable vectors remain gated. Synthetic bytes never execute and no runtime allowlist entry
is added. [Qualification](../TestRuns/IsaRbxDirectGuard_2026-10-18.md).


### Exact AL/RCX guarded terminal DWORD switch

The observed Release stroke-finish table is recognized only after the contiguous exact sequence
`MOVZX EAX,byte [RCX+disp32]; CMP AL,max; JA rel32; LEA RDX,imagebase;
MOV EAX,[RDX+4*RAX+tableRva]; ADD RAX,RDX; JMP RAX`. The unsigned maximum is 1–127.
Every guard/dispatch row must have known nonoverlapping boundaries and match the PE bytes.
The exact same-owner anonymous PDB symbol starts the aligned DWORD table; `max+1` entries end
at the exact containing procedure end within its executable contribution. Every case/default
target is a decoded body boundary after dispatch and before the terminal external CALL.

The complete owning body is capped at 1 MiB, known, contiguous, nonoverlapping and PE-matching.
Its exact tail is external `CALL rel32; INT3; NOP 0F 1F 00` immediately before the table. Refuse
guard-bypass branches, trap/padding/table entries, unknown effects, other indirect dispatches,
far/system flow and local calls into this body. Bounded candidate-body row collection grants no
exemption without this complete proof. Reported vector bytes must match the PE before table-data
classification; genuine vector instructions in the body or adjacent code remain gated.

`IsaAlNearRcxTests.ps1` supplies 121 default encoded controls for valid forms and independent
guard, flow, ownership, extent, target, terminal-row and PE/listing corruptions. The encoded bytes
are never executed. The same originally rejected Release image/full PDB passes after correction;
the original failed scan remains retained and the native Application is not replayed for a scanner
repair. The runtime allowlist is unchanged. [Evidence](../TestRuns/IsaAlNearRcx_2026-10-19.md).


### Runtime function object identity

The reviewed runtime-function allowlist matches a Windows object basename with ordinal case-insensitive
comparison and the complete C/C++ procedure name with ordinal case-sensitive comparison. Both complete
names, the exact matching PE/PDB procedure range and its owning contribution are required. Object or
function prefixes/suffixes, a different object, and function-name case changes MUST remain refused.
Linker path casing alone MUST NOT reject an otherwise identical approved runtime procedure. This rule
adds no runtime allowlist entry and grants no whole-object exemption.

### Unsigned R9D / RCX / R8 terminal DWORD table

Recognize only the observed seven-row, 30-byte `CMP R9D,max; JA rel8; LEA RCX,imagebase;
MOVSXD RAX,R9D; MOV R8D,[RCX+4*RAX+tableRva]; ADD R8,RCX; JMP R8` form. The unsigned maximum
is 1–127; it establishes a nonnegative signed-extended index. Every guard, dispatch, complete body
and table listing row MUST match the PE without overlaps or unknown instructions. One valid owning
PDB procedure (bounded to 1 MiB) and its anonymous untyped same-contribution terminal data symbol
prove exactly max+1 aligned DWORDs. Earlier anonymous data does not qualify this form.

The body ends at RET or RET plus exact `0f 1f 00` alignment. Default and case targets are known
forward body boundaries after dispatch and before data; the default is represented in the table.
Direct edges cannot enter the guard interior, instruction interiors, padding or data. Other indirect
jumps and orphan prefixes refuse the proof. Candidate body collection grants no exemption. Only
proved table bytes receive data status; real VEX/EVEX instructions remain gated everywhere else.
`IsaR9dRcxR8Tests.ps1` contributes 85 encoded controls, including conflicting scopes and genuine
vector body/target/adjacent cases. [Failure and qualification](../TestRuns/IsaR9dRcxR8_2026-10-21.md).

## MSVC 14.52 Release compilation and linkage

All native profiles require the installed default MSVC **14.52.x** family with the v145 platform toolset. Discovery remains installation-independent. Restore rejects another default family before dependency compilation; MSBuild validates the selected family before compilation. Record exact patch/compiler/linker identities; GL archives and their final links require exactly matching tools.

Release explicitly uses /Gy and /GL, /OPT:REF and full /LTCG, with ordinary incremental linking disabled. Debug/ASan diagnostic optimization settings are unchanged. Preserve existing project-specific size/speed, warning, ISA, CRT and subsystem choices. Evaluated-profile tests check all six native configurations, pinned archives, and negative compiler/linker controls; resource-only satellites contain no C++ bodies.

Project references explicitly forward WholeProgramOptimization and VCToolsVersion into the adopted DxUi archive. Dependency identity includes releaseOptimizationPolicy=Gy-REF-GL-LTCG-full-v1 and is rehashed before choosing writable outputs. This separates the new archive policy without changing the adopted API/source pin. Keep header/library/compiler/CRT identities matched.

LTO is scoped to each final EXE/DLL and does not guarantee reduced size or improved speed. Retain baseline/candidate image hashes, sizes, build/link durations, fixture and dependency identities, every runtime/resource sample and concurrent-activity observations. Different compiler policies are an intentional experiment, not identical-profile evidence reuse. Cross-built ARM64 and headless functional passes cannot close native ARM64, interactive, or existing product/performance acceptance gates.

### MSVC 14.52 CRT inline wrapper

The exact runtime allowance includes IO.Tests.Runner.obj!std::_WChar_traits<wchar_t>::find after reviewed /GL inlining of UCRT wmemchr. The matching PDB inline-site, installed SDK source and linked flag branch establish its attribution. Only its exact procedure range is allowed; all surrounding code remains gated. IsaScanTests.ps1 adds exact-name and refusal controls. See [attribution and qualification](../TestRuns/Msvc1452ReleaseOptimization_2026-10-23.md).


### CI toolchain preparation and audit evidence

Active Windows compilation/tooling workflows use the local setup-msvc composite action. It reads RequiredMsvcToolsVersion, verifies the discovered default compiler, linker, ASan target libraries, SDK and x64 LLVM scanner tools, and provisions Visual Studio 2026 Insiders only on GitHub-hosted runners when necessary. The Microsoft-signed bootstrapper runs under RUNNER_TEMP; existing installations are retained. A channel that no longer supplies 14.52 fails rather than accepting another compiler. Local -VerifyOnly is read-only; automatic local installation is refused. Hosted provisioning remains a separate unexecuted branch until its actual CI receipt exists.

[Full evaluated settings and differences](../../Measurements/Toolchain/2026-10-23-msvc1452/Settings.md) include both platform/configuration matrices and exact per-file metadata. [Microsoft installation guidance](https://learn.microsoft.com/en-us/cpp/overview/acquire-msvc) documents channel-specific acquisition. The published [runner image inventory](https://github.com/actions/runner-images/blob/main/images/windows/Windows2025-VS2026-Readme.md) does not establish that the required toolset is installed; runtime versions alone are also not compiler proof.

Hosted setup waits for the hidden, owned GUI bootstrapper with both its --wait switch and PowerShell Start-Process -Wait. It reads that process's explicit exit code (0 or 3010), then rechecks the required compiler family and complete target tools. Ambient LASTEXITCODE cannot establish installer success. Paths with spaces stay one argument; installer stdout, stderr and fresh dd_* logs remain downloadable on failure. Delayed-GUI, stale-exit, wrong-family, missing-tool, signature and local/read-only refusal controls exercise the actual setup script without running an installer.

The hosted action exports MSBUILD_EXE_PATH through GITHUB_ENV from the exact installation it verified, preferring its native 64-bit MSBuild and requiring the chosen executable to exist. Build and restore therefore consume that verified toolchain ahead of the runner's older environment or PATH entries. Local -VerifyOnly does not publish or change compiler selection. The regression applies the exported value to the actual product discovery function and retains the older-ambient counterfactual; rejecting MSVC 14.51 remains required.

The [native policy, console and dependency-path contract](Build_ConsoleAndPaths.md) owns /Wall without /WX, zero-warning acceptance, Debug/ASan symbols, Release-only ICF, common terminal results and verified short dependency folders.

## Repository skill validation

`validate-skills.ps1` uses the local `Build/SkillValidation.psm1` on PowerShell 7. No Python,
pip package, external YAML module or developer Codex installation is required. The routine CI tooling
job runs the same validator and behavioral fixtures once; native jobs retain their independent coverage.

The supported front matter is an explicit YAML subset: exact top-level keys `name`, `description`,
`license`, `allowed-tools` and `metadata`; plain, single-quoted or JSON-compatible double-quoted scalars;
two-space-indented literal (`|`) and folded (`>`) block scalars with optional `-`/`+` chomping; comments;
and a one-level, two-space-indented scalar mapping for `metadata`. Duplicate keys, flow collections,
tags, anchors, aliases, arbitrary nesting and unsupported syntax fail validation. Required strings retain
YAML 1.1 scalar-type rejection, exact folder/name identity, 64/1024-character limits and nonempty
instructions. Description TODO placeholders and standalone body TODO placeholders outside Markdown
backtick/tilde fences fail. This is a deliberate restricted grammar, not general YAML compatibility.

`Tests/BuildProcessTests/SkillValidation.Tests.ps1` covers valid and malformed metadata, Unicode,
CRLF, multiline values, boundaries, fenced examples and aggregate failures. Its parser fixtures run in
memory and its small aggregate fixtures stay under repository `.build`. Python is not discovered or
included in tooling receipt identity. Historical measurement scripts remain reproduction artifacts;
manual source generators and upstream dependency tools retain their separate prerequisites.
