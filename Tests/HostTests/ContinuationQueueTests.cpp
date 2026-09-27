#include "ContinuationQueueTests.h"

#include "../../Common/ContinuationQueue.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>

namespace
{
struct Lifetime final
{
    int destroyed = 0;
    int moves = 0;
};

struct CountedDelete final
{
    Lifetime* lifetime = nullptr;

    void operator()(int* value) const noexcept
    {
        if (value != nullptr)
        {
            ++lifetime->destroyed;
            delete value;
        }
    }
};

using Payload = std::unique_ptr<int, CountedDelete>;

struct TrackedRequest final
{
    int opcode = 0;
    int value = 0;
    Payload payload;
    Lifetime* lifetime = nullptr;

    TrackedRequest(Lifetime& owner, int operation, int parameter, int ownedValue)
        : opcode(operation), value(parameter), payload(new int(ownedValue), CountedDelete{&owner}), lifetime(&owner)
    {
    }

    TrackedRequest(TrackedRequest&& other) noexcept
        : opcode(other.opcode), value(other.value), payload(std::move(other.payload)), lifetime(other.lifetime)
    {
        ++lifetime->moves;
    }

    TrackedRequest& operator=(TrackedRequest&& other) noexcept
    {
        if (this != &other)
        {
            opcode = other.opcode;
            value = other.value;
            payload = std::move(other.payload);
            lifetime = other.lifetime;
            ++lifetime->moves;
        }
        return *this;
    }

    TrackedRequest(const TrackedRequest&) = delete;
    TrackedRequest& operator=(const TrackedRequest&) = delete;
};

using Queue = ContinuationQueue<TrackedRequest>;

int g_failures = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s continuation-queue.%s\n", condition ? "PASS" : "FAIL", name);
    if (!condition)
    {
        ++g_failures;
    }
}

TrackedRequest Make(Lifetime& lifetime, int opcode, int value, int ownedValue)
{
    return TrackedRequest{lifetime, opcode, value, ownedValue};
}

void Enqueue(Queue& queue, std::uint64_t document, std::uint64_t ordinal, ContinuationKind kind,
             TrackedRequest& request, ContinuationAdmission expected, const char* name)
{
    Expect(queue.Enqueue(document, ordinal, kind, request) == expected, name);
}

void TestCapacityAndOwnership()
{
    Lifetime lifetime;
    Queue queue;
    std::array<TrackedRequest, Queue::kCapacity> requests{
        Make(lifetime, 0, 10, 10), Make(lifetime, 1, 11, 11), Make(lifetime, 2, 12, 12), Make(lifetime, 3, 13, 13),
        Make(lifetime, 4, 14, 14), Make(lifetime, 5, 15, 15), Make(lifetime, 6, 16, 16), Make(lifetime, 7, 17, 17)};

    for (std::size_t i = 0; i < requests.size(); ++i)
    {
        Enqueue(queue, 41, static_cast<std::uint64_t>(i + 1), ContinuationKind::Command, requests[i],
                ContinuationAdmission::Queued, "capacity-eight-admits-in-order");
    }

    TrackedRequest ninth = Make(lifetime, 8, 18, 18);
    const int movesBeforeRefusal = lifetime.moves;
    Enqueue(queue, 41, 9, ContinuationKind::Command, ninth, ContinuationAdmission::Full, "ninth-entry-refused-full");
    Expect(queue.Size() == Queue::kCapacity && ninth.payload && *ninth.payload == 18 &&
               lifetime.moves == movesBeforeRefusal && lifetime.destroyed == 0,
           "full-retains-caller-ownership");
}

