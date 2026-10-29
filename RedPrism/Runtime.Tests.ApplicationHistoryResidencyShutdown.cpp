#include "Application.h"

#include "Tests/Support/Support.Tests.DocumentFixture.h"

#include "Runtime.Tests.ApplicationShutdownTestProbe.h"
#include "Runtime.Tests.HistoryResidencyTestGate.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <thread>
#include <utility>

namespace
{
// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027)
struct CacheShutdownWitness final
{
    wil::unique_handle ready;
    std::atomic<bool> literal{false};
};
#pragma warning(pop)

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4626 5027)
class GrowShutdownCache final : public Job
{
  public:
    GrowShutdownCache(Raster raster, std::shared_ptr<CacheShutdownWitness> witness) noexcept
        : _raster(std::move(raster)), _witness(std::move(witness))
    {
    }
    JobResult Run(JobContext& context) override
    {
        constexpr std::array<std::uint8_t, 8> expected{10, 20, 30, 255, 1, 1, 1, 1};
        const auto* pixels = context.cancellation.IsCancelled() ? nullptr : _raster.Data();
        const bool literal =
            pixels && _raster.ByteSize() == expected.size() && std::equal(expected.begin(), expected.end(), pixels);
        _witness->literal.store(literal, std::memory_order_release);
        if (!SetEvent(_witness->ready.get()))
            return {JobStatus::Failed, HRESULT_FROM_WIN32(GetLastError())};
        return literal ? JobResult{} : JobResult{JobStatus::Failed, E_FAIL};
    }
    void Completed(JobResult) noexcept override {}

  private:
    const Raster _raster;
    const std::shared_ptr<CacheShutdownWitness> _witness;
};
#pragma warning(pop)

} // namespace

