#include "Document.Tests.LayerOpacityWork.h"

#include "Tests/Support/Support.Tests.DocumentFixture.h"

#include "Document.h"
#include "Executor.h"
#include "LayerMetadataWork.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <new>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>

namespace
{
int gFailures = 0;
int gAssertions = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s layer-opacity.%s\n", condition ? "PASS" : "FAIL", name);
    ++gAssertions;
    gFailures += condition ? 0 : 1;
}

constexpr Guid kDocumentId{{0x72, 0, 0, 1, 0, 0, 0x40, 0, 0x80, 0, 0, 0, 0, 0, 0, 1}};
constexpr Guid kTargetId{{0x72, 0, 0, 2, 0, 0, 0x40, 0, 0x80, 0, 0, 0, 0, 0, 0, 2}};
constexpr Guid kSiblingId{{0x72, 0, 0, 3, 0, 0, 0x40, 0, 0x80, 0, 0, 0, 0, 0, 0, 3}};

CanvasDocument FixtureDocument(bool targetIsGroup = false)
{
    constexpr std::array<std::uint8_t, 16> targetPixels{11, 21, 31, 255, 41,  51,  61,  255,
                                                        71, 81, 91, 255, 101, 111, 121, 255};
    constexpr std::array<std::uint8_t, 16> siblingPixels{12, 22, 32, 255, 42,  52,  62,  255,
                                                         72, 82, 92, 255, 102, 112, 122, 255};
    constexpr std::array<std::uint8_t, 4> maskPixels{17, 67, 137, 227};
    const Raster targetRaster = Raster::Make(2, 2, RasterFormat::Bgra8888, targetPixels.data(), targetPixels.size());
    const Raster siblingRaster = Raster::Make(2, 2, RasterFormat::Bgra8888, siblingPixels.data(), siblingPixels.size());
    const Raster maskRaster = Raster::Make(2, 2, RasterFormat::Gray8, maskPixels.data(), maskPixels.size());

    ImageLayer target = ImageLayer::Blank(targetIsGroup ? L"Opacity folder" : L"Target layer", Vec2{2, 2});
    target.id = kTargetId;
    target.isGroup = targetIsGroup;
    target.opacity = targetIsGroup ? 1.0 : 0.75;
    if (!targetIsGroup)
    {
        target.asset = RasterAsset{targetRaster, Raster::MakeThumbnail(targetRaster), L"Target pixels"};
        target.mask = LayerMask{RasterAsset{maskRaster, Raster::MakeThumbnail(maskRaster), L"Target mask"}};
    }

    ImageLayer sibling = ImageLayer::Blank(L"Sibling layer", Vec2{2, 2});
    sibling.id = kSiblingId;
    sibling.visible = false;
    sibling.asset = RasterAsset{siblingRaster, Raster::MakeThumbnail(siblingRaster), L"Sibling pixels"};

    DocumentFixture document;
    document.id = kDocumentId;
    document.width = 2;
    document.height = 2;
    document.layers = {target, sibling};
    document.selection = DocumentSelection{false, SelectionLoops{{Vec2{0, 0}, Vec2{2, 0}, Vec2{2, 2}, Vec2{0, 2}}}};
    return std::move(document).Seal();
}

DocumentSession MakeSession(bool targetIsGroup = false)
{
    DocumentSession session;
    session.ReplaceDocument(FixtureDocument(targetIsGroup), kTargetId);
    return session;
}

struct OwnerState final
{
    std::optional<CanvasDocument> document;
    DocumentHistory::StateView history;
    Guid revision;
    int undoCount = 0;
    std::size_t redoCount = 0;
    std::wstring undoName;
    std::wstring redoName;
    std::uint64_t generation = 0;
    std::optional<Guid> active;
    std::unordered_set<Guid> selected;
    std::unordered_set<Guid> collapsed;
    bool maskSelected = false;

    explicit OwnerState(DocumentSession& session)
        : document(session.Document()), history(session.history.CaptureState()),
          revision(session.history.CurrentRevision()), undoCount(session.history.UndoCount()),
          redoCount(history.FutureCount()), undoName(session.history.UndoName()), redoName(session.history.RedoName()),
          generation(session.Generation()), active(session.ActiveLayerID()), selected(session.SelectedLayerIDs()),
          collapsed(session.CollapsedGroupIDs()), maskSelected(session.IsMaskSelected())
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
           session.ActiveLayerID() == before.active && session.SelectedLayerIDs() == before.selected &&
           session.CollapsedGroupIDs() == before.collapsed && session.IsMaskSelected() == before.maskSelected;
}

