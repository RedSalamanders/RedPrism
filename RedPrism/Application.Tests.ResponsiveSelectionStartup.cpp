#include "Application.h"

#include "Runtime.Tests.ApplicationProcessingTestProbe.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>

namespace
{
class InertStartupRoute final : public OwnerWorkPump
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

// Expected acquisition values are namespace-scope so the C callbacks need no captures.
constexpr std::array kResponsiveSelectionOrder{
    ApplicationResponsiveSourceStage::BeforeAcquisition, ApplicationResponsiveSourceStage::LayerRows,
    ApplicationResponsiveSourceStage::Display,           ApplicationResponsiveSourceStage::Stroke,
    ApplicationResponsiveSourceStage::SelectionOutline,  ApplicationResponsiveSourceStage::LassoDraft,
    ApplicationResponsiveSourceStage::ReadyToEnable};
constexpr std::array<std::size_t, 7> kResponsiveSelectionRouteCounts{6, 7, 9, 10, 11, 11, 11};
constexpr std::array<bool, 7> kResponsiveSelectionRows{false, true, true, true, true, true, true};
constexpr std::array<bool, 7> kResponsiveSelectionDisplay{false, false, true, true, true, true, true};
constexpr std::array<bool, 7> kResponsiveSelectionEnabled{false, false, false, false, true, true, true};
constexpr std::array<bool, 7> kResponsiveLassoEnabled{false, false, false, false, false, true, true};
// Source-derived roster: Before, Rows, Display, Stroke, Selection, Lasso, Ready.
// Actual multiplexer counts at those callbacks are 6, 7, 9, 10, 11, 11, 11.
// Lasso is a child of SelectionOutlineDisplayController and adds no twelfth route.
// Literal matrix: normal 1, capacity refusal 1, callback refusals 7, reentrant shutdowns 7.
// Expected complete-run summary is 282 assertions: normal 18 + capacity 19 + seven refusals 17 + seven reentrant
// shutdowns 18.
int Application::RunStandaloneResponsiveSelectionStartupSelfTest(HINSTANCE instance) noexcept
{
    enum class Mode : unsigned int
    {
        Normal,
        Refuse,
        ReentrantShutdown,
        Capacity
    };
    using Stage = ApplicationResponsiveSourceStage;
    struct Case final
    {
        const char* name;
        Mode mode;
        Stage boundary;
    };
    constexpr std::array cases{
        Case{"normal", Mode::Normal, Stage::ReadyToEnable},
        Case{"capacity-selection-attach", Mode::Capacity, Stage::Stroke},
        Case{"refuse-before", Mode::Refuse, Stage::BeforeAcquisition},
        Case{"refuse-rows", Mode::Refuse, Stage::LayerRows},
        Case{"refuse-display", Mode::Refuse, Stage::Display},
        Case{"refuse-stroke", Mode::Refuse, Stage::Stroke},
        Case{"refuse-selection", Mode::Refuse, Stage::SelectionOutline},
        Case{"refuse-lasso", Mode::Refuse, Stage::LassoDraft},
        Case{"refuse-ready", Mode::Refuse, Stage::ReadyToEnable},
        Case{"shutdown-before", Mode::ReentrantShutdown, Stage::BeforeAcquisition},
        Case{"shutdown-rows", Mode::ReentrantShutdown, Stage::LayerRows},
        Case{"shutdown-display", Mode::ReentrantShutdown, Stage::Display},
        Case{"shutdown-stroke", Mode::ReentrantShutdown, Stage::Stroke},
        Case{"shutdown-selection", Mode::ReentrantShutdown, Stage::SelectionOutline},
        Case{"shutdown-lasso", Mode::ReentrantShutdown, Stage::LassoDraft},
        Case{"shutdown-ready", Mode::ReentrantShutdown, Stage::ReadyToEnable},
    };

    int failures = 0;
    unsigned int checks = 0;
    for (const auto& scenario : cases)
    {
        const auto expect = [&](bool condition, const char* name) noexcept
        {
            ++checks;
            failures += condition ? 0 : 1;
            std::printf("%s responsive-selection-startup.%s.%s\n", condition ? "PASS" : "FAIL", scenario.name, name);
        };
        std::array<InertStartupRoute, OwnerWorkMultiplexer::kCapacity> inertRoutes;
        auto owner = std::make_unique<Application>(instance, true, nullptr, 0);
        const auto shutdown = wil::scope_exit([&]() noexcept { owner->Shutdown(); });
        owner->_quietUi = true;
        owner->_prompts = &QuietPrompts();
        const HRESULT processing = owner->InitializeProcessing();
        expect(SUCCEEDED(processing) && owner->_jobs.WakeHandle(), "real-processing-initializes");
        expect(owner->_utilityReadbackInitialized && !owner->_utilityReadback.HasPending() &&
                   !owner->_utilityReadback.HasReady() && !owner->_ownerWork.Attach(owner->_utilityReadback),
               "utility-route-is-live-but-not-duplicated");
        if (FAILED(processing) || !owner->_jobs.WakeHandle())
            return 5;

        const bool normal = scenario.mode == Mode::Normal;
        if (!normal)
        {
            constexpr std::array<std::wstring_view, 1> paths{L"selection-startup-prefix-must-not-open.prism"};
            expect(owner->OpenCommandLinePaths(paths), "owns-startup-input-before-window");
            if (!owner->_startupPaths)
                return 5;
        }

        struct State final
        {
            Application& owner;
            const Case& scenario;
            std::array<InertStartupRoute, OwnerWorkMultiplexer::kCapacity>& inert;
            unsigned int visits = 0;
            unsigned int runtimeVisits = 0;
            std::size_t attached = 0;
            bool literal = true;
            bool immutable = true;
            bool heldAtBoundary = true;
            bool enginesReadyAtRuntime = false;
            bool normalRouteRoster = false;
            bool duplicateControlsUnchanged = false;
            bool shutdownJoinedInCallback = false;
            bool closePosted = false;
            bool selectionWasAcquired = false;
            bool selectionRouteWasAttached = false;
            bool lassoWasAcquired = false;
            std::uint64_t generation = 0;
            Guid revision;
            std::optional<DocumentReadAuthority> authority;
        } state{*owner, scenario, inertRoutes};

        ApplicationProcessingTestProbe probe;
        probe.context = &state;
        probe.responsiveSelectionSourcesForTest = true;
        probe.afterResponsiveSourceStage = [](void* raw, Stage stage) noexcept -> std::int32_t
        {
            auto& state = *static_cast<State*>(raw);
            auto& app = state.owner;
            const auto index = state.visits++;
            state.literal = state.literal && index < kResponsiveSelectionOrder.size() &&
                            stage == kResponsiveSelectionOrder[index] &&
                            app._ownerWork.SourceCountForTest() == kResponsiveSelectionRouteCounts[index] &&
                            app._preparedLayerRowsEnabled == kResponsiveSelectionRows[index] &&
                            app._committedDisplayInitialized == kResponsiveSelectionDisplay[index] &&
                            app._pendingDisplayInitialized == kResponsiveSelectionDisplay[index] &&
                            app._selectionOutlineInitialized == kResponsiveSelectionEnabled[index] &&
                            app._lassoDraftDisplayEnabled == kResponsiveLassoEnabled[index] &&
                            !app._asyncStrokeFinishEnabled && !app._asyncStrokeSamplesEnabled &&
                            !app._asyncWarpLiveEnabled && !app._shutdownStarted;
            if (index == 0)
            {
                state.authority = app._session.CaptureDocumentReadAuthority();
                state.generation = app._session.Generation();
                state.revision = app._session.history.CurrentRevision();
                state.immutable = state.authority.has_value();
            }
            state.immutable = state.immutable && app._session.CaptureDocumentReadAuthority() == state.authority &&
                              app._session.Generation() == state.generation &&
                              app._session.history.CurrentRevision() == state.revision;
            state.heldAtBoundary =
                state.heldAtBoundary && (state.scenario.mode == Mode::Normal || app._startupPaths.has_value());
            state.selectionWasAcquired = state.selectionWasAcquired || app._selectionOutlineInitialized;
            state.selectionRouteWasAttached =
                state.selectionRouteWasAttached || app._ownerWork.ContainsSourceForTest(app._selectionOutlineDisplay);
            state.lassoWasAcquired = state.lassoWasAcquired || app._lassoDraftDisplayEnabled;

            if (state.scenario.mode == Mode::Capacity && stage == Stage::Stroke)
            {
                const bool startsAtTen = app._ownerWork.SourceCountForTest() == 10;
                if (startsAtTen && app._ownerWork.Attach(state.inert[0]))
                    ++state.attached;
                state.literal = state.literal && startsAtTen && state.attached == 1 &&
                                app._ownerWork.SourceCountForTest() == OwnerWorkMultiplexer::kCapacity;
                return S_OK; // The next real owner acquisition is SelectionOutline and must refuse.
            }
            if (stage != state.scenario.boundary || state.scenario.mode == Mode::Normal)
                return S_OK;
            if (state.scenario.mode == Mode::Refuse)
                return E_ACCESSDENIED;
            if (state.scenario.mode == Mode::ReentrantShutdown)
            {
                app.Shutdown();
                state.shutdownJoinedInCallback = app._shutdownComplete && !app._jobs.WakeHandle() &&
                                                 app._jobs.IsIdle() && app._jobs.ReservedMemoryBytes() == 0 &&
                                                 app._jobs.ReleasingMemoryBytes() == 0 &&
                                                 !app._selectionOutlineInitialized && !app._lassoDraftDisplayEnabled;
                return S_OK; // A successful callback result cannot resume acquisition after joined shutdown.
            }
            return S_OK;
        };
        probe.afterRuntimeSources = [](void* raw) noexcept -> std::int32_t
        {
            auto& state = *static_cast<State*>(raw);
            auto& app = state.owner;
            ++state.runtimeVisits;
            state.enginesReadyAtRuntime = app._preparedLayerRowsEnabled && app._committedDisplayInitialized &&
                                          app._pendingDisplayInitialized && app._selectionOutlineInitialized &&
                                          app._lassoDraftDisplayEnabled && app._asyncStrokeFinishEnabled &&
                                          app._asyncStrokeSamplesEnabled && app._asyncWarpLiveEnabled;
            state.normalRouteRoster = app._ownerWork.SourceCountForTest() == OwnerWorkMultiplexer::kCapacity &&
                                      app._ownerWork.ContainsSourceForTest(app._gpuWork) &&
                                      app._ownerWork.ContainsSourceForTest(app._retryWait) &&
                                      app._ownerWork.ContainsSourceForTest(app._pathReadWait) &&
                                      app._ownerWork.ContainsSourceForTest(app._layerRowsSource) &&
                                      app._ownerWork.ContainsSourceForTest(app._historyResidencySource) &&
                                      app._ownerWork.ContainsSourceForTest(app._strokeFinishSource) &&
                                      app._ownerWork.ContainsSourceForTest(app._committedDisplay) &&
                                      app._ownerWork.ContainsSourceForTest(app._pendingDisplay) &&
                                      app._ownerWork.ContainsSourceForTest(app._recordedCommands) &&
                                      app._ownerWork.ContainsSourceForTest(app._utilityReadback) &&
                                      app._ownerWork.ContainsSourceForTest(app._selectionOutlineDisplay);
            const auto visits = state.visits;
            const auto sourceCount = app._ownerWork.SourceCountForTest();
            const bool strokeFinishEnabled = app._asyncStrokeFinishEnabled;
            const bool strokeSamplesEnabled = app._asyncStrokeSamplesEnabled;
            const bool warpLiveEnabled = app._asyncWarpLiveEnabled;

            const bool legacyRefused = !app.InitializeResponsiveSourcesOwner(false);
            const bool legacyUnchanged = state.visits == visits && app._ownerWork.SourceCountForTest() == sourceCount &&
                                         app._asyncStrokeFinishEnabled == strokeFinishEnabled &&
                                         app._asyncStrokeSamplesEnabled == strokeSamplesEnabled &&
                                         app._asyncWarpLiveEnabled == warpLiveEnabled;

            const bool selectionRefused = !app.InitializeResponsiveSourcesOwner(false, true);
            const bool selectionUnchanged =
                state.visits == visits && app._ownerWork.SourceCountForTest() == sourceCount &&
                app._asyncStrokeFinishEnabled == strokeFinishEnabled &&
                app._asyncStrokeSamplesEnabled == strokeSamplesEnabled && app._asyncWarpLiveEnabled == warpLiveEnabled;
            state.duplicateControlsUnchanged =
                legacyRefused && legacyUnchanged && selectionRefused && selectionUnchanged;
            if (!state.duplicateControlsUnchanged)
                return E_FAIL;
            state.closePosted = PostMessageW(app._window.get(), WM_CLOSE, 0, 0) != FALSE;
            return state.closePosted ? S_OK : HRESULT_FROM_WIN32(GetLastError());
        };

        owner->_processingTestProbe = &probe;
        const int result = owner->Run(SW_HIDE);
        const auto boundaryIndex = [&]() noexcept
        {
            for (std::size_t index = 0; index < kResponsiveSelectionOrder.size(); ++index)
                if (kResponsiveSelectionOrder[index] == scenario.boundary)
                    return index;
            return kResponsiveSelectionOrder.size();
        }();
        const unsigned int expectedVisits = normal ? static_cast<unsigned int>(kResponsiveSelectionOrder.size())
                                                   : static_cast<unsigned int>(boundaryIndex + 1);
        const bool capacity = scenario.mode == Mode::Capacity;
        const bool reentrant = scenario.mode == Mode::ReentrantShutdown;
        expect(result == (normal ? 0 : 5), "actual-run-exit-matches-complete-or-refused-prefix");
        expect(state.visits == expectedVisits, "only-reached-acquisition-callbacks-run");
        expect(state.literal, "literal-seven-stage-order-route-counts-and-disabled-engines");
        expect(state.immutable, "source-acquisition-preserves-document-authority-generation-and-history");
        expect(state.heldAtBoundary, "failed-prefix-keeps-startup-input-undispatched");
        expect(state.runtimeVisits == (normal ? 1u : 0u), "runtime-input-follows-only-complete-acquisition");
        if (normal)
        {
            expect(state.enginesReadyAtRuntime, "all-three-engines-enable-after-final-ready-callback");
            expect(state.normalRouteRoster && state.closePosted, "eleven-real-routes-and-normal-wm-close");
            expect(state.duplicateControlsUnchanged, "repeat-acquisition-refused-without-callbacks");
            expect(state.selectionWasAcquired && state.lassoWasAcquired,
                   "selection-route-owns-nested-lasso-without-extra-source");
        }
        else
        {
            expect(!owner->_asyncStrokeFinishEnabled && !owner->_asyncStrokeSamplesEnabled &&
                       !owner->_asyncWarpLiveEnabled,
                   "failed-or-shutdown-prefix-never-enables-engines");
            if (!reentrant)
                expect(owner->_startupPaths.has_value(), "failed-prefix-retains-undispatched-startup-input");
        }
        if (capacity)
        {
            expect(state.attached == 1 && owner->_ownerWork.SourceCountForTest() == OwnerWorkMultiplexer::kCapacity,
                   "single-real-filler-exhausts-route-capacity-after-stroke");
            expect(!owner->_selectionOutlineInitialized && !owner->_lassoDraftDisplayEnabled &&
                       !state.selectionWasAcquired && !state.lassoWasAcquired && state.visits == 4 &&
                       kResponsiveSelectionOrder[state.visits - 1] == Stage::Stroke,
                   "selection-attach-refusal-has-no-selection-or-lasso-callback");
        }
        if (reentrant)
        {
            expect(state.shutdownJoinedInCallback, "reentrant-shutdown-joins-at-every-boundary");
            expect(!owner->_startupPaths, "reentrant-shutdown-retires-held-startup-input");
        }

        const bool selectionExpected = !capacity && boundaryIndex >= 4;
        const bool lassoExpected = !capacity && boundaryIndex >= 5;
        owner->Shutdown();
        expect(owner->_shutdownComplete && owner->_jobs.IsIdle() && !owner->_jobs.WakeHandle() &&
                   owner->_jobs.ReservedMemoryBytes() == 0 && owner->_jobs.ReleasingMemoryBytes() == 0 &&
                   !owner->_ownerWork.HasPending() && !owner->_ownerWork.HasReady() &&
                   !owner->_selectionOutlineInitialized && !owner->_lassoDraftDisplayEnabled,
               "shutdown-joins-workers-refunds-charges-and-retires-selection-and-lasso");
        expect(state.selectionRouteWasAttached == selectionExpected &&
                   !owner->_ownerWork.ContainsSourceForTest(owner->_selectionOutlineDisplay) &&
                   (state.lassoWasAcquired == lassoExpected),
               "selection-route-detaches-on-every-acquired-prefix-lasso-shares-that-route");

        if (state.attached != 0 && !owner->_ownerWork.Detach(inertRoutes[0]))
            std::_Exit(5); // The borrowed inert route must detach before its fixture storage retires.
        const auto detached = [&](OwnerWorkPump& source) noexcept
        {
            const bool attached = owner->_ownerWork.Attach(source);
            return attached && owner->_ownerWork.Detach(source);
        };
        const bool allOriginalRoutesDetached =
            detached(owner->_gpuWork) && detached(owner->_retryWait) && detached(owner->_pathReadWait) &&
            detached(owner->_layerRowsSource) && detached(owner->_historyResidencySource) &&
            detached(owner->_strokeFinishSource) && detached(owner->_committedDisplay) &&
            detached(owner->_pendingDisplay) && detached(owner->_recordedCommands) &&
            detached(owner->_utilityReadback) && detached(owner->_selectionOutlineDisplay);
        expect(allOriginalRoutesDetached && !owner->_utilityReadbackInitialized &&
                   owner->_ownerWork.SourceCountForTest() == 0,
               "shutdown-detaches-selection-and-every-original-startup-member");
        const auto visits = state.visits;
        expect(!owner->InitializeResponsiveSourcesOwner(false, true) && state.visits == visits,
               "closed-owner-refuses-complete-source-reacquisition");
        owner->Shutdown();
        expect(owner->_shutdownComplete && owner->_jobs.IsIdle() && !owner->_jobs.WakeHandle() &&
                   !owner->_ownerWork.HasPending() && !owner->_ownerWork.HasReady(),
               "repeated-shutdown-is-idempotent");
        owner->_processingTestProbe = nullptr;
        const auto quit = PumpMessages();
        const auto remaining = PumpMessages();
        expect((normal ? !quit.quit : quit.quit && quit.exitCode == 0) && !remaining.quit,
               "fixture-drains-only-its-own-failure-quit");
    }
    std::printf("responsive-selection-startup summary: %u checks, %d failures\n", checks, failures);
    return failures ? 5 : 0;
}
