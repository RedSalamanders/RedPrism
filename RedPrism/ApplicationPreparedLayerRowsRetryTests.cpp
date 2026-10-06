#include "Application.h"

#include "../Tests/Support/DocumentFixture.h"
#include "ApplicationProcessingTestProbe.h"
#include "ChromeHost.h"
#include "KeyboardState.h"
#include "LayerRowsTestGate.h"

#include <array>
#include <cstdio>
#include <memory>

namespace
{
struct RetryMemory final
{
    MemorySnapshot snapshot{3ULL << 30, 3ULL << 30};
    unsigned int samples = 0;
};

bool SampleRetryMemory(void* context, MemorySnapshot& snapshot) noexcept
{
    auto& memory = *static_cast<RetryMemory*>(context);
    ++memory.samples;
    snapshot = memory.snapshot;
    return true;
}
} // namespace

int Application::RunStandalonePreparedLayerRowsRetrySelfTest(HINSTANCE instance) noexcept
{
    unsigned int checks = 0;
    int failures = 0;
    const auto expect = [&](bool condition, const char* name) noexcept
    {
        ++checks;
        failures += condition ? 0 : 1;
        std::printf("%s application-prepared-layer-rows-retry.%s\n", condition ? "PASS" : "FAIL", name);
    };
    std::array<BYTE, 256> savedKeys{};
    if (!KeyboardState::Read(savedKeys))
        return 5;
    KeyboardState::Write({});
    const auto restoreKeys = wil::scope_exit([savedKeys]() noexcept { KeyboardState::Write(savedKeys); });

    RetryMemory memory;
    ApplicationProcessingTestProbe processing;
    processing.memory = {SampleRetryMemory, &memory};
    auto owner = std::make_unique<Application>(instance, true, nullptr, 0);
    const auto shutdown = wil::scope_exit([&]() noexcept { owner->Shutdown(); });
    owner->_quietUi = true;
    owner->_prompts = &QuietPrompts();
    owner->_processingTestProbe = &processing;
    if (FAILED(owner->InitializeProcessing()) || FAILED(owner->RegisterWindowClass()) ||
        FAILED(owner->CreateMainWindow(false)) || FAILED(owner->InitializeRenderer()) ||
        FAILED(owner->InitializeChrome()))
        return 5;
    owner->BindCompositionDevice();
    expect(owner->_renderer.IsWarp() && owner->_jobs.WorkerCount() != 0 && owner->_chrome,
           "uses-real-hidden-WARP-host-and-memory-admission");

    DocumentFixture fixture;
    fixture.id = Guid::Generate();
    fixture.width = 2;
    fixture.height = 1;
    constexpr std::array<std::uint8_t, 8> pixels{10, 20, 30, 255, 40, 50, 60, 255};
    constexpr std::array<std::uint8_t, 2> coverage{0, 255};
    ImageLayer layer = ImageLayer::Blank(L"Before retry", {2, 1});
    const Guid layerId = layer.id;
    layer.asset = RasterAsset{Raster::Make(2, 1, RasterFormat::Bgra8888, pixels.data(), pixels.size()), {}, L"Pixels"};
    const Raster mask = Raster::Make(2, 1, RasterFormat::Gray8, coverage.data(), coverage.size());
    layer.mask = LayerMask{RasterAsset{mask, Raster::MakeThumbnail(mask), L"Mask"}, true, std::nullopt, true};
    fixture.layers.push_back(std::move(layer));
    owner->_session.ReplaceDocument(std::move(fixture).Seal(), layerId);
    owner->_session.SelectLayerTarget(layerId, true);

    auto initialGate = std::make_shared<LayerRowsTestGate>();
    initialGate->hold = false; // Initial publication is observed; only the later explicit retry is event-gated.
    owner->_layerRowsTestGate = initialGate;
    struct Pump final
    {
        Application& owner;
        std::shared_ptr<LayerRowsTestGate> gate;
        const PreparedLayerRows* previous = nullptr;
        bool held = false;
        bool viewInput = false;
        bool duplicateRetryBlocked = false;
        bool unexpectedInitialHold = false;
    } pump{*owner, {}};
    const auto dispatch = [](void* context, void* native) noexcept
    {
        auto& state = *static_cast<Pump*>(context);
        auto& message = *static_cast<MSG*>(native);
        if (message.message != LayerRowsTestGate::kMessage)
        {
            state.owner.DispatchQueuedMessage(message);
            return;
        }
        if (!state.gate)
        {
            state.unexpectedInitialHold = true;
            SelfTestFailure(L"The retry fixture unexpectedly held its ungated initial publication.\n");
            return;
        }
        state.held = state.gate && state.gate->captureReserved.load() && state.gate->captures.load() == 1 &&
                     state.gate->preparations.load() == 1 && state.owner._layerRowsTicket.has_value() &&
                     state.owner._preparedLayerRows.get() == state.previous && !state.owner._layerRowsRetryAvailable;
        const auto opening = state.owner._layerRowsTicket;
        state.owner.OnCommand(ChromeHost::kCommandRetryLayerList);
        state.duplicateRetryBlocked =
            opening == state.owner._layerRowsTicket && !state.owner._layerRowsSource.HasReady();
        const double before = state.owner._viewport.zoom;
        state.owner.OnCommand(ChromeHost::kCommandZoomIn);
        state.viewInput = state.owner._viewport.zoom > before;
        std::printf("LAYER ROW RETRY held=%d view=%d duplicate=%d zoom=%.4f->%.4f\n", state.held, state.viewInput,
                    state.duplicateRetryBlocked, before, state.owner._viewport.zoom);
        (void)SetEvent(state.gate->release.get());
    };
    // Prepare the actual sized viewport before the first row publication can scroll its selection into view.
    const UINT initialDpi = GetDpiForWindow(owner->_window.get());
    const HRESULT initialPrepared = owner->_chrome->Prepare(owner->_renderer.Width(), owner->_renderer.Height(),
                                                            initialDpi == 0 ? 96.0f : static_cast<float>(initialDpi));
    if (FAILED(initialPrepared))
    {
        std::printf("LAYER ROW RETRY initial viewport failed: result=%08lx size=%u,%u dpi=%u\n",
                    static_cast<unsigned long>(initialPrepared), owner->_renderer.Width(), owner->_renderer.Height(),
                    initialDpi);
        return 5;
    }
    if (!owner->EnablePreparedLayerRows())
    {
        SelfTestFailure(L"The retry fixture could not enable its prepared-row source.\n");
        return 5;
    }
    owner->_layerRowsSource.Pump();
    const auto initialSettled = SettleJobs(owner->_jobs, 10000, dispatch, &pump);
    if (initialSettled != SettleResult::Settled)
    {
        std::printf("LAYER ROW RETRY initial settlement failed: result=%u captures=%u preparations=%u\n",
                    static_cast<unsigned int>(initialSettled), initialGate->captures.load(),
                    initialGate->preparations.load());
        return 5;
    }
    owner->SyncChrome();
    const auto* const previous = owner->_preparedLayerRows.get();
    const auto retained = owner->_layerRowsCacheTicket;
    const auto originalBytes = owner->_jobs.ReservedMemoryBytes();
    expect(previous && previous->Rows().size() == 1 && previous->Rows()[0].name == L"Before retry" && retained &&
               originalBytes != 0 && initialGate->captures.load() == 1 && initialGate->preparations.load() == 1 &&
               initialGate->publications.load() == 1 && !pump.unexpectedInitialHold,
           "initial-publication-owns-one-original-reservation");
    if (!previous || !retained)
        return 5;

    memory.snapshot = {0, 0};
    owner->_session.RenameLayer(layerId, L"After retry");
    const Guid renamedRevision = owner->_session.history.CurrentRevision();
    const int renamedUndoCount = owner->_session.history.UndoCount();
    const auto renamedGeneration = owner->_session.Generation();
    const auto requestedSamples = memory.samples;
    owner->SyncPreparedLayerRows();
    owner->_layerRowsSource.Pump();
    expect(owner->_layerRowsTicket.has_value() && owner->_preparedLayerRows.get() == previous,
           "replacement-original-ticket-keeps-the-old-cache");
    if (SettleJobs(owner->_jobs, 10000, dispatch, &pump) != SettleResult::Settled)
        return 5;
    expect(memory.samples == requestedSamples + 1 && !owner->_layerRowsTicket &&
               owner->_preparedLayerRows.get() == previous && retained == owner->_layerRowsCacheTicket &&
               owner->_jobs.ReservedMemoryBytes() == originalBytes && owner->_jobs.ReleasingMemoryBytes() == 0 &&
               owner->_jobs.IsIdle() && !owner->_layerRowsSource.HasReady(),
           "replacement-pressure-refuses-once-without-waiting-for-its-own-cache");
    expect(initialGate->captures.load() == 1 && initialGate->preparations.load() == 1 &&
               initialGate->publications.load() == 1 && owner->_layerRowsRetryAvailable,
           "refusal-captures-no-new-source-and-offers-explicit-retry");
    const auto dpi = GetDpiForWindow(owner->_window.get());
    const float scale = dpi == 0 ? 1.0f : static_cast<float>(dpi) / 96.0f;
    if (FAILED(owner->_chrome->Prepare(owner->_renderer.Width(), owner->_renderer.Height(),
                                       dpi == 0 ? 96.0f : static_cast<float>(dpi))))
        return 5;
    const auto retry = owner->_chrome->LayerRetryBoundsDipForTest();
    expect(retry.right > retry.left && retry.bottom > retry.top && owner->_chrome->UnnamedInteractiveControls().empty(),
           "actual-retry-control-is-visible-laid-out-and-accessibly-named");

    owner->OnCommand(ChromeHost::kCommandRetryLayerList);
    owner->_layerRowsSource.Pump();
    if (SettleJobs(owner->_jobs, 10000, dispatch, &pump) != SettleResult::Settled)
        return 5;
    expect(memory.samples == requestedSamples + 2 && !owner->_layerRowsTicket &&
               owner->_preparedLayerRows.get() == previous && retained == owner->_layerRowsCacheTicket &&
               owner->_jobs.ReservedMemoryBytes() == originalBytes && owner->_jobs.IsIdle() &&
               owner->_layerRowsRetryAvailable && !owner->_layerRowsSource.HasReady() &&
               initialGate->captures.load() == 1 && initialGate->preparations.load() == 1,
           "explicit-retry-that-is-still-unaffordable-refuses-once-and-preserves-the-old-cache");

    memory.snapshot = {3ULL << 30, 3ULL << 30};
    owner->_jobs.NotifyMemoryChanged();
    owner->SyncPreparedLayerRows();
    owner->_layerRowsSource.Pump();
    if (SettleJobs(owner->_jobs, 10000, dispatch, &pump) != SettleResult::Settled)
        return 5;
    expect(memory.samples == requestedSamples + 2 && !owner->_layerRowsTicket &&
               owner->_preparedLayerRows.get() == previous && owner->_layerRowsRetryAvailable &&
               owner->_jobs.ReservedMemoryBytes() == originalBytes,
           "memory-event-with-unchanged-key-does-not-rearm-refusal");

    auto retryGate = std::make_shared<LayerRowsTestGate>();
    retryGate->release.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!retryGate->release)
        return 5;
    retryGate->hold = true;
    retryGate->holdAfterPrepare = true;
    retryGate->reservationBytes = originalBytes + 1;
    const auto unblock = wil::scope_exit([retryGate]() noexcept { (void)SetEvent(retryGate->release.get()); });
    owner->_layerRowsTestGate = retryGate;
    pump.gate = retryGate;
    pump.previous = previous;
    // This tiny literal image fits at maximum zoom. Use the actual view command to leave room for Zoom In.
    std::printf("LAYER ROW RETRY before actual-size command: zoom=%.4f\n", owner->_viewport.zoom);
    owner->OnCommand(ChromeHost::kCommandZoomActual);
    // Refusal hid then restored the button, changing the pinned view's interaction revision. A real frame
    // prepares that geometry before the next pointer press; stale prepared input is correctly refused by DxUi.
    const HRESULT retryPrepared = owner->_chrome->Prepare(owner->_renderer.Width(), owner->_renderer.Height(),
                                                          dpi == 0 ? 96.0f : static_cast<float>(dpi));
    if (FAILED(retryPrepared))
    {
        std::printf("LAYER ROW RETRY pointer frame failed: result=%08lx\n", static_cast<unsigned long>(retryPrepared));
        return 5;
    }
    const auto currentRetry = owner->_chrome->LayerRetryBoundsDipForTest();
    const int x = static_cast<int>((currentRetry.left + currentRetry.right) * 0.5f * scale);
    const int y = static_cast<int>((currentRetry.top + currentRetry.bottom) * 0.5f * scale);
    owner->OnPointer(WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, y));
    owner->OnPointer(WM_LBUTTONUP, 0, MAKELPARAM(x, y));
    std::printf("LAYER ROW RETRY pointer: ready=%d retry=%d generation-preserved=%d\n",
                owner->_layerRowsSource.HasReady(), owner->_layerRowsRetryAvailable,
                owner->_session.Generation() == renamedGeneration);
    expect(!owner->_layerRowsRetryAvailable && owner->_layerRowsSource.HasReady() &&
               owner->_session.Generation() == renamedGeneration,
           "actual-button-pointer-requests-one-retry-without-document-mutation");
    owner->OnCommand(ChromeHost::kCommandRetryLayerList);
    owner->_layerRowsSource.Pump();
    expect(owner->_layerRowsTicket.has_value() && !owner->_layerRowsRetryAvailable,
           "retry-uses-a-fresh-original-ticket-and-hides-the-control");
    if (SettleJobs(owner->_jobs, 10000, dispatch, &pump) != SettleResult::Settled)
        return 5;
    expect(pump.held && pump.viewInput && pump.duplicateRetryBlocked,
           "view-input-continues-and-duplicate-retry-is-ignored-during-held-worker");
    expect(memory.samples == requestedSamples + 3 && retryGate->captures.load() == 1 &&
               retryGate->preparations.load() == 1 && retryGate->publications.load() == 1,
           "explicit-input-makes-exactly-one-fresh-admission-capture-prepare-and-publication");
    owner->SyncChrome();
    // A real frame publishes the replacement semantic snapshot before the previous epoch can retire.
    // Drain its whole-graph destruction and acknowledgement before testing the settled reservation.
    if (FAILED(owner->_chrome->Prepare(owner->_renderer.Width(), owner->_renderer.Height(),
                                       dpi == 0 ? 96.0f : static_cast<float>(dpi))) ||
        SettleJobs(owner->_jobs, 10000, dispatch, &pump) != SettleResult::Settled)
        return 5;
    expect(owner->_preparedLayerRows && owner->_preparedLayerRows.get() != previous &&
               owner->_preparedLayerRows->Rows().size() == 1 && owner->_preparedLayerRows->Rows()[0].id == layerId &&
               owner->_preparedLayerRows->Rows()[0].name == L"After retry" &&
               owner->_chrome->LayerRowItemForTest(0).text == L"After retry" &&
               retained != owner->_layerRowsCacheTicket && owner->_layerRowsCacheTicket && owner->_jobs.IsIdle() &&
               owner->_jobs.ReservedMemoryBytes() == owner->_preparedLayerRows->MemoryBytes() &&
               owner->_preparedLayerRows->ResidentBytes() == owner->_preparedLayerRows->MemoryBytes() &&
               owner->_jobs.ReleasingMemoryBytes() == 0,
           "only-current-renamed-rows-replace-the-borrow-and-original-charge");
    const auto hiddenRetry = owner->_chrome->LayerRetryBoundsDipForTest();
    expect(!owner->_layerRowsRetryAvailable && hiddenRetry.right == hiddenRetry.left &&
               hiddenRetry.bottom == hiddenRetry.top && !owner->_layerRowsSource.HasReady(),
           "successful-retry-leaves-no-retry-control-or-periodic-work");
    const auto& current = owner->_session.Document();
    bool exact = current && current->layers.size() == 1 && current->layers[0].asset && current->layers[0].mask &&
                 current->layers[0].mask->enabled && current->layers[0].name == L"After retry";
    if (exact)
    {
        for (std::size_t index = 0; index < pixels.size(); ++index)
            exact = exact && current->layers[0].asset->image.Data()[index] == pixels[index];
        for (std::size_t index = 0; index < coverage.size(); ++index)
            exact = exact && current->layers[0].mask->asset.image.Data()[index] == coverage[index];
    }
    expect(exact && owner->_session.history.CurrentRevision() == renamedRevision &&
               owner->_session.history.UndoCount() == renamedUndoCount &&
               owner->_session.ActiveLayerID() == std::optional<Guid>(layerId) && owner->_session.IsMaskSelected(),
           "refusal-and-pointer-retry-preserve-literal-pixels-mask-target-and-rename-history");
    expect(retryGate->retired.load() && retryGate->retiredOffOwner.load(),
           "retry-input-final-destruction-is-acknowledged-off-owner");
    owner->Shutdown();
    expect(owner->_shutdownComplete && owner->_jobs.WorkerCount() == 0 && owner->_jobs.ReservedMemoryBytes() == 0 &&
               !owner->_preparedLayerRows && !owner->_layerRowsCacheTicket && !owner->_layerRowsRetryAvailable,
           "shutdown-clears-the-borrow-cache-accounting-and-retry-state");
    // WM_QUIT is generated only after pending posted messages are drained. COM notification delivery may
    // leave messages after host disconnect; a quit-only peek cannot establish fixture closeout.
    const auto quit = PumpMessages();
    const auto remainingMessages = PumpMessages();
    expect(quit.quit && quit.exitCode == 0 && !remainingMessages.quit,
           "fixture-quit-consumed-before-the-next-fresh-owner");
    std::printf("%s application-prepared-layer-rows-retry.summary checks=%u failures=%d\n", failures ? "FAIL" : "PASS",
                checks, failures);
    return failures == 0 ? 0 : 5;
}
