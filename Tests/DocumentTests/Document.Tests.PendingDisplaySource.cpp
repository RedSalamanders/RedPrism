#include "Document.Tests.PendingDisplaySource.h"

#include "../Support/Support.Tests.DocumentFixture.h"
#include "Adjust.h"
#include "ColorDisplayPublication.h"
#include "ColorEditWork.h"
#include "Document.h"
#include "Executor.h"
#include "PendingDisplaySource.h"
#include "Selection.h"
#include "TransformPreviewWork.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <utility>

namespace
{
int gFailures = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s pending-display-source.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

Raster SourceRaster()
{
    constexpr std::array<std::uint8_t, 16> pixels{10, 20, 30, 255, 40,  50,  60,  255,
                                                  70, 80, 90, 255, 100, 110, 120, 255};
    return Raster::Make(2, 2, RasterFormat::Bgra8888, pixels.data(), pixels.size());
}

DocumentSession Session(bool selection = false)
{
    const Raster image = SourceRaster();
    DocumentFixture fixture;
    fixture.id = Guid::Generate();
    fixture.width = 4;
    fixture.height = 4;
    if (selection)
        fixture.selection = DocumentSelection{true, SelectionGeometry::Rect(0, 0, 1, 2)};
    ImageLayer layer = ImageLayer::Blank(L"Pending display", Vec2{2, 2});
    const Guid active = layer.id;
    layer.asset = RasterAsset{image, Raster::MakeThumbnail(image), layer.name};
    fixture.layers.push_back(std::move(layer));
    DocumentSession session;
    session.ReplaceDocument(std::move(fixture).Seal(), active);
    return session;
}

FilterSettings Levels(double gamma = 2.0)
{
    FilterSettings settings;
    settings.levels.ranges[0].gamma = gamma;
    return settings;
}

struct OwnerSnapshot final
{
    const CanvasDocument* document = nullptr;
    const void* layerRoot = nullptr;
    std::uint64_t generation = 0;
    int undoCount = 0;
    DocumentHistory::StateView history;

