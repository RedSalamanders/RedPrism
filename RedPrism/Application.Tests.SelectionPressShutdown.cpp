#include "Application.h"
#include "ChromeHost.h"
#include "Runtime.Tests.ApplicationProcessingTestProbe.h"
#include "SharedPublicationMemory.h"
#include "Tests/Support/Support.Tests.DocumentFixture.h"

#include <cmath>
#include <cstdio>
#include <memory>
#include <new>

namespace
{
constexpr std::uint64_t kObserverBytes = 8192;
// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4626 5027)
class ShutdownEndpointJob final : public Job
{
  public:
    ShutdownEndpointJob(JobScheduler& scheduler, std::shared_ptr<SharedPublicationMemory>& output)
        : _scheduler(scheduler), _output(output)
    {
    }
    JobResult Run(JobContext&) override
    {
        _memory = SharedPublicationMemory::CreateForWork(kObserverBytes);
        return _memory ? JobResult{} : JobResult{JobStatus::Failed, E_OUTOFMEMORY};
    }
    bool DeferRetirement(JobResult, JobTicket ticket) noexcept override
    {
        _ticket = ticket;
        return false;
    }
    void Completed(JobResult result) noexcept override
    {
        if (result.status == JobStatus::Finished && _memory &&
            _memory->BindAfterCompletedWork(_scheduler, _ticket) == S_OK)
            _output = std::move(_memory);
    }

