#include "Document.Tests.WandChunkBoundary.h"

#include "Selection.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace
{
constexpr std::size_t kMaxCheckpoints = 16;
int gAssertions = 0;
int gFailures = 0;

enum class StageSlot : std::size_t
{
    MatchScan,
    FloodRun,
    FloodNeighbors,
    TraceEdges,
    TraceStarts,
    Count,
};

struct StageRecord final
{
    std::array<std::size_t, kMaxCheckpoints> offsets{};
    std::size_t size = 0;
    bool overflow = false;
};

struct CheckpointRecorder final
{
    std::array<StageRecord, static_cast<std::size_t>(StageSlot::Count)> stages{};
    CancelSource* cancelAtBoundary = nullptr;
    MagicWand::WorkStage cancelStage{};
    std::size_t cancelOffset = 0;
    std::size_t cancelHits = 0;
};

void Expect(bool condition, const char* name) noexcept
{
    ++gAssertions;
    std::printf("%s wand-chunk.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

bool SlotFor(MagicWand::WorkStage stage, StageSlot& slot) noexcept
{
    switch (stage)
    {
    case MagicWand::WorkStage::MatchScan:
        slot = StageSlot::MatchScan;
        return true;
    case MagicWand::WorkStage::FloodRun:
        slot = StageSlot::FloodRun;
        return true;
    case MagicWand::WorkStage::FloodNeighbors:
        slot = StageSlot::FloodNeighbors;
        return true;
    case MagicWand::WorkStage::TraceEdges:
        slot = StageSlot::TraceEdges;
        return true;
    case MagicWand::WorkStage::TraceStarts:
        slot = StageSlot::TraceStarts;
        return true;
    case ::MagicWand::WorkStage::Complete:
    case ::MagicWand::WorkStage::FloodSeeds:
    case ::MagicWand::WorkStage::MaskInitialize:
    case ::MagicWand::WorkStage::Sample:
    case ::MagicWand::WorkStage::Source:
    case ::MagicWand::WorkStage::TraceInitialize:
    case ::MagicWand::WorkStage::TraceWalk:
    default:
        return false;
    }
}

void RecordCheckpoint(void* opaque, MagicWand::WorkStage stage, std::size_t offset) noexcept
{
    auto& recorder = *static_cast<CheckpointRecorder*>(opaque);
    StageSlot slot{};
    if (SlotFor(stage, slot))
    {
        StageRecord& record = recorder.stages[static_cast<std::size_t>(slot)];
        if (record.size < record.offsets.size())
        {
            record.offsets[record.size++] = offset;
        }
        else
        {
            record.overflow = true;
        }
    }
    if (recorder.cancelAtBoundary != nullptr && stage == recorder.cancelStage && offset == recorder.cancelOffset)
    {
        ++recorder.cancelHits;
        static_cast<void>(recorder.cancelAtBoundary->Cancel());
    }
}

MagicWand::WorkObserver Observer(CheckpointRecorder& recorder) noexcept
{
    return MagicWand::WorkObserver{&recorder, &RecordCheckpoint};
}

bool HasOffsets(const CheckpointRecorder& recorder, StageSlot slot, std::span<const std::size_t> expected) noexcept
{
    const StageRecord& record = recorder.stages[static_cast<std::size_t>(slot)];
    return !record.overflow && record.size == expected.size() &&
           std::equal(expected.begin(), expected.end(), record.offsets.begin());
}

Raster SolidImage(int width, int height)
{
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
    for (std::size_t offset = 0; offset < pixels.size(); offset += 4u)
    {
        pixels[offset] = 17;
        pixels[offset + 1u] = 19;
        pixels[offset + 2u] = 23;
        pixels[offset + 3u] = 255;
    }
    return Raster::Make(width, height, RasterFormat::Bgra8888, pixels.data(), pixels.size());
}

std::vector<std::uint8_t> SolidMask(int width, int height)
{
    return std::vector<std::uint8_t>(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 255);
}

bool IsSolidMask(const std::vector<std::uint8_t>& actual, std::size_t pixelCount) noexcept
{
    return actual.size() == pixelCount &&
           std::all_of(actual.begin(), actual.end(), [](std::uint8_t value) { return value == 255; });
}

bool IsRectangle(const std::optional<std::vector<SelectionLoop>>& actual, double width, double height) noexcept
{
    if (!actual || actual->size() != 1u)
    {
        return false;
    }
    const SelectionLoop expected{Vec2{0, 0}, Vec2{width, 0}, Vec2{width, height}, Vec2{0, height}};
    return actual->front() == expected;
}

constexpr std::array<std::size_t, 5> kOffsetsThrough16384{0, 4096, 8192, 12288, 16384};
constexpr std::array<std::size_t, 3> kOffsetsThrough8192{0, 4096, 8192};
// Both neighbors of each row except the top/bottom: 2 * 64 * 257 = 32,896 pixel steps.
constexpr std::array<std::size_t, 9> kNeighborOffsetsThrough32768{0,     4096,  8192,  12288, 16384,
                                                                  20480, 24576, 28672, 32768};

void NonDividingGridLiteralAndFlood()
{
    constexpr int width = 257;
    constexpr int height = 65;
    constexpr std::size_t pixelCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    const Raster image = SolidImage(width, height);
    WandSettings settings;
    settings.contiguous = false;

    CheckpointRecorder scan;
    long count = -9;
    const auto mask = MagicWand::Mask(image, 0, 0, settings, count, CancelToken{}, Observer(scan));
    Expect(HasOffsets(scan, StageSlot::MatchScan, kOffsetsThrough16384),
           "grid.match-scan-ordered-chunk-boundaries-through-tail");
    Expect(count == static_cast<long>(pixelCount) && IsSolidMask(mask, pixelCount),
           "grid.match-scan-exact-work-and-literal-full-mask");

    CheckpointRecorder trace;
    WandFailure failure = WandFailure::None;
    const auto loops =
        MagicWand::Trace(SolidMask(width, height), width, height, &failure, CancelToken{}, Observer(trace));
    Expect(HasOffsets(trace, StageSlot::TraceEdges, kOffsetsThrough16384) &&
               HasOffsets(trace, StageSlot::TraceStarts, kOffsetsThrough16384),
           "grid.trace-edge-and-start-offsets-ordered-through-tail");
    Expect(failure == WandFailure::None && IsRectangle(loops, width, height),
           "grid.trace-exact-full-selection-clockwise-literal-rectangle");

    settings.contiguous = true;
    CheckpointRecorder flood;
    count = -1;
    const auto floodMask = MagicWand::Mask(image, 0, 0, settings, count, CancelToken{}, Observer(flood));
    Expect(count == static_cast<long>(pixelCount) && IsSolidMask(floodMask, pixelCount),
           "grid.flood-exact-work-and-literal-full-mask");

    failure = WandFailure::None;
    const auto selected = MagicWand::Select(image, Vec2{0.5, 0.5}, settings, &failure);
    Expect(HasOffsets(flood, StageSlot::FloodRun, kOffsetsThrough16384) &&
               HasOffsets(flood, StageSlot::FloodNeighbors, kNeighborOffsetsThrough32768),
           "grid.flood-run-and-neighbor-cumulative-row-boundaries");
    Expect(failure == WandFailure::None && IsRectangle(selected, width, height),
           "grid.flood-exact-full-selection-clockwise-literal-rectangle");
}

void SkinnyAndTailDimensions()
{
    for (const auto [width, height] : {std::pair{8193, 1}, std::pair{1, 8193}})
    {
        const std::size_t pixelCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
        const Raster image = SolidImage(width, height);
        WandSettings settings;
        settings.contiguous = false;
        CheckpointRecorder scan;
        long count = -1;
        const auto mask = MagicWand::Mask(image, 0, 0, settings, count, CancelToken{}, Observer(scan));
        Expect(HasOffsets(scan, StageSlot::MatchScan, kOffsetsThrough8192) && count == static_cast<long>(pixelCount) &&
                   IsSolidMask(mask, pixelCount),
               width == 8193 ? "tail.horizontal-matchscan-8193-exact-count-and-mask"
                             : "tail.vertical-matchscan-8193-exact-count-and-mask");

        CheckpointRecorder trace;
        WandFailure failure = WandFailure::None;
        const auto loops =
            MagicWand::Trace(SolidMask(width, height), width, height, &failure, CancelToken{}, Observer(trace));
        Expect(HasOffsets(trace, StageSlot::TraceEdges, kOffsetsThrough8192) &&
                   HasOffsets(trace, StageSlot::TraceStarts, kOffsetsThrough16384) && failure == WandFailure::None &&
                   IsRectangle(loops, width, height),
               width == 8193 ? "tail.horizontal-trace-exact-edge-start-work-and-rectangle"
                             : "tail.vertical-trace-exact-edge-start-work-and-rectangle");
    }
}

void CancellationAtFinalChunkBoundary()
{
    constexpr int width = 8193;
    constexpr int height = 1;
    const Raster image = SolidImage(width, height);
    WandSettings settings;
    settings.contiguous = false;
    CancelSource stopped;
    CheckpointRecorder recorder;
    recorder.cancelAtBoundary = &stopped;
    recorder.cancelStage = MagicWand::WorkStage::MatchScan;
    recorder.cancelOffset = 8192;
    long count = 41;
    const auto partial = MagicWand::Mask(image, 0, 0, settings, count, stopped.Token(), Observer(recorder));
    Expect(recorder.cancelHits == 1u && stopped.Token().IsCancelled() &&
               HasOffsets(recorder, StageSlot::MatchScan, kOffsetsThrough8192),
           "cancel.last-matchscan-boundary-reached-once");
    Expect(partial.empty() && count == MagicWand::kCancelledMaskCount,
           "cancel.last-boundary-discards-partial-output-and-sets-minus-two");

    long freshCount = -1;
    const auto recovered = MagicWand::Mask(image, 0, 0, settings, freshCount, CancelToken{});
    Expect(freshCount == width && IsSolidMask(recovered, static_cast<std::size_t>(width)),
           "cancel.fresh-token-completes-literal-output");

    CancelSource traceStopped;
    CheckpointRecorder traceRecorder;
    traceRecorder.cancelAtBoundary = &traceStopped;
    traceRecorder.cancelStage = MagicWand::WorkStage::TraceStarts;
    traceRecorder.cancelOffset = 16384;
    WandFailure failure = WandFailure::None;
    const auto cancelledTrace = MagicWand::Trace(SolidMask(width, height), width, height, &failure,
                                                 traceStopped.Token(), Observer(traceRecorder));
    Expect(traceRecorder.cancelHits == 1u && traceStopped.Token().IsCancelled() &&
               HasOffsets(traceRecorder, StageSlot::TraceStarts, kOffsetsThrough16384),
           "cancel.last-trace-start-boundary-reached-once");
    Expect(!cancelledTrace && failure == WandFailure::Cancelled,
           "cancel.last-trace-boundary-discards-partial-outline-as-cancelled");

    failure = WandFailure::None;
    const auto recoveredTrace = MagicWand::Trace(SolidMask(width, height), width, height, &failure);
    Expect(failure == WandFailure::None && IsRectangle(recoveredTrace, width, height),
           "cancel.fresh-trace-completes-literal-clockwise-rectangle");
}
} // namespace

int RunWandChunkBoundaryTests()
{
    gAssertions = 0;
    gFailures = 0;
    NonDividingGridLiteralAndFlood();
    SkinnyAndTailDimensions();
    CancellationAtFinalChunkBoundary();
    std::printf("wand-chunk.summary assertions=%d failures=%d\n", gAssertions, gFailures);
    return gFailures;
}
