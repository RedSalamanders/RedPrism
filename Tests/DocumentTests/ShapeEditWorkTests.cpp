#include "ShapeEditWorkTests.h"

#include "Document.h"
#include "Executor.h"
#include "ShapeEditWork.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <limits>
#include <new>
#include <optional>
#include <system_error>
#include <thread>
#include <utility>

namespace
{
int gFailures = 0;
int gAssertions = 0;

void Expect(bool condition, const char* name)
{
    ++gAssertions;
    std::printf("%s shape-work.%s\n", condition ? "PASS" : "FAIL", name);
    if (!condition)
        ++gFailures;
}

ShapeDraft Draft(ShapeKind kind, double x, double y, double width, double height, double radius = 0) noexcept
{
    return ShapeDraft{kind, Vec2{x, y}, SelectionBounds{x, y, x + width, y + height}, radius};
}

bool PixelEquals(const Raster& raster, int x, int y, std::array<std::uint8_t, 4> expected) noexcept
{
    if (!raster.HasStorage() || raster.Format() != RasterFormat::Bgra8888 || x < 0 || y < 0 || x >= raster.Width() ||
        y >= raster.Height())
        return false;
    std::array<std::uint8_t, 4> actual{};
    raster.ReadRegion(x, y, 1, 1, actual.data(), 4, 0);
    return actual == expected;
}

bool AlphaEquals(const Raster& raster, int x, int y, std::uint8_t expected) noexcept
{
    if (!raster.HasStorage() || raster.Format() != RasterFormat::Bgra8888 || x < 0 || y < 0 || x >= raster.Width() ||
        y >= raster.Height())
        return false;
    std::array<std::uint8_t, 4> actual{};
    raster.ReadRegion(x, y, 1, 1, actual.data(), 4, 0);
    return actual[3] == expected;
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

ShapeEditResult Prepare(DocumentSession& session, const ShapeDraft& draft, std::uint32_t foreground, Executor& executor,
                        CancelToken cancellation = {}, ShapeWorkObserver observer = {},
                        DocumentHistory::PrepareObserver historyObserver = {})
{
    auto input = session.PrepareShapeEditInput(draft, foreground);
    if (!input)
        return {};
    return DocumentSession::PrepareShapeEditPublication(std::move(input.value()), executor, cancellation, observer,
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
    ShapeWorkStage target = ShapeWorkStage::Geometry;
    int hits = 0;
};

void CancelCheckpoint(void* opaque, ShapeWorkStage stage)
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

class SmallThreadExecutor final : public Executor
{
  public:
    [[nodiscard]] unsigned int Concurrency() const noexcept override
    {
        return 4;
    }
    [[nodiscard]] std::stop_token ShutdownToken() const noexcept override
    {
        return {};
    }
    [[nodiscard]] std::size_t HelpersStarted() const noexcept
    {
        return _helpersStarted;
    }
    void RunParticipants(void* context, Participant run, unsigned int participants) noexcept override
    {
        std::array<std::thread, 3> helpers;
        std::size_t started = 0;
        const std::size_t requested =
            (std::min)(helpers.size(), static_cast<std::size_t>(participants > 0 ? participants - 1 : 0));
        for (; started < requested; ++started)
        {
            try
            {
                helpers[started] = std::thread(run, context);
            }
            catch (const std::system_error&)
            {
                break;
            }
            catch (const std::bad_alloc&)
            {
                std::terminate();
            }
        }
        run(context);
        _helpersStarted += started;
        for (std::size_t i = 0; i < started; ++i)
            helpers[i].join();
    }

  private:
    std::size_t _helpersStarted = 0;
};

void LegacyRectangleAndHistory()
{
    DocumentSession session;
    session.CreateDocument(100, 80, true);
    session.SelectAll();
    const auto selection = session.Selection();
    const auto beforeCount = session.history.UndoCount();
    session.SetForegroundColor(0xFF0000FFu);
    InlineExecutor executor;
    auto input = session.PrepareShapeEditInput(Draft(ShapeKind::Rectangle, 10, 10, 30, 20), 0x12FF0000u);
    Expect(input.has_value(), "legacy.rectangle-input-captured");
    if (!input)
        return;
    session.SetForegroundColor(0xFF00FF00u);
    auto result = DocumentSession::PrepareShapeEditPublication(std::move(input.value()), executor, {}, {});
    const bool ready = result.status == ShapeEditStatus::Ready && result.publication.has_value();
    Expect(ready, "legacy.rectangle-prepared");
    if (!ready)
        return;
    auto& publication = result.publication.value();
    const bool applied = session.ApplyPreparedShapeEditIfCurrent(publication);
    Expect(applied, "legacy.rectangle-published");
    if (!applied)
        return;
    const auto* layer = session.ActiveLayer();
    Expect(layer && layer->name == L"Rectangle 1" && layer->asset.has_value(), "legacy.rectangle-name-and-image");
    if (layer && layer->asset)
    {
        const Raster& image = layer->asset.value().image;
        Expect(image.Width() == 30 && image.Height() == 20 && layer->transform.origin == Vec2{10, 10} &&
                   layer->transform.size == Vec2{30, 20},
               "legacy.rectangle-30x20-placement");
        const auto liveShape = LiveShape(*layer);
        Expect(liveShape.has_value() && liveShape.value().kind == ShapeKind::Rectangle &&
                   liveShape.value().red == 1.0 && liveShape.value().green == 0.0 && liveShape.value().blue == 0.0,
               "publication.live-shape-style-retained");
        Expect(PixelEquals(image, 0, 0, {0, 0, 255, 255}) && PixelEquals(image, 15, 10, {0, 0, 255, 255}) &&
                   PixelEquals(image, 29, 19, {0, 0, 255, 255}),
               "legacy.rectangle-opaque-red-bgra-ignores-input-alpha");
        Expect(AlphaEquals(image, 0, 10, 255) && AlphaEquals(image, 29, 10, 255),
               "legacy.rectangle-inclusive-pixel-bounds");
    }
    Expect(session.Selection() == selection, "publication.selection-retained");
    Expect(session.history.UndoCount() == beforeCount + 1 && session.history.UndoName() == L"Rectangle",
           "publication.one-named-history-entry");
    Expect(!session.ApplyPreparedShapeEditIfCurrent(publication), "publication.single-use");
    session.Undo();
    Expect(session.Document().value().layers.size() == 1 && session.Selection() == selection &&
               session.history.CanRedo(),
           "legacy.rectangle-one-undo-restores-source-and-selection");
    session.Redo();
    Expect(session.Document().value().layers.size() == 2 && session.ActiveLayer() &&
               session.ActiveLayer()->name == L"Rectangle 1",
           "publication.redo-restores-shape");
    auto second = Prepare(session, Draft(ShapeKind::Rectangle, 60, 10, 10, 10), 0xFFFF0000u, executor);
    const bool secondReady = second.status == ShapeEditStatus::Ready && second.publication.has_value();
    Expect(secondReady, "legacy.second-rectangle-prepared");
    if (secondReady)
    {
        const bool secondApplied = session.ApplyPreparedShapeEditIfCurrent(second.publication.value());
        Expect(secondApplied && session.ActiveLayer() && session.ActiveLayer()->name == L"Rectangle 2" &&
                   session.history.UndoCount() == beforeCount + 2,
               "legacy.second-name-and-one-entry");
        if (secondApplied)
        {
            session.Undo();
            session.Undo();
            Expect(session.Document().value().layers.size() == 1 && session.history.CanRedo(),
                   "legacy.two-undo-steps-remove-two-shapes");
        }
    }
}

void LegacyEllipseRoundedAndPill()
{
    InlineExecutor executor;
    {
        DocumentSession session;
        session.CreateDocument(100, 80, true);
        auto result = Prepare(session, Draft(ShapeKind::Ellipse, 40, 30, 20, 20), 0x00FF0000u, executor);
        const bool ready = result.status == ShapeEditStatus::Ready && result.publication.has_value();
        Expect(ready, "legacy.ellipse-prepared");
        if (ready && session.ApplyPreparedShapeEditIfCurrent(result.publication.value()))
        {
            const auto* layer = session.ActiveLayer();
            Expect(layer && layer->name == L"Ellipse 1" && layer->transform.origin == Vec2{40, 30} &&
                       layer->transform.size == Vec2{20, 20},
                   "legacy.ellipse-20x20-placement");
            if (layer && layer->asset)
            {
                const Raster& image = layer->asset.value().image;
                Expect(PixelEquals(image, 10, 10, {0, 0, 255, 255}), "legacy.ellipse-center-opaque-red");
                Expect(AlphaEquals(image, 0, 0, 0) && AlphaEquals(image, 19, 19, 0),
                       "legacy.ellipse-box-corners-clear");
                Expect(AlphaEquals(image, 1, 10, 255) && AlphaEquals(image, 10, 1, 255),
                       "legacy.ellipse-cardinal-interior-opaque");
            }
        }
        else if (ready)
            Expect(false, "legacy.ellipse-published");
    }
    {
        DocumentSession session;
        session.CreateDocument(100, 80, true);
        auto result = Prepare(session, Draft(ShapeKind::Rectangle, 10, 10, 40, 30, 8), 0xFF0000FFu, executor);
        const bool ready = result.status == ShapeEditStatus::Ready && result.publication.has_value();
        Expect(ready, "legacy.rounded-rectangle-prepared");
        if (ready && session.ApplyPreparedShapeEditIfCurrent(result.publication.value()))
        {
            const auto* layer = session.ActiveLayer();
            if (layer && layer->asset)
            {
                const Raster& image = layer->asset.value().image;
                Expect(AlphaEquals(image, 0, 0, 0) && AlphaEquals(image, 1, 1, 0),
                       "legacy.rounded-corner-0-and-1-1-clear");
                Expect(AlphaEquals(image, 3, 3, 255) && AlphaEquals(image, 20, 0, 255),
                       "legacy.rounded-corner-3-3-and-straight-top-full");
            }
            else
                Expect(false, "legacy.rounded-image-present");
        }
        else if (ready)
            Expect(false, "legacy.rounded-published");
    }
    {
        DocumentSession session;
        session.CreateDocument(100, 80, true);
        auto result = Prepare(session, Draft(ShapeKind::Rectangle, 55, 50, 40, 20, 500), 0xFF0000FFu, executor);
        const bool ready = result.status == ShapeEditStatus::Ready && result.publication.has_value();
        Expect(ready, "legacy.pill-prepared");
        if (ready && session.ApplyPreparedShapeEditIfCurrent(result.publication.value()))
        {
            const auto* layer = session.ActiveLayer();
            if (layer && layer->asset)
            {
                const Raster& image = layer->asset.value().image;
                Expect(image.Width() == 40 && image.Height() == 20 && AlphaEquals(image, 0, 0, 0) &&
                           AlphaEquals(image, 20, 10, 255),
                       "legacy.pill-40x20-radius-clamped-to-10");
            }
            else
                Expect(false, "legacy.pill-image-present");
        }
        else if (ready)
            Expect(false, "legacy.pill-published");
    }
}

void AtomicGroupSelectionMaskAndRedo()
{
    DocumentSession session;
    session.CreateDocument(100, 80, true);
    session.AddLayerMask();
    const Guid sourceID = session.ActiveLayerID().value();
    session.SelectLayerTarget(sourceID, true);
    session.SelectAll();
    session.AddGroup();
    const Guid groupID = session.ActiveLayerID().value();
    static_cast<void>(session.PlaceLayer(sourceID, groupID));
    session.SelectLayerTarget(sourceID, true);
    const auto* group = FindLayer(session, groupID);
    Expect(group && group->isGroup, "publication.group-fixture");
    if (!group)
        return;
    session.SetOpacity(0.5);
    session.Undo();
    session.SelectLayerTarget(sourceID, true);
    session.CollapsedGroupIDs().insert(groupID);
    const auto selection = session.Selection();
    const auto sourceDocument = session.Document();
    const auto priorCount = session.history.UndoCount();
    Expect(session.history.CanRedo(), "publication.redo-fixture");
    InlineExecutor executor;
    auto result = Prepare(session, Draft(ShapeKind::Rectangle, 4, 5, 8, 6), 0xFF00FF00u, executor);
    const bool ready = result.status == ShapeEditStatus::Ready && result.publication.has_value();
    Expect(ready, "publication.group-shape-prepared");
    if (!ready)
        return;
    const bool applied = session.ApplyPreparedShapeEditIfCurrent(result.publication.value());
    Expect(applied, "publication.group-shape-applied");
    if (!applied)
        return;
    const auto* added = session.ActiveLayer();
    const auto groupIndex = IndexOf(session.Document().value().layers, groupID);
    const auto sourceIndex = IndexOf(session.Document().value().layers, sourceID);
    std::optional<std::size_t> addedIndex;
    if (added)
        addedIndex = IndexOf(session.Document().value().layers, added->id);
    Expect(added && added->parentID == groupID && added->name == L"Rectangle 1" && groupIndex.has_value() &&
               sourceIndex.has_value() && addedIndex.has_value() && groupIndex.value() < sourceIndex.value() &&
               sourceIndex.value() < addedIndex.value() && session.Document().value().layers.back().id == added->id,
           "publication.inserted-under-active-group-after-subtree");
    Expect(added && !added->mask.has_value() && !session.IsMaskSelected(), "publication.mask-target-reset");
    Expect(session.Selection() == selection && selection.has_value() &&
               session.Document().value().selection == sourceDocument.value().selection,
           "publication.selection-retained-in-document");
    Expect(!session.CollapsedGroupIDs().contains(groupID), "publication.parent-group-expanded");
    Expect(session.history.UndoCount() == priorCount + 1 && !session.history.CanRedo() &&
               session.history.UndoName() == L"Rectangle",
           "publication.single-entry-clears-redo");
    session.Undo();
    Expect(session.Document() == sourceDocument && session.Selection() == selection && session.history.CanRedo(),
           "publication.undo-restores-document-and-selection");
}

void StaleInputsAndMovedFrom()
{
    InlineExecutor executor;
    {
        DocumentSession session;
        session.CreateDocument(32, 24, true);
        auto input = session.PrepareShapeEditInput(Draft(ShapeKind::Rectangle, 1, 1, 4, 4), 0xFFFF0000u);
        Expect(input.has_value(), "stale.same-id-input-prepared");
        if (input)
        {
            const Guid id = session.ActiveLayerID().value();
            session.EditForTest(
                [](CanvasDocument& document)
                {
                    ImageLayer& layer = document.layers.front();
                    const std::array<std::uint8_t, 4> pixel{0x37, 0x29, 0x1B, 0xFF};
                    const Raster replacement = Raster::Make(1, 1, RasterFormat::Bgra8888, pixel.data(), pixel.size());
                    layer.asset = RasterAsset{replacement, replacement, layer.name};
                });
            const auto before = session.Document();
            const auto history = session.history.CaptureState();
            auto result = DocumentSession::PrepareShapeEditPublication(std::move(input.value()), executor, {}, {});
            const bool prepared = result.status == ShapeEditStatus::Ready && result.publication.has_value();
            Expect(prepared, "stale.same-id-worker-prepared");
            const bool published = prepared && session.ApplyPreparedShapeEditIfCurrent(result.publication.value());
            Expect(prepared && !published && session.ActiveLayerID() == id && session.Document() == before &&
                       SameHistory(session.history, history),
                   "stale.same-id-replacement-rejected");
        }
    }
    {
        DocumentSession session;
        session.CreateDocument(32, 24, true);
        auto input = session.PrepareShapeEditInput(Draft(ShapeKind::Rectangle, 1, 1, 4, 4), 0xFFFF0000u);
        Expect(input.has_value(), "stale.generation-input-prepared");
        if (input)
        {
            session.EditForTest([](CanvasDocument&) {});
            const auto before = session.Document();
            const auto history = session.history.CaptureState();
            auto result = DocumentSession::PrepareShapeEditPublication(std::move(input.value()), executor, {}, {});
            Expect(result.publication.has_value(), "stale.generation-worker-prepared");
            if (result.publication)
                Expect(!session.ApplyPreparedShapeEditIfCurrent(result.publication.value()) &&
                           session.Document() == before && SameHistory(session.history, history),
                       "stale.changed-generation-refused");
        }
    }
    {
        DocumentSession session;
        session.CreateDocument(32, 24, true);
        auto input = session.PrepareShapeEditInput(Draft(ShapeKind::Rectangle, 1, 1, 4, 4), 0xFFFF0000u);
        Expect(input.has_value(), "stale.history-input-prepared");
        if (input)
        {
            session.SetOpacity(0.5);
            session.Undo();
            const auto before = session.Document();
            const auto state = session.history.CaptureState();
            auto result = DocumentSession::PrepareShapeEditPublication(std::move(input.value()), executor, {}, {});
            Expect(result.publication.has_value(), "stale.history-worker-prepared");
            if (result.publication)
                Expect(!session.ApplyPreparedShapeEditIfCurrent(result.publication.value()) &&
                           session.Document() == before && SameHistory(session.history, state),
                       "stale.changed-history-refused");
        }
    }
    {
        DocumentSession session;
        session.CreateDocument(32, 24, true);
        auto input = session.PrepareShapeEditInput(Draft(ShapeKind::Rectangle, 1, 1, 4, 4), 0xFFFF0000u);
        Expect(input.has_value(), "stale.transform-input-prepared");
        if (input)
        {
            session.EditForTest([](CanvasDocument& document)
                                { document.layers.front().transform.origin = Vec2{3, 4}; });
            const auto before = session.Document();
            auto result = DocumentSession::PrepareShapeEditPublication(std::move(input.value()), executor, {}, {});
            Expect(result.publication.has_value(), "stale.transform-worker-prepared");
            if (result.publication)
                Expect(!session.ApplyPreparedShapeEditIfCurrent(result.publication.value()) &&
                           session.Document() == before,
                       "stale.changed-source-transform-refused");
        }
    }
    {
        DocumentSession session;
        session.CreateDocument(32, 24, true);
        auto input = session.PrepareShapeEditInput(Draft(ShapeKind::Rectangle, 1, 1, 4, 4), 0xFFFF0000u);
        Expect(input.has_value(), "input.moved-from-fixture");
        if (input)
        {
            ShapeEditInput moved(std::move(input.value()));
            static_cast<void>(moved);
            auto result = DocumentSession::PrepareShapeEditPublication(std::move(input.value()), executor, {}, {});
            Expect(result.status == ShapeEditStatus::Invalid && !result.publication, "input.moved-from-rejected");
        }
    }
}

void ActiveGroupSubtreeAndUnsettledDraft()
{
    DocumentSession session;
    CanvasDocument document;
    document.id = Guid::Generate();
    document.width = 100;
    document.height = 80;
    auto group = ImageLayer::Blank(L"Outer", {100, 80});
    group.isGroup = true;
    auto nested = ImageLayer::Blank(L"Nested", {100, 80});
    nested.isGroup = true;
    nested.parentID = group.id;
    auto child = ImageLayer::Blank(L"Child", {2, 2});
    child.parentID = nested.id;
    const auto unrelated = ImageLayer::Blank(L"Root sibling", {2, 2});
    document.layers = {group, nested, child, unrelated};
    session.ReplaceDocument(std::move(document), group.id);
    session.CollapsedGroupIDs().insert(group.id);
    session.CollapsedGroupIDs().insert(nested.id);
    InlineExecutor executor;
    auto result = Prepare(session, Draft(ShapeKind::Rectangle, 4, 5, 8, 6), 0xFF0033AAu, executor);
    const bool applied = result.publication && session.ApplyPreparedShapeEditIfCurrent(result.publication.value());
    Expect(applied, "group.active-group-publication");
    if (applied)
    {
        const auto* added = session.ActiveLayer();
        Expect(added && added->parentID == group.id && session.Document()->layers.size() == 5 &&
                   session.Document()->layers[3].id == added->id && session.Document()->layers[4].id == unrelated.id,
               "group.inserts-after-entire-nested-subtree-before-root-sibling");
        Expect(!session.CollapsedGroupIDs().contains(group.id) && session.CollapsedGroupIDs().contains(nested.id),
               "group.expands-only-inserted-parent");
    }
    auto pending = Prepare(session, Draft(ShapeKind::Rectangle, 0, 0, 2, 2), 0xFFFF0000u, executor);
    const auto before = session.Document();
    const auto history = session.history.CaptureState();
    session.BeginShape({3, 3});
    Expect(pending.publication && session.PendingShape() &&
               !session.ApplyPreparedShapeEditIfCurrent(pending.publication.value()) && session.Document() == before &&
               SameHistory(session.history, history) && session.PendingShape(),
           "draft.new-unsettled-shape-refuses-old-publication-and-survives");
}

void CancellationAndHistoryFaults()
{
    InlineExecutor executor;
    constexpr std::array<ShapeWorkStage, 4> stages{ShapeWorkStage::Geometry, ShapeWorkStage::Coverage,
                                                   ShapeWorkStage::Pixels, ShapeWorkStage::Document};
    for (const ShapeWorkStage stage : stages)
    {
        DocumentSession session;
        session.CreateDocument(40, 30, true);
        session.SelectAll();
        const auto before = session.Document();
        const auto selection = session.Selection();
        const auto history = session.history.CaptureState();
        CancelSource cancellation;
        CancelAtStage state{&cancellation, stage};
        const ShapeWorkObserver observer{&CancelCheckpoint, &state};
        auto result = Prepare(session, Draft(ShapeKind::Rectangle, 2, 3, 12, 10), 0xFFABCDEFu, executor,
                              cancellation.Token(), observer);
        Expect(result.status == ShapeEditStatus::Cancelled && !result.publication && state.hits == 1,
               "cancel.stage-no-publication");
        Expect(session.Document() == before && session.Selection() == selection &&
                   SameHistory(session.history, history),
               "cancel.stage-source-and-history-unchanged");
    }
    {
        DocumentSession session;
        session.CreateDocument(40, 30, true);
        const auto before = session.Document();
        const auto history = session.history.CaptureState();
        CancelSource cancellation;
        static_cast<void>(cancellation.Cancel());
        const auto result =
            Prepare(session, Draft(ShapeKind::Rectangle, 2, 3, 12, 10), 0xFFABCDEFu, executor, cancellation.Token());
        Expect(result.status == ShapeEditStatus::Cancelled && !result.publication && session.Document() == before &&
                   SameHistory(session.history, history),
               "cancel.before-work-no-publication-or-mutation");
    }
    {
        DocumentSession session;
        session.CreateDocument(40, 30, true);
        session.AddBlankLayer();
        session.Undo();
        const auto before = session.Document();
        const auto state = session.history.CaptureState();
        ThrowHistoryAllocation fault;
        const DocumentHistory::PrepareObserver observer{&ThrowAtHistoryEntry, &fault};
        bool caught = false;
        try
        {
            static_cast<void>(
                Prepare(session, Draft(ShapeKind::Rectangle, 2, 3, 12, 10), 0xFFABCDEFu, executor, {}, {}, observer));
        }
        catch (const std::bad_alloc&)
        {
            caught = true;
        }
        Expect(caught && fault.hits == 1, "history-fault.bad-alloc-propagates");
        Expect(session.Document() == before && SameHistory(session.history, state) && session.history.CanRedo(),
               "history-fault.live-document-and-redo-untouched");
    }
}

void DraftValidationAndBudget()
{
    int width = -1;
    int height = -1;
    ShapeDraft invalid = Draft(ShapeKind::Rectangle, 0, 0, 4, 4);
    invalid.rect.minX = (std::numeric_limits<double>::quiet_NaN)();
    Expect(InspectShapeDraft(invalid, width, height) == ShapeEditStatus::Invalid && width == 0 && height == 0,
           "draft.nonfinite-zeroes-output-dimensions");
    invalid = Draft(ShapeKind::Rectangle, 0, 0, 4, 4);
    invalid.cornerRadius = (std::numeric_limits<double>::infinity)();
    Expect(InspectShapeDraft(invalid, width, height) == ShapeEditStatus::Invalid && width == 0 && height == 0,
           "draft.nonfinite-radius-zeroes-output-dimensions");
    invalid = Draft(ShapeKind::Rectangle, 0, 0, 4, 4);
    invalid.kind = static_cast<ShapeKind>(0xFF);
    Expect(InspectShapeDraft(invalid, width, height) == ShapeEditStatus::Invalid && width == 0 && height == 0,
           "draft.invalid-kind-zeroes-output-dimensions");
    Expect(InspectShapeDraft(Draft(ShapeKind::Rectangle, 0, 0, 10'000, 10'001), width, height) ==
                   ShapeEditStatus::TooLarge &&
               width == 0 && height == 0,
           "draft.over-pixel-budget-zeroes-output-dimensions");
    Expect(InspectShapeDraft(Draft(ShapeKind::Rectangle, 0, 0, 30'001, 1), width, height) ==
                   ShapeEditStatus::TooLarge &&
               width == 0 && height == 0,
           "draft.over-side-limit-zeroes-output-dimensions");
    Expect(InspectShapeDraft(Draft(ShapeKind::Rectangle, 1, 1, 0, 20), width, height) == ShapeEditStatus::Unchanged &&
               width == 0 && height == 0,
           "draft.click-zeroes-output-dimensions");

    constexpr std::uint64_t pixels = 40ull * 30;
    const std::uint64_t outputFloor = pixels * 4;
    const std::uint64_t outputStorageFloor = PixelBuffer::RequiredBytes(static_cast<std::size_t>(outputFloor));
    const std::uint64_t coverageStorageFloor = PixelBuffer::RequiredBytes(static_cast<std::size_t>(pixels));
    const auto solidBudget = ShapeImageMemoryBytes(ShapeKind::Rectangle, 40, 30, 0);
    const auto ellipseBudget = ShapeImageMemoryBytes(ShapeKind::Ellipse, 40, 30, 0);
    const auto roundedBudget = ShapeImageMemoryBytes(ShapeKind::Rectangle, 40, 30, 8);
    Expect(solidBudget >= outputStorageFloor && solidBudget < pixels * 8,
           "budget.solid-rectangle-has-no-antialias-buffer-peak");
    Expect(ellipseBudget >= outputStorageFloor + coverageStorageFloor + 4096 &&
               roundedBudget >= outputStorageFloor + coverageStorageFloor + 4096,
           "budget.curved-shapes-cover-output-and-coverage-floor");
    DocumentSession session;
    session.CreateDocument(100, 80, true);
    const auto admitted = session.ShapeEditPreparationMemoryBytes(Draft(ShapeKind::Rectangle, 0, 0, 40, 30));
    Expect(admitted >= solidBudget, "budget.session-declaration-covers-independent-raster-floor");
}

void ParallelMatchesInline()
{
    InlineExecutor inlineExecutor;
    SmallThreadExecutor threadedExecutor;
    const Raster inlineImage = ShapeImageForWork(ShapeKind::Ellipse, 300, 20, 0x3A72B4C6u, 0, inlineExecutor);
    const Raster parallelImage = ShapeImageForWork(ShapeKind::Ellipse, 300, 20, 0x3A72B4C6u, 0, threadedExecutor);
    bool equal = inlineImage.HasStorage() && parallelImage.HasStorage() &&
                 inlineImage.Width() == parallelImage.Width() && inlineImage.Height() == parallelImage.Height();
    if (equal)
    {
        const auto* a = inlineImage.Data();
        const auto* b = parallelImage.Data();
        equal = a != nullptr && b != nullptr &&
                std::equal(a, a + static_cast<std::size_t>(inlineImage.Width()) * inlineImage.Height() * 4, b);
    }
    Expect(equal, "parallel.stateless-color-output-matches-inline-literal-engine");
    Expect(threadedExecutor.HelpersStarted() != 0, "parallel.participant-actually-started");
}
} // namespace

int RunShapeEditWorkTests()
{
    gFailures = 0;
    gAssertions = 0;
    LegacyRectangleAndHistory();
    LegacyEllipseRoundedAndPill();
    AtomicGroupSelectionMaskAndRedo();
    StaleInputsAndMovedFrom();
    ActiveGroupSubtreeAndUnsettledDraft();
    CancellationAndHistoryFaults();
    DraftValidationAndBudget();
    ParallelMatchesInline();
    std::printf("shape-work assertions=%d failures=%d\n", gAssertions, gFailures);
    return gFailures;
}
