#include "Document.Tests.DocumentFixture.h"

#include "Tests/Support/Support.Tests.DocumentFixture.h"

#include <array>
#include <cstdio>
#include <type_traits>
#include <utility>

static_assert(!std::is_convertible_v<DocumentFixture, CanvasDocument>);

int RunDocumentFixtureTests()
{
    int failures = 0;
    const auto expect = [&](bool condition, const char* name)
    {
        std::printf("%s document-fixture.%s\n", condition ? "PASS" : "FAIL", name);
        failures += condition ? 0 : 1;
    };
    DocumentFixture fixture;
    fixture.width = 13;
    fixture.height = 17;
    fixture.resolution = 144;
    ImageLayer layer = ImageLayer::Blank(L"Literal", Vec2{3, 2});
    const auto id = layer.id;
    constexpr std::array<std::uint8_t, 4> pixel{19, 37, 71, 255};
    const auto raster = Raster::Make(1, 1, RasterFormat::Bgra8888, pixel.data(), pixel.size());
    layer.asset = RasterAsset{raster, {}, L"Asset"};
    fixture.layers.push_back(std::move(layer));
    const CanvasDocument first = fixture.Seal();
    expect(first.width == 13 && first.height == 17 && first.resolution == 144 && first.layers.size() == 1 &&
               first.layers[0].id == id && first.layers[0].name == L"Literal" &&
               first.layers[0].asset.value().image == raster && fixture.layers[0].name == L"Literal",
           "explicit-seal-preserves-literals-and-source");
    fixture.layers[0].name = L"Changed";
    const auto second = std::move(fixture).Seal();
    expect(first.layers[0].name == L"Literal" && second.layers[0].name == L"Changed" && first.layers != second.layers,
           "later-private-write-preserves-captured-root");
    auto copied = DocumentFixture::Copy(first);
    copied.layers[0].opacity = 0.25;
    const auto third = std::move(copied).Seal();
    expect(first.layers[0].opacity == 1 && third.layers[0].opacity == 0.25 &&
               third.layers[0].asset.value().image == raster,
           "editable-copy-shares-pixels-preserves-old-records");
    DocumentFixture invalid;
    invalid.width = kMaxCanvasSide + 1;
    invalid.layers.resize(static_cast<std::size_t>(kMaxLayerCount) + 1);
    const auto invalidDocument = std::move(invalid).Seal();
    expect(invalidDocument.width == kMaxCanvasSide + 1 &&
               invalidDocument.layers.size() == static_cast<std::size_t>(kMaxLayerCount) + 1 &&
               ValidateDocument(invalidDocument) != DocumentStatus::Ok,
           "intentionally-invalid-values-reach-production-validation");
    DocumentSession session;
    session.ReplaceDocument(first, id);
    const auto generation = session.Generation();
    const auto captured = session.Document().value().layers;
    EditLayerRecordsForTest(session, [](DocumentFixture& document) { document.layers[0].visible = false; });
    expect(session.Generation() == generation + 1 && captured[0].visible &&
               !session.Document().value().layers[0].visible && session.Document().value().layers[0].id == id,
           "test-edit-preserves-generation-hook-and-old-root");
    CanvasDocument document = first;
    EditDocumentFixture(document, [](DocumentFixture& edit) { edit.layers.clear(); });
    expect(document.layers.empty() && document.width == 13 && first.layers.size() == 1,
           "standalone-edit-seals-replacement-without-mutating-source");
    return failures;
}
