#include "SamplerInputTests.h"

#include "../../Common/SamplerInput.h"

#include <cstdint>
#include <cstdio>
#include <limits>
#include <type_traits>
#include <utility>

static_assert(!std::is_copy_constructible_v<SamplerInput>);
static_assert(!std::is_copy_assignable_v<SamplerInput>);
static_assert(std::is_nothrow_move_constructible_v<SamplerInput>);
static_assert(std::is_nothrow_move_assignable_v<SamplerInput>);

namespace
{
int g_failures = 0;
int g_checks = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s sampler-input.%s\n", condition ? "PASS" : "FAIL", name);
    g_failures += condition ? 0 : 1;
    ++g_checks;
}

bool IsPoint(const std::optional<SamplerPoint>& point, std::uint64_t opening, std::uint64_t sequence, std::int32_t x,
             std::int32_t y) noexcept
{
    return point.has_value() && point.value().x == x && point.value().y == y &&
           point.value().stamp.opening == opening && point.value().stamp.sequence == sequence;
}

SamplerPointStamp StampOrEmpty(const std::optional<SamplerPoint>& point) noexcept
{
    return point.has_value() ? point.value().stamp : SamplerPointStamp{0, 0};
}

void TestOpeningAndIdle() noexcept
{
    SamplerInput input;
    Expect(!input.Begin(1, 0, 4) && !input.Begin(1, 4, 0) && !input.Begin(1, -2, 4),
           "zero-or-negative-dimensions-refuse");
    Expect(input.Begin(1, 4, 3), "positive-bounds-open");
    Expect(input.HasNoWork(1) && !input.SelectNext(1).has_value() && input.HasNoWork(1), "idle-open-has-no-dummy-work");
    Expect(!input.Begin(1, 4, 3) && !input.Begin(0, 4, 3), "opening-zero-and-equal-aba-refuse");
    Expect(input.Accept(99, 1, 1) == SamplerInput::Admission::Refused && input.HasNoWork(1),
           "foreign-opening-does-not-change-idle-session");
    Expect(input.Seal(1) && input.IsFinished(1) && !input.Seal(1), "seal-finishes-without-release-point");
    Expect(input.Begin(2, 1, 1), "newer-opening-after-finish");
    Expect(input.Accept(1, 0, 0) == SamplerInput::Admission::Refused && input.HasNoWork(2),
           "old-opening-cannot-feed-reused-stream");
}

void TestMoveTransfersFixedState() noexcept
{
    SamplerInput source;
    Expect(source.Begin(3, 4, 4) && source.Accept(3, 2, 1) == SamplerInput::Admission::Recorded,
           "move-source-opens-and-records");
    SamplerInput moved(std::move(source));
    Expect(moved.PendingCount() == 1 && !source.SelectNext(3).has_value() &&
               source.Accept(3, 1, 1) == SamplerInput::Admission::Refused,
           "move-transfers-prefix-and-invalidates-source");
    const auto selected = moved.SelectNext(3);
    Expect(IsPoint(selected, 3, 1, 2, 1) && selected.has_value() &&
               moved.Acknowledge(StampOrEmpty(selected), 0xFF010203),
           "moved-stream-retains-active-stamp-and-point");
}

