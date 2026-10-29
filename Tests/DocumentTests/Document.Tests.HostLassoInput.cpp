#include "Document.Tests.HostLassoInput.h"

#include "Common/Document.h"
#include "RedPrism/HostLassoInput.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <optional>
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
    std::printf("%s host-lasso-input.%s\n", condition ? "PASS" : "FAIL", label);
}

HostLassoInput::Context ReferenceContext(LassoKind kind, SelectionMode mode) noexcept
{
    HostLassoInput::Context context;
    context.document.sessionIdentity.bytes = {0x19, 0x27, 0x35, 0x43, 0x51, 0x6f, 0x7d, 0x8b,
                                              0x9a, 0xa8, 0xb6, 0xc4, 0xd2, 0xe0, 0xfe, 0x0c};
    context.document.documentSerial = 101;
    context.document.generation = 17;
    context.document.restoreSerial = 4;
    context.document.canvasWidth = 320;
    context.document.canvasHeight = 240;
    Guid active;
    active.bytes = {0xf0, 0xe1, 0xd2, 0xc3, 0xb4, 0xa5, 0x96, 0x87, 0x78, 0x69, 0x5a, 0x4b, 0x3c, 0x2d, 0x1e, 0x0f};
    context.active = active;
    context.crop = CropRect{11.5, 13.25, 91.0, 73.5};
    context.ordinal = 71;
    context.viewEpoch = 9;
    context.kind = kind;
    context.mode = mode;
    context.mask = true;
    return context;
}

bool ContextEquals(const HostLassoInput::Context& actual, const HostLassoInput::Context& expected) noexcept
{
    return actual.document == expected.document && actual.active == expected.active && actual.crop == expected.crop &&
           actual.ordinal == expected.ordinal && actual.viewEpoch == expected.viewEpoch &&
           actual.kind == expected.kind && actual.mode == expected.mode && actual.mask == expected.mask;
}

struct ExpectedEvent final
{
    HostLassoInput::Action action;
    HostLassoInput::Sample sample;
};

bool EventEquals(const HostLassoInput::Event& actual, const ExpectedEvent& expected) noexcept
{
    const HostLassoInput::Sample& sample = actual.sample;
    const HostLassoInput::Sample& wanted = expected.sample;
    return actual.action == expected.action && sample.point == wanted.point && sample.operation == wanted.operation &&
           sample.cursorPresent == wanted.cursorPresent && sample.square == wanted.square &&
           sample.fromCenter == wanted.fromCenter && sample.antialiased == wanted.antialiased;
}

bool EventVectorsEqual(const std::vector<HostLassoInput::Event>& actual,
                       const std::vector<ExpectedEvent>& expected) noexcept
{
    if (actual.size() != expected.size())
        return false;
    for (std::size_t index = 0; index < expected.size(); ++index)
        if (!EventEquals(actual[index], expected[index]))
            return false;
    return true;
}

bool TakeExpectedSequence(HostLassoInput& input, std::uint64_t opening, const std::vector<ExpectedEvent>& expected)
{
    std::vector<HostLassoInput::Event> actual;
    actual.reserve(expected.size());
    for (std::size_t index = 0; index < expected.size(); ++index)
    {
        const auto event = input.Take(opening);
        if (!event.has_value())
            return false;
        actual.push_back(event.value());
    }
    return EventVectorsEqual(actual, expected);
}

