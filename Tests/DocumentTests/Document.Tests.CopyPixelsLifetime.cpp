#include "Document.Tests.CopyPixelsLifetime.h"

#include "CopyPixelsWork.h"
#include "PixelBuffer.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <span>
#include <utility>

namespace
{
int gFailures = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s copypixelslifetime.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

std::shared_ptr<const void> MakeLifetime(std::uint64_t marker)
{
    return std::make_shared<const std::uint64_t>(marker);
}

Raster OwnedBgra(std::span<const std::uint8_t> pixels, const std::shared_ptr<const void>& lifetime)
{
    PixelBuffer bytes = PixelBuffer::TryAllocate(pixels.size());
    if (!bytes || bytes.Size() != pixels.size())
        return {};
    std::memcpy(bytes.Data(), pixels.data(), pixels.size());
    return Raster::Adopt(2, 1, RasterFormat::Bgra8888, std::move(bytes), lifetime);
}

CopyPixelsInput Input(Raster source)
{
    CopyPixelsInput input;
    input.source = std::move(source);
    input.placement.size = Vec2{static_cast<double>(input.source.Width()), static_cast<double>(input.source.Height())};
    input.placement.sampling = LayerSampling::Nearest;
    input.canvasWidth = input.source.Width();
    input.canvasHeight = input.source.Height();
    return input;
}

bool HasLiteralPixels(const Raster& image, std::span<const std::uint8_t> expected) noexcept
{
    return image.HasStorage() && image.Width() == 2 && image.Height() == 1 &&
           image.Format() == RasterFormat::Bgra8888 && image.IsMaterialized() && image.ByteSize() == expected.size() &&
           image.Data() && std::equal(expected.begin(), expected.end(), image.Data());
}

constexpr std::array<std::uint8_t, 8> kSourcePixels{11, 21, 31, 255, 41, 51, 61, 255};
constexpr std::array<std::uint8_t, 8> kExpectedPixels{11, 21, 31, 255, 41, 51, 61, 255};

void ExplicitOutputOwnerFollowsCopiedRasterAndWeakStorageControl()
{
    std::weak_ptr<const void> weakSourceOwner;
    std::weak_ptr<const void> weakOutputOwner;
    std::weak_ptr<const void> weakOutputStorage;
    Raster copiedOutput;
    CopyPixelsResult result;
    CopyPixelsInput input;
    {
        auto sourceOwner = MakeLifetime(0x53524345u);
        auto outputOwner = MakeLifetime(0x4f555450u);
        weakSourceOwner = sourceOwner;
        weakOutputOwner = outputOwner;
        input = Input(OwnedBgra(kSourcePixels, sourceOwner));
        input.outputLifetime = outputOwner;
        sourceOwner.reset();
        outputOwner.reset();

        result = RunCopyPixels(input);
        if (result.status == CopyPixelsStatus::Ready && result.clipboard)
        {
            copiedOutput = result.clipboard->image;
            weakOutputStorage = copiedOutput.WeakStorageForTest();
        }
    }

    const bool outputIsIndependentLiteral = result.status == CopyPixelsStatus::Ready && result.clipboard &&
                                            result.clipboard->origin == Vec2{0, 0} &&
                                            HasLiteralPixels(copiedOutput, kExpectedPixels);
    result.clipboard.reset();
    input.source = {};
    input.outputLifetime.reset();

    Expect(outputIsIndependentLiteral, "explicit.ready-copied-raster-has-independent-literal-and-origin-zero");
    Expect(weakSourceOwner.expired(), "explicit.output-does-not-inherit-distinct-source-owner");
    Expect(!weakOutputOwner.expired() && !weakOutputStorage.expired(),
           "explicit.copied-output-raster-retains-caller-owner");

    copiedOutput = {};
    Expect(weakOutputStorage.expired() && !weakOutputOwner.expired(),
           "explicit.final-raster-release-expires-storage-but-weak-control-retains-owner");
    weakOutputStorage.reset();
    Expect(weakOutputOwner.expired(), "explicit.final-weak-storage-control-releases-caller-owner");
}

void OrdinaryNoOwnerPathKeepsLiteralResult()
{
    const Raster source = Raster::Make(2, 1, RasterFormat::Bgra8888, kSourcePixels.data(), kSourcePixels.size());
    CopyPixelsInput input = Input(source);
    const CopyPixelsResult result = RunCopyPixels(input);
    const bool unchanged = result.status == CopyPixelsStatus::Ready && result.clipboard &&
                           result.clipboard->origin == Vec2{0, 0} &&
                           HasLiteralPixels(result.clipboard->image, kExpectedPixels);
    Expect(unchanged, "ordinary.no-owner-path-returns-ready-literal-and-origin-zero");
}

void SourceOwnerIsNotInheritedWithoutExplicitOutputOwner()
{
    std::weak_ptr<const void> weakSourceOwner;
    Raster copiedOutput;
    CopyPixelsResult result;
    CopyPixelsInput input;
    {
        auto sourceOwner = MakeLifetime(0x4e4f534fu);
        weakSourceOwner = sourceOwner;
        input = Input(OwnedBgra(kSourcePixels, sourceOwner));
        sourceOwner.reset();
        result = RunCopyPixels(input);
        if (result.status == CopyPixelsStatus::Ready && result.clipboard)
            copiedOutput = result.clipboard->image;
    }

    const bool outputIsLiteral = HasLiteralPixels(copiedOutput, kExpectedPixels);
    result.clipboard.reset();
    input.source = {};
    Expect(outputIsLiteral && weakSourceOwner.expired(),
           "inheritance.no-owner-output-copies-pixels-without-retaining-source-owner");
}

void CancelledAndInvalidCallsPreserveSourceAndDoNotRetainOutputOwner()
{
    const Raster source = Raster::Make(2, 1, RasterFormat::Bgra8888, kSourcePixels.data(), kSourcePixels.size());

    auto cancelledOwner = MakeLifetime(0x43414e43u);
    std::weak_ptr<const void> weakCancelledOwner = cancelledOwner;
    CopyPixelsInput cancelledInput = Input(source);
    cancelledInput.outputLifetime = cancelledOwner;
    CancelSource cancellation;
    (void)cancellation.Cancel();
    const CopyPixelsResult cancelled = RunCopyPixels(cancelledInput, cancellation.Token());
    cancelledInput.outputLifetime.reset();
    cancelledOwner.reset();
    Expect(cancelled.status == CopyPixelsStatus::Cancelled && !cancelled.clipboard && weakCancelledOwner.expired() &&
               HasLiteralPixels(source, kSourcePixels),
           "failure.cancelled-call-preserves-source-and-does-not-retain-output-owner");

    auto invalidOwner = MakeLifetime(0x494e5641u);
    std::weak_ptr<const void> weakInvalidOwner = invalidOwner;
    CopyPixelsInput invalidInput = Input(source);
    invalidInput.outputLifetime = invalidOwner;
    invalidInput.canvasWidth = 0;
    const CopyPixelsResult invalid = RunCopyPixels(invalidInput);
    invalidInput.outputLifetime.reset();
    invalidOwner.reset();
    Expect(invalid.status == CopyPixelsStatus::Invalid && !invalid.clipboard && weakInvalidOwner.expired() &&
               HasLiteralPixels(source, kSourcePixels),
           "failure.invalid-call-preserves-source-and-does-not-retain-output-owner");
}
} // namespace

int RunCopyPixelsLifetimeTests()
{
    gFailures = 0;
    ExplicitOutputOwnerFollowsCopiedRasterAndWeakStorageControl();
    OrdinaryNoOwnerPathKeepsLiteralResult();
    SourceOwnerIsNotInheritedWithoutExplicitOutputOwner();
    CancelledAndInvalidCallsPreserveSourceAndDoNotRetainOutputOwner();
    return gFailures;
}