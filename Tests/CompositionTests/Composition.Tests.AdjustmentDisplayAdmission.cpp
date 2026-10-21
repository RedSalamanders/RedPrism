#include "Composition.Tests.AdjustmentDisplayAdmission.h"

#include "../../Common/AdjustmentDisplayAdmission.h"
#include "../../Common/Document.h"

#include <cstdint>
#include <cstdio>

namespace RedPrism::CompositionTests
{
namespace
{
int gFailures = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s adjustment-display-admission.%s\n", condition ? "PASS" : "FAIL", name);
    if (!condition)
        ++gFailures;
}

bool Matches(const AdjustmentDisplayAdmission::Envelope& value, std::uint64_t cpuLogical, std::uint64_t gpuLogical,
             std::uint64_t cpuAdditional, std::uint64_t gpuAdditional, std::uint32_t levels,
             std::uint32_t textures) noexcept
{
    return value.logicalCpuBytes == cpuLogical && value.logicalGpuBytes == gpuLogical &&
           value.additionalCpuBytes == cpuAdditional && value.additionalGpuBytes == gpuAdditional &&
           value.mipLevels == levels && value.textureCount == textures;
}

bool Rejected(const AdjustmentDisplayAdmission::Descriptor& descriptor) noexcept
{
    return !AdjustmentDisplayAdmission::Estimate(descriptor).has_value();
}
} // namespace

int RunAdjustmentDisplayAdmissionTests()
{
    gFailures = 0;
    using AdjustmentDisplayAdmission::Descriptor;
    using AdjustmentDisplayAdmission::Estimate;

    // Independent references: 2x2 mips contain 4+1 texels; 1x8 mips contain 8+4+2+1.
    constexpr std::uint64_t kBaseLiveCpu = 1060864; // 1 MiB + 4096 table + 4096 settings + 4096 curve bound.
    constexpr std::uint64_t kCube = 574992;
    constexpr std::uint64_t kRound = 65536;
    constexpr std::uint64_t kLiveTableGpuWithRounding = 200704; // 4096 + 3 * 65536.
    constexpr std::uint64_t kLiveCubeGpuWithRounding = 771600;  // 574992 + 3 * 65536.

    const auto square = Estimate(Descriptor{2, 2, false, true, AdjustmentKind::Hsv, false});
    Expect(square.has_value(), "2x2-hsv-coverage-estimate-exists");
    if (square.has_value())
        Expect(Matches(square.value(), kBaseLiveCpu + kCube + 64, 20 + 5 + kCube, kBaseLiveCpu + kCube + 64 + kRound,
                       25 + kCube + 8 * kRound, 2, 3),
               "2x2-hsv-coverage-independent-exact-mip-bytes");

    const auto strip = Estimate(Descriptor{1, 8, false, true, AdjustmentKind::Levels, false});
    Expect(strip.has_value(), "1x8-levels-coverage-estimate-exists");
    if (strip.has_value())
        Expect(Matches(strip.value(), kBaseLiveCpu + 32, 4171, kBaseLiveCpu + 32 + kRound, 4171 + 8 * kRound, 4, 3),
               "1x8-levels-coverage-independent-exact-mip-bytes");

    const auto liveHsv = Estimate(Descriptor{0, 0, true, false, AdjustmentKind::Hsv, false});
    Expect(liveHsv.has_value(), "live-hsv-estimate-exists");
    if (liveHsv.has_value())
        Expect(Matches(liveHsv.value(), kBaseLiveCpu + kCube, kCube, kBaseLiveCpu + kCube + kRound,
                       kLiveCubeGpuWithRounding, 0, 1),
               "live-hsv-counts-retained-cube-and-table");

    const auto liveLevels = Estimate(Descriptor{0, 0, true, false, AdjustmentKind::Levels, false});
    Expect(liveLevels.has_value(), "live-levels-estimate-exists");
    if (liveLevels.has_value())
        Expect(Matches(liveLevels.value(), kBaseLiveCpu, 4096, kBaseLiveCpu + kRound, kLiveTableGpuWithRounding, 0, 1),
               "live-levels-counts-embedded-cpu-table-and-gpu-table");

    const auto liveCurves = Estimate(Descriptor{0, 0, true, false, AdjustmentKind::Curves, false});
    Expect(liveCurves.has_value(), "live-curves-estimate-exists");
    if (liveCurves.has_value())
        Expect(Matches(liveCurves.value(), kBaseLiveCpu, 4096, kBaseLiveCpu + kRound, kLiveTableGpuWithRounding, 0, 1),
               "live-curves-counts-two-bounded-curve-owners");

    const auto liveExposure = Estimate(Descriptor{0, 0, true, false, AdjustmentKind::Exposure, false});
    Expect(liveExposure.has_value(), "live-exposure-estimate-exists");
    if (liveExposure.has_value())
        Expect(
            Matches(liveExposure.value(), kBaseLiveCpu, 4096, kBaseLiveCpu + kRound, kLiveTableGpuWithRounding, 0, 1),
            "live-exposure-counts-actual-table-upload");

    const auto liveGradient = Estimate(Descriptor{0, 0, true, false, AdjustmentKind::GradientMap, false});
    Expect(liveGradient.has_value(), "live-gradient-map-estimate-exists");
    if (liveGradient.has_value())
        Expect(
            Matches(liveGradient.value(), kBaseLiveCpu, 4096, kBaseLiveCpu + kRound, kLiveTableGpuWithRounding, 0, 1),
            "live-gradient-map-counts-actual-table-upload");

    const auto liveGrain = Estimate(Descriptor{0, 0, true, false, AdjustmentKind::Grain, false});
    Expect(liveGrain.has_value(), "live-grain-estimate-exists");
    if (liveGrain.has_value())
        Expect(Matches(liveGrain.value(), kBaseLiveCpu, 0, kBaseLiveCpu + kRound, 0, 0, 0),
               "live-grain-has-cpu-table-but-no-gpu-lookup");

    const auto software = Estimate(Descriptor{2, 2, false, true, AdjustmentKind::Hsv, true});
    Expect(software.has_value(), "software-estimate-exists");
    if (software.has_value())
        Expect(software.value().additionalCpuBytes == kBaseLiveCpu + kCube + 64 + kRound + 25 + kCube,
               "software-adds-logical-textures-to-host-memory-charge");

    Expect(Rejected(Descriptor{16385, 1, false, false, AdjustmentKind::Grain, false}), "width-above-16384-refused");
    Expect(Rejected(Descriptor{1, 16385, false, false, AdjustmentKind::Grain, false}), "height-above-16384-refused");
    Expect(Rejected(Descriptor{0, 1, false, false, AdjustmentKind::Grain, false}),
           "partial-zero-destructive-dimensions-refused");
    Expect(Rejected(Descriptor{0, 0, false, false, AdjustmentKind::Grain, false}),
           "zero-destructive-dimensions-refused");
    Expect(Rejected(Descriptor{1, 1, true, false, AdjustmentKind::Grain, false}),
           "live-adjustment-requires-zero-dimensions");
    Expect(Rejected(Descriptor{0, 0, true, true, AdjustmentKind::Grain, false}),
           "live-adjustment-refuses-coverage-target");
    Expect(Rejected(Descriptor{1, 1, false, false, static_cast<AdjustmentKind>(255), false}),
           "unknown-document-adjustment-kind-refused");

    return gFailures == 0 ? 0 : 1;
}
} // namespace RedPrism::CompositionTests
