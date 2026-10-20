# DxUi integration

Status: current normative consumer contract
Last reviewed: 2026-10-06
Owner: `restore-dxui.ps1`, `Build/DxUiRestore.psm1`, `Build/DxUiOutputPath.psm1`, `Dependencies/DxUi.lock.json`, `RedPrism/ChromeHost.*`, `build.ps1` provenance

## Scope

This contract owns how RedPrism consumes the standalone DxUi library: the exact source pin, restore/build isolation,
the single `DxUi.lib` archive linked into `RedPrism.exe`, and the EmbeddedHost overlay on the application swap chain.
Library behavior remains in DxUi's own contracts. Splitter, ColorPicker, and NumericStepper are library controls
(DxUi `Specs/Plans/Done/EditorConsumerControls_2026-09-20.md`) consumed as-is by `ChromeHost`.

## Pin and restore

`Dependencies/DxUi.lock.json` identifies `https://github.com/RedSalamanders/DxUi`, current candidate commit
`8e8994079e82f79e6765e6a885c03fc687957591`, API revision 3, and lock target `["DxUi"]`. Its identical-source full
library x64 Debug/Release/real ASan suites, three ARM64 cross-builds and paired Release common-scene comparison pass.
The managed RedPrism consumer repair passes full x64 Debug/Release/real ASan gates, three ARM64 cross-builds and
Release analysis on the 858-input freeze recorded in `Specs/TestRuns/P1LayerRowsCosts_2026-10-06.md`. Fresh hardware
and WARP metadata diagnostics preserve all initial and rename costs; full preparation, production/native activation
and external-client G1 remain open. This pin adds optional
caller-arranged interaction acknowledgement without painting; it preserves full preparation after failure, resize,
DPI/device changes or hide/zero extent. Library ARM64 results are cross-compilation only. Internal embedded property-event providers keep weak
control identity and read the coherent current snapshot; public row providers retain their original source.
Structure invalidation uses a bounded coalesced delivery slot and a GIT-marshalled provider proxy on an MTA while
the public provider retains its STA contract. The slot holds no historical row snapshot; owner disconnect revokes
registration without joining delivery. The property-only predecessor `36c0ff66bd7574038ed2e255f79847a81bf054c7`
passes the library matrix but stalls the real consumer callback inline on its owner (r211). The preceding library commit
`5f1ccdb6b5decde9f8ef6137e8b646dba51555c4` adds the optional prepared Tree accessibility source described below while
keeping API revision 3; because the public C++ interface changed, the consumer MUST rebuild DxUi and RedPrism from
matching headers and library. Library qualification alone does not qualify a consumer or its activation. The nearest rollback is
`271bd54be24eadf3f03ba5221d1be7be069860f2`, also API revision 3; it retains the existing Tree reorder callback but
does not provide the prepared accessibility-row interface. A rollback changes the lock but leaves
`$requiredApiRevision` at 3. The consumer adapter must also return to legacy Tree accessibility capture: remove the
optional prepared-source include and `CapturePreparedAccessibilityRows()` override (and the wrapper's implementation
of that optional interface), and restore the preceding full-prepare input boundary instead of calling the absent
`PrepareInteraction`, before rebuilding against the rollback headers/library. `build.ps1` runs
`restore-dxui.ps1` for the selected platform after vcpkg. Restore clones that exact commit under
`.build/dependencies/DxUi/source/<commit>` (from the sibling checkout when it already has the commit, else from the
public repository) and isolates vcpkg/library outputs under a fingerprint. It never checks out, resets, or edits a
sibling `Z:\src\DxUi` working tree. A mismatched or dirty pin fails restore.

Output folders use the first 16 hexadecimal characters of the full build fingerprint, reducing the path length
seen by native vcpkg directory creation. The full 64-character fingerprint and complete build identity remain in
`DxUi.build-identity.json` inside that folder and in the platform identity/provenance records. Before using an
existing output folder, restore MUST compare its complete identity; a prefix collision, identity mismatch or
nonempty unmarked cache MUST fail without overwriting the marker. An empty folder may receive its identity once.
`DxUi.resolved.<Platform>.props` supplies the actual `DxUiConsumerOutputRoot` to both MSBuild and provenance.
Provenance MUST validate that literal absolute root and its marker, then bind the linker command to the archive
under that root. It MUST NOT reconstruct the archive location from a full fingerprint or assume a folder width.

The clone (`Build/DxUiRestore.psm1`) is a sparse checkout of the pinned commit that leaves out `Measurements/`,
`docs/gallery` and `Specs/`. A build, `Tools/validate_consumer.ps1`, `Tools/ConsumerBuild.psm1` and DxUi's `vcpkg-install.ps1`
read none of them, they hold most of the library's files (about 180 of 1,650 are checked out at the previous pin), and pins
before API revision 3 kept tracked paths of up to 164 characters in them (176 on DxUi `main` between its #22 and the
150-character budget). Git gets `core.longpaths=true` for the clone and the checkout, and the
clone keeps it in its own configuration, which the clean-tree check of the pin validation needs as much as the checkout does.
A clone from the public repository is blobless, so only the files the checkout names are downloaded (about 2 MB instead of
35 MB at the previous pin). A root deep enough that a kept file's Win32 path passes 259 characters therefore restores, which a
plain restore did not: from a 121-character root it left 39 files of the previous pin uncreated and the checkout dirty, and the
pin validation failed. `Tests/BuildProcessTests/DxUiRestoreTests.ps1` restores a fixture repository into a destination of about
210 characters (from a checkout at `D:\RedPrism-dxui`), which puts a 107-character kept path past 259; `-RealPin` restores the
locked commit the same way, from the sibling checkout or the public repository.