void TestWandCoalescingAndFifo()
{
    Lifetime lifetime;
    Queue queue;

    TrackedRequest firstWand = Make(lifetime, 1, 11, 101);
    Enqueue(queue, 7, 1, ContinuationKind::WandNew, firstWand, ContinuationAdmission::Queued,
            "wand-new-first-entry-queued");
    TrackedRequest add = Make(lifetime, 2, 12, 102);
    Enqueue(queue, 7, 2, ContinuationKind::WandAdd, add, ContinuationAdmission::Queued, "wand-add-keeps-own-entry");
    TrackedRequest secondWand = Make(lifetime, 3, 13, 103);
    Enqueue(queue, 7, 3, ContinuationKind::WandNew, secondWand, ContinuationAdmission::Queued,
            "wand-new-after-add-is-distinct");
    TrackedRequest subtract = Make(lifetime, 4, 14, 104);
    Enqueue(queue, 7, 4, ContinuationKind::WandSubtract, subtract, ContinuationAdmission::Queued,
            "wand-subtract-keeps-own-entry");
    TrackedRequest thirdWand = Make(lifetime, 5, 15, 105);
    Enqueue(queue, 7, 5, ContinuationKind::WandNew, thirdWand, ContinuationAdmission::Queued,
            "wand-new-after-subtract-is-distinct");
    TrackedRequest intersect = Make(lifetime, 6, 16, 106);
    Enqueue(queue, 7, 6, ContinuationKind::WandIntersect, intersect, ContinuationAdmission::Queued,
            "wand-intersect-keeps-own-entry");
    TrackedRequest importBatch = Make(lifetime, 7, 3, 3); // One payload represents the whole three-file drop.
    Enqueue(queue, 7, 7, ContinuationKind::Import, importBatch, ContinuationAdmission::Queued,
            "multi-file-import-is-one-entry");
    TrackedRequest fourthWand = Make(lifetime, 8, 17, 107);
    Enqueue(queue, 7, 8, ContinuationKind::WandNew, fourthWand, ContinuationAdmission::Queued,
            "wand-new-after-import-is-distinct");

    TrackedRequest fullTailReplacement = Make(lifetime, 9, 19, 109);
    Enqueue(queue, 7, 9, ContinuationKind::WandNew, fullTailReplacement, ContinuationAdmission::Coalesced,
            "consecutive-new-wand-coalesces-while-full");
    Expect(queue.Size() == Queue::kCapacity && lifetime.destroyed == 1,
           "full-coalesce-replaces-only-prior-tail-payload");

    constexpr std::array<ContinuationKind, 8> expectedKinds{ContinuationKind::WandNew, ContinuationKind::WandAdd,
                                                            ContinuationKind::WandNew, ContinuationKind::WandSubtract,
                                                            ContinuationKind::WandNew, ContinuationKind::WandIntersect,
                                                            ContinuationKind::Import,  ContinuationKind::WandNew};
    constexpr std::array<int, 8> expectedParameters{11, 12, 13, 14, 15, 16, 3, 19};
    constexpr std::array<int, 8> expectedPayloads{101, 102, 103, 104, 105, 106, 3, 109};
    for (std::size_t i = 0; i < expectedKinds.size(); ++i)
    {
        const Queue::Taken taken = queue.TakeReady(7, false);
        Expect(taken.ready && taken.stale == 0 && taken.ready->kind == expectedKinds[i] &&
                   taken.ready->input.value == expectedParameters[i] && taken.ready->input.payload &&
                   *taken.ready->input.payload == expectedPayloads[i],
               "wand-modes-and-import-remain-fifo");
    }
    Expect(queue.Size() == 0, "fifo-drains-all-eight-entries");

    Lifetime gapLifetime;
    Queue gapQueue;
    TrackedRequest beforeStroke = Make(gapLifetime, 1, 21, 201);
    Enqueue(gapQueue, 9, 1, ContinuationKind::WandNew, beforeStroke, ContinuationAdmission::Queued,
            "wand-before-stroke-queued");
    // Ordinal 2 belongs to an independently recorded stroke, not to this queue.
    TrackedRequest afterStroke = Make(gapLifetime, 2, 22, 202);
    Enqueue(gapQueue, 9, 3, ContinuationKind::WandNew, afterStroke, ContinuationAdmission::Queued,
            "stroke-ordinal-gap-prevents-coalescing");
    Expect(gapQueue.Size() == 2, "interleaved-stroke-keeps-both-wands");

    Lifetime documentLifetime;
    Queue documentQueue;
    TrackedRequest firstDocumentWand = Make(documentLifetime, 1, 31, 231);
    Enqueue(documentQueue, 10, 1, ContinuationKind::WandNew, firstDocumentWand, ContinuationAdmission::Queued,
            "wand-first-document-queued");
    TrackedRequest otherDocumentWand = Make(documentLifetime, 2, 32, 232);
    Enqueue(documentQueue, 11, 2, ContinuationKind::WandNew, otherDocumentWand, ContinuationAdmission::Queued,
            "wand-other-document-does-not-coalesce");
    Expect(documentQueue.Size() == 2, "consecutive-new-wands-require-same-document");
}

