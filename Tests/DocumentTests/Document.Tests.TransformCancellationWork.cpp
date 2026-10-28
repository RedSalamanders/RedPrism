#include "Document.Tests.TransformCancellationWork.h"

#include "Document.h"
#include "Executor.h"
#include "FloatingOpeningWork.h"
#include "Tests/Support/Support.Tests.DocumentFixture.h"
#include "TransformCancellationWork.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
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
    ++gAssertions;
    std::printf("%s transform-cancellation.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

constexpr std::array<std::uint8_t, 16> kSourcePixels{11, 21, 31, 255, 41,  51,  61,  255,
                                                     71, 81, 91, 255, 101, 111, 121, 255};
constexpr std::array<std::uint8_t, 4> kMaskPixels{255, 128, 64, 0};

struct Fixture final
{
    DocumentSession session;
    Guid sourceID;
    Guid siblingID;
    Raster sourceImage;
};

void MakeFixture(Fixture& fixture)
{
    fixture.sourceImage = Raster::Make(2, 2, RasterFormat::Bgra8888, kSourcePixels.data(), kSourcePixels.size());
    const Raster maskImage = Raster::Make(2, 2, RasterFormat::Gray8, kMaskPixels.data(), kMaskPixels.size());
    DocumentFixture document;
    document.id = Guid::Generate();
    document.width = 8;
    document.height = 8;
    document.selection = DocumentSelection{false, SelectionLoops{{Vec2{0, 0}, Vec2{1, 0}, Vec2{1, 1}, Vec2{0, 1}}}};

    ImageLayer source = ImageLayer::Blank(L"Transform source", Vec2{2, 2});
    source.asset = RasterAsset{fixture.sourceImage, fixture.sourceImage, source.name};
    source.mask = LayerMask{RasterAsset{maskImage, maskImage, L"Source mask"}, true, std::nullopt, true};
    source.transform.sampling = LayerSampling::Nearest;
    fixture.sourceID = source.id;

    ImageLayer sibling = ImageLayer::Blank(L"Sibling", Vec2{2, 2});
    sibling.asset = RasterAsset{fixture.sourceImage, fixture.sourceImage, sibling.name};
    sibling.transform.origin = Vec2{3, 0};
    sibling.transform.sampling = LayerSampling::Nearest;
    fixture.siblingID = sibling.id;
    document.layers = {source, sibling};
    fixture.session.ReplaceDocument(std::move(document).Seal(), fixture.sourceID);

    // Seed redo without depending on CreateDocument's initial history shape.
    fixture.session.RenameLayer(fixture.siblingID, L"Sibling edit");
    fixture.session.Undo();
    fixture.session.history.MarkSaved();
    static_cast<void>(fixture.session.CopySelection());
    fixture.session.SelectLayerTarget(fixture.sourceID, true);
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
    bool modified = false;
    std::uint64_t generation = 0;
    std::uint64_t clipboardSerial = 0;
    std::optional<PixelClipboard> clipboard;
    std::optional<Guid> active;
    std::unordered_set<Guid> selected;
    bool maskSelected = false;
    const void* imageIdentity = nullptr;

    explicit OwnerState(DocumentSession& session)
        : document(session.Document()), history(session.history.CaptureState()),
          revision(session.history.CurrentRevision()), undoCount(session.history.UndoCount()),
          redoCount(history.FutureCount()), undoName(session.history.UndoName()), redoName(session.history.RedoName()),
          modified(session.history.IsModified()), generation(session.Generation()),
          clipboardSerial(session.ClipboardSerial()), clipboard(session.Clipboard()), active(session.ActiveLayerID()),
          selected(session.SelectedLayerIDs()), maskSelected(session.IsMaskSelected())
    {
        if (document && !document->layers.empty() && document->layers.front().asset)
            imageIdentity = document->layers.front().asset->image.Identity();
    }
};

bool SameClipboard(const std::optional<PixelClipboard>& left, const std::optional<PixelClipboard>& right) noexcept
{
    return left.has_value() == right.has_value() &&
           (!left || (left->image == right->image && left->origin == right->origin));
}

bool SameOwnerState(DocumentSession& session, const OwnerState& before)
{
    const auto afterHistory = session.history.CaptureState();
    const auto& document = session.Document();
    const void* imageIdentity = document && !document->layers.empty() && document->layers.front().asset
                                    ? document->layers.front().asset->image.Identity()
                                    : nullptr;
    return document == before.document && imageIdentity == before.imageIdentity &&
           session.history.IsCurrent(before.history) && afterHistory.Depth() == before.history.Depth() &&
           session.history.CurrentRevision() == before.revision && session.history.UndoCount() == before.undoCount &&
           afterHistory.FutureCount() == before.redoCount && session.history.UndoName() == before.undoName &&
           session.history.RedoName() == before.redoName && session.history.IsModified() == before.modified &&
           session.Generation() == before.generation && session.ClipboardSerial() == before.clipboardSerial &&
           SameClipboard(session.Clipboard(), before.clipboard) && session.ActiveLayerID() == before.active &&
           session.SelectedLayerIDs() == before.selected && session.IsMaskSelected() == before.maskSelected;
}

void BeginMovedTransform(DocumentSession& session, double x = 5)
{
    session.BeginTransform();
    LayerTransform draft = session.PendingTransformEdit().value().draft;
    draft.origin.x = x;
    session.PreviewTransform(draft);
}

std::optional<PreparedTransformCancellation> Prepare(DocumentSession& session, CancelToken cancellation = {},
                                                     DocumentHistory::PrepareObserver observer = {})
{
    auto input = session.PrepareTransformCancellationInput();
    if (!input)
        return std::nullopt;
    return DocumentSession::PrepareTransformCancellationPublication(std::move(input.value()), cancellation, observer);
}

void OrdinaryCancellation()
{
    Fixture fixture;
    MakeFixture(fixture);
    Expect(fixture.session.Clipboard().has_value(), "fixture-starts-with-private-clipboard");
    BeginMovedTransform(fixture.session);
    const OwnerState before(fixture.session);
    Expect(fixture.session.TransformCancellationPreparationMemoryBytes() > 0,
           "ordinary-admission-reports-supported-draft");
    auto prepared = Prepare(fixture.session);
    Expect(prepared.has_value() && SameOwnerState(fixture.session, before),
           "ordinary-prepare-keeps-live-document-history-and-session-state");
    if (!prepared)
        return;
    Expect(fixture.session.ApplyPreparedTransformCancellationIfCurrent(prepared.value()),
           "ordinary-cancellation-applies");
    Expect(!fixture.session.PendingTransformEdit() && SameOwnerState(fixture.session, before),
           "ordinary-cancel-drops-only-draft-and-preserves-exact-owner-state");
    Expect(!fixture.session.ApplyPreparedTransformCancellationIfCurrent(prepared.value()),
           "ordinary-publication-is-single-use");
    BeginMovedTransform(fixture.session);
    const OwnerState cancelled(fixture.session);
    CancelSource stop;
    (void)stop.Cancel();
    Expect(!Prepare(fixture.session, stop.Token()), "ordinary-pre-cancelled-work-has-no-publication");
    Expect(SameOwnerState(fixture.session, cancelled) && fixture.session.PendingTransformEdit(),
           "ordinary-pre-cancellation-keeps-live-draft-and-history");
}

void DuplicateCancellation()
{
    Fixture fixture;
    MakeFixture(fixture);
    const CanvasDocument original = fixture.session.Document().value();
    const Guid sourceID = fixture.sourceID;
    const int undoCount = fixture.session.history.UndoCount();
    const std::wstring undoName = fixture.session.history.UndoName();
    const std::wstring redoName = fixture.session.history.RedoName();
    const Guid revision = fixture.session.history.CurrentRevision();

    fixture.session.BeginDuplicateTransform();
    Expect(fixture.session.PendingTransformEdit().has_value() && fixture.session.history.CaptureState().Depth() == 1,
           "duplicate-opening-has-copy-and-one-open-history-level");
    auto prepared = Prepare(fixture.session);
    Expect(prepared.has_value(), "duplicate-cancellation-prepares");
    if (!prepared)
        return;
    Expect(fixture.session.ApplyPreparedTransformCancellationIfCurrent(prepared.value()),
           "duplicate-cancellation-applies");
    Expect(fixture.session.Document().value() == original &&
               fixture.session.Document()->layers.front().asset->image.Identity() ==
                   original.layers.front().asset->image.Identity() &&
               fixture.session.ActiveLayerID() == sourceID && !fixture.session.PendingTransformEdit(),
           "duplicate-cancel-restores-exact-document-active-source-and-clears-draft");
    Expect(fixture.session.history.CaptureState().Depth() == 0 && fixture.session.history.UndoCount() == undoCount &&
               fixture.session.history.CurrentRevision() == revision &&
               fixture.session.history.UndoName() == undoName && fixture.session.history.RedoName() == redoName,
           "duplicate-cancel-closes-named-scope-without-history-entry");
    Expect(!fixture.session.ApplyPreparedTransformCancellationIfCurrent(prepared.value()),
           "duplicate-publication-is-single-use");
}

void NestedDuplicateLeavesOuterEditAndRedo()
{
    Fixture fixture;
    MakeFixture(fixture);
    const std::wstring undoName = fixture.session.history.UndoName();
    const std::wstring redoName = fixture.session.history.RedoName();
    fixture.session.BeginEdit(L"Outer edit");
    fixture.session.BeginDuplicateTransform();
    Expect(fixture.session.history.CaptureState().Depth() == 2, "nested-duplicate-opens-depth-two");
    auto prepared = Prepare(fixture.session);
    Expect(prepared && fixture.session.ApplyPreparedTransformCancellationIfCurrent(prepared.value()),
           "nested-duplicate-cancel-applies");
    Expect(fixture.session.history.CaptureState().Depth() == 1 && fixture.session.history.UndoName() == undoName &&
               fixture.session.history.RedoName() == redoName && fixture.session.Document()->layers.size() == 2,
           "nested-cancel-keeps-outer-scope-copy-removed-and-history-names");
    fixture.session.EndEdit();
    Expect(fixture.session.history.CaptureState().Depth() == 0 && fixture.session.history.CanRedo() &&
               fixture.session.history.RedoName() == redoName,
           "unchanged-outer-close-preserves-redo");

    Fixture named;
    MakeFixture(named);
    const int namedUndoCount = named.session.history.UndoCount();
    named.session.BeginEdit(L"Outer edit");
    named.session.BeginDuplicateTransform();
    auto namedCancellation = Prepare(named.session);
    Expect(namedCancellation && named.session.ApplyPreparedTransformCancellationIfCurrent(namedCancellation.value()) &&
               named.session.history.CaptureState().Depth() == 1,
           "named-outer-cancel-returns-to-outer-depth");
    EditLayerRecordsForTest(named.session,
                            [](DocumentFixture& document) { document.layers.back().name = L"Outer result"; });
    named.session.EndEdit();
    Expect(named.session.history.UndoName() == L"Outer edit" && named.session.history.UndoCount() == namedUndoCount + 1,
           "nested-duplicate-cancel-preserves-outer-entry-name");
}

struct HistoryInterruption final
{
    DocumentHistory::PrepareStage stage;
    CancelSource* cancellation = nullptr;
    bool throwing = false;
    bool reached = false;
};

void InterruptHistory(void* opaque, DocumentHistory::PrepareStage stage)
{
    auto& state = *static_cast<HistoryInterruption*>(opaque);
    if (stage != state.stage)
        return;
    state.reached = true;
    if (state.throwing)
        throw std::bad_alloc{};
    static_cast<void>(state.cancellation->Cancel());
}

void OpenChangedDuplicate(DocumentSession& session)
{
    session.BeginDuplicateTransform();
    // Change an independent source record so PrepareEnd must build a history entry. Without this change,
    // cancellation restores the opening exactly and AfterSnapshot, Entry and StackGrowth are not visited.
    EditLayerRecordsForTest(session, [](DocumentFixture& document)
                            { document.layers.back().name = L"Independent source change"; });
}

void DuplicateWorkerCancellationAndHistoryFaults()
{
    constexpr std::array stages{DocumentHistory::PrepareStage::AfterSnapshot, DocumentHistory::PrepareStage::Entry,
                                DocumentHistory::PrepareStage::StackGrowth, DocumentHistory::PrepareStage::TrimIndex};
    for (const auto stage : stages)
    {
        for (const bool throwing : {false, true})
        {
            Fixture fixture;
            MakeFixture(fixture);
            OpenChangedDuplicate(fixture.session);
            const OwnerState before(fixture.session);
            CancelSource cancellation;
            HistoryInterruption interruption{stage, &cancellation, throwing};
            bool failed = false;
            try
            {
                auto prepared = Prepare(fixture.session, cancellation.Token(), {InterruptHistory, &interruption});
                failed = !throwing && !prepared;
            }
            catch (const std::bad_alloc&)
            {
                failed = throwing;
            }
            Expect(interruption.reached,
                   throwing ? "history-allocation-fault-stage-reached" : "worker-cancel-stage-reached");
            Expect(failed && SameOwnerState(fixture.session, before) &&
                       fixture.session.PendingTransformEdit().has_value(),
                   throwing ? "history-allocation-failure-keeps-live-draft-and-history"
                            : "worker-cancellation-keeps-live-draft-and-history");
        }
    }
}

void Refusals()
{
    {
        auto fixtureOwner = std::make_unique<Fixture>();
        auto& fixture = *fixtureOwner;
        MakeFixture(fixture);
        BeginMovedTransform(fixture.session);
        auto prepared = Prepare(fixture.session);
        LayerTransform newer = fixture.session.PendingTransformEdit().value().draft;
        newer.origin.x += 1;
        fixture.session.PreviewTransform(newer);
        Expect(prepared && !fixture.session.ApplyPreparedTransformCancellationIfCurrent(prepared.value()) &&
                   fixture.session.PendingTransformEdit()->draft == newer,
               "newer-draft-revision-refuses-old-cancellation");
    }
    {
        auto fixtureOwner = std::make_unique<Fixture>();
        auto& fixture = *fixtureOwner;
        MakeFixture(fixture);
        BeginMovedTransform(fixture.session);
        auto prepared = Prepare(fixture.session);
        EditLayerRecordsForTest(fixture.session,
                                [](DocumentFixture& document) { document.layers.front().name = L"Changed source"; });
        const OwnerState changed(fixture.session);
        Expect(prepared && !fixture.session.ApplyPreparedTransformCancellationIfCurrent(prepared.value()) &&
                   SameOwnerState(fixture.session, changed),
               "changed-source-refuses-with-state-intact");
    }
    {
        auto fixtureOwner = std::make_unique<Fixture>();
        auto& fixture = *fixtureOwner;
        MakeFixture(fixture);
        BeginMovedTransform(fixture.session);
        auto prepared = Prepare(fixture.session);
        fixture.session.SelectLayer(fixture.siblingID);
        const auto active = fixture.session.ActiveLayerID();
        const OwnerState selected(fixture.session);
        Expect(prepared && !fixture.session.ApplyPreparedTransformCancellationIfCurrent(prepared.value()) &&
                   fixture.session.ActiveLayerID() == active && !fixture.session.PendingTransformEdit() &&
                   SameOwnerState(fixture.session, selected),
               "changed-active-layer-refuses-old-cancellation");
    }
    {
        auto fixtureOwner = std::make_unique<Fixture>();
        auto& fixture = *fixtureOwner;
        MakeFixture(fixture);
        BeginMovedTransform(fixture.session);
        auto prepared = Prepare(fixture.session);
        fixture.session.history.Reset();
        const OwnerState reset(fixture.session);
        Expect(prepared && !fixture.session.ApplyPreparedTransformCancellationIfCurrent(prepared.value()) &&
                   SameOwnerState(fixture.session, reset),
               "changed-history-epoch-refuses-with-state-intact");
    }
    {
        auto fixtureOwner = std::make_unique<Fixture>();
        auto& fixture = *fixtureOwner;
        MakeFixture(fixture);
        BeginMovedTransform(fixture.session);
        auto prepared = Prepare(fixture.session);
        CanvasDocument replacement = fixture.session.Document().value();
        fixture.session.ReplaceDocument(std::move(replacement), fixture.sourceID);
        const OwnerState replaced(fixture.session);
        Expect(prepared && !fixture.session.ApplyPreparedTransformCancellationIfCurrent(prepared.value()) &&
                   SameOwnerState(fixture.session, replaced) &&
                   fixture.session.Document()->layers.front().id == fixture.sourceID,
               "same-id-document-replacement-refuses-old-cancellation");
    }
    {
        auto fixtureOwner = std::make_unique<Fixture>();
        auto& fixture = *fixtureOwner;
        MakeFixture(fixture);
        BeginMovedTransform(fixture.session);
        auto input = fixture.session.PrepareTransformCancellationInput();
        Expect(input.has_value(), "move-only-input-created");
        if (input)
        {
            TransformCancellationInput owned = std::move(input.value());
            auto movedFrom = DocumentSession::PrepareTransformCancellationPublication(std::move(input.value()));
            auto prepared = DocumentSession::PrepareTransformCancellationPublication(std::move(owned));
            Expect(!movedFrom && prepared.has_value(), "moved-from-input-refused-and-owner-input-works");
        }
    }
    {
        auto fixtureOwner = std::make_unique<Fixture>();
        auto& fixture = *fixtureOwner;
        MakeFixture(fixture);
        BeginMovedTransform(fixture.session);
        auto prepared = Prepare(fixture.session);
        auto foreignOwner = std::make_unique<Fixture>();
        auto& foreign = *foreignOwner;
        MakeFixture(foreign);
        foreign.session.ReplaceDocument(fixture.session.Document().value(), fixture.sourceID);
        BeginMovedTransform(foreign.session);
        const OwnerState before(foreign.session);
        Expect(prepared && !foreign.session.ApplyPreparedTransformCancellationIfCurrent(prepared.value()) &&
                   SameOwnerState(foreign.session, before),
               "foreign-session-refuses-prepared-restore");
    }
    {
        auto fixtureOwner = std::make_unique<Fixture>();
        auto& fixture = *fixtureOwner;
        MakeFixture(fixture);
        Expect(fixture.session.TransformCancellationPreparationMemoryBytes() == 0 &&
                   !fixture.session.PrepareTransformCancellationInput(),
               "absent-transform-input-refused");
    }
}

void FloatingTransformRefusal()
{
    Fixture fixture;
    MakeFixture(fixture);
    fixture.session.SelectLayerTarget(fixture.sourceID, false);
    InlineExecutor executor;
    auto input = fixture.session.PrepareFloatingOpeningInput();
    Expect(input.has_value(), "floating-refusal-opening-input-created");
    if (!input)
        return;
    auto opened = DocumentSession::PrepareFloatingOpeningPublication(std::move(input.value()), executor, {}, {}, {});
    Expect(opened.publication && fixture.session.ApplyPreparedFloatingOpeningIfCurrent(opened.publication.value()),
           "floating-refusal-fixture-opens");
    fixture.session.BeginTransform();
    Expect(fixture.session.PendingTransformEdit() && fixture.session.PendingTransformEdit()->floating &&
               fixture.session.TransformCancellationPreparationMemoryBytes() == 0 &&
               !fixture.session.PrepareTransformCancellationInput(),
           "floating-transform-cancellation-input-refused");
}
} // namespace

int RunTransformCancellationWorkTests()
{
    gFailures = gAssertions = 0;
    OrdinaryCancellation();
    DuplicateCancellation();
    NestedDuplicateLeavesOuterEditAndRedo();
    DuplicateWorkerCancellationAndHistoryFaults();
    Refusals();
    FloatingTransformRefusal();
    std::printf("TRANSFORM CANCELLATION WORK TESTS checks=%d failures=%d\n", gAssertions, gFailures);
    return gFailures;
}
