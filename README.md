# RedPrism

Windows-native raster editing application in the Red family, built with Win32, Direct3D 11, DirectWrite, WIL, yyjson,
libzip, Clipper2 and DxUi. See [specifications](Specs/README.md) and the [user guide](docs/README.md).

## Recording changes

Follow the [Changes workflow](Changes/README.md): each change adds one fragment under `Changes/` and never edits `CHANGELOG.md`; after merges, `Tools/Submit-ChangelogFold.ps1` folds the fragments on `main` through a pull request of its own.

CI behavior and manual actions: [Unified CI contract](Specs/Build/Build_UnifiedCI.md).

## Testing

Use `./Test-Changes.ps1 -Explain` to inspect affected scopes, then `./Test-Changes.ps1` to run them.
The default advances from its last successful run; unchanged input selects no tests. Explicit partial
selections and failures do not advance that baseline. Exact successful evidence can be reused.

Use `./Test-Changes.ps1 -Mode PrePush` for the six local build profiles, with runtime testing on x64.
`-Mode Full` runs the applicable scoped obligations for one profile. IO and Runtime use the session clipboard
and require a separate, admitted desktop run.
See the [test guide](Tests/README.md) and [validation contract](Specs/Testing/Testing_Validation.md).

Builds require the default MSVC **14.52.x** tools (v145). Release uses /Gy /GL and final /OPT:REF /LTCG; archive and final-link tools must match exactly. The build contract documents the preserved API pin, explicit archive forwarding and policy-specific output identity.

Native builds use the [shared build policy](Specs/Build/Build_ConsoleAndPaths.md):
/Wall without /WX, zero-warning validation, Debug /ZI and ASan /Zi, and
Release-only /OPT:ICF. A passing build prints a green result with artifact/log
links and a quoted launch command. OSC 8 capable terminals can open the local
file links. Cross-built ARM64 applications still need ARM64 Windows to run.

Repository skill validation and its regression tests run on PowerShell 7 without Python or PyYAML. Manual Clipper source regeneration and archived analysis retain their documented prerequisites.

CI behavior and manual actions: [Unified CI contract](Specs/Build/Build_UnifiedCI.md).

Project license: [MIT](LICENSE.txt). [Third-party notices](THIRD-PARTY-NOTICES.md) retain their respective licenses.
