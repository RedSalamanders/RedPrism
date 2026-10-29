#include "Document.Tests.DeferredSelectionMove.h"

#include "Selection.h"
#include "SelectionMoveOpeningWork.h"
#include "SelectionMoveWork.h"
#include "SelectionNudgeWork.h"
#include "Tests/Support/Support.Tests.DocumentFixture.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <utility>

namespace
{
int gFailures = 0;
int gAssertions = 0;

void Expect(bool condition, const char* name)
{
    ++gAssertions;
    std::printf("%s deferred-selection-move.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

DocumentSelection SourceSelection()
{
    return DocumentSelection{false, SelectionLoops{SelectionLoop{{1, 1}, {9, 1}, {9, 9}, {1, 9}},
                                                   SelectionLoop{{3, 3}, {3, 7}, {7, 7}, {7, 3}}}};
}

bool HasLiteral(const std::optional<DocumentSelection>& selection, const std::array<Vec2, 4>& outer,
                const std::array<Vec2, 4>& hole)
{
    if (!selection || selection->antialiased || selection->loops.size() != 2)
        return false;
    const auto& loops = selection->loops.Get();
    if (loops[0].size() != outer.size() || loops[1].size() != hole.size())
        return false;
    for (std::size_t i = 0; i < outer.size(); ++i)
    {
        if (loops[0][i] != outer[i] || loops[1][i] != hole[i])
            return false;
    }
    return true;
}

constexpr std::array<Vec2, 4> kOuter{{{-1, -2}, {7, -2}, {7, 6}, {-1, 6}}};
constexpr std::array<Vec2, 4> kHole{{{1, 0}, {1, 4}, {5, 4}, {5, 0}}};
constexpr std::array<Vec2, 4> kOriginOuter{{{1, 1}, {9, 1}, {9, 9}, {1, 9}}};
constexpr std::array<Vec2, 4> kOriginHole{{{3, 3}, {3, 7}, {7, 7}, {7, 3}}};

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626)
struct Fixture final
{
    std::unique_ptr<DocumentSession> session{new DocumentSession()};
    bool ready = false;

    Fixture()
    {
        Install();
    }

    void Install()
    {
        DocumentFixture source;
        source.id = Guid::Generate();
        source.width = 10;
        source.height = 10;
        source.selection = SourceSelection();
        auto layer = ImageLayer::Blank(L"Deferred selection move", Vec2{10, 10});
        const std::array<std::uint8_t, 4> pixel{0, 0, 0, 255};
        layer.asset =
            RasterAsset{Raster::Make(1, 1, RasterFormat::Bgra8888, pixel.data(), pixel.size()), {}, layer.name};
        const Guid active = layer.id;
        source.layers.push_back(std::move(layer));
        session->ReplaceDocument(std::move(source).Seal(), active);
        session->SetSelection(DocumentSelection{false, SelectionGeometry::Rect(0, 0, 1, 1)}, L"Seed redo");
        session->Undo();
        ready = session->Document().has_value() && session->Selection() &&
                HasLiteral(session->Selection(), kOriginOuter, kOriginHole) && session->history.CanRedo();
    }
};
#pragma warning(pop)

using InputResult = std::optional<SelectionMoveClosureInput>;
using PreparedResult = std::optional<PreparedSelectionMoveClosure>;

std::unique_ptr<InputResult> CaptureInput(Fixture& fixture, bool cancel = false,
                                          std::optional<std::uint64_t> opening = std::nullopt)
{
    return std::unique_ptr<InputResult>(
        new InputResult(fixture.session->PrepareSelectionMoveClosureInput(cancel, opening)));
}

std::unique_ptr<PreparedResult> Prepare(Fixture& fixture, bool cancel = false,
                                        DocumentHistory::PrepareObserver observer = {}, CancelToken cancellation = {},
                                        const SelectionNudgeObserver* geometryObserver = nullptr)
{
    auto input = CaptureInput(fixture, cancel);
    if (!*input)
        return std::unique_ptr<PreparedResult>(new PreparedResult());
    return std::unique_ptr<PreparedResult>(new PreparedResult(DocumentSession::PrepareSelectionMoveClosurePublication(
        std::move(input->value()), cancellation, observer, geometryObserver)));
}

struct CancelAtTranslate final
{
    CancelSource* source = nullptr;
    bool reached = false;

    static void Checkpoint(void* context, SelectionNudgeStage stage, std::uint64_t) noexcept
    {
        auto& self = *static_cast<CancelAtTranslate*>(context);
        if (stage == SelectionNudgeStage::Translate)
        {
            self.reached = true;
            (void)self.source->Cancel();
        }
    }
};

struct HistoryFault final
{
    bool reached = false;
    static void Checkpoint(void* context, DocumentHistory::PrepareStage)
    {
        static_cast<HistoryFault*>(context)->reached = true;
        throw std::bad_alloc{};
    }
};

void PreparedOpeningAvoidsOwnerHistoryConstruction()
{
    auto fixture = std::unique_ptr<Fixture>(new Fixture());
    const auto identity = fixture->session->Selection()->loops.Identity();
    using OpeningInput = std::optional<SelectionMoveOpeningInput>;
    using OpeningResult = std::optional<PreparedSelectionMoveOpening>;
    auto input =
        std::unique_ptr<OpeningInput>(new OpeningInput(fixture->session->PrepareDeferredSelectionMoveOpeningInput()));
    Expect(fixture->ready && input->has_value() && !fixture->session->IsMovingSelection() &&
               fixture->session->history.CaptureState().Depth() == 0,
           "prepared-opening-capture-keeps-owner-history-and-geometry-unchanged");
    if (!*input)
        return;
    auto second =
        std::unique_ptr<OpeningInput>(new OpeningInput(fixture->session->PrepareDeferredSelectionMoveOpeningInput()));
    auto prepared = std::unique_ptr<OpeningResult>(
        new OpeningResult(DocumentSession::PrepareDeferredSelectionMoveOpening(std::move(input->value()))));
    Expect(prepared->has_value() && !fixture->session->IsMovingSelection() &&
               fixture->session->history.CaptureState().Depth() == 0 &&
               fixture->session->Selection()->loops.Identity() == identity,
           "prepared-opening-worker-history-stays-private-until-publication");
    if (!*prepared || !*second)
        return;
    auto duplicate = std::unique_ptr<OpeningResult>(
        new OpeningResult(DocumentSession::PrepareDeferredSelectionMoveOpening(std::move(second->value()))));
    Expect(fixture->session->ApplyPreparedDeferredSelectionMoveOpeningIfCurrent(prepared->value()) &&
               fixture->session->IsMovingSelection() && fixture->session->history.CaptureState().Depth() == 1 &&
               fixture->session->Selection()->loops.Identity() == identity &&
               fixture->session->DisplayedSelectionValue().translation == Vec2{},
           "prepared-opening-publishes-one-history-depth-and-scalar-mode");
    Expect(!fixture->session->ApplyPreparedDeferredSelectionMoveOpeningIfCurrent(prepared->value()) && *duplicate &&
               !fixture->session->ApplyPreparedDeferredSelectionMoveOpeningIfCurrent(duplicate->value()),
           "prepared-opening-cannot-reapply-or-publish-second-candidate");
    fixture->session->MoveSelection(-2, -3);
    auto close = Prepare(*fixture);
    Expect(*close && fixture->session->ApplyPreparedSelectionMoveClosureIfCurrent(close->value()) &&
               HasLiteral(fixture->session->Selection(), kOuter, kHole),
           "prepared-opening-and-worker-close-publish-literal-geometry");
    fixture->session->Undo();
    Expect(HasLiteral(fixture->session->Selection(), kOriginOuter, kOriginHole),
           "prepared-opening-worker-close-undo-restores-original");

    auto cancelled = std::unique_ptr<Fixture>(new Fixture());
    auto cancelledInput =
        std::unique_ptr<OpeningInput>(new OpeningInput(cancelled->session->PrepareDeferredSelectionMoveOpeningInput()));
    CancelSource cancellation;
    (void)cancellation.Cancel();
    auto noOpening =
        std::unique_ptr<OpeningResult>(new OpeningResult(DocumentSession::PrepareDeferredSelectionMoveOpening(
            std::move(cancelledInput->value()), cancellation.Token())));
    Expect(!*noOpening && !cancelled->session->IsMovingSelection() &&
               cancelled->session->history.CaptureState().Depth() == 0,
           "prepared-opening-cancellation-leaves-history-and-gesture-unopened");

    auto changed = std::unique_ptr<Fixture>(new Fixture());
    auto staleInput =
        std::unique_ptr<OpeningInput>(new OpeningInput(changed->session->PrepareDeferredSelectionMoveOpeningInput()));
    auto stale = std::unique_ptr<OpeningResult>(
        new OpeningResult(DocumentSession::PrepareDeferredSelectionMoveOpening(std::move(staleInput->value()))));
    changed->session->SetSelection(DocumentSelection{false, SelectionGeometry::Rect(0, 0, 2, 2)}, L"New selection");
    Expect(*stale && !changed->session->ApplyPreparedDeferredSelectionMoveOpeningIfCurrent(stale->value()) &&
               !changed->session->IsMovingSelection(),
           "prepared-opening-source-change-refuses-before-history-publication");
}

void DeferredGestureKeepsIdentityAndUsesRoundedScalar()
{
    auto fixture = std::unique_ptr<Fixture>(new Fixture());
    Expect(fixture->ready && fixture->session->BeginDeferredSelectionMove(), "begin-deferred-with-redo");
    if (!fixture->ready || !fixture->session->IsMovingSelection())
        return;
    const auto sourceIdentity = fixture->session->Selection()->loops.Identity();
    const auto opening = fixture->session->SelectionMoveOpening();
    const auto generationBeforeMove = fixture->session->Generation();
    fixture->session->MoveSelection(-1.5, -2.5);
    const auto display = fixture->session->DisplayedSelectionValue();
    Expect(fixture->session->Selection()->loops.Identity() == sourceIdentity &&
               fixture->session->Document()->selection->loops.Identity() == sourceIdentity && display.selection &&
               display.selection->loops.Identity() == sourceIdentity && display.translation == Vec2{-2, -3} &&
               fixture->session->SelectionMoveOpening() == opening &&
               fixture->session->Generation() == generationBeforeMove + 1,
           "negative-half-rounding-updates-only-display-scalar-and-generation");
    Expect(HasLiteral(fixture->session->DisplayedSelection(), kOuter, kHole),
           "displayed-outline-has-literal-translated-rectangle-and-hole");
    Expect(fixture->session->CanMoveSelection(Vec2{0.5, 0.5}),
           "reference-can-move-query-hits-translated-geometry-outside-source-outline");
    fixture->session->MoveSelection(-2, -3);
    Expect(fixture->session->DisplayedSelectionValue().translation == Vec2{-2, -3},
           "repeated-identical-rounded-sample-keeps-current-offset");
    fixture->session->MoveSelection((std::numeric_limits<double>::max)(), 0);
    Expect(fixture->session->DisplayedSelectionValue().translation.x == (std::numeric_limits<double>::max)(),
           "finite-maximum-offset-is-accepted-when-bounded-endpoints-stay-finite");
    auto collapsedReference = std::unique_ptr<Fixture>(new Fixture());
    const bool referenceBegan = collapsedReference->session->BeginSelectionMove();
    collapsedReference->session->MoveSelection((std::numeric_limits<double>::max)(), 0);
    const Vec2 collapsedPoint{(std::numeric_limits<double>::max)(), 2};
    Expect(referenceBegan && collapsedReference->session->Selection()->IsEmpty() &&
               !collapsedReference->session->CanMoveSelection(collapsedPoint) &&
               !fixture->session->CanMoveSelection(collapsedPoint),
           "collapsed-finite-offset-extent-is-not-movable-like-numeric-reference");
    fixture->session->MoveSelection(-2, -3);

    const auto stableIdentity = fixture->session->Selection()->loops.Identity();
    for (int i = 0; i < 40; ++i)
        fixture->session->MoveSelection(static_cast<double>(i % 7) - 3.0, static_cast<double>(i % 5) - 2.0);
    Expect(fixture->session->Selection()->loops.Identity() == stableIdentity &&
               fixture->session->Document()->selection->loops.Identity() == stableIdentity,
           "many-scalar-updates-retain-source-loop-storage-identity");
}

void InvalidDeltasDoNotMutatePendingState()
{
    auto fixture = std::unique_ptr<Fixture>(new Fixture());
    Expect(fixture->ready && fixture->session->BeginDeferredSelectionMove(), "invalid-delta-fixture-begins");
    if (!fixture->ready || !fixture->session->IsMovingSelection())
        return;
    fixture->session->MoveSelection(2, 1);
    const auto generation = fixture->session->Generation();
    const auto identity = fixture->session->Selection()->loops.Identity();
    const auto offset = fixture->session->DisplayedSelectionValue().translation;
    fixture->session->MoveSelection((std::numeric_limits<double>::quiet_NaN)(), 0);
    fixture->session->MoveSelection(0, (std::numeric_limits<double>::infinity)());
    fixture->session->MoveSelection(0, -(std::numeric_limits<double>::infinity)());
    Expect(
        fixture->session->Generation() == generation && fixture->session->Selection()->loops.Identity() == identity &&
            fixture->session->DisplayedSelectionValue().translation == offset && fixture->session->IsMovingSelection(),
        "nonfinite-samples-preserve-pending-state-and-generation");
    // This fixture uses canvas-clipped source coordinates in [1, 9], so no finite endpoint addition overflows:
    // DBL_MAX plus or minus these ordinary values remains finite. A wider synthetic source would violate this
    // fixture's source-geometry contract; do not fabricate a private overflow seam.
}

void WorkerEndCommitsLiteralGeometryAndHistory()
{
    auto fixture = std::unique_ptr<Fixture>(new Fixture());
    Expect(fixture->ready && fixture->session->BeginDeferredSelectionMove(), "worker-end-fixture-begins");
    if (!fixture->ready || !fixture->session->IsMovingSelection())
        return;
    const auto sourceIdentity = fixture->session->Selection()->loops.Identity();
    fixture->session->MoveSelection(-1.5, -2.5);
    auto prepared = Prepare(*fixture);
    Expect(*prepared && fixture->session->Selection()->loops.Identity() == sourceIdentity,
           "worker-end-prepares-with-live-source-identity-retained");
    if (!*prepared)
        return;
    Expect(fixture->session->ApplyPreparedSelectionMoveClosureIfCurrent(prepared->value()),
           "worker-end-publication-applies");
    Expect(!fixture->session->IsMovingSelection() && HasLiteral(fixture->session->Selection(), kOuter, kHole),
           "worker-end-publishes-literal-translated-vertices");
    fixture->session->Undo();
    Expect(HasLiteral(fixture->session->Selection(), kOriginOuter, kOriginHole), "worker-end-undo-restores-origin");
    fixture->session->Redo();
    Expect(HasLiteral(fixture->session->Selection(), kOuter, kHole), "worker-end-redo-restores-translated-outline");
}

void ZeroOffsetAndWorkerCancelPreserveRedo()
{
    auto zero = std::unique_ptr<Fixture>(new Fixture());
    Expect(zero->ready && zero->session->BeginDeferredSelectionMove(), "zero-offset-fixture-begins");
    if (!zero->ready || !zero->session->IsMovingSelection())
        return;
    const auto identity = zero->session->Selection()->loops.Identity();
    const auto redoName = zero->session->history.RedoName();
    auto noOp = Prepare(*zero);
    Expect(*noOp && zero->session->ApplyPreparedSelectionMoveClosureIfCurrent(noOp->value()) &&
               !zero->session->IsMovingSelection() && zero->session->Selection()->loops.Identity() == identity &&
               zero->session->history.CanRedo() && zero->session->history.RedoName() == redoName,
           "zero-offset-end-preserves-loop-identity-and-redo");

    auto cancel = std::unique_ptr<Fixture>(new Fixture());
    Expect(cancel->ready && cancel->session->BeginDeferredSelectionMove(), "worker-cancel-fixture-begins");
    if (!cancel->ready || !cancel->session->IsMovingSelection())
        return;
    cancel->session->MoveSelection(3, 2);
    auto prepared = Prepare(*cancel, true);
    Expect(*prepared && cancel->session->ApplyPreparedSelectionMoveClosureIfCurrent(prepared->value()) &&
               !cancel->session->IsMovingSelection() &&
               HasLiteral(cancel->session->Selection(), kOriginOuter, kOriginHole) &&
               cancel->session->history.CanRedo() && cancel->session->history.RedoName() == L"Seed redo",
           "worker-cancel-restores-origin-without-entry-and-retains-redo");
}

void PreparationCancellationAndHistoryFaultKeepGestureLive()
{
    auto fixture = std::unique_ptr<Fixture>(new Fixture());
    Expect(fixture->ready && fixture->session->BeginDeferredSelectionMove(), "cancel-checkpoint-fixture-begins");
    if (!fixture->ready || !fixture->session->IsMovingSelection())
        return;
    fixture->session->MoveSelection(1, 0);
    const auto generation = fixture->session->Generation();
    const auto identity = fixture->session->Selection()->loops.Identity();
    CancelSource cancellation;
    CancelAtTranslate observed{&cancellation};
    const SelectionNudgeObserver geometryObserver{&observed, &CancelAtTranslate::Checkpoint};
    auto input = CaptureInput(*fixture);
    auto cancelled =
        std::unique_ptr<PreparedResult>(new PreparedResult(DocumentSession::PrepareSelectionMoveClosurePublication(
            std::move(input->value()), cancellation.Token(), {}, &geometryObserver)));
    Expect(observed.reached && !*cancelled && fixture->session->Generation() == generation &&
               fixture->session->Selection()->loops.Identity() == identity && fixture->session->IsMovingSelection() &&
               fixture->session->DisplayedSelectionValue().translation == Vec2{1, 0},
           "geometry-checkpoint-cancellation-leaves-live-pending-document-unchanged");

    HistoryFault fault;
    const DocumentHistory::PrepareObserver historyObserver{&HistoryFault::Checkpoint, &fault};
    auto faultInput = CaptureInput(*fixture);
    bool threw = false;
    try
    {
        const auto faulted =
            std::unique_ptr<PreparedResult>(new PreparedResult(DocumentSession::PrepareSelectionMoveClosurePublication(
                std::move(faultInput->value()), {}, historyObserver)));
        (void)faulted;
    }
    catch (const std::bad_alloc&)
    {
        threw = true;
    }
    Expect(fault.reached && threw && fixture->session->Generation() == generation &&
               fixture->session->Selection()->loops.Identity() == identity && fixture->session->IsMovingSelection(),
           "history-preparation-allocation-fault-leaves-live-pending-document-unchanged");
}

void AuthorityRefusalsAndOwnerReset()
{
    auto old = std::unique_ptr<Fixture>(new Fixture());
    Expect(old->ready && old->session->BeginDeferredSelectionMove(), "opening-guard-fixture-begins");
    const auto oldOpening = old->session->SelectionMoveOpening();
    auto oldCapture = CaptureInput(*old, false, oldOpening);
    Expect(oldCapture->has_value(), "capture-exists-before-closing-opening");
    old->session->EndSelectionMove();
    Expect(old->session->BeginDeferredSelectionMove() &&
               !old->session->PrepareSelectionMoveClosureInput(false, oldOpening),
           "older-opening-refused-after-reopen");

    auto moved = std::unique_ptr<Fixture>(new Fixture());
    Expect(moved->ready && moved->session->BeginDeferredSelectionMove(), "moved-input-fixture-begins");
    moved->session->MoveSelection(1, 0);
    auto movedInput = CaptureInput(*moved);
    auto consumed = std::unique_ptr<PreparedResult>(
        new PreparedResult(DocumentSession::PrepareSelectionMoveClosurePublication(std::move(movedInput->value()))));
    auto reused = std::unique_ptr<PreparedResult>(
        new PreparedResult(DocumentSession::PrepareSelectionMoveClosurePublication(std::move(movedInput->value()))));
    Expect(*consumed && !*reused, "moved-from-worker-input-is-refused");

    auto stale = std::unique_ptr<Fixture>(new Fixture());
    Expect(stale->ready && stale->session->BeginDeferredSelectionMove(), "offset-guard-fixture-begins");
    stale->session->MoveSelection(1, 0);
    auto stalePrepared = Prepare(*stale);
    stale->session->MoveSelection(2, 0);
    Expect(*stalePrepared && !stale->session->IsPreparedSelectionMoveClosureCurrent(stalePrepared->value()) &&
               !stale->session->ApplyPreparedSelectionMoveClosureIfCurrent(stalePrepared->value()),
           "prepared-result-with-older-offset-is-refused");

    auto foreign = std::unique_ptr<Fixture>(new Fixture());
    auto other = std::unique_ptr<Fixture>(new Fixture());
    Expect(foreign->ready && other->ready && foreign->session->BeginDeferredSelectionMove() &&
               other->session->BeginDeferredSelectionMove(),
           "foreign-session-fixtures-begin");
    auto foreignPrepared = Prepare(*foreign);
    Expect(*foreignPrepared && !other->session->IsPreparedSelectionMoveClosureCurrent(foreignPrepared->value()) &&
               !other->session->ApplyPreparedSelectionMoveClosureIfCurrent(foreignPrepared->value()),
           "foreign-session-cannot-accept-prepared-closure");

    auto changed = std::unique_ptr<Fixture>(new Fixture());
    Expect(changed->ready && changed->session->BeginDeferredSelectionMove(), "generation-guard-fixture-begins");
    changed->session->MoveSelection(1, 0);
    auto generationPrepared = Prepare(*changed);
    changed->session->EditForTest([](CanvasDocument& document) { document.resolution += 1.0; });
    Expect(*generationPrepared && !changed->session->IsPreparedSelectionMoveClosureCurrent(generationPrepared->value()),
           "newer-document-generation-invalidates-prepared-closure");

    auto replaced = std::unique_ptr<Fixture>(new Fixture());
    Expect(replaced->ready && replaced->session->BeginDeferredSelectionMove(), "replacement-fixture-begins");
    replaced->session->MoveSelection(4, 3);
    replaced->Install();
    Expect(!replaced->session->IsMovingSelection() &&
               replaced->session->DisplayedSelectionValue().translation == Vec2{} &&
               HasLiteral(replaced->session->Selection(), kOriginOuter, kOriginHole),
           "replaced-document-clears-pending-offset-and-mode");
}

void SourceAndPanelGuardsAndReferenceRoutes()
{
    auto sourceChanged = std::unique_ptr<Fixture>(new Fixture());
    Expect(sourceChanged->ready && sourceChanged->session->BeginDeferredSelectionMove(),
           "external-source-change-fixture-begins");
    sourceChanged->session->SetSelection(DocumentSelection{false, SelectionGeometry::Rect(0, 0, 2, 2)},
                                         L"External source replacement");
    Expect(!sourceChanged->session->PrepareSelectionMoveClosureInput(false),
           "external-selection-replacement-before-capture-is-refused");

    auto panelBefore = std::unique_ptr<Fixture>(new Fixture());
    panelBefore->session->BeginColorEdit(FilterKind::Levels, true);
    Expect(!panelBefore->session->BeginDeferredSelectionMove() &&
               panelBefore->session->HasColorEdit(FilterKind::Levels),
           "existing-panel-refuses-deferred-begin");

    auto panelAfter = std::unique_ptr<Fixture>(new Fixture());
    Expect(panelAfter->ready && panelAfter->session->BeginDeferredSelectionMove(),
           "panel-after-capture-fixture-begins");
    panelAfter->session->MoveSelection(1, 0);
    auto captured = CaptureInput(*panelAfter);
    auto prepared = std::unique_ptr<PreparedResult>(
        new PreparedResult(DocumentSession::PrepareSelectionMoveClosurePublication(std::move(captured->value()))));
    panelAfter->session->BeginColorEdit(FilterKind::Levels, true);
    Expect(*prepared && panelAfter->session->HasColorEdit(FilterKind::Levels) &&
               !panelAfter->session->IsPreparedSelectionMoveClosureCurrent(prepared->value()) &&
               !panelAfter->session->ApplyPreparedSelectionMoveClosureIfCurrent(prepared->value()),
           "panel-opened-after-capture-invalidates-prepared-closure");

    auto direct = std::unique_ptr<Fixture>(new Fixture());
    Expect(direct->ready && direct->session->BeginSelectionMove(), "reference-move-api-still-begins");
    const auto originalIdentity = direct->session->Selection()->loops.Identity();
    direct->session->MoveSelection(1, 0);
    Expect(direct->session->Document()->selection->loops.Identity() != originalIdentity &&
               HasLiteral(direct->session->DisplayedSelection(),
                          std::array<Vec2, 4>{{{2, 1}, {10, 1}, {10, 9}, {2, 9}}},
                          std::array<Vec2, 4>{{{4, 3}, {4, 7}, {8, 7}, {8, 3}}}),
           "reference-move-updates-materialized-selection-during-gesture");
    direct->session->EndSelectionMove();
    Expect(!direct->session->IsMovingSelection() && direct->session->DisplayedSelectionValue().translation == Vec2{},
           "direct-numeric-end-closes-reference-route");

    auto deferredEnd = std::unique_ptr<Fixture>(new Fixture());
    Expect(deferredEnd->ready && deferredEnd->session->BeginDeferredSelectionMove(),
           "direct-deferred-end-fixture-begins");
    deferredEnd->session->MoveSelection(1, 0);
    deferredEnd->session->EndSelectionMove();
    Expect(!deferredEnd->session->IsMovingSelection() &&
               HasLiteral(deferredEnd->session->Selection(), std::array<Vec2, 4>{{{2, 1}, {10, 1}, {10, 9}, {2, 9}}},
                          std::array<Vec2, 4>{{{4, 3}, {4, 7}, {8, 7}, {8, 3}}}),
           "direct-numeric-end-materializes-deferred-reference-translation");

    auto numeric = std::unique_ptr<Fixture>(new Fixture());
    Expect(numeric->ready && numeric->session->BeginDeferredSelectionMove(), "numeric-reader-fixture-begins");
    numeric->session->MoveSelection(1, 0);
    const auto numericDisplay = numeric->session->DisplayedSelection();
    const bool hit = numeric->session->CanMoveSelection(Vec2{2.5, 2.5});
    Expect(HasLiteral(numericDisplay, std::array<Vec2, 4>{{{2, 1}, {10, 1}, {10, 9}, {2, 9}}},
                      std::array<Vec2, 4>{{{4, 3}, {4, 7}, {8, 7}, {8, 3}}}) &&
               hit,
           "numeric-displayed-selection-and-can-move-use-reference-materialized-geometry");
    numeric->session->CancelSelectionMove();
    Expect(!numeric->session->IsMovingSelection() &&
               HasLiteral(numeric->session->Selection(), kOriginOuter, kOriginHole),
           "direct-numeric-cancel-restores-reference-origin");
}
} // namespace

int RunDocumentDeferredSelectionMoveTests()
{
    gFailures = 0;
    gAssertions = 0;
    PreparedOpeningAvoidsOwnerHistoryConstruction();
    DeferredGestureKeepsIdentityAndUsesRoundedScalar();
    InvalidDeltasDoNotMutatePendingState();
    WorkerEndCommitsLiteralGeometryAndHistory();
    ZeroOffsetAndWorkerCancelPreserveRedo();
    PreparationCancellationAndHistoryFaultKeepGestureLive();
    AuthorityRefusalsAndOwnerReset();
    SourceAndPanelGuardsAndReferenceRoutes();
    std::printf("SUMMARY deferred-selection-move assertions=%d failures=%d\n", gAssertions, gFailures);
    return gFailures;
}
