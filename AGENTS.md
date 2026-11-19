# RedPrism development guidance

RedPrism is a Windows-native raster editing application built with Win32, Direct3D 11, DXGI, DirectWrite,
WIL, yyjson, libzip, Clipper2, DxUi, and modern C++.

## Requirements

- Windows 11 build 22000.2600 minimum
- Visual Studio 2026 (18.x), toolset `v145`, Desktop development with C++
- Windows 11 SDK 10.0.26100
- MSVC `stdcpplatest`, Unicode Win32 APIs
- Debug, Release, and ASan Debug on x64 and ARM64, including real sanitizer instrumentation
- PowerShell 7 for repository tooling and skill validation; no Python is required for routine validation.
- Git for the pinned vcpkg bootstrap
- Package manager: vcpkg (`vcpkg.json` + `vcpkg-tool.json`)
- Graphics: Direct3D 11, DXGI, DirectWrite, DxUi (from C2). No GDI text. No chrome HWND over the swap chain.

## Drive-root output policy

Do not create scratch, caches, evidence, copied checkouts, or other output directories directly at a drive root.
Use repository-local generated outputs or an existing approved workspace. The only permitted new drive-root
output name is `<Project>.Perf`, named after the owning project (for example `RedSalamander.Perf` or
`RedPrism.Perf`), and the user must explicitly authorize the exact path before its first creation or
initialization. A matching name, environment variable, low disk space, or authorization for another drive or
project is not authorization. Keep outputs beneath the approved root; do not invent sibling root folders. Preserve
runner-specific ownership markers and authorization. Historical cleanup inventories and preserves useful evidence
and recovery data, checks process use, repairs live junction and worktree references, and verifies preservation
before removal. Never follow junctions during cleanup or leave compatibility junctions at forbidden old drive-root
paths.

## Canonical guidance

Future qualification archives use `archive-qualification.ps1` and
[`Specs/Build/Build_QualificationArchives.md`](Specs/Build/Build_QualificationArchives.md), rather than per-run
payload copy loops. Retain independent run evidence and every existing source/freshness/profile/stage/runtime
check. Shared payloads remain read-only; edit only independent copies. Preserve frozen historical helpers.

- This file is the repository-wide instruction source.
- `Specs/README.md` defines specification authority and the plan lifecycle.
- Normative product behavior lives in `Specs/<Domain>/`. Read the owning contract before changing behavior.
- Active work lives in `Specs/Plans/WIP/` and is indexed in `Specs/Plans/WIP/README.md`.
- Repo skills live in `.agents/skills/` and must pass `.\validate-skills.ps1`.
- Generated products belong under `.build/`; never commit them.
- Preserve the documented blend math, project schema and independent numeric goldens in the owning specs.

Native test output follows [the retention contract](Specs/Testing/Testing_RunEvidence.md): unique per-run evidence,
preserved failures and interruptions, verified identities/hashes/streams/metadata, and no automatic old-run sweep.

## Skills

Shared skill index: `../RedHome/AGENTS.md`; packages: `../RedHome/.agents/skills/`. If `../RedHome` is missing, clone https://github.com/RedSalamanders/RedHome.git to that path. This repository's guidance and domain specs own product behavior and validation.

| Skill | Use it for |
| --- | --- |
| [hlsl-composition](.agents/skills/hlsl-composition/SKILL.md) | Document-space passes, coverage, clipping stacks, the brush compute shader, build-time HLSL |
| [numeric-goldens](.agents/skills/numeric-goldens/SKILL.md) | Independent numeric expectations, fixture formulas and documented tolerances |
| [consumer-dxui](.agents/skills/consumer-dxui/SKILL.md) | Exact DxUi pin, pinned-library facts, pin bumps |

Cross-repository C++/WIL, `yyjson`, `d3d11-win32-host`, `localization`, `performance-resources`, and `spec-workflow` skills live in the sibling `RedHome` repository. So do the shared skills that replaced four local ones, each with RedPrism's facts in its `references/repository-facts.md`:

