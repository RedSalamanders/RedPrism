#include "Document.Tests.TransformDeferred.h"

#include "Document.h"
#include "Executor.h"
#include "TransformCancellationWork.h"
#include "TransformPreviewWork.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <span>
#include <utility>

namespace
{
int gFailures = 0;
int gAssertions = 0;

void Expect(bool condition, const char* name) noexcept
{
    ++gAssertions;
    std::printf("%s transform-deferred.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

constexpr std::array<Vec2, 4> Rect(double left, double top, double right, double bottom)
{
    return {Vec2{left, top}, Vec2{right, top}, Vec2{right, bottom}, Vec2{left, bottom}};
}

struct Fixture final
{
    DocumentSession session;
    Guid sourceID;
    Guid siblingID;

    Fixture()
    {
        constexpr std::array<std::uint8_t, 16> pixels{11, 21, 31, 255, 41,  51,  61,  255,
                                                      71, 81, 91, 255, 101, 111, 121, 255};
        constexpr std::array<std::uint8_t, 4> maskPixels{255, 128, 64, 0};
        const Raster image = Raster::Make(2, 2, RasterFormat::Bgra8888, pixels.data(), pixels.size());
        const Raster mask = Raster::Make(2, 2, RasterFormat::Gray8, maskPixels.data(), maskPixels.size());
        CanvasDocument document;
        document.id = Guid::Generate();
        document.width = 8;
        document.height = 8;
        document.selection = DocumentSelection{false, SelectionLoops{{Vec2{0, 0}, Vec2{1, 0}, Vec2{1, 1}, Vec2{0, 1}}}};

        ImageLayer source = ImageLayer::Blank(L"Deferred source", Vec2{2, 2});
        source.asset = RasterAsset{image, image, source.name};
        source.mask = LayerMask{RasterAsset{mask, mask, L"Source mask"}, true, std::nullopt, true};
        source.transform.sampling = LayerSampling::Nearest;
        sourceID = source.id;

        ImageLayer sibling = ImageLayer::Blank(L"Sibling", Vec2{2, 2});
        sibling.asset = RasterAsset{image, image, sibling.name};
        sibling.transform.origin = Vec2{3, 0};
        sibling.transform.sampling = LayerSampling::Nearest;
        siblingID = sibling.id;
        document.layers = {source, sibling};
        session.ReplaceDocument(std::move(document), sourceID);
        static_cast<void>(session.CopySelection());
        session.SelectLayerTarget(sourceID, true);
    }
};

TransformPreviewResult PreparePreview(DocumentSession& session, Executor& executor)
{
    auto input = session.PrepareTransformPreviewInput();
    return input ? DocumentSession::PrepareTransformPreviewPublication(std::move(input.value()), executor, {}, {})
                 : TransformPreviewResult{};
}

std::optional<PreparedTransformCancellation> PrepareCancellation(DocumentSession& session)
{
    auto input = session.PrepareTransformCancellationInput();
    return input ? DocumentSession::PrepareTransformCancellationPublication(std::move(input.value())) : std::nullopt;
}

void SetDistortion(DocumentSession& session, double right, double bottom)
{
    if (!session.PendingTransformEdit())
        return;
    if (!session.PendingTransformEdit()->corners)
        session.BeginDistort();
    auto corners = session.PendingTransformEdit()->corners.value_or(Rect(2, 2, 4, 4));
    corners[1].x = right;
    corners[2].x = right;
    corners[2].y = bottom;
    corners[3].y = bottom;
    session.PreviewCorners(corners);
}

bool SameClipboard(const std::optional<PixelClipboard>& a, const std::optional<PixelClipboard>& b) noexcept
{
    return a.has_value() == b.has_value() && (!a || (a->image == b->image && a->origin == b->origin));
}

void InlineBehaviorRemainsUnchanged()
{
    Fixture fixture;
    const auto original = fixture.session.Document().value();
    const int historyCount = fixture.session.history.UndoCount();
    fixture.session.BeginTransform();
    Expect(fixture.session.PendingTransformEdit() && !fixture.session.PendingTransformEdit()->deferred,
           "inline-begin-remains-nondeferred-by-default");
    auto draft = fixture.session.PendingTransformEdit()->draft;
    draft.origin.x += 2;
    fixture.session.PreviewTransform(draft);
    const CanvasDocument* shown = fixture.session.DisplayDocument();
    Expect(shown && shown != &fixture.session.Document().value() && shown->layers.front().transform == draft,
           "inline-display-still-previews-draft");
    fixture.session.CommitTransform();
    Expect(!fixture.session.PendingTransformEdit() && fixture.session.Document()->layers.front().transform == draft &&
               fixture.session.Document().value() != original &&
               fixture.session.history.UndoCount() == historyCount + 1,
           "inline-commit-still-applies-one-history-entry");

    Fixture cancelled;
    const auto before = cancelled.session.Document().value();
    cancelled.session.BeginTransform();
    auto cancelledDraft = cancelled.session.PendingTransformEdit()->draft;
    cancelledDraft.origin.x += 1;
    cancelled.session.PreviewTransform(cancelledDraft);
    cancelled.session.CancelTransform();
    Expect(!cancelled.session.PendingTransformEdit() && cancelled.session.Document().value() == before,
           "inline-cancel-still-drops-only-preview");
}

void DeferredOpeningAndPublication()
{
    Fixture fixture;
    InlineExecutor executor;
    const auto original = fixture.session.Document().value();
    const auto history = fixture.session.history.CaptureState();
    const int undoCount = fixture.session.history.UndoCount();
    const std::uint64_t generation = fixture.session.Generation();
    const std::uint64_t clipboardSerial = fixture.session.ClipboardSerial();
    const auto clipboard = fixture.session.Clipboard();
    const CanvasDocument* committed = &fixture.session.Document().value();

    Expect(fixture.session.BeginDeferredTransform(), "deferred-begin-succeeds");
    const auto opening = fixture.session.PendingTransformEdit()->opening;
    const auto revision = fixture.session.TransformDraftRevision();
    Expect(fixture.session.PendingTransformEdit()->deferred && opening == revision && opening != 0,
           "deferred-begin-marks-edit-and-captures-opening-revision");
    Expect(fixture.session.DisplayDocument() == committed,
           "deferred-distortion-without-preview-returns-committed-document");
    SetDistortion(fixture.session, 5, 5);
    Expect(fixture.session.TransformDraftRevision() != revision &&
               fixture.session.PendingTransformEdit()->opening == opening,
           "corner-tick-advances-draft-but-keeps-opening");
    Expect(fixture.session.DisplayDocument() == committed, "deferred-corner-tick-does-not-reconstruct-display");
    Expect(fixture.session.Document().value() == original && fixture.session.history.IsCurrent(history) &&
               fixture.session.history.UndoCount() == undoCount && fixture.session.Generation() == generation &&
               fixture.session.ClipboardSerial() == clipboardSerial &&
               SameClipboard(fixture.session.Clipboard(), clipboard),
           "deferred-draft-preserves-document-history-generation-and-clipboard");

    auto first = PreparePreview(fixture.session, executor);
    Expect(first.status == DistortWorkStatus::Ready && first.publication,
           "deferred-worker-prepares-first-complete-preview");
    if (!first.publication)
        return;
    auto firstPublication = std::move(first.publication.value());
    Expect(fixture.session.ApplyPreparedTransformPreviewIfCurrent(firstPublication),
           "deferred-worker-preview-publishes");
    const CanvasDocument* complete = fixture.session.DisplayDocument();
    const ImageLayer* shown = complete ? &complete->layers.front() : nullptr;
    Expect(complete && complete != committed && shown && shown->asset &&
               shown->asset->image.Identity() != original.layers.front().asset->image.Identity(),
           "deferred-display-uses-prepared-distorted-raster");

    const void* completeRaster = shown && shown->asset ? shown->asset->image.Identity() : nullptr;
    SetDistortion(fixture.session, 6, 5);
    Expect(fixture.session.DisplayDocument() == complete &&
               fixture.session.DisplayDocument()->layers.front().asset->image.Identity() == completeRaster,
           "new-corner-tick-retains-exact-last-complete-display-and-raster");
    auto stale = PreparePreview(fixture.session, executor);
    SetDistortion(fixture.session, 6, 6);
    Expect(stale.status == DistortWorkStatus::Ready && stale.publication,
           "older-tick-preview-can-be-prepared-before-newer-tick");
    if (stale.publication)
    {
        auto stalePublication = std::move(stale.publication.value());
        Expect(!fixture.session.ApplyPreparedTransformPreviewIfCurrent(stalePublication) &&
                   fixture.session.DisplayDocument() == complete,
               "stale-prepared-tick-cannot-replace-last-complete-preview");
    }

    auto latest = PreparePreview(fixture.session, executor);
    Expect(latest.status == DistortWorkStatus::Ready && latest.publication, "most-recent-draft-prepares-successfully");
    if (latest.publication)
    {
        auto latestPublication = std::move(latest.publication.value());
        Expect(fixture.session.ApplyPreparedTransformPreviewIfCurrent(latestPublication),
               "most-recent-draft-publication-applies");
        Expect(fixture.session.DisplayDocument() && fixture.session.DisplayDocument() != committed &&
                   fixture.session.DisplayDocument()->layers.front().asset->image.Identity() != completeRaster,
               "most-recent-ready-preview-becomes-displayed-raster");
    }
    Expect(fixture.session.Document().value() == original && fixture.session.history.IsCurrent(history) &&
               fixture.session.history.UndoCount() == undoCount && fixture.session.Generation() == generation &&
               fixture.session.ClipboardSerial() == clipboardSerial &&
               SameClipboard(fixture.session.Clipboard(), clipboard),
           "preview-publication-remains-display-only");
}

void DeferredDirectGuardsAndPreparedCancellation()
{
    Fixture fixture;
    InlineExecutor executor;
    const auto original = fixture.session.Document().value();
    const auto history = fixture.session.history.CaptureState();
    const auto clipboard = fixture.session.Clipboard();
    Expect(fixture.session.BeginDeferredTransform(), "guard-fixture-opens-deferred-transform");
    SetDistortion(fixture.session, 5, 5);
    const TransformEdit editBefore = fixture.session.PendingTransformEdit().value();
    const auto generation = fixture.session.Generation();
    const auto undoCount = fixture.session.history.UndoCount();
    fixture.session.CommitTransform();
    fixture.session.CancelTransform();
    Expect(fixture.session.PendingTransformEdit() && fixture.session.PendingTransformEdit()->deferred &&
               fixture.session.PendingTransformEdit()->draft == editBefore.draft &&
               fixture.session.PendingTransformEdit()->corners == editBefore.corners &&
               fixture.session.PendingTransformEdit()->opening == editBefore.opening,
           "direct-commit-and-cancel-cannot-remove-deferred-draft");
    Expect(fixture.session.Document().value() == original && fixture.session.history.IsCurrent(history) &&
               fixture.session.history.UndoCount() == undoCount && fixture.session.Generation() == generation &&
               SameClipboard(fixture.session.Clipboard(), clipboard),
           "direct-commit-and-cancel-leave-document-history-and-clipboard-unchanged");

    auto ready = PreparePreview(fixture.session, executor);
    Expect(ready.status == DistortWorkStatus::Ready && ready.publication,
           "guard-fixture-prepares-preview-before-cancellation");
    if (ready.publication)
    {
        auto preview = std::move(ready.publication.value());
        Expect(fixture.session.ApplyPreparedTransformPreviewIfCurrent(preview),
               "guard-fixture-publishes-before-cancellation");
    }
    auto cancellation = PrepareCancellation(fixture.session);
    Expect(cancellation.has_value(), "deferred-opening-prepares-ordered-cancellation");
    if (!cancellation)
        return;
    Expect(fixture.session.ApplyPreparedTransformCancellationIfCurrent(cancellation.value()),
           "prepared-deferred-cancellation-applies");
    Expect(!fixture.session.PendingTransformEdit() &&
               fixture.session.DisplayDocument() == &fixture.session.Document().value(),
           "prepared-cancellation-clears-draft-and-display-cache");
    Expect(fixture.session.Document().value() == original && fixture.session.history.IsCurrent(history) &&
               fixture.session.history.UndoCount() == undoCount && fixture.session.Generation() == generation &&
               SameClipboard(fixture.session.Clipboard(), clipboard),
           "prepared-cancellation-preserves-document-history-generation-and-clipboard");
}

void OpeningCacheAndSourceRejection()
{
    Fixture fixture;
    InlineExecutor executor;
    Expect(fixture.session.BeginDeferredTransform(), "cache-fixture-opens-first-deferred-transform");
    SetDistortion(fixture.session, 5, 5);
    auto ready = PreparePreview(fixture.session, executor);
    Expect(ready.publication.has_value(), "first-opening-preview-prepared-for-cache-test");
    if (!ready.publication)
        return;
    auto publication = std::move(ready.publication.value());
    Expect(fixture.session.ApplyPreparedTransformPreviewIfCurrent(publication),
           "first-opening-preview-populates-cache");
    auto cancel = PrepareCancellation(fixture.session);
    Expect(cancel && fixture.session.ApplyPreparedTransformCancellationIfCurrent(cancel.value()),
           "first-opening-closes-through-prepared-cancellation");

    const CanvasDocument* committed = &fixture.session.Document().value();
    Expect(fixture.session.BeginDeferredTransform(), "cache-fixture-reopens-deferred-transform");
    SetDistortion(fixture.session, 6, 5);
    Expect(fixture.session.DisplayDocument() == committed, "closed-opening-cache-is-not-shown-by-reopened-transform");

    auto stale = PreparePreview(fixture.session, executor);
    Expect(stale.publication.has_value(), "source-rejection-preview-prepared");
    auto current = PreparePreview(fixture.session, executor);
    Expect(current.publication && fixture.session.ApplyPreparedTransformPreviewIfCurrent(current.publication.value()) &&
               fixture.session.DisplayDocument() != committed,
           "source-rejection-fixture-first-displays-current-complete-preview");
    fixture.session.EditForTest([](CanvasDocument& document) { document.layers.back().name = L"Changed source"; });
    Expect(fixture.session.DisplayDocument() == &fixture.session.Document().value(),
           "source-generation-change-falls-back-to-current-committed-document");
    if (stale.publication)
    {
        auto oldPublication = std::move(stale.publication.value());
        Expect(!fixture.session.ApplyPreparedTransformPreviewIfCurrent(oldPublication),
               "source-generation-change-refuses-old-preview-token");
    }
}

void RepeatedDeferAndUnchangedRefusals()
{
    Fixture fixture;
    Expect(!fixture.session.DeferTransform(), "defer-without-opening-refuses");
    fixture.session.BeginTransform();
    const auto revision = fixture.session.TransformDraftRevision();
    Expect(fixture.session.DeferTransform(), "existing-inline-edit-can-be-deferred");
    const auto opening = fixture.session.PendingTransformEdit()->opening;
    const auto history = fixture.session.history.CaptureState();
    const auto generation = fixture.session.Generation();
    Expect(fixture.session.DeferTransform() && fixture.session.PendingTransformEdit()->opening == opening &&
               fixture.session.TransformDraftRevision() == revision,
           "repeated-defer-keeps-opening-and-revision-stable");
    Expect(fixture.session.history.IsCurrent(history) && fixture.session.Generation() == generation,
           "defer-does-not-change-history-or-generation");

    InlineExecutor executor;
    auto foreignPrepared = PreparePreview(fixture.session, executor);
    Fixture foreign;
    Expect(foreignPrepared.publication.has_value(), "foreign-token-fixture-prepares-preview");
    if (foreignPrepared.publication)
    {
        auto token = std::move(foreignPrepared.publication.value());
        Expect(!foreign.session.ApplyPreparedTransformPreviewIfCurrent(token),
               "foreign-session-preview-token-remains-refused");
    }

    auto changedHistory = PreparePreview(fixture.session, executor);
    Expect(changedHistory.publication.has_value(), "history-rejection-fixture-prepares-preview");
    fixture.session.history.Reset();
    if (changedHistory.publication)
    {
        auto token = std::move(changedHistory.publication.value());
        Expect(!fixture.session.ApplyPreparedTransformPreviewIfCurrent(token),
               "changed-history-preview-token-remains-refused");
    }
}
} // namespace

int RunTransformDeferredTests()
{
    gFailures = 0;
    gAssertions = 0;
    InlineBehaviorRemainsUnchanged();
    DeferredOpeningAndPublication();
    DeferredDirectGuardsAndPreparedCancellation();
    OpeningCacheAndSourceRejection();
    RepeatedDeferAndUnchangedRefusals();
    std::printf("TRANSFORM DEFERRED TESTS checks=%d failures=%d\n", gAssertions, gFailures);
    return gFailures;
}
