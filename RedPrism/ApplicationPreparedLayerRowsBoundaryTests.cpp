#include "Application.h"

#include "../Tests/Support/DocumentFixture.h"
#include "ApplicationProcessingTestProbe.h"
#include "ApplicationShutdownTestProbe.h"
#include "ChromeHost.h"
#include "DeferredRelease.h"
#include "LayerRowsTestGate.h"
#include "Resources.h"
#include "resource.h"

#include <array>
#include <atomic>
#include <cstdio>
#include <memory>
#include <utility>

namespace
{
struct MemoryControl final
{
    MemorySnapshot snapshot{};
    unsigned int samples = 0;
};

bool SampleMemory(void* opaque, MemorySnapshot& snapshot) noexcept
{
    auto& control = *static_cast<MemoryControl*>(opaque);
    ++control.samples;
    snapshot = control.snapshot;
    return true;
}

struct PumpState final
{
    Application* owner = nullptr;
    UINT gateMessages = 0;
    bool quit = false;
};

struct ShutdownObservation final
{
    Application* owner = nullptr;
    bool cacheClear = false;
    bool accountingClear = false;
    bool borrowedRowsClear = false;
};

} // namespace

int Application::RunStandalonePreparedLayerRowsBoundarySelfTest(HINSTANCE instance) noexcept
{
    unsigned int checks = 0;
    int failures = 0;
    const auto expect = [&](bool condition, const char* scenario, const char* name) noexcept
    {
        ++checks;
        failures += condition ? 0 : 1;
        std::printf("%s application-prepared-layer-rows-boundary.%s.%s\n", condition ? "PASS" : "FAIL", scenario, name);
        std::fflush(stdout);
    };

    enum class Scenario : std::uint8_t
    {
        MemoryRefusal,
        ShutdownBeforePrepare,
        ShutdownAfterPrepare,
        ShutdownWithActiveCache,
    };
    constexpr std::array scenarios{"memory-refusal-retry", "shutdown-before-prepare", "shutdown-after-prepare",
                                   "shutdown-active-cache"};

    for (unsigned int ordinal = 0; ordinal < scenarios.size(); ++ordinal)
    {
        const auto scenario = static_cast<Scenario>(ordinal);
        const char* const label = scenarios[ordinal];
        expect(!DeferredReleaseDetail::sink.enqueue, label, "starts-without-an-outer-release-sink");

        MemoryControl memory;
        memory.snapshot = scenario == Scenario::MemoryRefusal
                              ? MemorySnapshot{0, 0}
                              : MemorySnapshot{2ULL * 1024 * 1024 * 1024, 2ULL * 1024 * 1024 * 1024};
        ApplicationProcessingTestProbe processing;
        processing.memory = {SampleMemory, &memory};
        ApplicationShutdownTestProbe retirement;
        ShutdownObservation shutdownObservation;
        auto owner = std::make_unique<Application>(instance, true, nullptr, 0);
        const auto shutdownOnExit = wil::scope_exit(
            [&]() noexcept
            {
                if (owner)
                    owner->Shutdown();
            });
        owner->_quietUi = true;
        owner->_prompts = &QuietPrompts();
        owner->_processingTestProbe = &processing;
        owner->_shutdownTestProbe = &retirement;
        if (FAILED(owner->InitializeProcessing()) || FAILED(owner->RegisterWindowClass()) ||
            FAILED(owner->CreateMainWindow(false)) || FAILED(owner->InitializeRenderer()) ||
            FAILED(owner->InitializeChrome()))
        {
            SelfTestFailure(L"Prepared layer rows boundary fixture could not initialize its hidden WARP host.\n");
            return 5;
        }
        owner->BindCompositionDevice();
        expect(owner->_renderer.IsWarp() && owner->_window && owner->_chrome && owner->_jobs.WorkerCount() != 0 &&
                   DeferredReleaseDetail::sink.enqueue,
               label, "uses-the-real-hidden-WARP-host-pool-and-release-sink");

        DocumentFixture fixture;
        fixture.id = Guid::Generate();
        fixture.width = 2;
        fixture.height = 1;
        ImageLayer group = ImageLayer::Blank(L"Boundary group", {2, 1});
        group.isGroup = true;
        const Guid groupId = group.id;
        ImageLayer child = ImageLayer::Blank(L"Literal child", {2, 1});
        child.parentID = groupId;
        child.visible = false;
        const Guid childId = child.id;
        constexpr std::array<std::uint8_t, 8> pixels{10, 20, 30, 255, 40, 50, 60, 255};
        constexpr std::array<std::uint8_t, 2> mask{0, 255};
        const Raster image = Raster::Make(2, 1, RasterFormat::Bgra8888, pixels.data(), pixels.size());
        const Raster coverage = Raster::Make(2, 1, RasterFormat::Gray8, mask.data(), mask.size());
        child.asset = RasterAsset{image, {}, L"Literal child pixels"};
        child.mask = LayerMask{RasterAsset{coverage, Raster::MakeThumbnail(coverage), L"Literal child mask"}, true,
                               std::nullopt, true};
        fixture.layers.push_back(std::move(group));
        fixture.layers.push_back(std::move(child));
        owner->_session.ReplaceDocument(std::move(fixture).Seal(), childId);
        owner->_session.SelectLayerTarget(childId, true);
        const Guid fixtureRevision = owner->_session.history.CurrentRevision();
        const int fixtureUndoCount = owner->_session.history.UndoCount();
        const auto fixtureIsIntact = [&](const Guid& revision, int undoCount, const wchar_t* childName) noexcept
        {
            const auto& document = owner->_session.Document();
            if (!document || document->layers.size() != 2)
                return false;
            const auto& actualGroup = document->layers[0];
            const auto& actualChild = document->layers[1];
            if (!actualGroup.isGroup || actualGroup.name != L"Boundary group" || actualChild.name != childName ||
                actualChild.parentID != std::optional<Guid>(groupId) || actualChild.visible || !actualChild.asset ||
                !actualChild.mask || !actualChild.mask->enabled || !actualChild.mask->asset.image.HasStorage())
                return false;
            const auto& actualPixels = actualChild.asset->image;
            const auto& actualMask = actualChild.mask->asset.image;
            if (!actualPixels.HasStorage() || actualPixels.Width() != 2 || actualPixels.Height() != 1 ||
                actualMask.Width() != 2 || actualMask.Height() != 1)
                return false;
            for (std::size_t index = 0; index < pixels.size(); ++index)
                if (actualPixels.Data()[index] != pixels[index])
                    return false;
            for (std::size_t index = 0; index < mask.size(); ++index)
                if (actualMask.Data()[index] != mask[index])
                    return false;
            return owner->_session.history.CurrentRevision() == revision &&
                   owner->_session.history.UndoCount() == undoCount;
        };

        auto gate = std::make_shared<LayerRowsTestGate>();
        gate->release.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        gate->hold = scenario == Scenario::ShutdownBeforePrepare || scenario == Scenario::ShutdownAfterPrepare;
        gate->holdAfterPrepare = scenario == Scenario::ShutdownAfterPrepare;
        gate->reservationBytes = 1;
        if (!gate->release)
            return 5;
        owner->_layerRowsTestGate = gate;

        const auto initialSamples = memory.samples;
        if (!owner->EnablePreparedLayerRows())
        {
            SelfTestFailure(L"Prepared layer rows boundary fixture could not enable the explicitly requested path.\n");
            return 5;
        }
        owner->_layerRowsSource.Pump();
        PumpState pump{owner.get()};
        const auto dispatch = [](void* opaque, void* native) noexcept
        {
            auto& state = *static_cast<PumpState*>(opaque);
            auto& message = *static_cast<MSG*>(native);
            if (message.message == LayerRowsTestGate::kMessage)
            {
                ++state.gateMessages;
                return;
            }
            state.owner->DispatchQueuedMessage(message);
        };

        if (scenario == Scenario::MemoryRefusal)
        {
            if (SettleJobs(owner->_jobs, 10000, dispatch, &pump) != SettleResult::Settled)
                return 5;
            expect(memory.samples == initialSamples + 1 && !gate->captures.load() && !gate->preparations.load() &&
                       !owner->_layerRowsTicket && !owner->_preparedLayerRows && !owner->_layerRowsCacheTicket &&
                       owner->_jobs.ReservedMemoryBytes() == 0 && owner->_jobs.ReleasingMemoryBytes() == 0 &&
                       owner->_jobs.IsIdle() && !owner->_layerRowsSource.HasReady(),
                   label, "low-headroom-refusal-captures-no-owner-input-no-worker-no-cache-or-token");
            memory.snapshot = {3ULL * 1024 * 1024 * 1024, 3ULL * 1024 * 1024 * 1024};
            owner->_jobs.NotifyMemoryChanged();
            owner->SyncPreparedLayerRows();
            owner->_layerRowsSource.Pump();
            if (SettleJobs(owner->_jobs, 10000, dispatch, &pump) != SettleResult::Settled)
                return 5;
            expect(memory.samples == initialSamples + 1 && !gate->captures.load() && !gate->preparations.load() &&
                       !owner->_layerRowsTicket && !owner->_layerRowsSource.HasReady() && owner->_jobs.IsIdle(),
                   label, "higher-memory-snapshot-and-unchanged-key-stay-dormant");

            owner->_session.RenameLayer(childId, L"Renamed child");
            const Guid renamedRevision = owner->_session.history.CurrentRevision();
            const int renamedUndoCount = owner->_session.history.UndoCount();
            owner->SyncPreparedLayerRows();
            owner->_layerRowsSource.Pump();
            const auto waitForPublication = [&]() noexcept
            {
                const ULONGLONG deadline = GetTickCount64() + 10000;
                for (;;)
                {
                    const auto messages = PumpMessages(dispatch, &pump);
                    if (messages.quit)
                    {
                        pump.quit = true;
                        return false;
                    }
                    (void)DrainCompletions(owner->_jobs);
                    (void)RunUiActions(owner->_jobs);
                    if (gate->publications.load(std::memory_order_acquire) == 1 && owner->_jobs.IsIdle())
                        return true;
                    if (owner->_jobs.HasUiWork())
                        continue;
                    const ULONGLONG now = GetTickCount64();
                    if (now >= deadline || !owner->_jobs.WakeHandle())
                        return false;
                    const HANDLE wake = static_cast<HANDLE>(owner->_jobs.WakeHandle());
                    const DWORD waited = MsgWaitForMultipleObjectsEx(1, &wake, static_cast<DWORD>(deadline - now),
                                                                     QS_ALLINPUT, MWMO_INPUTAVAILABLE);
                    if (waited != WAIT_OBJECT_0 && waited != WAIT_OBJECT_0 + 1)
                        return false;
                }
            };
            const bool published = waitForPublication();
            const auto prepared = owner->_preparedLayerRows.get();
            expect(published && memory.samples == initialSamples + 2 && gate->captures.load() == 1 &&
                       gate->preparations.load() == 1 && gate->publications.load() == 1 &&
                       owner->_layerRowsTicket == std::nullopt && owner->_layerRowsCacheTicket &&
                       owner->_jobs.ReservedMemoryBytes() != 0 && !owner->_layerRowsSource.HasReady(),
                   label, "changed-key-admits-exactly-one-fresh-worker-preparation-and-cache");
            expect(fixtureIsIntact(renamedRevision, renamedUndoCount, L"Renamed child"), label,
                   "preparation-preserves-literal-pixels-mask-and-rename-history");
            expect(prepared && prepared->Rows().size() == 2 && prepared->Rows()[0].name == L"Boundary group" &&
                       prepared->Rows()[1].name == L"Renamed child" && !prepared->Rows()[1].visible &&
                       prepared->Rows()[1].hasMask && prepared->DisplayText().size() == 2 &&
                       prepared->DisplayText()[1] ==
                           FormatStringResource(nullptr, IDS_LAYER_HIDDEN_SUFFIX, L"Renamed child"),
                   label, "published-rows-carry-current-name-hidden-state-and-mask");
        }
        else if (scenario == Scenario::ShutdownBeforePrepare || scenario == Scenario::ShutdownAfterPrepare)
        {
            expect(owner->_layerRowsTicket.has_value() && owner->_jobs.ReservedMemoryBytes() != 0 &&
                       !gate->captures.load(),
                   label, "original-ticket-is-admitted-before-owner-capture");
            const ULONGLONG deadline = GetTickCount64() + 10000;
            while (pump.gateMessages == 0)
            {
                const auto messages = PumpMessages(dispatch, &pump);
                if (messages.quit)
                {
                    PostQuitMessage(messages.exitCode);
                    pump.quit = true;
                    break;
                }
                (void)DrainCompletions(owner->_jobs);
                (void)RunUiActions(owner->_jobs);
                if (pump.gateMessages != 0)
                    break;
                if (owner->_jobs.HasUiWork())
                    continue;
                const ULONGLONG now = GetTickCount64();
                if (now >= deadline || !owner->_jobs.WakeHandle())
                    break;
                const HANDLE wake = static_cast<HANDLE>(owner->_jobs.WakeHandle());
                const DWORD waited = MsgWaitForMultipleObjectsEx(1, &wake, static_cast<DWORD>(deadline - now),
                                                                 QS_ALLINPUT, MWMO_INPUTAVAILABLE);
                if (waited != WAIT_OBJECT_0 && waited != WAIT_OBJECT_0 + 1)
                    break;
            }
            expect(!pump.quit && pump.gateMessages == 1 && gate->captureReserved.load() && gate->captures.load() == 1 &&
                       gate->preparations.load() == 1 && gate->publications.load() == 0 && owner->_layerRowsTicket &&
                       owner->_jobs.ReservedMemoryBytes() >= gate->reservationBytes,
                   label,
                   scenario == Scenario::ShutdownBeforePrepare ? "actual-worker-held-before-layerrows-prepare"
                                                               : "actual-worker-held-after-layerrows-prepare");
            expect(fixtureIsIntact(fixtureRevision, fixtureUndoCount, L"Literal child"), label,
                   "held-worker-preserves-the-literal-document-and-history");
        }

        if (scenario == Scenario::ShutdownWithActiveCache)
        {
            // This scenario has no worker hold; wait for its real completion before shutdown.
            const auto deadline = GetTickCount64() + 10000;
            while (!owner->_preparedLayerRows || !owner->_jobs.IsIdle())
            {
                const auto messages = PumpMessages(dispatch, &pump);
                if (messages.quit)
                {
                    pump.quit = true;
                    break;
                }
                (void)DrainCompletions(owner->_jobs);
                (void)RunUiActions(owner->_jobs);
                if (owner->_preparedLayerRows && owner->_jobs.IsIdle())
                    break;
                if (owner->_jobs.HasUiWork())
                    continue;
                const auto now = GetTickCount64();
                if (now >= deadline || !owner->_jobs.WakeHandle())
                    break;
                const HANDLE wake = static_cast<HANDLE>(owner->_jobs.WakeHandle());
                const DWORD waited = MsgWaitForMultipleObjectsEx(1, &wake, static_cast<DWORD>(deadline - now),
                                                                 QS_ALLINPUT, MWMO_INPUTAVAILABLE);
                if (waited != WAIT_OBJECT_0 && waited != WAIT_OBJECT_0 + 1)
                    break;
            }
            expect(!pump.quit && owner->_preparedLayerRows && owner->_layerRowsCacheTicket && owner->_jobs.IsIdle() &&
                       gate->captures.load() == 1 && gate->preparations.load() == 1 && gate->publications.load() == 1,
                   label, "successful-worker-publishes-active-retained-cache-before-shutdown");
        }

        if (scenario == Scenario::ShutdownWithActiveCache)
        {
            // The successful-cache case also proves that stable Sync borrows the active rows without rewriting.
            const auto* const cache = owner->_preparedLayerRows.get();
            const auto retained = owner->_layerRowsCacheTicket;
            const auto layerSyncs = owner->_chrome->SyncCountersForTest().layerRows;
            owner->SyncPreparedLayerRows();
            expect(cache && owner->_preparedLayerRows.get() == cache && retained == owner->_layerRowsCacheTicket &&
                       owner->_chrome->SyncCountersForTest().layerRows == layerSyncs &&
                       owner->_layerRowsTicket == std::nullopt && gate->publications.load() == 1,
                   label, "active-cache-is-retained-and-borrowed-across-settled-sync");
            expect(fixtureIsIntact(fixtureRevision, fixtureUndoCount, L"Literal child"), label,
                   "successful-preparation-preserves-document-and-history");
        }

        if (scenario == Scenario::ShutdownWithActiveCache)
            gate->hold = false;
        shutdownObservation.owner = owner.get();
        retirement.checkContext = &shutdownObservation;
        retirement.checkOwner = [](void* opaque) noexcept
        {
            auto& state = *static_cast<ShutdownObservation*>(opaque);
            Application& application = *state.owner;
            state.cacheClear = !application._preparedLayerRows && !application._layerRowsCacheTicket;
            // The closing job still owns its original scheduler reservation until Close returns. The retained
            // cache ticket itself must already be released at this owner-affine boundary.
            state.accountingClear = !application._layerRowsCacheTicket;
            state.borrowedRowsClear = application._chrome && application._chrome->LayerRowItemForTest(0).id == 0;
        };
        const unsigned int capturesBeforeShutdown = gate->captures.load();
        const unsigned int preparationsBeforeShutdown = gate->preparations.load();
        const unsigned int publicationsBeforeShutdown = gate->publications.load();
        owner->Shutdown(); // The scheduler's real shutdownEvent releases either held worker.

        expect(owner->_shutdownComplete && !owner->_window && !owner->_chrome && !owner->_rendererReady, label,
               "first-shutdown-joins-and-destroys-the-hidden-host");
        expect(shutdownObservation.cacheClear && shutdownObservation.accountingClear &&
                   shutdownObservation.borrowedRowsClear && !owner->_preparedLayerRows &&
                   !owner->_layerRowsCacheTicket && owner->_jobs.ReservedMemoryBytes() == 0 &&
                   owner->_jobs.ReleasingMemoryBytes() == 0,
               label, "shutdown-clears-cache-accounting-and-chrome-borrow-after-worker-join");
        expect(owner->_jobs.IsIdle() && owner->_jobs.WorkerCount() == 0 && !owner->_jobs.WakeHandle() &&
                   !DeferredReleaseDetail::sink.enqueue && retirement.ownerCalls == 1 &&
                   retirement.workerCalls.load() == 1 && retirement.mta.load() && retirement.offOwner.load(),
               label, "actual-sink-pool-and-worker-retirement-are-fully-cleaned");
        expect(gate->captures.load() == capturesBeforeShutdown &&
                   gate->preparations.load() == preparationsBeforeShutdown &&
                   gate->publications.load() == publicationsBeforeShutdown && gate->retired.load() &&
                   gate->retiredOffOwner.load(),
               label, "shutdown-suppresses-row-callbacks-and-joins-input-retirement");
        const auto capturesAfterShutdown = gate->captures.load();
        const auto preparationsAfterShutdown = gate->preparations.load();
        const auto publicationsAfterShutdown = gate->publications.load();
        owner->Shutdown();
        expect(retirement.ownerCalls == 1 && retirement.workerCalls.load() == 1 &&
                   gate->captures.load() == capturesAfterShutdown &&
                   gate->preparations.load() == preparationsAfterShutdown &&
                   gate->publications.load() == publicationsAfterShutdown && !DeferredReleaseDetail::sink.enqueue,
               label, "second-shutdown-does-not-repeat-hooks-or-row-callbacks");
        owner->_processingTestProbe = nullptr;
        owner->_shutdownTestProbe = nullptr;
        owner->_layerRowsTestGate.reset();
        owner.reset();

        bool consumedQuit = false;
        for (;;)
        {
            const auto messages = PumpMessages();
            consumedQuit = consumedQuit || messages.quit;
            if (!messages.quit)
                break;
        }
        expect(consumedQuit, label, "fixture-quit-consumed-before-the-next-fresh-owner");
        (void)shutdownOnExit;
    }

    std::printf("%s application-prepared-layer-rows-boundary.summary checks=%u failures=%d\n",
                failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 5 : 0;
}
