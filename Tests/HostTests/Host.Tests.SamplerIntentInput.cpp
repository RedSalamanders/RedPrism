#include "Host.Tests.SamplerIntentInput.h"

#include "../../Common/SamplerIntentInput.h"

#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>

static_assert(!std::is_copy_constructible_v<SamplerIntentInput>);
static_assert(!std::is_copy_assignable_v<SamplerIntentInput>);
static_assert(std::is_nothrow_move_constructible_v<SamplerIntentInput>);
static_assert(std::is_nothrow_move_assignable_v<SamplerIntentInput>);

namespace
{
int g_failures = 0;
int g_checks = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s sampler-intent-input.%s\n", condition ? "PASS" : "FAIL", name);
    g_failures += condition ? 0 : 1;
    ++g_checks;
}

SamplerPointIntent Intent(std::uint64_t palette, std::uint64_t picker, std::uint32_t baseline) noexcept
{
    return SamplerPointIntent{palette, picker, baseline};
}

bool IsPoint(const std::optional<SamplerIntentPoint>& point, std::uint64_t opening, std::uint64_t sequence,
             std::int32_t x, std::int32_t y, SamplerPointIntent intent) noexcept
{
    return point.has_value() && point.value().point.x == x && point.value().point.y == y &&
           point.value().point.stamp.opening == opening && point.value().point.stamp.sequence == sequence &&
           point.value().intent == intent;
}

SamplerPointStamp StampOrEmpty(const std::optional<SamplerIntentPoint>& point) noexcept
{
    return point.has_value() ? point.value().point.stamp : SamplerPointStamp{0, 0};
}

void TestIntentSensitiveCoalescingAndActiveAck()
{
    auto input = std::make_unique<SamplerIntentInput>();
    constexpr auto foregroundA = SamplerPointIntent{11, 0, 0xFF102030U};
    constexpr auto foregroundB = SamplerPointIntent{12, 0, 0xFF102030U};
    constexpr auto changedBaseline = SamplerPointIntent{12, 0, 0xFF405060U};

    Expect(input->Begin(100, 4, 4), "coalesce-begin");
    Expect(input->Accept(100, 1, 2, foregroundA) == SamplerIntentInput::Admission::Recorded &&
               input->Accept(100, 1, 2, foregroundA) == SamplerIntentInput::Admission::Coalesced,
           "same-pixel-same-intent-coalesces");
    Expect(input->Accept(100, 1, 2, foregroundB) == SamplerIntentInput::Admission::Recorded &&
               input->Accept(100, 1, 2, foregroundB) == SamplerIntentInput::Admission::Coalesced,
           "same-pixel-different-intent-is-retained-then-coalesced-with-itself");
    Expect(input->Accept(100, 1, 2, changedBaseline) == SamplerIntentInput::Admission::Recorded &&
               input->PendingCount() == 3 && input->LastSequence() == 5,
           "baseline-change-at-same-pixel-is-retained");

    const auto first = input->SelectNext(100);
    Expect(IsPoint(first, 100, 2, 1, 2, foregroundA), "coalesced-slot-keeps-its-own-context-and-latest-stamp");
    Expect(input->Accept(100, 1, 2, foregroundB) == SamplerIntentInput::Admission::Recorded &&
               IsPoint(input->ActivePoint(100), 100, 2, 1, 2, foregroundA),
           "accepted-younger-context-never-overwrites-active-context");
    Expect(!input->Acknowledge(SamplerPointStamp{100, 99}, 0xFFABCDEFU) &&
               !input->Acknowledge(SamplerPointStamp{101, 2}, 0xFFABCDEFU) &&
               IsPoint(input->ActivePoint(100), 100, 2, 1, 2, foregroundA) && !input->LatestOpaqueColor().has_value(),
           "wrong-and-foreign-acks-preserve-active-context-and-color");

    Expect(first.has_value() && input->Acknowledge(first.value().point.stamp, 0xFF010203U) &&
               !input->ActivePoint(100).has_value(),
           "exact-ack-clears-active-context");
    const auto second = input->SelectNext(100);
    Expect(IsPoint(second, 100, 4, 1, 2, foregroundB) && second.has_value() &&
               !input->Acknowledge(StampOrEmpty(first), 0xFF998877U) &&
               IsPoint(input->ActivePoint(100), 100, 4, 1, 2, foregroundB),
           "different-intent-remains-next-and-duplicate-ack-preserves-it");
    Expect(second.has_value() && input->Acknowledge(second.value().point.stamp, std::nullopt),
           "different-intent-acknowledges");
    const auto third = input->SelectNext(100);
    Expect(IsPoint(third, 100, 5, 1, 2, changedBaseline), "changed-baseline-context-remains-in-order");
    Expect(third.has_value() && input->Acknowledge(third.value().point.stamp, std::nullopt),
           "changed-baseline-context-acknowledges");
    const auto fourth = input->SelectNext(100);
    Expect(IsPoint(fourth, 100, 6, 1, 2, foregroundB), "post-selection-same-pixel-is-a-new-pending-entry");
}

