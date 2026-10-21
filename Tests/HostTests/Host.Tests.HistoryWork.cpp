#include "Host.Tests.HistoryWork.h"

#include "../../Common/ContinuationQueue.h"
#include "../../Common/HistoryWork.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <optional>

namespace
{
int gFailures = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s history-work-host.%s\n", condition ? "PASS" : "FAIL", name);
    if (!condition)
    {
        ++gFailures;
    }
}

void TestOrdinalsAndExclusivity()
{
    HistoryWork history;
    const std::optional<std::uint64_t> first = history.NextOrdinal();
    const std::optional<std::uint64_t> second = history.NextOrdinal();
    Expect(first.has_value() && first.value() == 1 && second.has_value() && second.value() == 2,
           "ordinals-increase-from-one");
    Expect(!history.Begin(0, 1, HistoryWorkKind::Commit).has_value(), "zero-document-serial-refused");
    Expect(!history.Begin(9, 3, HistoryWorkKind::Commit).has_value(), "unissued-ordinal-refused");
    if (!first.has_value() || !second.has_value())
    {
        return;
    }

    const std::optional<HistoryWorkStamp> commit = history.Begin(9, first.value(), HistoryWorkKind::Commit);
    Expect(commit.has_value() && history.Busy(), "commit-begins-and-holds-busy");
    Expect(!history.Begin(9, second.value(), HistoryWorkKind::Selection).has_value(),
           "selection-exclusive-with-commit");
    Expect(!history.Begin(9, second.value(), HistoryWorkKind::Stroke).has_value(), "stroke-exclusive-with-commit");
    bool applied = false;
    Expect(history.Busy() && !applied, "busy-remains-until-apply");
    if (commit.has_value())
    {
        applied = true; // The owner applies/drops the result before Finish releases the ordinal.
        Expect(applied && history.Finish(commit.value()), "finish-after-apply-releases-busy");
    }
    Expect(!history.Busy(), "finish-clears-busy");

    const std::optional<HistoryWorkStamp> selection = history.Begin(9, second.value(), HistoryWorkKind::Selection);
    Expect(selection.has_value(), "selection-begins-after-commit");
    Expect(!history.Begin(9, second.value(), HistoryWorkKind::Stroke).has_value(), "stroke-exclusive-with-selection");
    if (selection.has_value())
    {
        Expect(history.Finish(selection.value()), "selection-finish");
    }
    const std::optional<std::uint64_t> third = history.NextOrdinal();
    if (third.has_value())
    {
        const std::optional<HistoryWorkStamp> stroke = history.Begin(9, third.value(), HistoryWorkKind::Stroke);
        Expect(stroke.has_value(), "stroke-begins-after-selection");
        Expect(!history.Begin(9, third.value(), HistoryWorkKind::Commit).has_value(), "commit-exclusive-with-stroke");
        if (stroke.has_value())
        {
            Expect(history.Finish(stroke.value()), "stroke-finish");
        }
    }
}

