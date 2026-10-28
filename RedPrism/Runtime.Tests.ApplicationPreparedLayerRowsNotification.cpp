#include "Application.h"

#include "ChromeHost.h"
#include "PreparedLayerRows.h"
#include "Resources.h"
#include "Tests/Support/Support.Tests.DocumentFixture.h"
#include "resource.h"

#include <array>
#include <atomic>
#include <cstdio>
#include <exception>
#include <memory>
#include <string_view>
#include <thread>
#include <utility>
#include <wrl.h>

namespace
{
// A real UIA client on a fresh MTA. Provider calls are COM-marshalled to the embedded owner's STA.
// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027)
class LayerRowsStructureObserver final
    : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
                                          IUIAutomationStructureChangedEventHandler, Microsoft::WRL::FtmBase>
{
  public:
    std::atomic<bool> armed{false};
    std::atomic<bool> publicationReturnedBeforeDelivery{false};
    std::atomic<HRESULT> readResult{E_PENDING};
    std::atomic<HRESULT> selectResult{E_PENDING};
    DWORD providerOwnerThread = 0;
    wil::com_ptr_nothrow<IUIAutomationElement> row;
    wil::com_ptr_nothrow<IUIAutomationSelectionItemPattern> selection;
    wil::unique_event_nothrow publicationReturned;
    wil::unique_event_nothrow continueDelivery;
    wil::unique_event_nothrow abandon;
    wil::unique_event_nothrow entered;
    wil::unique_event_nothrow completed;

    HRESULT Initialize() noexcept
    {
        RETURN_IF_FAILED(publicationReturned.create(wil::EventOptions::ManualReset));
        RETURN_IF_FAILED(continueDelivery.create(wil::EventOptions::ManualReset));
        RETURN_IF_FAILED(abandon.create(wil::EventOptions::ManualReset));
        RETURN_IF_FAILED(entered.create(wil::EventOptions::ManualReset));
        return completed.create(wil::EventOptions::ManualReset);
    }

    HRESULT STDMETHODCALLTYPE HandleStructureChangedEvent(IUIAutomationElement*, StructureChangeType change,
                                                          SAFEARRAY*) noexcept override
    {
        if (change != StructureChangeType_ChildrenInvalidated || !armed.exchange(false, std::memory_order_acq_rel))
            return S_OK;
        (void)SetEvent(entered.get());
        if (GetCurrentThreadId() == providerOwnerThread)
        {
            // UIA can invoke an agile client handler inline on the publishing STA. Fail immediately rather
            // than making a nested client proxy call that can deadlock the very owner under observation.
            readResult.store(RPC_E_WRONG_THREAD, std::memory_order_release);
            selectResult.store(RPC_E_WRONG_THREAD, std::memory_order_release);
            (void)SetEvent(completed.get());
            return S_OK;
        }
        const HANDLE gates[]{publicationReturned.get(), abandon.get()};
        // This is a failure bound, not a production timeout or a performance target. A synchronous publisher
        // cannot pass by waiting for the test callback: that is observed independently before client re-entry.
        const auto ready = WaitForMultipleObjects(2, gates, FALSE, 3000);
        publicationReturnedBeforeDelivery.store(ready == WAIT_OBJECT_0, std::memory_order_release);
        if (ready == WAIT_OBJECT_0 + 1)
        {
            (void)SetEvent(completed.get());
            return S_OK;
        }
        const HANDLE delivery[]{continueDelivery.get(), abandon.get()};
        const auto resumed = WaitForMultipleObjects(2, delivery, FALSE, 3000);
        if (resumed == WAIT_OBJECT_0 + 1)
        {
            (void)SetEvent(completed.get());
            return S_OK;
        }
        wil::unique_bstr name;
        HRESULT result = row->get_CurrentName(name.put());
        if (SUCCEEDED(result) &&
            (!name || std::wstring_view(name.get(), SysStringLen(name.get())) != L"Omega nouveau Ω"))
            result = E_FAIL;
        readResult.store(result, std::memory_order_release);
        selectResult.store(selection->Select(), std::memory_order_release);
        (void)SetEvent(completed.get());
        return S_OK;
    }
};
#pragma warning(pop)

} // namespace

