#include "Application.h"

#include "Tests/Support/Support.Tests.DocumentFixture.h"

#include "ChromeHost.h"
#include "Runtime.Tests.SelectionNudgeTestGate.h"

#include <array>
#include <cstdio>
#include <memory>
#include <optional>
#include <utility>

namespace
{
class OccupyNudgeTestSlot final : public Job
{
  public:
    JobResult Run(JobContext&) override
    {
        return {};
    }
    void Completed(JobResult) noexcept override {}
};
DocumentSelection NudgeFaultRectangle(double left, double top)
{
    return DocumentSelection{false, SelectionLoops{SelectionLoop{Vec2{left, top}, Vec2{left + 20, top},
                                                                 Vec2{left + 20, top + 20}, Vec2{left, top + 20}}}};
}
} // namespace

int Application::RunSelectionNudgeFaultSelfTest() noexcept
{
    const auto originalHistoryBudget = _session.history.RetainedByteLimit();
    const auto restoreHistoryBudget =
        wil::scope_exit([this, originalHistoryBudget]() noexcept
                        { _session.history = DocumentHistory{kHistoryEntryLimit, originalHistoryBudget}; });
    enum class Mode
    {
        Success,
        Ordered,
        RealQueued,
        Pump,
        Cancel,
        Failure,
        CaptureFailure,
        HistoryFault,
        Generation,
        Active,
        Mask,
        Crop,
        Epoch,
        Replacement,
        Lasso,
        Outline,
        NestedCancel,
        Saved,
        ZeroRedo,
        ResidencyRefresh,
        ResidencyFault,
        ResidencyCancel
    };
    struct Case final
    {
        const char* name;
        Mode mode;
        std::optional<DocumentHistory::PrepareStage> stage;
    };
    constexpr std::array cases{
        Case{"success", Mode::Success, {}},
        Case{"ordered", Mode::Ordered, {}},
        Case{"real-queued-input", Mode::RealQueued, {}},
        Case{"pump", Mode::Pump, {}},
        Case{"cancel", Mode::Cancel, {}},
        Case{"worker-failure", Mode::Failure, {}},
        Case{"capture-failure", Mode::CaptureFailure, {}},
        Case{"before-snapshot", Mode::HistoryFault, DocumentHistory::PrepareStage::BeforeSnapshot},
        Case{"after-snapshot", Mode::HistoryFault, DocumentHistory::PrepareStage::AfterSnapshot},
        Case{"entry", Mode::HistoryFault, DocumentHistory::PrepareStage::Entry},
        Case{"stack-growth", Mode::HistoryFault, DocumentHistory::PrepareStage::StackGrowth},
        Case{"trim-index", Mode::HistoryFault, DocumentHistory::PrepareStage::TrimIndex},
        Case{"generation", Mode::Generation, {}},
        Case{"active", Mode::Active, {}},
        Case{"mask", Mode::Mask, {}},
        Case{"crop", Mode::Crop, {}},
        Case{"epoch", Mode::Epoch, {}},
        Case{"replacement", Mode::Replacement, {}},
        Case{"lasso", Mode::Lasso, {}},
        Case{"outline", Mode::Outline, {}},
        Case{"nested-cancel", Mode::NestedCancel, {}},
        Case{"saved", Mode::Saved, {}},
        Case{"zero-redo", Mode::ZeroRedo, {}},
        Case{"residency-refresh", Mode::ResidencyRefresh, {}},
        Case{"residency-fault", Mode::ResidencyFault, {}},
        Case{"residency-cancel", Mode::ResidencyCancel, {}}};
// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4623 4626 5027)
    struct State final
    {
        Application& owner;
        Mode mode;
        ContinuationReceipt receipt;
        std::optional<ContinuationReceipt> younger;
        std::optional<ContinuationReceipt> undo;
        std::optional<CanvasDocument> before;
        std::optional<CanvasDocument> afterMutation;
        DocumentHistory::StateView history;
        DocumentHistory::StateView afterHistory;
        std::optional<Guid> active;
        std::optional<Guid> afterActive;
        std::optional<CropRect> afterCrop;
        Guid revision;
        std::uint64_t generation = 0;
        std::uint64_t afterGeneration = 0;
        bool afterMask = false;
        bool visited = false;
        bool held = false;
        bool mutationReached = false;
        bool cancelPending = false;
        bool youngerPending = false;
        bool pumpPending = false;
        bool zoomChanged = false;
    };
#pragma warning(pop)

    constexpr UINT kPumpMessage = WM_APP + 0x4cc;
    const auto originalViewport = _viewport;
    const auto originalShift = _acceleratorShift;
    const auto restore = wil::scope_exit(
        [this, originalViewport, originalShift]() noexcept
        {
            _viewport = originalViewport;
            _acceleratorShift = originalShift;
            _selectionNudgeTestGate.reset();
            _hostAdmissionAllocationFailureForTest = false;
            if (_session.IsMovingSelection())
                _session.CancelSelectionMove();
            _session.CancelLasso();
        });
    int assertions = 0;
    int failures = 0;
    int modes = 0;
    const auto expect = [&assertions, &failures](const char* mode, const char* check, bool condition) noexcept
    {
        ++assertions;
        failures += condition ? 0 : 1;
        std::fprintf(stderr, "%s selection-nudge-fault-host.%s.%s\n", condition ? "PASS" : "FAIL", mode, check);
        return condition;
    };
    const auto prepare = [this, originalHistoryBudget]()
    {
        _session.history = DocumentHistory{kHistoryEntryLimit, originalHistoryBudget};
        _session.ClearDocument();
        _session.CreateDocument(40, 40, true);
        _session.AddBlankLayer();
        _session.SetSelection(NudgeFaultRectangle(10, 10), L"Nudge fault source");
        _session.history.Reset();
        _session.history.MarkSaved();
        DocumentChanged();
        _viewport.zoom = 1;
        _viewport.followsFit = false;
    };
    const auto pump = [](void* opaque, void* native) noexcept
    {
        auto& state = *static_cast<State*>(opaque);
        auto& owner = state.owner;
        auto& message = *static_cast<MSG*>(native);
        if (message.message == SelectionNudgeTestGate::kMessage)
        {
            state.visited = true;
            const auto gate = owner._selectionNudgeTestGate;
            state.held = gate && owner._selectionNudgeTicket && owner._selectionNudgeStamp &&
                         owner._historyWork.Busy() && state.receipt.Read().outcome == HostOutcome::Pending &&
                         owner._session.Document() == state.before && owner._session.history.IsCurrent(state.history) &&
                         owner._jobs.ReservedMemoryBytes() >= gate->reservationBytes && gate->captures.load() == 1;
            if (state.mode == Mode::Ordered)
            {
                state.younger = owner.QueueHostInput(HostSelectionNudgeInput{0, -10});
                state.undo = owner.QueueHostInput(HostCommandInput{ChromeHost::kCommandUndo});
                state.youngerPending = state.younger.value().Read().outcome == HostOutcome::Pending &&
                                       state.undo.value().Read().outcome == HostOutcome::Pending &&
                                       state.receipt.Read().outcome == HostOutcome::Pending;
                owner._selectionNudgeTestGate.reset(); // Its payload retains the first gate; younger work is ungated.
            }
            if (state.mode == Mode::RealQueued)
            {
                const auto queuedBefore = owner._hostRequests.Size();
                owner._acceleratorShift = true;
                owner.OnCommand(ChromeHost::kCommandNudgeSelectionUp);
                owner._acceleratorShift = false;
                state.youngerPending = owner._hostRequests.Size() == queuedBefore + 1 &&
                                       state.receipt.Read().outcome == HostOutcome::Pending;
                state.undo = owner.QueueHostInput(HostCommandInput{ChromeHost::kCommandUndo});
                state.youngerPending =
                    state.youngerPending && state.undo.value().Read().outcome == HostOutcome::Pending;
                owner._selectionNudgeTestGate.reset();
            }
            if (state.mode == Mode::Pump)
            {
                const auto zoom = owner._viewport.zoom;
                owner.OnCommand(ChromeHost::kCommandZoomIn);
                state.zoomChanged = zoom != owner._viewport.zoom && owner._historyWork.Busy() &&
                                    state.receipt.Read().outcome == HostOutcome::Pending;
                if (!PostMessageW(owner._window.get(), kPumpMessage, 0, 0))
                    static_cast<void>(SetEvent(gate->release.get()));
                return;
            }
            if (state.mode == Mode::Cancel || state.mode == Mode::NestedCancel)
            {
                owner.CancelHostContinuations();
                state.cancelPending = state.receipt.Read().outcome == HostOutcome::Pending && owner._historyWork.Busy();
            }
            switch (state.mode)
            {
            case Mode::Generation:
                owner._session.EditForTest([](CanvasDocument& doc) { doc.resolution = 144; });
                state.mutationReached = owner._session.Generation() != state.generation;
                break;
            case Mode::Active:
                if (state.before && !state.before.value().layers.empty())
                    owner._session.SelectLayer(state.before.value().layers.front().id);
                state.mutationReached =
                    owner._session.ActiveLayerID() != state.active && owner._session.Generation() == state.generation;
                break;
            case Mode::Mask:
                if (state.active)
                    owner._session.SelectLayerTarget(state.active.value(), true);
                state.mutationReached =
                    owner._session.IsMaskSelected() && owner._session.Generation() == state.generation;
                break;
            case Mode::Crop:
                owner._session.SetCropFrame(CropRect{0, 0, 20, 20});
                state.mutationReached = owner._session.CropFrame() == std::optional(CropRect{0, 0, 20, 20}) &&
                                        owner._session.Generation() == state.generation;
                break;
            case Mode::Epoch:
                owner._session.history.Begin(L"Epoch nudge fixture", owner._session.Document(),
                                             owner._session.ActiveLayerID());
                owner._session.history.End(owner._session.Document(), owner._session.ActiveLayerID());
                state.mutationReached =
                    !owner._session.history.IsCurrent(state.history) && owner._session.Generation() == state.generation;
                break;
            case Mode::Replacement:
                owner._session.CreateDocument(41, 41, true);
                state.mutationReached = owner._session.Document() && owner._session.Document().value().width == 41;
                break;
            case Mode::Lasso:
                owner._session.BeginLasso({0, 0}, SelectionMode::New);
                state.mutationReached = owner._session.PendingLasso().has_value();
                break;
            case Mode::Outline:
                state.mutationReached =
                    owner._session.BeginSelectionMove() && owner._session.SelectionMoveOpening().has_value();
                break;
            case Mode::Saved:
                owner._session.history.MarkSaved(state.revision);
                break;
            case Mode::Cancel:
            case Mode::CaptureFailure:
            case Mode::Failure:
            case Mode::HistoryFault:
            case Mode::NestedCancel:
            case Mode::Ordered:
            case Mode::Pump:
            case Mode::RealQueued:
            case Mode::ResidencyCancel:
            case Mode::ResidencyFault:
            case Mode::ResidencyRefresh:
            case Mode::Success:
            case Mode::ZeroRedo:
            default:
                break;
            }
            state.afterMutation = owner._session.Document();
            state.afterHistory = owner._session.history.CaptureState();
            state.afterGeneration = owner._session.Generation();
            state.afterActive = owner._session.ActiveLayerID();
            state.afterMask = owner._session.IsMaskSelected();
            state.afterCrop = owner._session.CropFrame();
            static_cast<void>(SetEvent(gate->release.get()));
            return;
        }
        if (message.message == kPumpMessage)
        {
            state.pumpPending = owner._historyWork.Busy() && state.receipt.Read().outcome == HostOutcome::Pending;
            if (owner._selectionNudgeTestGate)
                static_cast<void>(SetEvent(owner._selectionNudgeTestGate->release.get()));
        }
        owner.DispatchQueuedMessage(message);
    };
    for (const auto& test : cases)
    {
        ++modes;
        prepare();
        if (test.mode == Mode::Mask)
        {
            _session.AddLayerMask();
            if (_session.ActiveLayerID())
                _session.SelectLayerTarget(_session.ActiveLayerID().value(), false);
            _session.history.Reset();
        }
        if (test.mode == Mode::NestedCancel)
            _session.history.Begin(L"Outer nudge fixture", _session.Document(), _session.ActiveLayerID());
        if (test.mode == Mode::ZeroRedo)
        {
            _session.history.Begin(L"Redo Fixture", _session.Document(), _session.ActiveLayerID());
            _session.EditForTest([](CanvasDocument& doc) { doc.resolution = 144; });
            _session.history.End(_session.Document(), _session.ActiveLayerID());
            _session.Undo();
        }
        const bool residency = test.mode >= Mode::ResidencyRefresh;
        Raster lateResidencySource;
        std::uint64_t originalSelectionBytes = 0;
        if (residency)
        {
            constexpr std::array<std::uint8_t, 4> literal{51, 52, 53, 255};
            const Raster leaf = Raster::Make(1, 1, RasterFormat::Bgra8888, literal.data(), literal.size());
            lateResidencySource = Raster::MakeSparse(2, 1, RasterFormat::Bgra8888, {}, 0, 0, {{0, 0, leaf}}, 1);
            auto historical = DocumentFixture::Copy(_session.Document().value());
            historical.layers.front().asset = RasterAsset{lateResidencySource, {}, L"Historical nudge pixels"};
            originalSelectionBytes = _session.Selection().value().loops.StorageBytes();
            // The new outline excludes its own storage; the retained original outline still counts. Only eight
            // bytes remain for historical pixels, so the grown leaf+cache (12) must drop the oldest entry.
            _session.history = DocumentHistory(100, 8 + originalSelectionBytes);
            auto baseline = DocumentHistory::PrepareEdit(_session.history.CaptureState(), std::move(historical).Seal(),
                                                         _session.ActiveLayerID(), _session.Document(),
                                                         _session.ActiveLayerID(), L"Saved nudge cache fixture");
            if (!baseline || !_session.history.Publish(baseline.value()))
                return 5;
            baseline.reset();
            _session.history.MarkSaved();
            if (_session.history.RetainedBytes(_session.Document()) != 4 || lateResidencySource.IsMaterialized())
                return 5;
        }
        const auto gate = std::make_shared<SelectionNudgeTestGate>();
        gate->release.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (!expect(test.name, "gate-event", static_cast<bool>(gate->release)))
            return 5;
        gate->throwCaptureFailure = test.mode == Mode::CaptureFailure;
        gate->throwHistoryStage = test.stage;
        gate->lateResidencySource = lateResidencySource;
        gate->throwResidencyRefresh = test.mode == Mode::ResidencyFault;
        gate->cancelResidencyRefresh = test.mode == Mode::ResidencyCancel;
        if (test.mode == Mode::Failure)
            gate->result = {JobStatus::Failed, E_ACCESSDENIED};
        _selectionNudgeTestGate = gate;
        State state{*this, test.mode, QueueHostInput(HostSelectionNudgeInput{test.mode == Mode::ZeroRedo ? 0 : 1, 0})};
        state.before = _session.Document();
        state.history = _session.history.CaptureState();
        state.active = _session.ActiveLayerID();
        state.generation = _session.Generation();
        state.revision = _session.history.CurrentRevision();
        if (test.mode == Mode::Saved)
            _session.history.MarkSaved(Guid::Generate());
        const auto settled = SettleJobs(_jobs, 15000, pump, &state);
        expect(test.name, "settled", settled == SettleResult::Settled);
        const bool earlyFault = test.mode == Mode::CaptureFailure || test.mode == Mode::HistoryFault;
        expect(test.name, "worker-reachability",
               state.visited == !earlyFault && gate->captures.load() == (test.mode == Mode::CaptureFailure ? 0u : 1u));
        if (!earlyFault)
            expect(test.name, "held-input-history-reservation-before-ack", state.held);
        expect(test.name, "payload-retired-off-owner",
               gate->reservationHeld.load() && gate->retired.load() && gate->retiredOffOwner.load());
        expect(test.name, "complete-resource-release",
               !_selectionNudgeTicket && !_selectionNudgeStamp && !_historyWork.Busy() &&
                   _jobs.ReservedMemoryBytes() == 0 && _hostInputBudget.Used() == 0);
        const auto outcome = state.receipt.Read().outcome;
        if (test.mode == Mode::Cancel || test.mode == Mode::NestedCancel)
            expect(test.name, "cancel-pending-until-worker-ack",
                   state.cancelPending && outcome == HostOutcome::Cancelled && _session.Document() == state.before &&
                       _session.history.IsCurrent(state.history) && _session.Generation() == state.generation);
        else if (test.mode == Mode::ResidencyCancel)
            expect(test.name, "refresh-cancel-preserves-complete-owner",
                   outcome == HostOutcome::Cancelled && _session.Document() == state.before &&
                       _session.history.IsCurrent(state.history) && _session.Generation() == state.generation &&
                       !_session.history.IsModified());
        else if (earlyFault || test.mode == Mode::Failure || test.mode == Mode::ResidencyFault)
            expect(test.name, "failure-preserves-complete-owner",
                   outcome == HostOutcome::Failed &&
                       state.receipt.Read().error ==
                           static_cast<std::int32_t>(
                               earlyFault || test.mode == Mode::ResidencyFault ? E_OUTOFMEMORY : E_ACCESSDENIED) &&
                       _session.Document() == state.before && _session.history.IsCurrent(state.history) &&
                       _session.Generation() == state.generation &&
                       (!test.stage || gate->historyFaultHits.load() == 1));
        else if (test.mode >= Mode::Generation && test.mode <= Mode::Outline)
            expect(test.name, "reached-stale-guard-preserves-new-owner",
                   state.mutationReached && outcome == HostOutcome::Stale &&
                       _session.Document() == state.afterMutation && _session.history.IsCurrent(state.afterHistory) &&
                       _session.Generation() == state.afterGeneration &&
                       _session.ActiveLayerID() == state.afterActive && _session.IsMaskSelected() == state.afterMask &&
                       _session.CropFrame() == state.afterCrop);
        else if (test.mode == Mode::ZeroRedo)
            expect(test.name, "zero-delta-retains-real-redo-and-acknowledges-generations",
                   outcome == HostOutcome::Succeeded && _session.Document() == state.before &&
                       _session.history.CurrentRevision() == state.revision &&
                       _session.history.CaptureState().FutureCount() == 1 &&
                       _session.Generation() == state.generation + 2);
        else
            expect(test.name, "literal-one-pixel-owner-publication",
                   outcome == HostOutcome::Succeeded &&
                       _session.Selection() == std::optional(NudgeFaultRectangle(11, 10)) &&
                       _session.history.UndoName() == L"Move Selection");
        if (residency)
        {
            expect(test.name, "same-ticket-refresh-preserves-input-and-literal-history-budget",
                   gate->captures.load() == 1 && gate->visits.load() == 1 && gate->residentGrowths.load() == 1 &&
                       gate->residencyRefreshes.load() == 1 && gate->refreshReservationHeld.load() &&
                       lateResidencySource.ByteSize() == 8 &&
                       (outcome == HostOutcome::Succeeded
                            ? _session.history.UndoCount() == 1 &&
                                  _session.history.RetainedBytes(_session.Document()) == originalSelectionBytes &&
                                  _session.Generation() == state.generation + 2 && _session.history.IsModified()
                            : _session.history.UndoCount() == 1 &&
                                  _session.history.RetainedBytes(_session.Document()) == 12 &&
                                  _session.history.CurrentRevision() == state.revision &&
                                  !_session.history.IsModified()));
        }
        if (test.mode == Mode::Ordered)
        {
            expect(test.name, "younger-writers-wait-for-ack",
                   state.youngerPending && state.younger && state.undo &&
                       state.younger.value().Read().outcome == HostOutcome::Succeeded &&
                       state.undo.value().Read().outcome == HostOutcome::Succeeded &&
                       _session.history.UndoCount() == 1 && _session.history.CaptureState().FutureCount() == 1);
            _session.Redo();
            expect(test.name, "younger-redo-has-literal-event-time-delta",
                   _session.Selection() == std::optional(NudgeFaultRectangle(11, 0)));
        }
        if (test.mode == Mode::RealQueued)
        {
            expect(test.name, "real-producer-queues-while-first-worker-is-held",
                   state.youngerPending && state.undo && state.undo.value().Read().outcome == HostOutcome::Succeeded &&
                       _session.history.UndoCount() == 1 && _session.history.CaptureState().FutureCount() == 1);
            _session.Redo();
            expect(test.name, "real-queued-producer-retains-event-time-shift",
                   _session.Selection() == std::optional(NudgeFaultRectangle(11, 0)));
        }
        if (test.mode == Mode::Pump)
            expect(test.name, "view-and-message-pump-while-held", state.zoomChanged && state.pumpPending);
        if (test.mode == Mode::NestedCancel)
        {
            expect(test.name, "cancel-retains-outer-depth", _session.history.CaptureState().Depth() == 1);
            _session.SetSelection(NudgeFaultRectangle(11, 10), L"Later inner edit");
            _session.history.End(_session.Document(), _session.ActiveLayerID());
            expect(test.name, "cancel-retains-original-outer-name",
                   _session.history.UndoName() == L"Outer nudge fixture");
            _session.Undo();
            expect(test.name, "later-outer-undo-keeps-original-outline",
                   _session.Selection() == std::optional(NudgeFaultRectangle(10, 10)));
        }
        if (test.mode == Mode::Saved)
        {
            expect(test.name, "saved-marker-does-not-mark-new-nudge-saved", _session.history.IsModified());
            _session.Undo();
            expect(test.name, "latest-saved-marker-survives-publication",
                   !_session.history.IsModified() && _session.history.CurrentRevision() == state.revision &&
                       _session.Selection() == std::optional(NudgeFaultRectangle(10, 10)));
        }
        if (test.mode == Mode::ZeroRedo)
        {
            _session.Redo();
            expect(test.name, "zero-delta-retains-literal-redo",
                   _session.Document().value().resolution == 144 && _session.history.UndoName() == L"Redo Fixture");
        }
        if (_session.IsMovingSelection())
            _session.CancelSelectionMove();
        _session.CancelLasso();
        _selectionNudgeTestGate.reset();
        if (residency)
            _session.history = DocumentHistory(100, HistoryLimitForPhysicalMemory(0));
    }
    for (const bool allocation : {false, true})
    {
        ++modes;
        const char* name = allocation ? "input-allocation-refusal" : "input-budget-refusal";
        prepare();
        const auto before = _session.Document();
        const auto history = _session.history.CaptureState();
        const auto generation = _session.Generation();
        std::optional<HostInputBudget::Reservation> budget;
        if (allocation)
            _hostAdmissionAllocationFailureForTest = true;
        else
            budget = _hostInputBudget.TryReserve(HostInputBudget::kStorageLimit);
        _acceleratorShift = false;
        OnCommand(ChromeHost::kCommandNudgeSelectionRight);
        _hostAdmissionAllocationFailureForTest = false;
        expect(name, "real-producer-refuses-without-fallback",
               (allocation || budget) && _session.Document() == before && _session.history.IsCurrent(history) &&
                   _session.Generation() == generation && !_activeHostRequest && _hostRequests.Size() == 0 &&
                   !_historyWork.Busy());
        budget.reset();
        OnCommand(ChromeHost::kCommandNudgeSelectionRight);
        _acceleratorShift = true;
        expect(name, "fresh-producer-retry-keeps-input-delta",
               SettleJobs(_jobs, 15000, DispatchMessageThunk, this) == SettleResult::Settled &&
                   _session.Selection() == std::optional(NudgeFaultRectangle(11, 10)) &&
                   _jobs.ReservedMemoryBytes() == 0 && _hostInputBudget.Used() == 0);
    }
    ++modes;
    prepare();
    const auto before = _session.Document();
    const auto history = _session.history.CaptureState();
    for (std::size_t i = 0; i < JobScheduler::kCapacity; ++i)
    {
        std::unique_ptr<Job> job = std::make_unique<OccupyNudgeTestSlot>();
        JobTicket ticket;
        if (FAILED(_jobs.Submit(job, {JobClass::Background}, ticket)))
            return 5;
    }
    const auto refused = QueueHostInput(HostSelectionNudgeInput{1, 0});
    for (unsigned int pass = 0; pass < 4 && refused.Read().outcome == HostOutcome::Pending; ++pass)
        static_cast<void>(RunUiActions(_jobs));
    expect("scheduler-admission", "actual-record-table-refuses-before-capture",
           refused.Read().outcome == HostOutcome::Failed &&
               refused.Read().error == static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA)) &&
               _session.Document() == before && _session.history.IsCurrent(history) && !_historyWork.Busy() &&
               !_selectionNudgeTicket && !_selectionNudgeStamp);
    expect("scheduler-admission", "occupied-records-settle",
           SettleJobs(_jobs, 15000, DispatchMessageThunk, this) == SettleResult::Settled);
    const auto retry = QueueHostInput(HostSelectionNudgeInput{1, 0});
    expect("scheduler-admission", "fresh-request-recovers",
           SettleJobs(_jobs, 15000, DispatchMessageThunk, this) == SettleResult::Settled &&
               retry.Read().outcome == HostOutcome::Succeeded &&
               _session.Selection() == std::optional(NudgeFaultRectangle(11, 10)) && _jobs.ReservedMemoryBytes() == 0 &&
               _hostInputBudget.Used() == 0);
    std::fprintf(stderr, "SUMMARY selection-nudge-fault-host assertions=%d failures=%d modes=%d\n", assertions,
                 failures, modes);
    return failures ? 5 : 0;
}