void TestStampAndCancellation()
{
    HistoryWork history;
    HistoryWork otherOwner;
    const std::optional<std::uint64_t> ordinal = history.NextOrdinal();
    if (!ordinal.has_value())
    {
        Expect(false, "stamp-ordinal-issued");
        return;
    }
    const std::optional<HistoryWorkStamp> stamp = history.Begin(12, ordinal.value(), HistoryWorkKind::Commit);
    Expect(stamp.has_value(), "stamp-created");
    if (!stamp.has_value())
    {
        return;
    }
    HistoryWorkStamp staleDocument = *stamp;
    staleDocument.documentSerial = 13;
    Expect(!history.IsCurrent(staleDocument) && !history.Finish(staleDocument), "stale-document-stamp-refused");
    const auto otherOrdinal = otherOwner.NextOrdinal();
    const auto otherStamp = otherOrdinal ? otherOwner.Begin(12, *otherOrdinal, HistoryWorkKind::Commit) : std::nullopt;
    if (!otherStamp)
    {
        Expect(false, "other-owner-matching-acquisition-begins");
        return;
    }
    Expect(!otherOwner.IsCurrent(stamp.value()) && !otherOwner.Finish(stamp.value()), "cross-owner-stamp-refused");
    Expect(otherOwner.IsCurrent(*otherStamp) && otherOwner.Finish(*otherStamp),
           "cross-owner-refusal-preserves-own-work");
    const std::optional<HistoryWorkStamp> cancel = history.RequestCancel();
    Expect(cancel.has_value() && cancel.value() == stamp.value() && history.CancelRequested(stamp.value()),
           "commit-cancel-requested");
    Expect(history.Finish(stamp.value()) && !history.CancelRequested(stamp.value()), "finish-clears-cancel-request");

    const std::optional<std::uint64_t> selectionOrdinal = history.NextOrdinal();
    if (selectionOrdinal.has_value())
    {
        const std::optional<HistoryWorkStamp> selection =
            history.Begin(12, selectionOrdinal.value(), HistoryWorkKind::Selection);
        const std::optional<HistoryWorkStamp> selectionCancel = history.RequestCancel();
        Expect(selection.has_value() && selectionCancel.has_value() &&
                   selectionCancel.value().kind == HistoryWorkKind::Selection,
               "selection-cancel-requested");
        if (selection.has_value())
        {
            Expect(history.Finish(selection.value()), "selection-cancel-finish");
        }
    }

    const std::optional<std::uint64_t> strokeOrdinal = history.NextOrdinal();
    if (strokeOrdinal.has_value())
    {
        const std::optional<HistoryWorkStamp> stroke =
            history.Begin(12, strokeOrdinal.value(), HistoryWorkKind::Stroke);
        Expect(stroke.has_value() && !history.RequestCancel().has_value(), "pointer-up-stroke-not-escape-cancelled");
        if (stroke.has_value())
        {
            Expect(history.Finish(stroke.value()), "pointer-up-stroke-finishes");
        }
    }
}

void TestStrokeLifecycleAndQueueOrdering()
{
    HistoryWork history;
    const std::optional<std::uint64_t> inputOrdinal = history.NextOrdinal();
    const std::optional<std::uint64_t> commandOrdinal = history.NextOrdinal();
    if (!inputOrdinal.has_value() || !commandOrdinal.has_value())
    {
        Expect(false, "stroke-and-command-ordinals-issued");
        return;
    }

    StrokeReplay cancelled(21, inputOrdinal.value());
    Expect(cancelled.CancelInput() && cancelled.Phase() == StrokeReplayPhase::CancelledInput,
           "pointercancel-before-release-drops-input");
    StrokeReplay released(21, inputOrdinal.value());
    Expect(released.EndInput() && !released.CancelInput() && released.Phase() == StrokeReplayPhase::Pending,
           "pointercancel-after-release-does-not-drop-stroke");

    StrokeReplay pending(21, commandOrdinal.value());
    Expect(pending.EndInput(), "stroke-recording-ends-pending");
    const std::optional<HistoryWorkStamp> blocked = pending.Begin(history, 21, inputOrdinal.value());
    Expect(!blocked.has_value() && pending.Phase() == StrokeReplayPhase::Pending,
           "earlier-queued-command-prevents-stroke-start");
    const std::optional<HistoryWorkStamp> begun = pending.Begin(history, 21);
    Expect(begun.has_value() && pending.Phase() == StrokeReplayPhase::Running, "stroke-begins-after-predecessor");
    if (!begun.has_value())
    {
        return;
    }
    Expect(pending.Finish(history, begun.value(), false) && pending.Phase() == StrokeReplayPhase::Failed,
           "failed-stroke-remains-unresolved");
    Expect(!pending.Begin(history, 21).has_value() && pending.Ordinal() == commandOrdinal.value(),
           "failed-stroke-has-no-automatic-retry");
    Expect(!pending.Retry(commandOrdinal.value()), "retry-requires-fresh-ordinal");
    const std::optional<std::uint64_t> retryOrdinal = history.NextOrdinal();
    if (retryOrdinal.has_value())
    {
        Expect(pending.Retry(retryOrdinal.value()) && pending.Ordinal() == retryOrdinal.value(),
               "explicit-retry-uses-fresh-ordinal");
        const std::optional<HistoryWorkStamp> retry = pending.Begin(history, 21);
        Expect(retry.has_value(), "explicit-retry-begins");
        if (retry.has_value())
        {
            Expect(pending.Finish(history, retry.value(), true), "retry-applies");
            Expect(!pending.Finish(history, retry.value(), true), "duplicate-completion-refused");
        }
    }
}

