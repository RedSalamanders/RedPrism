#include "../../Common/ContinuationReceipt.h"

#include "Host.Tests.ContinuationReceipt.h"

#include "../../Common/ContinuationQueue.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <type_traits>
#include <utility>

namespace
{
int g_failures = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s continuation-receipt.%s\n", condition ? "PASS" : "FAIL", name);
    if (!condition)
    {
        ++g_failures;
    }
}

using Receipt = ContinuationReceipt;
using Outcome = ContinuationOutcome;

static_assert(std::is_nothrow_copy_constructible_v<Receipt>);
static_assert(std::is_nothrow_copy_assignable_v<Receipt>);
static_assert(std::is_nothrow_move_constructible_v<Receipt>);
static_assert(std::is_nothrow_move_assignable_v<Receipt>);
static_assert(std::is_nothrow_destructible_v<ContinuationCompletion>);
static_assert(std::is_nothrow_move_constructible_v<ContinuationCompletion>);
static_assert(std::is_nothrow_move_assignable_v<ContinuationCompletion>);
static_assert(!std::is_copy_constructible_v<ContinuationCompletion>);
static_assert(!std::is_copy_assignable_v<ContinuationCompletion>);

void TestOutcomesAndFirstTerminalWins()
{
    constexpr std::array<Outcome, 4> terminalOutcomes{Outcome::Succeeded, Outcome::Cancelled, Outcome::Failed,
                                                      Outcome::Stale};
    constexpr std::array<std::int32_t, 4> errors{0, -7, -2147024882, 2000000037};
    for (std::size_t i = 0; i < terminalOutcomes.size(); ++i)
    {
        ContinuationCompletion owner = ContinuationCompletion::Create();
        const Receipt observer = owner.Receipt();
        const Receipt::Result before = observer.Read();
        Expect(before.outcome == Outcome::Pending && before.error == 0, "new-receipt-starts-pending");
        Expect(before == Receipt::Result{Outcome::Pending, 0}, "result-value-equality");
        Expect(!owner.Publish(Outcome::Pending, 91), "pending-cannot-finalize");
        constexpr Outcome invalidOutcome = static_cast<Outcome>(99);
        Expect(!owner.Publish(invalidOutcome, 92), "invalid-enum-cannot-finalize");
        const Receipt::Result stillPending = observer.Read();
        Expect(stillPending.outcome == Outcome::Pending && stillPending.error == 0,
               "invalid-enum-leaves-receipt-pending");
        Expect(owner.Publish(terminalOutcomes[i], errors[i]), "first-terminal-publication-accepted");
        const Receipt::Result after = observer.Read();
        Expect(after.outcome == terminalOutcomes[i] && after.error == errors[i],
               "terminal-outcome-and-error-preserved");
        Expect(!owner.Publish(Outcome::Succeeded, 1234), "later-terminal-publication-rejected");
        const Receipt::Result unchanged = observer.Read();
        Expect(unchanged.outcome == terminalOutcomes[i] && unchanged.error == errors[i],
               "first-terminal-result-remains-unchanged");
    }

    ContinuationCompletion empty;
    Expect(!empty.Publish(Outcome::Succeeded, 4), "empty-publisher-refuses-publication");
    const Receipt::Result emptyResult = empty.Receipt().Read();
    Expect(emptyResult.outcome == Outcome::Failed && emptyResult.error == 0, "empty-publisher-returns-failed-receipt");

    Receipt completedBeforeDestruction;
    {
        ContinuationCompletion completed = ContinuationCompletion::Create();
        completedBeforeDestruction = completed.Receipt();
        Expect(completed.Publish(Outcome::Failed, -29), "first-terminal-before-destruction-accepted");
    }
    const Receipt::Result completedResult = completedBeforeDestruction.Read();
    Expect(completedResult.outcome == Outcome::Failed && completedResult.error == -29,
           "first-terminal-result-survives-owner-destruction");
}

