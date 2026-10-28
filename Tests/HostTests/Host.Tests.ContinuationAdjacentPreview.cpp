#include "Host.Tests.ContinuationAdjacentPreview.h"

#include "Common/ContinuationQueue.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <type_traits>
#include <utility>

namespace
{
int gFailures = 0;

struct PreviewRequest final
{
    std::uint32_t opening;
    std::uint32_t target;
    std::uint32_t value;
    std::uint32_t phase;
};

static_assert(std::is_trivial_v<PreviewRequest> && std::is_standard_layout_v<PreviewRequest>);

using Queue = ContinuationQueue<PreviewRequest>;

struct ReceiptLog final
{
    std::array<unsigned int, 32> counts{};
    std::array<ContinuationRetirement, 32> reasons{};
};

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s continuation-adjacent-preview.%s\n", condition ? "PASS" : "FAIL", name);
    if (!condition)
    {
        ++gFailures;
    }
}

PreviewRequest Request(std::uint32_t value, std::uint32_t opening = 1, std::uint32_t target = 2,
                       std::uint32_t phase = 3) noexcept
{
    return PreviewRequest{opening, target, value, phase};
}

bool SamePreview(const PreviewRequest& oldInput, const PreviewRequest& newInput) noexcept
{
    return oldInput.opening == newInput.opening && oldInput.target == newInput.target &&
           oldInput.phase == newInput.phase;
}

auto Recorder(ReceiptLog& log) noexcept
{
    return [&log](Queue::Entry&& entry, ContinuationRetirement reason) noexcept
    {
        const std::size_t receipt = entry.input.value;
        if (receipt < log.counts.size())
        {
            ++log.counts[receipt];
            log.reasons[receipt] = reason;
        }
    };
}

void TestSameKeyCoalescesAndRetiresOldReceipt()
{
    Queue queue;
    ReceiptLog receipts;
    auto retire = Recorder(receipts);
    auto equivalentCalls = 0;
    auto equivalent = [&equivalentCalls](const PreviewRequest& oldInput, const PreviewRequest& newInput) noexcept
    {
        ++equivalentCalls;
        return SamePreview(oldInput, newInput);
    };
    PreviewRequest first = Request(1);
    PreviewRequest second = Request(2);
    const ContinuationAdmission initial =
        queue.EnqueueAdjacent(10, 1, ContinuationKind::Import, first, retire, equivalent);
    const ContinuationAdmission replaced =
        queue.EnqueueAdjacent(10, 2, ContinuationKind::Import, second, retire, equivalent);
    const Queue::Entry* front = queue.Front();
    Expect(initial == ContinuationAdmission::Queued && replaced == ContinuationAdmission::Coalesced &&
               queue.Size() == 1 && front != nullptr && front->ordinal == 2 && front->input.value == 2,
           "same-key-coalesces-to-newest-request");
    Expect(equivalentCalls == 1 && receipts.counts[1] == 1 &&
               receipts.reasons[1] == ContinuationRetirement::Superseded && receipts.counts[2] == 0,
           "superseded-receipt-retired-once");
}

void TestDifferentOpeningTargetAndPhaseStaySeparate()
{
    for (std::uint32_t mismatch = 0; mismatch < 3; ++mismatch)
    {
        Queue queue;
        ReceiptLog receipts;
        auto retire = Recorder(receipts);
        auto equivalent = [](const PreviewRequest& oldInput, const PreviewRequest& newInput) noexcept
        { return SamePreview(oldInput, newInput); };
        PreviewRequest first = Request(1);
        PreviewRequest next = mismatch == 0   ? Request(2, 9, 2, 3)
                              : mismatch == 1 ? Request(2, 1, 9, 3)
                                              : Request(2, 1, 2, 9);
        const ContinuationAdmission firstAdmission =
            queue.EnqueueAdjacent(10, 1, ContinuationKind::Import, first, retire, equivalent);
        const ContinuationAdmission secondAdmission =
            queue.EnqueueAdjacent(10, 2, ContinuationKind::Import, next, retire, equivalent);
        const char* name = mismatch == 0   ? "different-opening-does-not-coalesce"
                           : mismatch == 1 ? "different-target-does-not-coalesce"
                                           : "different-phase-does-not-coalesce";
        Expect(firstAdmission == ContinuationAdmission::Queued && secondAdmission == ContinuationAdmission::Queued &&
                   queue.Size() == 2 && receipts.counts[1] == 0,
               name);
    }
}

