#include "Host.Tests.HueInput.h"

#include "RedPrism/HostHueInput.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <limits>
#include <memory>
#include <type_traits>

namespace
{
using Input = HostHueInput;
using Event = Input::Event;
using Action = Input::Action;
using Admission = Input::Admission;
using Context = Input::Context;
using Sample = Input::Sample;

int gFailures = 0;
int gChecks = 0;

void Expect(bool condition, const char* label) noexcept
{
    std::printf("%s host-hue-input.%s\n", condition ? "PASS" : "FAIL", label);
    gFailures += condition ? 0 : 1;
    ++gChecks;
}

Context ValidContext() noexcept
{
    Context context;
    context.document.documentSerial = 7001;
    context.document.generation = 22;
    context.document.restoreSerial = 9;
    context.document.canvasWidth = 200;
    context.document.canvasHeight = 100;
    context.panelOpening = 81;
    context.ordinal = 300;
    context.documentPoint = Vec2{24.25, 37.5};
    context.startViewX = 120.0;
    context.purpose = Input::Purpose::Targeting;
    context.mode = HueSampleMode::Add;
    return context;
}

bool SameSample(const Sample& left, const Sample& right) noexcept
{
    return left.viewDelta == right.viewDelta && left.ordinal == right.ordinal && left.adjustsHue == right.adjustsHue;
}

bool SameEvent(const std::optional<Event>& actual, Action action, const Sample& sample) noexcept
{
    return actual.has_value() && actual.value().action == action && SameSample(actual.value().sample, sample);
}

// Independent expected trace: literal expected events are kept in a deque. This model has no calls into
// HostHueInput and represents only the scenario's intended FIFO and terminal phase.
struct ExpectedTrace final
{
    enum class Phase : std::uint8_t
    {
        Sampling,
        TerminalPending,
        AwaitingAcknowledgement,
        Idle,
    };

    std::uint64_t opening = 0;
    Context context{};
    std::deque<Event> events;
    Phase phase = Phase::Sampling;
    bool overflow = false;

    ExpectedTrace(std::uint64_t openingValue, const Context& contextValue)
        : opening(openingValue), context(contextValue)
    {
        events.push_back(Event{Action::Down, Sample{0, context.ordinal, false}});
    }

    void Move(const Sample& sample)
    {
        events.push_back(Event{Action::Move, sample});
    }

    void Terminal(Action action, const Sample& sample, bool wasOverflow = false)
    {
        events.push_back(Event{action, sample});
        phase = Phase::TerminalPending;
        overflow = wasOverflow;
    }

    [[nodiscard]] bool StateMatches(const Input& input) const noexcept
    {
        const bool expectedBusy = phase != Phase::Idle;
        const bool expectedSampling = phase == Phase::Sampling;
        return input.Busy() == expectedBusy && input.Sampling() == expectedSampling &&
               input.Opening() == (expectedBusy ? opening : 0) && input.PendingCount() == events.size() &&
               input.Overflowed() == overflow;
    }

    [[nodiscard]] bool TakeAllMatches(Input& input)
    {
        bool matches = true;
        while (!events.empty())
        {
            const Event expected = events.front();
            const std::optional<Event> actual = input.Take(opening);
            matches = matches && SameEvent(actual, expected.action, expected.sample);
            events.pop_front();
        }
        matches = matches && !input.Take(opening).has_value();
        if (phase == Phase::TerminalPending)
            phase = Phase::AwaitingAcknowledgement;
        return matches;
    }

