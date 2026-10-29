#include "Document.Tests.HistoryReferenceTopology.h"
#include "Document.h"
#include "Tests/Support/Support.Tests.DocumentFixture.h"
#include <array>
#include <cstdio>
#include <optional>
#include <utility>
namespace
{
int failures = 0;
int controls = 0;
void Expect(bool value, const char* label) noexcept
{
    ++controls;
    failures += value ? 0 : 1;
    std::printf("%s history-reference-topology.%s\n", value ? "PASS" : "FAIL", label);
}
struct Fixture final
{
    Raster leaf;
    Raster inner;
    Raster historical;
    DocumentSession session;
    explicit Fixture(bool nested, std::uint64_t limit = 64)
    {
        constexpr std::array<std::uint8_t, 4> currentBytes{17, 34, 51, 255};
        constexpr std::array<std::uint8_t, 4> historicalBytes{61, 62, 63, 255};
        const Raster image = Raster::Make(1, 1, RasterFormat::Bgra8888, currentBytes.data(), currentBytes.size());
        auto layer = ImageLayer::Blank(L"Current", {1, 1});
        layer.asset = RasterAsset{image, Raster::MakeThumbnail(image), L"Current"};
        layer.opacity = 0.5;
        const auto active = layer.id;
        DocumentFixture document;
        document.width = 2;
        document.height = 1;
        document.layers.push_back(std::move(layer));
        session.ReplaceDocument(std::move(document).Seal(), active);
        session.history = DocumentHistory{100, limit};
        leaf = Raster::Make(1, 1, RasterFormat::Bgra8888, historicalBytes.data(), historicalBytes.size());
        inner = Raster::MakeSparse(2, 1, RasterFormat::Bgra8888, {}, 0, 0, {{0, 0, leaf}}, 1);
        historical = nested
                         ? Raster::MakeSparse(4, 1, RasterFormat::Bgra8888, {}, 0, 0, {{0, 0, inner}, {2, 0, inner}}, 1)
                         : inner;
        auto previous = DocumentFixture::Copy(session.Document().value());
        previous.layers.front().asset->image = historical;
        auto publication = DocumentHistory::PrepareEdit(session.history.CaptureState(), std::move(previous).Seal(),
                                                        active, session.Document(), active, L"Prepared history");
        if (!publication)
            std::terminate();
        DocumentHistory::ResidencyPermit permit;
        if (permit.TryAcquire(publication.value()) != DocumentHistory::ResidencyValidation::Ready ||
            !session.history.Publish(publication.value()))
            std::terminate();
    }
    double Opacity() const
    {
        return session.Document()->layers.front().opacity;
    }
    bool Lazy() const
    {
        return !historical.IsMaterialized() && !inner.IsMaterialized();
    }
    std::uint64_t Retained() const
    {
        return session.history.RetainedBytes(session.Document());
    }
};
void MixedReference(bool nested)
{
    Fixture fixture{nested};
    auto& session = fixture.session;
    Expect(fixture.Retained() == 4 && fixture.Lazy(), "prepared-entry-counts-literal-leaf-and-keeps-sparse-lazy");
    session.SetOpacity(0.25);
    Expect(fixture.Retained() == 4 && session.history.UndoCount() == 2 && fixture.Lazy(),
           "reference-end-keeps-prepared-historical-closure");
    session.Undo();
    Expect(fixture.Opacity() == 0.5 && fixture.Retained() == 4 && session.history.UndoCount() == 1 && fixture.Lazy(),
           "reference-undo-keeps-prepared-historical-closure");
    session.Redo();
    Expect(fixture.Opacity() == 0.25 && fixture.Retained() == 4 && session.history.UndoCount() == 2 && fixture.Lazy(),
           "reference-redo-keeps-prepared-historical-closure");
    session.history.Reset();
    Expect(fixture.Retained() == 0 && fixture.Lazy(), "reference-reset-releases-history-without-materializing");
}
void LateResidentTrim()
{
    Fixture fixture{false, 8};
    Expect(fixture.Retained() == 4 && fixture.Lazy(), "late-cache-starts-with-four-literal-leaf-bytes");
    const auto* pixels = fixture.historical.Data();
    Expect(pixels && pixels[0] == 61 && pixels[1] == 62 && pixels[2] == 63 && pixels[3] == 255 &&
               fixture.historical.ByteSize() == 8 && fixture.Retained() == 12,
           "late-cache-adds-eight-bytes-to-four-byte-leaf");
    fixture.session.SetOpacity(0.25);
    Expect(fixture.Retained() == 0 && fixture.session.history.UndoCount() == 1,
           "reference-trim-drops-twelve-byte-prepared-entry-at-eight-byte-cap");
    const auto& image = fixture.session.Document()->layers.front().asset->image;
    const auto* current = image.Data();
    Expect(current && current[0] == 17 && current[1] == 34 && current[2] == 51 && current[3] == 255 &&
               fixture.Opacity() == 0.25,
           "trim-preserves-literal-current-pixels-and-opacity");
    fixture.session.Undo();
    Expect(fixture.Opacity() == 0.5 && fixture.Retained() == 0 && fixture.session.history.CanRedo(),
           "undo-after-trim-preserves-reference-entry");
    fixture.session.Redo();
    Expect(fixture.Opacity() == 0.25 && fixture.Retained() == 0 && fixture.session.history.UndoCount() == 1,
           "redo-after-trim-preserves-reference-entry");
}
} // namespace
int RunHistoryReferenceTopologyTests()
{
    failures = 0;
    controls = 0;
    MixedReference(false);
    MixedReference(true);
    {
        Fixture nested{true};
        Expect(nested.historical.Tiles().size() == 2 && nested.inner.Tiles().size() == 1 && nested.Retained() == 4 &&
                   nested.Lazy(),
               "two-parent-aliases-count-one-shared-leaf-without-assembly");
    }
    LateResidentTrim();
    std::printf("%s history-reference-topology.summary controls=%d failures=%d\n", failures ? "FAIL" : "PASS", controls,
                failures);
    return failures;
}
