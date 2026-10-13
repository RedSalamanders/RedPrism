#include "Document.Tests.BoundedLassoPoints.h"

#include "BoundedLassoPoints.h"
#include "DeferredRelease.h"
#include "SharedPublicationMemory.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{
int gAssertions = 0;
int gFailures = 0;

void Expect(bool condition, const char* label) noexcept
{
    ++gAssertions;
    gFailures += condition ? 0 : 1;
    std::printf("%s bounded-lasso-points.%s\n", condition ? "PASS" : "FAIL", label);
}

Vec2 ReferencePoint(std::uint32_t index) noexcept
{
    return {static_cast<double>(index) * 0.25 - 17.0, static_cast<double>(static_cast<int>(index % 11u) - 5) * 1.5};
}

std::optional<SelectionBounds> ReferenceBounds(std::span<const Vec2> points) noexcept
{
    if (points.empty())
        return std::nullopt;
    SelectionBounds result{points.front().x, points.front().y, points.front().x, points.front().y};
    for (const Vec2 point : points.subspan(1))
    {
        if (point.x < result.minX)
            result.minX = point.x;
        if (point.y < result.minY)
            result.minY = point.y;
        if (point.x > result.maxX)
            result.maxX = point.x;
        if (point.y > result.maxY)
            result.maxY = point.y;
    }
    return result;
}

bool SameBounds(const std::optional<SelectionBounds>& actual, const std::optional<SelectionBounds>& expected) noexcept
{
    if (actual.has_value() != expected.has_value())
        return false;
    if (!actual)
        return true;
    return actual->minX == expected->minX && actual->minY == expected->minY && actual->maxX == expected->maxX &&
           actual->maxY == expected->maxY;
}

bool SnapshotMatches(const BoundedLassoPoints::Snapshot& snapshot, std::span<const Vec2> expected)
{
    const auto maximumScratch = ((expected.size() + 63u) / 64u) * sizeof(std::uint32_t);
    if (snapshot.Count() != expected.size() || snapshot.Empty() != expected.empty() ||
        !SameBounds(snapshot.Bounds(), ReferenceBounds(expected)) ||
        snapshot.TraversalScratchBytes() > maximumScratch ||
        (snapshot.ArenaIdentity() != nullptr && snapshot.RetainedArenaBytes() == 0))
        return false;

    std::vector<Vec2> visited;
    visited.reserve(expected.size());
    bool boundedChunks = true;
    const bool visitedAll = snapshot.VisitForWork(
        [&](std::span<const Vec2> chunk)
        {
            boundedChunks = boundedChunks && !chunk.empty() && chunk.size() <= 64;
            visited.insert(visited.end(), chunk.begin(), chunk.end());
            return true;
        });
    if (!visitedAll || !boundedChunks || visited.size() != expected.size())
        return false;
    for (std::size_t index = 0; index < expected.size(); ++index)
        if (!(visited[index] == expected[index]))
            return false;

    std::vector<Vec2> copied;
    copied.reserve(expected.size());
    if (!snapshot.CopyToForWork(copied) || copied.size() != expected.size())
        return false;
    for (std::size_t index = 0; index < expected.size(); ++index)
        if (!(copied[index] == expected[index]))
            return false;
    return true;
}

struct SavedSnapshot final
{
    const char* label;
    BoundedLassoPoints::Snapshot snapshot;
    std::vector<Vec2> expected;
};

struct DeferredFixture final
{
    std::array<DeferredReleaseNode*, 4> nodes{};
    std::size_t count = 0;
    bool overflow = false;
    std::uint32_t destroyed = 0;

    static void Enqueue(void* context, DeferredReleaseNode& node) noexcept
    {
        auto& fixture = *static_cast<DeferredFixture*>(context);
        if (fixture.count == fixture.nodes.size())
        {
            fixture.overflow = true;
            return;
        }
        fixture.nodes[fixture.count++] = &node;
    }

    void DrainOnWorker()
    {
        std::thread worker(
            [this]() noexcept
            {
                for (std::size_t index = 0; index < count; ++index)
                {
                    auto* node = nodes[index];
                    if (node && node->destroy)
                    {
                        node->destroy(node);
                        ++destroyed;
                    }
                    nodes[index] = nullptr;
                }
            });
        worker.join();
        count = 0;
    }
};

