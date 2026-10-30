# RedPrism packaging

Status: current normative repository contract
Last reviewed: 2026-09-21
Owner: `package.ps1`, `Common/Version.h`, `build.ps1` (the version stamp)

## Scope

How a RedPrism build becomes something a user can download and run: the portable ZIP, what it carries, how it is
named and checked, and how it is proven. Installers, winget and code signing are follow-ons named at the end. There
is no auto-update: RedPrism never contacts a server ([`Core_CrashHandling.md`](../Core/Core_CrashHandling.md),
[`Core_Settings.md`](../Core/Core_Settings.md)).

## Entry point

`.\package.ps1 [-Platform x64|ARM64] [-SkipBuild] [-SkipSmoke] [-OutputDirectory <dir>] [-BuildNumber <n>]`

1. Builds test-disabled `Release -ProductionOnly` for the platform through `build.ps1` (the same preflight: a running `RedPrism.exe` blocks the
   build and is never terminated), unless `-SkipBuild`.
   The exact source, artifacts, profile, frozen build number and disabled test role must match the build receipt.
2. Reads the version from the built executable (`OriginalFilename` `RedPrism.exe`, `ProductName` `RedPrism`,
   file version `0.1.<build>.0` from [`Build_Toolchain.md`](Build_Toolchain.md), where `<build>` is the Git commit
   count of `HEAD`). The package version is `major.minor.build`; `Common/Version.h` stays authoritative and the
   script never edits it.
3. Stages `RedPrism-<version>-<Platform>\` under `.build\packages\stage\`, zips it to
   `.build\packages\RedPrism-<version>-<Platform>.zip` (the folder is the ZIP's root) and writes
   `<zip>.sha256` (`<hash> *<name>`, the `sha256sum` form winget's `InstallerSha256` can take).
4. Runs the clean-extraction smoke.

`.build\` is never committed; packages live there only.

## Staging rules

| Staged | Rule |
| --- | --- |
| `RedPrism.exe` | The Release executable only; test executables and PDBs never ship |
| Dependency DLLs | The executable's import table, walked transitively with `dumpbin /dependents`: a name found next to the executable (vcpkg's app-local copies: `yyjson.dll`, `zip.dll`, `z.dll`) or in the Visual C++ redistributable CRT folder is staged and walked in turn; every other import is Windows' own. Without `dumpbin` every DLL next to the executable and the standard CRT set are staged instead |
| Visual C++ CRT | App-local from the installed Visual Studio's `VC\Redist\MSVC\<version>\<platform>\Microsoft.VC*.CRT` (`vcruntime140*.dll`, `msvcp140*.dll` as imported). Release links `/MD`; the Debug CRT is not redistributable, so a package is always Release. The Universal CRT ships with Windows 10 and later and is not staged |
| `ThirdPartyNotices\<port>.txt` | vcpkg's `share/<port>/copyright` for `yyjson`, `libzip`, `zlib`, `clipper2` and `wil` |
| `ThirdPartyNotices\lucide.txt` | `ThirdParty/Lucide/LICENSE` (ISC, plus the MIT notice for the Feather-derived icons) |
| `ThirdPartyNotices\dxui.txt` | DxUi's own license (MIT), read from the pinned source `restore-dxui.ps1` leaves at `.build\dependencies\DxUi\source\<first-16-commit-hex>\LICENSE*`; packaging fails if it is missing rather than shipping `DxUi.lib` without its notice |
| `README.md`, `usage.md` | `docs/README.md` and `docs/usage.md` as shipped |
| `LICENSE.txt` | The project's MIT license; third-party notices retain their own licenses |
| `Languages\<tag>\RedPrism.resources.dll` | Not staged in v1 (no translation exists); a release with one adds the folder ([`UI_ApplicationChrome.md`](../UI/UI_ApplicationChrome.md), Strings) |

Nothing else: no settings template (the app writes it), no crash folder, no registry work.

## Clean-extraction smoke

The ZIP is expanded into a fresh folder and the extracted executable runs with a `Path` reduced to
`%SystemRoot%\System32`, `%SystemRoot%` and `System32\Wbem`, so a DLL the package forgot cannot be found on the
developer's machine: `--help` MUST exit 0 and print the command catalog; `--self-test --warp` MUST refuse with exit 2;
`--screenshot <owned-path> --warp` MUST exit 0 and create a nonempty hidden native capture. The extracted executable
MUST carry the packaged file version. Smoke evidence is retained. The full in-product regression suite belongs to
the preceding test-enabled Release role; see [unified CI](Build_UnifiedCI.md). The smoke is skipped with a notice when the host cannot run the platform (ARM64 packaged on x64) or
with `-SkipSmoke`.

## What the user gets

Extract the folder anywhere and run `RedPrism.exe`. "Portable" means no installer and no registry: settings and
crash reports still live under `%LocalAppData%\RedPrism` per user, never next to the executable
([`Core_Settings.md`](../Core/Core_Settings.md)). The executable is unsigned in v1, so SmartScreen may ask once on
first run; the `.sha256` beside the download is the integrity check.

## Follow-ons (not v1)

- **winget**: a manifest needs a public, stable download URL and the `.sha256` above; it is a submission to
  `microsoft/winget-pkgs` once such a URL exists. Nothing in the package changes for it.
- **Code signing**: a certificate and `signtool` on `RedPrism.exe` before zipping; the script gains one step.
- **MSIX or an installer**: only if file associations for `.prism` or Start-menu integration are wanted.
- **Auto-update** (the legacy Sparkle): out of v1 by decision; a later update check would be opt-in and documented
  before it exists.

## Validation

`package.ps1 -Platform x64` exits 0 with the smoke passed; `package.ps1 -Platform ARM64` exits 0 (smoke skipped on
an x64 host). Recorded at each release and in the C15 closeout with the ZIP names and sizes. It is a release gate,
not part of `test.ps1`.

RedMonitor is installed independently. Application packages include its client MIT notice and no viewer executable, runtime, satellite or theme payload. Normal builds verify only the pinned local header/license; see [RedMonitor integration](../Core/Core_RedMonitorIntegration.md).
