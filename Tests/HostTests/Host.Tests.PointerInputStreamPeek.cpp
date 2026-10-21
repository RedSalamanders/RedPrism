#include "Host.Tests.PointerInputStreamPeek.h"

#include "PointerInputStream.h"

#include <cstdint>
#include <cstdio>
#include <optional>

namespace
{
struct Context final
{
    std::uint32_t owner;
};

struct Sample final
{
    std::int32_t x;
    std::int32_t y;
    std::uint32_t marker;
};

using Stream = PointerInputStream<Context, Sample, 3>;
int g_failures = 0;
int g_checks = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s host-pointer-input-stream-peek.%s\n", condition ? "PASS" : "FAIL", name);
    g_failures += condition ? 0 : 1;
    ++g_checks;
}

bool Is(const std::optional<Stream::Event>& event, Stream::Action action, Sample sample) noexcept
{
    return event.has_value() && event->action == action && event->sample.x == sample.x && event->sample.y == sample.y &&
           event->sample.marker == sample.marker;
}

void TestRepeatedPeekAndIndependentConsumption() noexcept
{
    Stream stream;
    const Sample down{1, 2, 0x11};
    Expect(stream.Begin(1, Context{7}, down), "begin-down");
    const auto first = stream.Peek(1);
    const auto second = stream.Peek(1);
    Expect(Is(first, Stream::Action::Down, down) && Is(second, Stream::Action::Down, down),
           "repeated-peek-returns-identical-down");
    Expect(stream.PendingCount() == 1 && !stream.TerminalConsumed(), "peek-does-not-change-count-or-terminal-state");
    auto independent = first;
    independent->sample.marker = 0x99;
    Expect(Is(stream.Peek(1), Stream::Action::Down, down), "mutating-returned-value-cannot-mutate-stream-event");
    Expect(Is(stream.Take(1), Stream::Action::Down, down) && stream.PendingCount() == 0,
           "take-after-peek-consumes-the-peeked-event");
}

void TestWrongOpeningAndEmptyStream() noexcept
{
    Stream stream;
    Expect(!stream.Peek(1), "empty-stream-peek-is-empty");
    Expect(stream.Begin(4, Context{8}, Sample{3, 4, 0x22}), "begin-for-wrong-opening");
    Expect(!stream.Peek(3) && stream.PendingCount() == 1, "wrong-opening-peek-is-empty-and-nonconsuming");
    Expect(Is(stream.Peek(4), Stream::Action::Down, Sample{3, 4, 0x22}), "right-opening-still-sees-down");
}

void TestDownMoveUpOrder() noexcept
{
    Stream stream;
    Expect(stream.Begin(10, Context{9}, Sample{10, 11, 1}), "begin-ordered-gesture");
    Expect(stream.Move(10, Sample{12, 13, 2}) == Stream::Admission::Recorded, "ordered-move-admitted");
    Expect(stream.End(10, Sample{14, 15, 3}, false) == Stream::Admission::Recorded, "ordered-up-admitted");
    const auto down = stream.Peek(10);
    const auto stillDown = stream.Peek(10);
    Expect(Is(down, Stream::Action::Down, Sample{10, 11, 1}) &&
               Is(stillDown, Stream::Action::Down, Sample{10, 11, 1}) && stream.PendingCount() == 3,
           "down-peek-is-repeatable-before-admission");
    Expect(Is(stream.Take(10), Stream::Action::Down, Sample{10, 11, 1}), "ordered-take-down");
    Expect(Is(stream.Peek(10), Stream::Action::Move, Sample{12, 13, 2}), "next-peek-is-move");
    Expect(Is(stream.Take(10), Stream::Action::Move, Sample{12, 13, 2}), "ordered-take-move");
    Expect(Is(stream.Peek(10), Stream::Action::Up, Sample{14, 15, 3}), "next-peek-is-terminal-up");
    Expect(Is(stream.Take(10), Stream::Action::Up, Sample{14, 15, 3}), "ordered-take-up");
    Expect(!stream.Peek(10) && stream.TerminalConsumed(), "consumed-terminal-is-not-peekable");
    Expect(!stream.Begin(11, Context{10}, Sample{16, 17, 4}) && !stream.Peek(11),
           "consumed-terminal-keeps-original-storage-until-acknowledgement");
    Expect(stream.Acknowledge(10) && stream.Begin(11, Context{10}, Sample{16, 17, 4}) && !stream.Peek(10) &&
               Is(stream.Peek(11), Stream::Action::Down, Sample{16, 17, 4}),
           "acknowledgement-reopens-with-only-new-peek-authority");
}