void TestSelectedRequestContinuesBeforeYoungerInput()
{
    HistoryWork history;
    HistoryWork other;
    const auto request = history.NextOrdinal();
    const auto younger = history.NextOrdinal();
    if (!request || !younger)
    {
        Expect(false, "continuation-ordinals-issued");
        return;
    }
    const auto commit = history.Begin(31, *request, HistoryWorkKind::Commit);
    if (!commit)
    {
        Expect(false, "continuation-commit-admitted");
        return;
    }
    Expect(!history.Continue(*commit, 31, HistoryWorkKind::Import), "unfinished-prerequisite-cannot-continue");
    Expect(history.Finish(*commit), "prerequisite-applied-before-continuation");
    Expect(!history.Begin(31, *request, HistoryWorkKind::Import), "begin-still-refuses-reused-ordinal");
    Expect(!history.Continue(*commit, 32, HistoryWorkKind::Import), "replaced-document-cannot-continue");
    const auto otherOrdinal = other.NextOrdinal();
    const auto otherStamp = otherOrdinal ? other.Begin(31, *otherOrdinal, HistoryWorkKind::Commit) : std::nullopt;
    if (!otherStamp)
    {
        Expect(false, "other-owner-matching-prerequisite-begins");
        return;
    }
    Expect(other.Finish(*otherStamp), "other-owner-matching-prerequisite-finishes");
    Expect(!other.Continue(*commit, 31, HistoryWorkKind::Import), "other-owner-cannot-continue");
    const auto continuedOther = other.Continue(*otherStamp, 31, HistoryWorkKind::Import);
    Expect(continuedOther && other.Finish(*continuedOther), "other-owner-refusal-preserves-own-continuation");
    const auto imported = history.Continue(*commit, 31, HistoryWorkKind::Import);
    Expect(imported && imported->ordinal == *request && imported->acquisition != commit->acquisition,
           "import-retains-request-order-with-distinct-acquisition");
    if (!imported)
    {
        return;
    }
    Expect(!history.Begin(31, *younger, HistoryWorkKind::Stroke), "younger-stroke-waits-for-dependent-import");
    Expect(!history.IsCurrent(*commit) && !history.Finish(*commit), "late-commit-cannot-release-import");
    Expect(history.Busy() && history.IsCurrent(*imported), "late-commit-leaves-dependent-barrier-intact");
    Expect(history.Finish(*imported), "dependent-import-finishes");
    Expect(!history.Continue(*commit, 31, HistoryWorkKind::Import), "older-finished-token-cannot-reacquire");

    // Repeating the same kind as well as the ordinal must not resurrect a former completion.
    const auto repeated = history.Continue(*imported, 31, HistoryWorkKind::Import);
    Expect(repeated && *repeated != *imported, "same-kind-continuation-never-aliases-previous-stamp");
    if (!repeated)
    {
        return;
    }
    Expect(!history.Finish(*imported) && history.IsCurrent(*repeated), "late-same-kind-completion-is-refused");
    Expect(history.Finish(*repeated), "same-kind-continuation-finishes");
    const auto stroke = history.Begin(31, *younger, HistoryWorkKind::Stroke);
    Expect(stroke.has_value(), "younger-input-starts-after-whole-selected-request");
    if (!stroke)
    {
        return;
    }
    Expect(!history.Continue(*repeated, 31, HistoryWorkKind::Import), "newer-acquisition-invalidates-prior-request");
    Expect(history.Finish(*stroke), "younger-input-finishes");
    Expect(!history.Continue(*repeated, 31, HistoryWorkKind::Import), "finished-newer-input-prevents-old-continuation");

    const auto cancelOrdinal = history.NextOrdinal();
    const auto cancelWork = cancelOrdinal ? history.Begin(31, *cancelOrdinal, HistoryWorkKind::Commit) : std::nullopt;
    if (!cancelWork)
    {
        Expect(false, "continued-cancel-work-begins");
        return;
    }
    Expect(history.RequestCancel().has_value() && history.CancelRequested(*cancelWork), "first-acquisition-cancelled");
    Expect(history.Finish(*cancelWork), "cancelled-acquisition-dropped");
    const auto next = history.Continue(*cancelWork, 31, HistoryWorkKind::Commit);
    Expect(next && !history.CancelRequested(*next), "acquisition-cancellation-does-not-leak-to-next-stage");
    if (next)
    {
        Expect(history.Finish(*next), "next-stage-after-drop-finishes");
    }
}

