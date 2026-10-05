#include "Application.h"

#include "../Tests/Support/DocumentFixture.h"
#include "ChromeHost.h"
#include "DocumentLifecycleTestGate.h"
#include "Selection.h"
#include "resource.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>

int Application::RunDocumentLifecycleCloseSelfTest() noexcept
{
    enum class Mode : std::uint8_t
    {
        CancelBeforePreparation,
        CancelAfterPreparation,
        WorkerPreparationFailure,
        OwnerCaptureFailure,
        AdmissionFailure,
        ReplacementSnapshotFailure,
        ReplacementResetFailure,
    };
    struct State final
    {
        Application& owner;
        Mode mode;
        ContinuationReceipt receipt;
        std::optional<CanvasDocument> document;
        DocumentHistory::StateView history;
        std::optional<Guid> active;
        std::unordered_set<Guid> selected;
        std::unordered_set<Guid> collapsed;
        Raster clipboardImage;
        std::wstring path;
        Guid revision;
        std::uint64_t generation = 0;
        std::uint64_t serial = 0;
        std::uint32_t foreground = 0;
        std::uint32_t background = 0;
        int notices = 0;
        bool wasModified = false;
        bool couldUndo = false;
        bool couldRedo = false;
        bool maskSelected = false;
        bool visited = false;
        bool reservationHeld = false;
        bool cancelPending = false;
    };

    unsigned int checks = 0;
    unsigned int failures = 0;
    const auto expect = [&](bool condition, const char* caseName, const char* fact) noexcept
    {
        ++checks;
        failures += condition ? 0u : 1u;
        std::printf("%s lifecycle-close.%s.%s\n", condition ? "PASS" : "FAIL", caseName, fact);
        std::fflush(stdout);
    };
    const auto caseName = [](Mode mode) noexcept -> const char*
    {
        switch (mode)
        {
        case Mode::CancelBeforePreparation:
            return "cancel-before-prepare";
        case Mode::CancelAfterPreparation:
            return "cancel-after-ready";
        case Mode::WorkerPreparationFailure:
            return "worker-prepare-oom";
        case Mode::OwnerCaptureFailure:
            return "owner-capture-oom";
        case Mode::AdmissionFailure:
            return "admission-oom";
        case Mode::ReplacementSnapshotFailure:
            return "replacement-snapshot-oom";
        case Mode::ReplacementResetFailure:
            return "replacement-reset-oom";
        }
        return "unknown";
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
        const auto& live = owner._session.Document();
        state.reservationHeld =
            owner._activeHostRequest && owner._documentLifecycleTicket && owner._documentLifecycleStamp &&
            owner._historyWork.Busy() && owner._hostCommitStamp == owner._documentLifecycleStamp &&
            gate->captureReservationHeld.load() && gate->reservationBytes != 0 &&
            owner._jobs.ReservedMemoryBytes() >= gate->reservationBytes &&
            state.receipt.Read().outcome == HostOutcome::Pending && live && state.document &&
            live->id == state.document->id && owner._session.history.CurrentRevision() == state.revision;

        if (state.mode == Mode::CancelBeforePreparation || state.mode == Mode::CancelAfterPreparation)
        {
            owner.CancelHostContinuations();
            state.cancelPending = state.receipt.Read().outcome == HostOutcome::Pending &&
                                  owner._documentLifecycleTicket && owner._historyWork.Busy();
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
            _documentLifecycleTestGate.reset();
        });

    const auto seed = [this, &settle]() noexcept
    {
        if (_documentLifecycleTestGate && _documentLifecycleTestGate->release)
            (void)SetEvent(_documentLifecycleTestGate->release.get());
        _documentLifecycleTestGate.reset();
        CancelHostContinuations();
        if (!settle())
            return false;

        DocumentFixture document;
        document.id = Guid::Generate();
        document.width = 1;
        document.height = 1;
        constexpr std::array<std::uint8_t, 4> bgra{23, 47, 89, 255};
        constexpr std::array<std::uint8_t, 1> maskByte{255};
        const Raster image = Raster::Make(1, 1, RasterFormat::Bgra8888, bgra.data(), bgra.size());
        const Raster mask = Raster::Make(1, 1, RasterFormat::Gray8, maskByte.data(), maskByte.size());
        if (!image.HasStorage() || !mask.HasStorage())
            return false;

        ImageLayer group = ImageLayer::Blank(L"Lifecycle group", Vec2{1, 1});
        group.isGroup = true;
        const Guid groupID = group.id;
        ImageLayer layer = ImageLayer::Blank(L"Lifecycle pixel", Vec2{1, 1});
        layer.parentID = groupID;
        layer.asset = RasterAsset{image, Raster::MakeThumbnail(image), L"Lifecycle pixel"};
        layer.mask =
            LayerMask{RasterAsset{mask, Raster::MakeThumbnail(mask), L"Lifecycle mask"}, true, std::nullopt, true};
        const Guid active = layer.id;
        document.layers.push_back(std::move(group));
        document.layers.push_back(std::move(layer));
        document.selection = DocumentSelection{true, SelectionGeometry::Rect(0, 0, 1, 1)};
        _session.ReplaceDocument(std::move(document).Seal(), active);
        _session.history.Reset();
        _session.history.MarkSaved();
        _session.SetGroupCollapsed(groupID, true);
        _session.SetForegroundColor(0xFF3167A9u);
        _session.SetBackgroundColor(0xFFE2B45Cu);
        _documentPath = L"Z:\\lifecycle-close-test\\source.prism";
        if (!_session.CopySelection() || !_session.Clipboard().has_value() ||
            !_session.Clipboard()->image.HasStorage() || _session.Clipboard()->image.Width() != 1 ||
            _session.Clipboard()->image.Height() != 1 || _session.Clipboard()->image.Data()[0] != bgra[0] ||
            _session.Clipboard()->image.Data()[1] != bgra[1] || _session.Clipboard()->image.Data()[2] != bgra[2] ||
            _session.Clipboard()->image.Data()[3] != bgra[3])
            return false;
        _session.SelectLayerTarget(active, true);
        _session.AddBlankLayer();
        const auto undo = QueueHostInput(HostCommandInput{ChromeHost::kCommandUndo});
        if (!settle() || undo.Read().outcome != HostOutcome::Succeeded)
            return false;
        _session.SelectLayerTarget(active, true);
        _session.history.MarkSaved();
        return _session.Clipboard().has_value() && _session.Clipboard()->image.HasStorage() &&
               _session.Clipboard()->image.Width() == 1 && _session.Clipboard()->image.Height() == 1 &&
               _session.Clipboard()->image.Data()[0] == bgra[0] && _session.Clipboard()->image.Data()[1] == bgra[1] &&
               _session.Clipboard()->image.Data()[2] == bgra[2] && _session.Clipboard()->image.Data()[3] == bgra[3] &&
               _session.IsMaskSelected() && _session.history.CanRedo() && !_session.history.IsModified();
    };

    if (!seed())
    {
        SelfTestFailure(L"The Close lifecycle host fixture could not be seeded.\n");
        return 5;
    }

    constexpr std::array modes{Mode::CancelBeforePreparation,  Mode::CancelAfterPreparation,
                               Mode::WorkerPreparationFailure, Mode::OwnerCaptureFailure,
                               Mode::AdmissionFailure,         Mode::ReplacementSnapshotFailure,
                               Mode::ReplacementResetFailure};
    for (const Mode mode : modes)
    {
        if (!seed())
            return 5;
        _documentLifecycleTestGate = std::make_shared<DocumentLifecycleTestGate>();
        if (!_documentLifecycleTestGate->release)
            return 5;
        const auto gate = _documentLifecycleTestGate;
        gate->afterPreparation = mode == Mode::CancelAfterPreparation;
        gate->throwPreparationFailure = mode == Mode::WorkerPreparationFailure;
        gate->throwCaptureFailure = mode == Mode::OwnerCaptureFailure;
        gate->throwAdmissionFailure = mode == Mode::AdmissionFailure;
        if (mode == Mode::ReplacementSnapshotFailure)
            gate->historyFailure = DocumentHistory::PrepareStage::ReplacementSnapshot;
        if (mode == Mode::ReplacementResetFailure)
            gate->historyFailure = DocumentHistory::PrepareStage::ReplacementReset;

        State state{*this};
        state.mode = mode;
        state.document = _session.Document();
        state.history = _session.history.CaptureState();
        state.active = _session.ActiveLayerID();
        state.selected = _session.SelectedLayerIDs();
        state.collapsed = _session.CollapsedGroupIDs();
        state.clipboardImage = _session.Clipboard()->image;
        state.path = _documentPath;
        state.revision = _session.history.CurrentRevision();
        state.generation = _session.Generation();
        state.serial = _session.DocumentSerial();
        state.foreground = _session.ForegroundColor();
        state.background = _session.BackgroundColor();
        state.notices = _errorsShown;
        state.wasModified = _session.history.IsModified();
        state.couldUndo = _session.history.CanUndo();
        state.couldRedo = _session.history.CanRedo();
        state.maskSelected = _session.IsMaskSelected();

        state.receipt = QueueHostInput(HostCloseProjectInput{});
        const SettleResult settled = SettleJobs(_jobs, 15000, dispatch, &state);
        const ContinuationReceipt::Result result = state.receipt.Read();
        const bool admitted = mode != Mode::AdmissionFailure;
        const bool fault = mode != Mode::CancelBeforePreparation && mode != Mode::CancelAfterPreparation;
        const bool eventExpected = admitted && mode != Mode::OwnerCaptureFailure;
        const HostOutcome expected = fault ? HostOutcome::Failed : HostOutcome::Cancelled;

        expect(settled == SettleResult::Settled, caseName(mode), "bounded-settle");
        expect(result.outcome == expected, caseName(mode), "exact-receipt-outcome");
        expect(state.visited == eventExpected && (!eventExpected || state.reservationHeld), caseName(mode),
               "event-and-whole-reservation-before-source-capture");
        const bool cancelExpected = mode == Mode::CancelBeforePreparation || mode == Mode::CancelAfterPreparation;
        expect(state.cancelPending == cancelExpected, caseName(mode), "cancel-requested-while-gated-receipt-pending");
        expect(!_historyWork.Busy() && !_documentLifecycleTicket && !_documentLifecycleStamp && !_hostCommitStamp &&
                   !_activeHostRequest && _hostRequests.Size() == 0 && _hostInputBudget.Used() == 0 &&
                   _jobs.ReservedMemoryBytes() == 0 && _jobs.ReleasingMemoryBytes() == 0,
               caseName(mode), "host-and-job-ownership-settled");
        expect(gate->retired.load() == (admitted ? 1U : 0U) && (!admitted || gate->retiredOffOwner.load()),
               caseName(mode), "payload-retired-exactly-once-off-owner");
        if (admitted)
            expect(gate->captureReservationHeld.load(), caseName(mode), "reservation-precedes-owner-capture");
        if (mode == Mode::ReplacementSnapshotFailure || mode == Mode::ReplacementResetFailure)
            expect(gate->historyFaultHits.load() == 1, caseName(mode), "reached-close-history-checkpoint-once");

        if (fault)
        {
            expect(result.error == E_OUTOFMEMORY, caseName(mode), "exact-allocation-error");
            expect(_errorsShown == state.notices + 1 && _lastErrorId == IDS_ERROR_DOCUMENT_LIFECYCLE, caseName(mode),
                   "one-localized-oom-notice");
        }
        else
        {
            expect(result.error == E_ABORT, caseName(mode), "exact-cancellation-error");
            expect(_errorsShown == state.notices, caseName(mode), "cancellation-has-no-error-notice");
        }

        const auto& clipboard = _session.Clipboard();
        const bool clipboardSame = clipboard && clipboard->image == state.clipboardImage &&
                                   clipboard->image.Width() == 1 && clipboard->image.Height() == 1 &&
                                   clipboard->image.Data()[0] == 23 && clipboard->image.Data()[1] == 47 &&
                                   clipboard->image.Data()[2] == 89 && clipboard->image.Data()[3] == 255;
        expect(_session.Document() == state.document && _session.ActiveLayerID() == state.active &&
                   _session.SelectedLayerIDs() == state.selected && _session.IsMaskSelected() == state.maskSelected &&
                   _session.CollapsedGroupIDs() == state.collapsed &&
                   _session.Selection() == state.document.value().selection,
               caseName(mode), "live-document-target-mask-collapsed-selection-unchanged");
        expect(_session.history.IsCurrent(state.history) && _session.history.CurrentRevision() == state.revision &&
                   _session.history.IsModified() == state.wasModified &&
                   _session.history.CanUndo() == state.couldUndo && _session.history.CanRedo() == state.couldRedo &&
                   _documentPath == state.path && _session.Generation() == state.generation &&
                   _session.DocumentSerial() == state.serial,
               caseName(mode), "history-redo-saved-marker-path-and-lifetime-unchanged");
        expect(_session.ForegroundColor() == state.foreground && _session.BackgroundColor() == state.background &&
                   clipboardSame,
               caseName(mode), "private-clipboard-and-palette-unchanged");

        _documentLifecycleTestGate.reset();
    }

    std::printf("lifecycle-close summary: %u checks, %u failures\n", checks, failures);
    std::fflush(stdout);
    return failures == 0 ? 0 : 5;
}