std::optional<PreparedLayerMetadata> PreparePreview(DocumentSession& session, const Guid& target, double value,
                                                    CancelToken cancellation = {},
                                                    DocumentHistory::PrepareObserver historyObserver = {},
                                                    LayerMetadataPrepareObserver observer = {})
{
    auto input = session.PrepareLayerOpacityPreviewInput(target, value);
    if (!input)
        return std::nullopt;
    return PrepareLayerMetadataPublication(std::move(input.value()), cancellation, historyObserver, observer);
}

std::optional<PreparedLayerMetadata> PrepareTyped(DocumentSession& session, const Guid& target, double opening,
                                                  double value, CancelToken cancellation = {},
                                                  DocumentHistory::PrepareObserver historyObserver = {},
                                                  LayerMetadataPrepareObserver observer = {})
{
    auto input = session.PrepareTypedLayerOpacityInput(target, opening, value);
    if (!input)
        return std::nullopt;
    return PrepareLayerMetadataPublication(std::move(input.value()), cancellation, historyObserver, observer);
}

void PreviewRepeatAndEscape()
{
    DocumentSession session = MakeSession();
    const CanvasDocument opening = session.Document().value();
    auto expectedPreview = DocumentFixture::Copy(opening);
    expectedPreview.layers[0].opacity = 0.4;
    const CanvasDocument shownDocument = std::move(expectedPreview).Seal();
    const Guid revision = session.history.CurrentRevision();
    const auto undoCount = session.history.UndoCount();
    auto preview = PreparePreview(session, kTargetId, 0.4);
    Expect(preview && preview->ChangesDocument() && session.Document()->layers[0].opacity == 0.75,
           "preview-preparation-keeps-live-opacity-at-opening");
    Expect(preview && session.ApplyPreparedLayerMetadataIfCurrent(preview.value()) &&
               session.Document().value() == shownDocument && session.history.UndoCount() == undoCount &&
               session.history.CurrentRevision() == revision,
           "preview-changes-only-target-opacity-without-history");

    const auto shownGeneration = session.Generation();
    const OwnerState shown = [&]() { return OwnerState(session); }();
    auto repeated = PreparePreview(session, kTargetId, 0.4);
    Expect(repeated && !repeated->ChangesDocument() && session.ApplyPreparedLayerMetadataIfCurrent(repeated.value()) &&
               SameOwnerState(session, shown) && session.Generation() == shownGeneration,
           "repeated-same-preview-is-a-generation-and-revision-preserving-noop");

    auto escapeRestore = PreparePreview(session, kTargetId, 0.75);
    Expect(escapeRestore && escapeRestore->ChangesDocument() &&
               session.ApplyPreparedLayerMetadataIfCurrent(escapeRestore.value()) &&
               session.Document().value() == opening && session.history.UndoCount() == undoCount &&
               session.history.CurrentRevision() == revision,
           "escape-preview-restores-literal-opening-document-without-history");
}