void TestOpaquePrefixAndImmutableActive() noexcept
{
    SamplerInput input;
    Expect(input.Begin(10, 8, 8), "prefix-begin");
    Expect(input.Accept(10, 1, 2) == SamplerInput::Admission::Recorded, "opaque-active-point-recorded");
    const auto active = input.SelectNext(10);
    Expect(IsPoint(active, 10, 1, 1, 2), "first-point-becomes-active");
    Expect(input.Accept(10, 3, 4) == SamplerInput::Admission::Recorded &&
               input.Accept(10, 5, 6) == SamplerInput::Admission::Recorded,
           "opaque-pending-and-transparent-last-recorded");
    const auto stillActive = input.ActivePoint(10);
    Expect(IsPoint(stillActive, 10, 1, 1, 2) && IsPoint(active, 10, 1, 1, 2), "new-points-do-not-mutate-active-copy");
    Expect(!input.Acknowledge(SamplerPointStamp{10, 2}, 0xFFFF0000) && IsPoint(input.ActivePoint(10), 10, 1, 1, 2),
           "wrong-sequence-ack-preserves-active-and-color");
    Expect(active.has_value() && input.Acknowledge(StampOrEmpty(active), 0xFF112233),
           "active-opaque-result-acknowledged");
    Expect(input.LatestOpaqueColor().has_value() && input.LatestOpaqueColor().value().argb == 0xFF112233 &&
               input.LatestOpaqueColor().value().stamp.sequence == 1,
           "first-opaque-result-is-valid-prefix-color");
    const auto middle = input.SelectNext(10);
    Expect(IsPoint(middle, 10, 2, 3, 4), "fifo-selects-opaque-middle-before-transparent-last");
    Expect(middle.has_value() && input.Acknowledge(StampOrEmpty(middle), 0xFF789ABC),
           "middle-opaque-result-acknowledged");
    const auto last = input.SelectNext(10);
    Expect(IsPoint(last, 10, 3, 5, 6), "transparent-last-coordinate-keeps-its-sequence");
    Expect(last.has_value() && input.Acknowledge(StampOrEmpty(last), std::nullopt), "transparent-result-acknowledged");
    const auto latest = input.LatestOpaqueColor();
    Expect(latest.has_value() && latest.value().argb == 0xFF789ABC && latest.value().stamp.sequence == 2,
           "transparent-latest-preserves-opaque-middle-prefix");
    Expect(input.Seal(10) && input.IsFinished(10), "sealed-prefix-finishes-after-all-acks");
    Expect(!input.Acknowledge(StampOrEmpty(last), 0xFF000000), "duplicate-ack-refused");
}

void TestOpaqueColorBelongsToOpening() noexcept
{
    SamplerInput input;
    Expect(input.Begin(11, 2, 2) && input.Accept(11, 1, 0) == SamplerInput::Admission::Recorded,
           "first-color-opening-records-point");
    const auto first = input.SelectNext(11);
    Expect(IsPoint(first, 11, 1, 1, 0) && input.Seal(11), "first-opening-seals-with-active-point");
    Expect(first.has_value() && input.Acknowledge(StampOrEmpty(first), 0xFF2468AC) && input.IsFinished(11),
           "opaque-terminal-ack-publishes-first-opening-color");
    const auto firstColor = input.LatestOpaqueColor();
    Expect(firstColor.has_value() && firstColor.value().argb == 0xFF2468AC && firstColor.value().stamp.opening == 11 &&
               firstColor.value().stamp.sequence == 1,
           "opaque-color-is-stamped-to-first-opening");

    Expect(input.Begin(12, 2, 2) && !input.LatestOpaqueColor().has_value() &&
               input.Accept(12, 0, 1) == SamplerInput::Admission::Recorded,
           "new-opening-clears-prior-opening-color");
    const auto transparent = input.SelectNext(12);
    Expect(IsPoint(transparent, 12, 2, 0, 1) && input.Seal(12), "transparent-only-opening-seals-active-point");
    Expect(transparent.has_value() && input.Acknowledge(StampOrEmpty(transparent), std::nullopt) &&
               input.IsFinished(12) && !input.LatestOpaqueColor().has_value(),
           "transparent-terminal-in-new-opening-has-no-inherited-color");
}

