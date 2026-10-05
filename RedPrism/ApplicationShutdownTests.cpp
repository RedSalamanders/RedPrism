#include "Application.h"
#include "ApplicationShutdownTestProbe.h"

#include "BrushStroke.h"
#include "ChromeHost.h"
#include "ColorWorkTestGate.h"
#include "DeferredRelease.h"
#include "DocumentLifecycleTestGate.h"
#include "UiBenchmark.h"
#include "resource.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>

int Application::RunStandaloneShutdownSelfTest(HINSTANCE instance) noexcept
{
    unsigned int checks = 0;
    int failures = 0;
    const auto expect = [&](bool condition, const char* name, bool after) noexcept
    {
        ++checks;
        failures += condition ? 0 : 1;
        std::printf("%s application-shutdown.%s.%s\n", condition ? "PASS" : "FAIL", after ? "after" : "before", name);
    };
    for (const bool after : std::array{false, true})
    {
        expect(!DeferredReleaseDetail::sink.enqueue, "no-surrogate-outer-sink", after);
        APTTYPE original{};
        APTTYPEQUALIFIER originalQualifier{};
        const HRESULT originalApartment = CoGetApartmentType(&original, &originalQualifier);
        auto owner = std::make_unique<Application>(instance, true, nullptr, 0);
        const auto shutdown = wil::scope_exit(
            [&]() noexcept
            {
                if (owner)
                    owner->Shutdown();
            });
        owner->_quietUi = true;
        owner->_prompts = &QuietPrompts();
        if (FAILED(owner->InitializeProcessing()) || FAILED(owner->RegisterWindowClass()) ||
            FAILED(owner->CreateMainWindow(false)) || FAILED(owner->InitializeRenderer()) ||
            FAILED(owner->InitializeChrome()))
        {
            SelfTestFailure(L"Standalone shutdown fixture could not initialize its actual hidden WARP host.\n");
            return 5;
        }
        owner->BindCompositionDevice();
        expect(DeferredReleaseDetail::sink.enqueue && owner->_jobs.WorkerCount() != 0,
               "actual-application-owns-sink-and-pool", after);
        expect(owner->_renderer.IsWarp() && owner->_brushEngine.Ready() && owner->_window && owner->_chrome,
               "actual-hidden-graphics-and-chrome-alive", after);
        owner->_session.CreateDocument(2048, 2048, true);
        const Guid layer = InsertPaintedLayer(owner->_session, L"Retained pixels", 2048, 2048, {}, 0, 0xFF7F3F1Fu);
        expect(owner->_session.ActiveLayer() && owner->_session.ActiveLayer()->id == layer &&
                   owner->_session.ActiveLayer()->asset &&
                   owner->_session.ActiveLayer()->asset->image.ByteSize() == 16ull * 1024 * 1024 &&
                   owner->_session.history.CanUndo(),
               "real-large-document-and-history", after);
        expect(owner->_session.CopySelection() && owner->_session.Clipboard() &&
                   owner->_session.Clipboard()->image.HasStorage() &&
                   owner->_session.Clipboard()->image.Width() == 2048 &&
                   owner->_session.Clipboard()->image.Height() == 2048,
               "private-clipboard-owner-retained-without-os-transfer", after);
        auto gate = std::make_shared<DocumentLifecycleTestGate>();
        gate->afterPreparation = after;
        owner->_documentLifecycleTestGate = gate;
        owner->OnCommand(ChromeHost::kCommandNew);
        const ULONGLONG deadline = GetTickCount64() + 10000;
        while (gate->visits.load() == 0)
        {
            MSG message{};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
            {
                if (message.message != DocumentLifecycleTestGate::kMessage)
                    owner->DispatchQueuedMessage(message);
            }
            (void)DrainCompletions(owner->_jobs);
            (void)RunUiActions(owner->_jobs);
            if (gate->visits.load() != 0)
                break;
            const ULONGLONG now = GetTickCount64();
            if (now >= deadline)
            {
                SelfTestFailure(L"Standalone lifecycle shutdown fixture did not reach its event gate.\n");
                std::_Exit(5);
            }
            const HANDLE wake = static_cast<HANDLE>(owner->_jobs.WakeHandle());
            if (MsgWaitForMultipleObjectsEx(1, &wake, static_cast<DWORD>(deadline - now), QS_ALLINPUT,
                                            MWMO_INPUTAVAILABLE) == WAIT_FAILED)
                std::_Exit(5);
        }
        expect(owner->_documentLifecycleTicket && owner->_documentLifecycleStamp && owner->_historyWork.Busy() &&
                   gate->captureReservationHeld.load() && owner->_jobs.ReservedMemoryBytes() != 0,
               "lifecycle-pending-with-complete-reservation", after);
        // Add a live graphics-backed draft after reaching the immutable job gate. Shutdown suppresses publication
        // regardless of later session authority; this owner must remain alive through producer acknowledgement.
        const bool gradient = owner->_session.BeginGradient({0, 0});
        std::weak_ptr<BrushStroke> draft;
        if (owner->_session.PendingGradient())
            draft = owner->_session.PendingGradient()->raster;
        expect(gradient && !draft.expired(), "graphics-backed-draft-alive-before-shutdown", after);
        Raster rootStorage = Raster::Make(2048, 2048, RasterFormat::Bgra8888);
        owner->_drag = ColorSampling{0xFF123456u, rootStorage, Vec2{2, 3}};
        owner->_overlaySelection =
            DocumentSelection{false, SelectionLoops{SelectionLoop{Vec2{4, 5}, Vec2{44, 5}, Vec2{44, 35}, Vec2{4, 35}}}};
        owner->_uiBenchmark = std::make_unique<UiBenchmarkState>();
        const CanvasDocument& currentDocument = owner->_session.Document().value();
        owner->_uiBenchmark->fixture = currentDocument;
        owner->_uiBenchmark->imageImportBaseline = currentDocument;
        owner->_uiBenchmark->previousImage = Raster::Make(2048, 2048, RasterFormat::Bgra8888);
        owner->_uiBenchmark->previousPreview = Raster::Make(2048, 2048, RasterFormat::Bgra8888);
        owner->_uiBenchmark->complete = true;
        owner->_uiBenchmark->failed = false;
        owner->_uiBenchmark->runs = 7;
        owner->_uiBenchmark->ownsSaveDirectory = false;
        UiBenchmarkState* const benchmarkIdentity = owner->_uiBenchmark.get();
        UiTimingStats* const benchmarkTimingIdentity = &owner->_uiBenchmark->timing;
        const ColorSampling* const sampling = std::get_if<ColorSampling>(&owner->_drag);
        expect(sampling && sampling->composite && sampling->composite->Identity() == rootStorage.Identity() &&
                   rootStorage.ByteSize() == 16ull * 1024 * 1024 && rootStorage.PixelCount() == 2048 * 2048 &&
                   sampling->point == Vec2{2, 3},
               "sampling-root-identity-size-count-and-point-before-shutdown", after);
        expect(owner->_overlaySelection && !owner->_overlaySelection->IsEmpty() &&
                   owner->_overlaySelection->loops.PointCount() == 4,
               "explicit-overlay-selection-before-shutdown", after);
        expect(owner->_uiBenchmark && owner->_uiBenchmark.get() == benchmarkIdentity &&
                   owner->_uiBenchmark->fixture == currentDocument &&
                   owner->_uiBenchmark->imageImportBaseline == currentDocument &&
                   owner->_uiBenchmark->previousImage.HasStorage() &&
                   owner->_uiBenchmark->previousPreview.HasStorage() &&
                   owner->_uiBenchmark->previousImage.Identity() != owner->_uiBenchmark->previousPreview.Identity() &&
                   owner->_uiBenchmark->previousImage.Identity() != rootStorage.Identity() &&
                   owner->_uiBenchmark->previousPreview.Identity() != rootStorage.Identity() &&
                   owner->_uiBenchmark->previousImage.ByteSize() == 16ull * 1024 * 1024 &&
                   owner->_uiBenchmark->previousPreview.ByteSize() == 16ull * 1024 * 1024 &&
                   owner->_uiBenchmark->complete && !owner->_uiBenchmark->failed && owner->_uiBenchmark->runs == 7 &&
                   !owner->_uiBenchmark->ownsSaveDirectory,
               "benchmark-roots-and-complete-metadata-before-shutdown", after);
        // The fixture must not retain the last pixel owner across Shutdown. Identity/size checks above finish
        // while the temporary exists; the ColorSampling variant owns its sole raster reference from here.
        rootStorage = {};
        ApplicationShutdownTestProbe probe;
        probe.draft = draft;
        owner->_shutdownTestProbe = &probe;
        owner->Shutdown();
        expect(owner->_shutdownComplete && !owner->_window && !owner->_chrome && !owner->_rendererReady,
               "actual-application-shutdown-complete", after);
        expect(std::holds_alternative<std::monostate>(owner->_drag) && !owner->_overlaySelection,
               "host-drag-and-outline-roots-retired-after-shutdown", after);
        expect(owner->_uiBenchmark && owner->_uiBenchmark.get() == benchmarkIdentity &&
                   &owner->_uiBenchmark->timing == benchmarkTimingIdentity && owner->_uiBenchmark->complete &&
                   !owner->_uiBenchmark->failed && owner->_uiBenchmark->runs == 7 &&
                   !owner->_uiBenchmark->ownsSaveDirectory && owner->_uiBenchmark->fixture.layers.empty() &&
                   owner->_uiBenchmark->imageImportBaseline.layers.empty() &&
                   !owner->_uiBenchmark->previousImage.HasStorage() &&
                   !owner->_uiBenchmark->previousPreview.HasStorage(),
               "benchmark-metadata-preserved-and-document-image-roots-retired-after-shutdown", after);
        expect(draft.expired(), "complete-session-cpu-graph-retired-before-shutdown-ack", after);
        expect(probe.ownerCalls == 1 && probe.draftAliveAtDetach && probe.graphicsDetachedOnOwner &&
                   probe.reservationAndWakeAlive,
               "owner-detaches-graphics-after-join-with-cpu-and-reservation-alive", after);
        expect(probe.workerCalls.load() == 1 && probe.mta.load() && probe.offOwner.load() && probe.draftRetired.load(),
               "whole-session-retired-once-on-final-mta-worker", after);
        expect(probe.workerCalls.load() == 1 && probe.mta.load() && probe.offOwner.load() &&
                   probe.hostCpuRootsRetired.load(),
               "host-cpu-roots-retired-on-acknowledged-final-mta-worker", after);
        expect(!owner->_session.Clipboard() || !owner->_session.Clipboard()->image.HasStorage(),
               "private-clipboard-pixels-retired-with-session", after);
        expect(gate->retired.load() == 1 && gate->retiredOffOwner.load(), "job-payload-retired-once-off-owner", after);
        expect(!owner->_documentLifecycleTicket && !owner->_documentLifecycleStamp && !owner->_activeHostRequest &&
                   !owner->_historyWork.Busy(),
               "tickets-input-and-history-barrier-acknowledged", after);
        expect(owner->_jobs.IsIdle() && owner->_jobs.WorkerCount() == 0 && !owner->_jobs.WakeHandle() &&
                   owner->_jobs.ReservedMemoryBytes() == 0 && owner->_jobs.ReleasingMemoryBytes() == 0 &&
                   !DeferredReleaseDetail::sink.enqueue,
               "pool-reservations-events-and-sink-retired", after);
        APTTYPE apartment{};
        APTTYPEQUALIFIER qualifier{};
        expect(SUCCEEDED(originalApartment) && SUCCEEDED(CoGetApartmentType(&apartment, &qualifier)) &&
                   apartment == original && qualifier == originalQualifier,
               "caller-apartment-preserved", after);
        owner->Shutdown();
        expect(gate->retired.load() == 1 && owner->_shutdownComplete, "second-shutdown-is-idempotent", after);
        expect(probe.ownerCalls == 1 && probe.workerCalls.load() == 1, "second-shutdown-cannot-repeat-retirement-hooks",
               after);
        expect(owner->_uiBenchmark && owner->_uiBenchmark.get() == benchmarkIdentity &&
                   &owner->_uiBenchmark->timing == benchmarkTimingIdentity && owner->_uiBenchmark->complete &&
                   !owner->_uiBenchmark->failed && owner->_uiBenchmark->runs == 7 &&
                   !owner->_uiBenchmark->ownsSaveDirectory && owner->_uiBenchmark->fixture.layers.empty() &&
                   owner->_uiBenchmark->imageImportBaseline.layers.empty() &&
                   !owner->_uiBenchmark->previousImage.HasStorage() &&
                   !owner->_uiBenchmark->previousPreview.HasStorage() && probe.workerCalls.load() == 1,
               "second-shutdown-preserves-empty-benchmark-and-single-callback", after);
        owner->_shutdownTestProbe = nullptr;
        owner.reset();
        // DestroyWindow posts this fixture's WM_QUIT. Drain the full queue so pending window messages cannot
        // delay that quit until the next owner's SettleJobs; no application or its borrowed owners remain.
        bool consumedQuit = false;
        for (;;)
        {
            const auto drained = PumpMessages();
            consumedQuit = consumedQuit || drained.quit;
            if (!drained.quit)
                break;
        }
        expect(consumedQuit, "fixture-quit-consumed-before-next-application", after);
    }
    std::printf("application-shutdown summary: %u checks, %d failures\n", checks, failures);
    if (failures)
        return 5;
    const int colorResult = RunStandaloneColorDisplayShutdownSelfTest(instance);
    return colorResult ? colorResult : RunStandaloneSamplerShutdownSelfTest(instance);
}