void CheckBoundariesAndSnapshotRetention()
{
    static_assert(!std::is_copy_constructible_v<BoundedLassoPoints>);
    static_assert(!std::is_copy_assignable_v<BoundedLassoPoints>);
    static_assert(std::is_nothrow_move_constructible_v<BoundedLassoPoints>);
    static_assert(std::is_nothrow_move_assignable_v<BoundedLassoPoints>);
    static_assert(std::is_copy_constructible_v<BoundedLassoPoints::Snapshot>);
    static_assert(std::is_nothrow_move_constructible_v<BoundedLassoPoints::Snapshot>);

    auto memory = SharedPublicationMemory::CreateForWork();
    auto writer = BoundedLassoPoints::CreateForWork(memory, 5);
    Expect(writer.has_value(), "worker-arena-created-with-five-blocks");
    if (!writer)
        return;

    const auto arenaCharge = memory->LocalAllocationBytes();
    Expect(memory->SealLocalAllocations(), "arena-allocation-ledger-sealed-before-owner-writes");
    constexpr std::array<std::pair<std::uint32_t, const char*>, 6> boundaries{{{63, "boundary-63"},
                                                                               {64, "boundary-64"},
                                                                               {65, "boundary-65"},
                                                                               {127, "boundary-127"},
                                                                               {128, "boundary-128"},
                                                                               {129, "boundary-129"}}};
    std::vector<SavedSnapshot> saved;
    saved.reserve(boundaries.size());
    std::vector<Vec2> reference;
    reference.reserve(129);
    std::size_t nextBoundary = 0;
    for (std::uint32_t index = 0; index < 129; ++index)
    {
        const Vec2 point = ReferencePoint(index);
        Expect(writer->PushOwner(point), "boundary-point-admitted");
        reference.push_back(point);
        if (nextBoundary < boundaries.size() && boundaries[nextBoundary].first == index + 1)
        {
            auto snapshot = writer->CaptureOwner();
            Expect(SnapshotMatches(snapshot, reference), boundaries[nextBoundary].second);
            saved.push_back(SavedSnapshot{boundaries[nextBoundary].second, std::move(snapshot), reference});
            ++nextBoundary;
        }
    }
    Expect(nextBoundary == boundaries.size() && writer->CountOwner() == reference.size(),
           "all-literal-boundaries-reached");

    auto copied = saved[2].snapshot;
    auto moved = std::move(copied);
    const std::vector<Vec2> empty;
    Expect(SnapshotMatches(copied, empty) && copied.ArenaIdentity() == nullptr && copied.RetainedArenaBytes() == 0 &&
               moved.ArenaIdentity() == saved[2].snapshot.ArenaIdentity(),
           "moved-from-snapshot-copy-and-visit-are-empty");
    Expect(SnapshotMatches(saved[2].snapshot, saved[2].expected) && SnapshotMatches(moved, saved[2].expected),
           "snapshot-copy-and-move-preserve-independent-prefix");

    for (std::uint32_t count = 0; count < 65; ++count)
    {
        Expect(writer->PopOwner(), "pop-from-live-writer");
        reference.pop_back();
    }
    Expect(writer->CountOwner() == reference.size() && SnapshotMatches(writer->CaptureOwner(), reference),
           "pop-across-sealed-block-restores-reference-and-bounds");
    for (std::uint32_t index = 0; index < 70; ++index)
    {
        const Vec2 point = {200.0 + static_cast<double>(index), -300.0 - static_cast<double>(index)};
        Expect(writer->PushOwner(point), "append-after-pop-admitted");
        reference.push_back(point);
    }
    Expect(SnapshotMatches(writer->CaptureOwner(), reference), "reextended-writer-matches-reference");
    writer->ClearOwner();
    reference.clear();
    Expect(writer->CountOwner() == 0 && writer->SealedBlocksOwner() == 3 &&
               SnapshotMatches(writer->CaptureOwner(), reference),
           "clear-drops-current-prefix-without-recycling-sealed-nodes");
    for (std::uint32_t index = 0; index < 80; ++index)
    {
        const Vec2 point = {-500.0 + static_cast<double>(index), 700.0 - static_cast<double>(index)};
        Expect(writer->PushOwner(point), "append-after-clear-admitted");
        reference.push_back(point);
    }
    Expect(SnapshotMatches(writer->CaptureOwner(), reference), "post-clear-extension-matches-reference");
    for (const auto& item : saved)
        Expect(SnapshotMatches(item.snapshot, item.expected), item.label);
    Expect(SnapshotMatches(moved, saved[2].expected), "moved-snapshot-survives-writer-clear-and-reextension");
    Expect(memory->LocalAllocationBytes() == arenaCharge,
           "sealed-arena-charge-is-unchanged-through-owner-edits-and-snapshots");
}

