#include "Host.Tests.ContinuationQueue.h"

#include "Common/ContinuationQueue.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <optional>
#include <type_traits>

namespace
{
struct Lifetime final
{
    int destroyed = 0;
    int moves = 0;
};

enum class RequestState
{
    Pending,
    Failed,
    Succeeded,
    Cancelled,
    Stale
};

struct RetirementReceipt final
{
    bool retired = false;
    ContinuationRetirement outcome = ContinuationRetirement::Stale;
    RequestState state = RequestState::Pending;
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
    std::shared_ptr<RetirementReceipt> receipt;

    TrackedRequest(Lifetime& owner, int operation, int parameter, int ownedValue)
        : opcode(operation), value(parameter), payload(new int(ownedValue), CountedDelete{&owner}), lifetime(&owner)
    {
    }

    TrackedRequest(Lifetime& owner, int operation, int parameter, int ownedValue,
                   std::shared_ptr<RetirementReceipt> retirementReceipt)
        : TrackedRequest(owner, operation, parameter, ownedValue)
    {
        receipt = std::move(retirementReceipt);
    }

    TrackedRequest(TrackedRequest&& other) noexcept
        : opcode(other.opcode), value(other.value), payload(std::move(other.payload)), lifetime(other.lifetime),
          receipt(std::move(other.receipt))
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
            receipt = std::move(other.receipt);
            ++lifetime->moves;
        }
        return *this;
    }

    TrackedRequest(const TrackedRequest&) = delete;
    TrackedRequest& operator=(const TrackedRequest&) = delete;
};

using Queue = ContinuationQueue<TrackedRequest>;

static_assert(std::is_nothrow_destructible_v<Queue>);
static_assert(std::is_nothrow_move_constructible_v<Queue>);

struct RetirementRecord final
{
    std::uint64_t documentSerial = 0;
    std::uint64_t ordinal = 0;
    ContinuationKind kind = ContinuationKind::Command;
    ContinuationRetirement outcome = ContinuationRetirement::Stale;
    int destroyedAtCallback = 0;
    bool ownsPayload = false;
};

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626)
struct RetirementLog final
{
    std::array<RetirementRecord, 32> records{};
    std::size_t count = 0;
    bool overflowed = false;
    Lifetime* lifetime = nullptr;
    std::uint64_t retainOrdinal = 0;
    std::optional<Queue::Entry> retained;

    void operator()(Queue::Entry&& entry, ContinuationRetirement outcome) noexcept
    {
        if (count < records.size())
        {
            records[count] = RetirementRecord{entry.documentSerial,
                                              entry.ordinal,
                                              entry.kind,
                                              outcome,
                                              lifetime == nullptr ? 0 : lifetime->destroyed,
                                              entry.input.payload != nullptr};
            ++count;
        }
        else
        {
            overflowed = true;
        }
        if (entry.input.receipt)
        {
            entry.input.receipt->retired = true;
            entry.input.receipt->outcome = outcome;
            entry.input.receipt->state =
                outcome == ContinuationRetirement::Stale ? RequestState::Stale : RequestState::Cancelled;
        }
        if (entry.ordinal == retainOrdinal)
        {
            retained.emplace(std::move(entry));
        }
    }
};
#pragma warning(pop)

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

TrackedRequest MakeWithReceipt(Lifetime& lifetime, int opcode, int value, int ownedValue,
                               const std::shared_ptr<RetirementReceipt>& receipt)
{
    return TrackedRequest(lifetime, opcode, value, ownedValue, receipt);
}