void TestBusyAndLiveDequeueState()
{
    Lifetime lifetime;
    Queue queue;
    TrackedRequest queued = Make(lifetime, 1, 7, 70);
    Enqueue(queue, 15, 1, ContinuationKind::Command, queued, ContinuationAdmission::Queued, "busy-case-command-queued");

    const Queue::Entry* const frontBefore = queue.Front();
    const int movesBeforeBusyTake = lifetime.moves;
    const Queue::Taken busy = queue.TakeReady(15, true);
    Expect(!busy.ready && busy.stale == 0 && queue.Size() == 1 && queue.Front() == frontBefore &&
               queue.Front()->input.payload && *queue.Front()->input.payload == 70 &&
               lifetime.moves == movesBeforeBusyTake,
           "busy-take-does-not-pop-or-move-input");

    // A preceding history job changes the live document while this parameter-only request waits.
    int liveDocumentValue = 20;
    liveDocumentValue = 100; // The predecessor applied before the continuation is taken.
    const Queue::Taken ready = queue.TakeReady(15, false);
    Expect(ready.ready && ready.ready->input.value == 7 && ready.ready->input.payload &&
               *ready.ready->input.payload == 70,
           "dequeue-yields-parameters-without-document-snapshot");
    if (ready.ready)
    {
        liveDocumentValue += ready.ready->input.value;
    }
    Expect(liveDocumentValue == 107, "continuation-computes-from-current-document-value");
}

void TestStaleSkipAndDestruction()
{
    Lifetime lifetime;
    Queue queue;
    TrackedRequest staleOne = Make(lifetime, 1, 1, 31);
    Enqueue(queue, 30, 1, ContinuationKind::Command, staleOne, ContinuationAdmission::Queued, "stale-one-queued");
    TrackedRequest staleTwo = Make(lifetime, 2, 2, 32);
    Enqueue(queue, 30, 2, ContinuationKind::Import, staleTwo, ContinuationAdmission::Queued, "stale-two-queued");
    TrackedRequest current = Make(lifetime, 3, 3, 33);
    Enqueue(queue, 31, 3, ContinuationKind::Undo, current, ContinuationAdmission::Queued,
            "current-document-entry-queued");
    TrackedRequest later = Make(lifetime, 4, 4, 34);
    Enqueue(queue, 31, 4, ContinuationKind::Redo, later, ContinuationAdmission::Queued, "entry-after-current-queued");

    const Queue::Taken first = queue.TakeReady(31, false);
    Expect(first.stale == 2 && first.ready && first.ready->ordinal == 3 && first.ready->input.value == 3 &&
               lifetime.destroyed == 2 && queue.Size() == 1,
           "stale-entries-destroyed-counted-before-first-match");
    const Queue::Taken second = queue.TakeReady(31, false);
    Expect(second.stale == 0 && second.ready && second.ready->ordinal == 4 &&
               second.ready->kind == ContinuationKind::Redo && queue.Size() == 0,
           "stale-scan-stops-at-first-matching-document");
}