void TypedClosureAndHistory()
{
    {
        DocumentSession session = MakeSession();
        const CanvasDocument opening = session.Document().value();
        auto expectedFinal = DocumentFixture::Copy(opening);
        expectedFinal.layers[0].opacity = 0.6;
        const CanvasDocument final = std::move(expectedFinal).Seal();
        auto tickOne = PreparePreview(session, kTargetId, 0.2);
        const bool firstApplied = tickOne && session.ApplyPreparedLayerMetadataIfCurrent(tickOne.value());
        auto tickTwo = PreparePreview(session, kTargetId, 0.4);
        Expect(firstApplied && tickTwo && session.ApplyPreparedLayerMetadataIfCurrent(tickTwo.value()) &&
                   session.Document()->layers[0].opacity == 0.4 && session.history.UndoCount() == 0,
               "multiple-preview-ticks-remain-outside-history");
        auto closure = PrepareTyped(session, kTargetId, 0.75, 0.6);
        Expect(closure && closure->ChangesDocument() && session.Document()->layers[0].opacity == 0.4 &&
                   session.ApplyPreparedLayerMetadataIfCurrent(closure.value()) &&
                   session.Document().value() == final && session.history.UndoCount() == 1 &&
                   session.history.UndoName() == L"Layer Opacity",
               "typed-final-after-multiple-ticks-publishes-one-opacity-entry");
        session.Undo();
        Expect(session.Document().value() == opening && session.history.UndoCount() == 0 &&
                   session.history.CaptureState().FutureCount() == 1 && session.history.RedoName() == L"Layer Opacity",
               "typed-undo-restores-complete-literal-opening-document-and-entry-count");
        session.Redo();
        Expect(session.Document().value() == final && session.history.UndoCount() == 1 &&
                   session.history.CaptureState().FutureCount() == 0 && session.history.UndoName() == L"Layer Opacity",
               "typed-redo-restores-complete-literal-final-document-and-entry-count");
    }
    {
        DocumentSession session = MakeSession();
        auto expectedShown = DocumentFixture::Copy(session.Document().value());
        expectedShown.layers[0].opacity = 0.35;
        const CanvasDocument final = std::move(expectedShown).Seal();
        auto shown = PreparePreview(session, kTargetId, 0.35);
        Expect(shown && session.ApplyPreparedLayerMetadataIfCurrent(shown.value()),
               "final-equals-shown-fixture-applies-preview");
        auto closure = PrepareTyped(session, kTargetId, 0.75, 0.35);
        Expect(closure && closure->ChangesDocument() && session.ApplyPreparedLayerMetadataIfCurrent(closure.value()) &&
                   session.Document().value() == final && session.history.UndoCount() == 1 &&
                   session.history.UndoName() == L"Layer Opacity",
               "typed-final-equal-to-displayed-value-still-records-opening-to-final");
        session.Undo();
        Expect(session.Document()->layers[0].opacity == 0.75 && session.history.RedoName() == L"Layer Opacity",
               "final-equals-shown-undo-restores-opening-opacity");
    }
}

void ReturnToOpeningPreservesRedo()
{
    DocumentSession session = MakeSession();
    auto seed = PrepareTyped(session, kTargetId, 0.75, 0.5);
    Expect(seed && session.ApplyPreparedLayerMetadataIfCurrent(seed.value()), "redo-fixture-seeds-opacity-history");
    session.Undo();
    const OwnerState afterUndo(session);
    auto preview = PreparePreview(session, kTargetId, 0.25);
    Expect(preview && session.ApplyPreparedLayerMetadataIfCurrent(preview.value()) &&
               session.Document()->layers[0].opacity == 0.25,
           "redo-fixture-shows-temporary-opacity");
    auto close = PrepareTyped(session, kTargetId, 0.75, 0.75);
    Expect(close && close->ChangesDocument() && session.ApplyPreparedLayerMetadataIfCurrent(close.value()) &&
               session.Document()->layers[0].opacity == 0.75 && session.history.UndoCount() == afterUndo.undoCount &&
               session.Document() == afterUndo.document && session.history.IsCurrent(afterUndo.history) &&
               session.history.CaptureState().FutureCount() == afterUndo.redoCount &&
               session.history.CurrentRevision() == afterUndo.revision &&
               session.history.RedoName() == L"Layer Opacity",
           "return-to-opening-restores-opacity-without-clearing-redo-or-changing-revision");
}

