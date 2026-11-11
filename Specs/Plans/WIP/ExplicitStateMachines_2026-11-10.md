# Explicit state machines and lifetime safety

Status: ACTIVE — plan only; code implementation has not started.
Priority: P1 session-ownership safety, async preview/GPU lifecycle invariants, developer ergonomics (DX) and agent safety (AX).
Owner: RedPrism maintainers.
Prepared: 2026-11-10.
Planned-at commit: `500eff00cacbeb0a79bdb17b3960552bb000c4dc` (`RedSalamanders/RedPrism`).

- **Owning contracts**: [repository guidance](../../../AGENTS.md), [specification authority](../../README.md), [architecture](../../Core/Core_Architecture.md), [performance and resources](../../Core/Core_PerformanceAndResources.md), [DxUi integration](../../Core/Core_DxUiIntegration.md), [changes workflow](../../Core/Core_Changes.md), [document model](../../Document/Document_Model.md), [document history](../../Document/Document_History.md), [document tools](../../Document/Document_Tools.md), [composition](../../Rendering/Rendering_Composition.md), [brush rendering](../../Rendering/Rendering_Brush.md), [application chrome](../../UI/UI_ApplicationChrome.md), [windowing](../../UI/UI_Windowing.md), [toolchain](../../Build/Build_Toolchain.md), [unified CI](../../Build/Build_UnifiedCI.md), [validation](../../Testing/Testing_Validation.md), and [run evidence](../../Testing/Testing_RunEvidence.md).
- **Coordinated plans**: [UiNeverBlocks_2026-09-22.md](UiNeverBlocks_2026-09-22.md) and its `HOLD` child plans remain paused since 21 October 2026 unless resumed from that file; [MergeReview_2026-10-28.md](MergeReview_2026-10-28.md) and [RedTeamDelivery_2026-10-31.md](RedTeamDelivery_2026-10-31.md) retain ownership of `Q1`–`Q15` and `WP0`–`WP6`; [ScopedTesting_2026-10-05.md](ScopedTesting_2026-10-05.md) retains ownership of its remaining qualification checklist. This plan does not duplicate rows from `MergeReview_2026-10-28.md` and does not resume paused P1 milestones; it owns only private explicit state-machine consolidation and tooling lock/lease hardening with zero `.prism` schema, blend-math, CLI, or `DxUi.lock.json` changes.

## 1. Mandatory pre-execution drift check

Before editing any file for this plan, run these read-only checks from the `RedPrism` repository root:

```powershell
git status --short
git rev-parse HEAD
git diff --name-status 500eff00cacbeb0a79bdb17b3960552bb000c4dc HEAD -- AGENTS.md Common RedPrism Build Tests Test-Changes.ps1 validate.ps1 Specs
git diff --cached --name-status
git ls-files --others --exclude-standard
git worktree list --porcelain
```

Reopen every file reported by `git diff` and verify line anchors against current `HEAD` before applying any slice.

## 2. Architectural decision and non-breaking boundaries

While `Application::_drag` in [Application.h](../../../RedPrism/Application.h) (`L1802`–`L1804`, `RF-904`) already uses a value-type `std::variant` for active canvas pointer gestures, 15 major subsystems across `Application`, `ChromeHost`, `CanvasRenderer`, `GpuWorkQueue`, `GpuCommandStream`, display controllers, `DocumentSession`, `BrushStroke`, `WarpStroke`, `TransformEdit`, `JobScheduler`, and `Build/` still encode mutually exclusive lifecycle phases across correlated booleans, sentinel integers (`_slot == 64`, `_part == 2`), or nullable worker-loaned smart pointers (`_brush == nullptr` while loaned to a worker).

### Non-breaking design patterns