void TestPendingCoalescingAndOrder() noexcept
{
    SamplerInput input;
    Expect(input.Begin(20, 10, 10), "coalesce-begin");
    Expect(input.Accept(20, 1, 1) == SamplerInput::Admission::Recorded &&
               input.Accept(20, 1, 1) == SamplerInput::Admission::Coalesced,
           "adjacent-identical-pending-point-coalesces");
    Expect(input.Accept(20, 2, 2) == SamplerInput::Admission::Recorded &&
               input.Accept(20, 1, 1) == SamplerInput::Admission::Recorded && input.PendingCount() == 3,
           "changed-coordinate-a-b-a-remains-three-pending-points");
    const auto first = input.SelectNext(20);
    Expect(IsPoint(first, 20, 2, 1, 1), "coalesced-slot-carries-latest-accepted-sequence");
    Expect(first.has_value() && input.Acknowledge(StampOrEmpty(first), std::nullopt),
           "coalesced-first-point-acknowledges");
    const auto second = input.SelectNext(20);
    Expect(IsPoint(second, 20, 3, 2, 2), "a-b-a-middle-coordinate-remains-in-order");
    Expect(second.has_value() && input.Acknowledge(StampOrEmpty(second), std::nullopt),
           "a-b-middle-point-acknowledges");
    const auto third = input.SelectNext(20);
    Expect(IsPoint(third, 20, 4, 1, 1), "a-b-a-final-coordinate-is-not-coalesced-backward");
    Expect(third.has_value() && input.Acknowledge(StampOrEmpty(third), std::nullopt), "a-b-a-final-point-acknowledges");
}

void TestActiveSerializationAndForeignAck() noexcept
{
    SamplerInput input;
    Expect(input.Begin(30, 4, 4) && input.Accept(30, 0, 0) == SamplerInput::Admission::Recorded,
           "active-serialization-begin-and-record");
    const auto first = input.SelectNext(30);
    const auto firstActive = input.ActivePoint(30);
    Expect(input.Accept(30, 1, 1) == SamplerInput::Admission::Recorded && !input.SelectNext(30).has_value() &&
               IsPoint(firstActive, 30, 1, 0, 0),
           "select-next-refuses-while-active");
    Expect(!input.Acknowledge(SamplerPointStamp{31, StampOrEmpty(first).sequence}, 0xFF010203) &&
               !input.LatestOpaqueColor().has_value() && IsPoint(input.ActivePoint(30), 30, 1, 0, 0),
           "foreign-opening-ack-changes-nothing");
    Expect(first.has_value() && input.Acknowledge(StampOrEmpty(first), 0xFF010203),
           "exact-opening-and-sequence-ack-succeeds");
    Expect(!input.Acknowledge(StampOrEmpty(first), 0xFFFFFFFF), "stale-ack-cannot-overwrite-newer-state");
    const auto second = input.SelectNext(30);
    Expect(IsPoint(second, 30, 2, 1, 1), "point-update-while-active-was-retained");
    Expect(second.has_value() && input.Acknowledge(StampOrEmpty(second), 0x80112233) &&
               input.LatestOpaqueColor().has_value() && input.LatestOpaqueColor().value().argb == 0xFF010203 &&
               input.LatestOpaqueColor().value().stamp.sequence == 1,
           "nonopaque-acknowledgement-does-not-replace-valid-color");
}