void TestReceiptOwnershipAndMoves()
{
    Receipt retained;
    Receipt copied;
    {
        ContinuationCompletion owner = ContinuationCompletion::Create();
        retained = owner.Receipt();
        copied = retained;
        Receipt moved = std::move(copied);
        copied = std::move(moved);
        Expect(owner.Publish(Outcome::Succeeded, 18), "receipt-copy-and-move-observe-publication");
    }
    const Receipt::Result afterOwner = retained.Read();
    const Receipt::Result afterMove = copied.Read();
    Expect(afterOwner.outcome == Outcome::Succeeded && afterOwner.error == 18 &&
               afterMove.outcome == Outcome::Succeeded && afterMove.error == 18,
           "copied-and-moved-observers-survive-publisher");

    ContinuationCompletion source = ContinuationCompletion::Create();
    const Receipt movedSourceReceipt = source.Receipt();
    ContinuationCompletion destination = std::move(source);
    Expect(!source.Publish(Outcome::Succeeded, 31), "moved-from-publisher-cannot-publish");
    Expect(destination.Publish(Outcome::Stale, -32), "move-construction-transfers-publication-owner");
    const Receipt::Result transferred = movedSourceReceipt.Read();
    Expect(transferred.outcome == Outcome::Stale && transferred.error == -32,
           "moved-owner-publishes-to-original-observer");

    ContinuationCompletion survivingOwner;
    Receipt survivingObserver;
    {
        ContinuationCompletion shortLived = ContinuationCompletion::Create();
        survivingObserver = shortLived.Receipt();
        survivingOwner = std::move(shortLived);
    }
    Expect(survivingObserver.Read().outcome == Outcome::Pending,
           "moved-from-destruction-cannot-cancel-the-transferred-owner");
    Expect(survivingOwner.Publish(Outcome::Succeeded) && survivingObserver.Read().outcome == Outcome::Succeeded,
           "transferred-owner-completes-after-source-destruction");

    ContinuationCompletion replaced = ContinuationCompletion::Create();
    const Receipt replacedObserver = replaced.Receipt();
    ContinuationCompletion incoming = ContinuationCompletion::Create();
    const Receipt incomingObserver = incoming.Receipt();
    replaced = std::move(incoming);
    const Receipt::Result replacedResult = replacedObserver.Read();
    Expect(replacedResult.outcome == Outcome::Cancelled && replacedResult.error == 0,
           "move-assignment-cancels-replaced-pending-state");
    Expect(!incoming.Publish(Outcome::Succeeded, 33), "move-assignment-leaves-source-empty");
    Expect(replaced.Publish(Outcome::Failed, -34), "move-assignment-preserves-incoming-owner");
    const Receipt::Result incomingResult = incomingObserver.Read();
    Expect(incomingResult.outcome == Outcome::Failed && incomingResult.error == -34,
           "move-assignment-incoming-observer-remains-connected");

    ContinuationCompletion self = ContinuationCompletion::Create();
    const Receipt selfObserver = self.Receipt();
    self = std::move(self);
    Expect(selfObserver.Read().outcome == Outcome::Pending, "self-move-assignment-does-not-cancel");
    Expect(self.Publish(Outcome::Succeeded, 35), "self-moved-publisher-remains-usable");

    const Receipt empty;
    const Receipt::Result emptyResult = empty.Read();
    Expect(emptyResult.outcome == Outcome::Failed && emptyResult.error == 0, "default-receipt-is-failed-zero");
    const Receipt failure = Receipt::Failure(-2147024882);
    Receipt failureCopy = failure;
    Receipt failureMove = std::move(failureCopy);
    const Receipt::Result failureResult = failureMove.Read();
    Expect(failureResult.outcome == Outcome::Failed && failureResult.error == -2147024882,
           "immediate-allocation-refusal-failure-is-owning-free-and-copyable");

    ContinuationCompletion abandoned = ContinuationCompletion::Create();
    const Receipt abandonedObserver = abandoned.Receipt();
    abandoned = ContinuationCompletion{};
    const Receipt::Result abandonedResult = abandonedObserver.Read();
    Expect(abandonedResult.outcome == Outcome::Cancelled && abandonedResult.error == 0,
           "overwriting-pending-owner-publishes-cancelled");

    Receipt lateObserver;
    {
        ContinuationCompletion pending = ContinuationCompletion::Create();
        lateObserver = pending.Receipt();
    }
    const Receipt::Result lateResult = lateObserver.Read();
    Expect(lateResult.outcome == Outcome::Cancelled && lateResult.error == 0,
           "owner-destruction-cancels-for-late-copied-observer");
}

struct QueueRequest final
{
    ContinuationCompletion completion;
    int argument = 0;

    QueueRequest() noexcept = default;
    QueueRequest(ContinuationCompletion&& publisher, int value) noexcept
        : completion(std::move(publisher)), argument(value)
    {
    }
    QueueRequest(QueueRequest&&) noexcept = default;
    QueueRequest& operator=(QueueRequest&&) noexcept = default;
    QueueRequest(const QueueRequest&) = delete;
    QueueRequest& operator=(const QueueRequest&) = delete;
};