`build.ps1` passes `-CheckUpdates`. `Show-DxUiUpdateNotice` from the pinned `Tools/ConsumerUpdate.psm1` then compares
the lock with DxUi `main`. The notice is build output only: yellow when a newer `main` commit has a successful
completed `ci.yml` push run, red when that validation is missing, dark yellow when the pin is ahead of or diverged
from `main` or the lookup cannot run. The same pin prints nothing. The check never edits the lock and never fails a
valid restore.

`RedPrism` imports `Build/RedPrism.DxUi.props` / `.targets`, which reference `src/DxUi.vcxproj` once. Project
references keep Configuration/Platform; they do not enumerate library `.cpp` files. Debug and ASan Debug use `/MDd`,
Release `/MD`, matching DxUi. There is no `DxUi.dll` to stage.

Builds produce `DxUi.provenance.json` beside `RedPrism.exe`: exact source/API, public-header tree, archive hash, build
identity, and the `RedPrism.exe` module hash. `test.ps1` rejects wrong pins, profiles, missing/duplicate modules, and
replaced binaries. The sidecar is product evidence, not a separate release system.

## Window messages and the API revision

DxUi registers its private window messages by name (`RedSalamanders.DxUi.<Component>.<Purpose>.v1`), so their values lie in
0xC000–0xFFFF, and it reserves no `WM_APP` or `WM_USER` value (DxUi `Changes/2026-10-01-api-revision-3.md`). A consumer forwards
every message, registered ones included, to `HandleMessage` of each window it attaches a `DxUi::WindowHost` to, copies no DxUi
`WM_APP` value, and posts a menu-bar hover with `DxUi::ContextMenu::PostMenuBarHover`. API revision 3 also renamed the model and
delegate interfaces `IGridModel`, `IGridDelegate`, `ITreeModel` and `ITreeDelegate`; `ChromeHost` implements the last two for the
layers list and the Command List.