    explicit OwnerSnapshot(const DocumentSession& session)
        : document(session.Document() ? &session.Document().value() : nullptr),
          layerRoot(document ? document->layers.Identity() : nullptr), generation(session.Generation()),
          undoCount(session.history.UndoCount()), history(session.history.CaptureState())
    {
    }
};

bool SameOwner(const DocumentSession& session, const OwnerSnapshot& before) noexcept
{
    const CanvasDocument* const document = session.Document() ? &session.Document().value() : nullptr;
    return document == before.document && (!document || document->layers.Identity() == before.layerRoot) &&
           session.Generation() == before.generation && session.history.UndoCount() == before.undoCount &&
           session.history.IsCurrent(before.history);
}

Raster ActiveImage(const DocumentSession& session)
{
    const auto* const active = session.ActiveLayer();
    return active != nullptr && active->asset ? active->asset->image : Raster{};
}

bool BeginLiveAdjustment(DocumentSession& session, const Raster& below, Guid& id)
{
    if (!below.HasStorage())
        return false;
    session.AddAdjustment(AdjustmentKind::Levels);
    const auto editing = session.AdjustmentEditingID();
    if (!editing)
        return false;
    id = editing.value();
    return session.BeginAdjustmentEditing(id, below, true);
}

ColorEditPreviewResult PrepareLiveLookup(DocumentSession& session)
{
    session.UpdateColorEdit(Levels(), true);
    auto input = session.PrepareColorEditPreview();
    if (!input || !session.AdjustmentOriginal())
        return {};
    // Match the actual host ConfigureColorLookupPreview capture: live work carries the original
    // adjustment record separately from the generic immutable panel input.
    input->liveAdjustment = session.AdjustmentOriginal();
    InlineExecutor executor;
    return RunColorEditPreviewJob(input.value(), executor);
}

bool PublishDestructiveColorDisplay(DocumentSession& session)
{
    auto input = session.PrepareColorDisplayPreviewInput(true);
    if (!input)
        return false;
    InlineExecutor executor;
    auto result = DocumentSession::PrepareColorDisplayPreviewPublication(std::move(input.value()), executor, {}, {});
    return result.status == ColorEditJobStatus::Ready && result.publication &&
           session.ApplyPreparedColorDisplayPreviewIfCurrent(result.publication.value());
}

bool Describe(DocumentSession& session, PendingDisplaySource& output)
{
    return session.DescribePendingDisplaySource(output);
}

void NoPendingSourceAndOptInControls()
{
    DocumentSession empty;
    PendingDisplaySource output;
    output.opening = 73;
    output.kind = PendingDisplaySource::Kind::Color;
    Expect(!Describe(empty, output) && output.opening == 73 && output.document == nullptr,
           "empty-session-has-no-source-and-refusal-preserves-output");

    DocumentSession idle = Session();
    Expect(!Describe(idle, output), "committed-idle-session-has-no-pending-source");

    DocumentSession disabled = Session();
    const Raster disabledBelow = ActiveImage(disabled);
    Guid disabledId;
    if (!BeginLiveAdjustment(disabled, disabledBelow, disabledId))
    {
        Expect(false, "reference-live-fixture-opens");
        return;
    }
    disabled.SetColorDisplayJobsEnabled(true);
    auto completed = PrepareLiveLookup(disabled);
    const bool applied = completed.status == ColorEditJobStatus::Ready && disabled.ApplyColorEditPreview(completed);
    Expect(applied, "reference-live-fixture-builds-completed-lookup");
    if (!applied)
        return;
    disabled.SetColorDisplayJobsEnabled(false);
    Expect(!Describe(disabled, output), "default-reference-live-route-rejects-cached-lookup");

    DocumentSession waiting = Session();
    waiting.SetColorDisplayJobsEnabled(true);
    const Raster waitingBelow = ActiveImage(waiting);
    Guid waitingId;
    const bool opened = BeginLiveAdjustment(waiting, waitingBelow, waitingId);
    Expect(opened, "deferred-live-fixture-opens-with-worker-route");
    if (opened)
        Expect(!Describe(waiting, output), "worker-enabled-live-route-waits-for-completed-lookup");
}

void CompletedLiveLookupIsBorrowedWithoutOwnerMutation()
{
    DocumentSession session = Session();
    session.SetColorDisplayJobsEnabled(true);
    const Raster below = ActiveImage(session);
    Guid id;
    if (!BeginLiveAdjustment(session, below, id))
    {
        Expect(false, "live-descriptor-fixture-opens");
        return;
    }
    auto completed = PrepareLiveLookup(session);
    const bool applied = completed.status == ColorEditJobStatus::Ready && session.ApplyColorEditPreview(completed);
    const auto& published = session.PendingColorEdit();
    const bool lookupOnly = applied && published && published->previewLookup && !published->previewRaster.HasStorage();
    Expect(lookupOnly, "live-descriptor-fixture-publishes-lookup-only-result");
    if (!lookupOnly)
        return;

    const auto& pending = session.PendingColorEdit();
    const auto authority = session.CaptureDocumentReadAuthority();
    const auto& originalSettings = session.AdjustmentOriginal();
    if (!session.Document() || !authority || !originalSettings)
    {
        Expect(false, "completed-live-fixture-has-document-authority-and-original");
        return;
    }
    const CanvasDocument* const root = &session.Document().value();
    const auto rootIdentity = root->layers.Identity();
    const auto* const lookup = pending->previewLookup.get();
    const auto* const original = &originalSettings.value();
    const OwnerSnapshot before(session);
    PendingDisplaySource described;
    const bool available = Describe(session, described);
    Expect(available, "completed-live-lookup-is-describable");
    if (available)
        Expect(described.kind == PendingDisplaySource::Kind::Adjustment && described.authority == authority.value() &&
                   described.authority.generation == session.Generation() && described.document == root &&
                   described.sourceIdentity == rootIdentity && described.target == id &&
                   described.opening == pending->openingSequence && described.publication == pending->previewSequence &&
                   described.lookup == &session.PendingColorEdit()->previewLookup &&
                   described.lookup->get() == lookup && described.original == original &&
                   described.adjustmentSource == nullptr && described.adjustmentCoverage == nullptr,
               "live-descriptor-borrows-exact-root-authority-opening-publication-lookup-and-original");
    Expect(SameOwner(session, before), "describe-live-source-does-not-change-document-generation-or-history");
}

void NewerLiveSettingsKeepCompatibleLookupAndOffRefuses()
{
    DocumentSession session = Session();
    session.SetColorDisplayJobsEnabled(true);
    const Raster below = ActiveImage(session);
    Guid id;
    if (!BeginLiveAdjustment(session, below, id))
    {
        Expect(false, "newer-controls-fixture-opens");
        return;
    }
    auto completed = PrepareLiveLookup(session);
    if (completed.status != ColorEditJobStatus::Ready || !session.ApplyColorEditPreview(completed))
    {
        Expect(false, "newer-controls-fixture-publishes-lookup");
        return;
    }
    const auto& pending = session.PendingColorEdit();
    if (!pending || !pending->previewLookup)
    {
        Expect(false, "newer-controls-fixture-has-published-lookup");
        return;
    }
    const std::uint64_t opening = pending->openingSequence;
    const std::uint64_t completedSequence = pending->previewSequence;
    const auto* const lookup = pending->previewLookup.get();
    session.UpdateColorEdit(Levels(1.5), true);
    PendingDisplaySource newer;
    const bool stillAvailable = Describe(session, newer);
    Expect(stillAvailable && newer.kind == PendingDisplaySource::Kind::Adjustment && newer.target == id &&
               newer.opening == opening && newer.publication == completedSequence && newer.lookup != nullptr &&
               newer.lookup->get() == lookup && session.PendingColorEdit() &&
               session.PendingColorEdit()->settings == Levels(1.5),
           "new-controls-retain-last-compatible-completed-lookup-and-sequence");

    session.UpdateColorEdit(Levels(1.5), false);
    PendingDisplaySource off;
    Expect(!Describe(session, off), "preview-off-refuses-previously-completed-lookup");
}

void CancelAndReopenRefuseLateOldLookup()
{
    DocumentSession session = Session();
    session.SetColorDisplayJobsEnabled(true);
    const Raster below = ActiveImage(session);
    Guid id;
    if (!BeginLiveAdjustment(session, below, id))
    {
        Expect(false, "late-opening-fixture-opens");
        return;
    }
    auto oldResult = PrepareLiveLookup(session);
    const auto& pending = session.PendingColorEdit();
    if (!pending)
    {
        Expect(false, "late-opening-fixture-has-pending-edit");
        return;
    }
    const std::uint64_t oldOpening = pending->openingSequence;
    Expect(oldResult.status == ColorEditJobStatus::Ready, "late-opening-old-worker-completes");
    if (oldResult.status != ColorEditJobStatus::Ready)
        return;

    session.CancelColorEdit();
    session.SetAdjustmentEditingID(id);
    const bool reopened = session.BeginAdjustmentEditing(id, below, true);
    const auto& reopenedPending = session.PendingColorEdit();
    Expect(reopened && reopenedPending && reopenedPending->openingSequence != oldOpening,
           "late-opening-new-panel-has-distinct-opening");
    if (!reopened || !reopenedPending)
        return;
    PendingDisplaySource output;
    Expect(!session.ApplyColorEditPreview(oldResult) && !Describe(session, output) && session.PendingColorEdit() &&
               !session.PendingColorEdit()->previewLookup,
           "late-old-lookup-result-cannot-describe-or-publish-into-new-opening");
}

void DestructiveColorDescribesPreparedDisplayAndLookupInputs()
{
    DocumentSession session = Session(true);
    session.SetColorDisplayJobsEnabled(true);
    session.BeginColorEdit(FilterKind::Levels, true);
    session.UpdateColorEdit(Levels(), true);
    if (!session.Document() || !session.PendingColorEdit())
    {
        Expect(false, "destructive-color-fixture-has-document-and-pending-edit");
        return;
    }
    const CanvasDocument* const committed = &session.Document().value();
    const auto authority = session.CaptureDocumentReadAuthority();
    if (!authority)
    {
        Expect(false, "destructive-color-fixture-has-read-authority");
        return;
    }
    const bool published = PublishDestructiveColorDisplay(session);
    Expect(published, "destructive-color-prepared-display-and-lookup-publish");
    if (!published)
        return;
    if (!session.ColorLookupPreviewReady())
    {
        Expect(false, "destructive-color-lookup-is-ready");
        return;
    }
    const auto& pending = session.PendingColorEdit();
    if (!pending || !pending->previewLookup)
    {
        Expect(false, "destructive-color-fixture-retains-published-lookup");
        return;
    }
    const CanvasDocument* const display = session.DisplayDocument();
    const OwnerSnapshot before(session);
    PendingDisplaySource described;
    const bool available = Describe(session, described);
    Expect(available, "destructive-color-source-is-describable-after-prepared-display");
    if (available)
        Expect(described.kind == PendingDisplaySource::Kind::Color && described.authority == authority.value() &&
                   described.document == display && display != nullptr && display != committed &&
                   !display->layers.empty() && described.sourceIdentity == display->layers.Identity() &&
                   described.target == pending->layerID && described.opening == pending->openingSequence &&
                   described.publication == pending->previewSequence &&
                   described.lookup == &session.PendingColorEdit()->previewLookup && described.lookup != nullptr &&
                   described.lookup->get() == pending->previewLookup.get() &&
                   described.adjustmentSource == &session.PendingColorEdit()->previewSource &&
                   described.adjustmentCoverage == &session.PendingColorEdit()->previewCoverage &&
                   described.adjustmentSource->Format() == RasterFormat::Bgra8888 &&
                   described.adjustmentCoverage->Format() == RasterFormat::Gray8 &&
                   described.adjustmentCoverage->Width() == described.adjustmentSource->Width() &&
                   described.adjustmentCoverage->Height() == described.adjustmentSource->Height() &&
                   described.previewScale == pending->previewScale && described.previewScale > 0 &&
                   described.original == nullptr,
               "destructive-color-descriptor-borrows-display-source-gray-coverage-lookup-and-scale");
    Expect(SameOwner(session, before), "describe-destructive-source-does-not-change-document-generation-or-history");
}

DocumentSession TransformSession()
{
    DocumentSession session = Session();
    static_cast<void>(session.BeginDeferredTransform());
    return session;
}

bool PublishTransform(DocumentSession& session)
{
    auto input = session.PrepareTransformPreviewInput();
    if (!input)
        return false;
    InlineExecutor executor;
    auto result = DocumentSession::PrepareTransformPreviewPublication(std::move(input.value()), executor, {}, {});
    return result.status == DistortWorkStatus::Ready && result.publication &&
           session.ApplyPreparedTransformPreviewIfCurrent(result.publication.value());
}

bool TransformDescriptorMatches(DocumentSession& session, const Raster& source, bool expectSharedRaster,
                                const char* prefix)
{
    const auto& pending = session.PendingTransformEdit();
    const auto authority = session.CaptureDocumentReadAuthority();
    if (!pending || !authority || !session.Document())
    {
        Expect(false, "transform-descriptor-fixture-has-edit-authority-and-document");
        return false;
    }
    const auto opening = pending->opening;
    const auto* const display = session.DisplayDocument();
    const OwnerSnapshot before(session);
    PendingDisplaySource described;
    const bool available = Describe(session, described);
    Expect(available, prefix);
    const bool matches =
        available && described.kind == PendingDisplaySource::Kind::Transform &&
        described.authority == authority.value() && described.document == display && display != nullptr &&
        !display->layers.empty() && described.document != &session.Document().value() &&
        display->layers.front().asset.has_value() && described.sourceIdentity == display->layers.Identity() &&
        described.opening == opening && described.publication == session.TransformDraftRevision() &&
        !described.lookup && !described.original && !described.adjustmentSource && !described.adjustmentCoverage &&
        described.previewScale == 1.0 && display->layers.front().asset->image.HasStorage() &&
        (display->layers.front().asset->image.Identity() == source.Identity()) == expectSharedRaster;
    Expect(matches, "transform-descriptor-borrows-prepared-display-and-exact-opening-revision");
    Expect(SameOwner(session, before), "describe-transform-source-does-not-change-document-generation-or-history");
    return matches;
}

void DeferredTransformRequiresPreparedAffineOrDistortDisplay()
{
    {
        DocumentSession session = TransformSession();
        const auto* const document = session.Document() ? &session.Document().value() : nullptr;
        const Raster source = document != nullptr && !document->layers.empty() && document->layers.front().asset
                                  ? document->layers.front().asset->image
                                  : Raster{};
        PendingDisplaySource initial;
        Expect(source.HasStorage() && session.PendingTransformEdit() && !Describe(session, initial) &&
                   session.DisplayDocument() == &session.Document().value(),
               "deferred-affine-without-completed-display-is-refused");
        if (!source.HasStorage() || !session.PendingTransformEdit())
        {
            Expect(false, "deferred-affine-fixture-opens");
            return;
        }
        auto draft = session.PendingTransformEdit()->draft;
        draft.origin.x += 1;
        session.PreviewTransform(draft);
        const auto& affine = session.PendingTransformEdit();
        const bool published = affine.has_value() && affine->deferred && PublishTransform(session);
        Expect(published, "deferred-affine-display-is-prepared-through-transform-preview-work");
        if (!published)
            return;
        Expect(TransformDescriptorMatches(session, source, true, "prepared-affine-display-is-describable"),
               "prepared-affine-source-shares-original-raster");
    }
    {
        DocumentSession session = TransformSession();
        const auto* const document = session.Document() ? &session.Document().value() : nullptr;
        const Raster source = document != nullptr && !document->layers.empty() && document->layers.front().asset
                                  ? document->layers.front().asset->image
                                  : Raster{};
        if (!source.HasStorage() || !session.PendingTransformEdit())
        {
            Expect(false, "deferred-distort-fixture-opens");
            return;
        }
        session.BeginDistort();
        const auto& distort = session.PendingTransformEdit();
        if (!distort || !distort->corners)
        {
            Expect(false, "deferred-distort-fixture-has-corners");
            return;
        }
        auto corners = distort->corners.value();
        corners[2].x += 0.5;
        corners[2].y += 0.25;
        session.PreviewCorners(corners);
        PendingDisplaySource initial;
        Expect(!Describe(session, initial), "deferred-distort-without-completed-display-is-refused");
        const bool published = PublishTransform(session);
        Expect(published, "deferred-distort-display-is-prepared-through-transform-preview-work");
        if (!published)
            return;
        const bool described =
            TransformDescriptorMatches(session, source, false, "prepared-distort-display-is-describable");
        const auto* display = session.DisplayDocument();
        Expect(described && display->layers.front().asset->image.Identity() != source.Identity(),
               "prepared-distort-display-owns-warped-raster");
    }
}
} // namespace

int RunPendingDisplaySourceDescriptorTests()
{
    gFailures = 0;
    NoPendingSourceAndOptInControls();
    CompletedLiveLookupIsBorrowedWithoutOwnerMutation();
    NewerLiveSettingsKeepCompatibleLookupAndOffRefuses();
    CancelAndReopenRefuseLateOldLookup();
    DestructiveColorDescribesPreparedDisplayAndLookupInputs();
    DeferredTransformRequiresPreparedAffineOrDistortDisplay();
    return gFailures;
}
