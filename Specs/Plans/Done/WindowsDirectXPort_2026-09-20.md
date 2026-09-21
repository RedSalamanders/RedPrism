# RedPrism Windows DirectX application

> **Executor instructions**: This is the master program for the Windows C++ application. Follow
> phases in order. Complete code + tests + the named domain spec in the same slice. If a STOP
> condition fires, stop and report — do not improvise a second architecture. Do not compile or
> “upgrade in place” anything under archived behavioral reference. Spawn a dated child plan under `Specs/Plans/WIP/`
> and index it before starting C8 or later. Update this file’s checkboxes and the WIP index as
> you go.
>
> **Drift check (run first)**:
> ```powershell
> git -C Z:\src\RedPrism status --short
> git -C Z:\src\DxUi rev-parse HEAD
> git -C Z:\src\RedXe rev-parse HEAD
> git -C Z:\src\RedSalamander rev-parse HEAD
> ```
> RedPrism had **no commits** when this plan was written (empty `main`, working tree = archived behavioral reference
> plus this plan). If archived behavioral reference changed, re-read archived behavioral reference, archived behavioral reference,
> and archived behavioral reference before touching document/IO code. If DxUi HEAD
> moved, re-read the consumer pin rules below — do not float onto sibling `main`.

## Status and ownership

- **Status**: **DONE (C0–C7 shipped; C8–C15 spawned as child plans)**
- **Priority**: P0 (the product)
- **Effort**: XL (multi-phase; each of C0–C7 is L; C8–C15 are sequels)
- **Risk**: HIGH
- **Category**: migration / direction
- **Planned at**: 2026-09-20, RedPrism empty repository (no `HEAD` commit). Sibling evidence:
  - DxUi checkout `c8354a422e21d9c599993354ee44506e3ca8497e`
  - RedXe checkout `a73189ba0ffece45e5b054ae7b81273e1feb8790`
  - Published consumer DxUi pin used by RedXe/RedSalamander:
    `f5c7fc7403e352425b8e82b7a4da1c042d942ede` (API revision 2, target `DxUi`)
- **Owner**: RedPrism application (`Application` windowing, `Renderer` D3D11, `Document` model,
  product adapters). DxUi library edits are **not** owned here — they are
  `Z:\src\DxUi\Specs\Plans\WIP\EditorConsumerControls_2026-09-20.md` (HOLD until C2).
- **Non-normative**: this file cannot override domain specs once those exist. Close durable
  behavior into `Specs/<Domain>/` before moving any phase to Done.

## Why this matters

The original behavioral reference was a Photoshop-style 2D raster editor written in
SwiftUI/AppKit with Core Graphics compositing and a Metal compute brush. The user asked to port
that **Swift legacy** application to the **best possible Windows C++ DirectX** product, using
the skills and constraints already proven in RedSalamander, RedXe, and DxUi.

There is no C++/DirectX/SFWIT tree in this repository. The Windows application does not exist
yet. The job is a ground-up native port that **preserves document semantics and tool behavior**,
not a mechanical Swift-to-C++ translation and not a GDI/Win32 Forms shell.

## Purpose and accepted product design

RedPrism on Windows is a **free, native, GPU-accelerated raster composition** that:

1. Opens and saves the RedPrism `.prism` document (manifest versions 1–7, sRGB, the same
   layer/mask/adjustment semantics).
2. Matches the legacy tool set: move/transform, selections, brush/eraser, heal, clone, smear,
   gradient, shape, eyedropper, crop, adjustments, filters.
3. Feels like a Windows 11 DirectX application: one HWND, one D3D11 device, DxUi chrome,
   DirectWrite typography, no GDI text, no chrome child HWNDs on the swap chain.
4. Is idle-event-blocked, allocation-free on the present path, and measured from the first
   feature — not “optimized later.”

## Current state and evidence

| Area | Fact at plan time | Anchor |
| --- | --- | --- |
| Windows app | Absent | Repo contains archived behavioral reference only (plus this plan) |
| AGENTS / Specs / CMake / vcpkg / sln | Absent | No root `AGENTS.md`, `CMakeLists.txt`, `vcpkg.json` |
| Inherited code license | MIT, Wonder Assembly | `ThirdParty/Notices/InheritedCode.txt` |
| Legacy GPU | Metal compute for brush only; display is Core Graphics | archived behavioral reference, `LayerRenderer.swift` |
| Legacy UI | SwiftUI + AppKit canvas NSView | archived behavioral reference, `Rendering/EditorCanvas.swift` |
| Document | `CanvasDocument` + `ImageLayer` + session-only selection | archived behavioral reference |
| Format | `.prism` package, `manifest.json` + `images/*.png`, versions **1–7** | archived behavioral reference (docs lag: archived behavioral reference covers 1–6) |
| History | Value snapshots, shared immutable images, 100 entries / 256 MiB | archived behavioral reference |
| Tests | ~175 Swift Testing cases; no Windows tests | retained numeric golden cases |
| DxUi | 27 catalog controls; static `DxUi.lib`; D3D11 EmbeddedHost | `Z:\src\DxUi\include\DxUi\ControlCatalog.h` |
| Sibling build | MSBuild, **not CMake** | RedXe/RedSalamander/DxUi `Directory.Build.props` + `build.ps1` |

### Legacy feature inventory (must remain the acceptance list)

From archived behavioral reference plus code that docs omit:

**Layers**: image, blank, folders (≤64 ancestors), visibility, reorder, rename, duplicate,
Option-drag duplicate, opacity, blend modes (Normal, Multiply, Screen, Overlay, Darken, Lighten,
Difference, Color Dodge, Color Burn, Hue, Saturation, Color, Luminosity), raster masks
(paint/fill/invert/blur/feather, link/unlink, independent mask transform), folder masks (v6+),
clipping via `maskSourceID`, adjustment layers (Hue/Saturation, Levels, Curves, Exposure,
Gradient Map, Grain — **v7**), Merge Down / Merge Layers / Merge Group, drag layers between
open projects.