void TestRingWrapAndLossPrefix() noexcept
{
    Stream stream;
    Expect(stream.Begin(20, Context{10}, Sample{20, 21, 10}), "wrap-begin");
    (void)stream.Take(20);
    Expect(stream.Move(20, Sample{22, 23, 11}) == Stream::Admission::Recorded &&
               stream.Move(20, Sample{24, 25, 12}) == Stream::Admission::Recorded &&
               Is(stream.Take(20), Stream::Action::Move, Sample{22, 23, 11}) &&
               stream.Move(20, Sample{26, 27, 13}) == Stream::Admission::Recorded,
           "ring-advances-through-wrap");
    Expect(Is(stream.Peek(20), Stream::Action::Move, Sample{24, 25, 12}) && stream.PendingCount() == 2,
           "wrapped-front-is-next-event-without-consuming");
    Expect(Is(stream.Take(20), Stream::Action::Move, Sample{24, 25, 12}) &&
               Is(stream.Peek(20), Stream::Action::Move, Sample{26, 27, 13}) && stream.PendingCount() == 1,
           "peek-reads-event-at-wrapped-index-zero-after-prior-front-consumption");
    Expect(stream.Drop(20), "drop-wrapped-stream");

    Expect(stream.Begin(21, Context{11}, Sample{30, 31, 20}), "loss-begin");
    Expect(stream.Move(21, Sample{32, 33, 21}) == Stream::Admission::Recorded &&
               stream.Move(21, Sample{34, 35, 22}) == Stream::Admission::Recorded &&
               stream.Move(21, Sample{36, 37, 23}) == Stream::Admission::Overflow,
           "full-ring-move-creates-lost-terminal");
    Expect(stream.PendingCount() == 4 && Is(stream.Peek(21), Stream::Action::Down, Sample{30, 31, 20}),
           "lost-event-does-not-overtake-accepted-prefix");
    (void)stream.Take(21);
    (void)stream.Take(21);
    (void)stream.Take(21);
    Expect(Is(stream.Peek(21), Stream::Action::Lost, Sample{34, 35, 22}) && stream.PendingCount() == 1,
           "lost-terminal-peek-follows-prefix-and-keeps-last-accepted-sample");
}

void TestDropReopenStaleness() noexcept
{
    Stream stream;
    Expect(stream.Begin(30, Context{12}, Sample{40, 41, 30}), "old-opening-begins");
    Expect(Is(stream.Peek(30), Stream::Action::Down, Sample{40, 41, 30}), "old-opening-down-peek");
    Expect(stream.Drop(30), "old-opening-dropped");
    Expect(stream.Begin(31, Context{13}, Sample{42, 43, 31}), "new-opening-after-drop");
    Expect(!stream.Peek(30) && stream.PendingCount() == 1, "old-opening-cannot-peek-reused-storage");
    Expect(Is(stream.Peek(31), Stream::Action::Down, Sample{42, 43, 31}), "new-opening-peek-is-intact");
}
} // namespace

int RunHostPointerInputStreamPeekTests()
{
    g_failures = 0;
    g_checks = 0;
    TestRepeatedPeekAndIndependentConsumption();
    TestWrongOpeningAndEmptyStream();
    TestDownMoveUpOrder();
    TestRingWrapAndLossPrefix();
    TestDropReopenStaleness();
    std::printf("SUMMARY host-pointer-input-stream-peek checks=%d failures=%d expected=36\n", g_checks, g_failures);
    return g_failures == 0 && g_checks == 36 ? 0 : 1;
}
