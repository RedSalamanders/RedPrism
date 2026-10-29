#include "Application.h"

#include "ChromeHost.h"
#include "PreparedLayerRows.h"
#include "Resources.h"
#include "Runtime.Tests.LayerRowsTestGate.h"
#include "Tests/Support/Support.Tests.PreparedLayerRowsStressFixture.h"
#include "resource.h"

#include <array>
#include <atomic>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace
{
[[nodiscard]] std::wstring ProviderName(IRawElementProviderFragment* fragment)
{
    if (!fragment)
        return {};
    wil::com_ptr_nothrow<IRawElementProviderSimple> simple;
    wil::unique_variant value;
    if (FAILED(fragment->QueryInterface(IID_PPV_ARGS(simple.put()))) ||
        FAILED(simple->GetPropertyValue(UIA_NamePropertyId, &value)) || value.vt != VT_BSTR)
        return {};
    return value.bstrVal ? std::wstring(value.bstrVal, SysStringLen(value.bstrVal)) : std::wstring{};
}

[[nodiscard]] wil::com_ptr_nothrow<IRawElementProviderFragment> FindLayerTree(IRawElementProviderFragment* parent,
                                                                              std::wstring_view name,
                                                                              unsigned int& remaining)
{
    wil::com_ptr_nothrow<IRawElementProviderFragment> found;
    if (!parent || remaining == 0)
        return found;
    --remaining;
    wil::com_ptr_nothrow<IRawElementProviderSimple> simple;
    wil::unique_variant type;
    if (SUCCEEDED(parent->QueryInterface(IID_PPV_ARGS(simple.put()))) &&
        SUCCEEDED(simple->GetPropertyValue(UIA_ControlTypePropertyId, &type)) && type.vt == VT_I4 &&
        type.lVal == UIA_TreeControlTypeId && ProviderName(parent) == name)
    {
        (void)parent->QueryInterface(IID_PPV_ARGS(found.put()));
        return found;
    }
    wil::com_ptr_nothrow<IRawElementProviderFragment> child;
    if (FAILED(parent->Navigate(NavigateDirection_FirstChild, child.put())))
        return found;
    while (child && remaining != 0)
    {
        found = FindLayerTree(child.get(), name, remaining);
        if (found)
            return found;
        wil::com_ptr_nothrow<IRawElementProviderFragment> next;
        if (FAILED(child->Navigate(NavigateDirection_NextSibling, next.put())))
            break;
        child = std::move(next);
    }
    return found;
}
} // namespace

