#include "Application.h"
#include "ApplicationShutdownTestProbe.h"

#include "BrushStroke.h"
#include "ChromeHost.h"
#include "DeferredRelease.h"
#include "DocumentLifecycleTestGate.h"
#include "resource.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <memory>

int Application::RunStandaloneShutdownSelfTest(HINSTANCE instance) noexcept
{
    (void)std::setvbuf(stdout, nullptr, _IONBF, 0);
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
        ApplicationShutdownTestProbe probe;
        probe.draft = draft;
        owner->_shutdownTestProbe = &probe;
        owner->Shutdown();
        expect(owner->_shutdownComplete && !owner->_window && !owner->_chrome && !owner->_rendererReady,
               "actual-application-shutdown-complete", after);
        expect(draft.expired(), "complete-session-cpu-graph-retired-before-shutdown-ack", after);
        expect(probe.ownerCalls == 1 && probe.draftAliveAtDetach && probe.graphicsDetachedOnOwner &&
                   probe.reservationAndWakeAlive,
               "owner-detaches-graphics-after-join-with-cpu-and-reservation-alive", after);
        expect(probe.workerCalls.load() == 1 && probe.mta.load() && probe.offOwner.load() && probe.draftRetired.load(),
               "whole-session-retired-once-on-final-mta-worker", after);
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
    return failures ? 5 : 0;
}