void AdmissionAndNoOps()
{
    {
        DocumentSession empty;
        Expect(!empty.PrepareLayerOpacityPreviewInput(kTargetId, 0.4) &&
                   !empty.PrepareTypedLayerOpacityInput(kTargetId, 0.75, 0.4),
               "no-document-rejects-opacity-inputs");
    }
    {
        DocumentSession session = MakeSession(true);
        const OwnerState before(session);
        auto groupPreview = PreparePreview(session, kTargetId, 0.2);
        auto groupTyped = PrepareTyped(session, kTargetId, 0.75, 0.2);
        Expect(groupPreview && groupTyped && !groupPreview->ChangesDocument() && !groupTyped->ChangesDocument() &&
                   session.ApplyPreparedLayerMetadataIfCurrent(groupPreview.value()) &&
                   session.ApplyPreparedLayerMetadataIfCurrent(groupTyped.value()) && SameOwnerState(session, before),
               "group-opacity-preview-and-typed-closure-are-owner-preserving-noops");
    }
    {
        DocumentSession session = MakeSession();
        const Guid missing{{0x72, 0, 0, 9, 0, 0, 0x40, 0, 0x80, 0, 0, 0, 0, 0, 0, 9}};
        session.SelectLayerTarget(kSiblingId, false);
        const OwnerState before(session);
        Expect(!session.PrepareLayerOpacityPreviewInput(missing, 0.2) &&
                   !session.PrepareTypedLayerOpacityInput(missing, 0.75, 0.2) &&
                   !session.PrepareLayerOpacityPreviewInput(kTargetId, 0.2) &&
                   !session.PrepareTypedLayerOpacityInput(kTargetId, 0.75, 0.2),
               "missing-or-nonactive-opacity-target-is-not-admitted");
        Expect(SameOwnerState(session, before), "rejected-opacity-target-admission-preserves-owner-state");
    }
    {
        DocumentSession session = MakeSession();
        const OwnerState before(session);
        const double nan = std::numeric_limits<double>::quiet_NaN();
        Expect(!session.PrepareLayerOpacityPreviewInput(kTargetId, nan) &&
                   !session.PrepareTypedLayerOpacityInput(kTargetId, nan, 0.4) &&
                   !session.PrepareTypedLayerOpacityInput(kTargetId, 0.75, nan) &&
                   !session.PrepareTypedLayerOpacityInput(kTargetId, -0.01, 0.4) &&
                   !session.PrepareTypedLayerOpacityInput(kTargetId, 1.01, 0.4) && SameOwnerState(session, before),
               "nonfinite-opacity-and-invalid-opening-range-are-rejected-with-owner-unchanged");
    }
    {
        DocumentSession session = MakeSession();
        const auto bytes = session.LayerMetadataMemoryBytes(LayerMetadataKind::SetOpacity);
        const auto withName = session.LayerMetadataMemoryBytes(LayerMetadataKind::Rename, 3);
        const auto base = session.history.EditMemoryBytes(session.Document());
        Expect(bytes > base && withName == bytes + 8 * sizeof(wchar_t) * 3,
               "opacity-admission-retains-eight-copy-metadata-reservation");
    }
}

void MovedValuesAndNoopInputs()
{
    {
        DocumentSession session = MakeSession();
        auto source = session.PrepareLayerOpacityPreviewInput(kTargetId, 0.3);
        LayerMetadataInput movedInput(std::move(source.value()));
        auto fromConsumed = PrepareLayerMetadataPublication(std::move(source.value()));
        auto prepared = PrepareLayerMetadataPublication(std::move(movedInput));
        auto fromMovedInput = PrepareLayerMetadataPublication(std::move(movedInput));
        Expect(prepared && !fromConsumed && !fromMovedInput,
               "moving-preview-input-consumes-original-and-moved-from-input");
        auto movedPrepared = std::move(prepared.value());
        Expect(!session.IsPreparedLayerMetadataCurrent(prepared.value()) &&
                   session.ApplyPreparedLayerMetadataIfCurrent(movedPrepared) &&
                   session.Document()->layers[0].opacity == 0.3,
               "moved-preview-result-is-current-and-publishes-once");
    }
    {
        DocumentSession session = MakeSession();
        auto source = session.PrepareLayerOpacityPreviewInput(kTargetId, 0.75);
        LayerMetadataInput movedInput(std::move(source.value()));
        auto fromConsumed = PrepareLayerMetadataPublication(std::move(source.value()));
        auto noOp = PrepareLayerMetadataPublication(std::move(movedInput));
        auto fromMovedInput = PrepareLayerMetadataPublication(std::move(movedInput));
        Expect(noOp && !noOp->ChangesDocument() && !fromConsumed && !fromMovedInput &&
                   session.ApplyPreparedLayerMetadataIfCurrent(noOp.value()),
               "moved-same-value-preview-noop-is-acknowledged-once");
        auto movedNoop = std::move(noOp.value());
        Expect(!session.IsPreparedLayerMetadataCurrent(noOp.value()) &&
                   !session.ApplyPreparedLayerMetadataIfCurrent(noOp.value()) &&
                   !session.ApplyPreparedLayerMetadataIfCurrent(movedNoop) &&
                   session.Document()->layers[0].opacity == 0.75,
               "moved-noop-result-is-consumed-and-cannot-publish-again");
    }
}

