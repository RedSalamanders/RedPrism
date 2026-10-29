#include "Application.h"

#include "ChromeHost.h"
#include "Runtime.Tests.DocumentLifecycleTestGate.h"
#include "Selection.h"
#include "Tests/Support/Support.Tests.DocumentFixture.h"
#include "resource.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <utility>

int Application::RunDocumentLifecycleSelfTest() noexcept
{
    enum class Mode
    {
        NewDefault,
        NewCanvasCopiedDraft,
        CloseProject,
        CloseAbsentPreservesRedo,
        CancelBeforePreparation,
        CancelAfterPreparation,
        PreparationFailure,
        CaptureFailure,
        AdmissionFailure,
        HistoryFailure,
        StaleGeneration,
        StaleReplacement,
        StaleHistoryEpoch,
        NewThenUndo,
    };
// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4623 4626 5027)
    struct State final
    {
        Application& owner;
        Mode mode;
        ContinuationReceipt receipt;
        std::optional<ContinuationReceipt> youngerUndo;
        Raster source;
        Guid revision;
        std::uint64_t generation = 0;
        std::uint64_t documentSerial = 0;
        int errorsBefore = 0;
        bool visited = false;
        bool held = false;
        bool cancelPending = false;
        bool youngerPending = false;
    };
#pragma warning(pop)

    unsigned int checks = 0;
    int failures = 0;
    const auto expect = [&](bool condition, const char* fact, Mode mode) noexcept
    {
        ++checks;
        failures += condition ? 0 : 1;
        std::printf("%s lifecycle-host.mode-%u.%s\n", condition ? "PASS" : "FAIL", static_cast<unsigned int>(mode),
                    fact);
        std::fflush(stdout);
    };
    const auto dispatch = [](void* context, void* native) noexcept
    {
        auto& state = *static_cast<State*>(context);
        auto& owner = state.owner;
        auto& message = *static_cast<MSG*>(native);
        const auto gate = owner._documentLifecycleTestGate;
        if (!gate || message.message != DocumentLifecycleTestGate::kMessage)
        {
            owner.DispatchQueuedMessage(message);
            return;
        }

        state.visited = true;
        const ImageLayer* active = owner._session.ActiveLayer();
        state.held = owner._activeHostRequest && owner._documentLifecycleTicket && owner._documentLifecycleStamp &&
                     owner._historyWork.Busy() && owner._hostCommitStamp == owner._documentLifecycleStamp &&
                     gate->captureReservationHeld.load() && gate->reservationBytes != 0 &&
                     owner._jobs.ReservedMemoryBytes() >= gate->reservationBytes &&
                     state.receipt.Read().outcome == HostOutcome::Pending && owner._session.Document() && active &&
                     active->asset && active->asset->image == state.source &&
                     owner._session.history.CurrentRevision() == state.revision;

        switch (state.mode)
        {
        case Mode::NewCanvasCopiedDraft:
            owner._sizeDialog.newWidth = 91;
            owner._sizeDialog.newHeight = 93;
            break;
        case Mode::CancelBeforePreparation:
        case Mode::CancelAfterPreparation:
            owner.CancelHostContinuations();
            state.cancelPending = state.receipt.Read().outcome == HostOutcome::Pending &&
                                  owner._documentLifecycleTicket && owner._historyWork.Busy();
            break;
        case Mode::StaleGeneration:
            EditLayerRecordsForTest(owner._session,
                                    [](DocumentFixture& document) { document.layers.back().opacity = 0.5; });
            break;
        case Mode::StaleReplacement:
        {
            CanvasDocument replacement = owner._session.Document().value();
            const std::optional<Guid> activeID = owner._session.ActiveLayerID();
            owner._session.ReplaceDocument(std::move(replacement), activeID);
            break;
        }
        case Mode::StaleHistoryEpoch:
            owner._session.history.Begin(L"Lifecycle stale witness", owner._session.Document(),
                                         owner._session.ActiveLayerID());
            owner._session.history.End(owner._session.Document(), owner._session.ActiveLayerID());
            break;
        case Mode::NewThenUndo:
            state.youngerUndo = owner.QueueHostInput(HostCommandInput{ChromeHost::kCommandUndo});
            state.youngerPending = state.youngerUndo->Read().outcome == HostOutcome::Pending &&
                                   owner._historyWork.Busy() && owner._documentLifecycleTicket.has_value();
            break;
        case Mode::AdmissionFailure:
        case Mode::CaptureFailure:
        case Mode::CloseAbsentPreservesRedo:
        case Mode::CloseProject:
        case Mode::HistoryFailure:
        case Mode::NewDefault:
        case Mode::PreparationFailure:
        default:
            break;
        }
        (void)SetEvent(gate->release.get());
    };

    const auto settle = [this]() noexcept
    { return SettleJobs(_jobs, 15000, DispatchMessageThunk, this) == SettleResult::Settled; };
    const auto cleanup = wil::scope_exit(
        [&]() noexcept
        {
            if (_documentLifecycleTestGate && _documentLifecycleTestGate->release)
                (void)SetEvent(_documentLifecycleTestGate->release.get());
            CancelHostContinuations();
            if (!settle())
                std::_Exit(5);
            if (_session.PendingTransformEdit())
            {
                const auto& pending = _session.PendingTransformEdit().value();
                (void)QueueHostInput(
                    HostTransformFinishInput{pending.layerID, _session.TransformDraftRevision(), true});
                if (!settle())
                    std::_Exit(5);
            }
            _documentLifecycleTestGate.reset();
            CancelSizeCard();
        });

    const auto makeFixture = [this, &settle]() noexcept
    {
        if (_documentLifecycleTestGate && _documentLifecycleTestGate->release)
            (void)SetEvent(_documentLifecycleTestGate->release.get());
        _documentLifecycleTestGate.reset();
        CancelHostContinuations();
        if (!settle())
            return false;
        if (_session.PendingTransformEdit())
        {
            const auto& pending = _session.PendingTransformEdit().value();
            (void)QueueHostInput(HostTransformFinishInput{pending.layerID, _session.TransformDraftRevision(), true});
            if (!settle())
                return false;
        }

        DocumentFixture document;
        document.id = Guid::Generate();
        document.width = 8;
        document.height = 4;
        constexpr std::array<std::uint8_t, 4> pixel{23, 47, 89, 255};
        const Raster image = Raster::Make(1, 1, RasterFormat::Bgra8888, pixel.data(), pixel.size());
        if (!image.HasStorage())
            return false;
        ImageLayer lower = ImageLayer::Blank(L"Lifecycle lower", Vec2{1, 1});
        lower.asset = RasterAsset{image, Raster::MakeThumbnail(image), L"Lifecycle lower"};
        ImageLayer upper = ImageLayer::Blank(L"Lifecycle upper", Vec2{1, 1});
        upper.asset = RasterAsset{image, Raster::MakeThumbnail(image), L"Lifecycle upper"};
        const Guid active = upper.id;
        document.layers.push_back(std::move(lower));
        document.layers.push_back(std::move(upper));
        document.selection = DocumentSelection{true, SelectionGeometry::Rect(0, 0, 1, 1)};
        _session.ReplaceDocument(std::move(document).Seal(), active);
        _session.history.Reset();
        _session.history.MarkSaved();
        _session.SetForegroundColor(0xFF3167A9u);
        _session.SetBackgroundColor(0xFFE2B45Cu);
        _documentPath = L"Z:\\lifecycle-host-test\\source.prism";
        return _session.CopySelection() && _session.Clipboard().has_value() &&
               _session.Clipboard()->image.HasStorage() && _session.Clipboard()->image.Width() == 1 &&
               _session.Clipboard()->image.Height() == 1 && _session.Clipboard()->image.Data()[0] == pixel[0] &&
               _session.Clipboard()->image.Data()[1] == pixel[1] && _session.Clipboard()->image.Data()[2] == pixel[2] &&
               _session.Clipboard()->image.Data()[3] == pixel[3];
    };
    const auto privateStatePreserved = [this]() noexcept
    {
        const auto& clipboard = _session.Clipboard();
        return _session.ForegroundColor() == 0xFF3167A9u && _session.BackgroundColor() == 0xFFE2B45Cu &&
               clipboard.has_value() && clipboard->image.HasStorage() && clipboard->image.Width() == 1 &&
               clipboard->image.Height() == 1 && clipboard->image.Data()[0] == 23 && clipboard->image.Data()[1] == 47 &&
               clipboard->image.Data()[2] == 89 && clipboard->image.Data()[3] == 255;
    };

    if (!makeFixture())
    {
        SelfTestFailure(L"The document lifecycle host fixture could not be seeded.\n");
        return 5;
    }

    constexpr std::array modes{Mode::NewDefault,
                               Mode::NewCanvasCopiedDraft,
                               Mode::CloseProject,
                               Mode::CloseAbsentPreservesRedo,
                               Mode::CancelBeforePreparation,
                               Mode::CancelAfterPreparation,
                               Mode::PreparationFailure,
                               Mode::CaptureFailure,
                               Mode::AdmissionFailure,
                               Mode::HistoryFailure,
                               Mode::StaleGeneration,
                               Mode::StaleReplacement,
                               Mode::StaleHistoryEpoch,
                               Mode::NewThenUndo};
    for (const Mode mode : modes)
    {
        if (!makeFixture())
        {
            SelfTestFailure(L"The document lifecycle host fixture could not be reset.\n");
            return 5;
        }

        if (mode == Mode::CloseAbsentPreservesRedo)
        {
            _documentLifecycleTestGate.reset();
            const ContinuationReceipt makeNew = QueueHostInput(HostNewInput{});
            if (!settle() || makeNew.Read().outcome != HostOutcome::Succeeded)
                return 5;
            _session.Undo(); // Seed the real New Canvas redo branch and the absent document state.
            _session.history.MarkSaved();
            _documentPath = L"Z:\\lifecycle-host-test\\absent.prism";
        }

        _documentLifecycleTestGate = std::make_shared<DocumentLifecycleTestGate>();
        if (!_documentLifecycleTestGate->release)
            return 5;
        const auto gate = _documentLifecycleTestGate;
        gate->afterPreparation = mode == Mode::CancelAfterPreparation || mode == Mode::NewCanvasCopiedDraft ||
                                 mode == Mode::StaleGeneration || mode == Mode::StaleReplacement ||
                                 mode == Mode::StaleHistoryEpoch || mode == Mode::NewThenUndo;
        gate->throwPreparationFailure = mode == Mode::PreparationFailure;
        gate->throwCaptureFailure = mode == Mode::CaptureFailure;
        gate->throwAdmissionFailure = mode == Mode::AdmissionFailure;
        if (mode == Mode::HistoryFailure)
            gate->historyFailure = DocumentHistory::PrepareStage::AfterSnapshot;
        if (mode == Mode::NewCanvasCopiedDraft)
        {
            _sizeDialog = HostSizeDraft{};
            _sizeDialog.kind = SizeCardKind::NewCanvas;
            _sizeDialog.newWidth = 5;
            _sizeDialog.newHeight = 7;
            ++_sizeInvocation;
        }

        const Raster source =
            _session.ActiveLayer() && _session.ActiveLayer()->asset ? _session.ActiveLayer()->asset->image : Raster{};
        const Guid revision = _session.history.CurrentRevision();
        const auto generation = _session.Generation();
        const auto documentSerial = _session.DocumentSerial();
        const int errorsBefore = _errorsShown;
        State state{*this, mode, {}, {}, source, revision, generation, documentSerial, errorsBefore};
        switch (mode)
        {
        case Mode::NewDefault:
        case Mode::NewThenUndo:
            state.receipt = QueueHostInput(HostNewInput{});
            break;
        case Mode::NewCanvasCopiedDraft:
            state.receipt = QueueHostInput(HostSizeApplyInput{_sizeDialog, _sizeInvocation});
            break;
        case Mode::CloseProject:
        case Mode::CloseAbsentPreservesRedo:
            state.receipt = QueueHostInput(HostCloseProjectInput{});
            break;
        case Mode::AdmissionFailure:
        case Mode::CancelAfterPreparation:
        case Mode::CancelBeforePreparation:
        case Mode::CaptureFailure:
        case Mode::HistoryFailure:
        case Mode::PreparationFailure:
        case Mode::StaleGeneration:
        case Mode::StaleHistoryEpoch:
        case Mode::StaleReplacement:
        default:
            state.receipt = QueueHostInput(HostNewInput{});
            break;
        }

        const SettleResult settled = SettleJobs(_jobs, 15000, dispatch, &state);
        const ContinuationReceipt::Result actual = state.receipt.Read();
        const bool expectsAdmission = mode != Mode::AdmissionFailure && mode != Mode::CloseAbsentPreservesRedo;
        const bool expectsEvent = expectsAdmission && mode != Mode::CaptureFailure;
        const bool expectsFailure = mode == Mode::PreparationFailure || mode == Mode::CaptureFailure ||
                                    mode == Mode::AdmissionFailure || mode == Mode::HistoryFailure;
        const bool expectsCancelled = mode == Mode::CancelBeforePreparation || mode == Mode::CancelAfterPreparation;
        const bool expectsStale =
            mode == Mode::StaleGeneration || mode == Mode::StaleReplacement || mode == Mode::StaleHistoryEpoch;
        const HostOutcome expected = expectsFailure     ? HostOutcome::Failed
                                     : expectsCancelled ? HostOutcome::Cancelled
                                     : expectsStale     ? HostOutcome::Stale
                                                        : HostOutcome::Succeeded;

        expect(settled == SettleResult::Settled, "settled", mode);
        expect(actual.outcome == expected, "receipt-outcome", mode);
        expect(state.visited == expectsEvent && (!expectsEvent || state.held), "event-and-whole-reservation-held",
               mode);
        expect(!_historyWork.Busy() && !_documentLifecycleTicket && !_documentLifecycleStamp && !_hostCommitStamp &&
                   !_activeHostRequest && _hostRequests.Size() == 0 && _hostInputBudget.Used() == 0 &&
                   _jobs.ReservedMemoryBytes() == 0 && _jobs.ReleasingMemoryBytes() == 0,
               "complete-host-and-job-retirement", mode);

        const bool payloadExpected = expectsAdmission;
        expect(gate->retired.load() == (payloadExpected ? 1U : 0U) &&
                   (!payloadExpected || gate->retiredOffOwner.load()),
               "aggregate-retired-once-off-owner", mode);
        if (expectsAdmission)
            expect(gate->captureReservationHeld.load(), "capture-followed-complete-reservation", mode);
        if (mode == Mode::HistoryFailure)
            expect(gate->historyFaultHits.load() == 1, "history-failure-checkpoint-reached", mode);

        if (expectsFailure)
        {
            expect(actual.error == E_OUTOFMEMORY, "exact-allocation-error", mode);
            expect(_errorsShown == errorsBefore + 1 && _lastErrorId == IDS_ERROR_DOCUMENT_LIFECYCLE,
                   "exactly-one-lifecycle-notice", mode);
        }
        if (expectsStale)
            expect(actual.error == E_ABORT, "exact-stale-receipt-error", mode);

        if (mode == Mode::NewDefault || mode == Mode::NewThenUndo)
        {
            const auto& document = _session.Document();
            const bool dimensions = document && document->width == 1280 && document->height == 800;
            expect(dimensions && document->layers.size() == 1 && document->layers.front().name == L"Layer 1" &&
                       document->layers.front().asset == std::nullopt && _session.ActiveLayerID() &&
                       _session.ActiveLayerID().value() == document->layers.front().id &&
                       _session.history.UndoCount() == 1 && _session.history.UndoName() == L"New Canvas" &&
                       !_session.history.IsModified() && _documentPath.empty() && _viewport.followsFit,
                   "new-default-1280x800-saved-canvas", mode);
            expect(privateStatePreserved(), "new-preserves-palette-and-private-copy", mode);
            if (mode == Mode::NewThenUndo)
            {
                expect(state.youngerPending && state.youngerUndo &&
                           state.youngerUndo->Read().outcome == HostOutcome::Stale &&
                           state.youngerUndo->Read().error == 0 && dimensions && _session.history.UndoCount() == 1,
                       "younger-old-lifetime-undo-waits-and-refuses-after-replacement", mode);
                const auto undo = QueueHostInput(HostCommandInput{ChromeHost::kCommandUndo});
                if (!settle())
                    return 5;
                expect(undo.Read().outcome == HostOutcome::Succeeded && !_session.Document() &&
                           !_session.ActiveLayerID() && _session.history.CanRedo() &&
                           _session.history.RedoName() == L"New Canvas" && _session.history.IsModified(),
                       "fresh-new-lifetime-undo-restores-no-document", mode);
                const auto redo = QueueHostInput(HostCommandInput{ChromeHost::kCommandRedo});
                if (!settle())
                    return 5;
                expect(redo.Read().outcome == HostOutcome::Succeeded && _session.Document() &&
                           _session.Document()->width == 1280 && _session.Document()->height == 800 &&
                           !_session.history.IsModified(),
                       "fresh-redo-restores-literal-saved-new-canvas", mode);
            }
        }
        else if (mode == Mode::NewCanvasCopiedDraft)
        {
            const auto& document = _session.Document();
            expect(document && document->width == 5 && document->height == 7 && document->layers.size() == 1 &&
                       document->layers.front().name == L"Layer 1" && !_session.history.IsModified() &&
                       _session.history.UndoName() == L"New Canvas" && _documentPath.empty() && _viewport.followsFit,
                   "new-canvas-uses-copied-5x7-draft-after-live-edits", mode);
            expect(privateStatePreserved(), "new-canvas-preserves-palette-and-private-copy", mode);
        }
        else if (mode == Mode::CloseProject)
        {
            expect(!_session.Document() && !_session.ActiveLayerID() && !_session.Selection() &&
                       !_session.IsMaskSelected() && _session.CollapsedGroupIDs().empty() &&
                       _session.history.UndoCount() == 0 && !_session.history.CanUndo() &&
                       !_session.history.CanRedo() && !_session.history.IsModified() && _documentPath.empty() &&
                       _viewport.followsFit,
                   "close-clears-document-targets-history-path-and-fits", mode);
            expect(privateStatePreserved(), "close-preserves-palette-and-private-copy", mode);
        }
        else if (mode == Mode::CloseAbsentPreservesRedo)
        {
            expect(!_session.Document() && _session.history.CanRedo() && _session.history.RedoName() == L"New Canvas" &&
                       _documentPath == L"Z:\\lifecycle-host-test\\absent.prism" &&
                       _session.DocumentSerial() == documentSerial && _session.history.CurrentRevision() == revision &&
                       !_session.history.IsModified(),
                   "absent-close-is-no-op-preserving-redo-and-identity", mode);
        }
        else if (expectsCancelled)
        {
            expect(state.cancelPending && actual.error == E_ABORT && _session.Document() &&
                       _session.Document()->width == 8 && _session.Document()->height == 4 &&
                       _session.Document()->layers.size() == 2 && _session.history.CurrentRevision() == revision &&
                       _session.Generation() == generation && _session.DocumentSerial() == documentSerial &&
                       _documentPath == L"Z:\\lifecycle-host-test\\source.prism" && privateStatePreserved(),
                   "cancel-preserves-source-path-generation-history-and-private-state", mode);
        }
        else if (expectsFailure)
        {
            expect(_session.Document() && _session.Document()->width == 8 && _session.Document()->height == 4 &&
                       _session.Document()->layers.size() == 2 && _session.history.CurrentRevision() == revision &&
                       _session.Generation() == generation && _session.DocumentSerial() == documentSerial &&
                       _documentPath == L"Z:\\lifecycle-host-test\\source.prism" && privateStatePreserved(),
                   "failure-preserves-source-path-generation-history-and-private-state", mode);
        }
        else if (mode == Mode::StaleGeneration)
        {
            expect(_session.Document() && _session.Document()->width == 8 && _session.Document()->height == 4 &&
                       _session.Document()->layers.size() == 2 && _session.Generation() != generation &&
                       _session.DocumentSerial() == documentSerial &&
                       _session.Document()->layers.back().opacity == 0.5 &&
                       _documentPath == L"Z:\\lifecycle-host-test\\source.prism" && privateStatePreserved(),
                   "generation-stale-keeps-intervening-document-and-private-state", mode);
        }
        else if (mode == Mode::StaleReplacement)
        {
            expect(_session.Document() && _session.Document()->width == 8 && _session.Document()->height == 4 &&
                       _session.Document()->layers.size() == 2 && _session.Generation() != generation &&
                       _session.DocumentSerial() != documentSerial &&
                       _documentPath == L"Z:\\lifecycle-host-test\\source.prism" && privateStatePreserved(),
                   "replacement-stale-keeps-reopened-source-and-private-state", mode);
        }
        else if (mode == Mode::StaleHistoryEpoch)
        {
            expect(_session.Document() && _session.Document()->width == 8 && _session.Document()->height == 4 &&
                       _session.Document()->layers.size() == 2 && _session.Generation() == generation &&
                       _session.DocumentSerial() == documentSerial && _session.history.CurrentRevision() == revision &&
                       _documentPath == L"Z:\\lifecycle-host-test\\source.prism" && privateStatePreserved(),
                   "history-epoch-stale-keeps-intervening-history-and-source", mode);
        }

        _documentLifecycleTestGate.reset();
        CancelSizeCard();
    }

    std::printf("lifecycle-host summary: %u checks, %d failures\n", checks, failures);
    std::fflush(stdout);
    return failures == 0 ? 0 : 5;
}
