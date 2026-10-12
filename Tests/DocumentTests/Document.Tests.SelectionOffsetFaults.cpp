#include "Document.Tests.SelectionOffsetFaults.h"
#include "BoundedClipper/BoundedSelectionOffset.h"
#include "Selection.h"

#include <cstdio>
#include <limits>
#include <new>
#include <stop_token>

namespace
{
int gFailures = 0;
void Expect(bool value, const char* name) noexcept
{
    std::printf("%s selection-offset-fault.%s\n", value ? "PASS" : "FAIL", name);
    gFailures += value ? 0 : 1;
}
bool Refuses(const std::vector<SelectionLoop>& input, BoundedSelectionBoolean::Limits limits)
{
    try
    {
        return !BoundedSelectionOffset::Run(input, 5, 100, 100, {}, limits);
    }
    catch (const std::bad_alloc&)
    {
        return true;
    }
}
struct StopAt final
{
    std::stop_source source;
    std::uint64_t boundary = 0;
    std::uint64_t visits = 0;
    static void Checkpoint(void* raw, std::uint64_t work) noexcept
    {
        auto& self = *static_cast<StopAt*>(raw);
        self.visits = work;
        if (work >= self.boundary)
            self.source.request_stop();
    }
};
} // namespace

int RunSelectionOffsetFaultTests()
{
    gFailures = 0;
    const auto input = SelectionGeometry::Rect(40, 40, 20, 20);
    const auto unchanged = input;
    for (int amount : {0, 501, -501})
    {
        char name[80]{};
        std::snprintf(name, sizeof(name), "invalid-amount-%d", amount);
        Expect(!BoundedSelectionOffset::Run(input, amount, 100, 100), name);
    }
    Expect(!BoundedSelectionOffset::Run(input, 5, 0, 100), "invalid-canvas-width");
    Expect(!BoundedSelectionOffset::Run(input, 5, 100, 0), "invalid-canvas-height");
    Expect(!BoundedSelectionOffset::Run(input, 5, kMaxCanvasSide + 1, 100), "oversize-canvas");
    auto invalid = input;
    invalid.front().front().x = (std::numeric_limits<double>::infinity)();
    Expect(!BoundedSelectionOffset::Run(invalid, 5, 100, 100), "nonfinite-coordinate");
    invalid.front().front().x = 1.0e30;
    Expect(!BoundedSelectionOffset::Run(invalid, 5, 100, 100), "out-of-range-coordinate");
    std::stop_source alreadyStopped;
    alreadyStopped.request_stop();
    Expect(!BoundedSelectionOffset::Run(input, 5, 100, 100, CancelToken(alreadyStopped.get_token())),
           "stopped-before-entry");
    auto limits = BoundedSelectionBoolean::Limits{};
    limits.workspaceBytes = 0;
    Expect(Refuses(input, limits), "zero-workspace");
    limits = {};
    limits.inputPoints = 3;
    Expect(Refuses(input, limits), "input-point-ceiling");
    limits = {};
    limits.inputLoops = 0;
    Expect(Refuses(input, limits), "input-loop-ceiling");
    limits = {};
    limits.outputPoints = 3;
    Expect(Refuses(input, limits), "output-point-ceiling");
    limits = {};
    limits.outputLoops = 0;
    Expect(Refuses(input, limits), "output-loop-ceiling");
    BoundedSelectionBoolean::Statistics measured;
    const auto baseline = BoundedSelectionOffset::Run(input, 5, 100, 100, {}, {}, &measured);
    Expect(baseline && measured.allocations > 0 && measured.workspaceConsumed > 0 && measured.workUnits >= 64,
           "successful-engine-has-positive-bounded-statistics");
    if (!baseline)
        return gFailures;
    bool everyAllocation = true;
    for (std::size_t prefix = 0; prefix < measured.allocations; ++prefix)
    {
        limits = {};
        limits.allocations = prefix;
        everyAllocation = Refuses(input, limits) && everyAllocation;
    }
    Expect(everyAllocation, "every-reached-allocation-prefix-refuses-without-escaping-arena");
    bool everyCheckpoint = true;
    for (std::uint64_t boundary = 64; boundary <= measured.workUnits; boundary += 64)
    {
        StopAt stop;
        stop.boundary = boundary;
        bool refused = false;
        try
        {
            refused = !BoundedSelectionOffset::Run(input, 5, 100, 100, CancelToken(stop.source.get_token()), {},
                                                   nullptr, StopAt::Checkpoint, &stop);
        }
        catch (const std::bad_alloc&)
        {
            refused = true;
        }
        everyCheckpoint = refused && stop.source.stop_requested() && stop.visits == boundary && everyCheckpoint;
    }
    Expect(everyCheckpoint, "every-reached-64-unit-checkpoint-cancels-and-unwinds");
    bool capacities = true;
    for (std::size_t capacity = 64; capacity < measured.workspaceConsumed; capacity += 64)
    {
        limits = {};
        limits.workspaceBytes = capacity;
        capacities = Refuses(input, limits) && capacities;
    }
    Expect(capacities, "every-smaller-64-byte-capacity-refuses");
    Expect(input == unchanged, "all-faults-preserve-source-geometry");
    const auto after = BoundedSelectionOffset::Run(input, 5, 100, 100);
    Expect(after && *after == *baseline, "fresh-engine-after-all-faults-matches-original-output");
    std::printf("INFO selection-offset-fault allocations=%zu workspace=%zu work=%llu points=%llu loops=%llu\n",
                measured.allocations, measured.workspaceConsumed, static_cast<unsigned long long>(measured.workUnits),
                static_cast<unsigned long long>(measured.outputPoints),
                static_cast<unsigned long long>(measured.outputLoops));
    return gFailures;
}
