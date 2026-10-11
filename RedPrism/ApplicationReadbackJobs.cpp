#include "Application.h"

#include "ApplicationProcessingTestProbe.h"
#include "ApplicationShutdownTestProbe.h"
#include "ChromeHost.h"
#include "DeferredRelease.h"
#include "GpuCapabilities.h"
#include "UtilityWorkTestGate.h"
#include "resource.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
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

HRESULT Application::PrepareRecordedUtilityAdmission(const CanvasDocument& source, std::uint64_t additionalCpuBytes,
                                                     UtilityReadbackAdmission::Envelope& envelope,
                                                     GpuResidencyReservation& reservation) noexcept
{
    if (reservation || !additionalCpuBytes)
        return E_INVALIDARG;
    RETURN_IF_FAILED(PrepareUtilityDevice(S_OK));
    const auto tileSide = (std::min)(2048u, static_cast<unsigned int>(_composition.TextureLimit()));
    const auto estimated = UtilityReadbackAdmission::Estimate(source, tileSide, additionalCpuBytes, _renderer.IsWarp());
    if (!estimated)
        return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
    GpuCapabilities::VideoMemorySnapshot snapshot{};
    RETURN_IF_FAILED(GpuCapabilities::QueryVideoMemorySnapshot(_renderer.Device(), snapshot));
    const auto& segment = _renderer.IsWarp() && snapshot.nonLocal.available && snapshot.nonLocal.budget
                              ? snapshot.nonLocal
                              : snapshot.local;
    if (!segment.available || !segment.budget)
        return E_OUTOFMEMORY;
    GpuResidencyAdmission decision;
    auto admitted = _graphGpuBudget.ReserveOwner(
        estimated.value().gpuBytes, {_gpuWork.DeviceGeneration(), segment.budget, segment.currentUsage}, decision);
    if (!admitted)
        return E_OUTOFMEMORY;
    envelope = estimated.value();
    reservation = std::move(admitted);
    return S_OK;
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
        const bool published = mode == Mode::Ordered || mode == Mode::Rebind;
        const auto retained = published ? _utilityWorkTestGate->readbackReservationBytes : 0;
        bool correct = (constructionFailure || mode == Mode::NoPlan ? !state.visited : state.visited && state.held) &&
                       outcome.outcome == expected && _utilityWorkTestGate->retiredNodes.load() != 0 &&
                       _utilityWorkTestGate->retiredOwnerNodes.load() == 0 &&
                       _utilityWorkTestGate->probeReservationHeld && _gpuWork.LentCount() == 0 &&
                       _gpuWork.InFlightCount() == 0 && !_gpuWork.HasPending() &&
                       _jobs.ReservedMemoryBytes() == retained && (!published || retained != 0) &&
                       _jobs.ReleasingMemoryBytes() == 0 && _composition.UtilitySurfaceBytes() == 0 &&
                       !_historyWork.Busy() && !_utilityTicket && _renderer.PresentCount() == presents;
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
        if (published && correct)
        {
            // Ordered's younger Invert was undone above: both modes now expose the same literal merge.
            const auto* active = _session.ActiveLayer();
            Raster copied = active && active->asset ? active->asset->image : Raster{};
            std::weak_ptr<const void> weak = copied.WeakStorageForTest();
            const auto literal = [&]() noexcept
            {
                if (copied.Width() != 12 || copied.Height() != 8 || copied.Stride() != 48)
                    return false;
                const auto* pixels = copied.Data();
                if (!pixels)
                    return false;
                for (int y = 0; y != 8; ++y)
                    for (int x = 0; x != 12; ++x)
                    {
                        const auto offset = static_cast<std::size_t>(y * 12 + x) * 4;
                        // Red below opaque Blue: x=0..3 red, x=4..11 blue, all alpha 255.
                        if (pixels[offset] != (x < 4 ? 0 : 255) || pixels[offset + 1] != 0 ||
                            pixels[offset + 2] != (x < 4 ? 255 : 0) || pixels[offset + 3] != 255)
                            return false;
                    }
                return true;
            };
            const auto lifetime = [&](bool condition, const char* stage) noexcept
            {
                correct = correct && condition;
                std::fprintf(stderr, "%s utility-merge-lifetime mode=%u stage=%s\n", condition ? "PASS" : "FAIL",
                             static_cast<unsigned>(mode), stage);
            };
            lifetime(literal() && _jobs.ReservedMemoryBytes() == retained,
                     "published-crop-and-history-retain-original-charge");
            _composition.ReleaseDocumentSurfaces();
            _session.ClearDocument(); // Close releases live pixels and history; New Canvas is itself undoable.
            DocumentChanged();
            const bool settled = SettleJobs(_jobs, 15000, dispatch, &state) == SettleResult::Settled;
            lifetime(settled && !_session.Document() && _session.history.UndoCount() == 0 &&
                         !_session.history.CanRedo() && literal() && _jobs.ReservedMemoryBytes() == retained &&
                         _jobs.ReleasingMemoryBytes() == 0,
                     "copied-reader-retains-charge-after-document-and-history-release");
            copied = {};
            const bool storageSettled = SettleJobs(_jobs, 15000, dispatch, &state) == SettleResult::Settled;
            lifetime(storageSettled && weak.expired() && _jobs.ReservedMemoryBytes() == retained &&
                         _jobs.ReleasingMemoryBytes() == 0,
                     "last-weak-storage-control-retains-original-charge");
            weak.reset();
            const bool finalSettled = SettleJobs(_jobs, 15000, dispatch, &state) == SettleResult::Settled;
            lifetime(finalSettled && _jobs.ReservedMemoryBytes() == 0 && _jobs.ReleasingMemoryBytes() == 0 &&
                         _jobs.IsIdle(),
                     "final-control-acknowledges-original-refund");
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

int Application::RunStandaloneMergeReadbackSelfTest(HINSTANCE instance) noexcept
{
    unsigned checks = 0;
    unsigned failures = 0;
    for (const bool close : std::array{false, true})
    {
        const char* scenario = close ? "joined-probe-close" : "owned-probe-admission-refusal";
        const auto expect = [&](bool condition, const char* label) noexcept
        {
            ++checks;
            failures += condition ? 0u : 1u;
            std::printf("%s utility-merge-boundary.%s.%s\n", condition ? "PASS" : "FAIL", scenario, label);
        };
        expect(!DeferredReleaseDetail::sink.enqueue, "no-outer-sink");
        struct Memory final
        {
            std::uint64_t available = 4ull * 1024 * 1024 * 1024;
            unsigned samples = 0;
        } memory;
        ApplicationProcessingTestProbe processing;
        processing.memory = {[](void* opaque, MemorySnapshot& snapshot) noexcept
                             {
                                 auto& value = *static_cast<Memory*>(opaque);
                                 ++value.samples;
                                 snapshot = {value.available, value.available};
                                 return true;
                             },
                             &memory};
        auto gate = std::make_shared<UtilityWorkTestGate>();
        gate->release.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        gate->holdProbe = true;
        auto owner = std::make_unique<Application>(instance, true, nullptr, 0);
        const auto cleanup = wil::scope_exit(
            [&]() noexcept
            {
                if (owner)
                    owner->Shutdown();
            });
        owner->_processingTestProbe = &processing;
        owner->_quietUi = true;
        owner->_prompts = &QuietPrompts();
        if (!gate->release || FAILED(owner->InitializeProcessing()) || FAILED(owner->RegisterWindowClass()) ||
            FAILED(owner->CreateMainWindow(false)) || FAILED(owner->InitializeRenderer()) ||
            FAILED(owner->InitializeChrome()))
            return 5;
        owner->BindCompositionDevice();
        owner->_session.CreateDocument(16, 12);
        (void)InsertPaintedLayer(owner->_session, L"Red", 8, 8, {0, 0}, 0, 0xFFFF0000);
        (void)InsertPaintedLayer(owner->_session, L"Blue", 8, 8, {4, 0}, 0, 0xFF0000FF);
        owner->_session.history.MarkSaved();
        owner->DocumentChanged();
        const auto root = owner->_session.Document()->layers.Identity();
        const auto generation = owner->_session.Generation();
        const auto revision = owner->_session.history.CurrentRevision();
        const auto undo = owner->_session.history.UndoCount();
        owner->_utilityWorkTestGate = gate;
        const auto receipt = owner->QueueHostInput(HostCommandInput{ChromeHost::kCommandMergeDown});
        struct State final
        {
            Application& owner;
            bool reached = false;
        } state{*owner};
        const auto dispatch = [](void* opaque, void* native) noexcept
        {
            auto& current = *static_cast<State*>(opaque);
            auto& message = *static_cast<MSG*>(native);
            if (message.message == UtilityWorkTestGate::kMessage)
                current.reached = true;
            else
                current.owner.DispatchQueuedMessage(message);
        };
        // Event-held real probe worker; the deadline is a failure bound, not a throughput assertion.
        const auto deadline = GetTickCount64() + 10000;
        while (!state.reached)
        {
            if (PumpMessages(dispatch, &state).quit)
                std::_Exit(5);
            (void)DrainCompletions(owner->_jobs);
            (void)RunUiActions(owner->_jobs);
            if (state.reached)
                break;
            const auto now = GetTickCount64();
            if (now >= deadline)
                std::_Exit(5);
            if (owner->_jobs.HasUiWork())
                continue;
            const HANDLE wake = owner->_jobs.WakeHandle();
            if (MsgWaitForMultipleObjectsEx(1, &wake, static_cast<DWORD>(deadline - now), QS_ALLINPUT,
                                            MWMO_INPUTAVAILABLE) == WAIT_FAILED)
                std::_Exit(5);
        }
        expect(receipt.Read().outcome == HostOutcome::Pending && owner->_historyWork.Busy() &&
                   gate->probePreparations.load() == 1 && gate->readbackCaptures.load() == 0 &&
                   gate->probeReservationHeld && gate->probeReservationBytes != 0 &&
                   owner->_jobs.ReservedMemoryBytes() == gate->probeReservationBytes,
               "actual-probe-holds-original-admission-before-readback");
        const auto unchanged = [&]() noexcept
        {
            return owner->_session.Document() && owner->_session.Document()->layers.Identity() == root &&
                   owner->_session.Generation() == generation &&
                   owner->_session.history.CurrentRevision() == revision &&
                   owner->_session.history.UndoCount() == undo && !owner->_session.history.IsModified();
        };
        if (close)
        {
            struct ShutdownState final
            {
                Application& owner;
                const void* root;
                std::uint64_t generation;
                Guid revision;
                int undo;
                bool unchanged = false;
            } observed{*owner, root, generation, revision, undo};
            ApplicationShutdownTestProbe probe;
            probe.checkContext = &observed;
            probe.checkOwner = [](void* opaque) noexcept
            {
                auto& value = *static_cast<ShutdownState*>(opaque);
                value.unchanged = value.owner._session.Document() &&
                                  value.owner._session.Document()->layers.Identity() == value.root &&
                                  value.owner._session.Generation() == value.generation &&
                                  value.owner._session.history.CurrentRevision() == value.revision &&
                                  value.owner._session.history.UndoCount() == value.undo;
            };
            owner->_shutdownTestProbe = &probe;
            owner->Shutdown(); // Actual shutdown event joins the held probe; its gate is never signalled.
            expect(receipt.Read().outcome == HostOutcome::Cancelled && observed.unchanged,
                   "joined-close-cancels-without-document-or-history-publication");
            expect(gate->readbackCaptures.load() == 0 && gate->readbackReservationBytes == 0 &&
                       gate->retiredNodes.load() == 1 && gate->retiredOwnerNodes.load() == 0 &&
                       owner->_jobs.ReservedMemoryBytes() == 0 && owner->_jobs.ReleasingMemoryBytes() == 0 &&
                       owner->_jobs.IsIdle() && !owner->_utilityTicket && !owner->_utilityStamp &&
                       !owner->_historyWork.Busy() && !owner->_utilityReadback.HasPending() &&
                       owner->_graphGpuBudget.ReservedOwner() == 0,
                   "joined-close-retires-the-original-probe-aggregate-and-charge");
            owner->_shutdownTestProbe = nullptr;
        }
        else
        {
            // The only outstanding allocation is this probe itself. Its owned dependency cannot free while
            // the readback retains it; waiting here would strand the original history ordinal indefinitely.
            memory.available = kMemoryAdmissionHeadroomBytes + gate->probeReservationBytes;
            const auto samples = memory.samples;
            if (!SetEvent(gate->release.get()) ||
                SettleJobs(owner->_jobs, 10000, dispatch, &state) != SettleResult::Settled)
                return 5;
            expect(receipt.Read().outcome == HostOutcome::Failed && receipt.Read().error == E_OUTOFMEMORY &&
                       memory.samples > samples && unchanged() && gate->readbackCaptures.load() == 0 &&
                       gate->readbackReservationBytes != 0,
                   "fresh-owner-query-refuses-owned-dependency-without-capture-or-publication");
            expect(gate->retiredNodes.load() >= 1 && gate->retiredOwnerNodes.load() == 0 &&
                       owner->_jobs.ReservedMemoryBytes() == 0 && owner->_jobs.ReleasingMemoryBytes() == 0 &&
                       owner->_jobs.IsIdle() && !owner->_utilityTicket && !owner->_utilityStamp &&
                       !owner->_historyWork.Busy() && !owner->_utilityReadback.HasPending() &&
                       owner->_graphGpuBudget.ReservedOwner() == 0,
                   "refused-second-phase-refunds-after-original-probe-retirement");
            owner->Shutdown();
        }
        owner->_processingTestProbe = nullptr;
        owner.reset();
        MSG quit{};
        unsigned quitCount = 0;
        while (PeekMessageW(&quit, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE))
            ++quitCount;
        expect(quitCount == 1 && !DeferredReleaseDetail::sink.enqueue,
               "fixture-quit-and-owner-sink-cleared-before-next-owner");
    }
    std::printf("%u utility-merge-boundary assertions; %u failed.\n", checks, failures);
    return failures == 0 ? 0 : 5;
}
