#include "Document.Tests.HealWork.h"

#include "Document.h"
#include "Heal.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <limits>
#include <memory>
#include <span>
#include <utility>

namespace
{
int gFailures = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s heal-work.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

constexpr std::size_t kWidth = 32;
constexpr std::size_t kHeight = 32;
constexpr std::size_t kStride = kWidth * 4 + 5;
constexpr std::uint8_t kPadding = 0xCD;
using Pixels = std::array<std::uint8_t, kStride * kHeight>;
using Coverage = std::array<std::uint8_t, kWidth * kHeight>;

constexpr std::size_t kLargeWidth = 128;
constexpr std::size_t kLargeHeight = 128;
constexpr std::size_t kLargeStride = kLargeWidth * 4 + 7;
using LargePixels = std::array<std::uint8_t, kLargeStride * kLargeHeight>;
using LargeCoverage = std::array<std::uint8_t, kLargeWidth * kLargeHeight>;

void MakeBlemish(Pixels& pixels, Coverage& coverage) noexcept
{
    pixels.fill(kPadding);
    coverage.fill(0);
    for (std::size_t y = 0; y < kHeight; ++y)
    {
        for (std::size_t x = 0; x < kWidth; ++x)
        {
            const std::size_t offset = y * kStride + x * 4;
            const bool blemish = x >= 10 && x < 15 && y >= 10 && y < 15;
            pixels[offset] = blemish ? 0 : 128;
            pixels[offset + 1] = blemish ? 0 : 128;
            pixels[offset + 2] = blemish ? 255 : 128;
            pixels[offset + 3] = 255;
            if (blemish)
            {
                coverage[y * kWidth + x] = 255;
            }
        }
    }
}

void MakeLargeBlemish(LargePixels& pixels, LargeCoverage& coverage) noexcept
{
    pixels.fill(kPadding);
    coverage.fill(0);
    for (std::size_t y = 0; y < kLargeHeight; ++y)
    {
        for (std::size_t x = 0; x < kLargeWidth; ++x)
        {
            const std::size_t offset = y * kLargeStride + x * 4;
            const bool blemish = x >= 44 && x < 84 && y >= 44 && y < 84;
            pixels[offset] = blemish ? 0 : 128;
            pixels[offset + 1] = blemish ? 0 : 128;
            pixels[offset + 2] = blemish ? 255 : 128;
            pixels[offset + 3] = 255;
            if (blemish)
            {
                coverage[y * kLargeWidth + x] = 255;
            }
        }
    }
}

struct CancelAt final
{
    CancelSource source;
    SpotHealing::WorkStage target;
    bool observed = false;
};

struct CancelAfter final
{
    CancelSource source;
    SpotHealing::WorkStage target;
    int targetCount;
    int observedCount = 0;
};

void CancelOnStage(void* context, SpotHealing::WorkStage stage) noexcept
{
    auto& state = *static_cast<CancelAt*>(context);
    if (stage == state.target && !state.observed)
    {
        state.observed = true;
        state.source.Cancel();
    }
}

void CancelAfterStage(void* context, SpotHealing::WorkStage stage) noexcept
{
    auto& state = *static_cast<CancelAfter*>(context);
    if (stage == state.target && ++state.observedCount == state.targetCount)
    {
        state.source.Cancel();
    }
}

template <std::size_t N>
bool Same(const std::array<std::uint8_t, N>& left, const std::array<std::uint8_t, N>& right) noexcept
{
    return left == right;
}

bool RejectsWithoutWrite(const std::uint8_t* coverage, std::size_t width, std::size_t height, std::size_t stride,
                         const Pixels& before, bool nullBgra = false) noexcept
{
    Pixels probe = before;
    const auto source =
        coverage ? std::span<const std::uint8_t>(coverage, kWidth * kHeight) : std::span<const std::uint8_t>{};
    const auto target = nullBgra ? std::span<std::uint8_t>{} : std::span<std::uint8_t>(probe);
    const bool accepted =
        SpotHealing::HealForWork(target, source, width, height, stride, 1.0f, SpotHealingMode::ContentAware, 7);
    return !accepted && Same(probe, before);
}
} // namespace

