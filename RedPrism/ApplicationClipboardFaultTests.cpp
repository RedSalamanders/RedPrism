#include "Application.h"

#include "../Tests/Support/DocumentFixture.h"
#include "ChromeHost.h"
#include "ClipboardWorkTestGate.h"
#include "SystemClipboard.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

namespace
{
bool OpenClipboardFixture(HWND owner) noexcept
{
    wil::unique_handle timer(CreateWaitableTimerW(nullptr, TRUE, nullptr));
    if (!timer)
        return false;
    for (unsigned int attempt = 0; attempt < 30; ++attempt)
    {
        if (OpenClipboard(owner))
            return true;
        if (attempt == 29)
            return false;
        LARGE_INTEGER due{};
        due.QuadPart = -1'000'000; // one-shot 100 ms readiness wait; never a latency assertion
        if (!SetWaitableTimer(timer.get(), &due, 0, nullptr, nullptr, FALSE) ||
            WaitForSingleObject(timer.get(), 1000) != WAIT_OBJECT_0)
            return false;
    }
    return false;
}
bool ClearClipboardFixture(HWND owner) noexcept
{
    if (!OpenClipboardFixture(owner))
        return false;
    const auto close = wil::scope_exit([]() noexcept { (void)CloseClipboard(); });
    return EmptyClipboard() != FALSE;
}
bool InstallForeignClipboard(HWND owner) noexcept
{
    constexpr std::array<std::uint8_t, 44> dib{40, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 32, 0, 0, 0, 0,  0,  4,  0,
                                               0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 17, 33, 65, 255};
    wil::unique_hglobal block(GlobalAlloc(GMEM_MOVEABLE, dib.size()));
    if (!block)
        return false;
    {
        const wil::unique_hglobal_locked locked(block.get());
        if (!locked.get())
            return false;
        std::memset(locked.get(), 0, GlobalSize(block.get()));
        std::memcpy(locked.get(), dib.data(), dib.size());
    }
    if (!OpenClipboardFixture(owner))
        return false;
    const auto close = wil::scope_exit([]() noexcept { (void)CloseClipboard(); });
    if (!EmptyClipboard() || !SetClipboardData(CF_DIB, block.get()))
        return false;
    (void)block.release();
    return true;
}

constexpr std::array<std::uint8_t, 12> kSourcePixels{11, 22, 33, 255, 20, 40, 80, 255, 99, 100, 101, 255};
constexpr std::array<std::uint8_t, 4> kSelectedPixel{20, 40, 80, 255};

CanvasDocument ClipboardFaultFixture(Guid& activeLayer)
{
    DocumentFixture document;
    document.id = Guid::Generate();
    document.width = 9;
    document.height = 7;
    ImageLayer layer = ImageLayer::Blank(L"Clipboard fault literal", Vec2{3, 1});
    layer.asset = RasterAsset{
        Raster::Make(3, 1, RasterFormat::Bgra8888, kSourcePixels.data(), kSourcePixels.size()), {}, layer.name};
    activeLayer = layer.id;
    document.layers.push_back(std::move(layer));
    return std::move(document).Seal();
}
bool PrivateCopyIsLiteral(const DocumentSession& session) noexcept
{
    const auto& clipboard = session.Clipboard();
    if (!clipboard || clipboard->origin != Vec2{1, 0} || clipboard->image.Width() != 1 ||
        clipboard->image.Height() != 1 || !clipboard->image.HasStorage())
        return false;
    return std::memcmp(clipboard->image.Data(), kSelectedPixel.data(), kSelectedPixel.size()) == 0;
}
bool DocumentAndHistoryAreUnchanged(const DocumentSession& session, Guid revision, int undoCount, bool canRedo,
                                    bool isModified) noexcept
{
    const auto* layer = session.ActiveLayer();
    if (!session.Document() || session.Document()->layers.size() != 1 || !layer || !layer->asset ||
        !layer->asset->image.HasStorage() || layer->asset->image.Width() != 3 || layer->asset->image.Height() != 1 ||
        std::memcmp(layer->asset->image.Data(), kSourcePixels.data(), kSourcePixels.size()) != 0)
        return false;
    return session.history.CurrentRevision() == revision && session.history.UndoCount() == undoCount &&
           session.history.CanRedo() == canRedo && session.history.IsModified() == isModified;
}
} // namespace

