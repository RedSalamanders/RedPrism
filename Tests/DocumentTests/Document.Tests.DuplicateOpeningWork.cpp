#include "Document.Tests.DuplicateOpeningWork.h"

#include "Document.h"
#include "DuplicateOpeningWork.h"
#include "Executor.h"
#include "Tests/Support/Support.Tests.DocumentFixture.h"
#include "TransformCancellationWork.h"
#include "TransformCommitWork.h"

#include <array>
#include <cstdio>
#include <new>
#include <optional>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

namespace
{
int gFailures = 0;
int gAssertions = 0;

void Expect(bool condition, const char* name) noexcept
{
    ++gAssertions;
    std::printf("%s duplicate-opening.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

constexpr std::array<std::uint8_t, 16> kImageBytes{11, 21, 31, 255, 41,  51,  61,  255,
                                                   71, 81, 91, 255, 101, 111, 121, 255};
constexpr std::array<std::uint8_t, 4> kMaskBytes{255, 128, 64, 0};
constexpr const wchar_t* kSourceName = L"Literal transform source";

struct Fixture final
{
    DocumentSession session;
    Guid sourceID;
    Guid siblingID;
    Raster image;
    Raster mask;

    Fixture()
    {
        image = Raster::Make(2, 2, RasterFormat::Bgra8888, kImageBytes.data(), kImageBytes.size());
        mask = Raster::Make(2, 2, RasterFormat::Gray8, kMaskBytes.data(), kMaskBytes.size());

        DocumentFixture document;
        document.id = Guid::Generate();
        document.width = 8;
        document.height = 8;
        document.selection = DocumentSelection{false, SelectionLoops{{Vec2{0, 0}, Vec2{2, 0}, Vec2{2, 2}, Vec2{0, 2}}}};

        ImageLayer source = ImageLayer::Blank(kSourceName, Vec2{2, 2});
        source.asset = RasterAsset{image, image, source.name};
        source.transform = LayerTransform{{1, 2}, {2, 2}, 0.25, true, false, LayerSampling::Nearest};
        source.mask = LayerMask{RasterAsset{mask, mask, L"Literal source mask"}, true, std::nullopt, true};
        sourceID = source.id;

        ImageLayer sibling = ImageLayer::Blank(L"Literal sibling", Vec2{2, 2});
        sibling.asset = RasterAsset{image, image, sibling.name};
        sibling.transform.origin = Vec2{4, 1};
        sibling.transform.sampling = LayerSampling::Nearest;
        siblingID = sibling.id;
        document.layers = {source, sibling};

        session.ReplaceDocument(std::move(document).Seal(), sourceID);
        session.history.Reset();
        // Retain a real redo branch while the source state is saved.
        session.RenameLayer(siblingID, L"Redo branch edit");
        session.Undo();
        session.history.MarkSaved();
        static_cast<void>(session.CopySelection());
        session.SelectLayerTarget(sourceID, true);
    }
};

struct OwnerState final
{
    std::optional<CanvasDocument> document;
    DocumentHistory::StateView history;
    std::uint64_t generation = 0;
    std::uint64_t clipboardSerial = 0;
    std::optional<PixelClipboard> clipboard;
    std::optional<Guid> active;
    std::unordered_set<Guid> selected;
    bool maskSelected = false;
    Guid revision;
    int undoCount = 0;
    bool modified = false;
    std::wstring undoName;
    std::wstring redoName;

    explicit OwnerState(DocumentSession& session)
        : document(session.Document()), history(session.history.CaptureState()), generation(session.Generation()),
          clipboardSerial(session.ClipboardSerial()), clipboard(session.Clipboard()), active(session.ActiveLayerID()),
          selected(session.SelectedLayerIDs()), maskSelected(session.IsMaskSelected()),
          revision(session.history.CurrentRevision()), undoCount(session.history.UndoCount()),
          modified(session.history.IsModified()), undoName(session.history.UndoName()),
          redoName(session.history.RedoName())
    {
    }
};

bool SameClipboard(const std::optional<PixelClipboard>& left, const std::optional<PixelClipboard>& right) noexcept
{
    return left.has_value() == right.has_value() &&
           (!left || (left->image == right->image && left->origin == right->origin));
}

bool SameOwnerState(DocumentSession& session, const OwnerState& before)
{
    const auto state = session.history.CaptureState();
    return session.Document() == before.document && session.history.IsCurrent(before.history) &&
           state.Depth() == before.history.Depth() && state.PastCount() == before.history.PastCount() &&
           state.FutureCount() == before.history.FutureCount() &&
           session.history.CurrentRevision() == before.revision && session.history.UndoCount() == before.undoCount &&
           session.history.UndoName() == before.undoName && session.history.RedoName() == before.redoName &&
           session.history.IsModified() == before.modified && session.Generation() == before.generation &&
           session.ClipboardSerial() == before.clipboardSerial &&
           SameClipboard(session.Clipboard(), before.clipboard) && session.ActiveLayerID() == before.active &&
           session.SelectedLayerIDs() == before.selected && session.IsMaskSelected() == before.maskSelected;
}

bool SameRestoredOwnerState(DocumentSession& session, const OwnerState& before)
{
    // Opening and rollback each publish a new generation/structural epoch. They restore the exact history
    // roots and visible state while invalidating workers captured before either publication.
    const auto state = session.history.CaptureState();
    return session.Document() == before.document && !session.history.IsCurrent(before.history) &&
           state.Depth() == before.history.Depth() && state.PastCount() == before.history.PastCount() &&
           state.FutureCount() == before.history.FutureCount() &&
           session.history.CurrentRevision() == before.revision && session.history.UndoCount() == before.undoCount &&
           session.history.UndoName() == before.undoName && session.history.RedoName() == before.redoName &&
           session.history.IsModified() == before.modified && session.Generation() == before.generation + 2 &&
           session.ClipboardSerial() == before.clipboardSerial &&
           SameClipboard(session.Clipboard(), before.clipboard) && session.ActiveLayerID() == before.active &&
           session.SelectedLayerIDs() == before.selected && session.IsMaskSelected() == before.maskSelected;
}

DuplicateOpeningResult Prepare(DocumentSession& session, Executor& executor, CancelToken cancellation = {},
                               DocumentHistory::PrepareObserver historyObserver = {},
                               DuplicateOpeningObserver openingObserver = {})
{
    auto input = session.PrepareDuplicateOpeningInput();
    return input ? DocumentSession::PrepareDuplicateOpeningPublication(std::move(input.value()), executor, cancellation,
                                                                       historyObserver, openingObserver)
                 : DuplicateOpeningResult{};
}

TransformCommitResult PrepareCommit(DocumentSession& session, Executor& executor)
{
    auto input = session.PrepareTransformCommitInput();
    return input ? DocumentSession::PrepareTransformCommitPublication(std::move(input.value()), executor, {}, {})
                 : TransformCommitResult{};
}

const ImageLayer* FindLayer(const CanvasDocument& document, const Guid& id) noexcept
{
    for (const ImageLayer& layer : document.layers)
        if (layer.id == id)
            return &layer;
    return nullptr;
}

void OpeningAndCommit()
{
    Fixture fixture;
    auto& session = fixture.session;
    const OwnerState before(session);
    const CanvasDocument sourceDocument = session.Document().value();
    const auto& source = sourceDocument.layers.front();
    InlineExecutor executor;

    Expect(!session.PendingTransformEdit() && session.DuplicateOpeningPreparationMemoryBytes() > 0,
           "settled-source-admitted");
    auto opening = Prepare(session, executor);
    Expect(opening.status == DuplicateOpeningStatus::Ready && opening.publication && opening.publication->Restoration(),
           "opening-publication-and-restore-ready");
    Expect(SameOwnerState(session, before), "preparation-does-not-publish-document-history-or-clipboard");
    if (!opening.publication)
        return;

    auto restore = opening.publication->Restoration();
    Expect(session.ApplyPreparedDuplicateOpeningIfCurrent(opening.publication.value()), "opening-publishes-once");
    const auto& opened = session.Document().value();
    Expect(opened.layers.size() == sourceDocument.layers.size() + 1 && opened.layers[0] == source &&
               opened.layers[2] == sourceDocument.layers[1],
           "source-sibling-order-and-index-preserved");
    const ImageLayer& copy = opened.layers[1];
    Expect(copy.id != fixture.sourceID && copy.name == std::wstring(kSourceName) + L" copy" &&
               copy.parentID == source.parentID && copy.transform == source.transform,
           "copy-inserted-after-source-under-same-parent-with-original-transform-and-name-suffix");
    Expect(copy.asset && copy.asset->image.Identity() == source.asset->image.Identity() && copy.mask &&
               copy.mask->asset.image.Identity() == source.mask->asset.image.Identity() &&
               opened.layers.front().asset->image.Identity() == source.asset->image.Identity() &&
               opened.layers.front().mask->asset.image.Identity() == source.mask->asset.image.Identity(),
           "copy-and-original-share-image-and-mask-storage");
    Expect(session.ActiveLayerID() == copy.id && session.SelectedLayerIDs().size() == 1 &&
               session.SelectedLayerIDs().contains(copy.id) && !session.IsMaskSelected(),
           "copy-becomes-only-active-layer-target");
    Expect(opened.selection == sourceDocument.selection && session.ClipboardSerial() == before.clipboardSerial &&
               SameClipboard(session.Clipboard(), before.clipboard),
           "opening-retains-selection-and-private-clipboard");
    Expect(session.history.CaptureState().Depth() == before.history.Depth() + 1 &&
               session.history.UndoCount() == before.undoCount && session.history.RedoName() == before.redoName,
           "opening-has-no-history-entry-before-commit");
    Expect(!session.ApplyPreparedDuplicateOpeningIfCurrent(opening.publication.value()),
           "opening-publication-single-use");

    auto moved = session.PendingTransformEdit()->draft;
    moved.origin = {9, -3};
    session.PreviewTransform(moved);
    auto committed = PrepareCommit(session, executor);
    Expect(committed.status == TransformPixelsStatus::Ready && committed.publication,
           "existing-transform-commit-prepares-moved-copy");
    if (!committed.publication)
        return;
    Expect(session.ApplyPreparedTransformCommitIfCurrent(committed.publication.value()),
           "existing-transform-commit-publishes-copy");
    const ImageLayer* committedCopy = FindLayer(session.Document().value(), copy.id);
    const ImageLayer* committedSource = FindLayer(session.Document().value(), fixture.sourceID);
    Expect(committedCopy && committedCopy->transform == moved && committedSource &&
               committedSource->transform == source.transform && !session.PendingTransformEdit(),
           "commit-moves-copy-and-leaves-source-unchanged");
    Expect(session.history.CaptureState().Depth() == before.history.Depth() &&
               session.history.UndoCount() == before.undoCount + 1 &&
               session.history.UndoName() == L"Duplicate Layer" && !session.history.CanRedo(),
           "commit-closes-one-duplicate-layer-history-entry");
    session.Undo();
    Expect(session.Document() == before.document && session.ActiveLayerID() == before.active &&
               session.Document()->selection == sourceDocument.selection,
           "undo-restores-original-document-active-layer-and-selection");
}

void RestoreCancellationAndNestedParent()
{
    {
        Fixture fixture;
        auto& session = fixture.session;
        const OwnerState before(session);
        InlineExecutor executor;
        auto opening = Prepare(session, executor);
        Expect(opening.publication && SameOwnerState(session, before), "cancel-path-prepares-without-live-change");
        if (opening.publication)
        {
            const auto restoration = opening.publication->Restoration();
            Expect(session.ApplyPreparedDuplicateOpeningIfCurrent(opening.publication.value()),
                   "cancel-path-opens-copy");
            auto draft = session.PendingTransformEdit()->draft;
            draft.origin = {-40, 28};
            session.PreviewTransform(draft);
            Expect(restoration && session.ApplyPreparedDuplicateRestoreIfCurrent(*restoration),
                   "preprepared-restoration-cancels-after-draft-movement");
            Expect(!session.PendingTransformEdit() && SameRestoredOwnerState(session, before),
                   "restore-exactly-recovers-document-history-selection-mask-target-and-clipboard");
            Expect(restoration && !session.ApplyPreparedDuplicateRestoreIfCurrent(*restoration),
                   "restoration-publication-single-use");
        }
    }
    {
        Fixture fixture;
        auto& session = fixture.session;
        InlineExecutor executor;
        session.BeginEdit(L"Literal outer parent edit");
        EditLayerRecordsForTest(session, [](DocumentFixture& document) { document.layers.front().opacity = 0.625; });
        const OwnerState parent(session);
        auto opening = Prepare(session, executor);
        Expect(opening.publication && session.ApplyPreparedDuplicateOpeningIfCurrent(opening.publication.value()) &&
                   session.history.CaptureState().Depth() == parent.history.Depth() + 1,
               "nested-open-adds-only-child-history-depth");
        if (opening.publication)
        {
            const auto restoration = opening.publication->Restoration();
            auto draft = session.PendingTransformEdit()->draft;
            draft.origin.x += 6;
            session.PreviewTransform(draft);
            Expect(restoration && session.ApplyPreparedDuplicateRestoreIfCurrent(*restoration) &&
                       SameRestoredOwnerState(session, parent),
                   "nested-restore-retains-parent-document-depth-name-and-redo");
            session.EndEdit();
            Expect(session.history.UndoName() == L"Literal outer parent edit" &&
                       session.history.UndoCount() == parent.undoCount + 1,
                   "nested-restored-parent-commits-under-parent-name");
        }
    }
    {
        Fixture fixture;
        auto& session = fixture.session;
        InlineExecutor executor;
        session.BeginEdit(L"Literal outer commit parent");
        EditLayerRecordsForTest(session, [](DocumentFixture& document) { document.layers.front().opacity = 0.75; });
        const OwnerState parent(session);
        auto opening = Prepare(session, executor);
        const bool opened =
            opening.publication && session.ApplyPreparedDuplicateOpeningIfCurrent(opening.publication.value());
        Expect(opened && session.history.CaptureState().Depth() == parent.history.Depth() + 1,
               "nested-commit-opening-keeps-parent-depth");
        if (!opened)
            return;
        auto draft = session.PendingTransformEdit()->draft;
        draft.origin.y += 7;
        session.PreviewTransform(draft);
        auto committed = PrepareCommit(session, executor);
        const bool committedCurrent =
            committed.publication && session.ApplyPreparedTransformCommitIfCurrent(committed.publication.value());
        Expect(committedCurrent && session.history.CaptureState().Depth() == parent.history.Depth() &&
                   session.history.UndoCount() == parent.undoCount && session.history.UndoName() == parent.undoName,
               "nested-commit-closes-child-and-retains-parent-history-name");
        if (committedCurrent)
        {
            session.EndEdit();
            Expect(session.history.UndoName() == L"Literal outer commit parent" &&
                       session.history.UndoCount() == parent.undoCount + 1,
                   "nested-commit-records-parent-name-on-parent-close");
        }
    }
}

enum class FaultMode : std::uint8_t
{
    Cancel,
    BadAlloc
};

struct OpeningFault final
{
    DuplicateOpeningStage stage;
    FaultMode mode;
    CancelSource* cancellation = nullptr;
    unsigned int hits = 0;
};

void FailOpeningCheckpoint(void* context, DuplicateOpeningStage stage)
{
    auto& fault = *static_cast<OpeningFault*>(context);
    if (stage != fault.stage)
        return;
    ++fault.hits;
    if (fault.mode == FaultMode::BadAlloc)
        throw std::bad_alloc{};
    static_cast<void>(fault.cancellation->Cancel());
}

struct HistoryFault final
{
    DocumentHistory::PrepareStage stage;
    bool hit = false;
};

void FailHistoryCheckpoint(void* context, DocumentHistory::PrepareStage stage)
{
    auto& fault = *static_cast<HistoryFault*>(context);
    if (stage == fault.stage)
    {
        fault.hit = true;
        throw std::bad_alloc{};
    }
}

class ShutdownExecutor final : public Executor
{
  public:
    explicit ShutdownExecutor(std::stop_token shutdown) noexcept : _shutdown(shutdown) {}

    [[nodiscard]] unsigned int Concurrency() const noexcept override
    {
        return 1;
    }

    [[nodiscard]] std::stop_token ShutdownToken() const noexcept override
    {
        return _shutdown;
    }

    void RunParticipants(void* context, Participant run, unsigned int) noexcept override
    {
        run(context);
    }

  private:
    std::stop_token _shutdown;
};

void CheckpointAndHistoryFaults()
{
    InlineExecutor executor;
    constexpr std::array stages{DuplicateOpeningStage::Layer, DuplicateOpeningStage::Document,
                                DuplicateOpeningStage::History, DuplicateOpeningStage::Restoration,
                                DuplicateOpeningStage::Publication};
    for (const DuplicateOpeningStage stage : stages)
    {
        for (const FaultMode mode : {FaultMode::Cancel, FaultMode::BadAlloc})
        {
            Fixture fixture;
            const OwnerState before(fixture.session);
            CancelSource cancellation;
            OpeningFault fault{stage, mode, &cancellation};
            const DuplicateOpeningObserver observer{&fault, &FailOpeningCheckpoint};
            bool caught = false;
            std::optional<DuplicateOpeningResult> result;
            try
            {
                result.emplace(Prepare(fixture.session, executor, cancellation.Token(), {}, observer));
            }
            catch (const std::bad_alloc&)
            {
                caught = true;
            }
            Expect(fault.hits == 1 &&
                       (mode == FaultMode::Cancel
                            ? result && result->status == DuplicateOpeningStatus::Cancelled && !result->publication
                            : caught && !result),
                   mode == FaultMode::Cancel ? "opening-cancel-checkpoint-reached"
                                             : "opening-bad-alloc-checkpoint-reached");
            Expect(SameOwnerState(fixture.session, before) && !fixture.session.PendingTransformEdit(),
                   "opening-checkpoint-failure-publishes-no-partial-document-or-history");
        }
    }

    for (const auto stage :
         {DocumentHistory::PrepareStage::PendingSnapshot, DocumentHistory::PrepareStage::PendingRecord})
    {
        Fixture fixture;
        const OwnerState before(fixture.session);
        HistoryFault fault{stage};
        const DocumentHistory::PrepareObserver observer{&FailHistoryCheckpoint, &fault};
        bool caught = false;
        try
        {
            static_cast<void>(Prepare(fixture.session, executor, {}, observer));
        }
        catch (const std::bad_alloc&)
        {
            caught = true;
        }
        Expect(fault.hit && caught, "non-nested-history-pending-allocation-checkpoint-reached");
        Expect(SameOwnerState(fixture.session, before) && !fixture.session.PendingTransformEdit(),
               "history-pending-allocation-failure-leaves-owner-state-unchanged");
    }

    {
        Fixture fixture;
        const OwnerState before(fixture.session);
        CancelSource cancellation;
        static_cast<void>(cancellation.Cancel());
        OpeningFault fault{DuplicateOpeningStage::Layer, FaultMode::Cancel, &cancellation};
        auto result = Prepare(fixture.session, executor, cancellation.Token(), {},
                              DuplicateOpeningObserver{&fault, &FailOpeningCheckpoint});
        Expect(result.status == DuplicateOpeningStatus::Cancelled && !result.publication && fault.hits == 0 &&
                   SameOwnerState(fixture.session, before),
               "pre-cancelled-opening-does-not-reach-checkpoint-or-publish");
    }
    {
        Fixture fixture;
        const OwnerState before(fixture.session);
        std::stop_source shutdown;
        static_cast<void>(shutdown.request_stop());
        ShutdownExecutor stopped(shutdown.get_token());
        auto result = Prepare(fixture.session, stopped);
        Expect(result.status == DuplicateOpeningStatus::Cancelled && !result.publication &&
                   SameOwnerState(fixture.session, before),
               "executor-shutdown-cancels-before-publication");
    }
}

void StaleAuthorityAndOwnership()
{
    InlineExecutor executor;
    enum class Mutation : std::uint8_t
    {
        Authority,
        Source,
        History,
        Revision
    };
    for (const Mutation mutation : {Mutation::Authority, Mutation::Source, Mutation::History, Mutation::Revision})
    {
        Fixture fixture;
        auto opening = Prepare(fixture.session, executor);
        if (!opening.publication)
        {
            Expect(false, "stale-opening-fixture-prepared");
            continue;
        }
        switch (mutation)
        {
        case Mutation::Authority:
            fixture.session.SelectLayerTarget(fixture.sourceID, false);
            break;
        case Mutation::Source:
            EditLayerRecordsForTest(fixture.session,
                                    [](DocumentFixture& document) { document.layers.front().opacity = 0.5; });
            break;
        case Mutation::History:
            fixture.session.history.Reset();
            break;
        case Mutation::Revision:
            fixture.session.BeginEdit(L"Revision-only parent");
            fixture.session.EndEdit();
            break;
        }
        const OwnerState changed(fixture.session);
        Expect(!fixture.session.ApplyPreparedDuplicateOpeningIfCurrent(opening.publication.value()) &&
                   SameOwnerState(fixture.session, changed),
               mutation == Mutation::Authority ? "stale-target-authority-refuses-opening"
               : mutation == Mutation::Source  ? "stale-source-revision-refuses-opening"
               : mutation == Mutation::History ? "stale-history-epoch-refuses-opening"
                                               : "stale-edit-revision-refuses-opening");
    }
    {
        Fixture fixture;
        auto input = fixture.session.PrepareDuplicateOpeningInput();
        Expect(input.has_value(), "move-only-opening-input-created");
        if (input)
        {
            DuplicateOpeningInput owned = std::move(input.value());
            auto movedFrom =
                DocumentSession::PrepareDuplicateOpeningPublication(std::move(input.value()), executor, {}, {}, {});
            auto valid = DocumentSession::PrepareDuplicateOpeningPublication(std::move(owned), executor, {}, {}, {});
            Expect(movedFrom.status == DuplicateOpeningStatus::Invalid && !movedFrom.publication && valid.publication,
                   "moved-from-input-refused-and-owned-input-remains-usable");
        }
    }
    {
        Fixture fixture;
        auto opening = Prepare(fixture.session, executor);
        if (opening.publication)
        {
            Expect(fixture.session.ApplyPreparedDuplicateOpeningIfCurrent(opening.publication.value()) &&
                       !fixture.session.ApplyPreparedDuplicateOpeningIfCurrent(opening.publication.value()),
                   "prepared-opening-is-single-use");
        }
        else
            Expect(false, "single-use-opening-fixture-prepared");
    }
    {
        Fixture fixture;
        auto opening = Prepare(fixture.session, executor);
        Fixture foreign;
        const OwnerState before(foreign.session);
        Expect(opening.publication &&
                   !foreign.session.ApplyPreparedDuplicateOpeningIfCurrent(opening.publication.value()) &&
                   SameOwnerState(foreign.session, before),
               "foreign-session-refuses-opening-authority");
    }
    {
        Fixture fixture;
        auto opening = Prepare(fixture.session, executor);
        Expect(opening.publication && opening.publication->Restoration(), "restore-authority-fixture-prepared");
        if (opening.publication && opening.publication->Restoration())
        {
            auto restore = opening.publication->Restoration();
            Expect(!fixture.session.ApplyPreparedDuplicateRestoreIfCurrent(*restore),
                   "unpublished-restore-cannot-skip-opening");
            Expect(fixture.session.ApplyPreparedDuplicateOpeningIfCurrent(opening.publication.value()) &&
                       fixture.session.ApplyPreparedDuplicateRestoreIfCurrent(*restore),
                   "restore-authority-applies-only-after-opening");
        }
    }
}

void Refusals()
{
    {
        Fixture fixture;
        fixture.session.BeginTransform();
        Expect(fixture.session.PendingTransformEdit() &&
                   fixture.session.DuplicateOpeningPreparationMemoryBytes() == 0 &&
                   !fixture.session.PrepareDuplicateOpeningInput(),
               "pending-transform-source-refused");
        fixture.session.CancelTransform();
    }
    {
        Fixture fixture;
        EditLayerRecordsForTest(fixture.session,
                                [](DocumentFixture& document) { document.layers.front().asset.reset(); });
        Expect(!fixture.session.CanTransform() && fixture.session.DuplicateOpeningPreparationMemoryBytes() == 0 &&
                   !fixture.session.PrepareDuplicateOpeningInput(),
               "nontransformable-blank-source-refused");
    }
    {
        Fixture fixture;
        auto document = DocumentFixture::Copy(fixture.session.Document().value());
        ImageLayer group = ImageLayer::Blank(L"Transform group", Vec2{8, 8});
        group.isGroup = true;
        ImageLayer child = document.layers.front();
        child.parentID = group.id;
        document.layers = {group, child};
        fixture.session.ReplaceDocument(std::move(document).Seal(), group.id);
        Expect(fixture.session.TransformsAsGroup() && fixture.session.DuplicateOpeningPreparationMemoryBytes() == 0 &&
                   !fixture.session.PrepareDuplicateOpeningInput(),
               "group-transform-source-refused");
    }
    {
        Fixture fixture;
        auto document = DocumentFixture::Copy(fixture.session.Document().value());
        document.layers.reserve(static_cast<std::size_t>(kMaxLayerCount));
        while (document.layers.size() < static_cast<std::size_t>(kMaxLayerCount))
            document.layers.push_back(ImageLayer::Blank(L"Source-limit filler", Vec2{1, 1}));
        fixture.session.ReplaceDocument(std::move(document).Seal(), fixture.sourceID);
        Expect(fixture.session.DuplicateOpeningPreparationMemoryBytes() == 0 &&
                   !fixture.session.PrepareDuplicateOpeningInput(),
               "source-layer-count-limit-refuses-opening");
    }
}

void GroupedInsertionAndStaleRollback()
{
    InlineExecutor executor;
    {
        Fixture fixture;
        auto document = DocumentFixture::Copy(fixture.session.Document().value());
        ImageLayer group = ImageLayer::Blank(L"Parent", {8, 8});
        group.isGroup = true;
        const auto parentID = group.id;
        document.layers.front().parentID = parentID;
        document.layers.insert(document.layers.begin(), std::move(group));
        fixture.session.ReplaceDocument(std::move(document).Seal(), fixture.sourceID);
        const auto original = fixture.session.Document().value();
        auto opened = Prepare(fixture.session, executor);
        Expect(opened.publication && fixture.session.ApplyPreparedDuplicateOpeningIfCurrent(*opened.publication),
               "grouped-child-opening-publishes");
        if (fixture.session.PendingTransformEdit())
        {
            const auto& layers = fixture.session.Document()->layers;
            Expect(layers.size() == 4 && layers[0] == original.layers[0] && layers[1] == original.layers[1] &&
                       layers[2].parentID == parentID && layers[2].name == std::wstring(kSourceName) + L" copy" &&
                       layers[3] == original.layers[2],
                   "grouped-copy-retains-actual-parent-and-adjacent-sibling-order");
        }
    }
    for (const bool sourceChange : {false, true})
    {
        Fixture fixture;
        auto opened = Prepare(fixture.session, executor);
        Expect(opened.publication && fixture.session.ApplyPreparedDuplicateOpeningIfCurrent(*opened.publication),
               "stale-rollback-opening-publishes");
        if (!opened.publication)
            continue;
        const auto restore = opened.publication->Restoration();
        if (sourceChange)
            EditLayerRecordsForTest(fixture.session,
                                    [](DocumentFixture& document) { document.layers[1].opacity = 0.5; });
        else
            fixture.session.history.Reset();
        const OwnerState changed(fixture.session);
        const auto draftRevision = fixture.session.TransformDraftRevision();
        Expect(restore && !fixture.session.IsDuplicateRestoreCurrent(*restore) &&
                   !fixture.session.ApplyPreparedDuplicateRestoreIfCurrent(*restore) &&
                   SameOwnerState(fixture.session, changed) && fixture.session.PendingTransformEdit() &&
                   fixture.session.TransformDraftRevision() == draftRevision,
               sourceChange ? "stale-source-refuses-prepaid-rollback-without-discarding-current-draft"
                            : "stale-history-refuses-prepaid-rollback-without-discarding-current-draft");
    }
}

void ExactPixelBudgets()
{
    for (const bool maskBudget : {false, true})
    {
        for (const bool over : {false, true})
        {
            Fixture fixture;
            auto document = DocumentFixture::Copy(fixture.session.Document().value());
            const auto format = maskBudget ? RasterFormat::Gray8 : RasterFormat::Bgra8888;
            const auto large = Raster::MakeSparse(10'000, 9999, format, Raster{}, 0, 0, {}, 0);
            // Both original layers have four color pixels; only the source has four mask pixels.
            // After another four-pixel copy, 99,990,000 + tail + existing + copy equals exactly 100 M.
            const int tailWidth = (maskBudget ? 9992 : 9988) + (over ? 1 : 0);
            const auto tail = Raster::MakeSparse(tailWidth, 1, format, Raster{}, 0, 0, {}, 0);
            for (const auto& raster : {large, tail})
            {
                auto layer = ImageLayer::Blank(L"Virtual budget sibling", {1, 1});
                if (maskBudget)
                    layer.mask = LayerMask{RasterAsset{raster, {}, L"Virtual mask"}, true, std::nullopt, true};
                else
                    layer.asset = RasterAsset{raster, {}, L"Virtual image"};
                document.layers.push_back(std::move(layer));
            }
            fixture.session.ReplaceDocument(std::move(document).Seal(), fixture.sourceID);
            const OwnerState before(fixture.session);
            InlineExecutor executor;
            auto prepared = Prepare(fixture.session, executor);
            Expect(over ? fixture.session.DuplicateOpeningPreparationMemoryBytes() == 0 && !prepared.publication
                        : prepared.status == DuplicateOpeningStatus::Ready && prepared.publication,
                   maskBudget ? "mask-budget-exact-equality-or-one-pixel-over-refusal"
                              : "color-budget-exact-equality-or-one-pixel-over-refusal");
            Expect(SameOwnerState(fixture.session, before) && !large.IsMaterialized() && !tail.IsMaterialized(),
                   "budget-checks-never-materialize-virtual-siblings-or-mutate-owner");
        }
    }
}
} // namespace

int RunDuplicateOpeningWorkTests()
{
    gFailures = gAssertions = 0;
    OpeningAndCommit();
    RestoreCancellationAndNestedParent();
    CheckpointAndHistoryFaults();
    StaleAuthorityAndOwnership();
    Refusals();
    GroupedInsertionAndStaleRollback();
    ExactPixelBudgets();
    std::printf("DUPLICATE OPENING WORK TESTS checks=%d failures=%d\n", gAssertions, gFailures);
    return gFailures;
}
