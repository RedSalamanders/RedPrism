# P1 pinned native ARM64 toolchain provisioning

Status: integrated; exact-function host-selection controls pass. Real CI installation and runtime remain pending.
P1 remains WIP.

## Why this change exists

The retained [r6 native run](https://github.com/RedSalamanders/RedPrism/actions/runs/39591618490) correctly refuses
the hosted image's default MSVC 14.51.36231 before canonical work. Current local qualification uses 14.52.37025;
the earlier compiler cannot qualify that environment. The default-version guard remains unchanged.

The existing disposable native workflow now provisions the fixed official BuildTools catalog 18.12.12316.0
side by side at `C:\P1NativeTools1452`. The helper binds bootstrapper, channel and catalog by fixed URL and
SHA-256, validates the bootstrapper's Microsoft signature, passes local pinned manifests, and requests native
ARM64 tools plus SDK 26100. Microsoft documents local channel/catalog input in its
[installation guidance](https://learn.microsoft.com/en-us/visualstudio/install/automated-installation-with-response-file?view=visualstudio).
No tools are installed on the user's computer by this change.

## Independent input inspection

Main verifies the official metadata and bootstrapper bytes/signature and inspects the native MSBuild/compiler
VSIX payloads without executing them. Both packages match their publisher SHA-256 and contain AA64 images.
The catalog's declared package sizes differ from the observed download sizes; both original observations and
the initial strict-size reader refusals are retained. The accepted identity is the matching publisher digest,
with the mismatch explicitly recorded rather than silently altering the catalog claim.

The native MSBuild image SHA-256 is `7C67299F429B832719ACBDC43240F6BF5D50B26014E7462E361392C0A261A60B`;
the 14.52.37025 native compiler image is `745C496597A6658804E1A8DBD514CE916783E342B31BFE974ED2A867E06078C8`.
The installed files must match these independently inspected members before host selection. Installer failure,
restart requirement, existing installation target and identity mismatch refuse the run. Expected and actual
tool hashes are written before mismatch refusal; failed log collection cannot replace a primary installer error.
The workflow also retains Release intermediates and the selected DxUi library/identity for independent readback.

## Reviewed integration and controls

The bounded helper review finds two diagnostic defects in the private draft: `finally` log copying could mask
the primary failure, and an installed-tool mismatch discarded the observed hashes. Main confirms and corrects
both before integration. All original worktree hashes match before copying the reviewed helper, existing
regression fixture and workflow. The production DxUi pin remains API3.

The actual worktree regression fixture runs with PID 87920 and exits 0. Main reads complete stdout/stderr,
verifies all 74 unique checks, every preceding 60 check name, all four unchanged source bookends and nine case
entries. Five actual child receipts comprise two read-only environment observers and three typed argument
refusals. Six exact initializer cases cover ordinary and explicitly provisioned selection, wrong defaults and
missing compiler. Compiler/MSBuild stand-ins never execute. The helper's installer body and native application
are not executed by these controls.

Evidence lives under `P1Evidence/p1-native-arm-hosted-r6` through its S: junction: `ProvisioningInputs`,
`ProvisioningDraft`, and `native-provision-controls-integrated-r1/main-qualified.json`.
Control-result SHA-256: `BFE94449AFBA8F6EBB74E6E3BA031F385EBCBF5A7E77C5BC63D96DBF3DAF5CF6`.

Publication contains code, regression controls and contracts together. A real disposable CI installation and
canonical native run remain required. Provisioning, parser controls and cross-builds confer no native runtime,
DPI repair, G1 or P1 completion.