void TestRingWrapAndClearOrder()
{
    Lifetime lifetime;
    Queue queue;
    for (std::uint64_t ordinal = 1; ordinal <= 6; ++ordinal)
    {
        TrackedRequest request =
            Make(lifetime, static_cast<int>(ordinal), static_cast<int>(ordinal), static_cast<int>(ordinal + 100));
        Enqueue(queue, 51, ordinal, ContinuationKind::Command, request, ContinuationAdmission::Queued,
                "initial-ring-fill");
    }
    for (std::uint64_t ordinal = 1; ordinal <= 5; ++ordinal)
    {
        const Queue::Taken taken = queue.TakeReady(51, false);
        Expect(taken.ready && taken.ready->ordinal == ordinal && taken.ready->input.value == ordinal,
               "ring-prefix-popped");
    }
    for (std::uint64_t ordinal = 7; ordinal <= 12; ++ordinal)
    {
        TrackedRequest request =
            Make(lifetime, static_cast<int>(ordinal), static_cast<int>(ordinal), static_cast<int>(ordinal + 100));
        Enqueue(queue, 51, ordinal, ContinuationKind::Command, request, ContinuationAdmission::Queued,
                "ring-refill-after-wrap");
    }
    constexpr std::array<std::uint64_t, 7> expectedOrdinals{6, 7, 8, 9, 10, 11, 12};
    for (const std::uint64_t expected : expectedOrdinals)
    {
        const Queue::Taken taken = queue.TakeReady(51, false);
        Expect(taken.ready && taken.ready->ordinal == expected && taken.ready->input.value == expected,
               "ring-wrap-preserves-fifo");
    }

    TrackedRequest completedInput = Make(lifetime, 1, 1, 200);
    Enqueue(queue, 51, 13, ContinuationKind::Command, completedInput, ContinuationAdmission::Queued,
            "clear-case-completed-entry-queued");
    Queue::Taken completed = queue.TakeReady(51, false);
    TrackedRequest one = Make(lifetime, 2, 2, 201);
    Enqueue(queue, 51, 14, ContinuationKind::Command, one, ContinuationAdmission::Queued, "clear-case-first-queued");
    TrackedRequest two = Make(lifetime, 3, 3, 202);
    Enqueue(queue, 51, 15, ContinuationKind::Command, two, ContinuationAdmission::Queued, "clear-case-second-queued");
    const int destroyedBeforeClear = lifetime.destroyed;
    Expect(queue.Clear() == 2 && queue.Size() == 0 && lifetime.destroyed == destroyedBeforeClear + 2 &&
               completed.ready && completed.ready->input.payload && *completed.ready->input.payload == 200,
           "clear-drops-queued-only-and-retains-completed-input");

    TrackedRequest next = Make(lifetime, 4, 4, 203);
    Enqueue(queue, 51, 16, ContinuationKind::Command, next, ContinuationAdmission::Queued,
            "clear-preserves-monotonic-ordinal");
    TrackedRequest retrograde = Make(lifetime, 4, 4, 204);
    Enqueue(queue, 51, 15, ContinuationKind::Command, retrograde, ContinuationAdmission::InvalidOrder,
            "clear-does-not-reset-ordinal");
    Expect(retrograde.payload && *retrograde.payload == 204, "post-clear-invalid-order-retains-caller");
}

