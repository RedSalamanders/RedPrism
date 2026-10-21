#include "Application.h"
#include <array>
#include <cstdio>
#include <memory>
#include <utility>
namespace
{
class ExtraOwner final : public OwnerWorkPump
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
} // namespace
int Application::RunStandaloneSelectionOutlineOwnerCapacitySelfTest(HINSTANCE instance) noexcept
{
    unsigned int checks = 0, failures = 0;
    for (bool selectionFirst : {false, true})
    {
        const auto expect = [&](bool value, const char* label) noexcept
        {
            std::printf("%s application-selection-owner-capacity.%s.%s\n", value ? "PASS" : "FAIL",
                        selectionFirst ? "selection-first" : "responsive-first", label);
            ++checks;
            failures += value ? 0u : 1u;
            return value;
        };
        auto owner = std::make_unique<Application>(instance, true, L"", 0, nullptr, nullptr);
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
        if (!expect(SUCCEEDED(owner->InitializeProcessing()) && owner->_ownerWork.SourceCountForTest() == 5,
                    "actual-five-processing-owners-acquired"))
            return 5;
        if (!expect(SUCCEEDED(owner->RegisterWindowClass()) && SUCCEEDED(owner->CreateMainWindow(false)) &&
                        SUCCEEDED(owner->InitializeRenderer()) && SUCCEEDED(owner->InitializeChrome()),
                    "actual-warp-window-and-chrome-initialize"))
            return 5;
        owner->BindCompositionDevice(); // Actual cold startup binds the shared device and brush before acquiring
                                       // responsive engines.
        if (!expect(owner->_historyResidencySource.Start() && owner->_ownerWork.SourceCountForTest() == 6,
                    "actual-history-subscription-is-sixth-owner"))
            return 5;
        if (selectionFirst)
        {
            if (!expect(owner->EnableSelectionOutlineForTest(true) && owner->_ownerWork.SourceCountForTest() == 7,
                        "actual-selection-acquires-before-responsive-group"))
                return 5;
            if (!expect(owner->InitializeResponsiveSourcesOwner(true),
                        "actual-responsive-group-acquires-after-selection"))
                return 5;
        }
        else
        {
            if (!expect(owner->InitializeResponsiveSourcesOwner(true) && owner->_ownerWork.SourceCountForTest() == 10,
                        "actual-responsive-group-acquires-before-selection"))
                return 5;
            if (!expect(owner->EnableSelectionOutlineForTest(true), "actual-selection-acquires-after-responsive-group"))
                return 5;
        }
        expect(owner->_ownerWork.SourceCountForTest() == 11 && owner->_selectionOutlineInitialized &&
                   owner->_preparedLayerRowsEnabled && owner->_committedDisplayInitialized &&
                   owner->_pendingDisplayInitialized && owner->_asyncStrokeFinishEnabled &&
                   owner->_asyncStrokeSamplesEnabled && owner->_asyncWarpLiveEnabled,
               "literal-eleven-distinct-actual-owners-with-all-responsive-engines-enabled");
        // These are actual acquired application owners, not substitutes that merely fill the table.
        const std::array<std::pair<OwnerWorkPump*, const char*>, 11> owners{
            {{&owner->_gpuWork, "actual-gpu-work-owner-already-present"},
             {&owner->_retryWait, "actual-retry-wait-owner-already-present"},
             {&owner->_pathReadWait, "actual-path-read-owner-already-present"},
             {&owner->_utilityReadback, "actual-utility-readback-owner-already-present"},
             {&owner->_recordedCommands, "actual-shared-command-pump-owner-already-present"},
             {&owner->_historyResidencySource, "actual-history-owner-already-present"},
             {&owner->_layerRowsSource, "actual-row-owner-already-present"},
             {&owner->_committedDisplay, "actual-committed-display-owner-already-present"},
             {&owner->_pendingDisplay, "actual-pending-display-owner-already-present"},
             {&owner->_strokeFinishSource, "actual-stroke-owner-already-present"},
             {&owner->_selectionOutlineDisplay, "actual-selection-owner-already-present"}}};
        for (const auto& [source, label] : owners)
            expect(owner->_ownerWork.ContainsSourceForTest(*source) && !owner->_ownerWork.Attach(*source), label);
        ExtraOwner extra;
        expect(!owner->_ownerWork.Attach(extra) && owner->_ownerWork.SourceCountForTest() == 11,
               "twelfth-extra-refused-with-actual-eleven-owners-intact");
        owner->Shutdown();
        expect(owner->_shutdownComplete && !owner->_jobs.WakeHandle() && owner->_ownerWork.SourceCountForTest() == 0,
               "actual-joined-shutdown-detaches-all-eleven-owners");
        expect(owner->_jobs.ReservedMemoryBytes() == 0 && owner->_graphGpuBudget.ReservedOwner() == 0,
               "joined-shutdown-refunds-original-source-and-gpu-charges");
    }
    std::printf("%s application-selection-owner-capacity.summary.%u-passed.%u-failed\n", failures ? "FAIL" : "PASS",
                checks - failures, failures);
    return failures ? 5 : 0;
}
