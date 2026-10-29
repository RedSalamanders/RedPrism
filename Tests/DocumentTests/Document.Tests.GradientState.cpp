#include "Document.Tests.GradientState.h"
#include "Tests/Support/Support.Tests.DocumentFixture.h"

#include "BrushStroke.h"
#include "Document.h"
#include "RasterEditWork.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <span>
#include <utility>

// Public model surface only. Private BeginGradientImpl/ApplyRasterEditImpl/CommitTransform paths are reached only
// through BeginDeferredGradient, ApplyGradientIfCurrent, and public guarded operations; direct private-path probes
// are intentionally unavailable in this staging suite.
namespace
{
int gFailures = 0;
int gAssertions = 0;

void Expect(bool condition, const char* name)
{
    ++gAssertions;
    std::printf("%s gradientstate.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

constexpr std::array<std::uint8_t, 16> kBlackWhite4{0,   0,   0,   255, 85,  85,  85,  255,
                                                    170, 170, 170, 255, 255, 255, 255, 255};
constexpr std::array<std::uint8_t, 4> kGrayBlackWhite4{0, 85, 170, 255};

Raster SparseBlackImage()
{
    return Raster::MakeSparse(4, 1, RasterFormat::Bgra8888, Raster{}, 0, 0, {}, 0);
}

Raster GrayRaster()
{
    constexpr std::array<std::uint8_t, 4> bytes{20, 70, 140, 220};
    return Raster::Make(4, 1, RasterFormat::Gray8, bytes.data(), bytes.size());
}

bool BytesEqual(const Raster& raster, RasterFormat format, int width, int height,
                std::span<const std::uint8_t> expected)
{
    const std::size_t channels = format == RasterFormat::Gray8 ? 1u : 4u;
    if (!raster.HasStorage() || raster.Format() != format || raster.Width() != width || raster.Height() != height ||
        expected.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * channels)
        return false;
    std::array<std::uint8_t, 16> bytes{};
    if (expected.size() > bytes.size())
        return false;
    raster.ReadRegion(0, 0, width, height, bytes.data(), width * static_cast<int>(channels), 0);
    return std::equal(expected.begin(), expected.end(), bytes.begin());
}

bool PixelBytesEqual(const Raster& raster, RasterFormat format, int width, int height,
                     std::span<const std::uint8_t> expected)
{
    return BytesEqual(raster, format, width, height, expected);
}

bool Initialize(DocumentSession& session, bool withMask = false)
{
    session.CreateDocument(4, 1, true);
    const auto target = session.ActiveLayerID();
    const Raster source = SparseBlackImage();
    if (!target || !source.HasStorage() || !session.Document() || session.Document()->layers.empty())
        return false;
    const Raster thumbnail = Raster::MakeThumbnail(source);
    if (!thumbnail.HasStorage())
        return false;
    Raster mask;
    Raster maskThumbnail;
    if (withMask)
    {
        mask = GrayRaster();
        if (!mask.HasStorage())
            return false;
        maskThumbnail = Raster::MakeThumbnail(mask);
        if (!maskThumbnail.HasStorage())
            return false;
    }
    EditLayerRecordsForTest(session,
                            [&](DocumentFixture& document)
                            {
                                ImageLayer& layer = document.layers.front();
                                layer.asset = RasterAsset{source, thumbnail, layer.name};
                                if (withMask)
                                    layer.mask = LayerMask{RasterAsset{mask, maskThumbnail, L"Gradient state mask"}};
                            });
    session.SelectLayerTarget(target.value(), false);
    session.SetForegroundColor(0xFF000000u);
    session.SetBackgroundColor(0xFFFFFFFFu);
    session.gradientSettings.shape = GradientShape::Linear;
    session.gradientSettings.style = GradientStyle::ForegroundToBackground;
    session.gradientSettings.reversed = false;
    session.gradientSettings.opacity = 1.0;
    if (withMask)
        session.SelectLayerTarget(target.value(), true);
    return session.ActiveLayer() != nullptr && session.Document().has_value();
}

bool BeginLine(DocumentSession& session, Vec2 start = Vec2{0.5, 0.5}, Vec2 end = Vec2{3.5, 0.5})
{
    return session.BeginDeferredGradient(start) &&
           (session.MoveGradient(std::nullopt, end),
            session.PendingGradient() != nullptr && session.PendingGradient()->HasLine());
}

std::optional<JobToken> Token(DocumentSession& session, std::uint64_t acquisitionSequence = 91)
{
    return session.CaptureJobToken(session.ActiveLayerID(), acquisitionSequence);
}

std::optional<RasterEditInput> PendingInput(const DocumentSession& session)
{
    const GradientEdit* const gradient = session.PendingGradient();
    const ImageLayer* const layer = session.ActiveLayer();
    if (!gradient || !layer || !session.Document())
        return std::nullopt;
    const auto colors = session.GradientColors();
    RasterEditInput input;
    input.layer = *layer;
    input.selection = session.Document()->selection;
    input.canvasWidth = session.Document()->width;
    input.canvasHeight = session.Document()->height;
    input.pixelLimit = kMaxLayerPixels;
    input.mask = gradient->mask;
    input.kind = RasterEditKind::Gradient;
    input.color = colors[0];
    input.start = gradient->start;
    input.end = gradient->end;
    input.shape = session.gradientSettings.shape;
    input.endColor = colors[1];
    input.opacity = session.gradientSettings.opacity;
    return input;
}

RasterEditPreviewResult MakePreview(const DocumentSession& session)
{
    const auto input = PendingInput(session);
    return input ? RunRasterEditPreview(*input) : RasterEditPreviewResult{};
}

RasterEditResult MakeCommit(const DocumentSession& session)
{
    const auto input = PendingInput(session);
    return input ? RunRasterEdit(*input) : RasterEditResult{};
}

bool CandidateImageOwned(const ImageLayer& candidate, const Raster& retained, bool mask)
{
    return mask ? candidate.mask && candidate.mask->asset.image == retained
                : candidate.asset && candidate.asset->image == retained;
}

void DeferredMetadataAndInlineCompatibility()
{
    DocumentSession session;
    Expect(Initialize(session), "metadata.fixture-ready");
    if (!session.Document() || !session.ActiveLayer() || !session.ActiveLayer()->asset)
        return;
    const CanvasDocument openingDocument = session.Document().value();
    const Raster source = session.ActiveLayer()->asset->image;
    const bool initiallySparse = source.IsSparse() && !source.IsMaterialized();
    const int history = session.history.UndoCount();
    Expect(session.BeginDeferredGradient(Vec2{0.5, 0.5}), "metadata.deferred-begin");
    const GradientEdit* gradient = session.PendingGradient();
    Expect(gradient && gradient->deferred && !gradient->raster && gradient->sequence == 1 &&
               gradient->publishedSequence == 0,
           "metadata.pending-is-null-raster-and-sequence-one");
    session.MoveGradient(std::nullopt, Vec2{3.5, 0.5});
    session.RefreshGradient();
    session.SetForegroundColor(0xFF000000u);
    session.SetBackgroundColor(0xFFFFFFFFu);
    gradient = session.PendingGradient();
    Expect(gradient && gradient->raster == nullptr && gradient->sequence == 5 && gradient->HasLine(),
           "metadata.move-refresh-palette-advance-only-metadata");
    const auto ends = session.GradientColors();
    Expect(ends[0] == std::array<double, 4>{0, 0, 0, 1} && ends[1] == std::array<double, 4>{1, 1, 1, 1},
           "metadata.image-palette-endpoints-remain-explicit-black-and-white");
    Expect(session.Document() && session.Document().value() == openingDocument &&
               session.ActiveLayer()->asset->image == source && source.IsSparse() && !source.IsMaterialized() &&
               initiallySparse && session.history.UndoCount() == history,
           "metadata.source-sparsity-document-and-history-unchanged");
    Expect(!session.ResolveGradient(), "metadata.resolve-refuses-unsettled-deferred");
    session.CommitGradient();
    Expect(session.PendingGradient() && session.Document() && session.Document().value() == openingDocument &&
               session.history.UndoCount() == history,
           "metadata.direct-commit-cannot-settle-deferred");
    session.InvertPixels();
    Expect(session.PendingGradient() && session.Document() && session.Document().value() == openingDocument &&
               session.history.UndoCount() == history,
           "metadata.surface-invert-refuses-before-transform-or-history-change");
    session.BeginTransform();
    Expect(!session.PendingTransformEdit() && session.PendingGradient() && session.Document() &&
               session.Document().value() == openingDocument && session.history.UndoCount() == history,
           "metadata.resolve-wrapper-does-not-commit-transform-or-history");

    session.MoveGradient(Vec2{1.5, 0.5}, Vec2{1.9, 0.5});
    session.EndGradientDrag();
    Expect(session.PendingGradient() == nullptr && session.Document() &&
               session.Document().value() == openingDocument && session.history.UndoCount() == history,
           "metadata-short-end-drag-drops-edit-without-document-change");
    Expect(BeginLine(session), "metadata-reopen-after-short-drag");
    if (session.PendingGradient())
    {
        const auto firstOpening = session.PendingGradient()->opening;
        const auto beforeRedragSequence = session.PendingGradient()->sequence;
        Expect(session.BeginDeferredGradient(Vec2{1.5, 0.5}) && session.PendingGradient()->opening == firstOpening &&
                   session.PendingGradient()->sequence > beforeRedragSequence && !session.PendingGradient()->raster,
               "metadata-same-target-redrag-reuses-opening-and-refreshes-metadata");
        const auto cancelledOpening = session.PendingGradient()->opening;
        session.CancelGradient();
        Expect(BeginLine(session) && session.PendingGradient()->opening > cancelledOpening,
               "metadata-cancel-reopen-allocates-new-opening");
        session.EndGradientDrag();
        Expect(session.PendingGradient() && session.PendingGradient()->HasLine(),
               "metadata-valid-end-drag-keeps-deferred-line-pending");
    }
}

void PreviewPublicationUsesIndependentSequences()
{
    DocumentSession session;
    Expect(Initialize(session, true), "publish.fixture-ready");
    session.SelectLayerTarget(session.ActiveLayerID().value(), false);
    if (!BeginLine(session) || !session.PendingGradient())
    {
        Expect(false, "publish.line-ready");
        return;
    }
    const std::uint64_t opening = session.PendingGradient()->opening;
    const std::uint64_t offeredMetadataSequence = session.PendingGradient()->sequence;
    const auto token = Token(session);
    RasterEditPreviewResult preview = MakePreview(session);
    Expect(token && token->sequence == 91 && offeredMetadataSequence == 2 &&
               preview.status == RasterEditStatus::Ready && preview.stroke,
           "publish.fixture-token-91-differs-from-metadata-sequence-2");
    if (!token || !preview.stroke)
        return;
    std::shared_ptr<BrushStroke> offered = std::move(preview.stroke);
    session.SetBackgroundColor(0xFF00FF00u); // sequence 3 is pending while sequence 2 is in flight.
    const bool acceptedOlder =
        session.PublishGradientPreview(std::move(offered), token.value(), opening, offeredMetadataSequence);
    Expect(acceptedOlder && !offered && session.PendingGradient()->sequence == 3 &&
               session.PendingGradient()->publishedSequence == 2 && session.PendingGradient()->raster,
           "publish-accepts-offered-sequence-older-than-current-pending");
    Expect(session.PendingGradient()->publishedGeneration == session.Generation(),
           "publish-retains-the-source-generation-required-for-gpu-adoption");

    for (const std::uint64_t refusedSequence : {2ull, 1ull})
    {
        RasterEditPreviewResult duplicate = MakePreview(session);
        if (!duplicate.stroke)
        {
            Expect(false, "publish.refusal-candidate-prepared");
            continue;
        }
        std::shared_ptr<BrushStroke> retained = std::move(duplicate.stroke);
        const BrushStroke* const address = retained.get();
        const bool accepted =
            session.PublishGradientPreview(std::move(retained), token.value(), opening, refusedSequence);
        Expect(!accepted && retained.get() == address && session.PendingGradient()->publishedSequence == 2,
               refusedSequence == 2 ? "publish-duplicate-sequence-rejected-and-owned"
                                    : "publish-backward-sequence-rejected-and-owned");
    }

    RasterEditPreviewResult stalePreview = MakePreview(session);
    std::shared_ptr<BrushStroke> staleCandidate = std::move(stalePreview.stroke);
    const BrushStroke* const staleAddress = staleCandidate.get();
    JobToken badTarget = token.value();
    badTarget.target->layerId = Guid::Generate();
    Expect(!session.PublishGradientPreview(std::move(staleCandidate), badTarget, opening, 3) &&
               staleCandidate.get() == staleAddress,
           "publish-stale-target-refused-with-candidate-owned");

    auto maskInput = PendingInput(session).value();
    maskInput.mask = true;
    RasterEditPreviewResult wrongMaskPreview = RunRasterEditPreview(maskInput);
    std::shared_ptr<BrushStroke> wrongMaskCandidate = std::move(wrongMaskPreview.stroke);
    const BrushStroke* const wrongMaskAddress = wrongMaskCandidate.get();
    session.SelectLayerTarget(session.ActiveLayerID().value(), true);
    Expect(!session.IsMaskSelected(), "metadata.unsettled-deferred-refuses-target-switch");
    const bool acceptedWrongMask =
        session.PublishGradientPreview(std::move(wrongMaskCandidate), token.value(), opening, 3);
    Expect(!acceptedWrongMask && wrongMaskCandidate.get() == wrongMaskAddress,
           "publish-private-stroke-mask-mismatch-refuses-and-preserves-candidate");

    RasterEditPreviewResult staleGenerationPreview = MakePreview(session);
    std::shared_ptr<BrushStroke> staleGenerationCandidate = std::move(staleGenerationPreview.stroke);
    const BrushStroke* const generationAddress = staleGenerationCandidate.get();
    constexpr std::array<std::uint8_t, 16> changedSourceBytes{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    const Raster changedSource =
        Raster::Make(4, 1, RasterFormat::Bgra8888, changedSourceBytes.data(), changedSourceBytes.size());
    const Raster changedThumbnail = Raster::MakeThumbnail(changedSource);
    EditLayerRecordsForTest(session,
                            [&](DocumentFixture& document)
                            {
                                document.layers.front().asset->image = changedSource;
                                document.layers.front().asset->thumbnail = changedThumbnail;
                            });
    Expect(!session.PublishGradientPreview(std::move(staleGenerationCandidate), token.value(), opening, 3) &&
               staleGenerationCandidate.get() == generationAddress,
           "publish-stale-generation-source-refused-and-owned");

    session.CancelGradient();
    Expect(BeginLine(session), "publish.reopen-ready");
    if (session.PendingGradient())
    {
        RasterEditPreviewResult reopenedPreview = MakePreview(session);
        std::shared_ptr<BrushStroke> reopenedCandidate = std::move(reopenedPreview.stroke);
        const BrushStroke* const reopenedAddress = reopenedCandidate.get();
        Expect(!session.PublishGradientPreview(std::move(reopenedCandidate), token.value(), opening, 2) &&
                   reopenedCandidate.get() == reopenedAddress && session.PendingGradient()->publishedSequence == 0,
               "publish-stale-opening-after-cancel-reopen-refused-and-owned");
    }

    DocumentSession redrag;
    Expect(Initialize(redrag) && BeginLine(redrag), "redrag.fixture-ready");
    const auto redragToken = Token(redrag);
    auto redragPreview = MakePreview(redrag);
    if (redragToken && redragPreview.stroke)
    {
        const auto redragOpening = redrag.PendingGradient()->opening;
        const auto redragSequence = redrag.PendingGradient()->sequence;
        auto oldCandidate = MakePreview(redrag);
        Expect(redrag.PublishGradientPreview(std::move(redragPreview.stroke), redragToken.value(), redragOpening,
                                             redragSequence),
               "redrag.first-preview-published");
        Expect(redrag.BeginDeferredGradient(Vec2{1.5, 0.5}) && !redrag.PendingGradient()->raster &&
                   !redrag.PendingGradient()->HasLine(),
               "redrag.no-line-clears-previous-pixels");
        const auto* owned = oldCandidate.stroke.get();
        Expect(!redrag.PublishGradientPreview(std::move(oldCandidate.stroke), redragToken.value(), redragOpening,
                                              redragSequence) &&
                   oldCandidate.stroke.get() == owned,
               "redrag.old-running-preview-cannot-resurrect-no-line-pixels");
    }
}

void LiteralPreparedApplyAndRefusals()
{
    {
        DocumentSession session;
        Expect(Initialize(session), "apply.image-fixture-ready");
        const CanvasDocument original = session.Document().value();
        const int history = session.history.UndoCount();
        Expect(BeginLine(session), "apply.image-line-ready");
        const auto token = Token(session);
        RasterEditResult result = MakeCommit(session);
        Expect(token && token->sequence == 91 && result.status == RasterEditStatus::Ready && result.layer &&
                   result.layer->asset &&
                   PixelBytesEqual(result.layer->asset->image, RasterFormat::Bgra8888, 4, 1, kBlackWhite4),
               "apply.image-worker-literal-black-white-four");
        if (token && result.layer)
        {
            ImageLayer prepared = std::move(result.layer.value());
            const auto opening = session.PendingGradient()->opening;
            const auto sequence = session.PendingGradient()->sequence;
            Expect(session.ApplyGradientIfCurrent(std::move(prepared), token.value(), opening, sequence) &&
                       session.PendingGradient() == nullptr && session.history.UndoCount() == history + 1 &&
                       session.history.UndoName() == L"Gradient" && session.ActiveLayer()->asset &&
                       PixelBytesEqual(session.ActiveLayer()->asset->image, RasterFormat::Bgra8888, 4, 1, kBlackWhite4),
                   "apply.image-current-metadata-commits-one-gradient-entry");
            session.Undo();
            Expect(session.Document() && session.Document().value() == original &&
                       session.history.UndoCount() == history,
                   "apply.image-undo-restores-exact-source-document");
        }
    }
    {
        DocumentSession session;
        Expect(Initialize(session, true), "apply.mask-fixture-ready");
        const CanvasDocument original = session.Document().value();
        const int history = session.history.UndoCount();
        Expect(BeginLine(session), "apply.mask-line-ready");
        const auto maskEnds = session.GradientColors();
        Expect(maskEnds[0] == std::array<double, 4>{0, 0, 0, 1} && maskEnds[1] == std::array<double, 4>{1, 1, 1, 1},
               "apply.mask-palette-endpoints-map-black-to-white");
        const auto token = Token(session);
        RasterEditResult result = MakeCommit(session);
        Expect(token && result.status == RasterEditStatus::Ready && result.layer && result.layer->mask &&
                   PixelBytesEqual(result.layer->mask->asset.image, RasterFormat::Gray8, 4, 1, kGrayBlackWhite4),
               "apply.mask-worker-literal-black-white-four");
        if (token && result.layer)
        {
            ImageLayer prepared = std::move(result.layer.value());
            const auto opening = session.PendingGradient()->opening;
            const auto sequence = session.PendingGradient()->sequence;
            Expect(session.ApplyGradientIfCurrent(std::move(prepared), token.value(), opening, sequence) &&
                       session.PendingGradient() == nullptr && session.history.UndoCount() == history + 1 &&
                       session.history.UndoName() == L"Gradient Mask" && session.ActiveLayer()->mask &&
                       PixelBytesEqual(session.ActiveLayer()->mask->asset.image, RasterFormat::Gray8, 4, 1,
                                       kGrayBlackWhite4),
                   "apply.mask-current-metadata-commits-one-gradient-mask-entry");
            session.Undo();
            Expect(session.Document() && session.Document().value() == original &&
                       session.history.UndoCount() == history,
                   "apply.mask-undo-restores-exact-source-document");
        }
    }
}

void ApplyRefusalPreservesPreparedLayerAndOpening()
{
    const auto refusal = [](const char* label, bool reopen, bool advanceSequence, bool staleGeneration)
    {
        DocumentSession session;
        Expect(Initialize(session), label);
        if (!BeginLine(session) || !session.PendingGradient())
        {
            Expect(false, "apply-refusal.line-ready");
            return;
        }
        const std::uint64_t opening = session.PendingGradient()->opening;
        const std::uint64_t sequence = session.PendingGradient()->sequence;
        const auto token = Token(session);
        RasterEditResult result = MakeCommit(session);
        if (!token || !result.layer || result.status != RasterEditStatus::Ready)
        {
            Expect(false, "apply-refusal.candidate-ready");
            return;
        }
        ImageLayer prepared = std::move(result.layer.value());
        const bool mask = session.PendingGradient()->mask;
        const Raster retained = mask ? prepared.mask->asset.image : prepared.asset->image;
        if (reopen)
        {
            session.CancelGradient();
            (void)BeginLine(session);
        }
        else if (advanceSequence)
            session.MoveGradient(std::nullopt, Vec2{2.5, 0.5});
        else if (staleGeneration)
            session.EditForTest([](CanvasDocument&) {});
        const CanvasDocument before = session.Document().value();
        const int history = session.history.UndoCount();
        const std::uint64_t currentOpening = session.PendingGradient()->opening;
        const bool accepted = session.ApplyGradientIfCurrent(std::move(prepared), token.value(), opening, sequence);
        Expect(!accepted && CandidateImageOwned(prepared, retained, mask) && session.PendingGradient() &&
                   session.PendingGradient()->opening == currentOpening && session.Document() &&
                   session.Document().value() == before && session.history.UndoCount() == history,
               reopen            ? "apply-refusal-reopened-opening-retains-candidate-and-pending"
               : advanceSequence ? "apply-refusal-newer-metadata-sequence-retains-candidate-and-pending"
                                 : "apply-refusal-stale-source-generation-retains-candidate-and-pending");
    };
    refusal("apply-refusal.newer-sequence-fixture", false, true, false);
    refusal("apply-refusal.reopen-fixture", true, false, false);
    refusal("apply-refusal.generation-fixture", false, false, true);
}

void RunAll()
{
    DeferredMetadataAndInlineCompatibility();
    PreviewPublicationUsesIndependentSequences();
    LiteralPreparedApplyAndRefusals();
    ApplyRefusalPreservesPreparedLayerAndOpening();
}
} // namespace

int RunGradientStateTests()
{
    gFailures = 0;
    gAssertions = 0;
    RunAll();
    std::printf("gradientstate.summary assertions=%d failures=%d\n", gAssertions, gFailures);
    return gFailures;
}
