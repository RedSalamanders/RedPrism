#include "PointerInputStreamTests.h"

#include "../../Common/PointerInputStream.h"

#include <cstdint>
#include <cstdio>
#include <limits>
#include <optional>
#include <type_traits>

namespace
{
struct Context final
{
    std::uint32_t target;
    std::uint32_t tool;
    std::uint64_t generation;
};

struct Sample final
{
    std::int32_t x;
    std::int32_t y;
    std::uint32_t modifiers;
    std::uint32_t target;
};

using Stream = PointerInputStream<Context, Sample, 3>;
using SingleSlotStream = PointerInputStream<Context, Sample, 1>;

static_assert(!std::is_copy_constructible_v<Stream>);
static_assert(!std::is_copy_assignable_v<Stream>);
static_assert(!std::is_move_constructible_v<Stream>);
static_assert(!std::is_move_assignable_v<Stream>);
static_assert(!std::is_copy_constructible_v<SingleSlotStream>);
static_assert(!std::is_move_constructible_v<SingleSlotStream>);

int g_failures = 0;
int g_passes = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s pointer-input-stream.%s\n", condition ? "PASS" : "FAIL", name);
    g_failures += condition ? 0 : 1;
    g_passes += condition ? 1 : 0;
}

void ExpectEvent(const std::optional<Stream::Event>& event, Stream::Action action, Sample sample,
                 const char* name) noexcept
{
    Expect(event.has_value() && event->action == action && event->sample.x == sample.x && event->sample.y == sample.y &&
               event->sample.modifiers == sample.modifiers && event->sample.target == sample.target,
           name);
}

bool MatchesEvent(const std::optional<Stream::Event>& event, Stream::Action action, Sample sample) noexcept
{
    return event.has_value() && event->action == action && event->sample.x == sample.x && event->sample.y == sample.y &&
           event->sample.modifiers == sample.modifiers && event->sample.target == sample.target;
}

void ExpectThreeEvents(Stream& stream, std::uint64_t opening, Stream::Action actionOne, Sample sampleOne,
                       Stream::Action actionTwo, Sample sampleTwo, Stream::Action actionThree, Sample sampleThree,
                       const char* name) noexcept
{
    const auto first = stream.Take(opening);
    const auto second = stream.Take(opening);
    const auto third = stream.Take(opening);
    Expect(MatchesEvent(first, actionOne, sampleOne) && MatchesEvent(second, actionTwo, sampleTwo) &&
               MatchesEvent(third, actionThree, sampleThree),
           name);
}

void ExpectFourEvents(Stream& stream, std::uint64_t opening, Stream::Action actionOne, Sample sampleOne,
                      Stream::Action actionTwo, Sample sampleTwo, Stream::Action actionThree, Sample sampleThree,
                      Stream::Action actionFour, Sample sampleFour, const char* name) noexcept
{
    const auto first = stream.Take(opening);
    const auto second = stream.Take(opening);
    const auto third = stream.Take(opening);
    const auto fourth = stream.Take(opening);
    Expect(MatchesEvent(first, actionOne, sampleOne) && MatchesEvent(second, actionTwo, sampleTwo) &&
               MatchesEvent(third, actionThree, sampleThree) && MatchesEvent(fourth, actionFour, sampleFour),
           name);
}

void ExpectTwoEvents(Stream& stream, std::uint64_t opening, Stream::Action actionOne, Sample sampleOne,
                     Stream::Action actionTwo, Sample sampleTwo, const char* name) noexcept
{
    const auto first = stream.Take(opening);
    const auto second = stream.Take(opening);
    Expect(MatchesEvent(first, actionOne, sampleOne) && MatchesEvent(second, actionTwo, sampleTwo), name);
}

