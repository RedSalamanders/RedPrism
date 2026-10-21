// Private real-caller controls. Expected state follows the ordered-host contract, not pixel implementation.
#include "Application.h"

#include "../Tests/Support/Support.Tests.DocumentFixture.h"

#include "ChromeHost.h"
#include "Runtime.Tests.SelectionOffsetTestGate.h"
#include "Selection.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <vector>

int Application::RunSelectionOffsetFailureForAmountSelfTest(int amount) noexcept
{
    const auto originalViewport = _viewport;
    const auto originalHistoryBudget = _session.history.RetainedByteLimit();
    const auto restore = wil::scope_exit(
        [this, originalViewport, originalHistoryBudget]() noexcept
        {
            _selectionOffsetTestGate.reset();
            _session.ClearDocument();
            _session.history = DocumentHistory{kHistoryEntryLimit, originalHistoryBudget};
            DocumentChanged();
            _viewport = originalViewport;
        });
    enum class Mode
    {
        Success,
        Ordered,
        Cancel,
        WorkerFailure,
        CaptureFailure,
        SelectionSnapshot,
        BeforeSnapshot,
        AfterSnapshot,
        Entry,
        StackGrowth,
        TrimIndex,
        Generation,
        Epoch,
        Replacement,
        Crop,
        Active,
        Mask,
        Lasso,
        Brush,
        ViewPump,
        ResidencyRefresh,
        ResidencyFault,
        ResidencyCancel
    };
    constexpr UINT kOffsetPumpMessage = WM_APP + 0x4cf;
    struct State final
    {
        Application& owner;
        Mode mode;
        ContinuationReceipt receipt;
        std::optional<ContinuationReceipt> younger, undo;
        std::shared_ptr<const void> copiedOutline;
        std::optional<CanvasDocument> before;
        Guid revision;
        std::uint64_t generation = 0;
        bool visited = false, held = false, cancellationPending = false;
        bool youngerPending = false, mutationApplied = false, zoomPending = false, messagePending = false;
    };
    int failures = 0;
    const auto expect = [&failures, amount](const char* mode, const char* check, bool condition) noexcept
    {
        std::fprintf(stderr, "%s selection-offset-host.%s.%s.%s\n", condition ? "PASS" : "FAIL",
                     amount > 0 ? "expand" : "contract", mode, check);
        failures += condition ? 0 : 1;
        return condition;
    };
    const auto name = [](Mode mode) noexcept
    {
        switch (mode)
        {
        case Mode::Success:
            return "success";
        case Mode::Ordered:
            return "ordered";
        case Mode::Cancel:
            return "cancel";
        case Mode::WorkerFailure:
            return "worker-failure";
        case Mode::CaptureFailure:
            return "capture-failure";
        case Mode::SelectionSnapshot:
            return "selection-snapshot-fault";
        case Mode::BeforeSnapshot:
            return "before-snapshot-fault";
        case Mode::AfterSnapshot:
            return "after-snapshot-fault";
        case Mode::Entry:
            return "entry-fault";
        case Mode::StackGrowth:
            return "stack-growth-fault";
        case Mode::TrimIndex:
            return "trim-index-fault";
        case Mode::Generation:
            return "stale-generation";
        case Mode::Epoch:
            return "stale-history-epoch";
        case Mode::Replacement:
            return "stale-replacement";
        case Mode::Crop:
            return "stale-crop";
        case Mode::Active:
            return "stale-active";
        case Mode::Mask:
            return "stale-mask";
        case Mode::Lasso:
            return "stale-lasso";
        case Mode::Brush:
            return "stale-brush";
        case Mode::ViewPump:
            return "view-pump";
        case Mode::ResidencyRefresh:
            return "residency-refresh";
        case Mode::ResidencyFault:
            return "residency-refresh-fault";
        case Mode::ResidencyCancel:
            return "residency-refresh-cancel";
        }
        return "unknown";
    };
    const auto pump = [](void* opaque, void* native) noexcept
    {
        auto& state = *static_cast<State*>(opaque);
        auto& owner = state.owner;
        auto& message = *static_cast<MSG*>(native);
        if (message.message == SelectionOffsetTestGate::kMessage)
        {
            const auto gate = owner._selectionOffsetTestGate;
            if (!gate)
                return;
            state.visited = true;
            state.held = owner._selectionOffsetTicket && owner._selectionOffsetStamp && owner._historyWork.Busy() &&
                         state.receipt.Read().outcome == HostOutcome::Pending &&
                         owner._jobs.ReservedMemoryBytes() >= gate->reservationBytes && gate->captures.load() == 1 &&
                         owner._session.Document() == state.before &&
                         owner._session.history.CurrentRevision() == state.revision;
            if (state.mode == Mode::Success)
                state.copiedOutline = gate->offsetOutlineOwner.lock();
            if (state.mode == Mode::Ordered)
            {
                state.younger = owner.QueueHostInput(HostCommandInput{ChromeHost::kCommandDeselect});
                state.undo = owner.QueueHostInput(HostCommandInput{ChromeHost::kCommandUndo});
                state.youngerPending = state.younger->Read().outcome == HostOutcome::Pending &&
                                       state.undo->Read().outcome == HostOutcome::Pending &&
                                       state.receipt.Read().outcome == HostOutcome::Pending &&
                                       owner._session.Document() == state.before;
                owner._selectionOffsetTestGate.reset(); // Original payload retains its own gate.
            }
            else if (state.mode == Mode::Cancel)
            {
                owner.CancelHostContinuations();
                state.cancellationPending = state.receipt.Read().outcome == HostOutcome::Pending &&
                                            owner._selectionOffsetTicket && owner._historyWork.Busy();
            }
            else if (state.mode == Mode::Generation)
            {
                owner._session.EditForTest([](CanvasDocument& document) { document.resolution = 300; });
                state.mutationApplied = owner._session.Generation() != state.generation;
            }
            else if (state.mode == Mode::Epoch)
            {
                owner._session.history.Begin(L"Epoch only", owner._session.Document(), owner._session.ActiveLayerID());
                owner._session.history.End(owner._session.Document(), owner._session.ActiveLayerID());
                state.mutationApplied = owner._session.Generation() == state.generation &&
                                        owner._session.history.CurrentRevision() == state.revision;
            }
            else if (state.mode == Mode::Replacement)
            {
                const auto serial = owner._session.DocumentSerial();
                owner._session.CreateDocument(100, 100, true);
                state.mutationApplied = owner._session.DocumentSerial() != serial;
            }
            else if (state.mode == Mode::Crop)
            {
                owner._session.SetCropFrame(CropRect{0, 0, 25, 25});
                state.mutationApplied =
                    owner._session.CropFrame().has_value() && owner._session.Generation() == state.generation;
            }
            else if (state.mode == Mode::Active)
            {
                const auto prior = owner._session.ActiveLayerID();
                owner._session.SelectLayer(state.before->layers.front().id);
                state.mutationApplied = owner._session.ActiveLayerID() != prior;
            }
            else if (state.mode == Mode::Mask)
            {
                owner._session.SelectLayerTarget(owner._session.ActiveLayerID().value(), true);
                state.mutationApplied = owner._session.IsMaskSelected();
            }
            else if (state.mode == Mode::Lasso)
            {
                owner._session.BeginLasso({0, 0}, SelectionMode::New);
                state.mutationApplied = owner._session.PendingLasso().has_value();
            }
            else if (state.mode == Mode::Brush)
                state.mutationApplied = owner._session.BeginBrush({10, 10}) && owner._session.PendingBrush();
            else if (state.mode == Mode::ViewPump)
            {
                const double prior = owner._viewport.zoom;
                owner.OnCommand(ChromeHost::kCommandZoomIn);
                state.zoomPending =
                    owner._viewport.zoom != prior && state.receipt.Read().outcome == HostOutcome::Pending;
                (void)PostMessageW(owner._window.get(), kOffsetPumpMessage, 0, 0);
                return;
            }
            (void)SetEvent(gate->release.get());
            return;
        }
        if (message.message == kOffsetPumpMessage)
        {
            state.messagePending = state.receipt.Read().outcome == HostOutcome::Pending && owner._historyWork.Busy();
            (void)SetEvent(owner._selectionOffsetTestGate->release.get());
        }
        owner.DispatchQueuedMessage(message);
    };
    constexpr std::array modes{Mode::Success,        Mode::Ordered,        Mode::Cancel,
                               Mode::WorkerFailure,  Mode::CaptureFailure, Mode::SelectionSnapshot,
                               Mode::BeforeSnapshot, Mode::AfterSnapshot,  Mode::Entry,
                               Mode::StackGrowth,    Mode::TrimIndex,      Mode::Generation,
                               Mode::Epoch,          Mode::Replacement,    Mode::Crop,
                               Mode::Active,         Mode::Mask,           Mode::Lasso,
                               Mode::Brush,          Mode::ViewPump,       Mode::ResidencyRefresh,
                               Mode::ResidencyFault, Mode::ResidencyCancel};
    for (const Mode mode : modes)
    {
        _session.ClearDocument();
        _session.history = DocumentHistory{kHistoryEntryLimit, originalHistoryBudget};
        DocumentChanged();
        if (SettleJobs(_jobs, 15000, DispatchMessageThunk, this) != SettleResult::Settled)
            return 5;
        const auto cleanBaseline = _jobs.ReservedMemoryBytes();
        _session.CreateDocument(100, 100, true);
        _session.AddBlankLayer();
        if (mode == Mode::Mask)
        {
            _session.AddLayerMask();
            _session.SelectLayerTarget(_session.ActiveLayerID().value(), false);
        }
        const DocumentSelection partial{false, SelectionGeometry::Rect(0, 0, 50, 50)};
        _session.SetSelection(partial, L"Fixture selection");
        _session.history.Reset();
        _session.history.MarkSaved();
        Raster late;
        const bool residency =
            mode == Mode::ResidencyRefresh || mode == Mode::ResidencyFault || mode == Mode::ResidencyCancel;
        if (residency)
        {
            constexpr std::array<std::uint8_t, 4> pixel{51, 52, 53, 255};
            const Raster leaf = Raster::Make(1, 1, RasterFormat::Bgra8888, pixel.data(), pixel.size());
            late = Raster::MakeSparse(2, 1, RasterFormat::Bgra8888, {}, 0, 0, {{0, 0, leaf}}, 1);
            auto historical = DocumentFixture::Copy(_session.Document().value());
            historical.layers.front().asset = RasterAsset{late, {}, L"Literal sparse history"};
            // Keep the offset entry's original four-point selection, but require refresh to trim the grown cache.
            _session.history = DocumentHistory{100, partial.loops.StorageBytes() + leaf.ByteSize()};
            auto seed = DocumentHistory::PrepareEdit(_session.history.CaptureState(), std::move(historical).Seal(),
                                                     _session.ActiveLayerID(), _session.Document(),
                                                     _session.ActiveLayerID(), L"Literal historical cache");
            if (!expect(name(mode), "literal-four-byte-history",
                        seed && _session.history.Publish(*seed) &&
                            _session.history.RetainedBytes(_session.Document()) == 4 && !late.IsMaterialized()))
                return 5;
            _session.history.MarkSaved();
        }
        DocumentChanged();
        _viewport.zoom = 1;
        _viewport.followsFit = false;
        auto gate = std::make_shared<SelectionOffsetTestGate>();
        gate->release.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (!expect(name(mode), "event-created", static_cast<bool>(gate->release)))
            return 5;
        gate->throwCaptureFailure = mode == Mode::CaptureFailure;
        gate->observeOffsetOutputLifetime = mode == Mode::Success;
        gate->lateResidencySource = late;
        gate->throwResidencyRefresh = mode == Mode::ResidencyFault;
        gate->cancelResidencyRefresh = mode == Mode::ResidencyCancel;
        if (mode == Mode::WorkerFailure)
            gate->result = {JobStatus::Failed, E_ACCESSDENIED};
        switch (mode)
        {
        case Mode::SelectionSnapshot:
            gate->throwHistoryStage = DocumentHistory::PrepareStage::SelectionSnapshot;
            break;
        case Mode::BeforeSnapshot:
            gate->throwHistoryStage = DocumentHistory::PrepareStage::BeforeSnapshot;
            break;
        case Mode::AfterSnapshot:
            gate->throwHistoryStage = DocumentHistory::PrepareStage::AfterSnapshot;
            break;
        case Mode::Entry:
            gate->throwHistoryStage = DocumentHistory::PrepareStage::Entry;
            break;
        case Mode::StackGrowth:
            gate->throwHistoryStage = DocumentHistory::PrepareStage::StackGrowth;
            break;
        case Mode::TrimIndex:
            gate->throwHistoryStage = DocumentHistory::PrepareStage::TrimIndex;
            break;
        default:
            break;
        }
        _selectionOffsetTestGate = gate;
        State state{*this, mode, QueueHostInput(HostSelectionOffsetInput{amount})};
        state.before = _session.Document();
        state.revision = _session.history.CurrentRevision();
        state.generation = _session.Generation();
        const auto undoCount = _session.history.UndoCount();
        const auto redoCount = _session.history.CaptureState().FutureCount();
        const auto settled = SettleJobs(_jobs, 15000, pump, &state);
        const auto receipt = state.receipt.Read();
        const bool earlyFault = gate->throwCaptureFailure || gate->throwHistoryStage.has_value();
        (void)expect(name(mode), "settled", settled == SettleResult::Settled);
        (void)expect(name(mode), "held-under-original-reservation",
                     earlyFault ? !state.visited : state.visited && state.held);
        (void)expect(name(mode), "single-capture-and-worker-retirement",
                     gate->reservationHeld.load() && gate->captures.load() == (gate->throwCaptureFailure ? 0u : 1u) &&
                         gate->retired.load() && gate->retiredOffOwner.load());
        (void)expect(name(mode), "ticket-and-history-finished",
                     !_selectionOffsetTicket && !_selectionOffsetStamp && !_historyWork.Busy());
        const bool stale = mode >= Mode::Generation && mode <= Mode::Brush;
        const bool failed = mode == Mode::WorkerFailure || earlyFault || mode == Mode::ResidencyFault;
        const bool cancelled = mode == Mode::Cancel || mode == Mode::ResidencyCancel;
        if (stale)
            (void)expect(name(mode), "stale-refuses-publication",
                         state.mutationApplied && receipt.outcome == HostOutcome::Stale &&
                             (mode == Mode::Replacement || _session.Selection() == state.before->selection));
        else if (failed || cancelled)
        {
            const auto expected = cancelled ? HostOutcome::Cancelled : HostOutcome::Failed;
            const auto error = static_cast<std::int32_t>(mode == Mode::WorkerFailure ? E_ACCESSDENIED : E_OUTOFMEMORY);
            (void)expect(
                name(mode), "failure-or-cancel-preserves-authority",
                receipt.outcome == expected && (cancelled || receipt.error == error) &&
                    _session.Document() == state.before && _session.Generation() == state.generation &&
                    _session.history.CurrentRevision() == state.revision && _session.history.UndoCount() == undoCount &&
                    _session.history.CaptureState().FutureCount() == redoCount && !_session.history.IsModified());
        }
        else
        {
            const auto selected = _session.Selection();
            const Raster coverage =
                selected ? SelectionGeometry::Coverage(selected->loops.Get(), 100, 100, selected->antialiased)
                         : Raster{};
            const auto* bytes = coverage.Data();
            (void)expect(name(mode), "literal-offset-publication",
                         receipt.outcome == HostOutcome::Succeeded && bytes && bytes[25 * 100 + 25] == 255 &&
                             bytes[75 * 100 + 75] == 0 && selected &&
                             selected->Bounds() ==
                                 (amount > 0 ? SelectionBounds{0, 0, 55, 55} : SelectionBounds{5, 5, 45, 45}) &&
                             _session.history.UndoName() == (amount > 0 ? L"Expand Selection" : L"Contract Selection"));
        }
        if (gate->throwHistoryStage)
            (void)expect(name(mode), "allocation-stage-reached-once", gate->historyFaultHits.load() == 1);
        if (mode == Mode::Cancel)
            (void)expect(name(mode), "cancel-remains-pending-until-ack", state.cancellationPending);
        if (mode == Mode::Ordered)
            (void)expect(name(mode), "younger-deselect-and-undo-ordered",
                         state.youngerPending && state.younger && state.undo &&
                             state.younger->Read().outcome == HostOutcome::Succeeded &&
                             state.undo->Read().outcome == HostOutcome::Succeeded &&
                             _session.history.RedoName() == L"Deselect");
        if (mode == Mode::ViewPump)
            (void)expect(name(mode), "view-and-message-run-before-completion",
                         state.zoomPending && state.messagePending);
        if (residency)
            (void)expect(name(mode), "refresh-keeps-original-capture-ticket-and-charge",
                         gate->captures.load() == 1 && gate->visits.load() == 1 && gate->residentGrowths.load() == 1 &&
                             gate->residencyRefreshes.load() == 1 && gate->refreshReservationHeld.load() &&
                             late.ByteSize() == 8);
        if (mode == Mode::Success)
        {
            const auto* points = _chrome->SelectionOutlineForTest();
            (void)expect(name(mode), "prepared-point-buffer-adopted-without-owner-copy",
                         gate->offsetOutlinePreparedOffOwner.load() && points && !points->empty() &&
                             points->front().data() == gate->offsetPointBuffer.load());
            (void)expect(name(mode), "complete-output-bound-to-original-ticket",
                         gate->offsetPublishedOutputs == 1 && gate->offsetPublishedBytes != 0 &&
                             gate->offsetOutputBoundToOriginal &&
                             _jobs.ReservedMemoryBytes() == cleanBaseline + gate->offsetPublishedBytes);
        }
        if (mode == Mode::Lasso)
            _session.CancelLasso();
        if (mode == Mode::Brush)
            _session.CancelBrush();
        _selectionOffsetTestGate.reset();
        state.before.reset();
        _session.ClearDocument();
        DocumentChanged();
        const bool cleared = SettleJobs(_jobs, 15000, DispatchMessageThunk, this) == SettleResult::Settled;
        if (mode == Mode::Success)
        {
            (void)expect(name(mode), "copied-outline-retains-charge-after-document-history-close",
                         cleared && state.copiedOutline && !gate->offsetOutlineOwner.expired() &&
                             !gate->offsetMemoryOwner.expired() &&
                             _jobs.ReservedMemoryBytes() == cleanBaseline + gate->offsetPublishedBytes);
            state.copiedOutline.reset();
            (void)expect(name(mode), "outline-last-weak-control-retains-charge",
                         SettleJobs(_jobs, 15000, DispatchMessageThunk, this) == SettleResult::Settled &&
                             gate->offsetOutlineOwner.expired() && !gate->offsetMemoryOwner.expired() &&
                             _jobs.ReservedMemoryBytes() == cleanBaseline + gate->offsetPublishedBytes);
            gate->offsetOutlineOwner.reset();
            (void)expect(name(mode), "memory-last-weak-control-retains-charge",
                         SettleJobs(_jobs, 15000, DispatchMessageThunk, this) == SettleResult::Settled &&
                             gate->offsetMemoryOwner.expired() &&
                             _jobs.ReservedMemoryBytes() == cleanBaseline + gate->offsetPublishedBytes);
            gate->offsetMemoryOwner.reset();
        }
        (void)expect(name(mode), "final-owner-refund-to-actual-baseline",
                     SettleJobs(_jobs, 15000, DispatchMessageThunk, this) == SettleResult::Settled &&
                         _jobs.ReservedMemoryBytes() == cleanBaseline);
    }
    // This fragment runs after the prior mode cleared its document. Build and settle its own literal fixture so
    // neither the per-mode cleanBaseline nor state from the last mode is an implicit dependency.
    _selectionOffsetTestGate.reset();
    _session.ClearDocument();
    _session.history = DocumentHistory{kHistoryEntryLimit, originalHistoryBudget};
    _session.CreateDocument(100, 100, true);
    _session.AddBlankLayer();
    _session.SetSelection(DocumentSelection{false, SelectionGeometry::Rect(0, 0, 50, 50)}, L"Admission fixture");
    _session.history.Reset();
    _session.history.MarkSaved();
    DocumentChanged();
    if (SettleJobs(_jobs, 15000, DispatchMessageThunk, this) != SettleResult::Settled || !_session.Document() ||
        _session.history.IsModified() || _session.history.UndoCount() != 0)
        return 5;
    const auto cleanAdmissionBaseline = _jobs.ReservedMemoryBytes();

    // Fill the actual fixed scheduler table with event-blocked jobs. The host's UI source remains runnable;
    // offset admission reaches JobScheduler::Submit and must refuse before owner capture/publication.
    class OffsetAdmissionOccupier final : public Job
    {
      public:
        explicit OffsetAdmissionOccupier(HANDLE release) noexcept : _release(release) {}
        JobResult Run(JobContext& context) override
        {
            const HANDLE handles[]{_release, static_cast<HANDLE>(context.shutdownEvent)};
            const DWORD waited = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
            return waited == WAIT_OBJECT_0 ? JobResult{} : JobResult{JobStatus::Cancelled};
        }

        void Completed(JobResult) noexcept override {}

      private:
        HANDLE _release;
    };
    wil::unique_handle admissionRelease{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    if (!admissionRelease)
        return 5;
    const auto drainAdmission = wil::scope_exit(
        [&]() noexcept
        {
            // Every occupier borrows this event. Join it even when a fixture assertion fails.
            (void)SetEvent(admissionRelease.get());
            if (SettleJobs(_jobs, 15000, DispatchMessageThunk, this) != SettleResult::Settled)
                std::_Exit(5);
        });
    std::array<JobTicket, JobScheduler::kCapacity> admissionTickets{};
    for (std::size_t index = 0; index < admissionTickets.size(); ++index)
    {
        std::unique_ptr<Job> occupier = std::make_unique<OffsetAdmissionOccupier>(admissionRelease.get());
        if (FAILED(static_cast<HRESULT>(_jobs.Submit(occupier, {JobClass::Background}, admissionTickets[index]))) ||
            occupier)
        {
            (void)SetEvent(admissionRelease.get());
            (void)SettleJobs(_jobs, 15000, DispatchMessageThunk, this);
            return 5;
        }
    }
    auto admissionDocument = _session.Document();
    const auto admissionRevision = _session.history.CurrentRevision();
    const auto admissionGeneration = _session.Generation();
    const auto admissionUndoCount = _session.history.UndoCount();
    const auto admissionRedoCount = _session.history.CaptureState().FutureCount();
    const auto admissionBytes = _jobs.ReservedMemoryBytes();
    auto admissionGate = std::make_shared<SelectionOffsetTestGate>();
    _selectionOffsetTestGate = admissionGate;
    const auto refusedInverse = QueueHostInput(HostSelectionOffsetInput{amount});
    // Match the native host test: another pending source may consume an action pass. Keep all 128 records
    // occupied and give the ordered receipt up to four real owner-action passes to reach offset admission.
    for (unsigned int pass = 0; pass < 4 && refusedInverse.Read().outcome == HostOutcome::Pending; ++pass)
        (void)RunUiActions(_jobs);
    const auto refusalResult = refusedInverse.Read();
    const bool inverseRefusedAtomically =
        refusalResult.outcome == HostOutcome::Failed &&
        refusalResult.error == static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA)) &&
        admissionGate->captures.load() == 0 && !_selectionOffsetTicket && !_selectionOffsetStamp &&
        !_historyWork.Busy() && _session.Document() == admissionDocument &&
        _session.Generation() == admissionGeneration && _session.history.CurrentRevision() == admissionRevision &&
        _session.history.UndoCount() == admissionUndoCount &&
        _session.history.CaptureState().FutureCount() == admissionRedoCount && !_session.history.IsModified() &&
        _jobs.ReservedMemoryBytes() == admissionBytes && _hostRequests.Size() == 0 && !_activeHostRequest;
    _selectionOffsetTestGate.reset();
    admissionDocument.reset();
    (void)expect("admission", "full-record-refusal-is-atomic-no-capture-or-barrier-leak", inverseRefusedAtomically);
    (void)SetEvent(admissionRelease.get());
    if (SettleJobs(_jobs, 15000, DispatchMessageThunk, this) != SettleResult::Settled || !_jobs.IsIdle() ||
        _jobs.ReservedMemoryBytes() != cleanAdmissionBaseline)
        return 5;

    const auto recoveredInverse = QueueHostInput(HostSelectionOffsetInput{amount});
    const bool recoverySettled = SettleJobs(_jobs, 15000, DispatchMessageThunk, this) == SettleResult::Settled;
    bool recoveredAndSettled = false;
    {
        auto recoveredSelection = _session.Selection();
        Raster recoveredCoverage = recoveredSelection
                                       ? SelectionGeometry::Coverage(recoveredSelection->loops.Get(), 100, 100,
                                                                     recoveredSelection->antialiased)
                                       : Raster{};
        const auto* recoveredPixels = recoveredCoverage.Data();
        recoveredAndSettled =
            recoverySettled && recoveredInverse.Read().outcome == HostOutcome::Succeeded && !_selectionOffsetTicket &&
            !_selectionOffsetStamp && !_historyWork.Busy() &&
            _session.history.UndoName() == (amount > 0 ? L"Expand Selection" : L"Contract Selection") &&
            recoveredPixels && recoveredPixels[25 * 100 + 25] == 255 && recoveredPixels[75 * 100 + 75] == 0 &&
            recoveredSelection &&
            recoveredSelection->Bounds() ==
                (amount > 0 ? SelectionBounds{0, 0, 55, 55} : SelectionBounds{5, 5, 45, 45});
    }
    (void)expect("admission", "fresh-offset-after-drain-succeeds-with-literal-coverage", recoveredAndSettled);
    _session.ClearDocument();
    DocumentChanged();
    const bool refunded = SettleJobs(_jobs, 15000, DispatchMessageThunk, this) == SettleResult::Settled &&
                          _jobs.ReservedMemoryBytes() == cleanAdmissionBaseline;
    (void)expect("admission", "successful-offset-output-refunds-to-exact-pre-admission-charge", refunded);
    return failures == 0 ? 0 : 5;
}

int Application::RunSelectionOffsetFailureSelfTest() noexcept
{
    if (RunSelectionOffsetFailureForAmountSelfTest(5) != 0)
        return 5;
    return RunSelectionOffsetFailureForAmountSelfTest(-5);
}
