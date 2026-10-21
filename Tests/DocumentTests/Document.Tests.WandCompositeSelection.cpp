#include "Document.Tests.WandCompositeSelection.h"

#include "../Support/Support.Tests.DocumentFixture.h"

#include "Document.h"
#include "PixelBuffer.h"
#include "Selection.h"
#include "WandLayerSelectionWork.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <optional>
#include <utility>

namespace
{
int gFailures = 0;
int gAssertions = 0;

void Expect(bool condition, const char* name) noexcept
{
    ++gAssertions;
    std::printf("%s wand-composite-core.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

WandSettings CompositeSettings(bool contiguous = true) noexcept
{
    WandSettings settings;
    settings.tolerance = 0;
    settings.sampleSize = WandSampleSize::Point;
    settings.contiguous = contiguous;
    settings.sampleAllLayers = true;
    return settings;
}

DocumentSession Session(int width = 4, int height = 2)
{
    DocumentSession session;
    DocumentFixture document;
    document.id = Guid::Generate();
    document.width = width;
    document.height = height;

    // The active layer is uniformly the right-hand composite color. A successful left-half
    // outline therefore proves the detached composite, rather than the active layer, was sampled.
    constexpr std::array<std::uint8_t, 4> activePixel{20, 40, 60, 255};
    ImageLayer active = ImageLayer::Blank(L"Composite wand active", Vec2{1, 1});
    const Guid activeID = active.id;
    active.asset = RasterAsset{
        Raster::Make(1, 1, RasterFormat::Bgra8888, activePixel.data(), activePixel.size()), {}, active.name};
    active.transform.size = Vec2{static_cast<double>(width), static_cast<double>(height)};
    active.transform.sampling = LayerSampling::Nearest;
    document.layers.push_back(std::move(active));
    session.ReplaceDocument(std::move(document).Seal(), activeID);
    return session;
}

constexpr std::array<std::uint8_t, 32> kTwoColorComposite{200, 10,  20, 255, 200, 10,  20, 255, 20,  40,  60,
                                                          255, 20,  40, 60,  255, 200, 10, 20,  255, 200, 10,
                                                          20,  255, 20, 40,  60,  255, 20, 40,  60,  255};

Raster TwoColorComposite()
{
    return Raster::Make(4, 2, RasterFormat::Bgra8888, kTwoColorComposite.data(), kTwoColorComposite.size());
}

bool LiteralRect(const std::optional<DocumentSelection>& selection, double x, double y, double width, double height,
                 bool antialiased)
{
    if (!selection.has_value() || selection.value().antialiased != antialiased || selection.value().loops.size() != 1)
        return false;
    const SelectionLoop expected{Vec2{x, y}, Vec2{x + width, y}, Vec2{x + width, y + height}, Vec2{x, y + height}};
    return selection.value().loops.Get().front() == expected;
}

std::optional<PreparedWandLayerSelection> PrepareComposite(DocumentSession& session, const Raster& composite,
                                                           Vec2 point = Vec2{0.5, 0.5},
                                                           WandSettings settings = CompositeSettings(),
                                                           bool antialiased = false, WandFailure* failure = nullptr,
                                                           CancelToken cancellation = {},
                                                           DocumentHistory::PrepareObserver historyObserver = {},
                                                           const WandLayerSelectionObserver* observer = nullptr)
{
    auto input = session.PrepareWandCompositeSelectionInput(point, settings, antialiased);
    if (!input.has_value())
        return std::nullopt;
    return DocumentSession::PrepareWandCompositeSelectionPublication(std::move(input.value()), composite, failure,
                                                                     cancellation, historyObserver, observer);
}

void CompositePixelsAndHistory()
{
    DocumentSession session = Session();
    const auto originalDocument = session.Document();
    auto input = session.PrepareWandCompositeSelectionInput(Vec2{0.5, 0.5}, CompositeSettings(), true);
    Expect(input.has_value(), "capture.requires-all-layers-and-accepts-point");
    if (!input.has_value())
        return;
    const CanvasDocument* readbackDocument = input.value().ReadbackDocument();
    Expect(readbackDocument != nullptr && originalDocument.has_value() && *readbackDocument == originalDocument.value(),
           "capture.readback-source-is-the-original-immutable-document");

    auto prepared =
        DocumentSession::PrepareWandCompositeSelectionPublication(std::move(input.value()), TwoColorComposite());
    Expect(prepared.has_value(), "publication.detached-two-color-composite-prepares");
    if (!prepared.has_value())
        return;
    Expect(prepared.value().ChangesSelection(), "publication.new-composite-outline-is-a-change");
    Expect(session.ApplyPreparedWandLayerSelectionIfCurrent(prepared.value()) &&
               LiteralRect(session.Selection(), 0, 0, 2, 2, true),
           "composite.left-region-exact-rectangle-differs-from-active-layer-color");
    Expect(session.history.UndoCount() == 1 && session.history.UndoName() == L"Magic Wand",
           "history.success-is-one-magic-wand-entry");
    session.Undo();
    const bool undoRestored = !session.Selection().has_value() && session.history.RedoName() == L"Magic Wand";
    session.Redo();
    Expect(undoRestored && LiteralRect(session.Selection(), 0, 0, 2, 2, true),
           "history.undo-redo-restores-exact-composite-outline");

    DocumentSession noOp = Session();
    noOp.SetSelection(DocumentSelection{true, SelectionGeometry::Rect(0, 0, 2, 2)}, L"Seed same outline");
    noOp.SetSelection(DocumentSelection{false, SelectionGeometry::Rect(3, 1, 1, 1)}, L"Seed redo");
    noOp.Undo();
    const int undoBefore = noOp.history.UndoCount();
    const std::size_t redoBefore = noOp.history.CaptureState().FutureCount();
    auto identical = PrepareComposite(noOp, TwoColorComposite(), Vec2{0.5, 0.5}, CompositeSettings(), true);
    Expect(identical.has_value(), "noop.identical-outline-prepares");
    if (identical.has_value())
    {
        Expect(!identical.value().ChangesSelection() &&
                   noOp.ApplyPreparedWandLayerSelectionIfCurrent(identical.value()) &&
                   LiteralRect(noOp.Selection(), 0, 0, 2, 2, true) && noOp.history.UndoCount() == undoBefore &&
                   noOp.history.CaptureState().FutureCount() == redoBefore,
               "noop.identical-outline-adds-no-history-and-preserves-redo");
    }

    DocumentSession transparent = Session(3, 2);
    constexpr std::array<std::uint8_t, 24> transparentPixels{};
    const Raster clearComposite =
        Raster::Make(3, 2, RasterFormat::Bgra8888, transparentPixels.data(), transparentPixels.size());
    auto clear = PrepareComposite(transparent, clearComposite, Vec2{2.5, 1.5}, CompositeSettings(false));
    Expect(clear.has_value(), "transparent.full-canvas-prepares");
    if (clear.has_value())
        Expect(transparent.ApplyPreparedWandLayerSelectionIfCurrent(clear.value()) &&
                   LiteralRect(transparent.Selection(), 0, 0, 3, 2, false),
               "transparent.composite-selects-literal-full-canvas");
}

void InputValidationAndLegacyIsolation()
{
    DocumentSession session = Session();
    const auto before = session.Document();
    auto legacyAllLayers = CompositeSettings();
    Expect(!session.PrepareWandLayerSelectionInput(Vec2{0.5, 0.5}, legacyAllLayers, false),
           "legacy.active-layer-input-still-refuses-sample-all-layers");
    auto activeOnlySettings = legacyAllLayers;
    activeOnlySettings.sampleAllLayers = false;
    Expect(!session.PrepareWandCompositeSelectionInput(Vec2{0.5, 0.5}, activeOnlySettings, false),
           "capture.composite-input-requires-sample-all-layers");

    const std::array<Vec2, 6> invalidPoints{Vec2{-0.01, 0},
                                            Vec2{4, 0},
                                            Vec2{0, 2},
                                            Vec2{std::numeric_limits<double>::quiet_NaN(), 0},
                                            Vec2{0, std::numeric_limits<double>::infinity()},
                                            Vec2{4.01, 1}};
    bool allInvalidRefused = true;
    for (const Vec2 point : invalidPoints)
        allInvalidRefused = allInvalidRefused &&
                            !session.PrepareWandCompositeSelectionInput(point, CompositeSettings(), false).has_value();
    Expect(allInvalidRefused, "capture.nonfinite-and-out-of-canvas-points-refuse");
    Expect(session.PrepareWandCompositeSelectionInput(Vec2{0, 0}, CompositeSettings(), false).has_value(),
           "capture.finite-top-left-edge-is-accepted");

    constexpr std::array<std::uint8_t, 8> grayPixels{};
    const Raster gray = Raster::Make(4, 2, RasterFormat::Gray8, grayPixels.data(), grayPixels.size());
    constexpr std::array<std::uint8_t, 8> smallPixels{};
    const Raster wrongSize = Raster::Make(2, 1, RasterFormat::Bgra8888, smallPixels.data(), smallPixels.size());
    const Raster empty;
    const std::array<const Raster*, 3> invalidComposites{&empty, &gray, &wrongSize};
    constexpr std::array<const char*, 3> names{"missing", "gray8", "wrong-size"};
    for (std::size_t i = 0; i < invalidComposites.size(); ++i)
    {
        const Raster& composite = *invalidComposites[i];
        const Guid revision = session.history.CurrentRevision();
        const int undoCount = session.history.UndoCount();
        WandFailure failure = WandFailure::None;
        const auto published =
            PrepareComposite(session, composite, Vec2{0.5, 0.5}, CompositeSettings(), false, &failure);
        char name[80]{};
        std::snprintf(name, sizeof(name), "publication.invalid-%s-is-empty-and-preserves-source", names[i]);
        Expect(!published.has_value() && failure == WandFailure::Memory && session.Document() == before &&
                   session.history.CurrentRevision() == revision && session.history.UndoCount() == undoCount,
               i == 0 ? "publication.invalid-missing-is-empty-and-preserves-source" : name);
    }

    auto unsupported = CompositeSettings();
    unsupported.sampleSize = static_cast<WandSampleSize>(255);
    Expect(!session.PrepareWandCompositeSelectionInput(Vec2{0.5, 0.5}, unsupported, false).has_value() &&
               !session.PlanWandCompositeSelectionMemory(Vec2{0.5, 0.5}, unsupported).has_value(),
           "capture.literal-unsupported-sample-size-is-refused");
}

void CrossedPublicationModes()
{
    {
        DocumentSession session = Session();
        auto input = session.PrepareWandCompositeSelectionInput(Vec2{0.5, 0.5}, CompositeSettings(), false);
        Expect(input.has_value(), "crossed.composite-input-fixture-captured");
        if (!input.has_value())
            return;
        const auto before = session.Document();
        const Guid revision = session.history.CurrentRevision();
        const int undoCount = session.history.UndoCount();
        WandFailure failure = WandFailure::None;
        const auto publication =
            DocumentSession::PrepareWandLayerSelectionPublication(std::move(input.value()), &failure);
        Expect(!publication.has_value() && failure == WandFailure::Memory &&
                   input.value().ReadbackDocument() == nullptr && session.Document() == before &&
                   session.history.CurrentRevision() == revision && session.history.UndoCount() == undoCount,
               "crossed.composite-input-refused-by-layer-publication");
    }
    {
        DocumentSession session = Session();
        auto input = session.PrepareWandLayerSelectionInput(Vec2{0.5, 0.5}, WandSettings{}, false);
        Expect(input.has_value(), "crossed.layer-input-fixture-captured");
        if (!input.has_value())
            return;
        const auto before = session.Document();
        const Guid revision = session.history.CurrentRevision();
        const int undoCount = session.history.UndoCount();
        WandFailure failure = WandFailure::None;
        const auto publication = DocumentSession::PrepareWandCompositeSelectionPublication(
            std::move(input.value()), TwoColorComposite(), &failure);
        Expect(!publication.has_value() && failure == WandFailure::Memory &&
                   input.value().ReadbackDocument() == nullptr && session.Document() == before &&
                   session.history.CurrentRevision() == revision && session.history.UndoCount() == undoCount,
               "crossed.layer-input-refused-by-composite-publication");
    }
}

void StaleAndForeignPublication()
{
    {
        DocumentSession session = Session();
        auto prepared = PrepareComposite(session, TwoColorComposite());
        Expect(prepared.has_value(), "authority.same-id-fixture-prepared");
        if (prepared.has_value())
        {
            const auto document = session.Document();
            Expect(document.has_value(), "authority.same-id-fixture-has-document");
            if (!document.has_value())
                return;
            DocumentFixture replacement = DocumentFixture::Copy(document.value());
            replacement.resolution = 144;
            CanvasDocument sameIDReplacement = std::move(replacement).Seal();
            session.ReplaceDocument(std::move(sameIDReplacement), session.ActiveLayerID());
            Expect(!session.IsPreparedWandLayerSelectionCurrent(prepared.value()) &&
                       !session.ApplyPreparedWandLayerSelectionIfCurrent(prepared.value()),
                   "authority.same-document-id-replacement-is-stale");
        }
    }
    {
        DocumentSession session = Session();
        auto prepared = PrepareComposite(session, TwoColorComposite());
        Expect(prepared.has_value(), "authority.epoch-fixture-prepared");
        if (prepared.has_value())
        {
            session.BeginEdit(L"Epoch only");
            Expect(!session.IsPreparedWandLayerSelectionCurrent(prepared.value()) &&
                       !session.ApplyPreparedWandLayerSelectionIfCurrent(prepared.value()),
                   "authority.history-epoch-only-change-is-stale");
            session.EndEdit();
        }
    }
    {
        DocumentSession owner = Session();
        auto prepared = PrepareComposite(owner, TwoColorComposite());
        Expect(prepared.has_value(), "authority.foreign-fixture-prepared");
        if (!prepared.has_value())
            return;
        DocumentSession copy(owner);
        DocumentSession foreign = Session();
        Expect(!copy.IsPreparedWandLayerSelectionCurrent(prepared.value()) &&
                   !copy.ApplyPreparedWandLayerSelectionIfCurrent(prepared.value()) &&
                   !foreign.IsPreparedWandLayerSelectionCurrent(prepared.value()) &&
                   !foreign.ApplyPreparedWandLayerSelectionIfCurrent(prepared.value()),
               "authority.copied-and-foreign-session-refuse-publication");
        DocumentSession moved(std::move(owner));
        Expect(!moved.IsPreparedWandLayerSelectionCurrent(prepared.value()) &&
                   !moved.ApplyPreparedWandLayerSelectionIfCurrent(prepared.value()),
               "authority.moved-session-refuses-prior-publication");
    }
}

struct CancelAtStage final
{
    WandLayerSelectionStage target{};
    CancelSource* source = nullptr;
    int hits = 0;
};

void CancelCheckpoint(void* context, WandLayerSelectionStage stage) noexcept
{
    auto& observer = *static_cast<CancelAtStage*>(context);
    if (observer.target == stage)
    {
        ++observer.hits;
        static_cast<void>(observer.source->Cancel());
    }
}

void CancellationAndMoveReuse()
{
    constexpr std::array stages{WandLayerSelectionStage::BeforeSource, WandLayerSelectionStage::SourceReady,
                                WandLayerSelectionStage::OutlineReady, WandLayerSelectionStage::SelectionReady,
                                WandLayerSelectionStage::DocumentReady};
    constexpr std::array names{"before-source", "source-ready", "outline-ready", "selection-ready", "document-ready"};
    for (std::size_t i = 0; i < stages.size(); ++i)
    {
        DocumentSession session = Session();
        const auto initialSelection = session.Selection();
        CancelSource cancellation;
        CancelAtStage checkpoint{stages[i], &cancellation, 0};
        const WandLayerSelectionObserver observer{&checkpoint, &CancelCheckpoint};
        WandFailure failure = WandFailure::None;
        auto prepared = PrepareComposite(session, TwoColorComposite(), Vec2{0.5, 0.5}, CompositeSettings(), false,
                                         &failure, cancellation.Token(), {}, &observer);
        char name[96]{};
        std::snprintf(name, sizeof(name), "cancel.%s-checkpoint-hit-once", names[i]);
        Expect(checkpoint.hits == 1, name);
        std::snprintf(name, sizeof(name), "cancel.%s-fails-without-publishing", names[i]);
        Expect(!prepared.has_value() && failure == WandFailure::Cancelled && session.Selection() == initialSelection &&
                   session.history.UndoCount() == 0,
               name);
    }

    {
        DocumentSession session = Session();
        const auto before = session.Document();
        const Guid revision = session.history.CurrentRevision();
        const int undoCount = session.history.UndoCount();
        CancelSource cancellation;
        static_cast<void>(cancellation.Cancel());
        WandFailure failure = WandFailure::None;
        const auto prepared = PrepareComposite(session, TwoColorComposite(), Vec2{0.5, 0.5}, CompositeSettings(), false,
                                               &failure, cancellation.Token());
        Expect(!prepared.has_value() && failure == WandFailure::Cancelled && session.Document() == before &&
                   session.history.CurrentRevision() == revision && session.history.UndoCount() == undoCount,
               "cancel.precancelled-token-refuses-without-publication");
    }

    DocumentSession session = Session();
    auto input = session.PrepareWandCompositeSelectionInput(Vec2{0.5, 0.5}, CompositeSettings(), false);
    Expect(input.has_value(), "ownership.move-fixture-captured");
    if (input.has_value())
    {
        WandLayerSelectionInput moved(std::move(input.value()));
        Expect(input.value().ReadbackDocument() == nullptr, "ownership.moved-from-input-readback-is-null");
        auto reused =
            DocumentSession::PrepareWandCompositeSelectionPublication(std::move(input.value()), TwoColorComposite());
        Expect(!reused.has_value(), "ownership.moved-from-input-cannot-be-reused");
        auto prepared =
            DocumentSession::PrepareWandCompositeSelectionPublication(std::move(moved), TwoColorComposite());
        Expect(prepared.has_value(), "ownership.moved-input-remains-usable-once");
        if (prepared.has_value())
            Expect(session.ApplyPreparedWandLayerSelectionIfCurrent(prepared.value()) &&
                       !session.IsPreparedWandLayerSelectionCurrent(prepared.value()) &&
                       !session.ApplyPreparedWandLayerSelectionIfCurrent(prepared.value()),
                   "ownership.publication-is-single-use");
    }
}

void CompositeMemoryPlan()
{
    DocumentSession empty;
    CanvasDocument noLayers;
    noLayers.id = Guid::Generate();
    noLayers.width = 4;
    noLayers.height = 2;
    empty.ReplaceDocument(std::move(noLayers), std::nullopt);
    const auto plan = empty.PlanWandCompositeSelectionMemory(Vec2{0.5, 0.5}, CompositeSettings());
    const auto& capturedDocument = empty.Document();
    const auto logicalCompositeBytes = capturedDocument ? static_cast<std::size_t>(capturedDocument->width) *
                                                              static_cast<std::size_t>(capturedDocument->height) * 4u
                                                        : 0u;
    const std::uint64_t requiredCompositeBytes = PixelBuffer::RequiredBytes(logicalCompositeBytes);
    Expect(plan.has_value(), "memory.empty-document-plan-is-available");
    if (plan.has_value())
        Expect(!plan.value().transparentShortcut && logicalCompositeBytes == 32u &&
                   requiredCompositeBytes >= logicalCompositeBytes &&
                   plan.value().sampledPixelsBytes == requiredCompositeBytes && plan.value().privateSourceBytes == 0 &&
                   plan.value().preparationBytes >= requiredCompositeBytes &&
                   plan.value().retirementBytes >= requiredCompositeBytes,
               "memory.empty-document-does-not-shortcut-and-charges-composite-output-and-retirement");

    DocumentSession sparseSession = Session();
    const auto sourceDocument = sparseSession.Document();
    Expect(sourceDocument.has_value() && !sourceDocument.value().layers.empty() &&
               sourceDocument.value().layers.front().asset.has_value(),
           "memory.sparse-layer-fixture-has-source-raster");
    if (!sourceDocument.has_value() || sourceDocument.value().layers.empty() ||
        !sourceDocument.value().layers.front().asset.has_value())
        return;
    const Raster tile = sourceDocument.value().layers.front().asset.value().image;
    const Raster sparse = Raster::MakeSparse(1, 1, RasterFormat::Bgra8888, Raster{}, 0, 0, {RasterTile{0, 0, tile}}, 0);
    EditLayerRecordsForTest(sparseSession,
                            [&](DocumentFixture& document) { document.layers.front().asset.value().image = sparse; });
    const auto layerPlan = sparseSession.PlanWandLayerSelectionMemory(Vec2{0.5, 0.5}, WandSettings{});
    const auto compositePlan = sparseSession.PlanWandCompositeSelectionMemory(Vec2{0.5, 0.5}, CompositeSettings());
    Expect(sparse.IsSparse() && layerPlan.has_value() && compositePlan.has_value() &&
               layerPlan.value().privateSourceBytes > 0 && compositePlan.value().privateSourceBytes == 0,
           "memory.sparse-source-is-charged-only-by-active-layer-planner");
}
} // namespace

int RunWandCompositeSelectionTests()
{
    gFailures = 0;
    gAssertions = 0;
    CompositePixelsAndHistory();
    InputValidationAndLegacyIsolation();
    CrossedPublicationModes();
    StaleAndForeignPublication();
    CancellationAndMoveReuse();
    CompositeMemoryPlan();
    std::printf("wand-composite-core.summary assertions=%d failures=%d\n", gAssertions, gFailures);
    return gFailures;
}