void TestValidatedReplacementContinuation()
{
    HistoryWork history;
    const auto ordinal = history.NextOrdinal();
    const auto first = ordinal ? history.Begin(40, ordinal.value(), HistoryWorkKind::Commit) : std::nullopt;
    if (!first)
    {
        Expect(false, "replacement-first-acquisition");
        return;
    }
    auto wrong = first.value();
    ++wrong.acquisition;
    Expect(!history.FinishReplacement(wrong, 41) && history.IsCurrent(first.value()),
           "replacement-wrong-acquisition-retains-barrier");
    Expect(!history.FinishReplacement(first.value(), 0) && history.IsCurrent(first.value()),
           "replacement-zero-lineage-retains-barrier");
    Expect(!history.FinishReplacement(first.value(), 40) && history.IsCurrent(first.value()),
           "replacement-same-lineage-retains-barrier");
    Expect(history.FinishReplacement(first.value(), 41), "replacement-valid-owner-finishes");
    Expect(!history.Continue(first.value(), 40, HistoryWorkKind::Commit), "replacement-old-lineage-refused");
    Expect(!history.Continue(first.value(), 42, HistoryWorkKind::Commit), "replacement-external-lineage-refused");
    Expect(!history.Continue(wrong, 41, HistoryWorkKind::Commit), "replacement-exact-finished-stamp-required");
    const auto second = history.Continue(first.value(), 41, HistoryWorkKind::Commit);
    Expect(second && second.value().ordinal == ordinal.value() &&
               second.value().acquisition != first.value().acquisition,
           "replacement-continues-same-input-with-new-acquisition");
    if (!second)
        return;
    Expect(!history.FinishReplacement(first.value(), 42) && history.IsCurrent(second.value()),
           "replacement-late-first-completion-cannot-release-second");
    Expect(history.Finish(second.value()), "replacement-second-finishes");
    Expect(!history.Continue(second.value(), 42, HistoryWorkKind::Commit),
           "replacement-authorization-does-not-leak-to-later-stage");
    const auto third = history.Continue(second.value(), 41, HistoryWorkKind::Commit);
    Expect(third && history.Finish(third.value()), "replacement-ordinary-stage-retains-current-lineage");
}
} // namespace

int RunHistoryWorkHostTests()
{
    gFailures = 0;
    TestOrdinalsAndExclusivity();
    TestStampAndCancellation();
    TestStrokeLifecycleAndQueueOrdering();
    TestSelectedRequestContinuesBeforeYoungerInput();
    TestValidatedReplacementContinuation();
    return gFailures;
}