void TestAdmissionAndCapturedValues() noexcept
{
    Stream stream;
    Context context{17, 4, 9001};
    Sample down{11, -7, 0x05, 17};
    (void)stream.Begin(1, context, down);
    context = Context{99, 88, 77};
    down = Sample{99, 99, 99, 99};
    const Context* captured = stream.CapturedContext(1);
    Expect(captured != nullptr && captured->target == 17 && captured->tool == 4 && captured->generation == 9001,
           "begin-retains-event-time-context-values");
    Expect(!stream.Begin(2, Context{22, 5, 9002}, Sample{20, 30, 0x02, 22}), "busy-stream-refuses-second-begin");
    captured = stream.CapturedContext(1);
    Expect(captured != nullptr && captured->target == 17 && captured->tool == 4 && captured->generation == 9001 &&
               stream.PendingCount() == 1,
           "refused-begin-preserves-context-and-down");
    ExpectEvent(stream.Take(1), Stream::Action::Down, Sample{11, -7, 0x05, 17}, "down-keeps-original-literal-sample");
}

void TestWrapAroundAndInterleavedTake() noexcept
{
    Stream stream;
    (void)stream.Begin(10, Context{31, 6, 400}, Sample{-3, 8, 0x01, 31});
    ExpectEvent(stream.Take(10), Stream::Action::Down, Sample{-3, 8, 0x01, 31}, "wrap-down-taken-first");
    Expect(stream.Move(10, Sample{4, 9, 0x03, 31}) == Stream::Admission::Recorded, "wrap-first-move-recorded");
    Expect(stream.Move(10, Sample{5, 10, 0x07, 31}) == Stream::Admission::Recorded, "wrap-second-move-recorded");
    ExpectEvent(stream.Take(10), Stream::Action::Move, Sample{4, 9, 0x03, 31}, "interleaved-take-gets-first-move");
    Expect(stream.Move(10, Sample{6, 12, 0x0b, 31}) == Stream::Admission::Recorded,
           "move-after-take-recorded-through-wrap");
    (void)stream.End(10, Sample{7, 13, 0x0f, 31}, false);
    ExpectThreeEvents(stream, 10, Stream::Action::Move, Sample{5, 10, 0x07, 31}, Stream::Action::Move,
                      Sample{6, 12, 0x0b, 31}, Stream::Action::Up, Sample{7, 13, 0x0f, 31},
                      "wrapped-fifo-moves-then-terminal");
    Expect(!stream.Take(10).has_value(), "up-has-no-ghost-events");
    Expect(stream.Acknowledge(10), "wrapped-terminal-acknowledges");
    Expect(!stream.Acknowledge(10), "duplicate-acknowledgement-refused");
}

void TestFullCapacityTerminalAndOverflow() noexcept
{
    Stream stream;
    (void)stream.Begin(20, Context{42, 3, 501}, Sample{1, 2, 0x10, 42});
    Expect(stream.Move(20, Sample{3, 4, 0x11, 42}) == Stream::Admission::Recorded &&
               stream.Move(20, Sample{5, 6, 0x12, 42}) == Stream::Admission::Recorded && stream.PendingCount() == 3,
           "move-ring-reaches-literal-capacity-three");
    Expect(stream.End(20, Sample{7, 8, 0x13, 42}, false) == Stream::Admission::Recorded,
           "terminal-fits-while-move-ring-full");
    ExpectFourEvents(stream, 20, Stream::Action::Down, Sample{1, 2, 0x10, 42}, Stream::Action::Move,
                     Sample{3, 4, 0x11, 42}, Stream::Action::Move, Sample{5, 6, 0x12, 42}, Stream::Action::Up,
                     Sample{7, 8, 0x13, 42}, "full-ring-fifo-through-terminal");
    Expect(stream.End(20, Sample{8, 9, 0x14, 42}, true) == Stream::Admission::Closed, "duplicate-terminal-is-closed");
    Expect(!stream.Take(20).has_value() && stream.Acknowledge(20), "full-ring-up-has-no-ghost-and-acknowledges");

    (void)stream.Begin(21, Context{43, 7, 502}, Sample{-2, -4, 0x20, 43});
    Expect(stream.Move(21, Sample{10, 20, 0x21, 43}) == Stream::Admission::Recorded &&
               stream.Move(21, Sample{30, 40, 0x22, 43}) == Stream::Admission::Recorded,
           "overflow-prefix-moves-recorded");
    Expect(stream.Move(21, Sample{50, 60, 0x23, 43}) == Stream::Admission::Overflow,
           "offered-move-refused-at-capacity");
    ExpectFourEvents(stream, 21, Stream::Action::Down, Sample{-2, -4, 0x20, 43}, Stream::Action::Move,
                     Sample{10, 20, 0x21, 43}, Stream::Action::Move, Sample{30, 40, 0x22, 43}, Stream::Action::Lost,
                     Sample{30, 40, 0x22, 43}, "overflow-prefix-and-loss-coordinate-are-literal");
    Expect(stream.Move(21, Sample{70, 80, 0x24, 43}) == Stream::Admission::Closed &&
               stream.End(21, Sample{90, 100, 0x25, 43}, false) == Stream::Admission::Closed &&
               !stream.Take(21).has_value(),
           "lost-rejects-late-input-and-has-no-ghost-moves");
}