void TestOrdinalGapAndDifferentDocumentStaySeparate()
{
    auto equivalentCalls = 0;
    auto equivalent = [&equivalentCalls](const PreviewRequest& oldInput, const PreviewRequest& newInput) noexcept
    {
        ++equivalentCalls;
        return SamePreview(oldInput, newInput);
    };
    {
        Queue queue;
        ReceiptLog receipts;
        auto retire = Recorder(receipts);
        PreviewRequest first = Request(1);
        PreviewRequest next = Request(2);
        static_cast<void>(queue.EnqueueAdjacent(10, 1, ContinuationKind::Import, first, retire, equivalent));
        const ContinuationAdmission gap =
            queue.EnqueueAdjacent(10, 3, ContinuationKind::Import, next, retire, equivalent);
        Expect(gap == ContinuationAdmission::Queued && queue.Size() == 2 && equivalentCalls == 0,
               "ordinal-gap-does-not-call-equivalent");
    }
    equivalentCalls = 0;
    {
        Queue queue;
        ReceiptLog receipts;
        auto retire = Recorder(receipts);
        PreviewRequest first = Request(1);
        PreviewRequest next = Request(2);
        static_cast<void>(queue.EnqueueAdjacent(10, 1, ContinuationKind::Import, first, retire, equivalent));
        const ContinuationAdmission differentDocument =
            queue.EnqueueAdjacent(11, 2, ContinuationKind::Import, next, retire, equivalent);
        Expect(differentDocument == ContinuationAdmission::Queued && queue.Size() == 2 && equivalentCalls == 0,
               "different-document-does-not-call-equivalent");
    }
}

void TestDifferentKindStaysSeparate()
{
    Queue queue;
    ReceiptLog receipts;
    auto retire = Recorder(receipts);
    auto equivalentCalls = 0;
    auto equivalent = [&equivalentCalls](const PreviewRequest& oldInput, const PreviewRequest& newInput) noexcept
    {
        ++equivalentCalls;
        return SamePreview(oldInput, newInput);
    };
    PreviewRequest first = Request(1);
    PreviewRequest next = Request(2);
    static_cast<void>(queue.EnqueueAdjacent(10, 1, ContinuationKind::Import, first, retire, equivalent));
    const ContinuationAdmission admission =
        queue.EnqueueAdjacent(10, 2, ContinuationKind::WandNew, next, retire, equivalent);
    Expect(admission == ContinuationAdmission::Queued && queue.Size() == 2 && equivalentCalls == 0 &&
               receipts.counts[1] == 0,
           "different-kind-does-not-call-equivalent");
}

void TestFullQueueCoalescesButRefusalPreservesRequest()
{
    auto equivalent = [](const PreviewRequest& oldInput, const PreviewRequest& newInput) noexcept
    { return SamePreview(oldInput, newInput); };
    {
        Queue queue;
        ReceiptLog receipts;
        auto retire = Recorder(receipts);
        for (std::uint64_t ordinal = 1; ordinal <= Queue::kCapacity; ++ordinal)
        {
            PreviewRequest request = Request(static_cast<std::uint32_t>(ordinal));
            static_cast<void>(queue.Enqueue(10, ordinal, ContinuationKind::Import, request, retire));
        }
        PreviewRequest newest = Request(20);
        const ContinuationAdmission admission =
            queue.EnqueueAdjacent(10, Queue::kCapacity + 1, ContinuationKind::Import, newest, retire, equivalent);
        for (std::size_t index = 0; index + 1 < Queue::kCapacity; ++index)
        {
            static_cast<void>(queue.TakeReady(10, false));
        }
        const Queue::Entry* tail = queue.Front();
        Expect(admission == ContinuationAdmission::Coalesced && queue.Size() == 1 && tail != nullptr &&
                   tail->input.value == 20 && receipts.counts[Queue::kCapacity] == 1,
               "full-ring-tail-coalesces");
        static_cast<void>(queue.Clear(retire));
    }
    {
        Queue queue;
        ReceiptLog receipts;
        auto retire = Recorder(receipts);
        for (std::uint64_t ordinal = 1; ordinal <= Queue::kCapacity; ++ordinal)
        {
            PreviewRequest request = Request(static_cast<std::uint32_t>(ordinal));
            static_cast<void>(queue.Enqueue(10, ordinal, ContinuationKind::Import, request, retire));
        }
        PreviewRequest refused = Request(21, 1, 2, 99);
        const PreviewRequest original = refused;
        const ContinuationAdmission admission =
            queue.EnqueueAdjacent(10, Queue::kCapacity + 1, ContinuationKind::Import, refused, retire, equivalent);
        Expect(admission == ContinuationAdmission::Full && queue.Size() == Queue::kCapacity &&
                   refused.opening == original.opening && refused.target == original.target &&
                   refused.value == original.value && refused.phase == original.phase &&
                   receipts.counts[Queue::kCapacity] == 0 && receipts.counts[21] == 0,
               "full-nonmatching-refusal-preserves-request-and-callbacks");
        static_cast<void>(queue.Clear(retire));
    }
}