void CheckCapturedDescriptorAndPointSequence()
{
    auto inputOwner = std::make_unique<HostLassoInput>();
    auto& input = *inputOwner;
    const auto context = ReferenceContext(LassoKind::Freehand, SelectionMode::Add);
    constexpr std::uint64_t opening = 41;
    const bool began = input.Begin(opening, context, {2.25, -3.5});
    Expect(began, "begin.freehand-fixture-admitted");
    if (!began)
        return;

    const auto* captured = input.CapturedContext(opening);
    Expect(captured != nullptr && ContextEquals(captured == nullptr ? HostLassoInput::Context{} : *captured, context),
           "descriptor.captures-document-active-crop-ordinal-view-kind-mode-and-mask");
    const bool firstPoint = input.Point(opening, {8.5, 13.25}) == HostLassoInput::Admission::Recorded;
    const bool secondPoint = input.Point(opening, {-4.0, 27.75}) == HostLassoInput::Admission::Recorded;
    Expect(firstPoint && secondPoint, "point.accepts-literal-document-coordinates-in-order");
    Expect(input.PendingCount() == 3 && input.Sampling(), "point.queue-count-includes-down-and-keeps-sampling-open");

    const std::vector<ExpectedEvent> expected{
        {HostLassoInput::Action::Down, {{2.25, -3.5}, HostLassoInput::Operation::Point}},
        {HostLassoInput::Action::Move, {{8.5, 13.25}, HostLassoInput::Operation::Point}},
        {HostLassoInput::Action::Move, {{-4.0, 27.75}, HostLassoInput::Operation::Point}}};
    const auto peeked = input.Peek(opening);
    Expect(peeked.has_value() && EventEquals(peeked.value(), expected.front()) && input.PendingCount() == 3,
           "peek-observes-first-point-without-consuming-it");
    Expect(TakeExpectedSequence(input, opening, expected), "take-replays-independent-down-and-point-vector");

    const bool terminalRecorded = input.Finish(opening, false) == HostLassoInput::Admission::Recorded;
    Expect(terminalRecorded && !input.Sampling(), "finish-closes-physical-sampling");
    const ExpectedEvent terminal{HostLassoInput::Action::Up,
                                 {{}, HostLassoInput::Operation::Finish, false, false, false, false}};
    const auto terminalPeek = input.Peek(opening);
    Expect(terminalPeek.has_value() && EventEquals(terminalPeek.value(), terminal),
           "physical-up-retains-explicit-non-antialiased-terminal");
    const auto terminalTaken = input.Take(opening);
    Expect(terminalTaken.has_value() && EventEquals(terminalTaken.value(), terminal),
           "terminal-up-is-taken-after-all-moves");
    Expect(input.Busy() && input.Acknowledge(opening) && !input.Busy(),
           "terminal-consumption-keeps-busy-until-owner-acknowledges");
}

void CheckPolygonCursorClearingAndBackspace()
{
    auto inputOwner = std::make_unique<HostLassoInput>();
    auto& input = *inputOwner;
    const auto context = ReferenceContext(LassoKind::Polygonal, SelectionMode::Subtract);
    constexpr std::uint64_t opening = 52;
    const bool began = input.Begin(opening, context, {10, 20});
    Expect(began, "cursor.fixture-begins-polygonal-lasso");
    if (!began)
        return;

    const bool cursorSet = input.Cursor(opening, Vec2{30.5, 40.25}) == HostLassoInput::Admission::Recorded;
    const bool cursorCleared = input.Cursor(opening, std::nullopt) == HostLassoInput::Admission::Recorded;
    const bool vertexRemoved = input.Backspace(opening) == HostLassoInput::Admission::Recorded;
    Expect(cursorSet && cursorCleared && vertexRemoved, "cursor-clear-and-backspace-are-recorded");

    const std::vector<ExpectedEvent> expected{
        {HostLassoInput::Action::Down, {{10, 20}, HostLassoInput::Operation::Point}},
        {HostLassoInput::Action::Move, {{30.5, 40.25}, HostLassoInput::Operation::Cursor, true}},
        {HostLassoInput::Action::Move, {{}, HostLassoInput::Operation::Cursor, false}},
        {HostLassoInput::Action::Move, {{}, HostLassoInput::Operation::Backspace}}};
    Expect(TakeExpectedSequence(input, opening, expected), "cursor-clear-and-backspace-match-independent-event-vector");
    const bool finished = input.Finish(opening, true) == HostLassoInput::Admission::Recorded;
    const ExpectedEvent terminal{HostLassoInput::Action::Up,
                                 {{}, HostLassoInput::Operation::Finish, false, false, false, true}};
    const auto actual = input.Take(opening);
    Expect(finished && actual.has_value() && EventEquals(actual.value(), terminal),
           "physical-up-preserves-antialiasing-request");
    Expect(input.Acknowledge(opening), "polygonal-terminal-acknowledges-after-take");
}

