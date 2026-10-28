#include "Document.Tests.LassoConditionalInput.h"
#include "RedPrism/HostLassoInput.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <utility>

namespace
{
int gAssertions = 0;
int gFailures = 0;

void Expect(bool condition, const char* label) noexcept
{
    ++gAssertions;
    gFailures += condition ? 0 : 1;
    std::printf("%s lasso-conditional-input.%s\n", condition ? "PASS" : "FAIL", label);
}

HostLassoInput::Context Context(LassoKind kind, std::uint64_t ordinal = 31)
{
    HostLassoInput::Context context{};
    context.document.documentSerial = 701;
    context.document.generation = 8;
    context.document.restoreSerial = 4;
    context.document.canvasWidth = 64;
    context.document.canvasHeight = 48;
    context.ordinal = ordinal;
    context.viewEpoch = 19;
    context.kind = kind;
    context.mode = SelectionMode::Add;
    context.mask = true;
    return context;
}

bool SameSample(const HostLassoInput::Sample& actual, const HostLassoInput::Sample& expected) noexcept
{
    return actual.point.x == expected.point.x && actual.point.y == expected.point.y &&
           actual.operation == expected.operation && actual.cursorPresent == expected.cursorPresent &&
           actual.square == expected.square && actual.fromCenter == expected.fromCenter &&
           actual.antialiased == expected.antialiased && actual.nearFirst == expected.nearFirst &&
           actual.doubleClick == expected.doubleClick && actual.closingIntent == expected.closingIntent;
}

bool SameContext(const HostLassoInput::Context* actual, const HostLassoInput::Context& expected) noexcept
{
    return actual && actual->document == expected.document && !actual->active && !actual->crop &&
           actual->ordinal == expected.ordinal && actual->viewEpoch == expected.viewEpoch &&
           actual->kind == expected.kind && actual->mode == expected.mode && actual->mask == expected.mask;
}

static_assert(noexcept(HostLassoInput{}));
static_assert(noexcept(std::declval<HostLassoInput&>().Begin(1, std::declval<HostLassoInput::Context>(), Vec2{})));
static_assert(noexcept(std::declval<HostLassoInput&>().PolygonPress(1, Vec2{}, false, false, true)));
static_assert(noexcept(std::declval<HostLassoInput&>().SelectReplayedPolygonClose(1)));
static_assert(noexcept(std::declval<HostLassoInput&>().Peek(1)));
static_assert(noexcept(std::declval<HostLassoInput&>().Take(1)));
static_assert(noexcept(std::declval<HostLassoInput&>().Acknowledge(1)));
static_assert(noexcept(std::declval<HostLassoInput&>().Drop(1)));
static_assert(noexcept(std::declval<HostLassoInput&>().Finish(1, true)));
static_assert(noexcept(std::declval<HostLassoInput&>().Point(1, Vec2{})));
static_assert(noexcept(std::declval<HostLassoInput&>().Backspace(1)));
static_assert(noexcept(std::declval<const HostLassoInput&>().CapturedContext(1)));
static_assert(noexcept(std::declval<const HostLassoInput&>().PendingCount()));
static_assert(HostLassoInput::kCapacity == 512);
} // namespace