int Application::RunStandaloneSamplerShutdownSelfTest(HINSTANCE instance) noexcept
{
    enum class Phase : std::uint8_t
    {
        BeforeCapture,
        LivePrefix,
        MapRecovery
    };
    struct Case final
    {
        const char* name;
        Phase phase;
    };
    constexpr std::array cases{Case{"before-capture", Phase::BeforeCapture}, Case{"live-prefix", Phase::LivePrefix},
                               Case{"map-recovery", Phase::MapRecovery}};
    unsigned int checks = 0;
    unsigned int failures = 0;
    const auto expect = [&checks, &failures](bool condition, const char* scenario, const char* name) noexcept
    {
        ++checks;
        failures += condition ? 0 : 1;
        std::printf("%s sampler-shutdown.%s.%s\n", condition ? "PASS" : "FAIL", scenario, name);
    };
    for (const auto& scenario : cases)
    {
        struct Observation final
        {
            Application* owner = nullptr;
            DocumentReadAuthority source;
            bool joined = false;
            bool sourcePreservedAtDetach = false;
        } observation;
        ApplicationShutdownTestProbe probe;
        std::uint32_t captures = 0;
        auto owner = std::make_unique<Application>(instance, true, nullptr, 0);
        const auto shutdown = wil::scope_exit(
            [&owner]() noexcept
            {
                if (owner)
                    owner->Shutdown();
            });
        owner->_quietUi = true;
        owner->_prompts = &QuietPrompts();
        if (DeferredReleaseDetail::sink.enqueue || FAILED(owner->InitializeProcessing()) ||
            FAILED(owner->RegisterWindowClass()) || FAILED(owner->CreateMainWindow(false)) ||
            FAILED(owner->InitializeRenderer()) || FAILED(owner->InitializeChrome()))
            return 5;
        owner->BindCompositionDevice();
        const HWND originalWindow = owner->_window.get();
        owner->_session.CreateDocument(2048, 2048, true);
        const Guid layerId =
            InsertPaintedLayer(owner->_session, L"Sampler shutdown source", 2048, 2048, {}, 0, 0xFF3366CCu);
        owner->_session.history.Reset();
        owner->_session.SetForegroundColor(0xFF804020u);
        owner->_tool = EditorTool::Eyedropper;
        owner->_viewport.Fit(owner->DocumentSize());
        owner->DocumentChanged();
        if (SettleJobs(owner->_jobs, 10000, DispatchMessageThunk, owner.get()) != SettleResult::Settled)
            return 5;
        const auto authority = owner->_session.CaptureDocumentReadAuthority();
        if (!authority || !owner->_session.ActiveLayer() || owner->_session.ActiveLayer()->id != layerId ||
            !owner->_session.ActiveLayer()->asset ||
            owner->_session.ActiveLayer()->asset->image.ByteSize() != 16ull * 1024 * 1024)
            return 5;
        observation.owner = owner.get();
        observation.source = authority.value();
        const auto originalRevision = owner->_session.history.CurrentRevision();
        wil::unique_handle acknowledged{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
        if (!acknowledged)
            return 5;
        owner->_samplerSourceCapturesForTest = &captures;
        owner->_samplerAcknowledgedForTest = acknowledged.get();
        const auto clearBorrowedHooks = wil::scope_exit(
            [&owner]() noexcept
            {
                if (owner)
                {
                    owner->_samplerSourceCapturesForTest = nullptr;
                    owner->_samplerAcknowledgedForTest = nullptr;
                    owner->_shutdownTestProbe = nullptr;
                }
            });
        if (scenario.phase == Phase::MapRecovery)
        {
            owner->_gpuWork.SetMapBusyForTest(1);
            owner->_gpuWork.HoldMapRecoveryForTest(true);
        }
        const auto hole = owner->_chrome->HolePixels();
        const auto view = owner->_viewport.ViewFromDocument({1024, 1024}, owner->DocumentSize());
        owner->OnPointer(WM_LBUTTONDOWN, MK_LBUTTON,
                         MAKELPARAM(static_cast<int>(std::lround(hole.left + view.x)),
                                    static_cast<int>(std::lround(hole.top + view.y))));
        const auto* sampling = std::get_if<ColorSampling>(&owner->_drag);
        const auto openingId = sampling && sampling->opening ? sampling->opening.value() : 0;
        const auto* opening = owner->FindSamplerOpening(openingId);
        if (!opening)
            return 5;
        const auto receipt = opening->receipt;
        (void)RunUiActions(owner->_jobs);
        bool reached = false;
        if (scenario.phase == Phase::BeforeCapture)
            reached = owner->_samplerHostTicket && owner->_samplerHostJob && captures == 0;
        else if (scenario.phase == Phase::LivePrefix)
            reached = WaitForColorWorkTestEvent(owner->_jobs, acknowledged.get(), DispatchMessageThunk, owner.get()) &&
                      owner->_session.ForegroundColor() == 0xFF3366CCu && owner->_gpuWork.LentCount() == 0 &&
                      owner->_gpuWork.InFlightCount() == 0 && owner->_composition.UtilitySurfaceBytes() == 0 &&
                      !RunUiActions(owner->_jobs) && !owner->_jobs.HasUiWork();
        else
        {
            const auto deadline = GetTickCount64() + 10000;
            for (;;)
            {
                const auto messages = PumpMessages(DispatchMessageThunk, owner.get());
                if (messages.quit)
                {
                    PostQuitMessage(messages.exitCode);
                    break;
                }
                (void)DrainCompletions(owner->_jobs);
                (void)RunUiActions(owner->_jobs);
                if (owner->_gpuWork.MapAttemptsForTest() == 1 && owner->_gpuWork.InFlightCount() == 1 &&
                    owner->_gpuWork.LentCount() == 0 && !owner->_gpuWork.HasReady())
                {
                    reached = true;
                    break;
                }
                const auto now = GetTickCount64();
                if (now >= deadline || !owner->_jobs.WakeHandle())
                    break;
                const HANDLE wake = static_cast<HANDLE>(owner->_jobs.WakeHandle());
                if (MsgWaitForMultipleObjectsEx(1, &wake, static_cast<DWORD>(deadline - now), QS_ALLINPUT,
                                                MWMO_INPUTAVAILABLE) == WAIT_FAILED)
                    break;
            }
        }
        const auto expectedCaptures = scenario.phase == Phase::BeforeCapture ? 0u : 1u;
        expect(reached && captures == expectedCaptures && owner->_samplerHostTicket && owner->_samplerHostStamp &&
                   owner->_historyWork.Busy() && owner->_jobs.ReservedMemoryBytes() != 0 &&
                   receipt.Read().outcome == HostOutcome::Pending &&
                   owner->_session.IsDocumentReadCurrent(authority.value()) &&
                   owner->_session.history.CurrentRevision() == originalRevision &&
                   owner->_session.history.UndoCount() == 0,
               scenario.name, "original-live-record-source-and-reservation-held");
        if (!reached)
            return 5;
        probe.checkContext = &observation;
        probe.checkJoined = [](void* opaque) noexcept
        {
            auto& state = *static_cast<Observation*>(opaque);
            auto& app = *state.owner;
            state.joined = !app._samplerHostJob && !app._hostSourceRegistered && !app._activeHostRequest &&
                           app._hostRequests.Size() == 0 && app._hostInputBudget.Used() == 0 && app._samplerState &&
                           std::all_of(app._samplerState->openings.begin(), app._samplerState->openings.end(),
                                       [](const SamplerOpening& slot)
                                       { return !slot.opening && !slot.jobAlive && !slot.consumers; });
        };
        probe.checkOwner = [](void* opaque) noexcept
        {
            auto& state = *static_cast<Observation*>(opaque);
            const auto& app = *state.owner;
            state.sourcePreservedAtDetach = app._session.IsDocumentReadCurrent(state.source) &&
                                            std::holds_alternative<std::monostate>(app._drag) &&
                                            app._gpuWork.LentCount() == 0 && app._gpuWork.InFlightCount() == 0;
        };
        owner->_shutdownTestProbe = &probe;
        owner->Shutdown();
        expect(owner->_shutdownComplete && !owner->_window && !owner->_chrome && !owner->_rendererReady &&
                   receipt.Read() == ContinuationReceipt::Result{HostOutcome::Cancelled, E_ABORT} &&
                   captures == expectedCaptures && !owner->_samplerHostTicket && !owner->_samplerHostStamp &&
                   !owner->_samplerHostJob && !owner->_historyWork.Busy() && observation.joined,
               scenario.name, "actual-shutdown-acknowledges-original-receipt-and-stamp");
        expect(observation.sourcePreservedAtDetach && probe.ownerCalls == 1 && probe.graphicsDetachedOnOwner &&
                   probe.workerCalls.load() == 1 && probe.offOwner.load() && probe.mta.load() &&
                   probe.hostCpuRootsRetired.load() && !owner->_session.Document() &&
                   !owner->_session.history.CanUndo(),
               scenario.name, "source-survives-join-and-retires-with-host-roots-on-MTA");
        expect(owner->_jobs.IsIdle() && owner->_jobs.WorkerCount() == 0 && !owner->_jobs.WakeHandle() &&
                   owner->_jobs.ReservedMemoryBytes() == 0 && owner->_jobs.ReleasingMemoryBytes() == 0 &&
                   !owner->_gpuWork.HasPending() && owner->_composition.UtilitySurfaceBytes() == 0 &&
                   !DeferredReleaseDetail::sink.enqueue && GetCapture() != originalWindow,
               scenario.name, "no-producer-reservation-device-or-retirement-owner-remains");
        owner->Shutdown();
        expect(probe.ownerCalls == 1 && probe.workerCalls.load() == 1 && owner->_shutdownComplete, scenario.name,
               "second-shutdown-is-idempotent");
        owner->_samplerSourceCapturesForTest = nullptr;
        owner->_samplerAcknowledgedForTest = nullptr;
        owner->_shutdownTestProbe = nullptr;
        owner.reset();
        bool quit = false;
        for (;;)
        {
            const auto messages = PumpMessages();
            quit = quit || messages.quit;
            if (!messages.quit)
                break;
        }
        expect(quit, scenario.name, "fixture-quit-consumed-before-next-owner");
    }
    std::printf("sampler-shutdown summary: %u checks, %u failures\n", checks, failures);
    return failures ? 5 : 0;
}
