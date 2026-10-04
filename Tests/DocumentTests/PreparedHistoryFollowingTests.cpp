#include "PreparedHistoryFollowingTests.h"

#include "../Support/DocumentFixture.h"
#include "Document.h"

#include <cstdio>
#include <utility>

namespace
{
int gFailures = 0;
int gAssertions = 0;

void Expect(bool value, const char* name)
{
    ++gAssertions;
    std::printf("%s prepared-history-following.%s\n", value ? "PASS" : "FAIL", name);
    gFailures += value ? 0 : 1;
}

void DependentClosure(bool restore)
{
    DocumentFixture beforeFixture;
    beforeFixture.id = Guid::Generate();
    beforeFixture.width = 8;
    beforeFixture.height = 8;
    beforeFixture.layers.push_back(ImageLayer::Blank(L"Before", {8, 8}));
    const auto active = beforeFixture.layers.front().id;
    const CanvasDocument before = std::move(beforeFixture).Seal();
    DocumentFixture afterFixture = DocumentFixture::Copy(before);
    afterFixture.layers.front().name = L"After";
    const CanvasDocument after = std::move(afterFixture).Seal();
    DocumentHistory history;
    const auto opening = history.CaptureState();
    auto begin = DocumentHistory::PrepareBegin(opening, before, active, L"Transform Selection");
    Expect(begin.has_value(), "begin-prepared");
    if (!begin)
        return;
    auto next = DocumentHistory::FollowingPreparedState(begin.value());
    Expect(next && next->Depth() == 1 && next->Revision() == opening.Revision(), "following-owns-pending-opening");
    Expect(next && !history.IsCurrent(next.value()), "future-not-yet-current");
    if (!next)
        return;
    auto end = DocumentHistory::PrepareEnd(std::move(next.value()), restore ? before : after, active);
    Expect(end.has_value(), "closure-prepared-before-opening-publishes");
    if (!end)
        return;
    Expect(!history.CanPublish(end.value()) && !history.Publish(end.value()), "dependent-cannot-skip-predecessor");
    Expect(history.IsCurrent(opening), "early-refusal-preserves-current-history");
    Expect(history.Publish(begin.value()), "opening-publishes");
    Expect(history.CanPublish(end.value()), "exact-predecessor-enables-dependent");
    Expect(!DocumentHistory::FollowingPreparedState(begin.value()), "published-predecessor-refuses-following-reuse");
    Expect(history.Publish(end.value()), "dependent-publishes");
    Expect(history.CaptureState().Depth() == 0, "dependent-closes-transaction");
    Expect(history.UndoCount() == (restore ? 0 : 1), "restoration-no-entry-commit-one-entry");
    Expect(restore ? history.UndoName().empty() : history.UndoName() == L"Transform Selection",
           "original-history-name");
    Expect(!history.Publish(end.value()), "dependent-consumed-once");
    if (!restore)
    {
        const auto undone = history.Undo();
        Expect(undone && undone->document == std::optional(before) && undone->activeLayerID == active,
               "undo-exact-opening");
        const auto redone = history.Redo();
        Expect(redone && redone->document == std::optional(after) && redone->activeLayerID == active,
               "redo-exact-after");
    }
}

void InvalidLineage()
{
    DocumentHistory history;
    DocumentHistory foreign;
    CanvasDocument document;
    document.id = Guid::Generate();
    document.width = 8;
    document.height = 8;
    const auto state = history.CaptureState();
    auto begin = DocumentHistory::PrepareBegin(state, document, {}, L"Opening");
    if (!begin)
    {
        Expect(false, "lineage-fixture-prepared");
        return;
    }
    auto next = DocumentHistory::FollowingPreparedState(begin.value());
    auto end = next ? DocumentHistory::PrepareEnd(std::move(next.value()), document, {}) : std::nullopt;
    Expect(end.has_value(), "lineage-dependent-prepared");
    Expect(!foreign.Publish(begin.value()) && (!end || !foreign.Publish(end.value())), "foreign-owner-refuses-both");
    history.Begin(L"Other", document, {});
    history.End(document, {});
    Expect(!history.Publish(begin.value()) && (!end || !history.Publish(end.value())),
           "intervening-epoch-refuses-both");
    auto moved = std::move(begin.value());
    Expect(!DocumentHistory::FollowingPreparedState(begin.value()), "moved-from-predecessor-refuses");
    Expect(DocumentHistory::FollowingPreparedState(moved).has_value(), "owning-move-retains-dependent-preparation");
}

void SameEpochWrongPredecessor()
{
    DocumentHistory history;
    CanvasDocument document;
    document.id = Guid::Generate();
    document.width = document.height = 8;
    auto first = DocumentHistory::PrepareBegin(history.CaptureState(), document, {}, L"First");
    auto second = DocumentHistory::PrepareBegin(history.CaptureState(), document, {}, L"Second");
    if (!first || !second)
    {
        Expect(false, "same-epoch-fixtures");
        return;
    }
    auto firstFollowing = DocumentHistory::FollowingPreparedState(first.value());
    auto secondFollowing = DocumentHistory::FollowingPreparedState(second.value());
    Expect(firstFollowing && secondFollowing, "same-epoch-following-prepared");
    if (!firstFollowing || !secondFollowing)
        return;
    auto firstEnd = DocumentHistory::PrepareEnd(firstFollowing.value(), document, {});
    auto secondEnd = DocumentHistory::PrepareEnd(secondFollowing.value(), document, {});
    Expect(firstEnd && secondEnd, "same-epoch-closures-prepared");
    Expect(history.Publish(second.value()), "competing-predecessor-publishes");
    Expect(!history.IsCurrent(firstFollowing.value()), "wrong-predecessor-not-current");
    Expect(history.IsCurrent(secondFollowing.value()), "exact-predecessor-current");
    Expect(firstEnd && !history.CanPublish(firstEnd.value()) && !history.Publish(firstEnd.value()),
           "same-epoch-wrong-closure-refuses");
    Expect(secondEnd && history.Publish(secondEnd.value()), "same-epoch-exact-closure-publishes");
    Expect(history.UndoCount() == 0 && history.CaptureState().Depth() == 0, "wrong-closure-changes-nothing");
}
} // namespace

int RunPreparedHistoryFollowingTests()
{
    gFailures = gAssertions = 0;
    DependentClosure(false);
    DependentClosure(true);
    InvalidLineage();
    SameEpochWrongPredecessor();
    std::printf("PREPARED HISTORY FOLLOWING TESTS checks=%d failures=%d\n", gAssertions, gFailures);
    return gFailures;
}