1. **Pattern A — Exhaustive flat `enum class Phase : std::uint8_t` + centralized transitions**:
   - Used for window close/end-session sequencing, async preview/commit sessions, `GpuWorkQueue` slot sub-states, `GpuCommandStream` 64-bit packed `{epoch:32, threadId:24, phase:8}` CAS words, `BrushStroke` persona/ownership phases, and `WarpStroke` tile states. Switch statements omit `default:` so `/Wall` enforces exhaustiveness (`C4061`/`C4062`).
2. **Pattern B — Inline value-type `std::variant<std::monostate, ...>`**:
   - Used for `ActiveHostContinuation` stage payloads, `ChromeHost` menu focus-return targets (`RestoreCanvasFocus` vs `RestoreControlFocus`), `TransformEdit` target kinds (`LayerTarget`, `MaskTarget`, `GroupTarget`, `FloatingTarget`), and `GpuWorkQueue` receiver payloads—keeping steady-state hot paths allocation-free.
3. **Pattern C — `ExclusiveSessionLease<T>` for worker-loaned sessions**:
   - Prevents `nullptr` from ambiguously meaning both *"no active session"* and *"session checked out to a background worker"* across `DocumentSession` brush, warp, lasso, and adjustment/filter worker lifecycles, preserving deterministic cancellation (`cancelPending`) across worker slices.