int Application::RunClipboardFaultSelfTest() noexcept
{
    int failures = 0;
    unsigned int checks = 0;
    const auto expect = [&](bool condition, const char* scenario, const char* predicate) noexcept
    {
        ++checks;
        std::printf("%s clipboard-fault.%s.%s\n", condition ? "PASS" : "FAIL", scenario, predicate);
        failures += condition ? 0 : 1;
    };

    enum class Kind : std::uint8_t
    {
        Copy,
        CutNoPaint,
        PasteCopyOpenWait,
    };
    struct Scenario
    {
        const char* name;
        Kind kind;
        ClipboardTransfer::Fault fault;
        ClipboardWorkPhase phase;
        bool emptied;
        bool encodeFailure;
        unsigned int inspectionBusyAttempts = 0;
    };
    constexpr std::array scenarios{
        Scenario{"copy-empty-fault", Kind::Copy, ClipboardTransfer::Fault::Empty, ClipboardWorkPhase::CopyPublished,
                 false, false},
        Scenario{"copy-png-place-fault", Kind::Copy, ClipboardTransfer::Fault::Png, ClipboardWorkPhase::CopyPublished,
                 true, false},
        Scenario{"copy-dib-place-fault", Kind::Copy, ClipboardTransfer::Fault::Dib, ClipboardWorkPhase::CopyPublished,
                 true, false},
        Scenario{"copy-close-fault", Kind::Copy, ClipboardTransfer::Fault::Close, ClipboardWorkPhase::CopyPublished,
                 true, false},
        Scenario{"copy-dib-global-fault", Kind::Copy, ClipboardTransfer::Fault::DibGlobal,
                 ClipboardWorkPhase::CopyEncoded, false, true},
        Scenario{"copy-receipt-global-fault", Kind::Copy, ClipboardTransfer::Fault::ReceiptGlobal,
                 ClipboardWorkPhase::CopyEncoded, false, true},
        Scenario{"cut-hidden-layer-no-paint", Kind::CutNoPaint, ClipboardTransfer::Fault::Empty,
                 ClipboardWorkPhase::CopyPublished, false, false},
        Scenario{"paste-copy-open-wait-exhausted", Kind::PasteCopyOpenWait, ClipboardTransfer::Fault::Open,
                 ClipboardWorkPhase::PasteInspected, false, false},
        Scenario{"paste-copy-open-wait-exhausted-after-inspection-busy", Kind::PasteCopyOpenWait,
                 ClipboardTransfer::Fault::Open, ClipboardWorkPhase::PasteInspected, false, false, 1},
    };

    SystemClipboard::SavedContents saved;
    if (!SystemClipboard::SaveContents(_window.get(), saved))
    {
        std::printf("FAIL clipboard-fault.save-user-clipboard error=%lu\n", GetLastError());
        return 5;
    }
    auto restore = wil::scope_exit(
        [&]() noexcept
        {
            _clipboardWorkTestGate.reset();
            (void)SystemClipboard::RestoreContents(_window.get(), saved);
        });

    struct PumpState
    {
        Application& owner;
        std::shared_ptr<ClipboardWorkTestGate> gate;
        ContinuationReceipt receipt;
        bool visited = false;
        bool barrierHeld = false;
        bool injectOpenAfterInspection = false;
    };
    const auto dispatch = [](void* raw, void* native) noexcept
    {
        auto& state = *static_cast<PumpState*>(raw);
        auto& message = *static_cast<MSG*>(native);
        if (message.message != ClipboardWorkTestGate::kMessage)
        {
            state.owner.DispatchQueuedMessage(message);
            return;
        }
        state.visited = true;
        const auto requiredReservation =
            state.gate->reservationBytes != 0 ? state.gate->reservationBytes : state.gate->probeReservationBytes;
        state.barrierHeld = state.owner._clipboardTicket && state.owner._historyWork.Busy() &&
                            state.owner._activeHostRequest && state.receipt.Read().outcome == HostOutcome::Pending &&
                            state.owner._hostRequests.Size() == 0 && state.owner._hostInputBudget.Used() &&
                            requiredReservation != 0 && state.owner._jobs.ReservedMemoryBytes() >= requiredReservation;
        if (state.injectOpenAfterInspection)
        {
            state.gate->fault = ClipboardTransfer::Fault::Open;
            state.gate->nativeError = ERROR_ACCESS_DENIED;
        }
        (void)SetEvent(state.gate->release.get());
    };

    constexpr int copyCommand = ChromeHost::kCommandCopy;
    constexpr int cutCommand = ChromeHost::kCommandCut;
    constexpr int pasteCommand = ChromeHost::kCommandPaste;
    for (const auto& scenario : scenarios)
    {
        _clipboardWorkTestGate.reset();
        Guid activeLayer;
        auto document = ClipboardFaultFixture(activeLayer);
        _session.ReplaceDocument(std::move(document), activeLayer);
        _session.SetSelection(
            DocumentSelection{false, SelectionLoops{{Vec2{1, 0}, Vec2{2, 0}, Vec2{2, 1}, Vec2{1, 1}}}}, L"Literal 1x1");
        if (!_session.CopySelection() || !PrivateCopyIsLiteral(_session))
        {
            std::printf("FAIL clipboard-fault.%s.private-fixture active=%d\n", scenario.name,
                        _session.ActiveLayer() != nullptr);
            return 5;
        }
        _session.history.Reset();
        _session.history.MarkSaved();
        _clipboardBinding.reset();
        DocumentChanged();

        const bool paste = scenario.kind == Kind::PasteCopyOpenWait;
        const bool noPaintCut = scenario.kind == Kind::CutNoPaint;
        if (paste ? !InstallForeignClipboard(_window.get()) : !ClearClipboardFixture(_window.get()))
        {
            std::printf("FAIL clipboard-fault.%s.fixture-clipboard error=%lu\n", scenario.name, GetLastError());
            return 5;
        }
        const std::uint32_t initialSequence = SystemClipboard::SequenceNumber();
        _clipboardSequence = paste ? (initialSequence ^ 1u) : initialSequence;
        _clipboardWriteSucceeded = true; // each command must replace this sentinel with observed transfer truth

        if (noPaintCut)
        {
            EditLayerRecordsForTest(_session,
                                    [](DocumentFixture& document) { document.layers.front().visible = false; });
            if (_session.CanPaint() || !_session.CanCopyPixels() || !PrivateCopyIsLiteral(_session))
                return 5;
            DocumentChanged();
        }

        _clipboardWorkTestGate = std::make_shared<ClipboardWorkTestGate>();
        auto gate = _clipboardWorkTestGate;
        if (!gate->release)
            return 5;
        gate->phase = scenario.phase;
        if (paste)
            gate->fault.reset(); // Inspect must complete before the owner arms the Copy-open retry fault.
        else
            gate->fault = scenario.fault;
        gate->nativeError = ERROR_ACCESS_DENIED;
        gate->openFailures.store(scenario.inspectionBusyAttempts);

        const auto serial = _session.ClipboardSerial();
        const auto historyRevision = _session.history.CurrentRevision();
        const auto historyUndoCount = _session.history.UndoCount();
        const auto historyCanRedo = _session.history.CanRedo();
        const auto historyIsModified = _session.history.IsModified();
        const std::uint32_t sequenceBeforeCommand = SystemClipboard::SequenceNumber();
        const int command = paste ? pasteCommand : noPaintCut ? cutCommand : copyCommand;
        PumpState state{*this, gate, QueueHostInput(HostCommandInput{command}), false, false, paste};
        const auto settled = SettleJobs(_jobs, 15000, dispatch, &state);
        if (settled != SettleResult::Settled)
        {
            SelfTestFailure(L"Clipboard fault work did not acknowledge within the bounded wait.\n");
            std::_Exit(5); // preserve SavedContents and all owners while an asynchronous borrower may remain
        }

        const HostOutcome expected = paste ? HostOutcome::Failed : HostOutcome::Succeeded;
        if (state.receipt.Read().outcome != expected)
            std::printf("clipboard-fault.%s.actual-receipt=%u error=%08x visits=%u opens=%u\n", scenario.name,
                        static_cast<unsigned int>(state.receipt.Read().outcome),
                        static_cast<unsigned int>(state.receipt.Read().error), gate->visits.load(),
                        gate->openAttempts.load());
        expect(state.receipt.Read().outcome == expected, scenario.name, "final-receipt");
        if (!paste)
        {
            expect(state.visited && state.barrierHeld, scenario.name, "event-gated-owner-barrier");
            expect(!_clipboardTicket && !_clipboardStamp && !_historyWork.Busy() && !_activeHostRequest &&
                       _hostRequests.Size() == 0 && !_hostInputBudget.Used() && _jobs.ReservedMemoryBytes() == 0 &&
                       _jobs.ReleasingMemoryBytes() == 0,
                   scenario.name, "barrier-and-all-memory-released");
            expect(gate->retiredNodes.load() != 0 && gate->retiredOwnerNodes.load() == 0, scenario.name,
                   "aggregate-destroyed-off-owner");
            expect(PrivateCopyIsLiteral(_session), scenario.name, "private-copy-survives-os-fault");
            expect(_session.ClipboardSerial() == serial + (noPaintCut ? 0u : 1u), scenario.name,
                   "private-serial-publication");
            expect(!_clipboardWriteSucceeded, scenario.name, "exact-os-write-failure");
            expect(DocumentAndHistoryAreUnchanged(_session, historyRevision, historyUndoCount, historyCanRedo,
                                                  historyIsModified),
                   scenario.name, "literal-layer-and-history-preserved");
            expect(gate->emptyAttempts.load() == (scenario.emptied ? 1u : 0u), scenario.name,
                   "exact-empty-attempt-count");
            if (scenario.emptied)
                expect(_clipboardSequence != sequenceBeforeCommand, scenario.name, "sequence-updated-after-empty");
            else
                expect(_clipboardSequence == sequenceBeforeCommand, scenario.name, "sequence-preserved-before-empty");
            if (scenario.encodeFailure)
                expect(gate->publications.load() == 0 && _clipboardSequence == sequenceBeforeCommand, scenario.name,
                       "encoding-failure-never-publishes");
            if (noPaintCut)
            {
                expect(!_session.CanPaint(), scenario.name, "hidden-layer-can-paint-false");
                expect(_session.CanCopyPixels() && _session.ClipboardSerial() == serial &&
                           PrivateCopyIsLiteral(_session),
                       scenario.name, "hidden-layer-no-paint-cut-mirrors-prior-copy");
            }
        }
        else
        {
            expect(state.visited && state.barrierHeld && gate->visits.load() == 1 &&
                       gate->fault == ClipboardTransfer::Fault::Open,
                   scenario.name, "inspect-settled-before-copy-open-fault");
            expect(gate->openAttempts.load() == 6 && gate->forcedBusyAttempts.load() == scenario.inspectionBusyAttempts,
                   scenario.name, "six-total-inspect-and-copy-open-attempts");
            expect(state.receipt.Read().error == static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_BUSY)),
                   scenario.name, "copy-retry-exhaustion-reports-error-busy");
            expect(!_clipboardTicket && !_clipboardStamp && !_historyWork.Busy() && !_activeHostRequest &&
                       _hostRequests.Size() == 0 && !_hostInputBudget.Used() && _jobs.ReservedMemoryBytes() == 0 &&
                       _jobs.ReleasingMemoryBytes() == 0,
                   scenario.name, "paste-barrier-and-all-memory-released");
            expect(_session.ClipboardSerial() == serial && PrivateCopyIsLiteral(_session), scenario.name,
                   "failed-paste-preserves-private-copy");
            expect(DocumentAndHistoryAreUnchanged(_session, historyRevision, historyUndoCount, historyCanRedo,
                                                  historyIsModified),
                   scenario.name, "literal-layer-and-history-preserved");
        }
    }
    std::printf("clipboard-fault summary: %u checks, %d failures\n", checks, failures);
    return failures ? 5 : 0;
}
