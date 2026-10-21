#include "Document.Tests.MaskEditWork.h"

#include "Document.h"
#include "Executor.h"
#include "MaskEditWork.h"
#include "PixelBuffer.h"
#include "Selection.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <new>
#include <optional>
#include <utility>
#include <vector>

namespace
{
int gFailures = 0;
int gAssertions = 0;

void Expect(bool condition, const char* name)
{
    ++gAssertions;
    std::printf("%s mask-work.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

Raster RedImage(int width, int height)
{
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4u);
    for (std::size_t index = 0; index < pixels.size(); index += 4)
    {
        pixels[index] = 0;
        pixels[index + 1] = 0;
        pixels[index + 2] = 255;
        pixels[index + 3] = 255;
    }
    return Raster::Make(width, height, RasterFormat::Bgra8888, pixels.data(), pixels.size());
}

DocumentSession LiteralSession(int width, int height, std::optional<LayerTransform> transform = std::nullopt)
{
    const Raster image = RedImage(width, height);
    CanvasDocument document;
    document.id = Guid::Generate();
    document.width = 160;
    document.height = 100;
    ImageLayer layer =
        ImageLayer::Blank(L"Literal red source", Vec2{static_cast<double>(width), static_cast<double>(height)});
    if (transform.has_value())
        layer.transform = transform.value();
    layer.asset = RasterAsset{image, Raster::MakeThumbnail(image), layer.name};
    const Guid active = layer.id;
    document.layers.push_back(std::move(layer));
    DocumentSession session;
    session.ReplaceDocument(std::move(document), active);
    session.history.Reset();
    return session;
}

const ImageLayer* FindLayer(const DocumentSession& session, const Guid& id) noexcept
{
    if (!session.Document())
        return nullptr;
    const auto& layers = session.Document().value().layers;
    const auto found =
        std::find_if(layers.begin(), layers.end(), [&](const ImageLayer& layer) { return layer.id == id; });
    return found == layers.end() ? nullptr : &*found;
}

std::uint8_t GrayAt(const Raster& raster, int x, int y) noexcept
{
    if (!raster.HasStorage() || raster.Format() != RasterFormat::Gray8 || x < 0 || y < 0 || x >= raster.Width() ||
        y >= raster.Height())
        return 0;
    return raster
        .Data()[static_cast<std::size_t>(y) * static_cast<std::size_t>(raster.Stride()) + static_cast<std::size_t>(x)];
}

MaskEditResult Prepare(DocumentSession& session, bool revealing, CancelToken cancellation = {},
                       MaskWorkObserver observer = {}, DocumentHistory::PrepareObserver historyObserver = {})
{
    auto input = session.PrepareMaskEditInput(revealing);
    if (!input)
        return {};
    InlineExecutor executor;
    return DocumentSession::PrepareMaskEditPublication(std::move(input.value()), executor, cancellation, observer,
                                                       historyObserver);
}

bool SameHistory(const DocumentHistory& history, const DocumentHistory::StateView& before) noexcept
{
    const auto after = history.CaptureState();
    return history.CurrentRevision() == before.Revision() &&
           static_cast<std::size_t>(history.UndoCount()) == before.PastCount() &&
           history.UndoName() == before.UndoName() && history.RedoName() == before.RedoName() &&
           after.Depth() == before.Depth() && after.PastCount() == before.PastCount() &&
           after.FutureCount() == before.FutureCount() && after.UndoName() == before.UndoName() &&
           after.RedoName() == before.RedoName();
}

struct CancelAtStage final
{
    CancelSource* source = nullptr;
    MaskWorkStage target = MaskWorkStage::Coverage;
    int hits = 0;
};

void CancelCheckpoint(void* opaque, MaskWorkStage stage)
{
    auto& state = *static_cast<CancelAtStage*>(opaque);
    if (stage == state.target)
    {
        ++state.hits;
        static_cast<void>(state.source->Cancel());
    }
}

struct ThrowHistoryAllocation final
{
    int hits = 0;
};

void ThrowAtHistoryEntry(void* opaque, DocumentHistory::PrepareStage stage)
{
    if (stage == DocumentHistory::PrepareStage::Entry)
    {
        ++static_cast<ThrowHistoryAllocation*>(opaque)->hits;
        throw std::bad_alloc{};
    }
}

void SelectedMaskPolarityAndHistory()
{
    constexpr int width = 100;
    constexpr int height = 40;
    const std::optional<DocumentSelection> baselineSelection{
        DocumentSelection{true, SelectionGeometry::Rect(20, 10, 30, 20)}};

    for (const bool revealing : {true, false})
    {
        DocumentSession current = LiteralSession(width, height);
        current.EditForTest([](CanvasDocument& document)
                            { document.selection = DocumentSelection{true, SelectionGeometry::Rect(20, 10, 30, 20)}; });
        current.history.Reset();
        const Guid id = current.ActiveLayerID().value();
        const Raster originalPixels = FindLayer(current, id)->asset.value().image;
        const auto before = current.Document();
        const auto historyBefore = current.history.CaptureState();
        InlineExecutor executor;
        auto input = current.PrepareMaskEditInput(revealing);
        Expect(input.has_value(), revealing ? "selection.reveal-input-admitted" : "selection.hide-input-admitted");
        if (!input)
            continue;
        MaskEditResult result = DocumentSession::PrepareMaskEditPublication(std::move(input.value()), executor, {}, {});
        Expect(result.status == MaskEditStatus::Ready && result.publication.has_value(),
               revealing ? "selection.reveal-publication-ready" : "selection.hide-publication-ready");
        Expect(current.Document() == before && SameHistory(current.history, historyBefore) &&
                   current.Selection() == std::optional<DocumentSelection>{DocumentSelection{
                                              true, SelectionGeometry::Rect(20, 10, 30, 20)}} &&
                   FindLayer(current, id)->asset.value().image == originalPixels,
               revealing ? "selection.reveal-preparation-does-not-publish"
                         : "selection.hide-preparation-does-not-publish");
        if (!result.publication)
            continue;
        PreparedMaskEdit publication = std::move(result.publication.value());
        Expect(current.ApplyPreparedMaskEditIfCurrent(publication),
               revealing ? "selection.reveal-apply" : "selection.hide-apply");
        const ImageLayer* layer = FindLayer(current, id);
        if (!layer || !layer->mask)
        {
            Expect(false, revealing ? "selection.reveal-mask-exists" : "selection.hide-mask-exists");
            continue;
        }
        const Raster& mask = layer->mask.value().asset.image;
        const std::uint8_t selected = revealing ? 0 : 255;
        const std::uint8_t exterior = revealing ? 255 : 0;
        Expect(mask.Width() == width && mask.Height() == height && mask.Format() == RasterFormat::Gray8 &&
                   GrayAt(mask, 30, 20) == selected && GrayAt(mask, 5, 5) == exterior &&
                   GrayAt(mask, 49, 29) == selected && GrayAt(mask, 50, 30) == exterior,
               revealing ? "selection.reveal-legacy-numeric-anchor" : "selection.hide-legacy-numeric-anchor");
        Expect(layer->asset.value().image == originalPixels && !current.Selection() && current.IsMaskSelected() &&
                   current.history.UndoCount() == 1 && current.history.UndoName() == L"Add Mask from Selection",
               revealing ? "selection.reveal-clears-outline-targets-mask-one-entry"
                         : "selection.hide-clears-outline-targets-mask-one-entry");
        Expect(layer->mask.value().asset.thumbnail.HasStorage() &&
                   layer->mask.value().asset.thumbnail.Format() == RasterFormat::Gray8 &&
                   MaskEdgeBackground(layer->mask.value().asset.thumbnail) == (revealing ? 1.0 : 0.0),
               revealing ? "selection.reveal-eager-thumbnail-edge-background"
                         : "selection.hide-eager-thumbnail-edge-background");
        current.Undo();
        Expect(!FindLayer(current, id)->mask && current.Selection() == baselineSelection &&
                   current.history.UndoCount() == 0 && current.history.RedoName() == L"Add Mask from Selection",
               revealing ? "selection.reveal-undo-restores-no-mask-and-outline"
                         : "selection.hide-undo-restores-no-mask-and-outline");
        current.Redo();
        Expect(FindLayer(current, id)->mask.has_value() && !current.Selection() && current.history.UndoCount() == 1,
               revealing ? "selection.reveal-redo-restores-mask" : "selection.hide-redo-restores-mask");
    }
}

void NoSelectionAndExplicitEmpty()
{
    for (const bool revealing : {true, false})
    {
        DocumentSession session = LiteralSession(1, 1);
        const Guid id = session.ActiveLayerID().value();
        const auto before = session.Document();
        MaskEditResult result = Prepare(session, revealing);
        Expect(result.status == MaskEditStatus::Ready && result.publication.has_value(),
               revealing ? "uniform.reveal-ready" : "uniform.hide-ready");
        if (result.publication)
        {
            PreparedMaskEdit publication = std::move(result.publication.value());
            Expect(session.ApplyPreparedMaskEditIfCurrent(publication),
                   revealing ? "uniform.reveal-applies" : "uniform.hide-applies");
        }
        const ImageLayer* layer = FindLayer(session, id);
        const std::uint8_t expected = revealing ? 255 : 0;
        Expect(layer && layer->mask && layer->mask.value().asset.image.Width() == 1 &&
                   layer->mask.value().asset.image.Height() == 1 &&
                   GrayAt(layer->mask.value().asset.image, 0, 0) == expected &&
                   layer->mask.value().asset.thumbnail == layer->mask.value().asset.image &&
                   MaskEdgeBackground(layer->mask.value().asset.thumbnail) == (revealing ? 1.0 : 0.0) &&
                   session.history.UndoName() == (revealing ? L"Add Reveal-All Mask" : L"Add Hide-All Mask") &&
                   session.history.UndoCount() == 1 && before.has_value(),
               revealing ? "uniform.reveal-one-pixel-title-thumbnail" : "uniform.hide-one-pixel-title-thumbnail");
    }

    for (const bool revealing : {true, false})
    {
        DocumentSession session = LiteralSession(5, 3);
        session.EditForTest([](CanvasDocument& document) { document.selection = DocumentSelection{true, {}}; });
        session.history.Reset();
        const Guid id = session.ActiveLayerID().value();
        MaskEditResult result = Prepare(session, revealing);
        Expect(result.status == MaskEditStatus::Ready && result.publication.has_value(),
               revealing ? "empty.reveal-ready" : "empty.hide-ready");
        if (!result.publication)
            continue;
        PreparedMaskEdit publication = std::move(result.publication.value());
        Expect(session.ApplyPreparedMaskEditIfCurrent(publication),
               revealing ? "empty.reveal-applies" : "empty.hide-applies");
        const ImageLayer* layer = FindLayer(session, id);
        bool uniform = layer && layer->mask && layer->mask.value().asset.image.Width() == 5 &&
                       layer->mask.value().asset.image.Height() == 3;
        if (uniform)
        {
            for (int y = 0; y < 3; ++y)
                for (int x = 0; x < 5; ++x)
                    uniform = uniform && GrayAt(layer->mask.value().asset.image, x, y) == (revealing ? 255 : 0);
        }
        Expect(uniform && !session.Selection() && session.IsMaskSelected() &&
                   session.history.UndoName() == L"Add Mask from Selection",
               revealing ? "empty.reveal-uniform-layer-grid" : "empty.hide-uniform-layer-grid");
    }
}

void TransformMappingAndPixels()
{
    LayerTransform transform;
    transform.origin = Vec2{10, 20};
    transform.size = Vec2{12, 12};
    DocumentSession session = LiteralSession(6, 4, transform);
    session.EditForTest([](CanvasDocument& document)
                        { document.selection = DocumentSelection{false, SelectionGeometry::Rect(12, 23, 6, 6)}; });
    session.history.Reset();
    const Guid id = session.ActiveLayerID().value();
    const Raster source = FindLayer(session, id)->asset.value().image;
    const std::vector<std::uint8_t> sourceBytes(source.Data(), source.Data() + source.Stride() * source.Height());
    MaskEditResult result = Prepare(session, true);
    Expect(result.status == MaskEditStatus::Ready && result.publication.has_value(), "mapping.translated-scaled-ready");
    if (!result.publication)
        return;
    PreparedMaskEdit publication = std::move(result.publication.value());
    Expect(session.ApplyPreparedMaskEditIfCurrent(publication), "mapping.translated-scaled-applies");
    const ImageLayer* layer = FindLayer(session, id);
    const Raster afterImage = layer->asset.value().image;
    const std::vector<std::uint8_t> afterBytes(afterImage.Data(),
                                               afterImage.Data() + afterImage.Stride() * afterImage.Height());
    Expect(layer->mask && layer->mask.value().asset.image.Width() == 6 &&
               layer->mask.value().asset.image.Height() == 4 && GrayAt(layer->mask.value().asset.image, 0, 0) == 255 &&
               GrayAt(layer->mask.value().asset.image, 1, 1) == 0 &&
               GrayAt(layer->mask.value().asset.image, 2, 2) == 0 &&
               GrayAt(layer->mask.value().asset.image, 3, 2) == 0 &&
               GrayAt(layer->mask.value().asset.image, 4, 2) == 255 &&
               GrayAt(layer->mask.value().asset.image, 5, 3) == 255,
           "mapping.document-rectangle-rasterizes-through-translation-and-nonuniform-scale");
    Expect(afterImage == source && afterBytes == sourceBytes && sourceBytes.size() == 6u * 4u * 4u &&
               sourceBytes[2] == 255 && sourceBytes[3] == 255,
           "mapping.source-layer-pixels-retained-byte-for-byte");
}

void UnchangedMovedInputAndAdmission()
{
    DocumentSession existing = LiteralSession(2, 2);
    existing.AddLayerMask(true);
    existing.history.Reset();
    Expect(existing.MaskEditPreparationMemoryBytes() == 0 && !existing.PrepareMaskEditInput(true),
           "refusal.existing-mask-has-no-admission");

    DocumentSession moved = LiteralSession(2, 2);
    auto input = moved.PrepareMaskEditInput(true);
    Expect(input.has_value(), "input.moved-from-fixture-admitted");
    if (input)
    {
        MaskEditInput owned(std::move(input.value()));
        InlineExecutor executor;
        MaskEditResult movedFrom =
            DocumentSession::PrepareMaskEditPublication(std::move(input.value()), executor, {}, {});
        Expect(movedFrom.status == MaskEditStatus::Invalid && !movedFrom.publication,
               "input.moved-from-object-refused");
        MaskEditResult valid = DocumentSession::PrepareMaskEditPublication(std::move(owned), executor, {}, {});
        Expect(valid.status == MaskEditStatus::Ready && valid.publication.has_value(),
               "input.moved-owner-remains-valid");
    }

    DocumentSession smallBudget;
    Expect(smallBudget.MaskEditPreparationMemoryBytes() == 0 && !smallBudget.PrepareMaskEditInput(true),
           "admission.no-document-refused");

    DocumentSession floor = LiteralSession(4, 3);
    floor.EditForTest([](CanvasDocument& document)
                      { document.selection = DocumentSelection{true, SelectionGeometry::Rect(1, 1, 2, 1)}; });
    floor.history.Reset();
    const auto memory = floor.MaskEditPreparationMemoryBytes();
    constexpr std::uint64_t pixels = 12;
    const std::uint64_t output = PixelBuffer::RequiredBytes(static_cast<std::size_t>(pixels));
    const std::uint64_t coverage = 2ull * sizeof(float) * (4 + 2) * 3;
    Expect(memory >= output + coverage, "admission.selection-covers-independent-float-cell-and-gray-storage-floor");

    // The retained-history limit does not limit job memory. Exhaust the independent 100 MP mask pixel budget.
    const Raster occupied = Raster::MakeSparse(10'000, 10'000, RasterFormat::Gray8, {}, 0, 0, {}, 255);
    constexpr std::uint8_t edge = 255;
    const Raster thumbnail = Raster::Make(1, 1, RasterFormat::Gray8, &edge, 1);
    ImageLayer other = ImageLayer::Blank(L"Mask budget owner", {1, 1});
    other.mask = LayerMask{RasterAsset{occupied, thumbnail, other.name}, true, std::nullopt, true};
    floor.EditForTest([&](CanvasDocument& document) { document.layers.push_back(std::move(other)); });
    floor.history.Reset();
    const auto before = floor.Document();
    const auto history = floor.history.CaptureState();
    Expect(occupied.HasStorage() && !occupied.IsMaterialized() && floor.MaskEditPreparationMemoryBytes() == 0 &&
               !floor.PrepareMaskEditInput(true) && floor.Document() == before && SameHistory(floor.history, history) &&
               !occupied.IsMaterialized(),
           "admission.independent-mask-budget-refuses-before-source-materialization-or-mutation");
}

void StalePublicationRefusals()
{
    InlineExecutor executor;
    {
        DocumentSession session = LiteralSession(3, 2);
        auto input = session.PrepareMaskEditInput(true);
        const auto before = session.Document();
        const auto history = session.history.CaptureState();
        CanvasDocument replacement = session.Document().value();
        const Guid active = session.ActiveLayerID().value();
        const Guid documentId = replacement.id;
        session.ReplaceDocument(std::move(replacement), active);
        Expect(session.Document().value().id == documentId && input.has_value(), "stale.same-id-replacement-fixture");
        if (!input)
            return;
        auto result = DocumentSession::PrepareMaskEditPublication(std::move(input.value()), executor, {}, {});
        Expect(result.publication && !session.ApplyPreparedMaskEditIfCurrent(result.publication.value()) &&
                   session.Document().value().layers.front().mask == std::nullopt &&
                   session.history.UndoCount() == static_cast<int>(history.PastCount()) && before.has_value(),
               "stale.same-id-new-document-serial-refused-without-mutation");
    }
    {
        DocumentSession session = LiteralSession(3, 2);
        auto input = session.PrepareMaskEditInput(false);
        session.SetOpacity(0.5);
        const auto document = session.Document();
        const auto history = session.history.CaptureState();
        auto result = DocumentSession::PrepareMaskEditPublication(std::move(input.value()), executor, {}, {});
        Expect(result.publication && !session.ApplyPreparedMaskEditIfCurrent(result.publication.value()) &&
                   session.Document() == document && SameHistory(session.history, history),
               "stale.changed-generation-refused");
    }
    {
        DocumentSession session = LiteralSession(3, 2);
        auto input = session.PrepareMaskEditInput(false);
        session.history.Reset();
        const auto document = session.Document();
        const auto history = session.history.CaptureState();
        auto result = DocumentSession::PrepareMaskEditPublication(std::move(input.value()), executor, {}, {});
        Expect(result.publication && !session.ApplyPreparedMaskEditIfCurrent(result.publication.value()) &&
                   session.Document() == document && SameHistory(session.history, history),
               "stale.history-epoch-refused");
    }
    {
        DocumentSession session = LiteralSession(3, 2);
        ImageLayer other = ImageLayer::Blank(L"Other", Vec2{1, 1});
        const Guid otherId = other.id;
        session.EditForTest([&](CanvasDocument& document) { document.layers.push_back(std::move(other)); });
        session.history.Reset();
        auto input = session.PrepareMaskEditInput(false);
        session.SelectLayer(otherId);
        const auto document = session.Document();
        const auto history = session.history.CaptureState();
        auto result = DocumentSession::PrepareMaskEditPublication(std::move(input.value()), executor, {}, {});
        Expect(result.publication && !session.ApplyPreparedMaskEditIfCurrent(result.publication.value()) &&
                   session.Document() == document && SameHistory(session.history, history) &&
                   session.ActiveLayerID() == otherId,
               "stale.active-layer-refused");
    }
}

void CancellationAndHistoryFaults()
{
    InlineExecutor executor;
    CancelSource precancelled;
    static_cast<void>(precancelled.Cancel());
    {
        DocumentSession session = LiteralSession(8, 5);
        session.EditForTest([](CanvasDocument& document)
                            { document.selection = DocumentSelection{true, SelectionGeometry::Rect(1, 1, 3, 2)}; });
        session.history.Reset();
        const auto before = session.Document();
        const auto history = session.history.CaptureState();
        auto input = session.PrepareMaskEditInput(true);
        auto result =
            DocumentSession::PrepareMaskEditPublication(std::move(input.value()), executor, precancelled.Token(), {});
        Expect(result.status == MaskEditStatus::Cancelled && !result.publication && session.Document() == before &&
                   SameHistory(session.history, history),
               "cancel.precancel-no-mutation");
    }

    for (const MaskWorkStage stage :
         {MaskWorkStage::Coverage, MaskWorkStage::Pixels, MaskWorkStage::Thumbnail, MaskWorkStage::Document})
    {
        DocumentSession session = LiteralSession(8, 5);
        session.EditForTest([](CanvasDocument& document)
                            { document.selection = DocumentSelection{true, SelectionGeometry::Rect(1, 1, 3, 2)}; });
        session.history.Reset();
        const auto before = session.Document();
        const auto history = session.history.CaptureState();
        CancelSource cancellation;
        CancelAtStage state{&cancellation, stage};
        const MaskWorkObserver observer{&CancelCheckpoint, &state};
        auto input = session.PrepareMaskEditInput(true);
        auto result = DocumentSession::PrepareMaskEditPublication(std::move(input.value()), executor,
                                                                  cancellation.Token(), observer);
        Expect(result.status == MaskEditStatus::Cancelled && !result.publication && state.hits == 1,
               "cancel.stage-reports-cancelled-without-publication");
        Expect(session.Document() == before && SameHistory(session.history, history),
               "cancel.stage-source-history-unchanged");
    }
    {
        DocumentSession session = LiteralSession(8, 5);
        session.EditForTest([](CanvasDocument& document)
                            { document.selection = DocumentSelection{true, SelectionGeometry::Rect(1, 1, 3, 2)}; });
        session.history.Reset();
        const auto before = session.Document();
        const auto history = session.history.CaptureState();
        ThrowHistoryAllocation fault;
        const DocumentHistory::PrepareObserver observer{&ThrowAtHistoryEntry, &fault};
        auto input = session.PrepareMaskEditInput(true);
        bool caught = false;
        try
        {
            static_cast<void>(
                DocumentSession::PrepareMaskEditPublication(std::move(input.value()), executor, {}, {}, observer));
        }
        catch (const std::bad_alloc&)
        {
            caught = true;
        }
        Expect(caught && fault.hits == 1, "history.entry-allocation-bad-alloc-propagates");
        Expect(session.Document() == before && SameHistory(session.history, history) &&
                   session.history.UndoCount() == 0,
               "history.entry-allocation-failure-does-not-mutate-session");
    }
}

} // namespace

int RunMaskEditWorkTests()
{
    gFailures = 0;
    gAssertions = 0;
    SelectedMaskPolarityAndHistory();
    NoSelectionAndExplicitEmpty();
    TransformMappingAndPixels();
    UnchangedMovedInputAndAdmission();
    StalePublicationRefusals();
    CancellationAndHistoryFaults();
    std::printf("%d mask-work assertion(s), %d failure(s).\n", gAssertions, gFailures);
    return gFailures;
}