    void Acknowledge()
    {
        phase = Phase::Idle;
        opening = 0;
        overflow = false;
    }
};

bool ContextEquals(const Context& actual, const Context& expected) noexcept
{
    return actual.document == expected.document && actual.panelOpening == expected.panelOpening &&
           actual.ordinal == expected.ordinal && actual.documentPoint == expected.documentPoint &&
           actual.startViewX == expected.startViewX && actual.purpose == expected.purpose &&
           actual.mode == expected.mode;
}

void InvalidBeginCasesDoNotOpen()
{
    static_assert(std::is_trivially_copyable_v<Context>);
    static_assert(std::is_trivially_destructible_v<Context>);
    static_assert(std::is_trivially_copyable_v<Sample>);
    static_assert(std::is_trivially_destructible_v<Sample>);

    const auto refuses = [](const Context& context, std::uint64_t opening)
    {
        auto input = std::make_unique<Input>();
        return !input->Begin(opening, context) && !input->Busy() && input->Opening() == 0 && input->PendingCount() == 0;
    };

    Context context = ValidContext();
    Context bad = context;
    bad.document.documentSerial = 0;
    Expect(refuses(bad, 1), "begin-rejects-missing-document-serial");
    bad = context;
    bad.document.canvasWidth = 0;
    bad.document.canvasHeight = -1;
    Expect(refuses(bad, 1), "begin-rejects-nonpositive-dimensions");
    bad = context;
    bad.documentPoint.x = -0.01;
    Expect(refuses(bad, 1), "begin-rejects-negative-point");
    bad = context;
    bad.documentPoint.y = static_cast<double>(bad.document.canvasHeight);
    Expect(refuses(bad, 1), "begin-rejects-point-at-canvas-edge");
    bad = context;
    bad.documentPoint.x = std::numeric_limits<double>::quiet_NaN();
    Expect(refuses(bad, 1), "begin-rejects-nonfinite-point");
    bad = context;
    bad.panelOpening = 0;
    Expect(refuses(bad, 1), "begin-rejects-zero-panel-opening");
    bad = context;
    bad.ordinal = 0;
    Expect(refuses(bad, 1), "begin-rejects-zero-event-ordinal");
    bad = context;
    bad.startViewX = std::numeric_limits<double>::infinity();
    Expect(refuses(bad, 1), "begin-rejects-nonfinite-view-origin");
    bad = context;
    bad.purpose = static_cast<Input::Purpose>(0xff);
    Expect(refuses(bad, 1), "begin-rejects-invalid-purpose");
    bad = context;
    bad.mode = static_cast<HueSampleMode>(0xff);
    Expect(refuses(bad, 1), "begin-rejects-invalid-mode");
    Expect(refuses(context, 0), "begin-rejects-zero-opening");

    auto retry = std::make_unique<Input>();
    bad = context;
    bad.mode = static_cast<HueSampleMode>(0xff);
    const bool invalidRefused = !retry->Begin(44, bad);
    const bool validSameOpeningAccepted = retry->Begin(44, context);
    Expect(invalidRefused && validSameOpeningAccepted && retry->Opening() == 44,
           "invalid-begin-does-not-consume-opening");
    Expect(retry->Drop(44), "invalid-begin-retry-cleanup");
}

void CapturedContextIsAnIndependentValueSnapshot()
{
    auto input = std::make_unique<Input>();
    Context submitted = ValidContext();
    const Context expected = submitted;
    constexpr std::uint64_t opening = 7; // The adapter stamp is independent from the context event ordinal.
    const bool began = input->Begin(opening, submitted);
    submitted.document.documentSerial = 0;
    submitted.document.canvasWidth = -1;
    submitted.panelOpening = 0;
    submitted.ordinal = 0;
    submitted.documentPoint = Vec2{-1, -1};
    submitted.startViewX = std::numeric_limits<double>::quiet_NaN();
    submitted.purpose = Input::Purpose::RangeSample;
    submitted.mode = HueSampleMode::Remove;
    const Context* const captured = input->CapturedContext(opening);
    const std::optional<Event> down = input->Peek(opening);
    Expect(began && captured != nullptr && ContextEquals(captured == nullptr ? Context{} : *captured, expected),
           "begin-retains-document-panel-point-purpose-and-mode-values");
    Expect(down.has_value() && down.value().action == Action::Down &&
               SameSample(down.value().sample, Sample{0, expected.ordinal, false}) &&
               input->CapturedContext(opening + 1) == nullptr,
           "down-uses-captured-ordinal-and-wrong-opening-has-no-context");
    Expect(input->Drop(opening), "context-snapshot-cleanup");
}

void TargetMoveAndFinishUseLastAcceptedMove()
{
    auto input = std::make_unique<Input>();
    Context context = ValidContext();
    context.ordinal = 100;
    context.startViewX = 10.0;
    constexpr std::uint64_t opening = 12;
    ExpectedTrace expected(opening, context);
    Expect(input->Begin(opening, context), "target-begin-records-down");
    Expect(input->Move(opening, 16.25, true, 101) == Admission::Recorded,
           "target-move-records-eventtime-delta-and-modifier");
    expected.Move(Sample{6.25, 101, true});
    Expect(input->Move(opening, 5.5, false, 102) == Admission::Recorded, "target-second-move-records-negative-delta");
    expected.Move(Sample{-4.5, 102, false});

    Expect(input->Finish(opening, 103) == Admission::Recorded, "finish-accepts-new-terminal-ordinal");
    expected.Terminal(Action::Up, Sample{-4.5, 103, false});
    Expect(expected.StateMatches(*input), "target-finish-keeps-terminal-in-separate-pending-slot");
    Expect(expected.TakeAllMatches(*input), "target-fifo-down-moves-up-uses-previous-move-values");
    Expect(input->Busy() && !input->Sampling() && input->PendingCount() == 0,
           "target-terminal-consumption-waits-for-acknowledgement");
    Expect(input->Acknowledge(opening) && !input->Busy(), "target-terminal-acknowledgement-releases-context");
}

void OrdinalRefusalsAndLateClosedCallsPreserveTerminal()
{
    auto input = std::make_unique<Input>();
    Context context = ValidContext();
    context.ordinal = 100;
    context.startViewX = 4.0;
    constexpr std::uint64_t opening = 20;
    ExpectedTrace expected(opening, context);
    Expect(input->Begin(opening, context), "ordinal-begin");

    const Admission accepted = input->Move(opening, 8.0, true, 101);
    expected.Move(Sample{4.0, 101, true});
    const Admission duplicate = input->Move(opening, 9.0, false, 101);
    const Admission staleOrdinal = input->Move(opening, 9.0, false, 100);
    const Admission zeroOrdinal = input->Move(opening, 9.0, false, 0);
    const Admission foreignOpening = input->Move(opening - 1, 9.0, false, 102);
    Expect(accepted == Admission::Recorded && duplicate == Admission::Invalid && staleOrdinal == Admission::Invalid &&
               zeroOrdinal == Admission::Invalid && foreignOpening == Admission::Stale && expected.StateMatches(*input),
           "duplicate-stale-zero-and-foreign-events-preserve-accepted-prefix");

    Expect(input->Finish(opening, 102) == Admission::Recorded, "ordinal-finish-records-new-ordinal");
    expected.Terminal(Action::Up, Sample{4.0, 102, true});
    const std::size_t beforeLateCalls = input->PendingCount();
    const Admission lateMove = input->Move(opening, 99.0, false, 0);
    const Admission duplicateFinish = input->Finish(opening, 0);
    const Admission staleFinish = input->Finish(opening - 1, 103);
    Expect(lateMove == Admission::Closed && duplicateFinish == Admission::Closed && staleFinish == Admission::Stale &&
               input->PendingCount() == beforeLateCalls && expected.StateMatches(*input),
           "closed-guard-precedes-ordinal-validation-and-retains-terminal");
    Expect(expected.TakeAllMatches(*input) && input->Acknowledge(opening),
           "terminal-retained-through-exact-drain-and-ack");
}

void InvalidTargetMovesPreservePrefix()
{
    auto input = std::make_unique<Input>();
    Context context = ValidContext();
    context.ordinal = 1;
    context.startViewX = (std::numeric_limits<double>::max)();
    constexpr std::uint64_t opening = 31;
    ExpectedTrace expected(opening, context);
    Expect(input->Begin(opening, context), "overflow-delta-begin");
    const Admission infiniteDelta = input->Move(opening, -(std::numeric_limits<double>::max)(), true, 2);
    const Admission infinitePosition = input->Move(opening, std::numeric_limits<double>::infinity(), true, 3);
    Expect(infiniteDelta == Admission::Invalid && infinitePosition == Admission::Invalid &&
               expected.StateMatches(*input),
           "target-move-refuses-nonfinite-position-and-subtraction-overflow");
    Expect(input->Move(opening, 0.0, true, 4) == Admission::Recorded, "finite-move-after-refusals-still-records");
    expected.Move(Sample{-(std::numeric_limits<double>::max)(), 4, true});
    Expect(expected.TakeAllMatches(*input), "invalid-target-moves-preserve-prefix-and-later-finite-value");
    Expect(input->Finish(opening, 5) == Admission::Recorded &&
               SameEvent(input->Take(opening), Action::Up, Sample{-(std::numeric_limits<double>::max)(), 5, true}),
           "finite-target-terminal-copies-last-move-and-owns-new-ordinal");
    Expect(input->Acknowledge(opening), "finite-target-terminal-ack");
}

void RangePurposeCannotProduceTargetMoves()
{
    auto input = std::make_unique<Input>();
    Context context = ValidContext();
    context.purpose = Input::Purpose::RangeSample;
    context.mode = HueSampleMode::Add;
    context.ordinal = 10;
    constexpr std::uint64_t opening = 40;
    ExpectedTrace expected(opening, context);
    Expect(input->Begin(opening, context), "range-purpose-begin");
    Expect(input->Move(opening, 130.0, false, 11) == Admission::Invalid && expected.StateMatches(*input) &&
               SameEvent(input->Peek(opening), Action::Down, Sample{0, 10, false}),
           "range-purpose-move-is-explicitly-refused");
    Expect(input->Finish(opening, 12) == Admission::Recorded &&
               SameEvent(input->Take(opening), Action::Down, Sample{0, 10, false}) &&
               SameEvent(input->Take(opening), Action::Up, Sample{0, 12, false}) && input->Acknowledge(opening),
           "range-purpose-terminal-retains-down-without-move");
}

void OpeningStampsAreMonotonicIncludingMaximum()
{
    auto input = std::make_unique<Input>();
    const Context first = ValidContext();
    Expect(input->Begin(5, first) && input->Drop(5), "opening-first-stamp-is-admitted-and-dropped");
    Expect(!input->Begin(5, first) && !input->Begin(4, first) && !input->Busy(),
           "opening-equal-and-retrograde-stamps-cannot-reopen");
    const Context next = ValidContext();
    Expect(input->Begin(6, next), "opening-newer-stamp-after-drop-is-accepted");
    Expect(input->Drop(6), "opening-newer-stamp-cleanup");

    auto maximumInput = std::make_unique<Input>();
    constexpr std::uint64_t maximum = (std::numeric_limits<std::uint64_t>::max)();
    Expect(maximumInput->Begin(maximum, first) && maximumInput->Opening() == maximum,
           "opening-uint64-maximum-is-accepted-once");
    Expect(maximumInput->Drop(maximum) && !maximumInput->Begin(maximum, first) && !maximumInput->Begin(1, first) &&
               !maximumInput->Busy(),
           "opening-maximum-cannot-repeat-or-wrap");
}

void OldDropStampCannotAffectNewOpening()
{
    auto input = std::make_unique<Input>();
    const Context oldContext = ValidContext();
    const Context newContext = ValidContext();
    constexpr std::uint64_t oldOpening = 70;
    constexpr std::uint64_t newOpening = 71;
    Expect(input->Begin(oldOpening, oldContext) && input->Drop(oldOpening), "drop-old-opening");
    Expect(input->Begin(newOpening, newContext), "drop-following-opening-begins");
    const bool oldDrop = input->Drop(oldOpening);
    const bool oldAck = input->Acknowledge(oldOpening);
    const Context* const current = input->CapturedContext(newOpening);
    Expect(!oldDrop && !oldAck && input->Busy() && input->Opening() == newOpening && current != nullptr &&
               ContextEquals(current == nullptr ? Context{} : *current, newContext) &&
               SameEvent(input->Peek(newOpening), Action::Down, Sample{0, newContext.ordinal, false}),
           "old-stamp-drop-and-ack-leave-new-context-and-down-intact");
    Expect(input->Drop(newOpening), "drop-current-opening-cleanup");
}

void FullMoveBufferCreatesLostAfterAccepted512EventPrefix()
{
    auto input = std::make_unique<Input>();
    Context context = ValidContext();
    context.ordinal = 1;
    context.startViewX = 100.0;
    constexpr std::uint64_t opening = 90;
    ExpectedTrace expected(opening, context);
    const bool began = input->Begin(opening, context);
    bool allMovesAccepted = true;
    Sample lastAccepted{0, 1, false};
    for (std::uint64_t index = 1; index <= 511; ++index)
    {
        const double delta = static_cast<double>(index) * 0.25;
        const std::uint64_t ordinal = index + 1;
        const bool adjustsHue = (index % 2) != 0;
        const bool moveRecorded =
            input->Move(opening, context.startViewX + delta, adjustsHue, ordinal) == Admission::Recorded;
        allMovesAccepted = allMovesAccepted && moveRecorded;
        const Sample sample{delta, ordinal, adjustsHue};
        expected.Move(sample);
        lastAccepted = sample;
    }
    Expect(began && allMovesAccepted && input->PendingCount() == 512, "capacity-admits-down-plus-511-ordered-moves");
    const Admission overflow = input->Move(opening, context.startViewX + 128.0, true, 513);
    expected.Terminal(Action::Lost, lastAccepted, true);
    const Admission lateUp = input->Finish(opening, 514);
    Expect(overflow == Admission::Overflow && lateUp == Admission::Closed && input->PendingCount() == 513 &&
               input->Overflowed() && expected.StateMatches(*input),
           "capacity-refuses-next-move-keeps-lost-terminal-and-closes-late-up");
    Expect(expected.TakeAllMatches(*input),
           "capacity-literal-formula-matches-all-512-prefix-events-and-lost-last-accepted-sample");
    expected.Acknowledge();
    Expect(input->Acknowledge(opening) && expected.StateMatches(*input),
           "capacity-lost-terminal-releases-only-after-consumption-and-ack");
}

void TerminalSlotSurvivesFullMoveRing()
{
    auto input = std::make_unique<Input>();
    Context context = ValidContext();
    context.ordinal = 1;
    context.startViewX = 50.0;
    constexpr std::uint64_t opening = 100;
    ExpectedTrace expected(opening, context);
    const bool began = input->Begin(opening, context);
    bool allMovesAccepted = true;
    Sample lastAccepted{0, 1, false};
    for (std::uint64_t index = 1; index <= 511; ++index)
    {
        const double delta = static_cast<double>(index) * 0.5;
        const std::uint64_t ordinal = index + 1;
        const bool adjustsHue = (index % 2) == 0;
        const bool moveRecorded =
            input->Move(opening, context.startViewX + delta, adjustsHue, ordinal) == Admission::Recorded;
        allMovesAccepted = allMovesAccepted && moveRecorded;
        lastAccepted = Sample{delta, ordinal, adjustsHue};
        expected.Move(lastAccepted);
    }
    Expect(began && allMovesAccepted && input->PendingCount() == Input::kCapacity,
           "terminal-slot-starts-with-full-move-ring");
    lastAccepted.ordinal = 513;
    Expect(input->Finish(opening, 513) == Admission::Recorded && !input->Overflowed(),
           "terminal-slot-admits-up-beside-full-ring-without-overflow");
    expected.Terminal(Action::Up, lastAccepted);
    Expect(input->PendingCount() == Input::kCapacity + 1 && expected.StateMatches(*input) &&
               expected.TakeAllMatches(*input),
           "terminal-slot-fifo-keeps-all-512-prefix-events-before-up");
    expected.Acknowledge();
    Expect(input->Acknowledge(opening) && expected.StateMatches(*input),
           "terminal-slot-acknowledges-after-up-consumption");
}

void AcknowledgementRequiresTerminalConsumption()
{
    auto input = std::make_unique<Input>();
    Context context = ValidContext();
    context.ordinal = 40;
    constexpr std::uint64_t opening = 110;
    ExpectedTrace expected(opening, context);
    Expect(input->Begin(opening, context), "ack-begin");
    Expect(input->Finish(opening, 41) == Admission::Recorded, "ack-terminal-admitted");
    expected.Terminal(Action::Up, Sample{0, 41, false});
    const bool ackBeforeAnyTake = input->Acknowledge(opening);
    const std::optional<Event> down = input->Take(opening);
    const bool ackBeforeTerminal = input->Acknowledge(opening);
    const std::optional<Event> up = input->Take(opening);
    Expect(!ackBeforeAnyTake && SameEvent(down, Action::Down, Sample{0, 40, false}) && !ackBeforeTerminal &&
               SameEvent(up, Action::Up, Sample{0, 41, false}),
           "ack-refuses-before-terminal-consumed-but-preserves-fifo");
    expected.events.clear();
    expected.phase = ExpectedTrace::Phase::AwaitingAcknowledgement;
    const bool acknowledged = input->Acknowledge(opening);
    expected.Acknowledge();
    Expect(acknowledged && expected.StateMatches(*input), "ack-releases-only-after-terminal-consumption");
    Expect(!input->Acknowledge(opening), "ack-duplicate-is-refused");
}

void NegativeModeCastIsRefused()
{
    auto input = std::make_unique<Input>();
    Context context = ValidContext();
    context.mode = static_cast<HueSampleMode>(-1);
    const bool refused = !input->Begin(700, context);
    Expect(refused && !input->Busy() && input->Opening() == 0 && input->PendingCount() == 0,
           "begin-rejects-negative-mode-cast");
}

void SharedEventOrdinalMaximumFormsExactTerminalFifo()
{
    constexpr std::uint64_t maximum = 18'446'744'073'709'551'615ull;
    constexpr std::uint64_t maximumMinusOne = 18'446'744'073'709'551'614ull;
    constexpr std::uint64_t maximumMinusTwo = 18'446'744'073'709'551'613ull;
    constexpr std::uint64_t opening = 701;

    auto input = std::make_unique<Input>();
    Context context = ValidContext();
    context.ordinal = maximumMinusTwo;
    context.startViewX = 100.0;
    ExpectedTrace expected(opening, context);
    const bool began = input->Begin(opening, context);
    Expect(began && SameEvent(input->Peek(opening), Action::Down, Sample{0.0, maximumMinusTwo, false}),
           "event-ordinal-max-minus-two-down-is-literal");

    const Admission move = input->Move(opening, 106.25, true, maximumMinusOne);
    expected.Move(Sample{6.25, maximumMinusOne, true});
    Expect(move == Admission::Recorded && expected.StateMatches(*input), "event-ordinal-max-minus-one-move-is-literal");

    const Admission finish = input->Finish(opening, maximum);
    expected.Terminal(Action::Up, Sample{6.25, maximum, true});
    Expect(finish == Admission::Recorded && expected.StateMatches(*input),
           "event-ordinal-max-finish-copies-last-move-with-new-ordinal");

    const std::size_t pendingBeforeLateCalls = input->PendingCount();
    const Admission lateMove = input->Move(opening, 999.0, false, 0);
    const Admission lateFinish = input->Finish(opening, 0);
    Expect(lateMove == Admission::Closed && lateFinish == Admission::Closed &&
               input->PendingCount() == pendingBeforeLateCalls && expected.StateMatches(*input),
           "event-ordinal-zero-after-max-terminal-is-closed-before-validation");

    const bool exactFifo = expected.TakeAllMatches(*input);
    const bool acknowledged = input->Acknowledge(opening);
    expected.Acknowledge();
    Expect(exactFifo && acknowledged && expected.StateMatches(*input),
           "event-ordinal-boundary-exact-down-move-up-fifo-and-ack");
}

void LiveMaximumEventOrdinalRefusesWrapWithoutChangingPrefix()
{
    constexpr std::uint64_t maximum = 18'446'744'073'709'551'615ull;
    constexpr std::uint64_t opening = 702;

    auto input = std::make_unique<Input>();
    Context context = ValidContext();
    context.ordinal = 900;
    context.startViewX = 40.0;
    ExpectedTrace expected(opening, context);
    const bool began = input->Begin(opening, context);
    const Admission maximumMove = input->Move(opening, 43.5, false, maximum);
    expected.Move(Sample{3.5, maximum, false});
    Expect(began && maximumMove == Admission::Recorded && expected.StateMatches(*input),
           "live-event-ordinal-max-move-is-accepted");

    const std::size_t pendingBeforeRefusals = input->PendingCount();
    const Admission wrappedMove = input->Move(opening, 44.0, true, 0);
    const Admission wrappedFinish = input->Finish(opening, 0);
    Expect(wrappedMove == Admission::Invalid && wrappedFinish == Admission::Invalid &&
               input->PendingCount() == pendingBeforeRefusals && expected.StateMatches(*input),
           "live-zero-after-max-move-refuses-without-prefix-mutation");

    const bool exactPrefix = expected.TakeAllMatches(*input);
    const bool callerDrop = input->Drop(opening);
    expected.Acknowledge();
    Expect(exactPrefix && callerDrop && expected.StateMatches(*input),
           "max-ordinal-prefix-fifo-then-caller-owned-drop");
}
} // namespace

int RunHostHueInputTests()
{
    gFailures = 0;
    gChecks = 0;
    InvalidBeginCasesDoNotOpen();
    CapturedContextIsAnIndependentValueSnapshot();
    TargetMoveAndFinishUseLastAcceptedMove();
    OrdinalRefusalsAndLateClosedCallsPreserveTerminal();
    InvalidTargetMovesPreservePrefix();
    RangePurposeCannotProduceTargetMoves();
    OpeningStampsAreMonotonicIncludingMaximum();
    OldDropStampCannotAffectNewOpening();
    FullMoveBufferCreatesLostAfterAccepted512EventPrefix();
    TerminalSlotSurvivesFullMoveRing();
    AcknowledgementRequiresTerminalConsumption();
    NegativeModeCastIsRefused();
    SharedEventOrdinalMaximumFormsExactTerminalFifo();
    LiveMaximumEventOrdinalRefusesWrapWithoutChangingPrefix();
    constexpr int kExpectedChecks = 70;
    std::printf("SUMMARY host-hue-input checks=%d failures=%d expected=%d\n", gChecks, gFailures, kExpectedChecks);
    return gFailures == 0 && gChecks == kExpectedChecks ? 0 : 1;
}
