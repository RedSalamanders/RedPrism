#include "Host.Tests.OpacityInputOrder.h"

#include "../../Common/OpacityInputOrder.h"

#include <cstdint>
#include <cstdio>
#include <limits>
#include <optional>
#include <type_traits>
#include <utility>

// These tests cover producer input ordering only, not model edit opening, document state, or history.
namespace
{
using Order = OpacityInputOrder;
using Phase = Order::Phase;
using Stamp = Order::Stamp;
using Counters = Order::Counters;
using Proposal = Order::Proposal;

static_assert(std::is_nothrow_move_constructible_v<Proposal>);
static_assert(!std::is_copy_constructible_v<Proposal>);
static_assert(!std::is_copy_assignable_v<Proposal>);
static_assert(!std::is_move_assignable_v<Proposal>);
static_assert(!std::is_copy_constructible_v<Order>);
static_assert(!std::is_move_constructible_v<Order>);
static_assert(noexcept(Order()));
static_assert(noexcept(Order(Counters{})));
static_assert(noexcept(std::declval<const Order&>().Read()));
static_assert(noexcept(std::declval<const Order&>().Propose(Phase::Preview, false)));
static_assert(noexcept(std::declval<Order&>().Accept(std::declval<Proposal&>())));
static_assert(noexcept(std::declval<Order&>().Barrier()));
static_assert(noexcept(std::declval<const Proposal&>().Identity()));

void Expect(bool condition, const char* name, int& failures) noexcept
{
    std::printf("%s opacity-input-order.%s\n", condition ? "PASS" : "FAIL", name);
    std::fflush(stdout);
    failures += condition ? 0 : 1;
}

void TestDefaultPreviewAndDrop(int& failures) noexcept
{
    Order order;

    auto dropped = order.Propose(Phase::Preview, false);
    dropped.reset();
    Expect(order.Read() == Counters{0, 0, 0, 0}, "dropped-proposal-retains-counters", failures);

    auto first = order.Propose(Phase::Preview, false);
    Expect(first.has_value() && order.Accept(first.value()), "initial-preview-accepted", failures);
    Expect(order.Read() == Counters{1, 1, 1, 1}, "initial-preview-literal-counters", failures);
    Expect(!order.Accept(first.value()), "accepted-proposal-cannot-be-reaccepted", failures);
}

void TestContinuationAndTerminal(int& failures) noexcept
{
    Order order;
    auto preview = order.Propose(Phase::Preview, false);
    Expect(preview.has_value() && order.Accept(preview.value()), "stream-preview-accepted", failures);

    auto continuation = order.Propose(Phase::Preview, true);
    Expect(continuation.has_value() && continuation->Identity() == Stamp{1, 2},
           "continuation-reuses-stream-and-advances-sequence", failures);
    Expect(continuation.has_value() && order.Accept(continuation.value()), "continuation-accepted", failures);
    Expect(order.Read() == Counters{1, 2, 2, 1}, "continuation-literal-counters", failures);

    auto commit = order.Propose(Phase::Commit, true);
    Expect(commit.has_value() && commit->Identity() == Stamp{1, 3}, "commit-stamps-open-stream", failures);
    Expect(commit.has_value() && order.Accept(commit.value()), "commit-accepted", failures);
    Expect(order.Read() == Counters{1, 3, 3, 0}, "commit-closes-stream-literal-counters", failures);

    auto fresh = order.Propose(Phase::Preview, true);
    Expect(fresh.has_value() && fresh->Identity() == Stamp{2, 4}, "preview-after-commit-mints-new-stream", failures);
    Expect(fresh.has_value() && order.Accept(fresh.value()), "preview-after-commit-accepted", failures);
}

void TestCancelAndNonStreamTerminal(int& failures) noexcept
{
    Order order;
    auto preview = order.Propose(Phase::Preview, false);
    Expect(preview.has_value() && order.Accept(preview.value()), "cancel-setup-preview-accepted", failures);
    auto cancel = order.Propose(Phase::Cancel, true);
    Expect(cancel.has_value() && order.Accept(cancel.value()), "cancel-accepted", failures);
    Expect(order.Read() == Counters{1, 2, 2, 0}, "cancel-closes-stream-literal-counters", failures);

    auto afterCancel = order.Propose(Phase::Preview, false);
    Expect(afterCancel.has_value() && afterCancel->Identity() == Stamp{2, 3}, "preview-after-cancel-mints-new-stream",
           failures);
    Expect(afterCancel.has_value() && order.Accept(afterCancel.value()), "preview-after-cancel-accepted", failures);

    Order withoutStream;
    auto commit = withoutStream.Propose(Phase::Commit, false);
    Expect(commit.has_value() && withoutStream.Accept(commit.value()), "commit-without-stream-accepted", failures);
    auto cancelWithoutStream = withoutStream.Propose(Phase::Cancel, false);
    Expect(cancelWithoutStream.has_value() && cancelWithoutStream->Identity() == Stamp{0, 2},
           "cancel-without-stream-uses-zero", failures);
    Expect(cancelWithoutStream.has_value() && withoutStream.Accept(cancelWithoutStream.value()),
           "cancel-without-stream-accepted", failures);
    auto laterPreview = withoutStream.Propose(Phase::Preview, false);
    Expect(laterPreview.has_value() && laterPreview->Identity() == Stamp{1, 3},
           "preview-after-zero-stream-terminals-mints-one", failures);
}

void TestFreshPreviewAndBarrier(int& failures) noexcept
{
    Order order;
    auto first = order.Propose(Phase::Preview, false);
    Expect(first.has_value() && order.Accept(first.value()), "replacement-setup-preview-accepted", failures);
    auto replacement = order.Propose(Phase::Preview, false);
    Expect(replacement.has_value() && replacement->Identity() == Stamp{2, 2}, "noncontinuing-preview-mints-new-stream",
           failures);
    Expect(replacement.has_value() && order.Accept(replacement.value()), "replacement-preview-accepted", failures);

    auto pending = order.Propose(Phase::Preview, true);
    Expect(order.Barrier(), "barrier-succeeds", failures);
    Expect(order.Read() == Counters{2, 2, 3, 0}, "barrier-closes-stream-and-advances-epoch", failures);
    Expect(pending.has_value() && !order.Accept(pending.value()), "barrier-expires-outstanding-proposal", failures);

    auto afterBarrier = order.Propose(Phase::Preview, true);
    Expect(afterBarrier.has_value() && afterBarrier->Identity() == Stamp{3, 3}, "preview-after-barrier-mints-stream",
           failures);
    Expect(afterBarrier.has_value() && order.Accept(afterBarrier.value()), "preview-after-barrier-accepted", failures);
}

void TestProposalInvalidationAndOwnership(int& failures) noexcept
{
    Order order;
    auto old = order.Propose(Phase::Preview, false);
    auto newer = order.Propose(Phase::Preview, false);
    Expect(newer.has_value() && order.Accept(newer.value()), "one-parallel-proposal-accepted", failures);
    Expect(old.has_value() && !order.Accept(old.value()), "other-proposal-stale-after-accept", failures);

    Order other;
    auto foreign = other.Propose(Phase::Preview, false);
    Expect(foreign.has_value() && !order.Accept(foreign.value()), "cross-controller-proposal-refused", failures);
    Expect(other.Read() == Counters{0, 0, 0, 0}, "wrong-controller-preserves-proposal-owner", failures);
    Expect(foreign.has_value() && other.Accept(foreign.value()), "wrong-controller-does-not-consume-proposal",
           failures);

    Order movedOrder;
    auto source = movedOrder.Propose(Phase::Preview, false);
    if (source.has_value())
    {
        Proposal moved(std::move(source.value()));
        Expect(!movedOrder.Accept(source.value()), "moved-from-proposal-refused", failures);
        Expect(movedOrder.Accept(moved), "moved-to-proposal-accepted", failures);
        Expect(movedOrder.Read() == Counters{1, 1, 1, 1}, "moved-to-proposal-preserves-identity", failures);
    }
}

void TestRefusedAdmissionAndEmptyBarrier(int& failures) noexcept
{
    Order order;
    auto first = order.Propose(Phase::Preview, false);
    if (!first || !order.Accept(first.value()))
    {
        Expect(false, "refused-admission-setup", failures);
        return;
    }
    auto deniedTerminal = order.Propose(Phase::Commit, true);
    deniedTerminal.reset();
    Expect(order.Read() == Counters{1, 1, 1, 1}, "refused-terminal-keeps-open-stream", failures);
    auto next = order.Propose(Phase::Preview, true);
    Expect(next && next.value().Identity() == Stamp{1, 2}, "refused-terminal-consumes-no-sequence", failures);
    Order empty;
    auto pending = empty.Propose(Phase::Preview, false);
    Expect(empty.Barrier() && empty.Read() == Counters{0, 0, 1, 0}, "empty-barrier-literal-counters", failures);
    Expect(pending && !empty.Accept(pending.value()), "empty-barrier-expires-preview", failures);
}

void TestInvalidPhaseAndSeeds(int& failures) noexcept
{
    Order order;
    const auto invalidPhase = static_cast<Phase>(255);
    Expect(!order.Propose(invalidPhase, false).has_value(), "out-of-range-phase-refused", failures);

    Order invalidSeed(Counters{2, 0, 0, 3});
    Expect(!invalidSeed.Propose(Phase::Preview, true).has_value(), "current-stream-above-last-refuses-proposal",
           failures);
    Expect(!invalidSeed.Barrier(), "invalid-seed-barrier-refused", failures);
    Expect(invalidSeed.Read() == Counters{2, 0, 0, 3}, "invalid-seed-failures-preserve-counters", failures);
}

void TestSequenceAndEpochExhaustion(int& failures) noexcept
{
    constexpr std::uint64_t maximum = (std::numeric_limits<std::uint64_t>::max)();
    Order exhaustedSequence(Counters{8, maximum, 4, 8});
    Expect(!exhaustedSequence.Propose(Phase::Preview, true).has_value(), "maximum-sequence-refuses-proposal", failures);

    Order lastSequence(Counters{8, maximum - 1, 4, 8});
    auto finalSequence = lastSequence.Propose(Phase::Preview, true);
    Expect(finalSequence.has_value() && finalSequence->Identity() == Stamp{8, maximum},
           "one-before-maximum-issues-literal-maximum", failures);
    Expect(finalSequence.has_value() && lastSequence.Accept(finalSequence.value()), "maximum-sequence-accepted",
           failures);
    Expect(lastSequence.Read() == Counters{8, maximum, 5, 8}, "maximum-sequence-counter-values", failures);
    Expect(!lastSequence.Propose(Phase::Commit, true).has_value(), "maximum-sequence-prevents-terminal-proposal",
           failures);

    Order maximumEpoch(Counters{4, 9, maximum, 4});
    Expect(!maximumEpoch.Propose(Phase::Preview, true), "maximum-epoch-refuses-preview", failures);
    Expect(!maximumEpoch.Propose(Phase::Cancel, true), "maximum-epoch-refuses-cancel", failures);
    Expect(!maximumEpoch.Barrier(), "maximum-epoch-barrier-refused", failures);
    Expect(maximumEpoch.Read() == Counters{4, 9, maximum, 4}, "maximum-epoch-failures-preserve-counters", failures);

    Order lastEpoch(Counters{4, 9, maximum - 1, 4});
    auto lastEpochProposal = lastEpoch.Propose(Phase::Preview, true);
    Expect(lastEpochProposal.has_value() && lastEpoch.Accept(lastEpochProposal.value()),
           "one-before-maximum-epoch-accepts", failures);
    Expect(lastEpoch.Read() == Counters{4, 10, maximum, 4}, "accept-reaches-literal-maximum-epoch", failures);
    Expect(!lastEpoch.Barrier(), "barrier-at-maximum-epoch-refused", failures);
    Expect(lastEpoch.Read() == Counters{4, 10, maximum, 4}, "failed-maximum-epoch-barrier-preserves-counters",
           failures);
}

void TestStreamExhaustion(int& failures) noexcept
{
    constexpr std::uint64_t maximum = (std::numeric_limits<std::uint64_t>::max)();
    Order lastAvailableStream(Counters{maximum - 1, 0, 0, 0});
    auto finalStream = lastAvailableStream.Propose(Phase::Preview, false);
    Expect(finalStream.has_value() && finalStream->Identity() == Stamp{maximum, 1},
           "one-before-maximum-stream-mints-literal-maximum", failures);
    Expect(finalStream.has_value() && lastAvailableStream.Accept(finalStream.value()),
           "maximum-stream-from-final-available-id-accepted", failures);
    Expect(lastAvailableStream.Read() == Counters{maximum, 1, 1, maximum}, "final-stream-id-counter-values", failures);
    Order order(Counters{maximum, 0, 0, maximum});
    auto continuingPreview = order.Propose(Phase::Preview, true);
    Expect(continuingPreview.has_value() && continuingPreview->Identity() == Stamp{maximum, 1},
           "maximum-last-stream-allows-continuing-preview", failures);
    Expect(continuingPreview.has_value() && order.Accept(continuingPreview.value()),
           "maximum-stream-continuation-accepted", failures);

    auto terminal = order.Propose(Phase::Commit, true);
    Expect(terminal.has_value() && terminal->Identity() == Stamp{maximum, 2}, "maximum-last-stream-allows-terminal",
           failures);
    Expect(terminal.has_value() && order.Accept(terminal.value()), "maximum-stream-terminal-accepted", failures);
    Expect(!order.Propose(Phase::Preview, false).has_value(), "maximum-last-stream-refuses-fresh-preview", failures);
    Expect(order.Read() == Counters{maximum, 2, 2, 0}, "fresh-preview-refusal-preserves-maximum-counters", failures);
}
} // namespace

int RunOpacityInputOrderTests()
{
    int failures = 0;
    TestDefaultPreviewAndDrop(failures);
    TestContinuationAndTerminal(failures);
    TestCancelAndNonStreamTerminal(failures);
    TestFreshPreviewAndBarrier(failures);
    TestProposalInvalidationAndOwnership(failures);
    TestRefusedAdmissionAndEmptyBarrier(failures);
    TestInvalidPhaseAndSeeds(failures);
    TestSequenceAndEpochExhaustion(failures);
    TestStreamExhaustion(failures);
    return failures;
}