4. **Pattern D — Discriminated PowerShell verdicts, `Global\` mutexes & retryable evidence locks**:
   - Aligns [ScopedTesting.psm1](../../../Build/ScopedTesting.psm1) and [TestRunEvidence.psm1](../../../Build/TestRunEvidence.psm1) with cross-session `Global\` mutex names and bounded `ERROR_SHARING_VIOLATION` retry on `Native::Lock`.

### Hard invariants (zero breaking changes)

- **Zero changes to `.prism` format 7**, blend math, independent numeric goldens, `STRINGTABLE` user-visible behavior, or command-line switches.
- **Zero changes to [DxUi.lock.json](../../../Dependencies/DxUi.lock.json)** or `DxUi` source files; `DxUi` C++ stays strictly inside `RedPrism.exe`.
- **Zero heap allocation on composition/render hot paths**, zero rendering in `WM_PAINT`, zero `catch (...)`, and WIL RAII only.

## 3. Source-verified defect and hazard ledger

### 3.1 Concrete state-tearing hazards and tooling defects

| ID | Location & symbol | Verified defect / state-tearing hazard | Non-breaking fix |
| --- | --- | --- | --- |
| `RP-SM-01` | `DocumentSession` worker-loaned stroke/lasso state in [Document.h](../../../Common/Document.h) (`L3620`–`L3662`) & `BrushStroke` in [BrushStroke.h](../../../Common/BrushStroke.h) (`L290`–`L355`, `L514`–`L586`) | While `_brush` or `_warp` is checked out to a sample/finish worker (`TakeBrushForSample` / `TakeBrushForFinish`), `_brush` is `nullptr`, so `HasBrush()` returns `false` while `HasBrushInFlight()` (`_brush != nullptr \|\| _brushUpdatingOpening.has_value() \|\| _brushFinishingOpening.has_value()`) is `true`. Meanwhile `BrushStroke` checks 7–9 booleans simultaneously (`_valid`, `_failed`, `_warpReplay`, `_privateWorkerOwner`, `_workerRasterEdit`, `_detachedFinish`, `_finalCoveragePrepared`, `_finalCoverageComplete`). | Wrap worker-loaned tool sessions in `ExclusiveSessionLease<T>` (`Idle`, `OwnerHeld`, `SampleWorkerLoaned`, `FinishWorkerLoaned`) with sticky `cancelRequested`, and replace `BrushStroke`'s 11 booleans with `StrokePersona` + `OwnershipPhase`. |
| `RP-SM-02` | Window close, callback nesting & `WM_QUERYENDSESSION` in [Application.h](../../../RedPrism/Application.h) (`L832`–`L845`, `L1726`–`L1735`), [Application.cpp](../../../RedPrism/Application.cpp), [ApplicationFiles.cpp](../../../RedPrism/ApplicationFiles.cpp) | 9 independent booleans (`_closeDeferred`, `_jobCloseRequested`, `_shutdownDeferred`, `_shutdownStarted`, `_shutdownComplete`, `_closeConfirmed`, `_endSessionPending`, `_endSessionPromptDeferred`, `_endSessionWriteBlocked` = 512 combinations) coordinate 7 real close/session-end states across nested Win32 messages and job callbacks. | Consolidate into `WindowShutdownStateMachine` (`Running`, `CloseRequested`, `EndSessionQuery`, `PromptingUnsaved`, `ConfirmedPendingUnwind`, `ShuttingDown`, `Terminated`) + explicit reentrancy guard bitmask (`InWin32Message`, `InJobCallback`, `WriteInFlight`). |
| `RP-SM-03` | `ChromeHost` UIA deferral & menu focus return in [ChromeHost.h](../../../RedPrism/ChromeHost.h) (`L1114`–`L1125`, `L1170`–`L1178`) | Menu focus restoration pairs raw `DxUi::Control* _menuReturnFocus` with `bool _menuReturnCanvas` (allowing contradictory `_menuReturnFocus != nullptr && _menuReturnCanvas` or dangling pointers if dynamic layer rows rebuild while a menu is open); UIA structure rebuilds block indefinitely if `_pointerPressed` latches. | Replace `_menuReturnFocus` + `_menuReturnCanvas` with `std::variant<RestoreCanvasFocus, RestoreControlId>` and model UIA dirtiness via `enum class UiaSnapshotState : std::uint8_t { Clean, PlacementDirty, StructureDirty }`. |
| `RP-SM-04` | Typed opacity tombstone `_opacityRecorded` & `_shapeCommitPreview` in [Application.h](../../../RedPrism/Application.h) (`L1545`, `L1562`–`L1566`, `L1704`–`L1725`) | `_opacityRecorded` persists as an unkeyed `std::optional<double>` tombstone after `_opacityEdit` resets, risking suppression of a subsequent focus-loss commit of the same value; `_shapeCommitPreview` splits shape ownership across `DocumentSession::_shape` and `Application`. | Bind `OpacityStepperSession::ClosedAwaitingBlur` to `(documentSerial, layerId, focusEpoch)` and unify `_shapeCommitPreview` + `_shapeTicket` + `_shapeStamp` inside `ShapeCommitSession`. |
| `RP-SM-05` | `CanvasRenderer::SwapUtilityView` in [CanvasRenderer.h](../../../RedPrism/CanvasRenderer.h) (`L436`–`L514`) | `SwapUtilityView()` mutates the live display renderer in-place by swapping `_canvas`, `_stack`, `_coverage`, `_documentDirty`, `_canvasHasMips`, and `_canvasMipsStale` into `_utilityView` during offscreen utility/readback passes, and permits invalid `!_canvasHasMips && _canvasMipsStale`. | Extract `CompositionSurfaceSet` (`canvas`, `stack`, `coverage`, `MipState { Unallocated, Stale, Valid }`, `documentDirty`) passed explicitly to display vs utility composition passes instead of swapping members in-place. |
| `RP-SM-06` | `TestRunEvidence.psm1` `Native::Lock` & `ScopedTesting.psm1` mutex in [TestRunEvidence.psm1](../../../Build/TestRunEvidence.psm1) (`L64`–`L71`) and [ScopedTesting.psm1](../../../Build/ScopedTesting.psm1) (`L388`–`L400`) | `Native::Lock` opens lease files with `dwShareMode = 0` and zero retry (`ERROR_SHARING_VIOLATION` on transient Defender/indexer handle inspection), and `Enter-ScopedIterationLease` creates `RedSalamanders.Iteration.$key` without a `Global\` session prefix. | Add bounded retry/backoff on Win32 error `32` (`ERROR_SHARING_VIOLATION`) in `Native::Lock` and prefix `Global\RedSalamanders.Iteration.$key` in `Enter-ScopedIterationLease`. |

### 3.2 Complete 15-subsystem state-machine catalog

| # | Subsystem & location | Current implicit encoding | Target non-breaking refactor |
| --- | --- | --- | --- |
| `4.1` | Window close & `WM_QUERYENDSESSION` ([Application.h](../../../RedPrism/Application.h) `L1726`–`L1735`, [ApplicationFiles.cpp](../../../RedPrism/ApplicationFiles.cpp)) | 9 booleans + `_messageDepth` + `_endSessionGeneration` (`RP-SM-02`) | **Pattern A:** `WindowShutdownStateMachine` + `ReentrancyFlags`. |
| `4.2` | Host continuation pipeline ([Application.h](../../../RedPrism/Application.h) `L1586`–`L1633`, [ApplicationContinuations.cpp](../../../RedPrism/ApplicationContinuations.cpp)) | `HostStage` + 3 return registers (`_hostAfterPrerequisite`, `_hostAfterPanelCancel`, `_hostAfterOpacity`) + 7 stage booleans | **Pattern B:** `std::optional<ActiveHostContinuation>` with fixed `std::array<HostStage, 4>` return stack and stage payload `std::variant`. |
| `4.3` | 4-way async preview/commit mailboxes (`Color`, `Gradient`, `PixelMove`, `Transform` in [Application.h](../../../RedPrism/Application.h) `L1549`–`L1676`, [PreviewMailbox.h](../../../Common/PreviewMailbox.h)) | 52 duplicated fields including shadow `_*MailboxOpen` and `_*Completing` booleans | **Pattern A:** Reusable `AsyncPreviewCommitSession` (`Inactive`, `Opening`, `LivePreview`, `DrainingForCommit`, `Committing`, `Cancelling`). |
| `4.4` | Freehand/polygonal lasso host replay ([Application.h](../../../RedPrism/Application.h) `L593`–`L634`, `L1476`–`L1486`, [HostLassoInput.h](../../../RedPrism/HostLassoInput.h) `L148`–`L288`) | 4 `_lasso*Opening` `uint64_t` counters + `_selectedTerminal` shadow branch across 9 `HostLassoInput` methods | **Pattern A:** `LassoHostSession` (`Idle`, `OpeningWorker`, `ReplayingToSession`, `LiveSampling`, `ClosingQueued`, `FinishingWorker`, `Cancelled`) + first-class terminal promotion in `PointerInputStream`. |
| `4.5` | Canvas brush deferred input & clone readback ([Application.h](../../../RedPrism/Application.h) `L463`–`L504`) | `CanvasCloneSampleState` (`active`, `ready`, `failed`) + 3 `_canvasBrush*Opening` fields | **Pattern A:** `CloneSamplePhase` (`Inactive`, `PendingGpuReadback`, `Ready`, `Failed`) + `CanvasBrushHostPhase`. |
| `4.6` | Alt-drag `DuplicateGesture` ([Application.h](../../../RedPrism/Application.h) `L1749`–`L1771`) | `pointerDown`, `finishQueued`, `finishCancel`, `abandoned`, `copy.has_value()` | **Pattern A:** `DuplicateGesture::Phase` (`CreatingCopyWhileDown`, `CreatingCopyReleased`, `CreatingCopyCancelled`, `DraggingCopy`, `CommittingPlacement`, `RevertingCopy`). |
| `4.7` | Layer opacity stepper & shape commit shadow ([Application.h](../../../RedPrism/Application.h) `L1545`, `L1562`–`L1566`, `L1704`–`L1725`) | Unkeyed `_opacityRecorded` tombstone + split `_shapeCommitPreview` (`RP-SM-04`) | **Pattern A + C:** Keyed `OpacityStepperSession` and RAII-scoped `ShapeCommitSession`. |
| `4.8` | `ChromeHost` UIA snapshot & menu focus return ([ChromeHost.h](../../../RedPrism/ChromeHost.h) `L1114`–`L1125`, `L1170`–`L1178`) | `_menuReturnFocus` + `_menuReturnCanvas` + 5 UIA deferral booleans (`RP-SM-03`) | **Pattern A + B:** `UiaSnapshotState` + `MenuReturnTarget` `std::variant`. |
| `4.9` | `CanvasRenderer` utility view & mip state ([CanvasRenderer.h](../../../RedPrism/CanvasRenderer.h) `L429`–`L514`) | In-place `SwapUtilityView()` + `_canvasHasMips`/`_canvasMipsStale` (`RP-SM-05`) | **Pattern A:** Explicit `CompositionSurfaceSet` + `MipState` (`Unallocated`, `Stale`, `Valid`). |
| `4.10` | `GpuWorkQueue::Impl::Slot` ([GpuWork.cpp](../../../RedPrism/GpuWork.cpp) `L25`–`L71`, `L824`–`L973`) | `Slot::State` subdivided by 6 booleans and 4 nullable receiver pointers | **Pattern A + B:** Explicit `Slot::State` sub-phases + `ReceiverPayload` `std::variant` reset in one assignment. |
| `4.11` | Display controllers (`Committed`, `SelectionOutline`, `LassoDraft` in [CommittedDisplayController.h](../../../RedPrism/CommittedDisplayController.h), [SelectionOutlineDisplayController.h](../../../RedPrism/SelectionOutlineDisplayController.h), [LassoDraftDisplayController.h](../../../RedPrism/LassoDraftDisplayController.h)) | `Cell::Phase` layered with 6–8 retirement/quarantine booleans per cell | **Pattern A:** Explicit `Retiring*` sub-phases shared across a common double-buffered cell helper. |
| `4.12` | `GpuCommandStream` retirement handshake ([GpuCommandStream.h](../../../RedPrism/GpuCommandStream.h) `L122`–`L139`, [GpuCommandStream.cpp](../../../RedPrism/GpuCommandStream.cpp)) | 5 separate atomics (`_producerThread`, `_finished`, `_retired`, `_retirementAuthorized`, `_backpressured`) | **Pattern A:** Single 64-bit packed atomic `{epoch:32, threadId:24, phase:8}` with CAS transitions. |
| `4.13` | `BrushStroke`, `WarpStroke`, & worker-loaned `DocumentSession` ([BrushStroke.h](../../../Common/BrushStroke.h), [WarpStroke.h](../../../Common/WarpStroke.h), [Document.h](../../../Common/Document.h)) | 11 `BrushStroke` booleans, raw `_dirty` tile bitmasks, nullable worker-loaned `_brush`/`_warp` (`RP-SM-01`) | **Pattern A + C:** `StrokePersona` + `OwnershipPhase`, `WarpStroke::TileState`, and `ExclusiveSessionLease<T>`. |
| `4.14` | `TransformEdit`, `DocumentHistory::Publication` & `JobScheduler` ([Document.h](../../../Common/Document.h) `L1161`–`L1193`, `L1857`–`L1866`, [Jobs.h](../../../Common/Jobs.h) `L129`–`L134`, [Jobs.cpp](../../../Common/Jobs.cpp)) | External `_transformDuplicate`, `Publication` permit booleans, magic `_slot == 64`/`_part == 2` sentinels | **Pattern A + B:** `TransformTarget` `std::variant`, `PublicationStatus` enum, `SharedRetainedMemoryLease::LeaseKind`, and `CallbackContext` scope guard. |
| `4.15` | `ScopedTesting.psm1` & `TestRunEvidence.psm1` ([ScopedTesting.psm1](../../../Build/ScopedTesting.psm1), [TestRunEvidence.psm1](../../../Build/TestRunEvidence.psm1), [Test-Changes.ps1](../../../Test-Changes.ps1)) | Unprefixed iteration mutex + zero-wait `Native::Lock` (`RP-SM-06`) | **Pattern D:** `Global\` iteration mutex + bounded `ERROR_SHARING_VIOLATION` retry. |

### 3.3 Target C++23 blueprint (`ExclusiveSessionLease<T>`)

```cpp
enum class LoanedSessionPhase : std::uint8_t
{
    Idle = 0,
    OwnerHeld,
    SampleWorkerLoaned,
    FinishWorkerLoaned,
};

