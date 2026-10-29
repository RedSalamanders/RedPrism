#include "Application.h"

#include "ChromeHost.h"
#include "Runtime.Tests.ClipboardWorkTestGate.h"
#include "SystemClipboard.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

namespace
{
enum class PrefillFixture
{
    Dib,
    Png,
    BadDib,
    Empty,
};

bool OpenPrefillFixtureClipboard(HWND owner) noexcept
{
    wil::unique_handle timer(CreateWaitableTimerW(nullptr, TRUE, nullptr));
    if (!timer)
        return false;
    for (unsigned int attempt = 0; attempt < 4; ++attempt)
    {
        if (OpenClipboard(owner))
            return true;
        if (attempt == 3)
            return false;
        LARGE_INTEGER due{};
        due.QuadPart = -1'000'000; // one bounded 100 ms wait between at most four opens
        if (!SetWaitableTimer(timer.get(), &due, 0, nullptr, nullptr, FALSE) ||
            WaitForSingleObject(timer.get(), 1000) != WAIT_OBJECT_0)
            return false;
    }
    return false;
}

bool PlacePrefillFixture(HWND owner, UINT format, const std::uint8_t* bytes, std::size_t size) noexcept
{
    wil::unique_hglobal block(GlobalAlloc(GMEM_MOVEABLE, size));
    if (!block)
        return false;
    {
        const wil::unique_hglobal_locked locked(block.get());
        if (!locked.get())
            return false;
        std::memcpy(locked.get(), bytes, size);
    }
    if (!OpenPrefillFixtureClipboard(owner))
        return false;
    auto close = wil::scope_exit([]() noexcept { (void)CloseClipboard(); });
    if (!EmptyClipboard() || !SetClipboardData(format, block.get()))
        return false;
    (void)block.release();
    return true;
}

bool InstallPrefillFixture(HWND owner, PrefillFixture fixture) noexcept
{
    // Existing ClipboardTransferTests literal 1x1 CF_DIB: one opaque BGRA pixel {17, 33, 65, 255}.
    constexpr std::array<std::uint8_t, 44> dib{40, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 32, 0, 0, 0, 0,  0,  4,  0,
                                               0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 17, 33, 65, 255};
    // Existing ClipboardTransferTests independent literal 2x3 RGB PNG.
    constexpr std::array<std::uint8_t, 77> png{
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,
        0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x03, 0x08, 0x02, 0x00, 0x00, 0x00, 0x36, 0x88, 0x49,
        0xD6, 0x00, 0x00, 0x00, 0x14, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0xF4, 0xCC, 0x9F, 0xCB,
        0xC0, 0xC0, 0xC0, 0xC4, 0x00, 0x06, 0x50, 0x0A, 0x00, 0x19, 0x4F, 0x01, 0x5B, 0x54, 0x13, 0x85,
        0x0A, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82};
    if (fixture == PrefillFixture::Empty)
    {
        if (!OpenPrefillFixtureClipboard(owner))
            return false;
        auto close = wil::scope_exit([]() noexcept { (void)CloseClipboard(); });
        return EmptyClipboard() != FALSE;
    }
    if (fixture == PrefillFixture::Png)
    {
        const UINT format = RegisterClipboardFormatW(L"PNG");
        return format != 0 && PlacePrefillFixture(owner, format, png.data(), png.size());
    }
    if (fixture == PrefillFixture::BadDib)
    {
        auto malformed = dib;
        malformed[12] = 0; // invalid planes; the clipboard still advertises CF_DIB
        return PlacePrefillFixture(owner, CF_DIB, malformed.data(), malformed.size());
    }
    return PlacePrefillFixture(owner, CF_DIB, dib.data(), dib.size());
}
} // namespace

