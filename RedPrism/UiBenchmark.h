#pragma once

#include "BenchmarkMachine.h"
#include "Document.h"
#include "UiTiming.h"

#include <array>
#include <cstddef>
#include <cstdint>

// Opt-in harness state only. Fixed per-operation sample storage lives on the heap, outside timed work.
// Application owns it until shutdown; every command is driven through the actual outer message/frame loop.
struct UiBenchmarkState final
{
    enum class Phase
    {
        Preparing,
        Posted,
        AwaitingFrame,
        Done
    };
    struct Sample final
    {
        UiTimingStats timing;
        std::uint64_t elapsedUs = 0;
        std::uint64_t frames = 0;
    };
    CanvasDocument fixture;
    Guid activeLayer;
    BenchmarkMachineInfo machine;
    UiTimingStats timing;
    std::array<Sample, 10> samples;
    Phase phase = Phase::Preparing;
    std::size_t operation = 0;
    unsigned int trial = 0; // zero is the excluded warmup
    unsigned int runs = 10;
    bool smoke = false; // bounded correctness harness; may render while session display power is off
    std::uint64_t previousPresents = 0;
    std::int64_t requestedAt = 0;
    std::uint64_t fixtureChecksum = 0;
    Guid previousRevision;
    Raster previousImage;
    Raster previousPreview;
    std::uint64_t previousPreviewSequence = 0;
    double previousZoom = 1;
    Vec2 previousPan{};
    bool complete = false;
    bool failed = false;
};
