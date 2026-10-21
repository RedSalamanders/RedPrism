#include "Host.Tests.PreservedContinuation.h"

#include "../../RedPrism/ContinuationRequests.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string_view>
#include <utility>

namespace
{
using Queue = ContinuationQueue<HostRequest>;

struct RetirementLog final
{
    struct Record final
    {
        std::uint64_t document = 0;
        std::uint64_t ordinal = 0;
        ContinuationRetirement outcome = ContinuationRetirement::Stale;
    };

    std::array<Record, Queue::kCapacity> records{};
    std::size_t count = 0;
    bool overflowed = false;

    void operator()(Queue::Entry&& entry, ContinuationRetirement outcome) noexcept
    {
        if (count == records.size())
        {
            overflowed = true;
        }
        else
        {
            records[count++] = {entry.documentSerial, entry.ordinal, outcome};
        }
        entry.input.Retire(outcome);
    }
};

struct DestructionCounts final
{
    std::size_t destroyed = 0;
};

struct MoveOnlyRequest final
{
    DestructionCounts* counts = nullptr;
    bool owns = false;

    explicit MoveOnlyRequest(DestructionCounts& lifetime) noexcept : counts(&lifetime), owns(true) {}
    MoveOnlyRequest(const MoveOnlyRequest&) = delete;
    MoveOnlyRequest& operator=(const MoveOnlyRequest&) = delete;
    MoveOnlyRequest(MoveOnlyRequest&& other) noexcept : counts(other.counts), owns(std::exchange(other.owns, false)) {}
    MoveOnlyRequest& operator=(MoveOnlyRequest&& other) noexcept
    {
        if (this != &other)
        {
            if (owns)
                ++counts->destroyed;
            counts = other.counts;
            owns = std::exchange(other.owns, false);
        }
        return *this;
    }
    ~MoveOnlyRequest() noexcept
    {
        if (owns)
            ++counts->destroyed;
    }
};

using MoveOnlyQueue = ContinuationQueue<MoveOnlyRequest>;

struct MoveOnlyRetirementLog final
{
    std::array<std::uint64_t, MoveOnlyQueue::kCapacity> ordinals{};
    std::array<ContinuationRetirement, MoveOnlyQueue::kCapacity> outcomes{};
    std::size_t count = 0;

    void operator()(MoveOnlyQueue::Entry&& entry, ContinuationRetirement outcome) noexcept
    {
        if (count < ordinals.size())
        {
            ordinals[count] = entry.ordinal;
            outcomes[count] = outcome;
            ++count;
        }
    }
};

int gFailures = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s preserved-continuation.%s\n", condition ? "PASS" : "FAIL", name);
    if (!condition)
        ++gFailures;
}

HostRequest MakeRequest()
{
    return HostRequest::Create(HostWriteInput{HostWriteFormat::Project, false, std::nullopt});
}

void Enqueue(Queue& queue, std::uint64_t document, std::uint64_t ordinal,
             ContinuationKind kind = ContinuationKind::Command)
{
    HostRequest request = MakeRequest();
    const ContinuationAdmission result = queue.Enqueue(document, ordinal, kind, request);
    Expect(result == ContinuationAdmission::Queued, "fixture-entry-queued");
}

void MakeWrappedFullQueue(Queue& queue)
{
    for (std::uint64_t ordinal = 1; ordinal <= Queue::kCapacity; ++ordinal)
        Enqueue(queue, 77, ordinal);
    for (int i = 0; i < 3; ++i)
    {
        auto taken = queue.TakeReady(77, false);
        Expect(taken.ready.has_value(), "ring-fixture-advances-head");
    }
    for (std::uint64_t ordinal = 9; ordinal <= 11; ++ordinal)
        Enqueue(queue, 77, ordinal);
    Expect(queue.Size() == Queue::kCapacity, "ring-fixture-full-after-wrap");
}

