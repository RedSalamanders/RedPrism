#include "Document.Tests.CapturedCloneOffset.h"

#include "../Support/Support.Tests.DocumentFixture.h"
#include "BrushStroke.h"
#include "Document.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <utility>
#include <vector>

namespace
{
int gFailures = 0;
int gAssertions = 0;

void Expect(bool condition, const char* name) noexcept
{
    ++gAssertions;
    std::printf("%s document.captured-clone-offset.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

Raster SolidRaster(int width, int height, const std::array<std::uint8_t, 4>& color)
{
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
    for (std::size_t offset = 0; offset < pixels.size(); offset += color.size())
    {
        for (std::size_t channel = 0; channel < color.size(); ++channel)
            pixels[offset + channel] = color[channel];
    }
    return Raster::Make(width, height, RasterFormat::Bgra8888, pixels.data(), pixels.size());
}

Raster CompositeRaster()
{
    constexpr int width = 8;
    constexpr int height = 4;
    constexpr std::array<std::uint8_t, 4> left{11, 22, 33, 255};
    constexpr std::array<std::uint8_t, 4> right{44, 55, 66, 255};
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            const auto& color = x < 4 ? left : right;
            const std::size_t offset = (static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x)) * 4u;
            for (std::size_t channel = 0; channel < color.size(); ++channel)
                pixels[offset + channel] = color[channel];
        }
    }
    return Raster::Make(width, height, RasterFormat::Bgra8888, pixels.data(), pixels.size());
}

DocumentSession Session()
{
    constexpr int width = 8;
    constexpr int height = 4;
    DocumentSession session;
    DocumentFixture document;
    document.id = Guid::Generate();
    document.width = width;
    document.height = height;

    ImageLayer background = ImageLayer::Blank(L"Background", Vec2{width, height});
    background.asset = RasterAsset{SolidRaster(width, height, {200, 0, 0, 255}), {}, background.name};
    document.layers.push_back(std::move(background));

    ImageLayer active = ImageLayer::Blank(L"Active", Vec2{width, height});
    const Guid activeId = active.id;
    active.asset = RasterAsset{SolidRaster(width, height, {0, 0, 200, 255}), {}, active.name};
    document.layers.push_back(std::move(active));
    session.ReplaceDocument(std::move(document).Seal(), activeId);
    session.SetBrushEngine(&CpuBrushCoverage::Shared());
    session.brushSettings = BrushSettings{1, 1, 0, 0, 0, 1};
    session.cloneSettings = CloneSettings{true, false};
    session.SetCloneSource(Vec2{1, 1});
    return session;
}

std::array<std::uint8_t, 4> ActivePixel(const DocumentSession& session, int x, int y)
{
    std::array<std::uint8_t, 4> pixel{};
    const ImageLayer* active = session.ActiveLayer();
    if (active == nullptr || !active->asset.has_value())
        return pixel;
    active->asset->image.ReadRegion(x, y, 1, 1, pixel.data(), 4, 0);
    return pixel;
}

bool BeginCaptured(DocumentSession& session, const Raster* sample, Vec2 offset)
{
    const BrushSettings settings{1, 1, 0, 0, 0, 1};
    return session.BeginBrush(Vec2{6.5, 1.5}, BrushKind::Clone, sample, settings, false, offset);
}

void RefusalDoesNotPublish(const char* name, const Raster* sample, Vec2 offset)
{
    DocumentSession session = Session();
    const int undoCount = session.history.UndoCount();
    const auto revision = session.history.CurrentRevision();
    const auto original = session.ActiveLayer()->asset->image;
    const auto offsetBefore = session.CloneStrokeOffset({6.5, 1.5});
    const bool began = BeginCaptured(session, sample, offset);
    Expect(!began && session.PendingBrush() == nullptr && session.history.UndoCount() == undoCount &&
               session.history.CurrentRevision() == revision && session.ActiveLayer()->asset->image == original &&
               session.CloneStrokeOffset({6.5, 1.5}) == offsetBefore,
           name);
}

void CapturedCompositeAndOffsetAreStable()
{
    DocumentSession session = Session();
    const Raster composite = CompositeRaster();
    const int undoCount = session.history.UndoCount();
    const bool began = BeginCaptured(session, &composite, Vec2{-4, 0});
    Expect(began && session.PendingBrush() != nullptr, "begin-accepts-captured-composite-with-sample-all-layers-off");
    if (!began)
        return;

    // Target (6, 1) maps through the captured offset (-4, 0) to composite pixel (2, 1).
    // Later live source and alignment changes must not reinterpret this accepted stroke.
    session.SetCloneSource(Vec2{0, 3});
    session.cloneSettings = CloneSettings{false, true};
    session.FinishBrush();

    Expect(ActivePixel(session, 6, 1) == std::array<std::uint8_t, 4>{11, 22, 33, 255},
           "target-6-1-equals-composite-source-2-1-bgra-11-22-33-255");
    Expect(ActivePixel(session, 7, 1) == std::array<std::uint8_t, 4>{0, 0, 200, 255},
           "neighbor-7-1-retains-active-layer-bgra-0-0-200-255");
    Expect(session.PendingBrush() == nullptr && session.history.UndoCount() == undoCount + 1 &&
               session.history.UndoName() == L"Clone Stamp",
           "one-clone-history-entry-published");
}

void InvalidCapturedInputsAreRefused()
{
    const Raster valid = CompositeRaster();
    const Raster empty;
    const Raster wrongSize = SolidRaster(7, 4, {1, 2, 3, 255});
    const Raster gray = Raster::Make(8, 4, RasterFormat::Gray8);
    const Vec2 validOffset{-4, 0};
    RefusalDoesNotPublish("missing-composite-refuses-without-brush-or-history", nullptr, validOffset);
    RefusalDoesNotPublish("empty-composite-refuses-without-brush-or-history", &empty, validOffset);
    RefusalDoesNotPublish("wrong-size-composite-refuses-without-brush-or-history", &wrongSize, validOffset);
    RefusalDoesNotPublish("gray8-composite-refuses-without-brush-or-history", &gray, validOffset);
    RefusalDoesNotPublish("nan-offset-refuses-without-brush-or-history", &valid,
                          Vec2{std::numeric_limits<double>::quiet_NaN(), 0});
    RefusalDoesNotPublish("infinite-offset-refuses-without-brush-or-history", &valid,
                          Vec2{0, std::numeric_limits<double>::infinity()});
    RefusalDoesNotPublish("offset-above-int-range-refuses-without-brush-or-history", &valid,
                          Vec2{static_cast<double>((std::numeric_limits<int>::max)()) + 1.0, 0});
    RefusalDoesNotPublish("offset-below-int-range-refuses-without-brush-or-history", &valid,
                          Vec2{static_cast<double>((std::numeric_limits<int>::min)()) - 1.0, 0});
}
} // namespace

int RunDocumentTestsCapturedCloneOffset()
{
    gFailures = 0;
    gAssertions = 0;
    CapturedCompositeAndOffsetAreStable();
    InvalidCapturedInputsAreRefused();
    std::printf("%d document.captured-clone-offset assertions; %d failed.\n", gAssertions, gFailures);
    return gFailures;
}