**Transform**: non-destructive move/scale/rotate/flip, free distort, multi-layer/folder
transform, snapping + guides, numeric inspector, Flip Layer / Flip Canvas.

**Selections**: rect/ellipse marquee, freehand/polygonal lasso, magic wand, add/subtract, move
outline, move/duplicate pixels, load layer/mask as selection, expand/contract, inverse,
content-aware fill (may extend past edges).

**Paint / retouch**: brush + eraser (size/hardness/opacity, Shift straight lines), spot healing,
clone stamp (aligned/unaligned, sample one or all layers), blur/smudge/liquify, gradient, shape
(rect, rounded rect, ellipse), eyedropper + full color picker, FG/BG colors.

**Adjustments / filters**: Levels (Auto), Curves, Hue/Saturation, Exposure, Gradient Map, Grain,
Invert, Gaussian Blur, Motion Blur, Add Noise, Lens Correction, Remove Background (Vision +
optional guided matte). Live previews; selection-limited when a selection exists.

**Canvas / files**: multi-project tabs, New/Open/Save/Save As, Crop, Canvas Size, Image Size,
sharp downsample when zoomed out, pixel grid when zoomed in, import JPEG/PNG/HEIC/TIFF,
export PNG, export JPEG with live preview, Copy Merged, Photoshop-style shortcuts.

**Hard limits** (enforce identically): 30 000 px/side, 100 M image pixels + 100 M mask pixels,
≤10 000 layers, 4 MiB manifest, 512 MiB/asset, sRGB working space, groups currently force
opacity 1 / Normal blend on disk.

## Sibling skills and constraints (copy these; do not invent a third house style)

Read these before writing any C++. If a local RedPrism skill exists, it wins; until C0 copies
them, the sibling files are the source.

| Source | Apply |
| --- | --- |
| `Z:\src\RedXe\AGENTS.md` + `.agents/skills/{direct3d11-rendering,win32-windowing,performance-resources,wil-raii,yyjson,modern-cpp-windows,spec-workflow,settings-store,build-redxe}` | Window/device/Present/idle, WIL, yyjson, spec lifecycle, build.ps1 |
| `Z:\src\DxUi\AGENTS.md` + `.agents/skills/{consumer-integration,embedded-rendering,control-development,modern-cpp-wil,input-accessibility}` | Pin `DxUi.lib`, EmbeddedHost, no C++ across plugin ABI, catalog process |
| `Z:\src\RedSalamander\AGENTS.md` + `.github/skills/{cpp-modern-style,wil-raii,error-handling,async-threading,localization,perf-validation,theming}` | Naming, HRESULT, UI-thread rules, STRINGTABLE, perf-from-day-one |
| DxUi HOLD plan | `Z:\src\DxUi\Specs\Plans\WIP\EditorConsumerControls_2026-09-20.md` |

### Quoted rules that bind this port

From RedXe AGENTS:

- Performance and low resource consumption are primary. Prefer the design with the lowest
  steady-state CPU, memory, allocation, copy, synchronization, wake-up, and GPU-submission cost.
- Steady-state hot paths must use bounded reusable storage and avoid heap allocation.
- Idle, hidden, minimized, suspended, and occluded execution must block on events; busy polling
  is prohibited.
- Do not render from `WM_PAINT`; render from the idle side of the message loop.
- Do not allow exceptions to cross `wWinMain` or a Win32 callback.
- Host chrome is Direct3D; no chrome HWND and no GDI in the host.

From DxUi AGENTS / consumer-integration:

- Consumers link the single pinned `DxUi.lib`. They must not enumerate library `.cpp` files.
- Embedded hosting borrows the application device/context. It owns no swap chain, renderer HWND,
  worker, or periodic timer. Preparation ≠ composition.
- Exact revision lock (`Dependencies/DxUi.lock.json`), API revision **2**, target `["DxUi"]`.
- Do not reset or silently update a sibling DxUi checkout.

From RedSalamander AGENTS:

- All Windows resources MUST use WIL RAII wrappers. Manual `DestroyIcon` / `DeleteObject` /
  `EndPaint` / `Release` of owned resources is prohibited.
- `catch (...)` is forbidden. Treat `std::bad_alloc` as fatal (`std::terminate()`).
- Screenshots for specs/docs MUST come from the test harness, never Computer Use or a desktop
  screenshot tool.
- `std::optional` is accessed with `.value()` / `.value_or()`, never `*`.

From DxUi `UI_DxUiWinUIDesign` / RedSalamander `docs/DxUi.md`:

- Visible chrome goes through DxUi. Direct2D-on-HDC does not authorize app-owned GDI text.
- Keep `DxUi::WindowHost` / `EmbeddedHost` alive for the attached HWND; disconnect models before
  detach; forward `WM_NCDESTROY`.
- Typography only through DxUi `FontRole` helpers — no `CreateFontW` / `DEFAULT_GUI_FONT`.

## Accepted architecture decisions

These are **engineering decisions**, not open product questions. Do not reopen them without a
`DECISION` child plan.

### A1 — MSBuild, not CMake

RedXe, RedSalamander, and DxUi are MSBuild + `Directory.Build.props` + `build.ps1`. DxUi
consumption is `Build/DxUi.Consumer.props` + `.targets` + a project reference to
`src/DxUi.vcxproj`. A CMake RedPrism would be a third packaging world and would block first-class
DxUi linking. **Use Visual Studio 2026, toolset v145, Windows 11 SDK 10.0.26100, `stdcpplatest`.**