void TestCancellationSealAndFreshOpening()
{
    auto input = std::make_unique<SamplerIntentInput>();
    constexpr auto firstIntent = SamplerPointIntent{21, 31, 0xFF112233U};
    constexpr auto pendingIntent = SamplerPointIntent{22, 32, 0xFF445566U};
    Expect(input->Begin(200, 3, 2) &&
               input->Accept(200, 1, 0, firstIntent) == SamplerIntentInput::Admission::Recorded &&
               input->Accept(200, 2, 1, pendingIntent) == SamplerIntentInput::Admission::Recorded,
           "cancel-begin-and-record-prefix");
    const auto active = input->SelectNext(200);
    Expect(IsPoint(active, 200, 1, 1, 0, firstIntent), "cancel-active-selected");
    Expect(input->Cancel(200) && input->PendingCount() == 0 &&
               IsPoint(input->ActivePoint(200), 200, 1, 1, 0, firstIntent) && !input->IsFinished(200),
           "cancel-drops-pending-context-and-retains-active-context");
    Expect(active.has_value() && input->Acknowledge(active.value().point.stamp, 0xFF998877U) &&
               input->IsFinished(200) && !input->LatestOpaqueColor().has_value() &&
               !input->ActivePoint(200).has_value(),
           "cancelled-active-ack-retires-context-without-publishing-color");

    constexpr auto nextIntent = SamplerPointIntent{41, 51, 0xFFABCDEFU};
    Expect(input->Begin(201, 1, 1) && input->HasNoWork(201) && !input->Begin(200, 1, 1) &&
               input->Accept(201, 0, 0, nextIntent) == SamplerIntentInput::Admission::Recorded,
           "fresh-monotonic-opening-starts-empty");
    const auto sealedPoint = input->SelectNext(201);
    Expect(IsPoint(sealedPoint, 201, 3, 0, 0, nextIntent) && input->Seal(201) &&
               input->Accept(201, 0, 0, nextIntent) == SamplerIntentInput::Admission::Refused &&
               !input->IsFinished(201),
           "seal-keeps-accepted-prefix-and-does-not-add-release-point");
    Expect(sealedPoint.has_value() && input->Acknowledge(sealedPoint.value().point.stamp, std::nullopt) &&
               input->IsFinished(201),
           "sealed-prefix-finishes-after-exact-active-ack");
}

void TestCapacityAndWrapOrdering()
{
    auto input = std::make_unique<SamplerIntentInput>();
    Expect(input->Begin(300, 900, 1), "wrap-begin");
    for (std::uint32_t index = 0; index < 400; ++index)
    {
        const auto intent = Intent(static_cast<std::uint64_t>(index) + 1, 0, 0xFF000000U | index);
        Expect(input->Accept(300, static_cast<std::int32_t>(index), 0, intent) ==
                   SamplerIntentInput::Admission::Recorded,
               "wrap-prefix-recorded");
    }
    for (std::uint32_t index = 0; index < 350; ++index)
    {
        const auto point = input->SelectNext(300);
        const auto intent = Intent(static_cast<std::uint64_t>(index) + 1, 0, 0xFF000000U | index);
        Expect(
            IsPoint(point, 300, static_cast<std::uint64_t>(index) + 1, static_cast<std::int32_t>(index), 0, intent) &&
                point.has_value() && input->Acknowledge(point.value().point.stamp, std::nullopt),
            "wrap-prefix-drains-in-literal-order");
    }
    for (std::uint32_t index = 400; index < 862; ++index)
    {
        const auto intent = Intent(static_cast<std::uint64_t>(index) + 1, 0, 0xFF000000U | index);
        Expect(input->Accept(300, static_cast<std::int32_t>(index), 0, intent) ==
                   SamplerIntentInput::Admission::Recorded,
               "wrap-suffix-recorded");
    }
    Expect(input->PendingCount() == SamplerIntentInput::kPendingCapacity && input->LastSequence() == 862 &&
               input->Accept(300, 862, 0, Intent(863, 0, 0xFF0002EEU)) == SamplerIntentInput::Admission::Overflow &&
               input->PendingCount() == SamplerIntentInput::kPendingCapacity && input->LastSequence() == 862,
           "capacity-overflow-preserves-exact-512-entry-prefix");
    for (std::uint32_t index = 350; index < 862; ++index)
    {
        const auto point = input->SelectNext(300);
        const auto intent = Intent(static_cast<std::uint64_t>(index) + 1, 0, 0xFF000000U | index);
        Expect(
            IsPoint(point, 300, static_cast<std::uint64_t>(index) + 1, static_cast<std::int32_t>(index), 0, intent) &&
                point.has_value() && input->Acknowledge(point.value().point.stamp, std::nullopt),
            "wrapped-ring-keeps-coordinate-stamp-and-context-order");
    }
    Expect(input->IsFinished(300), "overflowed-wrapped-prefix-finishes");
}

