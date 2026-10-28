#include "Application.h"

#include "Runtime.Tests.ApplicationProcessingTestProbe.h"
#include "Runtime.Tests.HistoryResidencyTestGate.h"
#include "Tests/Support/Support.Tests.DocumentFixture.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <utility>

namespace
{
// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4626 5027)
class StartupCacheJob final : public Job
{
  public:
    StartupCacheJob(Raster source, bool& completed) noexcept : _source(std::move(source)), _completed(completed) {}
    JobResult Run(JobContext& context) override
    {
        if (context.cancellation.IsCancelled())
            return {JobStatus::Cancelled};
        constexpr std::array<std::uint8_t, 8> expected{10, 20, 30, 255, 1, 1, 1, 1};
        const auto* bytes = _source.Data();
        return bytes && _source.ByteSize() == expected.size() && std::equal(expected.begin(), expected.end(), bytes)
                   ? JobResult{}
                   : JobResult{JobStatus::Failed, E_FAIL};
    }
    void Completed(JobResult result) noexcept override
    {
        _completed = result.status == JobStatus::Finished;
    }

  private:
    const Raster _source;
    bool& _completed;
};
#pragma warning(pop)

class EmptyStartupSource final : public OwnerWorkPump
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

int Application::RunStandaloneHistoryResidencyStartupSelfTest(HINSTANCE instance) noexcept
{
    enum class Mode : unsigned int
    {
        Normal,
        AfterStartFailure,
        SubscriptionFull,
        RoutesFull
    };
    constexpr std::array names{"normal", "after-start-failure", "subscription-full", "routes-full"};
    int failures = 0;
    unsigned int checks = 0;
    for (unsigned int index = 0; index < names.size(); ++index)
    {
        const auto mode = static_cast<Mode>(index);
        const auto expect = [&](bool condition, const char* name) noexcept
        {
            ++checks;
            failures += condition ? 0 : 1;
            std::printf("%s history-residency-startup.%s.%s\n", condition ? "PASS" : "FAIL", names[index], name);
        };
        std::array<Raster::ResidencySubscription, Raster::ResidencySubscription::kCapacity> subscriptions;
        std::array<EmptyStartupSource, OwnerWorkMultiplexer::kCapacity> sources;
        auto owner = std::make_unique<Application>(instance, true, nullptr, 0);
        const auto shutdown = wil::scope_exit([&]() noexcept { owner->Shutdown(); });
        owner->_quietUi = true;
        owner->_prompts = &QuietPrompts();
        const HRESULT processing = owner->InitializeProcessing();
        expect(SUCCEEDED(processing), "real-processing-initializes");
        expect(owner->_utilityReadbackInitialized && !owner->_utilityReadback.HasPending() &&
                   !owner->_utilityReadback.HasReady() && !owner->_ownerWork.Attach(owner->_utilityReadback),
               "original-processing-retains-one-dormant-utility-registration");
        if (FAILED(processing) || !owner->_jobs.WakeHandle())
            return 5;
        std::size_t attached = 0;
        if (mode == Mode::RoutesFull)
        {
            for (auto& source : sources)
            {
                if (!owner->_ownerWork.Attach(source))
                    break;
                ++attached;
            }
            if (attached == 0 || attached == sources.size())
                return 5;
        }
        if (mode == Mode::SubscriptionFull)
            for (auto& subscription : subscriptions)
                if (!subscription.Start([](void*) noexcept {}, nullptr))
                    return 5;
// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4623 4626 5027)
        struct State final
        {
            Application& owner;
            Mode mode;
            unsigned int visits = 0;
            bool ready = false;
            bool literal = false;
            bool producerCompleted = false;
            bool retired = false;
        }
#pragma warning(pop)
        state{*owner, mode};
        ApplicationProcessingTestProbe probe;
        probe.context = &state;
        probe.afterRuntimeSources = [](void* raw) noexcept -> std::int32_t
        {
            auto& state = *static_cast<State*>(raw);
            auto& app = state.owner;
            ++state.visits;
            state.ready = app._historyResidencySource.HasReady();
            if (state.mode == Mode::AfterStartFailure)
                return E_ACCESSDENIED;
            if (state.mode != Mode::Normal)
                return E_UNEXPECTED;
            constexpr std::array<std::uint8_t, 4> currentPixel{90, 80, 70, 255};
            constexpr std::array<std::uint8_t, 4> historicPixel{10, 20, 30, 255};
            const Raster current = Raster::Make(1, 1, RasterFormat::Bgra8888, currentPixel.data(), currentPixel.size());
            const Raster leaf = Raster::Make(1, 1, RasterFormat::Bgra8888, historicPixel.data(), historicPixel.size());
            const Raster sparse = Raster::MakeSparse(2, 1, RasterFormat::Bgra8888, {}, 0, 0, {{0, 0, leaf}}, 1);
            DocumentFixture fixture;
            fixture.width = 2;
            fixture.height = 1;
            ImageLayer layer = ImageLayer::Blank(L"Startup literal", {2, 1});
            const Guid active = layer.id;
            layer.asset = RasterAsset{current, {}, L"Current literal"};
            fixture.layers.push_back(std::move(layer));
            app._session.ReplaceDocument(std::move(fixture).Seal(), active);
            auto historical = DocumentFixture::Copy(app._session.Document().value());
            historical.layers.front().asset = RasterAsset{sparse, {}, L"Historical literal"};
            app._session.history = DocumentHistory(100, 8);
            auto baseline =
                DocumentHistory::PrepareEdit(app._session.history.CaptureState(), std::move(historical).Seal(), active,
                                             app._session.Document(), active, L"Original edit");
            if (!baseline || !app._session.history.Publish(baseline.value()))
                return E_FAIL;
            baseline.reset();
            app._session.history.MarkSaved();
            const auto document = app._session.Document();
            const auto generation = app._session.Generation();
            const auto revision = app._session.history.CurrentRevision();
            const auto gate = std::make_shared<HistoryResidencyTestGate>();
            gate->hold = false;
            app._historyResidencyTestGate = gate;
            std::unique_ptr<Job> producer = std::make_unique<StartupCacheJob>(sparse, state.producerCompleted);
            JobTicket ticket;
            if (FAILED(
                    static_cast<HRESULT>(app._jobs.Submit(producer, {JobClass::InteractivePreview, 0, 65536}, ticket))))
                return E_FAIL;
            if (SettleJobs(app._jobs, 15000, DispatchMessageThunk, &app) != SettleResult::Settled)
                std::_Exit(5); // Failure bound; the original producer still borrows this stack if it has not joined.
            state.literal = state.producerCompleted && gate->captures.load() == 1 && gate->preparations.load() == 1 &&
                            gate->published.load() && app._session.history.UndoCount() == 0 &&
                            app._session.history.RetainedBytes(document) == 0 && !app._session.history.IsModified() &&
                            app._session.Document() == document && app._session.Generation() == generation &&
                            app._session.history.CurrentRevision() == revision &&
                            !app._session.history.NeedsResidencyMaintenance();
            state.retired = gate->retired.load() && gate->retiredOffOwner.load() && !app._historyResidencyTicket &&
                            !app._historyResidencyStamp && !app._historyWork.Busy() &&
                            app._jobs.ReservedMemoryBytes() == 0 && app._jobs.ReleasingMemoryBytes() == 0;
            app._historyResidencyTestGate.reset();
            return PostMessageW(app._window.get(), WM_CLOSE, 0, 0) ? S_OK : HRESULT_FROM_WIN32(GetLastError());
        };
        owner->_processingTestProbe = &probe;
        const int result = owner->Run(SW_HIDE); // Actual normal startup/loop; no surrogate subscription call.
        const bool starts = mode == Mode::Normal || mode == Mode::AfterStartFailure;
        expect(result == (mode == Mode::Normal ? 0 : 5), "actual-run-exit-matches-startup-or-close");
        expect(state.visits == (starts ? 1u : 0u), "checkpoint-follows-successful-subscription-only");
        expect(state.ready == starts, "real-notice-route-is-armed-before-runtime-input");
        if (mode == Mode::Normal)
        {
            expect(state.literal, "real-worker-growth-trims-history-preserving-document-revision-and-saved-state");
            expect(state.retired, "original-maintenance-graph-retires-off-owner-with-no-held-charge");
        }
        for (auto& subscription : subscriptions)
            subscription.Stop();
        for (std::size_t i = 0; i < attached; ++i)
            if (!owner->_ownerWork.Detach(sources[i]))
                std::_Exit(5); // Never retire a borrowed leaf while its owner still retains it.
        if (!starts)
        {
            const bool attachedSource = owner->_ownerWork.Attach(owner->_historyResidencySource);
            const bool detachedSource = attachedSource && owner->_ownerWork.Detach(owner->_historyResidencySource);
            expect(attachedSource && detachedSource && !owner->_historyResidencySource.HasReady(),
                   "failed-start-rolls-back-route-with-no-notice-authority");
        }
        owner->Shutdown();
        expect(owner->_shutdownComplete && !owner->_historyResidencySource.HasPending() &&
                   !owner->_historyResidencySource.HasReady() && !owner->_historyResidencyTicket &&
                   !owner->_historyResidencyStamp && !owner->_jobs.WakeHandle() && owner->_jobs.IsIdle() &&
                   owner->_jobs.ReservedMemoryBytes() == 0 && owner->_jobs.ReleasingMemoryBytes() == 0,
               "shutdown-joins-notices-workers-events-and-reservations");
        const bool sourceAttached = owner->_ownerWork.Attach(owner->_historyResidencySource);
        const bool sourceDetached = sourceAttached && owner->_ownerWork.Detach(owner->_historyResidencySource);
        expect(sourceAttached && sourceDetached, "shutdown-detaches-the-real-notification-route");
        const bool utilityAttached = owner->_ownerWork.Attach(owner->_utilityReadback);
        expect(utilityAttached && owner->_ownerWork.Detach(owner->_utilityReadback) &&
                   !owner->_utilityReadbackInitialized && !owner->_utilityReadback.HasPending(),
               "shutdown-detaches-dormant-utility-route-after-joined-final-worker");
        owner->Shutdown();
        expect(owner->_shutdownComplete && !owner->_jobs.WakeHandle() && !owner->_historyResidencySource.HasReady(),
               "repeat-shutdown-has-no-new-notification-or-pool");
        owner->_processingTestProbe = nullptr;
        // Failed startup returns before Run can consume WM_QUIT from destruction. Drain each
        // fixture's queue after all callback owners have joined, before constructing the next owner.
        const auto quit = PumpMessages();
        const auto remaining = PumpMessages();
        expect((mode == Mode::Normal ? !quit.quit : quit.quit && quit.exitCode == 0) && !remaining.quit,
               "fixture-consumes-startup-failure-quit-before-next-owner");
    }
    std::printf("history-residency-startup summary: %u checks, %d failures\n", checks, failures);
    return failures ? 5 : 0;
}
