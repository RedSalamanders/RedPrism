#include "Document.Tests.SelectionHistoryOwnership.h"

#include "../Support/DocumentFixture.h"
#include "SelectionMoveOpeningWork.h"
#include "SelectionMoveWork.h"
#include "SharedPublicationMemory.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <utility>

namespace
{
unsigned int gPassed = 0;
unsigned int gFailed = 0;
unsigned int gAssertions = 0;

void Expect(bool condition, const char* label) noexcept
{
    ++gAssertions;
    std::printf("%s selection-history-ownership.%s\n", condition ? "PASS" : "FAIL", label);
    condition ? ++gPassed : ++gFailed;
}

constexpr std::array<Vec2, 4> kOrigin{{{1, 2}, {3, 2}, {3, 4}, {1, 4}}};
constexpr std::array<Vec2, 4> kMoved{{{-1, 5}, {1, 5}, {1, 7}, {-1, 7}}};

DocumentSelection SquareSelection()
{
    return DocumentSelection{false, SelectionLoops{SelectionLoop{kOrigin[0], kOrigin[1], kOrigin[2], kOrigin[3]}}};
}

bool HasLiteral(const std::optional<DocumentSelection>& selection, const std::array<Vec2, 4>& expected)
{
    if (!selection || selection->antialiased || selection->loops.size() != 1)
        return false;
    const auto& loop = selection->loops.Get()[0];
    if (loop.size() != expected.size())
        return false;
    for (std::size_t i = 0; i < expected.size(); ++i)
    {
        if (loop[i] != expected[i])
            return false;
    }
    return true;
}

struct Fixture final
{
    std::unique_ptr<DocumentSession> session{new DocumentSession()};
    bool ready = false;