void ExpectRetirement(const RetirementLog& log, std::size_t index, std::uint64_t documentSerial, std::uint64_t ordinal,
                      ContinuationKind kind, ContinuationRetirement outcome, int destroyedAtCallback, const char* name)
{
    const bool exists = index < log.count;
    Expect(!log.overflowed && exists && log.records[index].documentSerial == documentSerial &&
               log.records[index].ordinal == ordinal && log.records[index].kind == kind &&
               log.records[index].outcome == outcome && log.records[index].destroyedAtCallback == destroyedAtCallback &&
               log.records[index].ownsPayload,
           name);
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
        Expect(taken.ready && taken.ready->ordinal == ordinal && taken.ready->input.value == static_cast<int>(ordinal),
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
        Expect(taken.ready && taken.ready->ordinal == expected &&
                   taken.ready->input.value == static_cast<int>(expected),
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

void TestSupersededRetirement()
{
    Lifetime lifetime;
    Queue queue;
    RetirementLog retire;
    retire.lifetime = &lifetime;
    std::array<std::shared_ptr<RetirementReceipt>, 2> receipts{std::make_shared<RetirementReceipt>(),
                                                               std::make_shared<RetirementReceipt>()};

    TrackedRequest original = MakeWithReceipt(lifetime, 1, 11, 501, receipts[0]);
    const ContinuationAdmission originalAdmission = queue.Enqueue(81, 40, ContinuationKind::WandNew, original, retire);
    Expect(originalAdmission == ContinuationAdmission::Queued, "retirement-first-wand-queued");
    if (originalAdmission != ContinuationAdmission::Queued)
    {
        return;
    }
    TrackedRequest replacement = MakeWithReceipt(lifetime, 2, 12, 502, receipts[1]);
    const ContinuationAdmission replacementAdmission =
        queue.Enqueue(81, 41, ContinuationKind::WandNew, replacement, retire);
    Expect(replacementAdmission == ContinuationAdmission::Coalesced, "retirement-consecutive-wand-replaces");
    if (replacementAdmission != ContinuationAdmission::Coalesced)
    {
        return;
    }

    Expect(retire.count == 1 && receipts[0]->retired && receipts[0]->outcome == ContinuationRetirement::Superseded &&
               receipts[0]->state == RequestState::Cancelled && lifetime.destroyed == 1 && !retire.overflowed,
           "superseded-receipt-published-before-old-request-release");
    ExpectRetirement(retire, 0, 81, 40, ContinuationKind::WandNew, ContinuationRetirement::Superseded, 0,
                     "superseded-callback-sees-live-old-payload-and-exact-metadata");
    Expect(queue.Size() == 1 && queue.Front() != nullptr && queue.Front()->documentSerial == 81 &&
               queue.Front()->ordinal == 41 && queue.Front()->kind == ContinuationKind::WandNew &&
               queue.Front()->input.value == 12 && queue.Front()->input.payload &&
               *queue.Front()->input.payload == 502 && !receipts[1]->retired,
           "coalesced-replacement-remains-pending-with-updated-metadata");
    Queue::Taken active = queue.TakeReady(81, false);
    Expect(active.ready && active.ready.value().ordinal == 41 && active.ready.value().input.receipt == receipts[1] &&
               !receipts[1]->retired && receipts[1]->state == RequestState::Pending,
           "taken-replacement-is-active-and-not-retired");
    if (active.ready && active.ready.value().input.receipt == receipts[1])
    {
        active.ready.value().input.receipt->state = RequestState::Failed;
        active.ready.reset();
        Expect(receipts[1]->state == RequestState::Failed && lifetime.destroyed == 2,
               "active-replacement-owner-publishes-failure-before-release");
    }
}

void TestRetirementRefusalAndStaleSkip()
{
    Lifetime fullLifetime;
    Queue fullQueue;
    RetirementLog fullRetire;
    fullRetire.lifetime = &fullLifetime;
    std::array<TrackedRequest, Queue::kCapacity> requests{
        Make(fullLifetime, 0, 10, 510), Make(fullLifetime, 1, 11, 511), Make(fullLifetime, 2, 12, 512),
        Make(fullLifetime, 3, 13, 513), Make(fullLifetime, 4, 14, 514), Make(fullLifetime, 5, 15, 515),
        Make(fullLifetime, 6, 16, 516), Make(fullLifetime, 7, 17, 517)};
    for (std::size_t i = 0; i < requests.size(); ++i)
    {
        const ContinuationAdmission admission = fullQueue.Enqueue(82, static_cast<std::uint64_t>(i + 1),
                                                                  ContinuationKind::Command, requests[i], fullRetire);
        Expect(admission == ContinuationAdmission::Queued, "retirement-full-case-fills");
        if (admission != ContinuationAdmission::Queued)
        {
            return;
        }
    }
    std::shared_ptr<RetirementReceipt> fullReceipt = std::make_shared<RetirementReceipt>();
    TrackedRequest full = MakeWithReceipt(fullLifetime, 8, 18, 518, fullReceipt);
    const int movesBeforeFull = fullLifetime.moves;
    const ContinuationAdmission fullAdmission = fullQueue.Enqueue(82, 9, ContinuationKind::Command, full, fullRetire);
    const bool fullRefused = fullAdmission == ContinuationAdmission::Full && full.payload && *full.payload == 518 &&
                             fullLifetime.moves == movesBeforeFull && fullRetire.count == 0 &&
                             fullReceipt->state == RequestState::Pending && full.receipt == fullReceipt;
    Expect(fullRefused, "full-refusal-retains-caller-without-retirement");
    if (!fullRefused)
    {
        return;
    }
    full.receipt->state = RequestState::Failed;
    Expect(fullReceipt->state == RequestState::Failed && full.payload && fullRetire.count == 0,
           "full-refusal-caller-can-mark-retained-request-failed");

    std::shared_ptr<RetirementReceipt> invalidReceipt = std::make_shared<RetirementReceipt>();
    TrackedRequest invalid = MakeWithReceipt(fullLifetime, 9, 19, 519, invalidReceipt);
    const int movesBeforeInvalid = fullLifetime.moves;
    const ContinuationAdmission invalidAdmission =
        fullQueue.Enqueue(82, 8, ContinuationKind::Undo, invalid, fullRetire);
    const bool invalidRefused = invalidAdmission == ContinuationAdmission::InvalidOrder && invalid.payload &&
                                *invalid.payload == 519 && fullLifetime.moves == movesBeforeInvalid &&
                                fullRetire.count == 0 && invalidReceipt->state == RequestState::Pending &&
                                invalid.receipt == invalidReceipt;
    Expect(invalidRefused, "invalid-order-retains-caller-without-retirement");
    if (!invalidRefused)
    {
        return;
    }
    invalid.receipt->state = RequestState::Failed;
    Expect(invalidReceipt->state == RequestState::Failed && invalid.payload && fullRetire.count == 0,
           "invalid-order-caller-can-mark-retained-request-failed");

    Lifetime staleLifetime;
    Queue staleQueue;
    RetirementLog staleRetire;
    staleRetire.lifetime = &staleLifetime;
    std::array<std::shared_ptr<RetirementReceipt>, 3> receipts{std::make_shared<RetirementReceipt>(),
                                                               std::make_shared<RetirementReceipt>(),
                                                               std::make_shared<RetirementReceipt>()};
    TrackedRequest staleOne = MakeWithReceipt(staleLifetime, 1, 21, 521, receipts[0]);
    const ContinuationAdmission staleOneAdmission =
        staleQueue.Enqueue(90, 1, ContinuationKind::Command, staleOne, staleRetire);
    Expect(staleOneAdmission == ContinuationAdmission::Queued, "stale-retirement-first-entry-queued");
    if (staleOneAdmission != ContinuationAdmission::Queued)
    {
        return;
    }
    TrackedRequest staleTwo = MakeWithReceipt(staleLifetime, 2, 22, 522, receipts[1]);
    const ContinuationAdmission staleTwoAdmission =
        staleQueue.Enqueue(90, 2, ContinuationKind::Import, staleTwo, staleRetire);
    Expect(staleTwoAdmission == ContinuationAdmission::Queued, "stale-retirement-second-entry-queued");
    if (staleTwoAdmission != ContinuationAdmission::Queued)
    {
        return;
    }
    TrackedRequest live = MakeWithReceipt(staleLifetime, 3, 23, 523, receipts[2]);
    const ContinuationAdmission liveAdmission = staleQueue.Enqueue(91, 3, ContinuationKind::Redo, live, staleRetire);
    Expect(liveAdmission == ContinuationAdmission::Queued, "stale-retirement-live-entry-queued");
    if (liveAdmission != ContinuationAdmission::Queued)
    {
        return;
    }

    const Queue::Taken busy = staleQueue.TakeReady(91, true, staleRetire);
    Expect(!busy.ready && busy.stale == 0 && staleRetire.count == 0 && staleLifetime.destroyed == 0 &&
               staleQueue.Size() == 3,
           "busy-take-does-not-retire-stale-or-live-entry");
    const Queue::Taken ready = staleQueue.TakeReady(91, false, staleRetire);
    Expect(ready.stale == 2 && ready.ready && ready.ready.value().ordinal == 3 && staleQueue.Size() == 0 &&
               staleRetire.count == 2 && staleLifetime.destroyed == 2 && !staleRetire.overflowed,
           "stale-skip-retires-in-order-before-returning-live-entry");
    ExpectRetirement(staleRetire, 0, 90, 1, ContinuationKind::Command, ContinuationRetirement::Stale, 0,
                     "stale-first-retirement-metadata-order-and-destruction");
    ExpectRetirement(staleRetire, 1, 90, 2, ContinuationKind::Import, ContinuationRetirement::Stale, 1,
                     "stale-second-retirement-metadata-order-and-destruction");
    Expect(receipts[0]->retired && receipts[0]->outcome == ContinuationRetirement::Stale &&
               receipts[0]->state == RequestState::Stale && receipts[1]->retired &&
               receipts[1]->outcome == ContinuationRetirement::Stale && receipts[1]->state == RequestState::Stale &&
               !receipts[2]->retired && receipts[2]->state == RequestState::Pending,
           "stale-receipts-record-outcomes-and-live-request-remains-active");
}

void TestRetirementClearWrapAndTransfer()
{
    Lifetime wrapLifetime;
    Queue wrapQueue;
    RetirementLog wrapRetire;
    wrapRetire.lifetime = &wrapLifetime;
    for (std::uint64_t ordinal = 1; ordinal <= 6; ++ordinal)
    {
        TrackedRequest request =
            Make(wrapLifetime, static_cast<int>(ordinal), static_cast<int>(ordinal), static_cast<int>(600 + ordinal));
        const ContinuationAdmission admission =
            wrapQueue.Enqueue(100, ordinal, ContinuationKind::Command, request, wrapRetire);
        Expect(admission == ContinuationAdmission::Queued, "retirement-wrap-initial-fill");
        if (admission != ContinuationAdmission::Queued)
        {
            return;
        }
    }
    for (std::uint64_t ordinal = 1; ordinal <= 5; ++ordinal)
    {
        const Queue::Taken taken = wrapQueue.TakeReady(100, false);
        const bool takenExpected = taken.ready && taken.ready.value().ordinal == ordinal;
        Expect(takenExpected, "retirement-wrap-prefix-taken");
        if (!takenExpected)
        {
            return;
        }
    }
    for (std::uint64_t ordinal = 7; ordinal <= 12; ++ordinal)
    {
        TrackedRequest request =
            Make(wrapLifetime, static_cast<int>(ordinal), static_cast<int>(ordinal), static_cast<int>(600 + ordinal));
        const ContinuationAdmission admission =
            wrapQueue.Enqueue(100, ordinal, ContinuationKind::Command, request, wrapRetire);
        Expect(admission == ContinuationAdmission::Queued, "retirement-wrap-refill");
        if (admission != ContinuationAdmission::Queued)
        {
            return;
        }
    }
    const std::size_t wrappedCleared = wrapQueue.Clear(wrapRetire);
    const bool wrappedClearExpected = wrappedCleared == 7 && wrapQueue.Size() == 0 && wrapRetire.count == 7;
    Expect(wrappedClearExpected, "clear-retires-seven-wrapped-entries");
    if (!wrappedClearExpected)
    {
        return;
    }
    constexpr std::array<std::uint64_t, 7> wrappedExpected{6, 7, 8, 9, 10, 11, 12};
    for (std::size_t i = 0; i < wrappedExpected.size(); ++i)
    {
        ExpectRetirement(wrapRetire, i, 100, wrappedExpected[i], ContinuationKind::Command,
                         ContinuationRetirement::Cancelled, static_cast<int>(5 + i),
                         "wrapped-clear-retires-literal-fifo-with-progressive-destruction");
    }
    const std::size_t retiredBeforeEmptyClear = wrapRetire.count;
    Expect(wrapQueue.Clear(wrapRetire) == 0 && wrapRetire.count == retiredBeforeEmptyClear,
           "empty-clear-does-not-call-retirement");

    Lifetime transferLifetime;
    Queue transferQueue;
    RetirementLog transferRetire;
    transferRetire.lifetime = &transferLifetime;
    transferRetire.retainOrdinal = 2;
    std::array<std::shared_ptr<RetirementReceipt>, 3> receipts{std::make_shared<RetirementReceipt>(),
                                                               std::make_shared<RetirementReceipt>(),
                                                               std::make_shared<RetirementReceipt>()};
    for (std::uint64_t ordinal = 1; ordinal <= 3; ++ordinal)
    {
        TrackedRequest request =
            MakeWithReceipt(transferLifetime, static_cast<int>(ordinal), static_cast<int>(ordinal + 30),
                            static_cast<int>(700 + ordinal), receipts[static_cast<std::size_t>(ordinal - 1)]);
        const ContinuationAdmission admission =
            transferQueue.Enqueue(101, ordinal, ContinuationKind::Command, request, transferRetire);
        Expect(admission == ContinuationAdmission::Queued, "clear-transfer-entry-queued");
        if (admission != ContinuationAdmission::Queued)
        {
            return;
        }
    }
    Queue::Taken active = transferQueue.TakeReady(101, false);
    const bool activeExpected = active.ready && active.ready.value().ordinal == 1 && !receipts[0]->retired &&
                                receipts[0]->state == RequestState::Pending &&
                                active.ready.value().input.receipt == receipts[0];
    Expect(activeExpected, "taken-active-entry-remains-pending-before-clear");
    if (!activeExpected)
    {
        return;
    }
    const std::size_t transferCleared = transferQueue.Clear(transferRetire);
    const bool transferClearExpected = transferCleared == 2 && transferRetire.count == 2 && transferQueue.Size() == 0;
    Expect(transferClearExpected, "clear-cancels-queued-entries-in-order");
    if (!transferClearExpected)
    {
        return;
    }
    ExpectRetirement(transferRetire, 0, 101, 2, ContinuationKind::Command, ContinuationRetirement::Cancelled, 0,
                     "clear-first-cancellation-can-transfer-entry-ownership");
    ExpectRetirement(transferRetire, 1, 101, 3, ContinuationKind::Command, ContinuationRetirement::Cancelled, 0,
                     "clear-second-cancellation-follows-first");
    Expect(receipts[1]->retired && receipts[1]->outcome == ContinuationRetirement::Cancelled &&
               receipts[1]->state == RequestState::Cancelled && receipts[2]->retired &&
               receipts[2]->outcome == ContinuationRetirement::Cancelled &&
               receipts[2]->state == RequestState::Cancelled && transferRetire.retained &&
               transferRetire.retained.value().ordinal == 2 && transferRetire.retained.value().input.payload &&
               *transferRetire.retained.value().input.payload == 702 && transferLifetime.destroyed == 1 &&
               !transferRetire.overflowed && !receipts[0]->retired && receipts[0]->state == RequestState::Pending,
           "clear-transfer-keeps-cancelled-payload-alive-and-active-entry-pending");
    transferRetire.retained.reset();
    Expect(transferLifetime.destroyed == 2, "transferred-entry-releases-after-callback-owner-drops-it");
    active.ready.value().input.receipt->state = RequestState::Succeeded;
    active.ready.reset();
    Expect(transferLifetime.destroyed == 3 && receipts[0]->state == RequestState::Succeeded && !receipts[0]->retired,
           "active-entry-succeeds-under-owner-control-after-queue-clear");
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
    TestSupersededRetirement();
    TestRetirementRefusalAndStaleSkip();
    TestRetirementClearWrapAndTransfer();
    return g_failures;
}
