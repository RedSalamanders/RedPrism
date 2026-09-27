#include "HistoryWorkTests.h"

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
    const HistoryWorkStamp staleDocument{stamp->owner, 13, stamp->ordinal, stamp->kind};
    Expect(!history.IsCurrent(staleDocument) && !history.Finish(staleDocument), "stale-document-stamp-refused");
    Expect(!otherOwner.IsCurrent(stamp.value()) && !otherOwner.Finish(stamp.value()), "cross-owner-stamp-refused");
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

} // namespace

int RunHistoryWorkHostTests()
{
    gFailures = 0;
    TestOrdinalsAndExclusivity();
    TestStampAndCancellation();
    TestStrokeLifecycleAndQueueOrdering();
    return gFailures;
}
