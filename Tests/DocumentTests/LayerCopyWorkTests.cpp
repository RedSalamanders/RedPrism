#include "LayerCopyWorkTests.h"

#include "../Support/DocumentFixture.h"

#include "Document.h"
#include "Executor.h"
#include "LayerCopyWork.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <new>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

namespace
{
int gFailures = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s layer-copy.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

class ExecutorSerial final : public Executor
{
  public:
    [[nodiscard]] unsigned int Concurrency() const noexcept override
    {
        return 1;
    }
    [[nodiscard]] std::stop_token ShutdownToken() const noexcept override
    {
        return {};
    }
    void RunParticipants(void* context, Participant run, unsigned int) noexcept override
    {
        run(context);
    }
};

Guid Id(const char* text)
{
    return Guid::Parse(text).value();
}

constexpr std::array<std::uint8_t, 24> kSourceBgra{
    1, 2, 3, 255, 11, 12, 13, 255, 21, 22, 23, 255, 31, 32, 33, 255, 41, 42, 43, 255, 51, 52, 53, 255,
};
constexpr std::array<std::uint8_t, 16> kSelectedBgra{
    21, 22, 23, 255, 31, 32, 33, 255, 41, 42, 43, 255, 51, 52, 53, 255,
};
constexpr std::array<std::uint8_t, 6> kMaskGray{100, 150, 200, 50, 60, 70};
constexpr std::array<std::uint8_t, 8> kSelectedMaskBgra{60, 60, 60, 255, 70, 70, 70, 255};

Raster Bgra(int width, int height, std::span<const std::uint8_t> bytes)
{
    return Raster::Make(width, height, RasterFormat::Bgra8888, bytes.data(), bytes.size());
}

Raster Gray(int width, int height, std::span<const std::uint8_t> bytes)
{
    return Raster::Make(width, height, RasterFormat::Gray8, bytes.data(), bytes.size());
}

template <std::size_t Size> Raster Bgra(int width, int height, const std::array<std::uint8_t, Size>& bytes)
{
    return Bgra(width, height, std::span<const std::uint8_t>{bytes});
}

template <std::size_t Size> Raster Gray(int width, int height, const std::array<std::uint8_t, Size>& bytes)
{
    return Gray(width, height, std::span<const std::uint8_t>{bytes});
}

bool RasterEquals(const Raster& image, int width, int height, std::span<const std::uint8_t> expected) noexcept
{
    if (!image.HasStorage() || image.Width() != width || image.Height() != height ||
        image.Format() != RasterFormat::Bgra8888 ||
        expected.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u)
        return false;
    const std::uint8_t* pixels = image.Data();
    return pixels && std::equal(expected.begin(), expected.end(), pixels);
}

DocumentSelection Rect(double x, double y, double width, double height)
{
    return DocumentSelection{
        false, SelectionLoops{{Vec2{x, y}, Vec2{x + width, y}, Vec2{x + width, y + height}, Vec2{x, y + height}}}};
}

ImageLayer SourceLayer(std::wstring name = L"Paint")
{
    const Raster image = Bgra(2, 3, kSourceBgra);
    ImageLayer layer = ImageLayer::Blank(std::move(name), Vec2{2, 3});
    layer.id = Id("61000000-0000-4000-8000-000000000002");
    layer.transform.origin = Vec2{1, 0};
    layer.transform.size = Vec2{2, 3};
    layer.transform.sampling = LayerSampling::Nearest;
    layer.asset = RasterAsset{image, Raster::MakeThumbnail(image), L"Source pixels"};
    return layer;
}

DocumentFixture CanvasDoc4x3()
{
    DocumentFixture document;
    document.id = Id("61000000-0000-4000-8000-000000000001");
    document.width = 4;
    document.height = 3;
    document.layers.push_back(SourceLayer());
    return document;
}

DocumentSession SelectedSession()
{
    DocumentSession session;
    DocumentFixture document = CanvasDoc4x3();
    const Guid active = document.layers.front().id;
    session.ReplaceDocument(std::move(document).Seal(), active);
    session.SetSelection(Rect(1, 1, 2, 2), L"Fixture selection");
    return session;
}

bool SameClipboard(const std::optional<PixelClipboard>& left, const std::optional<PixelClipboard>& right) noexcept
{
    return left.has_value() == right.has_value() &&
           (!left || (left->origin == right->origin && left->image == right->image));
}

struct OwnerState final
{
    std::optional<CanvasDocument> document;
    DocumentHistory::StateView history;
    Guid revision;
    std::size_t undoCount = 0;
    std::size_t redoCount = 0;
    std::wstring undoName;
    std::wstring redoName;
    std::uint64_t generation = 0;
    std::uint64_t clipboardSerial = 0;
    Guid clipboardIdentity;
    std::optional<PixelClipboard> clipboard;
    std::optional<Guid> active;
    std::unordered_set<Guid> selected;
    std::unordered_set<Guid> collapsed;
    bool maskSelected = false;

