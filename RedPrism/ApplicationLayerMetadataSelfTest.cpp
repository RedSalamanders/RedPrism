#include "Application.h"

#include "../Tests/Support/DocumentFixture.h"

#include "ChromeHost.h"
#include "LayerMetadataTestGate.h"

#include <array>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace
{
class OccupyMetadataSlot final : public Job
{
  public:
    JobResult Run(JobContext&) override
    {
        return {};
    }
    void Completed(JobResult) noexcept override {}
};
} // namespace

int Application::RunLayerMetadataSelfTest() noexcept
{
    enum class Mode
    {
        Rename,
        Inactive,
        Visibility,
        Opacity,
        Blend,
        NoOp,
        Ordered,
        ViewPump,
        Cancel,
        Failure,
        CaptureFault,
        StageFault,
        Generation,
        Epoch,
        Active,
        ResidencyRefresh,
        ResidencyRefreshFault,
        ResidencyRefreshCancel,
    };
    struct Case final
    {
        const char* name;
        Mode mode;
        std::optional<LayerMetadataPrepareStage> metadata;
        std::optional<DocumentHistory::PrepareStage> history;
    };
    constexpr std::array cases{
        Case{"rename", Mode::Rename, {}, {}},
        Case{"saved-target", Mode::Inactive, {}, {}},
        Case{"visibility", Mode::Visibility, {}, {}},
        Case{"opacity", Mode::Opacity, {}, {}},
        Case{"blend", Mode::Blend, {}, {}},
        Case{"noop-redo", Mode::NoOp, {}, {}},
        Case{"ordered", Mode::Ordered, {}, {}},
        Case{"view-pump", Mode::ViewPump, {}, {}},
        Case{"cancel", Mode::Cancel, {}, {}},
        Case{"worker-failure", Mode::Failure, {}, {}},
        Case{"capture-fault", Mode::CaptureFault, {}, {}},
        Case{"draft-fault", Mode::StageFault, LayerMetadataPrepareStage::BeforeDraft, {}},
        Case{"record-fault", Mode::StageFault, LayerMetadataPrepareStage::AfterEdit, {}},
        Case{"seal-fault", Mode::StageFault, LayerMetadataPrepareStage::AfterSeal, {}},
        Case{"before-history-fault", Mode::StageFault, {}, DocumentHistory::PrepareStage::BeforeSnapshot},
        Case{"after-history-fault", Mode::StageFault, {}, DocumentHistory::PrepareStage::AfterSnapshot},
        Case{"entry-fault", Mode::StageFault, {}, DocumentHistory::PrepareStage::Entry},
        Case{"stack-fault", Mode::StageFault, {}, DocumentHistory::PrepareStage::StackGrowth},
        Case{"trim-fault", Mode::StageFault, {}, DocumentHistory::PrepareStage::TrimIndex},
        Case{"generation", Mode::Generation, {}, {}},
        Case{"epoch", Mode::Epoch, {}, {}},
        Case{"active-target", Mode::Active, {}, {}},
        Case{"residency-refresh", Mode::ResidencyRefresh, {}, {}},
        Case{"residency-refresh-fault", Mode::ResidencyRefreshFault, {}, {}},
        Case{"residency-refresh-cancel", Mode::ResidencyRefreshCancel, {}, {}},
    };
    constexpr UINT kPumpMessage = WM_APP + 0x5c1;
    struct State final
    {
        Application& owner;
        Mode mode;
        ContinuationReceipt receipt;
        std::optional<ContinuationReceipt> younger, undo;
        std::optional<CanvasDocument> before;
        Guid revision;
        Guid target;
        Guid other;
        std::uint64_t generation = 0;
        bool visited = false, held = false, viewPending = false, cancelPending = false;
        int originalUndoCount = 0;
    };
    int checks = 0;
    const auto expect = [&checks](const char* mode, const char* check, bool condition) noexcept
    {
        std::fprintf(stderr, "%s metadata-host.%s.%s\n", condition ? "PASS" : "FAIL", mode, check);
        checks += condition ? 1 : 0;
        return condition;
    };
    const auto originalViewport = _viewport;
    const auto originalSmearMode = _session.smearMode;
    const auto cleanup = wil::scope_exit(
        [this, originalViewport, originalSmearMode]
        {
            if (_layerMetadataTestGate)
                (void)SetEvent(_layerMetadataTestGate->release.get());
            _layerMetadataTestGate.reset();
            _viewport = originalViewport;
            _session.smearMode = originalSmearMode;
        });
    const auto prepare = [this]()
    {
        _session.ClearDocument();
        _session.CreateDocument(4, 3, true);
        _session.AddBlankLayer();
        _session.history.Reset();
        _session.history.MarkSaved();
        _opacityEdit.reset();
        _opacityRecorded.reset();
        DocumentChanged();
        _viewport.zoom = 1;
        _viewport.followsFit = false;
    };
    const auto rename = [this](Guid target, std::wstring_view name)
    {
        auto input = HostRequest::TryCopyRename(target, name, _hostInputBudget);
        return input ? QueueHostRequest(std::move(input.value())) : ContinuationReceipt::Failure(E_OUTOFMEMORY);
    };
    const auto pump = [](void* opaque, void* native) noexcept
    {
        auto& state = *static_cast<State*>(opaque);
        auto& owner = state.owner;
        auto& message = *static_cast<MSG*>(native);
        if (message.message == LayerMetadataTestGate::kMessage)
        {
            const auto gate = owner._layerMetadataTestGate;
            state.visited = true;
            state.held = gate && gate->captures.load() == 1 && gate->reservationHeld.load() &&
                         owner._layerMetadataTicket && owner._layerMetadataStamp && owner._historyWork.Busy() &&
                         owner._jobs.ReservedMemoryBytes() >= gate->reservationBytes &&
                         owner._session.Document() == state.before &&
                         owner._session.history.CurrentRevision() == state.revision &&
                         state.receipt.Read().outcome == HostOutcome::Pending;
            if (state.mode == Mode::Ordered)
            {
                state.younger = owner.QueueHostInput(
                    HostLayerOpacityInput{owner._session.ActiveLayerID().value(), 0.625, ChromeEditPhase::Commit});
                state.undo = owner.QueueHostInput(HostCommandInput{ChromeHost::kCommandUndo});
                owner._layerMetadataTestGate.reset(); // only the first operation is held
            }
            else if (state.mode == Mode::ViewPump)
            {
                const double zoom = owner._viewport.zoom;
                owner.OnCommand(ChromeHost::kCommandZoomIn);
                state.viewPending =
                    owner._viewport.zoom != zoom && state.receipt.Read().outcome == HostOutcome::Pending;
                (void)PostMessageW(owner._window.get(), kPumpMessage, 0, 0);
                return;
            }
            else if (state.mode == Mode::Cancel)
            {
                owner.CancelHostContinuations();
                state.cancelPending = state.receipt.Read().outcome == HostOutcome::Pending && owner._historyWork.Busy();
            }
            else if (state.mode == Mode::Generation)
            {
                owner._session.EditForTest([](CanvasDocument& document) { document.resolution = 300; });
                state.before = owner._session.Document();
                state.generation = owner._session.Generation();
            }
            else if (state.mode == Mode::Epoch)
            {
                owner._session.history.Begin(L"Literal epoch change", owner._session.Document(),
                                             owner._session.ActiveLayerID());
                owner._session.history.End(owner._session.Document(), owner._session.ActiveLayerID());
            }
            else if (state.mode == Mode::Active)
                owner._session.SelectLayer(state.other);
            (void)SetEvent(gate->release.get());
            return;
        }
        if (message.hwnd && message.message == kPumpMessage)
        {
            state.viewPending =
                state.viewPending && state.receipt.Read().outcome == HostOutcome::Pending && owner._historyWork.Busy();
            (void)SetEvent(owner._layerMetadataTestGate->release.get());
        }
        owner.DispatchQueuedMessage(message);
    };
    for (const auto& test : cases)
    {
        prepare();
        const Guid active = _session.ActiveLayerID().value();
        const Guid other = _session.Document().value().layers.front().id;
        const Guid target = test.mode == Mode::Inactive || test.mode == Mode::Ordered ? other : active;
        if (test.mode == Mode::NoOp)
        {
            _session.RenameLayer(target, L"Literal redo source");
            _session.Undo();
        }
        const auto gate = std::make_shared<LayerMetadataTestGate>();
        gate->release.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (!gate->release)
            return 5;
        gate->throwCaptureFailure = test.mode == Mode::CaptureFault;
        gate->throwMetadataStage = test.metadata;
        gate->throwHistoryStage = test.history;
        if (test.mode == Mode::Failure)
            gate->result = {JobStatus::Failed, E_ACCESSDENIED};
        _layerMetadataTestGate = gate;
        const bool residency = test.mode == Mode::ResidencyRefresh || test.mode == Mode::ResidencyRefreshFault ||
                               test.mode == Mode::ResidencyRefreshCancel;
        if (residency)
        {
            constexpr std::array<std::uint8_t, 4> leafBytes{61, 62, 63, 255};
            const Raster leaf = Raster::Make(1, 1, RasterFormat::Bgra8888, leafBytes.data(), leafBytes.size());
            gate->lateResidencySource = Raster::MakeSparse(2, 1, RasterFormat::Bgra8888, {}, 0, 0, {{0, 0, leaf}}, 1);
            auto historical = DocumentFixture::Copy(_session.Document().value());
            historical.layers.front().asset = RasterAsset{gate->lateResidencySource, {}, L"Literal sparse history"};
            const auto source = std::optional<CanvasDocument>{std::move(historical).Seal()};
            _session.history = DocumentHistory{100, 8};
            auto baseline = DocumentHistory::PrepareEdit(_session.history.CaptureState(), source, active,
                                                         _session.Document(), active, L"Literal cache baseline");
            if (!expect(test.name, "literal-four-byte-history-baseline",
                        baseline && _session.history.Publish(baseline.value()) &&
                            _session.history.RetainedBytes(_session.Document()) == 4 &&
                            !gate->lateResidencySource.IsMaterialized()))
                return 5;
            _session.history.MarkSaved();
            gate->throwResidencyRefresh = test.mode == Mode::ResidencyRefreshFault;
            gate->cancelResidencyRefresh = test.mode == Mode::ResidencyRefreshCancel;
        }
        const auto before = _session.Document();
        State state{*this,
                    test.mode,
                    {},
                    {},
                    {},
                    before,
                    _session.history.CurrentRevision(),
                    target,
                    other,
                    _session.Generation()};
        state.originalUndoCount = _session.history.UndoCount();
        if (test.mode == Mode::Visibility || test.mode == Mode::Blend)
            state.receipt = QueueHostInput(HostCommandInput{test.mode == Mode::Visibility
                                                                ? ChromeHost::kCommandToggleLayerVisibility
                                                                : ChromeHost::kCommandBlendNext});
        else if (test.mode == Mode::Opacity)
            state.receipt = QueueHostInput(HostLayerOpacityInput{active, 0.375, ChromeEditPhase::Commit});
        else
            state.receipt = rename(target, test.mode == Mode::NoOp ? _session.ActiveLayer()->name : L"Worker layer");
        if (!expect(test.name, "admitted-pending", state.receipt.Read().outcome == HostOutcome::Pending) ||
            SettleJobs(_jobs, 15000, pump, &state) != SettleResult::Settled)
            return 5;
        const bool fault = test.mode == Mode::CaptureFault || test.mode == Mode::StageFault;
        const bool stale = test.mode == Mode::Generation || test.mode == Mode::Epoch || test.mode == Mode::Active;
        const auto outcome = state.receipt.Read();
        const auto expected = fault || test.mode == Mode::Failure || test.mode == Mode::ResidencyRefreshFault
                                  ? HostOutcome::Failed
                              : test.mode == Mode::ResidencyRefreshCancel ? HostOutcome::Cancelled
                              : stale                                     ? HostOutcome::Stale
                              : test.mode == Mode::Cancel                 ? HostOutcome::Cancelled
                                                                          : HostOutcome::Succeeded;
        if (!expect(test.name, "acknowledged-outcome", outcome.outcome == expected) ||
            !expect(test.name, "complete-retirement",
                    gate->retired.load() && gate->retiredOffOwner.load() && _jobs.ReservedMemoryBytes() == 0 &&
                        !_historyWork.Busy() && !_layerMetadataTicket && !_layerMetadataStamp && !_activeHostRequest))
            return 5;
        if (!fault && !expect(test.name, "held-source-and-reservation", state.visited && state.held))
            return 5;
        if (fault &&
            !expect(test.name, "named-bad-alloc-fails-operation",
                    outcome.error == E_OUTOFMEMORY && gate->reservationHeld.load() &&
                        (test.mode == Mode::CaptureFault ? gate->captures.load() == 0 : gate->faultHits.load() == 1)))
            return 5;
        if (expected != HostOutcome::Succeeded || test.mode == Mode::NoOp)
        {
            if (!expect(test.name, "owner-and-history-preserved",
                        _session.Document() == state.before && _session.Generation() == state.generation &&
                            _session.history.CurrentRevision() == state.revision &&
                            _session.history.UndoCount() == state.originalUndoCount &&
                            _session.history.CaptureState().FutureCount() == (test.mode == Mode::NoOp ? 1u : 0u)))
                return 5;
        }
        else
        {
            auto expectedDocument = DocumentFixture::Copy(before.value());
            for (auto& layer : expectedDocument.layers)
            {
                if (layer.id != target)
                    continue;
                if (test.mode == Mode::Visibility)
                    layer.visible = false;
                else if (test.mode == Mode::Opacity)
                    layer.opacity = 0.375;
                else if (test.mode == Mode::Blend)
                    layer.blendMode = LayerBlendMode::Multiply;
                else
                    layer.name = L"Worker layer";
            }
            const std::wstring_view title = test.mode == Mode::Visibility ? L"Hide Layer"
                                            : test.mode == Mode::Opacity  ? L"Layer Opacity"
                                            : test.mode == Mode::Blend    ? L"Layer Blend Mode"
                                                                          : L"Rename Layer";
            if (!expect(test.name, "literal-output-and-history",
                        _session.Document().value() == std::move(expectedDocument).Seal() &&
                            _session.history.UndoCount() == 1 && _session.history.UndoName() == title &&
                            _session.ActiveLayerID() == active))
                return 5;
        }
        if (test.mode == Mode::Ordered &&
            !expect(test.name, "younger-opacity-then-undo",
                    state.younger && state.undo && state.younger.value().Read().outcome == HostOutcome::Succeeded &&
                        state.undo.value().Read().outcome == HostOutcome::Succeeded &&
                        _session.history.RedoName() == L"Layer Opacity"))
            return 5;
        if (test.mode == Mode::ViewPump && !expect(test.name, "view-message-runs-while-worker-held", state.viewPending))
            return 5;
        if (test.mode == Mode::Cancel &&
            !expect(test.name, "cancel-retains-unacknowledged-barrier", state.cancelPending))
            return 5;
        if (residency)
        {
            if (!expect(test.name, "same-reservation-single-capture-and-metadata-preparation",
                        gate->captures.load() == 1 && gate->visits.load() == 1 && gate->residentGrowths.load() == 1 &&
                            gate->residencyRefreshes.load() == 1 && gate->refreshReservationHeld.load() &&
                            gate->lateResidencySource.ByteSize() == 8))
                return 5;
            if (test.mode == Mode::ResidencyRefresh &&
                !expect(test.name, "literal-grown-history-trim-and-completed-rename",
                        _session.history.RetainedBytes(_session.Document()) == 0 &&
                            _session.history.CurrentRevision() != state.revision && _session.history.IsModified()))
                return 5;
            if (test.mode == Mode::ResidencyRefreshFault &&
                !expect(test.name, "worker-refresh-allocation-fails-original-operation",
                        outcome.error == E_OUTOFMEMORY))
                return 5;
            // No live-history maintenance is claimed here: failure/cancel preserves the old revision/history,
            // whose independently grown cache is a separate notification/maintenance obligation.
            _session.history = DocumentHistory{};
        }
        _layerMetadataTestGate.reset();
    }
    prepare();
    const auto before = _session.Document();
    const auto revision = _session.history.CurrentRevision();
    for (std::size_t i = 0; i < JobScheduler::kCapacity; ++i)
    {
        std::unique_ptr<Job> job = std::make_unique<OccupyMetadataSlot>();
        JobTicket ticket;
        if (FAILED(_jobs.Submit(job, {JobClass::Background}, ticket)))
            return 5;
    }
    const auto refused = QueueHostInput(HostCommandInput{ChromeHost::kCommandToggleLayerVisibility});
    for (unsigned int i = 0; i < 4 && refused.Read().outcome == HostOutcome::Pending; ++i)
        (void)RunUiActions(_jobs);
    const bool safeRefusal =
        expect("admission", "full-scheduler-preserves-owner",
               refused.Read().outcome == HostOutcome::Failed &&
                   refused.Read().error == static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA)) &&
                   _session.Document() == before && _session.history.CurrentRevision() == revision &&
                   !_historyWork.Busy() && !_layerMetadataTicket && !_layerMetadataStamp);
    if (SettleJobs(_jobs, 15000, DispatchMessageThunk, this) != SettleResult::Settled || !safeRefusal)
        return 5;
    const auto recovery = QueueHostInput(HostCommandInput{ChromeHost::kCommandToggleLayerVisibility});
    if (!expect("admission", "fresh-input-recovers",
                SettleJobs(_jobs, 15000, DispatchMessageThunk, this) == SettleResult::Settled &&
                    recovery.Read().outcome == HostOutcome::Succeeded && !_session.ActiveLayer()->visible &&
                    _jobs.ReservedMemoryBytes() == 0 && !_historyWork.Busy()))
        return 5;
    // The worker slice must preserve commands that were legal during an existing gesture or transaction.
    // Each context must capture under admission and retire its complete worker publication.
    constexpr std::array contexts{"brush", "warp", "shape", "lasso", "nested"};
    constexpr std::array kinds{LayerMetadataKind::Rename, LayerMetadataKind::ToggleVisibility,
                               LayerMetadataKind::SetOpacity, LayerMetadataKind::SetBlendMode};
    constexpr std::array kindNames{"rename", "visibility", "opacity", "blend"};
    for (std::size_t context = 0; context < contexts.size(); ++context)
    {
        for (const auto kind : kinds)
        {
            prepare();
            const std::string contextName =
                std::string(contexts[context]) + "-" + kindNames[static_cast<std::size_t>(kind)];
            const auto gate = std::make_shared<LayerMetadataTestGate>();
            gate->hold = false;
            _layerMetadataTestGate = gate;
            if (context == 0 && !_session.BeginBrush({0.5, 0.5}))
                return 5;
            if (context == 1)
            {
                // Tool settings survive document replacement. Blur from an earlier fixture is a brush,
                // not a Warp mode: this case explicitly exercises the supported Liquify gesture.
                _session.smearMode = SmearMode::Liquify;
                if (!_session.BeginWarp({0.5, 0.5}))
                {
                    SelfTestFailure(L"The layer metadata Liquify fixture could not open its Warp gesture.\n");
                    return 5;
                }
            }
            if (context == 2)
                _session.BeginShape({0.5, 0.5});
            if (context == 3)
                _session.BeginLasso({0.5, 0.5}, SelectionMode::New);
            if (context == 4)
                _session.BeginEdit(L"Literal outer transaction");
            const auto brush = _session.PendingBrush();
            const auto warp = _session.PendingWarp();
            const auto shape = _session.PendingShape();
            const auto lasso = _session.PendingLasso();
            const auto active = _session.ActiveLayerID();
            const auto depth = _session.history.CaptureState().Depth();
            auto expectedDocument = DocumentFixture::Copy(_session.Document().value());
            auto& expectedLayer = expectedDocument.layers.back();
            ContinuationReceipt receipt;
            std::wstring_view title;
            switch (kind)
            {
            case LayerMetadataKind::Rename:
                expectedLayer.name = L"Gesture metadata";
                title = L"Rename Layer";
                receipt = rename(active.value(), L"  Gesture metadata  ");
                break;
            case LayerMetadataKind::ToggleVisibility:
                expectedLayer.visible = false;
                title = L"Hide Layer";
                receipt = QueueHostInput(HostCommandInput{ChromeHost::kCommandToggleLayerVisibility});
                break;
            case LayerMetadataKind::SetOpacity:
                expectedLayer.opacity = 0.375;
                title = L"Layer Opacity";
                receipt = QueueHostInput(HostLayerOpacityInput{active.value(), 0.375, ChromeEditPhase::Commit});
                break;
            case LayerMetadataKind::SetBlendMode:
                expectedLayer.blendMode = static_cast<LayerBlendMode>(1);
                title = L"Layer Blend Mode";
                receipt = QueueHostInput(HostCommandInput{ChromeHost::kCommandBlendNext});
                break;
            }
            const bool finished = SettleJobs(_jobs, 15000, DispatchMessageThunk, this) == SettleResult::Settled;
            const auto& currentLasso = _session.PendingLasso();
            const bool unchangedLasso = currentLasso.has_value() == lasso.has_value() &&
                                        (!lasso || (currentLasso.value().points == lasso.value().points &&
                                                    currentLasso.value().cursor == lasso.value().cursor &&
                                                    currentLasso.value().mode == lasso.value().mode &&
                                                    currentLasso.value().kind == lasso.value().kind &&
                                                    currentLasso.value().anchor == lasso.value().anchor));
            const bool unchangedGesture = _session.PendingBrush() == brush && _session.PendingWarp() == warp &&
                                          _session.PendingShape() == shape && unchangedLasso;
            if (!expect(contextName.c_str(), "worker-preserves-gesture-and-literal-edit",
                        finished && receipt.Read().outcome == HostOutcome::Succeeded &&
                            _session.Document() == std::optional<CanvasDocument>{std::move(expectedDocument).Seal()} &&
                            _session.ActiveLayerID() == active && unchangedGesture &&
                            _session.history.CaptureState().Depth() == depth && !_layerMetadataTicket &&
                            !_historyWork.Busy() && _jobs.ReservedMemoryBytes() == 0 && gate->captures.load() == 1 &&
                            gate->reservationHeld.load() && gate->retired.load() && gate->retiredOffOwner.load() &&
                            _session.history.UndoCount() == (context == 4 ? 0 : 1) &&
                            (context == 4 || _session.history.UndoName() == title)))
                return 5;
            if (context == 4)
            {
                _session.EndEdit();
                if (!expect(contextName.c_str(), "outer-name-and-single-entry-preserved",
                            _session.history.UndoCount() == 1 &&
                                _session.history.UndoName() == L"Literal outer transaction"))
                    return 5;
            }
        }
    }
    std::fprintf(stderr, "PASS metadata-host.summary checks=%d cases=%zu\n", checks,
                 cases.size() + 1 + contexts.size() * kinds.size());
    if (const int residency = RunHistoryResidencySelfTest(); residency != 0)
        return residency;
    return 0;
}
