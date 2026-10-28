#include "Document.Tests.FloatingOpeningWork.h"

#include "FloatingOpeningWork.h"
#include "Selection.h"
#include "Tests/Support/Support.Tests.DocumentFixture.h"

#include <array>
#include <cstdio>
#include <new>
#include <utility>

namespace
{
int gFailures = 0, gAssertions = 0;
void Expect(bool value, const char* name)
{
    ++gAssertions;
    std::printf("%s floating-opening-work.%s\n", value ? "PASS" : "FAIL", name);
    gFailures += value ? 0 : 1;
}
void Initialize(DocumentSession& session)
{
    DocumentFixture document;
    document.id = Guid::Generate();
    document.width = 8;
    document.height = 4;
    constexpr std::array<std::uint8_t, 8> bytes{0, 0, 255, 255, 255, 0, 0, 255};
    auto layer = ImageLayer::Blank(L"Source", {2, 1});
    auto image = Raster::Make(2, 1, RasterFormat::Bgra8888, bytes.data(), bytes.size());
    layer.asset = RasterAsset{image, image, L"Source"};
    document.layers.push_back(layer);
    document.selection = DocumentSelection{false, SelectionGeometry::Rect(0, 0, 1, 1)};
    session.ReplaceDocument(std::move(document).Seal(), layer.id);
    (void)session.CopySelection();
}
FloatingOpeningResult Prepare(DocumentSession& session, Executor& executor, CancelToken cancellation = {},
                              DocumentHistory::PrepareObserver historyObserver = {},
                              FloatingOpeningObserver observer = {})
{
    auto input = session.PrepareFloatingOpeningInput();
    return input ? DocumentSession::PrepareFloatingOpeningPublication(std::move(input.value()), executor, cancellation,
                                                                      historyObserver, observer)
                 : FloatingOpeningResult{};
}
void OpeningAndRestore()
{
    DocumentSession session;
    Initialize(session);
    const auto before = session.Document().value();
    const auto active = session.ActiveLayerID();
    const auto selected = session.SelectedLayerIDs();
    const auto state = session.history.CaptureState();
    const auto clipboard = session.Clipboard();
    const auto serial = session.ClipboardSerial();
    InlineExecutor executor;
    Expect(session.FloatingOpeningPreparationMemoryBytes() > 0, "whole-admission");
    auto prepared = Prepare(session, executor);
    Expect(prepared.status == FloatingPixelsStatus::Ready && prepared.publication &&
               prepared.publication->Restoration(),
           "opening-and-rollback-ready");
    Expect(session.Document().value() == before && session.history.IsCurrent(state) && !session.PendingTransformEdit(),
           "preparation-never-exposes-hole-or-layer");
    if (!prepared.publication || !prepared.publication->Restoration())
        return;
    const auto restore = prepared.publication->Restoration();
    Expect(!session.ApplyPreparedFloatingRestoreIfCurrent(*restore), "restoration-cannot-skip-opening");
    Expect(session.ApplyPreparedFloatingOpeningIfCurrent(prepared.publication.value()), "opening-publishes");
    Expect(!session.ApplyPreparedFloatingOpeningIfCurrent(prepared.publication.value()), "opening-once");
    Expect(session.Document()->layers.size() == 2 && session.PendingTransformEdit() &&
               session.PendingTransformEdit()->floating &&
               session.ActiveLayerID() == session.PendingTransformEdit()->layerID &&
               session.ActiveLayer()->name == L"Floating Selection",
           "opening-selects-floating-layer");
    Expect(session.history.CaptureState().Depth() == 1 &&
               session.history.UndoCount() == static_cast<int>(state.PastCount()),
           "opening-keeps-outer-history-unrecorded");
    Expect(session.Document()->selection == before.selection && session.Clipboard()->image == clipboard->image &&
               session.Clipboard()->origin == clipboard->origin && session.ClipboardSerial() == serial,
           "opening-preserves-outline-and-clipboard-authority");
    auto draft = session.PendingTransformEdit()->draft;
    draft.origin = {-2, 1};
    session.PreviewTransform(draft);
    Expect(session.ApplyPreparedFloatingRestoreIfCurrent(*restore), "prebuilt-restoration-allows-newer-draft-geometry");
    Expect(session.Document().value() == before && session.ActiveLayerID() == active &&
               session.SelectedLayerIDs() == selected && !session.PendingTransformEdit() &&
               session.history.CaptureState().Depth() == 0 &&
               session.history.UndoCount() == static_cast<int>(state.PastCount()) &&
               session.history.CurrentRevision() == state.Revision(),
           "restores-exact-opening-and-history-without-entry");
    Expect(!session.ApplyPreparedFloatingRestoreIfCurrent(*restore), "restoration-consumed-once");
    Expect(session.Clipboard()->image == clipboard->image && session.ClipboardSerial() == serial,
           "restoration-preserves-private-clipboard");
}
void StaleAuthority()
{
    InlineExecutor executor;
    for (int mode = 0; mode < 4; ++mode)
    {
        DocumentSession session;
        Initialize(session);
        auto prepared = Prepare(session, executor);
        if (!prepared.publication)
        {
            Expect(false, "stale-fixture-ready");
            continue;
        }
        const auto restore = prepared.publication->Restoration();
        if (mode == 0)
            session.Deselect();
        else if (mode == 1)
            session.history.Reset();
        else if (mode == 2)
            EditLayerRecordsForTest(session, [](DocumentFixture& document) { document.layers.front().opacity = 0.5; });
        else
        {
            auto doc = session.Document().value();
            session.ReplaceDocument(std::move(doc), session.ActiveLayerID());
        }
        const auto current = session.Document().value();
        Expect(!session.ApplyPreparedFloatingOpeningIfCurrent(prepared.publication.value()), "stale-opening-refuses");
        Expect(!session.ApplyPreparedFloatingRestoreIfCurrent(*restore) && session.Document().value() == current,
               "unpublished-restoration-cannot-change-newer-source");
    }
    for (int mode = 0; mode < 3; ++mode)
    {
        DocumentSession session;
        Initialize(session);
        auto prepared = Prepare(session, executor);
        if (!prepared.publication)
        {
            Expect(false, "restore-stale-fixture-ready");
            continue;
        }
        const auto restore = prepared.publication->Restoration();
        Expect(session.ApplyPreparedFloatingOpeningIfCurrent(prepared.publication.value()),
               "restore-stale-opening-publishes");
        if (mode == 0)
            session.history.Reset();
        else if (mode == 1)
            EditLayerRecordsForTest(session, [](DocumentFixture& document) { document.layers.front().opacity = 0.5; });
        else
        {
            auto doc = session.Document().value();
            session.ReplaceDocument(std::move(doc), session.ActiveLayerID());
        }
        const auto current = session.Document().value();
        Expect(!session.ApplyPreparedFloatingRestoreIfCurrent(*restore) && session.Document().value() == current,
               "restore-refuses-new-history-source-or-session");
    }
}
void NestedOpeningRestoration()
{
    DocumentSession session;
    Initialize(session);
    const auto parentOpening = session.Document().value();
    session.BeginEdit(L"Outer edit");
    EditLayerRecordsForTest(session, [](DocumentFixture& document) { document.layers.front().opacity = 0.5; });
    const auto childOpening = session.Document().value();
    const auto parentState = session.history.CaptureState();
    InlineExecutor executor;
    auto opening = Prepare(session, executor);
    Expect(opening.status == FloatingPixelsStatus::Ready && opening.publication,
           "nested-opening-prepared-under-existing-outer-transaction");
    if (!opening.publication)
        return;
    const auto restore = opening.publication->Restoration();
    Expect(session.ApplyPreparedFloatingOpeningIfCurrent(opening.publication.value()) &&
               session.history.CaptureState().Depth() == 2,
           "nested-opening-adds-only-inner-depth");
    Expect(restore && session.ApplyPreparedFloatingRestoreIfCurrent(*restore) &&
               session.Document().value() == childOpening && session.history.CaptureState().Depth() == 1 &&
               session.history.CurrentRevision() == parentState.Revision() &&
               session.history.UndoCount() == static_cast<int>(parentState.PastCount()),
           "nested-restoration-preserves-parent-changes-and-pending-lineage");
    session.EndEdit();
    Expect(session.history.UndoCount() == static_cast<int>(parentState.PastCount()) + 1 &&
               session.history.UndoName() == L"Outer edit",
           "nested-cancel-then-parent-close-records-only-outer-name");
    session.Undo();
    Expect(session.Document().value() == parentOpening, "nested-cancel-parent-undo-restores-parent-opening");
}
// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4623 4626 5027)
struct FaultState final
{
    FloatingOpeningStage stage;
    bool allocation;
    CancelSource& cancel;
    unsigned visits = 0;
};
#pragma warning(pop)

void Fault(void* raw, FloatingOpeningStage stage)
{
    auto& fault = *static_cast<FaultState*>(raw);
    if (fault.stage != stage)
        return;
    ++fault.visits;
    if (fault.allocation)
        throw std::bad_alloc{};
    (void)fault.cancel.Cancel();
}
void FaultsAndMoves()
{
    InlineExecutor executor;
    for (auto stage :
         {FloatingOpeningStage::Pixels, FloatingOpeningStage::History, FloatingOpeningStage::RestorationDocument,
          FloatingOpeningStage::RestorationOwner, FloatingOpeningStage::Publication})
        for (bool allocation : {false, true})
        {
            DocumentSession session;
            Initialize(session);
            const auto before = session.Document().value();
            const auto state = session.history.CaptureState();
            CancelSource cancel;
            FaultState fault{stage, allocation, cancel};
            bool threw = false;
            std::optional<FloatingOpeningResult> result;
            try
            {
                result.emplace(Prepare(session, executor, cancel.Token(), {}, {&fault, Fault}));
            }
            catch (const std::bad_alloc&)
            {
                threw = true;
            }
            Expect(fault.visits == 1 &&
                       (allocation ? threw : result && result->status == FloatingPixelsStatus::Cancelled),
                   "worker-fault-or-cancel-reaches-boundary");
            Expect((!result || !result->publication) && session.Document().value() == before &&
                       session.history.IsCurrent(state) && !session.PendingTransformEdit(),
                   "failure-before-ready-restoration-never-exposes-opening");
        }
    DocumentSession session;
    Initialize(session);
    auto input = session.PrepareFloatingOpeningInput();
    if (!input)
    {
        Expect(false, "move-fixture-prepared");
        return;
    }
    auto moved = std::move(input.value());
    auto invalid = DocumentSession::PrepareFloatingOpeningPublication(std::move(input.value()), executor, {}, {}, {});
    auto valid = DocumentSession::PrepareFloatingOpeningPublication(std::move(moved), executor, {}, {}, {});
    Expect(invalid.status == FloatingPixelsStatus::Invalid && !invalid.publication && valid.publication,
           "owning-move-invalidates-old-input");
}
} // namespace

int RunFloatingOpeningWorkTests()
{
    gFailures = gAssertions = 0;
    OpeningAndRestore();
    StaleAuthority();
    NestedOpeningRestoration();
    FaultsAndMoves();
    std::printf("FLOATING OPENING WORK TESTS checks=%d failures=%d\n", gAssertions, gFailures);
    return gFailures;
}