  private:
    JobScheduler& _scheduler;
    std::shared_ptr<SharedPublicationMemory>& _output;
    std::shared_ptr<SharedPublicationMemory> _memory;
    JobTicket _ticket;
};
#pragma warning(pop)

} // namespace
int Application::RunStandaloneSelectionPressShutdownSelfTest(HINSTANCE instance) noexcept
{
    unsigned checks = 0, failures = 0;
    const char* scenario = "setup";
    const auto expect = [&](bool value, const char* label) noexcept
    {
        ++checks;
        failures += value ? 0u : 1u;
        std::printf("%s application-selection-press-shutdown.%s.%s\n", value ? "PASS" : "FAIL", scenario, label);
        return value;
    };
    auto summary = wil::scope_exit(
        [&]() noexcept
        { std::printf("SUMMARY application-selection-press-shutdown checks=%u failures=%u\n", checks, failures); });
    for (unsigned mode = 0; mode < 2; ++mode)
    {
        scenario = mode == 0 ? "queued" : "waiting";
        ApplicationProcessingTestProbe probe;
        probe.workerCount = 2;
        std::shared_ptr<SharedPublicationMemory> endpoint; // Outlives cleanup and every accepted callback.
        auto owner =
            std::unique_ptr<Application>(new (std::nothrow) Application(instance, true, L"", 0, nullptr, nullptr));
        auto cleanup = wil::scope_exit(
            [&]() noexcept
            {
                if (owner)
                {
                    owner->Shutdown();
                    owner.reset();
                    while (PumpMessages().quit)
                    {
                    }
                }
            });
        if (!expect(bool(owner), "real-owner-allocated"))
            return 5;
        owner->_processingTestProbe = &probe;
        owner->_quietUi = true;
        owner->_prompts = &QuietPrompts();
        if (!expect(SUCCEEDED(owner->InitializeProcessing()) && SUCCEEDED(owner->RegisterWindowClass()) &&
                        SUCCEEDED(owner->CreateMainWindow(false)) && SUCCEEDED(owner->InitializeRenderer()) &&
                        SUCCEEDED(owner->InitializeChrome()) && owner->EnableCommittedDisplayForTest() &&
                        owner->EnableSelectionOutlineForTest(),
                    "real-hidden-owner-and-display-created"))
            return 5;
        owner->_selectionPressTestEnabled = true;
        const auto settle = [&]() noexcept
        { return SettleJobs(owner->_jobs, 30000, DispatchMessageThunk, owner.get()) == SettleResult::Settled; };
        const auto until = [&](auto done) noexcept
        {
            const auto deadline = GetTickCount64() + 10000;
            while (!done())
            {
                if (PumpMessages(DispatchMessageThunk, owner.get()).quit)
                    return false;
                (void)DrainCompletions(owner->_jobs);
                (void)RunUiActions(owner->_jobs);
                if (done())
                    return true;
                const auto now = GetTickCount64();
                if (now >= deadline)
                    return false;
                const HANDLE wake = owner->_jobs.WakeHandle();
                if (MsgWaitForMultipleObjectsEx(1, &wake, static_cast<DWORD>(deadline - now), QS_ALLINPUT,
                                                MWMO_INPUTAVAILABLE) == WAIT_FAILED)
                    return false;
            }
            return true;
        };
        DocumentFixture fixture;
        fixture.width = 64;
        fixture.height = 64;
        fixture.layers.push_back(ImageLayer::Blank(L"Shutdown press", {64, 64}));
        fixture.selection = DocumentSelection{false, SelectionLoops{SelectionLoop{{8, 8}, {40, 8}, {40, 40}, {8, 40}}}};
        const auto active = fixture.layers[0].id;
        owner->_session.ReplaceDocument(std::move(fixture).Seal(), active);
        owner->_tool = EditorTool::Marquee;
        owner->_session.marqueeKind = LassoKind::Rectangle;
        owner->_session.selectionModeChoice = SelectionMode::New;
        owner->_session.selectionAntialiased = false;
        owner->_viewport.zoom = 1.0;
        owner->DocumentChanged();
        if (!expect(settle(), "fixture-and-first-display-settled"))
            return 5;
        owner->RenderFrame();
        std::unique_ptr<Job> observer = std::make_unique<ShutdownEndpointJob>(owner->_jobs, endpoint);
        JobTicket observerTicket;
        const auto admitted = owner->_jobs.Submit(observer,
                                                  {.kind = JobClass::Selection,
                                                   .memoryBytes = kObserverBytes,
                                                   .deferredRetirement = true,
                                                   .allowMemoryWaiting = false,
                                                   .reserveRetainedMemory = true},
                                                  observerTicket);
        if (!expect(admitted == S_OK && settle() && endpoint && endpoint->Bytes() == kObserverBytes,
                    "independent-observer-is-retained"))
            return 5;
        const auto send = [&](UINT message, Vec2 document, WPARAM modifiers) noexcept
        {
            const auto view = owner->_viewport.ViewFromDocument(document, owner->DocumentSize());
            const auto hole = owner->_chrome->HolePixels();
            const auto x = static_cast<int>(std::lround(hole.left + view.x));
            const auto y = static_cast<int>(std::lround(hole.top + view.y));
            SendMessageW(owner->_window.get(), message, modifiers, MAKELPARAM(x, y));
        };
        send(WM_LBUTTONDOWN, {24, 24}, MK_LBUTTON);
        const auto receipt = owner->_selectionPressReceipt;
        if (!expect(owner->_selectionPressInput.Busy() && receipt.Read().outcome == HostOutcome::Pending &&
                        !owner->_session.IsMovingSelection(),
                    "wndproc-down-records-descriptor-before-work"))
            return 5;
        if (mode == 1)
        {
            send(WM_MOUSEMOVE, {32, 28}, MK_LBUTTON);
            if (!until(
                    [&]() noexcept
                    {
                        return owner->_selectionPressState && owner->_selectionPressState->waiting &&
                               owner->_selectionPressState->moves == 1;
                    }))
                return 5;
        }
        const auto* state = owner->_selectionPressState ? &owner->_selectionPressState.value() : nullptr;
        if (!expect(mode == 0 ? !state && !owner->_selectionPressStamp && !owner->_historyWork.Busy()
                              : state && state->controller && state->opened && state->waiting && state->captures == 1 &&
                                    owner->_historyWork.Busy() && owner->_selectionPressStamp &&
                                    owner->_jobs.ReservedMemoryBytes() >= state->limits.totalBytes + kObserverBytes &&
                                    endpoint->ResidentBytes() >= state->openingBytes + kObserverBytes,
                    "exact-prejoin-queued-or-active-opening-and-reservation"))
            return 5;
        owner->Shutdown();
        if (!expect(owner->_jobs.IsIdle() && receipt.Read().outcome == HostOutcome::Cancelled &&
                        !owner->_selectionPressInput.Busy() && !owner->_selectionPressStamp &&
                        !owner->_historyWork.Busy() && !owner->_activeHostRequest && !owner->_hostCommitStamp,
                    "joined-close-finishes-prefix-receipt-and-exact-history-stamp"))
            return 5;
        if (!expect(mode == 0
                        ? !owner->_selectionPressState
                        : state && !state->controller && !state->completed && state->sameTicket && state->aborted &&
                              state->inputAcknowledged && state->outcome.status == JobStatus::Cancelled,
                    "completion-suppressed-and-original-controller-joined"))
            return 5;
        if (!expect(endpoint->Bytes() == kObserverBytes && endpoint->ResidentBytes() == kObserverBytes,
                    "final-worker-retires-all-charges-except-independent-observer"))
            return 5;
        owner->Shutdown();
        if (!expect(owner->_jobs.IsIdle() && !owner->_historyWork.Busy() && !owner->_selectionPressInput.Busy() &&
                        receipt.Read().outcome == HostOutcome::Cancelled && endpoint->ResidentBytes() == kObserverBytes,
                    "repeated-shutdown-remains-idempotent"))
            return 5;
    }
    scenario = "complete";
    if (!expect(checks == 20 && failures == 0, "all-queued-active-cases-reached"))
        return 5;
    return failures ? 5 : 0;
}