void TestMoveOnlyRequestsRetireExactlyOnce() noexcept
{
    DestructionCounts counts;
    MoveOnlyQueue queue;
    for (std::uint64_t ordinal = 1; ordinal <= 3; ++ordinal)
    {
        MoveOnlyRequest request(counts);
        Expect(queue.Enqueue(12, ordinal, ContinuationKind::Command, request) == ContinuationAdmission::Queued,
               "move-only-request-admitted");
    }

    MoveOnlyRetirementLog log;
    Expect(queue.ClearExcept(12, 2, log) == 2 && queue.Size() == 1 && log.count == 2 && counts.destroyed == 2,
           "move-only-removed-requests-destroy-after-one-cancel-callback-each");
    Expect(log.ordinals[0] == 1 && log.ordinals[1] == 3 && log.outcomes[0] == ContinuationRetirement::Cancelled &&
               log.outcomes[1] == ContinuationRetirement::Cancelled,
           "move-only-cancellation-order-and-outcome");
    auto taken = queue.TakeReady(12, false);
    Expect(taken.ready && taken.ready->ordinal == 2 && counts.destroyed == 2,
           "move-only-preserved-owner-survives-take-ready");
    taken.ready.reset();
    Expect(counts.destroyed == 3, "move-only-preserved-owner-destroys-on-final-release");
}
void TestWrappedFullQueuePreservesHeadMiddleAndTail()
{
    constexpr std::array<std::uint64_t, 3> keepOrdinals{4, 7, 11};
    constexpr std::array<const char*, 3> names{"wrapped-head-match-preserved", "wrapped-middle-match-preserved",
                                               "wrapped-tail-match-preserved"};
    for (std::size_t test = 0; test < keepOrdinals.size(); ++test)
    {
        Queue queue;
        MakeWrappedFullQueue(queue);
        RetirementLog log;
        const std::size_t removed = queue.ClearExcept(77, keepOrdinals[test], log);
        const Queue::Entry* const kept = queue.Front();
        Expect(removed == Queue::kCapacity - 1 && queue.Size() == 1 && kept != nullptr && kept->documentSerial == 77 &&
                   kept->ordinal == keepOrdinals[test],
               names[test]);
        constexpr std::array<std::uint64_t, 8> originalOrder{4, 5, 6, 7, 8, 9, 10, 11};
        std::size_t expectedCallbacks = 0;
        bool fifo = !log.overflowed;
        for (const auto ordinal : originalOrder)
        {
            if (ordinal == keepOrdinals[test])
                continue;
            fifo = fifo && expectedCallbacks < log.count && log.records[expectedCallbacks].document == 77 &&
                   log.records[expectedCallbacks].ordinal == ordinal &&
                   log.records[expectedCallbacks].outcome == ContinuationRetirement::Cancelled;
            ++expectedCallbacks;
        }
        Expect(fifo && log.count == Queue::kCapacity - 1, "wrapped-retirements-once-in-original-fifo-order");

        for (std::uint64_t ordinal = 12; ordinal <= 18; ++ordinal)
            Enqueue(queue, 77, ordinal);
        Expect(queue.Size() == Queue::kCapacity && queue.Front() != nullptr &&
                   queue.Front()->ordinal == keepOrdinals[test],
               "seven-new-admissions-reclaim-capacity-behind-preserved-entry");
        HostRequest eighth = MakeRequest();
        const ContinuationReceipt refusedReceipt = eighth.Receipt();
        const ContinuationAdmission refusal = queue.Enqueue(77, 19, ContinuationKind::Command, eighth);
        Expect(refusal == ContinuationAdmission::Full && queue.Size() == Queue::kCapacity &&
                   refusedReceipt.Read().outcome == ContinuationOutcome::Pending,
               "eighth-new-admission-is-full-and-caller-keeps-request");

        auto taken = queue.TakeReady(77, false);
        bool fifoAfterPreserved = taken.ready && taken.ready->ordinal == keepOrdinals[test];
        for (std::uint64_t ordinal = 12; ordinal <= 18; ++ordinal)
        {
            auto admitted = queue.TakeReady(77, false);
            fifoAfterPreserved = fifoAfterPreserved && admitted.ready && admitted.ready->ordinal == ordinal;
        }
        Expect(fifoAfterPreserved && queue.Size() == 0, "preserved-entry-and-reclaimed-entries-dequeue-in-order");
    }
}

