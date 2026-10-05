# RedPrism windowing and host command line

Status: current normative product contract
Last reviewed: 2026-09-27
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
| Close | `WM_CLOSE` asks for a document with unsaved work ([`UI_ApplicationChrome.md`](UI_ApplicationChrome.md) Files), then shuts the renderer down and resets the HWND. A close that arrives while another window-procedure call is on the stack (a modal loop: a dialog, a message box, a menu, a caption drag) or inside a chrome dispatch MUST wait: it is posted again once the outermost call returns, so the chrome is never destroyed under its own stack and one prompt never opens over another |
| Session end | See below |
| Pointer loss | See below |
| Minimum size | `WM_GETMINMAXINFO` keeps the client at least the rail, a 200 DIP canvas, the splitter and the minimum layers panel wide, and the menu, tabs, tool header, a 200 DIP canvas and the status strip tall, at the window's DPI (or the `--dpi` layout DPI) |
| Power | `RegisterPowerSettingNotification` for `GUID_SESSION_DISPLAY_STATUS`; display-off blocks Present except in the screenshot and bounded `ui-smoke` correctness harnesses. Full UI performance runs refuse display-off measurements. |
| Adapter follow | `WM_EXITSIZEMOVE`, `WM_DISPLAYCHANGE`, and `WM_DPICHANGED` call `EnsureDeviceForWindowMonitor` |

No chrome child HWND is created over the swap chain. Common file dialogs (`IFileDialog`) are allowed from C7; they are
not chrome.

### Session end