| Shared skill | Use it for |
| --- | --- |
| `native-build` | `build.ps1`, `test.ps1`, `package.ps1`, configurations, outputs (was `build-redprism`) |
| `test-evidence` | In-process tests, goldens, WARP fixtures, harness screenshots, run evidence (was `test-harness`) |
| `app-settings` | The settings document, its schema, cold recovery, atomic saves, UI-thread live reload (was `settings-store`) |
| `crash-dumps` | Local minidumps, the pending marker and prompt, the `--crash-probe` gate (was `crash-handling`) |

## Architecture ownership

| Owner | Responsibility |
| --- | --- |
| `Application` | HWND, messages, DPI, command line, frame scheduling, files, commands, `DocumentSession`, `JobScheduler`, `CanvasViewport`, `CanvasRenderer` |
| `Renderer` | D3D11 device, swap chain, Present, occlusion, adapter-of-monitor, frame order (chrome layout → canvas → chrome) |
| `CanvasRenderer` | Document-space GPU passes, coverage textures, clipping stacks, raster residency, sparse-raster assembly and the brush stroke preview; borrows the device |
| `GpuBrushCoverage` | The brush compute engine (`BrushCoverage.hlsl`) on the application device: per-tile density and preview buffers, readback of the preview only |
| `ChromeHost` | DxUi EmbeddedHost shell: menus, rail with palette wells, tabs, Move-tool, selection-tool, brush, Gradient and Shape headers, the adjustment panel card with its drawn curve, levels, spectrum and ramp controls (`AdjustControls.*`, `ChromeHostAdjust.cpp`), layers with splitter and opacity stepper, status, color picker card, canvas overlay (transform box, marching ants, lasso draft, brush tip, gradient line, shape draft, sample ring, clone source); strings from resources |
| Document (`Common/Document.*`, `DocumentHistory`, `TransformEdit`, `Selection`, `BrushStroke`, `WarpStroke`, `Heal`, `ImageFilters`, `Adjust`, `AdjustEdit`, `ProjectStore`, `ImageCodecs`) | Layers, validation, history, sparse rasters, transform editing (drag geometry, snapping, warp), selection (Clipper2 booleans, coverage, wand and mask tracing), brush strokes (tiles, CPU kernel, snapshots), retouch (the spot healing kernel, clone and blur samples, warps, gradients, shapes), pixel edits (lifted pixels, invert, masks from the selection, the floating selection transform, the clipboard model), color adjustments and filters (the levels, curves, exposure, gradient map, grain, hue/saturation cube, noise, lens and motion blur kernels, the panel sessions and adjustment layer editing), `.prism` I/O, WIC — never GPU device ownership |
| DxUi EmbeddedHost | Retained chrome; borrows the app device; no swap chain |
| `JobScheduler` (`Common/Jobs.*`), owned by `Application` | Bounded job records, class/priority scheduling, cancellation, ordered completion delivery and UI actions; standard-only public header with a private implementation; no document mutation or GPU ownership |
| `GpuWorkQueue` (`RedPrism/GpuWork.*`), optional registered owner pump | Bounded staging/copy slots, completion fences/events, mapped-row leases and retired context/resource references; owner-only recording/Map/Unmap; shares the scheduler's event wait pool; no swap chain, Present, pixel copying on the UI thread or document mutation |
| `GpuCommandStream`, `GpuCommandPump`, `GpuRecordingCommands` | Two-slot command transport, original-context owner execution/acknowledgement and worker-only bounded recording into a private deferred context; explicitly enabled single scheduler recorder, shared admission/FIFO, no immediate context on workers, read Map, swap chain, Present or document mutation |
| `Kernels` (`Common/Kernels.*`, `Kernels_<Tier>_<Module>.*`) | Immutable CPU dispatch tables, guarded CPU/OS capability detection and noinline tier entry points; no UI, document mutation or GPU ownership |
| `Executor`, `WorkerPool`, `WaitPool` | Standard C++ kernel execution interface; private bounded Windows compute pool and separate event-wait pool, cancellation, callback/COM lifetime; no document mutation or GPU ownership |