void StalePublicationGuards()
{
    {
        DocumentSession session = MakeSession();
        auto older = PreparePreview(session, kTargetId, 0.2);
        auto newer = PreparePreview(session, kTargetId, 0.4);
        Expect(newer && session.ApplyPreparedLayerMetadataIfCurrent(newer.value()) &&
                   session.Document()->layers[0].opacity == 0.4,
               "newer-preview-can-publish-before-an-older-prepared-result");
        const OwnerState shown(session);
        Expect(older && !session.ApplyPreparedLayerMetadataIfCurrent(older.value()) && SameOwnerState(session, shown),
               "older-preview-cannot-overwrite-newer-acknowledged-state");
    }
    {
        DocumentSession session = MakeSession();
        auto prepared = PrepareTyped(session, kTargetId, 0.5, 0.25);
        const auto generation = session.Generation();
        session.history.Begin(L"Literal history-only change", session.Document(), session.ActiveLayerID());
        session.history.End(session.Document(), session.ActiveLayerID());
        Expect(session.Generation() == generation, "history-epoch-fixture-does-not-change-document-generation");
        const OwnerState before(session);
        Expect(prepared && !session.ApplyPreparedLayerMetadataIfCurrent(prepared.value()) &&
                   SameOwnerState(session, before),
               "history-only-epoch-change-refuses-typed-closure-without-mutation");
    }
    {
        DocumentSession first = MakeSession();
        DocumentSession second = MakeSession();
        auto prepared = PreparePreview(first, kTargetId, 0.2);
        Expect(prepared && !second.IsPreparedLayerMetadataCurrent(prepared.value()) &&
                   !second.ApplyPreparedLayerMetadataIfCurrent(prepared.value()),
               "session-epoch-rejects-cross-session-preview-publication");
    }
    {
        DocumentSession session = MakeSession();
        auto prepared = PreparePreview(session, kTargetId, 0.2);
        session.EditForTest(
            [](CanvasDocument& document)
            {
                auto edited = DocumentFixture::Copy(document);
                edited.layers[1].name = L"Generation changed";
                document = std::move(edited).Seal();
            });
        Expect(prepared && !session.IsPreparedLayerMetadataCurrent(prepared.value()) &&
                   !session.ApplyPreparedLayerMetadataIfCurrent(prepared.value()),
               "generation-change-invalidates-opacity-publication");
    }
    {
        DocumentSession session = MakeSession();
        auto prepared = PreparePreview(session, kTargetId, 0.2);
        session.ReplaceDocument(FixtureDocument(), kTargetId);
        Expect(prepared && !session.IsPreparedLayerMetadataCurrent(prepared.value()) &&
                   !session.ApplyPreparedLayerMetadataIfCurrent(prepared.value()),
               "document-replacement-invalidates-opacity-publication");
    }
    {
        DocumentSession session = MakeSession();
        auto prepared = PreparePreview(session, kTargetId, 0.2);
        session.SelectLayerTarget(kSiblingId, false);
        Expect(prepared && !session.IsPreparedLayerMetadataCurrent(prepared.value()) &&
                   !session.ApplyPreparedLayerMetadataIfCurrent(prepared.value()),
               "active-target-change-invalidates-opacity-publication");
    }
}

struct Inject final
{
    LayerMetadataPrepareStage metadataStage = LayerMetadataPrepareStage::BeforeDraft;
    DocumentHistory::PrepareStage historyStage = DocumentHistory::PrepareStage::BeforeSnapshot;
    CancelSource* cancellation = nullptr;
    bool throwBadAlloc = false;
    bool visited = false;
};

void MetadataCheckpoint(void* raw, LayerMetadataPrepareStage stage)
{
    auto& inject = *static_cast<Inject*>(raw);
    if (stage != inject.metadataStage)
        return;
    inject.visited = true;
    if (inject.throwBadAlloc)
        throw std::bad_alloc{};
    (void)inject.cancellation->Cancel();
}

