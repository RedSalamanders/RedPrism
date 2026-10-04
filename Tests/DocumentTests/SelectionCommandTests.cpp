#include "SelectionCommandTests.h"

#include "../Support/DocumentFixture.h"

#include "Document.h"
#include "Selection.h"
#include "SelectionCommandWork.h"

#include <array>
#include <cstdio>
#include <new>
#include <optional>
#include <type_traits>
#include <utility>

namespace
{
int gFailures = 0;
int gAssertions = 0;

void Expect(bool condition, const char* name) noexcept
{
    ++gAssertions;
    std::printf("%s selection-command.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

DocumentSession Session()
{
    DocumentSession session;
    DocumentFixture document;
    document.id = Guid::Generate();
    document.width = 4;
    document.height = 1;
    constexpr std::array<std::uint8_t, 16> pixels{0,   0,   0,   255, 64,  64,  64,  255,
                                                  128, 128, 128, 255, 255, 255, 255, 255};
    constexpr std::array<std::uint8_t, 4> maskBytes{0, 128, 255, 255};
    const auto image = Raster::Make(4, 1, RasterFormat::Bgra8888, pixels.data(), pixels.size());
    const auto mask = Raster::Make(4, 1, RasterFormat::Gray8, maskBytes.data(), maskBytes.size());
    ImageLayer first = ImageLayer::Blank(L"Literal selection source", Vec2{4, 1});
    const auto active = first.id;
    first.asset = RasterAsset{image, {}, first.name};
    first.mask = LayerMask{RasterAsset{mask, mask, L"Literal selection mask"}, true, std::nullopt, true};
    document.layers.push_back(std::move(first));
    document.layers.push_back(ImageLayer::Blank(L"Other selection target", Vec2{4, 1}));
    session.ReplaceDocument(std::move(document).Seal(), active);
    return session;
}

bool LiteralFullCanvas(const std::optional<CanvasDocument>& document) noexcept
{
    if (!document || !document->selection || !document->selection->antialiased ||
        document->selection->loops.size() != 1 || document->selection->loops.Get().front().size() != 4)
        return false;
    const auto& loop = document->selection->loops.Get().front();
    return loop[0] == Vec2{0, 0} && loop[1] == Vec2{4, 0} && loop[2] == Vec2{4, 1} && loop[3] == Vec2{0, 1};
}

bool FullCoverageIsLiteral(const DocumentSelection& selection)
{
    const Raster coverage = SelectionGeometry::Coverage(selection.loops, 4, 1, selection.antialiased);
    if (!coverage.HasStorage() || coverage.Format() != RasterFormat::Gray8 || coverage.Width() != 4 ||
        coverage.Height() != 1)
        return false;
    const std::uint8_t* pixels = coverage.Data();
    return pixels && pixels[0] == 255 && pixels[1] == 255 && pixels[2] == 255 && pixels[3] == 255;
}

struct State final
{
    std::optional<CanvasDocument> document;
    std::optional<Guid> active;
    DocumentHistory::StateView history;
    Guid revision;
    int undoCount = 0;
    std::wstring undoName;
    std::wstring redoName;
    std::uint64_t generation = 0;
    std::optional<JobToken> token;
    std::optional<CropRect> crop;
    bool maskSelected = false;
    FilterSettings filterSettings;

    explicit State(const DocumentSession& session)
        : document(session.Document()), active(session.ActiveLayerID()), history(session.history.CaptureState()),
          revision(session.history.CurrentRevision()), undoCount(session.history.UndoCount()),
          undoName(session.history.UndoName()), redoName(session.history.RedoName()), generation(session.Generation()),
          token(session.CaptureJobToken()), crop(session.CropFrame()), maskSelected(session.IsMaskSelected()),
          filterSettings(session.filterSettings)
    {
    }
};

bool SameState(const DocumentSession& session, const State& before)
{
    const auto afterHistory = session.history.CaptureState();
    const auto afterToken = session.CaptureJobToken();
    return session.Document() == before.document && session.ActiveLayerID() == before.active &&
           session.history.IsCurrent(before.history) && session.history.CurrentRevision() == before.revision &&
           session.history.UndoCount() == before.undoCount && session.history.UndoName() == before.undoName &&
           session.history.RedoName() == before.redoName && afterHistory.Depth() == before.history.Depth() &&
           afterHistory.PastCount() == before.history.PastCount() &&
           afterHistory.FutureCount() == before.history.FutureCount() && session.Generation() == before.generation &&
           before.token.has_value() && afterToken.has_value() && session.CanApplyJobResult(*before.token, 0) &&
           session.CanApplyJobResult(*afterToken, 0) && session.CropFrame() == before.crop &&
           session.IsMaskSelected() == before.maskSelected && session.filterSettings == before.filterSettings;
}

enum class FailureMode : std::uint8_t
{
    Cancel,
    ThrowBadAlloc,
};

struct InjectFailure final
{
    DocumentHistory::PrepareStage target{};
    FailureMode mode{};
    CancelSource* cancel = nullptr;
    int hits = 0;
};

void FailAtStage(void* opaque, DocumentHistory::PrepareStage stage)
{
    auto& failure = *static_cast<InjectFailure*>(opaque);
    if (stage != failure.target)
        return;
    ++failure.hits;
    if (failure.mode == FailureMode::Cancel)
        static_cast<void>(failure.cancel->Cancel());
    else
        throw std::bad_alloc{};
}

const char* StageName(DocumentHistory::PrepareStage stage) noexcept
{
    using Stage = DocumentHistory::PrepareStage;
    switch (stage)
    {
    case Stage::SelectionSnapshot:
        return "selection-snapshot";
    case Stage::BeforeSnapshot:
        return "before-snapshot";
    case Stage::AfterSnapshot:
        return "after-snapshot";
    case Stage::Entry:
        return "entry";
    case Stage::StackGrowth:
        return "stack-growth";
    case Stage::TrimIndex:
        return "trim-index";
    case Stage::PendingSnapshot:
        return "pending-snapshot";
    case Stage::PendingRecord:
        return "pending-record";
    case Stage::RestoreSnapshot:
        return "restore-snapshot";
    case Stage::ReplacementReset:
        return "replacement-reset";
    case Stage::ReplacementSnapshot:
        return "replacement-snapshot";
    case Stage::AdjustmentSnapshot:
        return "adjustment-snapshot";
    }
    return "unknown";
}

template <typename Mutate> void StaleAfterPreparation(const char* name, Mutate&& mutate)
{
    DocumentSession session = Session();
    auto input = session.PrepareSelectionCommandInput(SelectionCommandKind::SelectAll);
    auto prepared = input ? DocumentSession::PrepareSelectionCommandPublication(std::move(*input)) : std::nullopt;
    Expect(prepared && session.IsPreparedSelectionCommandCurrent(*prepared), name);
    if (!prepared)
        return;
    mutate(session);
    const State changed(session);
    Expect(!session.IsPreparedSelectionCommandCurrent(*prepared) &&
               !session.ApplyPreparedSelectionCommandIfCurrent(*prepared),
           name);
    Expect(SameState(session, changed), name);
}

void GeometryAndPublication()
{
    static_assert(!std::is_copy_constructible_v<SelectionCommandInput>);
    static_assert(std::is_nothrow_move_constructible_v<SelectionCommandInput>);
    static_assert(!std::is_copy_constructible_v<PreparedSelectionCommand>);
    static_assert(std::is_nothrow_move_constructible_v<PreparedSelectionCommand>);

    DocumentSession session = Session();
    const State before(session);
    auto input = session.PrepareSelectionCommandInput(SelectionCommandKind::SelectAll);
    Expect(input.has_value(), "select-all-input-admitted");
    auto prepared = input ? DocumentSession::PrepareSelectionCommandPublication(std::move(*input)) : std::nullopt;
    Expect(prepared && prepared->ChangesSelection() && session.IsPreparedSelectionCommandCurrent(*prepared),
           "select-all-preparation-is-private-and-current");
    if (!prepared)
        return;
    Expect(session.Document() == before.document && session.Generation() == before.generation &&
               session.history.IsCurrent(before.history),
           "select-all-preparation-does-not-mutate-owner");
    PreparedSelectionCommand movedPrepared(std::move(*prepared));
    Expect(!session.IsPreparedSelectionCommandCurrent(*prepared) &&
               !session.ApplyPreparedSelectionCommandIfCurrent(*prepared) &&
               session.IsPreparedSelectionCommandCurrent(movedPrepared),
           "moved-from-prepared-command-is-refused");
    Expect(session.ApplyPreparedSelectionCommandIfCurrent(movedPrepared), "select-all-publication-succeeds");
    Expect(LiteralFullCanvas(session.Document()) && FullCoverageIsLiteral(*session.Document()->selection),
           "select-all-is-literal-antialiased-4x1-geometry-and-coverage");
    Expect(session.Generation() == before.generation + 1 && session.history.CurrentRevision() != before.revision &&
               session.history.UndoName() == L"Select All" && session.history.UndoCount() == 1,
           "select-all-publishes-one-named-revision");
    Expect(before.token && !session.CanApplyJobResult(*before.token, 0),
           "changed-selection-invalidates-old-token-at-same-sequence");
    Expect(session.Document()->layers == before.document->layers &&
               session.Document()->resolution == before.document->resolution,
           "selection-publication-preserves-every-source-record");
    Expect(!session.IsPreparedSelectionCommandCurrent(movedPrepared) &&
               !session.ApplyPreparedSelectionCommandIfCurrent(movedPrepared),
           "changed-publication-is-single-use");

    session.Undo();
    Expect(!session.Document()->selection && session.history.RedoName() == L"Select All", "undo-restores-none");
    session.Redo();
    Expect(LiteralFullCanvas(session.Document()) && session.history.UndoName() == L"Select All",
           "redo-restores-literal-selection-and-title");
}

void EmptyNoOpAndRedo()
{
    {
        DocumentSession session = Session();
        const State before(session);
        auto input = session.PrepareSelectionCommandInput(SelectionCommandKind::Deselect);
        auto prepared = input ? DocumentSession::PrepareSelectionCommandPublication(std::move(*input)) : std::nullopt;
        Expect(prepared && !prepared->ChangesSelection() && session.IsPreparedSelectionCommandCurrent(*prepared),
               "deselect-nullopt-prepares-no-op");
        Expect(prepared && session.ApplyPreparedSelectionCommandIfCurrent(*prepared) && SameState(session, before),
               "deselect-nullopt-preserves-revision-generation-epoch-and-token");
        Expect(prepared && !session.IsPreparedSelectionCommandCurrent(*prepared) &&
                   !session.ApplyPreparedSelectionCommandIfCurrent(*prepared),
               "no-op-acknowledgement-is-single-use");
    }
    {
        DocumentSession session = Session();
        session.EditForTest([](CanvasDocument& document) { document.selection = DocumentSelection{true, {}}; });
        const State before(session);
        auto input = session.PrepareSelectionCommandInput(SelectionCommandKind::Deselect);
        auto prepared = input ? DocumentSession::PrepareSelectionCommandPublication(std::move(*input)) : std::nullopt;
        Expect(prepared && prepared->ChangesSelection(), "explicit-empty-selection-is-distinct-from-none");
        Expect(prepared && session.ApplyPreparedSelectionCommandIfCurrent(*prepared) &&
                   !session.Document()->selection && session.history.UndoName() == L"Deselect" &&
                   session.Generation() == before.generation + 1,
               "deselect-explicit-empty-publishes-named-change");
        session.Undo();
        Expect(session.Document()->selection == std::optional<DocumentSelection>{DocumentSelection{true, {}}},
               "undo-restores-explicit-empty-selection");
    }
    {
        DocumentSession session = Session();
        session.SelectAll();
        session.Undo();
        const State before(session);
        Expect(session.history.CanRedo() && session.history.RedoName() == L"Select All", "noop-redo-fixture");
        auto input = session.PrepareSelectionCommandInput(SelectionCommandKind::Deselect);
        auto prepared = input ? DocumentSession::PrepareSelectionCommandPublication(std::move(*input)) : std::nullopt;
        Expect(prepared && !prepared->ChangesSelection(), "redo-state-deselect-is-no-op");
        Expect(prepared && session.ApplyPreparedSelectionCommandIfCurrent(*prepared) && SameState(session, before) &&
                   session.history.CanRedo() && session.history.RedoName() == L"Select All",
               "no-op-retains-redo-revision-generation-and-history-epoch");
        session.Redo();
        Expect(LiteralFullCanvas(session.Document()), "redo-still-applies-after-no-op");
    }
    {
        DocumentSession session = Session();
        session.SelectAll();
        const State before(session);
        auto input = session.PrepareSelectionCommandInput(SelectionCommandKind::SelectAll);
        auto prepared = input ? DocumentSession::PrepareSelectionCommandPublication(std::move(*input)) : std::nullopt;
        Expect(prepared && !prepared->ChangesSelection(), "same-select-all-is-geometric-no-op");
        Expect(prepared && session.ApplyPreparedSelectionCommandIfCurrent(*prepared) && SameState(session, before),
               "same-select-all-preserves-owner-state");
    }
    {
        DocumentSession session = Session();
        session.SelectAll();
        auto input = session.PrepareSelectionCommandInput(SelectionCommandKind::SelectAll);
        auto prepared = input ? DocumentSession::PrepareSelectionCommandPublication(std::move(*input)) : std::nullopt;
        Expect(prepared && !prepared->ChangesSelection(), "no-op-history-epoch-fixture");
        session.BeginEdit(L"No-op epoch change");
        Expect(prepared && !session.IsPreparedSelectionCommandCurrent(*prepared) &&
                   !session.ApplyPreparedSelectionCommandIfCurrent(*prepared),
               "no-op-refuses-changed-history-epoch");
    }
}

void NestedAndSavedRevision()
{
    {
        DocumentSession session = Session();
        session.history.MarkSaved();
        const Guid savedRevision = session.history.CurrentRevision();
        auto input = session.PrepareSelectionCommandInput(SelectionCommandKind::SelectAll);
        auto prepared = input ? DocumentSession::PrepareSelectionCommandPublication(std::move(*input)) : std::nullopt;
        session.history.MarkSaved(Guid::Generate()); // Save completes while immutable preparation is in flight.
        const bool savedRevisionMoved = session.history.IsModified();
        Expect(prepared && session.IsPreparedSelectionCommandCurrent(*prepared) && savedRevisionMoved &&
                   session.ApplyPreparedSelectionCommandIfCurrent(*prepared) && session.history.IsModified() &&
                   session.history.CurrentRevision() != savedRevision,
               "saved-revision-change-after-preparation-is-honored");
        session.Undo();
        Expect(session.history.CurrentRevision() == savedRevision && session.history.IsModified(),
               "undo-proves-later-saved-marker-was-not-overwritten-with-opening-revision");
    }
    {
        DocumentSession session = Session();
        session.BeginEdit(L"Outer selection transaction");
        const Guid revision = session.history.CurrentRevision();
        auto input = session.PrepareSelectionCommandInput(SelectionCommandKind::SelectAll);
        auto prepared = input ? DocumentSession::PrepareSelectionCommandPublication(std::move(*input)) : std::nullopt;
        Expect(prepared && session.ApplyPreparedSelectionCommandIfCurrent(*prepared) &&
                   session.history.CaptureState().Depth() == 1 && session.history.CurrentRevision() == revision,
               "publication-preserves-open-nested-transaction");
        session.EndEdit();
        Expect(session.history.CaptureState().Depth() == 0 &&
                   session.history.UndoName() == L"Outer selection transaction",
               "outer-transaction-commits-selection-under-original-title");
        session.Undo();
        Expect(!session.Document()->selection, "undo-outer-transaction-restores-original-selection");
    }
}

void AdditionalLiteralAndDraftCases()
{
    for (const auto kind : {SelectionCommandKind::SelectAll, SelectionCommandKind::Deselect})
    {
        auto session = Session();
        session.SetSelection(DocumentSelection{false, {SelectionLoop{Vec2{1, 0}, Vec2{2, 0}, Vec2{2, 1}, Vec2{1, 1}}}},
                             L"Literal original selection");
        session.SelectLayerTarget(session.ActiveLayerID().value(), true);
        session.SetCropFrame(CropRect{0, 0, 3, 1});
        const State before(session);
        auto input = session.PrepareSelectionCommandInput(kind);
        auto prepared = input ? DocumentSession::PrepareSelectionCommandPublication(std::move(*input)) : std::nullopt;
        Expect(prepared && session.ApplyPreparedSelectionCommandIfCurrent(*prepared),
               "literal-all-none-mask-target-publication");
        Expect(session.Document()->layers == before.document->layers && session.ActiveLayerID() == before.active &&
                   session.IsMaskSelected() && session.CropFrame() == before.crop &&
                   session.filterSettings == before.filterSettings,
               "literal-all-none-preserves-pixels-mask-target-crop-template");
        Expect(kind == SelectionCommandKind::SelectAll ? LiteralFullCanvas(session.Document()) : !session.Selection(),
               "literal-all-none-result-and-aa");
        session.Undo();
        Expect(session.Selection() == before.document->selection,
               "literal-all-none-undo-restores-non-aa-one-pixel-selection");
    }
    {
        auto session = Session();
        session.SelectAll();
        session.BeginEdit(L"Redo after full selection");
        session.EditForTest([](CanvasDocument& document) { document.resolution = 144; });
        session.EndEdit();
        session.Undo();
        const State before(session);
        auto input = session.PrepareSelectionCommandInput(SelectionCommandKind::SelectAll);
        auto prepared = input ? DocumentSession::PrepareSelectionCommandPublication(std::move(*input)) : std::nullopt;
        Expect(prepared && !prepared->ChangesSelection() && session.ApplyPreparedSelectionCommandIfCurrent(*prepared) &&
                   SameState(session, before) && session.history.CanRedo(),
               "geometric-select-all-noop-preserves-real-redo-branch");
    }
    {
        auto session = Session();
        session.EditForTest(
            [](CanvasDocument& document)
            {
                document.selection =
                    DocumentSelection{false, {SelectionLoop{Vec2{0, 0}, Vec2{4, 0}, Vec2{4, 1}, Vec2{0, 1}}}};
            });
        auto input = session.PrepareSelectionCommandInput(SelectionCommandKind::SelectAll);
        auto prepared = input ? DocumentSession::PrepareSelectionCommandPublication(std::move(*input)) : std::nullopt;
        Expect(prepared && prepared->ChangesSelection() && session.ApplyPreparedSelectionCommandIfCurrent(*prepared) &&
                   LiteralFullCanvas(session.Document()),
               "same-coordinates-with-false-aa-is-a-real-change");
    }
    for (int draft = 0; draft < 5; ++draft)
    {
        auto session = Session();
        switch (draft)
        {
        case 0:
            session.BeginMarquee({0, 0}, SelectionMode::New);
            break;
        case 1:
            session.BeginLasso({0, 0}, SelectionMode::New);
            break;
        case 2:
            session.BeginShape({0, 0});
            break;
        case 3:
            session.BeginColorEdit(FilterKind::Levels);
            break;
        case 4:
            (void)session.BeginGradient({0, 0});
            break;
        }
        const bool draftIsActive = draft <= 1   ? session.PendingLasso().has_value()
                                   : draft == 2 ? session.PendingShape().has_value()
                                   : draft == 3 ? session.HasColorEdit()
                                                : session.PendingGradient() != nullptr;
        Expect(draftIsActive && !session.PrepareSelectionCommandInput(SelectionCommandKind::SelectAll) &&
                   !session.PrepareSelectionCommandInput(SelectionCommandKind::Deselect),
               "reached-pending-draft-refuses-both-captures");
    }
    DocumentSession empty;
    Expect(empty.SelectionCommandMemoryBytes(SelectionCommandKind::SelectAll) == 0,
           "memory-planning-refuses-missing-document");
    auto session = Session();
    Expect(session.SelectionCommandMemoryBytes(static_cast<SelectionCommandKind>(255)) == 0 &&
               session.SelectionCommandMemoryBytes(SelectionCommandKind::SelectAll) > 0 &&
               session.SelectionCommandMemoryBytes(SelectionCommandKind::Deselect) > 0,
           "memory-planning-refuses-invalid-kind-and-admits-both-bounded-graphs");
}

void AdmissionAndOwnershipRefusals()
{
    {
        DocumentSession empty;
        Expect(!empty.PrepareSelectionCommandInput(SelectionCommandKind::SelectAll), "empty-session-refused");
        DocumentSession invalid = Session();
        Expect(!invalid.PrepareSelectionCommandInput(static_cast<SelectionCommandKind>(255)), "invalid-kind-refused");
    }
    {
        DocumentSession owner = Session();
        auto captured = owner.PrepareSelectionCommandInput(SelectionCommandKind::SelectAll);
        Expect(captured.has_value(), "moved-input-fixture-captured");
        if (captured)
        {
            SelectionCommandInput moved(std::move(*captured));
            const bool movedFromRefused = !DocumentSession::PrepareSelectionCommandPublication(std::move(*captured));
            Expect(movedFromRefused && !owner.Document()->selection,
                   "moved-from-input-refused-while-owner-selection-is-nullopt");
            auto valid = DocumentSession::PrepareSelectionCommandPublication(std::move(moved));
            Expect(valid && owner.ApplyPreparedSelectionCommandIfCurrent(*valid),
                   "moved-input-destination-retains-authority");
        }
    }
    {
        DocumentSession owner = Session();
        auto input = owner.PrepareSelectionCommandInput(SelectionCommandKind::SelectAll);
        auto prepared = input ? DocumentSession::PrepareSelectionCommandPublication(std::move(*input)) : std::nullopt;
        DocumentSession foreign = Session();
        Expect(prepared && !foreign.IsPreparedSelectionCommandCurrent(*prepared) &&
                   !foreign.ApplyPreparedSelectionCommandIfCurrent(*prepared),
               "foreign-session-refused");
    }
    {
        DocumentSession original = Session();
        auto input = original.PrepareSelectionCommandInput(SelectionCommandKind::SelectAll);
        auto prepared = input ? DocumentSession::PrepareSelectionCommandPublication(std::move(*input)) : std::nullopt;
        DocumentSession copied = original;
        Expect(prepared && !copied.IsPreparedSelectionCommandCurrent(*prepared) &&
                   !copied.ApplyPreparedSelectionCommandIfCurrent(*prepared) &&
                   original.IsPreparedSelectionCommandCurrent(*prepared),
               "session-copy-refused-with-original-still-current");
        DocumentSession moveSource = Session();
        auto moveInput = moveSource.PrepareSelectionCommandInput(SelectionCommandKind::SelectAll);
        auto movePrepared =
            moveInput ? DocumentSession::PrepareSelectionCommandPublication(std::move(*moveInput)) : std::nullopt;
        DocumentSession moved = std::move(moveSource);
        Expect(movePrepared && !moved.IsPreparedSelectionCommandCurrent(*movePrepared) &&
                   !moveSource.IsPreparedSelectionCommandCurrent(*movePrepared),
               "session-move-retires-both-authorities");
    }
    {
        DocumentSession session = Session();
        session.BeginShape({0, 0});
        Expect(!session.PrepareSelectionCommandInput(SelectionCommandKind::SelectAll),
               "active-shape-draft-blocks-admission");
    }
}

void Staleness()
{
    StaleAfterPreparation("active-layer-change-stales",
                          [](DocumentSession& session)
                          {
                              const auto generation = session.Generation();
                              const auto history = session.history.CaptureState();
                              session.SelectLayer(session.Document()->layers.back().id);
                              Expect(session.Generation() == generation && session.history.IsCurrent(history),
                                     "active-only-invalidation-does-not-use-generation-or-epoch");
                          });
    StaleAfterPreparation("mask-state-change-stales",
                          [](DocumentSession& session)
                          {
                              const auto generation = session.Generation();
                              const auto history = session.history.CaptureState();
                              session.SelectLayerTarget(session.ActiveLayerID().value(), true);
                              Expect(session.IsMaskSelected() && session.Generation() == generation &&
                                         session.history.IsCurrent(history),
                                     "mask-only-invalidation-does-not-use-generation-or-epoch");
                          });
    StaleAfterPreparation("crop-frame-change-stales",
                          [](DocumentSession& session) { session.SetCropFrame(CropRect{0, 0, 3, 1}); });
    StaleAfterPreparation("history-epoch-change-stales",
                          [](DocumentSession& session) { session.BeginEdit(L"Epoch invalidation"); });
    StaleAfterPreparation("generation-change-stales", [](DocumentSession& session)
                          { session.EditForTest([](CanvasDocument& document) { document.resolution = 144; }); });
    StaleAfterPreparation("new-draft-stales", [](DocumentSession& session) { session.BeginShape({0, 0}); });
    {
        DocumentSession session = Session();
        session.SetCropFrame(CropRect{0, 0, 3, 1});
        auto input = session.PrepareSelectionCommandInput(SelectionCommandKind::SelectAll);
        Expect(input.has_value(), "settled-crop-frame-does-not-block-admission");
    }
}

void PreparationFailures()
{
    using Stage = DocumentHistory::PrepareStage;
    constexpr std::array stages{Stage::SelectionSnapshot, Stage::BeforeSnapshot, Stage::AfterSnapshot, Stage::Entry,
                                Stage::StackGrowth,       Stage::TrimIndex};
    for (const Stage stage : stages)
    {
        for (const auto kind : {SelectionCommandKind::SelectAll, SelectionCommandKind::Deselect})
        {
            for (const FailureMode mode : {FailureMode::Cancel, FailureMode::ThrowBadAlloc})
            {
                DocumentSession session = Session();
                session.SetSelection(
                    DocumentSelection{false, {SelectionLoop{Vec2{1, 0}, Vec2{2, 0}, Vec2{2, 1}, Vec2{1, 1}}}},
                    L"Literal seed selection");
                session.BeginEdit(L"Retained redo after selection");
                session.EditForTest([](CanvasDocument& document) { document.resolution = 144; });
                session.EndEdit();
                session.Undo();
                session.history.MarkSaved();
                session.SelectLayerTarget(session.ActiveLayerID().value(), true);
                session.SetCropFrame(CropRect{0, 0, 3, 1});
                const State before(session);
                auto input = session.PrepareSelectionCommandInput(kind);
                CancelSource cancellation;
                InjectFailure injection{stage, mode, &cancellation};
                const DocumentHistory::PrepareObserver observer{&FailAtStage, &injection};
                bool caught = false;
                std::optional<PreparedSelectionCommand> prepared;
                try
                {
                    if (input)
                        prepared = DocumentSession::PrepareSelectionCommandPublication(std::move(*input),
                                                                                       cancellation.Token(), observer);
                }
                catch (const std::bad_alloc&)
                {
                    caught = true;
                }
                const char* modeName = mode == FailureMode::Cancel ? "cancel" : "bad-alloc";
                char label[128]{};
                std::snprintf(label, sizeof(label), "failure-%s-%s-%s-reached",
                              kind == SelectionCommandKind::SelectAll ? "all" : "none", StageName(stage), modeName);
                Expect(injection.hits == 1 && !prepared &&
                           (mode == FailureMode::Cancel ? cancellation.Token().IsCancelled() : caught),
                       label);
                std::snprintf(label, sizeof(label), "failure-%s-%s-%s-preserves-document-history-generation-token",
                              kind == SelectionCommandKind::SelectAll ? "all" : "none", StageName(stage), modeName);
                Expect(SameState(session, before), label);
            }
        }
    }
    {
        DocumentSession session = Session();
        const State before(session);
        CancelSource cancellation;
        static_cast<void>(cancellation.Cancel());
        auto input = session.PrepareSelectionCommandInput(SelectionCommandKind::SelectAll);
        auto prepared =
            input ? DocumentSession::PrepareSelectionCommandPublication(std::move(*input), cancellation.Token())
                  : std::nullopt;
        Expect(!prepared && SameState(session, before), "pre-cancelled-input-refuses-without-owner-mutation");
    }
}
} // namespace

int RunSelectionCommandTests()
{
    gFailures = 0;
    gAssertions = 0;
    GeometryAndPublication();
    EmptyNoOpAndRedo();
    NestedAndSavedRevision();
    AdditionalLiteralAndDraftCases();
    AdmissionAndOwnershipRefusals();
    Staleness();
    PreparationFailures();
    std::printf("%d selection-command assertions; %d failed.\n", gAssertions, gFailures);
    return gFailures;
}