void CheckMarqueeSquareAndCenterDescriptors()
{
    auto inputOwner = std::make_unique<HostLassoInput>();
    auto& input = *inputOwner;
    const auto context = ReferenceContext(LassoKind::Rectangle, SelectionMode::Subtract);
    constexpr std::uint64_t opening = 63;
    const bool began = input.Begin(opening, context, {17, 19});
    Expect(began, "marquee.fixture-begins-rectangle-subtract");
    if (!began)
        return;

    const bool square = input.Marquee(opening, {32.5, 44.25}, true, false) == HostLassoInput::Admission::Recorded;
    const bool center = input.Marquee(opening, {48.75, 63.5}, false, true) == HostLassoInput::Admission::Recorded;
    Expect(square && center, "marquee-records-square-and-from-center-modifiers");
    const std::vector<ExpectedEvent> expected{
        {HostLassoInput::Action::Down, {{17, 19}, HostLassoInput::Operation::Point}},
        {HostLassoInput::Action::Move, {{32.5, 44.25}, HostLassoInput::Operation::Marquee, false, true, false}},
        {HostLassoInput::Action::Move, {{48.75, 63.5}, HostLassoInput::Operation::Marquee, false, false, true}}};
    Expect(TakeExpectedSequence(input, opening, expected), "marquee-events-match-independent-modifier-descriptors");
    const bool finishRecorded = input.Finish(opening, true) == HostLassoInput::Admission::Recorded;
    const ExpectedEvent terminal{HostLassoInput::Action::Up,
                                 {{}, HostLassoInput::Operation::Finish, false, false, false, true}};
    const auto actualTerminal = input.Take(opening);
    Expect(finishRecorded && actualTerminal.has_value() && EventEquals(actualTerminal.value(), terminal),
           "marquee-physical-terminal-is-up-with-antialiasing");
    Expect(input.Acknowledge(opening), "marquee-terminal-acknowledges");
}

void CheckFullRingPhysicalUp()
{
    auto inputOwner = std::make_unique<HostLassoInput>();
    auto& input = *inputOwner;
    const auto context = ReferenceContext(LassoKind::Freehand, SelectionMode::New);
    constexpr std::uint64_t opening = 73;
    constexpr std::size_t ringSlots = 512;
    const bool began = input.Begin(opening, context, {-12.0, 7.0});
    Expect(began, "full-up.fixture-begins-before-filling-fixed-ring");
    if (!began)
        return;

    std::vector<ExpectedEvent> expectedPrefix;
    expectedPrefix.reserve(ringSlots);
    expectedPrefix.push_back({HostLassoInput::Action::Down, {{-12.0, 7.0}, HostLassoInput::Operation::Point}});
    bool allAccepted = true;
    for (std::size_t index = 0; index < ringSlots - 1; ++index)
    {
        const Vec2 point{500.0 + static_cast<double>(index), 700.0 - static_cast<double>(index) * 0.25};
        allAccepted = allAccepted && input.Point(opening, point) == HostLassoInput::Admission::Recorded;
        expectedPrefix.push_back({HostLassoInput::Action::Move, {point, HostLassoInput::Operation::Point}});
    }
    Expect(allAccepted && input.PendingCount() == ringSlots,
           "full-up-fills-all-512-event-slots-with-down-and-accepted-moves");
    const bool ended = input.Finish(opening, false) == HostLassoInput::Admission::Recorded;
    Expect(ended && input.PendingCount() == ringSlots + 1 && !input.Sampling(),
           "physical-up-records-in-separate-terminal-slot-after-full-ring");

    std::vector<HostLassoInput::Event> actualPrefix;
    actualPrefix.reserve(ringSlots);
    for (std::size_t index = 0; index < ringSlots; ++index)
    {
        const auto event = input.Take(opening);
        if (!event.has_value())
            break;
        actualPrefix.push_back(event.value());
    }
    Expect(EventVectorsEqual(actualPrefix, expectedPrefix), "full-up-drain-preserves-all-512-ring-events-in-order");
    const ExpectedEvent physicalUp{HostLassoInput::Action::Up,
                                   {{}, HostLassoInput::Operation::Finish, false, false, false, false}};
    const auto terminal = input.Take(opening);
    Expect(terminal.has_value() && EventEquals(terminal.value(), physicalUp),
           "full-ring-terminal-is-physical-up-with-explicit-antialiasing-state");
    Expect(input.Acknowledge(opening) && !input.Busy(), "full-ring-up-clears-after-terminal-acknowledgement");
}

