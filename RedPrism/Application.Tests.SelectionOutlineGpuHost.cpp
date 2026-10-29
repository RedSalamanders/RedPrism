#include "Application.h"
#include "ChromeHost.h"
#include "Tests/Support/Support.Tests.DocumentFixture.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

namespace
{
class IdleOutlineRoute final : public OwnerWorkPump
{
  public:
    bool HasPending() const noexcept override
    {
        return false;
    }
    bool HasReady() const noexcept override
    {
        return false;
    }
    void Pump() noexcept override {}
};
CanvasDocument OutlineHostFixture(bool large)
{
    DocumentFixture fixture;
    fixture.width = 64;
    fixture.height = 64;
    auto layer = ImageLayer::Blank(L"Outline host", Vec2{64, 64});
    fixture.layers.push_back(std::move(layer));
    std::vector<SelectionLoop> loops;
    if (large)
    {
        // 32,768 independent 4-point contours exercise the actual UI source path, not cached numeric points.
        loops.reserve(32768);
        for (unsigned int index = 0; index < 32768; ++index)
        {
            const double x = 8.0 + static_cast<double>(index % 128) / 8.0;
            const double y = 8.0 + static_cast<double>(index / 128) / 16.0;
            loops.push_back({{x, y}, {x + 0.25, y}, {x + 0.25, y + 0.25}, {x, y + 0.25}});
        }
    }
    else
        loops.push_back({{8, 8}, {48, 8}, {48, 48}, {8, 48}});
    fixture.selection = DocumentSelection{false, SelectionLoops{std::move(loops)}};
    return std::move(fixture).Seal();
}
} // namespace

