# Red-team delivery review (15-day window ending 31 October 2026)

> Spawned on 2026-10-31 from the request to review all RedPrism code from the last 15 days
> for architecture problems, bugs in and around new code, and anything fishy; launch a
> red team; re-check from a second angle; and produce a detailed plan of remaining work.

- **Status**: DECISION. Findings are verified at source; MergeReview questions Q1–Q16 remain
  open; no product fix has started from this file.
- **Correction (2026-11-01, `e79a83a`)**: CI-8 is no longer "no ZIP can be built". `package.ps1` already calls `Get-DxUiSourcePath`. The open residue is `VcpkgToolsetTests.ps1:165` and the stale comment. CI-2 and CI-3 stay closed in MergeReview. `ci.yml` now has a push trigger; see that plan's CI-10 correction.
- **Reviewed**: last 15 days of product C++ and the surrounding host/tooling, from
  `eb7bcee5` (main @ 2026-10-16) through `96d0fb96` (`ej/unified-ci-20261030`). That branch
  is an ancestor of `origin/main` `a7e81222` (31 Oct 2026, PR #31). Product C++ at
  `96d0fb96` matches that tip. Line anchors below are to `a7e81222` unless a row names an
  older MergeReview snapshot.
- **Method**: six parallel area reviews (selection-gesture jobs, Common/ document work,
  architecture/performance, I/O, clipboard/shutdown/GPU/diagnostics, build/CI) plus a
  lead second-angle re-read of every High and Medium claim. The area reviewers died on a
  model-usage limit before filing; their notes were harvested and every High/Medium row
  was re-traced at source by the lead. **Nothing was built or run for this review.**
  Memory magnitudes are source arithmetic, not measurements.
- **Owning specs**: `Core/Core_Architecture.md`, `Core/Core_PerformanceAndResources.md`,
  `Core/Core_CrashHandling.md`, `Core/Core_DxUiIntegration.md`,
  `Document/Document_Tools.md`, `Rendering/Rendering_Composition.md`,
  `UI/UI_ApplicationChrome.md`, `UI/UI_Windowing.md`, `IO/IO_ProjectFormat.md`,
  `Build/Build_Toolchain.md`, `Build/Build_Packaging.md`, `Testing/Testing_Validation.md`.
- **Coordinates with**:
  - [`MergeReview_2026-10-28.md`](MergeReview_2026-10-28.md) (DECISION; PRs #4–#22;
    Q1–Q16). This file re-confirms that review at a later tip and adds RT-01, RT-02 and
    the 31 October CI observation. Do not duplicate MergeReview rows here as a second
    source of truth: fix them from MergeReview, then tick the matching row below.
  - [`DeliveryReview_2026-10-31.md`](DeliveryReview_2026-10-31.md) (ACTIVE; DX-2 only).
    That file's "no product defect" sentence is too narrow: it means no new Critical C++
    hole in the mechanisms it checked. It does not retire MergeReview or this plan.
    DX-2 stays owned there; WP1 only sequences it.
  - [`AdversarialReview.md`](AdversarialReview.md) (historical at `97dd25b`; not re-triaged).
  - [`UiNeverBlocks_2026-09-22.md`](UiNeverBlocks_2026-09-22.md) (HOLD). Latent P1 rows
    only block P1 activation.

This file is a non-normative execution record. Requirements move into the owning domain
spec in the slice that lands them.

## Verdict

`main` is **not production-ready**. No new Critical memory-safety defect was constructed
(no use-after-free, integer overflow, zip-slip, second Present, or failed-save dirty
clear). The scheduler, GPU transport, project-save durability and history ownership still
trace clean.

Delivery is blocked by the same four reasons as MergeReview, all re-confirmed at
`a7e81222`:

1. **Ordinary commands refuse on typical machines.** Inverse / Expand / Contract reserve
   about 2.9 GB for any selection. Magic Wand reserves about 4.8 GB in New mode and
   7–9 GB for Shift/Alt-click on any canvas of 2 MP or more.
2. **User feedback is broken.** Every busy notice is the literal `{0}…`. Many worker
   failures are silent. The selection cursor flickers over a selection.
3. **The assurance gap is open.** Hosted CI still fails at the coordinator in 3–4 s
   (native jobs never start). Local proof at review time was one x64 Release attestation,
   not a Full / ASan / ARM64 matrix.
4. **The shipping image still carries non-product code.** Test TUs are excluded from the
   ZIP, but unguarded self-test bodies, TestGate seams and five benchmark TUs remain in
   product translation units. About 12–14k dormant P1 lines still compile into
   `RedPrism.exe`.

## Production activation map

A normal launch (`Application::Run`, no `--self-test`, `--bench`, `--demo` or
`--screenshot`) enables:

- history residency (`Application.cpp:308`);
- the async selection cursor (`:319`, `_asyncSelectionCursorEnabled = true`);
- recorded Hue readback (`:322`);
- utility readback (eyedropper, Copy Merged, Merge, Export, Wand composite, Clone,
  adjustment opening);
- worker Inverse / Expand / Contract / nudge / Wand / masks;
- worker brush-press opening (`CanDeferCanvasBrush`).

`package.ps1` sets `$env:RedPrismEnableTests = 'false'`. `--self-test` and `--crash-probe`
exit 2 (`Main.cpp:208-212`). These stay **dormant** (compiled, default-off, no
settings / command-line / registry flip):

- `_selectionPressTestEnabled`, `_polygonalDrawingTestEnabled`,
  `_polygonalPressTestEnabled` (armed only from `Application.Tests.*` TUs);
- selection outline / lasso-draft / polygonal journals;
- prepared layer rows, committed / pending display, GPU upload stack, async stroke
  finish / warp live.

`--bench`, `--screenshot`, `--demo` and `--redmonitor-probe` remain live in the ZIP.

## Findings added by this review

MergeReview rows MR-01–MR-25, AR-1–AR-13 and CI-0–CI-13 stay in that file. This section
only names what the 31 October pass added or independently re-anchored.

### New

- [ ] **RT-01** · Medium · isolation · CONFIRMED — **"Complete production test-mode
  isolation" is entry-point only.**
  - 164 `Runtime.Tests.*` / `Application.Tests.*` TUs are conditioned on
    `RedPrismEnableTests` (`RedPrism.vcxproj`). That part holds.
  - About 15 product TUs still compile `Run*SelfTest` bodies with no
    `REDPRISM_ENABLE_TESTS` guard, including
    `ApplicationAdjustmentClosureJobs.cpp:270`, `ApplicationExportJobs.cpp`,
    `ApplicationFileJobs.cpp`, `ApplicationProjectOpenJobs.cpp`,
    `ApplicationProjectSaveJobs.cpp`, `ApplicationHistoryRestoreJobs.cpp`,
    `ApplicationSize.cpp`, `ApplicationSizeJobs.cpp`, `ApplicationJobs.cpp`,
    `ApplicationChromeSync.cpp`, `ApplicationLayerEdits.cpp`,
    `ApplicationReadbackJobs.cpp`, `ApplicationSettings.cpp` and
    `ApplicationCrop.cpp`. Dispatch lives inside the guarded
    `Application::RunSelfTest` (`Application.cpp:678-2993`), so they are not
    reachable from `--self-test` in a packaged build. They still ship as
    `Application` methods.
  - `Application.h` still carries unguarded `_colorWorkTestGate`,
    `_hueReadbackTestGate` and `PassXxxTestGate` helpers. Product `.cpp` files
    include `Support.Tests.DocumentFixture.h` and `Runtime.Tests.*TestGate.h`
    unconditionally. In production the gates stay null and the helpers return
    immediately.
  - **Fix**: guard or move every leftover self-test body and gate (minimum).
    Then AR-1 (harness split).
- [ ] **RT-02** · Medium · isolation · CONFIRMED — **Five benchmark TUs always
  compile; `--bench` / `--screenshot` / `--demo` stay in the ZIP.**
  - Unconditioned `<ClCompile>`: `Common/BenchmarkMachine.cpp`,
    `ApplicationBenchmark.cpp`, `ApplicationCancellationBenchmark.cpp`,
    `ApplicationLayerRowsBenchmark.cpp`, `KernelBenchmark.cpp`.
  - **Fix**: condition them the same way as test TUs, or document them as an
    explicit shipping surface.
- [ ] **RT-03** · Low · diagnostics · CONFIRMED — **`--redmonitor-probe` Sleep-polls
  for 2 s in the shipped binary** (`MonitorDiagnostics.cpp:36-37`). Bounded, once,
  intentional. Leave or refuse in production.
- [ ] **DX-2** · Medium · chrome · CONFIRMED — **Pinned DxUi `a059e07c` (API 3) lacks
  keyboard cancel of menu hover timers.** The fix is DxUi `762983a0` on DxUi
  `origin/main` only (API 4). Owned by
  [`DeliveryReview_2026-10-31.md`](DeliveryReview_2026-10-31.md). Port onto
  `ej/wall-compat-redprism-20261028`, re-pin, keep API 3. Do not take DxUi
  `origin/main`.

### Independently re-confirmed at `a7e81222`

These MergeReview rows were re-read at the later tip. Tick them here when the
MergeReview row closes.

- [ ] **MR-01** · High · `IDS_STATUS_BUSY` is `"{0}…"` (`RedPrism.rc`).
  `FormatPostedNotice` (`ApplicationFiles.cpp:245-249`) formats only
  `IDS_ERROR_UNSUPPORTED_VERSION`. `ReportHostAdmission`
  (`ApplicationContinuations.cpp:450-457`) posts that id for every Failed
  receipt.
- [ ] **MR-02** · High · `InverseSelectionWork.cpp:82-110` charges
  `limits.outputPoints` / `outputLoops` / 128 MiB workspace in both preparation
  and retirement.
- [ ] **MR-03** · High · `WandLayerSelectionWork.cpp:597-651` uses
  `edges = min(4·pixels, 8M)` plus boolean and outline.
- [ ] **MR-07** · Medium · **new and production-live.** `Observe()`
  (`SelectionCursorRequests.h:53-66`) drops `_ready` on every new pixel.
  Completion posts `WM_SETCURSOR` (`ApplicationSelectionCursorJobs.cpp:75-81`).
- [ ] **MR-10** · Medium · `OnQueryEndSession` (`ApplicationFiles.cpp:838-848`)
  sets `_endSessionWriteBlocked` but not `_endSessionPending`.
  `ClearEndSessionWriteBlock` (`:811-821`) restores the unsaved reason only when
  `_endSessionPending`.
- [ ] **MR-11** · Medium · `GpuRecordingCommands.cpp:174` and
  `RecordedUtilityGraph.cpp:41` refuse `DriverConcurrentCreates == FALSE`.
- [ ] **MR-12** · Medium · `ApplicationClipboardJobs.cpp:478` returns
  `HostOutcome::Succeeded` once the private copy finished.
- [ ] **MR-18** · Low · `PathClassificationRead.cpp:91,113` still calls
  `GetFileAttributesW` / `CreateFileW` on the raw path. `Win32FilePath` exists
  and is used on the save path.
- [ ] **CI-0** · process · Every recent hosted run of `main` / PRs still fails in
  3–4 s at `coordinate`; `native` is skipped. Shared pin `c3c778ca` exists.
  Logs of the 31 October runs were purged (`BlobNotFound`).
- [ ] **CI-8** · residue · `package.ps1` calls `Get-DxUiSourcePath` since `18fd8ba`
  (16 hex digits). A missing license under the 40-hex folder is not why a ZIP fails.
  `Tests/BuildProcessTests/VcpkgToolsetTests.ps1:165` still joins the full commit and
  skips when that folder is absent. The `package.ps1` comment still describes
  `source\<commit>`. Owned by MergeReview.

MR-04, MR-05, MR-06, MR-08, MR-09, MR-13, MR-14, MR-15, MR-16, MR-17, MR-19–MR-25
and CI-1–CI-7 / CI-9–CI-13 were not re-litigated. Treat them as still open unless
MergeReview marks them closed.

## Rejected in the second angle

- **ColorEdit default-Ready.** `ColorEditJobResult.status` defaults to `Failed`
  (`ColorEditWork.h:35`). A cancelled / mismatched completion does not apply an
  empty image as Ready.
- **UniformTiles / `Raster::Adopt` padding hole.** DeliveryReview RP-1 stays
  killed. Do not add a second failure path.
- **Project-file orphans for `MonitorDiagnostics.cpp`, `PixelMove*.cpp`,
  `SelectionHitWork.cpp`, `SelectionMoveOpeningWork.cpp`.** All are in
  `RedPrism.vcxproj`. Headers do not need a `<ClCompile>`. Real orphans:
  `Tests/BuildProcessTests/Tooling.Tests.IsaProbe.cpp` and
  `ConcurrencyAnalysisProbe.cpp` (Low).
- **`PendingLasso().value()` in the overlay** runs after `has_value()`
  (`Application.cpp`).
- **SelectionOutlineGestureJob `_click`** is as specified and the job is dormant
  in production.

## Checked clean (do not "fix")

- `WM_PAINT` (`Application.cpp:6296-6299`) only `InvalidateFrame()` +
  `ValidateRect`. No draw, no Present.
- One frame `Present(1, 0)` (`Renderer.cpp:671`). Occlusion probe is
  `Present(0, DXGI_PRESENT_TEST)` only while already occluded.
- `GpuRecordingCommands::InitializeForWork` requires
  `D3D11_DEVICE_CONTEXT_DEFERRED` (`:159`).
- Zip-slip rejection, exploded-save rollback, and `MarkSaved` only after
  `ProjectIoStage::Published` on a still-current save.
- Local minidumps only. `Main.cpp` tells the user nothing is uploaded.
- ARM64 `ChargedStorage` tail (`arm64AbiTailPadding`) occupies base tail; x64
  layout unchanged.
- BoundedClipper / Kernels / Executor diffs in this window are `/Wall` hygiene
  (`static_cast<double>`, explicit `enum` cases, `!= nullptr`). No geometry
  semantics change.
- No HLSL change in the window. Blend math untouched.
- RedMonitor client is ETW TraceLogging only. Single-owner CAS. A missing
  viewer is harmless.

## Work that must not start from this plan

- Do not enable selection-press / polygonal / outline-display / prepared-rows as
  the production path. They stay test-gated until MergeReview F1-2…F1-7 and E-5 /
  E-7 close.
- Do not bump DxUi to `origin/main` (API 4).
- Do not add a second `Raster::Adopt` failure path.
- Do not "fix" BoundedClipper math from this window.
- Do not rebaseline an existing legacy golden.
- Do not treat [`DeliveryReview_2026-10-31.md`](DeliveryReview_2026-10-31.md) as
  the remaining product-work list.

## Detailed execution order

Each package lands with its spec text, its test (a legacy numeric golden where
the oracle covers the behavior), one `Changes/` fragment, and the AGENTS.md
validation required by the owning spec. Do not fold `CHANGELOG.md` in a feature
change.

### WP0 — decisions and measurements (no product code)

Must happen before anyone calls this a release.

- [ ] Answer MergeReview **Q1–Q5, Q8, Q10, Q11, Q13–Q16**.
- [ ] Restore hosted CI billing **or** write the local-Full authorization into
  `AGENTS.md` / `Build_UnifiedCI.md`.
- [ ] Measure on 8 GB and 16 GB machines: Inverse / Offset / Wand
  `reservationBytes`; history-residency frequency and duration; first-eyedropper
  latency on a 45 MP document; forced TDR during Export.
- [ ] Keep a Full local matrix on the tip intended to ship:
  ```powershell
  .\test.ps1 -Platform x64 -Configuration Debug
  .\test.ps1 -Platform x64 -Configuration Release
  .\test.ps1 -Platform x64 -Configuration "ASan Debug"
  ```
  plus the ARM64 cross-build. One x64 Release attestation is not the gate.

### WP1 — quick wins

- [ ] **CI-8 residue** (MergeReview) — `VcpkgToolsetTests.ps1` uses
  `Get-DxUiSourcePath` and fails when the restored source is missing. The
  `package.ps1` comment matches that helper. `package.ps1` already calls it.
- [ ] **MR-01** — rendered busy text has no leftover `{0}`. Test asserts the
  rendered string.
- [ ] **MR-12** — failed OS clipboard publish is not `Succeeded`.
- [ ] **MR-17** — cause-specific messages for clone / paste / save / metadata /
  CLI.
- [ ] **MR-18** — `PathClassificationRead` uses `Win32FilePath::Prepare`.
- [ ] **MR-21** / **MR-23** — clipper manifest hashes committed bytes;
  multiplexer has headroom or a `static_assert`.
- [ ] **DX-2** — port + re-pin API 3 (DeliveryReview). Hover submenu, arrow
  keys, `WM_TIMER`: one popup on the focused row. Same for UIA focus.
- [ ] **CI-1, CI-6, CI-7, CI-9** (MergeReview). CI-2 is fixed and CI-3 is
  superseded in that plan. Do not reopen them. Their `ci.yml:44-49` anchor is
  stale: those lines are the shared-engine checkout.
- [ ] **RT-01 (minimum)** — `RedPrismEnableTests=false` build has zero
  `Run*SelfTest` bodies in product TUs.

### WP2 — admission model

- [ ] **AR-4**, fixing MR-02, MR-03, MR-04, MR-05, MR-06 and MR-07's reservation.
  Size from actual inputs; engine limits as refusal caps; do not charge resident
  immutable inputs; count retirement once; user-initiated commands wait with
  trim. Spec change in `Core_PerformanceAndResources.md`.
- [ ] If AR-4 is too large for this release (Q1): temporary fallback of Inverse /
  Offset / Wand Add-Subtract to the pre-window synchronous route. Do not ship
  the current envelopes.

### WP3 — feedback and input

- [ ] **MR-08** — central failure notice. Failed only; never Stale / Cancelled.
- [ ] **MR-07** — hysteresis: last Ready held while Pending; inline answer below
  a point-count threshold; charge only the job wrapper.
- [ ] **MR-13** — coalesce interior Moves; sum adjacent nudges.
- [ ] **MR-14** — one `IDS_ERROR_FILTER` notice per panel opening, never
  `MessageBoxW`.
- [ ] **MR-10** — dirty + export-during-logoff still prompts after the writer
  ends.
- [ ] **MR-09** — after WP0 measurement: Background, no busy card, cancellable.

### WP4 — resiliency

- [ ] **MR-11** — eyedropper / Export / Merge work when
  `DriverConcurrentCreates == FALSE`.
- [ ] **MR-16** — forced-TDR during Export does not wedge, or "restart required"
  is documented.
- [ ] **MR-19**, **MR-20** — staging sweep / real Clipper ceiling.
- [ ] **AR-8** / **MR-15** — named invariants in the dump; reachable sites
  demoted; recovery per Q11.
- [ ] **RT-02** — condition benchmark TUs or document the shipping surface.
- [ ] **RT-03** — decide: leave or refuse `--redmonitor-probe` in production.

### WP5 — structure (after WP1–WP4)

- [ ] **AR-12** / **AR-13** first (warning boilerplate and build-path tooling).
- [ ] **AR-1** harness split; **AR-2** dormant P1 out of the ZIP; **AR-3** job
  skeleton; **AR-5** selection routes per Q7; **AR-6**, **AR-7**, **AR-11**.
- [ ] **AR-9** / **AR-10** per Q6.
- [ ] **RT-01 (complete)** after AR-1.

### WP6 — release qualification

A build is a release candidate only when all of these are true:

- [ ] WP1 + WP2 (or the Q1 interim fallback) + WP3 user-facing Medium/High items
  are on `main`.
- [ ] Hosted CI is green **or** `AGENTS.md` names the local Full evidence that
  replaces it, and that evidence exists.
- [ ] Local Full matrix: x64 Debug / Release / ASan Debug.
- [ ] ARM64 cross-build; native ARM64 runtime if ARM64 is claimed.
- [ ] Interactive / manual (IME, accessibility, input) if Q5 requires them.
- [ ] Paired performance for AR-4 / MR-05 / MR-07 (same fixture, before/after).
- [ ] `package.ps1` ZIP: `--self-test` exits 2, no `REDPRISM_ENABLE_TESTS`,
  portable startup smoke, DxUi license found (CI-8 closed).
- [ ] DxUi pin includes DX-2; lock sentence in `Core_DxUiIntegration.md` matches
  `Dependencies/DxUi.lock.json`.
- [ ] No existing legacy golden rebaselined.

## Closeout

When every WP item that this release accepted is done, merge any durable
requirement into the owning domain spec, move this file to `Specs/Plans/Done/`,
and remove its index row. Leave MergeReview and DeliveryReview to their own
closeout. Do not close this plan while MR-01, MR-02, MR-03, CI-0/CI-8 or DX-2
are still open unless the owner records an explicit deferral in the owning spec.

## Coverage and limits

- **Covered**: production activation map; test-isolation mechanism; High/Medium
  MergeReview rows listed above; clipboard outcome; end-session write-block;
  Present / WM_PAINT / deferred-context; DxUi pin ancestry; hosted CI status at
  review time; BoundedClipper / kernel hygiene in the window.
- **Not re-read line-by-line**: dormant files (`PolygonalDrawingJob.cpp`,
  `GpuUploadWork.cpp` mid-file, outline display) beyond reachability;
  `CanvasRenderer.cpp` pre-window body (same residue as DeliveryReview);
  self-test bodies as product logic; generated Clipper engine beyond the window
  diff.
- **Not executed**: no build, test, profiler or hardware repro. WP0 must measure
  the High admission rows before WP2 sizes its fix.