template <typename T>
class ExclusiveSessionLease final
{
  public:
    [[nodiscard]] LoanedSessionPhase Phase() const noexcept { return _phase; }
    [[nodiscard]] bool IsInFlight() const noexcept { return _phase != LoanedSessionPhase::Idle; }
    [[nodiscard]] T* OwnerPtr() noexcept
    {
        return _phase == LoanedSessionPhase::OwnerHeld ? _owned.get() : nullptr;
    }

    void Begin(std::shared_ptr<T> session) noexcept
    {
        _owned = std::move(session);
        _cancelRequested = false;
        _phase = _owned ? LoanedSessionPhase::OwnerHeld : LoanedSessionPhase::Idle;
    }

    [[nodiscard]] std::shared_ptr<T> LoanToWorker(LoanedSessionPhase workerPhase) noexcept
    {
        _phase = workerPhase;
        return std::move(_owned);
    }

    [[nodiscard]] bool ReclaimFromWorker(std::shared_ptr<T> returned) noexcept
    {
        _owned = std::move(returned);
        _phase = _owned ? LoanedSessionPhase::OwnerHeld : LoanedSessionPhase::Idle;
        return !std::exchange(_cancelRequested, false);
    }

    [[nodiscard]] bool RequestCancel() noexcept
    {
        switch (_phase)
        {
        case LoanedSessionPhase::Idle:
            return false;
        case LoanedSessionPhase::OwnerHeld:
            return true;
        case LoanedSessionPhase::SampleWorkerLoaned:
        case LoanedSessionPhase::FinishWorkerLoaned:
            _cancelRequested = true;
            return true;
        }
        return false;
    }

