#include "Application.h"

#include "ChromeHost.h"
#include "KeyboardState.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <new>
#include <vector>

namespace
{
std::uint64_t OutlineFrameCounter() noexcept
{
    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    return static_cast<std::uint64_t>(counter.QuadPart);
}

std::vector<CanvasOverlay::OutlineLoop> TimingContours(unsigned int kind)
{
    std::vector<CanvasOverlay::OutlineLoop> loops;
    if (kind == 0)
        loops = {{{8, 8}, {56, 8}, {56, 56}, {8, 56}}};
    else if (kind == 1)
        loops = {{{8, 8}, {120, 8}, {120, 120}, {8, 120}}, {{32, 32}, {32, 96}, {96, 96}, {96, 32}}};
    else
    {
        // Exactly 32,768 disjoint closed squares, four stored vertices each. All
        // coordinates fit the independently fixed 2048 by 1024 document grid.
        loops.reserve(32768);
        for (unsigned int y = 0; y < 128; ++y)
            for (unsigned int x = 0; x < 256; ++x)
            {
                const float left = static_cast<float>(x * 8 + 1);
                const float top = static_cast<float>(y * 8 + 1);
                loops.push_back({{left, top}, {left + 5, top}, {left + 5, top + 5}, {left, top + 5}});
            }
    }
    return loops;
}
} // namespace

int Application::RunSelectionOutlineTimingSelfTest(HINSTANCE instance) noexcept
{
    const KeyboardState::ScopedSynthetic syntheticKeyboard;
    int failures = 0;
    const auto expect = [&](const char* name, bool condition) noexcept
    {
        std::fprintf(stderr, "%s selection-outline-cost.%s\n", condition ? "PASS" : "FAIL", name);
        failures += condition ? 0 : 1;
        return condition;
    };
    LARGE_INTEGER frequency{};
    if (!expect("actual-performance-counter-frequency",
                QueryPerformanceFrequency(&frequency) && frequency.QuadPart > 0))
        return 5;
    constexpr std::array<const char*, 3> names{"rectangle", "hole", "grid32768"};
    constexpr std::array<std::size_t, 3> contourCounts{1, 2, 32768};
    for (unsigned int kind = 0; kind < names.size(); ++kind)
    {
        std::unique_ptr<Application> owner{new (std::nothrow) Application(instance, true, L"", 96, nullptr, nullptr)};
        if (!expect("actual-host-allocated", owner != nullptr))
            return 5;
        const auto close = wil::scope_exit(
            [&]() noexcept
            {
                owner->Shutdown();
                owner.reset();
                while (PumpMessages().quit)
                {
                }
            });
        owner->_quietUi = true;
        owner->_prompts = &QuietPrompts();
        if (!expect("actual-WARP-renderer-and-chrome-initialize",
                    SUCCEEDED(owner->InitializeProcessing()) && owner->EnableCommittedDisplayForTest() &&
                        SUCCEEDED(owner->RegisterWindowClass()) && SUCCEEDED(owner->CreateMainWindow(false)) &&
                        SUCCEEDED(owner->InitializeRenderer()) && SUCCEEDED(owner->InitializeChrome())))
            return 5;
        owner->_session.CreateDocument(2048, 1024, false);
        owner->DocumentChanged();
        if (!expect("actual-admitted-display-settles-before-diagnostic",
                    SettleJobs(owner->_jobs, 15000, DispatchMessageThunk, owner.get()) == SettleResult::Settled))
            return 5;
        owner->RenderFrame(); // Exclude cold document composition from the outline-only API observations.
        auto loops = TimingContours(kind);
        if (!expect("independent-contour-and-point-counts",
                    loops.size() == contourCounts[kind] &&
                        std::all_of(loops.begin(), loops.end(),
                                    [](const auto& loop) noexcept { return loop.size() == 4; })))
            return 5;
        const auto pathsBefore = owner->_chrome->SelectionResourcesStateForTest().pathsBuilt;
        owner->_chrome->SetSelectionOutline(std::move(loops));
        owner->_chrome->SetSelectionMapping(Vec2{0, 0}, 1.0, 96.0f);
        const auto frame = [&](const char* label, bool expectBuild) noexcept
        {
            owner->_chrome->EnableSelectionTimingForTest(true);
            owner->InvalidateFrame();
            const auto began = OutlineFrameCounter();
            owner->RenderFrame();
            const auto frameTicks = OutlineFrameCounter() - began;
            const auto timed = owner->_chrome->SelectionTimingStateForTest();
            owner->_chrome->EnableSelectionTimingForTest(false);
            const auto resources = owner->_chrome->SelectionResourcesStateForTest();
            expect("actual-path-build-and-draw-counts", timed.builds == (expectBuild ? 1u : 0u) &&
                                                            timed.drawPairs == 1 && resources.hasPath &&
                                                            resources.fixedSolid && resources.fixedDashes &&
                                                            resources.dashes[0] == 4 && resources.dashes[1] == 4);
            std::printf(
                "OUTLINE_OWNER_COST case=%s frame=%s loops=%zu points=%zu createTicks=%llu appendTicks=%llu "
                "closeTicks=%llu solidDrawTicks=%llu dashDrawTicks=%llu inclusiveRenderFrameTicks=%llu "
                "frequency=%llu builds=%llu drawPairs=%llu scope=hidden-WARP-diagnostic-not-G1\n",
                names[kind], label, contourCounts[kind], contourCounts[kind] * 4,
                static_cast<unsigned long long>(timed.createTicks), static_cast<unsigned long long>(timed.appendTicks),
                static_cast<unsigned long long>(timed.closeTicks),
                static_cast<unsigned long long>(timed.solidDrawTicks),
                static_cast<unsigned long long>(timed.dashDrawTicks), static_cast<unsigned long long>(frameTicks),
                static_cast<unsigned long long>(frequency.QuadPart), static_cast<unsigned long long>(timed.builds),
                static_cast<unsigned long long>(timed.drawPairs));
        };
        frame("first", true);
        owner->_chrome->SetSelectionAntPhase(1);
        frame("next-phase", false);
        owner->_chrome->SetSelectionAntPhase(0);
        frame("cached-phase-zero", false);
        owner->_chrome->SetSelectionMapping(Vec2{11, 7}, 1.0, 96.0f);
        frame("pan", false);
        owner->_chrome->SetSelectionMapping(Vec2{11, 7}, 0.5, 96.0f);
        frame("zoom", false);
        expect("view-and-phase-changes-reuse-one-real-path",
               owner->_chrome->SelectionResourcesStateForTest().pathsBuilt == pathsBefore + 1);
        owner->Shutdown();
        expect("joined-diagnostic-owner-releases-original-charges",
               owner->_shutdownComplete && owner->_jobs.IsIdle() && !owner->_jobs.WakeHandle() &&
                   owner->_jobs.ReservedMemoryBytes() == 0 && owner->_jobs.ReleasingMemoryBytes() == 0);
    }
    return failures ? 5 : 0;
}
