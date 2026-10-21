#include "Document.Tests.SelectionHitWork.h"

#include "Selection.h"
#include "SelectionHitWork.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace
{
int gFailures = 0;
int gAssertions = 0;

void Expect(bool condition, const char* name) noexcept
{
    ++gAssertions;
    std::printf("%s selection-hit-work.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

SelectionDisplayValue Display(std::vector<SelectionLoop> loops, Vec2 translation = {})
{
    return SelectionDisplayValue{DocumentSelection{false, std::move(loops)}, translation};
}

bool Equals(const std::optional<bool>& actual, bool expected) noexcept
{
    return actual.has_value() && actual.value() == expected;
}

struct Checkpoints final
{
    CancelSource* cancellation = nullptr;
    SelectionHitStage cancelAt = SelectionHitStage::Complete;
    bool cancelEnabled = false;
    std::size_t previous = 0;
    std::size_t largestGap = 0;
    unsigned begin = 0;
    unsigned loops = 0;
    unsigned edges = 0;
    unsigned complete = 0;
    std::size_t cancelAfterWork = 0;
};

void Observe(void* opaque, SelectionHitStage stage, std::size_t work) noexcept
{
    auto& state = *static_cast<Checkpoints*>(opaque);
    state.largestGap = (std::max)(state.largestGap, work - state.previous);
    state.previous = work;
    switch (stage)
    {
    case SelectionHitStage::Begin:
        ++state.begin;
        break;
    case SelectionHitStage::Loop:
        ++state.loops;
        break;
    case SelectionHitStage::Edge:
        ++state.edges;
        break;
    case SelectionHitStage::Complete:
        ++state.complete;
        break;
    }
    if (state.cancelEnabled && stage == state.cancelAt && work >= state.cancelAfterWork)
        static_cast<void>(state.cancellation->Cancel());
}

void WindingAndBoundaries()
{
    const std::vector<SelectionLoop> ccw{SelectionLoop{Vec2{0, 0}, Vec2{10, 0}, Vec2{10, 10}, Vec2{0, 10}}};
    const std::vector<SelectionLoop> cw{SelectionLoop{Vec2{0, 10}, Vec2{10, 10}, Vec2{10, 0}, Vec2{0, 0}}};
    Expect(Equals(ContainsSelectionForWork(Display(ccw), Vec2{5, 5}), true), "counterclockwise-outer-interior");
    Expect(Equals(ContainsSelectionForWork(Display(ccw), Vec2{11, 5}), false), "counterclockwise-outer-exterior");
    Expect(Equals(ContainsSelectionForWork(Display(ccw), Vec2{10, 4}), true), "outer-boundary-segment-included");
    Expect(Equals(ContainsSelectionForWork(Display(ccw), Vec2{10, 10.0000000005}), true),
           "segment-endpoint-tolerance-includes-nearby-query");
    Expect(Equals(ContainsSelectionForWork(Display(ccw), Vec2{10, 10.000000002}), false),
           "segment-endpoint-tolerance-rejects-distant-query");
    Expect(Equals(ContainsSelectionForWork(Display(cw), Vec2{5, 5}), true), "clockwise-outer-interior");

    const std::vector<SelectionLoop> withHole{
        SelectionLoop{Vec2{0, 0}, Vec2{20, 0}, Vec2{20, 20}, Vec2{0, 20}},
        SelectionLoop{Vec2{5, 5}, Vec2{5, 15}, Vec2{15, 15}, Vec2{15, 5}},
    };
    Expect(Equals(ContainsSelectionForWork(Display(withHole), Vec2{2, 2}), true), "opposite-winding-hole-ring");
    Expect(Equals(ContainsSelectionForWork(Display(withHole), Vec2{10, 10}), false), "opposite-winding-hole-interior");
    Expect(Equals(ContainsSelectionForWork(Display(withHole), Vec2{5, 10}), true), "hole-boundary-included");
    const std::vector<SelectionLoop> sameWinding{withHole[0],
                                                 SelectionLoop{Vec2{5, 5}, Vec2{15, 5}, Vec2{15, 15}, Vec2{5, 15}}};
    Expect(Equals(ContainsSelectionForWork(Display(sameWinding), Vec2{10, 10}), true),
           "same-winding-inner-loop-adds-winding");
}

void TranslationAndInputRefusal()
{
    const std::vector<SelectionLoop> source{
        SelectionLoop{Vec2{1, 2}, Vec2{7, 2}, Vec2{7, 8}, Vec2{1, 8}},
        SelectionLoop{Vec2{3, 4}, Vec2{3, 6}, Vec2{5, 6}, Vec2{5, 4}},
    };
    const auto display = Display(source);
    const std::vector<SelectionLoop> zeroTranslationExpected{
        SelectionLoop{Vec2{1, 2}, Vec2{7, 2}, Vec2{7, 8}, Vec2{1, 8}},
        SelectionLoop{Vec2{3, 4}, Vec2{3, 6}, Vec2{5, 6}, Vec2{5, 4}},
    };
    const std::vector<SelectionLoop> finiteTranslationExpected{
        SelectionLoop{Vec2{3, -1}, Vec2{9, -1}, Vec2{9, 5}, Vec2{3, 5}},
        SelectionLoop{Vec2{5, 1}, Vec2{5, 3}, Vec2{7, 3}, Vec2{7, 1}},
    };
    Expect(SelectionGeometry::Translated(source, 0, 0) == zeroTranslationExpected,
           "zero-translation-matches-literal-vertices");
    Expect(SelectionGeometry::Translated(source, 2, -3) == finiteTranslationExpected,
           "finite-translation-matches-literal-vertices");
    const auto identity = display.selection->loops.Identity();

    Expect(Equals(ContainsSelectionForWork(Display(source, Vec2{2, -3}), Vec2{4, 0}), true),
           "finite-translation-applies-to-vertices");
    Expect(ContainsSelectionForWork(display, Vec2{2, 3}).has_value() && display.selection->loops.Identity() == identity,
           "source-selection-identity-unchanged");
    Expect(display.selection.value().loops[0] == source[0] && display.selection.value().loops[1] == source[1],
           "source-selection-vertices-unchanged");

    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double infinity = std::numeric_limits<double>::infinity();
    Expect(Equals(ContainsSelectionForWork(display, Vec2{nan, 3}), false), "nan-query-is-outside");
    Expect(Equals(ContainsSelectionForWork(display, Vec2{3, infinity}), false), "infinite-query-is-outside");
    Expect(Equals(ContainsSelectionForWork(Display({}), Vec2{3, 3}), false), "empty-loops-are-outside");
    Expect(Equals(ContainsSelectionForWork(SelectionDisplayValue{}, Vec2{3, 3}), false),
           "missing-selection-is-outside");
    Expect(!ContainsSelectionForWork(Display(source, Vec2{nan, 0}), Vec2{3, 3}).has_value(),
           "nan-translation-is-refused");
    Expect(!ContainsSelectionForWork(Display(source, Vec2{0, infinity}), Vec2{3, 3}).has_value(),
           "infinite-translation-is-refused");
}

void CancellationAndCheckpoints()
{
    {
        CancelSource cancellation;
        Checkpoints checkpoints{&cancellation, SelectionHitStage::Begin, true};
        const SelectionHitObserver observer{&checkpoints, Observe};
        const auto result = ContainsSelectionForWork(Display({}), Vec2{0, 0}, cancellation.Token(), observer);
        Expect(!result.has_value() && cancellation.Token().IsCancelled() && checkpoints.begin == 1 &&
                   checkpoints.complete == 0,
               "begin-checkpoint-cancels-before-work");
    }
    {
        CancelSource cancellation;
        Checkpoints checkpoints{&cancellation, SelectionHitStage::Complete, true};
        const SelectionHitObserver observer{&checkpoints, Observe};
        const auto result = ContainsSelectionForWork(Display({}), Vec2{0, 0}, cancellation.Token(), observer);
        Expect(!result.has_value() && cancellation.Token().IsCancelled() && checkpoints.begin == 1 &&
                   checkpoints.complete == 1,
               "complete-checkpoint-discards-result");
    }
    {
        CancelSource cancellation;
        static_cast<void>(cancellation.Cancel());
        Checkpoints checkpoints{&cancellation};
        const SelectionHitObserver observer{&checkpoints, Observe};
        const auto result = ContainsSelectionForWork(Display({}), Vec2{0, 0}, cancellation.Token(), observer);
        Expect(!result.has_value() && checkpoints.begin == 0 && checkpoints.complete == 0,
               "pre-cancelled-query-stops-before-begin-observer");
    }
    {
        std::vector<SelectionLoop> manyEmptyLoops(2300);
        Checkpoints checkpoints;
        const SelectionHitObserver observer{&checkpoints, Observe};
        const auto result = ContainsSelectionForWork(Display(std::move(manyEmptyLoops)), Vec2{0, 0}, {}, observer);
        Expect(Equals(result, false) && checkpoints.begin == 1 && checkpoints.complete == 1 && checkpoints.loops > 2,
               "empty-loop-scan-reaches-periodic-checkpoints");
        Expect(checkpoints.largestGap <= 1024, "empty-loop-checkpoint-gap-at-most-1024");
    }
    {
        CancelSource cancellation;
        Checkpoints checkpoints{&cancellation, SelectionHitStage::Loop, true};
        checkpoints.cancelAfterWork = 1024;
        const SelectionHitObserver observer{&checkpoints, Observe};
        const auto result =
            ContainsSelectionForWork(Display(std::vector<SelectionLoop>(2300)), Vec2{}, cancellation.Token(), observer);
        Expect(!result && checkpoints.previous == 1024 && checkpoints.loops == 2 && checkpoints.complete == 0,
               "empty-loop-cancellation-discards-result-at-next-quantum");
    }
    {
        CancelSource cancellation;
        Checkpoints checkpoints{&cancellation, SelectionHitStage::Edge, true};
        checkpoints.cancelAfterWork = 1024;
        const SelectionHitObserver observer{&checkpoints, Observe};
        SelectionLoop longLoop(2200, Vec2{0, 0});
        const auto result =
            ContainsSelectionForWork(Display({std::move(longLoop)}), Vec2{3000, 20}, cancellation.Token(), observer);
        Expect(!result && checkpoints.previous == 1024 && checkpoints.edges == 1 && checkpoints.complete == 0,
               "long-edge-cancellation-discards-result-at-next-quantum");
    }
    {
        SelectionLoop longLoop;
        longLoop.reserve(2200);
        for (int index = 0; index < 2200; ++index)
            longLoop.push_back(Vec2{static_cast<double>(index), 0});
        std::vector<SelectionLoop> loops;
        loops.push_back(std::move(longLoop));
        Checkpoints checkpoints;
        const SelectionHitObserver observer{&checkpoints, Observe};
        const auto result = ContainsSelectionForWork(Display(std::move(loops)), Vec2{3000, 20}, {}, observer);
        Expect(Equals(result, false) && checkpoints.begin == 1 && checkpoints.complete == 1 && checkpoints.edges >= 2,
               "long-edge-scan-reaches-periodic-checkpoints");
        Expect(checkpoints.largestGap <= 1024, "long-edge-checkpoint-gap-at-most-1024");
    }
}

void LargeCoordinateRounding()
{
    constexpr double base = -281474976710656.0;
    const std::vector<SelectionLoop> source{SelectionLoop{Vec2{base, base}, Vec2{base + 0.0625, base},
                                                          Vec2{base + 0.0625, base + 0.25}, Vec2{base, base + 0.25}}};
    const auto display = Display(source, Vec2{-0.03125, 0});
    const Vec2 query{base - 0.0625, base};
    const Vec2 inverseQuery{query.x - display.translation.x, query.y - display.translation.y};
    Expect(SelectionGeometry::Contains(source, inverseQuery),
           "large-coordinate-inverse-query-rounds-to-source-boundary");
    Expect(Equals(ContainsSelectionForWork(display, query), false),
           "large-coordinate-vertex-translation-keeps-query-outside");
}
} // namespace

int RunSelectionHitWorkTests()
{
    gFailures = 0;
    gAssertions = 0;
    WindingAndBoundaries();
    TranslationAndInputRefusal();
    CancellationAndCheckpoints();
    LargeCoordinateRounding();
    std::printf("selection-hit-work: %d assertions, %d failures\n", gAssertions, gFailures);
    return gFailures;
}