void TestMissingKeysHaveClearSemantics()
{
    constexpr std::array<std::uint64_t, 3> documents{91, 91, 91};
    constexpr std::array<std::uint64_t, 3> ordinals{2, 99, 0};
    constexpr std::array<std::uint64_t, 3> lookupDocuments{92, 91, 91};
    constexpr std::array<const char*, 3> names{"wrong-document-clears-all", "absent-ordinal-clears-all",
                                               "zero-ordinal-clears-all"};
    for (std::size_t test = 0; test < ordinals.size(); ++test)
    {
        Queue queue;
        for (std::uint64_t ordinal = 1; ordinal <= 3; ++ordinal)
            Enqueue(queue, documents[test], ordinal);
        RetirementLog log;
        const std::size_t removed = queue.ClearExcept(lookupDocuments[test], ordinals[test], log);
        bool allCancelledOnce = removed == 3 && queue.Size() == 0 && log.count == 3 && !log.overflowed;
        for (std::size_t i = 0; i < log.count; ++i)
            allCancelledOnce = allCancelledOnce && log.records[i].ordinal == i + 1 &&
                               log.records[i].outcome == ContinuationRetirement::Cancelled;
        Expect(allCancelledOnce, names[test]);
    }
}

void TestThreeBudgetedRequestsRetireAroundPreservedMiddle()
{
    HostInputBudget budget;
    Queue queue;
    auto first = HostRequest::TryCopyWrite(HostWriteFormat::Png, true, std::wstring_view(L"C:\\one.png"), budget);
    const std::size_t afterFirst = budget.Used();
    auto middle = HostRequest::TryCopyWrite(HostWriteFormat::Jpeg, true,
                                            std::wstring_view(L"C:\\preserved-middle-image.jpg"), budget);
    const std::size_t afterMiddle = budget.Used();
    auto last =
        HostRequest::TryCopyWrite(HostWriteFormat::Project, false, std::wstring_view(L"C:\\three.prism"), budget);
    const std::size_t beforeClear = budget.Used();
    Expect(first.has_value() && middle.has_value() && last.has_value() && afterFirst > 0 && afterMiddle > afterFirst &&
               beforeClear > afterMiddle,
           "three-real-host-requests-admit-with-independent-charges");
    if (!first || !middle || !last)
        return;

    const std::size_t firstCharge = afterFirst;
    const std::size_t middleCharge = afterMiddle - afterFirst;
    const std::size_t lastCharge = beforeClear - afterMiddle;
    const ContinuationReceipt firstReceipt = first->Receipt();
    const ContinuationReceipt middleReceipt = middle->Receipt();
    const ContinuationReceipt lastReceipt = last->Receipt();
    const bool allQueued =
        queue.Enqueue(56, 1, ContinuationKind::Command, first.value()) == ContinuationAdmission::Queued &&
        queue.Enqueue(56, 2, ContinuationKind::Import, middle.value()) == ContinuationAdmission::Queued &&
        queue.Enqueue(56, 3, ContinuationKind::Redo, last.value()) == ContinuationAdmission::Queued;
    Expect(allQueued && budget.Used() == beforeClear, "three-budgeted-requests-queued-with-charges-held");
    if (!allQueued)
        return;

    RetirementLog log;
    const std::size_t removed = queue.ClearExcept(56, 2, log);
    Expect(removed == 2 && log.count == 2 && !log.overflowed && log.records[0].ordinal == 1 &&
               log.records[1].ordinal == 3 && log.records[0].outcome == ContinuationRetirement::Cancelled &&
               log.records[1].outcome == ContinuationRetirement::Cancelled,
           "only-first-and-last-retire-once-as-cancelled-in-order");
    Expect(firstReceipt.Read().outcome == ContinuationOutcome::Cancelled &&
               lastReceipt.Read().outcome == ContinuationOutcome::Cancelled &&
               middleReceipt.Read().outcome == ContinuationOutcome::Pending,
           "external-receipts-observe-two-cancellations-and-preserved-pending-middle");
    Expect(firstCharge + middleCharge + lastCharge == beforeClear && budget.Used() == middleCharge,
           "retirement-returns-exactly-removed-request-charges");

    auto active = queue.TakeReady(56, false);
    Expect(active.ready && active.ready->ordinal == 2 && budget.Used() == middleCharge &&
               middleReceipt.Read().outcome == ContinuationOutcome::Pending,
           "preserved-middle-lease-and-receipt-survive-take-ready");
    if (active.ready)
        Expect(active.ready->input.Finish(ContinuationOutcome::Succeeded), "preserved-middle-publishes-success");
    active.ready.reset();
    Expect(budget.Used() == 0 && middleReceipt.Read().outcome == ContinuationOutcome::Succeeded,
           "preserved-middle-final-release-returns-last-charge");
}
void TestAdmissionLeaseAndReceiptFollowPreservedRequest()
{
    HostInputBudget budget;
    Queue queue;
    const std::wstring_view path = L"C:\\exports\\preserved.png";
    auto admitted = HostRequest::TryCopyWrite(HostWriteFormat::Png, true, path, budget);
    Expect(admitted.has_value() && budget.Used() > 0, "admitted-host-request-holds-real-budget-lease");
    if (!admitted)
        return;

    const ContinuationReceipt receipt = admitted->Receipt();
    const std::size_t admittedBytes = budget.Used();
    Expect(queue.Enqueue(55, 42, ContinuationKind::Command, admitted.value()) == ContinuationAdmission::Queued,
           "budgeted-request-queued-by-move");
    RetirementLog log;
    Expect(queue.ClearExcept(55, 42, log) == 0 && log.count == 0 && budget.Used() == admittedBytes &&
               receipt.Read().outcome == ContinuationOutcome::Pending,
           "preservation-keeps-budget-and-pending-receipt");

    auto active = queue.TakeReady(55, false);
    Expect(active.ready && active.ready->ordinal == 42 && budget.Used() == admittedBytes &&
               receipt.Read().outcome == ContinuationOutcome::Pending,
           "take-ready-transfers-same-live-lease-and-receipt");
    if (active.ready)
        Expect(active.ready->input.Finish(ContinuationOutcome::Succeeded), "active-owner-publishes-completion");
    active.ready.reset();
    Expect(budget.Used() == 0 && receipt.Read().outcome == ContinuationOutcome::Succeeded,
           "final-retirement-releases-charge-after-receipt-publication");
}

