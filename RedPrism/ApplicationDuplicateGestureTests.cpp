#include "Application.h"

#include "../Tests/Support/DocumentFixture.h"
#include "ChromeHost.h"
#include "TransformWorkTestGate.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <utility>

int Application::RunDuplicateGestureSelfTest() noexcept
{
    enum class Case : unsigned char
    {
        Success,
        ReleaseWhileOpening,
        ReleaseAfterPreparation,
        CoalescedModifiers,
        CoalescedAfterPreparation,
        EscapeBeforePreparation,
        EscapeAfterPreparation,
        CaptureLostBeforePreparation,
        CaptureLostAfterPreparation,
        FullReleaseQueue,
        YoungerUndo,
        AdmissionFailure,
        CaptureFailure,
        WorkerFailure,
        AllocationFailure,
        PendingSnapshotFailure,
        PendingRecordFailure,
        CommitFailure
    };
    struct State final
    {
        Application& owner;
        Case test;
        std::shared_ptr<TransformWorkTestGate> gate;
        CanvasDocument document;
        DocumentHistory::StateView history;
        Raster source;
        Raster clipboard;
        Vec2 clipboardOrigin{};
        bool clipboardPresent = false;
        std::uint64_t clipboardSerial = 0;
        std::uint64_t documentSerial = 0;
        std::uint64_t presents = 0;
        int errorsShown = 0;
        bool visited = false;
        bool held = false;
        bool responsive = false;
        bool latestCaptured = false;
        bool retained = false;
        std::optional<ContinuationReceipt> younger;
        bool modified = false;

        [[nodiscard]] bool VisibleHistoryMatches(const DocumentHistory& actual) const noexcept
        {
            const auto current = actual.CaptureState();
            return current.Revision() == history.Revision() && current.Depth() == history.Depth() &&
                   current.PastCount() == history.PastCount() && current.FutureCount() == history.FutureCount() &&
                   current.UndoName() == history.UndoName() && current.RedoName() == history.RedoName() &&
                   actual.IsModified() == modified;
        }
    };

    unsigned checks = 0;
    unsigned failures = 0;
    const auto expect = [&](bool condition, const char* fact, Case test) noexcept
    {
        ++checks;
        failures += condition ? 0u : 1u;
        std::printf("%s duplicate-gesture.case-%u.%s\n", condition ? "PASS" : "FAIL", static_cast<unsigned>(test),
                    fact);
        std::fflush(stdout);
    };
    const auto settle = [&]() noexcept
    { return SettleJobs(_jobs, 15000, DispatchMessageThunk, this) == SettleResult::Settled; };
    const auto terminal = [&]() noexcept
    {
        return !_transformWorkTicket && !_transformWorkStamp && !_historyWork.Busy() && !_hostCommitStamp &&
               !_activeHostRequest && !_hostRequests.Size() && !_hostInputBudget.Used() &&
               !_jobs.ReservedMemoryBytes() && !_jobs.ReleasingMemoryBytes() && !_transformRestoreOwner &&
               !_duplicateGesture;
    };
    const auto cleanup = wil::scope_exit(
        [&]() noexcept
        {
            if (_transformWorkTestGate)
                (void)SetEvent(_transformWorkTestGate->release.get());
            if (!settle())
            {
                SelfTestFailure(L"Duplicate gesture cleanup could not acknowledge borrowed worker state.\n");
                std::_Exit(5);
            }
            _transformWorkTestGate.reset();
            CancelHostContinuations();
            if (!settle())
                std::_Exit(5);
        });

    const auto prepare = [&]() noexcept -> std::optional<Guid>
    {
        if (!settle())
            return std::nullopt;
        _transformWorkTestGate.reset();
        _session.ClearDocument();
        constexpr std::array<std::uint8_t, 16> pixels{11, 21, 31, 255, 41,  51,  61,  255,
                                                      71, 81, 91, 255, 101, 111, 121, 255};
        const Raster image = Raster::Make(2, 2, RasterFormat::Bgra8888, pixels.data(), pixels.size());
        if (!image.HasStorage())
            return std::nullopt;
        DocumentFixture document;
        document.id = Guid::Generate();
        document.width = 16;
        document.height = 16;
        ImageLayer layer = ImageLayer::Blank(L"Duplicate fixture", Vec2{2, 2});
        layer.asset = RasterAsset{image, image, layer.name};
        layer.transform.origin = {2, 2};
        layer.transform.size = {2, 2};
        layer.transform.sampling = LayerSampling::Nearest;
        const Guid id = layer.id;
        document.layers.push_back(std::move(layer));
        _session.ReplaceDocument(std::move(document).Seal(), id);
        _session.history.Reset();
        _session.history.MarkSaved();
        _viewport.SetZoom(1, _viewport.Center(), DocumentSize());
        DocumentChanged();
        if (!_session.BeginDeferredTransform(false))
            return std::nullopt;
        const auto* active = _session.ActiveLayer();
        const auto* edit = _session.PendingTransformEdit() ? &*_session.PendingTransformEdit() : nullptr;
        if (!active || !active->asset || active->asset->image != image || !edit || edit->floating || edit->group ||
            edit->draft.origin != Vec2{2, 2} || edit->draft.size != Vec2{2, 2})
            return std::nullopt;
        TransformDrag drag{TransformDrag::Mode::Move, -1, edit->draft, Vec2{2, 2}, std::nullopt};
        _drag = MoveToolDrag{drag, false};
        return id;
    };
    const auto makeGate = [&](Case test, bool after) noexcept -> bool
    {
        _transformWorkTestGate = std::make_shared<TransformWorkTestGate>();
        _transformWorkTestGate->kind = TransformWorkTestKind::OpenDuplicate;
        _transformWorkTestGate->afterPreparation = after;
        _transformWorkTestGate->throwAdmissionFailure = test == Case::AdmissionFailure;
        _transformWorkTestGate->throwCaptureFailure = test == Case::CaptureFailure;
        _transformWorkTestGate->throwAllocationFailure = test == Case::AllocationFailure;
        if (test == Case::WorkerFailure)
            _transformWorkTestGate->result = {JobStatus::Failed, E_ACCESSDENIED};
        if (test == Case::PendingSnapshotFailure)
            _transformWorkTestGate->historyFailure = DocumentHistory::PrepareStage::PendingSnapshot;
        if (test == Case::PendingRecordFailure)
            _transformWorkTestGate->historyFailure = DocumentHistory::PrepareStage::PendingRecord;
        return static_cast<bool>(_transformWorkTestGate->release);
    };
    const auto snapshot = [&](State& state) noexcept
    {
        state.document = _session.Document().value();
        state.history = _session.history.CaptureState();
        state.source = _session.ActiveLayer()->asset->image;
        state.clipboardPresent = _session.Clipboard().has_value();
        state.clipboardSerial = _session.ClipboardSerial();
        state.clipboard = _session.Clipboard() ? _session.Clipboard()->image : Raster{};
        state.clipboardOrigin = _session.Clipboard() ? _session.Clipboard()->origin : Vec2{};
        state.documentSerial = _session.DocumentSerial();
        state.presents = _renderer.PresentCount();
        state.errorsShown = _errorsShown;
        state.modified = _session.history.IsModified();
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
        state.held =
            owner._activeHostRequest && owner._transformWorkTicket && owner._transformWorkStamp &&
            owner._historyWork.Busy() && owner._hostCommitStamp == owner._transformWorkStamp &&
            owner._jobs.ReservedMemoryBytes() == state.gate->reservationBytes && state.gate->captureReservationHeld &&
            state.documentSerial == owner._session.DocumentSerial() && owner._session.Document() &&
            owner._session.Document().value() == state.document &&
            state.VisibleHistoryMatches(owner._session.history) && owner._session.ActiveLayer() &&
            owner._session.ActiveLayer()->asset && owner._session.ActiveLayer()->asset->image == state.source &&
            owner._session.ClipboardSerial() == state.clipboardSerial &&
            owner._session.Clipboard().has_value() == state.clipboardPresent &&
            (owner._session.Clipboard() ? owner._session.Clipboard()->image : Raster{}) == state.clipboard &&
            (owner._session.Clipboard() ? owner._session.Clipboard()->origin : Vec2{}) == state.clipboardOrigin;
        const double zoom = owner._viewport.zoom;
        owner.OnCommand(ChromeHost::kCommandZoomIn);
        state.responsive = owner._viewport.zoom != zoom;
        if (state.test == Case::CoalescedModifiers || state.test == Case::CoalescedAfterPreparation)
        {
            constexpr Vec2 intermediate{5, 4};
            const Vec2 firstView = owner._viewport.ViewFromDocument(intermediate, owner.DocumentSize());
            owner.ContinueTransformDrag(firstView, MK_SHIFT);
            constexpr Vec2 latest{7, 6};
            const Vec2 view = owner._viewport.ViewFromDocument(latest, owner.DocumentSize());
            const Vec2 mapped = owner._viewport.DocumentFromView(view, owner.DocumentSize());
            owner.ContinueTransformDrag(view, MK_SHIFT | MK_CONTROL);
            state.latestCaptured = owner._duplicateGesture && owner._duplicateGesture->latest == mapped &&
                                   owner._duplicateGesture->modifiers == (MK_SHIFT | MK_CONTROL);
        }
        if (state.test == Case::ReleaseWhileOpening || state.test == Case::ReleaseAfterPreparation)
            (void)owner.FinishDuplicateGesture(false);
        else if (state.test == Case::EscapeBeforePreparation || state.test == Case::EscapeAfterPreparation ||
                 state.test == Case::CaptureLostBeforePreparation || state.test == Case::CaptureLostAfterPreparation)
        {
            if (state.test == Case::CaptureLostBeforePreparation || state.test == Case::CaptureLostAfterPreparation)
                owner.EndPointerDrag(PointerEnd::Lost);
            else
                (void)owner.FinishDuplicateGesture(true);
            state.retained = owner._duplicateGesture && owner._transformWorkTicket && owner._historyWork.Busy() &&
                             owner._jobs.ReservedMemoryBytes() != 0;
        }
        else if (state.test == Case::FullReleaseQueue)
        {
            for (std::size_t index = 0; index < HostRequestQueue::kCapacity; ++index)
                (void)owner.QueueHostInput(HostCommandInput{ChromeHost::kCommandZoomIn});
            state.latestCaptured = owner._hostRequests.Size() == HostRequestQueue::kCapacity &&
                                   owner.FinishDuplicateGesture(false) && owner._duplicateGesture &&
                                   owner._duplicateGesture->abandoned;
        }
        else if (state.test == Case::YoungerUndo)
            state.younger = owner.QueueHostInput(HostCommandInput{ChromeHost::kCommandUndo});
        (void)SetEvent(state.gate->release.get());
    };

    constexpr std::array normalCases{Case::Success,
                                     Case::ReleaseWhileOpening,
                                     Case::ReleaseAfterPreparation,
                                     Case::CoalescedModifiers,
                                     Case::CoalescedAfterPreparation,
                                     Case::EscapeBeforePreparation,
                                     Case::EscapeAfterPreparation,
                                     Case::CaptureLostBeforePreparation,
                                     Case::CaptureLostAfterPreparation,
                                     Case::FullReleaseQueue,
                                     Case::YoungerUndo};
    for (const Case test : normalCases)
    {
        const auto sourceId = prepare();
        if (!sourceId)
            return 5;
        const bool after = test == Case::EscapeAfterPreparation || test == Case::CaptureLostAfterPreparation ||
                           test == Case::ReleaseAfterPreparation || test == Case::CoalescedAfterPreparation;
        if (!makeGate(test, after))
            return 5;
        State state{*this, test, _transformWorkTestGate};
        snapshot(state);
        auto& gate = *_transformWorkTestGate;
        if (!StartDuplicateGesture(std::get<MoveToolDrag>(_drag).drag, Vec2{2, 2}, 0, true))
            return 5;
        if (SettleJobs(_jobs, 15000, dispatch, &state) != SettleResult::Settled)
            std::_Exit(5);

        const bool cancel = test == Case::EscapeBeforePreparation || test == Case::EscapeAfterPreparation ||
                            test == Case::CaptureLostBeforePreparation || test == Case::CaptureLostAfterPreparation ||
                            test == Case::FullReleaseQueue;
        const bool undo = test == Case::YoungerUndo;
        const auto* active = _session.ActiveLayer();
        expect((test == Case::AdmissionFailure ? gate.visits.load() == 0 : state.visited), "opening-gate-reached",
               test);
        if (test != Case::AdmissionFailure)
            expect(state.held && state.responsive, "held-opening-retains-state-and-view-command-works", test);
        if (cancel && test != Case::FullReleaseQueue)
            expect(state.retained, "escape-retains-worker-ownership-until-ack", test);
        if (test == Case::FullReleaseQueue)
            expect(state.latestCaptured, "full-release-queue-cancels-the-owned-opening", test);
        if (test == Case::CoalescedModifiers || test == Case::CoalescedAfterPreparation)
            expect(state.latestCaptured, "latest-point-and-shift-control-captured-while-opening", test);
        if (test == Case::ReleaseWhileOpening || test == Case::ReleaseAfterPreparation)
        {
            expect(!_duplicateGesture && !_session.PendingTransformEdit() && _session.Document()->layers.size() == 2 &&
                       _session.history.UndoCount() == 1 && _session.history.UndoName() == L"Duplicate Layer",
                   "release-queues-after-opening-and-commits-copy", test);
            _session.Undo();
            expect(_session.Document()->layers.size() == 1 && _session.ActiveLayer()->id == sourceId.value() &&
                       _session.ActiveLayer()->asset->image == state.source && _session.history.UndoCount() == 0,
                   "release-before-ready-copy-has-one-undo", test);
        }
        else if (cancel)
            expect(!_duplicateGesture && !_session.PendingTransformEdit() &&
                       _session.Document().value() == state.document && state.VisibleHistoryMatches(_session.history) &&
                       _session.history.UndoCount() == 0,
                   "escape-restores-exact-opening-document-and-history", test);
        else if (undo)
            expect(state.younger && state.younger->Read().outcome == HostOutcome::Succeeded &&
                       _session.Document().value() == state.document && !_session.PendingTransformEdit() &&
                       _session.history.UndoCount() == 0,
                   "younger-undo-settles-copy-before-undo", test);
        else
        {
            expect(_duplicateGesture && _duplicateGesture->copy && _session.PendingTransformEdit() &&
                       _session.PendingTransformEdit()->layerID == *_duplicateGesture->copy &&
                       _session.PendingTransformEdit()->deferred && _transformRestoreOwner &&
                       _session.Document()->layers.size() == 2 && active && active->asset &&
                       active->asset->image == state.source,
                   "successful-open-publishes-deferred-copy-and-prepaid-restore", test);
            if (test == Case::CoalescedModifiers || test == Case::CoalescedAfterPreparation)
                expect(_session.PendingTransformEdit() && _session.PendingTransformEdit()->draft.origin == Vec2{7, 2},
                       "shift-axis-result-is-independent-geometry-reference", test);
            if (test == Case::Success || test == Case::CoalescedModifiers || test == Case::CoalescedAfterPreparation)
            {
                if (!FinishDuplicateGesture(false) || !settle())
                    std::_Exit(5);
                expect(!_duplicateGesture && !_session.PendingTransformEdit() && !_transformRestoreOwner &&
                           _session.Document()->layers.size() == 2 && _session.history.UndoCount() == 1 &&
                           _session.history.UndoName() == L"Duplicate Layer" &&
                           _session.history.UndoName() == L"Duplicate Layer" && _session.ActiveLayer() &&
                           _session.ActiveLayer()->asset && _session.ActiveLayer()->asset->image == state.source,
                       "release-commits-one-copy-entry", test);
                _session.Undo();
                expect(_session.Document()->layers.size() == 1 && _session.ActiveLayer()->id == sourceId.value() &&
                           _session.ActiveLayer()->asset->image == state.source && _session.history.UndoCount() == 0,
                       "one-undo-removes-only-the-duplicate", test);
            }
        }
        expect(_session.ClipboardSerial() == state.clipboardSerial &&
                   _session.Clipboard().has_value() == state.clipboardPresent &&
                   (_session.Clipboard() ? _session.Clipboard()->image : Raster{}) == state.clipboard &&
                   (_session.Clipboard() ? _session.Clipboard()->origin : Vec2{}) == state.clipboardOrigin,
               "clipboard-image-and-serial-preserved", test);
        expect(_renderer.PresentCount() == state.presents, "worker-test-does-not-present", test);
        if (SettleJobs(_jobs, 15000, dispatch, &state) != SettleResult::Settled)
            std::_Exit(5);
        if (test != Case::AdmissionFailure)
            expect(gate.retired && gate.retiredOffOwner, "opening-payload-retires-off-owner", test);
        expect(terminal(), "all-opening-and-restore-reservations-retired", test);
        _transformWorkTestGate.reset();
    }

    constexpr std::array faultCases{Case::AdmissionFailure,  Case::CaptureFailure,         Case::WorkerFailure,
                                    Case::AllocationFailure, Case::PendingSnapshotFailure, Case::PendingRecordFailure};
    for (const Case test : faultCases)
    {
        const auto sourceId = prepare();
        if (!sourceId || !makeGate(test, false))
            return 5;
        State state{*this, test, _transformWorkTestGate};
        snapshot(state);
        auto& gate = *_transformWorkTestGate;
        if (!StartDuplicateGesture(std::get<MoveToolDrag>(_drag).drag, Vec2{2, 2}, 0, true))
            return 5;
        if (SettleJobs(_jobs, 15000, dispatch, &state) != SettleResult::Settled)
            std::_Exit(5);
        const auto* active = _session.ActiveLayer();
        if (test != Case::AdmissionFailure && test != Case::CaptureFailure)
            expect(state.visited && state.held && state.responsive,
                   "fault-gate-retains-visible-state-and-view-responsiveness", test);
        expect(_session.Document().value() == state.document && !_duplicateGesture &&
                   !_session.PendingTransformEdit() && !_transformRestoreOwner && active &&
                   active->id == sourceId.value() && active->asset && active->asset->image == state.source &&
                   state.VisibleHistoryMatches(_session.history) && _session.history.UndoCount() == 0,
               "opening-fault-restores-exact-source-document-and-history", test);
        expect(_session.ClipboardSerial() == state.clipboardSerial &&
                   _session.Clipboard().has_value() == state.clipboardPresent &&
                   (_session.Clipboard() ? _session.Clipboard()->image : Raster{}) == state.clipboard &&
                   (_session.Clipboard() ? _session.Clipboard()->origin : Vec2{}) == state.clipboardOrigin &&
                   terminal(),
               "opening-fault-preserves-clipboard-and-retires-reservations", test);
        if (test == Case::AdmissionFailure || test == Case::CaptureFailure)
            expect(gate.visits.load() == 0, "preparation-gate-not-visited-for-admission-or-capture-fault", test);
        else
            expect(gate.visits.load() == 1 && gate.retired && gate.retiredOffOwner,
                   "worker-fault-reaches-and-retires-gate-payload", test);
        if (test == Case::PendingSnapshotFailure || test == Case::PendingRecordFailure)
            expect(gate.historyFaultHits.load() == 1, "pending-history-fault-stage-reached", test);
        expect(_errorsShown == state.errorsShown + 1, "one-error-notice-for-opening-failure", test);
        _transformWorkTestGate.reset();
    }

    {
        const auto sourceId = prepare();
        if (!sourceId || !makeGate(Case::CommitFailure, false))
            return 5;
        State opening{*this, Case::CommitFailure, _transformWorkTestGate};
        snapshot(opening);
        if (!StartDuplicateGesture(std::get<MoveToolDrag>(_drag).drag, Vec2{2, 2}, 0, true) ||
            SettleJobs(_jobs, 15000, dispatch, &opening) != SettleResult::Settled)
            std::_Exit(5);
        expect(_session.Document()->layers.size() == 2 && _duplicateGesture && _duplicateGesture->copy &&
                   _session.PendingTransformEdit() && _transformRestoreOwner,
               "commit-fault-fixture-has-published-copy-and-prepaid-restore", Case::CommitFailure);
        const auto openingGate = _transformWorkTestGate;
        _transformWorkTestGate = std::make_shared<TransformWorkTestGate>();
        _transformWorkTestGate->kind = TransformWorkTestKind::Commit;
        _transformWorkTestGate->result = {JobStatus::Failed, E_ACCESSDENIED};
        if (!_transformWorkTestGate->release)
            return 5;
        State commit{*this, Case::CommitFailure, _transformWorkTestGate};
        snapshot(commit);
        if (!FinishDuplicateGesture(false) || SettleJobs(_jobs, 15000, dispatch, &commit) != SettleResult::Settled)
            std::_Exit(5);
        expect(commit.visited && commit.held && commit.responsive, "commit-failure-reaches-held-worker-gate",
               Case::CommitFailure);
        const auto* active = _session.ActiveLayer();
        expect(_session.Document().value() == opening.document && !_duplicateGesture &&
                   !_session.PendingTransformEdit() && !_transformRestoreOwner && active &&
                   active->id == sourceId.value() && active->asset && active->asset->image == opening.source &&
                   opening.VisibleHistoryMatches(_session.history) && _session.history.UndoCount() == 0,
               "failed-copy-commit-restores-original-document-and-history", Case::CommitFailure);
        expect(_errorsShown == opening.errorsShown + 1 && _session.ClipboardSerial() == opening.clipboardSerial &&
                   _session.Clipboard().has_value() == opening.clipboardPresent &&
                   (_session.Clipboard() ? _session.Clipboard()->image : Raster{}) == opening.clipboard &&
                   (_session.Clipboard() ? _session.Clipboard()->origin : Vec2{}) == opening.clipboardOrigin &&
                   terminal(),
               "commit-failure-notice-clipboard-and-ownership-retirement", Case::CommitFailure);
        expect(_transformWorkTestGate->retired && _transformWorkTestGate->retiredOffOwner &&
                   openingGate->restoreRetired && openingGate->restoreRetiredOffOwner,
               "commit-and-prepaid-restore-retire-off-owner", Case::CommitFailure);
        _transformWorkTestGate.reset();
    }

    {
        const auto sourceId = prepare();
        if (!sourceId)
            return 5;
        const Raster source = _session.ActiveLayer()->asset->image;
        auto* move = std::get_if<MoveToolDrag>(&_drag);
        move->duplicateOnMove = true;
        ContinueTransformDrag(_viewport.ViewFromDocument(Vec2{4, 3}, DocumentSize()), MK_CONTROL);
        expect(_duplicateGesture && _hostRequests.Size() == 1, "first-armed-pointer-move-admits-owned-opening",
               Case::ReleaseWhileOpening);
        EndTransformDrag();
        // Releasing Win32 capture after an actual Up must not cancel its already accepted commit.
        EndPointerDrag(PointerEnd::Lost);
        if (!settle())
            std::_Exit(5);
        const auto* active = _session.ActiveLayer();
        expect(active && active->id != sourceId.value() && active->transform.origin == Vec2{4, 3} && active->asset &&
                   active->asset->image == source && _session.Document()->layers.size() == 2 &&
                   _session.history.UndoCount() == 1 && _session.history.UndoName() == L"Duplicate Layer" && terminal(),
               "quick-real-pointer-release-keeps-its-copy-and-input-geometry", Case::ReleaseWhileOpening);
    }

    // Omitted to keep this sidecar bounded: distortion/resize duplication, stale document replacement, remaining
    // queue-capacity exhaustion, duplicate-layer limit, worker timeout/shutdown, and every other history preparation
    // stage.
    std::printf("duplicate-gesture summary: %u checks, %u failures; omitted: distort/resize, stale replacement, queue "
                "and layer limits, timeout/shutdown, remaining history stages\n",
                checks, failures);
    std::fflush(stdout);
    return failures ? 1 : 0;
}
