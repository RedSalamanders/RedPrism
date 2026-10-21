#include "Document.Tests.SelectionOffsetLarge.h"
#include "BoundedClipper/BoundedSelectionOffset.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <new>

// Independent integer-grid reference: 32,768 disjoint 20x20 squares contract by five
// into disjoint 10x10 squares. No expected result comes from the implementation.
int RunSelectionOffsetLargeTests()
{
    std::vector<SelectionLoop> input;
    input.reserve(32768);
    for (int row = 0; row < 128; ++row)
        for (int column = 0; column < 256; ++column)
        {
            const double x = column * 32 + 8, y = row * 32 + 8;
            input.push_back({{x, y}, {x + 20, y}, {x + 20, y + 20}, {x, y + 20}});
        }
    const auto before = input;
    BoundedSelectionBoolean::Statistics measured;
    std::optional<std::vector<SelectionLoop>> result;
    try
    {
        result = BoundedSelectionOffset::Run(input, -5, 8192, 4096, {}, {}, &measured);
    }
    catch (const std::bad_alloc&)
    {
        // Refusal is reported by fixed assertions below; it is never a passing large fixture.
    }
    std::size_t points = 0;
    bool squares = result && result->size() == 32768;
    double twiceArea = 0, left = (std::numeric_limits<double>::max)(), top = left;
    double right = -(std::numeric_limits<double>::max)(), bottom = right;
    if (result)
        for (const auto& loop : *result)
        {
            points += loop.size();
            squares = squares && loop.size() == 4;
            for (std::size_t index = 0; index < loop.size(); ++index)
            {
                const auto& point = loop[index];
                const auto& next = loop[(index + 1) % loop.size()];
                twiceArea += point.x * next.y - next.x * point.y;
                left = (std::min)(left, point.x);
                top = (std::min)(top, point.y);
                right = (std::max)(right, point.x);
                bottom = (std::max)(bottom, point.y);
            }
        }
    int failures = 0;
    const auto expect = [&](bool value, const char* name)
    {
        std::printf("%s selection-offset-large.%s\n", value ? "PASS" : "FAIL", name);
        failures += value ? 0 : 1;
    };
    expect(squares && points == 131072, "literal-square-and-point-count");
    expect(result && twiceArea == 6553600.0, "literal-signed-twice-area");
    expect(result && left == 13 && top == 13 && right == 8183 && bottom == 4087, "literal-bounds");
    expect(result && measured.workspaceCapacity == 128 * 1024 * 1024ULL && measured.workspaceConsumed > 0 &&
               measured.workspaceConsumed <= measured.workspaceCapacity && measured.outputPoints == 131072 &&
               measured.outputLoops == 32768,
           "unchanged-128mib-ceiling-and-output-statistics");
    expect(input == before, "immutable-input");
    std::printf("INFO selection-offset-large allocations=%zu workspace=%zu work=%llu\n", measured.allocations,
                measured.workspaceConsumed, static_cast<unsigned long long>(measured.workUnits));
    return failures;
}