RedPrism attaches no `WindowHost`. Its main window is a plain HWND that hosts one `EmbeddedHost` (`ChromeHost`), which the
application prepares and composites on its own frame path. `Application::WindowProcedure` hands every message, registered ones
included, to `Application::HandleMessage`, which ends in `DefWindowProcW`, so nothing of the product's sits between a DxUi
message and the system's default handling. The product's own messages (`Application.h` `WM_APP + 8`, `+ 9` and `+ 10`,
`Renderer.h` `+ 1`, the test gates and `ApplicationAdjustJobs.cpp`'s `+ 0x7D0`) stay its own, and none equals a value DxUi
used before (`+ 0x06A` to `+ 0x06D`, `+ 0x214` to `+ 0x219`, `+ 0x539`). Its native menus run through
`ShowNativeHMenuContextMenu`, whose modal loop is DxUi's, and the product posts no menu-bar hover. A window that attaches a
`WindowHost` later forwards every message and is recorded here.

## Host boundary

The application owns the HWND, D3D11 device, swap chain, and Present. `DxUi::GraphicsDevice::Create` borrows that
device. `EmbeddedHost` owns no HWND, swap chain, worker, or periodic timer.

Direct2D objects the host keeps for its own drawing live only while the chrome is attached to the device they were
made on. The rail's Lucide SVG documents (`LucideToolButton`, `DrawLucideSvg`) are the one process-wide cache:
`ChromeHost` releases it (`ReleaseLucideIcons`) before it replaces its device and before it detaches, while DxUi's
`GraphicsDevice` still holds the Direct2D factory. No Direct2D object may be left for the static destructors at
process exit. d2d1 destroys a factory from inside the `Release` that drops a resource's last reference to it and
then leaves that factory's lock, which is freed memory by then; a document released by a static destructor after
the chrome let go of the factory crashed the process at exit with an access violation in `d2d1.dll` (RF-1110). The
self-test proves the cache is empty after a device replacement and after the chrome detaches.

`ChromeHost` attaches one EmbeddedHost, applies `MakeRedPrismThemePalette()` (local adapter, not a DxUi type), and
builds the editor shell described in [`UI_ApplicationChrome.md`](../UI/UI_ApplicationChrome.md): MenuBar, TabControl,
Toolbar rail of icon-only Buttons, tool header Label, layers Tree, StatusStrip. Pointer, key, and character messages
dispatch into the host. Preparation runs before the canvas blit (it decides the hole) and composition runs after
it, into the same back buffer, on the idle Present path. The DxUi surface is cleared transparent and composited with
premultiplied alpha, so controls that overlap the canvas hole (tooltips, popups) stay visible. Hidden or zero-extent
EmbeddedHost holds no cached surface (0 bytes). Visible surface stays within the library 64 MiB cap.

Facts about the pinned library that the host designs around (verified against the pinned sources):

- `EmbeddedHost::PrepareInteraction(width, height, dpi)` acknowledges explicitly arranged live hit geometry at the
  last successfully painted extent/DPI. It performs no paint, surface allocation, cache trim or text/UIA publication.
  `SyncChrome` finishes control updates and arrangement before calling this boundary; `Renderer::Render` retains
  full preparation on the frame path. Initial, failed, hidden, zero-sized and replaced-device views cannot resume
  interaction through acknowledgement. Dirty pixels/text geometry remain pending for the next full preparation.

- `Toolbar` is a `Panel` subclass with no child layout; consumers set every button's bounds.
- `Toggle` paints a switch regardless of `ButtonVariant::IconOnly`; a latched tool button is an icon-only `Button`
  with `SetPressedVisual(true)`.
- Icon-only buttons draw their text with `FontRole::Icon` (Segoe Fluent Icons); Latin letters do not render there.
- `Tree` reports expander toggles through `ITreeDelegate::OnTreeToggleExpanded`. When reorder is enabled,
  `SetReorderEnabled` reports one completed row drag through `ITreeDelegate::OnTreeReorder`; DxUi does not move the
  model. API revision 3 also has this callback at the nearest rollback pin.
- The qualified commit adds `ITreeModel::CapturePreparedAccessibilityRows()` as an optional source for the current
  visible-model epoch, including offscreen rows. Its default returns null and preserves legacy capture. A non-null
  `IPreparedTreeAccessibilityRows` source is already prepared: capture only retains its shared owner, while
  `GetCount`, `GetItem` and `FindItem` are bounded, allocation-free reads. DxUi's UIA snapshot retains that owner
  through provider reads and replacement. The interface addition leaves the numeric API revision at 3, but a
  consumer MUST rebuild against the matching DxUi library.
- `Splitter` hit-tests only its separator plus 2 DIP of slop; the host sizes it over both panes and places the pane
  controls itself from the committed position. `NumericStepper` and `ColorPicker` are Panels with their own children;
  `ColorPicker` ships no strings, so every caption comes from the RedPrism STRINGTABLE.

RedPrism's prepared layer-row/accessibility integration remains an experimental consumer draft with its production
flag false. Its current source shares one immutable admitted `PreparedLayerRows` owner across Application, Chrome and
UIA snapshots; the consumer matrix, client-reentrant UIA behavior, actual UIA latency, largest-count/name owner cost,
production activation and G1 remain open. Do not infer consumer qualification or P1 completion from the passed DxUi
library matrix.

DxUi C++ stays inside `RedPrism.exe`. Never pass DxUi types, STL, WIL, or exceptions across a future plugin ABI.

## Library promotion status

Shipped. `EditorConsumerControls_2026-09-20.md` closed on 2026-09-21 at DxUi `7e3be94` (catalog 30, API revision 2,
19 library suites green in Debug, Release and ASan Debug, ARM64 Debug build, gallery republished). The C3 fallbacks
are retired; [`UI_ApplicationChrome.md`](../UI/UI_ApplicationChrome.md) describes the consumed splitter, palette
wells with the color picker, and the opacity stepper. Do not implement those three controls in RedPrism.

## Screenshot harness

`--screenshot <png>` shows the titled window with `SW_SHOWNOACTIVATE`, presents chrome and the canvas, waits 800 ms
on a one-shot timer so DxUi animations settle, captures the owned window through `Common/WindowCapture.cpp`
(Windows.Graphics.Capture, no cursor, no activation), and exits 0 when the PNG is written, 8 when this system cannot
capture a window, or 9 when the capture failed or never happened ([`UI_Windowing.md`](../UI/UI_Windowing.md)).
Computer Use and desktop screenshot tools are not evidence.

## Validation

```powershell
.\test.ps1 -Platform x64 -Configuration Debug
.\test.ps1 -Platform x64 -Configuration Release
.\test.ps1 -Platform x64 -Configuration "ASan Debug"
```

`--self-test --warp` MUST prove a visible chrome surface with at least one composite and a hidden surface of 0 bytes.
`Tests/BuildProcessTests/DxUiProvenanceTests.ps1` MUST reject a wrong pin.
`Tests/BuildProcessTests/DxUiOutputPathTests.ps1` MUST prove full identity retention, idempotence, collision refusal,
unmarked cache refusal and props-based path resolution, including malformed, relative and escaping roots.

## Test-only public row interface

CompositionTests imports the existing `Build/RedPrism.DxUi.props` to compile the application's prepared-row adapter
against the exact restored public `IPreparedTreeAccessibilityRows` declaration. It imports no consumer library
targets or library source files and links no DxUi implementation. The product's single pinned library reference,
archive/provenance, CRT/profile and ABI policies remain required. Solution entrypoints restore and validate that
same pin before compiling either consumer.