void CheckSeparateTerminalAndFullRingOverflow()
{
    auto inputOwner = std::make_unique<HostLassoInput>();
    auto& input = *inputOwner;
    const auto context = ReferenceContext(LassoKind::Freehand, SelectionMode::New);
    constexpr std::uint64_t opening = 74;
    const bool began = input.Begin(opening, context, {-12.0, 7.0});
    Expect(began, "overflow.fixture-begins-before-filling-fixed-ring");
    if (!began)
        return;

    constexpr std::size_t ringSlots = 512;
    std::vector<ExpectedEvent> expectedPrefix;
    expectedPrefix.reserve(ringSlots);
    expectedPrefix.push_back({HostLassoInput::Action::Down, {{-12.0, 7.0}, HostLassoInput::Operation::Point}});
    bool allAccepted = true;
    for (std::size_t index = 0; index < ringSlots - 1; ++index)
    {
        const Vec2 point{1000.0 + static_cast<double>(index) * 0.5, -2000.0 + static_cast<double>(index) * 1.25};
        const auto admission = input.Point(opening, point);
        allAccepted = allAccepted && admission == HostLassoInput::Admission::Recorded;
        expectedPrefix.push_back({HostLassoInput::Action::Move, {point, HostLassoInput::Operation::Point}});
    }
    Expect(allAccepted && input.PendingCount() == ringSlots,
           "overflow-accepts-literal-prefix-until-all-512-ring-slots-are-full");

    const Vec2 refusedPoint{9000.25, -8000.5};
    const auto overflow = input.Point(opening, refusedPoint);
    Expect(overflow == HostLassoInput::Admission::Overflow && input.Overflowed(),
           "overflow-refuses-next-point-and-records-pointer-loss");
    Expect(input.PendingCount() == ringSlots + 1 && !input.Sampling() && input.Busy(),
           "lost-terminal-uses-separate-slot-after-full-ring");
    Expect(input.Finish(opening, true) == HostLassoInput::Admission::Closed,
           "later-physical-finish-is-closed-after-overflow-loss");

    std::vector<HostLassoInput::Event> actualPrefix;
    actualPrefix.reserve(ringSlots);
    for (std::size_t index = 0; index < ringSlots; ++index)
    {
        const auto event = input.Take(opening);
        if (!event.has_value())
            break;
        actualPrefix.push_back(event.value());
    }
    Expect(EventVectorsEqual(actualPrefix, expectedPrefix),
           "overflow-retains-every-accepted-prefix-event-literally-and-in-order");
    const ExpectedEvent lost{HostLassoInput::Action::Lost,
                             {expectedPrefix.back().sample.point, HostLassoInput::Operation::Point}};
    const auto lostPeek = input.Peek(opening);
    Expect(lostPeek.has_value() && EventEquals(lostPeek.value(), lost),
           "overflow-lost-terminal-identifies-last-accepted-point-not-refused-point");
    const auto lostTake = input.Take(opening);
    Expect(lostTake.has_value() && EventEquals(lostTake.value(), lost),
           "overflow-terminal-remains-takeable-after-ring-drain");
    Expect(input.Acknowledge(opening) && !input.Busy() && !input.Overflowed(),
           "overflow-storage-clears-only-after-terminal-acknowledgement");
}