## Mandatory rules

- WIL RAII for owned Windows and COM resources. No manual `Release` / `DestroyWindow` of owned objects.
  Destroy HWNDs with `wil::unique_hwnd::reset()`.
- `wil::com_ptr_nothrow<T>` in HRESULT code. Assign with `.put()`. Never `AddRef` then `attach`.
- No exceptions across `wWinMain`, `WndProc`, or COM. `catch (...)` is forbidden. The `JobScheduler` worker runner
  catches only `std::bad_alloc`, translating it to a failed job with `E_OUTOFMEMORY`. Other allocation failures
  remain fatal except at explicit `noexcept` construction/admission boundaries that return `E_OUTOFMEMORY` only
  when the caller then fails the operation or exits. Hot paths do not catch.
- Unicode `stdcpplatest`, `/Wall`, `/permissive-`, SDL, zero warnings, with /WX disabled, `/utf-8`. Headers that are not Win32
  headers do not depend on `windows.h` typedefs (`UINT`, `DWORD`); use fixed-width or plain types.
- One D3D11 device, one flip-model swap chain, one Present. Do not render from `WM_PAINT`.
- Idle, hidden, minimized, suspended, and occluded work blocks on events. Busy polling is prohibited.
- Steady-state hot paths use bounded reusable storage and avoid heap allocation. The composition composites only
  when the document changed; a settled document costs zero passes per frame.
- Every user-visible chrome string is a `STRINGTABLE` entry with positional `{0}` placeholders
  (`Common/Resources.h`, `Specs/UI/UI_ApplicationChrome.md`). Document data such as default layer names is not chrome.
- Formatting into fixed buffers normally uses `std::format_to_n`; the allocation-sensitive crash writer in
  `Common/CrashDumps.cpp` deliberately retains fixed-buffer `swprintf_s` per `Specs/Core/Core_CrashHandling.md`.
- `.prism` Swift `Codable` JSON shapes on disk (`Specs/IO/IO_ProjectFormat.md`): non-optional properties are always
  present, optional ones are omitted when nil, and dictionaries keyed by non-`CodingKeyRepresentable` enums serialize as
  alternating key/value arrays (read both array and object forms; write the array form in fixed order).
- Caches keyed by `Raster::Identity()` hold the `Raster`; a freed storage address may be reused by another image.
- Screenshots for specs and docs come from the application harness. Never Computer Use or a desktop screenshot tool.
- `build.ps1` rejects a running `RedPrism.exe` whose path is the selected output and MUST NOT terminate it.
- Consume DxUi only as pinned `DxUi.lib` at the API revision in `Dependencies/DxUi.lock.json`. Do not enumerate library sources. Do not reset
  the sibling `../DxUi` checkout from restore. Design against the pinned library as it is (see `consumer-dxui` for the facts that
  matter: `Toolbar` has no layout, `Toggle` is a switch, icon-only buttons draw with the icon font).
- Keep DxUi C++ inside `RedPrism.exe`. If a plugin ABI appears later, it is COM/POD only.
- Every behavior ships with an independent numeric golden and a documented tolerance, never a screenshot
  comparison or "looks fine". New behavior with no legacy counterpart uses an independent reference
  named by its owning spec (for example, state-machine expectations for worker jobs and crash handling). Existing
  legacy goldens are never rebaselined to match an implementation.

## Performance

`Specs/Core/Core_PerformanceAndResources.md` applies to every design. Among correct designs, prefer the lowest
steady-state CPU, memory, allocation, copy, synchronization, wake-up, and GPU-submission cost. Intentional
regressions need measurement and a spec or WIP justification.

## Validation before declaring a change complete

These are the checks a complete change answers for. Account for them with `Test-Changes.ps1 -Mode PrePush`
(scoped testing policy below) and run them in full only where qualification requires it:

