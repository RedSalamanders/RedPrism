# DxUi API revision 3 pin, default-toolset vcpkg restore and sparse DxUi restore — 2026-10-01

Status: implemented and qualified on the branch `dxui/revision-3-2026-10-01`, which carries `origin/main` `ca51108`. The unpatched
x64 builds pass the ISA gate in Release, Debug and ASan Debug; `test.ps1` passes in all three with the checks that need the clipboard
left out (and with the local-only patches listed below); the three ARM64 configurations build. The clipboard checks ran and passed on 2 October, after merging the
newer `origin/main` (last section); Tree multi-select (DxUi #35) remains open. RedPrism has no CI, so this record is the qualification. Plan:
[DxUiFollowUps_2026-10-01.md](../Plans/WIP/DxUiFollowUps_2026-10-01.md).

## What changed

- **Pin.** `Dependencies/DxUi.lock.json` moves from `c403052` (API revision 2) to `271bd54` (DxUi #45, API revision 3), and
  `restore-dxui.ps1` requires revision 3. `ChromeHost.h` implements `ITreeModel` and `ITreeDelegate` where it implemented
  `IDxTreeModel` and `IDxTreeDelegate`; it implemented no grid interface. The product attaches no `DxUi::WindowHost`, copies no
  DxUi `WM_APP` value and posts no menu-bar hover, so message forwarding needed no change
  ([Core_DxUiIntegration.md](../Core/Core_DxUiIntegration.md)).
- **vcpkg toolset.** `vcpkg-install.ps1` resolves the MSBuild the build uses (and refuses one that does not exist), takes its Visual
  Studio installation and default MSVC toolset, and passes generated overlay triplets (`.build/vcpkg-triplets`, all four triplets, the
  ASan ones chain-loading `asan-toolchain.cmake`) that pin both. Before, the ARM64 restore chose MSVC 14.52.36725, the newest toolset,
  which has no x64-hosted ARM64 compiler, and `clipper2:arm64-windows` failed to configure; MSBuild's default is 14.51.36231. After,
  all four restores passed with the toolset pinned to 14.51 (274 s x64, 290 s x64 ASan, 439 s ARM64, 265 s ARM64 ASan, packages
  rebuilt; they ran on the tree before the merge and were not repeated, because no input changed). The triplet change alters the
  package ABI hash, so the first restore rebuilds the packages ([Build_Toolchain.md](../Build/Build_Toolchain.md)).
- **DxUi restore.** `Build/DxUiRestore.psm1` clones with `core.longpaths=true` (stored in the clone's own configuration), checks out
  sparsely without `Measurements/`, `docs/gallery` and `Specs/` (179 of 1,657 files at the previous pin), and clones blobless from the
  public repository. `DxUiRestoreTests.ps1` restores a fixture with a 107-character kept path into a destination of about 210
  characters; `-RealPin` restores the locked commit the same way. A restore that fails part way leaves its directory and a later
  restore keeps it, as before; the failures now say to delete it.
- **No dialogs from test runs.** `Tests/Support/FailureReports.h` (modeled on DxUi's) routes CRT reports to stderr and ends the run
  with exit code 3 in HostTests, DocumentTests, IoTests and CompositionTests; the harness modes set the error mode.
  `FailureReportTests.ps1` runs each executable's `--failure-report-self-test` first in `test.ps1`, bounded.
- **ISA gate.** The unpatched x64 Release build of the merged tree was rejected at `0x1400962dc` in `ApplicationFileJobs.obj`: the
  first DWORD of the switch table of the compiled `std::variant` destruction, not code. `Build/IsaScan.cs` now recognizes that exact
  dispatch (signed byte index, R8 image base, RDX target, byte table biased by one entry); the finding, the proof and the 93 controls
  are in [IsaSignedBiasedByteTable_2026-10-01.md](IsaSignedBiasedByteTable_2026-10-01.md). An earlier table form of the same pin move
  (`0x140193260` in `KernelBenchmark.obj`, before the merge) is covered by the guarded RDX-base form `origin/main` carries.
- **Review corrections.** The restore figures, the CRT hook wording and a test of the MSBuild existence check (`7ac5ab8`).

## Gates

All runs are `build.ps1` or `test.ps1` (which builds first), non-interactive, from the worktree `D:\RedPrism-dxui` at the branch tip.
The unpatched builds and the ARM64 builds ran on a clean tree; the `test.ps1` runs used the local-only patches below.

| Gate | Result |
| --- | --- |
| x64 Release build, no local patch | Passed in 67 s (an incremental build). The ISA gate accepts all five linked images: `RedPrism.exe` 459 vector rows, all in the allowed runtime ranges, 8 undecoded prefixes and 3 verified table-data rows (the `_Destroy` table); `HostTests.exe` 260, `DocumentTests.exe` 126, `CompositionTests.exe` 102, `IoTests.exe` 263. The compiled scalar, AVX2 and AVX-512 controls pass. `RedPrism.exe` SHA256 `7891209215881293BD28460D2E04886D948D77DD48B8CB285DCB0C97F0FA1CE4`, PDB `CB2CA20636E76F6BB01890D38846EDF7F8BF9F5772A016BCDA555C55ECF68855` |
| x64 Debug and ASan Debug builds, no local patch | Passed in 115 s and 100 s; the ISA gate accepts all five images of each (11,690 runtime vector rows in `RedPrism.exe`, 14 verified table-data rows in both) |
| `test.ps1` x64 Release | Passed in 142 s |
| `test.ps1` x64 Debug | Passed in 412 s |
| `test.ps1` x64 ASan Debug | Passed in 761 s: the real AddressSanitizer probe diagnoses its heap-use-after-free, and the raster and worker stress runs pass |
| ARM64 Release, Debug and ASan Debug builds | Passed in 176, 126 and 127 s, each a full compile of the merged tree (compile evidence: ARM64 runtime claims need a native ARM64 host) |
| `IsaInlineTableTests.ps1` | 527 controls pass (93 are new), in every x64 build |
| `format.ps1 -Check` | Passed (412 files) |
| `validate-skills.ps1` | Not run as written: it needs PyYAML, which is not installed here and was not downloaded. Its checker ran with a local stand-in for `yaml.safe_load` (flat front matter only): all 15 skills validate |

In each of the three configurations `test.ps1` passed: the four failure-report self-tests; 2,746 host checks (2,747 in ASan Debug),
4,158 document, 727 composition and 1,191 project I/O checks (the clipboard transfer suite left out); the main and worker crash
probes; the kernel and UI benchmark smokes; the gradient runner controls and the three ISA refusals; DxUi provenance (8), build
presentation (3), build preflight (6), tool discovery (5), vcpkg toolset (8), DxUi restore from a deep root (5), ARM64 ASan
wiring (3), WPR profile (3) and RAW corpus (20) checks; the hidden WARP self-test; and nine harness screenshots. Three SKIPs are
environmental: `path.junction-load` (the symbolic-link privilege is not held), the gradient snapshot `.build/p1-gradient-qualified`
(absent) and the RAW corpus byte controls (no corpus).

## Not run: the checks that need the clipboard

IoTests' `clipboardtransfer.*` suite (125 checks at the previous pin, now also the clipboard binding tests that run inside it),
`RunClipboardSelfTest` and the self-test's system-clipboard mirror checks were not run at the new pin. At the previous pin (x64
Release, 17:56, full `test.ps1`, no exclusions) they passed: 125 of 125, and the self-test passed. At the new pin the first runs
(19:04 to 19:14) stopped in IoTests with `clipboardtransfer.save-user-clipboard error=5` (`OpenClipboard` access denied) in all
three x64 configurations, because another application held the clipboard open without a window; every other IoTests check passed
in those runs. These tests replace the clipboard's contents, so they were not retried against it. The repository has no CI. They
need a run on a machine whose clipboard is free before the pin is trusted for the clipboard path; the plan keeps that item open.

## Local-only patches

Applied to the worktree for the three `test.ps1` runs and reverted with `git apply -R` afterwards (`git status` was clean before and
after; nothing was committed with them). Both patch files are outside the repository.

1. **Display size** (`local-validation-maxtrack.patch`): `Application::OnGetMinMaxInfo` raises `ptMaxTrackSize` to 16384. This
   machine's primary display is 1920 x 1200 at 150 percent, so the default 1280 x 800 DIP client area is as large as the screen and
   the system clamps the window; the self-test then fails with "The default client area does not match the current monitor DPI", at
   the previous pin as well. Not a regression of this change.
2. **The clipboard is never opened** (`local-validation-noclipboard-merged.patch`, two files): `ClipboardScope` in
   `SystemClipboard.cpp` skips `OpenClipboard`, so nothing in the process can open or replace the clipboard; and in `Application.cpp`
   the self-test's "Pixel edits" block neither sets the clipboard aside nor checks the system mirror, requiring only the session's
   own clip, and the call to `RunClipboardSelfTest()` is removed. Every other `OpenClipboard` call is reachable only through what the
   patch removes: `ClipboardTransfer.cpp`'s scope and the suites' helpers (the IoTests clipboard suite), and `HoldClipboard` (the
   removed self-test).
3. **IoTests** (the same patch file): the call to `RunClipboardTransferTests()`, which also runs the clipboard binding tests, is
   removed.

## Branch state

`origin/main` has moved on since the merge, to `fff9fa1` ("Prepare detached clipboard Paste and inspect producer DIB order", one
commit). The branch merges into it without conflicts, and an uncommitted merge of the two builds in x64 Release, Debug and ASan
Debug with the ISA gate passing on all five images of each (186, 192 and 196 s; the merge was then abandoned). That is a build check
only: the new code's tests were not run, and everything above is the qualification of the tree with `ca51108`.

## 2 October: main merged and the clipboard checks run

The branch merges `origin/main` `d08eedd` (eight commits after `ca51108`, among them clipboard Paste and host graphs on workers)
without conflicts, as `63b6dac`. On that tree, on the same machine, with no window holding the clipboard open:

- `format.ps1 -Check` passes (487 files), and the three ARM64 configurations build.
- Unpatched, x64 Release builds and passes the ISA gate on all five images (`RedPrism.exe`: 459 VEX/EVEX instructions, 2 undecoded
  data prefixes, 4 verified table data); `test.ps1` then stops at the self-test's display check, as described above.
- With only the display-size patch (1 above, neither clipboard patch), `test.ps1` passes in x64 Debug and ASan Debug, the IoTests
  clipboard suite and the self-test's system-clipboard checks included. The user agreed to the runs replacing their clipboard.
- With that patch, x64 Release did not reach the tests. The relink moved a jump table in `DxUi.Menu.obj`, and the ISA scan rejected
  it as six `vcvtsi2ss xmm8, xmm6, dword ptr [rax]` decodes four bytes apart (`0x1402a4ae8` to `0x1402a4b04`); unpatched, that
  table is among the four verified. The scan's data proof therefore depends on link layout, and an unrelated code change can trip
  it. That is recorded as a follow-up, not changed here.

**Correction, 2 October.** The clause "unpatched, that table is among the four verified" is wrong. No dispatch recognizer covers
that switch (`MenuPointerKindFromMessage`, which indexes through RDX from an R8 image base), so the scan verified it in neither
image. Unpatched, its entries happened to decode as no vector instruction, and the four verified rows belong to other tables. The
follow-up proves such tables from their PDB records whatever the layout:
[IsaMarkedDataTables_2026-10-02.md](IsaMarkedDataTables_2026-10-02.md).