void CheckStaleMissingAndOpeningAuthority()
{
    auto missingOwner = std::make_unique<HostLassoInput>();
    auto& missing = *missingOwner;
    Expect(missing.Point(1, {1, 2}) == HostLassoInput::Admission::Stale &&
               missing.Cursor(1, std::nullopt) == HostLassoInput::Admission::Stale &&
               missing.Finish(1, true) == HostLassoInput::Admission::Stale && !missing.Peek(1).has_value() &&
               !missing.Take(1).has_value() && !missing.Acknowledge(1) && !missing.Drop(1) && !missing.Busy(),
           "missing-opening-refuses-all-reads-writes-and-terminal-controls");

    auto inputOwner = std::make_unique<HostLassoInput>();
    auto& input = *inputOwner;
    const auto context = ReferenceContext(LassoKind::Freehand, SelectionMode::New);
    const bool began = input.Begin(85, context, {3, 4});
    Expect(began, "stale.fixture-begins-on-original-opening");
    if (!began)
        return;
    Expect(input.Point(86, {5, 6}) == HostLassoInput::Admission::Stale &&
               input.Cursor(86, std::nullopt) == HostLassoInput::Admission::Stale &&
               input.Finish(86, false) == HostLassoInput::Admission::Stale && input.CapturedContext(86) == nullptr &&
               input.PendingCount() == 1,
           "stale-opening-cannot-mutate-or-read-current-gesture");
    Expect(input.Drop(85) && !input.Busy() && input.Point(85, {7, 8}) == HostLassoInput::Admission::Stale,
           "drop-clears-current-opening-and-turns-late-samples-stale");

    auto zeroOpeningOwner = std::make_unique<HostLassoInput>();
    auto& zeroOpening = *zeroOpeningOwner;
    Expect(!zeroOpening.Begin(0, context, {1, 1}) && !zeroOpening.Busy() && zeroOpening.Begin(1, context, {1, 1}) &&
               zeroOpening.Opening() == 1,
           "zero-opening-refusal-does-not-consume-first-valid-opening");
    Expect(zeroOpening.Drop(1), "zero-opening-fixture-drops-valid-follow-up");

    auto maximumOpeningOwner = std::make_unique<HostLassoInput>();
    auto& maximumOpening = *maximumOpeningOwner;
    constexpr std::uint64_t maximum = std::numeric_limits<std::uint64_t>::max();
    const bool maximumBegan = maximumOpening.Begin(maximum, context, {9, 10});
    Expect(maximumBegan, "maximum-opening-is-admitted-once");
    const bool maximumRefusedReuse = maximumBegan && maximumOpening.Drop(maximum) &&
                                     !maximumOpening.Begin(1, context, {11, 12}) &&
                                     !maximumOpening.Begin(maximum, context, {13, 14}) && !maximumOpening.Busy();
    Expect(maximumRefusedReuse, "opening-counter-maximum-never-wraps-or-reuses-after-drop");
}

