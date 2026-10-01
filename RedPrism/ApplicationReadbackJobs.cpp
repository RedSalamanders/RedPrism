#include "Application.h"

#include "ChromeHost.h"
#include "DocumentReadbackJob.h"
#include "UtilityWorkTestGate.h"
#include "resource.h"

#include <array>
#include <cstdio>
#include <memory>
#include <new>
#include <utility>

HRESULT Application::PrepareUtilityDevice(HRESULT reason) noexcept
{
    if (_shutdownStarted || !_gpuWorkInitialized)
        return E_ABORT;
    RETURN_IF_FAILED(_renderer.RecoverDeviceForUtility(reason));
    BindCompositionDevice();
    return _gpuBindingError;
}

int Application::RunUtilityReadbackSelfTest() noexcept
{
    enum class Mode
    {
        Ordered,
        Cancel,
        Failure,
        Stale,
        Rebind,
        CaptureFailure,
        BeforeSnapshotFailure,
        AfterSnapshotFailure,
        EntryFailure,
        StackFailure,
        TrimFailure,
        Epoch,
        ProbeCaptureFailure,
        ProbeAllocationFailure,
        ProbeCancel,
        ProbeStale,
        NoPlan
    };
    struct State final
    {
        Application& owner;
        Mode mode;
        ContinuationReceipt merge;
        bool visited = false, held = false, cancelPending = false;
        HRESULT rebound = E_PENDING;
    };
    const auto dispatch = [](void* context, void* native) noexcept
    {
        auto& state = *static_cast<State*>(context);
        auto& owner = state.owner;
        const auto& message = *static_cast<MSG*>(native);
        if (message.message != UtilityWorkTestGate::kMessage)
        {
            owner.DispatchQueuedMessage(*static_cast<MSG*>(native));
            return;
        }
        state.visited = true;
        state.held = owner._historyWork.Busy() && owner._gpuWork.LentCount() == 0 &&
                     state.merge.Read().outcome == HostOutcome::Pending;
        if (state.mode == Mode::Cancel || state.mode == Mode::ProbeCancel)
        {
            owner.CancelHostContinuations();
            state.cancelPending = state.merge.Read().outcome == HostOutcome::Pending;
        }
        else if (state.mode == Mode::Stale || state.mode == Mode::ProbeStale)
            owner._session.SetOpacity(0.5); // deliberate external mutation: a stale prepared merge must be dropped
        else if (state.mode == Mode::Rebind)
        {
            state.rebound = owner._renderer.RecoverDeviceForTest();
            owner.BindCompositionDevice();
        }
        else if (state.mode == Mode::Epoch)
        {
            owner._session.history.Begin(L"Epoch-only", owner._session.Document(), owner._session.ActiveLayerID());
            owner._session.history.End(owner._session.Document(), owner._session.ActiveLayerID());
        }
        owner._composition.ReleaseDocumentSurfaces();
        state.held = state.held && owner._composition.CanvasSurfaceBytes() == 0;
        (void)SetEvent(owner._utilityWorkTestGate->release.get());
    };
    for (const auto mode : {Mode::Ordered, Mode::Cancel, Mode::Failure, Mode::Stale, Mode::Rebind, Mode::CaptureFailure,
                            Mode::BeforeSnapshotFailure, Mode::AfterSnapshotFailure, Mode::EntryFailure,
                            Mode::StackFailure, Mode::TrimFailure, Mode::Epoch, Mode::ProbeCaptureFailure,
                            Mode::ProbeAllocationFailure, Mode::ProbeCancel, Mode::ProbeStale, Mode::NoPlan})
    {
        _session.CreateDocument(mode == Mode::NoPlan ? 30000 : 16, mode == Mode::NoPlan ? 30000 : 12);
        if (mode != Mode::NoPlan)
        {
            (void)InsertPaintedLayer(_session, L"Red", 8, 8, {0, 0}, 0, 0xFFFF0000);
            (void)InsertPaintedLayer(_session, L"Blue", 8, 8, {4, 0}, 0, 0xFF0000FF);
        }
        else
            _session.history.Reset();
        const bool constructionFailure = (mode >= Mode::CaptureFailure && mode <= Mode::TrimFailure) ||
                                         mode == Mode::ProbeCaptureFailure || mode == Mode::ProbeAllocationFailure;
        if (constructionFailure || mode == Mode::Epoch)
        {
            _session.SetOpacity(0.75);
            _session.Undo();
        }
        _session.history.MarkSaved();
        DocumentChanged();
        const auto layers = _session.Document()->layers.size();
        const auto revision = _session.history.CurrentRevision();
        const auto presents = _renderer.PresentCount();
        _utilityWorkTestGate = std::make_shared<UtilityWorkTestGate>();
        _utilityWorkTestGate->release.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (!_utilityWorkTestGate->release)
            return 5;
        if (mode == Mode::Failure)
            _utilityWorkTestGate->result = {JobStatus::Failed, E_ACCESSDENIED};
        if (mode == Mode::CaptureFailure)
            _utilityWorkTestGate->throwCaptureFailure = true;
        _utilityWorkTestGate->throwProbeCaptureFailure = mode == Mode::ProbeCaptureFailure;
        _utilityWorkTestGate->throwProbeAllocationFailure = mode == Mode::ProbeAllocationFailure;
        _utilityWorkTestGate->holdProbe = mode == Mode::ProbeCancel || mode == Mode::ProbeStale;
        if (mode >= Mode::BeforeSnapshotFailure && mode <= Mode::TrimFailure)
        {
            constexpr std::array stages{
                DocumentHistory::PrepareStage::BeforeSnapshot, DocumentHistory::PrepareStage::AfterSnapshot,
                DocumentHistory::PrepareStage::Entry, DocumentHistory::PrepareStage::StackGrowth,
                DocumentHistory::PrepareStage::TrimIndex};
            _utilityWorkTestGate->throwHistoryStage =
                stages[static_cast<std::size_t>(mode) - static_cast<std::size_t>(Mode::BeforeSnapshotFailure)];
        }
        State state{*this, mode, QueueHostInput(HostCommandInput{ChromeHost::kCommandMergeDown})};
        std::optional<ContinuationReceipt> invert;
        if (mode == Mode::Ordered)
            invert = QueueHostInput(HostCommandInput{ChromeHost::kCommandInvert});
        if (SettleJobs(_jobs, 15000, dispatch, &state) != SettleResult::Settled)
        {
            SelfTestFailure(L"The utility readback harness did not settle.\n");
            return 5;
        }
        const auto outcome = state.merge.Read();
        const auto expected = constructionFailure                                 ? HostOutcome::Failed
                              : mode == Mode::Cancel || mode == Mode::ProbeCancel ? HostOutcome::Cancelled
                              : mode == Mode::Failure                             ? HostOutcome::Failed
                              : mode == Mode::Stale || mode == Mode::ProbeStale || mode == Mode::Epoch
                                  ? HostOutcome::Stale
                                  : HostOutcome::Succeeded;
        bool correct = (constructionFailure || mode == Mode::NoPlan ? !state.visited : state.visited && state.held) &&
                       outcome.outcome == expected && _utilityWorkTestGate->retiredNodes.load() != 0 &&
                       _utilityWorkTestGate->retiredOwnerNodes.load() == 0 &&
                       _utilityWorkTestGate->probeReservationHeld && _gpuWork.LentCount() == 0 &&
                       _gpuWork.InFlightCount() == 0 && !_gpuWork.HasPending() && _jobs.ReservedMemoryBytes() == 0 &&
                       _composition.UtilitySurfaceBytes() == 0 && !_historyWork.Busy() && !_utilityTicket &&
                       _renderer.PresentCount() == presents;
        if (mode == Mode::Ordered)
        {
            const auto* active = _session.ActiveLayer();
            correct = correct && invert->Read().outcome == HostOutcome::Succeeded &&
                      _session.Document()->layers.size() == layers - 1 && _session.history.UndoName() == L"Invert" &&
                      active && active->asset && active->asset->image.Width() == 12 &&
                      active->asset->image.Height() == 8;
            if (active && active->asset)
            {
                const auto* bytes = active->asset->image.Data();
                correct = correct && bytes && bytes[0] == 255 && bytes[1] == 255 && bytes[2] == 0 && bytes[3] == 255;
            }
            _session.Undo();
            correct = correct && _session.history.UndoName() == L"Merge Down";
        }
        else if (mode == Mode::Rebind)
            correct = correct && SUCCEEDED(state.rebound) && _utilityWorkTestGate->visits.load() == 1 &&
                      _session.Document()->layers.size() == layers - 1 && _session.history.UndoName() == L"Merge Down";
        else
        {
            correct = correct && _session.Document()->layers.size() == layers;
            if (mode == Mode::Stale || mode == Mode::ProbeStale)
                correct = correct && _session.ActiveLayer()->opacity == 0.5;
            else
                correct = correct && _session.history.CurrentRevision() == revision;
            if (mode == Mode::Cancel || mode == Mode::ProbeCancel)
                correct = correct && state.cancelPending;
            if (mode == Mode::Failure)
                correct = correct && outcome.error == E_ACCESSDENIED;
            if (constructionFailure || mode == Mode::Epoch)
                correct = correct && _session.history.CanRedo() && _session.history.RedoName() == L"Layer Opacity";
            if (constructionFailure)
                correct = correct && outcome.error == E_OUTOFMEMORY;
            if (mode == Mode::NoPlan)
                correct = correct && _session.Document()->width == 30000 && _session.Document()->height == 30000 &&
                          _session.history.UndoCount() == 0 && _utilityWorkTestGate->probePreparations.load() == 1 &&
                          _utilityWorkTestGate->readbackCaptures.load() == 0 &&
                          _utilityWorkTestGate->probeReservationBytes < 16 * 1024 * 1024;
            if (mode == Mode::ProbeCaptureFailure || mode == Mode::ProbeAllocationFailure ||
                mode == Mode::ProbeCancel || mode == Mode::ProbeStale)
                correct = correct && _utilityWorkTestGate->readbackCaptures.load() == 0;
        }
        const auto gateVisits = _utilityWorkTestGate->visits.load();
        _utilityWorkTestGate.reset();
        if (!correct)
        {
            std::fprintf(stderr, "Utility mode=%u visited=%u held=%u outcome=%u expected=%u error=0x%08x visits=%u\n",
                         static_cast<unsigned int>(mode), state.visited ? 1U : 0U, state.held ? 1U : 0U,
                         static_cast<unsigned int>(outcome.outcome), static_cast<unsigned int>(expected),
                         static_cast<unsigned int>(outcome.error), gateVisits);
            SelfTestFailure(L"Utility readback failed ordering, hidden ownership, exact drop or rebind validation.\n");
            return 5;
        }
        std::fprintf(stderr, "PASS utility-history mode=%u\n", static_cast<unsigned int>(mode));
    }
    return 0;
}
