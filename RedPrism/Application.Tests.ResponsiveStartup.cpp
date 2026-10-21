#include "Application.h"

#include "../Tests/Support/Support.Tests.DocumentFixture.h"
#include "ChromeHost.h"
#include "Runtime.Tests.ApplicationProcessingTestProbe.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <utility>

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
        bool pendingRefusal = false;
    };
    // Independent acquired-prefix expectations: row/display/stroke callbacks run only after acquisition.
    // Production has six routes before rows, seven before display and nine before stroke.
    // With eleven slots the independent filler counts are five/four/three/two; pending leaves exactly one slot.
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
        Case{"capacity-pending", Mode::Capacity, Stage::LayerRows, 2, 3, true},
        Case{"capacity-stroke", Mode::Capacity, Stage::Display, 3, 2},
    };
    int failures = 0;
    unsigned int checks = 0;
    {
        const auto expect = [&](bool value, const char* name) noexcept
        {
            ++checks;
            failures += value ? 0 : 1;
            std::printf("%s responsive-startup.ordinary-hue.%s\n", value ? "PASS" : "FAIL", name);
        };
        struct OrdinaryHue final
        {
            Application* owner = nullptr;
            bool visited = false;
            bool independent = false;
            bool inputHeld = false;
            bool scalar = false;
            bool literal = false;
            bool sourcePreserved = false;
            bool retired = false;
            std::uint32_t captures = 0;
        } state;
        ApplicationProcessingTestProbe probe;
        probe.context = &state;
        probe.workerCount = 2;
        probe.afterRuntimeSources = [](void* raw) noexcept -> std::int32_t
        {
            auto& state = *static_cast<OrdinaryHue*>(raw);
            auto& app = *state.owner;
            state.visited = true;
            state.independent = app._hueReadbackEnabled && app._hostSourceRegistered && app._gpuWorkInitialized &&
                                app._utilityReadbackInitialized && app._recordedCommandsInitialized &&
                                !app._preparedLayerRowsEnabled && !app._committedDisplayInitialized &&
                                !app._pendingDisplayInitialized && !app._asyncStrokeFinishEnabled &&
                                !app._asyncStrokeSamplesEnabled && !app._selectionOutlineInitialized;
            state.inputHeld = app._startupPaths.has_value();
            if (!state.independent)
                return E_ABORT;
            const auto settle = [&app]() noexcept
            { return SettleJobs(app._jobs, 10000, DispatchMessageThunk, &app) == SettleResult::Settled; };
            DocumentFixture fixture;
            fixture.width = 4;
            fixture.height = 1;
            ImageLayer layer = ImageLayer::Blank(L"Ordinary Hue literal", {4, 1});
            constexpr std::array<std::uint8_t, 16> bytes{0xCC, 0x66, 0x33, 0xFF, 0x20, 0x40, 0x80, 0xFF,
                                                         0,    0,    0,    0,    0x80, 0x80, 0x80, 0xFF};
            const auto pixels = Raster::Make(4, 1, RasterFormat::Bgra8888, bytes.data(), bytes.size());
            const auto id = layer.id;
            layer.asset = RasterAsset{pixels, {}, layer.name};
            fixture.layers.push_back(std::move(layer));
            app.ReplaceDocument(std::move(fixture).Seal(), id, {});
            if (!settle() || !app.BeginColorEdit(ChromeHost::kCommandHueSaturation))
                return E_ABORT;
            auto settings = app._session.PendingColorEdit()->settings.hsv;
            settings.range = ColorRange::Reds;
            app._session.UpdateHueSaturation(settings, true);
            app._session.SetHueTargeting(false);
            app._session.SetHueSampleMode(HueSampleMode::Replace);
            app.DocumentChanged();
            if (!settle())
                return E_ABORT;
            const auto revision = app._session.history.CurrentRevision();
            const auto undo = app._session.history.UndoCount();
            const auto before = app._gpuWork.Statistics();
            app._hueCapturesForTest = &state.captures;
            const auto resetCounter = wil::scope_exit([&app]() noexcept { app._hueCapturesForTest = nullptr; });
            if (!app.QueueHuePressOwner({.2, .2}, 0, HostHueInput::Purpose::RangeSample, HueSampleMode::Replace))
                return E_ABORT;
            const auto* entry =
                app._hostRequests.Size() ? app._hostRequests.PendingAt(app._hostRequests.Size() - 1) : nullptr;
            const auto receipt = entry ? entry->input.Receipt() : ContinuationReceipt::Failure(E_UNEXPECTED);
            const auto queued = app._gpuWork.Statistics();
            state.scalar = app._hueInput.Busy() && !app._hueReadbackTicket && state.captures == 0 &&
                           receipt.Read().outcome == HostOutcome::Pending && queued.copies == before.copies &&
                           queued.maps == before.maps;
            if (!settle())
                return E_ABORT;
            const auto& panel = app._session.PendingColorEdit();
            // #3366CC has hue220. The legacy fixed band is [175,205,235,265].
            state.literal =
                receipt.Read().outcome == HostOutcome::Succeeded && state.captures == 1 && panel &&
                panel->settings.hsv.bands[static_cast<std::size_t>(ColorRange::Reds)] == HueBand{175, 205, 235, 265};
            const auto* active = app._session.ActiveLayer();
            std::array<std::uint8_t, 16> actual{};
            if (active && active->asset)
                active->asset->image.ReadRegion(0, 0, 4, 1, actual.data(), 16, 0);
            state.sourcePreserved = active && active->id == id && active->asset && active->asset->image == pixels &&
                                    actual == bytes && app._session.history.CurrentRevision() == revision &&
                                    app._session.history.UndoCount() == undo;
            app.CancelColorEditForTest();
            state.retired = settle() && !app._hueReadbackTicket && !app._hueReadbackStamp && !app._hueInput.Busy() &&
                            !app._historyWork.Busy() && !app._utilityReadback.HasPending() &&
                            !app._recordedCommands.HasPending() && app._jobs.ReservedMemoryBytes() == 0 &&
                            app._jobs.ReleasingMemoryBytes() == 0;
            return E_ABORT; // Stop before startup paths and ordinary input can dispatch.
        };
        auto owner = std::make_unique<Application>(instance, true, nullptr, 0);
        const auto shutdown = wil::scope_exit([&]() noexcept { owner->Shutdown(); });
        state.owner = owner.get();
        owner->_quietUi = true;
        owner->_prompts = &QuietPrompts();
        owner->_processingTestProbe = &probe;
        const auto processing = owner->InitializeProcessing();
        expect(SUCCEEDED(processing), "real-processing-initializes");
        if (FAILED(processing))
            return 5;
        constexpr std::array<std::wstring_view, 1> held{L"ordinary-hue-startup-must-not-open.prism"};
        expect(owner->OpenCommandLinePaths(held), "startup-input-owned-before-run");
        if (!owner->_startupPaths)
            return 5;
        expect(owner->Run(SW_HIDE) == 5 && state.visited, "actual-run-reaches-ordinary-source-boundary");
        expect(state.independent, "recorded-hue-ready-with-other-prepared-consumers-off");
        expect(state.inputHeld && owner->_startupPaths, "original-startup-input-remains-undispatched");
        expect(state.scalar, "press-enqueues-without-owner-capture-or-copy-map");
        expect(state.literal, "real-original-readback-publishes-independent-hue220-band");
        expect(state.sourcePreserved, "literal-source-pixels-and-history-unchanged");
        expect(state.retired, "joined-command-acknowledges-and-refunds-original-work");
        owner->Shutdown();
        expect(owner->_shutdownComplete && owner->_jobs.IsIdle() && !owner->_jobs.WakeHandle() &&
                   owner->_jobs.ReservedMemoryBytes() == 0 && owner->_jobs.ReleasingMemoryBytes() == 0,
               "shutdown-retires-all-acquired-prefixes-and-input");
        const auto quit = PumpMessages();
        const auto remaining = PumpMessages();
        expect(quit.quit && quit.exitCode == 0 && !remaining.quit, "failed-startup-drains-one-quit");
    }
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
        expect(owner->_utilityReadbackInitialized && !owner->_utilityReadback.HasPending() &&
                   !owner->_utilityReadback.HasReady() && !owner->_ownerWork.Attach(owner->_utilityReadback),
               "processing-holds-one-dormant-utility-route-before-responsive-acquisition");
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
                // The pending case leaves one actual slot for committed display; the second child
                // refuses its attachment and rolls back the first without closing the shared pump.
                if (state.scenario.pendingRefusal && state.attached == state.scenario.routes)
                    break;
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
        const bool displayChildRefused = scenario.mode == Mode::Capacity && scenario.boundary == Stage::LayerRows;
        if (displayChildRefused)
        {
            expect(owner->_preparedDisplayAcquisitionStarted && !owner->_committedDisplayInitialized &&
                       !owner->_pendingDisplayInitialized && !owner->_committedDisplay.HasPending() &&
                       !owner->_committedDisplay.HasReady() && !owner->_pendingDisplay.HasPending() &&
                       !owner->_pendingDisplay.HasReady(),
                   "actual-child-attachment-refusal-rolls-back-display-only");
            expect(owner->_recordedCommandsInitialized && SUCCEEDED(owner->InitializeRecordedCommandsOwner()) &&
                       !owner->_ownerWork.Attach(owner->_recordedCommands) &&
                       owner->_recordedCommands.HasSourceSlotOwner() &&
                       owner->_recordedCommands.AttachedSourceCountOwnerForTest() == 0,
                   "display-child-refusal-keeps-original-shared-command-route-live");
            expect(!owner->InitializePreparedDisplayOwner(false) && owner->_recordedCommands.HasSourceSlotOwner(),
                   "partly-acquired-display-is-one-shot-without-poisoning-common-recording");
        }
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
                   detached(owner->_pendingDisplay) && detached(owner->_recordedCommands) &&
                   detached(owner->_utilityReadback) && !owner->_utilityReadbackInitialized,
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