void CheckRefusalDoesNotConsumeOpeningAndReuseIsRejected()
{
    const auto valid = ReferenceContext(LassoKind::Freehand, SelectionMode::New);
    auto invalidModeOwner = std::make_unique<HostLassoInput>();
    auto& invalidMode = *invalidModeOwner;
    auto badMode = valid;
    badMode.mode = static_cast<SelectionMode>(255);
    Expect(!invalidMode.Begin(1, badMode, {1, 2}) && !invalidMode.Busy() && invalidMode.Begin(1, valid, {3, 4}),
           "invalid-mode-refusal-does-not-consume-opening");
    Expect(invalidMode.Drop(1), "invalid-mode-valid-follow-up-cleans-up");

    auto invalidKindOwner = std::make_unique<HostLassoInput>();
    auto& invalidKind = *invalidKindOwner;
    auto badKind = valid;
    badKind.kind = static_cast<LassoKind>(255);
    Expect(!invalidKind.Begin(1, badKind, {1, 2}) && !invalidKind.Busy() && invalidKind.Begin(1, valid, {3, 4}),
           "invalid-kind-refusal-does-not-consume-opening");
    Expect(invalidKind.Drop(1), "invalid-kind-valid-follow-up-cleans-up");

    auto invalidDescriptorsOwner = std::make_unique<HostLassoInput>();
    auto& invalidDescriptors = *invalidDescriptorsOwner;
    auto noOrdinal = valid;
    noOrdinal.ordinal = 0;
    auto noDocument = valid;
    noDocument.document.documentSerial = 0;
    auto noView = valid;
    noView.viewEpoch = 0;
    auto noWidth = valid;
    noWidth.document.canvasWidth = 0;
    auto noHeight = valid;
    noHeight.document.canvasHeight = -1;
    Expect(!invalidDescriptors.Begin(1, noOrdinal, {1, 2}) && !invalidDescriptors.Begin(1, noDocument, {1, 2}) &&
               !invalidDescriptors.Begin(1, noView, {1, 2}) && !invalidDescriptors.Begin(1, noWidth, {1, 2}) &&
               !invalidDescriptors.Begin(1, noHeight, {1, 2}) && !invalidDescriptors.Busy() &&
               invalidDescriptors.Begin(1, valid, {3, 4}),
           "missing-document-view-or-ordinal-refusals-do-not-consume-opening");
    Expect(invalidDescriptors.Drop(1), "descriptor-valid-follow-up-cleans-up");

    auto nonfiniteDownOwner = std::make_unique<HostLassoInput>();
    auto& nonfiniteDown = *nonfiniteDownOwner;
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double infinity = std::numeric_limits<double>::infinity();
    Expect(!nonfiniteDown.Begin(1, valid, {nan, 0}) && !nonfiniteDown.Begin(1, valid, {0, infinity}) &&
               !nonfiniteDown.Busy() && nonfiniteDown.Begin(1, valid, {3, 4}),
           "nonfinite-opening-points-refuse-without-consuming-opening");
    Expect(nonfiniteDown.Drop(1), "finite-opening-after-nonfinite-refusals-cleans-up");

    auto reusedOwner = std::make_unique<HostLassoInput>();
    auto& reused = *reusedOwner;
    const bool began = reused.Begin(9, valid, {10, 11});
    Expect(began, "reuse.fixture-begins-first-opening");
    if (!began)
        return;
    const bool finished = reused.Finish(9, true) == HostLassoInput::Admission::Recorded;
    const auto down = reused.Take(9);
    const auto up = reused.Take(9);
    const bool acknowledged = reused.Acknowledge(9);
    Expect(finished && down.has_value() && up.has_value() && acknowledged && !reused.Busy(),
           "reuse.fixture-completes-and-acknowledges-first-opening");
    Expect(!reused.Begin(9, valid, {12, 13}) && reused.Begin(10, valid, {14, 15}),
           "reused-begin-opening-refuses-while-later-monotonic-opening-succeeds");
    Expect(reused.Drop(10), "reused-opening-follow-up-cleans-up");
}

