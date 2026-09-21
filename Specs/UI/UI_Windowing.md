# RedPrism windowing and host command line

Status: current normative product contract
Last reviewed: 2026-09-20
Owner: `Application` HWND, DPI, and `RedPrism/CommandLine.h`

## Scope

This specification owns the top-level window, Per-Monitor-V2 DPI, occlusion/power interaction with the message loop,
and the host command line. Direct3D device and swap-chain ownership remain in `Renderer`
([`Core/Core_Architecture.md`](../Core/Core_Architecture.md)).

The terms **MUST**, **MUST NOT**, **SHOULD**, and **MAY** are normative.

## Process DPI

- The application MUST run as Per-Monitor-V2 DPI aware. `RedPrism/app.manifest` is the persistent declaration.
  `wWinMain` MUST request `DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2` before creating an HWND.
- The default titled-window client canvas is **1280×800** logical pixels at `USER_DEFAULT_SCREEN_DPI` (96). Physical
  client size is `MulDiv(logicalSize, monitorDpi, 96)`.
- Initial outer size MUST use `AdjustWindowRectExForDpi` for `WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN` and
  `WS_EX_APPWINDOW | WS_EX_NOREDIRECTIONBITMAP`.
- After HWND creation, `GetDpiForWindow` MUST correct the outer size before the window is shown, so a shell-selected
  monitor whose DPI differs from the system DPI does not keep the wrong client area.
- `WM_DPICHANGED` MUST apply the suggested rectangle with `SetWindowPos` (`SWP_NOZORDER | SWP_NOACTIVATE`) and
  invalidate one frame.

## Window

| Item | Required |
| --- | --- |
| Class | `RedPrism.Window` |
| Title | `RedPrism` |
| Icon | The embedded `RedPrism/res/RedPrism.ico` supplies the large and small window-class icons; the same resource is visible on the executable. |
| Style | `WS_OVERLAPPEDWINDOW \| WS_CLIPCHILDREN` |
| Extended | `WS_EX_APPWINDOW \| WS_EX_NOREDIRECTIONBITMAP` |
| Binding | `Application*` at `WM_NCCREATE`; destroy via `wil::unique_hwnd::reset()` |
| Background | `WM_ERASEBKGND` returns 1; no GDI fill |
| Paint | `WM_PAINT` invalidates one coalesced GPU frame and `ValidateRect`; no `Present` |
| Close | `WM_CLOSE` shuts the renderer down and resets the HWND |
| Power | `RegisterPowerSettingNotification` for `GUID_SESSION_DISPLAY_STATUS`; display-off blocks Present |
| Adapter follow | `WM_EXITSIZEMOVE`, `WM_DISPLAYCHANGE`, and `WM_DPICHANGED` call `EnsureDeviceForWindowMonitor` |

No chrome child HWND is created over the swap chain. Common file dialogs (`IFileDialog`) are allowed from C7; they are
not chrome.

Debug and Release use the same titled window. There is no XENEON-only placement path and no missing-display prompt.

## Command line

Every switch is declared once in `RedPrism/CommandLine.h`. `--help`, `-h`, `/?`, and `-?` print the catalog and exit 0
before any other switch is applied: to the parent console (a GUI process attaches), to redirected stdout as UTF-8, or,
without either, a message box. `--self-test` suppresses the message box (debugger output only).

The first unknown token is a command-line error (exit 2, `Unknown argument "<token>". Run RedPrism.exe --help for the
command line.`) shown the same way.

| Switch | Behavior |
| --- | --- |
| *(none)* | Create the titled Direct3D 11 window and run the idle message loop |
| `--warp` | Force WARP (Microsoft Basic Render Driver) |
| `--self-test` | Create the titled window **hidden**, force WARP, assert the DPI-adjusted 1280×800 logical client, composite DxUi chrome, prove visible surface then 0 when hidden, exit 0 |
| `--screenshot <png>` | Show the titled window without activating it, present chrome and the canvas, capture through Windows.Graphics.Capture, exit 0 or 8 |
| `--dpi 96\|144` | Override layout DPI and the 1280×800 logical client scale for `--screenshot` |
| `--help` | Print the catalog and exit 0 |

`--self-test` without `--warp` still uses WARP. Self-test MUST refuse a non-WARP device (exit 5).

Exit codes: 0 ok, 1 allocation, 2 command line or window, 5 graphics, 8 screenshot.

## Self-test window checks

Before initializing the renderer, `--self-test` MUST verify:

- `GetDpiForWindow` is non-zero
- style includes `WS_CLIPCHILDREN`
- extended style includes `WS_EX_NOREDIRECTIONBITMAP`
- client size equals the 1280×800 logical canvas scaled to that DPI

## Validation

```powershell
.\test.ps1 -Platform x64 -Configuration Debug
```

`HostTests` pins the catalog. `test.ps1` runs `--help` with redirected stdout, rejects `--no-such-switch` with exit 2,
runs hidden `--self-test --warp`, and captures `--screenshot --dpi 96` and `--dpi 144` without stealing focus.
