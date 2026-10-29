#include "Application.h"

#include "Tests/Support/Support.Tests.DocumentFixture.h"

#include "ChromeHost.h"
#include "Runtime.Tests.TransformWorkTestGate.h"
#include "Selection.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>

int Application::RunTransformWorkSelfTest() noexcept
{
    enum class Case
    {
        Success,
        CancelBeforeReady,
        CancelAfterReady,
        WorkerFailure,
        Allocation,
        Admission,
        Capture,
        HistoryBefore,
        HistoryAfter,
        HistoryEntry,
        HistoryStack,
        HistoryTrim,
        StaleGeometry,
        StaleSource,
        StaleHistory,
        SameIdReplacement,
        YoungerUndo
    };
// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4623 4626 5027)
    struct State final
    {
        Application& owner;
        Case test;
        std::shared_ptr<TransformWorkTestGate> gate;
        ContinuationReceipt receipt;
        std::optional<ContinuationReceipt> younger;
        Raster source;
        Guid layer;
        Guid historyRevision;
        std::uint64_t documentSerial = 0;
        CanvasDocument document;
        Raster clipboard;
        std::uint64_t clipboardSerial = 0;
        bool visited = false;
        bool held = false;
        bool retained = false;
        bool viewChanged = false;
    };
#pragma warning(pop)

    unsigned checks = 0;
    unsigned failures = 0;
    const auto expect =
        [&](bool condition, const char* fact, Case test, bool after, TransformWorkTestKind kind) noexcept
    {
        ++checks;
        failures += condition ? 0u : 1u;
        std::printf("%s transform-host.kind-%u.after-%d.case-%u.%s\n", condition ? "PASS" : "FAIL",
                    static_cast<unsigned>(kind), after ? 1 : 0, static_cast<unsigned>(test), fact);
        std::fflush(stdout);
    };
    const auto settle = [&]() noexcept
    { return SettleJobs(_jobs, 15000, DispatchMessageThunk, this) == SettleResult::Settled; };
    const auto cleanup = wil::scope_exit(
        [&]() noexcept
        {
            if (_transformWorkTestGate)
                (void)SetEvent(_transformWorkTestGate->release.get());
            if (!settle())
            {
                SelfTestFailure(L"Transform cleanup could not settle borrowed host-test state.\n");
                std::_Exit(5);
            }
            _transformWorkTestGate.reset();
            CancelHostContinuations();
            if (!settle())
                std::_Exit(5);
        });
    const auto terminal = [&]() noexcept
    {
        return !_transformWorkTicket && !_transformWorkStamp && !_historyWork.Busy() && !_hostCommitStamp &&
               !_activeHostRequest && !_hostRequests.Size() && !_hostInputBudget.Used() &&
               !_jobs.ReservedMemoryBytes() && !_jobs.ReleasingMemoryBytes();
    };
    const auto prepareLayer = [&]() noexcept -> std::optional<Guid>
    {
        _session.ClearDocument(); // Every scenario starts without the preceding scenario's retained draft.
        _session.CreateDocument(16, 8);
        const Guid id = InsertPaintedLayer(_session, L"Transform baseline", 8, 6, {0, 0}, 0, 0xFFFF0000);
        if (id == Guid{})
            return std::nullopt;
        _session.SelectLayer(id);
        _session.EditForTest([](CanvasDocument& document)
                             { document.selection = DocumentSelection{true, SelectionGeometry::Rect(2, 1, 3, 2)}; });
        _session.history.Reset();
        _session.history.MarkSaved();
        _viewport.SetZoom(1, _viewport.Center(), DocumentSize());
        DocumentChanged();
        return id;
    };
    const auto snapshot = [&](State& state) noexcept
    {
        state.document = _session.Document().value();
        state.clipboard = _session.Clipboard() ? _session.Clipboard()->image : Raster{};
        state.clipboardSerial = _session.ClipboardSerial();
    };
    const auto makeGate = [&](TransformWorkTestKind kind, Case test, bool after) noexcept
    {
        _transformWorkTestGate = std::make_shared<TransformWorkTestGate>();
        _transformWorkTestGate->kind = kind;
        _transformWorkTestGate->afterPreparation = after;
        _transformWorkTestGate->throwAllocationFailure = test == Case::Allocation;
        _transformWorkTestGate->throwAdmissionFailure = test == Case::Admission;
        _transformWorkTestGate->throwCaptureFailure = test == Case::Capture;
        if (test == Case::WorkerFailure)
            _transformWorkTestGate->result = {JobStatus::Failed, E_ACCESSDENIED};
        if (test >= Case::HistoryBefore && test <= Case::HistoryTrim)
        {
            constexpr std::array stages{
                DocumentHistory::PrepareStage::BeforeSnapshot, DocumentHistory::PrepareStage::AfterSnapshot,
                DocumentHistory::PrepareStage::Entry, DocumentHistory::PrepareStage::StackGrowth,
                DocumentHistory::PrepareStage::TrimIndex};
            _transformWorkTestGate->historyFailure =
                stages[static_cast<std::size_t>(test) - static_cast<std::size_t>(Case::HistoryBefore)];
        }
        return static_cast<bool>(_transformWorkTestGate->release);
    };
    const auto dispatch = [](void* value, void* native) noexcept
    {
        auto& state = *static_cast<State*>(value);
        auto& owner = state.owner;
        auto& message = *static_cast<MSG*>(native);
        if (message.message != TransformWorkTestGate::kMessage)
        {
            owner.DispatchQueuedMessage(message);
            return;
        }
        state.visited = true;
        const bool floatingOpen = state.gate->kind == TransformWorkTestKind::OpenFloating;
        state.held = owner._activeHostRequest && owner._transformWorkTicket && owner._transformWorkStamp &&
                     owner._historyWork.Busy() && owner._hostCommitStamp == owner._transformWorkStamp &&
                     owner._jobs.ReservedMemoryBytes() == state.gate->reservationBytes &&
                     state.gate->captureReservationHeld && state.receipt.Read().outcome == HostOutcome::Pending &&
                     owner._session.DocumentSerial() == state.documentSerial && owner._session.Document() &&
                     owner._session.Document().value() == state.document &&
                     owner._session.ClipboardSerial() == state.clipboardSerial &&
                     (owner._session.Clipboard() ? owner._session.Clipboard()->image : Raster{}) == state.clipboard &&
                     owner._session.history.CurrentRevision() == state.historyRevision &&
                     (!state.younger || state.younger->Read().outcome == HostOutcome::Pending) &&
                     (floatingOpen || (owner._session.ActiveLayer() && owner._session.ActiveLayer()->asset &&
                                       owner._session.ActiveLayer()->asset->image == state.source));
        const auto zoom = owner._viewport.zoom;
        owner.OnCommand(ChromeHost::kCommandZoomIn);
        state.viewChanged = owner._viewport.zoom != zoom;
        if (state.test == Case::CancelBeforeReady || state.test == Case::CancelAfterReady)
        {
            owner.CancelHostContinuations();
            state.retained = state.receipt.Read().outcome == HostOutcome::Pending && owner._transformWorkTicket &&
                             owner._historyWork.Busy() && owner._jobs.ReservedMemoryBytes() != 0;
        }
        else if (state.test == Case::StaleGeometry)
        {
            const auto& pending = owner._session.PendingTransformEdit();
            if (pending)
            {
                auto changed = pending->draft;
                changed.origin.x += 1.0;
                owner._session.PreviewTransform(changed);
            }
        }
        else if (state.test == Case::StaleSource)
            EditLayerRecordsForTest(owner._session,
                                    [](DocumentFixture& document) { document.layers.back().opacity = 0.5; });
        else if (state.test == Case::StaleHistory)
            owner._session.history.Reset();
        else if (state.test == Case::SameIdReplacement)
        {
            auto document = owner._session.Document().value();
            owner._session.ReplaceDocument(std::move(document), owner._session.ActiveLayerID());
        }
        else if (state.test == Case::YoungerUndo)
        {
            state.younger = owner.QueueHostInput(HostCommandInput{ChromeHost::kCommandUndo});
        }
        (void)SetEvent(state.gate->release.get());
    };

    for (const bool after : {false, true})
    {
        for (const Case test :
             {Case::Success, Case::CancelBeforeReady, Case::CancelAfterReady, Case::WorkerFailure, Case::Allocation,
              Case::Admission, Case::Capture, Case::HistoryBefore, Case::HistoryAfter, Case::HistoryEntry,
              Case::HistoryStack, Case::HistoryTrim, Case::StaleGeometry, Case::StaleSource, Case::StaleHistory,
              Case::SameIdReplacement, Case::YoungerUndo})
        {
            const bool historyFault = test >= Case::HistoryBefore && test <= Case::HistoryTrim;
            if ((test == Case::CancelBeforeReady && after) || (test == Case::CancelAfterReady && !after))
                continue;
            if (after && (test == Case::Allocation || historyFault))
                continue; // These paths fail before an after-preparation rendezvous can be reached.
            if (!settle())
                return 5;
            const auto target = prepareLayer();
            if (!target)
                return 5;
            _session.BeginTransform(true);
            auto draft = _session.ActiveLayer()->transform;
            draft.origin.x += 2.0;
            _session.PreviewTransform(draft);
            _viewport.SetZoom(1, _viewport.Center(), DocumentSize());
            const Raster source = _session.ActiveLayer()->asset->image;
            const auto revision = _session.history.CurrentRevision();
            const auto documentSerial = _session.DocumentSerial();
            const auto presents = _renderer.PresentCount();
            if (!makeGate(TransformWorkTestKind::Commit, test, after))
                return 5;
            State state{
                *this,
                test,
                _transformWorkTestGate,
                QueueHostInput(HostTransformFinishInput{target.value(), _session.TransformDraftRevision(), false}),
                {},
                source,
                target.value(),
                revision,
                documentSerial};
            snapshot(state);
            if (SettleJobs(_jobs, 15000, dispatch, &state) != SettleResult::Settled)
                std::_Exit(5);
            const auto result = state.receipt.Read();
            const bool noGate = test == Case::Admission || test == Case::Capture;
            const bool cancelled = test == Case::CancelBeforeReady || test == Case::CancelAfterReady;
            const bool stale = test == Case::StaleGeometry || test == Case::StaleSource || test == Case::StaleHistory ||
                               test == Case::SameIdReplacement;
            const bool failed = test == Case::WorkerFailure || test == Case::Allocation || historyFault || noGate;
            expect(result.outcome == (cancelled ? HostOutcome::Cancelled
                                      : stale   ? HostOutcome::Stale
                                      : failed  ? HostOutcome::Failed
                                                : HostOutcome::Succeeded),
                   "receipt-outcome", test, after, TransformWorkTestKind::Commit);
            expect(noGate ? !state.visited : state.visited && state.held && state.viewChanged, "gated-view-responsive",
                   test, after, TransformWorkTestKind::Commit);
            expect(terminal() && _renderer.PresentCount() == presents, "ticket-history-reservation-retired-no-present",
                   test, after, TransformWorkTestKind::Commit);
            expect(_session.ClipboardSerial() == state.clipboardSerial &&
                       (_session.Clipboard() ? _session.Clipboard()->image : Raster{}) == state.clipboard,
                   "private-clipboard-identity-and-serial-preserved", test, after, TransformWorkTestKind::Commit);
            if (cancelled)
                expect(state.retained && result.error == E_ABORT, "cancel-waits-for-worker-ack", test, after,
                       TransformWorkTestKind::Commit);
            if (failed)
                expect(result.error == E_OUTOFMEMORY || result.error == E_ACCESSDENIED, "failure-error", test, after,
                       TransformWorkTestKind::Commit);
            if (historyFault)
                expect(_transformWorkTestGate->historyFaultHits.load() == 1, "history-stage-reached", test, after,
                       TransformWorkTestKind::Commit);
            if (!noGate)
                expect(_transformWorkTestGate->retired && _transformWorkTestGate->retiredOffOwner,
                       "payload-retires-off-owner", test, after, TransformWorkTestKind::Commit);
            const auto* active = _session.ActiveLayer();
            if (test == Case::Success)
            {
                expect(active && active->asset && active->asset->image == source && active->transform == draft &&
                           _session.history.UndoCount() == 1 && !_session.PendingTransformEdit(),
                       "commit-one-entry-preserves-raster", test, after, TransformWorkTestKind::Commit);
                _session.Undo();
                expect(_session.ActiveLayer() && _session.ActiveLayer()->transform != draft &&
                           _session.history.UndoCount() == 0,
                       "one-undo-restores-transform", test, after, TransformWorkTestKind::Commit);
            }
            else if (test == Case::YoungerUndo)
                expect(state.younger && state.younger->Read().outcome == HostOutcome::Succeeded && active &&
                           active->asset && active->asset->image == source && _session.history.UndoCount() == 0,
                       "younger-undo-runs-after-transform-publication", test, after, TransformWorkTestKind::Commit);
            else
                expect(active && active->asset && active->asset->image == source && _session.history.UndoCount() == 0,
                       "failure-or-cancel-preserves-owned-source", test, after, TransformWorkTestKind::Commit);
            _transformWorkTestGate.reset();
        }
    }

    for (const bool linked : {false, true})
    {
        if (!settle())
            return 5;
        _session.ClearDocument();
        _session.CreateDocument(16, 8);
        const Guid target = InsertPaintedLayer(_session, L"Mask transform", 8, 6, {0, 0}, 0, 0xFF204060);
        const Raster maskImage = Raster::Make(8, 6, RasterFormat::Gray8);
        if (target == Guid{} || !maskImage.HasStorage())
            return 5;
        LayerMask mask;
        mask.asset = RasterAsset{maskImage, Raster::MakeThumbnail(maskImage), L"Transform test mask"};
        mask.linked = linked;
        mask.placement = LayerTransform{Vec2{1, 1}, Vec2{8, 6}, 0, false, false, LayerSampling::Nearest};
        EditLayerRecordsForTest(_session, [&](DocumentFixture& document) { document.layers.back().mask = mask; });
        _session.SelectLayer(target);
        _session.history.Reset();
        const auto before = _session.ActiveLayer()->transform;
        const auto expectedPlacement = MaskPlacementMoving(mask, before,
                                                           [&]()
                                                           {
                                                               auto moved = before;
                                                               moved.origin.x += 2.0;
                                                               return moved;
                                                           }());
        auto moved = before;
        moved.origin.x += 2.0;
        _session.BeginTransform(true);
        _session.PreviewTransform(moved);
        if (!makeGate(TransformWorkTestKind::Commit, Case::Success, true))
            return 5;
        const auto source = _session.ActiveLayer()->asset->image;
        State maskCommit{*this,
                         Case::Success,
                         _transformWorkTestGate,
                         QueueHostInput(HostTransformFinishInput{target, _session.TransformDraftRevision(), false}),
                         {},
                         source,
                         target,
                         _session.history.CurrentRevision(),
                         _session.DocumentSerial()};
        snapshot(maskCommit);
        const auto settled = SettleJobs(_jobs, 15000, dispatch, &maskCommit);
        const auto* resultLayer = _session.ActiveLayer();
        expect(settled == SettleResult::Settled && maskCommit.visited && maskCommit.held &&
                   maskCommit.receipt.Read().outcome == HostOutcome::Succeeded && resultLayer && resultLayer->mask &&
                   resultLayer->mask->linked == linked && resultLayer->mask->asset.image == maskImage &&
                   resultLayer->mask->placement == expectedPlacement && resultLayer->asset->image == source &&
                   _session.history.UndoCount() == 1,
               linked ? "linked-mask-placement-and-fixture-owner" : "unlinked-mask-placement-and-fixture-owner",
               Case::Success, true, TransformWorkTestKind::Commit);
        _transformWorkTestGate.reset();
    }

    // Open a floating selection through the real host command, then exercise its prepared merge/rollback owner.
    if (!settle())
        return 5;
    auto floatingLayer = prepareLayer();
    if (!floatingLayer)
        return 5;
    const Raster floatingSource = _session.ActiveLayer()->asset->image;
    const auto floatingRevision = _session.history.CurrentRevision();
    if (!makeGate(TransformWorkTestKind::OpenFloating, Case::Success, false))
        return 5;
    State opening{*this,
                  Case::Success,
                  _transformWorkTestGate,
                  QueueHostInput(HostCommandInput{ChromeHost::kCommandFreeTransform}),
                  {},
                  floatingSource,
                  floatingLayer.value(),
                  floatingRevision,
                  _session.DocumentSerial()};
    snapshot(opening);
    if (SettleJobs(_jobs, 15000, dispatch, &opening) != SettleResult::Settled || !opening.visited || !opening.held ||
        opening.receipt.Read().outcome != HostOutcome::Succeeded || !_session.PendingTransformEdit() ||
        !_session.PendingTransformEdit()->floating || !_transformRestoreOwner)
        return 5;
    expect(_session.history.UndoCount() == 0 && _session.PendingTransformEdit() &&
               _session.PendingTransformEdit()->floating && _session.Document()->layers.size() == 2,
           "floating-open-keeps-opening-history-and-source", Case::Success, false, TransformWorkTestKind::OpenFloating);
    _transformWorkTestGate.reset();
    auto floatingDraft = _session.PendingTransformEdit()->draft;
    floatingDraft.origin.x += 1.0;
    _session.PreviewTransform(floatingDraft);
    if (!makeGate(TransformWorkTestKind::Commit, Case::Success, true))
        return 5;
    const auto finish = QueueHostInput(
        HostTransformFinishInput{_session.PendingTransformEdit()->layerID, _session.TransformDraftRevision(), false});
    State merge{*this,
                Case::Success,
                _transformWorkTestGate,
                finish,
                {},
                _session.ActiveLayer()->asset->image,
                floatingLayer.value(),
                _session.history.CurrentRevision(),
                _session.DocumentSerial()};
    snapshot(merge);
    if (SettleJobs(_jobs, 15000, dispatch, &merge) != SettleResult::Settled || !merge.visited || !merge.held ||
        finish.Read().outcome != HostOutcome::Succeeded || _session.history.UndoCount() != 1 ||
        _transformRestoreOwner || !_transformWorkTestGate->retired || !_transformWorkTestGate->retiredOffOwner)
        return 5;
    expect(!_session.PendingTransformEdit() && _session.Document()->layers.size() == 1,
           "floating-merge-publishes-once-and-retires-restore", Case::Success, true, TransformWorkTestKind::Commit);
    _session.Undo();
    expect(_session.ActiveLayer()->asset && _session.ActiveLayer()->asset->image == floatingSource &&
               _session.history.UndoCount() == 0,
           "floating-merge-one-undo-restores-source", Case::Success, true, TransformWorkTestKind::Commit);
    _transformWorkTestGate.reset();

    // The already prepared floating rollback is a synchronous idle cancellation: no new job or reservation.
    if (!settle())
        return 5;
    floatingLayer = prepareLayer();
    if (!floatingLayer)
        return 5;
    const Raster originalRollbackSource = _session.ActiveLayer()->asset->image;
    if (!makeGate(TransformWorkTestKind::OpenFloating, Case::Success, true))
        return 5;
    State secondOpening{*this,
                        Case::Success,
                        _transformWorkTestGate,
                        QueueHostInput(HostCommandInput{ChromeHost::kCommandFreeTransform}),
                        {},
                        _session.ActiveLayer()->asset->image,
                        floatingLayer.value(),
                        _session.history.CurrentRevision(),
                        _session.DocumentSerial()};
    snapshot(secondOpening);
    if (SettleJobs(_jobs, 15000, dispatch, &secondOpening) != SettleResult::Settled ||
        secondOpening.receipt.Read().outcome != HostOutcome::Succeeded || !_session.PendingTransformEdit() ||
        !_session.PendingTransformEdit()->floating || !_transformRestoreOwner)
        return 5;
    _transformWorkTestGate.reset();
    auto preview = _session.PendingTransformEdit()->draft;
    preview.origin.x += 3.0;
    _session.PreviewTransform(preview);

    // The prepared opening is retained through cancellation and used to roll back after worker acknowledgement.
    if (!makeGate(TransformWorkTestKind::Commit, Case::CancelAfterReady, true))
        return 5;
    const auto failedFloatingCommit = QueueHostInput(
        HostTransformFinishInput{_session.PendingTransformEdit()->layerID, _session.TransformDraftRevision(), false});
    State rollback{*this,
                   Case::CancelAfterReady,
                   _transformWorkTestGate,
                   failedFloatingCommit,
                   {},
                   _session.ActiveLayer()->asset->image,
                   floatingLayer.value(),
                   _session.history.CurrentRevision(),
                   _session.DocumentSerial()};
    snapshot(rollback);
    if (SettleJobs(_jobs, 15000, dispatch, &rollback) != SettleResult::Settled || !rollback.visited || !rollback.held ||
        !rollback.retained || failedFloatingCommit.Read().outcome != HostOutcome::Cancelled ||
        _session.PendingTransformEdit() || _session.ActiveLayer()->asset->image != originalRollbackSource ||
        _session.history.UndoCount() != 0 || _transformRestoreOwner || !_transformWorkTestGate->retired ||
        !_transformWorkTestGate->retiredOffOwner)
        return 5;
    expect(terminal(), "floating-cancel-restores-prepaid-opening-after-ack", Case::CancelAfterReady, true,
           TransformWorkTestKind::Commit);
    _transformWorkTestGate.reset();

    if (!settle() || !makeGate(TransformWorkTestKind::OpenFloating, Case::Success, true))
        return 5;
    State thirdOpening{*this,
                       Case::Success,
                       _transformWorkTestGate,
                       QueueHostInput(HostCommandInput{ChromeHost::kCommandFreeTransform}),
                       {},
                       _session.ActiveLayer()->asset->image,
                       floatingLayer.value(),
                       _session.history.CurrentRevision(),
                       _session.DocumentSerial()};
    snapshot(thirdOpening);
    if (SettleJobs(_jobs, 15000, dispatch, &thirdOpening) != SettleResult::Settled ||
        thirdOpening.receipt.Read().outcome != HostOutcome::Succeeded || !_session.PendingTransformEdit() ||
        !_session.PendingTransformEdit()->floating || !_transformRestoreOwner)
        return 5;
    _transformWorkTestGate.reset();
    auto idlePreview = _session.PendingTransformEdit()->draft;
    idlePreview.origin.x += 3.0;
    _session.PreviewTransform(idlePreview);
    const auto reservationBeforeIdleCancel = _jobs.ReservedMemoryBytes();
    const auto idleCancel = QueueHostInput(
        HostTransformFinishInput{_session.PendingTransformEdit()->layerID, _session.TransformDraftRevision(), true});
    if (!settle() || idleCancel.Read().outcome != HostOutcome::Succeeded || _session.PendingTransformEdit() ||
        _session.ActiveLayer()->asset->image != originalRollbackSource || _session.history.UndoCount() != 0 ||
        _jobs.ReservedMemoryBytes() != reservationBeforeIdleCancel || _transformRestoreOwner)
        return 5;
    expect(terminal(), "idle-floating-cancel-restores-without-new-work", Case::Success, true,
           TransformWorkTestKind::Cancel);

    // Every exposed floating opening owns a prepaid restore. Test the real failure boundaries, including
    // failure to admit/capture a merge, where the worker has not constructed a rollback result.
    for (const TransformWorkTestKind kind : {TransformWorkTestKind::OpenFloating, TransformWorkTestKind::Commit})
    {
        for (const Case test : {Case::CancelBeforeReady, Case::CancelAfterReady, Case::WorkerFailure, Case::Allocation,
                                Case::Admission, Case::Capture, Case::HistoryBefore, Case::HistoryAfter,
                                Case::HistoryEntry, Case::HistoryStack, Case::HistoryTrim})
        {
            const bool openingFault = kind == TransformWorkTestKind::OpenFloating;
            const bool historyFault = test >= Case::HistoryBefore && test <= Case::HistoryTrim;
            if ((openingFault && test >= Case::HistoryEntry && test <= Case::HistoryTrim) ||
                (!openingFault && test == Case::HistoryBefore))
                continue; // Opening reaches PendingSnapshot/Record; merge End starts at AfterSnapshot.
            if (!settle())
                return 5;
            _transformWorkTestGate.reset();
            const auto target = prepareLayer();
            if (!target)
                return 5;
            const auto before = _session.Document().value();
            const Raster original = _session.ActiveLayer()->asset->image;
            std::shared_ptr<TransformWorkTestGate> restorationGate;
            if (!openingFault)
            {
                if (!makeGate(TransformWorkTestKind::OpenFloating, Case::Success, true))
                    return 5;
                State setup{*this,
                            Case::Success,
                            _transformWorkTestGate,
                            QueueHostInput(HostCommandInput{ChromeHost::kCommandFreeTransform}),
                            {},
                            original,
                            target.value(),
                            _session.history.CurrentRevision(),
                            _session.DocumentSerial()};
                snapshot(setup);
                if (SettleJobs(_jobs, 15000, dispatch, &setup) != SettleResult::Settled)
                    std::_Exit(5);
                expect(setup.visited && setup.held && setup.receipt.Read().outcome == HostOutcome::Succeeded &&
                           _session.PendingTransformEdit() && _session.PendingTransformEdit()->floating &&
                           _transformRestoreOwner,
                       "fault-fixture-opened-through-worker", test, true, kind);
                if (!_session.PendingTransformEdit() || !_session.PendingTransformEdit()->floating ||
                    !_transformRestoreOwner)
                    return 5;
                restorationGate = _transformWorkTestGate;
                expect(restorationGate->retired && restorationGate->retiredOffOwner && !restorationGate->restoreRetired,
                       "restore-retained-after-opening-payload-retirement", test, true, kind);
                auto changed = _session.PendingTransformEdit()->draft;
                changed.origin.x += 3.0;
                _session.PreviewTransform(changed);
            }
            const bool after = test == Case::CancelAfterReady;
            if (!makeGate(kind, test, after))
                return 5;
            if (openingFault && historyFault)
                _transformWorkTestGate->historyFailure = test == Case::HistoryBefore
                                                             ? DocumentHistory::PrepareStage::PendingSnapshot
                                                             : DocumentHistory::PrepareStage::PendingRecord;
            const auto request =
                openingFault ? HostRequestInput{HostCommandInput{ChromeHost::kCommandFreeTransform}}
                             : HostRequestInput{HostTransformFinishInput{_session.PendingTransformEdit()->layerID,
                                                                         _session.TransformDraftRevision(), false}};
            State fault{*this,
                        test,
                        _transformWorkTestGate,
                        QueueHostInput(request),
                        {},
                        _session.ActiveLayer()->asset->image,
                        target.value(),
                        _session.history.CurrentRevision(),
                        _session.DocumentSerial()};
            snapshot(fault);
            const auto errorCount = _errorsShown;
            if (SettleJobs(_jobs, 15000, dispatch, &fault) != SettleResult::Settled)
                std::_Exit(5);
            const bool cancelled = test == Case::CancelBeforeReady || test == Case::CancelAfterReady;
            const bool noGate = test == Case::Admission || test == Case::Capture;
            const auto outcome = fault.receipt.Read();
            expect(outcome.outcome == (cancelled ? HostOutcome::Cancelled : HostOutcome::Failed) &&
                       outcome.error == static_cast<std::int32_t>(cancelled                     ? E_ABORT
                                                                  : test == Case::WorkerFailure ? E_ACCESSDENIED
                                                                                                : E_OUTOFMEMORY),
                   "floating-fault-exact-outcome", test, after, kind);
            expect(noGate ? !fault.visited : fault.visited && fault.held && fault.viewChanged,
                   "floating-fault-held-reservation-and-view-input", test, after, kind);
            expect(_session.Document() && _session.Document().value() == before && _session.ActiveLayerID() == target &&
                       _session.ActiveLayer()->asset->image == original && !_session.PendingTransformEdit() &&
                       !_transformRestoreOwner && _session.history.UndoCount() == 0 &&
                       _session.history.CaptureState().Depth() == 0,
                   "floating-fault-restores-exact-document-selection-source-and-depth", test, after, kind);
            expect(_session.ClipboardSerial() == fault.clipboardSerial &&
                       (_session.Clipboard() ? _session.Clipboard()->image : Raster{}) == fault.clipboard && terminal(),
                   "floating-fault-clipboard-and-complete-retirement", test, after, kind);
            expect(_errorsShown == errorCount + (cancelled ? 0 : 1), "floating-fault-one-notice-or-silent-cancel", test,
                   after, kind);
            if (cancelled)
                expect(fault.retained, "floating-cancel-retains-ordinal-until-ack", test, after, kind);
            if (historyFault)
                expect(fault.gate->historyFaultHits == 1, "floating-history-fault-reached", test, after, kind);
            if (!noGate)
                expect(fault.gate->retired && fault.gate->retiredOffOwner, "floating-fault-payload-retires-off-owner",
                       test, after, kind);
            if (restorationGate)
                expect(restorationGate->restoreRetired && restorationGate->restoreRetiredOffOwner,
                       "floating-prepaid-restore-retires-off-owner", test, after, kind);
            _transformWorkTestGate.reset();
        }
    }

    // Layer nudges capture Shift when the command is admitted. Keep the fixture selection-free so the
    // ordinary transform path cannot open a floating selection.
    const auto prepareNudgeLayer = [&]() noexcept -> std::optional<Guid>
    {
        const auto id = prepareLayer();
        if (!id)
            return std::nullopt;
        _session.EditForTest([](CanvasDocument& document) { document.selection.reset(); });
        _session.history.Reset();
        _session.history.MarkSaved();
        DocumentChanged();
        return id;
    };
    const bool savedAcceleratorShift = _acceleratorShift;
    const auto restoreAcceleratorShift = wil::scope_exit([&]() noexcept { _acceleratorShift = savedAcceleratorShift; });

    // Two queued commands straddle the modifier transition while the first commit is held by the worker gate.
    {
        if (!settle())
            return 5;
        const auto target = prepareNudgeLayer();
        if (!target || !makeGate(TransformWorkTestKind::Commit, Case::Success, true))
            return 5;
        const Raster source = _session.ActiveLayer()->asset->image;
        const Vec2 origin = _session.ActiveLayer()->transform.origin;
        expect(origin.x == 0.0 && origin.y == 0.0, "fresh-nudge-fixture-literal-origin-zero", Case::Success, true,
               TransformWorkTestKind::Commit);
        State state{*this, Case::Success, _transformWorkTestGate};
        state.source = source;
        state.layer = target.value();
        state.historyRevision = _session.history.CurrentRevision();
        state.documentSerial = _session.DocumentSerial();
        snapshot(state);
        const auto nudgeDispatch = [](void* value, void* native) noexcept
        {
            auto& state = *static_cast<State*>(value);
            auto& owner = state.owner;
            auto& message = *static_cast<MSG*>(native);
            if (message.message != TransformWorkTestGate::kMessage)
            {
                owner.DispatchQueuedMessage(message);
                return;
            }
            if (!state.visited)
            {
                state.visited = true;
                state.held = owner._activeHostRequest && owner._transformWorkTicket && owner._transformWorkStamp &&
                             owner._historyWork.Busy() && owner._hostCommitStamp == owner._transformWorkStamp &&
                             owner._jobs.ReservedMemoryBytes() == state.gate->reservationBytes &&
                             state.gate->captureReservationHeld && owner._hostRequests.Size() != 0 &&
                             owner._session.DocumentSerial() == state.documentSerial && owner._session.Document() &&
                             owner._session.Document().value() == state.document &&
                             owner._session.history.CurrentRevision() == state.historyRevision &&
                             owner._session.ActiveLayer() && owner._session.ActiveLayer()->asset &&
                             owner._session.ActiveLayer()->asset->image == state.source;
            }
            (void)SetEvent(state.gate->release.get());
        };

        _acceleratorShift = true;
        OnCommand(ChromeHost::kCommandNudgeLayerRight);
        _acceleratorShift = false;
        OnCommand(ChromeHost::kCommandNudgeLayerDown);
        _acceleratorShift = false; // The worker must use the offsets already stored in both requests.
        if (SettleJobs(_jobs, 15000, nudgeDispatch, &state) != SettleResult::Settled)
            std::_Exit(5);

        const ImageLayer* layer = _session.ActiveLayer();
        expect(state.visited && state.held, "shift-capture-first-commit-gated-with-younger-nudge", Case::Success, true,
               TransformWorkTestKind::Commit);
        expect(layer && layer->transform.origin.x == 10.0 && layer->transform.origin.y == 1.0,
               "shift-capture-literal-ten-right-one-down", Case::Success, true, TransformWorkTestKind::Commit);
        expect(_session.history.UndoCount() == 2, "fresh-nudges-close-two-independent-history-entries", Case::Success,
               true, TransformWorkTestKind::Commit);
        expect(layer && layer->asset && layer->asset->image == source, "fresh-nudges-preserve-raster-identity",
               Case::Success, true, TransformWorkTestKind::Commit);
        expect(terminal() && !_session.PendingTransformEdit(), "fresh-nudges-reach-complete-terminal-state",
               Case::Success, true, TransformWorkTestKind::Commit);
        _transformWorkTestGate.reset();
    }

    // A persistent ordinary draft accepts nudges as metadata only; its ordered finish publishes one entry.
    {
        if (!settle())
            return 5;
        const auto target = prepareNudgeLayer();
        if (!target)
            return 5;
        const Raster source = _session.ActiveLayer()->asset->image;
        const Vec2 origin = _session.ActiveLayer()->transform.origin;
        expect(origin.x == 0.0 && origin.y == 0.0, "persistent-nudge-fixture-literal-origin-zero", Case::Success, true,
               TransformWorkTestKind::Commit);
        _session.BeginTransform(true);
        _acceleratorShift = false;
        OnCommand(ChromeHost::kCommandNudgeLayerRight);
        OnCommand(ChromeHost::kCommandNudgeLayerDown);
        _acceleratorShift = false;
        if (!settle())
            return 5;
        const auto& pending = _session.PendingTransformEdit();
        const ImageLayer* layer = _session.ActiveLayer();
        expect(pending && pending->draft.origin.x == 1.0 && pending->draft.origin.y == 1.0,
               "persistent-draft-nudges-use-literal-one-pixel-offsets", Case::Success, true,
               TransformWorkTestKind::Commit);
        expect(layer && layer->transform.origin == origin && _session.history.UndoCount() == 0,
               "persistent-draft-nudges-do-not-publish-document-or-history", Case::Success, true,
               TransformWorkTestKind::Commit);
        expect(layer && layer->asset && layer->asset->image == source, "persistent-draft-preserves-raster-identity",
               Case::Success, true, TransformWorkTestKind::Commit);
        if (!pending || !makeGate(TransformWorkTestKind::Commit, Case::Success, true))
            return 5;
        State state{*this,
                    Case::Success,
                    _transformWorkTestGate,
                    QueueHostInput(HostTransformFinishInput{target.value(), _session.TransformDraftRevision(), false}),
                    {},
                    source,
                    target.value(),
                    _session.history.CurrentRevision(),
                    _session.DocumentSerial()};
        snapshot(state);
        if (SettleJobs(_jobs, 15000, dispatch, &state) != SettleResult::Settled)
            std::_Exit(5);
        layer = _session.ActiveLayer();
        expect(state.visited && state.held && state.receipt.Read().outcome == HostOutcome::Succeeded,
               "persistent-draft-ordered-finish-succeeds", Case::Success, true, TransformWorkTestKind::Commit);
        expect(layer && layer->transform.origin.x == 1.0 && layer->transform.origin.y == 1.0 &&
                   _session.history.UndoCount() == 1,
               "persistent-draft-finish-publishes-exactly-one-entry", Case::Success, true,
               TransformWorkTestKind::Commit);
        expect(layer && layer->asset && layer->asset->image == source && !_session.PendingTransformEdit() && terminal(),
               "persistent-draft-finish-preserves-raster-and-retires-terminal-state", Case::Success, true,
               TransformWorkTestKind::Commit);
        _transformWorkTestGate.reset();
    }

    // The nudge and younger Undo enter while the original transform commit is stopped at its worker rendezvous.
    {
        if (!settle())
            return 5;
        const auto target = prepareNudgeLayer();
        if (!target)
            return 5;
        const Raster source = _session.ActiveLayer()->asset->image;
        const Vec2 origin = _session.ActiveLayer()->transform.origin;
        expect(origin.x == 0.0 && origin.y == 0.0, "ordered-nudge-fixture-literal-origin-zero", Case::Success, true,
               TransformWorkTestKind::Commit);
        _session.BeginTransform(true);
        auto draft = _session.ActiveLayer()->transform;
        draft.origin.x += 2.0;
        _session.PreviewTransform(draft);
        if (!makeGate(TransformWorkTestKind::Commit, Case::Success, true))
            return 5;
        State state{*this,
                    Case::Success,
                    _transformWorkTestGate,
                    QueueHostInput(HostTransformFinishInput{target.value(), _session.TransformDraftRevision(), false}),
                    {},
                    source,
                    target.value(),
                    _session.history.CurrentRevision(),
                    _session.DocumentSerial()};
        snapshot(state);
        const auto nudgeUndoDispatch = [](void* value, void* native) noexcept
        {
            auto& state = *static_cast<State*>(value);
            auto& owner = state.owner;
            auto& message = *static_cast<MSG*>(native);
            if (message.message != TransformWorkTestGate::kMessage)
            {
                owner.DispatchQueuedMessage(message);
                return;
            }
            if (!state.visited)
            {
                state.visited = true;
                state.held =
                    owner._activeHostRequest && owner._transformWorkTicket && owner._transformWorkStamp &&
                    owner._historyWork.Busy() && owner._hostCommitStamp == owner._transformWorkStamp &&
                    owner._jobs.ReservedMemoryBytes() == state.gate->reservationBytes &&
                    state.gate->captureReservationHeld && state.receipt.Read().outcome == HostOutcome::Pending &&
                    owner._session.DocumentSerial() == state.documentSerial && owner._session.Document() &&
                    owner._session.Document().value() == state.document &&
                    owner._session.history.CurrentRevision() == state.historyRevision && owner._session.ActiveLayer() &&
                    owner._session.ActiveLayer()->asset && owner._session.ActiveLayer()->asset->image == state.source;
                owner._acceleratorShift = true;
                owner.OnCommand(ChromeHost::kCommandNudgeLayerRight);
                owner._acceleratorShift = false;
                state.younger = owner.QueueHostInput(HostCommandInput{ChromeHost::kCommandUndo});
            }
            (void)SetEvent(state.gate->release.get());
        };
        _acceleratorShift = false;
        if (SettleJobs(_jobs, 15000, nudgeUndoDispatch, &state) != SettleResult::Settled)
            std::_Exit(5);

        const ImageLayer* layer = _session.ActiveLayer();
        expect(state.visited && state.held, "nudge-and-undo-queued-behind-held-transform-commit", Case::Success, true,
               TransformWorkTestKind::Commit);
        expect(state.receipt.Read().outcome == HostOutcome::Succeeded && state.younger &&
                   state.younger->Read().outcome == HostOutcome::Succeeded,
               "nudge-publication-precedes-younger-undo", Case::Success, true, TransformWorkTestKind::Commit);
        expect(layer && layer->transform.origin.x == 2.0 && layer->transform.origin.y == 0.0 &&
                   _session.history.UndoCount() == 1,
               "younger-undo-restores-earlier-committed-origin-after-one-nudge-entry", Case::Success, true,
               TransformWorkTestKind::Commit);
        expect(layer && layer->asset && layer->asset->image == source && !_session.PendingTransformEdit() && terminal(),
               "ordered-nudge-undo-preserves-raster-and-complete-terminal-state", Case::Success, true,
               TransformWorkTestKind::Commit);
        _transformWorkTestGate.reset();
    }
    _acceleratorShift = savedAcceleratorShift;
    (void)restoreAcceleratorShift;

    // Ctrl+T already inside an ordinary persistent opening keeps that opening and its unapplied settings.
    if (!settle())
        return 5;
    const auto repeatedTarget = prepareNudgeLayer();
    if (!repeatedTarget)
        return 5;
    OnTool(EditorTool::Move);
    _session.BeginTransform(true);
    auto repeatedDraft = _session.PendingTransformEdit()->draft;
    repeatedDraft.origin.x = 2;
    _session.PreviewTransform(repeatedDraft);
    const auto repeatedRevision = _session.TransformDraftRevision();
    const auto repeatedDocument = _session.Document().value();
    const auto repeated = QueueHostInput(HostCommandInput{ChromeHost::kCommandFreeTransform});
    if (!settle())
        return 5;
    expect(repeated.Read().outcome == HostOutcome::Succeeded && _session.PendingTransformEdit() &&
               _session.PendingTransformEdit()->draft == repeatedDraft &&
               _session.TransformDraftRevision() == repeatedRevision,
           "repeated-ctrl-t-retains-opening-and-draft-revision", Case::Success, true, TransformWorkTestKind::Commit);
    expect(_session.Document().value() == repeatedDocument && _session.history.UndoCount() == 0 && terminal(),
           "repeated-ctrl-t-does-not-publish-or-record-an-entry", Case::Success, true, TransformWorkTestKind::Commit);
    const auto repeatedCancel =
        QueueHostInput(HostTransformFinishInput{repeatedTarget.value(), repeatedRevision, true});
    if (!settle() || repeatedCancel.Read().outcome != HostOutcome::Succeeded)
        return 5;

    std::printf("transform-host summary: %u checks, %u failures\n", checks, failures);
    std::fflush(stdout);
    return failures ? 5 : 0;
}
