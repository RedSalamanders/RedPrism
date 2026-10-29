#include "Document.Tests.PreparedRasterHistory.h"

#include "Executor.h"
#include "RasterEditWork.h"
#include "Tests/Support/Support.Tests.DocumentFixture.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <new>
#include <optional>
#include <span>
#include <stop_token>
#include <utility>

namespace
{
int gFailures = 0;
int gAssertions = 0;

void Expect(bool condition, const char* name)
{
    ++gAssertions;
    std::printf("%s prepared-raster-history.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

Guid StableGuid(const char* text)
{
    const auto parsed = Guid::Parse(text);
    return parsed.value_or(Guid{});
}

Raster Bgra(int width, int height, std::span<const std::uint8_t> bytes)
{
    return Raster::Make(width, height, RasterFormat::Bgra8888, bytes.data(), bytes.size());
}

bool BytesEqual(const Raster& raster, std::span<const std::uint8_t> expected)
{
    if (!raster.HasStorage() || raster.Format() != RasterFormat::Bgra8888 ||
        expected.size() != static_cast<std::size_t>(raster.Width()) * static_cast<std::size_t>(raster.Height()) * 4u)
        return false;
    std::array<std::uint8_t, 64> actual{};
    if (expected.size() > actual.size())
        return false;
    raster.ReadRegion(0, 0, raster.Width(), raster.Height(), actual.data(), raster.Width() * 4, 0);
    return std::equal(expected.begin(), expected.end(), actual.begin());
}

CanvasDocument MakeDocument(const Raster& image, bool selected = false)
{
    DocumentFixture document;
    document.id = StableGuid("10000000-0000-4000-8000-000000000001");
    document.width = image.Width();
    document.height = image.Height();
    ImageLayer layer = ImageLayer::Blank(L"Raster history fixture",
                                         Vec2{static_cast<double>(image.Width()), static_cast<double>(image.Height())});
    layer.id = StableGuid("20000000-0000-4000-8000-000000000002");
    layer.asset = RasterAsset{image, Raster::MakeThumbnail(image), layer.name};
    document.layers.push_back(std::move(layer));
    if (selected)
    {
        DocumentSelection selection;
        selection.antialiased = false;
        selection.loops = SelectionLoops{{Vec2{0, 0}, Vec2{1, 0}, Vec2{1, 1}, Vec2{0, 1}}};
        document.selection = std::move(selection);
    }
    return std::move(document).Seal();
}

bool Install(DocumentSession& session, CanvasDocument document)
{
    const Guid active = document.layers.front().id;
    session.ReplaceDocument(std::move(document), active);
    return session.Document().has_value() && session.ActiveLayer() && session.ActiveLayer()->asset &&
           session.ActiveLayer()->asset->thumbnail.HasStorage();
}

std::optional<JobToken> Capture(DocumentSession& session)
{
    return session.CaptureJobToken(session.ActiveLayerID(), 41);
}

RasterEditResult InvertResult(const ImageLayer& layer, int width, int height)
{
    RasterEditInput input;
    input.layer = layer;
    input.canvasWidth = width;
    input.canvasHeight = height;
    input.pixelLimit = kMaxLayerPixels;
    input.kind = RasterEditKind::Invert;
    return RunRasterEdit(input);
}

std::optional<PreparedRasterEdit> Prepare(DocumentSession& session, RasterEditKind kind, RasterEditResult&& result,
                                          const JobToken& token, std::wstring name = L"Prepared raster edit",
                                          CancelToken cancellation = {}, DocumentHistory::PrepareObserver observer = {})
{
    return PrepareRasterEditPublication(kind, false, session.Document(), session.ActiveLayerID(), token,
                                        session.history.CaptureState(), std::move(result), std::move(name),
                                        cancellation, observer);
}

void BasicInvertApplyUndoAndRetirement()
{
    constexpr std::array<std::uint8_t, 12> sourceBytes{10, 20, 30, 255, 20, 40, 80, 128, 30, 60, 90, 255};
    constexpr std::array<std::uint8_t, 12> invertedBytes{245, 235, 225, 255, 108, 88, 48, 128, 225, 195, 165, 255};
    const Raster source = Bgra(3, 1, sourceBytes);
    DocumentSession session;
    Expect(source.HasStorage() && Install(session, MakeDocument(source)), "invert.fixture-installed");
    const CanvasDocument before = session.Document().value();
    const Guid active = session.ActiveLayerID().value();
    const auto token = Capture(session);
    RasterEditResult result = InvertResult(*session.ActiveLayer(), 3, 1);
    Expect(BytesEqual(session.ActiveLayer()->asset->image, sourceBytes), "invert.literal-source-bgra");
    Expect(result.status == RasterEditStatus::Ready && result.layer && result.layer->asset &&
               BytesEqual(result.layer->asset->image, invertedBytes),
           "invert.worker-literal-inverted-bgra");
    auto prepared =
        token ? Prepare(session, RasterEditKind::Invert, std::move(result), *token, L"Invert") : std::nullopt;
    Expect(prepared.has_value(), "invert.factory-prepares-publication");
    if (!prepared)
        return;
    const int oldUndoCount = session.history.UndoCount();
    const bool applied = session.ApplyPreparedRasterEditIfCurrent(*prepared);
    Expect(applied && session.history.UndoCount() == oldUndoCount + 1 && session.history.UndoName() == L"Invert",
           "invert.apply-records-one-history-entry");
    Expect(session.ActiveLayer() && session.ActiveLayer()->id == active && session.ActiveLayer()->asset &&
               BytesEqual(session.ActiveLayer()->asset->image, invertedBytes),
           "invert.apply-publishes-literal-result");
    Expect(prepared->Layer().id == active && prepared->Layer().asset && prepared->Layer().asset->image == source,
           "invert.candidate-retains-retired-live-layer");
    session.Undo();
    Expect(session.Document() && session.Document().value() == before && session.history.UndoCount() == oldUndoCount,
           "invert.undo-restores-exact-document");
}

void StaleHistoryEpochRetainsCandidate()
{
    constexpr std::array<std::uint8_t, 4> sourceBytes{17, 38, 91, 255};
    const Raster source = Bgra(1, 1, sourceBytes);
    DocumentSession session;
    Expect(Install(session, MakeDocument(source)), "stale-history.fixture-installed");
    const CanvasDocument unchanged = session.Document().value();
    const auto token = Capture(session);
    RasterEditResult result = InvertResult(*session.ActiveLayer(), 1, 1);
    auto prepared = token ? Prepare(session, RasterEditKind::Invert, std::move(result), *token) : std::nullopt;
    Expect(prepared.has_value(), "stale-history.candidate-prepared");
    if (!prepared)
        return;
    const Raster candidateRaster = prepared->Layer().asset->image;
    const std::uint64_t generation = session.Generation();
    session.history.Reset(); // Advances the history state epoch without changing the document or job token.
    const bool accepted = session.ApplyPreparedRasterEditIfCurrent(*prepared);
    Expect(!accepted && session.Document() && session.Document().value() == unchanged &&
               session.Generation() == generation && prepared->Layer().asset &&
               prepared->Layer().asset->image == candidateRaster,
           "stale-history.epoch-refuses-with-document-token-and-candidate-intact");
}

void StaleSourceGenerationRetainsCandidate()
{
    constexpr std::array<std::uint8_t, 4> sourceBytes{12, 34, 56, 255};
    const Raster source = Bgra(1, 1, sourceBytes);
    DocumentSession session;
    Expect(Install(session, MakeDocument(source)), "stale-generation.fixture-installed");
    const auto token = Capture(session);
    RasterEditResult result = InvertResult(*session.ActiveLayer(), 1, 1);
    auto prepared = token ? Prepare(session, RasterEditKind::Invert, std::move(result), *token) : std::nullopt;
    Expect(prepared.has_value(), "stale-generation.candidate-prepared");
    if (!prepared)
        return;
    const Raster retained = prepared->Layer().asset->image;
    const CanvasDocument unchanged = session.Document().value();
    session.EditForTest([](CanvasDocument&) {});
    const bool accepted = session.ApplyPreparedRasterEditIfCurrent(*prepared);
    Expect(!accepted && session.Document() && session.Document().value() == unchanged && prepared->Layer().asset &&
               prepared->Layer().asset->image == retained,
           "stale-generation.refusal-retains-candidate-and-live-document");
}

struct CancelAtPreparation final
{
    CancelSource* source = nullptr;
    void operator()(DocumentHistory::PrepareStage) const
    {
        (void)source->Cancel();
    }
    static void Checkpoint(void* context, DocumentHistory::PrepareStage stage)
    {
        (*static_cast<CancelAtPreparation*>(context))(stage);
    }
};

void CancelAndObserverFailureKeepRasterOwnership()
{
    constexpr std::array<std::uint8_t, 4> sourceBytes{1, 2, 3, 255};
    const Raster source = Bgra(1, 1, sourceBytes);
    {
        DocumentSession session;
        Expect(Install(session, MakeDocument(source)), "ownership.cancel-fixture-installed");
        const auto token = Capture(session);
        RasterEditResult result = InvertResult(*session.ActiveLayer(), 1, 1);
        const Raster* const owned = result.layer && result.layer->asset ? &result.layer->asset->image : nullptr;
        CancelSource cancellation;
        CancelAtPreparation callback{&cancellation};
        DocumentHistory::PrepareObserver observer{&CancelAtPreparation::Checkpoint, &callback};
        auto prepared = token ? Prepare(session, RasterEditKind::Invert, std::move(result), *token, L"Invert",
                                        cancellation.Token(), observer)
                              : std::nullopt;
        Expect(!prepared && result.layer && result.layer->asset && owned && &result.layer->asset->image == owned &&
                   result.layer->asset->image.HasStorage(),
               "ownership.cancel-refusal-leaves-result-raster-owned-by-caller");
    }
    {
        DocumentSession session;
        Expect(Install(session, MakeDocument(source)), "ownership.exception-fixture-installed");
        const auto token = Capture(session);
        RasterEditResult result = InvertResult(*session.ActiveLayer(), 1, 1);
        const Raster* const owned = result.layer && result.layer->asset ? &result.layer->asset->image : nullptr;
        DocumentHistory::PrepareObserver observer{[](void*, DocumentHistory::PrepareStage) { throw std::bad_alloc{}; },
                                                  nullptr};
        bool threw = false;
        try
        {
            auto prepared =
                token ? Prepare(session, RasterEditKind::Invert, std::move(result), *token, L"Invert", {}, observer)
                      : std::nullopt;
            (void)prepared;
        }
        catch (const std::bad_alloc&)
        {
            threw = true;
        }
        Expect(threw && result.layer && result.layer->asset && owned && &result.layer->asset->image == owned &&
                   result.layer->asset->image.HasStorage(),
               "ownership.observer-bad-alloc-leaves-result-raster-owned-by-caller");
    }
}

void MovedFromCandidateIsRefused()
{
    constexpr std::array<std::uint8_t, 4> sourceBytes{20, 40, 60, 255};
    const Raster source = Bgra(1, 1, sourceBytes);
    DocumentSession session;
    Expect(Install(session, MakeDocument(source)), "moved-from.fixture-installed");
    const auto token = Capture(session);
    RasterEditResult result = InvertResult(*session.ActiveLayer(), 1, 1);
    auto prepared = token ? Prepare(session, RasterEditKind::Invert, std::move(result), *token) : std::nullopt;
    Expect(prepared.has_value(), "moved-from.candidate-prepared");
    if (!prepared)
        return;
    PreparedRasterEdit consumed(std::move(*prepared));
    (void)consumed;
    const CanvasDocument unchanged = session.Document().value();
    const int undoCount = session.history.UndoCount();
    const bool accepted = session.ApplyPreparedRasterEditIfCurrent(*prepared);
    Expect(!accepted && session.Document() && session.Document().value() == unchanged &&
               session.history.UndoCount() == undoCount,
           "moved-from.original-candidate-safely-refused");
}

void PreparedGradientApplyHasLiteralPixels()
{
    constexpr std::array<std::uint8_t, 16> black{0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255};
    constexpr std::array<std::uint8_t, 16> gradient{0,   0,   0,   255, 85,  85,  85,  255,
                                                    170, 170, 170, 255, 255, 255, 255, 255};
    const Raster source = Bgra(4, 1, black);
    DocumentSession session;
    Expect(Install(session, MakeDocument(source)), "gradient.fixture-installed");
    session.SetForegroundColor(0xFF000000u);
    session.SetBackgroundColor(0xFFFFFFFFu);
    session.gradientSettings.shape = GradientShape::Linear;
    session.gradientSettings.style = GradientStyle::ForegroundToBackground;
    session.gradientSettings.opacity = 1.0;
    const bool began = session.BeginDeferredGradient(Vec2{0.5, 0.5});
    session.MoveGradient(std::nullopt, Vec2{3.5, 0.5});
    Expect(began && session.PendingGradient() && session.PendingGradient()->HasLine(), "gradient.deferred-line-ready");
    if (!session.PendingGradient())
        return;
    const std::uint64_t opening = session.PendingGradient()->opening;
    const std::uint64_t sequence = session.PendingGradient()->sequence;
    const auto token = Capture(session);
    RasterEditInput input;
    input.layer = *session.ActiveLayer();
    input.selection = session.Document()->selection;
    input.canvasWidth = 4;
    input.canvasHeight = 1;
    input.pixelLimit = kMaxLayerPixels;
    input.kind = RasterEditKind::Gradient;
    input.start = session.PendingGradient()->start;
    input.end = session.PendingGradient()->end;
    input.shape = session.gradientSettings.shape;
    const auto colors = session.GradientColors();
    input.color = colors[0];
    input.endColor = colors[1];
    input.opacity = session.gradientSettings.opacity;
    RasterEditResult result = RunRasterEdit(input);
    Expect(result.status == RasterEditStatus::Ready && result.layer && result.layer->asset &&
               BytesEqual(result.layer->asset->image, gradient),
           "gradient.worker-literal-bgra-reference");
    auto preview = RunRasterEditPreview(input);
    const std::weak_ptr<BrushStroke> retiredPreview = preview.stroke;
    const bool publishedPreview =
        token && session.PublishGradientPreview(std::move(preview.stroke), *token, opening, sequence);
    Expect(publishedPreview && !retiredPreview.expired(), "gradient.opening-owns-preview-before-apply");
    auto prepared =
        token ? Prepare(session, RasterEditKind::Gradient, std::move(result), *token, L"Gradient") : std::nullopt;
    Expect(prepared.has_value(), "gradient.factory-prepares-publication");
    if (!prepared)
        return;
    const bool applied = session.ApplyPreparedGradientIfCurrent(*prepared, opening, sequence);
    Expect(applied && !session.PendingGradient() && session.history.UndoCount() == 1 &&
               BytesEqual(session.ActiveLayer()->asset->image, gradient),
           "gradient.prepared-apply-publishes-once");
    Expect(!retiredPreview.expired(), "gradient.retired-preview-survives-owner-apply");
    prepared.reset();
    Expect(retiredPreview.expired(), "gradient.retired-preview-releases-with-aggregate");
}

void PreparedPixelMoveMovesPixelsAndOutline()
{
    constexpr std::array<std::uint8_t, 12> sourceBytes{11, 22, 33, 255, 0, 0, 0, 0, 77, 88, 99, 255};
    constexpr std::array<std::uint8_t, 12> movedBytes{0, 0, 0, 0, 11, 22, 33, 255, 77, 88, 99, 255};
    const Raster source = Bgra(3, 1, sourceBytes);
    DocumentSession session;
    Expect(Install(session, MakeDocument(source, true)), "pixel-move.fixture-installed");
    const bool began = session.BeginDeferredPixelMove();
    session.MovePixels(1, 0);
    const PixelMove* pending = session.PendingPixelMove();
    Expect(began && pending && pending->HasOffset(), "pixel-move.deferred-offset-ready");
    if (!pending)
        return;
    const std::uint64_t opening = pending->opening;
    const std::uint64_t sequence = pending->sequence;
    const auto token = Capture(session);
    const CanvasDocument before = session.Document().value();
    RasterEditInput input;
    input.layer = *session.ActiveLayer();
    input.selection = session.Document()->selection;
    input.canvasWidth = 3;
    input.canvasHeight = 1;
    input.pixelLimit = kMaxLayerPixels;
    input.kind = RasterEditKind::PixelMove;
    input.offset = pending->offset;
    RasterEditResult result = RunRasterEdit(input);
    Expect(result.status == RasterEditStatus::Ready && result.layer && result.layer->asset && result.selection &&
               BytesEqual(result.layer->asset->image, movedBytes),
           "pixel-move.worker-literal-raster-and-outline");
    const auto expectedBounds = result.selection ? result.selection->Bounds() : std::nullopt;
    Expect(expectedBounds && expectedBounds->minX == 1 && expectedBounds->maxX == 2 && expectedBounds->minY == 0 &&
               expectedBounds->maxY == 1,
           "pixel-move.worker-translates-selected-outline");
    auto preview = RunRasterEditPreview(input);
    const std::weak_ptr<BrushStroke> retiredPreview = preview.stroke;
    const bool publishedPreview =
        token && session.PublishPixelMovePreview(std::move(preview.stroke), *token, opening, sequence);
    Expect(publishedPreview && !retiredPreview.expired(), "pixel-move.opening-owns-preview-before-apply");
    auto prepared =
        token ? Prepare(session, RasterEditKind::PixelMove, std::move(result), *token, L"Move Pixels") : std::nullopt;
    Expect(prepared.has_value(), "pixel-move.factory-prepares-publication");
    if (!prepared)
        return;
    const bool applied = session.ApplyPreparedPixelMoveIfCurrent(*prepared, opening, sequence);
    const auto installedBounds = session.Document()->selection ? session.Document()->selection->Bounds() : std::nullopt;
    Expect(applied && !session.PendingPixelMove() && session.history.UndoCount() == 1 &&
               BytesEqual(session.ActiveLayer()->asset->image, movedBytes) && installedBounds &&
               installedBounds->minX == 1 && installedBounds->maxX == 2 && installedBounds->minY == 0 &&
               installedBounds->maxY == 1,
           "pixel-move.prepared-apply-installs-pixels-and-translated-outline");
    Expect(!retiredPreview.expired(), "pixel-move.retired-preview-survives-owner-apply");
    prepared.reset();
    Expect(retiredPreview.expired(), "pixel-move.retired-preview-releases-with-aggregate");
    session.Undo();
    Expect(session.Document() && session.Document().value() == before,
           "pixel-move.undo-restores-literal-source-and-outline");
}

void RunAll()
{
    BasicInvertApplyUndoAndRetirement();
    StaleHistoryEpochRetainsCandidate();
    StaleSourceGenerationRetainsCandidate();
    CancelAndObserverFailureKeepRasterOwnership();
    MovedFromCandidateIsRefused();
    PreparedGradientApplyHasLiteralPixels();
    PreparedPixelMoveMovesPixelsAndOutline();
}
} // namespace

int RunPreparedRasterHistoryTests()
{
    gFailures = 0;
    gAssertions = 0;
    RunAll();
    std::printf("prepared-raster-history.summary assertions=%d failures=%d\n", gAssertions, gFailures);
    return gFailures;
}
