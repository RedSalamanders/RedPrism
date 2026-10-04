#include "TransformCommitWorkTests.h"

#include "../Support/DocumentFixture.h"
#include "TransformCommitWork.h"

#include <array>
#include <cstdio>
#include <new>
#include <utility>

namespace
{
int gFailures = 0;
int gAssertions = 0;

void Expect(bool value, const char* name)
{
    ++gAssertions;
    std::printf("%s transform-commit-work.%s\n", value ? "PASS" : "FAIL", name);
    gFailures += value ? 0 : 1;
}

void Initialize(DocumentSession& session)
{
    session.CreateDocument(32, 24, false);
    auto layer = ImageLayer::Blank(L"Source", {2, 2});
    constexpr std::array<std::uint8_t, 16> bytes{0, 0, 255, 255, 0, 255, 0, 255, 255, 0, 0, 255, 255, 255, 255, 255};
    const auto image = Raster::Make(2, 2, RasterFormat::Bgra8888, bytes.data(), bytes.size());
    layer.asset = RasterAsset{image, image, L"Source"};
    layer.transform.sampling = LayerSampling::Nearest;
    EditLayerRecordsForTest(session, [&](DocumentFixture& document) { document.layers.push_back(layer); });
    session.SelectLayer(layer.id);
}

void Move(DocumentSession& session, double x)
{
    session.BeginTransform();
    auto value = session.PendingTransformEdit()->draft;
    value.origin = {x, 3};
    session.PreviewTransform(value);
}

TransformCommitResult Prepare(DocumentSession& session, Executor& executor, CancelToken cancellation = {},
                              DocumentHistory::PrepareObserver observer = {})
{
    auto input = session.PrepareTransformCommitInput();
    return input ? DocumentSession::PrepareTransformCommitPublication(std::move(input.value()), executor, cancellation,
                                                                      observer)
                 : TransformCommitResult{};
}

void PublicationAndHistory()
{
    DocumentSession session;
    Initialize(session);
    const auto source = session.Document().value();
    const auto active = session.ActiveLayerID();
    const auto selected = session.SelectedLayerIDs();
    const auto history = session.history.CaptureState();
    InlineExecutor executor;
    Move(session, 7);
    Expect(session.TransformCommitPreparationMemoryBytes() > 0, "admission-complete-peak");
    auto prepared = Prepare(session, executor);
    Expect(prepared.status == TransformPixelsStatus::Ready && prepared.publication.has_value(), "prepare-ready");
    Expect(session.Document().value() == source && session.PendingTransformEdit().has_value(),
           "prepare-keeps-live-opening");
    Expect(session.history.IsCurrent(history), "prepare-keeps-history");
    if (!prepared.publication)
        return;
    Expect(session.ApplyPreparedTransformCommitIfCurrent(prepared.publication.value()), "apply-current");
    Expect(!session.PendingTransformEdit(), "apply-clears-edit");
    Expect(session.ActiveLayer()->transform.origin == Vec2{7, 3}, "apply-exact-placement");
    Expect(session.ActiveLayer()->asset->image == source.layers.back().asset->image, "affine-shares-pixels");
    Expect(session.ActiveLayerID() == active && session.SelectedLayerIDs() == selected, "preserves-target-selection");
    Expect(session.history.UndoCount() == static_cast<int>(history.PastCount()) + 1 &&
               session.history.UndoName() == L"Transform Layer",
           "one-named-entry");
    Expect(!session.ApplyPreparedTransformCommitIfCurrent(prepared.publication.value()), "apply-once");
    session.Undo();
    Expect(session.Document().value() == source && session.ActiveLayerID() == active, "undo-exact-opening");
    session.Redo();
    Expect(session.ActiveLayer()->transform.origin == Vec2{7, 3}, "redo-exact-after");
}

void DuplicateClosure()
{
    DocumentSession session;
    Initialize(session);
    const auto source = session.Document().value();
    const auto active = session.ActiveLayerID();
    const auto count = session.history.UndoCount();
    session.BeginDuplicateTransform();
    auto moved = session.PendingTransformEdit()->draft;
    moved.origin = {9, 4};
    session.PreviewTransform(moved);
    Expect(session.history.CaptureState().Depth() == 1, "duplicate-open-depth");
    InlineExecutor executor;
    auto prepared = Prepare(session, executor);
    Expect(prepared.publication.has_value(), "duplicate-prepared");
    if (!prepared.publication)
        return;
    Expect(session.ApplyPreparedTransformCommitIfCurrent(prepared.publication.value()), "duplicate-apply");
    Expect(session.history.CaptureState().Depth() == 0, "duplicate-closes-outer-depth");
    Expect(session.history.UndoCount() == count + 1 && session.history.UndoName() == L"Duplicate Layer",
           "duplicate-original-name-one-entry");
    Expect(session.Document()->layers.size() == 2 && session.ActiveLayer()->transform == moved, "duplicate-after");
    session.Undo();
    Expect(session.Document().value() == source && session.ActiveLayerID() == active,
           "duplicate-undo-original-document");
    session.BeginDuplicateTransform();
    Expect(session.PendingTransformEdit().has_value() && session.Document()->layers.size() == 2,
           "duplicate-next-opening-not-blocked-by-retired-state");
    session.CancelTransform();
    Expect(session.Document().value() == source, "duplicate-next-cancel-restores");
}

void StaleAndConsumedInput()
{
    DocumentSession session;
    Initialize(session);
    Move(session, 5);
    InlineExecutor executor;
    auto input = session.PrepareTransformCommitInput();
    Expect(input.has_value(), "input-captured");
    if (!input)
        return;
    auto moved = std::move(input.value());
    const auto empty = DocumentSession::PrepareTransformCommitPublication(std::move(input.value()), executor);
    Expect(empty.status == TransformPixelsStatus::Invalid && !empty.publication, "moved-from-input-refused");
    auto prepared = DocumentSession::PrepareTransformCommitPublication(std::move(moved), executor);
    Expect(prepared.publication.has_value(), "moved-owning-input-works");
    auto newer = session.PendingTransformEdit()->draft;
    newer.origin.x = 11;
    session.PreviewTransform(newer);
    const auto state = session.history.CaptureState();
    const auto opening = session.Document().value();
    Expect(prepared.publication && !session.ApplyPreparedTransformCommitIfCurrent(prepared.publication.value()),
           "newer-draft-refuses-old-result");
    Expect(session.Document().value() == opening && session.history.IsCurrent(state) &&
               session.PendingTransformEdit()->draft == newer,
           "stale-keeps-newer-draft-document-history");
    auto changedPrepared = Prepare(session, executor);
    EditLayerRecordsForTest(session, [](DocumentFixture& document) { document.layers.back().name = L"Changed"; });
    Expect(changedPrepared.publication &&
               !session.ApplyPreparedTransformCommitIfCurrent(changedPrepared.publication.value()),
           "source-mutation-refuses");
    auto sourcePrepared = Prepare(session, executor);
    session.CreateDocument(16, 16, true);
    Expect(sourcePrepared.publication &&
               !session.ApplyPreparedTransformCommitIfCurrent(sourcePrepared.publication.value()),
           "replacement-refuses");
}

struct HistoryStop final
{
    DocumentHistory::PrepareStage target;
    CancelSource* cancel = nullptr;
    bool throwing = false;
    bool visited = false;
};

void StopAt(void* context, DocumentHistory::PrepareStage stage)
{
    auto& state = *static_cast<HistoryStop*>(context);
    if (stage != state.target)
        return;
    state.visited = true;
    if (state.throwing)
        throw std::bad_alloc{};
    (void)state.cancel->Cancel();
}

void HistoryFaults()
{
    constexpr std::array stages{DocumentHistory::PrepareStage::BeforeSnapshot,
                                DocumentHistory::PrepareStage::AfterSnapshot, DocumentHistory::PrepareStage::Entry,
                                DocumentHistory::PrepareStage::StackGrowth, DocumentHistory::PrepareStage::TrimIndex};
    InlineExecutor executor;
    for (const auto stage : stages)
    {
        for (const bool throwing : {false, true})
        {
            DocumentSession session;
            Initialize(session);
            Move(session, 8);
            const auto source = session.Document().value();
            const auto state = session.history.CaptureState();
            CancelSource cancel;
            HistoryStop stop{stage, &cancel, throwing};
            bool failed = false;
            try
            {
                const auto result = Prepare(session, executor, cancel.Token(), {StopAt, &stop});
                failed = !throwing && result.status == TransformPixelsStatus::Cancelled && !result.publication;
            }
            catch (const std::bad_alloc&)
            {
                failed = throwing;
            }
            Expect(stop.visited, throwing ? "fault-stage-reached" : "cancel-stage-reached");
            Expect(failed, throwing ? "allocation-propagates-no-publication" : "cancel-no-publication");
            Expect(session.Document().value() == source && session.history.IsCurrent(state) &&
                       session.PendingTransformEdit().has_value(),
                   "failure-keeps-opening-and-pending-edit");
        }
    }
}

void GroupBudgetKeepsLaterOriginals()
{
    // A virtual untouched 99,990,000-pixel layer costs only a sparse storage header. Two one-pixel targets
    // cannot each grow to 10,000 pixels: whichever is processed first must keep the other's original reserved.
    DocumentFixture source;
    source.id = Guid::Generate();
    source.width = 128;
    source.height = 128;
    auto untouched = ImageLayer::Blank(L"Untouched", {30000, 3333});
    const auto large = Raster::MakeSparse(30000, 3333, RasterFormat::Bgra8888, {}, 0, 0, {}, 0);
    untouched.asset = RasterAsset{large, {}, L"Untouched"};
    constexpr std::array<std::uint8_t, 4> red{0, 0, 255, 255};
    const auto image = Raster::Make(1, 1, RasterFormat::Bgra8888, red.data(), red.size());
    auto first = ImageLayer::Blank(L"First", {1, 1});
    first.asset = RasterAsset{image, {}, L"First"};
    first.transform.sampling = LayerSampling::Nearest;
    auto second = first;
    second.id = Guid::Generate();
    second.name = L"Second";
    source.layers = {untouched, first, second};
    const CanvasDocument sourceDocument = std::move(source).Seal();
    TransformEdit edit;
    edit.layerID = first.id;
    edit.draft.size = {1, 1};
    edit.group = TransformGroup{edit.draft, {{second.id, second.transform}, {first.id, first.transform}}};
    edit.corners = std::array<Vec2, 4>{Vec2{0, 0}, Vec2{100, 0}, Vec2{100, 100}, Vec2{0, 100}};
    InlineExecutor executor;
    auto result = PrepareTransformPixels(sourceDocument, edit, executor);
    Expect(ValidateDocument(sourceDocument) == DocumentStatus::Ok, "budget-virtual-opening-valid");
    Expect(result.status == TransformPixelsStatus::Ready && result.document && result.budgetRefused,
           "budget-group-refusal-publishes-complete-valid-plan");
    if (result.document)
    {
        Expect(ValidateDocument(result.document.value()) == DocumentStatus::Ok, "budget-after-never-exceeds-cap");
        Expect(result.document->layers[1].asset->image == image && result.document->layers[2].asset->image == image,
               "budget-both-growths-refused-to-preserve-originals");
        Expect(!large.IsMaterialized() && result.document->layers[0].asset->image == large,
               "budget-untouched-huge-source-shared-without-materialization");
    }
}
} // namespace

int RunTransformCommitWorkTests()
{
    gFailures = gAssertions = 0;
    PublicationAndHistory();
    DuplicateClosure();
    StaleAndConsumedInput();
    HistoryFaults();
    GroupBudgetKeepsLaterOriginals();
    std::printf("TRANSFORM COMMIT WORK TESTS checks=%d failures=%d\n", gAssertions, gFailures);
    return gFailures;
}
