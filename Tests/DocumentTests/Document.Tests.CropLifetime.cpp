#include "Document.Tests.CropLifetime.h"

#include "Adjust.h"
#include "PixelBuffer.h"
#include "RasterWork.h"

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
    std::printf("%s croplifetime.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

std::shared_ptr<const void> MakeLifetime(std::uint64_t marker)
{
    return std::make_shared<const std::uint64_t>(marker);
}

Raster ChargedBgra(int width, int height, std::span<const std::uint8_t> pixels,
                   const std::shared_ptr<const void>& lifetime)
{
    PixelBuffer bytes = PixelBuffer::TryAllocate(pixels.size());
    if (!bytes || bytes.Size() != pixels.size())
    {
        return {};
    }
    std::memcpy(bytes.Data(), pixels.data(), pixels.size());
    return Raster::Adopt(width, height, RasterFormat::Bgra8888, std::move(bytes), lifetime);
}

bool HasBytes(const Raster& image, int width, int height, RasterFormat format,
              std::span<const std::uint8_t> expected) noexcept
{
    return image.HasStorage() && image.Width() == width && image.Height() == height && image.Format() == format &&
           image.IsMaterialized() && image.ByteSize() == expected.size() &&
           std::equal(expected.begin(), expected.end(), image.Data());
}

constexpr std::array<std::uint8_t, 48> kTrimSource{0, 0, 0, 0, 0,  0,  0,  0,   0,  0,  0,  0,   0, 0, 0, 0,
                                                   0, 0, 0, 0, 11, 21, 31, 255, 41, 51, 61, 128, 0, 0, 0, 0,
                                                   0, 0, 0, 0, 0,  0,  0,  0,   0,  0,  0,  0,   0, 0, 0, 0};
constexpr std::array<std::uint8_t, 8> kTrimPixels{11, 21, 31, 255, 41, 51, 61, 128};

void CropRasterPixelsAndLifetime()
{
    constexpr std::array<std::uint8_t, 24> input{1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12,
                                                 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24};
    constexpr std::array<std::uint8_t, 8> expected{5, 6, 7, 8, 9, 10, 11, 12};
    const Raster source = Raster::Make(3, 2, RasterFormat::Bgra8888, input.data(), input.size());
    const Raster ordinary = CropRasterForWork(source, {1, 0, 2, 1}, 0);

    auto ownerToken = MakeLifetime(0x43524f50u);
    std::weak_ptr<const void> weakOwnerToken = ownerToken;
    Raster charged = CropRasterForWork(source, {1, 0, 2, 1}, 0, {}, ownerToken);
    std::weak_ptr<const void> weakStorage = charged.WeakStorageForTest();
    ownerToken.reset();

    Expect(HasBytes(ordinary, 2, 1, RasterFormat::Bgra8888, expected) &&
               HasBytes(charged, 2, 1, RasterFormat::Bgra8888, expected) && ordinary.Identity() != source.Identity() &&
               charged.Identity() != source.Identity(),
           "crop.ordinary-and-charged-pixels-are-independent-literals");
    Expect(!weakOwnerToken.expired(), "crop.output-retains-the-supplied-ownerToken");
    charged = {};
    Expect(!weakOwnerToken.expired() && weakStorage.expired(), "crop.last-weak-storage-control-retains-supplied-owner");
    weakStorage.reset();
    Expect(weakOwnerToken.expired(), "crop.final-storage-control-releases-supplied-owner");
}

void TrimPixelsPlacementAndSourceLifetime()
{
    const LayerTransform placed{Vec2{10, 20}, Vec2{8, 6}};
    const Raster ordinarySource = Raster::Make(4, 3, RasterFormat::Bgra8888, kTrimSource.data(), kTrimSource.size());
    const Adjust::Trimmed ordinary = Adjust::TrimmedToAlpha(ordinarySource, placed);

    auto outputOwnerToken = MakeLifetime(0x5452494du);
    std::weak_ptr<const void> weakOutputOwnerToken = outputOwnerToken;
    std::weak_ptr<const void> weakOutputStorage;
    std::weak_ptr<const void> weakSourceOwnerToken;
    Adjust::Trimmed charged;
    {
        auto sourceOwnerToken = MakeLifetime(0x534f5552u);
        weakSourceOwnerToken = sourceOwnerToken;
        const Raster source = ChargedBgra(4, 3, kTrimSource, sourceOwnerToken);
        sourceOwnerToken.reset();
        charged = Adjust::TrimmedToAlpha(source, placed, {}, outputOwnerToken);
        weakOutputStorage = charged.image.WeakStorageForTest();
    }
    outputOwnerToken.reset();

    const bool ordinaryExpected = HasBytes(ordinary.image, 2, 1, RasterFormat::Bgra8888, kTrimPixels) &&
                                  ordinary.cropX == 1 && ordinary.cropY == 1 && ordinary.transform.origin.x == 12 &&
                                  ordinary.transform.origin.y == 22 && ordinary.transform.size.x == 4 &&
                                  ordinary.transform.size.y == 2;
    const bool chargedExpected = HasBytes(charged.image, 2, 1, RasterFormat::Bgra8888, kTrimPixels) &&
                                 charged.cropX == 1 && charged.cropY == 1 && charged.transform.origin.x == 12 &&
                                 charged.transform.origin.y == 22 && charged.transform.size.x == 4 &&
                                 charged.transform.size.y == 2;
    Expect(ordinaryExpected && chargedExpected, "trim.ordinary-and-charged-pixels-and-placement-literals");
    Expect(!weakOutputOwnerToken.expired() && !weakOutputStorage.expired(),
           "trim.cropped-storage-retains-caller-owner-and-storage-controls");
    Expect(weakSourceOwnerToken.expired(), "trim.cropped-output-does-not-inherit-source-lifetime");
    charged.image = {};
    Expect(!weakOutputOwnerToken.expired() && weakOutputStorage.expired(),
           "trim.last-weak-storage-control-retains-caller-owner");
    weakOutputStorage.reset();
    Expect(weakOutputOwnerToken.expired(), "trim.final-storage-control-releases-caller-owner");
}

void NoOpTrimDoesNotAdoptLifetime()
{
    constexpr std::array<std::uint8_t, 16> opaquePixels{1, 2, 3, 255, 4, 5, 6, 255, 7, 8, 9, 255, 10, 11, 12, 255};
    constexpr std::array<std::uint8_t, 16> transparentPixels{};
    const Raster full = Raster::Make(2, 2, RasterFormat::Bgra8888, opaquePixels.data(), opaquePixels.size());
    const Raster transparent =
        Raster::Make(2, 2, RasterFormat::Bgra8888, transparentPixels.data(), transparentPixels.size());

    auto fullOwner = MakeLifetime(1);
    std::weak_ptr<const void> weakFullOwner = fullOwner;
    const Adjust::Trimmed fullResult = Adjust::TrimmedToAlpha(full, LayerTransform{}, {}, fullOwner);
    fullOwner.reset();
    Expect(fullResult.image.Identity() == full.Identity() && weakFullOwner.expired(),
           "trim.full-bounds-alias-does-not-adopt-unused-lifetime");

    auto transparentOwner = MakeLifetime(2);
    std::weak_ptr<const void> weakTransparentOwner = transparentOwner;
    const Adjust::Trimmed transparentResult =
        Adjust::TrimmedToAlpha(transparent, LayerTransform{}, {}, transparentOwner);
    transparentOwner.reset();
    Expect(transparentResult.image.Identity() == transparent.Identity() && transparentResult.cropX == 0 &&
               transparentResult.cropY == 0 && weakTransparentOwner.expired(),
           "trim.transparent-image-alias-does-not-adopt-unused-lifetime");
}

void InvalidAndCancelledInputsRemainUnpublished()
{
    const Raster source = Raster::Make(4, 3, RasterFormat::Bgra8888, kTrimSource.data(), kTrimSource.size());
    const Raster gray = Raster::Make(2, 2, RasterFormat::Gray8, kTrimPixels.data(), 4);
    CancelSource cancelSource;
    (void)cancelSource.Cancel();

    auto invalidCropOwner = MakeLifetime(3);
    std::weak_ptr<const void> weakInvalidCropOwner = invalidCropOwner;
    const Raster invalidCrop = CropRasterForWork(source, {0, 0, 0, 1}, 0, {}, invalidCropOwner);
    invalidCropOwner.reset();

    auto cancelledCropOwner = MakeLifetime(4);
    std::weak_ptr<const void> weakCancelledCropOwner = cancelledCropOwner;
    const Raster cancelledCrop = CropRasterForWork(source, {0, 0, 2, 2}, 0, cancelSource.Token(), cancelledCropOwner);
    cancelledCropOwner.reset();

    auto invalidTrimOwner = MakeLifetime(5);
    std::weak_ptr<const void> weakInvalidTrimOwner = invalidTrimOwner;
    const Adjust::Trimmed invalidTrim = Adjust::TrimmedToAlpha(gray, LayerTransform{}, {}, invalidTrimOwner);
    invalidTrimOwner.reset();

    auto cancelledTrimOwner = MakeLifetime(6);
    std::weak_ptr<const void> weakCancelledTrimOwner = cancelledTrimOwner;
    const Adjust::Trimmed cancelledTrim =
        Adjust::TrimmedToAlpha(source, LayerTransform{}, cancelSource.Token(), cancelledTrimOwner);
    cancelledTrimOwner.reset();

    Expect(!invalidCrop.HasStorage() && !cancelledCrop.HasStorage() && !invalidTrim.image.HasStorage() &&
               !cancelledTrim.image.HasStorage(),
           "failure.invalid-and-cancelled-operations-publish-no-output");
    Expect(HasBytes(source, 4, 3, RasterFormat::Bgra8888, kTrimSource) && gray.HasStorage() && gray.Width() == 2 &&
               gray.Height() == 2,
           "failure.inputs-remain-readable-after-refusal");
    Expect(weakInvalidCropOwner.expired() && weakCancelledCropOwner.expired() && weakInvalidTrimOwner.expired() &&
               weakCancelledTrimOwner.expired(),
           "failure.refused-operations-do-not-retain-unused-lifetimes");
}

void ChargedMetadataAndTokenLifetime()
{
    constexpr std::array<std::uint8_t, 8> pixels{1, 2, 3, 255, 4, 5, 6, 255};
    constexpr std::uint64_t kChargedStorageMetadataBytes = 32;
    std::weak_ptr<const void> weakOriginalOwnerToken;
    Raster charged;
    {
        auto originalOwnerToken = MakeLifetime(0x52455356u);
        weakOriginalOwnerToken = originalOwnerToken;
        const Raster source = ChargedBgra(2, 1, pixels, originalOwnerToken);
        const Raster ordinary = CropRasterForWork(source, {0, 0, 2, 1}, 0);
        charged = CropRasterForWork(source, {0, 0, 2, 1}, 0, {}, originalOwnerToken);
        const bool metadataDelta = source.HasStorage() && ordinary.HasStorage() && charged.HasStorage() &&
                                   source.StorageRetirementMemoryBytes() > source.ByteSize() &&
                                   charged.StorageRetirementMemoryBytes() ==
                                       ordinary.StorageRetirementMemoryBytes() + kChargedStorageMetadataBytes &&
                                   charged.RetirementMemoryBytes() >= charged.ByteSize() + kChargedStorageMetadataBytes;
        Expect(metadataDelta, "metadata.original-charged-source-and-crop-add-fixed-32-byte-storage-metadata");
        originalOwnerToken.reset();
    }
    Expect(!weakOriginalOwnerToken.expired(), "metadata.output-retains-the-original-ownerToken-after-source-release");
    charged = {};
    Expect(weakOriginalOwnerToken.expired(), "metadata.output-release-releases-the-original-ownerToken");
}
} // namespace

int RunCropLifetimeTests()
{
    gFailures = 0;
    CropRasterPixelsAndLifetime();
    TrimPixelsPlacementAndSourceLifetime();
    NoOpTrimDoesNotAdoptLifetime();
    InvalidAndCancelledInputsRemainUnpublished();
    ChargedMetadataAndTokenLifetime();
    return gFailures;
}
