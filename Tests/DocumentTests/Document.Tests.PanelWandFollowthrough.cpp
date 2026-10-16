#include "Document.Tests.PanelWandFollowthrough.h"

#include "../Support/DocumentFixture.h"
#include "ColorEditHistory.h"
#include "ColorEditWork.h"
#include "Document.h"
#include "Executor.h"
#include "Selection.h"
#include "WandLayerSelectionWork.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <utility>

namespace
{
int gFailures = 0;
int gAssertions = 0;

void Expect(bool condition, const char* name)
{
    ++gAssertions;
    gFailures += condition ? 0 : 1;
    std::printf("%s panel-wand-followthrough.%s\n", condition ? "PASS" : "FAIL", name);
}

constexpr std::array<std::uint8_t, 8> kPixels{0, 0, 128, 255, 64, 0, 0, 255}; // BGRA: red 128, then blue 64.
constexpr std::array<std::uint8_t, 8> kLevelsPixels{0, 0, 0, 255, 64, 0, 0, 255};
constexpr Vec2 kWandPoint{1.5, 0.5};

Raster Source()
{
    return Raster::Make(2, 1, RasterFormat::Bgra8888, kPixels.data(), kPixels.size());
}

DocumentSelection OpeningSelection()
{
    return DocumentSelection{false, {SelectionLoop{Vec2{0, 0}, Vec2{1, 0}, Vec2{1, 1}, Vec2{0, 1}}}};
}

bool SameToken(const JobToken& left, const JobToken& right) noexcept
{
    if (left.sessionIdentity != right.sessionIdentity || left.documentSerial != right.documentSerial ||
        left.generation != right.generation || left.restoreSerial != right.restoreSerial ||
        left.canvasWidth != right.canvasWidth || left.canvasHeight != right.canvasHeight ||
        left.sequence != right.sequence || left.selection != right.selection ||
        left.target.has_value() != right.target.has_value())
        return false;
    if (!left.target)
        return true;
    const auto& a = left.target.value();
    const auto& b = right.target.value();
    return a.layerId == b.layerId && a.image == b.image && a.transform == b.transform && a.mask == b.mask &&
           a.isGroup == b.isGroup && a.adjustment == b.adjustment;
}

bool PixelBytes(const Raster& raster, const std::array<std::uint8_t, 8>& expected)
{
    if (!raster.HasStorage() || raster.Format() != RasterFormat::Bgra8888 || raster.Width() != 2 ||
        raster.Height() != 1 || raster.Stride() != 8 || raster.Data() == nullptr)
        return false;
    for (std::size_t i = 0; i < expected.size(); ++i)
        if (raster.Data()[i] != expected[i])
            return false;
    return true;
}

std::unique_ptr<DocumentSession> MakeSession()
{
    auto document = std::make_unique<DocumentFixture>();
    document->id = Guid::Generate();
    document->width = 2;
    document->height = 1;
    ImageLayer layer = ImageLayer::Blank(L"Panel Wand follow-through", Vec2{2, 1});
    const Guid active = layer.id;
    layer.transform.sampling = LayerSampling::Nearest;
    const Raster image = Source();
    layer.asset = RasterAsset{image, Raster::MakeThumbnail(image), layer.name};
    document->layers.push_back(std::move(layer));
    auto session = std::make_unique<DocumentSession>();
    session->ReplaceDocument(std::move(*document).Seal(), active);
    session->SetSelection(OpeningSelection(), L"Opening selection");
    session->history.Reset();
    return session;
}

FilterSettings LevelsOutputBlack()
{
    FilterSettings settings;
    settings.levels.ranges[0].outputWhite = 0;
    return settings;
}

WandSettings WandSettingsFor(bool composite) noexcept
{
    WandSettings settings;
    settings.tolerance = 0;
    settings.sampleSize = WandSampleSize::Point;
    settings.contiguous = true;
    settings.sampleAllLayers = composite;
    return settings;
}

std::unique_ptr<std::optional<WandLayerSelectionInput>> CaptureWandHeap(DocumentSession& session, bool composite)
{
    // Direct new-initialization from the API's prvalue constructs this large optional in its heap result slot.
    return std::unique_ptr<std::optional<WandLayerSelectionInput>>(new std::optional<WandLayerSelectionInput>(
        composite ? session.PreparePanelWandCompositeSelectionInput(kWandPoint, WandSettingsFor(true), false)
                  : session.PreparePanelWandLayerSelectionInput(kWandPoint, WandSettingsFor(false), false)));
}

std::unique_ptr<std::optional<PreparedWandLayerSelection>> PrepareWandHeap(DocumentSession& session, bool composite)
{
    auto input = CaptureWandHeap(session, composite);
    if (!input || !input->has_value())
        return {};
    if (composite)
    {
        return std::unique_ptr<std::optional<PreparedWandLayerSelection>>(new std::optional<PreparedWandLayerSelection>(
            DocumentSession::PrepareWandCompositeSelectionPublication(std::move(input->value()), Source())));
    }
    return std::unique_ptr<std::optional<PreparedWandLayerSelection>>(new std::optional<PreparedWandLayerSelection>(
        DocumentSession::PrepareWandLayerSelectionPublication(std::move(input->value()))));
}

std::unique_ptr<std::optional<ColorEditJobInput>> PreparePreviewHeap(const DocumentSession& session)
{
    // The returned optional is constructed directly in heap storage; do not route this large prvalue through
    // make_unique.
    return std::unique_ptr<std::optional<ColorEditJobInput>>(
        new std::optional<ColorEditJobInput>(session.PrepareColorEditPreview()));
}

std::unique_ptr<std::optional<ColorEditJobInput>> PrepareCommitHeap(const DocumentSession& session)
{
    return std::unique_ptr<std::optional<ColorEditJobInput>>(
        new std::optional<ColorEditJobInput>(session.PrepareColorEditCommit()));
}

std::unique_ptr<ColorEditPreviewResult> RunPreviewHeap(const ColorEditJobInput& input)
{
    InlineExecutor executor;
    // Same-type prvalue return is guaranteed-elided into the allocated result object.
    return std::unique_ptr<ColorEditPreviewResult>(new ColorEditPreviewResult(RunColorEditPreviewJob(input, executor)));
}

std::unique_ptr<ColorEditJobResult> RunCommitHeap(const ColorEditJobInput& input)
{
    InlineExecutor executor;
    return std::unique_ptr<ColorEditJobResult>(new ColorEditJobResult(RunColorEditJob(input, executor)));
}

std::unique_ptr<std::optional<PreparedColorEdit>> PrepareColorEditPublicationHeap(const DocumentSession& session,
                                                                                  const ColorEditJobResult& result)
{
    return std::unique_ptr<std::optional<PreparedColorEdit>>(new std::optional<PreparedColorEdit>(
        PrepareColorEditPublication(session.Document(), session.ActiveLayerID(), session.Generation(),
                                    session.history.CaptureState(), result)));
}

bool ApplyWand(DocumentSession& session, bool composite)
{
    auto prepared = PrepareWandHeap(session, composite);
    return prepared && prepared->has_value() && session.IsPreparedWandLayerSelectionCurrent(prepared->value()) &&
           session.ApplyPreparedWandLayerSelectionIfCurrent(prepared->value());
}

void VerifyPanelWandThenLevels(bool composite)
{
    auto sessionOwner = MakeSession();
    DocumentSession& session = *sessionOwner;
    const auto opening = OpeningSelection();
    session.BeginColorEdit(FilterKind::Levels, true);
    session.UpdateColorEdit(LevelsOutputBlack(), true);
    const JobToken panelOpeningToken = session.PendingColorEdit()->sourceToken.value();
    auto beforePreview = PreparePreviewHeap(session);
    auto beforeCommit = PrepareCommitHeap(session);
    Expect(beforePreview && beforePreview->has_value() && beforeCommit && beforeCommit->has_value(),
           composite ? "composite.opening.inputs-captured" : "layer.opening.inputs-captured");
    if (!beforePreview || !beforePreview->has_value() || !beforeCommit || !beforeCommit->has_value())
        return;
    auto oldPreview = RunPreviewHeap(beforePreview->value());
    auto oldCommit = RunCommitHeap(beforeCommit->value());

    Expect(ApplyWand(session, composite),
           composite ? "composite.wand-selection-applied" : "layer.wand-selection-applied");
    Expect(session.Selection().has_value() && session.Selection() != std::optional<DocumentSelection>{opening} &&
               session.PendingColorEdit() && session.PendingColorEdit()->selection == opening &&
               session.PendingColorEdit()->sourceToken &&
               SameToken(session.PendingColorEdit()->sourceToken.value(), panelOpeningToken),
           composite ? "composite.wand-keeps-panel-opening-coverage-and-token"
                     : "layer.wand-keeps-panel-opening-coverage-and-token");

    auto stalePublication = PrepareColorEditPublicationHeap(session, *oldCommit);
    const bool stalePreviewRefused = !session.ApplyColorEditPreview(*oldPreview);
    const bool staleCommitRefused = !session.ApplyColorEditResult(*oldCommit);
    Expect(stalePreviewRefused && staleCommitRefused && stalePublication && !stalePublication->has_value() &&
               session.HasColorEdit(FilterKind::Levels),
           composite ? "composite.pre-wand-results-refused-panel-retained"
                     : "layer.pre-wand-results-refused-panel-retained");

    auto afterPreviewInput = PreparePreviewHeap(session);
    Expect(afterPreviewInput && afterPreviewInput->has_value(),
           composite ? "composite.post-wand-preview-input-accepted" : "layer.post-wand-preview-input-accepted");
    if (!afterPreviewInput || !afterPreviewInput->has_value())
        return;
    auto preview = RunPreviewHeap(afterPreviewInput->value());
    const bool previewPixels = preview && preview->status == ColorEditJobStatus::Ready &&
                               PixelBytes(preview->edit.previewRaster, kLevelsPixels) &&
                               session.ApplyColorEditPreview(*preview);
    Expect(previewPixels, composite ? "composite.preview-renders-opening-selection-coverage"
                                    : "layer.preview-renders-opening-selection-coverage");

    auto afterCommitInput = PrepareCommitHeap(session);
    Expect(afterCommitInput && afterCommitInput->has_value(),
           composite ? "composite.post-wand-commit-input-accepted" : "layer.post-wand-commit-input-accepted");
    if (!afterCommitInput || !afterCommitInput->has_value())
        return;
    auto result = RunCommitHeap(afterCommitInput->value());
    auto prepared = PrepareColorEditPublicationHeap(session, *result);
    const bool committed = prepared && prepared->has_value() && session.IsPreparedColorEditCurrent(prepared->value()) &&
                           session.ApplyPreparedColorEditResult(prepared->value());
    Expect(committed && !session.HasColorEdit() && PixelBytes(session.ActiveLayer()->asset->image, kLevelsPixels) &&
               session.history.UndoCount() == 2 && session.history.UndoName() == L"Levels",
           composite ? "composite.commit-publishes-levels-after-wand" : "layer.commit-publishes-levels-after-wand");
    if (!committed)
        return;
    session.Undo();
    Expect(PixelBytes(session.ActiveLayer()->asset->image, kPixels) && session.Selection().has_value() &&
               session.history.UndoName() == L"Magic Wand",
           composite ? "composite.undo-levels-restores-pixels-retains-wand"
                     : "layer.undo-levels-restores-pixels-retains-wand");
    session.Undo();
    Expect(session.Selection() == std::optional<DocumentSelection>{opening} &&
               PixelBytes(session.ActiveLayer()->asset->image, kPixels) && session.history.UndoCount() == 0,
           composite ? "composite.undo-wand-restores-opening-selection" : "layer.undo-wand-restores-opening-selection");
}

void ArbitrarySelectionMutationDoesNotAuthorizeColorWork()
{
    auto sessionOwner = MakeSession();
    DocumentSession& session = *sessionOwner;
    session.BeginColorEdit(FilterKind::Levels, true);
    session.UpdateColorEdit(LevelsOutputBlack(), true);
    Expect(ApplyWand(session, false), "arbitrary-mutation.authorized-panel-wand-applied");
    session.SetSelection(std::nullopt, L"Direct selection mutation");
    Expect(!session.Selection() && session.HasColorEdit(FilterKind::Levels),
           "arbitrary-mutation.public-selection-change-applied-with-panel-open");

    auto input = PrepareCommitHeap(session);
    Expect(input && input->has_value(), "arbitrary-mutation.fresh-commit-input-captured");
    if (!input || !input->has_value())
        return;
    auto result = RunCommitHeap(input->value());
    auto publication = PrepareColorEditPublicationHeap(session, *result);
    const bool strict = !session.CanApplyJobResult(input->value().token, session.ColorEditSequence()) && publication &&
                        !publication->has_value() && session.HasColorEdit(FilterKind::Levels);
    Expect(strict, "arbitrary-mutation.no-panel-wand-authorization-after-direct-selection-change");
}

void CancelAfterWandStartsNextPanelFromCurrentSelection()
{
    auto sessionOwner = MakeSession();
    DocumentSession& session = *sessionOwner;
    session.BeginColorEdit(FilterKind::Levels, true);
    Expect(session.HasColorEdit(FilterKind::Levels), "cancel.levels-panel-opened-before-wand");
    Expect(ApplyWand(session, false), "cancel.authorized-wand-selection-applied");
    const auto wandSelection = session.Selection();
    const int undoCount = session.history.UndoCount();
    const auto undoName = session.history.UndoName();
    session.CancelColorEdit();
    const bool cancelled = !session.HasColorEdit() && session.Selection() == wandSelection &&
                           session.history.UndoCount() == undoCount && session.history.UndoName() == undoName;
    Expect(cancelled, "cancel.preserves-wand-selection-and-history");

    session.BeginColorEdit(FilterKind::Levels, true);
    const auto currentToken = session.CaptureJobToken(session.ActiveLayerID());
    const bool freshOpening = currentToken && session.PendingColorEdit() &&
                              session.PendingColorEdit()->selection == wandSelection &&
                              session.PendingColorEdit()->sourceToken &&
                              session.PendingColorEdit()->sourceToken->selection == currentToken->selection;
    Expect(freshOpening, "cancel.next-opening-captures-current-selection");
    session.CancelColorEdit();
}
} // namespace

int RunPanelWandFollowthroughTests()
{
    gFailures = 0;
    gAssertions = 0;
    VerifyPanelWandThenLevels(false);
    VerifyPanelWandThenLevels(true);
    ArbitrarySelectionMutationDoesNotAuthorizeColorWork();
    CancelAfterWandStartsNextPanelFromCurrentSelection();
    std::printf("panel-wand-followthrough.assertions=%d failures=%d\n", gAssertions, gFailures);
    return gFailures;
}