int Application::RunClipboardPrefillSelfTest() noexcept
{
    if (!_chrome || !_window)
    {
        SelfTestFailure(L"The clipboard prefill self-test needs the chrome and window.\n");
        return 5;
    }
    unsigned int checks = 0;
    int failures = 0;
    const auto expect = [&](bool condition, const char* scenario, const char* fact) noexcept
    {
        ++checks;
        std::printf("%s clipboard-prefill.%s.%s\n", condition ? "PASS" : "FAIL", scenario, fact);
        failures += condition ? 0 : 1;
    };
    SystemClipboard::SavedContents saved;
    if (!SystemClipboard::SaveContents(_window.get(), saved))
    {
        std::printf("FAIL clipboard-prefill.save-user-clipboard error=%lu\n", GetLastError());
        return 5;
    }
    auto cleanup = wil::scope_exit(
        [&]() noexcept
        {
            if (_clipboardWorkTestGate && _clipboardWorkTestGate->release)
                (void)SetEvent(_clipboardWorkTestGate->release.get());
            if (SizeCardOpen())
                CancelSizeCard();
            if (SettleJobs(_jobs, 15000, DispatchMessageThunk, this) != SettleResult::Settled)
            {
                SelfTestFailure(L"Clipboard prefill cleanup timed out while a worker borrowed test state.\n");
                std::_Exit(5);
            }
            _clipboardWorkTestGate.reset();
            if (!SystemClipboard::RestoreContents(_window.get(), saved))
                SelfTestFailure(L"Clipboard prefill could not restore saved clipboard contents.\n");
        });

    enum class Mode
    {
        Success,
        TypedWidth,
        TypedHeight,
        BadDib,
        Empty,
        Reopen,
        ReopenRetry,
        ReplaceDocument,
        Cancel,
        WorkerFailure,
        WorkerBadAlloc,
        Retry,
        Exhaust,
        SourceChanged,
    };
    struct Scenario final
    {
        const char* name;
        Mode mode;
        PrefillFixture fixture;
        int expectedWidth;
        int expectedHeight;
    };
    constexpr std::array scenarios{
        Scenario{"dib-success", Mode::Success, PrefillFixture::Dib, 1, 1},
        Scenario{"png-success", Mode::Success, PrefillFixture::Png, 2, 3},
        Scenario{"bad-dib", Mode::BadDib, PrefillFixture::BadDib, 91, 73},
        Scenario{"no-formats", Mode::Empty, PrefillFixture::Empty, 91, 73},
        Scenario{"typed-width", Mode::TypedWidth, PrefillFixture::Png, 100, 73},
        Scenario{"typed-height", Mode::TypedHeight, PrefillFixture::Png, 91, 100},
        Scenario{"close-reopen", Mode::Reopen, PrefillFixture::Png, 1, 1},
        Scenario{"close-reopen-busy", Mode::ReopenRetry, PrefillFixture::Png, 1, 1},
        Scenario{"replace-document", Mode::ReplaceDocument, PrefillFixture::Png, 91, 73},
        Scenario{"cancel", Mode::Cancel, PrefillFixture::Dib, 91, 73},
        Scenario{"worker-failure", Mode::WorkerFailure, PrefillFixture::Dib, 91, 73},
        Scenario{"worker-badalloc", Mode::WorkerBadAlloc, PrefillFixture::Dib, 91, 73},
        Scenario{"busy-retry-two", Mode::Retry, PrefillFixture::Dib, 1, 1},
        Scenario{"busy-exhaust-six", Mode::Exhaust, PrefillFixture::Dib, 91, 73},
        Scenario{"source-changed-held", Mode::SourceChanged, PrefillFixture::Dib, 1, 1},
    };
// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4623 4626 5027)
    struct State final
    {
        Application& owner;
        const Scenario& scenario;
        std::shared_ptr<ClipboardWorkTestGate> firstGate;
        std::shared_ptr<ClipboardWorkTestGate> secondGate;
        Guid revision;
        int undoCount = 0;
        unsigned int messages = 0;
        bool visited = false;
        bool heldOpenAndImmediate = false;
        bool zoomDispatched = false;
        bool idleHistoryAndBarrier = false;
        bool fixtureMutationSucceeded = true;
    };
#pragma warning(pop)

    const auto dispatch = [](void* raw, void* native) noexcept
    {
        auto& state = *static_cast<State*>(raw);
        auto& owner = state.owner;
        auto& message = *static_cast<MSG*>(native);
        if (message.message != ClipboardWorkTestGate::kMessage)
        {
            owner.DispatchQueuedMessage(message);
            return;
        }
        const bool second = state.messages != 0;
        const auto gate = second ? state.secondGate : state.firstGate;
        ++state.messages;
        state.visited = true;
        state.heldOpenAndImmediate = owner._clipboardPrefillTicket.has_value() && owner.SizeCardOpen() &&
                                     owner._sizeDialog.kind == SizeCardKind::NewCanvas &&
                                     owner._sizeDialog.newWidth == 91 && owner._sizeDialog.newHeight == 73 &&
                                     owner._jobs.ReservedMemoryBytes() > 0;
        owner.OnCommand(ChromeHost::kCommandZoomIn);
        state.zoomDispatched = true;
        state.idleHistoryAndBarrier = !owner._historyWork.Busy() && !owner._activeHostRequest &&
                                      owner._hostRequests.Size() == 0 &&
                                      owner._session.history.CurrentRevision() == state.revision &&
                                      owner._session.history.UndoCount() == state.undoCount;
        switch (state.scenario.mode)
        {
        case Mode::TypedWidth:
            owner.OnSizeAction(SizeAction::SetWidth, 100.0);
            break;
        case Mode::TypedHeight:
            owner.OnSizeAction(SizeAction::SetHeight, 100.0);
            break;
        case Mode::Reopen:
        case Mode::ReopenRetry:
            if (!second)
            {
                state.fixtureMutationSucceeded = InstallPrefillFixture(owner._window.get(), PrefillFixture::Dib);
                owner.CancelSizeCard();
                owner._sizeDialog.newWidth = 91;
                owner._sizeDialog.newHeight = 73;
                owner._clipboardWorkTestGate = state.secondGate;
                owner.OpenSizeCard(SizeCardKind::NewCanvas);
            }
            break;
        case Mode::ReplaceDocument:
            if (owner._session.Document())
            {
                owner._session.ReplaceDocument(owner._session.Document().value(), owner._session.ActiveLayerID());
                state.revision = owner._session.history.CurrentRevision();
                state.undoCount = owner._session.history.UndoCount();
            }
            else
                state.fixtureMutationSucceeded = false;
            break;
        case Mode::Cancel:
            owner.CancelSizeCard();
            break;
        case Mode::SourceChanged:
            state.fixtureMutationSucceeded = InstallPrefillFixture(owner._window.get(), PrefillFixture::Png);
            break;
        case Mode::BadDib:
        case Mode::Empty:
        case Mode::Exhaust:
        case Mode::Retry:
        case Mode::Success:
        case Mode::WorkerBadAlloc:
        case Mode::WorkerFailure:
        default:
            break;
        }
        if (gate && gate->release)
            (void)SetEvent(gate->release.get());
    };

    if (!_session.Document())
    {
        _session.CreateDocument(2, 2, true);
        _session.history.MarkSaved();
        DocumentChanged();
    }
    for (const Scenario& scenario : scenarios)
    {
        if (SizeCardOpen())
            CancelSizeCard();
        if (_clipboardPrefillTicket && SettleJobs(_jobs, 15000, DispatchMessageThunk, this) != SettleResult::Settled)
        {
            SelfTestFailure(L"Clipboard prefill could not settle before the next fixture.\n");
            std::_Exit(5);
        }
        _clipboardWorkTestGate.reset();
        if (!InstallPrefillFixture(_window.get(), scenario.fixture))
        {
            std::printf("FAIL clipboard-prefill.%s.fixture-install error=%lu\n", scenario.name, GetLastError());
            return 5;
        }
        _sizeDialog.newWidth = 91;
        _sizeDialog.newHeight = 73;
        auto firstGate = std::make_shared<ClipboardWorkTestGate>();
        const bool reopening = scenario.mode == Mode::Reopen || scenario.mode == Mode::ReopenRetry;
        auto secondGate = reopening ? std::make_shared<ClipboardWorkTestGate>() : nullptr;
        if (!firstGate->release || (secondGate && !secondGate->release))
        {
            std::printf("FAIL clipboard-prefill.%s.gate-create error=%lu\n", scenario.name, GetLastError());
            return 5;
        }
        firstGate->phase = ClipboardWorkPhase::PrefillInspected;
        if (secondGate)
            secondGate->phase = ClipboardWorkPhase::PrefillInspected;
        if (scenario.mode == Mode::WorkerFailure)
            firstGate->result = {JobStatus::Failed, E_ACCESSDENIED};
        if (scenario.mode == Mode::WorkerBadAlloc)
            firstGate->throwWorkerFailure = true;
        if (scenario.mode == Mode::Retry)
            firstGate->openFailures.store(2);
        if (scenario.mode == Mode::ReopenRetry)
        {
            firstGate->openFailures.store(2);
            secondGate->openFailures.store(2);
        }
        if (scenario.mode == Mode::Exhaust)
            firstGate->openFailures.store(6);
        _clipboardWorkTestGate = firstGate;
        State state{
            *this, scenario, firstGate, secondGate, _session.history.CurrentRevision(), _session.history.UndoCount()};
        OpenSizeCard(SizeCardKind::NewCanvas);
        const bool openedImmediately = SizeCardOpen() && _sizeDialog.kind == SizeCardKind::NewCanvas &&
                                       _sizeDialog.newWidth == 91 && _sizeDialog.newHeight == 73;
        expect(openedImmediately, scenario.name, "card-opens-immediately-with-default-draft");
        const auto settled = SettleJobs(_jobs, 15000, dispatch, &state);
        if (settled != SettleResult::Settled)
        {
            SelfTestFailure(L"Clipboard prefill worker did not settle within its bounded wait.\n");
            std::_Exit(5); // retain the worker's borrowed frame and fixture owners on timeout
        }
        expect(state.visited && state.heldOpenAndImmediate && state.zoomDispatched && state.idleHistoryAndBarrier,
               scenario.name, "held-worker-allows-zoom-without-history-or-barrier");
        expect(!_clipboardPrefillTicket && _jobs.ReservedMemoryBytes() == 0 && _jobs.ReleasingMemoryBytes() == 0 &&
                   !_historyWork.Busy() && !_activeHostRequest && _hostRequests.Size() == 0 &&
                   _session.history.CurrentRevision() == state.revision &&
                   _session.history.UndoCount() == state.undoCount && firstGate->retiredOwnerNodes.load() == 0,
               scenario.name, "completion-preserves-history-and-retires-all-job-memory");
        if (reopening)
        {
            const bool reopened = state.messages == 2 && secondGate && secondGate->visits.load() == 1 &&
                                  state.fixtureMutationSucceeded && SizeCardOpen() && !_newCanvasEdited &&
                                  _sizeDialog.newWidth == 1 && _sizeDialog.newHeight == 1;
            if (!reopened)
                std::printf("  prefill reopen messages=%u visits=%u fixture=%d open=%d edited=%d size=%.0fx%.0f "
                            "first-opens=%u second-opens=%u\n",
                            state.messages, secondGate ? secondGate->visits.load() : 0, state.fixtureMutationSucceeded,
                            SizeCardOpen(), _newCanvasEdited, _sizeDialog.newWidth, _sizeDialog.newHeight,
                            firstGate->openAttempts.load(), secondGate ? secondGate->openAttempts.load() : 0);
            expect(reopened, scenario.name, "old-completion-cannot-overwrite-reopened-card");
            if (scenario.mode == Mode::ReopenRetry)
                expect(firstGate->forcedBusyAttempts.load() == 2 && secondGate->forcedBusyAttempts.load() == 2 &&
                           firstGate->visits.load() == 1 && secondGate->visits.load() == 1 &&
                           firstGate->openAttempts.load() >= 3 && firstGate->openAttempts.load() <= 6 &&
                           secondGate->openAttempts.load() >= 3 && secondGate->openAttempts.load() <= 6,
                       scenario.name, "both-invocations-retry-before-one-settled-inspection-boundary");
        }
        else if (scenario.mode == Mode::Cancel)
        {
            expect(!SizeCardOpen() && _sizeDialog.newWidth == 91 && _sizeDialog.newHeight == 73, scenario.name,
                   "cancel-closes-card-without-publishing-suggestion");
        }
        else if (scenario.mode == Mode::Retry)
        {
            const unsigned int attempts = firstGate->openAttempts.load();
            expect(firstGate->forcedBusyAttempts.load() == 2 && attempts >= 3 && attempts <= 6 && SizeCardOpen() &&
                       _sizeDialog.newWidth == 1 && _sizeDialog.newHeight == 1,
                   scenario.name, "two-injected-busy-retries-with-success-within-six-attempts");
        }
        else if (scenario.mode == Mode::Exhaust)
        {
            expect(firstGate->forcedBusyAttempts.load() == 6 && firstGate->openAttempts.load() == 6 && SizeCardOpen() &&
                       _sizeDialog.newWidth == 91 && _sizeDialog.newHeight == 73,
                   scenario.name, "six-injected-busy-attempts-exhaust-without-suggestion");
        }
        else if (scenario.mode == Mode::SourceChanged)
        {
            expect(state.fixtureMutationSucceeded && _sizeDialog.newWidth == 1 && _sizeDialog.newHeight == 1,
                   scenario.name, "held-inspection-metadata-survives-source-change");
        }
        else
        {
            const bool edited = scenario.mode == Mode::TypedWidth || scenario.mode == Mode::TypedHeight;
            expect(SizeCardOpen() && _sizeDialog.kind == SizeCardKind::NewCanvas &&
                       _sizeDialog.newWidth == scenario.expectedWidth &&
                       _sizeDialog.newHeight == scenario.expectedHeight && _newCanvasEdited == edited,
                   scenario.name, "final-draft-matches-prefill-and-user-edit-policy");
        }
        if (secondGate)
            (void)SetEvent(secondGate->release.get());
        _clipboardWorkTestGate.reset();
        if (SizeCardOpen())
            CancelSizeCard();
    }
    std::printf("clipboard-prefill summary: %u checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 5;
}