// Literal names/order, real embedded provider ownership and foreign CPU readers. Timing is a separate Release
// gate; constructing this fixture on the test owner does not qualify production capture or G1.
int Application::RunStandalonePreparedLayerRowsAccessibilitySelfTest(HINSTANCE instance) noexcept
{
    unsigned int checks = 0;
    int failures = 0;
    const auto expect = [&](bool condition, const char* name) noexcept
    {
        ++checks;
        failures += condition ? 0 : 1;
        std::printf("%s application-prepared-layer-rows-accessibility.%s\n", condition ? "PASS" : "FAIL", name);
        std::fflush(stdout);
    };
    auto owner = std::make_unique<Application>(instance, true, nullptr, 0);
    const auto cleanup = wil::scope_exit(
        [&]() noexcept
        {
            if (owner)
                owner->Shutdown();
        });
    owner->_quietUi = true;
    owner->_prompts = &QuietPrompts();
    if (FAILED(owner->InitializeProcessing()) || FAILED(owner->RegisterWindowClass()) ||
        FAILED(owner->CreateMainWindow(false)) || FAILED(owner->InitializeRenderer()) ||
        FAILED(owner->InitializeChrome()))
        return 5;
    owner->BindCompositionDevice();
    const auto prepareChrome = [&]() noexcept
    {
        const UINT dpi = GetDpiForWindow(owner->_window.get());
        return owner->_chrome->Prepare(owner->_renderer.Width(), owner->_renderer.Height(),
                                       dpi == 0 ? 96.0f : static_cast<float>(dpi));
    };
    expect(owner->_renderer.IsWarp() && owner->_chrome && owner->_jobs.WorkerCount() != 0,
           "fixture.real-hidden-warp-owner-and-scheduler");
    auto fixture = MakePreparedLayerRowsStressFixture();
    owner->_session.ReplaceDocument(std::move(fixture.builder).Seal(), fixture.last.id);
    const auto historyRevision = owner->_session.history.CurrentRevision();
    const auto documentSerial = owner->_session.DocumentSerial();
    if (!owner->EnablePreparedLayerRows() ||
        SettleJobs(owner->_jobs, 15000, DispatchMessageThunk, owner.get()) != SettleResult::Settled)
        return 5;
    auto firstEpoch = owner->_preparedLayerRows;
    if (!firstEpoch)
        return 5;
    const auto firstBytes = firstEpoch->MemoryBytes();
    std::weak_ptr<const PreparedLayerRows> oldWitness = firstEpoch;
    expect(firstEpoch->HasMemoryLease() && firstEpoch->GetCount() == 10000 && firstBytes != 0 &&
               owner->_jobs.ReservedMemoryBytes() == firstBytes && owner->_jobs.IsIdle(),
           "initial.complete-10000-row-epoch-retains-original-admission");
    const auto firstLayerItemId = firstEpoch->ItemIdAt(fixture.first.topFirstRow);
    const auto middleLayerItemId = firstEpoch->ItemIdAt(fixture.middle.topFirstRow);
    const auto last = firstEpoch->GetItem(fixture.first.topFirstRow);
    const auto middle = firstEpoch->FindItem(middleLayerItemId);
    const std::wstring prefix(1018, L'N');
    expect(firstLayerItemId == PreparedLayerRows::FirstItemIdentity + fixture.first.storageIndex &&
               middleLayerItemId == PreparedLayerRows::FirstItemIdentity + fixture.middle.storageIndex && last &&
               last->itemId == firstLayerItemId && last->text == prefix + L"#00000" && middle &&
               middle->itemId == middleLayerItemId && middle->text == prefix + L"#05000" &&
               middle->visibleIndex == fixture.middle.topFirstRow,
           "initial.full-1024-unit-offscreen-names-and-independent-order");
    expect(
        !firstEpoch->GetItem(PreparedLayerRowsStressFixture::kLayerCount) && !firstEpoch->FindItem(0) &&
            !firstEpoch->FindItem(PreparedLayerRows::FirstItemIdentity + PreparedLayerRowsStressFixture::kLayerCount),
        "initial.absent-indices-and-ids-are-refused");
    expect(owner->_chrome->PreparedLayerAccessibilityForTest().get() == firstEpoch.get(),
           "initial.real-tree-captures-the-original-owner-without-a-row-copy");
    if (FAILED(prepareChrome()))
        return 5;
    wil::com_ptr_nothrow<IRawElementProviderFragment> root;
    auto* rawRoot = owner->_chrome->AccessibilityRoot();
    if (!rawRoot || FAILED(rawRoot->QueryInterface(IID_PPV_ARGS(root.put()))))
        return 5;
    unsigned int remaining = 512;
    auto tree = FindLayerTree(root.get(), LoadStringResource(nullptr, IDS_A11Y_LAYERS), remaining);
    wil::com_ptr_nothrow<IRawElementProviderFragment> lastProvider;
    if (!tree || FAILED(tree->Navigate(NavigateDirection_LastChild, lastProvider.put())) || !lastProvider)
        return 5;
    expect(ProviderName(lastProvider.get()) == prefix + L"#00000",
           "provider.real-embedded-last-child-exposes-full-offscreen-name");
    wil::com_ptr_nothrow<IRawElementProviderFragment> previousProvider;
    expect(SUCCEEDED(lastProvider->Navigate(NavigateDirection_PreviousSibling, previousProvider.put())) &&
               ProviderName(previousProvider.get()) == prefix + L"#00001",
           "provider.real-sibling-navigation-preserves-literal-top-first-order");
    previousProvider.reset();
    tree.reset();
    firstEpoch.reset();

    owner->_session.RenameLayer(fixture.first.id, L"Replacement α Ω");
    const auto renamedRevision = owner->_session.history.CurrentRevision();
    const auto renamedCount = owner->_session.history.UndoCount();
    owner->SyncPreparedLayerRows();
    if (SettleJobs(owner->_jobs, 15000, DispatchMessageThunk, owner.get()) != SettleResult::Settled ||
        FAILED(prepareChrome()))
        return 5;
    (void)owner->_chrome->AccessibilityRoot();
    auto secondEpoch = owner->_preparedLayerRows;
    if (!secondEpoch)
        return 5;
    expect(!oldWitness.expired() && owner->_jobs.ReservedMemoryBytes() == firstBytes + secondEpoch->MemoryBytes(),
           "replacement.retained-provider-keeps-both-original-epoch-charges");
    const auto replacementItem = secondEpoch->FindItem(firstLayerItemId);
    expect(ProviderName(lastProvider.get()) == L"Replacement α Ω" && replacementItem &&
               replacementItem->text == L"Replacement α Ω",
           "replacement.same-id-provider-reads-acknowledged-unicode-name");
    expect(owner->_session.DocumentSerial() == documentSerial && renamedCount == 1 &&
               owner->_session.history.CurrentRevision() == renamedRevision && historyRevision != renamedRevision,
           "replacement.preparation-preserves-the-single-rename-history-entry");
    lastProvider.reset();
    root.reset();
    if (SettleJobs(owner->_jobs, 15000, DispatchMessageThunk, owner.get()) != SettleResult::Settled)
        return 5;
    const bool oldGraphDestroyed = oldWitness.expired();
    expect(oldGraphDestroyed && owner->_jobs.ReservedMemoryBytes() == firstBytes + secondEpoch->MemoryBytes() &&
               owner->_jobs.ReleasingMemoryBytes() == 0,
           "replacement.weak-control-witness-keeps-complete-original-charge-after-data-retirement");
    oldWitness.reset(); // The remaining control block is part of the admitted graph too.
    if (SettleJobs(owner->_jobs, 15000, DispatchMessageThunk, owner.get()) != SettleResult::Settled)
        return 5;
    expect(oldGraphDestroyed && owner->_jobs.ReservedMemoryBytes() == secondEpoch->MemoryBytes() &&
               owner->_jobs.ReleasingMemoryBytes() == 0,
           "replacement.final-old-provider-release-retires-graph-and-acknowledges-exact-charge");

    auto start = wil::unique_event_nothrow{};
    auto finished = wil::unique_event_nothrow{};
    if (FAILED(start.create(wil::EventOptions::ManualReset)) || FAILED(finished.create(wil::EventOptions::ManualReset)))
        return 5;
    std::atomic<bool> foreignRead{false};
    std::weak_ptr<const PreparedLayerRows> finalWitness = secondEpoch;
    std::thread foreign(
        [reader = std::move(owner->_preparedLayerRows), begin = start.get(), end = finished.get(), &foreignRead,
         firstLayerItemId]() mutable noexcept
        {
            const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            if (WaitForSingleObject(begin, 15000) == WAIT_OBJECT_0)
            {
                const auto item = reader->FindItem(firstLayerItemId);
                foreignRead.store(initialized == S_OK && item && item->text == L"Replacement α Ω" &&
                                      reader->GetCount() == 10000 && reader->HasMemoryLease(),
                                  std::memory_order_release);
            }
            reader.reset();
            if (SUCCEEDED(initialized))
                CoUninitialize();
            (void)SetEvent(end);
        });
    const auto join = wil::scope_exit(
        [&]() noexcept
        {
            (void)SetEvent(start.get());
            foreign.join();
        });
    owner->Shutdown();
    expect(owner->_jobs.ReservedMemoryBytes() == 0 && !finalWitness.expired(),
           "shutdown.detaches-ledger-with-foreign-immutable-reader-still-alive");
    owner.reset();
    // Drain queued messages before observing the low-priority WM_QUIT, including COM disconnect messages.
    const auto quit = PumpMessages();
    const auto remainingMessages = PumpMessages();
    expect(quit.quit && quit.exitCode == 0 && !remainingMessages.quit,
           "shutdown.fixture-consumes-one-quit-before-the-next-owner");
    // secondEpoch intentionally also owns the graph until this assertion, then the MTA becomes final owner.
    auto survivor = std::move(secondEpoch);
    const auto survivorItem = survivor->FindItem(firstLayerItemId);
    expect(survivorItem && survivorItem->text == L"Replacement α Ω" && survivor->ResidentBytes() != 0,
           "shutdown.complete-source-and-owned-endpoint-outlive-application");
    survivor.reset();
    (void)SetEvent(start.get());
    expect(WaitForSingleObject(finished.get(), 15000) == WAIT_OBJECT_0 && foreignRead.load(std::memory_order_acquire),
           "shutdown.fresh-mta-reads-full-source-after-owner-destruction");
    expect(finalWitness.expired(), "shutdown.final-mta-release-retires-complete-graph-without-owner");
    std::printf("%s application-prepared-layer-rows-accessibility.summary checks=%u failures=%d\n",
                failures ? "FAIL" : "PASS", checks, failures);
    std::fflush(stdout);
    return failures ? 5 : 0;
}