void CheckNonfiniteAndWrongOperationRefusals()
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double infinity = std::numeric_limits<double>::infinity();

    auto freehandOwner = std::make_unique<HostLassoInput>();
    auto& freehand = *freehandOwner;
    const auto freehandContext = ReferenceContext(LassoKind::Freehand, SelectionMode::New);
    const bool freehandBegan = freehand.Begin(21, freehandContext, {1, 2});
    Expect(freehandBegan, "wrong-operation.freehand-fixture-begins");
    if (freehandBegan)
    {
        Expect(freehand.Point(21, {nan, 3}) == HostLassoInput::Admission::Invalid &&
                   freehand.Point(21, {4, infinity}) == HostLassoInput::Admission::Invalid &&
                   freehand.Cursor(21, Vec2{5, 6}) == HostLassoInput::Admission::Invalid &&
                   freehand.Marquee(21, {7, 8}, false, false) == HostLassoInput::Admission::Invalid &&
                   freehand.Backspace(21) == HostLassoInput::Admission::Invalid && freehand.PendingCount() == 1,
               "freehand-rejects-nonfinite-and-foreign-operation-samples-without-mutation");
        Expect(freehand.Drop(21), "freehand-refusal-fixture-drops");
    }

    auto polygonOwner = std::make_unique<HostLassoInput>();
    auto& polygon = *polygonOwner;
    const auto polygonContext = ReferenceContext(LassoKind::Polygonal, SelectionMode::Add);
    const bool polygonBegan = polygon.Begin(22, polygonContext, {1, 2});
    Expect(polygonBegan, "wrong-operation.polygon-fixture-begins");
    if (polygonBegan)
    {
        Expect(polygon.Cursor(22, Vec2{nan, 1}) == HostLassoInput::Admission::Invalid &&
                   polygon.Marquee(22, {3, 4}, true, true) == HostLassoInput::Admission::Invalid &&
                   polygon.PendingCount() == 1,
               "polygon-rejects-nonfinite-cursor-and-marquee-operation");
        Expect(polygon.Drop(22), "polygon-refusal-fixture-drops");
    }

    auto rectangleOwner = std::make_unique<HostLassoInput>();
    auto& rectangle = *rectangleOwner;
    const auto rectangleContext = ReferenceContext(LassoKind::Rectangle, SelectionMode::Subtract);
    const bool rectangleBegan = rectangle.Begin(23, rectangleContext, {1, 2});
    Expect(rectangleBegan, "wrong-operation.rectangle-fixture-begins");
    if (rectangleBegan)
    {
        Expect(rectangle.Point(23, {3, 4}) == HostLassoInput::Admission::Invalid &&
                   rectangle.Cursor(23, std::nullopt) == HostLassoInput::Admission::Invalid &&
                   rectangle.Backspace(23) == HostLassoInput::Admission::Invalid &&
                   rectangle.Marquee(23, {nan, 5}, false, false) == HostLassoInput::Admission::Invalid &&
                   rectangle.PendingCount() == 1,
               "rectangle-rejects-point-cursor-backspace-and-nonfinite-marquee");
        Expect(rectangle.Drop(23), "rectangle-refusal-fixture-drops");
    }

    auto ellipseOwner = std::make_unique<HostLassoInput>();
    auto& ellipse = *ellipseOwner;
    const auto ellipseContext = ReferenceContext(LassoKind::Ellipse, SelectionMode::New);
    const bool ellipseBegan = ellipse.Begin(24, ellipseContext, {2, 3});
    Expect(ellipseBegan, "wrong-operation.ellipse-fixture-begins");
    if (ellipseBegan)
    {
        Expect(ellipse.Marquee(24, {4, infinity}, true, false) == HostLassoInput::Admission::Invalid &&
                   ellipse.Point(24, {5, 6}) == HostLassoInput::Admission::Invalid && ellipse.PendingCount() == 1,
               "ellipse-rejects-nonfinite-marquee-and-point-operation");
        Expect(ellipse.Drop(24), "ellipse-refusal-fixture-drops");
    }
}

void CheckBoundedTakePeekAcknowledgeDropAndBusy()
{
    auto inputOwner = std::make_unique<HostLassoInput>();
    auto& input = *inputOwner;
    const auto context = ReferenceContext(LassoKind::Polygonal, SelectionMode::New);
    const bool began = input.Begin(31, context, {8, 9});
    Expect(began, "lifetime.fixture-begins-before-terminal");
    if (!began)
        return;
    const ExpectedEvent down{HostLassoInput::Action::Down, {{8, 9}, HostLassoInput::Operation::Point}};
    const auto peek = input.Peek(31);
    Expect(peek.has_value() && EventEquals(peek.value(), down) && input.PendingCount() == 1 && input.Busy(),
           "peek-is-bounded-and-does-not-consume-down");
    Expect(!input.Acknowledge(31) && input.Busy(), "acknowledge-before-terminal-refuses-and-stays-busy");
    const auto takenDown = input.Take(31);
    Expect(takenDown.has_value() && EventEquals(takenDown.value(), down) && input.PendingCount() == 0 && input.Busy() &&
               !input.Peek(31).has_value(),
           "take-removes-only-queued-down-while-opening-remains-busy");
    Expect(input.Finish(31, true) == HostLassoInput::Admission::Recorded && !input.Sampling(),
           "terminal-arrives-after-queued-events-drain");
    const ExpectedEvent up{HostLassoInput::Action::Up,
                           {{}, HostLassoInput::Operation::Finish, false, false, false, true}};
    const auto peekUp = input.Peek(31);
    Expect(peekUp.has_value() && EventEquals(peekUp.value(), up) && input.Busy(),
           "peek-observes-separate-terminal-without-clearing-busy");
    const auto takeUp = input.Take(31);
    Expect(takeUp.has_value() && EventEquals(takeUp.value(), up) && input.Busy(),
           "taking-terminal-does-not-acknowledge-gesture");
    Expect(!input.Begin(32, context, {10, 11}) && input.Busy(),
           "next-begin-is-busy-until-terminal-result-is-acknowledged");
    Expect(input.Acknowledge(31) && !input.Busy() && input.PendingCount() == 0,
           "acknowledgement-releases-terminal-and-opens-buffer");
    Expect(input.Begin(32, context, {10, 11}) && input.Busy(), "later-opening-begins-after-acknowledgement");
    const bool queued = input.Point(32, {12, 13}) == HostLassoInput::Admission::Recorded;
    Expect(queued && input.Drop(32) && !input.Busy() && input.PendingCount() == 0 && !input.Peek(32).has_value() &&
               input.Point(32, {14, 15}) == HostLassoInput::Admission::Stale,
           "drop-discards-pending-events-and-refuses-late-owner-samples");
}