    Fixture()
    {
        DocumentFixture source;
        source.id = Guid::Generate();
        source.width = 8;
        source.height = 8;
        source.selection = SquareSelection();
        auto layer = ImageLayer::Blank(L"Selection history ownership", Vec2{8, 8});
        const std::array<std::uint8_t, 4> pixel{0, 0, 0, 255};
        layer.asset =
            RasterAsset{Raster::Make(1, 1, RasterFormat::Bgra8888, pixel.data(), pixel.size()), {}, layer.name};
        const Guid active = layer.id;
        source.layers.push_back(std::move(layer));
        session->ReplaceDocument(std::move(source).Seal(), active);
        session->SetSelection(DocumentSelection{false, SelectionLoops{SelectionLoop{{0, 0}, {1, 0}, {1, 1}, {0, 1}}}},
                              L"Seed redo");
        session->Undo();
        ready = session->Document().has_value() && HasLiteral(session->Selection(), kOrigin) &&
                session->history.CanRedo() && session->history.RedoName() == L"Seed redo";
    }
};

using OpeningInput = std::optional<SelectionMoveOpeningInput>;
using PreparedOpening = std::optional<PreparedSelectionMoveOpening>;
using ClosureInput = std::optional<SelectionMoveClosureInput>;
using PreparedClosure = std::optional<PreparedSelectionMoveClosure>;

std::unique_ptr<OpeningInput> CaptureOpening(Fixture& fixture)
{
    return std::make_unique<OpeningInput>(fixture.session->PrepareDeferredSelectionMoveOpeningInput());
}

bool SameOpeningSource(const Fixture& fixture, const DocumentHistory::StateView& history, std::uint64_t generation,
                       const void* selectionIdentity, const void* layersIdentity)
{
    return fixture.session->Generation() == generation && fixture.session->history.IsCurrent(history) &&
           fixture.session->Selection() && fixture.session->Selection()->loops.Identity() == selectionIdentity &&
           fixture.session->Document() && fixture.session->Document()->layers.Identity() == layersIdentity &&
           !fixture.session->IsMovingSelection() && fixture.session->history.CanRedo() &&
           fixture.session->history.RedoName() == L"Seed redo";
}

bool SameSessionSource(const Fixture& fixture, const DocumentHistory::StateView& history, std::uint64_t generation,
                       const void* selectionIdentity, const void* layersIdentity)
{
    return fixture.session->Generation() == generation && fixture.session->history.IsCurrent(history) &&
           fixture.session->Selection() && fixture.session->Selection()->loops.Identity() == selectionIdentity &&
           fixture.session->Document() && fixture.session->Document()->layers.Identity() == layersIdentity;
}

void SelectedOpeningOwnsPrivateHistory()
{
    auto fixture = std::make_unique<Fixture>();
    auto input = CaptureOpening(*fixture);
    Expect(fixture->ready && input->has_value(), "selected-opening-fixture-captures-source");
    if (!fixture->ready || !input->has_value())
        return;
    auto history = fixture->session->history.CaptureState();
    const auto depth = history.Depth();
    const auto futureCount = history.FutureCount();
    const auto generation = fixture->session->Generation();
    const void* selectionIdentity = fixture->session->Selection()->loops.Identity();
    const void* layersIdentity = fixture->session->Document()->layers.Identity();
    auto memory = SharedPublicationMemory::CreateForWork();
    auto prepared = std::make_unique<PreparedOpening>(
        DocumentSession::PrepareDeferredSelectionMoveOpening(std::move(input->value()), {}, {}, memory));
    Expect(prepared->has_value() && memory->LocalAllocationBytes() > 0 && history.Depth() == depth &&
               history.FutureCount() == futureCount &&
               SameOpeningSource(*fixture, history, generation, selectionIdentity, layersIdentity),
           "selected-opening-prepares-charged-private-history-with-source-and-redo-untouched");

    const bool applied = *prepared && fixture->session->ApplyPreparedDeferredSelectionMoveOpeningIfCurrent(**prepared);
    Expect(!applied && history.Depth() == depth && history.FutureCount() == futureCount &&
               SameOpeningSource(*fixture, history, generation, selectionIdentity, layersIdentity),
           "unbound-selected-opening-refuses-without-changing-session-or-history");

    prepared.reset();
    input.reset();
    Expect(memory->LocalAllocationBytes() == 0 && fixture->session->history.IsCurrent(history),
           "dropping-private-opening-input-and-preparation-refunds-all-local-bytes");

    auto cancelled = std::make_unique<Fixture>();
    auto cancelledInput = CaptureOpening(*cancelled);
    Expect(cancelled->ready && cancelledInput->has_value(), "pre-cancelled-opening-fixture-captures-source");
    if (!cancelled->ready || !cancelledInput->has_value())
        return;
    auto cancelledMemory = SharedPublicationMemory::CreateForWork();
    CancelSource cancellation;
    (void)cancellation.Cancel();
    const auto noOpening = DocumentSession::PrepareDeferredSelectionMoveOpening(
        std::move(cancelledInput->value()), cancellation.Token(), {}, cancelledMemory);
    Expect(cancelled->ready && !noOpening && cancelledMemory->LocalAllocationBytes() == 0 &&
               !cancelled->session->IsMovingSelection() && cancelled->session->history.CaptureState().Depth() == 0,
           "pre-cancelled-opening-allocates-no-private-history");

    auto limited = std::make_unique<Fixture>();
    auto limitedInput = CaptureOpening(*limited);
    Expect(limited->ready && limitedInput->has_value(), "allocation-limit-fixture-captures-source");
    if (!limited->ready || !limitedInput->has_value())
        return;
    const auto limitedHistory = limited->session->history.CaptureState();
    const auto limitedGeneration = limited->session->Generation();
    const void* limitedSelection = limited->session->Selection()->loops.Identity();
    const void* limitedLayers = limited->session->Document()->layers.Identity();
    auto tinyMemory = SharedPublicationMemory::CreateForWork(1);
    bool threwBadAlloc = false;
    try
    {
        auto impossible =
            DocumentSession::PrepareDeferredSelectionMoveOpening(std::move(limitedInput->value()), {}, {}, tinyMemory);
        (void)impossible;
    }
    catch (const std::bad_alloc&)
    {
        threwBadAlloc = true;
    }
    Expect(limited->ready && threwBadAlloc && tinyMemory->LocalAllocationBytes() == 0 &&
               SameOpeningSource(*limited, limitedHistory, limitedGeneration, limitedSelection, limitedLayers),
           "tiny-local-limit-fails-allocation-and-preserves-original-source-and-history");
}

void DefaultReferencePathKeepsNumericBehavior()
{
    auto openingFixture = std::make_unique<Fixture>();
    auto openingInput = CaptureOpening(*openingFixture);
    Expect(openingFixture->ready && openingInput->has_value(), "default-prepared-opening-captures-fixed-source");
    if (!openingFixture->ready || !openingInput->has_value())
        return;
    auto opening = std::make_unique<PreparedOpening>(
        DocumentSession::PrepareDeferredSelectionMoveOpening(std::move(openingInput->value())));
    Expect(opening->has_value() &&
               openingFixture->session->ApplyPreparedDeferredSelectionMoveOpeningIfCurrent(**opening) &&
               openingFixture->session->IsDeferredSelectionMove() &&
               HasLiteral(openingFixture->session->Selection(), kOrigin),
           "default-prepared-opening-publishes-scalar-mode-without-requiring-memory-binding");
    auto endingInput = std::make_unique<ClosureInput>(openingFixture->session->PrepareSelectionMoveClosureInput(false));
    auto ending = endingInput->has_value()
                      ? std::make_unique<PreparedClosure>(
                            DocumentSession::PrepareSelectionMoveClosurePublication(std::move(endingInput->value())))
                      : std::make_unique<PreparedClosure>();
    Expect(ending->has_value() && openingFixture->session->ApplyPreparedSelectionMoveClosureIfCurrent(**ending) &&
               openingFixture->session->history.CanRedo() &&
               openingFixture->session->history.RedoName() == L"Seed redo",
           "default-prepared-opening-and-noop-closure-preserve-existing-redo");

    auto moved = std::make_unique<Fixture>();
    Expect(moved->ready && moved->session->BeginDeferredSelectionMove(), "reference-fixture-begins-with-redo");
    if (!moved->ready || !moved->session->IsMovingSelection())
        return;
    moved->session->MoveSelection(-2, 3);
    auto input = std::make_unique<ClosureInput>(moved->session->PrepareSelectionMoveClosureInput(false));
    Expect(input->has_value(), "reference-closure-captures-pending-source");
    if (!input->has_value())
        return;
    auto prepared = std::make_unique<PreparedClosure>(
        DocumentSession::PrepareSelectionMoveClosurePublication(std::move(input->value())));
    Expect(prepared->has_value() && moved->session->ApplyPreparedSelectionMoveClosureIfCurrent(**prepared) &&
               HasLiteral(moved->session->Selection(), kMoved),
           "default-no-memory-closure-applies-fixed-translated-square");

    auto noOp = std::make_unique<Fixture>();
    Expect(noOp->ready && noOp->session->BeginDeferredSelectionMove(), "reference-noop-fixture-begins-with-redo");
    if (!noOp->ready || !noOp->session->IsMovingSelection())
        return;
    const auto redoName = noOp->session->history.RedoName();
    auto noOpInput = std::make_unique<ClosureInput>(noOp->session->PrepareSelectionMoveClosureInput(false));
    Expect(noOpInput->has_value(), "reference-noop-captures-pending-source");
    if (!noOpInput->has_value())
        return;
    auto noOpPrepared = std::make_unique<PreparedClosure>(
        DocumentSession::PrepareSelectionMoveClosurePublication(std::move(noOpInput->value())));
    Expect(noOpPrepared->has_value() && noOp->session->ApplyPreparedSelectionMoveClosureIfCurrent(**noOpPrepared) &&
               HasLiteral(noOp->session->Selection(), kOrigin) && noOp->session->history.CanRedo() &&
               noOp->session->history.RedoName() == redoName,
           "default-no-memory-noop-end-preserves-origin-and-redo");
}

void SelectedClosureRejectsUnboundHistoryOwner()
{
    auto fixture = std::make_unique<Fixture>();
    Expect(fixture->ready && fixture->session->BeginDeferredSelectionMove(), "selected-closure-fixture-begins");
    if (!fixture->ready || !fixture->session->IsMovingSelection())
        return;
    fixture->session->MoveSelection(-2, 3);
    auto history = fixture->session->history.CaptureState();
    const auto generation = fixture->session->Generation();
    const void* selectionIdentity = fixture->session->Selection()->loops.Identity();
    const void* layersIdentity = fixture->session->Document()->layers.Identity();
    auto input = std::make_unique<ClosureInput>(fixture->session->PrepareSelectionMoveClosureInput(false));
    Expect(input->has_value(), "selected-closure-captures-pending-source");
    if (!input->has_value())
        return;
    auto memory = SharedPublicationMemory::CreateForWork();
    std::weak_ptr<const SharedPublicationMemory> weakMemory = memory;
    auto prepared = std::make_unique<PreparedClosure>(DocumentSession::PrepareSelectionMoveClosurePublication(
        std::move(input->value()), {}, {}, nullptr, {}, memory));
    Expect(prepared->has_value() && memory->LocalAllocationBytes() > 0 &&
               SameSessionSource(*fixture, history, generation, selectionIdentity, layersIdentity) &&
               fixture->session->IsMovingSelection(),
           "selected-closure-prepares-history-and-translated-geometry-under-one-owner");

    memory.reset();
    auto retainedMemory = weakMemory.lock();
    const bool preparedRetainsChargedOwner = retainedMemory && retainedMemory->LocalAllocationBytes() > 0;
    retainedMemory.reset();
    const bool applied = *prepared && fixture->session->ApplyPreparedSelectionMoveClosureIfCurrent(**prepared);
    Expect(preparedRetainsChargedOwner && !applied &&
               SameSessionSource(*fixture, history, generation, selectionIdentity, layersIdentity) &&
               fixture->session->IsMovingSelection() &&
               fixture->session->DisplayedSelectionValue().translation == Vec2{-2, 3},
           "unbound-selected-closure-refuses-before-history-or-document-publication");

    auto heldHistory = fixture->session->history.CaptureState();
    const auto heldDepth = heldHistory.Depth();
    const auto heldFutureCount = heldHistory.FutureCount();
    const std::wstring heldRedoName = heldHistory.RedoName();
    history = DocumentHistory::StateView{};
    fixture.reset();
    prepared.reset();
    input.reset();
    Expect(heldHistory.Depth() == heldDepth && heldHistory.FutureCount() == heldFutureCount &&
               heldHistory.RedoName() == heldRedoName && weakMemory.expired(),
           "unchanged-live-history-view-outlives-discarded-private-output");
    heldHistory = DocumentHistory::StateView{};
    Expect(heldHistory.Depth() == 0, "dropping-state-view-releases-its-history-roots");
}
} // namespace

int RunDocumentSelectionHistoryOwnershipTests()
{
    gPassed = 0;
    gFailed = 0;
    gAssertions = 0;
    SelectedOpeningOwnsPrivateHistory();
    DefaultReferencePathKeepsNumericBehavior();
    SelectedClosureRejectsUnboundHistoryOwner();
    std::printf("SUMMARY selection-history-ownership assertions=%u passed=%u failed=%u\n", gAssertions, gPassed,
                gFailed);
    return gFailed == 0 ? 0 : 1;
}