    explicit OwnerState(DocumentSession& session)
        : document(session.Document()), history(session.history.CaptureState()),
          revision(session.history.CurrentRevision()), undoCount(session.history.UndoCount()),
          redoCount(history.FutureCount()), undoName(session.history.UndoName()), redoName(session.history.RedoName()),
          generation(session.Generation()), clipboardSerial(session.ClipboardSerial()),
          clipboardIdentity(session.ClipboardSessionIdentity()), clipboard(session.Clipboard()),
          active(session.ActiveLayerID()), selected(session.SelectedLayerIDs()), collapsed(session.CollapsedGroupIDs()),
          maskSelected(session.IsMaskSelected())
    {
    }
};

bool SameOwnerState(DocumentSession& session, const OwnerState& before)
{
    const auto history = session.history.CaptureState();
    return session.Document() == before.document && session.history.IsCurrent(before.history) &&
           session.history.CurrentRevision() == before.revision && session.history.UndoCount() == before.undoCount &&
           history.FutureCount() == before.redoCount && session.history.UndoName() == before.undoName &&
           session.history.RedoName() == before.redoName && session.Generation() == before.generation &&
           session.ClipboardSerial() == before.clipboardSerial &&
           session.ClipboardSessionIdentity() == before.clipboardIdentity &&
           SameClipboard(session.Clipboard(), before.clipboard) && session.ActiveLayerID() == before.active &&
           session.SelectedLayerIDs() == before.selected && session.CollapsedGroupIDs() == before.collapsed &&
           session.IsMaskSelected() == before.maskSelected;
}

LayerCopyResult Prepare(DocumentSession& session, Executor& executor, CancelToken cancellation = {},
                        LayerCopyObserver observer = {}, DocumentHistory::PrepareObserver historyObserver = {})
{
    auto input = session.PrepareLayerCopyInput();
    return input ? DocumentSession::PrepareLayerCopyPublication(std::move(input.value()), executor, cancellation,
                                                                observer, historyObserver)
                 : LayerCopyResult{};
}

bool Apply(DocumentSession& session, LayerCopyResult& result)
{
    return result.status == LayerCopyStatus::Ready && result.publication &&
           session.ApplyPreparedLayerCopyIfCurrent(result.publication.value());
}

void SelectedCropPixelsOriginNameAndHistory()
{
    DocumentSession session = SelectedSession();
    DocumentFixture document = DocumentFixture::Copy(session.Document().value());
    ImageLayer other = ImageLayer::Blank(L"Layer 1", Vec2{1, 1});
    other.id = Id("61000000-0000-4000-8000-000000000003");
    ImageLayer otherName = ImageLayer::Blank(L"Layer 3", Vec2{1, 1});
    otherName.id = Id("61000000-0000-4000-8000-000000000004");
    document.layers.push_back(std::move(other));
    document.layers.push_back(std::move(otherName));
    const Guid active = document.layers.front().id;
    session.ReplaceDocument(std::move(document).Seal(), active);
    session.SetSelection(Rect(1, 1, 2, 2), L"Fixture selection");
    Expect(session.CopySelection(), "seed-private-clipboard-before-selected-copy");
    const OwnerState before(session);
    ExecutorSerial executor;
    LayerCopyResult prepared = Prepare(session, executor);
    Expect(prepared.status == LayerCopyStatus::Ready && prepared.publication.has_value(),
           "selection-copy-prepares-publication");
    const bool applied = Apply(session, prepared);
    const auto& after = session.Document().value();
    const ImageLayer* copied = session.ActiveLayer();
    Expect(applied && after.layers.size() == 4 && after.layers[1].id == session.ActiveLayerID().value() &&
               after.layers[0].id == active,
           "selection-copy-inserts-immediately-above-source");
    Expect(copied && copied->name == L"Layer 2" && copied->transform.origin == Vec2{1, 1} &&
               copied->transform.size == Vec2{2, 2} && copied->parentID == std::nullopt && copied->asset &&
               RasterEquals(copied->asset->image, 2, 2, kSelectedBgra),
           "selection-copy-has-literal-crop-origin-and-first-unused-layer-name");
    Expect(applied && !after.selection && !session.IsMaskSelected() && session.SelectedLayerIDs().size() == 1 &&
               session.SelectedLayerIDs().contains(copied->id),
           "selection-copy-drops-selection-and-targets-new-layer-pixels");
    Expect(applied && session.history.UndoCount() == before.undoCount + 1 &&
               session.history.UndoName() == L"Layer via Copy" && session.ClipboardSerial() == before.clipboardSerial &&
               session.ClipboardSessionIdentity() == before.clipboardIdentity &&
               SameClipboard(session.Clipboard(), before.clipboard),
           "selection-copy-is-one-history-entry-and-leaves-private-clipboard-identity-and-serial");
    session.Undo();
    Expect(session.Document() == before.document && session.ActiveLayerID() == before.active &&
               session.history.UndoCount() == before.undoCount && session.history.CanRedo(),
           "selection-copy-undo-restores-exact-document-and-active-layer");
    session.Redo();
    Expect(session.Document() && session.Document()->layers.size() == 4 && session.ActiveLayerID() &&
               session.ActiveLayerID().value() == session.Document()->layers[1].id &&
               session.Document()->layers[1].asset &&
               RasterEquals(session.Document()->layers[1].asset->image, 2, 2, kSelectedBgra) &&
               session.Document()->layers[1].transform.origin == Vec2{1, 1} && !session.Document()->selection &&
               session.history.UndoName() == L"Layer via Copy",
           "selection-copy-redo-restores-exact-copied-state");
}

void GroupInsertionAndMaskTargetCopy()
{
    {
        DocumentSession session;
        DocumentFixture document = CanvasDoc4x3();
        ImageLayer group = ImageLayer::Blank(L"Group", Vec2{1, 1});
        group.id = Id("61000000-0000-4000-8000-000000000011");
        group.isGroup = true;
        const Guid groupID = group.id;
        document.layers.front().parentID = groupID;
        document.layers.insert(document.layers.begin(), std::move(group));
        const Guid sourceID = document.layers[1].id;
        session.ReplaceDocument(std::move(document).Seal(), sourceID);
        session.CollapsedGroupIDs().insert(groupID);
        session.SetSelection(Rect(1, 1, 2, 2), L"Grouped selection");
        ExecutorSerial executor;
        LayerCopyResult prepared = Prepare(session, executor);
        const bool applied = Apply(session, prepared);
        const auto& after = session.Document().value();
        Expect(applied && after.layers.size() == 3 && after.layers[1].id == sourceID &&
                   after.layers[2].id == session.ActiveLayerID().value() && after.layers[2].parentID == groupID &&
                   !session.CollapsedGroupIDs().contains(groupID),
               "selected-copy-keeps-parent-insertion-and-expands-parent");
    }
    {
        DocumentSession session;
        DocumentFixture document = CanvasDoc4x3();
        const Guid sourceID = document.layers.front().id;
        const Raster mask = Gray(2, 3, kMaskGray);
        document.layers.front().mask = LayerMask{RasterAsset{mask, Raster::MakeThumbnail(mask), L"Mask"}};
        session.ReplaceDocument(std::move(document).Seal(), sourceID);
        session.SelectLayerTarget(sourceID, true);
        session.SetSelection(Rect(1, 2, 2, 1), L"Mask selection");
        ExecutorSerial executor;
        LayerCopyResult prepared = Prepare(session, executor);
        const bool applied = Apply(session, prepared);
        const ImageLayer* copied = session.ActiveLayer();
        Expect(applied && copied && copied->id != sourceID && copied->asset &&
                   RasterEquals(copied->asset->image, 2, 1, kSelectedMaskBgra) &&
                   copied->transform.origin == Vec2{1, 2} && !session.IsMaskSelected() &&
                   session.SelectedLayerIDs().contains(copied->id),
               "selected-mask-copy-renders-literal-gray-as-opaque-pixels-and-targets-new-layer");
    }
}

void DuplicatePreservesLayerSemantics()
{
    {
        DocumentSession session;
        DocumentFixture document = CanvasDoc4x3();
        document.layers.reserve(4);
        ImageLayer& source = document.layers.front();
        source.name = L"Portrait";
        source.visible = false;
        source.opacity = 0.375;
        source.blendMode = LayerBlendMode::Multiply;
        source.transform.origin = Vec2{1, 0};
        source.transform.rotation = 0;
        const Raster maskImage = Gray(2, 3, kMaskGray);
        const Raster thumbnail = Raster::Make(1, 1, RasterFormat::Bgra8888);
        source.asset->thumbnail = thumbnail;
        source.mask = LayerMask{RasterAsset{maskImage, Raster::MakeThumbnail(maskImage), L"Mask thumb"}, false,
                                LayerTransform{Vec2{0, 0}, Vec2{2, 3}, 0, false, false, LayerSampling::Nearest}, false};
        source.shape = LayerShapeStyle{ShapeKind::Ellipse, 0.25, 0.5, 0.75, 0.125};
        source.shapeImage = Bgra(2, 3, kSourceBgra);
        ImageLayer group = ImageLayer::Blank(L"Group", Vec2{1, 1});
        group.id = Id("61000000-0000-4000-8000-000000000020");
        group.isGroup = true;
        source.parentID = group.id;
        ImageLayer sibling = ImageLayer::Blank(L"Sibling", Vec2{1, 1});
        sibling.id = Id("61000000-0000-4000-8000-000000000021");
        document.layers.push_back(std::move(sibling));
        const Guid sourceID = source.id;
        const ImageLayer sourceSnapshot = source;
        document.layers.insert(document.layers.begin(), std::move(group));
        session.ReplaceDocument(std::move(document).Seal(), sourceID);
        Expect(session.CopySelection(), "seed-private-clipboard-before-duplicate");
        const OwnerState before(session);
        ExecutorSerial executor;
        LayerCopyResult prepared = Prepare(session, executor);
        const bool applied = Apply(session, prepared);
        const auto& after = session.Document().value();
        const ImageLayer* copied = session.ActiveLayer();
        ImageLayer expected = sourceSnapshot;
        const Guid copyID = copied ? copied->id : Guid{};
        expected.id = copyID;
        expected.name += L" copy";
        Expect(applied && copied && copied->id != sourceID && after.layers.size() == 4 &&
                   after.layers[1].id == sourceID && after.layers[2].id == copyID &&
                   after.layers[3].name == L"Sibling" && *copied == expected,
               "duplicate-layer-inserts-new-guid-in-order-and-clones-complete-layer-record");
        Expect(copied && copied->asset && sourceSnapshot.asset && copied->asset->image == sourceSnapshot.asset->image &&
                   copied->asset->thumbnail == sourceSnapshot.asset->thumbnail && copied->mask &&
                   copied->mask->asset.image == sourceSnapshot.mask->asset.image &&
                   copied->mask->asset.thumbnail == sourceSnapshot.mask->asset.thumbnail &&
                   copied->mask->enabled == sourceSnapshot.mask->enabled &&
                   copied->mask->placement == sourceSnapshot.mask->placement &&
                   copied->mask->linked == sourceSnapshot.mask->linked &&
                   copied->transform == sourceSnapshot.transform && copied->visible == sourceSnapshot.visible &&
                   copied->opacity == sourceSnapshot.opacity && copied->blendMode == sourceSnapshot.blendMode &&
                   copied->shape == sourceSnapshot.shape && copied->shapeImage == sourceSnapshot.shapeImage &&
                   copied->parentID == sourceSnapshot.parentID,
               "duplicate-layer-retains-image-thumbnail-placement-mask-semantics-and-shape");
        Expect(applied && session.history.UndoCount() == before.undoCount + 1 &&
                   session.history.UndoName() == L"Duplicate Layer" &&
                   session.ClipboardSerial() == before.clipboardSerial &&
                   session.ClipboardSessionIdentity() == before.clipboardIdentity &&
                   SameClipboard(session.Clipboard(), before.clipboard),
               "duplicate-layer-one-entry-and-private-clipboard-unchanged");
        session.Undo();
        Expect(session.Document() == before.document && session.ActiveLayerID() == before.active,
               "duplicate-layer-undo-restores-exact-original");
        session.Redo();
        const auto& redone = session.Document().value();
        Expect(redone.layers.size() == 4 && redone.layers[2].id == copyID && redone.layers[2] == expected &&
                   session.ActiveLayerID() == copyID && session.history.UndoName() == L"Duplicate Layer",
               "duplicate-layer-redo-restores-exact-copy");
    }
    {
        DocumentSession session;
        DocumentFixture document = CanvasDoc4x3();
        LayerAdjustment adjustment;
        adjustment.kind = AdjustmentKind::Exposure;
        adjustment.exposureSettings = ExposureSettings{1.25, 0.125, 1.5};
        document.layers.front().adjustment = adjustment;
        document.layers.front().asset.reset(); // Adjustment layers cannot own a raster asset.
        Expect(ValidateDocument(document.Seal()) == DocumentStatus::Ok, "adjustment-duplicate-fixture-is-valid");
        const Guid sourceID = document.layers.front().id;
        const ImageLayer source = document.layers.front();
        session.ReplaceDocument(std::move(document).Seal(), sourceID);
        ExecutorSerial executor;
        LayerCopyResult prepared = Prepare(session, executor);
        const bool applied = Apply(session, prepared);
        const ImageLayer* copied = session.ActiveLayer();
        Expect(applied && copied && copied->id != sourceID && copied->name == source.name + L" copy" &&
                   copied->adjustment == source.adjustment && !copied->asset && copied->adjustment &&
                   copied->adjustment->kind == AdjustmentKind::Exposure &&
                   copied->adjustment->exposureSettings == ExposureSettings{1.25, 0.125, 1.5},
               "duplicate-layer-preserves-adjustment-data-and-absent-asset");
    }
}

void RefusalsAndSparseBudgets()
{
    {
        DocumentSession session;
        DocumentFixture document = CanvasDoc4x3();
        document.layers.front().asset.reset();
        const Guid active = document.layers.front().id;
        session.ReplaceDocument(std::move(document).Seal(), active);
        ExecutorSerial executor;
        LayerCopyResult prepared = Prepare(session, executor);
        Expect(Apply(session, prepared) && session.Document()->layers.size() == 2 && session.ActiveLayer() &&
                   !session.ActiveLayer()->asset && session.ActiveLayer()->id != active &&
                   session.ActiveLayer()->name == L"Paint copy" && session.history.UndoName() == L"Duplicate Layer",
               "unselected-blank-layer-duplicates-its-metadata");
    }
    for (const int refusal : {0, 1, 2})
    {
        DocumentSession session;
        DocumentFixture document = CanvasDoc4x3();
        const Guid active = document.layers.front().id;
        if (refusal == 0)
        {
            document.selection = DocumentSelection{false, SelectionLoops{}};
        }
        else if (refusal == 1)
        {
            document.layers.front().isGroup = true;
            document.layers.front().asset.reset();
        }
        else
        {
            document.layers.front().asset.reset();
            document.selection = Rect(1, 1, 2, 2);
        }
        session.ReplaceDocument(std::move(document).Seal(), active);
        const OwnerState before(session);
        ExecutorSerial executor;
        const LayerCopyResult prepared = Prepare(session, executor);
        const char* const names[]{"explicit-empty-selection-refuses-without-mutation", "group-refuses-without-mutation",
                                  "selected-blank-layer-refuses-without-mutation"};
        Expect(prepared.status != LayerCopyStatus::Ready && !prepared.publication && SameOwnerState(session, before),
               names[refusal]);
    }
    {
        DocumentSession session;
        DocumentFixture document = CanvasDoc4x3();
        const Raster sparse = Raster::MakeSparse(10'000, 10'000, RasterFormat::Bgra8888, Raster{}, 0, 0, {}, 0);
        ImageLayer budget = ImageLayer::Blank(L"Image budget", Vec2{1, 1});
        budget.id = Id("61000000-0000-4000-8000-000000000031");
        budget.asset = RasterAsset{sparse, Raster::Make(1, 1, RasterFormat::Bgra8888), L"Sparse image"};
        document.layers.clear();
        document.layers.push_back(std::move(budget));
        const Guid active = document.layers.back().id;
        session.ReplaceDocument(std::move(document).Seal(), active);
        const OwnerState before(session);
        Expect(sparse.IsSparse() && !sparse.IsMaterialized() && session.LayerCopyPreparationMemoryBytes() == 0 &&
                   !session.PrepareLayerCopyInput() && !sparse.IsMaterialized() && SameOwnerState(session, before),
               "100mp-image-budget-refuses-before-materialization-and-preserves-owner");
    }
    {
        DocumentSession session;
        DocumentFixture document = CanvasDoc4x3();
        const Raster sparse = Raster::MakeSparse(10'000, 10'000, RasterFormat::Gray8, Raster{}, 0, 0, {}, 0);
        const Raster tiny = Gray(1, 1, std::array<std::uint8_t, 1>{255});
        document.layers.front().mask = LayerMask{RasterAsset{sparse, tiny, L"Sparse mask"}};
        const Guid active = document.layers.front().id;
        session.ReplaceDocument(std::move(document).Seal(), active);
        const OwnerState before(session);
        Expect(sparse.IsSparse() && !sparse.IsMaterialized() && session.LayerCopyPreparationMemoryBytes() == 0 &&
                   !session.PrepareLayerCopyInput() && !sparse.IsMaterialized() && SameOwnerState(session, before),
               "100mp-mask-budget-refuses-independently-before-materialization-and-preserves-owner");
    }
}

struct CancelAtStage final
{
    LayerCopyStage stage{};
    CancelSource* cancellation = nullptr;
    int hits = 0;
};

void CancelCheckpoint(void* context, LayerCopyStage stage)
{
    auto& action = *static_cast<CancelAtStage*>(context);
    if (stage == action.stage)
    {
        ++action.hits;
        static_cast<void>(action.cancellation->Cancel());
    }
}

void CancellationAndMovedFromInput()
{
    constexpr std::array stages{LayerCopyStage::Pixels, LayerCopyStage::Layer, LayerCopyStage::Document};
    for (const LayerCopyStage stage : stages)
    {
        DocumentSession session = SelectedSession();
        const OwnerState before(session);
        CancelSource cancellation;
        CancelAtStage action{stage, &cancellation};
        const LayerCopyObserver observer{&CancelCheckpoint, &action};
        ExecutorSerial executor;
        LayerCopyResult prepared = Prepare(session, executor, cancellation.Token(), observer);
        Expect(action.hits == 1 && prepared.status == LayerCopyStatus::Cancelled && !prepared.publication &&
                   SameOwnerState(session, before),
               stage == LayerCopyStage::Pixels  ? "cancel-at-pixels-preserves-all-state"
               : stage == LayerCopyStage::Layer ? "cancel-at-layer-preserves-all-state"
                                                : "cancel-at-document-preserves-all-state");
    }
    {
        DocumentSession session = SelectedSession();
        auto input = session.PrepareLayerCopyInput();
        ExecutorSerial executor;
        if (!input)
        {
            Expect(false, "moved-from-fixture-captures-input");
            return;
        }
        LayerCopyInput moved = std::move(input.value());
        LayerCopyResult first = DocumentSession::PrepareLayerCopyPublication(std::move(moved), executor, {}, {});
        LayerCopyResult second = DocumentSession::PrepareLayerCopyPublication(std::move(moved), executor, {}, {});
        Expect(first.status == LayerCopyStatus::Ready && first.publication &&
                   second.status == LayerCopyStatus::Invalid && !second.publication,
               "moved-from-input-is-invalid-and-single-use");
    }
}

void StalePublicationRefusals()
{
    enum class Change
    {
        Generation,
        History,
        MaskTarget,
        Source,
        Selection,
        ReplacedSameID,
    };
    constexpr std::array changes{Change::Generation, Change::History,   Change::MaskTarget,
                                 Change::Source,     Change::Selection, Change::ReplacedSameID};
    constexpr std::array names{
        "stale-generation-refuses-publication",  "stale-history-epoch-refuses-publication",
        "stale-mask-target-refuses-publication", "stale-source-refuses-publication",
        "stale-selection-refuses-publication",   "replaced-same-document-id-refuses-publication"};
    for (std::size_t i = 0; i < changes.size(); ++i)
    {
        DocumentSession session = SelectedSession();
        if (changes[i] == Change::MaskTarget)
        {
            EditLayerRecordsForTest(
                session, [](DocumentFixture& document)
                { document.layers.front().mask = LayerMask{RasterAsset{Gray(2, 3, kMaskGray), {}, L"Mask"}}; });
        }
        ExecutorSerial executor;
        LayerCopyResult prepared = Prepare(session, executor);
        bool changed = prepared.status == LayerCopyStatus::Ready && prepared.publication;
        if (changed)
        {
            switch (changes[i])
            {
            case Change::Generation:
                session.EditForTest([](CanvasDocument&) {});
                break;
            case Change::History:
                session.history.Begin(L"Epoch-only history event", session.Document(), session.ActiveLayerID());
                session.history.End(session.Document(), session.ActiveLayerID());
                break;
            case Change::MaskTarget:
                session.SelectLayerTarget(session.ActiveLayerID().value(), true);
                break;
            case Change::Source:
                EditLayerRecordsForTest(session, [](DocumentFixture& document)
                                        { document.layers.front().visible = !document.layers.front().visible; });
                break;
            case Change::Selection:
                session.SetSelection(Rect(0, 0, 1, 1), L"Changed selection");
                break;
            case Change::ReplacedSameID:
            {
                DocumentFixture replacement = CanvasDoc4x3();
                replacement.layers.front().asset->name = L"Replacement source";
                session.ReplaceDocument(std::move(replacement).Seal(), Id("61000000-0000-4000-8000-000000000002"));
                session.SetSelection(Rect(1, 1, 2, 2), L"Replacement selection");
                break;
            }
            }
        }
        const OwnerState changedState(session);
        const bool applied =
            prepared.publication && session.ApplyPreparedLayerCopyIfCurrent(prepared.publication.value());
        Expect(changed && !applied && SameOwnerState(session, changedState), names[i]);
    }
}

struct ResidencyRefreshFault final
{
    bool reached = false;
};

void FailLayerCopyRefreshAtStorageClone(void* raw, DocumentHistory::PrepareStage stage)
{
    if (stage == DocumentHistory::PrepareStage::StorageIndexClone)
    {
        static_cast<ResidencyRefreshFault*>(raw)->reached = true;
        throw std::bad_alloc{};
    }
}

void PreparedLayerCopyResidencyRefresh()
{
    using Validation = DocumentHistory::ResidencyValidation;
    constexpr std::array<std::uint8_t, 4> leafPixels{81, 82, 83, 255};
    constexpr std::array<std::uint8_t, 8> duplicatePixels{81, 82, 83, 255, 0, 0, 0, 0};
    const Raster leaf = Bgra(1, 1, leafPixels);
    const Raster sparse = Raster::MakeSparse(2, 1, RasterFormat::Bgra8888, Raster{}, 0, 0, {RasterTile{0, 0, leaf}}, 0);
    ImageLayer layer = ImageLayer::Blank(L"Sparse source", Vec2{2, 1});
    layer.id = Id("61000000-0000-4000-8000-000000000041");
    layer.asset = RasterAsset{sparse, {}, L"Sparse source pixels"};
    DocumentFixture fixture;
    fixture.id = Id("61000000-0000-4000-8000-000000000040");
    fixture.width = 2;
    fixture.height = 1;
    fixture.layers.push_back(std::move(layer));
    const Guid layerID = fixture.layers.front().id;
    DocumentSession session;
    session.ReplaceDocument(std::move(fixture).Seal(), layerID);
    for (int edit = 1; edit <= 100; ++edit)
    {
        const std::wstring name = L"Cap edit " + std::to_wstring(edit);
        session.RenameLayer(layerID, name);
    }
    session.history.MarkSaved();
    const Guid savedRevision = session.history.CurrentRevision();
    const OwnerState before(session);
    ExecutorSerial executor;
    LayerCopyResult prepared = Prepare(session, executor);
    DocumentHistory::ResidencyPermit permit;
    const auto initial =
        prepared.publication ? prepared.publication->TryAcquireHistoryResidency(permit) : Validation::Invalid;
    const bool sparseBeforeWorker = sparse.IsSparse() && !sparse.IsMaterialized() && sparse.ByteSize() == 0 &&
                                    leaf.ByteSize() == 4 && initial == Validation::Ready &&
                                    session.history.UndoCount() == 100 &&
                                    session.Document()->layers.front().name == L"Cap edit 100" &&
                                    session.history.CurrentRevision() == savedRevision && !session.history.IsModified();
    permit.Reset();
    const std::uint64_t epoch = Raster::ResidencyEpoch();
    const std::uint8_t* assembled = nullptr;
    std::thread worker([&] { assembled = sparse.Data(); });
    worker.join();
    const bool literalAssembly = assembled && std::equal(duplicatePixels.begin(), duplicatePixels.end(), assembled) &&
                                 sparse.ByteSize() == 8 && leaf.ByteSize() == 4 &&
                                 sparse.ByteSize() + leaf.ByteSize() == 12 && Raster::ResidencyEpoch() == epoch + 1;
    const auto refresh =
        prepared.publication ? prepared.publication->TryAcquireHistoryResidency(permit) : Validation::Invalid;

    CancelSource cancellation;
    (void)cancellation.Cancel();
    const bool cancelledRefresh = prepared.publication &&
                                  !prepared.publication->RefreshHistoryResidency(cancellation.Token()) &&
                                  prepared.publication->TryAcquireHistoryResidency(permit) == Validation::Refresh &&
                                  SameOwnerState(session, before) &&
                                  session.history.CurrentRevision() == savedRevision && !session.history.IsModified();
    ResidencyRefreshFault fault;
    bool allocationPropagated = false;
    if (prepared.publication)
    {
        try
        {
            (void)prepared.publication->RefreshHistoryResidency({}, {&FailLayerCopyRefreshAtStorageClone, &fault});
        }
        catch (const std::bad_alloc&)
        {
            allocationPropagated = true;
        }
    }
    const bool failedRefresh = fault.reached && allocationPropagated && prepared.publication &&
                               prepared.publication->TryAcquireHistoryResidency(permit) == Validation::Refresh &&
                               SameOwnerState(session, before) && session.history.CurrentRevision() == savedRevision &&
                               !session.history.IsModified();
    const bool refreshed = prepared.publication && prepared.publication->RefreshHistoryResidency();
    const auto ready =
        prepared.publication ? prepared.publication->TryAcquireHistoryResidency(permit) : Validation::Invalid;
    const bool originalIntentSurvivedRefresh = refreshed && ready == Validation::Ready &&
                                               SameOwnerState(session, before) && session.history.UndoCount() == 100 &&
                                               session.history.CurrentRevision() == savedRevision &&
                                               !session.history.IsModified();
    Expect(sparseBeforeWorker && literalAssembly && refresh == Validation::Refresh,
           "residency-late-sparse-growth-requests-same-duplicate-refresh-with-literal-bytes");
    Expect(cancelledRefresh && failedRefresh,
           "residency-cancelled-and-storage-clone-failed-refresh-preserve-live-intent");
    Expect(originalIntentSurvivedRefresh, "residency-refresh-preserves-saved-revision-and-entry-cap-trim-decision");

    if (!prepared.publication)
    {
        permit.Reset();
        Expect(false, "residency-moved-from-and-applied-layer-copy-refuse-reuse");
        Expect(false, "residency-publish-undo-redo-exact-duplicate-and-trim-oldest-entry");
        return;
    }
    PreparedLayerCopy moved(std::move(prepared.publication.value()));
    const bool movedFromRefused = prepared.publication->TryAcquireHistoryResidency(permit) == Validation::Invalid &&
                                  !prepared.publication->RefreshHistoryResidency();
    const bool movedRequiresFreshPermit =
        !session.ApplyPreparedLayerCopyIfCurrent(moved) && SameOwnerState(session, before);
    const bool movedReady = moved.TryAcquireHistoryResidency(permit) == Validation::Ready;
    const bool applied = session.ApplyPreparedLayerCopyIfCurrent(moved);
    permit.Reset(); // Publication acknowledgement releases the lease before restore callbacks or queries.
    const ImageLayer* duplicate = session.ActiveLayer();
    const bool appliedRefused = applied && moved.TryAcquireHistoryResidency(permit) == Validation::Invalid &&
                                !moved.RefreshHistoryResidency() && !session.ApplyPreparedLayerCopyIfCurrent(moved);
    const Guid duplicateID = duplicate ? duplicate->id : Guid{};
    const bool duplicatePublished = applied && appliedRefused && duplicate && duplicate->name == L"Cap edit 100 copy" &&
                                    duplicate->asset && duplicate->asset->image == sparse &&
                                    session.history.UndoCount() == 100 &&
                                    session.history.UndoName() == L"Duplicate Layer" && session.history.IsModified();
    session.Undo();
    const bool undoRestoresSavedUserState =
        session.Document() && session.Document()->layers.front().name == L"Cap edit 100" &&
        session.ActiveLayerID() == layerID && session.history.CurrentRevision() == savedRevision &&
        !session.history.IsModified();
    session.Redo();
    duplicate = session.ActiveLayer();
    const bool redoRestoresExactDuplicate = duplicate && duplicate->id == duplicateID && duplicate->asset &&
                                            RasterEquals(duplicate->asset->image, 2, 1, duplicatePixels) &&
                                            session.history.UndoName() == L"Duplicate Layer";
    for (int undo = 0; undo < 100; ++undo)
        session.Undo();
    const bool oldestEntryTrimmed = session.history.UndoCount() == 0 && session.Document() &&
                                    session.Document()->layers.front().name == L"Cap edit 1";
    permit.Reset();
    Expect(movedFromRefused && movedRequiresFreshPermit && movedReady && appliedRefused,
           "residency-moved-from-and-applied-layer-copy-refuse-reuse");
    Expect(duplicatePublished && undoRestoresSavedUserState && redoRestoresExactDuplicate && oldestEntryTrimmed,
           "residency-publish-undo-redo-exact-duplicate-and-trim-oldest-entry");
}
} // namespace

int RunLayerCopyWorkTests()
{
    gFailures = 0;
    SelectedCropPixelsOriginNameAndHistory();
    GroupInsertionAndMaskTargetCopy();
    DuplicatePreservesLayerSemantics();
    RefusalsAndSparseBudgets();
    CancellationAndMovedFromInput();
    StalePublicationRefusals();
    PreparedLayerCopyResidencyRefresh();
    return gFailures;
}
