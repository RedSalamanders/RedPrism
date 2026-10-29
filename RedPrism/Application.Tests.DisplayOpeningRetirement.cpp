#include "Application.h"
#include "Runtime.Tests.ApplicationProcessingTestProbe.h"
#include "Tests/Support/Support.Tests.DocumentFixture.h"

#include <cstdio>
#include <memory>
#include <new>

int Application::RunStandaloneDisplayOpeningRetirementSelfTest(HINSTANCE instance) noexcept
{
    unsigned checks = 0, failures = 0;
    unsigned scenario = 0;
    const auto expect = [&](bool condition, const char* name) noexcept
    {
        ++checks;
        failures += condition ? 0u : 1u;
        std::printf("%s application-display-opening-retirement.%u.%s\n", condition ? "PASS" : "FAIL", scenario, name);
        return condition;
    };
    auto summary = wil::scope_exit(
        [&]() noexcept
        { std::printf("SUMMARY application-display-opening-retirement checks=%u failures=%u\n", checks, failures); });
    for (; scenario < 3; ++scenario)
    {
        ApplicationProcessingTestProbe probe;
        probe.workerCount = 2;
        probe.memory = {[](void*, MemorySnapshot& out) noexcept
                        {
                            out = {8ull * 1024 * 1024 * 1024, 8ull * 1024 * 1024 * 1024};
                            return true;
                        },
                        nullptr};
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
                        SUCCEEDED(owner->InitializeChrome()) && owner->EnableCommittedDisplayForTest(),
                    "real-hidden-warp-window-and-display-owner-initialize"))
            return 5;
        const auto settle = [&]() noexcept
        { return SettleJobs(owner->_jobs, 15000, DispatchMessageThunk, owner.get()) == SettleResult::Settled; };
        if (!expect(settle(), "initial-empty-host-settles-before-exact-display-opening"))
            return 5;
        DocumentFixture fixture;
        fixture.width = fixture.height = 4;
        fixture.layers.push_back(ImageLayer::Blank(L"Opening retirement", Vec2{4, 4}));
        const auto active = fixture.layers[0].id;
        owner->_session.ReplaceDocument(std::move(fixture).Seal(), active);
        owner->DocumentChanged();
        const auto deadline = GetTickCount64() + 15000;
        // Do not drain completion nodes. Once the first original display opening is admitted, its actual
        // completed node is published AFTER RetireWorkerStage. The following owner action therefore
        // exercises cancellation/staleness/Close after the worker hook, before DeferRetirement consumes it.
        // Invoke this real owner leaf directly. DrainCompletions also pumps it, but can consume the
        // newly completed opening in that same pass, which would miss the boundary under test.
        owner->_committedDisplay.Pump();
        if (!expect(owner->_committedDisplay.HasPending() && !owner->_acknowledgedCommittedDisplay &&
                        owner->_jobs.ReservedMemoryBytes() != 0,
                    "one-original-display-opening-admitted-with-complete-reservation"))
            return 5;
        while (owner->_jobs.UiReadinessForTest().completions == 0)
        {
            const auto now = GetTickCount64();
            if (now >= deadline || WaitForSingleObject(static_cast<HANDLE>(owner->_jobs.WakeHandle()),
                                                       static_cast<DWORD>(deadline - now)) != WAIT_OBJECT_0)
                break;
        }
        if (!expect(owner->_jobs.UiReadinessForTest().completions == 1 && !owner->_acknowledgedCommittedDisplay,
                    "actual-opening-worker-hook-joined-before-undrained-owner-completion"))
            return 5;
        if (scenario == 0)
        {
            owner->_committedDisplay.SetSuspendedOwner(true);
            if (!expect(settle() && !owner->_acknowledgedCommittedDisplay && !owner->_committedDisplay.HasPending() &&
                            owner->_jobs.ReservedMemoryBytes() == 0 && owner->_jobs.ReleasingMemoryBytes() == 0,
                        "late-cancellation-retires-empty-root-on-worker-before-original-refund"))
                return 5;
        }
        else if (scenario == 2)
        {
            owner->_session.ClearDocument();
            owner->DocumentChanged();
            if (!expect(settle() && !owner->_acknowledgedCommittedDisplay && !owner->_committedDisplay.HasPending() &&
                            owner->_jobs.ReservedMemoryBytes() == 0 && owner->_jobs.ReleasingMemoryBytes() == 0,
                        "stale-source-retires-empty-root-without-publication"))
                return 5;
        }
        owner->Shutdown();
        if (!expect(owner->_shutdownComplete && owner->_jobs.IsIdle() && !owner->_acknowledgedCommittedDisplay &&
                        !owner->_committedDisplay.HasPending() && owner->_jobs.ReservedMemoryBytes() == 0 &&
                        owner->_jobs.ReleasingMemoryBytes() == 0,
                    "real-joined-close-retires-undrained-or-cancelled-root-before-refund"))
            return 5;
    }
    return failures ? 5 : 0;
}
