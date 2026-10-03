# RedPrism build toolchain

Status: current normative repository contract
Last reviewed: 2026-10-02
Owner: root MSBuild projects, `vcpkg.json`, `vcpkg-tool.json`, `build.ps1`, `Build/BuildPresentation.psm1`, `vcpkg-install.ps1`, `Build/VcpkgToolset.psm1`, `Build/VisualStudioDiscovery.psm1`, `format.ps1`

## Scope

This contract owns the Windows C++ toolchain, configurations, generated-output layout, vcpkg pin, and the root
build entrypoints. Runtime windowing lives in [`UI/UI_Windowing.md`](../UI/UI_Windowing.md). Architecture ownership
lives in [`Core/Core_Architecture.md`](../Core/Core_Architecture.md). Test orchestration lives in
[`Testing/Testing_Validation.md`](../Testing/Testing_Validation.md).

DxUi restore is C2 work: `restore-dxui.ps1` clones the lock in `Dependencies/DxUi.lock.json` into
`.build/dependencies/DxUi/` and never resets `Z:\src\DxUi`. The clone keeps Git long paths and leaves out the directories a
build never reads (`Measurements/`, `docs/gallery`, `Specs/`), so a deep checkout root restores. See
[`Core/Core_DxUiIntegration.md`](../Core/Core_DxUiIntegration.md).

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
| vcpkg overlay triplets | `.build/vcpkg-triplets/` |
| vcpkg tool checkout | `.build/vcpkg-tool/` |
| MSBuild logs | `.build/logs/` |

`.build/` MUST NOT be committed. `OutDir` / `IntDir` are set in `Directory.Build.props`.

Builds sharing a mutable dependency output MUST be serialized, including private candidate builds. A separate
application `OutDir`/`IntDir` does not isolate a pinned DxUi rebuild that writes the same resolved library path.
Parallel compilation requires isolated dependency outputs or an already-built immutable dependency; preserve
failed source/object/link evidence before retrying a candidate affected by shared-output rebuilds.

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

vcpkg builds with the Visual Studio installation and the default MSVC toolset that MSBuild compiles RedPrism with, never with
the newest toolset it finds. `vcpkg-install.ps1` takes the installation from the MSBuild that `build.ps1` discovered
(`-MSBuildPath`; the installation holding its `MSBuild\Current\Bin`, so `MSBUILD_EXE_PATH` and `PATH` are honored, and a
standalone run finds the MSBuild `build.ps1` would), and the toolset from that installation's
`VC\Auxiliary\Build\Microsoft.VCToolsVersion.default.txt` (MSBuild reaches the same version for `v145` through
`Microsoft.VCToolsVersion.v145.default.props`; the two agree on the installation this was written against, and the choice
follows DxUi's `Get-DxUiDefaultToolset`). Neither is hard-coded, and a missing or malformed version file
fails before anything is cloned, naming the file and the repair. The two differ when a newer toolset is installed beside the
default: VS 18 Insiders' 14.52, beside the default 14.51, has no x64-hosted ARM64 compiler, so every fresh ARM64 restore failed
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
AVX2 objects allow VEX; AVX-512 objects allow VEX and EVEX. Other objects must match the checked-in
`Build/IsaRuntimeAllowlist.psd1`. No broad first-party object exception is permitted.

The runtime exceptions are MSVC `vector_algorithms.obj` from `msvcprt[d].lib`, and the exact PDB procedure range
of the runtime-dispatched `wmemchr` intrinsic in the observed `ImageCodecs.obj`, `HostTests.obj`, `IoTests.obj` or
`SettingsStore.obj` (the CompositionTests worker/runtime link).
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
Unrecognized table/dispatch forms receive no exemption.

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
allowlist exception. These parser controls remain distinct from the
compiled ISA controls and cover broken dispatch, bounds, targets, trailing return and adjacent real VEX code.

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

The observed Release variant form loads a signed byte into RAX from RBP+disp8 or RBP+disp32 immediately before
LEA RCX of image base. Only `48 0F BE 45 <disp8>` or `48 0F BE 85 <disp32>` proves that index; a different
destination, REX prefix, frame base, SIB or load width does not. Its DWORD load can point exactly one entry after
the anonymous PDB table start: index -1 addresses
the first/default entry. Only the exact contiguous MOVSX/LEA/load/ADD/JMP form and same-owner anonymous data
symbol prove that bias. The complete table, including its default entry, must target decoded body instructions
after the jump and before the table. Different registers, noncontiguity, greater bias, invalid PDB records and
invalid default/other targets receive no exemption. This adds no runtime allowlist entry.

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

For the observed hoisted R8 setup in the inlined clipboard switch, only the exact contiguous eight-instruction
prelude is recognized: move EAX to EDX, store EAX at an RSP+disp8 address, move EAX to R12D, test EAX, forward JE,
LEA EAX with disp8, compare EAX with imm8, then forward JA. Every instruction preserves R8. Both branch destinations
must decode after the computed jump and before the table. This form additionally requires the exact anonymous
PDB data symbol in the owning contribution. A partial sequence, clobbered base, wrong instruction, invalid branch
or absent data symbol receives no exemption. Adjacent real VEX remains rejected. No generic instruction skipping
or new runtime allowlist entry is permitted.

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