int RunHealWorkChecks() noexcept
{
    gFailures = 0;

    constexpr std::array<std::pair<SpotHealingMode, const char*>, 3> modes{{
        {SpotHealingMode::ContentAware, "constant.content-aware"},
        {SpotHealingMode::CreateTexture, "constant.create-texture"},
        {SpotHealingMode::ProximityMatch, "constant.proximity-match"},
    }};
    const auto originalOwner = std::make_unique<Pixels>();
    auto& original = *originalOwner;
    Coverage coverage{};
    MakeBlemish(original, coverage);
    const auto expectedOwner = std::make_unique<Pixels>(original);
    auto& expected = *expectedOwner;
    for (std::size_t y = 0; y < kHeight; ++y)
    {
        for (std::size_t x = 0; x < kWidth; ++x)
        {
            if (coverage[y * kWidth + x] == 0)
            {
                continue;
            }
            const std::size_t offset = y * kStride + x * 4;
            expected[offset] = 128;
            expected[offset + 1] = 128;
            expected[offset + 2] = 128;
            expected[offset + 3] = 255;
        }
    }
    for (const auto& [mode, label] : modes)
    {
        const auto actualOwner = std::make_unique<Pixels>(original);
        auto& actual = *actualOwner;
        const auto modeExpectedOwner = std::make_unique<Pixels>(expected);
        auto& modeExpected = *modeExpectedOwner;
        if (mode == SpotHealingMode::CreateTexture)
        {
            // Independent analytic reference for legacy HealPixels.c's detail/output equations.
            // The 9x9 ring has 56 gray128 pixels; 20 touch the red hole, with neighbour-average
            // deviations 128/4 for B/G and -127/4 for R. Its membrane is identically gray128.
            // Grain amplitudes are sqrt(20/56)*(128 or 127)/4*0.9, keyed by seed0x1234 and y*32+x.
            // These literals were derived without executing either healing implementation.
            constexpr std::array<std::array<std::uint8_t, 4>, 25> textured{{
                {131, 131, 131, 255}, {135, 135, 135, 255}, {129, 129, 129, 255}, {128, 128, 128, 255},
                {154, 154, 154, 255}, {148, 148, 148, 255}, {125, 125, 125, 255}, {128, 128, 128, 255},
                {148, 148, 148, 255}, {114, 114, 114, 255}, {124, 124, 124, 255}, {120, 120, 121, 255},
                {145, 145, 145, 255}, {117, 117, 117, 255}, {115, 115, 115, 255}, {157, 157, 156, 255},
                {115, 115, 115, 255}, {101, 101, 101, 255}, {125, 125, 125, 255}, {122, 122, 122, 255},
                {123, 123, 123, 255}, {130, 130, 129, 255}, {138, 138, 138, 255}, {149, 149, 149, 255},
                {130, 130, 130, 255},
            }};
            for (std::size_t y = 10; y < 15; ++y)
                for (std::size_t x = 10; x < 15; ++x)
                    for (std::size_t channel = 0; channel < 4; ++channel)
                        modeExpected[y * kStride + x * 4 + channel] = textured[(y - 10) * 5 + x - 10][channel];
        }
        const bool completed =
            SpotHealing::HealForWork(actual, coverage, kWidth, kHeight, kStride, 1.0f, mode, 0x1234U);
        Expect(completed && Same(actual, modeExpected), label);
    }

    constexpr std::array<std::pair<SpotHealing::WorkStage, const char*>, 6> preOutputStages{{
        {SpotHealing::WorkStage::Role, "cancel.role"},
        {SpotHealing::WorkStage::RingRows, "cancel.ring-rows"},
        {SpotHealing::WorkStage::RingColumns, "cancel.ring-columns"},
        {SpotHealing::WorkStage::PatchSearch, "cancel.patch-search"},
        {SpotHealing::WorkStage::Statistics, "cancel.statistics"},
        {SpotHealing::WorkStage::Solve, "cancel.actual-solve"},
    }};
    for (std::size_t i = 0; i < preOutputStages.size(); ++i)
    {
        const auto actualOwner = std::make_unique<Pixels>(original);
        auto& actual = *actualOwner;
        CancelAt cancel{.target = preOutputStages[i].first};
        const SpotHealing::WorkObserver observer{CancelOnStage, &cancel};
        const bool completed =
            SpotHealing::HealForWork(actual, coverage, kWidth, kHeight, kStride, 1.0f, SpotHealingMode::ContentAware,
                                     0x1234U, cancel.source.Token(), observer);
        Expect(!completed && cancel.observed && Same(actual, original), preOutputStages[i].second);
    }
    {
        const auto actualOwner = std::make_unique<Pixels>(original);
        auto& actual = *actualOwner;
        CancelSource cancel;
        cancel.Cancel();
        const bool completed = SpotHealing::HealForWork(actual, coverage, kWidth, kHeight, kStride, 1.0f,
                                                        SpotHealingMode::ContentAware, 0x1234U, cancel.Token());
        Expect(!completed && Same(actual, original), "cancel.pre-requested");
    }
    {
        const auto actualOwner = std::make_unique<Pixels>(original);
        auto& actual = *actualOwner;
        CancelAt cancel{.target = SpotHealing::WorkStage::Output};
        const SpotHealing::WorkObserver observer{CancelOnStage, &cancel};
        const bool completed =
            SpotHealing::HealForWork(actual, coverage, kWidth, kHeight, kStride, 1.0f, SpotHealingMode::ContentAware,
                                     0x1234U, cancel.source.Token(), observer);
        Expect(!completed && cancel.observed && Same(actual, original), "cancel.output-first-boundary");
    }
    {
        const auto actualOwner = std::make_unique<Pixels>(original);
        auto& actual = *actualOwner;
        Coverage empty{};
        const bool completed = SpotHealing::HealForWork(actual, empty, kWidth, kHeight, kStride, 1.0f,
                                                        SpotHealingMode::ContentAware, 0x1234U);
        Expect(completed && Same(actual, original), "empty-coverage");
    }

    const auto sentinelOwner = std::make_unique<Pixels>();
    auto& sentinel = *sentinelOwner;
    sentinel.fill(0xA5);
    Coverage validCoverage{};
    validCoverage.fill(0);
    Expect(RejectsWithoutWrite(validCoverage.data(), 1, 1, 4, sentinel, true), "invalid.null-bgra");
    Expect(RejectsWithoutWrite(nullptr, 1, 1, 4, sentinel), "invalid.null-coverage");
    Expect(RejectsWithoutWrite(validCoverage.data(), 0, 1, 4, sentinel), "invalid.zero-width");
    Expect(RejectsWithoutWrite(validCoverage.data(), 1, 0, 4, sentinel), "invalid.zero-height");
    Expect(RejectsWithoutWrite(validCoverage.data(), 1, 2, (std::numeric_limits<std::size_t>::max)(), sentinel),
           "invalid.stride-span-overflow");
    Expect(RejectsWithoutWrite(validCoverage.data(), static_cast<std::size_t>(kMaxCanvasSide) + 1, 1,
                               (static_cast<std::size_t>(kMaxCanvasSide) + 1) * 4, sentinel),
           "invalid.canvas-side-cap");
    Expect(RejectsWithoutWrite(validCoverage.data(), 10'001, 10'000, 40'004, sentinel), "invalid.layer-pixel-cap");

    const std::size_t lastRowExtent = (kHeight - 1) * kStride + kWidth * 4;
    Coverage noCoverage{};
    const auto extentProbeOwner = std::make_unique<Pixels>(sentinel);
    auto& extentProbe = *extentProbeOwner;
    const bool shortLastPixelAccepted =
        SpotHealing::HealForWork(std::span<std::uint8_t>(extentProbe.data(), lastRowExtent - 1), noCoverage, kWidth,
                                 kHeight, kStride, 1.0f, SpotHealingMode::ContentAware, 7);
    Expect(!shortLastPixelAccepted && Same(extentProbe, sentinel), "span.short-last-actual-pixel");
    extentProbe = sentinel;
    const bool exactExtentAccepted =
        SpotHealing::HealForWork(std::span<std::uint8_t>(extentProbe.data(), lastRowExtent), noCoverage, kWidth,
                                 kHeight, kStride, 1.0f, SpotHealingMode::ContentAware, 7);
    Expect(exactExtentAccepted && Same(extentProbe, sentinel), "span.exact-last-row-extent");

    const auto spanProbeOwner = std::make_unique<Pixels>(sentinel);
    auto& spanProbe = *spanProbeOwner;
    const bool shortCoverageAccepted = SpotHealing::HealForWork(
        spanProbe, std::span<const std::uint8_t>(validCoverage.data(), validCoverage.size() - 1), kWidth, kHeight,
        kStride, 1.0f, SpotHealingMode::ContentAware, 7);
    Expect(!shortCoverageAccepted && Same(spanProbe, sentinel), "span.coverage-one-byte-short");

    spanProbe = sentinel;
    const bool shortStrideAccepted = SpotHealing::HealForWork(spanProbe, validCoverage, kWidth, kHeight, kWidth * 4 - 1,
                                                              1.0f, SpotHealingMode::ContentAware, 7);
    Expect(!shortStrideAccepted && Same(spanProbe, sentinel), "span.row-stride-one-byte-short");

    constexpr float invalidOpacities[] = {
        -0.01f,
        1.01f,
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity(),
    };
    constexpr const char* invalidOpacityNames[] = {
        "invalid.opacity-negative",
        "invalid.opacity-above-one",
        "invalid.opacity-nan",
        "invalid.opacity-infinity",
    };
    for (std::size_t i = 0; i < std::size(invalidOpacities); ++i)
    {
        const auto actualOwner = std::make_unique<Pixels>(sentinel);
        auto& actual = *actualOwner;
        const bool completed = SpotHealing::HealForWork(actual, validCoverage, kWidth, kHeight, kStride,
                                                        invalidOpacities[i], SpotHealingMode::ContentAware, 7);
        Expect(!completed && Same(actual, sentinel), invalidOpacityNames[i]);
    }
    {
        const auto actualOwner = std::make_unique<Pixels>(sentinel);
        auto& actual = *actualOwner;
        const bool completed = SpotHealing::HealForWork(actual, validCoverage, kWidth, kHeight, kStride, 1.0f,
                                                        static_cast<SpotHealingMode>(0xFF), 7);
        Expect(!completed && Same(actual, sentinel), "invalid.mode-enum");
    }
    {
        const auto actualOwner = std::make_unique<Pixels>(original);
        auto& actual = *actualOwner;
        const bool completed = SpotHealing::HealForWork(actual, coverage, kWidth, kHeight, kStride, 0.0f,
                                                        SpotHealingMode::ContentAware, 0x1234U);
        Expect(completed && Same(actual, original), "opacity.zero-preserves-every-byte");
    }

    {
        const auto originalLargeOwner = std::make_unique<LargePixels>();
        auto& originalLarge = *originalLargeOwner;
        const auto actualLargeOwner = std::make_unique<LargePixels>();
        auto& actualLarge = *actualLargeOwner;
        const auto coverageLargeOwner = std::make_unique<LargeCoverage>();
        auto& coverageLarge = *coverageLargeOwner;
        MakeLargeBlemish(originalLarge, coverageLarge);
        actualLarge = originalLarge;
        CancelAt cancel{.target = SpotHealing::WorkStage::SolveCoarse};
        const SpotHealing::WorkObserver observer{CancelOnStage, &cancel};
        const bool completed =
            SpotHealing::HealForWork(actualLarge, coverageLarge, kLargeWidth, kLargeHeight, kLargeStride, 1.0f,
                                     SpotHealingMode::ContentAware, 0x1234U, cancel.source.Token(), observer);
        Expect(!completed && cancel.observed && Same(actualLarge, originalLarge), "cancel.real-coarse-solve");
    }

    {
        const auto actualOwner = std::make_unique<Pixels>(original);
        auto& actual = *actualOwner;
        CancelAfter cancel{.target = SpotHealing::WorkStage::Output, .targetCount = 6};
        const SpotHealing::WorkObserver observer{CancelAfterStage, &cancel};
        const bool completed =
            SpotHealing::HealForWork(actual, coverage, kWidth, kHeight, kStride, 1.0f, SpotHealingMode::ContentAware,
                                     0x1234U, cancel.source.Token(), observer);
        Expect(!completed && cancel.observedCount == cancel.targetCount, "cancel.partial-output-after-rows");

        const auto expectedPartialOwner = std::make_unique<Pixels>(original);
        auto& expectedPartial = *expectedPartialOwner;
        for (std::size_t y = 10; y < 13; ++y)
        {
            for (std::size_t x = 10; x < 15; ++x)
            {
                const std::size_t offset = y * kStride + x * 4;
                expectedPartial[offset] = 128;
                expectedPartial[offset + 1] = 128;
                expectedPartial[offset + 2] = 128;
                expectedPartial[offset + 3] = 255;
            }
        }
        Expect(Same(actual, expectedPartial), "cancel.partial-output-analytic-prefix");

        bool boundariesUnchanged = true;
        for (std::size_t y = 0; y < kHeight; ++y)
        {
            for (std::size_t x = 0; x < kWidth; ++x)
            {
                if (coverage[y * kWidth + x] == 0 || y >= 13)
                {
                    const std::size_t offset = y * kStride + x * 4;
                    boundariesUnchanged = boundariesUnchanged && actual[offset] == original[offset] &&
                                          actual[offset + 1] == original[offset + 1] &&
                                          actual[offset + 2] == original[offset + 2] &&
                                          actual[offset + 3] == original[offset + 3];
                }
            }
            for (std::size_t x = kWidth * 4; x < kStride; ++x)
            {
                boundariesUnchanged = boundariesUnchanged && actual[y * kStride + x] == original[y * kStride + x];
            }
        }
        Expect(boundariesUnchanged, "cancel.partial-output-selection-and-padding-untouched");
    }

    return gFailures == 0 ? 0 : 1;
}