void HistoryCheckpoint(void* raw, DocumentHistory::PrepareStage stage)
{
    auto& inject = *static_cast<Inject*>(raw);
    if (stage != inject.historyStage)
        return;
    inject.visited = true;
    if (inject.throwBadAlloc)
        throw std::bad_alloc{};
    (void)inject.cancellation->Cancel();
}

const char* MetadataStageName(LayerMetadataPrepareStage stage) noexcept
{
    switch (stage)
    {
    case LayerMetadataPrepareStage::BeforeDraft:
        return "before-draft";
    case LayerMetadataPrepareStage::AfterEdit:
        return "after-edit";
    case LayerMetadataPrepareStage::AfterSeal:
        return "after-seal";
    case LayerMetadataPrepareStage::BeforeOpacityOpening:
        return "before-opacity-opening";
    case LayerMetadataPrepareStage::AfterOpacityOpening:
        return "after-opacity-opening";
    }
    return "unknown";
}

const char* HistoryStageName(DocumentHistory::PrepareStage stage) noexcept
{
    switch (stage)
    {
    case DocumentHistory::PrepareStage::BeforeSnapshot:
        return "before-snapshot";
    case DocumentHistory::PrepareStage::AfterSnapshot:
        return "after-snapshot";
    case DocumentHistory::PrepareStage::Entry:
        return "entry";
    case DocumentHistory::PrepareStage::StackGrowth:
        return "stack-growth";
    case DocumentHistory::PrepareStage::TrimIndex:
        return "trim-index";
    case ::DocumentHistory::PrepareStage::AdjustmentSnapshot:
    case ::DocumentHistory::PrepareStage::PendingRecord:
    case ::DocumentHistory::PrepareStage::PendingSnapshot:
    case ::DocumentHistory::PrepareStage::ReplacementReset:
    case ::DocumentHistory::PrepareStage::ReplacementSnapshot:
    case ::DocumentHistory::PrepareStage::RestoreSnapshot:
    case ::DocumentHistory::PrepareStage::SelectionSnapshot:
    case ::DocumentHistory::PrepareStage::StorageIndexBounds:
    case ::DocumentHistory::PrepareStage::StorageIndexClone:
    case ::DocumentHistory::PrepareStage::StorageIndexHistoryEdge:
    case ::DocumentHistory::PrepareStage::StorageIndexLiveEdge:
    case ::DocumentHistory::PrepareStage::StorageIndexMembership:
    case ::DocumentHistory::PrepareStage::StorageIndexResidencyFullScan:
    case ::DocumentHistory::PrepareStage::StorageIndexResidencyRecord:
    default:
        return "unexpected-stage";
    }
}