void TestClearAndStaleRetireExactlyOnce()
{
    {
        Queue queue;
        ReceiptLog receipts;
        auto retire = Recorder(receipts);
        PreviewRequest request = Request(4);
        static_cast<void>(queue.EnqueueAdjacent(10, 1, ContinuationKind::Command, request, retire, SamePreview));
        const std::size_t cleared = queue.Clear(retire);
        const std::size_t clearedAgain = queue.Clear(retire);
        Expect(cleared == 1 && clearedAgain == 0 && receipts.counts[4] == 1 &&
                   receipts.reasons[4] == ContinuationRetirement::Cancelled,
               "cleared-receipt-retired-once");
    }
    {
        Queue queue;
        ReceiptLog receipts;
        auto retire = Recorder(receipts);
        PreviewRequest request = Request(5);
        static_cast<void>(queue.EnqueueAdjacent(10, 1, ContinuationKind::Command, request, retire, SamePreview));
        const Queue::Taken taken = queue.TakeReady(11, false, retire);
        Expect(taken.stale == 1 && !taken.ready.has_value() && queue.Size() == 0 && receipts.counts[5] == 1 &&
                   receipts.reasons[5] == ContinuationRetirement::Stale,
               "stale-receipt-retired-once");
    }
}

void TestInvalidOrderingAndEmptyQueue()
{
    Queue queue;
    ReceiptLog receipts;
    auto retire = Recorder(receipts);
    auto equivalentCalls = 0;
    auto equivalent = [&equivalentCalls](const PreviewRequest& oldInput, const PreviewRequest& newInput) noexcept
    {
        ++equivalentCalls;
        return SamePreview(oldInput, newInput);
    };
    PreviewRequest request = Request(6);
    const Queue::Taken empty = queue.TakeReady(10, false, retire);
    const std::size_t emptyClear = queue.Clear(retire);
    const ContinuationAdmission zero =
        queue.EnqueueAdjacent(10, 0, ContinuationKind::Import, request, retire, equivalent);
    PreviewRequest accepted = Request(7);
    static_cast<void>(queue.EnqueueAdjacent(10, 3, ContinuationKind::Import, accepted, retire, equivalent));
    const ContinuationAdmission backwards =
        queue.EnqueueAdjacent(10, 3, ContinuationKind::Import, request, retire, equivalent);
    Expect(!empty.ready.has_value() && empty.stale == 0 && emptyClear == 0 && queue.Size() == 1,
           "empty-queue-take-and-clear");
    Expect(zero == ContinuationAdmission::InvalidOrder && backwards == ContinuationAdmission::InvalidOrder &&
               equivalentCalls == 0 && request.value == 6 && receipts.counts[6] == 0,
           "invalid-order-refuses-without-callback-or-move");
    static_cast<void>(queue.Clear(retire));
}