int Application::RunStandaloneHistoryResidencyShutdownSelfTest(HINSTANCE instance) noexcept
{
    int checks = 0;
    int failures = 0;
    const auto expect = [&](bool condition, const char* scenario, const char* name) noexcept
    {
        ++checks;
        failures += condition ? 0 : 1;
        std::printf("%s history-residency-shutdown.%s.%s\n", condition ? "PASS" : "FAIL", scenario, name);
    };
    for (const bool held : std::array{false, true})
    {
        const char* scenario = held ? "prepared-worker-held" : "notice-before-capture";
        auto owner = std::make_unique<Application>(instance, true, nullptr, 0);
        const auto shutdown = wil::scope_exit([&]() noexcept { owner->Shutdown(); });
        owner->_quietUi = true;
        owner->_prompts = &QuietPrompts();
        if (FAILED(owner->InitializeProcessing()) || FAILED(owner->RegisterWindowClass()) ||
            FAILED(owner->CreateMainWindow(false)) || FAILED(owner->InitializeRenderer()) ||
            FAILED(owner->InitializeChrome()))
            return 5;
        owner->BindCompositionDevice();
        constexpr std::array<std::uint8_t, 4> currentPixel{90, 80, 70, 255};
        constexpr std::array<std::uint8_t, 4> historicalPixel{10, 20, 30, 255};
        const Raster live = Raster::Make(1, 1, RasterFormat::Bgra8888, currentPixel.data(), currentPixel.size());
        const Raster leaf = Raster::Make(1, 1, RasterFormat::Bgra8888, historicalPixel.data(), historicalPixel.size());
        const Raster sparse = Raster::MakeSparse(2, 1, RasterFormat::Bgra8888, {}, 0, 0, {{0, 0, leaf}}, 1);
        DocumentFixture fixture;
        fixture.width = 2;
        fixture.height = 1;
        ImageLayer layer = ImageLayer::Blank(L"Shutdown current pixels", {2, 1});
        const Guid active = layer.id;
        layer.asset = RasterAsset{live, {}, L"Current literal"};
        fixture.layers.push_back(std::move(layer));
        owner->_session.ReplaceDocument(std::move(fixture).Seal(), active);
        auto historical = DocumentFixture::Copy(owner->_session.Document().value());
        historical.layers.front().asset = RasterAsset{sparse, {}, L"Historical literal"};
        owner->_session.history = DocumentHistory(100, 8);
        auto baseline =
            DocumentHistory::PrepareEdit(owner->_session.history.CaptureState(), std::move(historical).Seal(), active,
                                         owner->_session.Document(), active, L"Original saved edit");
        if (!baseline || !owner->_session.history.Publish(baseline.value()))
            return 5;
        baseline.reset();
        owner->_session.history.MarkSaved();
        const auto gate = std::make_shared<HistoryResidencyTestGate>();
        gate->release.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        const auto witness = std::make_shared<CacheShutdownWitness>();
        witness->ready.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (!gate->release || !witness->ready)
            return 5;
        owner->_historyResidencyTestGate = gate;
        if (!owner->_historyResidencySource.Start())
            return 5;
        const Guid revision = owner->_session.history.CurrentRevision();
        const auto generation = owner->_session.Generation();
        const auto document = owner->_session.Document();
        const auto presents = owner->_renderer.PresentCount();
        std::unique_ptr<Job> producer = std::make_unique<GrowShutdownCache>(sparse, witness);
        JobTicket producerTicket;
        if (FAILED(static_cast<HRESULT>(
                owner->_jobs.Submit(producer, {JobClass::InteractivePreview, 0, 65536}, producerTicket))))
            return 5;
        if (WaitForSingleObject(witness->ready.get(), 15000) != WAIT_OBJECT_0)
            std::_Exit(5); // Failure bound: never unwind borrowed owners beneath a stuck producer.
        expect(witness->literal.load(std::memory_order_acquire) && sparse.ByteSize() == 8 &&
                   owner->_session.history.RetainedBytes(document) == 12 &&
                   owner->_session.history.NeedsResidencyMaintenance(),
               scenario, "worker-growth-exceeds-literal-eight-byte-budget");
        bool reached = false;
        if (held)
        {
            const ULONGLONG deadline = GetTickCount64() + 15000;
            while (!reached)
            {
                MSG message{};
                while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
                {
                    if (message.message == HistoryResidencyTestGate::kMessage)
                        reached = true;
                    else
                        owner->DispatchQueuedMessage(message);
                }
                if (reached)
                    break;
                (void)DrainCompletions(owner->_jobs);
                (void)RunUiActions(owner->_jobs);
                const ULONGLONG now = GetTickCount64();
                if (now >= deadline)
                    std::_Exit(5);
                const HANDLE wake = static_cast<HANDLE>(owner->_jobs.WakeHandle());
                if (MsgWaitForMultipleObjectsEx(1, &wake, static_cast<DWORD>(deadline - now), QS_ALLINPUT,
                                                MWMO_INPUTAVAILABLE) == WAIT_FAILED)
                    std::_Exit(5);
            }
        }
        expect(held ? reached && gate->captures.load() == 1 && gate->preparations.load() == 1 &&
                          gate->reservationHeld.load() && owner->_historyResidencyTicket &&
                          owner->_historyResidencyStamp && owner->_historyWork.Busy()
                    : gate->captures.load() == 0 && gate->preparations.load() == 0 &&
                          owner->_historyResidencySource.HasReady() && !owner->_historyResidencyTicket,
               scenario, "actual-source-is-at-selected-shutdown-boundary");
// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4623 4626 5027)
        struct JoinedWitness final
        {
            Application& owner;
            const std::shared_ptr<HistoryResidencyTestGate> gate;
            const std::optional<CanvasDocument> document;
            Guid revision;
            std::uint64_t generation;
            bool called = false;
            bool preserved = false;
        }
#pragma warning(pop)
        joined{*owner, gate, document, revision, generation};
        ApplicationShutdownTestProbe probe;
        probe.checkContext = &joined;
        probe.checkJoined = [](void* context) noexcept
        {
            auto& state = *static_cast<JoinedWitness*>(context);
            state.called = true;
            state.preserved = state.owner._session.Document() == state.document &&
                              state.owner._session.Generation() == state.generation &&
                              state.owner._session.history.CurrentRevision() == state.revision &&
                              !state.owner._session.history.IsModified() &&
                              state.owner._session.history.UndoCount() == 1 && !state.gate->published.load() &&
                              !state.owner._historyResidencySource.HasPending();
        };
        owner->_shutdownTestProbe = &probe;
        // Deliberately do not signal gate->release. Actual RequestStop must wake the held job and join it.
        owner->Shutdown();
        expect(joined.called && joined.preserved && owner->_renderer.PresentCount() == presents, scenario,
               "shutdown-joins-without-history-document-revision-or-present-publication");
        expect(!owner->_historyResidencyTicket && !owner->_historyResidencyStamp && !owner->_historyWork.Busy() &&
                   !owner->_historyResidencySource.HasPending() && owner->_jobs.IsIdle() &&
                   owner->_jobs.ReservedMemoryBytes() == 0 && owner->_jobs.ReleasingMemoryBytes() == 0 &&
                   !owner->_jobs.WakeHandle() && owner->_jobs.WorkerCount() == 0,
               scenario, "source-ticket-barrier-reservation-and-worker-wake-retire");
        expect(held ? gate->retired.load() && gate->retiredOffOwner.load() && !gate->published.load()
                    : !gate->retired.load() && !gate->published.load(),
               scenario, "captured-payload-retires-off-owner-and-uncaptured-notice-owns-none");
        const Raster afterStop = Raster::MakeSparse(2, 1, RasterFormat::Bgra8888, {}, 0, 0, {{0, 0, leaf}}, 1);
        std::atomic<bool> afterStopLiteral{false};
        std::thread afterStopWorker(
            [&]() noexcept
            {
                const auto* pixels = afterStop.Data();
                afterStopLiteral.store(pixels && pixels[0] == 10 && pixels[4] == 1);
            });
        afterStopWorker.join();
        expect(afterStopLiteral.load() && !owner->_historyResidencySource.HasReady() && !owner->_jobs.WakeHandle() &&
                   !gate->published.load(),
               scenario, "post-stop-global-growth-cannot-use-retired-host-wake");
        owner->Shutdown();
        expect(owner->_shutdownComplete && !owner->_window && !owner->_chrome &&
                   !owner->_historyResidencySource.HasPending() && probe.ownerCalls == 1 && probe.workerCalls == 1,
               scenario, "second-shutdown-is-idempotent");
        owner->_shutdownTestProbe = nullptr;
        // The scope guard borrows owner until this iteration ends. Consume this hidden HWND's quit afterward.
        for (;;)
        {
            const auto drained = PumpMessages();
            if (!drained.quit)
                break;
        }
    }
    std::printf("history-residency-shutdown summary: %d checks, %d failures\n", checks, failures);
    return failures ? 5 : 0;
}
