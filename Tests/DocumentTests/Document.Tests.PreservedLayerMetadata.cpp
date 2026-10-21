#include "Document.Tests.PreservedLayerMetadata.h"

#include "../Support/Support.Tests.DocumentFixture.h"
#include "Document.h"
#include "LayerMetadataWork.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace
{
int gFailures = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s preserved-metadata.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

constexpr Guid kDocumentId{{0x75, 0, 0, 1, 0, 0, 0x40, 0, 0x80, 0, 0, 0, 0, 0, 0, 1}};
constexpr Guid kTargetId{{0x75, 0, 0, 2, 0, 0, 0x40, 0, 0x80, 0, 0, 0, 0, 0, 0, 2}};
constexpr Guid kSiblingId{{0x75, 0, 0, 3, 0, 0, 0x40, 0, 0x80, 0, 0, 0, 0, 0, 0, 3}};

// Small independent fixture: metadata must retain this exact image, mask, and selection graph.
CanvasDocument MakeFixtureDocument()
{
    constexpr std::array<std::uint8_t, 16> pixels{10, 20, 30, 255, 40,  50,  60,  255,
                                                  70, 80, 90, 255, 100, 110, 120, 255};
    constexpr std::array<std::uint8_t, 4> maskPixels{25, 75, 125, 200};
    const Raster raster = Raster::Make(2, 2, RasterFormat::Bgra8888, pixels.data(), pixels.size());
    const Raster maskRaster = Raster::Make(2, 2, RasterFormat::Gray8, maskPixels.data(), maskPixels.size());
    ImageLayer target = ImageLayer::Blank(L"Original", Vec2{2, 2});
    target.id = kTargetId;
    target.asset = RasterAsset{raster, Raster::MakeThumbnail(raster), L"Original pixels"};
    target.mask = LayerMask{RasterAsset{maskRaster, Raster::MakeThumbnail(maskRaster), L"Original mask"}};
    target.opacity = 0.75;
    target.blendMode = LayerBlendMode::Multiply;
    ImageLayer sibling = ImageLayer::Blank(L"Sibling", Vec2{2, 2});
    sibling.id = kSiblingId;
    DocumentFixture fixture;
    fixture.id = kDocumentId;
    fixture.width = 2;
    fixture.height = 2;
    fixture.layers = {target, sibling};
    fixture.selection = DocumentSelection{false, SelectionLoops{{Vec2{0, 0}, Vec2{2, 0}, Vec2{2, 2}, Vec2{0, 2}}}};
    return std::move(fixture).Seal();
}

DocumentSession MakeSession()
{
    DocumentSession session;
    session.ReplaceDocument(MakeFixtureDocument(), kTargetId);
    return session;
}

enum class Gesture : std::uint8_t
{
    Brush,
    Warp,
    Shape,
    Lasso,
};

bool BeginGesture(DocumentSession& session, Gesture gesture)
{
    switch (gesture)
    {
    case Gesture::Brush:
        return session.BeginBrush(Vec2{0.5, 0.5});
    case Gesture::Warp:
        return session.BeginWarp(Vec2{0.5, 0.5});
    case Gesture::Shape:
        session.BeginShape(Vec2{0, 0});
        session.DragShape(Vec2{1, 1}, false, false);
        return session.PendingShape().has_value();
    case Gesture::Lasso:
        session.BeginLasso(Vec2{0, 0}, SelectionMode::New);
        session.ExtendLasso(Vec2{1, 0});
        return session.PendingLasso().has_value();
    }
    return false;
}

void MoveGesture(DocumentSession& session, Gesture gesture)
{
    switch (gesture)
    {
    case Gesture::Brush:
        session.ContinueBrush(Vec2{1.25, 1.25});
        break;
    case Gesture::Warp:
        session.ContinueWarp(Vec2{1.25, 1.25});
        break;
    case Gesture::Shape:
        session.DragShape(Vec2{2, 2}, false, false);
        break;
    case Gesture::Lasso:
        session.ExtendLasso(Vec2{1, 1});
        break;
    }
}

void CancelGesture(DocumentSession& session, Gesture gesture) noexcept
{
    switch (gesture)
    {
    case Gesture::Brush:
        session.CancelBrush();
        break;
    case Gesture::Warp:
        session.CancelWarp();
        break;
    case Gesture::Shape:
        session.CancelShape();
        break;
    case Gesture::Lasso:
        session.CancelLasso();
        break;
    }
}

bool GestureActive(const DocumentSession& session, Gesture gesture) noexcept
{
    switch (gesture)
    {
    case Gesture::Brush:
        return session.PendingBrush() != nullptr;
    case Gesture::Warp:
        return session.PendingWarp() != nullptr;
    case Gesture::Shape:
        return session.PendingShape().has_value();
    case Gesture::Lasso:
        return session.PendingLasso().has_value();
    }
    return false;
}

std::optional<LayerMetadataInput> Input(DocumentSession& session, LayerMetadataKind kind, std::wstring_view name = {},
                                        double opacity = 1.0, LayerBlendMode blendMode = LayerBlendMode::Normal)
{
    return session.PreparePreservedLayerMetadataInput(kind, kTargetId, name, opacity, blendMode);
}

std::optional<PreparedLayerMetadata> Prepare(DocumentSession& session, LayerMetadataKind kind,
                                             std::wstring_view name = {}, double opacity = 1.0,
                                             LayerBlendMode blendMode = LayerBlendMode::Normal,
                                             CancelToken cancellation = {}, LayerMetadataPrepareObserver observer = {})
{
    auto input = Input(session, kind, name, opacity, blendMode);
    if (!input.has_value())
        return std::nullopt;
    return PrepareLayerMetadataPublication(std::move(input.value()), cancellation, {}, observer);
}

bool SameCapturedPayload(const CanvasDocument& actual, const CanvasDocument& expected) noexcept
{
    if (actual != expected || actual.layers.size() != 2 || !actual.selection.has_value() ||
        !expected.selection.has_value())
        return false;
    return actual.layers[0].asset.has_value() && expected.layers[0].asset.has_value() &&
           actual.layers[0].mask.has_value() && expected.layers[0].mask.has_value() &&
           actual.layers[0].asset.value().image.Identity() == expected.layers[0].asset.value().image.Identity() &&
           actual.layers[0].mask.value().asset.image.Identity() ==
               expected.layers[0].mask.value().asset.image.Identity() &&
           actual.selection.value().loops.Identity() == expected.selection.value().loops.Identity();
}

struct OwnerState final
{
    std::optional<CanvasDocument> document;
    std::uint64_t generation = 0;
    Guid revision;
    int undoCount = 0;
    std::size_t redoCount = 0;
    std::wstring undoName;
    std::wstring redoName;
    std::size_t depth = 0;

    explicit OwnerState(const DocumentSession& session)
        : document(session.Document()), generation(session.Generation()), revision(session.history.CurrentRevision()),
          undoCount(session.history.UndoCount()), redoCount(session.history.CaptureState().FutureCount()),
          undoName(session.history.UndoName()), redoName(session.history.RedoName()),
          depth(session.history.CaptureState().Depth())
    {
    }
};

bool SameOwnerState(const DocumentSession& session, const OwnerState& before)
{
    const auto history = session.history.CaptureState();
    return session.Document() == before.document && session.Generation() == before.generation &&
           session.history.CurrentRevision() == before.revision && session.history.UndoCount() == before.undoCount &&
           history.FutureCount() == before.redoCount && session.history.UndoName() == before.undoName &&
           session.history.RedoName() == before.redoName && history.Depth() == before.depth;
}

const char* GestureName(Gesture gesture) noexcept
{
    switch (gesture)
    {
    case Gesture::Brush:
        return "brush";
    case Gesture::Warp:
        return "warp";
    case Gesture::Shape:
        return "shape";
    case Gesture::Lasso:
        return "lasso";
    }
    return "unknown";
}

const char* KindName(LayerMetadataKind kind) noexcept
{
    switch (kind)
    {
    case LayerMetadataKind::Rename:
        return "rename";
    case LayerMetadataKind::ToggleVisibility:
        return "hide";
    case LayerMetadataKind::SetOpacity:
        return "opacity";
    case LayerMetadataKind::SetBlendMode:
        return "blend";
    }
    return "unknown";
}

struct ThrowFault final
{
    int hits = 0;
};

void FailBeforeHistoryPublication(void* context, LayerMetadataPrepareStage stage)
{
    if (stage == LayerMetadataPrepareStage::AfterEdit)
    {
        ++static_cast<ThrowFault*>(context)->hits;
        throw std::bad_alloc{};
    }
}

void PreservedGestureMatrix()
{
    constexpr std::array<Gesture, 4> gestures{Gesture::Brush, Gesture::Warp, Gesture::Shape, Gesture::Lasso};
    constexpr std::array<LayerMetadataKind, 4> kinds{LayerMetadataKind::Rename, LayerMetadataKind::ToggleVisibility,
                                                     LayerMetadataKind::SetOpacity, LayerMetadataKind::SetBlendMode};
    for (const Gesture gesture : gestures)
    {
        for (const LayerMetadataKind kind : kinds)
        {
            DocumentSession session = MakeSession();
            const bool began = BeginGesture(session, gesture);
            const std::string label = std::string(GestureName(gesture)) + "-" + KindName(kind);
            Expect(began, (label + "-gesture-opens").c_str());
            if (!began)
                continue;
            const CanvasDocument before = session.Document().value();
            const OwnerState ownerBefore(session);
            const std::wstring_view name = kind == LayerMetadataKind::Rename ? L"Portrait" : L"";
            const double opacity = kind == LayerMetadataKind::SetOpacity ? 0.25 : 1.0;
            const LayerBlendMode blend =
                kind == LayerMetadataKind::SetBlendMode ? LayerBlendMode::Screen : LayerBlendMode::Normal;
            auto prepared = Prepare(session, kind, name, opacity, blend);
            Expect(prepared.has_value(), (label + "-prepares-during-open-gesture").c_str());
            Expect(SameOwnerState(session, ownerBefore) && SameCapturedPayload(session.Document().value(), before),
                   (label + "-preparation-leaves-fields-generation-and-history-unchanged").c_str());
            if (!prepared.has_value())
            {
                CancelGesture(session, gesture);
                continue;
            }
            MoveGesture(session, gesture);
            Expect(session.IsPreparedLayerMetadataCurrent(prepared.value()),
                   (label + "-pointer-motion-keeps-same-opening-current").c_str());
            const bool applied = session.ApplyPreparedLayerMetadataIfCurrent(prepared.value());
            Expect(applied, (label + "-applies-while-original-gesture-remains-open").c_str());
            Expect(GestureActive(session, gesture), (label + "-keeps-original-gesture-active").c_str());
            if (applied)
            {
                auto expected = DocumentFixture::Copy(before);
                switch (kind)
                {
                case LayerMetadataKind::Rename:
                    expected.layers[0].name = L"Portrait";
                    break;
                case LayerMetadataKind::ToggleVisibility:
                    expected.layers[0].visible = false;
                    break;
                case LayerMetadataKind::SetOpacity:
                    expected.layers[0].opacity = 0.25;
                    break;
                case LayerMetadataKind::SetBlendMode:
                    expected.layers[0].blendMode = LayerBlendMode::Screen;
                    break;
                }
                Expect(SameCapturedPayload(session.Document().value(), std::move(expected).Seal()),
                       (label + "-changes-only-requested-field-retaining-image-mask-selection").c_str());
                const wchar_t* historyName = kind == LayerMetadataKind::Rename             ? L"Rename Layer"
                                             : kind == LayerMetadataKind::ToggleVisibility ? L"Hide Layer"
                                             : kind == LayerMetadataKind::SetOpacity       ? L"Layer Opacity"
                                                                                           : L"Layer Blend Mode";
                Expect(session.Generation() == ownerBefore.generation + 1 &&
                           session.history.CaptureState().Depth() == 0 &&
                           session.history.UndoCount() == ownerBefore.undoCount + 1 &&
                           session.history.UndoName() == historyName,
                       (label + "-one-independent-entry-and-one-generation-after-apply").c_str());
            }
            CancelGesture(session, gesture);
        }
    }
}

void NestedHistoryAndLegacyRefusal()
{
    {
        DocumentSession session = MakeSession();
        const CanvasDocument before = session.Document().value();
        session.BeginEdit(L"Outer gesture");
        const auto open = session.history.CaptureState();
        auto prepared = Prepare(session, LayerMetadataKind::Rename, L"Nested name");
        Expect(prepared.has_value(), "nested-history-metadata-prepares");
        if (prepared.has_value())
        {
            Expect(session.history.CaptureState().Depth() == open.Depth() && session.history.UndoCount() == 0 &&
                       session.Document().value() == before,
                   "nested-preparation-does-not-add-depth-entry-or-live-mutation");
            Expect(session.ApplyPreparedLayerMetadataIfCurrent(prepared.value()) &&
                       session.history.CaptureState().Depth() == open.Depth() && session.history.UndoCount() == 0,
                   "nested-apply-stays-inside-existing-history-opening");
        }
        session.EndEdit();
        Expect(session.Document().value().layers[0].name == L"Nested name" && session.history.UndoCount() == 1 &&
                   session.history.UndoName() == L"Outer gesture",
               "outer-end-records-opening-to-final-as-one-entry");
        session.Undo();
        Expect(session.Document().value().layers[0].name == L"Original", "outer-undo-restores-opening-metadata");
    }
    {
        DocumentSession session = MakeSession();
        session.BeginEdit(L"Existing transaction");
        Expect(!session.PrepareLayerMetadataInput(LayerMetadataKind::Rename, kTargetId, L"Settled path refusal"),
               "settled-prepare-retains-open-history-refusal");
        session.EndEdit();
    }
}

void CancellationFaultAndRedo()
{
    {
        DocumentSession session = MakeSession();
        const bool began = BeginGesture(session, Gesture::Warp);
        const OwnerState before(session);
        CancelSource cancelled;
        static_cast<void>(cancelled.Cancel());
        auto prepared =
            Prepare(session, LayerMetadataKind::Rename, L"Cancelled", 1.0, LayerBlendMode::Normal, cancelled.Token());
        Expect(began && !prepared.has_value() && SameOwnerState(session, before) &&
                   GestureActive(session, Gesture::Warp),
               "cancel-before-publication-leaves-document-generation-and-history");
        if (began)
            CancelGesture(session, Gesture::Warp);
    }
    {
        DocumentSession session = MakeSession();
        const bool began = BeginGesture(session, Gesture::Lasso);
        const OwnerState before(session);
        ThrowFault fault;
        const LayerMetadataPrepareObserver observer{&FailBeforeHistoryPublication, &fault};
        bool threw = false;
        try
        {
            auto prepared =
                Prepare(session, LayerMetadataKind::Rename, L"Faulted", 1.0, LayerBlendMode::Normal, {}, observer);
            static_cast<void>(prepared);
        }
        catch (const std::bad_alloc&)
        {
            threw = true;
        }
        Expect(began && fault.hits == 1 && threw && SameOwnerState(session, before) &&
                   GestureActive(session, Gesture::Lasso),
               "fault-before-publication-leaves-document-generation-and-history");
        if (began)
            CancelGesture(session, Gesture::Lasso);
    }
    {
        DocumentSession session = MakeSession();
        session.RenameLayer(kTargetId, L"Seed");
        session.Undo();
        const std::wstring redoName = session.history.RedoName();
        const std::size_t redoCount = session.history.CaptureState().FutureCount();
        const bool began = BeginGesture(session, Gesture::Brush);
        auto prepared = began ? Prepare(session, LayerMetadataKind::Rename, L"Original") : std::nullopt;
        const bool applied = prepared.has_value() && session.ApplyPreparedLayerMetadataIfCurrent(prepared.value());
        Expect(prepared.has_value() && !prepared.value().ChangesDocument() && applied &&
                   session.Document().value().layers[0].name == L"Original" &&
                   session.history.CaptureState().FutureCount() == redoCount && session.history.RedoName() == redoName,
               "metadata-no-op-preserves-redo-during-open-gesture");
        if (began)
            CancelGesture(session, Gesture::Brush);
    }
}

void StaleGestureAndSessionGuards()
{
    {
        DocumentSession session = MakeSession();
        const bool began = BeginGesture(session, Gesture::Shape);
        auto prepared = began ? Prepare(session, LayerMetadataKind::Rename, L"Stale") : std::nullopt;
        session.CancelShape();
        session.BeginShape(Vec2{0, 0});
        session.DragShape(Vec2{1, 1}, false, false);
        Expect(prepared.has_value() && !session.IsPreparedLayerMetadataCurrent(prepared.value()) &&
                   !session.ApplyPreparedLayerMetadataIfCurrent(prepared.value()) &&
                   session.Document().value().layers[0].name == L"Original",
               "closed-and-reopened-equal-gesture-rejects-old-result");
        session.CancelShape();
    }
    {
        DocumentSession session = MakeSession();
        const bool began = BeginGesture(session, Gesture::Brush);
        auto prepared = began ? Prepare(session, LayerMetadataKind::Rename, L"Stale active") : std::nullopt;
        session.SelectLayerTarget(kSiblingId, false);
        Expect(prepared.has_value() && !session.IsPreparedLayerMetadataCurrent(prepared.value()),
               "active-layer-change-invalidates-preserved-result");
        CancelGesture(session, Gesture::Brush);
    }
    {
        DocumentSession session = MakeSession();
        const bool began = BeginGesture(session, Gesture::Lasso);
        auto prepared = began ? Prepare(session, LayerMetadataKind::Rename, L"Stale mask") : std::nullopt;
        session.SelectLayerTarget(kTargetId, true);
        Expect(prepared.has_value() && !session.IsPreparedLayerMetadataCurrent(prepared.value()),
               "mask-target-change-invalidates-preserved-result");
        CancelGesture(session, Gesture::Lasso);
    }
    {
        DocumentSession session = MakeSession();
        const bool began = BeginGesture(session, Gesture::Warp);
        auto prepared = began ? Prepare(session, LayerMetadataKind::Rename, L"Stale history") : std::nullopt;
        session.RenameLayer(kSiblingId, L"Changed");
        Expect(prepared.has_value() && !session.IsPreparedLayerMetadataCurrent(prepared.value()),
               "published-history-change-invalidates-preserved-result");
        CancelGesture(session, Gesture::Warp);
    }
    {
        DocumentSession session = MakeSession();
        const bool began = BeginGesture(session, Gesture::Shape);
        auto prepared = began ? Prepare(session, LayerMetadataKind::Rename, L"Stale document") : std::nullopt;
        EditLayerRecordsForTest(session, [](DocumentFixture& document) { document.layers[1].name = L"Changed"; });
        Expect(prepared.has_value() && !session.IsPreparedLayerMetadataCurrent(prepared.value()),
               "document-mutation-invalidates-preserved-result");
        CancelGesture(session, Gesture::Shape);
    }
    {
        DocumentSession first = MakeSession();
        DocumentSession second = MakeSession();
        const bool began = BeginGesture(first, Gesture::Brush);
        auto prepared = began ? Prepare(first, LayerMetadataKind::Rename, L"Cross-session") : std::nullopt;
        Expect(prepared.has_value() && !second.IsPreparedLayerMetadataCurrent(prepared.value()),
               "session-change-invalidates-preserved-result");
        if (began)
            CancelGesture(first, Gesture::Brush);
    }
}

void MemoryAdmission()
{
    DocumentSession session = MakeSession();
    const auto settled = session.PreservedLayerMetadataMemoryBytes(LayerMetadataKind::Rename, 0);
    const auto named = session.PreservedLayerMetadataMemoryBytes(LayerMetadataKind::Rename, 8);
    Expect(settled > 0 && named > settled, "preserved-memory-admission-reserves-name-and-fixed-state");
    const bool began = BeginGesture(session, Gesture::Brush);
    const auto duringGesture = session.PreservedLayerMetadataMemoryBytes(LayerMetadataKind::Rename, 8);
    Expect(began && duringGesture > 0, "preserved-memory-admission-allows-active-gesture");
    if (began)
        CancelGesture(session, Gesture::Brush);
}

void AllOpeningAndNestedKinds()
{
    constexpr std::array gestures{Gesture::Brush, Gesture::Warp, Gesture::Shape, Gesture::Lasso};
    for (const auto gesture : gestures)
    {
        auto session = MakeSession();
        const std::string label = std::string(GestureName(gesture)) + "-opening-";
        Expect(BeginGesture(session, gesture), (label + "first-opens").c_str());
        const auto authority = session.CaptureLayerMetadataAuthority(true);
        auto prepared = Prepare(session, LayerMetadataKind::Rename, L"Expired opening");
        CancelGesture(session, gesture);
        Expect(prepared && !session.CanApplyLayerMetadataAuthority(authority) &&
                   !session.ApplyPreparedLayerMetadataIfCurrent(prepared.value()),
               (label + "closing-refuses-held-result-without-mutation").c_str());
        Expect(BeginGesture(session, gesture), (label + "equal-replacement-opens").c_str());
        Expect(prepared && !session.CanApplyLayerMetadataAuthority(authority) &&
                   !session.ApplyPreparedLayerMetadataIfCurrent(prepared.value()) &&
                   session.Document().value().layers[0].name == L"Original" && session.history.UndoCount() == 0,
               (label + "equal-replacement-cannot-reuse-authority").c_str());
        CancelGesture(session, gesture);
    }
    {
        auto session = MakeSession();
        session.BeginMarquee({0, 0}, SelectionMode::New);
        auto prepared = Prepare(session, LayerMetadataKind::Rename, L"Expired marquee");
        session.CancelLasso();
        session.BeginMarquee({0, 0}, SelectionMode::New);
        Expect(prepared && !session.ApplyPreparedLayerMetadataIfCurrent(prepared.value()),
               "marquee-reopen-cannot-reuse-lasso-opening-authority");
        session.CancelLasso();
    }
    constexpr std::array kinds{LayerMetadataKind::Rename, LayerMetadataKind::ToggleVisibility,
                               LayerMetadataKind::SetOpacity, LayerMetadataKind::SetBlendMode};
    for (const auto kind : kinds)
    {
        auto session = MakeSession();
        const auto before = session.Document().value();
        auto expected = DocumentFixture::Copy(before);
        switch (kind)
        {
        case LayerMetadataKind::Rename:
            expected.layers[0].name = L"Nested literal";
            break;
        case LayerMetadataKind::ToggleVisibility:
            expected.layers[0].visible = false;
            break;
        case LayerMetadataKind::SetOpacity:
            expected.layers[0].opacity = 0.25;
            break;
        case LayerMetadataKind::SetBlendMode:
            expected.layers[0].blendMode = LayerBlendMode::Screen;
            break;
        }
        const auto expectedDocument = std::move(expected).Seal();
        const std::string label = std::string("double-nested-") + KindName(kind) + "-";
        session.BeginEdit(L"Outer literal");
        session.BeginEdit(L"Inner literal");
        const auto history = session.history.CaptureState();
        const auto generation = session.Generation();
        auto prepared = Prepare(session, kind, L"Nested literal", 0.25, LayerBlendMode::Screen);
        Expect(prepared && session.history.IsCurrent(history) && session.Document().value() == before,
               (label + "preparation-preserves-complete-opening").c_str());
        Expect(prepared && session.ApplyPreparedLayerMetadataIfCurrent(prepared.value()) &&
                   session.Document().value() == expectedDocument && session.history.IsCurrent(history) &&
                   session.history.CaptureState().Depth() == 2 && session.Generation() == generation + 1 &&
                   session.history.UndoCount() == 0,
               (label + "apply-preserves-two-level-history-and-only-edits-field").c_str());
        session.EndEdit();
        Expect(session.history.CaptureState().Depth() == 1 && session.history.UndoCount() == 0,
               (label + "inner-end-records-no-entry").c_str());
        session.EndEdit();
        Expect(session.history.CaptureState().Depth() == 0 && session.history.UndoCount() == 1 &&
                   session.history.UndoName() == L"Outer literal",
               (label + "outer-end-records-one-original-name").c_str());
        session.Undo();
        Expect(session.Document().value() == before, (label + "undo-restores-every-original-field").c_str());
        session.Redo();
        Expect(session.Document().value() == expectedDocument, (label + "redo-restores-every-final-field").c_str());
    }
    {
        auto session = MakeSession();
        session.BeginEdit(L"Opening before capture");
        auto prepared = Prepare(session, LayerMetadataKind::Rename, L"Obsolete history opening");
        session.EndEdit();
        session.BeginEdit(L"Different opening");
        Expect(prepared && !session.ApplyPreparedLayerMetadataIfCurrent(prepared.value()) &&
                   session.Document().value().layers[0].name == L"Original",
               "equal-depth-different-history-opening-refuses-result");
        session.EndEdit();
    }
}
} // namespace

int RunPreservedLayerMetadataTests()
{
    gFailures = 0;
    PreservedGestureMatrix();
    NestedHistoryAndLegacyRefusal();
    CancellationFaultAndRedo();
    StaleGestureAndSessionGuards();
    MemoryAdmission();
    AllOpeningAndNestedKinds();
    return gFailures;
}