using Queue = ContinuationQueue<QueueRequest>;

struct RetirementTrace final
{
    std::array<std::uint64_t, Queue::kCapacity> ordinals{};
    std::array<Outcome, Queue::kCapacity> outcomes{};
    std::size_t count = 0;
    bool overflow = false;

    void operator()(Queue::Entry&& entry, ContinuationRetirement retirement) noexcept
    {
        if (count < ordinals.size())
        {
            ordinals[count] = entry.ordinal;
            outcomes[count] = retirement == ContinuationRetirement::Stale ? Outcome::Stale : Outcome::Cancelled;
            ++count;
        }
        else
        {
            overflow = true;
        }
        const Outcome result = retirement == ContinuationRetirement::Stale ? Outcome::Stale : Outcome::Cancelled;
        (void)entry.input.completion.Publish(result);
    }
};

void TestQueueReceiptLifecycle()
{
    Queue fullQueue;
    RetirementTrace fullTrace;
    std::array<QueueRequest, Queue::kCapacity> queued;
    std::array<Receipt, Queue::kCapacity> observers;
    for (std::size_t i = 0; i < queued.size(); ++i)
    {
        ContinuationCompletion publisher = ContinuationCompletion::Create();
        observers[i] = publisher.Receipt();
        queued[i] = QueueRequest{std::move(publisher), static_cast<int>(i + 10)};
        const ContinuationAdmission admission =
            fullQueue.Enqueue(44, static_cast<std::uint64_t>(i + 1), ContinuationKind::Command, queued[i], fullTrace);
        Expect(admission == ContinuationAdmission::Queued && observers[i].Read().outcome == Outcome::Pending,
               "queue-fixture-admits-pending-request");
        if (admission != ContinuationAdmission::Queued)
        {
            return;
        }
    }

    ContinuationCompletion refusedPublisher = ContinuationCompletion::Create();
    const Receipt refusedObserver = refusedPublisher.Receipt();
    QueueRequest refused{std::move(refusedPublisher), 99};
    const ContinuationAdmission full = fullQueue.Enqueue(44, 9, ContinuationKind::Command, refused, fullTrace);
    const Receipt::Result stillOwned = refusedObserver.Read();
    Expect(full == ContinuationAdmission::Full && refused.argument == 99 && stillOwned.outcome == Outcome::Pending &&
               refused.completion.Publish(Outcome::Failed, -40),
           "full-refusal-retains-request-and-observer-until-explicit-failure");
    const Receipt::Result refusedFailed = refusedObserver.Read();
    Expect(refusedFailed.outcome == Outcome::Failed && refusedFailed.error == -40,
           "full-refusal-explicit-failure-readable");

    ContinuationCompletion invalidPublisher = ContinuationCompletion::Create();
    const Receipt invalidObserver = invalidPublisher.Receipt();
    QueueRequest invalid{std::move(invalidPublisher), 100};
    const ContinuationAdmission invalidResult = fullQueue.Enqueue(44, 8, ContinuationKind::Undo, invalid, fullTrace);
    const Receipt::Result invalidPending = invalidObserver.Read();
    Expect(invalidResult == ContinuationAdmission::InvalidOrder && invalid.argument == 100 &&
               invalidPending.outcome == Outcome::Pending && invalid.completion.Publish(Outcome::Failed, -41),
           "invalid-refusal-retains-request-and-observer-until-explicit-failure");
    const Receipt::Result invalidFailed = invalidObserver.Read();
    Expect(invalidFailed.outcome == Outcome::Failed && invalidFailed.error == -41,
           "invalid-refusal-explicit-failure-readable");
    Expect(fullTrace.count == 0 && !fullTrace.overflow, "refused-admissions-never-retire-an-accepted-or-refused-input");

    Queue staleQueue;
    ContinuationCompletion stalePublisher = ContinuationCompletion::Create();
    const Receipt staleObserver = stalePublisher.Receipt();
    QueueRequest stale{std::move(stalePublisher), 7};
    Expect(staleQueue.Enqueue(50, 1, ContinuationKind::Command, stale, fullTrace) == ContinuationAdmission::Queued,
           "stale-request-queued");
    const Queue::Taken staleTake = staleQueue.TakeReady(51, false, fullTrace);
    const Receipt::Result staleResult = staleObserver.Read();
    Expect(!staleTake.ready && staleTake.stale == 1 && staleResult.outcome == Outcome::Stale,
           "stale-retirement-publishes-stale");

    Queue supersedeQueue;
    ContinuationCompletion supersededPublisher = ContinuationCompletion::Create();
    const Receipt supersededObserver = supersededPublisher.Receipt();
    QueueRequest superseded{std::move(supersededPublisher), 1};
    Expect(supersedeQueue.Enqueue(60, 1, ContinuationKind::WandNew, superseded, fullTrace) ==
               ContinuationAdmission::Queued,
           "superseded-request-queued");
    ContinuationCompletion replacementPublisher = ContinuationCompletion::Create();
    const Receipt replacementObserver = replacementPublisher.Receipt();
    QueueRequest replacement{std::move(replacementPublisher), 2};
    Expect(supersedeQueue.Enqueue(60, 2, ContinuationKind::WandNew, replacement, fullTrace) ==
               ContinuationAdmission::Coalesced,
           "successive-wand-request-coalesces");
    Expect(supersededObserver.Read().outcome == Outcome::Cancelled &&
               replacementObserver.Read().outcome == Outcome::Pending,
           "superseded-callback-cancels-only-prior-observer");
    Expect(fullTrace.count == 2 && fullTrace.ordinals[0] == 1 && fullTrace.outcomes[0] == Outcome::Stale &&
               fullTrace.ordinals[1] == 1 && fullTrace.outcomes[1] == Outcome::Cancelled && !fullTrace.overflow,
           "stale-and-superseded-callbacks-have-literal-outcomes");

    Queue clearQueue;
    RetirementTrace clearTrace;
    std::array<Receipt, 3> clearObservers;
    std::array<QueueRequest, 3> clearRequests;
    for (std::size_t i = 0; i < clearRequests.size(); ++i)
    {
        ContinuationCompletion publisher = ContinuationCompletion::Create();
        clearObservers[i] = publisher.Receipt();
        clearRequests[i] = QueueRequest{std::move(publisher), static_cast<int>(i + 1)};
        const ContinuationAdmission admission = clearQueue.Enqueue(
            70, static_cast<std::uint64_t>(i + 1), ContinuationKind::Command, clearRequests[i], clearTrace);
        Expect(admission == ContinuationAdmission::Queued, "clear-fixture-request-queued");
        if (admission != ContinuationAdmission::Queued)
        {
            return;
        }
    }
    Queue::Taken active = clearQueue.TakeReady(70, false, clearTrace);
    const bool activePending = active.ready.has_value() && active.ready.value().input.argument == 1 &&
                               clearObservers[0].Read().outcome == Outcome::Pending;
    Expect(activePending, "taken-active-input-remains-pending");
    if (!activePending)
    {
        return;
    }
    Expect(clearQueue.Clear(clearTrace) == 2 && clearTrace.count == 2 && clearTrace.ordinals[0] == 2 &&
               clearTrace.ordinals[1] == 3 && clearTrace.outcomes[0] == Outcome::Cancelled &&
               clearTrace.outcomes[1] == Outcome::Cancelled && clearObservers[1].Read().outcome == Outcome::Cancelled &&
               clearObservers[2].Read().outcome == Outcome::Cancelled &&
               clearObservers[0].Read().outcome == Outcome::Pending && !clearTrace.overflow,
           "clear-cancels-queued-observers-in-fifo-and-keeps-active-pending");
    Expect(active.ready.value().input.completion.Publish(Outcome::Succeeded, 42),
           "active-request-completes-successfully");
    const Receipt::Result activeResult = clearObservers[0].Read();
    Expect(activeResult.outcome == Outcome::Succeeded && activeResult.error == 42,
           "active-observer-reads-success-after-clear");
    active.ready.reset();

    Receipt teardownObserver;
    {
        Queue teardownQueue;
        ContinuationCompletion publisher = ContinuationCompletion::Create();
        teardownObserver = publisher.Receipt();
        QueueRequest teardownRequest{std::move(publisher), 5};
        Expect(teardownQueue.Enqueue(80, 1, ContinuationKind::Command, teardownRequest) ==
                   ContinuationAdmission::Queued,
               "teardown-fallback-request-queued");
    }
    const Receipt::Result teardownResult = teardownObserver.Read();
    Expect(teardownResult.outcome == Outcome::Cancelled && teardownResult.error == 0,
           "queue-teardown-owner-destructor-cancels-observer");
}
} // namespace

int RunContinuationReceiptTests()
{
    g_failures = 0;
    TestOutcomesAndFirstTerminalWins();
    TestReceiptOwnershipAndMoves();
    TestQueueReceiptLifecycle();
    return g_failures;
}