- `WM_QUERYENDSESSION` for a document with unsaved work (the save prompt's test) MUST NOT return TRUE at once unless
  the end is critical (`ENDSESSION_CRITICAL`): it calls `ShutdownBlockReasonCreate` with `IDS_SHUTDOWN_UNSAVED`,
  returns FALSE and posts the save prompt (`kEndSessionMessage`), which runs like a close (after any modal loop has
  unwound). A second request while the prompt waits is refused again without a second prompt. Quiet runs never ask
  unless the self-test answers.
- The posted prompt carries the session-end generation. An old message cannot prompt for a later request.
  Failed continuation admission destroys this request's reason and preserves the document. A critical query
  returns TRUE even while a previous ordinary request remains pending.
- Save (when it saved) or Don't Save destroys the reason and closes the window without asking again, and Windows goes
  on ending the session; Cancel destroys the reason and keeps the window.
- Save before close waits for successful color settlement and write completion. Failed, cancelled or stale
  prerequisites suppress the close; `WM_ENDSESSION(FALSE)` invalidates this generation's late continuation.
- `WM_ENDSESSION(TRUE)` saves the settings as a close does ([`Core_Settings.md`](../Core/Core_Settings.md)): the
  process may end as soon as it returns. `WM_ENDSESSION(FALSE)` (the end was cancelled) destroys the reason, and a
  prompt still waiting or showing no longer closes the window.
- The busy-state policy of the active `UiNeverBlocks` plan (a save in progress at session end) supersedes these rules
  when it lands.

### Pointer loss

Capture, focus and activation loss end every pointer interaction, as the legacy canvas does when it resigns first
responder: `WM_CAPTURECHANGED` (another window or the system took the pointer; the window's own release at
button-up is not a loss), `WM_CANCELMODE` (a dialog or message box is about to show; DefWindowProc then releases the
capture), `WM_KILLFOCUS` and `WM_ACTIVATEAPP(FALSE)`. A brush stroke or warp keeps what it painted, as a release
would; a transform drag goes back to where it started (a drag-only edit drops, an Alt-drag's duplicate with it);
marquee and freehand outlines drop while a polygonal one stays; an outline move keeps its place; lifted pixels and a
shape draft drop; crop, gradient and eyedropper drags stop where they are; Space panning ends (its key-up goes
elsewhere); and DxUi receives `PointerAction::Cancel` then `Leave`, so its own drags restore (the layers splitter
goes back to its start). `WM_KILLFOCUS` and `WM_ACTIVATEAPP(FALSE)` also take the keyboard back from the menu bar a
tapped Alt or F10 gave it ([`UI_ApplicationChrome.md`](UI_ApplicationChrome.md) "Menus and accelerators").
`WM_MOUSELEAVE` reaches the canvas as well as DxUi's hover (the tip circle goes, a drag
without a capture ends); while a drag holds the capture, leaving the window does not end it. Quiet runs (the
self-test, screenshots, the benchmark) drive the pointer synthetically from a hidden window whose capture follows no
real button, so Windows may take it back whenever the user's mouse moves: they honor only the losses their own thread
sends (`InSendMessage` false), never a change another thread or process reports.

Debug and Release use the same titled window. There is no XENEON-only placement path and no missing-display prompt.

### Job pump and shutdown

`Application` owns the `JobScheduler`. Each outer loop pass pumps queued messages, drains worker completions on the
UI thread, then runs at most one queued UI action. Completion application and UI actions do not recurse from a
nested window/control dispatch; reentrant completion-drain and UI-action calls are ignored. UI actions may enter a modal loop only after
revalidating their captured target, and the modal loop cannot recursively drain jobs. The scheduler wake event is
waited with the message queue while idle, hidden or suspended, and with the frame-latency object while waiting to
render. A scheduler wake returns control to the outer pump so hidden/suspended completions are still drained.
`SettleJobs` is reserved for harness/headless use, not a UI command.

The outer pump and `WindowProcedure` have separate optional timing scopes. They measure inclusive wall time;
nested/modal dispatch is included and the two duration streams overlap. Completion apply and deferred UI
actions have their own scopes. No timer or extra wakeup is introduced; details and ETW fields are specified in
[`Core_PerformanceAndResources.md`](../Core/Core_PerformanceAndResources.md#ui-duration-instrumentation).

On shutdown, the application requests cancellation and hides the window before joining scheduler workers. It keeps
their inputs and dependent owners alive until they return, drops undelivered callbacks, then tears down chrome,
renderer/device, HWND and localization/COM state. A shutdown request from inside a job UI callback is deferred
until that callback returns.
Completion-suppressed sampler jobs retain their original ticket, source and history stamp through the scheduler's
worker/retirement join. The join is their shutdown acknowledgement; only then does the host clear that ticket,
finish the exact stamp and sweep scalar openings. Normal completion remains the ordinary acknowledgement path.

### Job activity and end-session policy

The application observes scheduler submissions, progress and retirements on the UI thread. One timer targets the
oldest foreground job's 250 ms deadline; it is killed before handling its message. Progress/completion events
update the indicator without periodic polling. Already-queued timer messages revalidate current activity and cannot
revive a completed job. Shutdown removes the timer and clears its fixed-capacity activity state.

`GetEndSessionWorkPolicy` is the tested pure policy for the P1 file-flow wiring (the current synchronous file flows
do not yet call it). Flags may overlap; each row contributes its action:

| Work | Noncritical query | Critical query | Actual session end |
| --- | --- | --- | --- |
| Save or export in worker encode/write | Block, with a localized saving reason | Allow | Bounded wait on worker completion only; no UI apply |
| Export still in GPU/readback | Allow | Allow | Cancel |
| Clipboard write | Allow | Allow | Cancel |
| None of these | Existing unsaved-document policy | Allow | No worker-write wait |

HostTests enumerates all 32 combinations including the critical flag. Actual worker-write waits, shutdown block
reasons, save-before-close continuations and file-job cancellation land with P1.7.

## Command line

Every switch is declared once in `RedPrism/CommandLine.h`. `--help`, `-h`, `/?`, and `-?` print the catalog and exit 0
before any other switch is applied: to the parent console (a GUI process attaches), to redirected stdout as UTF-8, or,
without either, a message box. `--self-test` suppresses the message box (debugger output only). Preflight recognizes
switches only before `--` and skips separate switch values, so a path/value named `--help` or `--self-test` is not
a switch.

The first unknown token is a command-line error (exit 2, `Unknown argument "<token>". Run RedPrism.exe --help for the
command line.`) shown the same way.

Ordinary runs accept up to 256 positional paths of at most 32767 UTF-16 units each. Order is preserved;
leading `-` requires `--`, after which every token is positional. Empty/null/overlong arguments, excessive
path counts, invalid switches and positional paths mixed with diagnostic/harness modes are refused before
Application construction. Parsing is atomic: failure preserves the previous output value. The parser probes
no filesystem. Startup copies all path views into a request admitted under the host input byte budget, then
queues it only after the initial document exists, binding that document serial before normal interaction.
It follows the ordered OpenPaths flow. Owned paths are released on admission failure or shutdown; failure to
own/admit startup paths fails startup locally.

| Switch | Behavior |
| --- | --- |
| *(none)* | Create the titled Direct3D 11 window and run the idle message loop |
| `--warp` | Force WARP (Microsoft Basic Render Driver) |
| `--isa scalar\|sse2\|avx2\|avx512\|neon` | Force a CPU kernel tier before worker startup. Missing, empty, duplicate, unknown and `auto` values are refused. Unsupported or unimplemented tiers exit 2 with a localized diagnostic before creating the application. Currently only Scalar is implemented; no override selects Auto, which selects Scalar. |
| `--self-test` | Create the titled window **hidden**, force WARP, assert the DPI-adjusted 1280×800 logical client, composite DxUi chrome, prove visible surface then 0 when hidden, exit 0 |
| `--screenshot <png>` | Show the titled window without activating it, present chrome and the canvas (whatever the display's power, the window's visibility or occlusion), start the capture delay after the first frame that reached the display, capture through Windows.Graphics.Capture, exit 0, 8 when this system cannot capture a window (`E_NOTIMPL`, the capture API absent) or 9 when the capture failed or never happened |
| `--dpi 96\|144` | Override layout DPI and the 1280×800 logical client scale for `--screenshot` |
| `--bench brush` | Run the brush benchmark on the selected device and exit |
| `--bench kernels` | Run the CPU kernel benchmark at 3840×2160 with 10 samples, without creating a window or initializing the GPU |
| `--bench kernels45` | Run the CPU kernel benchmark at 8192×5464 with 10 samples, without creating a window or initializing the GPU |
| `--bench kernels-smoke` | Run the CPU kernel benchmark at 32×24 with 2 samples, without creating a window or initializing the GPU; this smoke run is not a performance measurement |
| `--bench buffers`, `--bench buffers45` | Compare ten alternating copy/adopt pairs for Levels, Hue/Saturation and uniform noise at 4K or 45 MP, checking identical outputs; no window or GPU |
| `--bench ui`, `--bench ui45`, `--bench ui100`, `--bench ui-smoke` | Measure 15 validated commands through the real application loop on 4K, 45 MP, 100 MP, or a 64×48 smoke fixture; one warmup, then 10 runs (2 for smoke). Smoke has no timing threshold. See [`Testing_Benchmarks.md`](../Testing/Testing_Benchmarks.md). |
| `--crash-probe <directory>` | Run the crash-handling probe using `<directory>` for its dump and pending marker. |
| `--crash-probe-worker` | Fault two private-pool threads in this process; requires `--crash-probe <directory>`. The two switches may appear in either order. |
| `--help` | Print the catalog and exit 0 |

`--self-test` without `--warp` still uses WARP. Self-test MUST refuse a non-WARP device (exit 5).

Exit codes: 0 ok, 1 allocation, 2 command line or window, 5 graphics (including a frame loop that stopped drawing
after repeated failures; the window closed through the save prompt), 8 screenshot capture unsupported, 9 screenshot
capture failed.

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
runs hidden `--self-test --warp`, and captures `--screenshot --dpi 96` and `--dpi 144` without stealing focus. The
self-test sends `WM_CLOSE` from inside a chrome command callback (the window and the chrome survive, and the close is
posted once the click has returned), sends `WM_QUERYENDSESSION` to a modified document (refused with the block
reason, the posted prompt's Cancel keeps the window, Don't Save posts a close that does not ask again, a critical end
is allowed) and `WM_ENDSESSION(TRUE)` (the settings file is written), and sends `WM_CAPTURECHANGED`, `WM_KILLFOCUS`,
`WM_ACTIVATEAPP` and `WM_MOUSELEAVE` mid-gesture (a stroke commits, a Move drag goes back, a splitter drag
restores, Space panning ends).

### Sampler Escape while admission waits

Escape on a live sampler MUST seal accepted input and release its pointer gesture/capture immediately,
including while the original scheduler ticket awaits memory admission. It requests cancellation without
publishing or releasing unfinished source/row/GPU owners. The selected receipt, ticket, history barrier and
reservation survive until acknowledgement; then the original receipt reports cancellation and joined cleanup
drains all opening references. Previously published color remains selected. No source is captured on refusal
or cancellation before admission; palette, document and history stay unchanged in that case.