void CheckOneBlockExhaustionAndRefusal()
{
    auto memory = SharedPublicationMemory::CreateForWork();
    auto created = BoundedLassoPoints::CreateForWork(memory, 1);
    Expect(created.has_value(), "one-block-arena-created");
    if (!created)
        return;
    auto& writer = *created;
    std::vector<Vec2> reference;
    reference.reserve(128);
    for (std::uint32_t index = 0; index < 128; ++index)
    {
        const Vec2 point = ReferencePoint(index + 1000);
        const bool accepted = writer.PushOwner(point);
        if (accepted)
            reference.push_back(point);
        if (index == 63 || index == 127)
            Expect(accepted, index == 63 ? "first-block-boundary-accepted" : "one-block-plus-tail-128-accepted");
    }
    const auto fullSnapshot = writer.CaptureOwner();
    const std::vector<Vec2> fullReference = reference;
    Expect(writer.CountOwner() == 128 && writer.SealedBlocksOwner() == 1 &&
               SnapshotMatches(fullSnapshot, fullReference),
           "one-block-arena-holds-one-sealed-block-and-one-tail");
    const auto fullCount = writer.CountOwner();
    const auto fullBlocks = writer.SealedBlocksOwner();
    Expect(!writer.PushOwner(ReferencePoint(1128)) && writer.CountOwner() == fullCount &&
               writer.SealedBlocksOwner() == fullBlocks && SnapshotMatches(writer.CaptureOwner(), reference),
           "129th-point-refusal-preserves-writer-and-captured-prefix");

    for (int index = 0; index < 65; ++index)
    {
        const bool popped = writer.PopOwner();
        if (popped)
            reference.pop_back();
    }
    Expect(writer.CountOwner() == 63 && SnapshotMatches(writer.CaptureOwner(), reference),
           "pop-crosses-sealed-boundary-before-reextension");
    const Vec2 refill{900.0, -900.0};
    Expect(writer.PushOwner(refill), "reextension-restores-inline-tail");
    reference.push_back(refill);
    const auto beforeRefusal = writer.CountOwner();
    const auto beforeSealed = writer.SealedBlocksOwner();
    Expect(!writer.PushOwner({901.0, -901.0}) && writer.CountOwner() == beforeRefusal &&
               writer.SealedBlocksOwner() == beforeSealed && SnapshotMatches(writer.CaptureOwner(), reference),
           "reextension-exhausts-arena-before-point-limit-without-mutation");
    Expect(SnapshotMatches(fullSnapshot, fullReference), "original-full-snapshot-survives-pop-and-refused-reextension");

    writer.ClearOwner();
    for (std::uint32_t index = 0; index < 64; ++index)
        Expect(writer.PushOwner(ReferencePoint(index + 3000)), "clear-reextension-fills-inline-tail");
    const auto beforeClearedRefusal = writer.CountOwner();
    Expect(!writer.PushOwner(ReferencePoint(3064)) && writer.CountOwner() == beforeClearedRefusal &&
               writer.SealedBlocksOwner() == 1 && SnapshotMatches(fullSnapshot, fullReference),
           "clear-does-not-recycle-old-arena-node-or-change-old-snapshot");
}

