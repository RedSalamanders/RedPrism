#include "SelectionPreserveTests.h"

#include "../Support/DocumentFixture.h"

#include "BrushStroke.h"
#include "Document.h"
#include "Selection.h"
#include "SelectionCommandWork.h"
#include "WarpStroke.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <new>
#include <optional>
#include <utility>
#include <vector>

namespace
{
int gFailures = 0;
int gAssertions = 0;

void Expect(bool condition, const char* name) noexcept
{
    ++gAssertions;
    std::printf("%s selection-preserve.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

const char* KindSuffix(SelectionCommandKind kind) noexcept
{
    return kind == SelectionCommandKind::SelectAll ? "select-all" : "deselect";
}

void ExpectKind(bool condition, const char* name, SelectionCommandKind kind) noexcept
{
    char label[128]{};
    std::snprintf(label, sizeof(label), "%s.%s", name, KindSuffix(kind));
    Expect(condition, label);
}

void ExpectDetail(bool condition, const char* name, const char* detail) noexcept
{
    char label[128]{};
    std::snprintf(label, sizeof(label), "%s.%s", name, detail);
    Expect(condition, label);
}

DocumentSession Session()
{
    DocumentSession session;
    DocumentFixture document;
    document.id = Guid::Generate();
    document.width = 4;
    document.height = 4;
    constexpr std::array<std::uint8_t, 64> pixels{
        0,   0,   0,   255, 32, 32,  32,  255, 64,  64,  64, 255, 96,  96,  96,  255, 16,  16,  16,  255, 48, 48,
        48,  255, 80,  80,  80, 255, 112, 112, 112, 255, 24, 24,  24,  255, 56,  56,  56,  255, 88,  88,  88, 255,
        120, 120, 120, 255, 40, 40,  40,  255, 72,  72,  72, 255, 104, 104, 104, 255, 136, 136, 136, 255};
    constexpr std::array<std::uint8_t, 16> maskBytes{0, 32, 64, 96, 16, 48, 80, 112, 24, 56, 88, 120, 40, 72, 104, 136};
    const Raster image = Raster::Make(4, 4, RasterFormat::Bgra8888, pixels.data(), pixels.size());
    const Raster mask = Raster::Make(4, 4, RasterFormat::Gray8, maskBytes.data(), maskBytes.size());
    ImageLayer source = ImageLayer::Blank(L"Independent 4x4 source", Vec2{4, 4});
    const Guid active = source.id;
    source.asset = RasterAsset{image, {}, source.name};
    source.mask = LayerMask{RasterAsset{mask, mask, L"Independent 4x4 mask"}, true, std::nullopt, true};
    document.layers.push_back(std::move(source));
    document.layers.push_back(ImageLayer::Blank(L"Independent target", Vec2{4, 4}));
    session.ReplaceDocument(std::move(document).Seal(), active);
    session.SetSelection(DocumentSelection{false, {SelectionLoop{Vec2{1, 1}, Vec2{3, 1}, Vec2{3, 3}, Vec2{1, 3}}}},
                         L"Literal 2x2 selection");
    return session;
}

bool Full4x4(const std::optional<CanvasDocument>& document) noexcept
{
    if (!document || !document->selection || !document->selection->antialiased ||
        document->selection->loops.size() != 1 || document->selection->loops.Get().front().size() != 4)
        return false;
    const auto& loop = document->selection->loops.Get().front();
    return loop[0] == Vec2{0, 0} && loop[1] == Vec2{4, 0} && loop[2] == Vec2{4, 4} && loop[3] == Vec2{0, 4};
}

bool Literal2x2(const std::optional<DocumentSelection>& selection) noexcept
{
    if (!selection || selection->antialiased || selection->loops.size() != 1 ||
        selection->loops.Get().front().size() != 4)
        return false;
    const auto& loop = selection->loops.Get().front();
    return loop[0] == Vec2{1, 1} && loop[1] == Vec2{3, 1} && loop[2] == Vec2{3, 3} && loop[3] == Vec2{1, 3};
}

std::vector<std::uint8_t> BrushCoverage(const BrushStroke* brush)
{
    std::vector<std::uint8_t> bytes;
    if (!brush)
        return bytes;
    for (const auto& tile : brush->Tiles())
    {
        for (std::size_t shift = 0; shift != sizeof(tile.key); ++shift)
            bytes.push_back(static_cast<std::uint8_t>(tile.key >> (shift * 8)));
        bytes.insert(bytes.end(), tile.coverage.begin(), tile.coverage.end());
    }
    return bytes;
}

template <typename T> bool SameOwner(const std::weak_ptr<T>& left, const std::weak_ptr<T>& right) noexcept
{
    return !left.owner_before(right) && !right.owner_before(left);
}

std::optional<PreparedSelectionCommand> Prepare(DocumentSession& session, SelectionCommandKind kind)
{
    auto input = session.PreparePreservingSelectionCommandInput(kind);
    if (!input)
        return std::nullopt;
    return DocumentSession::PrepareSelectionCommandPublication(std::move(*input));
}

void BrushPreservation()
{
    for (const auto kind : {SelectionCommandKind::SelectAll, SelectionCommandKind::Deselect})
    {
        DocumentSession session = Session();
        session.brushSettings.diameter = 2;
        session.brushSettings.hardness = 1;
        ExpectKind(session.BeginBrush(Vec2{1, 1}), "brush.fixture-reached", kind);
        const auto live = session.WeakPendingBrushForTest();
        const auto coverage = BrushCoverage(session.PendingBrush());
        ExpectKind(!live.expired() && !coverage.empty(), "brush.opening-and-coverage-captured", kind);
        ExpectKind(!session.PrepareSelectionCommandInput(kind), "brush.settled-api-refuses-live-brush", kind);
        auto prepared = Prepare(session, kind);
        ExpectKind(prepared && session.IsPreparedSelectionCommandCurrent(*prepared), "brush.prepared", kind);
        if (!prepared)
            continue;
        session.ContinueBrush(Vec2{2, 1});
        ExpectKind(session.IsPreparedSelectionCommandCurrent(*prepared), "brush.append-does-not-stale", kind);
        const auto continued = BrushCoverage(session.PendingBrush());
        ExpectKind(SameOwner(live, session.WeakPendingBrushForTest()) && continued != coverage,
                   "brush.append-keeps-identity-and-changes-coverage", kind);
        const auto beforePublication = BrushCoverage(session.PendingBrush());
        ExpectKind(session.ApplyPreparedSelectionCommandIfCurrent(*prepared), "brush.publication-succeeds", kind);
        ExpectKind(SameOwner(live, session.WeakPendingBrushForTest()) &&
                       BrushCoverage(session.PendingBrush()) == beforePublication,
                   "brush.publication-preserves-identity-and-exact-coverage-bytes", kind);
        Expect(kind == SelectionCommandKind::SelectAll ? Full4x4(session.Document()) : !session.Selection(),
               kind == SelectionCommandKind::SelectAll ? "brush.literal-full-clockwise-quad-aa-true"
                                                       : "brush.literal-nullopt-after-deselect");
        ExpectKind(session.history.UndoName() ==
                       (kind == SelectionCommandKind::SelectAll ? L"Select All" : L"Deselect"),
                   "brush.command-title", kind);
        ExpectKind(!session.ApplyPreparedSelectionCommandIfCurrent(*prepared), "brush.single-use", kind);
    }

    DocumentSession cancelled = Session();
    cancelled.brushSettings.diameter = 2;
    cancelled.brushSettings.hardness = 1;
    Expect(cancelled.BeginBrush(Vec2{1, 1}), "brush-cancel-fixture-reached");
    auto old = Prepare(cancelled, SelectionCommandKind::SelectAll);
    const auto cancelledIdentity = cancelled.WeakPendingBrushForTest();
    cancelled.CancelBrush();
    Expect(cancelledIdentity.expired() && old && !cancelled.IsPreparedSelectionCommandCurrent(*old) &&
               !cancelled.ApplyPreparedSelectionCommandIfCurrent(*old),
           "brush-weak-authority-does-not-retain-cancelled-stroke-and-stales");
    Expect(cancelled.BeginBrush(Vec2{1, 1}), "brush-replacement-fixture-reached");
    auto replacement = cancelled.WeakPendingBrushForTest();
    Expect(old && !cancelled.IsPreparedSelectionCommandCurrent(*old) && !SameOwner(cancelledIdentity, replacement),
           "brush-replacement-retains-distinct-live-control-block");
}

void CommittedBrushClip()
{
    for (const auto kind : {SelectionCommandKind::SelectAll, SelectionCommandKind::Deselect})
    {
        DocumentSession session = Session();
        std::array<std::uint8_t, 64> before{};
        session.ActiveLayer()->asset->image.ReadRegion(0, 0, 4, 4, before.data(), 16, 0);
        session.brushSettings.diameter = 1;
        session.brushSettings.hardness = 1;
        session.brushSettings.red = session.brushSettings.green = session.brushSettings.blue = 1;
        const bool opened = session.BeginBrush(Vec2{1.5, 1.5});
        ExpectKind(opened && session.PendingBrush(), "clip.fixture-live-brush", kind);
        if (!opened)
            continue;
        auto prepared = Prepare(session, kind);
        ExpectKind(prepared && session.ApplyPreparedSelectionCommandIfCurrent(prepared.value()),
                   "clip.selection-publication", kind);
        if (!prepared)
            continue;
        // This point is covered by the later selection, but was outside the literal 2x2 opening clip.
        session.ContinueBrush(Vec2{0.5, 0.5});
        session.FinishBrush();
        std::array<std::uint8_t, 64> after{};
        session.ActiveLayer()->asset->image.ReadRegion(0, 0, 4, 4, after.data(), 16, 0);
        bool outsideUnchanged = true;
        for (int y = 0; y != 4; ++y)
            for (int x = 0; x != 4; ++x)
                if (x < 1 || x >= 3 || y < 1 || y >= 3)
                    for (int c = 0; c != 4; ++c)
                        outsideUnchanged &= after[(y * 4 + x) * 4 + c] == before[(y * 4 + x) * 4 + c];
        ExpectKind(!session.PendingBrush() && session.history.UndoName() == L"Brush Stroke",
                   "clip.commit-reached-with-original-brush-title", kind);
        ExpectKind(after[20] == 255 && after[21] == 255 && after[22] == 255 && after[23] == 255,
                   "clip.literal-white-center-is-painted", kind);
        ExpectKind(outsideUnchanged, "clip.all-originally-unselected-pixels-remain-literal-source", kind);
        ExpectKind(kind == SelectionCommandKind::SelectAll ? Full4x4(session.Document()) : !session.Selection(),
                   "clip.commit-preserves-published-selection", kind);
        session.Undo();
        std::array<std::uint8_t, 64> restored{};
        session.ActiveLayer()->asset->image.ReadRegion(0, 0, 4, 4, restored.data(), 16, 0);
        ExpectKind(restored == before &&
                       (kind == SelectionCommandKind::SelectAll ? Full4x4(session.Document()) : !session.Selection()),
                   "clip.undo-brush-restores-literal-pixels-and-keeps-command-selection", kind);
        session.Undo();
        ExpectKind(Literal2x2(session.Selection()), "clip.undo-command-restores-original-outline-and-aa", kind);
    }
}

void WarpPreservation()
{
    for (const auto kind : {SelectionCommandKind::SelectAll, SelectionCommandKind::Deselect})
    {
        DocumentSession session = Session();
        ExpectKind(session.BeginWarp(Vec2{1, 1}), "warp.fixture-reached", kind);
        if (!session.PendingWarp())
            continue;
        session.ContinueWarp(Vec2{2, 1});
        const auto live = session.WeakPendingWarpForTest();
        const auto points = session.PendingWarp()->Points();
        ExpectKind(!live.expired() && !points.empty(), "warp.opening-and-samples-captured", kind);
        ExpectKind(!session.PrepareSelectionCommandInput(kind), "warp.settled-api-refuses-live-warp", kind);
        auto prepared = Prepare(session, kind);
        ExpectKind(prepared && session.IsPreparedSelectionCommandCurrent(*prepared), "warp.prepared", kind);
        if (!prepared)
            continue;
        session.ContinueWarp(Vec2{3, 1});
        ExpectKind(session.IsPreparedSelectionCommandCurrent(*prepared), "warp.append-does-not-stale", kind);
        const auto continued = session.PendingWarp()->Points();
        ExpectKind(SameOwner(live, session.WeakPendingWarpForTest()) && continued.size() > points.size(),
                   "warp.append-keeps-identity-and-adds-sample", kind);
        std::array<std::uint8_t, 64> image{};
        session.PendingWarp()->Image().ReadRegion(0, 0, 4, 4, image.data(), 16, 0);
        ExpectKind(session.ApplyPreparedSelectionCommandIfCurrent(*prepared), "warp.publication-succeeds", kind);
        std::array<std::uint8_t, 64> publishedImage{};
        session.PendingWarp()->Image().ReadRegion(0, 0, 4, 4, publishedImage.data(), 16, 0);
        ExpectKind(SameOwner(live, session.WeakPendingWarpForTest()) && session.PendingWarp()->Points() == continued &&
                       publishedImage == image,
                   "warp.publication-preserves-identity-samples-and-image", kind);
        Expect(kind == SelectionCommandKind::SelectAll ? Full4x4(session.Document()) : !session.Selection(),
               kind == SelectionCommandKind::SelectAll ? "warp.literal-full-clockwise-quad-aa-true"
                                                       : "warp.literal-nullopt-after-deselect");
        ExpectKind(session.history.UndoName() ==
                       (kind == SelectionCommandKind::SelectAll ? L"Select All" : L"Deselect"),
                   "warp.command-title", kind);
    }

    DocumentSession session = Session();
    Expect(session.BeginWarp(Vec2{1, 1}), "warp-replacement-fixture-reached");
    if (!session.PendingWarp())
        return;
    auto old = Prepare(session, SelectionCommandKind::Deselect);
    const auto cancelledIdentity = session.WeakPendingWarpForTest();
    session.CancelWarp();
    Expect(cancelledIdentity.expired() && old && !session.IsPreparedSelectionCommandCurrent(*old) &&
               !session.ApplyPreparedSelectionCommandIfCurrent(*old),
           "warp-weak-authority-does-not-retain-cancelled-stroke-and-stales");
    Expect(session.BeginWarp(Vec2{1, 1}), "warp-replacement-opens");
    const auto replacement = session.WeakPendingWarpForTest();
    Expect(old && !session.IsPreparedSelectionCommandCurrent(*old) && !replacement.expired() &&
               !SameOwner(cancelledIdentity, replacement),
           "warp-replacement-is-current-but-old-command-stays-stale");
}

void ShapePreservationAndAba()
{
    for (const auto kind : {SelectionCommandKind::SelectAll, SelectionCommandKind::Deselect})
    {
        DocumentSession session = Session();
        session.BeginShape(Vec2{0, 0});
        session.DragShape(Vec2{2, 2}, false, false);
        const auto original = session.PendingShape();
        ExpectKind(original.has_value(), "shape.fixture-reached", kind);
        ExpectKind(!session.PrepareSelectionCommandInput(kind), "shape.settled-api-refuses-live-draft", kind);
        auto prepared = Prepare(session, kind);
        ExpectKind(prepared && session.IsPreparedSelectionCommandCurrent(*prepared), "shape.prepared", kind);
        if (!prepared)
            continue;
        session.DragShape(Vec2{3, 2}, false, false);
        ExpectKind(session.IsPreparedSelectionCommandCurrent(*prepared) && session.PendingShape() != original,
                   "shape.drag-continuation-keeps-opening-current", kind);
        const auto continued = session.PendingShape();
        ExpectKind(session.ApplyPreparedSelectionCommandIfCurrent(*prepared) && session.PendingShape() == continued,
                   "shape.publication-preserves-live-draft", kind);
        Expect(kind == SelectionCommandKind::SelectAll ? Full4x4(session.Document()) : !session.Selection(),
               kind == SelectionCommandKind::SelectAll ? "shape.literal-full-clockwise-quad-aa-true"
                                                       : "shape.literal-nullopt-after-deselect");
        ExpectKind(session.history.UndoName() ==
                       (kind == SelectionCommandKind::SelectAll ? L"Select All" : L"Deselect"),
                   "shape.command-title", kind);
    }

    DocumentSession aba = Session();
    aba.BeginShape(Vec2{0, 0});
    aba.DragShape(Vec2{2, 2}, false, false);
    const auto sameGeometry = aba.PendingShape();
    auto prepared = Prepare(aba, SelectionCommandKind::SelectAll);
    aba.CancelShape();
    aba.BeginShape(Vec2{0, 0});
    aba.DragShape(Vec2{2, 2}, false, false);
    Expect(sameGeometry == aba.PendingShape(), "shape-aba-replacement-has-identical-geometry");
    Expect(prepared && !aba.IsPreparedSelectionCommandCurrent(*prepared) &&
               !aba.ApplyPreparedSelectionCommandIfCurrent(*prepared),
           "shape-opening-serial-rejects-cancel-rebegin-aba");
}

void OtherDraftsStillRefuse()
{
    constexpr std::array<const char*, 7> names{"color", "gradient",     "transform", "pixel-move",
                                               "lasso", "outline-move", "marquee"};
    for (int draft = 0; draft != 7; ++draft)
    {
        DocumentSession session = Session();
        bool reached = false;
        switch (draft)
        {
        case 0:
            session.BeginColorEdit(FilterKind::Levels);
            reached = session.HasColorEdit();
            break;
        case 1:
            static_cast<void>(session.BeginGradient(Vec2{0, 0}));
            reached = session.PendingGradient() != nullptr;
            break;
        case 2:
            session.BeginTransform();
            reached = session.PendingTransformEdit().has_value();
            break;
        case 3:
            reached = session.BeginPixelMove() && session.PendingPixelMove() != nullptr;
            break;
        case 4:
            session.BeginLasso(Vec2{0, 0}, SelectionMode::New);
            reached = session.PendingLasso().has_value();
            break;
        case 5:
            reached = session.BeginSelectionMove();
            break;
        case 6:
            session.BeginMarquee(Vec2{0, 0}, SelectionMode::New);
            reached = session.PendingLasso().has_value();
            break;
        }
        ExpectDetail(reached, "refusal-fixture", names[static_cast<std::size_t>(draft)]);
        ExpectDetail(!session.PreparePreservingSelectionCommandInput(SelectionCommandKind::SelectAll) &&
                         !session.PreparePreservingSelectionCommandInput(SelectionCommandKind::Deselect),
                     "refusal", names[static_cast<std::size_t>(draft)]);
    }
}

void StalenessAndOwnership()
{
    using Mutate = void (*)(DocumentSession&);
    const std::array<std::pair<const char*, Mutate>, 5> stale{
        std::pair{"document-lifetime",
                  +[](DocumentSession& s) { s.ReplaceDocument(*s.Document(), s.ActiveLayerID()); }},
        std::pair{"document-generation",
                  +[](DocumentSession& s) { s.EditForTest([](CanvasDocument& d) { d.resolution = 144; }); }},
        std::pair{"active-target", +[](DocumentSession& s) { s.SelectLayer(s.Document()->layers.back().id); }},
        std::pair{"crop", +[](DocumentSession& s) { s.SetCropFrame(CropRect{0, 0, 3, 3}); }},
        std::pair{"history-epoch", +[](DocumentSession& s) { s.BeginEdit(L"Changed history epoch"); }}};
    for (const auto& [name, mutate] : stale)
    {
        DocumentSession session = Session();
        auto prepared = Prepare(session, SelectionCommandKind::SelectAll);
        ExpectDetail(prepared && session.IsPreparedSelectionCommandCurrent(*prepared), "stale-fixture.prepared", name);
        mutate(session);
        Expect(prepared && !session.IsPreparedSelectionCommandCurrent(*prepared) &&
                   !session.ApplyPreparedSelectionCommandIfCurrent(*prepared),
               name);
    }

    DocumentSession maskSession = Session();
    auto maskPrepared = Prepare(maskSession, SelectionCommandKind::SelectAll);
    const auto generation = maskSession.Generation();
    const auto history = maskSession.history.CaptureState();
    const auto active = maskSession.ActiveLayerID();
    Expect(maskSession.Document() && maskSession.Document()->layers.front().mask.has_value(),
           "stale-mask.fixture-has-literal-mask");
    if (active)
        maskSession.SelectLayerTarget(*active, true);
    Expect(maskSession.IsMaskSelected() && maskSession.Generation() == generation &&
               maskSession.history.IsCurrent(history),
           "stale-mask.target-change-is-reached-without-generation-or-history-change");
    Expect(maskPrepared && !maskSession.IsPreparedSelectionCommandCurrent(*maskPrepared) &&
               !maskSession.ApplyPreparedSelectionCommandIfCurrent(*maskPrepared),
           "stale-mask.prepared-command-refuses-mask-target-change");

    DocumentSession owner = Session();
    auto prepared = Prepare(owner, SelectionCommandKind::SelectAll);
    DocumentSession foreign = Session();
    DocumentSession copied = owner;
    Expect(prepared && !foreign.IsPreparedSelectionCommandCurrent(*prepared) &&
               !foreign.ApplyPreparedSelectionCommandIfCurrent(*prepared),
           "foreign-owner-refused");
    Expect(prepared && !copied.IsPreparedSelectionCommandCurrent(*prepared) &&
               !copied.ApplyPreparedSelectionCommandIfCurrent(*prepared) &&
               owner.IsPreparedSelectionCommandCurrent(*prepared),
           "copied-owner-refused-original-remains-current");
    if (prepared)
    {
        PreparedSelectionCommand moved(std::move(*prepared));
        Expect(!owner.IsPreparedSelectionCommandCurrent(*prepared) &&
                   !owner.ApplyPreparedSelectionCommandIfCurrent(*prepared) &&
                   owner.IsPreparedSelectionCommandCurrent(moved),
               "moved-from-prepared-command-refused-destination-retains-authority");
        Expect(owner.ApplyPreparedSelectionCommandIfCurrent(moved) && !owner.IsPreparedSelectionCommandCurrent(moved) &&
                   !owner.ApplyPreparedSelectionCommandIfCurrent(moved),
               "moved-prepared-publication-is-single-use");
    }
}

void HistoryNoopNestedAndDirectOracle()
{
    {
        DocumentSession session = Session();
        session.BeginEdit(L"Outer gesture command");
        const auto depth = session.history.CaptureState().Depth();
        const auto priorUndoName = session.history.UndoName();
        auto prepared = Prepare(session, SelectionCommandKind::SelectAll);
        Expect(prepared && session.ApplyPreparedSelectionCommandIfCurrent(*prepared) &&
                   session.history.CaptureState().Depth() == depth && session.history.UndoName() == priorUndoName,
               "nested-publication-retains-depth-and-title");
        session.EndEdit();
        Expect(session.history.UndoName() == L"Outer gesture command", "nested-end-keeps-outer-title");
        session.Undo();
        Expect(Literal2x2(session.Selection()), "nested-undo-restores-literal-selection");
    }
    {
        DocumentSession session = Session();
        auto prepared = Prepare(session, SelectionCommandKind::SelectAll);
        Expect(prepared && session.ApplyPreparedSelectionCommandIfCurrent(*prepared) &&
                   session.history.UndoName() == L"Select All",
               "changed-command-records-exact-undo-title");
        session.Undo();
        Expect(Literal2x2(session.Selection()) && session.history.RedoName() == L"Select All",
               "changed-command-undo-restores-literal-selection-and-redo-title");
        session.Redo();
        Expect(Full4x4(session.Document()) && session.history.UndoName() == L"Select All",
               "changed-command-redo-restores-full-aa-selection");
    }
    {
        DocumentSession session = Session();
        session.Deselect();
        session.SelectAll();
        session.Undo();
        Expect(!session.Selection(), "no-op-redo-fixture-restores-no-selection");
        const auto redoName = session.history.RedoName();
        auto prepared = Prepare(session, SelectionCommandKind::Deselect);
        Expect(prepared && !prepared->ChangesSelection() && session.ApplyPreparedSelectionCommandIfCurrent(*prepared) &&
                   session.history.CanRedo() && session.history.RedoName() == redoName,
               "no-op-retains-redo-entry");
        session.Redo();
        Expect(Full4x4(session.Document()), "retained-redo-restores-literal-full-selection");
    }
    for (const auto kind : {SelectionCommandKind::SelectAll, SelectionCommandKind::Deselect})
    {
        DocumentSession preparedPath = Session();
        DocumentSession directPath = Session();
        preparedPath.brushSettings.diameter = directPath.brushSettings.diameter = 2;
        preparedPath.brushSettings.hardness = directPath.brushSettings.hardness = 1;
        const bool preparedBrush = preparedPath.BeginBrush(Vec2{1, 1});
        const bool directBrush = directPath.BeginBrush(Vec2{1, 1});
        ExpectKind(preparedBrush && directBrush, "direct-oracle.live-brush-fixture-reached", kind);
        const auto preparedIdentity = preparedPath.WeakPendingBrushForTest();
        const auto directIdentity = directPath.WeakPendingBrushForTest();
        auto prepared = Prepare(preparedPath, kind);
        ExpectKind(prepared && preparedPath.ApplyPreparedSelectionCommandIfCurrent(*prepared),
                   "direct-oracle.prepared-path-applies", kind);
        if (kind == SelectionCommandKind::SelectAll)
            directPath.SelectAll();
        else
            directPath.Deselect();
        ExpectKind(preparedPath.Selection() == directPath.Selection() &&
                       preparedPath.history.UndoName() == directPath.history.UndoName(),
                   "direct-oracle.selection-and-title-match", kind);
        ExpectKind(preparedBrush && directBrush &&
                       SameOwner(preparedIdentity, preparedPath.WeakPendingBrushForTest()) &&
                       SameOwner(directIdentity, directPath.WeakPendingBrushForTest()) &&
                       BrushCoverage(preparedPath.PendingBrush()) == BrushCoverage(directPath.PendingBrush()),
                   "direct-oracle.exact-coverage-and-identity-match", kind);
    }
}

struct CancelAtStage final
{
    CancelSource* cancellation = nullptr;
    DocumentHistory::PrepareStage target{};
    int hits = 0;
};

void CancelCheckpoint(void* opaque, DocumentHistory::PrepareStage stage)
{
    auto& state = *static_cast<CancelAtStage*>(opaque);
    if (stage == state.target)
    {
        ++state.hits;
        static_cast<void>(state.cancellation->Cancel());
    }
}

struct ThrowAtEntry final
{
    int hits = 0;
};

void ThrowCheckpoint(void* opaque, DocumentHistory::PrepareStage stage)
{
    if (stage == DocumentHistory::PrepareStage::Entry)
    {
        ++static_cast<ThrowAtEntry*>(opaque)->hits;
        throw std::bad_alloc{};
    }
}

void ReachedFailureAndRecovery()
{
    {
        DocumentSession session = Session();
        Expect(session.BeginBrush(Vec2{1, 1}), "cancel-prepare-live-brush-reached");
        CancelSource cancellation;
        CancelAtStage injected{&cancellation, DocumentHistory::PrepareStage::Entry};
        const DocumentHistory::PrepareObserver observer{&CancelCheckpoint, &injected};
        auto input = session.PreparePreservingSelectionCommandInput(SelectionCommandKind::SelectAll);
        auto cancelled = input ? DocumentSession::PrepareSelectionCommandPublication(std::move(*input),
                                                                                     cancellation.Token(), observer)
                               : std::nullopt;
        Expect(injected.hits == 1 && cancellation.Token().IsCancelled() && !cancelled && session.PendingBrush() &&
                   session.history.UndoName() == L"Literal 2x2 selection",
               "history-prepare-cancellation-is-reached-and-keeps-live-state");
        auto recovered = Prepare(session, SelectionCommandKind::SelectAll);
        Expect(recovered && session.ApplyPreparedSelectionCommandIfCurrent(*recovered) && Full4x4(session.Document()) &&
                   session.PendingBrush(),
               "fresh-preparation-recovers-after-cancelled-history-prepare");
    }
    {
        DocumentSession session = Session();
        Expect(session.BeginWarp(Vec2{1, 1}), "badalloc-prepare-live-warp-reached");
        ThrowAtEntry injected;
        const DocumentHistory::PrepareObserver observer{&ThrowCheckpoint, &injected};
        auto input = session.PreparePreservingSelectionCommandInput(SelectionCommandKind::Deselect);
        bool caught = false;
        std::optional<PreparedSelectionCommand> failed;
        try
        {
            if (input)
                failed = DocumentSession::PrepareSelectionCommandPublication(std::move(*input), {}, observer);
        }
        catch (const std::bad_alloc&)
        {
            caught = true;
        }
        Expect(injected.hits == 1 && caught && !failed && session.PendingWarp() && Literal2x2(session.Selection()),
               "history-entry-badalloc-is-reached-and-owner-recovers");
        auto recovered = Prepare(session, SelectionCommandKind::Deselect);
        Expect(recovered && session.ApplyPreparedSelectionCommandIfCurrent(*recovered) && !session.Selection() &&
                   session.PendingWarp(),
               "fresh-preparation-recovers-after-badalloc");
    }
}
} // namespace

int RunSelectionPreserveTests()
{
    gFailures = 0;
    gAssertions = 0;
    BrushPreservation();
    CommittedBrushClip();
    WarpPreservation();
    ShapePreservationAndAba();
    OtherDraftsStillRefuse();
    StalenessAndOwnership();
    HistoryNoopNestedAndDirectOracle();
    ReachedFailureAndRecovery();
    std::printf("%d selection-preserve assertions; %d failed.\n", gAssertions, gFailures);
    return gFailures;
}