void PreparationFailures()
{
    {
        DocumentSession session = MakeSession();
        const OwnerState before(session);
        CancelSource cancellation;
        (void)cancellation.Cancel();
        Expect(!PreparePreview(session, kTargetId, 0.25, cancellation.Token()) &&
                   !PrepareTyped(session, kTargetId, 0.75, 0.25, cancellation.Token()) &&
                   SameOwnerState(session, before),
               "pre-cancelled-preview-and-closure-preserve-complete-owner-state");
    }
    for (const auto stage : {LayerMetadataPrepareStage::BeforeDraft, LayerMetadataPrepareStage::AfterEdit,
                             LayerMetadataPrepareStage::AfterSeal})
    {
        for (const bool throwBadAlloc : {false, true})
        {
            DocumentSession session = MakeSession();
            const OwnerState before(session);
            CancelSource cancellation;
            Inject inject{stage, DocumentHistory::PrepareStage::BeforeSnapshot, &cancellation, throwBadAlloc};
            bool caught = false;
            bool returnedNull = false;
            try
            {
                auto prepared =
                    PreparePreview(session, kTargetId, 0.25, cancellation.Token(), {}, {&MetadataCheckpoint, &inject});
                returnedNull = !prepared;
            }
            catch (const std::bad_alloc&)
            {
                caught = true;
            }
            const std::string label =
                std::string("preview-") + MetadataStageName(stage) + (throwBadAlloc ? "-bad-alloc" : "-cancel");
            Expect(inject.visited, (label + "-checkpoint-reached").c_str());
            Expect(throwBadAlloc ? caught : returnedNull, (label + "-propagates-or-returns-no-result").c_str());
            Expect(caught == throwBadAlloc && returnedNull == !throwBadAlloc && SameOwnerState(session, before),
                   (label + "-preserves-complete-owner-state").c_str());
        }
    }
    for (const auto stage : {LayerMetadataPrepareStage::BeforeDraft, LayerMetadataPrepareStage::AfterEdit,
                             LayerMetadataPrepareStage::AfterSeal, LayerMetadataPrepareStage::BeforeOpacityOpening,
                             LayerMetadataPrepareStage::AfterOpacityOpening})
    {
        for (const bool throwBadAlloc : {false, true})
        {
            DocumentSession session = MakeSession();
            const OwnerState before(session);
            CancelSource cancellation;
            Inject inject{stage, DocumentHistory::PrepareStage::BeforeSnapshot, &cancellation, throwBadAlloc};
            const LayerMetadataPrepareObserver observer{&MetadataCheckpoint, &inject};
            bool caught = false;
            bool returnedNull = false;
            try
            {
                auto prepared = PrepareTyped(session, kTargetId, 0.5, 0.25, cancellation.Token(), {}, observer);
                returnedNull = !prepared;
            }
            catch (const std::bad_alloc&)
            {
                caught = true;
            }
            const std::string label =
                std::string("metadata-") + MetadataStageName(stage) + (throwBadAlloc ? "-bad-alloc" : "-cancel");
            Expect(inject.visited, (label + "-checkpoint-reached").c_str());
            Expect(throwBadAlloc ? caught : returnedNull, (label + "-propagates-or-returns-no-result").c_str());
            Expect(caught == throwBadAlloc && returnedNull == !throwBadAlloc && SameOwnerState(session, before),
                   (label + "-preserves-complete-owner-state").c_str());
        }
    }

    constexpr std::array<DocumentHistory::PrepareStage, 5> historyStages{
        DocumentHistory::PrepareStage::BeforeSnapshot, DocumentHistory::PrepareStage::AfterSnapshot,
        DocumentHistory::PrepareStage::Entry, DocumentHistory::PrepareStage::StackGrowth,
        DocumentHistory::PrepareStage::TrimIndex};
    for (const auto stage : historyStages)
    {
        for (const bool throwBadAlloc : {false, true})
        {
            DocumentSession session = MakeSession();
            const OwnerState before(session);
            CancelSource cancellation;
            Inject inject{LayerMetadataPrepareStage::BeforeDraft, stage, &cancellation, throwBadAlloc};
            const DocumentHistory::PrepareObserver observer{&HistoryCheckpoint, &inject};
            bool caught = false;
            bool returnedNull = false;
            try
            {
                auto prepared = PrepareTyped(session, kTargetId, 0.5, 0.25, cancellation.Token(), observer);
                returnedNull = !prepared;
            }
            catch (const std::bad_alloc&)
            {
                caught = true;
            }
            const std::string label =
                std::string("history-") + HistoryStageName(stage) + (throwBadAlloc ? "-bad-alloc" : "-cancel");
            Expect(inject.visited, (label + "-checkpoint-reached").c_str());
            Expect(throwBadAlloc ? caught : returnedNull, (label + "-propagates-or-returns-no-result").c_str());
            Expect(caught == throwBadAlloc && returnedNull == !throwBadAlloc && SameOwnerState(session, before),
                   (label + "-preserves-complete-owner-state").c_str());
        }
    }
}
} // namespace

int RunLayerOpacityWorkTests()
{
    gFailures = 0;
    gAssertions = 0;
    Expect(ValidateDocument(FixtureDocument()) == DocumentStatus::Ok &&
               ValidateDocument(FixtureDocument(true)) == DocumentStatus::Ok,
           "literal-layer-and-group-fixtures-satisfy-document-validation");
    PreviewRepeatAndEscape();
    TypedClosureAndHistory();
    ReturnToOpeningPreservesRedo();
    AdmissionAndNoOps();
    MovedValuesAndNoopInputs();
    StalePublicationGuards();
    PreparationFailures();
    std::printf("ASSERTIONS layer-opacity.count=%d\n", gAssertions);
    return gFailures;
}