void TestOrdinalValidation()
{
    Lifetime lifetime;
    Queue queue;
    TrackedRequest zero = Make(lifetime, 1, 1, 301);
    Enqueue(queue, 61, 0, ContinuationKind::Command, zero, ContinuationAdmission::InvalidOrder,
            "ordinal-zero-rejected");
    TrackedRequest first = Make(lifetime, 2, 2, 302);
    Enqueue(queue, 61, 5, ContinuationKind::Command, first, ContinuationAdmission::Queued,
            "first-positive-ordinal-accepted");
    TrackedRequest duplicate = Make(lifetime, 3, 3, 303);
    Enqueue(queue, 61, 5, ContinuationKind::Command, duplicate, ContinuationAdmission::InvalidOrder,
            "duplicate-ordinal-rejected");
    TrackedRequest retrograde = Make(lifetime, 4, 4, 304);
    Enqueue(queue, 61, 4, ContinuationKind::Command, retrograde, ContinuationAdmission::InvalidOrder,
            "retrograde-ordinal-rejected");
    Expect(zero.payload && duplicate.payload && retrograde.payload && lifetime.destroyed == 0,
           "invalid-ordinals-retain-caller-ownership");

    Lifetime maximumLifetime;
    Queue maximumQueue;
    constexpr std::uint64_t maximum = std::numeric_limits<std::uint64_t>::max();
    TrackedRequest maxRequest = Make(maximumLifetime, 5, 5, 305);
    Enqueue(maximumQueue, 62, maximum, ContinuationKind::Command, maxRequest, ContinuationAdmission::Queued,
            "maximum-ordinal-accepted");
    TrackedRequest duplicateMax = Make(maximumLifetime, 6, 6, 306);
    Enqueue(maximumQueue, 62, maximum, ContinuationKind::Command, duplicateMax, ContinuationAdmission::InvalidOrder,
            "maximum-ordinal-accepted-only-once");
    TrackedRequest wrappedZero = Make(maximumLifetime, 7, 7, 307);
    Enqueue(maximumQueue, 62, 0, ContinuationKind::Command, wrappedZero, ContinuationAdmission::InvalidOrder,
            "zero-after-maximum-wrap-rejected");
    Expect(duplicateMax.payload && wrappedZero.payload && maximumQueue.Size() == 1 && maximumLifetime.destroyed == 0,
           "maximum-followup-refusals-retain-caller-ownership");
}

void TestUndoRedoOrder()
{
    Lifetime lifetime;
    Queue queue;
    TrackedRequest undoOne = Make(lifetime, 101, 900, 401);
    Enqueue(queue, 71, 1, ContinuationKind::Undo, undoOne, ContinuationAdmission::Queued, "undo-one-queued");
    TrackedRequest redo = Make(lifetime, 102, 1200, 402);
    Enqueue(queue, 71, 2, ContinuationKind::Redo, redo, ContinuationAdmission::Queued, "redo-queued-after-undo");
    TrackedRequest undoTwo = Make(lifetime, 103, 800, 403);
    Enqueue(queue, 71, 3, ContinuationKind::Undo, undoTwo, ContinuationAdmission::Queued, "undo-two-queued-after-redo");

    constexpr std::array<ContinuationKind, 3> expectedKinds{ContinuationKind::Undo, ContinuationKind::Redo,
                                                            ContinuationKind::Undo};
    constexpr std::array<int, 3> expectedLiteralValues{900, 1200, 800};
    constexpr std::array<int, 3> expectedPayloads{401, 402, 403};
    for (std::size_t i = 0; i < expectedKinds.size(); ++i)
    {
        const Queue::Taken taken = queue.TakeReady(71, false);
        Expect(taken.ready && taken.ready->kind == expectedKinds[i] &&
                   taken.ready->input.value == expectedLiteralValues[i] && taken.ready->input.payload &&
                   *taken.ready->input.payload == expectedPayloads[i],
               "undo-redo-observes-literal-fifo-values");
    }
}
} // namespace

int RunContinuationQueueTests()
{
    g_failures = 0;
    TestCapacityAndOwnership();
    TestWandCoalescingAndFifo();
    TestBusyAndLiveDequeueState();
    TestStaleSkipAndDestruction();
    TestRingWrapAndClearOrder();
    TestOrdinalValidation();
    TestUndoRedoOrder();
    return g_failures;
}