void TestWrongOwnersAndTerminalRetention() noexcept
{
    Stream stream;
    (void)stream.Begin(30, Context{51, 8, 611}, Sample{2, 4, 0x40, 51});
    Expect(stream.Move(29, Sample{99, 98, 0x41, 51}) == Stream::Admission::Stale, "wrong-producer-move-is-stale");
    Expect(stream.End(29, Sample{97, 96, 0x42, 51}, false) == Stream::Admission::Stale, "wrong-producer-end-is-stale");
    Expect(!stream.Take(29).has_value() && stream.CapturedContext(29) == nullptr,
           "wrong-consumer-cannot-read-or-consume");
    Expect(stream.Move(30, Sample{6, 8, 0x43, 51}) == Stream::Admission::Recorded,
           "right-producer-still-appends-after-foreign-calls");
    ExpectTwoEvents(stream, 30, Stream::Action::Down, Sample{2, 4, 0x40, 51}, Stream::Action::Move,
                    Sample{6, 8, 0x43, 51}, "foreign-calls-preserve-down-and-accepted-move");
    Expect(stream.End(30, Sample{10, 12, 0x44, 51}, true) == Stream::Admission::Recorded,
           "explicit-loss-terminal-recorded");
    ExpectEvent(stream.Take(30), Stream::Action::Lost, Sample{10, 12, 0x44, 51}, "explicit-loss-keeps-terminal-sample");
    Expect(!stream.Acknowledge(29) && stream.Busy() && stream.Opening() == 30,
           "wrong-consumer-ack-cannot-release-terminal-owner");
    Expect(stream.Acknowledge(30) && !stream.Busy() && stream.Opening() == 0,
           "right-consumer-ack-releases-terminal-owner");
}

void TestRefusalDropAndOpeningExhaustion() noexcept
{
    Stream stream;
    Expect(!stream.Begin(0, Context{61, 1, 701}, Sample{1, 1, 0x01, 61}) &&
               stream.Begin(1, Context{62, 2, 702}, Sample{2, 3, 0x02, 62}),
           "zero-opening-refusal-consumes-no-monotonic-opening");
    Expect(!stream.Drop(2) && stream.Busy() && stream.Opening() == 1 &&
               stream.Move(1, Sample{4, 5, 0x03, 62}) == Stream::Admission::Recorded,
           "failed-drop-preserves-context-and-event-path");
    Expect(stream.Drop(1) && !stream.Busy() && stream.PendingCount() == 0, "explicit-drop-clears-pending-prefix");
    Expect(!stream.Drop(1) && !stream.Begin(1, Context{63, 3, 703}, Sample{6, 7, 0x04, 63}),
           "duplicate-drop-and-equal-stale-reopen-refused");
    Expect(stream.Begin(2, Context{64, 4, 704}, Sample{8, 9, 0x05, 64}), "newer-opening-admitted-after-drop");
    Expect(stream.Move(1, Sample{90, 91, 0x06, 64}) == Stream::Admission::Stale && !stream.Take(1).has_value() &&
               !stream.Drop(1) && stream.Opening() == 2,
           "old-reused-opening-cannot-produce-consume-or-release-new-stream");
    ExpectEvent(stream.Take(2), Stream::Action::Down, Sample{8, 9, 0x05, 64}, "new-opening-down-is-intact");
    Expect(stream.Drop(2), "drop-releases-new-opening");

    constexpr std::uint64_t maximum = (std::numeric_limits<std::uint64_t>::max)();
    Expect(stream.Begin(maximum, Context{71, 5, 801}, Sample{-11, 12, 0x80, 71}) && stream.Drop(maximum) &&
               !stream.Begin(maximum, Context{72, 6, 802}, Sample{13, 14, 0x81, 72}),
           "maximum-opening-cannot-be-reused-or-wrap");
    Expect(!stream.Busy() && stream.Opening() == 0, "maximum-opening-refusal-leaves-stream-empty");
}

