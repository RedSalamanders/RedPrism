// Independent large-output geometry and original-ticket ownership proof.
#include "Application.h"

#include "ChromeHost.h"
#include "SelectionOffsetTestGate.h"

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <utility>
#include <vector>

int Application::RunSelectionOffsetLargeOutputSelfTest() noexcept
{
    constexpr int kCanvasWidth = 8192;
    constexpr int kCanvasHeight = 4096;
    constexpr int kColumns = 256;
    constexpr int kRows = 128;
    constexpr int kInputSide = 20;
    constexpr int kContractAmount = -5;
    constexpr std::size_t kExpectedLoopCount = 32768;
    constexpr std::size_t kExpectedPointCount = 131072;
    constexpr double kExpectedTwiceArea = 6553600.0;
    constexpr SelectionBounds kExpectedBounds{13.0, 13.0, 8183.0, 4087.0};

    const auto originalViewport = _viewport;
    const auto originalHistoryBudget = _session.history.RetainedByteLimit();
    std::shared_ptr<SelectionOffsetTestGate> gate;
    const auto cleanup = wil::scope_exit(
        [this, &gate, originalViewport, originalHistoryBudget]() noexcept
        {
            if (gate && gate->release)
                (void)SetEvent(gate->release.get());
            _selectionOffsetTestGate.reset();
            _session.ClearDocument();
            _session.history = DocumentHistory{kHistoryEntryLimit, originalHistoryBudget};
            DocumentChanged();
            _viewport = originalViewport;
            if (SettleJobs(_jobs, 15000, DispatchMessageThunk, this) != SettleResult::Settled)
                std::_Exit(5);
        });

    int failures = 0;
    const auto expect = [&failures](const char* label, bool condition) noexcept
    {
        std::fprintf(stderr, "%s selection-offset-large.%s\n", condition ? "PASS" : "FAIL", label);
        failures += condition ? 0 : 1;
        return condition;
    };

    _selectionOffsetTestGate.reset();
    _session.ClearDocument();
    _session.history = DocumentHistory{kHistoryEntryLimit, originalHistoryBudget};
    _session.CreateDocument(kCanvasWidth, kCanvasHeight, true);

    std::vector<SelectionLoop> inputLoops;
    inputLoops.reserve(kExpectedLoopCount);
    for (int row = 0; row < kRows; ++row)
    {
        for (int column = 0; column < kColumns; ++column)
        {
            const double x = static_cast<double>(32 * column + 8);
            const double y = static_cast<double>(32 * row + 8);
            inputLoops.push_back({{x, y}, {x + kInputSide, y}, {x + kInputSide, y + kInputSide}, {x, y + kInputSide}});
        }
    }
    _session.SetSelection(DocumentSelection{false, std::move(inputLoops)}, L"Large offset fixture");
    _session.history.Reset();
    _session.history.MarkSaved();
    DocumentChanged();

    // Settle fixture-created work before taking the actual scheduler charge baseline.
    const bool baselineSettled = SettleJobs(_jobs, 15000, DispatchMessageThunk, this) == SettleResult::Settled;
    (void)expect("literal-fixture-settles-before-charge-snapshot", baselineSettled);
    if (!baselineSettled)
        return 5;
    const auto baselineBytes = _jobs.ReservedMemoryBytes();

    gate = std::make_shared<SelectionOffsetTestGate>();
    gate->release.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    (void)expect("worker-release-event-created", static_cast<bool>(gate->release));
    if (!gate->release)
        return 5;
    gate->hold = true;
    gate->observeOffsetOutputLifetime = true;
    _selectionOffsetTestGate = gate;

    struct Large final
    {
        Application& owner;
        std::shared_ptr<const void> copiedOutline;
        ContinuationReceipt receipt;
        bool heldAtGate = false;
    } large{*this};

    const auto pump = [](void* raw, void* native) noexcept
    {
        auto& state = *static_cast<Large*>(raw);
        auto& message = *static_cast<MSG*>(native);
        if (message.message == SelectionOffsetTestGate::kMessage)
        {
            const auto heldGate = state.owner._selectionOffsetTestGate;
            if (!heldGate)
                return;
            state.copiedOutline = heldGate->offsetOutlineOwner.lock();
            state.heldAtGate = state.copiedOutline && heldGate->captures.load() == 1 &&
                               heldGate->reservationHeld.load() && state.owner._selectionOffsetTicket &&
                               state.owner._selectionOffsetStamp && state.owner._historyWork.Busy() &&
                               state.receipt.Read().outcome == HostOutcome::Pending;
            (void)SetEvent(heldGate->release.get());
            return;
        }
        state.owner.DispatchQueuedMessage(message);
    };

    large.receipt = QueueHostInput(HostSelectionOffsetInput{kContractAmount});
    const bool settled = SettleJobs(_jobs, 15000, pump, &large) == SettleResult::Settled;
    const auto result = large.receipt.Read();

    bool literalOutput = false;
    if (const auto selection = _session.Selection())
    {
        double twiceArea = 0.0;
        for (const auto& loop : selection->loops.Get())
        {
            for (std::size_t index = 0; index < loop.size(); ++index)
            {
                const auto& a = loop[index];
                const auto& b = loop[(index + 1) % loop.size()];
                twiceArea += a.x * b.y - b.x * a.y;
            }
        }
        const auto bounds = selection->Bounds();
        literalOutput = selection->loops.Get().size() == kExpectedLoopCount &&
                        selection->loops.PointCount() == kExpectedPointCount && twiceArea == kExpectedTwiceArea &&
                        bounds == kExpectedBounds && !selection->antialiased;
    }
    (void)expect("contract-output-32768-loops-131072-points-area-bounds-no-antialias",
                 settled && result.outcome == HostOutcome::Succeeded && literalOutput &&
                     _session.history.UndoName() == L"Contract Selection");

    const auto* displayed = _chrome->SelectionOutlineForTest();
    (void)expect("copied-outline-is-the-prepared-worker-buffer",
                 large.heldAtGate && displayed && !displayed->empty() &&
                     displayed->front().data() == gate->offsetPointBuffer.load() &&
                     gate->offsetOutlinePreparedOffOwner.load() && gate->retiredOffOwner.load());
    const auto publishedBytes = gate->offsetPublishedBytes;
    (void)expect("complete-output-charge-exceeds-deferred-threshold-on-original-ticket",
                 gate->offsetPublishedOutputs == 1 && gate->offsetOutputBoundToOriginal &&
                     publishedBytes > kDeferredReleaseThreshold &&
                     _jobs.ReservedMemoryBytes() == baselineBytes + publishedBytes);

    _selectionOffsetTestGate.reset();
    _session.ClearDocument();
    DocumentChanged();
    const bool clearSettled = SettleJobs(_jobs, 15000, DispatchMessageThunk, this) == SettleResult::Settled;
    (void)expect("copied-outline-retains-charge-after-document-history-clear",
                 clearSettled && large.copiedOutline && !gate->offsetOutlineOwner.expired() &&
                     !gate->offsetMemoryOwner.expired() &&
                     _jobs.ReservedMemoryBytes() == baselineBytes + publishedBytes);

    large.copiedOutline.reset();
    const bool outlineReleased = SettleJobs(_jobs, 15000, DispatchMessageThunk, this) == SettleResult::Settled;
    (void)expect("last-outline-weak-reader-retains-memory-charge",
                 outlineReleased && gate->offsetOutlineOwner.expired() && !gate->offsetMemoryOwner.expired() &&
                     _jobs.ReservedMemoryBytes() == baselineBytes + publishedBytes);

    gate->offsetOutlineOwner.reset();
    const bool memoryExpired = SettleJobs(_jobs, 15000, DispatchMessageThunk, this) == SettleResult::Settled;
    (void)expect("last-memory-weak-control-expires-before-refund",
                 memoryExpired && gate->offsetMemoryOwner.expired() &&
                     _jobs.ReservedMemoryBytes() == baselineBytes + publishedBytes);

    gate->offsetMemoryOwner.reset();
    const bool refunded = SettleJobs(_jobs, 15000, DispatchMessageThunk, this) == SettleResult::Settled;
    (void)expect("last-owner-refunds-exactly-to-baseline-with-no-host-barrier",
                 refunded && _jobs.ReservedMemoryBytes() == baselineBytes && !_selectionOffsetTicket &&
                     !_selectionOffsetStamp && !_historyWork.Busy());

    return failures == 0 ? 0 : 5;
}