int Application::RunStandalonePreparedLayerRowsNotificationSelfTest(HINSTANCE instance) noexcept
{
    unsigned int checks = 0;
    int failures = 0;
    const auto expect = [&](bool condition, const char* name) noexcept
    {
        ++checks;
        failures += condition ? 0 : 1;
        std::printf("%s application-prepared-layer-rows-notification.%s\n", condition ? "PASS" : "FAIL", name);
        std::fflush(stdout);
    };
    auto owner = std::make_unique<Application>(instance, true, nullptr, 0);
    const auto shutdown = wil::scope_exit([&]() noexcept { owner->Shutdown(); });
    owner->_quietUi = true;
    owner->_prompts = &QuietPrompts();
    if (FAILED(owner->InitializeProcessing()) || FAILED(owner->RegisterWindowClass()) ||
        FAILED(owner->CreateMainWindow(false)) || FAILED(owner->InitializeRenderer()) ||
        FAILED(owner->InitializeChrome()))
        return 5;
    owner->BindCompositionDevice();
    const auto prepare = [&]() noexcept
    {
        const auto dpi = GetDpiForWindow(owner->_window.get());
        return owner->_chrome->Prepare(owner->_renderer.Width(), owner->_renderer.Height(),
                                       dpi == 0 ? 96.0f : static_cast<float>(dpi));
    };
    expect(owner->_renderer.IsWarp() && owner->_jobs.WorkerCount() != 0,
           "fixture.real-hidden-warp-owner-with-live-retirement-sink");
    DocumentFixture document;
    document.id = Guid::Generate();
    document.width = 2;
    document.height = 1;
    constexpr std::array<std::uint8_t, 8> pixels{12, 34, 56, 255, 78, 90, 123, 255};
    const Raster image = Raster::Make(2, 1, RasterFormat::Bgra8888, pixels.data(), pixels.size());
    const std::array names{L"Omega Ω", L"Beta β", L"Alpha α"};
    for (const auto name : names)
    {
        auto layer = ImageLayer::Blank(name, {2, 1});
        layer.asset = RasterAsset{image, {}, name};
        document.layers.push_back(std::move(layer));
    }
    const Guid lastId = document.layers.front().id;
    const Guid firstId = document.layers.back().id;
    owner->_session.ReplaceDocument(std::move(document).Seal(), firstId);
    owner->_viewport.SetZoom(1, owner->_viewport.Center(), owner->DocumentSize());
    if (FAILED(prepare()) || !owner->EnablePreparedLayerRows() ||
        SettleJobs(owner->_jobs, 15000, DispatchMessageThunk, owner.get()) != SettleResult::Settled ||
        FAILED(prepare()))
        return 5;

    wil::com_ptr_nothrow<LayerRowsStructureObserver> observer;
    observer.attach(Microsoft::WRL::Make<LayerRowsStructureObserver>().Detach());
    if (observer)
        observer->providerOwnerThread = GetCurrentThreadId();
    wil::unique_event_nothrow ready, stop, finished;
    if (!observer || FAILED(observer->Initialize()) || FAILED(ready.create(wil::EventOptions::ManualReset)) ||
        FAILED(stop.create(wil::EventOptions::ManualReset)) || FAILED(finished.create(wil::EventOptions::ManualReset)))
        return 5;
    const auto treeName = LoadStringResource(nullptr, IDS_A11Y_LAYERS);
    const HWND window = owner->_window.get();
    std::atomic<HRESULT> setup{E_PENDING};
    std::jthread client(
        [&]() noexcept
        {
            const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            const auto uninitialize = wil::scope_exit(
                [&]() noexcept
                {
                    if (SUCCEEDED(initialized))
                        CoUninitialize();
                });
            wil::com_ptr_nothrow<IUIAutomation> automation;
            wil::com_ptr_nothrow<IUIAutomationElement> root, tree;
            HRESULT result = initialized;
            if (SUCCEEDED(result))
                result = CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
                                          IID_PPV_ARGS(automation.put()));
            if (SUCCEEDED(result))
                result = automation->ElementFromHandle(window, root.put());
            wil::unique_variant name, type;
            name.vt = VT_BSTR;
            name.bstrVal = SysAllocString(treeName.c_str());
            type.vt = VT_I4;
            type.lVal = UIA_TreeControlTypeId;
            wil::com_ptr_nothrow<IUIAutomationCondition> named, typed, both;
            if (SUCCEEDED(result) && !name.bstrVal)
                result = E_OUTOFMEMORY;
            if (SUCCEEDED(result))
                result = automation->CreatePropertyCondition(UIA_NamePropertyId, name, named.put());
            if (SUCCEEDED(result))
                result = automation->CreatePropertyCondition(UIA_ControlTypePropertyId, type, typed.put());
            if (SUCCEEDED(result))
                result = automation->CreateAndCondition(named.get(), typed.get(), both.put());
            if (SUCCEEDED(result))
                result = root->FindFirst(TreeScope_Descendants, both.get(), tree.put());
            if (SUCCEEDED(result) && !tree)
                result = E_FAIL;
            wil::com_ptr_nothrow<IUIAutomationTreeWalker> walker;
            if (SUCCEEDED(result))
                result = automation->get_RawViewWalker(walker.put());
            if (SUCCEEDED(result))
                result = walker->GetLastChildElement(tree.get(), observer->row.put());
            if (SUCCEEDED(result) && !observer->row)
                result = E_FAIL;
            if (SUCCEEDED(result))
                result = observer->row->GetCurrentPatternAs(UIA_SelectionItemPatternId,
                                                            IID_PPV_ARGS(observer->selection.put()));
            if (SUCCEEDED(result))
                result =
                    automation->AddStructureChangedEventHandler(root.get(), TreeScope_Subtree, nullptr, observer.get());
            setup.store(result, std::memory_order_release);
            (void)SetEvent(ready.get());
            if (SUCCEEDED(result))
            {
                (void)WaitForSingleObject(stop.get(), INFINITE);
                (void)automation->RemoveStructureChangedEventHandler(root.get(), observer.get());
            }
            observer->selection.reset();
            observer->row.reset();
            (void)SetEvent(finished.get());
        });
    const auto waitFor = [&](HANDLE event) noexcept
    {
        const auto deadline = GetTickCount64() + 15000;
        while (WaitForSingleObject(event, 0) != WAIT_OBJECT_0)
        {
            MSG message{};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
                owner->DispatchQueuedMessage(message);
            if (WaitForSingleObject(event, 0) == WAIT_OBJECT_0)
                return true;
            const auto now = GetTickCount64();
            if (now >= deadline)
                return false;
            const auto waited = MsgWaitForMultipleObjectsEx(1, &event, static_cast<DWORD>(deadline - now), QS_ALLINPUT,
                                                            MWMO_INPUTAVAILABLE);
            if (waited != WAIT_OBJECT_0 && waited != WAIT_OBJECT_0 + 1)
                return false;
        }
        return true;
    };
    const auto stopClient = wil::scope_exit(
        [&]() noexcept
        {
            (void)SetEvent(observer->abandon.get());
            (void)SetEvent(stop.get());
            if (!waitFor(finished.get()))
                std::terminate(); // A failed fixture must not join a client that needs the owner's COM message pump.
        });
    const bool subscribed = waitFor(ready.get()) && SUCCEEDED(setup.load(std::memory_order_acquire));
    expect(subscribed, "client.fresh-mta-subscribes-through-the-real-window-provider");
    if (!subscribed)
        return 5;
    const auto serial = owner->_session.DocumentSerial();
    owner->_session.RenameLayer(lastId, L"Omega nouveau Ω");
    const auto historyRevision = owner->_session.history.CurrentRevision();
    owner->SyncPreparedLayerRows();
    if (SettleJobs(owner->_jobs, 15000, DispatchMessageThunk, owner.get()) != SettleResult::Settled)
        return 5;
    observer->armed.store(true, std::memory_order_release);
    const HRESULT published = prepare();
    (void)SetEvent(observer->publicationReturned.get());
    const bool entered = waitFor(observer->entered.get());
    const auto zoom = owner->_viewport.zoom;
    owner->OnCommand(ChromeHost::kCommandZoomIn);
    expect(entered && owner->_viewport.zoom != zoom &&
               WaitForSingleObject(observer->completed.get(), 0) == WAIT_TIMEOUT &&
               owner->_session.ActiveLayerID() == std::optional<Guid>(firstId),
           "publication.view-input-runs-while-the-real-client-callback-is-held");
    (void)SetEvent(observer->continueDelivery.get());
    const bool completed = waitFor(observer->completed.get());
    expect(SUCCEEDED(published) && completed, "publication.same-count-same-ids-deliver-a-real-structure-event");
    expect(observer->publicationReturnedBeforeDelivery.load(std::memory_order_acquire),
           "publication.owner-returns-before-held-client-delivery-is-released");
    expect(observer->readResult.load(std::memory_order_acquire) == S_OK,
           "callback.retained-client-row-reads-the-current-full-unicode-name");
    expect(observer->selectResult.load(std::memory_order_acquire) == S_OK &&
               owner->_session.ActiveLayerID() == std::optional<Guid>(lastId) && !owner->_session.IsMaskSelected(),
           "callback.real-selection-pattern-reenters-the-owner-and-selects-once");
    bool literal = owner->_session.Document() && owner->_session.Document()->layers.size() == 3;
    if (literal)
        for (const auto& layer : owner->_session.Document()->layers)
            literal = literal && layer.asset && layer.asset->image == image;
    expect(literal && owner->_session.DocumentSerial() == serial &&
               owner->_session.history.CurrentRevision() == historyRevision && owner->_session.history.UndoCount() == 1,
           "callback.selection-and-view-input-preserve-pixels-and-one-rename-history-entry");
    (void)SetEvent(stop.get());
    const bool stopped = waitFor(finished.get());
    expect(stopped, "client.unsubscribes-with-owner-pumping-before-join-or-shutdown");
    if (!stopped)
        std::terminate();
    client.join();
    owner->Shutdown();
    expect(owner->_jobs.ReservedMemoryBytes() == 0 && owner->_jobs.ReleasingMemoryBytes() == 0,
           "shutdown.retires-all-admitted-source-epochs-after-client-unsubscription");
    // Drain queued messages before observing the low-priority WM_QUIT, including COM disconnect messages.
    const auto quit = PumpMessages();
    const auto remainingMessages = PumpMessages();
    expect(quit.quit && quit.exitCode == 0 && !remainingMessages.quit,
           "shutdown.fixture-consumes-one-quit-before-the-next-owner");
    std::printf("%s application-prepared-layer-rows-notification.summary checks=%u failures=%d\n",
                failures ? "FAIL" : "PASS", checks, failures);
    std::fflush(stdout);
    return failures ? 5 : 0;
}