void TestCapacityOne() noexcept
{
    SingleSlotStream stream;
    static_assert(SingleSlotStream::kCapacity == 1);
    Expect(true, "single-slot-capacity-is-one"); // The public compile-time capacity is enforced above.
    Expect(stream.Begin(90, Context{81, 7, 901}, Sample{15, -16, 0x100, 81}), "single-slot-down-admitted");
    Expect(stream.Move(90, Sample{17, -18, 0x101, 81}) == SingleSlotStream::Admission::Overflow,
           "single-slot-offered-move-overflows-behind-down");
    Expect(stream.PendingCount() == 2 && stream.Overflowed(), "single-slot-retains-down-and-terminal");
    const auto down = stream.Take(90);
    Expect(down.has_value() && down->action == SingleSlotStream::Action::Down && down->sample.x == 15 &&
               down->sample.y == -16 && down->sample.modifiers == 0x100 && down->sample.target == 81,
           "single-slot-down-literal-fields");
    const auto lost = stream.Take(90);
    Expect(lost.has_value() && lost->action == SingleSlotStream::Action::Lost && lost->sample.x == 15 &&
               lost->sample.y == -16 && lost->sample.modifiers == 0x100 && lost->sample.target == 81,
           "single-slot-lost-uses-down-as-last-accepted-sample");
    Expect(!stream.Take(90).has_value() && stream.Acknowledge(90), "single-slot-loss-acknowledges-without-ghost");
}

void TestTerminalAcknowledgementProtocol() noexcept
{
    Stream stream;
    Expect(stream.Begin(10, Context{91, 9, 1001}, Sample{20, 21, 0x11, 91}), "ack-protocol-down-admitted");
    Expect(!stream.Acknowledge(10) && stream.Busy() && stream.PendingCount() == 1,
           "ack-before-down-consumption-preserves-owner");
    Expect(!stream.Begin(20, Context{92, 10, 1002}, Sample{30, 31, 0x22, 92}),
           "ack-protocol-refuses-younger-opening-while-busy");
    (void)stream.Take(10);
    Expect(stream.PendingCount() == 0 && stream.Sampling() && stream.Busy() && !stream.Acknowledge(10) &&
               !stream.Take(10),
           "empty-live-path-waits-without-releasing-context");
    Expect(stream.End(10, Sample{22, 23, 0x33, 91}, false) == Stream::Admission::Recorded &&
               stream.End(10, Sample{40, 41, 0x44, 91}, true) == Stream::Admission::Closed,
           "buffered-terminal-refuses-duplicate-loss");
    Expect(!stream.Acknowledge(10) && stream.PendingCount() == 1, "ack-before-terminal-consumption-preserves-terminal");
    ExpectEvent(stream.Take(10), Stream::Action::Up, Sample{22, 23, 0x33, 91},
                "buffered-up-remains-original-after-duplicate-loss");
    Expect(stream.Acknowledge(10) && stream.Begin(20, Context{92, 10, 1002}, Sample{30, 31, 0x22, 92}),
           "busy-begin-refusal-consumes-no-next-opening");
}
} // namespace

int RunPointerInputStreamTests()
{
    g_failures = 0;
    g_passes = 0;
    TestAdmissionAndCapturedValues();
    TestWrapAroundAndInterleavedTake();
    TestFullCapacityTerminalAndOverflow();
    TestWrongOwnersAndTerminalRetention();
    TestRefusalDropAndOpeningExhaustion();
    TestCapacityOne();
    TestTerminalAcknowledgementProtocol();
    std::printf("pointer-input-stream: %d checks, %d failures\n", g_passes + g_failures, g_failures);
    return g_failures;
}
