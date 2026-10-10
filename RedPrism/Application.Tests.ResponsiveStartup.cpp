#include "Application.h"

#include "ApplicationProcessingTestProbe.h"

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

int Application::RunStandaloneResponsiveStartupSelfTest(HINSTANCE instance) noexcept
{
    enum class Mode : unsigned int
    {
        Normal,
        Refuse,
        ReentrantClose,
        Capacity
    };
    using Stage = ApplicationResponsiveSourceStage;
    struct Case final
    {
        const char* name;
        Mode mode;
        Stage boundary;
        unsigned int visits;
        std::size_t routes;
    };
    // Independent acquired-prefix expectations: row/display/stroke callbacks run only after acquisition.
    // Production has four routes before rows, five before display and eight before stroke.
    constexpr std::array cases{
        Case{"normal", Mode::Normal, Stage::BeforeAcquisition, 5, 0},
        Case{"refuse-before", Mode::Refuse, Stage::BeforeAcquisition, 1, 0},
        Case{"refuse-rows", Mode::Refuse, Stage::LayerRows, 2, 0},
        Case{"refuse-display", Mode::Refuse, Stage::Display, 3, 0},
        Case{"refuse-stroke", Mode::Refuse, Stage::Stroke, 4, 0},
        Case{"refuse-ready", Mode::Refuse, Stage::ReadyToEnable, 5, 0},
        Case{"close-before", Mode::ReentrantClose, Stage::BeforeAcquisition, 1, 0},
        Case{"close-rows", Mode::ReentrantClose, Stage::LayerRows, 2, 0},
        Case{"close-display", Mode::ReentrantClose, Stage::Display, 3, 0},
        Case{"close-stroke", Mode::ReentrantClose, Stage::Stroke, 4, 0},
        Case{"close-ready", Mode::ReentrantClose, Stage::ReadyToEnable, 5, 0},
        Case{"capacity-rows", Mode::Capacity, Stage::BeforeAcquisition, 1, 5},
        Case{"capacity-display", Mode::Capacity, Stage::LayerRows, 2, 4},
        Case{"capacity-stroke", Mode::Capacity, Stage::Display, 3, 1},
    };
    int failures = 0;
    unsigned int checks = 0;
    for (const auto& scenario : cases)
    {
        const auto expect = [&](bool condition, const char* name) noexcept
        {
            ++checks;
            failures += condition ? 0 : 1;
            std::printf("%s responsive-startup.%s.%s\n", condition ? "PASS" : "FAIL", scenario.name, name);
        };
        std::array<InertStartupRoute, OwnerWorkMultiplexer::kCapacity> inertRoutes;
        auto owner = std::make_unique<Application>(instance, true, nullptr, 0);
        const auto shutdown = wil::scope_exit([&]() noexcept { owner->Shutdown(); });
        owner->_quietUi = true;
        owner->_prompts = &QuietPrompts();
        const HRESULT processing = owner->InitializeProcessing();
        expect(SUCCEEDED(processing) && owner->_jobs.WakeHandle(), "real-processing-initializes");
        if (FAILED(processing) || !owner->_jobs.WakeHandle())
            return 5;
        if (scenario.mode != Mode::Normal)
        {
            constexpr std::array<std::wstring_view, 1> paths{L"startup-prefix-must-not-open.prism"};
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
            bool inputHeld = true;
            bool immutable = true;
            bool enabled = false;
            bool duplicateRefused = false;
            bool shutdownJoinedInCallback = false;
            std::optional<CanvasDocument> document;
            std::uint64_t generation = 0;
            Guid revision;
        } state{*owner, scenario, inertRoutes};
        ApplicationProcessingTestProbe probe;
        probe.context = &state;
        probe.responsiveSourcesForTest = true;
        probe.afterResponsiveSourceStage = [](void* raw, Stage stage) noexcept -> std::int32_t
        {
            auto& state = *static_cast<State*>(raw);
            auto& app = state.owner;
            constexpr std::array order{Stage::BeforeAcquisition, Stage::LayerRows, Stage::Display, Stage::Stroke,
                                       Stage::ReadyToEnable};
            constexpr std::array rows{false, true, true, true, true};
            constexpr std::array display{false, false, true, true, true};
            const auto index = state.visits++;
            state.literal = state.literal && index < order.size() && stage == order[index] &&
                            app._preparedLayerRowsEnabled == rows[index] &&
                            app._committedDisplayInitialized == display[index] &&
                            app._pendingDisplayInitialized == display[index] && !app._asyncStrokeFinishEnabled &&
                            !app._asyncStrokeSamplesEnabled && !app._asyncWarpLiveEnabled && !app._shutdownStarted;
            if (index == 0)
            {
                state.document = app._session.Document();
                state.generation = app._session.Generation();
                state.revision = app._session.history.CurrentRevision();
            }
            state.immutable = state.immutable && state.document && app._session.Document() == state.document &&
                              app._session.Generation() == state.generation &&
                              app._session.history.CurrentRevision() == state.revision;
            state.inputHeld = state.inputHeld && (state.scenario.mode == Mode::Normal || app._startupPaths.has_value());
            if (stage != state.scenario.boundary || state.scenario.mode == Mode::Normal)
                return S_OK;
            if (state.scenario.mode == Mode::Refuse)
                return E_ACCESSDENIED;
            if (state.scenario.mode == Mode::ReentrantClose)
            {
                app.Shutdown();
                state.shutdownJoinedInCallback = app._shutdownComplete && !app._jobs.WakeHandle() &&
                                                 app._jobs.IsIdle() && !app._committedDisplayInitialized &&
                                                 !app._pendingDisplayInitialized;
                return S_OK; // Successful callback return must not resume acquisition after Shutdown.
            }
            for (auto& route : state.inert)
            {
                if (!app._ownerWork.Attach(route))
                    break;
                ++state.attached;
            }
            return S_OK; // Refusal must come from real next-source attachment, not a fabricated HRESULT.
        };
        probe.afterRuntimeSources = [](void* raw) noexcept -> std::int32_t
        {
            auto& state = *static_cast<State*>(raw);
            auto& app = state.owner;
            ++state.runtimeVisits;
            state.enabled = app._preparedLayerRowsEnabled && app._committedDisplayInitialized &&
                            app._pendingDisplayInitialized && app._asyncStrokeFinishEnabled &&
                            app._asyncStrokeSamplesEnabled && app._asyncWarpLiveEnabled;
            const auto visits = state.visits;
            state.duplicateRefused = !app.InitializeResponsiveSourcesOwner(false) && state.visits == visits &&
                                     app._asyncStrokeFinishEnabled && app._asyncStrokeSamplesEnabled &&
                                     app._asyncWarpLiveEnabled;
            state.immutable = state.immutable && app._session.Document() == state.document &&
                              app._session.Generation() == state.generation &&
                              app._session.history.CurrentRevision() == state.revision;
            return PostMessageW(app._window.get(), WM_CLOSE, 0, 0) ? S_OK : HRESULT_FROM_WIN32(GetLastError());
        };
        owner->_processingTestProbe = &probe;
        const int result = owner->Run(SW_HIDE);
        const bool normal = scenario.mode == Mode::Normal;
        expect(result == (normal ? 0 : 5), "actual-run-exit-matches-prefix-or-close");
        expect(state.visits == scenario.visits, "only-acquired-prefix-boundaries-are-called");
        expect(state.literal, "literal-order-and-disabled-flags-through-final-boundary");
        expect(state.immutable, "acquisition-preserves-document-generation-and-history");
        expect(state.inputHeld, "startup-paths-remain-owned-through-acquisition");
        expect(state.runtimeVisits == (normal ? 1u : 0u), "runtime-input-follows-complete-acquisition-only");
        if (normal)
        {
            expect(state.enabled, "all-engines-published-before-runtime-input");
            expect(state.duplicateRefused, "repeat-acquisition-is-refused-without-callbacks");
        }
        else
        {
            expect(!owner->_asyncStrokeFinishEnabled && !owner->_asyncStrokeSamplesEnabled &&
                       !owner->_asyncWarpLiveEnabled,
                   "failed-or-closed-prefix-never-enables-engines");
            if (scenario.mode == Mode::ReentrantClose)
                expect(!owner->_startupPaths, "joined-reentrant-close-retires-undispatched-paths");
            else
                expect(owner->_startupPaths.has_value(), "failed-startup-never-dispatches-held-paths");
        }
        if (scenario.mode == Mode::Capacity)
            expect(state.attached == scenario.routes && state.attached < inertRoutes.size(),
                   "independent-original-route-count-reaches-real-capacity");
        if (scenario.mode == Mode::ReentrantClose)
            expect(state.shutdownJoinedInCallback, "reentrant-close-joins-before-successful-probe-return");
        for (std::size_t i = 0; i < state.attached; ++i)
            if (!owner->_ownerWork.Detach(inertRoutes[i]))
                std::_Exit(5); // Borrowed route storage cannot retire until detached from its actual owner.
        owner->Shutdown();
        expect(owner->_shutdownComplete && owner->_jobs.IsIdle() && !owner->_jobs.WakeHandle() &&
                   owner->_jobs.ReservedMemoryBytes() == 0 && owner->_jobs.ReleasingMemoryBytes() == 0 &&
                   !owner->_layerRowsSource.HasPending() && !owner->_layerRowsSource.HasReady() &&
                   !owner->_historyResidencySource.HasPending() && !owner->_historyResidencySource.HasReady() &&
                   !owner->_strokeFinishSource.HasPending() && !owner->_strokeFinishSource.HasReady() &&
                   !owner->_committedDisplayInitialized && !owner->_pendingDisplayInitialized &&
                   !owner->_ownerWork.HasPending() && !owner->_ownerWork.HasReady(),
               "shutdown-joins-prefix-workers-events-charges-and-display");
        const auto detached = [&](OwnerWorkPump& source) noexcept
        {
            const bool attached = owner->_ownerWork.Attach(source);
            return attached && owner->_ownerWork.Detach(source);
        };
        expect(detached(owner->_layerRowsSource) && detached(owner->_historyResidencySource) &&
                   detached(owner->_strokeFinishSource) && detached(owner->_committedDisplay) &&
                   detached(owner->_pendingDisplay) && detached(owner->_recordedCommands),
               "shutdown-detaches-every-original-responsive-source");
        const auto visits = state.visits;
        expect(!owner->InitializeResponsiveSourcesOwner(false) && state.visits == visits,
               "closed-owner-refuses-acquisition-without-reattachment");
        owner->Shutdown();
        expect(owner->_shutdownComplete && !owner->_jobs.WakeHandle() && !owner->_ownerWork.HasPending(),
               "repeated-shutdown-does-not-recreate-resources");
        owner->_processingTestProbe = nullptr;
        const auto quit = PumpMessages();
        const auto remaining = PumpMessages();
        expect((normal ? !quit.quit : quit.quit && quit.exitCode == 0) && !remaining.quit,
               "joined-fixture-drains-failure-quit-before-next-owner");
    }
    std::printf("responsive-startup summary: %u checks, %d failures\n", checks, failures);
    return failures ? 5 : 0;
}
