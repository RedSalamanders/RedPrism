#include "Application.h"

#include "ApplicationShutdownTestProbe.h"

#include "../Tests/Support/DocumentFixture.h"
#include "ChromeHost.h"
#include "ClipboardWorkTestGate.h"
#include "SystemClipboard.h"

#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

namespace
{
constexpr UINT kRenderEnteredMessage = WM_APP + 56;
constexpr wchar_t kProducerClass[] = L"RedPrismClipboardHostDelayedProducer";
constexpr std::array<std::uint8_t, 44> kDib{40, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 32, 0, 0, 0, 0,  0,  4,  0,
                                            0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 17, 33, 65, 255};
struct DelayedClipboard final
{
    wil::unique_handle ready{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    wil::unique_handle release{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    DWORD ownerThread = 0;
    std::shared_ptr<ClipboardWorkTestGate> gate;
    std::atomic<HWND> window{nullptr};
    std::atomic<DWORD> thread{0};
    std::atomic<bool> installed{false}, rendered{false};
    std::atomic<unsigned int> renders{0};
    std::atomic<unsigned int> unrelatedReads{0};
    std::atomic<ULONGLONG> enteredAt{0}, releasedAt{0}, completedAt{0};
    std::atomic<DWORD> releaseWait{WAIT_FAILED}, publicationError{ERROR_SUCCESS};
};
bool OpenProducer(HWND window) noexcept
{
    wil::unique_handle timer(CreateWaitableTimerW(nullptr, TRUE, nullptr));
    if (!timer)
        return false;
    for (unsigned int attempt = 0; attempt < 4; ++attempt)
    {
        if (OpenClipboard(window))
            return true;
        if (attempt == 3)
            return false;
        LARGE_INTEGER due{};
        due.QuadPart = -1'000'000;
        if (!SetWaitableTimer(timer.get(), &due, 0, nullptr, nullptr, FALSE) ||
            WaitForSingleObject(timer.get(), 1000) != WAIT_OBJECT_0)
            return false;
    }
    return false;
}
LRESULT CALLBACK ProducerProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) noexcept
{
    auto* producer = reinterpret_cast<DelayedClipboard*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE)
    {
        producer = static_cast<DelayedClipboard*>(reinterpret_cast<const CREATESTRUCTW*>(lParam)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(producer));
    }
    if (message == WM_RENDERFORMAT && producer && wParam == CF_DIB)
    {
        // A clipboard listener can request delayed bytes immediately after installation. Only the actual
        // admitted worker's read participates in this gate. Its observer runs while it owns OpenClipboard;
        // the global clipboard lock excludes a different reader between this boundary and GetClipboardData.
        if (!producer->gate || !producer->gate->workerDibRead.load())
        {
            producer->unrelatedReads.fetch_add(1);
            return 0;
        }
        producer->renders.fetch_add(1);
        producer->enteredAt.store(GetTickCount64());
        if (!PostThreadMessageW(producer->ownerThread, kRenderEnteredMessage, 0, 0))
            return 0;
        const DWORD waited = WaitForSingleObject(producer->release.get(), 15000);
        producer->releaseWait.store(waited);
        producer->releasedAt.store(GetTickCount64());
        if (waited != WAIT_OBJECT_0)
            return 0;
        wil::unique_hglobal block(GlobalAlloc(GMEM_MOVEABLE, kDib.size()));
        if (!block)
            return 0;
        {
            const wil::unique_hglobal_locked locked(block.get());
            if (!locked.get())
                return 0;
            std::memset(locked.get(), 0, GlobalSize(block.get()));
            std::memcpy(locked.get(), kDib.data(), kDib.size());
        }
        SetLastError(ERROR_SUCCESS);
        if (SetClipboardData(CF_DIB, block.get()))
        {
            (void)block.release();
            producer->rendered.store(true);
        }
        else
            producer->publicationError.store(GetLastError());
        producer->completedAt.store(GetTickCount64());
        return 0;
    }
    if (message == WM_CLOSE || message == WM_DESTROY)
    {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
DWORD WINAPI ProducerMain(void* raw) noexcept
{
    auto& producer = *static_cast<DelayedClipboard*>(raw);
    producer.thread.store(GetCurrentThreadId());
    WNDCLASSEXW klass{sizeof(klass)};
    klass.lpfnWndProc = ProducerProc;
    klass.hInstance = GetModuleHandleW(nullptr);
    klass.lpszClassName = kProducerClass;
    const bool registered = RegisterClassExW(&klass) != 0;
    if (!registered && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    {
        (void)SetEvent(producer.ready.get());
        return 1;
    }
    auto unregister = wil::scope_exit(
        [&]() noexcept
        {
            if (registered)
                (void)UnregisterClassW(kProducerClass, klass.hInstance);
        });
    wil::unique_hwnd window(
        CreateWindowExW(0, kProducerClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, klass.hInstance, &producer));
    producer.window.store(window.get());
    if (window && OpenProducer(window.get()))
    {
        const bool emptied = EmptyClipboard() != FALSE;
        if (emptied)
            (void)SetClipboardData(CF_DIB, nullptr);
        const bool offered = emptied && IsClipboardFormatAvailable(CF_DIB) != FALSE;
        const bool closed = CloseClipboard() != FALSE;
        producer.installed.store(offered && closed);
    }
    (void)SetEvent(producer.ready.get());
    if (!window)
        return 1;
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    producer.window.store(nullptr);
    window.reset();
    return 0;
}
void JoinClipboardThread(HANDLE thread) noexcept
{
    if (thread && WaitForSingleObject(thread, 15000) != WAIT_OBJECT_0)
    {
        std::fprintf(stderr, "FAIL clipboard-delayed.thread-join\n");
        std::_Exit(5);
    }
}
struct ReleaseAfterStop final
{
    HANDLE stopped = nullptr;
    HANDLE release = nullptr;
    std::atomic<bool> observed{false};
};
DWORD WINAPI ReleaseOnStop(void* raw) noexcept
{
    auto& state = *static_cast<ReleaseAfterStop*>(raw);
    if (WaitForSingleObject(state.stopped, 15000) == WAIT_OBJECT_0)
    {
        state.observed.store(true);
        (void)SetEvent(state.release);
        return 0;
    }
    return 1;
}
} // namespace

int Application::RunClipboardDelayedSelfTest() noexcept
{
    unsigned int checks = 0;
    int failures = 0;
    const auto expect = [&](bool passed, const char* name, const char* fact) noexcept
    {
        ++checks;
        failures += passed ? 0 : 1;
        std::printf("%s clipboard-delayed.%s.%s\n", passed ? "PASS" : "FAIL", name, fact);
    };
    SystemClipboard::SavedContents saved;
    if (!SystemClipboard::SaveContents(_window.get(), saved))
        return 5;
    auto restore = wil::scope_exit([&]() noexcept { (void)SystemClipboard::RestoreContents(_window.get(), saved); });
    enum class Action
    {
        Paste,
        Prefill
    };
    enum class Mode
    {
        Success,
        Cancel,
        Replacement,
        Shutdown
    };
    struct Scenario
    {
        const char* name;
        Action action;
        Mode mode;
    };
    constexpr std::array scenarios{
        Scenario{"paste", Action::Paste, Mode::Success},
        Scenario{"paste-cancel", Action::Paste, Mode::Cancel},
        Scenario{"paste-replacement", Action::Paste, Mode::Replacement},
        Scenario{"prefill", Action::Prefill, Mode::Success},
        Scenario{"prefill-cancel", Action::Prefill, Mode::Cancel},
        Scenario{"paste-shutdown", Action::Paste, Mode::Shutdown},
        Scenario{"prefill-shutdown", Action::Prefill, Mode::Shutdown},
    };
    struct State final
    {
        Application& owner;
        const Scenario& scenario;
        DelayedClipboard& producer;
        std::shared_ptr<ClipboardWorkTestGate> gate;
        ContinuationReceipt receipt;
        Guid revision;
        bool visited = false, held = false, viewDispatched = false, retainedUntilAcknowledged = false;
        bool shutdownObserved = false;
        ULONGLONG dispatchedAt = 0, openedAt = 0;
    };
    const auto dispatch = [](void* raw, void* native) noexcept
    {
        auto& state = *static_cast<State*>(raw);
        auto& owner = state.owner;
        auto& message = *static_cast<MSG*>(native);
        if (message.message != kRenderEnteredMessage)
        {
            owner.DispatchQueuedMessage(message);
            return;
        }
        state.visited = true;
        state.dispatchedAt = GetTickCount64();
        const bool paste = state.scenario.action == Action::Paste;
        state.held = owner._jobs.ReservedMemoryBytes() > 0 &&
                     (paste ? owner._clipboardTicket.has_value() && owner._historyWork.Busy() &&
                                  state.receipt.Read().outcome == HostOutcome::Pending
                            : owner._clipboardPrefillTicket.has_value() && owner.SizeCardOpen() &&
                                  !owner._historyWork.Busy() && !owner._activeHostRequest);
        if (state.scenario.mode != Mode::Shutdown)
        {
            const double zoom = owner._viewport.ViewScale();
            owner.OnCommand(ChromeHost::kCommandZoomIn);
            state.viewDispatched = owner._viewport.ViewScale() != zoom;
        }
        switch (state.scenario.mode)
        {
        case Mode::Cancel:
            if (paste)
                owner.CancelHostContinuations();
            else
                owner.CancelSizeCard();
            state.retainedUntilAcknowledged = owner._jobs.ReservedMemoryBytes() > 0 &&
                                              (paste ? owner._clipboardTicket.has_value() && owner._historyWork.Busy()
                                                     : owner._clipboardPrefillTicket.has_value());
            break;
        case Mode::Replacement:
            owner._session.ReplaceDocument(owner._session.Document().value(), owner._session.ActiveLayerID());
            state.revision = owner._session.history.CurrentRevision();
            break;
        case Mode::Shutdown:
        {
            ReleaseAfterStop release;
            release.stopped = state.gate->workerShutdownEvent.load();
            release.release = state.producer.release.get();
            if (!release.stopped)
                std::_Exit(5);
            wil::unique_handle worker(CreateThread(nullptr, 0, ReleaseOnStop, &release, 0, nullptr));
            if (!worker)
                std::_Exit(5);
            owner.Shutdown(); // joins the still-blocked GetClipboardData callback; auxiliary release follows stop
            JoinClipboardThread(worker.get());
            state.shutdownObserved = release.observed.load() && owner._shutdownComplete;
            return;
        }
        default:
            break;
        }
        (void)SetEvent(state.producer.release.get());
    };
    for (const auto& scenario : scenarios)
    {
        std::unique_ptr<Application> closing;
        Application* owner = this;
        if (scenario.mode == Mode::Shutdown)
        {
            closing = std::make_unique<Application>(_instance, true, nullptr, 0);
            owner = closing.get();
            if (FAILED(owner->_jobs.Initialize(1)) || !owner->_jobs.RegisterOwnerWork(owner->_ownerWork) ||
                FAILED(owner->_retryWait.Initialize(owner->_jobs, owner->_ownerWork)) ||
                !owner->InitializeHostContinuations())
            {
                owner->Shutdown();
                return 5;
            }
        }
        auto closeOwner = wil::scope_exit(
            [&]() noexcept
            {
                if (closing)
                    closing->Shutdown();
            });
        owner->_clipboardWorkTestGate = std::make_shared<ClipboardWorkTestGate>();
        auto gate = owner->_clipboardWorkTestGate;
        auto clearGate = wil::scope_exit([&]() noexcept { owner->_clipboardWorkTestGate.reset(); });
        DocumentFixture document;
        document.id = Guid::Generate();
        document.width = 9;
        document.height = 7;
        constexpr std::array<std::uint8_t, 4> basePixel{1, 2, 3, 255};
        ImageLayer base = ImageLayer::Blank(L"Delayed source", Vec2{1, 1});
        base.asset =
            RasterAsset{Raster::Make(1, 1, RasterFormat::Bgra8888, basePixel.data(), basePixel.size()), {}, base.name};
        const auto active = base.id;
        document.layers.push_back(std::move(base));
        owner->_session.ReplaceDocument(std::move(document).Seal(), active);
        owner->_session.history.MarkSaved();
        owner->_clipboardSequence = 0;
        owner->_clipboardBinding.reset();
        const auto source = owner->_session.ActiveLayer()->asset->image;
        owner->DocumentChanged();
        owner->_viewport.SetZoom(1.0, owner->_viewport.Center(), owner->DocumentSize());
        DelayedClipboard producer;
        producer.ownerThread = GetCurrentThreadId();
        producer.gate = gate;
        if (!producer.ready || !producer.release)
            return 5;
        wil::unique_handle producerThread(CreateThread(nullptr, 0, ProducerMain, &producer, 0, nullptr));
        if (!producerThread)
            return 5;
        auto closeProducer = wil::scope_exit(
            [&]() noexcept
            {
                (void)SetEvent(producer.release.get());
                if (const HWND window = producer.window.load())
                    (void)PostMessageW(window, WM_CLOSE, 0, 0);
                else if (const DWORD thread = producer.thread.load())
                    (void)PostThreadMessageW(thread, WM_QUIT, 0, 0);
                JoinClipboardThread(producerThread.get());
            });
        if (WaitForSingleObject(producer.ready.get(), 15000) != WAIT_OBJECT_0)
            std::_Exit(5);
        if (!producer.installed.load())
        {
            std::printf("FAIL clipboard-delayed.%s.producer-install\n", scenario.name);
            return 5;
        }
        if (scenario.action == Action::Paste && scenario.mode == Mode::Success)
        {
            if (!OpenProducer(_window.get()))
                return 5;
            const auto close = wil::scope_exit([]() noexcept { (void)CloseClipboard(); });
            const auto early = GetClipboardData(CF_DIB);
            expect(!early && producer.unrelatedReads.load() != 0 && producer.renders.load() == 0, scenario.name,
                   "unrelated-read-before-worker-cannot-enter-event-gate");
            expect(IsClipboardFormatAvailable(CF_DIB) != FALSE, scenario.name,
                   "declined-early-read-preserves-delayed-format");
        }
        State state{*owner, scenario, producer, gate, {}, owner->_session.history.CurrentRevision()};
        struct ShutdownAuthority final
        {
            Application& owner;
            Raster source;
            const Guid& revision;
            bool unchanged = false;
        } authority{*owner, source, state.revision};
        ApplicationShutdownTestProbe probe;
        probe.checkContext = &authority;
        probe.checkOwner = [](void* opaque) noexcept
        {
            auto& check = *static_cast<ShutdownAuthority*>(opaque);
            const auto* layer = check.owner._session.ActiveLayer();
            check.unchanged = layer && layer->asset && layer->asset->image == check.source &&
                              check.owner._session.Document() && check.owner._session.Document()->layers.size() == 1 &&
                              check.owner._session.history.CurrentRevision() == check.revision &&
                              check.owner._session.history.UndoCount() == 0;
        };
        if (scenario.mode == Mode::Shutdown)
            owner->_shutdownTestProbe = &probe;
        const auto clearProbe = wil::scope_exit([owner]() noexcept { owner->_shutdownTestProbe = nullptr; });
        if (scenario.action == Action::Paste)
            state.receipt = owner->QueueHostInput(HostCommandInput{ChromeHost::kCommandPaste});
        else
        {
            owner->_sizeDialog.newWidth = 91;
            owner->_sizeDialog.newHeight = 73;
            owner->OpenSizeCard(SizeCardKind::NewCanvas);
            state.openedAt = GetTickCount64();
            expect(owner->SizeCardOpen() && owner->_sizeDialog.newWidth == 91 && owner->_sizeDialog.newHeight == 73,
                   scenario.name, "card-opens-before-delayed-render");
        }
        if (SettleJobs(owner->_jobs, 20000, dispatch, &state) != SettleResult::Settled)
            std::_Exit(5);
        if (!state.visited || !state.held || producer.renders.load() != 1 || !producer.rendered.load())
        {
            std::printf("clipboard-delayed.%s.actual visited=%d held=%d renders=%u rendered=%d unrelated=%u\n",
                        scenario.name, state.visited, state.held, producer.renders.load(), producer.rendered.load(),
                        producer.unrelatedReads.load());
            std::printf("clipboard-delayed.%s.native entered=%llu opened=%llu dispatched=%llu released=%llu "
                        "completed=%llu wait=%lu publication-error=%lu size=%.0fx%.0f\n",
                        scenario.name, producer.enteredAt.load(), state.openedAt, state.dispatchedAt,
                        producer.releasedAt.load(), producer.completedAt.load(), producer.releaseWait.load(),
                        producer.publicationError.load(), owner->_sizeDialog.newWidth, owner->_sizeDialog.newHeight);
        }
        expect(state.visited && state.held && producer.renders.load() == 1 && producer.rendered.load(), scenario.name,
               "actual-delayed-render-held-reserved-worker");
        expect(scenario.mode == Mode::Shutdown ? state.shutdownObserved : state.viewDispatched, scenario.name,
               "stop-or-view-dispatch-while-external-owner-blocked");
        expect(owner->_jobs.IsIdle() && owner->_jobs.ReservedMemoryBytes() == 0 &&
                   owner->_jobs.ReleasingMemoryBytes() == 0 && !owner->_historyWork.Busy() &&
                   !owner->_activeHostRequest && owner->_hostRequests.Size() == 0 && !owner->_hostInputBudget.Used(),
               scenario.name, "all-records-barrier-and-reservations-retired");
        if (scenario.action == Action::Paste && scenario.mode == Mode::Success)
        {
            const auto* layer = owner->_session.ActiveLayer();
            std::array<std::uint8_t, 4> pixel{};
            if (layer && layer->asset)
                layer->asset->image.ReadRegion(0, 0, 1, 1, pixel.data(), 4, 0);
            expect(state.receipt.Read().outcome == HostOutcome::Succeeded && layer && layer->asset &&
                       layer->transform.origin == Vec2{4, 3} && pixel == std::array<std::uint8_t, 4>{17, 33, 65, 255} &&
                       owner->_session.Document()->layers.size() == 2 && owner->_session.history.UndoName() == L"Paste",
                   scenario.name, "independent-foreign-pixels-origin-and-history");
        }
        else
        {
            const bool unchanged = scenario.mode == Mode::Shutdown
                                       ? probe.ownerCalls == 1 && authority.unchanged
                                       : owner->_session.ActiveLayer() && owner->_session.ActiveLayer()->asset &&
                                             owner->_session.ActiveLayer()->asset->image == source &&
                                             owner->_session.Document()->layers.size() == 1 &&
                                             owner->_session.history.CurrentRevision() == state.revision &&
                                             owner->_session.history.UndoCount() == 0;
            expect(unchanged, scenario.name, "no-document-publication-after-cancel-stale-or-prefill");
            if (scenario.action == Action::Paste)
                expect(state.receipt.Read().outcome ==
                           (scenario.mode == Mode::Replacement ? HostOutcome::Stale : HostOutcome::Cancelled),
                       scenario.name, "terminal-receipt-after-acknowledgement");
        }
        if (scenario.action == Action::Prefill && scenario.mode == Mode::Success)
            expect(owner->SizeCardOpen() && owner->_sizeDialog.newWidth == 1 && owner->_sizeDialog.newHeight == 1,
                   scenario.name, "metadata-suggestion-after-external-release");
        if (scenario.mode == Mode::Cancel)
            expect(state.retainedUntilAcknowledged, scenario.name, "cancel-keeps-reservation-until-native-return");
        if (scenario.mode == Mode::Shutdown)
        {
            expect(!owner->_session.Document() && !owner->_session.ActiveLayer() && !owner->_session.Clipboard(),
                   scenario.name, "shutdown-retires-session-after-source-preservation-check");
            std::array<std::uint8_t, 4> retainedSourcePixel{};
            source.ReadRegion(0, 0, 1, 1, retainedSourcePixel.data(), 4, 0);
            expect(retainedSourcePixel == basePixel, scenario.name, "shutdown-preserves-retained-source-pixel");
            expect(owner->QueueHostInput(HostCommandInput{ChromeHost::kCommandPaste}).Read().error == E_ABORT,
                   scenario.name, "closed-owner-refuses-new-command");
        }
        if (owner->SizeCardOpen())
            owner->CancelSizeCard();
    }
    std::printf("clipboard-delayed summary: %u checks, %d failures\n", checks, failures);
    return failures ? 5 : 0;
}
