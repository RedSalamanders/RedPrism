// Private real-caller controls. Expected state follows the ordered-host contract, not pixel implementation.
#include "Application.h"

#include "Tests/Support/Support.Tests.DocumentFixture.h"

#include "ChromeHost.h"
#include "Runtime.Tests.SelectionCommandTestGate.h"
#include "Selection.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <vector>

int Application::RunInverseSelectionFailureSelfTest() noexcept
{
    const auto originalViewport = _viewport;
    const auto originalHistoryBudget = _session.history.RetainedByteLimit();
    const auto restore = wil::scope_exit(
        [this, originalViewport, originalHistoryBudget]() noexcept
        {
            _inverseSelectionTestGate.reset();
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
    constexpr UINT kInversePumpMessage = WM_APP + 0x4ce;
// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4623 4626 5027)
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
#pragma warning(pop)

    int failures = 0;
    const auto expect = [&failures](const char* mode, const char* check, bool condition) noexcept
    {
        std::fprintf(stderr, "%s inverse-selection-host.%s.%s\n", condition ? "PASS" : "FAIL", mode, check);
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
        if (message.message == SelectionCommandTestGate::kMessage)
        {
            const auto gate = owner._inverseSelectionTestGate;
            if (!gate)
                return;
            state.visited = true;
            state.held = owner._inverseSelectionTicket && owner._inverseSelectionStamp && owner._historyWork.Busy() &&
                         state.receipt.Read().outcome == HostOutcome::Pending &&
                         owner._jobs.ReservedMemoryBytes() >= gate->reservationBytes && gate->captures.load() == 1 &&
                         owner._session.Document() == state.before &&
                         owner._session.history.CurrentRevision() == state.revision;
            if (state.mode == Mode::Success)
                state.copiedOutline = gate->inverseOutlineOwner.lock();
            if (state.mode == Mode::Ordered)
            {
                state.younger = owner.QueueHostInput(HostCommandInput{ChromeHost::kCommandDeselect});
                state.undo = owner.QueueHostInput(HostCommandInput{ChromeHost::kCommandUndo});
                state.youngerPending = state.younger->Read().outcome == HostOutcome::Pending &&
                                       state.undo->Read().outcome == HostOutcome::Pending &&
                                       state.receipt.Read().outcome == HostOutcome::Pending &&
                                       owner._session.Document() == state.before;
                owner._inverseSelectionTestGate.reset(); // Original payload retains its own gate.
            }
            else if (state.mode == Mode::Cancel)
            {
                owner.CancelHostContinuations();
                state.cancellationPending = state.receipt.Read().outcome == HostOutcome::Pending &&
                                            owner._inverseSelectionTicket && owner._historyWork.Busy();
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
                (void)PostMessageW(owner._window.get(), kInversePumpMessage, 0, 0);
                return;
            }
            (void)SetEvent(gate->release.get());
            return;
        }
        if (message.message == kInversePumpMessage)
        {
            state.messagePending = state.receipt.Read().outcome == HostOutcome::Pending && owner._historyWork.Busy();
            (void)SetEvent(owner._inverseSelectionTestGate->release.get());
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
            // Keep the Inverse entry's original four-point selection, but require refresh to trim the grown cache.
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
        auto gate = std::make_shared<SelectionCommandTestGate>();
        gate->release.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (!expect(name(mode), "event-created", static_cast<bool>(gate->release)))
            return 5;
        gate->throwCaptureFailure = mode == Mode::CaptureFailure;
        gate->observeInverseOutputLifetime = mode == Mode::Success;
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
        case Mode::Active:
        case Mode::Brush:
        case Mode::Cancel:
        case Mode::CaptureFailure:
        case Mode::Crop:
        case Mode::Epoch:
        case Mode::Generation:
        case Mode::Lasso:
        case Mode::Mask:
        case Mode::Ordered:
        case Mode::Replacement:
        case Mode::ResidencyCancel:
        case Mode::ResidencyFault:
        case Mode::ResidencyRefresh:
        case Mode::Success:
        case Mode::ViewPump:
        case Mode::WorkerFailure:
        default:
            break;
        }
        _inverseSelectionTestGate = gate;
        State state{*this, mode, QueueHostInput(HostCommandInput{ChromeHost::kCommandInverse})};
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
                     !_inverseSelectionTicket && !_inverseSelectionStamp && !_historyWork.Busy());
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
            (void)expect(name(mode), "literal-inverse-publication",
                         receipt.outcome == HostOutcome::Succeeded && bytes && bytes[25 * 100 + 25] == 0 &&
                             bytes[75 * 100 + 75] == 255 && _session.history.UndoName() == L"Inverse");
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
                         gate->inverseOutlinePreparedOffOwner.load() && points && !points->empty() &&
                             points->front().data() == gate->inversePointBuffer.load());
            (void)expect(name(mode), "complete-output-bound-to-original-ticket",
                         gate->inversePublishedOutputs == 1 && gate->inversePublishedBytes != 0 &&
                             gate->inverseOutputBoundToOriginal &&
                             _jobs.ReservedMemoryBytes() == cleanBaseline + gate->inversePublishedBytes);
        }
        if (mode == Mode::Lasso)
            _session.CancelLasso();
        if (mode == Mode::Brush)
            _session.CancelBrush();
        _inverseSelectionTestGate.reset();
        state.before.reset();
        _session.ClearDocument();
        DocumentChanged();
        const bool cleared = SettleJobs(_jobs, 15000, DispatchMessageThunk, this) == SettleResult::Settled;
        if (mode == Mode::Success)
        {
            (void)expect(name(mode), "copied-outline-retains-charge-after-document-history-close",
                         cleared && state.copiedOutline && !gate->inverseOutlineOwner.expired() &&
                             !gate->inverseMemoryOwner.expired() &&
                             _jobs.ReservedMemoryBytes() == cleanBaseline + gate->inversePublishedBytes);
            state.copiedOutline.reset();
            (void)expect(name(mode), "outline-last-weak-control-retains-charge",
                         SettleJobs(_jobs, 15000, DispatchMessageThunk, this) == SettleResult::Settled &&
                             gate->inverseOutlineOwner.expired() && !gate->inverseMemoryOwner.expired() &&
                             _jobs.ReservedMemoryBytes() == cleanBaseline + gate->inversePublishedBytes);
            gate->inverseOutlineOwner.reset();
            (void)expect(name(mode), "memory-last-weak-control-retains-charge",
                         SettleJobs(_jobs, 15000, DispatchMessageThunk, this) == SettleResult::Settled &&
                             gate->inverseMemoryOwner.expired() &&
                             _jobs.ReservedMemoryBytes() == cleanBaseline + gate->inversePublishedBytes);
            gate->inverseMemoryOwner.reset();
        }
        (void)expect(name(mode), "final-owner-refund-to-actual-baseline",
                     SettleJobs(_jobs, 15000, DispatchMessageThunk, this) == SettleResult::Settled &&
                         _jobs.ReservedMemoryBytes() == cleanBaseline);
    }
    // This fragment runs after the prior mode cleared its document. Build and settle its own literal fixture so
    // neither the per-mode cleanBaseline nor state from the last mode is an implicit dependency.
    _inverseSelectionTestGate.reset();
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
    // Inverse admission reaches JobScheduler::Submit and must refuse before owner capture/publication.
    class InverseAdmissionOccupier final : public Job
    {
      public:
        explicit InverseAdmissionOccupier(HANDLE release) noexcept : _release(release) {}
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
        std::unique_ptr<Job> occupier = std::make_unique<InverseAdmissionOccupier>(admissionRelease.get());
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
    auto admissionGate = std::make_shared<SelectionCommandTestGate>();
    _inverseSelectionTestGate = admissionGate;
    const auto refusedInverse = QueueHostInput(HostCommandInput{ChromeHost::kCommandInverse});
    // Match the native host test: another pending source may consume an action pass. Keep all 128 records
    // occupied and give the ordered receipt up to four real owner-action passes to reach Inverse admission.
    for (unsigned int pass = 0; pass < 4 && refusedInverse.Read().outcome == HostOutcome::Pending; ++pass)
        (void)RunUiActions(_jobs);
    const auto refusalResult = refusedInverse.Read();
    const bool inverseRefusedAtomically =
        refusalResult.outcome == HostOutcome::Failed &&
        refusalResult.error == static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA)) &&
        admissionGate->captures.load() == 0 && !_inverseSelectionTicket && !_inverseSelectionStamp &&
        !_historyWork.Busy() && _session.Document() == admissionDocument &&
        _session.Generation() == admissionGeneration && _session.history.CurrentRevision() == admissionRevision &&
        _session.history.UndoCount() == admissionUndoCount &&
        _session.history.CaptureState().FutureCount() == admissionRedoCount && !_session.history.IsModified() &&
        _jobs.ReservedMemoryBytes() == admissionBytes && _hostRequests.Size() == 0 && !_activeHostRequest;
    _inverseSelectionTestGate.reset();
    admissionDocument.reset();
    (void)expect("admission", "full-record-refusal-is-atomic-no-capture-or-barrier-leak", inverseRefusedAtomically);
    (void)SetEvent(admissionRelease.get());
    if (SettleJobs(_jobs, 15000, DispatchMessageThunk, this) != SettleResult::Settled || !_jobs.IsIdle() ||
        _jobs.ReservedMemoryBytes() != cleanAdmissionBaseline)
        return 5;

    const auto recoveredInverse = QueueHostInput(HostCommandInput{ChromeHost::kCommandInverse});
    const bool recoverySettled = SettleJobs(_jobs, 15000, DispatchMessageThunk, this) == SettleResult::Settled;
    bool recoveredAndSettled = false;
    {
        auto recoveredSelection = _session.Selection();
        Raster recoveredCoverage = recoveredSelection
                                       ? SelectionGeometry::Coverage(recoveredSelection->loops.Get(), 100, 100,
                                                                     recoveredSelection->antialiased)
                                       : Raster{};
        const auto* recoveredPixels = recoveredCoverage.Data();
        recoveredAndSettled = recoverySettled && recoveredInverse.Read().outcome == HostOutcome::Succeeded &&
                              !_inverseSelectionTicket && !_inverseSelectionStamp && !_historyWork.Busy() &&
                              _session.history.UndoName() == L"Inverse" && recoveredPixels &&
                              recoveredPixels[25 * 100 + 25] == 0 && recoveredPixels[75 * 100 + 75] == 255;
    }
    (void)expect("admission", "fresh-inverse-after-drain-succeeds-with-literal-coverage", recoveredAndSettled);
    _session.ClearDocument();
    DocumentChanged();
    const bool refunded = SettleJobs(_jobs, 15000, DispatchMessageThunk, this) == SettleResult::Settled &&
                          _jobs.ReservedMemoryBytes() == cleanAdmissionBaseline;
    (void)expect("admission", "successful-inverse-output-refunds-to-exact-pre-admission-charge", refunded);
    // Independent lattice oracle: a 1024x448 rectangle minus 28672 disjoint unit-square holes.
    // Exact NonZero area is 458752-28672=430080, with 28673 closed loops and 114692 vertices.
    {
        _inverseSelectionTestGate.reset();
        _session.CreateDocument(1024, 448, true);
        std::vector<SelectionLoop> holes;
        holes.reserve(28672);
        for (int row = 0; row < 112; ++row)
            for (int column = 0; column < 256; ++column)
            {
                const double x = 4.0 * column + 1.0, y = 4.0 * row + 1.0;
                holes.push_back({{x, y}, {x + 1, y}, {x + 1, y + 1}, {x, y + 1}});
            }
        _session.SetSelection(DocumentSelection{false, std::move(holes)}, L"Lattice fixture");
        _session.history.Reset();
        _session.history.MarkSaved();
        DocumentChanged();
        if (SettleJobs(_jobs, 15000, DispatchMessageThunk, this) != SettleResult::Settled)
            return 5;
        const auto largeBaseline = _jobs.ReservedMemoryBytes();
        auto largeGate = std::make_shared<SelectionCommandTestGate>();
        largeGate->release.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (!largeGate->release)
            return 5;
        largeGate->hold = true;
        largeGate->observeInverseOutputLifetime = true;
        _inverseSelectionTestGate = largeGate;
// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4623 4626 5027)
        struct Large final
        {
            Application& owner;
            std::shared_ptr<const void> outline;
            bool held = false;
        }
#pragma warning(pop)
        large{*this};
        const auto largePump = [](void* raw, void* native) noexcept
        {
            auto& state = *static_cast<Large*>(raw);
            auto& message = *static_cast<MSG*>(native);
            if (message.message == SelectionCommandTestGate::kMessage)
            {
                const auto gate = state.owner._inverseSelectionTestGate;
                state.outline = gate->inverseOutlineOwner.lock();
                state.held = state.outline && gate->captures.load() == 1 && gate->reservationHeld.load() &&
                             state.owner._inverseSelectionTicket && state.owner._historyWork.Busy();
                (void)SetEvent(gate->release.get());
            }
            else
                state.owner.DispatchQueuedMessage(message);
        };
        const auto receipt = QueueHostInput(HostCommandInput{ChromeHost::kCommandInverse});
        const bool settled = SettleJobs(_jobs, 15000, largePump, &large) == SettleResult::Settled;
        std::fprintf(
            stderr,
            "INFO inverse-large settled=%d outcome=%u error=%08X held=%d captures=%u visits=%u published=%u bytes=%llu "
            "bound=%d reserved=%llu baseline=%llu\n",
            settled, static_cast<unsigned int>(receipt.Read().outcome), static_cast<unsigned int>(receipt.Read().error),
            large.held, largeGate->captures.load(), largeGate->visits.load(), largeGate->inversePublishedOutputs,
            static_cast<unsigned long long>(largeGate->inversePublishedBytes), largeGate->inverseOutputBoundToOriginal,
            static_cast<unsigned long long>(_jobs.ReservedMemoryBytes()),
            static_cast<unsigned long long>(largeBaseline));
        bool literal = false;
        if (const auto selection = _session.Selection())
        {
            double twiceArea = 0;
            for (const auto& loop : selection->loops.Get())
                for (std::size_t index = 0; index < loop.size(); ++index)
                {
                    const auto& a = loop[index];
                    const auto& b = loop[(index + 1) % loop.size()];
                    twiceArea += a.x * b.y - b.x * a.y;
                }
            literal = selection->loops.Get().size() == 28673 && selection->loops.PointCount() == 114692 &&
                      std::abs(twiceArea) == 860160 && !selection->antialiased;
        }
        const auto* displayed = _chrome->SelectionOutlineForTest();
        (void)expect("large-lattice", "literal-topology-area-and-worker-buffer-publication",
                     settled && large.held && literal && receipt.Read().outcome == HostOutcome::Succeeded &&
                         displayed && !displayed->empty() &&
                         displayed->front().data() == largeGate->inversePointBuffer.load() &&
                         largeGate->inverseOutlinePreparedOffOwner.load() && largeGate->retiredOffOwner.load() &&
                         _session.history.UndoName() == L"Inverse");
        (void)expect("large-lattice", "complete-large-output-charge-on-original-ticket",
                     largeGate->inversePublishedOutputs == 1 && largeGate->inverseOutputBoundToOriginal &&
                         largeGate->inversePublishedBytes > kDeferredReleaseThreshold &&
                         _jobs.ReservedMemoryBytes() == largeBaseline + largeGate->inversePublishedBytes);
        _inverseSelectionTestGate.reset();
        _session.ClearDocument();
        DocumentChanged();
        (void)expect("large-lattice", "copied-large-reader-keeps-complete-charge-after-clear",
                     SettleJobs(_jobs, 15000, DispatchMessageThunk, this) == SettleResult::Settled && large.outline &&
                         _jobs.ReservedMemoryBytes() == largeBaseline + largeGate->inversePublishedBytes);
        large.outline.reset();
        (void)expect("large-lattice", "last-large-outline-weak-control-keeps-charge",
                     SettleJobs(_jobs, 15000, DispatchMessageThunk, this) == SettleResult::Settled &&
                         largeGate->inverseOutlineOwner.expired() && !largeGate->inverseMemoryOwner.expired() &&
                         _jobs.ReservedMemoryBytes() == largeBaseline + largeGate->inversePublishedBytes);
        largeGate->inverseOutlineOwner.reset();
        (void)expect("large-lattice", "last-large-memory-weak-control-keeps-charge",
                     SettleJobs(_jobs, 15000, DispatchMessageThunk, this) == SettleResult::Settled &&
                         largeGate->inverseMemoryOwner.expired() &&
                         _jobs.ReservedMemoryBytes() == largeBaseline + largeGate->inversePublishedBytes);
        largeGate->inverseMemoryOwner.reset();
        (void)expect("large-lattice", "last-large-owner-refunds-exactly-once",
                     SettleJobs(_jobs, 15000, DispatchMessageThunk, this) == SettleResult::Settled &&
                         _jobs.ReservedMemoryBytes() == largeBaseline && !_inverseSelectionTicket &&
                         !_inverseSelectionStamp && !_historyWork.Busy());
    }

    return failures == 0 ? 0 : 5;
}