  private:
    std::shared_ptr<T> _owned;
    LoanedSessionPhase _phase = LoanedSessionPhase::Idle;
    bool _cancelRequested = false;
};
```

## 4. Ordered implementation slices

1. **Slice S0 — Drift check & PowerShell lock/lease hardening (`4.15`)**:
   - Run Section 1 drift checks.
   - Add bounded `ERROR_SHARING_VIOLATION` retry to `Native::Lock` in [TestRunEvidence.psm1](../../../Build/TestRunEvidence.psm1) and `Global\` session prefixing in [ScopedTesting.psm1](../../../Build/ScopedTesting.psm1), with regression coverage in `Tests/BuildProcessTests/Invoke-ToolingTests.ps1`.
2. **Slice S1 — `Common` session leases, `BrushStroke`/`WarpStroke` phases & `JobScheduler` lease kinds (`4.13`, `4.14`)**:
   - Introduce `ExclusiveSessionLease<T>`, `BrushStroke` `StrokePersona`/`OwnershipPhase`, `WarpStroke::TileState`, `TransformEdit` target `std::variant`, `DocumentHistory::PublicationStatus`, and `SharedRetainedMemoryLease::LeaseKind`, backed by independent state-machine numeric goldens under `Tests/`.
3. **Slice S2 — GPU queue, command stream, display controllers & `CanvasRenderer` surface sets (`4.9`–`4.12`)**:
   - Refactor `GpuWorkQueue::Impl::Slot`, pack `GpuCommandStream` into a 64-bit CAS word, unify display-controller retirement sub-phases, and replace `CanvasRenderer::SwapUtilityView()` with `CompositionSurfaceSet` while preserving zero-pass settled frames and WARP goldens.
4. **Slice S3 — `Application` shutdown, continuations, preview sessions & `ChromeHost` UIA/focus (`4.1`–`4.8`)**:
   - Consolidate `WindowShutdownStateMachine`, `ActiveHostContinuation`, `AsyncPreviewCommitSession`, `OpacityStepperSession`, `ShapeCommitSession`, and `ChromeHost` `MenuReturnTarget` / `UiaSnapshotState`, with deterministic headless self-tests.
5. **Slice S4 — Specification reconciliation & qualification**:
   - Reconcile durable invariants into [Core_Architecture.md](../../Core/Core_Architecture.md), [Document_Tools.md](../../Document/Document_Tools.md), [Rendering_Composition.md](../../Rendering/Rendering_Composition.md), [UI_Windowing.md](../../UI/UI_Windowing.md), and [Testing_Validation.md](../../Testing/Testing_Validation.md).

## 5. Validation commands and expected outcomes

```powershell
pwsh -NoProfile -File ./validate.ps1
pwsh -NoProfile -File ./Tools/Test-Changelog.ps1 -Base origin/main
pwsh -NoProfile -File ./format.ps1 -Check
pwsh -NoProfile -File ./Test-Changes.ps1 -Explain
pwsh -NoProfile -File ./Test-Changes.ps1
pwsh -NoProfile -File ./test.ps1 -Platform x64 -Configuration Debug
pwsh -NoProfile -File ./test.ps1 -Platform x64 -Configuration Release
pwsh -NoProfile -File ./test.ps1 -Platform x64 -Configuration "ASan Debug"
pwsh -NoProfile -File ./build.ps1 -Platform ARM64 -Configuration Debug
pwsh -NoProfile -File ./build.ps1 -Platform ARM64 -Configuration Release
pwsh -NoProfile -File ./build.ps1 -Platform ARM64 -Configuration "ASan Debug"
```

- **Static & formatting checks**: `validate.ps1` (including `validate-skills.ps1`, `Assert-ScopedTestNames`, and `Invoke-ToolingTests.ps1`), `Test-Changelog.ps1 -Base origin/main`, and `format.ps1 -Check` pass with zero findings; `git diff -- Dependencies/DxUi.lock.json` is empty.
- **Native builds & headless tests**: All six `x64`/`ARM64` configurations compile under `/Wall` with zero warnings; `x64` `Debug`, `Release`, and `ASan Debug` suites pass with independent numeric/state-machine goldens and zero rebaselined legacy goldens.
- **Separate qualification gates**: Native `ARM64` runtime claims and paired performance measurements remain explicit under [Core_PerformanceAndResources.md](../../Core/Core_PerformanceAndResources.md) and [Testing_Validation.md](../../Testing/Testing_Validation.md).

## 6. Out-of-scope boundaries, rollback and stop conditions

- **Out of scope**: Resuming paused P1 milestones in [UiNeverBlocks_2026-09-22.md](UiNeverBlocks_2026-09-22.md), changing `.prism` format 7, changing blend math or legacy numeric goldens, or modifying `DxUi` / bumping `DxUi.lock.json`.
- **Stop conditions**:
  - Stop immediately if any slice touches code under an active `HOLD` boundary in [UiNeverBlocks_2026-09-22.md](UiNeverBlocks_2026-09-22.md) in a way that activates a paused P1 worker path rather than purely encapsulating existing `main` state.
  - Stop if any existing WARP or numeric golden changes or if steady-state frame/composition allocation counts increase above zero.
- **Rollback**: Each slice (`Build/`, `Common/`, GPU/Renderer, `Application`/`ChromeHost`) lands as an independent commit and reverts cleanly without affecting other slices.

## 7. Done criteria and closeout

1. Slices `S0`–`S4` pass `validate.ps1`, `format.ps1 -Check`, all six `x64`/`ARM64` builds with zero warnings, and `x64` `Debug`/`Release`/`ASan Debug` test suites.
2. Durable invariants are recorded in their owning `Specs/<Domain>/` contracts.
3. Move this file to `Specs/Plans/Done/ExplicitStateMachines_2026-11-10.md`, remove its row from `Specs/Plans/WIP/README.md`, and add an archive entry to [Done/README.md](../Done/README.md).