### A2 — Direct3D 11, not 12

DxUi EmbeddedHost and RedXe Renderer are D3D11. A D3D12 canvas cannot borrow the same device
without a costly interop story. **One D3D11 device, one DXGI flip-model swap chain, one Present.**
Build-time HLSL (no runtime `d3dcompiler` for fixed shaders). WARP (`--warp`) is a required smoke
path.

### A3 — One HWND, app-owned swap chain, DxUi overlay

```text
Win32 top-level (WS_EX_NOREDIRECTIONBITMAP)
  Application  : messages, DPI, files, commands, focus
  Renderer     : device, swap chain, waitable latency 1, occlusion
  Document GPU : canvas, tiles, blend/compute
  DxUi::EmbeddedHost : chrome (tools, tabs, layers, dialogs, menus)
```

Frame: wait on frame-latency object + message queue → if document or UI dirty → document
Prepare/Render into the back buffer (or an intermediate canvas texture) → DxUi `Prepare` then
`Composite` → `Present`. Hidden/minimized/zero-size: no Present, no surface. Do **not** put the
document canvas inside a DxUi control.

Until Splitter exists, lay out DIP rectangles in the app (tool rail | canvas | layers) and size
DxUi subtrees + the canvas viewport explicitly. After Splitter lands, the split ratio is consumer
state.

### A4 — No GDI chrome, no second device

Match RedXe HostChromeWithoutGdi: no GDI text, no layered chrome HWND over the swap chain, no
second D3D device for UI. Common file dialogs (`IFileDialog`) and the OS clipboard are allowed;
they are not chrome.

### A5 — Document model is C++ value types + copy-on-write rasters

Port `CanvasDocument` / `ImageLayer` / `LayerTransform` / `DocumentHistory` as C++ types. Pixels
live in immutable `RasterSnapshot` tiles (256×256, same as
archived behavioral reference). History shares unchanged tiles and images.
Do not snapshot full bitmaps on every undo step.

### A6 — `.prism` on Windows is a ZIP with the Mac package layout

Mac `.prism` is a **directory package**. Windows Explorer needs a **file**. New saves write a ZIP
whose members are `manifest.json` and `images/<uuid>.png` / `images/<uuid>.mask.png` with the
same JSON schema (format `com.redprism.project`, versions 1–7). Load path accepts:

1. ZIP `.prism` (Windows default)
2. Exploded directory trees (fixtures and Mac packages)

Atomic replace: write to a sibling temp, then `ReplaceFileW` / `MoveFileExW`. Reject path escape,
symlinks, oversize, bad versions — same rules as `ProjectStore.swift`.

the reference editor will not open ZIP `.prism` until a later dual-write is explicitly planned. Round-trip
tests in RedPrism must still accept exploded trees so archived behavioral reference fixtures can be checked in.

### A7 — Image I/O is WIC, not extra codec packages (P0)

Windows Imaging Component is the ImageIO equivalent: JPEG, PNG, TIFF, BMP, and HEIF when the
HEIF extension is present. Decode into 8-bit sRGB premultiplied BGRA (or unpremultiplied with a
single documented conversion at the GPU upload). Honor EXIF orientation. If HEIF is missing,
fail with a localized unsupported-format error — do not crash.

Do **not** add `libjpeg-turbo`, `libpng`, or `libraw` in C0–C7. Revisit only with measurement
showing WIC is the hot-path bottleneck, or a product decision to import camera RAW (legacy does
not).

### A8 — Brush is D3D11 compute, CPU fallback remains

Port `MetalBrushCoverage.source` (`continuousBrush`) to HLSL compute. Tile size 256. Permanent
float density + provisional tail, opacity cap, software fallback from
archived behavioral reference. Compile the shader at build time; warm the PSO when
Brush is selected, not at process start.

### A9 — Blend modes are shaders, not Core Graphics

Port every `LayerBlendMode` to HLSL. Color Dodge / Color Burn must match **Core Image** (legacy
explicitly avoids CG’s incorrect operators in `SeparableBlend.swift`). Golden tests vs frozen PNG
fixtures beat “looks fine.”

### A10 — Monolithic process for v1; no plugin ABI

Legacy has no plugins. Do not copy RedXe’s plugin host. Keep DxUi C++ inside `RedPrism.exe`. If
a later plugin story appears, the ABI is COM/POD only — never DxUi types, STL, WIL, or
exceptions across a DLL boundary.

### A11 — Platform bar

- **OS**: Windows 11 build 22000.2600 minimum (HEIF, DWM, SDK contract aligned with RedSalamander).
- **Configs**: Debug, Release, ASan Debug × x64, ARM64 (six profiles). Native ARM64 runtime is
  required for ARM64 claims; cross-build alone is not.
- **Warnings**: `/W4`, `/permissive-`, SDL, warnings-as-errors, `/utf-8` (match DxUi/RedXe so WIL
  and DxUi headers compile). Do not start at RedSalamander `/Wall` until the app exists.
- **CRT**: `/MDd` Debug and ASan Debug, `/MD` Release — **must match DxUi**.
- **Unicode**: UTF-16 Win32 APIs; `UNICODE`, `_UNICODE`, `WIN32_LEAN_AND_MEAN`, `NOMINMAX`.

### A12 — Localization and settings