void CheckNothrowOwnerSurface()
{
    static_assert(std::is_nothrow_default_constructible_v<HostLassoInput>);
    static_assert(noexcept(std::declval<HostLassoInput&>().Begin(
        std::declval<std::uint64_t>(), std::declval<HostLassoInput::Context>(), std::declval<Vec2>())));
    static_assert(noexcept(std::declval<HostLassoInput&>().Point(std::declval<std::uint64_t>(), std::declval<Vec2>())));
    static_assert(noexcept(
        std::declval<HostLassoInput&>().Cursor(std::declval<std::uint64_t>(), std::declval<std::optional<Vec2>>())));
    static_assert(noexcept(
        std::declval<HostLassoInput&>().Marquee(std::declval<std::uint64_t>(), std::declval<Vec2>(), true, true)));
    static_assert(noexcept(std::declval<HostLassoInput&>().Backspace(std::declval<std::uint64_t>())));
    static_assert(noexcept(std::declval<HostLassoInput&>().Finish(std::declval<std::uint64_t>(), true, false)));
    static_assert(noexcept(std::declval<HostLassoInput&>().Take(std::declval<std::uint64_t>())));
    static_assert(noexcept(std::declval<const HostLassoInput&>().Peek(std::declval<std::uint64_t>())));
    static_assert(noexcept(std::declval<HostLassoInput&>().Acknowledge(std::declval<std::uint64_t>())));
    static_assert(noexcept(std::declval<HostLassoInput&>().Drop(std::declval<std::uint64_t>())));

    auto inputOwner = std::make_unique<HostLassoInput>();
    auto& input = *inputOwner;
    const auto context = ReferenceContext(LassoKind::Freehand, SelectionMode::New);
    const bool began = input.Begin(111, context, {5, 7});
    const bool moved = began && input.Point(111, {9, 13}) == HostLassoInput::Admission::Recorded;
    Expect(began && moved && input.PendingCount() == 2, "nothrow-owner-surface-records-real-events");
    Expect(input.Drop(111), "nothrow-fixture-drops-pending-events");
}
} // namespace

int RunHostLassoInputTests()
{
    gAssertions = 0;
    gFailures = 0;
    CheckCapturedDescriptorAndPointSequence();
    CheckPolygonCursorClearingAndBackspace();
    CheckMarqueeSquareAndCenterDescriptors();
    CheckFullRingPhysicalUp();
    CheckSeparateTerminalAndFullRingOverflow();
    CheckStaleMissingAndOpeningAuthority();
    CheckRefusalDoesNotConsumeOpeningAndReuseIsRejected();
    CheckNonfiniteAndWrongOperationRefusals();
    CheckBoundedTakePeekAcknowledgeDropAndBusy();
    CheckNothrowOwnerSurface();
    std::printf("%s host-lasso-input.summary checks=%d failures=%d\n", gFailures == 0 ? "PASS" : "FAIL", gAssertions,
                gFailures);
    return gFailures;
}
