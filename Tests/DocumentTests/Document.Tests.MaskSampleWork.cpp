#include "Document.Tests.MaskSampleWork.h"

#include "CanvasSize.h"
#include "ImageFilters.h"
#include "RasterWork.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <span>
#include <stop_token>

namespace
{
int gFailures = 0;

void Expect(bool condition, const char* name)
{
    std::printf("%s masksamplework.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

Raster Gray(int width, int height, std::span<const std::uint8_t> values)
{
    return Raster::Make(width, height, RasterFormat::Gray8, values.data(), values.size());
}

bool Equals(const Raster& raster, int width, int height, std::span<const std::uint8_t> expected)
{
    if (!raster.HasStorage() || raster.Format() != RasterFormat::Gray8 || raster.Width() != width ||
        raster.Height() != height || raster.Data() == nullptr ||
        static_cast<std::size_t>(raster.Stride()) * static_cast<std::size_t>(height) != expected.size())
    {
        return false;
    }
    for (std::size_t index = 0; index < expected.size(); ++index)
    {
        if (raster.Data()[index] != expected[index])
        {
            return false;
        }
    }
    return true;
}

void NearestIdentityAndTranslatedClip()
{
    constexpr std::array<std::uint8_t, 6> sourcePixels{10, 80, 200, 30, 120, 250};
    const Raster source = Gray(3, 2, sourcePixels);
    LayerTransform transform;
    transform.size = Vec2{3, 2};
    transform.sampling = LayerSampling::Nearest;
    constexpr std::array<std::uint8_t, 6> identityExpected{10, 80, 200, 30, 120, 250};
    const Raster identity = ImageFilters::RenderMaskPixelsForWork(source, transform, 3, 2, 99);
    Expect(Equals(identity, 3, 2, identityExpected), "nearest.identity-literal");

    transform.origin = Vec2{-1, 1};
    constexpr std::array<std::uint8_t, 9> clippedExpected{99, 99, 99, 80, 200, 99, 120, 250, 99};
    const Raster clipped = ImageFilters::RenderMaskPixelsForWork(source, transform, 3, 3, 99);
    Expect(Equals(clipped, 3, 3, clippedExpected), "nearest.translated-negative-origin-clips-with-background");
}

void NearestScaledAndFlipped()
{
    constexpr std::array<std::uint8_t, 4> sourcePixels{10, 90, 150, 230};
    const Raster source = Gray(2, 2, sourcePixels);
    LayerTransform transform;
    transform.size = Vec2{4, 4};
    transform.flipX = true;
    transform.sampling = LayerSampling::Nearest;
    constexpr std::array<std::uint8_t, 16> flipXExpected{90,  90,  10,  10,  90,  90,  10,  10,
                                                         230, 230, 150, 150, 230, 230, 150, 150};
    const Raster flipX = ImageFilters::RenderMaskPixelsForWork(source, transform, 4, 4, 7);
    Expect(Equals(flipX, 4, 4, flipXExpected), "nearest.scaled-horizontal-flip-literal");

    transform.size = Vec2{2, 2};
    transform.flipX = false;
    transform.flipY = true;
    constexpr std::array<std::uint8_t, 4> flipYExpected{150, 230, 10, 90};
    const Raster flipY = ImageFilters::RenderMaskPixelsForWork(source, transform, 2, 2, 7);
    Expect(Equals(flipY, 2, 2, flipYExpected), "nearest.vertical-flip-literal");
}

void SmoothAndHighLiteralSamples()
{
    constexpr std::array<std::uint8_t, 4> sourcePixels{0, 100, 200, 255};
    const Raster source = Gray(2, 2, sourcePixels);
    LayerTransform transform;
    transform.size = Vec2{4, 4};
    transform.sampling = LayerSampling::Smooth;
    constexpr std::array<std::uint8_t, 16> smoothExpected{0,   25,  75,  100, 50,  72,  117, 139,
                                                          150, 167, 200, 216, 200, 214, 241, 255};
    const Raster smooth = ImageFilters::RenderMaskPixelsForWork(source, transform, 4, 4, 19);
    Expect(Equals(smooth, 4, 4, smoothExpected), "smooth.scaled-bilinear-pixel-center-literals");

    transform.sampling = LayerSampling::High;
    const Raster high = ImageFilters::RenderMaskPixelsForWork(source, transform, 4, 4, 19);
    Expect(Equals(high, 4, 4, smoothExpected), "high.collapses-to-smooth-literal");

    transform.sampling = LayerSampling::Nearest;
    constexpr std::array<std::uint8_t, 16> nearestExpected{0,   0,   100, 100, 0,   0,   100, 100,
                                                           200, 200, 255, 255, 200, 200, 255, 255};
    const Raster nearest = ImageFilters::RenderMaskPixelsForWork(source, transform, 4, 4, 19);
    Expect(Equals(nearest, 4, 4, nearestExpected) && smooth.HasStorage() && smooth.Data()[5] == 72 &&
               nearest.HasStorage() && nearest.Data()[5] == 0,
           "smooth.literal-differs-from-nearest");
}

void EdgeAndInvalidTransformBackground()
{
    constexpr std::array<std::uint8_t, 2> sourcePixels{40, 180};
    const Raster source = Gray(2, 1, sourcePixels);
    LayerTransform transform;
    transform.origin = Vec2{1, 0};
    transform.size = Vec2{2, 1};
    transform.sampling = LayerSampling::Nearest;
    constexpr std::array<std::uint8_t, 4> expected{77, 40, 180, 77};
    const Raster edge = ImageFilters::RenderMaskPixelsForWork(source, transform, 4, 1, 77);
    Expect(Equals(edge, 4, 1, expected), "edge.outside-placement-uses-background");

    transform.size.x = 0;
    constexpr std::array<std::uint8_t, 6> uniformExpected{33, 33, 33, 33, 33, 33};
    const Raster invalid = ImageFilters::RenderMaskPixelsForWork(source, transform, 3, 2, 33);
    Expect(Equals(invalid, 3, 2, uniformExpected), "invalid-transform-after-source-read-is-uniform-background");
}

void SparsePrivateReadAndClipping()
{
    constexpr std::array<std::uint8_t, 6> basePixels{10, 80, 200, 30, 120, 250};
    const Raster base = Gray(3, 2, basePixels);
    const Raster sparse = Raster::MakeSparse(3, 2, RasterFormat::Gray8, base, 1, 0, {}, 17);
    LayerTransform transform;
    transform.origin = Vec2{-1, 0};
    transform.size = Vec2{3, 2};
    transform.sampling = LayerSampling::Nearest;
    constexpr std::array<std::uint8_t, 6> expected{10, 80, 99, 30, 120, 99};
    const Raster rendered = ImageFilters::RenderMaskPixelsForWork(sparse, transform, 3, 2, 99);
    // Keep `sparse` alive through the post-read check; its shared materialization cache must stay absent.
    Expect(Equals(rendered, 3, 2, expected) && sparse.IsSparse() && !sparse.IsMaterialized(),
           "sparse.private-read-clipped-source-does-not-materialize");
}

void CancellationRefusal()
{
    constexpr std::array<std::uint8_t, 4> sourcePixels{1, 2, 3, 4};
    const Raster source = Gray(2, 2, sourcePixels);
    LayerTransform transform;
    transform.size = Vec2{2, 2};

    CancelSource local;
    static_cast<void>(local.Cancel());
    const Raster localCancelled = ImageFilters::RenderMaskPixelsForWork(source, transform, 2, 2, 9, local.Token());
    Expect(!localCancelled.HasStorage(), "cancel.pre-cancel-local-token-returns-empty");

    std::stop_source shutdown;
    static_cast<void>(shutdown.request_stop());
    const CancelToken shutdownToken{{}, shutdown.get_token()};
    const Raster shutdownCancelled = ImageFilters::RenderMaskPixelsForWork(source, transform, 2, 2, 9, shutdownToken);
    Expect(!shutdownCancelled.HasStorage(), "cancel.pre-cancel-shutdown-token-returns-empty");
}

void InvalidInputsAndBudgetRefusal()
{
    constexpr std::array<std::uint8_t, 4> grayPixels{1, 2, 3, 4};
    constexpr std::array<std::uint8_t, 16> colorPixels{0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255};
    const Raster gray = Gray(2, 2, grayPixels);
    const Raster color = Raster::Make(2, 2, RasterFormat::Bgra8888, colorPixels.data(), colorPixels.size());
    LayerTransform transform;
    transform.size = Vec2{2, 2};

    Expect(!ImageFilters::RenderMaskPixelsForWork(gray, transform, 0, 2, 9).HasStorage(),
           "invalid.zero-output-width-refused");
    Expect(!ImageFilters::RenderMaskPixelsForWork(gray, transform, 2, -1, 9).HasStorage(),
           "invalid.negative-output-height-refused");
    Expect(!ImageFilters::RenderMaskPixelsForWork(gray, transform, kMaxCanvasSide + 1, 1, 9).HasStorage(),
           "invalid.output-side-over-canvas-limit-refused");
    Expect(!ImageFilters::RenderMaskPixelsForWork(Raster{}, transform, 2, 2, 9).HasStorage(),
           "invalid.empty-source-refused");
    Expect(!ImageFilters::RenderMaskPixelsForWork(color, transform, 2, 2, 9).HasStorage(),
           "invalid.non-gray-source-refused");

    // Both sides are legal; only the 100,010,000-pixel output budget rejects this request.
    Expect(!ImageFilters::RenderMaskPixelsForWork(gray, transform, 10'001, 10'000, 9).HasStorage(),
           "budget.output-area-over-100-million-refused");
}
} // namespace

int RunDocumentMaskSampleWorkTests()
{
    gFailures = 0;
    NearestIdentityAndTranslatedClip();
    NearestScaledAndFlipped();
    SmoothAndHighLiteralSamples();
    EdgeAndInvalidTransformBackground();
    SparsePrivateReadAndClipping();
    CancellationRefusal();
    InvalidInputsAndBudgetRefusal();
    return gFailures;
}