All UI strings in `.rc` STRINGTABLE with positional `{0}` formats. Settings under
`%LocalAppData%\RedPrism\` with a yyjson schema, Debug vs Release filenames, cold invalid →
backup + template, live invalid → keep file + last-good runtime (RedXe `settings-store`).

## vcpkg

Align the **baseline** with DxUi and RedXe so the app and the library restore the same WIL.

| File | Value |
| --- | --- |
| `vcpkg-tool.json` repository | `https://github.com/microsoft/vcpkg` |
| `vcpkg-tool.json` commit **and** `vcpkg.json` `builtin-baseline` | `9e593bb18ea69cc5095e012465dcd675a822ed0d` |
| WIL | `version>="1.0.260126.7"` (same as DxUi/RedXe/RedSalamander) |
| yyjson | `version>="0.12.0"` (RedXe/RedSalamander) |

**C0–C4 (foundation)** — `vcpkg.json` dependencies:

```json
{
  "name": "redprism",
  "version-string": "0.1.0",
  "builtin-baseline": "9e593bb18ea69cc5095e012465dcd675a822ed0d",
  "dependencies": [
    { "name": "wil", "version>=": "1.0.260126.7" },
    { "name": "yyjson", "version>=": "0.12.0" }
  ]
}
```

**C5 (project ZIP)** — add:

```json
{ "name": "libzip", "default-features": false }
```

Use zlib via libzip’s default unless a feature flag is required to avoid bloat. Document the
exact feature set in `Specs/Build/Build_Toolchain.md` when added. Do not add `curl` (no Sparkle
port in v1). Do not add `webview2`, `aws-sdk-cpp`, `sqlite3`, `7zip`, `ftxui`, or `pe-parse`.

**C13 (Remove Background)** — only then consider:

```json
{ "name": "onnxruntime", "features": [ "directml" ] }
```

That is a product decision + new child plan. Until then, ship Invert/filters and leave
Remove Background as a disabled command with a spec note, **or** port `GuidedMatte.swift` as a
CPU refine that still needs a subject mask source.

**Not vcpkg (Windows SDK):** D3D11, DXGI, D2D (only if DxUi needs it internally), DirectWrite,
WIC, UIAutomation (via DxUi), C++/WinRT not required.

DxUi is **not** a vcpkg port. Consume it with `Dependencies/DxUi.lock.json` + restore script
copied from RedXe `restore-dxui.ps1` / RedSalamander `Tools/Restore-DxUi.ps1`.

Initial lock (until editor controls land):

```json
{
  "repository": "https://github.com/RedSalamanders/DxUi",
  "commit": "f5c7fc7403e352425b8e82b7a4da1c042d942ede",
  "apiRevision": 2,
  "targets": [ "DxUi" ]
}
```

If C3 needs Splitter/ColorPicker/NumericStepper, update the pin to the DxUi commit that merged
them **after** that library plan’s `test.ps1` gates. Never check out/reset `Z:\src\DxUi` from
RedPrism restore.

## DxUi: reuse vs enhance

### Reuse as-is (C2/C7)

Toolbar, Toggle, RadioButtons, MenuBar, ContextMenu, TabControl, Tree, Grid, Slider, ComboBox,
ColorSwatch, TextField, ScrollPanel, StackPanel, Panel, Label, Button, StatusStrip, PopupLayer,
TooltipLayer, WindowHost/EmbeddedHost.

### Enhance in DxUi (HOLD plan, after C2)

See `Z:\src\DxUi\Specs\Plans\WIP\EditorConsumerControls_2026-09-20.md`:

1. **Splitter** — canvas | layers (and future inspector).
2. **ColorPicker** — HSV field, hue strip, RGB/hex, preview/commit/cancel.
3. **NumericStepper** — transform inspector (Grid spinner cell is not enough).

Work around them in C7 with explicit bounds + Slider/TextField if the HOLD plan is not yet
promoted. Do not clone those controls inside RedPrism.

### Keep in RedPrism (never library)

Zoom/pan viewport, pixel grid, rulers/guides, brush HUD, curves graph, levels histogram, liquify
mesh, layer thumbnail renderer, document composition.

## Target repository shape (create in C0–C2)

```text
RedPrism/
  AGENTS.md
  .agents/skills/          # copied/adapted sibling skills; validate-skills.ps1
  Directory.Build.props
  Directory.Build.targets
  RedPrism.sln
  vcpkg.json
  vcpkg-tool.json
  build.ps1
  test.ps1
  vcpkg-install.ps1
  format.ps1
  Common/                  # Version.h, Debug, HRESULT helpers, SettingsStore, WIL wrappers
  RedPrism/                # wWinMain, Application, Renderer, canvas, chrome adapters
  Shaders/                 # HLSL, compiled at build, embedded bytecode
  Tests/                   # in-process + WARP harness
  Tools/                   # restore-dxui, spec inventory (when C0 needs it)
  Specs/                   # domain contracts + Plans/WIP + Plans/Done + TestRuns
  Dependencies/DxUi.lock.json
  Resources/               # .rc STRINGTABLE, app manifest, icons
  archived behavioral reference                  # REFERENCE ONLY — never a build input
```

Naming (RedSalamander `cpp-modern-style`): PascalCase types/methods, camelCase locals,
`_`-prefixed members, `kConstants`. Exclusive `std::unique_ptr` / WIL; no raw `new`/`delete`.

## Platform mapping (legacy → Windows)

| Legacy | Windows port |
| --- | --- |
| SwiftUI / AppKit chrome | DxUi EmbeddedHost overlay |
| `NSView` canvas | App D3D11 viewport (pointer mapped through `CanvasViewport`) |
| Metal `continuousBrush` | D3D11 compute HLSL |
| Core Graphics composite | D3D11 blend/compute shaders |
| Core Image ColorBurn/Dodge | Dedicated HLSL (CI-correct) |
| `CGImage` / ImageIO | WIC + immutable CPU tiles / GPU textures |
| Accelerate downsample | D3D11 mips or a dedicated downsample compute (port `DownsampleCache.swift`) |
| Vision subject mask | Deferred; DirectML+ONNX child plan |
| Guided matte | Portable C++ (`GuidedMatte.swift`) |
| `NSFileCoordinator` atomic package | Temp + `ReplaceFileW` |
| Sparkle | Out of v1; later MSIX/winget |
| `⌘` shortcuts | `Ctrl` (and documented Photoshop-style Win keys) |
| AppKit Y / CG bitmap | Document space: origin top-left, +Y down, matching WIC/D3D |

