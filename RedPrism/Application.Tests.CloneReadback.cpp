#include "Application.h"

#include "../Tests/Support/Support.Tests.DocumentFixture.h"
#include "BrushStroke.h"
#include "ChromeHost.h"
#include "KeyboardState.h"
#include "Runtime.Tests.ApplicationShutdownTestProbe.h"
#include "Runtime.Tests.CloneReadbackTestGate.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <utility>
#include <vector>

int Application::RunCloneReadbackSelfTest() noexcept
{
    const auto originalTool = _tool;
    const auto originalViewport = _viewport;
    const auto originalSettings = _session.brushSettings;
    const auto originalCloneSettings = _session.cloneSettings;
    const auto originalSource = _session.cloneSource;
    const auto originalSpacePan = _spacePan;
    const auto settle = [this]() noexcept
    { return SettleJobs(_jobs, 15000, DispatchMessageThunk, this) == SettleResult::Settled; };
    const auto cleanup = wil::scope_exit(
        [&]() noexcept
        {
            if (_cloneReadbackTestGate)
                (void)SetEvent(_cloneReadbackTestGate->release.get());
            CancelHostContinuations();
            if (!settle())
                std::_Exit(5);
            _cloneReadbackTestGate.reset();
            _tool = originalTool;
            _viewport = originalViewport;
            _session.brushSettings = originalSettings;
            _session.cloneSettings = originalCloneSettings;
            _session.SetCloneSource(originalSource.value_or(Vec2{}));
            _session.cloneSource = originalSource;
            _canvasPanLast.reset();
            _spacePan = originalSpacePan;
        });
    enum class Mode
    {
        Success,
        Cancel,
        Failure,
        CaptureFailure,
        Stale,
        Rebind,
        RestartLimit,
        LiveLease,
        CaptureLoss,
        PanLive,
        PanLoss,
        PanCancel,
    };
    constexpr std::array<const char*, 12> names{"success",      "cancel",   "failure",       "capture-failure",
                                                "stale",        "rebind",   "restart-limit", "live-lease",
                                                "capture-loss", "pan-live", "pan-loss",      "pan-cancel"};
    struct State final
    {
        Application& owner;
        Mode mode;
        bool visited = false;
        bool held = false;
        bool heldObserved = false;
        bool viewResponsive = false;
        bool cancelPending = false;
        bool liveLease = false;
        bool captureLost = false;
        bool panOpened = false;
        bool panLost = false;
        Vec2 expectedPan{};
        unsigned int rebinds = 0;
        HRESULT rebindResult = S_OK;
        std::optional<JobTicket> ticket;
        std::uint64_t bytes = 0;
    };
    const auto pointer = [](Application& owner, UINT message, WPARAM modifiers, Vec2 pixel) noexcept
    {
        const auto hole = owner._chrome->HolePixels();
        const auto view = owner._viewport.ViewFromDocument(pixel, owner.DocumentSize());
        owner.OnPointer(message, modifiers,
                        MAKELPARAM(static_cast<int>(std::lround(hole.left + view.x)),
                                   static_cast<int>(std::lround(hole.top + view.y))));
    };
    const auto dispatch = [](void* opaque, void* native) noexcept
    {
        auto& state = *static_cast<State*>(opaque);
        auto& owner = state.owner;
        auto& message = *static_cast<MSG*>(native);
        if (message.message != CloneReadbackTestGate::kMessage)
        {
            owner.DispatchQueuedMessage(message);
            return;
        }
        state.visited = true;
        const auto gate = owner._cloneReadbackTestGate;
        const auto receipt = owner._canvasBrushReceipt.Read();
        if (message.wParam == 2)
            return; // Source adoption precedes the separate worker opening.
        if (message.wParam == 3)
        {
            const auto* draft = owner._session.PendingBrush();
            const auto openingBytes = draft ? draft->OpeningRetainedMemoryBytes() : 0;
            state.liveLease = owner._canvasCloneSample && owner._canvasCloneSample->memory &&
                              !owner._canvasCloneSample->lease && owner._canvasCloneSample->HasReservation() &&
                              owner._canvasCloneSample->ReservationBytes() == state.bytes && openingBytes != 0 &&
                              owner._jobs.ReservedMemoryBytes() == state.bytes + openingBytes &&
                              !owner._utilityTicket && receipt.outcome == HostOutcome::Pending && draft &&
                              owner._session.history.UndoCount() == 0;
            const auto hole = owner._chrome->HolePixels();
            const auto view = owner._viewport.ViewFromDocument({12, 6}, owner.DocumentSize());
            owner.OnPointer(WM_LBUTTONUP, 0,
                            MAKELPARAM(static_cast<int>(std::lround(hole.left + view.x)),
                                       static_cast<int>(std::lround(hole.top + view.y))));
            return;
        }
        const bool same = !state.ticket || state.ticket == owner._utilityTicket;
        if (!state.ticket)
        {
            state.ticket = owner._utilityTicket;
            state.bytes = owner._jobs.ReservedMemoryBytes();
        }
        const bool held = same && owner._utilityTicket && owner._historyWork.Busy() && state.bytes != 0 &&
                          owner._jobs.ReservedMemoryBytes() == state.bytes && receipt.outcome == HostOutcome::Pending &&
                          !owner._session.PendingBrush() && owner._session.history.UndoCount() == 0 &&
                          owner._gpuWork.LentCount() == 0;
        state.held = state.heldObserved ? state.held && held : held;
        state.heldObserved = true;
        if (message.wParam == 0)
        {
            // A recovered attempt captures/admit its device before the next preparation body attaches a
            // source. The original ticket/source/CPU charge and real GPU reservation already remain live.
            state.held = state.held && owner._utilityReadback.HasPending() && owner._graphGpuBudget.ActiveOwner() != 0;
            state.rebindResult = owner._renderer.RecoverDeviceForTest();
            owner.BindCompositionDevice();
            ++state.rebinds;
            return;
        }
        const auto beforeZoom = owner._viewport.zoom;
        owner.OnCommand(ChromeHost::kCommandZoomIn);
        state.viewResponsive = owner._viewport.zoom > beforeZoom && owner._session.history.UndoCount() == 0 &&
                               owner._canvasBrushReceipt.Read().outcome == HostOutcome::Pending;
        if (state.mode == Mode::PanLive || state.mode == Mode::PanLoss || state.mode == Mode::PanCancel)
        {
            const auto hole = owner._chrome->HolePixels();
            const auto view = owner._viewport.ViewFromDocument({12, 6}, owner.DocumentSize());
            const int x = static_cast<int>(std::lround(hole.left + view.x));
            const int y = static_cast<int>(std::lround(hole.top + view.y));
            const auto originalPan = owner._viewport.pan;
            const auto pending = owner._canvasBrushInput.PendingCount();
            const auto ordinal = owner._activeHostRequest->ordinal;
            owner._spacePan = true;
            owner.OnPointer(WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, y));
            owner.OnPointer(WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(x + 9, y - 4));
            // A refused right gesture must not end this admitted left pan or release its capture.
            owner.OnPointer(WM_RBUTTONDOWN, MK_LBUTTON | MK_RBUTTON, MAKELPARAM(x + 9, y - 4));
            owner.OnPointer(WM_RBUTTONUP, MK_LBUTTON, MAKELPARAM(x + 9, y - 4));
            state.expectedPan = {originalPan.x + 9, originalPan.y - 4};
            state.panOpened = owner._canvasPanLast && owner._viewport.pan == state.expectedPan &&
                              owner._canvasBrushInput.PendingCount() == pending &&
                              !owner._canvasBrushInput.Sampling() && owner._activeHostRequest->ordinal == ordinal &&
                              owner._utilityTicket == state.ticket &&
                              owner._canvasBrushReceipt.Read().outcome == HostOutcome::Pending &&
                              !owner._session.PendingBrush() && owner._session.history.UndoCount() == 0;
            if (state.mode == Mode::PanLoss)
            {
                (void)SendMessageW(owner._window.get(), WM_CAPTURECHANGED, 0, 0);
                state.panLost = !owner._canvasPanLast && !owner._spacePan &&
                                owner._canvasBrushInput.PendingCount() == pending &&
                                owner._canvasBrushReceipt.Read().outcome == HostOutcome::Pending &&
                                owner._utilityTicket == state.ticket && owner._session.history.UndoCount() == 0;
            }
        }
        // A later direct settings/source edit must not reinterpret this accepted source offset or tip.
        owner._session.cloneSettings = {false, false};
        owner._session.SetCloneSource({14, 1});
        owner._session.brushSettings = BrushSettings{9, 1, 0, 1, 0, 1};
        if (state.mode == Mode::Cancel || state.mode == Mode::PanCancel)
        {
            (void)owner.CancelCanvasBrush();
            state.cancelPending = owner._canvasBrushReceipt.Read().outcome == HostOutcome::Pending;
        }
        else if (state.mode == Mode::Stale)
            owner._session.SetOpacity(.5);
        else if (state.mode == Mode::CaptureLoss)
        {
            const bool routed = owner.HonorsPointerLoss() && !owner._releasingCapture;
            (void)SendMessageW(owner._window.get(), WM_CAPTURECHANGED, 0, 0);
            state.captureLost = routed && !owner._canvasBrushInput.Sampling() &&
                                owner._canvasBrushReceipt.Read().outcome == HostOutcome::Pending &&
                                owner._session.history.UndoCount() == 0;
        }
        (void)SetEvent(gate->release.get());
    };
    int checks = 0;
    const auto expect = [&checks](const char* mode, const char* name, bool value) noexcept
    {
        std::printf("%s clone-readback.%s.%s\n", value ? "PASS" : "FAIL", mode, name);
        checks += value ? 1 : 0;
        return value;
    };
    for (unsigned int i = 0; i < names.size(); ++i)
    {
        if (!settle())
            return 5;
        const auto mode = static_cast<Mode>(i);
        std::vector<std::uint8_t> bytes(16 * 12 * 4);
        for (int y = 0; y < 12; ++y)
            for (int x = 0; x < 16; ++x)
            {
                const auto offset = static_cast<std::size_t>(y * 16 + x) * 4;
                bytes[offset] = x < 8 ? 0 : 255;
                bytes[offset + 2] = x < 8 ? 255 : 0;
                bytes[offset + 3] = 255;
            }
        DocumentFixture fixture;
        fixture.id = Guid::Generate();
        fixture.width = 16;
        fixture.height = 12;
        ImageLayer source = ImageLayer::Blank(L"Clone source", {16, 12});
        source.transform.sampling = LayerSampling::Nearest;
        source.asset =
            RasterAsset{Raster::Make(16, 12, RasterFormat::Bgra8888, bytes.data(), bytes.size()), {}, source.name};
        fixture.layers.push_back(std::move(source));
        ImageLayer target = ImageLayer::Blank(L"Clone destination", {16, 12});
        const Guid targetId = target.id;
        fixture.layers.push_back(std::move(target));
        ReplaceDocument(std::move(fixture).Seal(), targetId, {});
        _tool = EditorTool::Clone;
        _spacePan = false;
        _drag = PointerDrag{};
        _canvasPanLast.reset();
        _session.brushSettings = BrushSettings{3, 1, 0, 0, 0, 1};
        _session.cloneSettings = {true, true};
        _session.SetCloneSource({4, 6});
        _viewport.zoom = 1;
        _viewport.pan = {};
        _viewport.followsFit = false;
        DocumentChanged();
        const auto revision = _session.history.CurrentRevision();
        const auto presents = _renderer.PresentCount();
        auto gate = std::make_shared<CloneReadbackTestGate>();
        if (!gate->release)
            return 5;
        gate->throwCaptureFailure = mode == Mode::CaptureFailure;
        gate->rebindsBeforeCopy = mode == Mode::Rebind ? 1 : mode == Mode::RestartLimit ? 3 : 0;
        gate->holdAfterCopy = mode != Mode::CaptureFailure && mode != Mode::Rebind && mode != Mode::RestartLimit;
        gate->observeAdoption = mode == Mode::LiveLease;
        if (mode == Mode::Failure)
            gate->result = {JobStatus::Failed, E_ACCESSDENIED};
        _cloneReadbackTestGate = gate;
        pointer(*this, WM_LBUTTONDOWN, MK_LBUTTON, {12, 6});
        if (!expect(names[i], "press-queues-without-pixel-history-or-source-capture",
                    _canvasBrushInput.PendingCount() == 1 &&
                        _canvasBrushReceipt.Read().outcome == HostOutcome::Pending && !_session.PendingBrush() &&
                        !_session.ActiveLayer()->asset && !_utilityTicket && gate->captures.load() == 0 &&
                        _session.history.UndoCount() == 0))
            return 5;
        if (mode != Mode::LiveLease && mode != Mode::CaptureLoss)
            pointer(*this, WM_LBUTTONUP, 0, {12, 6});
        State state{*this, mode};
        if (SettleJobs(_jobs, 15000, dispatch, &state) != SettleResult::Settled)
            return 5;
        // A live stroke deliberately owns no running job while awaiting physical Up. Its already-posted
        // adoption message must still be pumped if the scheduler settled immediately after the handoff.
        if (mode == Mode::LiveLease && _canvasBrushReceipt.Read().outcome == HostOutcome::Pending &&
            SettleJobs(_jobs, 15000, dispatch, &state) != SettleResult::Settled)
            return 5;
        const auto receipt = _canvasBrushReceipt.Read();
        const bool success = mode == Mode::Success || mode == Mode::Rebind || mode == Mode::LiveLease ||
                             mode == Mode::CaptureLoss || mode == Mode::PanLive || mode == Mode::PanLoss;
        const auto expected = success                                           ? HostOutcome::Succeeded
                              : mode == Mode::Cancel || mode == Mode::PanCancel ? HostOutcome::Cancelled
                              : mode == Mode::Stale                             ? HostOutcome::Stale
                                                                                : HostOutcome::Failed;
        if (mode == Mode::RestartLimit)
            std::fprintf(stderr,
                         "INFO clone-readback.restart-limit outcome=%u expected=%u error=0x%08x captures=%u "
                         "visited=%u held=%u rebinds=%u recoveries=%u preparations=%u\n",
                         static_cast<unsigned int>(receipt.outcome), static_cast<unsigned int>(expected),
                         static_cast<unsigned int>(receipt.error), gate->captures.load(), state.visited ? 1u : 0u,
                         state.held ? 1u : 0u, state.rebinds, gate->recoveries.load(), gate->preparations.load());
        if (!expect(names[i], "exact-receipt-and-original-capture",
                    receipt.outcome == expected && gate->captures.load() == (mode == Mode::CaptureFailure ? 0u : 1u) &&
                        (mode == Mode::CaptureFailure ? !state.visited : state.visited && state.held) &&
                        (mode != Mode::Failure || receipt.error == E_ACCESSDENIED) &&
                        (mode != Mode::CaptureFailure || receipt.error == E_OUTOFMEMORY) &&
                        (mode != Mode::RestartLimit || receipt.error == E_ABORT) &&
                        ((mode != Mode::Cancel && mode != Mode::PanCancel) || state.cancelPending)))
            return 5;
        if (!expect(names[i], "complete-retirement-and-no-hidden-present",
                    !_canvasBrushInput.Busy() && !_canvasCloneSample && !_utilityTicket && !_utilityStamp &&
                        !_historyWork.Busy() && _gpuWork.LentCount() == 0 && _gpuWork.InFlightCount() == 0 &&
                        !_gpuWork.HasPending() && _composition.UtilitySurfaceBytes() == 0 &&
                        _jobs.ReservedMemoryBytes() == 0 && _renderer.PresentCount() == presents &&
                        (mode == Mode::CaptureFailure || (gate->retired.load() && gate->retiredOffOwner.load()))))
            return 5;
        if (mode == Mode::PanLive || mode == Mode::PanLoss || mode == Mode::PanCancel)
        {
            if (!expect(names[i], "new-physical-pan-keeps-original-fifo-ordinal-ticket-and-source", state.panOpened))
                return 5;
            if (mode == Mode::PanLoss)
            {
                if (!expect(names[i], "physical-pan-loss-leaves-sealed-clone-prefix-and-source-current",
                            state.panLost && !_canvasPanLast && _viewport.pan == state.expectedPan))
                    return 5;
            }
            else
            {
                if (!expect(names[i], "view-pan-survives-clone-acknowledgement", _canvasPanLast.has_value()))
                    return 5;
                const auto hole = _chrome->HolePixels();
                const int x = static_cast<int>(std::lround(hole.left + _canvasPanLast->x));
                const int y = static_cast<int>(std::lround(hole.top + _canvasPanLast->y));
                OnPointer(WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(x + 3, y + 2));
                OnPointer(WM_LBUTTONUP, 0, MAKELPARAM(x + 3, y + 2));
                if (!expect(names[i], "later-pan-move-and-release-publish-no-extra-stroke-or-history",
                            !_canvasPanLast && !_canvasBrushInput.Busy() && !_session.PendingBrush() &&
                                _viewport.pan == Vec2{state.expectedPan.x + 3, state.expectedPan.y + 2} &&
                                _session.history.UndoCount() == (success ? 1 : 0)))
                    return 5;
            }
            _spacePan = false;
        }
        if (success)
        {
            const auto* layer = _session.ActiveLayer();
            std::array<std::uint8_t, 4> pixel{};
            if (layer && layer->asset)
                layer->asset->image.ReadRegion(12 - static_cast<int>(layer->transform.origin.x),
                                               6 - static_cast<int>(layer->transform.origin.y), 1, 1, pixel.data(), 4,
                                               0);
            if (!expect(names[i], "literal-red-source-and-one-clone-history-entry",
                        pixel == std::array<std::uint8_t, 4>{0, 0, 255, 255} && _session.history.UndoCount() == 1 &&
                            _session.history.UndoName() == L"Clone Stamp"))
                return 5;
            OnCommand(ChromeHost::kCommandUndo);
            if (!settle() || !expect(names[i], "undo-restores-empty-target", !_session.ActiveLayer()->asset))
                return 5;
            OnCommand(ChromeHost::kCommandRedo);
            if (!settle() || !expect(names[i], "redo-restores-one-clone",
                                     _session.ActiveLayer()->asset && _session.history.UndoCount() == 1 &&
                                         _session.history.UndoName() == L"Clone Stamp"))
                return 5;
        }
        else if (!expect(
                     names[i], "failed-clone-publishes-no-pixels-or-history",
                     !_session.ActiveLayer()->asset &&
                         (mode == Mode::Stale
                              ? _session.history.UndoCount() == 1 && _session.history.UndoName() == L"Layer Opacity"
                              : _session.history.UndoCount() == 0 && _session.history.CurrentRevision() == revision)))
            return 5;
        if (gate->holdAfterCopy &&
            !expect(names[i], "view-input-remains-responsive-before-readback-acknowledges", state.viewResponsive))
            return 5;
        if ((mode == Mode::Rebind || mode == Mode::RestartLimit) &&
            !expect(names[i], "device-recovery-keeps-ticket-and-two-restart-limit",
                    state.rebinds == gate->rebindsBeforeCopy && SUCCEEDED(state.rebindResult) &&
                        gate->recoveries.load() == (mode == Mode::Rebind ? 1u : 2u) &&
                        gate->preparations.load() == (mode == Mode::Rebind ? 2u : 3u)))
            return 5;
        if (mode == Mode::LiveLease &&
            !expect(names[i], "same-admission-survives-live-stroke-after-job-retirement", state.liveLease))
            return 5;
        if (mode == Mode::CaptureLoss &&
            !expect(names[i], "physical-capture-loss-seals-original-prefix-before-copy-acknowledges",
                    state.captureLost))
            return 5;
        _cloneReadbackTestGate.reset();
    }
    std::printf("PASS clone-readback.summary checks=%d cases=12\n", checks);
    return 0;
}