void TestBoundsCancelAndSeal() noexcept
{
    SamplerInput input;
    Expect(input.Begin(40, 3, 2), "bounds-begin");
    Expect(input.Accept(40, -1, 0) == SamplerInput::Admission::Refused &&
               input.Accept(40, 0, -1) == SamplerInput::Admission::Refused &&
               input.Accept(40, 3, 0) == SamplerInput::Admission::Refused &&
               input.Accept(40, 0, 2) == SamplerInput::Admission::Refused && input.LastSequence() == 0,
           "out-of-bounds-refusal-does-not-consume-sequence");
    Expect(input.Accept(40, 2, 1) == SamplerInput::Admission::Recorded, "last-in-bounds-pixel-is-accepted");
    const auto acknowledgedPrefix = input.SelectNext(40);
    Expect(IsPoint(acknowledgedPrefix, 40, 1, 2, 1) && input.Acknowledge(StampOrEmpty(acknowledgedPrefix), 0xFF13579B),
           "pre-cancel-opaque-prefix-is-acknowledged");
    Expect(input.Accept(40, 0, 0) == SamplerInput::Admission::Recorded &&
               input.Accept(40, 1, 1) == SamplerInput::Admission::Recorded,
           "active-and-pending-cancel-points-recorded");
    const auto active = input.SelectNext(40);
    Expect(IsPoint(active, 40, 2, 0, 0), "cancel-active-point-selected");
    Expect(input.Cancel(40) && input.PendingCount() == 0 && !input.IsFinished(40),
           "cancel-discards-pending-and-retains-active-until-ack");
    Expect(input.Accept(40, 1, 0) == SamplerInput::Admission::Refused && !input.Seal(40),
           "cancelled-opening-remains-closed");
    Expect(active.has_value() && input.Acknowledge(StampOrEmpty(active), 0xFFABCDEF) && input.IsFinished(40) &&
               input.LatestOpaqueColor().has_value() && input.LatestOpaqueColor().value().argb == 0xFF13579B &&
               input.LatestOpaqueColor().value().stamp.sequence == 1,
           "cancelled-active-result-is-ignored-without-rolling-back-prefix-color");

    Expect(input.Begin(41, 2, 2) && input.Accept(41, 1, 0) == SamplerInput::Admission::Recorded &&
               input.Accept(41, 0, 1) == SamplerInput::Admission::Recorded,
           "seal-with-pending-begin");
    const auto upActive = input.SelectNext(41);
    // The cancelled opening consumed sequences 1-3; cancellation cannot reuse its discarded pending stamp.
    Expect(IsPoint(upActive, 41, 4, 1, 0), "up-while-active-selects-prefix-point");
    Expect(input.Seal(41) && input.Accept(41, 0, 0) == SamplerInput::Admission::Refused && !input.IsFinished(41),
           "up-seals-while-active-and-retains-accepted-prefix");
    Expect(upActive.has_value() && input.Acknowledge(StampOrEmpty(upActive), 0xFFABCDEF) && !input.IsFinished(41),
           "up-active-point-acknowledges-first-while-pending-remains");
    const auto retained = input.SelectNext(41);
    Expect(IsPoint(retained, 41, 5, 0, 1) && retained.has_value() &&
               input.Acknowledge(StampOrEmpty(retained), 0xFFABCDEF) && input.IsFinished(41),
           "sealed-accepted-prefix-completes-without-release-point");
}

void TestInterleavedRingWraparound() noexcept
{
    SamplerInput input;
    Expect(input.Begin(45, 520, 1), "ring-wrap-begin");
    for (std::uint32_t index = 0; index < 520; ++index)
    {
        const std::int32_t x = static_cast<std::int32_t>(index);
        Expect(input.Accept(45, x, 0) == SamplerInput::Admission::Recorded && input.PendingCount() == 1,
               "ring-wrap-accept-keeps-occupancy-one");
        const auto point = input.SelectNext(45);
        Expect(IsPoint(point, 45, static_cast<std::uint64_t>(index) + 1, x, 0) && point.has_value() &&
                   input.Acknowledge(StampOrEmpty(point), std::nullopt) && input.PendingCount() == 0,
               "ring-wrap-select-ack-preserves-literal-order");
    }
    Expect(input.LastSequence() == 520 && input.HasNoWork(45), "ring-wrap-completes-520-interleaved-points");
}