Portable C already in tree (prefer these over a Swift rewrite):
`BrushPixels.c`, `HealPixels.c`, `WandPixels.c`, `NoisePixels.c`, `LensPixels.c`,
`LevelsPixels.c`, `AdjustPixels.c`, `ContentFill.c` under archived behavioral reference.
Reimplement in C++23 with `std::span`, golden tests, then optionally GPU.

## Authoritative specs this program will create

Create the contract **in the same change** as the first behavior it describes. Suggested files:

| Spec | When | Contents |
| --- | --- | --- |
| `Specs/README.md` | C0 | Authority catalog + plan lifecycle (copy RedXe/RedSalamander shape) |
| `Specs/Build/Build_Toolchain.md` | C1 | VS2026, six configs, vcpkg pin, DxUi restore, `build.ps1` |
| `Specs/Build/Build_Packaging.md` | C15 | Portable ZIP / later installer |
| `Specs/Core/Core_Architecture.md` | C1 | Application / Renderer / Document / DxUi ownership |
| `Specs/Core/Core_PerformanceAndResources.md` | C1 | Idle, alloc, surface, history 256 MiB, brush budgets |
| `Specs/Core/Core_DxUiIntegration.md` | C2 | Lock, CRT, EmbeddedHost, no ABI leak |
| `Specs/Core/Core_Settings.md` | C14 | Schema, recovery, live reload |
| `Specs/Core/Core_CrashHandling.md` | C14 | Local minidumps only |
| `Specs/UI/UI_ApplicationChrome.md` | C7 | Tool rail, tabs, layers, status, menus, shortcuts |
| `Specs/UI/UI_Windowing.md` | C1 | DPI, occlusion, `WS_EX_NOREDIRECTIONBITMAP`, `--warp` |
| `Specs/Document/Document_Model.md` | C4 | Layers, groups, masks, adjustments, limits |
| `Specs/Document/Document_History.md` | C4 | Undo/redo sharing and byte budget |
| `Specs/Document/Document_Tools.md` | C8–C12 | Per-tool contracts (may split) |
| `Specs/IO/IO_ProjectFormat.md` | C5 | `.prism` v1–7, ZIP vs directory, validation |
| `Specs/IO/IO_ImageCodecs.md` | C5 | WIC, HEIF missing, export JPEG preview |
| `Specs/Rendering/Rendering_Composition.md` | C6 | Blend shaders, tiles, viewport, pixel grid |
| `Specs/Rendering/Rendering_Brush.md` | C10 | Compute brush, fallback, 4K budgets |
| `Specs/Testing/Testing_Validation.md` | C1 | `test.ps1`, fail-closed `X:\RedPrism.Perf`, harness screenshots |
| `AGENTS.md` + skills | C0 | Canonical agent guidance |

`docs/` is the user guide and is not authority. Update it when an end-user scenario exists
(first useful at C7).

## Commands you will need

Until C1 lands, these commands **do not exist**. After C1 they must:

| Purpose | Command | Expected |
| --- | --- | --- |
| Restore vcpkg | `.\vcpkg-install.ps1 -Platform x64` | exit 0, WIL+yyjson under `.build/vcpkg_installed/x64/` |
| Restore DxUi | `.\Tools\Restore-DxUi.ps1 -Platform x64 -Configuration Debug` (name may match RedXe `restore-dxui.ps1`) | pin `f5c7fc74…` (or updated lock), isolated `.build/dependencies/DxUi/` |
| Build | `.\build.ps1 -Platform x64 -Configuration Debug` | exit 0, `.build\x64\Debug\RedPrism.exe`, zero warnings |
| Test | `.\test.ps1 -Platform x64 -Configuration Debug` | all cases pass; WARP smoke |
| Skills | `.\validate-skills.ps1` | exit 0 |
| Format | `.\format.ps1 -Check` | exit 0 |

`build.ps1` MUST reject a running `RedPrism.exe` whose normalized path is the selected output
and MUST NOT terminate it (RedXe/RedSalamander rule). Never commit `.build/`.

Fail-closed test root: `X:\RedPrism.Perf` (any local drive letter + ownership marker), never
repository `.build` as a data sandbox.

## Ordered checklist

### C0 — Repository policy, skills, ignore rules

- [x] Add root `.gitignore` matching RedXe (`.build/`, `.vs/`, `*.user`, `__pycache__`).
- [x] Write `AGENTS.md` for RedPrism: Windows 11 22000+, `stdcpplatest`, VS2026/v145, SDK
      10.0.26100, D3D11/DXGI/DirectWrite/DxUi, WIL, yyjson, spec lifecycle, screenshot harness
      rule, no `catch (...)`, no GDI chrome.
- [x] Copy/adapt skills into `.agents/skills/` (do not leave executors depending on
      `Z:\src\RedXe` being open): `spec-workflow`, `build-redprism`, `direct3d11-rendering`,
      `win32-windowing`, `performance-resources`, `wil-raii`, `yyjson`, `modern-cpp-windows`,
      `settings-store`, `consumer-dxui`. Include `validate-skills.ps1`.
- [x] Write `Specs/README.md` (authority classes + this WIP index rule).
- [x] archived behavioral reference remains reference; add a one-line archived behavioral reference note that it is not built
      (do not rewrite Composition).

