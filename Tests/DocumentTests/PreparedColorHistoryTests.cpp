#include "PreparedColorHistoryTests.h"

#include "../Support/DocumentFixture.h"
#include "ColorEditHistory.h"
#include "ColorEditWork.h"
#include "Document.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <new>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace
{
int gFailures = 0;
int gAssertions = 0;

void Expect(bool condition, const char* name)
{
    ++gAssertions;
    std::printf("%s prepared-color-history.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

constexpr std::array<std::uint8_t, 16> kSourcePixels{0,   0,   0,   255, 64,  64,  64,  255,
                                                     128, 128, 128, 255, 255, 255, 255, 255};
constexpr std::array<std::uint8_t, 16> kGammaPixels{0,   0,   0,   255, 128, 128, 128, 255,
                                                    181, 181, 181, 255, 255, 255, 255, 255};

Raster Bgra(std::span<const std::uint8_t> bytes)
{
    return Raster::Make(4, 1, RasterFormat::Bgra8888, bytes.data(), bytes.size());
}

bool BytesEqual(const Raster& raster, std::span<const std::uint8_t> expected)
{
    if (!raster.HasStorage() || raster.Format() != RasterFormat::Bgra8888 || raster.Width() != 4 ||
        raster.Height() != 1 || expected.size() != kSourcePixels.size())
        return false;
    std::array<std::uint8_t, 16> actual{};
    raster.ReadRegion(0, 0, 4, 1, actual.data(), 16, 0);
    return std::equal(expected.begin(), expected.end(), actual.begin());
}

DocumentSession MakeSession()
{
    const Raster pixels = Bgra(kSourcePixels);
    DocumentFixture document;
    document.id = Guid::Generate();
    document.width = 4;
    document.height = 1;
    ImageLayer layer = ImageLayer::Blank(L"Color history fixture", Vec2{4.0, 1.0});
    const Guid active = layer.id;
    layer.asset = RasterAsset{pixels, Raster::MakeThumbnail(pixels), layer.name};
    document.layers.push_back(std::move(layer));
    DocumentSession session;
    session.ReplaceDocument(std::move(document).Seal(), active);
    return session;
}

FilterSettings GammaLevels()
{
    FilterSettings settings;
    settings.levels.ranges[0].gamma = 2.0;
    return settings;
}

std::optional<ColorEditJobInput> BeginAndPrepareInput(DocumentSession& session)
{
    session.BeginColorEdit(FilterKind::Levels);
    session.UpdateColorEdit(GammaLevels(), false);
    return session.PrepareColorEditCommit();
}

ColorEditJobResult Run(const ColorEditJobInput& input)
{
    InlineExecutor executor;
    return RunColorEditJob(input, executor);
}

bool ResultMatches(const ColorEditJobResult& result, const ColorEditJobResult& expected)
{
    const bool targetMatches =
        result.token.target.has_value() == expected.token.target.has_value() &&
        (!expected.token.target || (result.token.target.value().layerId == expected.token.target.value().layerId &&
                                    result.token.target.value().image == expected.token.target.value().image));
    return result.status == expected.status && result.kind == expected.kind && result.settings == expected.settings &&
           result.image == expected.image && result.thumbnail == expected.thumbnail &&
           result.transform == expected.transform && result.mask == expected.mask && targetMatches &&
           result.token.sessionIdentity == expected.token.sessionIdentity &&
           result.token.documentSerial == expected.token.documentSerial &&
           result.token.generation == expected.token.generation &&
           result.token.restoreSerial == expected.token.restoreSerial &&
           result.token.sequence == expected.token.sequence && result.token.canvasWidth == expected.token.canvasWidth &&
           result.token.canvasHeight == expected.token.canvasHeight &&
           result.token.selection.has_value() == expected.token.selection.has_value();
}

std::optional<PreparedColorEdit> Prepare(DocumentSession& session, const ColorEditJobResult& result,
                                         DocumentHistory::PrepareObserver observer = {}, CancelToken cancellation = {})
{
    return PrepareColorEditPublication(session.Document(), session.ActiveLayerID(), session.Generation(),
                                       session.history.CaptureState(), result, cancellation, observer);
}

bool InstallPriorRedo(DocumentSession& session)
{
    session.BeginEdit(L"Fixture change");
    EditLayerRecordsForTest(session,
                            [](DocumentFixture& document) { document.layers.front().name = L"Changed fixture"; });
    session.EndEdit();
    session.Undo();
    return session.history.CanRedo() && session.history.UndoCount() == 0 &&
           BytesEqual(session.ActiveLayer()->asset->image, kSourcePixels);
}

struct CheckpointAction final
{
    DocumentHistory::PrepareStage target{};
    CancelSource* cancellation = nullptr;
    bool throwAllocation = false;
    bool reached = false;

    static void Checkpoint(void* context, DocumentHistory::PrepareStage stage)
    {
        auto& action = *static_cast<CheckpointAction*>(context);
        if (stage != action.target)
            return;
        action.reached = true;
        if (action.cancellation)
            (void)action.cancellation->Cancel();
        if (action.throwAllocation)
            throw std::bad_alloc{};
    }
};

void ApplyUndoRedoLiteralAndSettings()
{
    DocumentSession session = MakeSession();
    const auto input = BeginAndPrepareInput(session);
    Expect(input.has_value(), "apply.input-ready");
    if (!input)
        return;
    ColorEditJobResult result = Run(input.value());
    Expect(result.status == ColorEditJobStatus::Ready && BytesEqual(result.image, kGammaPixels),
           "apply.worker-matches-literal-bgra-gamma2-reference");
    auto candidate = Prepare(session, result);
    Expect(candidate.has_value(), "apply.factory-prepares-candidate");
    if (!candidate)
        return;
    const bool applied = session.IsPreparedColorEditCurrent(candidate.value()) &&
                         session.ApplyPreparedColorEditResult(candidate.value());
    Expect(applied && !session.HasColorEdit() && session.history.UndoCount() == 1 &&
               session.history.UndoName() == L"Levels" && session.ActiveLayer() && session.ActiveLayer()->asset &&
               BytesEqual(session.ActiveLayer()->asset->image, kGammaPixels),
           "apply.success-publishes-pixels-closes-panel-and-records-one-named-entry");
    Expect(session.filterSettings == GammaLevels(), "apply.applied-settings-persist");
    session.Undo();
    Expect(session.history.CanRedo() && BytesEqual(session.ActiveLayer()->asset->image, kSourcePixels),
           "apply.undo-restores-literal-source-pixels");
    session.Redo();
    Expect(!session.history.CanRedo() && BytesEqual(session.ActiveLayer()->asset->image, kGammaPixels),
           "apply.redo-restores-literal-gamma-pixels");
    session.BeginColorEdit(FilterKind::Levels);
    auto nextSettings = GammaLevels();
    nextSettings.levels = {}; // The normative Levels reopening rule starts its ranges at identity.
    Expect(session.PendingColorEdit().has_value() && session.PendingColorEdit().value().settings == nextSettings,
           "apply.next-levels-panel-preserves-settings-and-resets-ranges-to-identity");
    session.CancelColorEdit();
}

void SavedRevisionDoesNotInvalidateCandidate()
{
    DocumentSession session = MakeSession();
    const auto input = BeginAndPrepareInput(session);
    if (!input)
    {
        Expect(false, "saved.input-ready");
        return;
    }
    ColorEditJobResult result = Run(input.value());
    auto candidate = Prepare(session, result);
    Expect(candidate.has_value(), "saved.candidate-prepared");
    if (!candidate)
        return;
    const Guid sourceRevision = session.history.CurrentRevision();
    session.history.MarkSaved();
    Expect(session.history.CurrentRevision() == sourceRevision && !session.history.IsModified() &&
               session.IsPreparedColorEditCurrent(candidate.value()),
           "saved.mark-saved-does-not-stale-prepared-history");
    const bool applied = session.ApplyPreparedColorEditResult(candidate.value());
    Expect(applied && session.history.IsModified() && session.history.UndoCount() == 1,
           "saved.apply-after-save-creates-modified-new-revision");
    session.Undo();
    Expect(!session.history.IsModified() && session.history.CurrentRevision() == sourceRevision,
           "saved.undo-retains-the-independently-saved-revision");
}

void RetiredPanelSurvivesUntilCandidateRelease()
{
    DocumentSession session = MakeSession();
    session.BeginColorEdit(FilterKind::Levels, true);
    session.UpdateColorEdit(GammaLevels(), true);
    auto input = session.PrepareColorEditPreview();
    Expect(input.has_value(), "panel-retirement.preview-input-ready");
    if (!input)
        return;
    input->lookupPreview = true;
    InlineExecutor executor;
    auto preview = RunColorEditPreviewJob(input.value(), executor);
    std::weak_ptr<const AdjustmentLookup> held = preview.edit.previewLookup;
    const bool previewApplied = session.ApplyColorEditPreview(preview);
    Expect(previewApplied && !held.expired(), "panel-retirement.lookup-published-and-held");
    if (!previewApplied)
        return;
    preview.edit.previewLookup.reset();
    input = session.PrepareColorEditCommit();
    auto result = Run(input.value());
    input.reset();
    auto candidate = Prepare(session, result);
    const bool applied = candidate && session.ApplyPreparedColorEditResult(candidate.value());
    Expect(applied && !session.HasColorEdit() && !held.expired(),
           "panel-retirement.old-lookup-survives-owner-publication");
    candidate.reset();
    Expect(held.expired(), "panel-retirement.old-lookup-releases-with-private-aggregate");
}

void RefuseStaleGenerationAndHistoryEpoch()
{
    {
        DocumentSession session = MakeSession();
        const auto input = BeginAndPrepareInput(session);
        if (!input)
        {
            Expect(false, "stale-generation.input-ready");
            return;
        }
        ColorEditJobResult result = Run(input.value());
        auto candidate = Prepare(session, result);
        Expect(candidate.has_value(), "stale-generation.candidate-prepared");
        if (!candidate)
            return;
        session.EditForTest([](CanvasDocument&) {});
        const auto live = session.Document();
        const auto revision = session.history.CurrentRevision();
        const int undoCount = session.history.UndoCount();
        const bool applied = session.ApplyPreparedColorEditResult(candidate.value());
        Expect(!applied && session.Document() == live && session.history.CurrentRevision() == revision &&
                   session.history.UndoCount() == undoCount && session.HasColorEdit() &&
                   BytesEqual(session.ActiveLayer()->asset->image, kSourcePixels),
               "stale-generation.refusal-preserves-pixels-history-and-panel");
    }
    {
        DocumentSession session = MakeSession();
        const auto input = BeginAndPrepareInput(session);
        if (!input)
        {
            Expect(false, "stale-epoch.input-ready");
            return;
        }
        ColorEditJobResult result = Run(input.value());
        auto candidate = Prepare(session, result);
        Expect(candidate.has_value(), "stale-epoch.candidate-prepared");
        if (!candidate)
            return;
        session.history.Reset();
        const auto live = session.Document();
        const auto revision = session.history.CurrentRevision();
        const int undoCount = session.history.UndoCount();
        const bool applied = session.ApplyPreparedColorEditResult(candidate.value());
        Expect(!applied && session.Document() == live && session.history.CurrentRevision() == revision &&
                   session.history.UndoCount() == undoCount && session.HasColorEdit() &&
                   BytesEqual(session.ActiveLayer()->asset->image, kSourcePixels),
               "stale-epoch.refusal-preserves-pixels-history-and-panel");
    }
}

void RefuseForeignAndMovedAuthority()
{
    DocumentSession owner = MakeSession();
    const auto input = BeginAndPrepareInput(owner);
    if (!input)
    {
        Expect(false, "authority.input-ready");
        return;
    }
    ColorEditJobResult result = Run(input.value());
    auto prepared = Prepare(owner, result);
    Expect(prepared.has_value(), "authority.candidate-prepared");
    if (!prepared)
        return;

    DocumentSession foreign = MakeSession();
    (void)BeginAndPrepareInput(foreign);
    const auto foreignDocument = foreign.Document();
    const auto foreignRevision = foreign.history.CurrentRevision();
    const int foreignUndoCount = foreign.history.UndoCount();
    const bool foreignApply = foreign.ApplyPreparedColorEditResult(prepared.value());
    Expect(!foreignApply && foreign.Document() == foreignDocument &&
               foreign.history.CurrentRevision() == foreignRevision &&
               foreign.history.UndoCount() == foreignUndoCount && foreign.HasColorEdit() &&
               BytesEqual(foreign.ActiveLayer()->asset->image, kSourcePixels),
           "authority.foreign-session-refused-without-live-state-change");

    PreparedColorEdit moved(std::move(prepared.value()));
    (void)moved;
    const auto ownerDocument = owner.Document();
    const auto ownerRevision = owner.history.CurrentRevision();
    const int ownerUndoCount = owner.history.UndoCount();
    const bool movedFromApply = owner.ApplyPreparedColorEditResult(prepared.value());
    Expect(!movedFromApply && owner.Document() == ownerDocument && owner.history.CurrentRevision() == ownerRevision &&
               owner.history.UndoCount() == ownerUndoCount && owner.HasColorEdit() &&
               BytesEqual(owner.ActiveLayer()->asset->image, kSourcePixels),
           "authority.moved-from-candidate-refused-with-panel-and-pixels-intact");
    Expect(owner.ApplyPreparedColorEditResult(moved) && !owner.HasColorEdit() &&
               BytesEqual(owner.ActiveLayer()->asset->image, kGammaPixels) &&
               !owner.ApplyPreparedColorEditResult(moved),
           "authority.destination-publishes-once-after-move");
}

void FailedPreparationPreservesRedoRawResultAndPanel()
{
    constexpr std::array stages{DocumentHistory::PrepareStage::BeforeSnapshot,
                                DocumentHistory::PrepareStage::AfterSnapshot, DocumentHistory::PrepareStage::Entry,
                                DocumentHistory::PrepareStage::StackGrowth, DocumentHistory::PrepareStage::TrimIndex};
    for (std::size_t index = 0; index < stages.size(); ++index)
    {
        DocumentSession session = MakeSession();
        const bool redoReady = InstallPriorRedo(session);
        const auto input = BeginAndPrepareInput(session);
        char fixtureName[80]{};
        std::snprintf(fixtureName, sizeof(fixtureName), "failure.stage-%zu-fixture-has-redo-and-input", index + 1);
        Expect(redoReady && input.has_value(), fixtureName);
        if (!redoReady || !input)
            continue;
        ColorEditJobResult result = Run(input.value());
        const ColorEditJobResult rawResult = result;
        const auto beforeDocument = session.Document();
        const auto beforeRevision = session.history.CurrentRevision();
        const int beforeUndo = session.history.UndoCount();
        const std::size_t beforeRedo = session.history.CaptureState().FutureCount();
        CancelSource cancellation;
        CheckpointAction cancelAction{stages[index], &cancellation, false, false};
        DocumentHistory::PrepareObserver cancelObserver{&CheckpointAction::Checkpoint, &cancelAction};
        auto cancelled = Prepare(session, result, cancelObserver, cancellation.Token());
        const bool cancelPreserved =
            cancelAction.reached && !cancelled && ResultMatches(result, rawResult) &&
            session.Document() == beforeDocument && session.history.CurrentRevision() == beforeRevision &&
            session.history.UndoCount() == beforeUndo && session.history.CaptureState().FutureCount() == beforeRedo &&
            session.HasColorEdit() && BytesEqual(session.ActiveLayer()->asset->image, kSourcePixels);
        char cancelName[80]{};
        std::snprintf(cancelName, sizeof(cancelName), "failure.cancel-stage-%zu-preserves-all", index + 1);
        Expect(cancelPreserved, cancelName);

        CheckpointAction allocationAction{stages[index], nullptr, true, false};
        DocumentHistory::PrepareObserver allocationObserver{&CheckpointAction::Checkpoint, &allocationAction};
        bool threw = false;
        try
        {
            auto allocationFailed = Prepare(session, result, allocationObserver);
            (void)allocationFailed;
        }
        catch (const std::bad_alloc&)
        {
            threw = true;
        }
        const bool allocationPreserved =
            allocationAction.reached && threw && ResultMatches(result, rawResult) &&
            session.Document() == beforeDocument && session.history.CurrentRevision() == beforeRevision &&
            session.history.UndoCount() == beforeUndo && session.history.CaptureState().FutureCount() == beforeRedo &&
            session.HasColorEdit() && BytesEqual(session.ActiveLayer()->asset->image, kSourcePixels);
        char allocationName[80]{};
        std::snprintf(allocationName, sizeof(allocationName), "failure.bad-alloc-stage-%zu-preserves-all", index + 1);
        Expect(allocationPreserved, allocationName);
    }
}

void RetirementAccountingFloors()
{
    constexpr std::size_t kCurveSpareCapacity = 65536;
    DocumentSession session = MakeSession();
    session.BeginColorEdit(FilterKind::Levels);
    FilterSettings settings = GammaLevels();
    session.UpdateColorEdit(settings, false);
    const auto pending = session.PendingColorEdit();
    Expect(pending.has_value(), "retirement.panel-open");
    if (!pending)
        return;

    std::vector<std::uint8_t> previewStorage(512u * 512u * 4u, 0);
    Raster largePreview = Raster::Make(512, 512, RasterFormat::Bgra8888, previewStorage.data(), previewStorage.size());
    if (!largePreview.HasStorage())
    {
        Expect(false, "retirement.preview-raster-allocated");
        return;
    }
    ColorEdit edit = pending.value();
    edit.settings = settings;
    const auto originalCapacity = edit.settings.curves.channels[0].capacity();
    const auto beforeCurve = ColorEditHeldMemoryBytes(edit);
    edit.settings.curves.channels[0].reserve(kCurveSpareCapacity);
    const auto curveGrowth = (edit.settings.curves.channels[0].capacity() - originalCapacity) * sizeof(CurvePoint);
    Expect(ColorEditHeldMemoryBytes(edit) >= beforeCurve + curveGrowth,
           "retirement.settings-spare-capacity-increases-complete-bound");
    const auto beforePreview = ColorEditHeldMemoryBytes(edit);
    edit.previewRaster = largePreview;
    Expect(ColorEditHeldMemoryBytes(edit) >= beforePreview + largePreview.RetirementMemoryBytes(),
           "retirement.preview-owner-increases-complete-bound");
    const auto beforeMask = ColorEditHeldMemoryBytes(edit);
    std::uint64_t tokenMaskFloor = 0;
    bool tokenMaskReady = edit.sourceToken.has_value() && edit.sourceToken.value().target.has_value();
    if (tokenMaskReady)
    {
        const std::vector<std::uint8_t> maskPixels(512 * 512, 255);
        const Raster maskImage = Raster::Make(512, 512, RasterFormat::Gray8, maskPixels.data(), maskPixels.size());
        std::wstring maskName;
        maskName.reserve(65536);
        maskName = L"Held token mask";
        LayerMask mask;
        mask.asset = RasterAsset{maskImage, Raster::MakeThumbnail(maskImage), std::move(maskName)};
        edit.sourceToken.value().target.value().mask = std::move(mask);
        const auto& heldMask = edit.sourceToken.value().target.value().mask.value();
        tokenMaskFloor = heldMask.asset.image.RetirementMemoryBytes() +
                         heldMask.asset.thumbnail.RetirementMemoryBytes() +
                         static_cast<std::uint64_t>(heldMask.asset.name.capacity() + 1) * sizeof(wchar_t);
    }
    const std::uint64_t held = ColorEditHeldMemoryBytes(edit);
    Expect(held >= beforeMask + tokenMaskFloor,
           "retirement.token-mask-increases-complete-bound-by-independent-capacity-floor");
    Expect(tokenMaskReady && tokenMaskFloor > 0, "retirement.token-held-mask-capacity-floor-fixture-ready");

    const std::array<std::uint8_t, 4> leafPixels{9, 8, 7, 255};
    const Raster leaf = Raster::Make(1, 1, RasterFormat::Bgra8888, leafPixels.data(), leafPixels.size());
    const Raster sparse = Raster::MakeSparse(4, 1, RasterFormat::Bgra8888, {}, 0, 0, {RasterTile{2, 0, leaf}}, 0);
    DocumentFixture sparseDocument = DocumentFixture::Copy(session.Document().value());
    sparseDocument.layers.front().asset.value().image = sparse;
    const std::optional<CanvasDocument> sparseOwner{std::move(sparseDocument).Seal()};
    const std::uint64_t sparseRetirement = session.history.RetirementMemoryBytes(sparseOwner);
    Expect(sparse.IsSparse() && sparseRetirement >= sparse.RetirementMemoryBytes() && !sparse.IsMaterialized(),
           "retirement.sparse-possible-cache-accounted-without-materialization");
}

void RunAll()
{
    ApplyUndoRedoLiteralAndSettings();
    SavedRevisionDoesNotInvalidateCandidate();
    RetiredPanelSurvivesUntilCandidateRelease();
    RefuseStaleGenerationAndHistoryEpoch();
    RefuseForeignAndMovedAuthority();
    FailedPreparationPreservesRedoRawResultAndPanel();
    RetirementAccountingFloors();
}
} // namespace

int RunPreparedColorHistoryTests()
{
    gFailures = 0;
    gAssertions = 0;
    RunAll();
    std::printf("prepared-color-history.summary assertions=%d failures=%d\n", gAssertions, gFailures);
    return gFailures;
}
