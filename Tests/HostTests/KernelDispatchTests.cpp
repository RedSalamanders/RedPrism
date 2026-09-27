#include "KernelDispatchTests.h"

#include "../../Common/Adjust.h"
#include "../../Common/Executor.h"
#include "../../Common/Kernels.h"

#include "../../Common/Kernels_Scalar_Adjust.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace
{
constexpr std::uint32_t kSse2 = 1u << 26;
constexpr std::uint32_t kAvxLeaf1 = (1u << 12) | (1u << 26) | (1u << 27) | (1u << 28) | (1u << 29);
constexpr std::uint32_t kAvxLeaf7 = (1u << 5) | (1u << 8);
constexpr std::uint32_t kAvxState = (1u << 1) | (1u << 2);

void FillTable(Adjust::ChannelTables& tables, std::size_t channel, float value) noexcept
{
    for (std::size_t index = 0; index < 256; ++index)
    {
        tables.values[channel * 256 + index] = value;
    }
}
} // namespace

int RunKernelDispatchTests()
{
    int failures = 0;
    const auto expect = [&failures](bool condition, const char* name)
    {
        std::printf("%s kernel-dispatch.%s\n", condition ? "PASS" : "FAIL", name);
        if (!condition)
        {
            ++failures;
        }
    };

    const X64KernelFeatures fullFeatures{kAvxLeaf1, kSse2, kAvxLeaf7, kAvxState};
    const KernelCapabilities full = DecodeX64KernelFeatures(fullFeatures);
    expect(full.sse2 && full.avx2, "decode-complete-required-features");

    struct MissingFeature final
    {
        std::uint32_t leaf1Ecx;
        std::uint32_t leaf1Edx;
        std::uint32_t leaf7Ebx;
        std::uint64_t xcr0;
        const char* name;
    };
    constexpr std::array missingFeatures{
        MissingFeature{kAvxLeaf1, kSse2 & ~(1u << 26), kAvxLeaf7, kAvxState, "decode-needs-sse2"},
        MissingFeature{kAvxLeaf1 & ~(1u << 12), kSse2, kAvxLeaf7, kAvxState, "decode-needs-fma"},
        MissingFeature{kAvxLeaf1 & ~(1u << 26), kSse2, kAvxLeaf7, kAvxState, "decode-needs-xsave"},
        MissingFeature{kAvxLeaf1 & ~(1u << 27), kSse2, kAvxLeaf7, kAvxState, "decode-needs-osxsave"},
        MissingFeature{kAvxLeaf1 & ~(1u << 28), kSse2, kAvxLeaf7, kAvxState, "decode-needs-avx"},
        MissingFeature{kAvxLeaf1 & ~(1u << 29), kSse2, kAvxLeaf7, kAvxState, "decode-needs-f16c"},
        MissingFeature{kAvxLeaf1, kSse2, kAvxLeaf7 & ~(1u << 5), kAvxState, "decode-needs-avx2"},
        MissingFeature{kAvxLeaf1, kSse2, kAvxLeaf7 & ~(1u << 8), kAvxState, "decode-needs-bmi2"},
        MissingFeature{kAvxLeaf1, kSse2, kAvxLeaf7, 1u << 1, "decode-needs-xcr0-avx"},
        MissingFeature{kAvxLeaf1, kSse2, kAvxLeaf7, 1u << 2, "decode-needs-xcr0-sse"},
    };
    for (const MissingFeature& feature : missingFeatures)
    {
        const KernelCapabilities decoded =
            DecodeX64KernelFeatures({feature.leaf1Ecx, feature.leaf1Edx, feature.leaf7Ebx, feature.xcr0});
        expect(!decoded.avx2, feature.name);
    }

    constexpr X64KernelFeatures irrelevantFeatures{
        kAvxLeaf1 | ((1u << 31) | (1u << 30) | (1u << 0)),
        kSse2 | 0xBFFFFFFFu,
        kAvxLeaf7 | 0xFFFFFFD7u,
        kAvxState | 0xFFFFFFFFFFFFFFF9ull,
    };
    const KernelCapabilities withIrrelevant = DecodeX64KernelFeatures(irrelevantFeatures);
    expect(withIrrelevant.sse2 && withIrrelevant.avx2, "decode-ignores-irrelevant-bits");

    const KernelCapabilities detected = DetectKernelCapabilities();
#if defined(_M_X64)
    expect(detected.sse2, "detect-x64-baseline-sse2");
#elif defined(_M_ARM64)
    expect(detected.neon, "detect-arm64-baseline-neon");
#endif

    constexpr KernelCapabilities noOptionalHardware{};
    constexpr KernelCapabilities allOptionalHardware{true, true, true, true};
    expect(ValidateKernelIsa(KernelIsa::Sse2, noOptionalHardware) == KernelSelection::UnsupportedHardware,
           "validate-sse2-hardware-absent");
    expect(ValidateKernelIsa(KernelIsa::Avx2, noOptionalHardware) == KernelSelection::UnsupportedHardware,
           "validate-avx2-hardware-absent");
    expect(ValidateKernelIsa(KernelIsa::Neon, noOptionalHardware) == KernelSelection::UnsupportedHardware,
           "validate-neon-hardware-absent");
    expect(ValidateKernelIsa(KernelIsa::Sse2, allOptionalHardware) == KernelSelection::NotImplemented,
           "validate-sse2-not-implemented");
    expect(ValidateKernelIsa(KernelIsa::Avx2, allOptionalHardware) == KernelSelection::NotImplemented,
           "validate-avx2-not-implemented");
    expect(ValidateKernelIsa(KernelIsa::Neon, allOptionalHardware) == KernelSelection::NotImplemented,
           "validate-neon-not-implemented");
    expect(ValidateKernelIsa(KernelIsa::Avx512, noOptionalHardware) == KernelSelection::NotImplemented &&
               ValidateKernelIsa(KernelIsa::Avx512, allOptionalHardware) == KernelSelection::NotImplemented,
           "validate-avx512-not-implemented-regardless-of-hardware");

    constexpr std::array tiers{KernelIsa::Auto, KernelIsa::Scalar, KernelIsa::Sse2,
                               KernelIsa::Avx2, KernelIsa::Avx512, KernelIsa::Neon};
    int implementedCount = 0;
    for (const KernelIsa tier : tiers)
    {
        if (KernelIsaImplemented(tier))
        {
            ++implementedCount;
        }
    }
    expect(implementedCount == 1 && KernelIsaImplemented(KernelIsa::Scalar), "implemented-only-scalar-p08");
    expect(!KernelIsaImplemented(KernelIsa::Auto) && !KernelIsaImplemented(KernelIsa::Sse2) &&
               !KernelIsaImplemented(KernelIsa::Avx2) && !KernelIsaImplemented(KernelIsa::Avx512) &&
               !KernelIsaImplemented(KernelIsa::Neon),
           "implemented-no-other-tier");

    expect(InitializeKernels(KernelIsa::Scalar) == KernelSelection::Selected, "initialize-scalar");
    expect(InitializeKernels(KernelIsa::Auto) == KernelSelection::Selected, "initialize-auto");
    expect(SetIsaForTest(KernelIsa::Scalar) == KernelSelection::Selected, "set-test-scalar");
    const KernelTable* const scalarTable = &ActiveKernels();
    expect(scalarTable->isa == KernelIsa::Scalar && scalarTable->applyTables != nullptr &&
               scalarTable->applyGradientMap != nullptr && scalarTable->applyGrain != nullptr &&
               scalarTable->addNoise != nullptr && scalarTable->lensDistort != nullptr &&
               scalarTable->applyCube != nullptr,
           "scalar-table-all-callbacks-present");
    expect(scalarTable->applyTables == &Kernels_Scalar_Adjust_ApplyTables &&
               scalarTable->applyGradientMap == &Kernels_Scalar_Adjust_ApplyGradientMap &&
               scalarTable->applyGrain == &Kernels_Scalar_Adjust_ApplyGrain &&
               scalarTable->addNoise == &Kernels_Scalar_Adjust_AddNoise &&
               scalarTable->lensDistort == &Kernels_Scalar_Adjust_LensDistort &&
               scalarTable->applyCube == &Kernels_Scalar_Adjust_ApplyCube,
           "scalar-table-explicit-reference-entries");
    expect(SetIsaForTest(KernelIsa::Auto) == KernelSelection::Selected && &ActiveKernels() == scalarTable,
           "set-test-auto-keeps-scalar-table");
    bool foundUnsupportedTier = false;
    constexpr std::array optionalTiers{KernelIsa::Sse2, KernelIsa::Avx2, KernelIsa::Neon};
    for (const KernelIsa tier : optionalTiers)
    {
        if (ValidateKernelIsa(tier, detected) == KernelSelection::UnsupportedHardware)
        {
            foundUnsupportedTier = true;
            const bool setRefused = SetIsaForTest(tier) == KernelSelection::UnsupportedHardware;
            const bool initializeRefused = InitializeKernels(tier) == KernelSelection::UnsupportedHardware;
            expect(setRefused && initializeRefused && &ActiveKernels() == scalarTable,
                   "rejected-unsupported-hardware-keeps-table-identity");
            break;
        }
    }
    expect(foundUnsupportedTier, "host-has-an-unsupported-optional-tier");
    expect(SetIsaForTest(KernelIsa::Avx512) == KernelSelection::NotImplemented && &ActiveKernels() == scalarTable,
           "rejected-tier-keeps-table-identity");
    expect(InitializeKernels(KernelIsa::Avx512) == KernelSelection::NotImplemented && &ActiveKernels() == scalarTable,
           "rejected-initialize-keeps-table-identity");

    Adjust::ChannelTables constantTables{};
    FillTable(constantTables, 0, 192.0f / 255.0f);
    FillTable(constantTables, 1, 128.0f / 255.0f);
    FillTable(constantTables, 2, 64.0f / 255.0f);
    constexpr int width = 3;
    constexpr int height = 2;
    constexpr int stride = 16;
    std::array<std::uint8_t, stride * height> tablePixels{10, 20,   30,   255,  40,   50, 60,   128,  17,   29,  43,
                                                          0,  0xA1, 0xA2, 0xA3, 0xA4, 90, 80,   70,   255,  60,  50,
                                                          40, 128,  31,   41,   51,   0,  0xB1, 0xB2, 0xB3, 0xB4};
    scalarTable->applyTables(tablePixels.data(), width, height, stride, constantTables, CancelToken{});
    constexpr std::array<std::uint8_t, stride * height> expectedTablePixels{
        64, 128, 192, 255, 32, 64, 96, 128, 17, 29, 43, 0, 0xA1, 0xA2, 0xA3, 0xA4,
        64, 128, 192, 255, 32, 64, 96, 128, 31, 41, 51, 0, 0xB1, 0xB2, 0xB3, 0xB4};
    expect(tablePixels == expectedTablePixels, "apply-tables-odd-width-alpha-transparent-padding");

    std::array<std::uint8_t, 768> gradient{};
    for (std::size_t level = 0; level < 256; ++level)
    {
        gradient[level * 3] = static_cast<std::uint8_t>(255 - level);
        gradient[level * 3 + 1] = 0;
        gradient[level * 3 + 2] = static_cast<std::uint8_t>(level);
    }
    std::array<std::uint8_t, stride * height> gradientPixels{0,  0,    0,    255,  64,   64,  64,   128,  17,   29,  43,
                                                             0,  0xC1, 0xC2, 0xC3, 0xC4, 255, 255,  255,  255,  32,  32,
                                                             32, 128,  31,   41,   51,   0,   0xD1, 0xD2, 0xD3, 0xD4};
    scalarTable->applyGradientMap(gradientPixels.data(), width, height, stride, gradient, CancelToken{});
    constexpr std::array<std::uint8_t, stride * height> expectedGradientPixels{
        0,   0, 255, 255, 64, 0, 64, 128, 17, 29, 43, 0, 0xC1, 0xC2, 0xC3, 0xC4,
        255, 0, 0,   255, 32, 0, 96, 128, 31, 41, 51, 0, 0xD1, 0xD2, 0xD3, 0xD4};
    expect(gradientPixels == expectedGradientPixels, "gradient-map-legacy-midpoint-alpha-transparent-padding");

    const auto initializePaddedPixels = [](std::array<std::uint8_t, stride * height>& pixels, std::uint8_t padding)
    {
        pixels.fill(padding);
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                std::uint8_t* pixel = pixels.data() + y * stride + x * 4;
                pixel[0] = 40;
                pixel[1] = 60;
                pixel[2] = 80;
                pixel[3] = 255;
            }
        }
    };
    const auto hasPadding = [](const std::array<std::uint8_t, stride * height>& pixels, std::uint8_t padding)
    {
        for (int y = 0; y < height; ++y)
        {
            for (int x = width * 4; x < stride; ++x)
            {
                if (pixels[static_cast<std::size_t>(y * stride + x)] != padding)
                {
                    return false;
                }
            }
        }
        return true;
    };
    GrainSettings grain{};
    grain.amount = 100;
    grain.size = 4;
    grain.roughness = 50;
    std::vector<float> cube(
        static_cast<std::size_t>(Adjust::kCubeDimension) * Adjust::kCubeDimension * Adjust::kCubeDimension * 4, 0.5f);
    std::array<std::uint8_t, stride * height> grainPixels{};
    initializePaddedPixels(grainPixels, 0xB1);
    scalarTable->applyGrain(grainPixels.data(), width, height, stride, grain, 0, 0, 1, CancelToken{});
    expect(hasPadding(grainPixels, 0xB1), "apply-grain-odd-width-padding");
    std::array<std::uint8_t, stride * height> noisePixels{};
    initializePaddedPixels(noisePixels, 0xB2);
    scalarTable->addNoise(noisePixels.data(), width, height, stride, 50, false, false, 17, CancelToken{});
    expect(hasPadding(noisePixels, 0xB2), "add-noise-odd-width-padding");
    std::array<std::uint8_t, stride * height> cubePixels{};
    initializePaddedPixels(cubePixels, 0xB3);
    scalarTable->applyCube(cubePixels.data(), width, height, stride, cube, CancelToken{});
    expect(hasPadding(cubePixels, 0xB3), "apply-cube-odd-width-padding");
    std::array<std::uint8_t, stride * height> lensSource{};
    initializePaddedPixels(lensSource, 0xB4);
    std::array<std::uint8_t, stride * height> lensDestination{};
    lensDestination.fill(0xB5);
    scalarTable->lensDistort(lensSource.data(), lensDestination.data(), width, height, stride, 0.2, CancelToken{});
    expect(hasPadding(lensDestination, 0xB5), "lens-distort-odd-width-destination-padding");

    CancelSource cancelledSource;
    expect(cancelledSource.Cancel(), "cancel-source-requested");
    const CancelToken cancelled = cancelledSource.Token();
    std::array<std::uint8_t, stride * height> cancelledPixels{};
    cancelledPixels.fill(0x5A);
    const auto originalCancelledPixels = cancelledPixels;
    scalarTable->applyTables(cancelledPixels.data(), width, height, stride, constantTables, cancelled);
    const bool tablesCancelledUntouched = cancelledPixels == originalCancelledPixels;
    scalarTable->applyGradientMap(cancelledPixels.data(), width, height, stride, gradient, cancelled);
    const bool gradientCancelledUntouched = cancelledPixels == originalCancelledPixels;
    scalarTable->applyGrain(cancelledPixels.data(), width, height, stride, grain, 0, 0, 1, cancelled);
    const bool grainCancelledUntouched = cancelledPixels == originalCancelledPixels;
    scalarTable->addNoise(cancelledPixels.data(), width, height, stride, 50, false, false, 17, cancelled);
    const bool noiseCancelledUntouched = cancelledPixels == originalCancelledPixels;
    std::array<std::uint8_t, stride * height> cancelledDestination{};
    cancelledDestination.fill(0xA5);
    const auto originalCancelledDestination = cancelledDestination;
    scalarTable->lensDistort(cancelledPixels.data(), cancelledDestination.data(), width, height, stride, 0.2,
                             cancelled);
    const bool lensCancelledUntouched = cancelledDestination == originalCancelledDestination;
    scalarTable->applyCube(cancelledPixels.data(), width, height, stride, cube, cancelled);
    const bool cubeCancelledUntouched = cancelledPixels == originalCancelledPixels;
    expect(tablesCancelledUntouched && gradientCancelledUntouched && grainCancelledUntouched &&
               noiseCancelledUntouched && lensCancelledUntouched && cubeCancelledUntouched,
           "scalar-pre-cancelled-calls-leave-output-untouched");

    expect(InitializeKernels(KernelIsa::Auto) == KernelSelection::Selected, "restore-auto-before-return");
    return failures;
}