void TestSequenceExhaustionAndMove()
{
    constexpr std::uint64_t maximum = (std::numeric_limits<std::uint64_t>::max)();
    auto input = std::make_unique<SamplerIntentInput>(maximum - 1);
    constexpr auto intent = SamplerPointIntent{61, 62, 0xFF778899U};
    Expect(input->Begin(400, 1, 1) && input->Accept(400, 0, 0, intent) == SamplerIntentInput::Admission::Recorded &&
               input->LastSequence() == maximum &&
               input->Accept(400, 0, 0, intent) == SamplerIntentInput::Admission::Refused && input->PendingCount() == 1,
           "maximum-sequence-accepted-once-without-context-drift");
    auto moved = std::make_unique<SamplerIntentInput>(std::move(*input));
    Expect(IsPoint(moved->SelectNext(400), 400, maximum, 0, 0, intent) && !input->SelectNext(400).has_value() &&
               input->Accept(400, 0, 0, intent) == SamplerIntentInput::Admission::Refused,
           "move-transfers-context-and-invalidates-source-session");
    const auto active = moved->ActivePoint(400);
    Expect(IsPoint(active, 400, maximum, 0, 0, intent) && active.has_value() &&
               moved->Acknowledge(active.value().point.stamp, std::nullopt) && moved->Seal(400) &&
               moved->IsFinished(400),
           "moved-active-context-acknowledges-at-exhausted-sequence");

    auto openingExhausted = std::make_unique<SamplerIntentInput>();
    Expect(openingExhausted->Begin(maximum, 1, 1) && openingExhausted->Seal(maximum) &&
               openingExhausted->IsFinished(maximum) && !openingExhausted->Begin(1, 1, 1) &&
               !openingExhausted->Begin(maximum, 1, 1),
           "opening-counter-exhaustion-prevents-aba-and-wrap");
}

void TestMoveAssignmentAndSelfMove()
{
    auto source = std::make_unique<SamplerIntentInput>(10);
    auto destination = std::make_unique<SamplerIntentInput>();
    constexpr auto firstIntent = SamplerPointIntent{71, 81, 0xFF102030U};
    constexpr auto secondIntent = SamplerPointIntent{72, 82, 0xFF405060U};
    constexpr auto discardedIntent = SamplerPointIntent{73, 83, 0xFF708090U};

    Expect(source->Begin(500, 4, 4) &&
               source->Accept(500, 2, 1, firstIntent) == SamplerIntentInput::Admission::Recorded &&
               source->Accept(500, 3, 2, secondIntent) == SamplerIntentInput::Admission::Recorded &&
               destination->Begin(400, 4, 4) &&
               destination->Accept(400, 0, 0, discardedIntent) == SamplerIntentInput::Admission::Recorded,
           "move-assignment-source-and-destination-have-independent-prefixes");
    *destination = std::move(*source);
    Expect(destination->PendingCount() == 2 && destination->LastSequence() == 12 &&
               !source->SelectNext(500).has_value() &&
               source->Accept(500, 1, 1, firstIntent) == SamplerIntentInput::Admission::Refused,
           "move-assignment-transfers-ring-and-invalidates-source");

    const auto active = destination->SelectNext(500);
    Expect(IsPoint(active, 500, 11, 2, 1, firstIntent), "move-assignment-preserves-first-intent");
    *destination = std::move(*destination);
    Expect(IsPoint(destination->ActivePoint(500), 500, 11, 2, 1, firstIntent) && destination->PendingCount() == 1 &&
               destination->LastSequence() == 12,
           "self-move-assignment-preserves-active-and-pending-state");
    Expect(active.has_value() && destination->Acknowledge(active.value().point.stamp, std::nullopt),
           "self-moved-active-point-still-acknowledges");
    const auto pending = destination->SelectNext(500);
    Expect(IsPoint(pending, 500, 12, 3, 2, secondIntent) && pending.has_value() &&
               destination->Acknowledge(pending.value().point.stamp, std::nullopt) && destination->Seal(500) &&
               destination->IsFinished(500),
           "self-move-retains-pending-intent-order-through-finish");
}
} // namespace

int RunSamplerIntentInputTests()
{
    g_failures = 0;
    g_checks = 0;
    TestIntentSensitiveCoalescingAndActiveAck();
    TestCancellationSealAndFreshOpening();
    TestCapacityAndWrapOrdering();
    TestSequenceExhaustionAndMove();
    TestMoveAssignmentAndSelfMove();
    std::printf("sampler-intent-input: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures;
}