struct OwnedPreview final
{
    OwnedPreview(unsigned int id, std::array<unsigned int, 32>& destroyed) noexcept : receipt(id), counts(&destroyed) {}
    OwnedPreview(const OwnedPreview&) = delete;
    OwnedPreview& operator=(const OwnedPreview&) = delete;
    OwnedPreview(OwnedPreview&& other) noexcept : receipt(other.receipt), counts(std::exchange(other.counts, nullptr))
    {
    }
    OwnedPreview& operator=(OwnedPreview&& other) noexcept
    {
        if (this != &other)
        {
            Release();
            receipt = other.receipt;
            counts = std::exchange(other.counts, nullptr);
        }
        return *this;
    }
    ~OwnedPreview()
    {
        Release();
    }
    void Release() noexcept
    {
        if (counts)
        {
            ++(*counts)[receipt];
            counts = nullptr;
        }
    }
    unsigned int receipt;
    std::array<unsigned int, 32>* counts;
};

void TestMoveOwnershipAndFullRefusal()
{
    std::array<unsigned int, 32> destroyed{}, retired{};
    {
        ContinuationQueue<OwnedPreview> queue;
        auto retire = [&retired](ContinuationQueue<OwnedPreview>::Entry&& entry, ContinuationRetirement) noexcept
        { ++retired[entry.input.receipt]; };
        auto equivalent = [](const OwnedPreview&, const OwnedPreview&) noexcept { return true; };
        OwnedPreview first(1, destroyed), second(2, destroyed);
        const auto initial = queue.EnqueueAdjacent(10, 1, ContinuationKind::Command, first, retire, equivalent);
        const auto replacement = queue.EnqueueAdjacent(10, 2, ContinuationKind::Command, second, retire, equivalent);
        Expect(initial == ContinuationAdmission::Queued && replacement == ContinuationAdmission::Coalesced &&
                   !first.counts && !second.counts && destroyed[1] == 1 && destroyed[2] == 0 && retired[1] == 1,
               "move-owned-superseded-request-releases-once");
        static_cast<void>(queue.Clear(retire));
        Expect(destroyed[2] == 1 && retired[2] == 1, "move-owned-newest-request-releases-on-clear");
    }
    Expect(destroyed[1] == 1 && destroyed[2] == 1, "moved-from-callers-do-not-release-again");
    {
        ContinuationQueue<OwnedPreview> queue;
        auto retire = [&retired](ContinuationQueue<OwnedPreview>::Entry&& entry, ContinuationRetirement) noexcept
        { ++retired[entry.input.receipt]; };
        for (unsigned int id = 3; id < 11; ++id)
        {
            OwnedPreview input(id, destroyed);
            static_cast<void>(queue.Enqueue(10, id, ContinuationKind::Command, input, retire));
        }
        OwnedPreview refused(12, destroyed);
        const auto result =
            queue.EnqueueAdjacent(10, 11, ContinuationKind::Command, refused, retire,
                                  [](const OwnedPreview&, const OwnedPreview&) noexcept { return false; });
        Expect(result == ContinuationAdmission::Full && queue.Size() == 8 && refused.counts == &destroyed &&
                   destroyed[12] == 0 && retired[12] == 0,
               "full-refusal-keeps-move-owned-request-with-caller");
        static_cast<void>(queue.Clear(retire));
        bool once = true;
        for (unsigned int id = 3; id < 11; ++id)
            once &= destroyed[id] == 1 && retired[id] == 1;
        Expect(once, "full-ring-owned-entries-release-and-retire-once");
    }
    Expect(destroyed[12] == 1 && retired[12] == 0, "refused-owner-releases-its-own-request");
}
} // namespace

int RunContinuationAdjacentPreviewTests()
{
    gFailures = 0;
    TestSameKeyCoalescesAndRetiresOldReceipt();
    TestDifferentOpeningTargetAndPhaseStaySeparate();
    TestOrdinalGapAndDifferentDocumentStaySeparate();
    TestDifferentKindStaysSeparate();
    TestFullQueueCoalescesButRefusalPreservesRequest();
    TestClearAndStaleRetireExactlyOnce();
    TestInvalidOrderingAndEmptyQueue();
    TestMoveOwnershipAndFullRefusal();
    return gFailures;
}