int Application::RunStandalonePreparedLayerRowsCapacitySelfTest(HINSTANCE instance) noexcept
{
    unsigned int checks = 0;
    int failures = 0;
    const auto expect = [&](bool condition, const char* name) noexcept
    {
        ++checks;
        failures += condition ? 0 : 1;
        std::printf("%s application-prepared-layer-rows-capacity.%s\n", condition ? "PASS" : "FAIL", name);
        std::fflush(stdout);
    };
    auto owner = std::make_unique<Application>(instance, true, nullptr, 0);
    const auto cleanup = wil::scope_exit([&]() noexcept { owner->Shutdown(); });
    owner->_quietUi = true;
    owner->_prompts = &QuietPrompts();
    if (FAILED(owner->InitializeProcessing()) || FAILED(owner->RegisterWindowClass()) ||
        FAILED(owner->CreateMainWindow(false)) || FAILED(owner->InitializeRenderer()) ||
        FAILED(owner->InitializeChrome()))
        return 5;
    owner->BindCompositionDevice();
    DocumentFixture fixture;
    fixture.id = Guid::Generate();
    fixture.width = 2;
    fixture.height = 1;
    constexpr std::array<std::uint8_t, 8> pixels{10, 20, 30, 255, 40, 50, 60, 255};
    ImageLayer layer = ImageLayer::Blank(L"Epoch 0", {2, 1});
    const Guid id = layer.id;
    layer.asset = RasterAsset{Raster::Make(2, 1, RasterFormat::Bgra8888, pixels.data(), pixels.size()), {}, L"Pixels"};
    fixture.layers.push_back(std::move(layer));
    owner->_session.ReplaceDocument(std::move(fixture).Seal(), id);
    auto gate = std::make_shared<LayerRowsTestGate>();
    gate->hold = false;
    owner->_layerRowsTestGate = gate;
    if (!owner->EnablePreparedLayerRows())
        return 5;
    std::vector<std::shared_ptr<const PreparedLayerRows>> epochs;
    epochs.reserve(64);
    std::uint64_t originalCharges = 0;
    std::uint64_t stableItemId = 0;
    bool complete = true;
    for (unsigned int epoch = 0; epoch < 64; ++epoch)
    {
        const auto name = L"Epoch " + std::to_wstring(epoch);
        if (epoch != 0)
        {
            owner->_session.RenameLayer(id, name);
            owner->SyncPreparedLayerRows();
        }
        if (SettleJobs(owner->_jobs, 10000, DispatchMessageThunk, owner.get()) != SettleResult::Settled)
            return 5;
        const auto rows = owner->_preparedLayerRows;
        if (epoch == 0 && rows)
            stableItemId = rows->ItemIdAt(0);
        const auto item = rows ? rows->FindItem(stableItemId) : std::nullopt;
        if (!rows || stableItemId != PreparedLayerRows::FirstItemIdentity || !item || item->itemId != stableItemId ||
            item->text != name || !rows->HasMemoryLease())
        {
            complete = false;
            break;
        }
        originalCharges += rows->MemoryBytes();
        epochs.push_back(rows);
    }
    expect(complete && epochs.size() == 64 && gate->publications.load() == 64 &&
               owner->_jobs.ReservedMemoryBytes() == originalCharges && owner->_jobs.IsIdle(),
           "retained-64-real-publications-keep-their-exact-original-charges-while-idle");
    if (!complete || epochs.size() != 64)
        return 5;
    const auto* previous = owner->_preparedLayerRows.get();
    const auto previousTicket = owner->_layerRowsCacheTicket;
    owner->_session.RenameLayer(id, L"Capacity retry α Ω");
    const Guid revision = owner->_session.history.CurrentRevision();
    const int undoCount = owner->_session.history.UndoCount();
    const auto generation = owner->_session.Generation();
    owner->SyncPreparedLayerRows();
    if (SettleJobs(owner->_jobs, 10000, DispatchMessageThunk, owner.get()) != SettleResult::Settled)
        return 5;
    expect(gate->preparations.load() == 65 && gate->publications.load() == 64 &&
               owner->_preparedLayerRows.get() == previous && owner->_layerRowsCacheTicket == previousTicket &&
               !owner->_layerRowsTicket && owner->_layerRowsRetryAvailable && owner->_jobs.IsIdle() &&
               owner->_jobs.ReservedMemoryBytes() == originalCharges && owner->_jobs.ReleasingMemoryBytes() == 0,
           "publication-65-refuses-exhausted-retained-slots-and-preserves-the-old-cache");
    expect(owner->_chrome->LayerRowItemForTest(0).text == L"Epoch 63" &&
               owner->_chrome->PreparedLayerAccessibilityForTest().get() == previous &&
               !owner->_layerRowsSource.HasReady(),
           "refusal-keeps-the-complete-old-chrome-source-and-offers-no-periodic-retry");
    const auto releasedCharge = epochs.front()->MemoryBytes();
    epochs.front().reset();
    // Even if the worker has already destroyed the graph, only an owner acknowledgement returns capacity.
    expect(owner->_jobs.ReservedMemoryBytes() == originalCharges,
           "final-owner-release-does-not-uncharge-before-owner-acknowledgement");
    if (SettleJobs(owner->_jobs, 10000, DispatchMessageThunk, owner.get()) != SettleResult::Settled)
        return 5;
    expect(owner->_jobs.ReservedMemoryBytes() == originalCharges - releasedCharge &&
               owner->_jobs.ReleasingMemoryBytes() == 0 && owner->_jobs.IsIdle(),
           "whole-graph-retirement-acknowledges-one-exact-original-reservation");
    owner->_jobs.NotifyMemoryChanged();
    owner->SyncPreparedLayerRows();
    if (SettleJobs(owner->_jobs, 10000, DispatchMessageThunk, owner.get()) != SettleResult::Settled)
        return 5;
    expect(gate->preparations.load() == 65 && gate->publications.load() == 64 && owner->_layerRowsRetryAvailable &&
               !owner->_layerRowsSource.HasReady() && owner->_preparedLayerRows.get() == previous,
           "returned-capacity-and-memory-event-do-not-rearm-an-unchanged-refused-key");
    owner->OnCommand(ChromeHost::kCommandRetryLayerList);
    owner->_layerRowsSource.Pump();
    const auto retryTicket = owner->_layerRowsTicket;
    expect(retryTicket && retryTicket != previousTicket && !owner->_layerRowsRetryAvailable,
           "explicit-retry-submits-one-fresh-original-ticket");
    if (SettleJobs(owner->_jobs, 10000, DispatchMessageThunk, owner.get()) != SettleResult::Settled)
        return 5;
    const auto current = owner->_preparedLayerRows;
    const auto currentItem = current ? current->FindItem(stableItemId) : std::nullopt;
    expect(current && current.get() != previous && currentItem && currentItem->text == L"Capacity retry α Ω" &&
               owner->_layerRowsCacheTicket == retryTicket && gate->preparations.load() == 66 &&
               gate->publications.load() == 65 && !owner->_layerRowsRetryAvailable && owner->_jobs.IsIdle() &&
               owner->_jobs.ReservedMemoryBytes() == originalCharges - releasedCharge + current->MemoryBytes(),
           "retry-reuses-the-acknowledged-slot-and-publishes-only-the-current-source");
    const auto& document = std::as_const(owner->_session).Document();
    bool exactPixels = document && document->layers.size() == 1 && document->layers[0].asset;
    if (exactPixels)
    {
        std::array<std::uint8_t, pixels.size()> read{};
        document->layers[0].asset->image.ReadRegion(0, 0, 2, 1, read.data(), 8, 0);
        exactPixels = read == pixels;
    }
    expect(exactPixels && owner->_session.Generation() == generation &&
               owner->_session.history.CurrentRevision() == revision &&
               owner->_session.history.UndoCount() == undoCount,
           "slot-refusal-retirement-and-retry-preserve-literal-pixels-and-rename-history");
    epochs.clear();
    if (SettleJobs(owner->_jobs, 10000, DispatchMessageThunk, owner.get()) != SettleResult::Settled)
        return 5;
    expect(current && owner->_jobs.ReservedMemoryBytes() == current->MemoryBytes() &&
               owner->_jobs.ReleasingMemoryBytes() == 0 && owner->_jobs.IsIdle(),
           "releasing-old-epochs-retains-only-the-current-original-reservation");
    owner->Shutdown();
    expect(owner->_jobs.ReservedMemoryBytes() == 0 && !owner->_preparedLayerRows && !owner->_layerRowsCacheTicket &&
               current && current->FindItem(stableItemId) &&
               current->FindItem(stableItemId)->text == L"Capacity retry α Ω",
           "shutdown-detaches-the-ledger-with-a-readable-external-current-epoch");
    // Drain queued messages before observing the low-priority WM_QUIT, including COM disconnect messages.
    const auto quit = PumpMessages();
    const auto remainingMessages = PumpMessages();
    expect(quit.quit && quit.exitCode == 0 && !remainingMessages.quit,
           "fixture-consumes-one-quit-before-the-next-owner");
    std::printf("%s application-prepared-layer-rows-capacity.summary checks=%u failures=%d\n",
                failures ? "FAIL" : "PASS", checks, failures);
    std::fflush(stdout);
    return failures ? 5 : 0;
}
