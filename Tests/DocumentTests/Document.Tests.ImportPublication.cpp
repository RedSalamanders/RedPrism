#include "Document.Tests.ImportPublication.h"

#include "../Support/Support.Tests.DocumentFixture.h"
#include "BrushStroke.h"
#include "ImageImportHistory.h"
#include "WarpStroke.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <new>
#include <optional>
#include <string_view>
#include <thread>
#include <utility>

namespace internal
{
namespace
{
int gFailures = 0;
int gAssertions = 0;

void Expect(bool condition, const char* name) noexcept
{
    ++gAssertions;
    std::printf("%s import-publication.%s\n", condition ? "PASS" : "FAIL", name);
    if (!condition)
        ++gFailures;
}

CanvasDocument PixelDocument(const wchar_t* name, std::array<std::uint8_t, 4> bgra)
{
    DocumentFixture document;
    document.id = Guid::Generate();
    document.width = 1;
    document.height = 1;
    ImageLayer layer = ImageLayer::Blank(name, Vec2{1, 1});
    const Raster image = Raster::Make(1, 1, RasterFormat::Bgra8888, bgra.data(), bgra.size());
    layer.asset = RasterAsset{image, image, name};
    document.layers.push_back(std::move(layer));
    return std::move(document).Seal();
}

CanvasDocument WithPixel(const CanvasDocument& source, std::array<std::uint8_t, 4> bgra)
{
    DocumentFixture result = DocumentFixture::Copy(source);
    const Raster image = Raster::Make(1, 1, RasterFormat::Bgra8888, bgra.data(), bgra.size());
    result.layers.front().asset.value().image = image;
    result.layers.front().asset.value().thumbnail = image;
    return std::move(result).Seal();
}

CanvasDocument WithSecondImage(const CanvasDocument& baseline, std::array<std::uint8_t, 4> bgra)
{
    DocumentFixture result = DocumentFixture::Copy(baseline);
    ImageLayer layer = ImageLayer::Blank(L"Imported image", Vec2{1, 1});
    const Raster image = Raster::Make(1, 1, RasterFormat::Bgra8888, bgra.data(), bgra.size());
    layer.asset = RasterAsset{image, image, layer.name};
    result.layers.push_back(std::move(layer));
    return std::move(result).Seal();
}

bool HasPixel(const CanvasDocument& document, const Guid& layerID, std::array<std::uint8_t, 4> expected) noexcept
{
    const auto layer = std::find_if(document.layers.begin(), document.layers.end(),
                                    [&](const ImageLayer& item) { return item.id == layerID; });
    if (layer == document.layers.end() || !layer->asset)
        return false;
    const Raster& image = layer->asset.value().image;
    const std::uint8_t* pixels = image.Data();
    return image.Width() == 1 && image.Height() == 1 && pixels != nullptr &&
           std::equal(expected.begin(), expected.end(), pixels);
}

bool HasActivePixel(const DocumentSession& session, std::array<std::uint8_t, 4> expected) noexcept
{
    return session.Document().has_value() && session.ActiveLayerID().has_value() &&
           HasPixel(session.Document().value(), session.ActiveLayerID().value(), expected);
}

ImageImportBatchResult NewImageBatch(const CanvasDocument& baseline)
{
    ImageImportBatchResult result;
    result.createdDocument = baseline;
    result.createdActiveLayerID = baseline.layers.front().id;
    result.historyName = L"Import Image";
    return result;
}

ImageImportBatchResult NewImageBatch(const CanvasDocument& baseline, const CanvasDocument& after)
{
    ImageImportBatchResult result = NewImageBatch(baseline);
    result.importedDocument = after;
    result.importedActiveLayerID = after.layers.back().id;
    return result;
}

ImageImportBatchResult ExistingImageBatch(const CanvasDocument& after, const Guid& active)
{
    ImageImportBatchResult result;
    result.importedDocument = after;
    result.importedActiveLayerID = active;
    result.historyName = L"Import Image";
    return result;
}

std::optional<PreparedImageImport> Prepare(ImageImportBatchResult result, ImageImportAuthority authority,
                                           std::optional<CanvasDocument> source, std::optional<Guid> sourceActive,
                                           DocumentHistory::StateView state, CancelToken cancellation = {})
{
    return DocumentSession::PrepareImageImportPublication(std::move(authority), std::move(source),
                                                          std::move(sourceActive), std::move(state), std::move(result),
                                                          cancellation);
}

std::optional<PreparedImageImport> PrepareCurrent(DocumentSession& session, ImageImportBatchResult result,
                                                  CancelToken cancellation = {})
{
    return Prepare(std::move(result), session.CaptureImageImportAuthority(), session.Document(),
                   session.ActiveLayerID(), session.history.CaptureState(), cancellation);
}

void InstallOldHistory(DocumentSession& session)
{
    CanvasDocument first = PixelDocument(L"Old document", {1, 2, 3, 255});
    session.ReplaceDocument(first, first.layers.front().id);
    CanvasDocument second = WithPixel(first, {4, 5, 6, 255});
    static_cast<void>(session.ApplyImportedDocument(second, first.layers.front().id, L"Old import one"));
    CanvasDocument third = WithPixel(second, {7, 8, 9, 255});
    static_cast<void>(session.ApplyImportedDocument(third, first.layers.front().id, L"Old import two"));
    session.Undo();
    session.history.MarkSaved();
}

struct SessionFixture final
{
    DocumentSession session;
    SessionFixture()
    {
        InstallOldHistory(session);
    }
};

void ExpectStaleRejected(const char* suffix, void (*mutate)(DocumentSession&))
{
    SessionFixture fixture;
    if (std::string_view(suffix) == "active-layer")
    {
        fixture.session.AddBlankLayer();
        fixture.session.SelectLayer(fixture.session.Document().value().layers.front().id);
    }
    if (std::string_view(suffix) == "mask-selection")
    {
        fixture.session.AddLayerMask();
        fixture.session.SelectLayerTarget(fixture.session.ActiveLayerID().value(), true);
    }
    auto prepared = PrepareCurrent(fixture.session,
                                   ExistingImageBatch(WithPixel(fixture.session.Document().value(), {17, 34, 51, 255}),
                                                      fixture.session.ActiveLayerID().value()));
    mutate(fixture.session);
    const auto document = fixture.session.Document();
    const auto active = fixture.session.ActiveLayerID();
    const auto crop = fixture.session.CropFrame();
    const auto mask = fixture.session.IsMaskSelected();
    const auto revision = fixture.session.history.CurrentRevision();
    const auto undoCount = fixture.session.history.UndoCount();
    const auto redoName = fixture.session.history.RedoName();
    const bool refused = prepared.has_value() && !fixture.session.ApplyPreparedImageImportIfCurrent(prepared.value());
    const bool retained = fixture.session.Document() == document && fixture.session.ActiveLayerID() == active &&
                          fixture.session.CropFrame() == crop && fixture.session.IsMaskSelected() == mask &&
                          fixture.session.history.CurrentRevision() == revision &&
                          fixture.session.history.UndoCount() == undoCount &&
                          fixture.session.history.RedoName() == redoName;
    char name[96]{};
    std::snprintf(name, sizeof(name), "stale.%s.refused", suffix);
    Expect(refused, name);
    std::snprintf(name, sizeof(name), "stale.%s.document-retained", suffix);
    Expect(retained, name);
}

void ChangeEpochOnly(DocumentSession& session)
{
    const auto source = session.Document();
    const auto active = session.ActiveLayerID();
    session.history.Begin(L"Epoch only", source, active);
    session.history.End(source, active);
}

void ChangeActive(DocumentSession& session)
{
    session.SelectLayer(session.Document().value().layers.back().id);
}

void ChangeMaskSelection(DocumentSession& session)
{
    const Guid active = session.ActiveLayerID().value();
    session.SelectLayerTarget(active, false);
}

void ChangeCrop(DocumentSession& session)
{
    session.SetCropFrame(CropRect{0, 0, 1, 1});
}

void ChangeSerial(DocumentSession& session)
{
    CanvasDocument replacement = PixelDocument(L"Serial replacement", {90, 91, 92, 255});
    session.ReplaceDocument(std::move(replacement), std::nullopt);
}

void ChangeGeneration(DocumentSession& session)
{
    session.EditForTest([](CanvasDocument&) {});
}

enum class DraftContext
{
    Color,
    Adjustment,
    Transform,
    Floating,
    Lasso,
    Shape,
    Brush,
    Warp,
    Gradient,
    PixelMove,
    Outline
};

class OwnerCoverage final : public BrushCoverageEngine
{
  public:
    const std::thread::id owner = std::this_thread::get_id();
    unsigned int ends = 0;
    bool wrongThread = false;
    bool Render(std::span<const BrushKernelTile> tiles, std::span<const BrushSegment> settled,
                std::span<const BrushSegment> tail, const BrushKernelParams& settings) noexcept override
    {
        return CpuBrushCoverage::Shared().Render(tiles, settled, tail, settings);
    }
    void EndStroke() noexcept override
    {
        ++ends;
        wrongThread = wrongThread || std::this_thread::get_id() != owner;
    }
};

bool DraftOpen(const DocumentSession& session, DraftContext context) noexcept
{
    switch (context)
    {
    case DraftContext::Color:
        return session.HasColorEdit();
    case DraftContext::Adjustment:
        return session.HasColorEdit() && session.AdjustmentEditingID().has_value();
    case DraftContext::Transform:
        return session.PendingTransformEdit().has_value();
    case DraftContext::Floating:
        return session.PendingTransformEdit() && session.PendingTransformEdit()->floating;
    case DraftContext::Lasso:
        return session.PendingLasso().has_value();
    case DraftContext::Shape:
        return session.PendingShape().has_value();
    case DraftContext::Brush:
        return session.PendingBrush() != nullptr;
    case DraftContext::Warp:
        return session.PendingWarp() != nullptr;
    case DraftContext::Gradient:
        return session.PendingGradient() != nullptr;
    case DraftContext::PixelMove:
        return session.PendingPixelMove() != nullptr;
    case DraftContext::Outline:
        return session.IsMovingSelection();
    }
    return false;
}

bool OpenDraft(DocumentSession& session, DraftContext context, OwnerCoverage& coverage)
{
    switch (context)
    {
    case DraftContext::Color:
        session.BeginColorEdit(FilterKind::Exposure, true);
        break;
    case DraftContext::Adjustment:
    {
        const auto image = session.ActiveLayer()->asset->image;
        session.AddAdjustment(AdjustmentKind::Exposure);
        if (!session.BeginAdjustmentEditing(session.ActiveLayerID().value(), image, true))
            return false;
        break;
    }
    case DraftContext::Transform:
        session.BeginTransform();
        break;
    case DraftContext::Floating:
        session.SelectAll();
        session.BeginSelectionTransform();
        break;
    case DraftContext::Lasso:
        session.BeginLasso({0, 0}, SelectionMode::New);
        session.ExtendLasso({1, 1});
        break;
    case DraftContext::Shape:
        session.BeginShape({0, 0});
        session.DragShape({1, 1}, false, false);
        break;
    case DraftContext::Brush:
        session.SetBrushEngine(&coverage);
        if (!session.BeginBrush({0.5, 0.5}))
            return false;
        break;
    case DraftContext::Warp:
        if (!session.BeginWarp({0.5, 0.5}))
            return false;
        break;
    case DraftContext::Gradient:
        if (!session.BeginGradient({0, 0}))
            return false;
        session.MoveGradient(Vec2{0, 0}, Vec2{1, 0});
        break;
    case DraftContext::PixelMove:
        session.SelectAll();
        if (!session.BeginPixelMove())
            return false;
        break;
    case DraftContext::Outline:
        session.SelectAll();
        if (!session.BeginSelectionMove())
            return false;
        break;
    }
    return DraftOpen(session, context);
}

void CheckLiveDraftReplacement()
{
    constexpr std::array<std::uint8_t, 4> kImportedPixel{17, 34, 51, 255};
    for (const auto context :
         {DraftContext::Color, DraftContext::Adjustment, DraftContext::Transform, DraftContext::Floating,
          DraftContext::Lasso, DraftContext::Shape, DraftContext::Brush, DraftContext::Warp, DraftContext::Gradient,
          DraftContext::PixelMove, DraftContext::Outline})
    {
        OwnerCoverage coverage;
        SessionFixture fixture;
        auto& session = fixture.session;
        Expect(OpenDraft(session, context, coverage), "live-context-fixture-opens-real-draft");
        const auto original = session.Document();
        const auto active = session.ActiveLayerID();
        const auto revision = session.history.CurrentRevision();
        const auto depth = session.history.CaptureState().Depth();
        const auto undo = session.history.UndoCount();
        const auto redo = session.history.RedoName();
        const auto serial = session.DocumentSerial();
        const auto authority = session.CaptureImageImportAuthority();
        Expect(session.CanApplyImageImport(authority) &&
                   !session.CanApplyHistoryRestore(session.CaptureHistoryRestoreAuthority()),
               "live-file-authority-does-not-enable-unsettled-undo");
        Expect(session.ImageImportRetirementMemoryBytes() >= session.HistoryRestoreRetirementMemoryBytes(),
               "live-retirement-declares-all-base-and-pending-owners");
        const CanvasDocument replacement = PixelDocument(L"Replacement", kImportedPixel);
        for (const bool stop : {false, true})
        {
            CancelSource cancellation;
            struct Refusal
            {
                CancelSource& cancellation;
                bool stop;
                unsigned int hits = 0;
            } fault{cancellation, stop};
            const DocumentHistory::PrepareObserver observer{[](void* opaque, DocumentHistory::PrepareStage stage)
                                                            {
                                                                auto& fault = *static_cast<Refusal*>(opaque);
                                                                if (stage ==
                                                                    DocumentHistory::PrepareStage::ReplacementSnapshot)
                                                                {
                                                                    ++fault.hits;
                                                                    if (fault.stop)
                                                                        static_cast<void>(fault.cancellation.Cancel());
                                                                    else
                                                                        throw std::bad_alloc{};
                                                                }
                                                            },
                                                            &fault};
            bool threw = false;
            std::optional<PreparedImageImport> refused;
            try
            {
                refused = DocumentSession::PrepareDocumentReplacementPublication(
                    authority, session.Document(), session.history.CaptureState(), replacement,
                    replacement.layers.front().id, cancellation.Token(), observer);
            }
            catch (const std::bad_alloc&)
            {
                threw = true;
            }
            Expect(!refused && threw == !stop && fault.hits == 1 && session.Document() == original &&
                       session.ActiveLayerID() == active && session.DocumentSerial() == serial &&
                       session.history.CurrentRevision() == revision && session.history.UndoCount() == undo &&
                       session.history.RedoName() == redo && session.history.CaptureState().Depth() == depth &&
                       DraftOpen(session, context) && coverage.ends == 0,
                   "live-refusal-preserves-draft-pixels-target-history-and-engine");
        }
        std::weak_ptr<BrushStroke> preview;
        if (context == DraftContext::Gradient)
            preview = session.PendingGradient()->raster;
        if (context == DraftContext::PixelMove)
            preview = session.PendingPixelMove()->raster;
        auto prepared = DocumentSession::PrepareDocumentReplacementPublication(
            authority, session.Document(), session.history.CaptureState(), replacement, replacement.layers.front().id);
        const bool applied = prepared && session.ApplyPreparedImageImportIfCurrent(prepared.value());
        Expect(applied && session.DocumentSerial() != serial && HasActivePixel(session, kImportedPixel) &&
                   !session.history.IsModified() && !session.history.CanUndo() && !session.history.CanRedo() &&
                   session.history.CaptureState().Depth() == 0,
               "live-replacement-installs-only-clean-new-literal-baseline");
        Expect(!session.HasColorEdit() && !session.AdjustmentEditingID() && !session.PendingTransformEdit() &&
                   !session.PendingLasso() && !session.PendingShape() && !session.PendingBrush() &&
                   !session.PendingWarp() && !session.PendingGradient() && !session.PendingPixelMove() &&
                   !session.IsMovingSelection(),
               "successful-replacement-clears-all-discarded-drafts");
        if (context == DraftContext::Gradient || context == DraftContext::PixelMove)
            Expect(!preview.expired(), "retired-preview-owner-stays-in-publication-aggregate");
        std::thread release([owned = std::move(prepared)]() mutable { owned.reset(); });
        release.join();
        if (context == DraftContext::Gradient || context == DraftContext::PixelMove)
            Expect(preview.expired(), "worker-releases-complete-retired-preview-owner");
        Expect(!coverage.wrongThread && coverage.ends == (context == DraftContext::Brush ? 1U : 0U),
               "coverage-engine-closes-once-on-owner-before-worker-destruction");
        std::printf("import-publication.live-context=%u\n", static_cast<unsigned int>(context));
    }
    {
        SessionFixture fixture;
        auto& session = fixture.session;
        const auto original = session.Document();
        const auto active = session.ActiveLayerID();
        const auto revision = session.history.CurrentRevision();
        const auto count = session.history.UndoCount();
        session.BeginEdit(L"Outer import transaction");
        const CanvasDocument after = WithSecondImage(original.value(), kImportedPixel);
        auto prepared = PrepareCurrent(session, ExistingImageBatch(after, after.layers.back().id));
        Expect(prepared && session.ApplyPreparedImageImportIfCurrent(prepared.value()) &&
                   session.history.CurrentRevision() == revision && session.history.UndoCount() == count &&
                   session.history.CaptureState().Depth() == 1 && HasActivePixel(session, kImportedPixel),
               "existing-import-retains-outer-opening-depth-name-and-revision");
        session.EndEdit();
        Expect(session.history.UndoCount() == count + 1 && session.history.UndoName() == L"Outer import transaction",
               "outer-end-records-one-import-entry-under-original-name");
        session.Undo();
        Expect(session.Document() == original && session.ActiveLayerID() == active &&
                   session.history.CurrentRevision() == revision,
               "nested-import-undo-restores-exact-outer-opening");
        session.Redo();
        Expect(session.Document() == std::optional(after) && HasActivePixel(session, kImportedPixel),
               "nested-import-redo-restores-independent-literal-import");
    }
}
} // namespace

void CheckImportResidencyMoveAndNoOp()
{
    auto owner = std::make_unique<SessionFixture>();
    auto& session = owner->session;
    const auto before = session.Document();
    const auto history = session.history.CaptureState();
    auto prepared = PrepareCurrent(session, ImageImportBatchResult{});
    if (!prepared)
    {
        Expect(false, "residency-no-op-prepared");
        return;
    }
    auto destination = std::move(prepared.value());
    DocumentHistory::ResidencyPermit permit;
    Expect(prepared->TryAcquireHistoryResidency(permit) == DocumentHistory::ResidencyValidation::Invalid &&
               !prepared->RefreshHistoryResidency() && !session.ApplyPreparedImageImportIfCurrent(prepared.value()),
           "residency-moved-no-op-has-no-authority");
    Expect(destination.TryAcquireHistoryResidency(permit) == DocumentHistory::ResidencyValidation::Ready &&
               destination.RefreshHistoryResidency() && session.ApplyPreparedImageImportIfCurrent(destination),
           "residency-no-op-destination-acknowledges-once");
    Expect(destination.TryAcquireHistoryResidency(permit) == DocumentHistory::ResidencyValidation::Invalid &&
               !destination.RefreshHistoryResidency() && !session.ApplyPreparedImageImportIfCurrent(destination) &&
               session.Document() == before && session.history.IsCurrent(history),
           "residency-applied-no-op-refuses-reuse-and-preserves-lineage");
    auto from = PrepareCurrent(session, ImageImportBatchResult{});
    auto to = PrepareCurrent(session, ImageImportBatchResult{});
    if (!from || !to)
    {
        Expect(false, "residency-no-op-assignment-prepared");
        return;
    }
    to.value() = std::move(from.value());
    Expect(from->TryAcquireHistoryResidency(permit) == DocumentHistory::ResidencyValidation::Invalid &&
               !session.ApplyPreparedImageImportIfCurrent(from.value()) &&
               session.ApplyPreparedImageImportIfCurrent(to.value()) &&
               !session.ApplyPreparedImageImportIfCurrent(to.value()),
           "residency-no-op-assignment-consumes-original-authority");
}

void CheckImportResidencyLateCache(bool allocationFailure, bool cancel)
{
    auto owner = std::make_unique<DocumentSession>(8);
    auto& session = *owner;
    const auto current = PixelDocument(L"Current dense image", {11, 12, 13, 255});
    session.ReplaceDocument(current, current.layers.front().id);
    const Raster leaf =
        Raster::Make(1, 1, RasterFormat::Bgra8888, std::array<std::uint8_t, 4>{21, 22, 23, 255}.data(), 4);
    const Raster late = Raster::MakeSparse(2, 1, RasterFormat::Bgra8888, {}, 0, 0, {{0, 0, leaf}}, 1);
    auto historical = DocumentFixture::Copy(current);
    historical.layers.front().asset = RasterAsset{late, {}, L"Literal history-only sparse image"};
    auto baseline = DocumentHistory::PrepareEdit(session.history.CaptureState(), std::move(historical).Seal(),
                                                 session.ActiveLayerID(), session.Document(), session.ActiveLayerID(),
                                                 L"Historical sparse cache");
    if (!baseline || !session.history.Publish(baseline.value()))
    {
        Expect(false, "residency-literal-baseline-prepared");
        return;
    }
    const auto original = session.Document();
    const auto history = session.history.CaptureState();
    const Guid originalRevision = session.history.CurrentRevision();
    const auto generation = session.Generation();
    session.history.MarkSaved();
    auto publication = PrepareCurrent(
        session, ExistingImageBatch(WithPixel(current, {31, 32, 33, 255}), session.ActiveLayerID().value()));
    Expect(publication && session.history.RetainedBytes(original) == 4 && !late.IsMaterialized(),
           "residency-literal-four-byte-sparse-baseline");
    if (!publication)
        return;
    const auto* bytes = late.Data();
    Expect(bytes && late.ByteSize() == 8 && session.history.RetainedBytes(original) == 12,
           "residency-late-eight-byte-cache-plus-four-byte-leaf");
    DocumentHistory::ResidencyPermit permit;
    Expect(publication->TryAcquireHistoryResidency(permit) == DocumentHistory::ResidencyValidation::Refresh &&
               !session.ApplyPreparedImageImportIfCurrent(publication.value()) && session.Document() == original &&
               session.history.IsCurrent(history),
           "residency-late-growth-requires-refresh-before-any-publication");
    CancelSource stop;
    if (cancel)
        (void)stop.Cancel();
    unsigned int faultHits = 0;
    const DocumentHistory::PrepareObserver fault{[](void* context, DocumentHistory::PrepareStage stage)
                                                 {
                                                     if (stage == DocumentHistory::PrepareStage::StorageIndexClone)
                                                     {
                                                         ++*static_cast<unsigned int*>(context);
                                                         throw std::bad_alloc{};
                                                     }
                                                 },
                                                 &faultHits};
    bool refreshed = false;
    bool threw = false;
    try
    {
        refreshed = publication->RefreshHistoryResidency(
            stop.Token(), allocationFailure ? fault : DocumentHistory::PrepareObserver{});
    }
    catch (const std::bad_alloc&)
    {
        threw = true;
    }
    if (allocationFailure || cancel)
    {
        Expect(!refreshed && threw == allocationFailure && faultHits == (allocationFailure ? 1U : 0U),
               "residency-refresh-fault-or-cancel-refuses-original-candidate");
        Expect(session.Document() == original && session.Generation() == generation &&
                   session.history.IsCurrent(history) && session.history.CurrentRevision() == originalRevision &&
                   !session.history.IsModified(),
               "residency-refusal-preserves-document-generation-user-and-saved-revision");
        refreshed = publication->RefreshHistoryResidency(); // Same pixel/history candidate, no recapture or new edit.
    }
    Expect(refreshed &&
               publication->TryAcquireHistoryResidency(permit) == DocumentHistory::ResidencyValidation::Ready &&
               session.ApplyPreparedImageImportIfCurrent(publication.value()),
           "residency-refreshed-original-import-publishes-once");
    permit.Reset();
    Expect(HasActivePixel(session, {31, 32, 33, 255}) && session.history.UndoCount() == 1 &&
               session.history.UndoName() == L"Import Image" &&
               session.history.RetainedBytes(session.Document()) == 4 && session.history.IsModified(),
           "residency-import-trims-old-cache-keeps-one-entry-and-original-saved-marker");
    const Guid committedRevision = session.history.CurrentRevision();
    session.Undo();
    Expect(session.Document() == original && session.history.CurrentRevision() == originalRevision &&
               !session.history.IsModified(),
           "residency-import-undo-restores-original-document-and-saved-revision");
    session.Redo();
    Expect(HasActivePixel(session, {31, 32, 33, 255}) && session.history.CurrentRevision() == committedRevision &&
               session.history.IsModified() && !session.ApplyPreparedImageImportIfCurrent(publication.value()),
           "residency-import-redo-keeps-original-prepared-revision-and-refuses-reuse");
}

void CheckImportPermitMoves()
{
    auto owner = std::make_unique<DocumentSession>();
    auto& session = *owner;
    const auto document = PixelDocument(L"Moved saved canvas", {81, 82, 83, 255});
    auto prepared = DocumentSession::PrepareDocumentLifecyclePublication(session.CaptureImageImportAuthority(),
                                                                         session.history.CaptureState(), document,
                                                                         document.layers.front().id, L"New Canvas");
    if (!prepared)
    {
        Expect(false, "residency-permit-move-prepared");
        return;
    }
    DocumentHistory::ResidencyPermit permit;
    Expect(prepared->TryAcquireHistoryResidency(permit) == DocumentHistory::ResidencyValidation::Ready,
           "residency-replacement-permit-held-before-move");
    auto destination = std::move(prepared.value());
    {
        auto lease = Raster::TryResidencyLease();
        Expect(lease.Held(), "residency-replacement-move-releases-old-borrowed-permit");
    }
    Expect(prepared->TryAcquireHistoryResidency(permit) == DocumentHistory::ResidencyValidation::Invalid &&
               !session.ApplyPreparedImageImportIfCurrent(prepared.value()) &&
               !session.ApplyPreparedImageImportIfCurrent(destination) && !session.Document(),
           "residency-moved-replacement-needs-fresh-permit-before-publication");
    Expect(destination.TryAcquireHistoryResidency(permit) == DocumentHistory::ResidencyValidation::Ready &&
               session.ApplyPreparedImageImportIfCurrent(destination) &&
               session.Document() == std::optional(document) && session.history.UndoCount() == 1 &&
               !session.history.IsModified(),
           "residency-moved-replacement-publishes-once-with-fresh-permit");
    permit.Reset();
    auto consumed = std::move(destination);
    Expect(consumed.TryAcquireHistoryResidency(permit) == DocumentHistory::ResidencyValidation::Invalid &&
               !consumed.RefreshHistoryResidency() && !session.ApplyPreparedImageImportIfCurrent(consumed),
           "residency-moving-applied-replacement-cannot-revive-authority");
}

void CheckReplacementLateResidency()
{
    const auto dense = PixelDocument(L"Final replacement", {71, 72, 73, 255});
    const Raster leaf =
        Raster::Make(1, 1, RasterFormat::Bgra8888, std::array<std::uint8_t, 4>{61, 62, 63, 255}.data(), 4);
    const Raster late = Raster::MakeSparse(2, 1, RasterFormat::Bgra8888, {}, 0, 0, {{0, 0, leaf}}, 1);
    auto baselineDraft = DocumentFixture::Copy(dense);
    baselineDraft.layers.front().asset = RasterAsset{late, {}, L"Sparse imported baseline"};
    const std::optional<CanvasDocument> baseline = std::move(baselineDraft).Seal();
    const std::optional<CanvasDocument> after = dense;
    DocumentHistory history{100, 8};
    auto replacement =
        DocumentHistory::PrepareReplacement(history.CaptureState(), baseline, dense.layers.front().id, after,
                                            dense.layers.front().id, L"Additional imported image");
    if (!replacement)
    {
        Expect(false, "residency-replacement-late-candidate-prepared");
        return;
    }
    const Guid savedBaseline = replacement->BaselineRevision();
    const auto original = history.CaptureState();
    (void)late.Data();
    DocumentHistory::ResidencyPermit permit;
    Expect(replacement->BaselineRevision() == savedBaseline && late.ByteSize() == 8 &&
               permit.TryAcquire(replacement.value()) == DocumentHistory::ResidencyValidation::Refresh &&
               !history.PublishReplacement(replacement.value()) && history.IsCurrent(original),
           "residency-replacement-late-cache-refuses-unrefreshed-original-lineage");
    CancelSource stop;
    (void)stop.Cancel();
    Expect(!DocumentHistory::RefreshPreparedResidency(replacement.value(), after, stop.Token()) &&
               replacement->BaselineRevision() == savedBaseline && history.IsCurrent(original),
           "residency-replacement-cancel-preserves-saved-baseline-and-live-history");
    Expect(DocumentHistory::RefreshPreparedResidency(replacement.value(), after) &&
               replacement->BaselineRevision() == savedBaseline &&
               permit.TryAcquire(replacement.value()) == DocumentHistory::ResidencyValidation::Ready &&
               history.PublishReplacement(replacement.value()),
           "residency-replacement-refresh-preserves-original-saved-baseline");
    permit.Reset();
    Expect(history.UndoCount() == 0 && !history.CanRedo() && history.RetainedBytes(after) == 0 &&
               history.CurrentRevision() != savedBaseline && history.IsModified() &&
               !history.PublishReplacement(replacement.value()),
           "residency-replacement-trim-drops-oversized-baseline-entry-without-new-revision");

    auto owner = std::make_unique<DocumentSession>();
    auto& session = *owner;
    auto newCanvas = DocumentHistory::PrepareSavedReplacement(session.history.CaptureState(), after,
                                                              dense.layers.front().id, L"New Canvas");
    if (!newCanvas)
    {
        Expect(false, "residency-new-canvas-saved-candidate-prepared");
        return;
    }
    const Guid savedFinal = newCanvas->BaselineRevision();
    const Raster secondLate = Raster::MakeSparse(2, 1, RasterFormat::Bgra8888, {}, 0, 0, {{0, 0, leaf}}, 2);
    (void)secondLate.Data();
    Expect(permit.TryAcquire(newCanvas.value()) == DocumentHistory::ResidencyValidation::Refresh &&
               DocumentHistory::RefreshPreparedResidency(newCanvas.value(), after) &&
               newCanvas->BaselineRevision() == savedFinal &&
               permit.TryAcquire(newCanvas.value()) == DocumentHistory::ResidencyValidation::Ready &&
               session.history.PublishReplacement(newCanvas.value()),
           "residency-new-canvas-refresh-keeps-original-final-saved-revision");
    permit.Reset();
    Expect(session.history.CurrentRevision() == savedFinal && !session.history.IsModified() &&
               session.history.UndoCount() == 1 && session.history.UndoName() == L"New Canvas",
           "residency-new-canvas-remains-saved-with-one-entry-after-unrelated-growth");
}

void CheckReplacementResidency()
{
    auto owner = std::make_unique<DocumentSession>();
    auto& session = *owner;
    auto replacement = PixelDocument(L"New saved canvas", {41, 42, 43, 255});
    auto prepared = DocumentSession::PrepareDocumentLifecyclePublication(session.CaptureImageImportAuthority(),
                                                                         session.history.CaptureState(), replacement,
                                                                         replacement.layers.front().id, L"New Canvas");
    DocumentHistory::ResidencyPermit permit;
    Expect(prepared && prepared->TryAcquireHistoryResidency(permit) == DocumentHistory::ResidencyValidation::Ready &&
               session.ApplyPreparedImageImportIfCurrent(prepared.value()),
           "residency-saved-replacement-uses-underlying-history-permit");
    permit.Reset();
    Expect(session.Document() == std::optional(replacement) && session.history.UndoCount() == 1 &&
               session.history.UndoName() == L"New Canvas" && !session.history.IsModified(),
           "residency-saved-replacement-keeps-new-canvas-entry-and-saved-lineage");
    session.Undo();
    auto closing = DocumentSession::PrepareDocumentLifecyclePublication(
        session.CaptureImageImportAuthority(), session.history.CaptureState(), std::nullopt, std::nullopt, {});
    Expect(closing && closing->TryAcquireHistoryResidency(permit) == DocumentHistory::ResidencyValidation::Ready &&
               session.ApplyPreparedImageImportIfCurrent(closing.value()) && session.history.CanRedo(),
           "residency-absent-close-no-op-preserves-redo");
    Expect(closing && closing->TryAcquireHistoryResidency(permit) == DocumentHistory::ResidencyValidation::Invalid &&
               !closing->RefreshHistoryResidency() && !session.ApplyPreparedImageImportIfCurrent(closing.value()),
           "residency-absent-close-no-op-is-single-use");
}

int RunImportPublicationTests()
{
    gFailures = 0;
    gAssertions = 0;
    constexpr std::array<std::uint8_t, 4> kImportedPixel{17, 34, 51, 255};

    {
        auto fixtureOwner = std::make_unique<SessionFixture>();
        auto& fixture = *fixtureOwner;
        Expect(fixture.session.history.CanUndo() && fixture.session.history.CanRedo(),
               "new-single-image-fixture-starts-with-old-undo-and-redo");
        const CanvasDocument baseline = PixelDocument(L"Single imported image", kImportedPixel);
        auto prepared = PrepareCurrent(fixture.session, NewImageBatch(baseline));
        const bool preparedForReplacement = prepared.has_value() && prepared.value().ReplacesDocument();
        const bool applied =
            prepared.has_value() && fixture.session.ApplyPreparedImageImportIfCurrent(prepared.value());
        Expect(preparedForReplacement && applied, "new-single-image-publication-applies");
        Expect(fixture.session.Document().has_value() && fixture.session.Document().value().layers.size() == 1 &&
                   HasActivePixel(fixture.session, kImportedPixel),
               "new-single-image-literal-active-pixel");
        Expect(!fixture.session.history.IsModified() && !fixture.session.history.CanUndo() &&
                   !fixture.session.history.CanRedo() && fixture.session.history.UndoCount() == 0,
               "new-single-image-is-clean-baseline-with-empty-history");
    }

    {
        auto fixtureOwner = std::make_unique<SessionFixture>();
        auto& fixture = *fixtureOwner;
        const CanvasDocument baseline = PixelDocument(L"Import baseline", {3, 4, 5, 255});
        const CanvasDocument after = WithSecondImage(baseline, kImportedPixel);
        auto prepared = PrepareCurrent(fixture.session, NewImageBatch(baseline, after));
        const bool applied =
            prepared.has_value() && fixture.session.ApplyPreparedImageImportIfCurrent(prepared.value());
        Expect(applied && fixture.session.Document().has_value() &&
                   fixture.session.Document().value().layers.size() == 2 &&
                   fixture.session.ActiveLayerID() == std::optional(after.layers.back().id),
               "baseline-plus-second-image-applies-final-active-layer");
        Expect(fixture.session.history.IsModified() && fixture.session.history.UndoCount() == 1 &&
                   fixture.session.history.CanUndo() && !fixture.session.history.CanRedo() &&
                   fixture.session.history.UndoName() == L"Import Image",
               "baseline-plus-second-image-is-exactly-one-import-entry");

        fixture.session.Undo();
        Expect(!fixture.session.history.IsModified() && fixture.session.history.CanRedo() &&
                   fixture.session.Document().has_value() && fixture.session.Document().value().layers.size() == 1 &&
                   HasActivePixel(fixture.session, {3, 4, 5, 255}),
               "undo-returns-to-clean-baseline-pixels");
        fixture.session.Redo();
        Expect(fixture.session.history.IsModified() && !fixture.session.history.CanRedo() &&
                   fixture.session.Document().has_value() && fixture.session.Document().value().layers.size() == 2 &&
                   HasActivePixel(fixture.session, kImportedPixel),
               "redo-returns-to-dirty-imported-active-pixel");
    }

    {
        auto fixtureOwner = std::make_unique<SessionFixture>();
        auto& fixture = *fixtureOwner;
        const Guid documentID = fixture.session.Document().value().id;
        const std::uint64_t serial = fixture.session.DocumentSerial();
        const CanvasDocument after = WithPixel(fixture.session.Document().value(), kImportedPixel);
        fixture.session.SetCropFrame(CropRect{0.25, 0.5, 0.75, 0.5});
        auto prepared =
            PrepareCurrent(fixture.session, ExistingImageBatch(after, fixture.session.ActiveLayerID().value()));
        const bool applied =
            prepared.has_value() && fixture.session.ApplyPreparedImageImportIfCurrent(prepared.value());
        Expect(applied && fixture.session.Document().value().id == documentID &&
                   fixture.session.DocumentSerial() == serial,
               "existing-document-import-keeps-lineage-and-serial");
        Expect(fixture.session.history.UndoCount() == 2 && fixture.session.history.CanUndo() &&
                   !fixture.session.history.CanRedo() && fixture.session.history.UndoName() == L"Import Image",
               "existing-document-import-adds-one-history-entry");
        Expect(HasActivePixel(fixture.session, kImportedPixel),
               "existing-document-import-publishes-literal-active-pixel");
        Expect(fixture.session.CropFrame() == std::optional(CropRect{0.25, 0.5, 0.75, 0.5}),
               "existing-document-import-retains-crop-frame");
    }

    {
        auto fixtureOwner = std::make_unique<SessionFixture>();
        auto& fixture = *fixtureOwner;
        const auto before = fixture.session.history.CaptureState();
        const Guid revision = fixture.session.history.CurrentRevision();
        const auto crop = std::optional(CropRect{0.1, 0.2, 0.7, 0.6});
        fixture.session.SetCropFrame(crop);
        auto prepared = PrepareCurrent(fixture.session, ImageImportBatchResult{});
        const bool preparedNoEffect =
            prepared.has_value() && !prepared.value().ChangesDocument() && !prepared.value().ReplacesDocument();
        const bool applied =
            prepared.has_value() && fixture.session.ApplyPreparedImageImportIfCurrent(prepared.value());
        const auto after = fixture.session.history.CaptureState();
        Expect(preparedNoEffect && applied, "no-effect-batch-prepares-and-publishes-session-no-op");
        Expect(fixture.session.history.CurrentRevision() == revision && before.Revision() == after.Revision() &&
                   before.PastCount() == after.PastCount() && before.FutureCount() == after.FutureCount() &&
                   fixture.session.history.CanUndo() && fixture.session.history.CanRedo(),
               "no-effect-batch-preserves-undo-redo-history");
        Expect(fixture.session.CropFrame() == crop, "no-effect-batch-preserves-crop-frame");
    }

    {
        auto fixtureOwner = std::make_unique<SessionFixture>();
        auto& fixture = *fixtureOwner;
        CanvasDocument invalid = PixelDocument(L"Invalid mask graph", {10, 11, 12, 255});
        EditDocumentFixture(invalid,
                            [](DocumentFixture& fixture) { fixture.layers.front().maskSourceID = Guid::Generate(); });
        auto invalidPrepared = PrepareCurrent(fixture.session, NewImageBatch(invalid));
        Expect(!invalidPrepared.has_value(), "invalid-mask-source-graph-refused");

        const CanvasDocument baseline = PixelDocument(L"Mismatched IDs", {13, 14, 15, 255});
        CanvasDocument wrongDocumentID = WithSecondImage(baseline, kImportedPixel);
        wrongDocumentID.id = Guid::Generate();
        auto wrongIDPrepared = PrepareCurrent(fixture.session, NewImageBatch(baseline, wrongDocumentID));
        Expect(!wrongIDPrepared.has_value(), "new-project-imported-document-wrong-id-refused");

        ImageImportBatchResult wrongActive = NewImageBatch(baseline);
        wrongActive.createdActiveLayerID = Guid::Generate();
        auto wrongActivePrepared = PrepareCurrent(fixture.session, std::move(wrongActive));
        Expect(!wrongActivePrepared.has_value(), "new-project-wrong-active-layer-id-refused");

        CanvasDocument existingWrongID = WithPixel(fixture.session.Document().value(), kImportedPixel);
        existingWrongID.id = Guid::Generate();
        auto existingWrongIDPrepared = PrepareCurrent(
            fixture.session, ExistingImageBatch(existingWrongID, fixture.session.ActiveLayerID().value()));
        Expect(!existingWrongIDPrepared.has_value(), "existing-import-wrong-document-id-refused");
    }

    {
        auto ownerOwner = std::make_unique<SessionFixture>();
        auto& owner = *ownerOwner;
        auto foreignOwner = std::make_unique<SessionFixture>();
        auto& foreign = *foreignOwner;
        ImageImportAuthority emptyAuthority;
        auto defaultPrepared = Prepare(ImageImportBatchResult{}, std::move(emptyAuthority), owner.session.Document(),
                                       owner.session.ActiveLayerID(), owner.session.history.CaptureState());
        Expect(!defaultPrepared.has_value(), "default-authority-refused");

        const CanvasDocument foreignAfter = WithPixel(foreign.session.Document().value(), kImportedPixel);
        auto foreignPrepared = Prepare(ExistingImageBatch(foreignAfter, foreign.session.ActiveLayerID().value()),
                                       foreign.session.CaptureImageImportAuthority(), foreign.session.Document(),
                                       foreign.session.ActiveLayerID(), foreign.session.history.CaptureState());
        Expect(foreignPrepared.has_value() && !owner.session.ApplyPreparedImageImportIfCurrent(foreignPrepared.value()),
               "foreign-session-authority-refused-at-apply");
    }

    {
        auto fixtureOwner = std::make_unique<SessionFixture>();
        auto& fixture = *fixtureOwner;
        const CanvasDocument after = WithPixel(fixture.session.Document().value(), kImportedPixel);
        ImageImportAuthority captured = fixture.session.CaptureImageImportAuthority();
        ImageImportAuthority moved = std::move(captured);
        auto prepared = Prepare(ExistingImageBatch(after, fixture.session.ActiveLayerID().value()), std::move(moved),
                                fixture.session.Document(), fixture.session.ActiveLayerID(),
                                fixture.session.history.CaptureState());
        const bool firstApply =
            prepared.has_value() && fixture.session.ApplyPreparedImageImportIfCurrent(prepared.value());
        const bool secondApply =
            prepared.has_value() && fixture.session.ApplyPreparedImageImportIfCurrent(prepared.value());
        Expect(firstApply, "moved-authority-remains-valid-for-owning-session");
        Expect(!secondApply, "published-authority-is-single-use");
    }

    ExpectStaleRejected("epoch-only", ChangeEpochOnly);
    ExpectStaleRejected("active-layer", ChangeActive);
    ExpectStaleRejected("mask-selection", ChangeMaskSelection);
    ExpectStaleRejected("crop-frame", ChangeCrop);
    ExpectStaleRejected("document-serial", ChangeSerial);
    ExpectStaleRejected("generation", ChangeGeneration);

    {
        auto fixtureOwner = std::make_unique<SessionFixture>();
        auto& fixture = *fixtureOwner;
        CancelSource cancellation;
        static_cast<void>(cancellation.Cancel());
        auto prepared = PrepareCurrent(fixture.session,
                                       ExistingImageBatch(WithPixel(fixture.session.Document().value(), kImportedPixel),
                                                          fixture.session.ActiveLayerID().value()),
                                       cancellation.Token());
        Expect(!prepared.has_value(), "prework-cancellation-refuses-publication");
    }

    {
        auto fixtureOwner = std::make_unique<SessionFixture>();
        auto& fixture = *fixtureOwner;
        const CanvasDocument replacementBaseline = PixelDocument(L"Prepared replacement", {21, 22, 23, 255});
        auto staleReplacement = PrepareCurrent(fixture.session, NewImageBatch(replacementBaseline));
        const CanvasDocument ordinaryImport = WithPixel(fixture.session.Document().value(), kImportedPixel);
        const bool ordinaryApplied = fixture.session.ApplyImportedDocument(
            ordinaryImport, fixture.session.ActiveLayerID(), L"Concurrent ordinary import");
        const Guid ordinaryRevision = fixture.session.history.CurrentRevision();
        const bool staleReplacementRefused =
            staleReplacement.has_value() &&
            !fixture.session.ApplyPreparedImageImportIfCurrent(staleReplacement.value());
        Expect(ordinaryApplied && staleReplacementRefused &&
                   fixture.session.history.CurrentRevision() == ordinaryRevision,
               "ordinary-import-wins-over-stale-replacement");
        Expect(fixture.session.history.IsModified() &&
                   fixture.session.Document().value().id != replacementBaseline.id &&
                   HasActivePixel(fixture.session, kImportedPixel),
               "stale-replacement-cannot-reset-ordinary-import-saved-revision");
    }

    {
        auto fixtureOwner = std::make_unique<SessionFixture>();
        auto& fixture = *fixtureOwner;
        const CanvasDocument ordinaryAfter = WithPixel(fixture.session.Document().value(), {31, 32, 33, 255});
        auto staleOrdinary =
            PrepareCurrent(fixture.session, ExistingImageBatch(ordinaryAfter, fixture.session.ActiveLayerID().value()));
        const CanvasDocument replacementBaseline = PixelDocument(L"Winning replacement", kImportedPixel);
        auto replacement = PrepareCurrent(fixture.session, NewImageBatch(replacementBaseline));
        const bool replacementApplied =
            replacement.has_value() && fixture.session.ApplyPreparedImageImportIfCurrent(replacement.value());
        const Guid savedBaselineRevision = fixture.session.history.CurrentRevision();
        const bool staleOrdinaryRefused =
            staleOrdinary.has_value() && !fixture.session.ApplyPreparedImageImportIfCurrent(staleOrdinary.value());
        Expect(replacementApplied && staleOrdinaryRefused &&
                   fixture.session.history.CurrentRevision() == savedBaselineRevision,
               "replacement-wins-over-stale-ordinary-import");
        Expect(!fixture.session.history.IsModified() && !fixture.session.history.CanUndo() &&
                   HasActivePixel(fixture.session, kImportedPixel),
               "stale-ordinary-import-cannot-dirty-new-saved-baseline");
    }

    {
        auto fixtureOwner = std::make_unique<SessionFixture>();
        auto& fixture = *fixtureOwner;
        auto ordinary = PrepareCurrent(fixture.session,
                                       ExistingImageBatch(WithPixel(fixture.session.Document().value(), kImportedPixel),
                                                          fixture.session.ActiveLayerID().value()));
        fixture.session.history.MarkSaved(Guid::Generate()); // A later save fact is independent of worker authority.
        const bool applied = ordinary && fixture.session.ApplyPreparedImageImportIfCurrent(ordinary.value());
        fixture.session.Undo();
        Expect(applied && fixture.session.history.IsModified(),
               "ordinary-import-retains-later-saved-revision-after-undo");
    }
    {
        auto fixtureOwner = std::make_unique<SessionFixture>();
        auto& fixture = *fixtureOwner;
        auto replacement =
            PrepareCurrent(fixture.session, NewImageBatch(PixelDocument(L"New baseline", kImportedPixel)));
        fixture.session.history.MarkSaved(Guid::Generate());
        const bool applied = replacement && fixture.session.ApplyPreparedImageImportIfCurrent(replacement.value());
        Expect(applied && !fixture.session.history.IsModified(),
               "replacement-installs-new-saved-baseline-after-later-save");
    }
    for (const int choice : {0, 1, 2})
    {
        auto fixtureOwner = std::make_unique<SessionFixture>();
        auto& fixture = *fixtureOwner;
        const auto serial = fixture.session.DocumentSerial();
        const CanvasDocument replacement = PixelDocument(L"Project baseline", kImportedPixel);
        const auto active = choice == 0   ? std::optional<Guid>{}
                            : choice == 1 ? std::optional(Guid::Generate())
                                          : std::optional(replacement.layers.front().id);
        auto prepared = DocumentSession::PrepareDocumentReplacementPublication(
            fixture.session.CaptureImageImportAuthority(), fixture.session.Document(),
            fixture.session.history.CaptureState(), replacement, active);
        const bool applied = prepared && fixture.session.ApplyPreparedImageImportIfCurrent(prepared.value());
        Expect(applied && fixture.session.DocumentSerial() != serial && !fixture.session.history.CanUndo() &&
                   !fixture.session.history.CanRedo() && !fixture.session.history.IsModified(),
               "project-replacement-installs-clean-new-lineage");
        Expect(fixture.session.ActiveLayerID() ==
                   (choice == 2 ? std::optional(replacement.layers.front().id) : std::optional<Guid>{}),
               "project-replacement-valid-absent-and-unknown-active-selection");
    }
    {
        auto emptyOwner = std::make_unique<DocumentSession>();
        auto& empty = *emptyOwner;
        CanvasDocument blank;
        blank.id = Guid::Generate();
        blank.width = blank.height = 1;
        auto prepared = DocumentSession::PrepareDocumentReplacementPublication(
            empty.CaptureImageImportAuthority(), empty.Document(), empty.history.CaptureState(), blank, {});
        const bool applied = prepared && empty.ApplyPreparedImageImportIfCurrent(prepared.value());
        Expect(applied && empty.Document().value().layers.empty() && !empty.ActiveLayerID() &&
                   !empty.history.IsModified(),
               "empty-project-can-replace-no-document-session");
    }
    for (const bool project : {false, true})
        for (const bool stop : {false, true})
        {
            auto fixtureOwner = std::make_unique<SessionFixture>();
            auto& fixture = *fixtureOwner;
            const auto original = fixture.session.Document();
            const auto active = fixture.session.ActiveLayerID();
            const auto revision = fixture.session.history.CurrentRevision();
            const auto undoCount = fixture.session.history.UndoCount();
            const auto redoName = fixture.session.history.RedoName();
            const CanvasDocument replacement = PixelDocument(L"Refused replacement", kImportedPixel);
            CancelSource cancellation;
            struct Fault final
            {
                CancelSource& cancellation;
                bool stop;
                unsigned int hits = 0;
            } fault{cancellation, stop};
            const DocumentHistory::PrepareObserver observer{[](void* context, DocumentHistory::PrepareStage stage)
                                                            {
                                                                auto& fault = *static_cast<Fault*>(context);
                                                                if (stage ==
                                                                    DocumentHistory::PrepareStage::ReplacementSnapshot)
                                                                {
                                                                    ++fault.hits;
                                                                    if (fault.stop)
                                                                        static_cast<void>(fault.cancellation.Cancel());
                                                                    else
                                                                        throw std::bad_alloc{};
                                                                }
                                                            },
                                                            &fault};
            bool threw = false;
            std::optional<PreparedImageImport> prepared;
            try
            {
                if (project)
                    prepared = DocumentSession::PrepareDocumentReplacementPublication(
                        fixture.session.CaptureImageImportAuthority(), fixture.session.Document(),
                        fixture.session.history.CaptureState(), replacement, replacement.layers.front().id,
                        cancellation.Token(), observer);
                else
                    prepared = DocumentSession::PrepareImageImportPublication(
                        fixture.session.CaptureImageImportAuthority(), fixture.session.Document(), active,
                        fixture.session.history.CaptureState(), NewImageBatch(replacement), cancellation.Token(),
                        observer);
            }
            catch (const std::bad_alloc&)
            {
                threw = true; // Independent injected-construction boundary; production runner translates failure.
            }
            Expect(!prepared && threw == !stop && fault.hits == 1,
                   "replacement-snapshot-refuses-requested-allocation-or-cancellation-once");
            Expect(fixture.session.Document() == original && fixture.session.ActiveLayerID() == active &&
                       fixture.session.history.CurrentRevision() == revision &&
                       fixture.session.history.UndoCount() == undoCount &&
                       fixture.session.history.RedoName() == redoName && !fixture.session.history.IsModified(),
                   "replacement-snapshot-refusal-preserves-live-document-target-and-history");
            Expect(HasActivePixel(fixture.session, {4, 5, 6, 255}),
                   "replacement-snapshot-refusal-preserves-literal-original-pixels");
        }
    CheckLiveDraftReplacement();
    CheckImportResidencyMoveAndNoOp();
    CheckImportResidencyLateCache(false, false);
    CheckImportResidencyLateCache(true, false);
    CheckImportResidencyLateCache(false, true);
    CheckReplacementResidency();
    CheckReplacementLateResidency();
    CheckImportPermitMoves();
    std::printf("import-publication.assertions=%d failed=%d\n", gAssertions, gFailures);
    return gFailures;
}
} // namespace internal
