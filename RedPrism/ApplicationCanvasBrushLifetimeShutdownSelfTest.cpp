#include "Application.h"

#include "ApplicationShutdownTestProbe.h"
#include "ChromeHost.h"
#include "DeferredRelease.h"
#include "KeyboardState.h"
#include "LayerMetadataTestGate.h"
#include "ModalProbePrompts.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>

int Application::RunStandaloneCanvasLifetimeShutdownSelfTest(HINSTANCE instance) noexcept
{
    // This independently owning fixture runs before RunSelfTest installs its own keyboard.
    // Preserve named input semantics even when Windows updates physical modifier state during a held job.
    const KeyboardState::ScopedSynthetic syntheticKeyboard;
    enum class Phase : std::uint8_t
    {
        Stream,
        Detached,
        DetachedSave,
        Presenter
    };
    struct Case
    {
        const char* name;
        bool live;
        Phase phase;
    };
    constexpr std::array cases{Case{"stream-held", false, Phase::Stream},
                               Case{"stream-live", true, Phase::Stream},
                               Case{"detached-held", false, Phase::Detached},
                               Case{"detached-live", true, Phase::Detached},
                               Case{"detached-save-held", false, Phase::DetachedSave},
                               Case{"detached-save-live", true, Phase::DetachedSave},
                               Case{"presenter-held", false, Phase::Presenter},
                               Case{"presenter-live", true, Phase::Presenter}};
    unsigned int checks = 0;
    unsigned int failures = 0;
    const auto expect = [&](bool value, const char* scenario, const char* name) noexcept
    {
        ++checks;
        failures += value ? 0 : 1;
        std::printf("%s canvas-brush-lifetime-shutdown.%s.%s\n", value ? "PASS" : "FAIL", scenario, name);
    };
    static constexpr auto pointer = [](Application& owner, UINT message, WPARAM modifiers, Vec2 point) noexcept
    {
        const auto hole = owner._chrome->HolePixels();
        const auto view = owner._viewport.ViewFromDocument(point, owner.DocumentSize());
        owner.OnPointer(message, modifiers,
                        MAKELPARAM(static_cast<int>(std::lround(hole.left + view.x)),
                                   static_cast<int>(std::lround(hole.top + view.y))));
    };
    struct State
    {
        Application& owner;
        const Case& scenario;
        ContinuationReceipt dependent;
        bool gate = false;
        bool beforeShutdown = false;
        bool selectedOriginal = false;
        bool presenterVisited = false;
        bool deferred = false;
        bool borrowedOwner = false;
        bool coreAliveAtDetach = false;
        bool producerClosedAtDetach = false;
        std::weak_ptr<BrushStroke> originalCore;
    };
    static constexpr auto modal = [](void* opaque) noexcept
    {
        auto& state = *static_cast<State*>(opaque);
        auto& owner = state.owner;
        state.presenterVisited = true;
        const auto ordinal = owner._canvasLifetime ? owner._canvasLifetime->entry.ordinal : 0;
        owner.Shutdown(); // Actual request while the registered UI source borrows the presenter-owned request.
        state.deferred = owner._jobs.InUiCallback() && owner._shutdownDeferred && !owner._shutdownStarted &&
                         owner._window && owner._chrome && owner._renderer.Device();
        state.borrowedOwner = ordinal != 0 && owner._canvasLifetime && owner._canvasLifetime->presenting &&
                              owner._canvasLifetime->entry.ordinal == ordinal &&
                              state.dependent.Read().outcome == HostOutcome::Pending;
    };
    static constexpr auto prepareShutdown = [](State& state) noexcept
    {
        auto& owner = state.owner;
        const auto* selected = owner._activeHostRequest
                                   ? std::get_if<HostCanvasBrushInput>(&owner._activeHostRequest->input.Input())
                                   : nullptr;
        state.selectedOriginal = selected && selected->opening == owner._canvasBrushInput.Opening();
        state.beforeShutdown =
            owner._canvasBrushInput.Busy() && owner._canvasBrushReceipt.Read().outcome == HostOutcome::Pending &&
            owner._hostInputBudget.Used() != 0 &&
            owner._session.PendingBrushOpening().has_value() == state.scenario.live && state.selectedOriginal;
        state.originalCore = owner._session.WeakPendingBrushForTest();
        if (state.scenario.phase != Phase::Stream)
        {
            state.dependent = owner.QueueHostInput(HostCloseProjectInput{});
            if (state.scenario.phase == Phase::Presenter)
                (void)RunUiActions(owner._jobs); // The prompt and Shutdown request run in an actual UI callback.
            else if (state.scenario.phase == Phase::Detached)
                owner.RunHostContinuation(); // Execute exact-opening discard cancellation before joining.
            else
                // Stop at the actual preflight boundary before the source can acknowledge and execute Save.
                // The live core may commit synchronously when the complete source continues after preflight.
                owner.RunCanvasLifetimePreflight();
            state.beforeShutdown =
                state.beforeShutdown &&
                (state.scenario.phase == Phase::Presenter
                     ? state.presenterVisited && state.deferred && state.borrowedOwner && !owner._canvasLifetime &&
                           state.dependent.Read().outcome == HostOutcome::Stale
                     : owner._canvasLifetime && !owner._canvasLifetime->presenting &&
                           !owner._canvasBrushInput.Sampling() &&
                           state.dependent.Read().outcome == HostOutcome::Pending &&
                           owner._canvasLifetime->save == (state.scenario.phase == Phase::DetachedSave) &&
                           owner._session.PendingBrushOpening().has_value() ==
                               (state.scenario.live && state.scenario.phase == Phase::DetachedSave));
        }
    };
    for (const auto& scenario : cases)
    {
        expect(!DeferredReleaseDetail::sink.enqueue, scenario.name, "no-surrogate-outer-sink");
        ApplicationShutdownTestProbe probe;
        auto owner = std::make_unique<Application>(instance, true, nullptr, 0);
        State state{*owner, scenario};
        ModalProbePrompts presenter(QuietPrompts(), &state, modal);
        const auto shutdown = wil::scope_exit(
            [&]() noexcept
            {
                if (owner)
                {
                    if (owner->_layerMetadataTestGate)
                        (void)SetEvent(owner->_layerMetadataTestGate->release.get());
                    owner->Shutdown();
                }
            });
        owner->_quietUi = true;
        owner->_prompts = scenario.phase == Phase::Presenter ? &presenter : &QuietPrompts();
        owner->_forcedDiscardAnswer =
            scenario.phase == Phase::DetachedSave ? Prompts::Answer::First : Prompts::Answer::Second;
        if (scenario.phase == Phase::DetachedSave)
        {
            // Select an existing document path, then shut down before the accepted prefix acknowledges or writes.
            // A nonexistent parent makes accidental write execution fail rather than modify any user file.
            wchar_t temporary[MAX_PATH]{};
            if (!GetTempPathW(MAX_PATH, temporary))
                return 5;
            owner->_documentPath = std::wstring(temporary) + L"RedPrism-Shutdown-NeverWrite-" +
                                   std::to_wstring(GetCurrentProcessId()) + L"/pending.prism";
            if (GetFileAttributesW(owner->_documentPath.c_str()) != INVALID_FILE_ATTRIBUTES)
                return 5;
        }
        if (FAILED(owner->InitializeProcessing()) || FAILED(owner->RegisterWindowClass()) ||
            FAILED(owner->CreateMainWindow(false)) || FAILED(owner->InitializeRenderer()) ||
            FAILED(owner->InitializeChrome()))
            return 5;
        owner->BindCompositionDevice();
        expect(owner->_window && owner->_chrome && owner->_renderer.IsWarp() && owner->_brushEngine.Ready() &&
                   DeferredReleaseDetail::sink.enqueue,
               scenario.name, "actual-hidden-WARP-host-and-retirement-sink");
        owner->_session.CreateDocument(32, 24, true);
        owner->_session.AddBlankLayer();
        owner->_session.history.Reset();
        owner->_tool = EditorTool::Brush;
        owner->_session.brushSettings = BrushSettings{3, 1, 1, 0, 0, 1};
        owner->_viewport.zoom = 1;
        owner->_viewport.pan = {};
        owner->_viewport.followsFit = false;
        owner->DocumentChanged();
        owner->OnLayerOpacity(.25, ChromeEditPhase::Preview);
        if (SettleJobs(owner->_jobs, 15000, DispatchMessageThunk, owner.get()) != SettleResult::Settled)
            return 5;
        expect(owner->_opacityEdit && owner->_session.ActiveLayer()->opacity == .25 &&
                   owner->_session.history.UndoCount() == 0,
               scenario.name, "typed-opacity-preview-before-original-down");
        owner->_layerMetadataTestGate = std::make_shared<LayerMetadataTestGate>();
        owner->_layerMetadataTestGate->release.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (!owner->_layerMetadataTestGate->release)
            return 5;
        pointer(*owner, WM_LBUTTONDOWN, MK_LBUTTON, {8, 8});
        const auto brush = owner->_canvasBrushReceipt;
        const auto pump = [](void* opaque, void* native) noexcept
        {
            auto& current = *static_cast<State*>(opaque);
            auto& application = current.owner;
            if (static_cast<MSG*>(native)->message != LayerMetadataTestGate::kMessage)
            {
                DispatchMessageThunk(&application, native);
                return;
            }
            const auto gate = application._layerMetadataTestGate;
            current.gate = application._historyWork.Busy() && application._layerMetadataTicket &&
                           gate->captures.load() == 1 && gate->reservationHeld.load() &&
                           application._canvasBrushInput.Sampling() && !application._session.PendingBrushOpening();
            if (!current.scenario.live)
                prepareShutdown(current);
            // Shutdown joins cancellation-aware producers; this explicit release also covers noncancelled teardown.
            gate->hold = false;
            (void)SetEvent(gate->release.get());
        };
        if (scenario.live)
        {
            if (SettleJobs(owner->_jobs, 15000, pump, &state) != SettleResult::Settled)
                return 5;
            prepareShutdown(state);
        }
        else
        {
            // Stop at the owned producer gate. Do not permit acknowledgement/replay before the held shutdown.
            const ULONGLONG deadline = GetTickCount64() + 15000;
            while (!state.gate)
            {
                MSG message{};
                while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
                {
                    pump(&state, &message);
                    if (state.gate)
                        break;
                }
                if (state.gate)
                    break;
                (void)DrainCompletions(owner->_jobs);
                (void)RunUiActions(owner->_jobs);
                const ULONGLONG now = GetTickCount64();
                if (now >= deadline)
                {
                    const auto gate = owner->_layerMetadataTestGate;
                    const auto hole = owner->_chrome->HolePixels();
                    const auto receipt = owner->_canvasBrushReceipt.Read();
                    std::fprintf(stderr,
                                 "DIAGNOSTIC canvas-brush-lifetime-shutdown.%s gate-timeout outcome=%u error=%08X "
                                 "busy=%u sampling=%u pointer=%u samplesColor=%u alt=%u captures=%u visits=%u "
                                 "held=%u metadata=%u history=%u reserved=%llu input=%llu hole=%.3f,%.3f,%.3f,%.3f\n",
                                 scenario.name, static_cast<unsigned int>(receipt.outcome),
                                 static_cast<unsigned int>(receipt.error),
                                 static_cast<unsigned int>(owner->_canvasBrushInput.Busy()),
                                 static_cast<unsigned int>(owner->_canvasBrushInput.Sampling()),
                                 static_cast<unsigned int>(owner->PointerBusy()),
                                 static_cast<unsigned int>(owner->SamplesColor()),
                                 static_cast<unsigned int>(KeyboardState::Held(VK_MENU)),
                                 gate ? gate->captures.load() : 0u, gate ? gate->visits.load() : 0u,
                                 gate ? static_cast<unsigned int>(gate->reservationHeld.load()) : 0u,
                                 static_cast<unsigned int>(owner->_layerMetadataTicket.has_value()),
                                 static_cast<unsigned int>(owner->_historyWork.Busy()),
                                 static_cast<unsigned long long>(owner->_jobs.ReservedMemoryBytes()),
                                 static_cast<unsigned long long>(owner->_hostInputBudget.Used()),
                                 static_cast<double>(hole.left), static_cast<double>(hole.top),
                                 static_cast<double>(hole.width), static_cast<double>(hole.height));
                    std::fflush(stderr);
                    std::_Exit(5);
                }
                const HANDLE wake = static_cast<HANDLE>(owner->_jobs.WakeHandle());
                if (MsgWaitForMultipleObjectsEx(1, &wake, static_cast<DWORD>(deadline - now), QS_ALLINPUT,
                                                MWMO_INPUTAVAILABLE) == WAIT_FAILED)
                    std::_Exit(5);
            }
        }
        expect(state.gate, scenario.name, "producer-gate-retains-whole-reservation-before-replay");
        expect(state.beforeShutdown, scenario.name, "exact-stream-or-presenter-owner-retained-before-join");
        probe.draft = state.originalCore;
        probe.checkContext = &state;
        probe.checkOwner = [](void* opaque) noexcept
        {
            auto& current = *static_cast<State*>(opaque);
            auto& application = current.owner;
            // The accepted discard source cancelled its exact opening; other live cases retain it for shutdown.
            const bool expectedLive = current.scenario.live && current.scenario.phase != Phase::Detached;
            current.coreAliveAtDetach = application._session.PendingBrushOpening().has_value() == expectedLive &&
                                        (!expectedLive || !current.originalCore.expired());
            current.producerClosedAtDetach = !application._canvasBrushInput.Busy() &&
                                             !application._hostSourceRegistered && !application._canvasLifetime &&
                                             !application._activeHostRequest && application._hostRequests.Size() == 0 &&
                                             application._hostInputBudget.Used() == 0;
        };
        owner->_shutdownTestProbe = &probe;
        owner->Shutdown();
        expect(owner->_shutdownComplete && !owner->_window && !owner->_chrome && !owner->_rendererReady, scenario.name,
               "actual-shutdown-completes-after-callback-unwinds");
        if (brush.Read() != ContinuationReceipt::Result{HostOutcome::Cancelled, E_ABORT})
            std::fprintf(stderr, "Shutdown receipt %s selected=%u outcome=%u error=%08X\n", scenario.name,
                         state.selectedOriginal ? 1u : 0u, static_cast<unsigned int>(brush.Read().outcome),
                         static_cast<unsigned int>(brush.Read().error));
        expect(brush.Read() == ContinuationReceipt::Result{HostOutcome::Cancelled, E_ABORT} &&
                   (scenario.phase == Phase::Stream ||
                    state.dependent.Read().outcome ==
                        (scenario.phase == Phase::Presenter ? HostOutcome::Stale : HostOutcome::Cancelled)),
               scenario.name, "original-and-dependent-receipts-retire-once");
        expect(state.coreAliveAtDetach && state.producerClosedAtDetach && probe.ownerCalls == 1 &&
                   probe.graphicsDetachedOnOwner,
               scenario.name, "admission-closed-before-graphics-detach-live-core-retained");
        expect(probe.workerCalls.load() == 1 && probe.offOwner.load() && probe.mta.load() &&
                   probe.draftRetired.load() && !DeferredReleaseDetail::sink.enqueue &&
                   !owner->_session.PendingBrushOpening(),
               scenario.name, "final-MTA-worker-retires-core-and-sink-after-detach");
        expect(owner->_jobs.WorkerCount() == 0 && !owner->_jobs.WakeHandle() && owner->_jobs.IsIdle() &&
                   owner->_jobs.ReservedMemoryBytes() == 0 && owner->_jobs.ReleasingMemoryBytes() == 0 &&
                   !owner->_retryWait.HasPending() && !owner->_gpuWork.HasPending() && owner->_gpuWork.LentCount() == 0,
               scenario.name, "all-producers-events-and-reservations-retired");
        expect(owner->QueueHostInput(HostCloseProjectInput{}).Read() ==
                   ContinuationReceipt::Result{HostOutcome::Failed, E_ABORT},
               scenario.name, "later-admission-refused-with-exact-error");
        owner->Shutdown();
        expect(owner->_shutdownComplete && probe.ownerCalls == 1 && probe.workerCalls.load() == 1, scenario.name,
               "repeated-shutdown-does-not-repeat-retirement-hooks");
        owner->_prompts = &QuietPrompts();
        owner->_shutdownTestProbe = nullptr;
        owner.reset();
        // As in the existing standalone shutdown fixtures, only this isolated host existed on this thread.
        // Its DestroyWindow-posted quit must not stop the next owner's SettleJobs.
        bool consumedQuit = false;
        for (;;)
        {
            const auto drained = PumpMessages();
            consumedQuit = consumedQuit || drained.quit;
            if (!drained.quit)
                break;
        }
        expect(consumedQuit, scenario.name, "fixture-quit-consumed-before-next-application");
    }
    std::printf("canvas-brush-lifetime-shutdown summary: %u checks, %u failures\n", checks, failures);
    return checks == 104 && failures == 0 ? 0 : 5;
}