int Application::RunStandaloneCloneReadbackShutdownSelfTest(HINSTANCE instance) noexcept
{
    const KeyboardState::ScopedSynthetic keyboard;
    unsigned int checks = 0;
    int failures = 0;
    for (const bool live : std::array{false, true})
    {
        const auto expect = [&](bool value, const char* name) noexcept
        {
            ++checks;
            failures += value ? 0 : 1;
            std::printf("%s clone-readback-shutdown.%s.%s\n", value ? "PASS" : "FAIL",
                        live ? "live-sample" : "held-readback", name);
        };
        expect(!DeferredReleaseDetail::sink.enqueue, "no-outer-sink");
        ApplicationShutdownTestProbe probe;
        auto gate = std::make_shared<CloneReadbackTestGate>();
        auto owner = std::make_unique<Application>(instance, true, nullptr, 0);
        const auto cleanup = wil::scope_exit(
            [&]() noexcept
            {
                if (owner)
                    owner->Shutdown();
            });
        owner->_quietUi = true;
        owner->_prompts = &QuietPrompts();
        if (!gate->release || FAILED(owner->InitializeProcessing()) || FAILED(owner->RegisterWindowClass()) ||
            FAILED(owner->CreateMainWindow(false)) || FAILED(owner->InitializeRenderer()) ||
            FAILED(owner->InitializeChrome()))
            return 5;
        owner->BindCompositionDevice();
        DocumentFixture fixture;
        fixture.id = Guid::Generate();
        fixture.width = 16;
        fixture.height = 12;
        std::vector<std::uint8_t> bytes(16 * 12 * 4, 127);
        for (std::size_t i = 3; i < bytes.size(); i += 4)
            bytes[i] = 255;
        ImageLayer source = ImageLayer::Blank(L"Shutdown clone source", {16, 12});
        source.asset =
            RasterAsset{Raster::Make(16, 12, RasterFormat::Bgra8888, bytes.data(), bytes.size()), {}, source.name};
        fixture.layers.push_back(std::move(source));
        ImageLayer target = ImageLayer::Blank(L"Shutdown clone target", {16, 12});
        const Guid targetId = target.id;
        fixture.layers.push_back(std::move(target));
        owner->ReplaceDocument(std::move(fixture).Seal(), targetId, {});
        owner->_tool = EditorTool::Clone;
        owner->_session.brushSettings = BrushSettings{3, 1, 0, 0, 0, 1};
        owner->_session.cloneSettings = {true, true};
        owner->_session.SetCloneSource({4, 6});
        owner->_viewport.zoom = 1;
        owner->_viewport.pan = {};
        owner->_viewport.followsFit = false;
        owner->DocumentChanged();
        const Guid revision = owner->_session.history.CurrentRevision();
        gate->holdAfterCopy = !live;
        gate->observeAdoption = live;
        owner->_cloneReadbackTestGate = gate;
        const auto hole = owner->_chrome->HolePixels();
        const auto view = owner->_viewport.ViewFromDocument({12, 6}, owner->DocumentSize());
        owner->OnPointer(WM_LBUTTONDOWN, MK_LBUTTON,
                         MAKELPARAM(static_cast<int>(std::lround(hole.left + view.x)),
                                    static_cast<int>(std::lround(hole.top + view.y))));
        const auto receipt = owner->_canvasBrushReceipt;
        struct State final
        {
            Application& owner;
            bool live;
            Guid revision;
            bool reached = false;
            std::uint64_t sourceBytes = 0;
            std::uint64_t openingBytes = 0;
            bool documentUnchangedAtDetach = false;
            bool sampleChargedAtDetach = false;
        } state{*owner, live, revision};
        const auto dispatch = [](void* opaque, void* native) noexcept
        {
            auto& state = *static_cast<State*>(opaque);
            auto& message = *static_cast<MSG*>(native);
            if (message.message == CloneReadbackTestGate::kMessage)
                state.reached = message.wParam == (state.live ? 3u : 1u);
            else
                state.owner.DispatchQueuedMessage(message);
        };
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
        if (live)
        {
            state.sourceBytes = owner->_canvasCloneSample ? owner->_canvasCloneSample->ReservationBytes() : 0;
            const auto* draft = owner->_session.PendingBrush();
            state.openingBytes = draft ? draft->OpeningRetainedMemoryBytes() : 0;
        }
        expect(receipt.Read().outcome == HostOutcome::Pending && owner->_jobs.ReservedMemoryBytes() != 0 &&
                   gate->captures.load() == 1 &&
                   (live ? owner->_canvasCloneSample && owner->_canvasCloneSample->HasReservation() &&
                               owner->_session.PendingBrush() && !owner->_utilityTicket && state.openingBytes != 0 &&
                               owner->_jobs.ReservedMemoryBytes() == state.sourceBytes + state.openingBytes
                         : owner->_utilityTicket && !owner->_session.PendingBrush() && !owner->_canvasCloneSample),
               "actual-admitted-boundary-held");
        probe.draft = owner->_session.WeakPendingBrushForTest();
        probe.checkContext = &state;
        probe.checkOwner = [](void* opaque) noexcept
        {
            auto& state = *static_cast<State*>(opaque);
            const auto* layer = state.owner._session.ActiveLayer();
            state.documentUnchangedAtDetach = layer && !layer->asset &&
                                              state.owner._session.history.CurrentRevision() == state.revision &&
                                              state.owner._session.history.UndoCount() == 0;
            state.sampleChargedAtDetach =
                state.owner._canvasCloneSample && state.owner._canvasCloneSample->HasReservation() &&
                state.owner._canvasCloneSample->ReservationBytes() == state.sourceBytes && state.openingBytes != 0 &&
                state.owner._jobs.ReservedMemoryBytes() == state.sourceBytes + state.openingBytes;
        };
        owner->_shutdownTestProbe = &probe;
        owner->Shutdown(); // The held worker exits on the actual shutdown event, without releasing its test gate.
        expect(receipt.Read().outcome == HostOutcome::Cancelled && state.documentUnchangedAtDetach,
               "cancelled-receipt-without-pixel-or-history-publication");
        expect(!live || (state.sampleChargedAtDetach && probe.draftAliveAtDetach && probe.reservationAndWakeAlive &&
                         probe.graphicsDetachedOnOwner),
               "live-source-charge-survives-owner-graphics-detach");
        expect(gate->retired.load() && gate->retiredOffOwner.load() && probe.draft.expired() &&
                   probe.workerCalls.load() == 1 && probe.offOwner.load() && probe.mta.load() &&
                   !owner->_canvasCloneSample && !owner->_session.PendingBrush() &&
                   owner->_jobs.ReservedMemoryBytes() == 0 && owner->_jobs.ReleasingMemoryBytes() == 0 &&
                   owner->_jobs.IsIdle() && !owner->_utilityTicket && !owner->_utilityStamp &&
                   !owner->_historyWork.Busy() && !owner->_gpuWork.HasPending(),
               "joined-worker-aggregate-and-full-reservation-retirement");
        owner->_shutdownTestProbe = nullptr;
        owner.reset();
        expect(!DeferredReleaseDetail::sink.enqueue, "sink-cleared-after-owner-destruction");
        MSG quit{};
        unsigned int quitCount = 0;
        while (PeekMessageW(&quit, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE))
            ++quitCount;
        expect(quitCount == 1, "fixture-quit-consumed-before-next-owner");
    }
    std::printf("%u clone-readback-shutdown assertions; %d failed.\n", checks, failures);
    return failures == 0 ? 0 : 5;
}