void CheckInvalidInputsAndMoves()
{
    static_assert(BoundedLassoPoints::kMaxPoints == 8u * 1024u * 1024u);
    static_assert(BoundedLassoPoints::kMaxBlocks == 131071u);
    Expect(BoundedLassoPoints::MemoryBytes(0) == 0 &&
               BoundedLassoPoints::MemoryBytes(BoundedLassoPoints::kMaxBlocks) != 0 &&
               BoundedLassoPoints::MemoryBytes(BoundedLassoPoints::kMaxBlocks + 1u) == 0,
           "public-point-and-block-limits-and-memory-bytes-boundaries");
    auto memory = SharedPublicationMemory::CreateForWork();
    Expect(!BoundedLassoPoints::CreateForWork({}, 1) && !BoundedLassoPoints::CreateForWork(memory, 0) &&
               !BoundedLassoPoints::CreateForWork(memory, BoundedLassoPoints::kMaxBlocks + 1u),
           "null-owner-zero-block-and-overmaximum-create-refuse");
    CancelSource cancelled;
    static_cast<void>(cancelled.Cancel());
    Expect(!BoundedLassoPoints::CreateForWork(memory, 1, cancelled.Token()) && memory->LocalAllocationBytes() == 0,
           "cancelled-page-pretouch-refuses-before-arena-acquisition");

    auto result = BoundedLassoPoints::CreateForWork(memory, 2);
    Expect(result.has_value(), "move-fixture-arena-created");
    if (!result)
        return;
    const auto initiallyEmpty = result->CaptureOwner();
    Expect(result->CountOwner() == 0 && result->SealedBlocksOwner() == 0 && !result->PopOwner() &&
               SnapshotMatches(initiallyEmpty, std::span<const Vec2>{}),
           "zero-use-writer-and-snapshot-are-empty");
    Expect(result->PushOwner({1, 2}), "move-fixture-first-point-admitted");
    BoundedLassoPoints moved(std::move(*result));

    const auto movedFromCount = result->CountOwner();
    const auto movedFromSnapshot = result->CaptureOwner();
    std::uint32_t movedFromVisits = 0;
    std::vector<Vec2> movedFromCopy{{9, 9}};
    const bool movedFromVisited = movedFromSnapshot.VisitForWork(
        [&](std::span<const Vec2>)
        {
            ++movedFromVisits;
            return true;
        });
    const bool movedFromCopied = movedFromSnapshot.CopyToForWork(movedFromCopy);
    Expect(!result->PushOwner({3, 4}) && !result->PopOwner() && movedFromCount == 0 &&
               result->CountOwner() == movedFromCount && result->SealedBlocksOwner() == 0 &&
               SnapshotMatches(movedFromSnapshot, std::span<const Vec2>{}) &&
               movedFromSnapshot.ArenaIdentity() == nullptr && movedFromSnapshot.RetainedArenaBytes() == 0 &&
               movedFromVisited && movedFromVisits == 0 && movedFromCopied && movedFromCopy.empty(),
           "moved-from-writer-capture-visit-and-copy-are-empty-and-mutations-refuse");

    const std::array<Vec2, 1> initialPoint{{{1, 2}}};
    Expect(moved.CountOwner() == 1 && SnapshotMatches(moved.CaptureOwner(), initialPoint),
           "moved-to-writer-retains-state");
    Expect(!moved.PushOwner({std::numeric_limits<double>::quiet_NaN(), 0}) && moved.CountOwner() == 1 &&
               !moved.PushOwner({0, std::numeric_limits<double>::infinity()}) && moved.CountOwner() == 1 &&
               SnapshotMatches(moved.CaptureOwner(), initialPoint),
           "nonfinite-x-and-y-refuse-without-changing-writer");
    Expect(moved.PopOwner() && moved.CountOwner() == 0 && !moved.CaptureOwner().Bounds(),
           "live-writer-pop-remains-available");
}