```powershell
.\validate-skills.ps1
.\format.ps1 -Check
.\test.ps1 -Platform x64 -Configuration Debug
.\test.ps1 -Platform x64 -Configuration Release
.\test.ps1 -Platform x64 -Configuration "ASan Debug"
```

ARM64 builds cross-compile with `build.ps1 -Platform ARM64`; runtime claims need a native ARM64 host.

Successful default affected runs advance their source and index baseline; explicit partial, failed, mutated or CI-pending runs do not. `CI_PENDING` requires every named candidate check to pass before merge, including when this private account cannot enforce protected checks. Native, manual, platform and performance gates stay in [Specs/Testing/Testing_Validation.md](Specs/Testing/Testing_Validation.md).

## Interactive desktop tests

A test that takes real foreground focus, the cursor or the clipboard runs only after the person at the desktop has
agreed to the time; silence never authorizes one. It warns before and while it runs, targets only windows the test
owns, and restores the foreground window, keyboard focus and pointer however it ends. Use the repository's
interactive entry point and desktop lease where it has one. Every other test and every harness capture stays hidden
and non-activating, and no scoped, CI or partial pass closes a focus-taking gate.

## Scoped testing policy

The user's accepted workflow replaces unconditional full-test iteration. While editing, run `Test-Changes.ps1 -Explain`
and then the affected default; use `-Mode PrePush` to account for the remaining local and pull-request obligations
without repeating identical ones, and explicit Full where qualification requires it. A pending CI check is never a
passed result. Reuse only exact successful evidence for the same source, pin, toolchain, binary and scenario. Native
test files use contextual `<Component>.Tests.<Subject>.h/.cpp` names, listed in the native file inventory where the
repository keeps one. Focus-taking work requires agreement to the time, and no scoped pass closes that gate. Build,
runtime and platform qualification still applies to changed behavior. [Specs/Testing/Testing_Validation.md](Specs/Testing/Testing_Validation.md) and [the test guide](Tests/README.md) own the details.

## Changes workflow

Record each change as one dated Markdown fragment under `Changes/`; follow [Changes/README.md](Changes/README.md)
and [Specs/Core/Core_Changes.md](Specs/Core/Core_Changes.md). Do not hand-edit generated `CHANGELOG.md` entries.
Keep the fragment in the change: never fold it in a feature change and never edit `CHANGELOG.md`. Fragments are
folded on `main` after merge by `Tools/Submit-ChangelogFold.ps1`, which opens a fold pull request of its own. Run
`Tools/Test-Changelog.ps1` while editing; PrePush and CI run it with `-Base`, which refuses any `CHANGELOG.md` change
that is not an exact fold of fragments already on `main` or, where the repository releases through
`Tools/Release-Changelog.ps1`, the exact generated release transition. Validation is read-only.

## Shared native build policy (2026-10-28)

Use /Wall with /WX disabled in every first-party profile. A build is accepted
only with zero warnings and zero errors; fix actionable diagnostics rather than
adding blanket suppressions. Ordinary Debug uses /ZI, ASan Debug uses /Zi.
Release explicitly enables /OPT:ICF; Debug/ASan explicitly disable it.
Follow Specs/Build/Build_ConsoleAndPaths.md for the common green result, safe
terminal launch links and short dependency folders with full identity checking.

## Unified CI policy

Use [Specs/Build/Build_UnifiedCI.md](Specs/Build/Build_UnifiedCI.md) for hosted CI. PRs run compiler-free checks and formatting feedback. Main native qualification starts after five continuous quiet minutes; both native Release test branches must pass before either production Release/ZIP branch starts. Scheduled dependency/security/status checks remain compiler-free. Formatting commits, extended qualification and any ZIP or winget publication are explicit manual actions. PR checks do not satisfy native acceptance or waive applicable local scoped tests. Shared-engine pins in .ci/project.json and workflow callers must agree. Preserve dependency pins, native toolchain, evidence and desktop ownership contracts.