void TestActiveEntryIsOutsideQueueAndWatermarkSurvivesBarrierRemoval()
{
    Queue queue;
    Enqueue(queue, 63, 1, ContinuationKind::WandNew);
    Enqueue(queue, 63, 2, ContinuationKind::Command);
    Enqueue(queue, 63, 3, ContinuationKind::WandNew);
    auto active = queue.TakeReady(63, false);
    Expect(active.ready && active.ready->ordinal == 1, "active-entry-detached-before-escape");

    RetirementLog log;
    const std::size_t removed = queue.ClearExcept(63, 99, log);
    Expect(removed == 2 && queue.Size() == 0 && log.count == 2 && active.ready.has_value(),
           "absent-key-clears-queued-work-without-touching-active-entry");

    Queue barrierQueue;
    Enqueue(barrierQueue, 64, 1, ContinuationKind::WandNew);
    Enqueue(barrierQueue, 64, 2, ContinuationKind::Command);
    Enqueue(barrierQueue, 64, 3, ContinuationKind::WandNew);
    RetirementLog barrierLog;
    Expect(barrierQueue.ClearExcept(64, 1, barrierLog) == 2 && barrierQueue.Size() == 1,
           "escape-removes-younger-barrier-and-preview");
    HostRequest reappeared = MakeRequest();
    Expect(barrierQueue.Enqueue(64, 2, ContinuationKind::Command, reappeared) == ContinuationAdmission::InvalidOrder &&
               reappeared.Receipt().Read().outcome == ContinuationOutcome::Pending,
           "removed-ordinal-cannot-reappear-below-watermark");
    HostRequest laterPreview = MakeRequest();
    const ContinuationAdmission later =
        barrierQueue.EnqueueAdjacent(64, 4, ContinuationKind::WandNew, laterPreview, barrierLog,
                                     [](const HostRequest&, const HostRequest&) noexcept { return true; });
    Expect(later == ContinuationAdmission::Queued && barrierQueue.Size() == 2,
           "preview-does-not-coalesce-across-removed-ordinal-barrier");
    Expect(active.ready && active.ready->ordinal == 1, "previously-taken-active-entry-remains-owned-by-caller");
}
} // namespace

int RunPreservedContinuationTests()
{
    gFailures = 0;
    TestMoveOnlyRequestsRetireExactlyOnce();
    TestWrappedFullQueuePreservesHeadMiddleAndTail();
    TestMissingKeysHaveClearSemantics();
    TestThreeBudgetedRequestsRetireAroundPreservedMiddle();
    TestAdmissionLeaseAndReceiptFollowPreservedRequest();
    TestActiveEntryIsOutsideQueueAndWatermarkSurvivesBarrierRemoval();
    std::printf("%s preserved-continuation.summary failures=%d\n", gFailures == 0 ? "PASS" : "FAIL", gFailures);
    return gFailures;
}