void CheckVisitorStopsAndCancellation()
{
    auto memory = SharedPublicationMemory::CreateForWork();
    auto result = BoundedLassoPoints::CreateForWork(memory, 2);
    Expect(result.has_value(), "visitor-fixture-arena-created");
    if (!result)
        return;
    auto& writer = *result;
    std::vector<Vec2> reference;
    for (std::uint32_t index = 0; index < 70; ++index)
    {
        const Vec2 point = ReferencePoint(index + 2000);
        Expect(writer.PushOwner(point), "visitor-fixture-point-admitted");
        reference.push_back(point);
    }
    const auto snapshot = writer.CaptureOwner();
    std::uint32_t calls = 0;
    bool bounded = false;
    const bool accepted = snapshot.VisitForWork(
        [&](std::span<const Vec2> chunk)
        {
            ++calls;
            bounded = !chunk.empty() && chunk.size() <= 64;
            return false;
        });
    Expect(!accepted && calls == 1 && bounded, "visitor-false-stops-after-first-bounded-chunk");
    CancelSource cancelled;
    static_cast<void>(cancelled.Cancel());
    calls = 0;
    const bool completed = snapshot.VisitForWork(
        [&](std::span<const Vec2>) noexcept
        {
            ++calls;
            return true;
        },
        cancelled.Token());
    Expect(!completed && calls == 0, "cancelled-visitor-stops-before-first-chunk");
    Expect(writer.CountOwner() == reference.size() && SnapshotMatches(writer.CaptureOwner(), reference) &&
               SnapshotMatches(snapshot, reference),
           "visitor-refusal-and-cancellation-preserve-owner-and-snapshot");
    CancelSource duringVisit;
    calls = 0;
    const bool finishedAfterCancel = snapshot.VisitForWork(
        [&](std::span<const Vec2> chunk) noexcept
        {
            ++calls;
            bounded = !chunk.empty() && chunk.size() <= 64;
            static_cast<void>(duringVisit.Cancel());
            return true;
        },
        duringVisit.Token());
    Expect(!finishedAfterCancel && calls == 1 && bounded, "cancellation-after-first-chunk-refuses-before-tail-visit");
    Expect(SnapshotMatches(snapshot, reference), "cancelled-inflight-reader-leaves-captured-source-intact");
    std::vector<Vec2> output{{-7, -9}};
    Expect(!snapshot.CopyToForWork(output, cancelled.Token()) && output == std::vector<Vec2>{{-7, -9}},
           "cancelled-copy-preserves-caller-output");
}

void CheckAccountingAndLastSnapshotLifetime()
{
    DeferredFixture sink;
    const bool installed = InstallDeferredReleaseSink(&DeferredFixture::Enqueue, &sink);
    Expect(installed, "isolated-deferred-release-sink-installed");
    if (!installed)
        return;

    auto memory = SharedPublicationMemory::CreateForWork();
    const auto maximumCharge = BoundedLassoPoints::MemoryBytes(1);
    std::optional<BoundedLassoPoints> writer = BoundedLassoPoints::CreateForWork(memory, 1);
    Expect(writer.has_value(), "accounting-arena-created");
    if (!writer)
    {
        static_cast<void>(ClearDeferredReleaseSink(&sink));
        return;
    }
    Expect(memory->LocalAllocationBytes() != 0 && memory->LocalAllocationBytes() <= maximumCharge,
           "actual-local-charge-fits-arena-admission-formula");
    Expect(writer->PushOwner({4, 8}), "accounting-fixture-point-admitted");
    std::optional<BoundedLassoPoints::Snapshot> lastSnapshot = writer->CaptureOwner();
    const auto retainedCharge = memory->LocalAllocationBytes();
    writer.reset();
    Expect(lastSnapshot->Count() == 1 && memory->LocalAllocationBytes() == retainedCharge && sink.count == 0,
           "snapshot-retains-arena-after-writer-release");
    lastSnapshot.reset();
    Expect(sink.count == 1 && !sink.overflow && memory->LocalAllocationBytes() > 0 &&
               memory->LocalAllocationBytes() < retainedCharge,
           "last-owner-release-defers-whole-arena-to-installed-sink");
    const bool cleared = ClearDeferredReleaseSink(&sink);
    Expect(cleared, "deferred-release-sink-cleared-before-drain");
    sink.DrainOnWorker();
    Expect(sink.destroyed == 1 && memory->LocalAllocationBytes() == 0, "worker-drain-releases-last-arena-charge");
}
} // namespace

int RunBoundedLassoPointTests()
{
    gAssertions = 0;
    gFailures = 0;
    CheckBoundariesAndSnapshotRetention();
    CheckOneBlockExhaustionAndRefusal();
    CheckInvalidInputsAndMoves();
    CheckVisitorStopsAndCancellation();
    CheckAccountingAndLastSnapshotLifetime();
    std::printf("%s bounded-lasso-points.summary checks=%d failures=%d\n", gFailures == 0 ? "PASS" : "FAIL",
                gAssertions, gFailures);
    return gFailures;
}