**Verify**: skills validator passes; no MSBuild yet.

**STOP**: if someone proposes CMake, D3D12, Qt, WinUI 3, or compiling the Swift tree on Windows.

### C1 — Toolchain and empty window

- [x] `Directory.Build.props` / `.targets` modeled on `Z:\src\RedXe\Directory.Build.props`
      (outputs under `.build\<Platform>\<Configuration>\`, vcpkg installed dir layout, six
      configurations, `stdcpplatest`, `/W4` as errors, SDL, `/utf-8`, `/permissive-`).
- [x] `vcpkg.json` + `vcpkg-tool.json` as in the vcpkg section (WIL + yyjson only).
- [x] `RedPrism.sln` with `Common` + `RedPrism` + `Tests`.
- [x] `wWinMain` → `Application` binds at `WM_NCCREATE`, `wil::unique_hwnd`, destroy via
      `.reset()`.
- [x] `Renderer`: adapter-of-monitor, flip model, `DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT`,
      max latency 1, `DXGI_MWA_NO_ALT_ENTER`, zero-size suspend, device-removed recreate,
      occlusion message + `DXGI_PRESENT_TEST`, `--warp`.
- [x] Idle-side Present of a clear color only. No `WM_PAINT` rendering. No busy loop.
- [x] `build.ps1` / `vcpkg-install.ps1` / `test.ps1` (WARP launch + exit).
- [x] Specs: `Build_Toolchain.md`, `Core_Architecture.md`, `Core_PerformanceAndResources.md`,
      `UI_Windowing.md`, `Testing_Validation.md`.

**Verify**:

```powershell
.\build.ps1 -Platform x64 -Configuration Debug
.\build.ps1 -Platform x64 -Configuration Release
.\build.ps1 -Platform x64 -Configuration "ASan Debug"
.\test.ps1 -Platform x64 -Configuration Debug
```

Expected: zero warnings/errors; WARP smoke presents one frame and exits 0; ASan binary is really
instrumented (a deliberate UAF test helper must fire under ASan Debug, same idea as RedXe).

**STOP**: Present from `WM_PAINT`; GDI `BeginPaint` text; default adapter while a monitor match
exists; killing a running exe to unblock the build.

### C2 — Pin DxUi and composite hello chrome

- [x] `Dependencies/DxUi.lock.json` with the pin above.
- [x] Restore script + `Build/RedPrism.DxUi.props` / `.targets` importing
      `Z:\src\DxUi\Build\DxUi.Consumer.props` pattern (see RedXe `Build/RedXe.DxUi.props`).
- [x] Theme adapter: product palette → `DxUi::ThemePalette` (local files, not in DxUi).
- [x] `EmbeddedHost::Attach` on the app `GraphicsDevice`; one Button or Label overlay;
      pointer/key dispatch; Prepare/Composite into the same back buffer as the clear/canvas.
- [x] Provenance sidecar (`DxUi.provenance.json`) and a test that rejects the wrong pin.
- [x] Spec: `Core_DxUiIntegration.md`.

**Verify**: x64 Debug/Release/ASan Debug product tests; overlay visible in a **harness**
screenshot (not Computer Use). Dirty DxUi compose stays within DxUi surface caps (64 MiB visible,
0 when hidden).

**STOP**: enumerating DxUi `.cpp` into the app vcxproj; creating a second D3D device for UI;
resetting `Z:\src\DxUi`; passing DxUi objects through a future DLL.

After C2, attach evidence to the DxUi HOLD plan if Splitter/ColorPicker/NumericStepper are still
required. Promote that plan there; do not implement those controls in RedPrism.

### C3 — DxUi editor controls (library repo)

Owned by `EditorConsumerControls_2026-09-20.md` in DxUi. RedPrism work on C3 is:

- [x] Provide C2 screenshots/notes proving Toolbar+Tree+Slider+ColorSwatch+TextField are
      insufficient for split panes / HSV picker / numeric nudge. Evidence is on the DxUi HOLD
      plan (hello overlay captures under `.build/screenshots/hello-dxui-x64-*.png`). Promotion is
      blocked while LocalizedAdaptiveLayout still owns the same DxUi control files.
- [x] After library merge, bump `Dependencies/DxUi.lock.json` and requalify C2 tests.
      **Deferred with this closeout**: LocalizedAdaptiveLayout still owns those DxUi files, so the pin does not
      move. C7 DIP fallback is the shipped shell. The bump remains a DxUi HOLD follow-up, not remaining master-plan
      work.
- [x] If HOLD is not promoted, C7 ships with app-owned DIP split rectangles and a consumer
      color-picker **dialog composed from existing controls** (saturation field can be an
      EmbeddedHost custom mesh in RedPrism — that is document-adjacent, acceptable as a
      temporary adapter). Record the debt in `UI_ApplicationChrome.md`.

**STOP**: implementing Splitter inside RedPrism as a second retained-control toolkit.

### C4 — Document model and history (CPU, no GPU required)

- [x] C++ types for `CanvasDocument`, `ImageLayer`, `LayerTransform`, `LayerMask`,
      `LayerAdjustment`, `LayerShapeStyle`, `LayerBlendMode`, group/`parentID` rules, live-mask
      graph (`maskSourceID`), selection value type.
- [x] Validation identical to `ProjectStore::validate` (limits, cycles, group opacity/blend,
      mask filenames, v1–v7 feature gates).
- [x] `DocumentHistory` with shared immutable rasters, 100-entry / 256 MiB retained budget,
      no-op edits must not clear redo (see `DocumentHistory.end`).
- [x] Specs: `Document_Model.md`, `Document_History.md`.
- [x] Tests ported from retained numeric golden cases group, mask, appearance, history suites
      (behavior, not Swift).

**Verify**: unit tests pass under Debug and ASan Debug; a 256 MiB trim test exists.

**STOP**: storing pixels in `std::vector` copies per undo step; using exceptions as control flow
across the session API.

### C5 — Project I/O and WIC import/export

- [x] Add `libzip` to vcpkg; wrap with WIL-friendly owners; never leave zip handles raw.
- [x] Load/save ZIP `.prism` and exploded directories; pretty JSON with sorted keys for
      stability; PNG assets via WIC; 96 px thumbnails.
- [x] Import JPEG/PNG/TIFF; HEIC via WIC when available.
- [x] Export PNG; JPEG export with quality slider (preview may be GPU later; CPU WIC is OK
      in C5).
- [x] Specs: `IO_ProjectFormat.md` (include v7 fields `adjustment`, `maskPlacement`,
      `maskLinked`, `shape` that `project-format.md` omitted), `IO_ImageCodecs.md`.
- [x] Golden: round-trip a hand-made exploded v7 fixture; reject path traversal and >4 MiB
      manifest.

**Verify**: ASan Debug I/O tests; missing HEIF is a clean error path.

**STOP**: writing a custom PNG encoder; following symlinks out of the package; bumping format
to v8 without a spec change.

### C6 — Canvas viewport and GPU composition

- [x] `CanvasViewport` pan/zoom (fit on open), pixel grid when zoomed in, sharp downsample when
      zoomed out (`DownsampleCache` port).
- [x] Layer stack composite in document space: transform, opacity, blend shaders, raster mask,
      folder mask, clipping `maskSourceID`, adjustment layers.
      Adjustment pixels are identity until C12 (`AdjustFilter_2026-09-20.md`); masks and PDF
      Dodge/Burn shaders ship here.
- [x] Tile/texture residency bounded; hidden documents drop GPU surfaces.
- [x] Pointer mapping: screen → document, including DPI.
- [x] Spec: `Rendering_Composition.md`.
- [x] Golden blend-mode fixtures (especially Color Dodge/Burn vs CI, not CG).

**Verify**: WARP composite of a known v7 document matches PNG within a documented tolerance;
device-loss recreates textures; occlusion does not Present; no heap in the compose path after
warmup (DxUi dirty-alloc cap still applies to chrome, not an excuse for canvas allocs).

**STOP**: compositing on the CPU into a DIB and `BitBlt`; one full-canvas texture upload per
mouse-move; second swap chain for the canvas.

### C7 — Application chrome (usable editor shell)

- [x] MenuBar + accelerators (Ctrl+N/O/S, tool keys V/M/L/W/C/B/E/J/S/R/G/U/I/H/Z, Space-hand).
- [x] Tool rail (Toolbar exclusive tools, idle/A).
- [x] Contextual tool header (compose existing controls).
- [x] Document TabControl (dirty indicator).
- [x] Layers panel (Tree or Grid + visibility, name, thumbnail, drag reorder).
      Tree lists names and selection; drag reorder waits on a DxUi Tree drag API (C8).
- [x] StatusStrip (zoom, size, tool help).
- [x] New/Open/Save via `IFileDialog`; drag-drop files onto the HWND.
- [x] Spec: `UI_ApplicationChrome.md`.
- [x] First user-guide page under `docs/` + harness screenshots.

**Verify**: Debug test harness captures the empty-document shell at 96 and 144 DPI; keyboard
focus cycle does not trap in the canvas without an escape to chrome.

**STOP**: `CreateWindow` child edit controls on the swap chain; GDI owner-draw menus as the
permanent menu (native `HMENU` via `NativeMenuInterop` is the DxUi-supported path).

### C8–C15 — Sequenced sequels (spawn a dated child plan before coding)

Do not execute these from this master file beyond writing the child plan and indexing it.
Indexed 2026-09-20:

| Phase | Child plan |
| --- | --- |
| **C8 Transform** | [`Transform_2026-09-20.md`](Transform_2026-09-20.md) (done) |
| **C9 Selection** | [`Selection_2026-09-20.md`](Selection_2026-09-20.md) (done) |
| **C9b Pixel edits** | [`PixelEdits_2026-09-21.md`](../WIP/PixelEdits_2026-09-21.md) (the edits C9 deferred to the raster primitives) |
| **C10 Brush** | [`Brush_2026-09-20.md`](Brush_2026-09-20.md) (done) |
| **C11 Retouch** | [`Retouch_2026-09-20.md`](Retouch_2026-09-20.md) (done) |
| **C12 Adjust/filter** | [`AdjustFilter_2026-09-20.md`](../WIP/AdjustFilter_2026-09-20.md) |
| **C13 Smart ops** | [`SmartOps_2026-09-20.md`](../WIP/SmartOps_2026-09-20.md) |
| **C14 Product hardening** | [`ProductHardening_2026-09-20.md`](../WIP/ProductHardening_2026-09-20.md) |
| **C15 Packaging** | [`Packaging_2026-09-20.md`](../WIP/Packaging_2026-09-20.md) |

| Phase | Outcome | Depends on |
| --- | --- | --- |
| **C8 Transform** | Move/scale/rotate/flip/distort, snapping, guides, numeric inspector, Flip Canvas | C6+C7 |
| **C9 Selection** | Marquee, lasso, wand (`WandPixels`), add/subtract, outline move, expand/contract, inverse, load layer/mask | C6 |
| **C10 Brush** | Compute brush + eraser, `RasterSnapshot` GPU/CPU, 4K budgets from archived brush benchmark notes | C6, C9 for clipped paint |
| **C11 Retouch** | Clone, heal (`HealPixels.c`), smear, gradient, shape, eyedropper + picker | C10 |
| **C12 Adjust/filter** | Levels/Curves/HSV/Exposure/Gradient Map/Grain/Invert/blurs/noise/lens; live preview; selection limit | C6, C4 adjustments |
| **C13 Smart ops** | Content-aware fill (`ContentFill.c`); Remove Background (DirectML child plan or disable) | C9, C12 |
| **C14 Product hardening** | Settings store, crash minidumps, localization pass, UIA for chrome, high contrast | C7 |
| **C15 Packaging** | Portable ZIP, version stamp, winget later; Sparkle-class update is out of v1 | C14 |

Brush performance **initial budgets** (measure on the same class of 4K / 800 px / 120-update
stroke described in archived brush benchmark notes; do not claim Photoshop parity):

- Pointer update p95 ≤ **8 ms** on WARP is **not** the shipping gate; shipping gate is hardware
  + WARP functional. Record hardware p95 and mouse-up; target order-of-magnitude with the
  legacy Debug ~3.7 ms / ~8.7 ms mouse-up on blank 4K, not the pre-tile 1 s mouse-up.
- Mouse-up must **not** materialize a contiguous bitmap (`RasterSnapshot` rule).
- History retained bytes ≤ 256 MiB.

## Test plan

Port characterization from retained numeric golden cases, not UI-driver scripts:

- Format v1–v7 load/save/reject
- Group hierarchy and 64-ancestor reject
- Live-mask cycles / 256-node cap
- History sharing and trim
- Blend-mode goldens
- Brush tile edges, opacity cap, self-intersection density, software fallback
- Wand/heal/content-fill numeric goldens from the C kernels
- Viewport mapping + DPI
- Device loss, occlusion, `--warp`
- DxUi pin provenance
- Harness screenshots for chrome (C7+)

Pattern: in-process tests in `Tests/`, invoked by `test.ps1`, ASan Debug required for any
parser/I/O/tile code. GUI tests default to no-activation (RedSalamander). Interactive desktop
only with an explicit lease/warning if a future suite needs real focus.

## Performance contract (from C1, not a cleanup pass)

Record in `Core_PerformanceAndResources.md`:

- Idle: block on messages + waitable Present + DXGI occlusion; no periodic timer for the shell.
- Hidden/minimized: 0 canvas surface bytes, 0 DxUi surface bytes, no Prepare/Composite.
- Compose/Present: no heap after warmup on the canvas path; DxUi composition remains
  allocation-free; dirty DxUi prep stays within library caps (Release ≤ 64 allocs/frame).
- Document limits as in ProjectStore.
- Any intentional regression needs a measured justification in the owning spec.

## Out of scope (this program)

- Compiling or shipping the macOS Swift app from this repo
- PSD / XCF / Photoshop plugin compatibility
- Camera RAW (`libraw`) unless a later product decision
- Sparkle-equivalent auto-update
- D3D12 / Vulkan / OpenGL
- CMake
- WinUI 3 / WPF / Qt / Dear ImGui as the chrome toolkit
- RedXe dashboard pages, plugin host, XENEON dock
- RedSalamander file-manager dual-pane product (Splitter in DxUi is the only shared ask)
- Editing DxUi `LocalizedAdaptiveLayout` files as a side quest
- Silently changing RedXe or RedSalamander pins

## STOP conditions (any phase)

1. Toolchain drift: CMake, MinGW, or a second C++ standard.
2. Second GPU API or second D3D device for UI.
3. GDI text or GDI chrome HWND over the swap chain.
4. Exceptions crossing `wWinMain` / `WndProc` / COM.
5. `catch (...)`.
6. Busy-wait Present or polling occlusion.
7. Rendering from `WM_PAINT`.
8. Enumerating DxUi sources or floating the DxUi pin onto sibling `main`.
9. Putting document/canvas types into `include/DxUi`.
10. Killing `RedPrism.exe` (or any other exe) to make a build proceed.
11. Committing `.build/` or secrets.
12. Format v8 or new blend modes without a spec.
13. Computer Use / desktop screenshots used as spec evidence.
14. Starting C8+ without an indexed child plan and a C7 shell that opens/saves a document.

If validation fails, fix the slice; do not rebaseline perf or skip ASan.

## Done criteria for this master plan

This master file is **not** done when C0–C7 land. It stays ACTIVE as the program index until:

- [x] C0–C7 checkboxes are complete with specs, tests, and harness screenshots.
- [x] C8–C15 exist as indexed child plans or are explicitly REJECTED in this file with a reason.
- [x] `AGENTS.md` and domain specs describe the shipped shell without needing this plan.
- [x] Then move **this** file to `Specs/Plans/Done/` and leave only unfinished child plans in WIP.

Early execution success (the next bounded slice) is **C0 then C1**: a warning-clean six-profile
project that presents a flip-model window under WARP and never paints with GDI.

## Maintenance

- Child plans must copy the sibling header: status, planned-at SHA, drift command, ownership,
  remaining outcomes, authoritative specs, implementation/tests, verification, done, STOP.
- When archived behavioral reference document semantics change upstream, update `IO_ProjectFormat.md` and tests
  first, then code.
- DxUi updates are pin bumps after library closeout, never submodule-at-HEAD.
- Keep archived behavioral reference as the behavioral oracle; do not “improve” blend math without goldens.

## Suggested executor toolkit

- RedPrism skills (after C0) for all C++.
- DxUi `consumer-integration` and `embedded-rendering` for C2.
- DxUi `control-development` only inside the DxUi repo under the HOLD plan.
- RedXe `direct3d11-rendering` for Renderer.
- `plan-execute-with-spec-update`: every slice updates the matching `Specs/<Domain>/` file in
  the same change.