int RunLassoConditionalInputTests()
{
    gAssertions = 0;
    gFailures = 0;
// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027)
    struct Fixture final
    {
        HostLassoInput replay;
        HostLassoInput invalid;
        HostLassoInput nanInput;
        HostLassoInput doubleClick;
        HostLassoInput overflow;
    };
#pragma warning(pop)

    const auto fixture = std::make_unique<Fixture>();
    const auto polygonContext = Context(LassoKind::Polygonal);
    auto& replay = fixture->replay;
    Expect(replay.Begin(10, polygonContext, {1.0, 2.0}), "polygon opening accepts the literal physical down");
    Expect(replay.Point(10, {1.0, 2.0}) == HostLassoInput::Admission::Recorded,
           "accepted ordinary point precedes the physical close press in FIFO order");
    Expect(replay.PolygonPress(10, {7.25, 8.5}, true, false, false, 71) == HostLassoInput::Admission::Recorded,
           "press stores literal point, near-first, double-click, and event-AA values");
    Expect(replay.Point(10, {40.0, 41.0}) == HostLassoInput::Admission::Recorded &&
               replay.Backspace(10) == HostLassoInput::Admission::Recorded &&
               replay.Finish(10, true) == HostLassoInput::Admission::Recorded,
           "later point, backspace, and late up-AA enter the pending physical stream");
    Expect(!replay.SelectReplayedPolygonClose(10) && !replay.SelectReplayedPolygonClose(9),
           "close selection rejects pre-take and stale-opening requests");

    const auto down = replay.Take(10);
    const bool literalDown = down && down->action == HostLassoInput::Action::Down && down->sample.point.x == 1.0 &&
                             down->sample.point.y == 2.0;
    Expect(literalDown && !replay.SelectReplayedPolygonClose(10),
           "literal down is first and a wrong last-taken operation cannot close");
    const auto priorPoint = replay.Take(10);
    const bool literalPriorPoint = priorPoint && priorPoint->action == HostLassoInput::Action::Move &&
                                   priorPoint->sample.operation == HostLassoInput::Operation::Point &&
                                   priorPoint->sample.point.x == 1.0 && priorPoint->sample.point.y == 2.0;
    Expect(literalPriorPoint && !replay.SelectReplayedPolygonClose(10),
           "ordinary point is replayed before press and cannot authorize close");

    const auto press = replay.Take(10);
    HostLassoInput::Sample expectedPress{};
    expectedPress.point = {7.25, 8.5};
    expectedPress.operation = HostLassoInput::Operation::PolygonPress;
    expectedPress.antialiased = false;
    expectedPress.nearFirst = true;
    expectedPress.doubleClick = false;
    expectedPress.closingIntent = 71;
    Expect(press && press->action == HostLassoInput::Action::Move && SameSample(press->sample, expectedPress),
           "taken press exactly retains independently written physical metadata");
    Expect(replay.SelectReplayedPolygonClose(10) && !replay.SelectReplayedPolygonClose(10),
           "accepted near-first press selects one close and rejects duplication");
    Expect(SameContext(replay.CapturedContext(10), polygonContext),
           "selected terminal retains original document, ordinal, view, kind, mode, and mask");
    const auto selected = replay.Peek(10);
    HostLassoInput::Sample expectedFinish{};
    expectedFinish.operation = HostLassoInput::Operation::Finish;
    expectedFinish.antialiased = false;
    Expect(selected && selected->action == HostLassoInput::Action::Up && SameSample(selected->sample, expectedFinish) &&
               replay.PendingCount() == 1,
           "remaining records become exactly one Up with the press AA, not late Up-AA");
    Expect(!replay.Acknowledge(10), "terminal cannot be acknowledged before it is consumed");
    const auto consumed = replay.Take(10);
    Expect(consumed && consumed->action == HostLassoInput::Action::Up && SameSample(consumed->sample, expectedFinish) &&
               !replay.Peek(10),
           "taking selected close consumes its only Up record");
    Expect(replay.Acknowledge(10) && !replay.Acknowledge(10),
           "consumed selected Up acknowledges once and rejects duplicate acknowledgement");
    Expect(!replay.Begin(10, polygonContext, {3.0, 4.0}) && replay.Begin(11, polygonContext, {3.0, 4.0}),
           "old opening reuse is refused and a larger opening is accepted");

    auto& invalid = fixture->invalid;
    Expect(invalid.Begin(20, Context(LassoKind::Freehand), {2.0, 3.0}) &&
               invalid.PolygonPress(20, {4.0, 5.0}, true, false, true) == HostLassoInput::Admission::Invalid &&
               invalid.PolygonPress(21, {4.0, 5.0}, true, false, true) == HostLassoInput::Admission::Stale &&
               invalid.Finish(20, false) == HostLassoInput::Admission::Recorded &&
               invalid.PolygonPress(20, {4.0, 5.0}, true, false, true) == HostLassoInput::Admission::Closed,
           "wrong-kind, stale-opening, and closed physical presses return their exact refusals");

    auto& nanInput = fixture->nanInput;
    const double nan = std::numeric_limits<double>::quiet_NaN();
    Expect(nanInput.Begin(30, polygonContext, {0.0, 0.0}) &&
               nanInput.PolygonPress(30, {nan, 1.0}, true, false, true) == HostLassoInput::Admission::Invalid,
           "non-finite physical press point is refused");

    auto& doubleClick = fixture->doubleClick;
    Expect(doubleClick.Begin(40, polygonContext, {5.0, 6.0}) &&
               doubleClick.PolygonPress(40, {9.0, 10.0}, false, true, true) == HostLassoInput::Admission::Recorded,
           "direct double-click metadata is recorded with near-first false");
    (void)doubleClick.Take(40);
    const auto doublePress = doubleClick.Take(40);
    HostLassoInput::Sample expectedDoublePress{};
    expectedDoublePress.point = {9.0, 10.0};
    expectedDoublePress.operation = HostLassoInput::Operation::PolygonPress;
    expectedDoublePress.antialiased = true;
    expectedDoublePress.doubleClick = true;
    const bool selectedByDoubleClick = doublePress && SameSample(doublePress->sample, expectedDoublePress) &&
                                       doubleClick.SelectReplayedPolygonClose(40);
    const auto doubleFinish = doubleClick.Peek(40);
    Expect(selectedByDoubleClick && doubleFinish && doubleFinish->action == HostLassoInput::Action::Up &&
               doubleFinish->sample.antialiased && doubleClick.Finish(40, false) == HostLassoInput::Admission::Closed,
           "double-click alone selects close, preserves press AA, and blocks conflicting late finish");

    auto& overflow = fixture->overflow;
    bool overflowPrefix = overflow.Begin(50, polygonContext, {0.0, 0.0});
    for (std::size_t index = 0; index < HostLassoInput::kCapacity - 2; ++index)
        overflowPrefix = overflowPrefix &&
                         overflow.Point(50, {static_cast<double>(index), 1.0}) == HostLassoInput::Admission::Recorded;
    overflowPrefix = overflowPrefix &&
                     overflow.PolygonPress(50, {33.0, 44.0}, true, false, true) == HostLassoInput::Admission::Recorded;
    Expect(overflowPrefix && overflow.PendingCount() == HostLassoInput::kCapacity &&
               overflow.Point(50, {55.0, 66.0}) == HostLassoInput::Admission::Overflow && overflow.Overflowed(),
           "full ring refuses extra input as overflow-lost with its accepted prefix intact");
    HostLassoInput::Event lastAccepted{};
    std::size_t drainedCount = 0;
    // Drain the fixed accepted prefix only; Lost lives in its separate terminal slot.
    while (drainedCount < HostLassoInput::kCapacity)
    {
        const auto event = overflow.Take(50);
        if (!event)
            break;
        lastAccepted = *event;
        ++drainedCount;
    }
    Expect(drainedCount == HostLassoInput::kCapacity && lastAccepted.action == HostLassoInput::Action::Move &&
               lastAccepted.sample.operation == HostLassoInput::Operation::PolygonPress &&
               lastAccepted.sample.point.x == 33.0 && lastAccepted.sample.point.y == 44.0,
           "overflow preserves the accepted prefix through its literal last press");
    Expect(!overflow.SelectReplayedPolygonClose(50),
           "overflow-lost cannot be overridden by a previously taken close-eligible press");
    const auto lost = overflow.Take(50);
    HostLassoInput::Sample expectedLost{};
    expectedLost.point = {33.0, 44.0};
    expectedLost.operation = HostLassoInput::Operation::PolygonPress;
    expectedLost.antialiased = true;
    expectedLost.nearFirst = true;
    Expect(lost && lost->action == HostLassoInput::Action::Lost && SameSample(lost->sample, expectedLost) &&
               overflow.Acknowledge(50),
           "single Lost terminal carries last accepted press rather than a successful close");

    std::printf("%s lasso-conditional-input.summary checks=%d failures=%d\n", gFailures == 0 ? "PASS" : "FAIL",
                gAssertions, gFailures);
    return gFailures + (gAssertions <= 26 ? 0 : 1);
}