int Application::RunStandaloneSelectionOutlineGpuHostSelfTest(HINSTANCE instance) noexcept
{
    unsigned int checks = 0;
    unsigned int failures = 0;
    const auto expect = [&](bool condition, const char* label) noexcept
    {
        std::printf("%s application-selection-outline-gpu.%s\n", condition ? "PASS" : "FAIL", label);
        ++checks;
        failures += condition ? 0u : 1u;
        return condition;
    };
    for (unsigned int mode = 0; mode < 4; ++mode)
    {
        std::unique_ptr<Application> owner{new (std::nothrow) Application(instance, true, L"", 0, nullptr, nullptr)};
        if (!expect(static_cast<bool>(owner), "actual-owner-allocation"))
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
        if (!expect(SUCCEEDED(owner->InitializeProcessing()) && SUCCEEDED(owner->RegisterWindowClass()) &&
                        SUCCEEDED(owner->CreateMainWindow(false)) && SUCCEEDED(owner->InitializeRenderer()) &&
                        SUCCEEDED(owner->InitializeChrome()),
                    "actual-hidden-warp-host-initializes"))
            return 5;
        const auto settle = [&]() noexcept
        { return SettleJobs(owner->_jobs, 30000, DispatchMessageThunk, owner.get()) == SettleResult::Settled; };
        if (mode == 3)
        {
            std::array<IdleOutlineRoute, OwnerWorkMultiplexer::kCapacity> pressure;
            std::size_t attached = 0;
            while (attached < pressure.size() && owner->_ownerWork.Attach(pressure[attached]))
                ++attached;
            expect(attached != 0 && !owner->EnableSelectionOutlineForTest() && !owner->_selectionOutlineInitialized &&
                       !owner->_selectionOutlineDisplay.HasPending() && owner->_graphGpuBudget.ReservedOwner() == 0 &&
                       SUCCEEDED(owner->_gpuBindingError) && owner->_gpuWork.DeviceGeneration() != 0,
                   "full-original-route-list-refuses-before-activation-or-admission");
            bool detached = true;
            for (std::size_t index = 0; index < attached; ++index)
                detached = owner->_ownerWork.Detach(pressure[index]) && detached;
            expect(detached && owner->EnableSelectionOutlineForTest(),
                   "refused-empty-route-can-acquire-after-pressure-retires");
            owner->Shutdown();
            expect(owner->_jobs.ReservedMemoryBytes() == 0 && owner->_graphGpuBudget.ReservedOwner() == 0,
                   "actual-close-after-empty-acquisition-refunds-all-charges");
            continue;
        }
        if (!expect(owner->EnableCommittedDisplayForTest(mode != 1) && owner->EnableSelectionOutlineForTest(mode != 1),
                    "prepared-canvas-and-outline-acquire-on-original-multiplexer"))
            return 5;
        const auto pointsBefore = owner->_chrome->SyncCountersForTest().selectionVertices;
        const auto pathsBefore = owner->_chrome->SelectionResourcesStateForTest().pathsBuilt;
        auto fixture = OutlineHostFixture(mode == 0);
        const auto target = fixture.layers[0].id;
        const auto source = fixture.selection->loops.Identity();
        expect(fixture.selection->loops.PointCount() == (mode == 0 ? 131072u : 4u),
               "independent-actual-source-point-count");
        owner->_session.ReplaceDocument(std::move(fixture), target);
        owner->DocumentChanged();
        expect(owner->_overlaySelection && owner->_overlaySelection->loops.Identity() == source &&
                   owner->_chrome->SyncCountersForTest().selectionVertices == pointsBefore &&
                   owner->_chrome->SelectionResourcesStateForTest().pathsBuilt == pathsBefore,
               "selection-sync-captures-source-without-owner-point-conversion-or-path");
        if (mode == 2)
        {
            expect(!owner->_selectionOutlineDisplay.BorrowFrameOwner(0), "queued-original-work-publishes-no-frame");
            owner->Shutdown();
            expect(owner->_jobs.ReservedMemoryBytes() == 0 && owner->_graphGpuBudget.ReservedOwner() == 0 &&
                       !owner->_selectionOutlineInitialized,
                   "actual-queued-host-shutdown-joins-and-retires-whole-source");
            continue;
        }
        if (mode == 1)
        {
            expect(settle() && !owner->_selectionOutlineDisplay.BorrowFrameOwner(0) &&
                       !owner->_selectionOutlineDisplay.HasPending() && owner->_graphGpuBudget.ReservedOwner() == 0,
                   "ordinary-hidden-host-does-not-admit-outline-or-prepared-canvas");
            owner->_selectionOutlineAllowHiddenForTest = true;
            owner->_committedDisplayAllowHiddenForTest = true;
            owner->ReleaseSurfacesWhileUnseen();
        }
        if (!expect(settle() && static_cast<bool>(owner->_selectionOutlineDisplay.BorrowFrameOwner(0)) &&
                        owner->_acknowledgedCommittedDisplay && !owner->_selectionOutlineDisplay.HasPending(),
                    "actual-original-jobs-publish-prepared-canvas-and-worker-created-outline"))
            return 5;
        const auto charge = owner->_jobs.ReservedMemoryBytes();
        const auto gpuCharge = owner->_graphGpuBudget.ReservedOwner();
        const auto draws = owner->_renderer.SelectionOutlineDrawCount();
        const auto composites = owner->_composition.CompositeCount();
        bool phases = true;
        for (unsigned int phase = 0; phase < 8; ++phase)
        {
            owner->_chrome->SetSelectionAntPhase(static_cast<float>(phase));
            owner->RenderFrame();
            phases = phases && owner->_renderer.SelectionOutlineDrawCount() == draws + phase + 1 &&
                     owner->_renderer.SelectionOutlineFailure() == S_OK &&
                     owner->_jobs.ReservedMemoryBytes() == charge &&
                     owner->_graphGpuBudget.ReservedOwner() == gpuCharge;
        }
        expect(phases && !owner->_selectionOutlineDisplay.HasPending() &&
                   owner->_composition.CompositeCount() == composites &&
                   owner->_chrome->SelectionResourcesStateForTest().pathsBuilt == pathsBefore,
               "all-eight-actual-renderer-phases-use-one-idle-cache-with-zero-owner-geometry-or-composition");
        const auto hole = owner->_selectionOutlineHole;
        expect(!owner->BorrowSelectionOutlineFrameOwner(hole[0] + 0.25f, hole[1], hole[2], hole[3]).solid,
               "post-layout-actual-hole-mismatch-refuses-before-draw");
        owner->_viewport.pan.x += 0.25;
        const auto shown = owner->_session.DisplayedSelectionValue();
        owner->SyncSelectionOverlay(&shown);
        expect(!owner->_selectionOutlineDisplay.BorrowFrameOwner(0),
               "actual-fractional-pan-excludes-old-mask-before-pump");
        expect(settle() && static_cast<bool>(owner->_selectionOutlineDisplay.BorrowFrameOwner(0)),
               "actual-view-replacement-settles-original-context-and-creator-retirement");
        const auto oldGeneration = owner->_gpuWork.DeviceGeneration();
        if (!expect(SUCCEEDED(owner->_renderer.RecoverDeviceForTest()), "actual-renderer-device-replacement"))
            return 5;
        owner->BindCompositionDevice();
        expect(owner->_gpuWork.DeviceGeneration() != oldGeneration &&
                   !owner->_selectionOutlineDisplay.BorrowFrameOwner(0),
               "actual-new-device-excludes-old-two-srv-borrow");
        expect(settle() && static_cast<bool>(owner->_selectionOutlineDisplay.BorrowFrameOwner(0)),
               "actual-new-device-retires-and-recreates-outline-through-original-jobs");
        owner->RenderFrame();
        expect(owner->_renderer.SelectionOutlineFailure() == S_OK &&
                   owner->_chrome->SyncCountersForTest().selectionVertices == pointsBefore &&
                   owner->_chrome->SelectionResourcesStateForTest().pathsBuilt == pathsBefore,
               "actual-rebuilt-renderer-draws-without-legacy-points-or-paths");
        owner->_session.ClearDocument();
        owner->DocumentChanged();
        expect(!owner->_selectionOutlineDisplay.BorrowFrameOwner(0),
               "actual-document-clear-excludes-outline-immediately");
        expect(settle() && owner->_jobs.ReservedMemoryBytes() == 0 && owner->_graphGpuBudget.ReservedOwner() == 0,
               "actual-last-use-and-worker-retirement-refund-both-caches");
        owner->Shutdown();
        expect(!owner->_selectionOutlineInitialized && owner->_jobs.ReservedMemoryBytes() == 0 &&
                   owner->_graphGpuBudget.ReservedOwner() == 0,
               "actual-host-final-close-preserves-worker-and-owner-retirement-order");
    }
    std::printf("%s application-selection-outline-gpu.summary.%u-passed.%u-failed\n", failures == 0 ? "PASS" : "FAIL",
                checks - failures, failures);
    return failures == 0 ? 0 : 5;
}