void TestOverflowAndCounterExhaustion() noexcept
{
    SamplerInput input;
    Expect(input.Begin(50, 600, 1), "overflow-begin");
    for (std::uint32_t index = 0; index < SamplerInput::kPendingCapacity; ++index)
    {
        Expect(input.Accept(50, static_cast<std::int32_t>(index), 0) == SamplerInput::Admission::Recorded,
               "overflow-prefix-point-recorded");
    }
    Expect(input.PendingCount() == 512 && input.LastSequence() == 512 &&
               input.Accept(50, 512, 0) == SamplerInput::Admission::Overflow && input.PendingCount() == 512 &&
               input.LastSequence() == 512,
           "overflow-seals-without-dropping-or-numbering-offered-point");
    Expect(input.Accept(50, 513, 0) == SamplerInput::Admission::Refused, "overflowed-prefix-refuses-further-input");
    for (std::uint32_t index = 0; index < SamplerInput::kPendingCapacity; ++index)
    {
        const auto point = input.SelectNext(50);
        Expect(IsPoint(point, 50, static_cast<std::uint64_t>(index) + 1, static_cast<std::int32_t>(index), 0),
               "overflow-retains-fifo-prefix-literal-coordinate");
        Expect(point.has_value() && input.Acknowledge(StampOrEmpty(point), std::nullopt),
               "overflow-prefix-point-acknowledges");
    }
    Expect(input.IsFinished(50), "overflowed-prefix-finishes-after-retained-acks");

    SamplerInput fullTail;
    Expect(fullTail.Begin(51, 600, 1), "full-tail-coalescing-begin");
    for (std::uint32_t index = 0; index < SamplerInput::kPendingCapacity; ++index)
    {
        (void)fullTail.Accept(51, static_cast<std::int32_t>(index), 0);
    }
    Expect(fullTail.Accept(51, 511, 0) == SamplerInput::Admission::Coalesced &&
               fullTail.PendingCount() == SamplerInput::kPendingCapacity && fullTail.LastSequence() == 513 &&
               fullTail.Accept(51, 512, 0) == SamplerInput::Admission::Overflow,
           "full-queue-identical-tail-still-coalesces-before-overflow");
    const auto fullTailLast = [&fullTail]() noexcept
    {
        std::optional<SamplerPoint> point;
        for (std::uint32_t index = 0; index < SamplerInput::kPendingCapacity; ++index)
        {
            point = fullTail.SelectNext(51);
            if (index + 1 == SamplerInput::kPendingCapacity)
            {
                return point;
            }
            (void)fullTail.Acknowledge(StampOrEmpty(point), std::nullopt);
        }
        return point;
    }();
    Expect(IsPoint(fullTailLast, 51, 513, 511, 0), "full-tail-coalescing-keeps-newest-tail-stamp");

    constexpr std::uint64_t maximum = (std::numeric_limits<std::uint64_t>::max)();
    SamplerInput exhausted(maximum - 1);
    Expect(exhausted.Begin(1, 1, 1) && exhausted.Accept(1, 0, 0) == SamplerInput::Admission::Recorded &&
               exhausted.LastSequence() == maximum,
           "maximum-sequence-is-accepted-without-wrap");
    Expect(exhausted.Accept(1, 0, 0) == SamplerInput::Admission::Refused && exhausted.LastSequence() == maximum &&
               exhausted.PendingCount() == 1,
           "exhausted-sequence-refuses-without-mutating-prefix");
    const auto maximumPoint = exhausted.SelectNext(1);
    Expect(IsPoint(maximumPoint, 1, maximum, 0, 0) && maximumPoint.has_value() &&
               exhausted.Acknowledge(StampOrEmpty(maximumPoint), 0xFF010101),
           "maximum-sequence-stamp-acknowledges-exactly");
    Expect(exhausted.Seal(1) && exhausted.IsFinished(1), "exhausted-stream-can-seal-retained-prefix");

    SamplerInput openingExhausted;
    Expect(openingExhausted.Begin(maximum, 1, 1) && openingExhausted.Seal(maximum) &&
               !openingExhausted.Begin(1, 1, 1) && !openingExhausted.Begin(maximum, 1, 1),
           "opening-counter-cannot-aba-or-wrap");
}
} // namespace

int RunSamplerInputTests()
{
    g_failures = 0;
    g_checks = 0;
    TestOpeningAndIdle();
    TestMoveTransfersFixedState();
    TestOpaquePrefixAndImmutableActive();
    TestOpaqueColorBelongsToOpening();
    TestPendingCoalescingAndOrder();
    TestActiveSerializationAndForeignAck();
    TestBoundsCancelAndSeal();
    TestInterleavedRingWraparound();
    TestOverflowAndCounterExhaustion();
    std::printf("sampler-input: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures;
}
