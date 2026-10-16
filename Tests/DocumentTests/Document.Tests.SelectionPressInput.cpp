#include "Document.Tests.SelectionPressInput.h"

#include "HostSelectionPressInput.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <optional>

namespace
{
using Input = HostSelectionPressInput;
using Event = Input::Event;
using Action = Input::Action;
using Admission = Input::Admission;

int gAssertions = 0;
int gFailures = 0;

void Expect(bool condition, const char* label) noexcept
{
    ++gAssertions;
    std::printf("%s selection-press-input.%s\n", condition ? "PASS" : "FAIL", label);
    gFailures += condition ? 0 : 1;
}

DocumentReadAuthority Authority() noexcept
{
    DocumentReadAuthority value{};
    value.sessionIdentity.bytes[0] = 0x31;
    value.sessionIdentity.bytes[15] = 0xA7;
    value.documentSerial = 41;
    value.generation = 9;
    value.restoreSerial = 3;
    value.canvasWidth = 640;
    value.canvasHeight = 480;
    return value;
}

Input::Context Context() noexcept
{
    Input::Context value{};
    value.document = Authority();
    Guid active{};
    active.bytes[0] = 0x52;
    active.bytes[8] = 0xC4;
    value.active = active;
    value.crop = CropRect{4.5, 7.25, 300.0, 200.0};
    value.wand = WandSettings{73, WandSampleSize::FiveByFive, false, true};
    value.ordinal = 19;
    value.tool = Input::Tool::Wand;
    value.kind = LassoKind::Freehand;
    value.mode = SelectionMode::Add;
    value.mask = true;
    return value;
}

bool IsEvent(const std::optional<Event>& event, Action action, double x, double y, bool shift, bool control, bool alt,
             std::uint64_t ordinal, bool antialiased) noexcept
{
    return event.has_value() && event->action == action && event->sample.point.x == x && event->sample.point.y == y &&
           event->sample.modifiers.shift == shift && event->sample.modifiers.control == control &&
           event->sample.modifiers.alt == alt && event->sample.ordinal == ordinal &&
           event->sample.antialiased == antialiased;
}

bool HasCapturedContext(const Input::Context* value) noexcept
{
    if (value == nullptr)
        return false;
    Guid active{};
    active.bytes[0] = 0x52;
    active.bytes[8] = 0xC4;
    return value->document.documentSerial == 41 && value->document.generation == 9 &&
           value->document.restoreSerial == 3 && value->document.canvasWidth == 640 &&
           value->document.canvasHeight == 480 && value->document.sessionIdentity.bytes[0] == 0x31 &&
           value->document.sessionIdentity.bytes[15] == 0xA7 && value->active == active && value->crop.has_value() &&
           value->crop->x == 4.5 && value->crop->y == 7.25 && value->crop->width == 300.0 &&
           value->crop->height == 200.0 && value->wand.tolerance == 73 &&
           value->wand.sampleSize == WandSampleSize::FiveByFive && !value->wand.contiguous &&
           value->wand.sampleAllLayers && value->ordinal == 19 && value->tool == Input::Tool::Wand &&
           value->kind == LassoKind::Freehand && value->mode == SelectionMode::Add && value->mask;
}

void TestCapturedValuesAndAdmission() noexcept
{
    auto input = std::make_unique<Input>();
    auto context = Context();
    Input::Modifiers downModifiers{true, false, true};
    Expect(input->Begin(7, context, Vec2{12.25, -3.5}, downModifiers, false), "begin-captures-valid-wand-press");

    context.document.documentSerial = 999;
    context.active.reset();
    context.crop = CropRect{0, 0, 1, 1};
    context.wand = WandSettings{1, WandSampleSize::Point, true, false};
    context.ordinal = 999;
    context.tool = Input::Tool::Marquee;
    context.kind = LassoKind::Ellipse;
    context.mode = SelectionMode::Subtract;
    context.mask = false;
    downModifiers = {};
    Expect(HasCapturedContext(input->CapturedContext(7)), "captured-document-and-tool-settings-ignore-caller-edits");
    Expect(IsEvent(input->Take(7), Action::Down, 12.25, -3.5, true, false, true, 19, false),
           "down-retains-event-time-document-point-modifiers-ordinal-and-aa");

    Expect(input->Move(7, Vec2{20.5, 30.75}, Input::Modifiers{false, true, true}, true, 20) == Admission::Recorded,
           "strictly-new-move-ordinal-is-recorded");
    const auto beforeInvalid = input->PendingCount();
    Expect(input->Move(7, Vec2{40, 50}, Input::Modifiers{true, true, false}, false, 20) == Admission::Invalid &&
               input->PendingCount() == beforeInvalid,
           "nonincreasing-ordinal-refusal-preserves-accepted-prefix");
    Expect(input->Move(7, Vec2{std::numeric_limits<double>::infinity(), 2}, {}, false, 21) == Admission::Invalid &&
               input->PendingCount() == beforeInvalid,
           "nonfinite-coordinate-refusal-preserves-accepted-prefix");
    Expect(IsEvent(input->Take(7), Action::Move, 20.5, 30.75, false, true, true, 20, true),
           "accepted-move-retains-independent-event-time-values");

    Expect(input->Finish(7, false, 21) == Admission::Recorded, "up-terminal-recorded-after-last-move");
    Expect(IsEvent(input->Take(7), Action::Up, 20.5, 30.75, false, true, true, 21, false),
           "up-keeps-last-accepted-point-modifiers-and-release-aa");
    Expect(input->Busy() && !input->Sampling() && input->PendingCount() == 0 &&
               input->Move(7, Vec2{22, 23}, {}, true, 22) == Admission::Closed &&
               input->Finish(7, true, 22) == Admission::Closed,
           "drained-up-remains-busy-and-late-events-are-closed");
    Expect(!input->Begin(8, Context(), Vec2{1, 2}, {}, true) && input->Opening() == 7,
           "second-begin-refused-before-terminal-acknowledgement");
    Expect(input->Acknowledge(7) && !input->Busy() && input->Opening() == 0, "terminal-acknowledgement-releases-slot");
    Expect(!input->Acknowledge(7) && input->Begin(8, Context(), Vec2{1, 2}, {}, true) &&
               !input->Begin(8, Context(), Vec2{3, 4}, {}, true) && !input->Begin(7, Context(), Vec2{3, 4}, {}, true),
           "acknowledged-slot-admits-only-a-strictly-new-opening");
    Expect(input->Drop(8) && !input->Drop(8), "drop-releases-active-opening-once");
}

void TestLostAndStaleOpenings() noexcept
{
    auto input = std::make_unique<Input>();
    Expect(input->Begin(30, Context(), Vec2{-8.25, 14.5}, Input::Modifiers{true, true, false}, false),
           "lost-policy-begins-with-literal-down");
    Expect(input->Move(30, Vec2{5.125, 6.75}, Input::Modifiers{false, true, true}, false, 20) == Admission::Recorded,
           "lost-policy-accepts-last-path-sample");
    Expect(input->Finish(30, true, 21, true) == Admission::Recorded, "explicit-pointer-loss-records-one-terminal");
    Expect(IsEvent(input->Take(30), Action::Down, -8.25, 14.5, true, true, false, 19, false) &&
               IsEvent(input->Take(30), Action::Move, 5.125, 6.75, false, true, true, 20, false) &&
               IsEvent(input->Take(30), Action::Lost, 5.125, 6.75, false, true, true, 21, true),
           "lost-terminal-uses-last-point-modifiers-release-aa-and-terminal-ordinal");
    Expect(input->Finish(30, false, 22) == Admission::Closed &&
               input->Move(30, Vec2{8, 9}, {}, true, 22) == Admission::Closed && !input->Peek(29) && !input->Take(29) &&
               input->Move(29, Vec2{1, 1}, {}, true, 22) == Admission::Stale &&
               input->Finish(29, false, 22) == Admission::Stale,
           "late-events-close-and-stale-opening-cannot-read-or-append");
    Expect(input->Acknowledge(30) && !input->Begin(29, Context(), Vec2{1, 1}, {}, true) &&
               input->Begin(31, Context(), Vec2{1, 1}, {}, true),
           "lost-acknowledges-stale-reopen-refuses-and-newer-opening-admits");
    Expect(input->Drop(31) && !input->Busy(), "drop-clears-undrained-down");
}

void TestOpeningValidationAndExhaustion() noexcept
{
    auto input = std::make_unique<Input>();
    auto invalid = Context();
    invalid.document.canvasWidth = 0;
    Expect(!input->Begin(1, invalid, Vec2{1, 2}, {}, true) && input->Begin(1, Context(), Vec2{1, 2}, {}, true),
           "invalid-context-does-not-consume-opening");
    Expect(input->Drop(1), "valid-opening-after-invalid-context-drops");
    constexpr std::uint64_t maximum = (std::numeric_limits<std::uint64_t>::max)();
    Expect(input->Begin(maximum, Context(), Vec2{7, 8}, {}, false) && input->Drop(maximum) &&
               !input->Begin(maximum, Context(), Vec2{9, 10}, {}, true) &&
               !input->Begin(1, Context(), Vec2{9, 10}, {}, true) && !input->Busy() && input->Opening() == 0,
           "maximum-opening-exhaustion-refuses-equal-and-wraparound-reopen");
}

void TestSealLostAtOrdinalExhaustionAndAfterUp() noexcept
{
    auto exhausted = std::make_unique<Input>();
    auto context = Context();
    context.ordinal = (std::numeric_limits<std::uint64_t>::max)();
    Expect(exhausted->Begin(100, context, Vec2{42.5, -6.25}, Input::Modifiers{true, false, true}, false) &&
               IsEvent(exhausted->Take(100), Action::Down, 42.5, -6.25, true, false, true,
                       (std::numeric_limits<std::uint64_t>::max)(), false) &&
               exhausted->SealLost(100) == Admission::Recorded &&
               IsEvent(exhausted->Take(100), Action::Lost, 42.5, -6.25, true, false, true,
                       (std::numeric_limits<std::uint64_t>::max)(), false),
           "seal-lost-at-maximum-initial-ordinal-keeps-last-accepted-sample");

    auto finished = std::make_unique<Input>();
    Expect(finished->Begin(101, Context(), Vec2{3.5, 4.25}, Input::Modifiers{false, true, false}, true) &&
               finished->Finish(101, false, 20) == Admission::Recorded &&
               finished->SealLost(101) == Admission::Closed &&
               IsEvent(finished->Take(101), Action::Down, 3.5, 4.25, false, true, false, 19, true) &&
               IsEvent(finished->Take(101), Action::Up, 3.5, 4.25, false, true, false, 20, false),
           "seal-lost-after-up-is-closed-and-preserves-accepted-terminal");
}

void TestInvalidPressDescriptorsDoNotConsumeOpening() noexcept
{
    auto input = std::make_unique<Input>();
    auto descriptor = Context();
    descriptor.document.documentSerial = 0;
    Expect(!input->Begin(1, descriptor, Vec2{}, {}, true) && !input->Busy(),
           "zero-document-serial-refuses-before-opening");
    descriptor = Context();
    descriptor.ordinal = 0;
    Expect(!input->Begin(1, descriptor, Vec2{}, {}, true) && !input->Busy(),
           "zero-event-ordinal-refuses-before-opening");
    descriptor = Context();
    descriptor.tool = Input::Tool::Lasso;
    descriptor.kind = LassoKind::Rectangle;
    const bool wrongKind = !input->Begin(1, descriptor, Vec2{}, {}, true);
    descriptor = Context();
    descriptor.mode = static_cast<SelectionMode>(255);
    Expect(wrongKind && !input->Begin(1, descriptor, Vec2{}, {}, true) && !input->Busy(),
           "tool-kind-mismatch-and-invalid-mode-refuse-before-opening");
    descriptor = Context();
    descriptor.wand.tolerance = 256;
    const bool wrongTolerance = !input->Begin(1, descriptor, Vec2{}, {}, true);
    descriptor = Context();
    descriptor.wand.sampleSize = static_cast<WandSampleSize>(255);
    Expect(wrongTolerance && !input->Begin(1, descriptor, Vec2{}, {}, true) && !input->Busy(),
           "invalid-captured-wand-settings-refuse-before-opening");
    Expect(!input->Begin(1, Context(), Vec2{std::numeric_limits<double>::quiet_NaN(), 1}, {}, true) &&
               input->Begin(1, Context(), Vec2{-5, 600}, {}, true),
           "nonfinite-down-refuses-without-consuming-valid-outside-canvas-press");
}

void TestFullCapacityOverflowPrefix() noexcept
{
    auto input = std::make_unique<Input>();
    static_assert(Input::kCapacity == 512);
    Expect(input->Begin(90, Context(), Vec2{0.25, -0.5}, {}, false), "literal-512-capacity-and-down-admitted");
    for (std::uint64_t ordinal = 20; ordinal <= 530; ++ordinal)
    {
        const double x = static_cast<double>(ordinal) + 0.25;
        const double y = -static_cast<double>(ordinal) - 0.5;
        if (input->Move(90, Vec2{x, y}, Input::Modifiers{(ordinal % 2) == 0, (ordinal % 3) == 0, true},
                        (ordinal % 5) == 0, ordinal) != Admission::Recorded)
            break;
    }
    Expect(input->PendingCount() == 512 && input->Sampling() && !input->Overflowed(),
           "down-plus-511-moves-fill-all-512-accepted-slots");
    Expect(input->Move(90, Vec2{9999.5, -9999.5}, Input::Modifiers{true, true, true}, true, 531) ==
                   Admission::Overflow &&
               input->Overflowed() && !input->Sampling() && input->PendingCount() == 513,
           "next-move-is-refused-with-separate-lost-terminal");
    bool prefixMatches = true;
    for (std::uint64_t index = 0; index < 512; ++index)
    {
        const auto event = input->Take(90);
        if (index == 0)
        {
            prefixMatches = prefixMatches && IsEvent(event, Action::Down, 0.25, -0.5, false, false, false, 19, false);
        }
        else
        {
            const std::uint64_t ordinal = index + 19;
            prefixMatches = prefixMatches && IsEvent(event, Action::Move, static_cast<double>(ordinal) + 0.25,
                                                     -static_cast<double>(ordinal) - 0.5, (ordinal % 2) == 0,
                                                     (ordinal % 3) == 0, true, ordinal, (ordinal % 5) == 0);
        }
    }
    Expect(prefixMatches, "all-512-down-and-moves-preserve-literal-accepted-prefix-in-order");
    Expect(input->SealLost(90) == Admission::Closed &&
               IsEvent(input->Take(90), Action::Lost, 530.25, -530.5, true, false, true, 530, true),
           "seal-lost-after-overflow-is-closed-and-preserves-accepted-terminal");
    Expect(input->PendingCount() == 0 && input->Busy() && !input->Take(90) && input->Acknowledge(90) && !input->Busy(),
           "overflow-terminal-drains-and-releases-only-after-acknowledgement");
}
} // namespace

int RunSelectionPressInputTests()
{
    gAssertions = 0;
    gFailures = 0;
    TestCapturedValuesAndAdmission();
    TestLostAndStaleOpenings();
    TestOpeningValidationAndExhaustion();
    TestSealLostAtOrdinalExhaustionAndAfterUp();
    TestInvalidPressDescriptorsDoNotConsumeOpening();
    TestFullCapacityOverflowPrefix();
    std::printf("SUMMARY selection-press-input assertions=%d failures=%d expected=37\n", gAssertions, gFailures);
    return gFailures == 0 && gAssertions == 37 ? 0 : 1;
}
