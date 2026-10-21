#include "Application.h"

#include "../Tests/Support/Support.Tests.DocumentFixture.h"

#include "ChromeHost.h"
#include "Runtime.Tests.PixelMoveWorkTestGate.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>

int Application::RunPixelMoveWorkSelfTest() noexcept
{
    const auto failure = [](int line) noexcept
    {
        std::printf("FAIL pixelmove-host.line-%d\n", line);
        return 5;
    };
    enum class Mode
    {
        Success,
        Cancel,
        LateCancel,
        Failure,
        Allocation,
        Admission,
        Capture,
        Invalid,
        Memory,
        TooLarge,
        Unchanged,
        StaleGeneration,
        StaleSelection,
        Replace,
        Coalesce,
        YoungerUndo,
        YoungerNudge,
        ReopenPreview,
    };
    struct State final
    {
        Application& owner;
        Mode mode;
        std::shared_ptr<PixelMoveWorkTestGate> gate;
        ContinuationReceipt receipt;
        Raster source;
        Guid revision;
        bool visited = false;
        bool held = false;
        bool acknowledged = false;
        bool firstShown = false;
        ContinuationReceipt younger;
    };
    const auto dispatch = [](void* context, void* native) noexcept
    {
        auto& state = *static_cast<State*>(context);
        auto& owner = state.owner;
        auto& message = *static_cast<MSG*>(native);
        if (message.message != PixelMoveWorkTestGate::kMessage)
        {
            owner.DispatchQueuedMessage(message);
            return;
        }
        state.visited = true;
        const bool commit = state.gate->kind == PixelMoveWorkTestKind::Commit;
        const bool subsequentNudge = state.mode == Mode::YoungerNudge && state.gate->visits.load() > 1;
        const auto presents = owner._renderer.PresentCount();
        owner.OnCommand(ChromeHost::kCommandZoomIn);
        state.held = state.gate->captureReservationHeld && state.gate->reservationBytes != 0 &&
                     owner._jobs.ReservedMemoryBytes() == state.gate->reservationBytes &&
                     owner._renderer.PresentCount() == presents &&
                     (!commit || (owner._pixelMoveCommitStamp && owner._historyWork.Busy() &&
                                  owner._hostCommitStamp == owner._pixelMoveCommitStamp && owner._activeHostRequest &&
                                  (subsequentNudge ? owner._session.history.UndoCount() == 1
                                                   : state.receipt.Read().outcome == HostOutcome::Pending &&
                                                         owner._session.history.CurrentRevision() == state.revision &&
                                                         owner._session.ActiveLayer()->asset->image == state.source)));
        switch (state.mode)
        {
        case Mode::Cancel:
            owner.CancelPixelDrag();
            state.acknowledged = state.receipt.Read().outcome == HostOutcome::Pending && owner._pixelMoveCommitTicket &&
                                 owner._historyWork.Busy();
            break;
        case Mode::LateCancel:
            (void)owner._historyWork.RequestCancel();
            state.acknowledged = state.receipt.Read().outcome == HostOutcome::Pending && owner._pixelMoveCommitTicket &&
                                 owner._historyWork.Busy();
            break;
        case Mode::StaleGeneration:
            owner._session.EditForTest([](CanvasDocument&) {});
            break;
        case Mode::StaleSelection:
            owner._session.EditForTest([](CanvasDocument& document) { document.selection.reset(); });
            break;
        case Mode::Replace:
        {
            DocumentFixture replacement;
            replacement.id = Guid::Generate();
            replacement.width = 3;
            replacement.height = 1;
            replacement.layers.push_back(ImageLayer::Blank(L"Replacement", Vec2{3, 1}));
            const Guid active = replacement.layers.front().id;
            owner.ReplaceDocument(std::move(replacement).Seal(), active, {});
            break;
        }
        case Mode::YoungerUndo:
            state.younger = owner.QueueHostInput(HostCommandInput{ChromeHost::kCommandUndo});
            state.acknowledged =
                state.younger.Read().outcome == HostOutcome::Pending && owner._session.history.UndoCount() == 0;
            break;
        case Mode::YoungerNudge:
            if (!subsequentNudge)
            {
                const bool previousShift = owner._acceleratorShift;
                owner._acceleratorShift = true;
                owner.OnCommand(ChromeHost::kCommandNudgePixelsRight);
                owner._acceleratorShift = previousShift;
                state.acknowledged = owner._hostRequests.Size() == 1 && owner._session.history.UndoCount() == 0;
            }
            break;
        case Mode::Coalesce:
            if (state.gate->visits.load() == 1)
            {
                owner.DragPixels(Vec2{3.5, 0.5}, false);
                owner.DragPixels(Vec2{4.5, 0.5}, false);
            }
            else
            {
                const auto* pending = owner._session.PendingPixelMove();
                state.firstShown = pending && pending->raster && pending->publishedSequence < pending->sequence &&
                                   owner._composition.HasStroke();
            }
            break;
        case Mode::ReopenPreview:
            if (state.gate->visits.load() == 1)
            {
                owner.CancelPixelDrag();
                owner.BeginPixelDrag(Vec2{1.5, 0.5}, false);
                owner.DragPixels(Vec2{3.5, 0.5}, false);
            }
            break;
        default:
            break;
        }
        (void)SetEvent(state.gate->release.get());
    };
    const auto settle = [&]() noexcept
    { return SettleJobs(_jobs, 15000, DispatchMessageThunk, this) == SettleResult::Settled; };
    const auto cleanup = wil::scope_exit(
        [&]() noexcept
        {
            if (_pixelMoveWorkTestGate)
                (void)SetEvent(_pixelMoveWorkTestGate->release.get());
            if (!settle())
            {
                SelfTestFailure(L"Pixel move cleanup could not settle borrowed harness state.\n");
                std::_Exit(5);
            }
            _pixelMoveWorkTestGate.reset();
            CancelPixelDrag();
        });
    const auto terminal = [&]() noexcept
    {
        return !_pixelMovePreviewTicket && !_pixelMoveCommitTicket && !_pixelMoveCommitStamp && !_historyWork.Busy() &&
               !_activeHostRequest && _hostRequests.Size() == 0 && _hostInputBudget.Used() == 0 &&
               _jobs.ReservedMemoryBytes() == 0 && _jobs.ReleasingMemoryBytes() == 0;
    };
    const auto gate = [&](PixelMoveWorkTestKind kind, Mode mode, bool after) noexcept
    {
        _pixelMoveWorkTestGate = std::make_shared<PixelMoveWorkTestGate>();
        _pixelMoveWorkTestGate->kind = kind;
        _pixelMoveWorkTestGate->afterPreparation = after;
        _pixelMoveWorkTestGate->throwAdmissionFailure = mode == Mode::Admission;
        _pixelMoveWorkTestGate->throwCaptureFailure = mode == Mode::Capture;
        _pixelMoveWorkTestGate->throwAllocationFailure = mode == Mode::Allocation;
        if (mode == Mode::Failure)
            _pixelMoveWorkTestGate->result = {JobStatus::Failed, E_ACCESSDENIED};
        if (mode == Mode::Invalid)
            _pixelMoveWorkTestGate->status = RasterEditStatus::Invalid;
        if (mode == Mode::Memory)
            _pixelMoveWorkTestGate->status = RasterEditStatus::Memory;
        if (mode == Mode::TooLarge)
            _pixelMoveWorkTestGate->status = RasterEditStatus::TooLarge;
        if (mode == Mode::Unchanged)
            _pixelMoveWorkTestGate->status = RasterEditStatus::Unchanged;
        return static_cast<bool>(_pixelMoveWorkTestGate->release);
    };
    constexpr std::size_t kLargeMetadataUnits = 1'048'576;
    const auto open = [&](bool largeMetadata = false) noexcept
    {
        _session.CreateDocument(16, 1, true);
        constexpr std::array<std::uint8_t, 16> bytes{10, 20, 30, 255, 40,  50,  60,  255,
                                                     70, 80, 90, 255, 100, 110, 120, 255};
        const Raster image = Raster::Make(4, 1, RasterFormat::Bgra8888, bytes.data(), bytes.size());
        if (!image.HasStorage())
            return false;
        constexpr std::uint8_t white = 255;
        const Raster mask = largeMetadata ? Raster::Make(1, 1, RasterFormat::Gray8, &white, 1) : Raster{};
        if (largeMetadata && !mask.HasStorage())
            return false;
        EditLayerRecordsForTest(_session,
                                [&](DocumentFixture& document)
                                {
                                    auto& layer = document.layers.front();
                                    layer.transform.size = Vec2{4, 1};
                                    layer.asset = RasterAsset{image, Raster::MakeThumbnail(image), layer.name};
                                    if (largeMetadata)
                                    {
                                        layer.asset->name.assign(kLargeMetadataUnits, L'P');
                                        layer.mask = LayerMask{RasterAsset{mask, mask, {}}};
                                        layer.mask->asset.name.assign(kLargeMetadataUnits, L'M');
                                    }
                                    document.selection = DocumentSelection{false, {{{1, 0}, {2, 0}, {2, 1}, {1, 1}}}};
                                });
        _session.history.Reset();
        DocumentChanged();
        BeginPixelDrag(Vec2{1.5, 0.5}, false);
        DragPixels(Vec2{2.5, 0.5}, false);
        if (!_session.PendingPixelMove())
            std::printf("FAIL pixelmove-host.open canPaint=%d selection=%d gradient=%d transform=%d mask=%d\n",
                        _session.CanPaint(), _session.Selection().has_value(), _session.PendingGradient() != nullptr,
                        _session.PendingTransformEdit().has_value(), _session.IsMaskSelected());
        return _session.PendingPixelMove() != nullptr;
    };

    for (const Mode mode :
         {Mode::Success, Mode::Cancel, Mode::LateCancel, Mode::Failure, Mode::Allocation, Mode::Admission,
          Mode::Capture, Mode::Invalid, Mode::Memory, Mode::TooLarge, Mode::Unchanged, Mode::StaleGeneration,
          Mode::StaleSelection, Mode::Replace, Mode::YoungerUndo, Mode::YoungerNudge})
    {
        std::printf("pixelmove-host.running-commit-%d\n", static_cast<int>(mode));
        CancelPixelDrag();
        const bool initialSettled = settle();
        const bool initialGate = initialSettled && gate(PixelMoveWorkTestKind::Preview, Mode::Success, true);
        const bool initialOpened = initialGate && open(mode == Mode::Success);
        if (!initialOpened)
        {
            std::printf("FAIL pixelmove-host.setup settled=%d gate=%d opened=%d\n", initialSettled, initialGate,
                        initialOpened);
            return failure(__LINE__);
        }
        State preview{*this,
                      Mode::Success,
                      _pixelMoveWorkTestGate,
                      {},
                      _session.ActiveLayer()->asset->image,
                      _session.history.CurrentRevision()};
        if (SettleJobs(_jobs, 15000, dispatch, &preview) != SettleResult::Settled || !preview.visited ||
            !preview.held || !terminal() || !_session.PendingPixelMove()->raster || !_composition.HasStroke() ||
            _session.history.UndoCount() != 0)
        {
            const auto* pending = _session.PendingPixelMove();
            std::printf("FAIL pixelmove-host.preview visited=%d held=%d terminal=%d pending=%d raster=%d gpu=%d "
                        "undo=%d reservation=%llu gate=%llu\n",
                        preview.visited, preview.held, terminal(), pending != nullptr,
                        pending && static_cast<bool>(pending->raster), _composition.HasStroke(),
                        _session.history.UndoCount(), static_cast<unsigned long long>(_jobs.ReservedMemoryBytes()),
                        static_cast<unsigned long long>(_pixelMoveWorkTestGate->reservationBytes));
            return failure(__LINE__);
        }
        if (_pixelMoveWorkTestGate->openingPreparations.load() != 1 ||
            _pixelMoveWorkTestGate->openingReuses.load() != 0 || !_pixelMoveOpening)
            return failure(__LINE__);
        const Raster source = _session.ActiveLayer()->asset->image;
        const Guid revision = _session.history.CurrentRevision();
        if (!gate(PixelMoveWorkTestKind::Commit, mode, mode != Mode::Cancel && mode != Mode::Allocation))
            return failure(__LINE__);
        const auto opening = _session.PendingPixelMove()->opening;
        DropDrag<PixelDrag>();
        const auto receipt = QueueHostInput(HostPixelMoveApplyInput{opening});
        State state{*this, mode, _pixelMoveWorkTestGate, receipt, source, revision};
        if (SettleJobs(_jobs, 15000, dispatch, &state) != SettleResult::Settled || !terminal())
            return failure(__LINE__);
        const bool refusedBeforeGate = mode == Mode::Admission || mode == Mode::Capture;
        if ((!refusedBeforeGate && (!state.visited || !state.held)) ||
            ((mode == Mode::Cancel || mode == Mode::LateCancel || mode == Mode::YoungerUndo ||
              mode == Mode::YoungerNudge) &&
             !state.acknowledged))
            return failure(__LINE__);
        const auto expected =
            mode == Mode::Success || mode == Mode::Unchanged || mode == Mode::YoungerUndo || mode == Mode::YoungerNudge
                ? HostOutcome::Succeeded
            : mode == Mode::Cancel || mode == Mode::LateCancel ? HostOutcome::Cancelled
            : mode == Mode::StaleGeneration || mode == Mode::StaleSelection || mode == Mode::Replace
                ? HostOutcome::Stale
                : HostOutcome::Failed;
        if (receipt.Read().outcome != expected || _pixelMoveOpening ||
            (_pixelMoveWorkTestGate->payloadRetired && _pixelMoveWorkTestGate->payloadReleaseRequired &&
             !_pixelMoveWorkTestGate->payloadRetiredOffOwner))
        {
            std::printf("FAIL pixelmove-host.outcome actual=%u expected=%u error=%08x retired=%d offOwner=%d\n",
                        static_cast<unsigned int>(receipt.Read().outcome), static_cast<unsigned int>(expected),
                        static_cast<unsigned int>(receipt.Read().error), _pixelMoveWorkTestGate->payloadRetired.load(),
                        _pixelMoveWorkTestGate->payloadRetiredOffOwner.load());
            return failure(__LINE__);
        }
        if (mode == Mode::Success)
        {
            const bool workerTarget = _pixelMoveWorkTestGate->rootOnlyCapturedOnOwner &&
                                      _pixelMoveWorkTestGate->targetCapturesOnOwner.load() == 0 &&
                                      _pixelMoveWorkTestGate->targetCapturesOnWorker.load() == 1;
            std::printf("%s pixelmove-host.commit-full-target-captured-on-worker-from-charged-root owner=%u worker=%u "
                        "rootOwner=%d\n",
                        workerTarget ? "PASS" : "FAIL", _pixelMoveWorkTestGate->targetCapturesOnOwner.load(),
                        _pixelMoveWorkTestGate->targetCapturesOnWorker.load(),
                        _pixelMoveWorkTestGate->rootOnlyCapturedOnOwner);
            if (!workerTarget)
                return failure(__LINE__);
            constexpr std::array<std::uint8_t, 16> expectedBytes{10, 20, 30, 255, 0,   0,   0,   0,
                                                                 40, 50, 60, 255, 100, 110, 120, 255};
            std::array<std::uint8_t, 16> actual{};
            _session.ActiveLayer()->asset->image.ReadRegion(0, 0, 4, 1, actual.data(), 16, 0);
            if (actual != expectedBytes || _session.history.UndoCount() != 1 ||
                _session.history.UndoName() != L"Move Pixels" || _session.PendingPixelMove() || _composition.HasStroke())
                return failure(__LINE__);
            const auto* target = _session.ActiveLayer();
            const bool metadataPreserved =
                target && target->asset && target->mask &&
                _pixelMoveWorkTestGate->capturedImageNameUnits.load() == kLargeMetadataUnits &&
                _pixelMoveWorkTestGate->capturedMaskNameUnits.load() == kLargeMetadataUnits &&
                target->asset->name == target->name && target->mask->asset.name.size() == kLargeMetadataUnits &&
                std::all_of(target->mask->asset.name.begin(), target->mask->asset.name.end(),
                            [](wchar_t c) { return c == L'M'; });
            std::printf(
                "%s "
                "pixelmove-host.large-source-names-captured-on-worker-and-committed-metadata-matches-existing-policy\n",
                metadataPreserved ? "PASS" : "FAIL");
            if (!metadataPreserved)
                return failure(__LINE__);
            _session.Undo();
            if (_session.ActiveLayer()->asset->image != source)
                return failure(__LINE__);
        }
        else if (mode == Mode::YoungerUndo)
        {
            if (state.younger.Read().outcome != HostOutcome::Succeeded ||
                _session.ActiveLayer()->asset->image != source || _session.history.UndoCount() != 0)
                return failure(__LINE__);
        }
        else if (mode == Mode::YoungerNudge)
        {
            const auto bounds = _session.Selection()->Bounds();
            std::array<std::uint8_t, 4> destination{};
            _session.ActiveLayer()->asset->image.ReadRegion(12, 0, 1, 1, destination.data(), 4, 0);
            if (_pixelMoveWorkTestGate->visits.load() != 2 || _session.history.UndoCount() != 2 || !bounds ||
                bounds->minX != 12 || bounds->maxX != 13 || destination != std::array<std::uint8_t, 4>{40, 50, 60, 255})
                return failure(__LINE__);
            _session.Undo();
            _session.Undo();
            if (_session.ActiveLayer()->asset->image != source)
                return failure(__LINE__);
        }
        else if (mode != Mode::Replace &&
                 (_session.ActiveLayer()->asset->image != source || _session.history.CurrentRevision() != revision ||
                  _session.history.UndoCount() != 0))
            return failure(__LINE__);
        std::printf("PASS pixelmove-host.commit-%d\n", static_cast<int>(mode));
    }
    CancelPixelDrag();
    if (!settle() || !gate(PixelMoveWorkTestKind::Preview, Mode::Coalesce, true) || !open())
        return failure(__LINE__);
    State coalesced{*this,
                    Mode::Coalesce,
                    _pixelMoveWorkTestGate,
                    {},
                    _session.ActiveLayer()->asset->image,
                    _session.history.CurrentRevision()};
    if (SettleJobs(_jobs, 15000, dispatch, &coalesced) != SettleResult::Settled || !coalesced.held ||
        !coalesced.firstShown || _pixelMoveWorkTestGate->visits.load() != 2 || !terminal() ||
        !_session.PendingPixelMove() ||
        _session.PendingPixelMove()->publishedSequence != _session.PendingPixelMove()->sequence)
        return failure(__LINE__);
    if (_pixelMoveWorkTestGate->openingPreparations.load() != 1 || _pixelMoveWorkTestGate->openingReuses.load() != 1 ||
        !_pixelMoveOpening)
        return failure(__LINE__);
    const auto latest = _session.PendingPixelMove()->raster->PaintSnapshot();
    std::array<std::uint8_t, 4> moved{};
    if (!latest)
        return failure(__LINE__);
    latest->image.ReadRegion(4, 0, 1, 1, moved.data(), 4, 0);
    if (moved != std::array<std::uint8_t, 4>{40, 50, 60, 255})
        return failure(__LINE__);
    std::printf("PASS pixelmove-host.active-preview-shown-and-latest-coalesced\n");
    const std::weak_ptr<const PixelMoveOpening> reusedOpening = _pixelMoveOpening;
    const Raster coalescedSource = _session.ActiveLayer()->asset->image;
    const auto coalescedRevision = _session.history.CurrentRevision();
    const auto coalescedOpening = _session.PendingPixelMove()->opening;
    if (!gate(PixelMoveWorkTestKind::Commit, Mode::Success, true))
        return failure(__LINE__);
    DropDrag<PixelDrag>();
    const auto coalescedReceipt = QueueHostInput(HostPixelMoveApplyInput{coalescedOpening});
    State reusedCommit{*this,           Mode::Success,    _pixelMoveWorkTestGate, coalescedReceipt,
                       coalescedSource, coalescedRevision};
    if (SettleJobs(_jobs, 15000, dispatch, &reusedCommit) != SettleResult::Settled || !reusedCommit.held ||
        coalescedReceipt.Read().outcome != HostOutcome::Succeeded || !terminal() || _pixelMoveOpening ||
        !reusedOpening.expired() || _pixelMoveWorkTestGate->openingPreparations.load() != 0 ||
        _pixelMoveWorkTestGate->openingReuses.load() != 1 || !_pixelMoveWorkTestGate->payloadRetired.load() ||
        !_pixelMoveWorkTestGate->payloadRetiredOffOwner.load() || _session.history.UndoCount() != 1 ||
        _session.history.UndoName() != L"Move Pixels")
        return failure(__LINE__);
    constexpr std::array<std::uint8_t, 20> reusedPixels{10, 20,  30,  255, 0,   0,   0,  0,  70, 80,
                                                        90, 255, 100, 110, 120, 255, 40, 50, 60, 255};
    std::array<std::uint8_t, 20> actualReused{};
    _session.ActiveLayer()->asset->image.ReadRegion(0, 0, 5, 1, actualReused.data(), 20, 0);
    if (actualReused != reusedPixels || !_session.Selection()->Bounds() || _session.Selection()->Bounds()->minX != 4 ||
        _session.Selection()->Bounds()->maxX != 5)
        return failure(__LINE__);
    _session.Undo();
    if (_session.ActiveLayer()->asset->image != coalescedSource)
        return failure(__LINE__);
    std::printf("PASS pixelmove-host.one-opening-two-previews-commit-literal-pixels-and-worker-retirement\n");

    CancelPixelDrag();
    if (!settle() || !gate(PixelMoveWorkTestKind::Preview, Mode::ReopenPreview, true) || !open())
        return failure(__LINE__);
    State reopened{*this,
                   Mode::ReopenPreview,
                   _pixelMoveWorkTestGate,
                   {},
                   _session.ActiveLayer()->asset->image,
                   _session.history.CurrentRevision()};
    if (SettleJobs(_jobs, 15000, dispatch, &reopened) != SettleResult::Settled || !terminal() ||
        _pixelMoveWorkTestGate->visits.load() != 2 || _pixelMoveWorkTestGate->openingPreparations.load() != 2 ||
        _pixelMoveWorkTestGate->openingReuses.load() != 0 || !_pixelMoveOpening || !_session.PendingPixelMove() ||
        _pixelMoveOpeningId != _session.PendingPixelMove()->opening || _session.history.UndoCount() != 0)
        return failure(__LINE__);
    const std::weak_ptr<const PixelMoveOpening> cancelledOpening = _pixelMoveOpening;
    CancelPixelDrag();
    if (!settle() || _pixelMoveOpening || !cancelledOpening.expired())
        return failure(__LINE__);
    std::printf("PASS pixelmove-host.reopen-refuses-old-opening-and-cancel-retires-cache\n");
    CancelPixelDrag();
    if (!settle())
        return failure(__LINE__);
    _pixelMoveWorkTestGate.reset();
    if (!open() || !settle())
        return failure(__LINE__);
    CancelPixelDrag();
    if (!gate(PixelMoveWorkTestKind::Commit, Mode::Admission, false))
        return failure(__LINE__);
    const int admissionErrors = _errorsShown;
    const Raster admissionSource = _session.ActiveLayer()->asset->image;
    const auto refusedNudge = QueueHostInput(HostPixelNudgeInput{Vec2{1, 0}});
    if (!settle() || refusedNudge.Read().outcome != HostOutcome::Failed || refusedNudge.Read().error != E_OUTOFMEMORY ||
        _errorsShown != admissionErrors + 1 || _session.ActiveLayer()->asset->image != admissionSource ||
        _session.history.UndoCount() != 0 || _session.PendingPixelMove() || !terminal())
        return failure(__LINE__);
    std::printf("PASS pixelmove-host.nudge-admission-failure-notice-preserves-source\n");
    _pixelMoveWorkTestGate.reset();
    _session.CreateDocument(2048, 2048, true);
    const Raster largeImage = Raster::Make(2048, 2048, RasterFormat::Bgra8888);
    EditLayerRecordsForTest(_session,
                            [&](DocumentFixture& document)
                            {
                                auto& layer = document.layers.front();
                                layer.asset = RasterAsset{largeImage, Raster::MakeThumbnail(largeImage), layer.name};
                                document.selection = DocumentSelection{false, {{{1, 0}, {2, 0}, {2, 1}, {1, 1}}}};
                            });
    _session.history.Reset();
    if (!largeImage.HasStorage() || !gate(PixelMoveWorkTestKind::Commit, Mode::LateCancel, true) ||
        !_session.BeginDeferredPixelMove())
        return failure(__LINE__);
    _session.MovePixels(1, 0); // metadata only: do not launch an unrelated preview in this commit-retirement control
    const auto largeReceipt = QueueHostInput(HostPixelMoveApplyInput{_session.PendingPixelMove()->opening});
    State large{*this,        Mode::LateCancel, _pixelMoveWorkTestGate,
                largeReceipt, largeImage,       _session.history.CurrentRevision()};
    if (SettleJobs(_jobs, 15000, dispatch, &large) != SettleResult::Settled || !large.held || !large.acknowledged ||
        largeReceipt.Read().outcome != HostOutcome::Cancelled || !_pixelMoveWorkTestGate->payloadReleaseRequired ||
        !_pixelMoveWorkTestGate->payloadRetired || !_pixelMoveWorkTestGate->payloadRetiredOffOwner ||
        _session.ActiveLayer()->asset->image != largeImage || _session.history.UndoCount() != 0 || !terminal())
        return failure(__LINE__);
    std::printf("PASS pixelmove-host.large-cancelled-graph-retires-off-owner\n");
    _pixelMoveWorkTestGate.reset();
    _session.CreateDocument(8, 1, true);
    _session.history.Reset();
    _session.EditForTest([](CanvasDocument& document)
                         { document.selection = DocumentSelection{false, {{{20, 0}, {21, 0}, {21, 1}, {20, 1}}}}; });
    const auto noPixels = QueueHostInput(HostPixelNudgeInput{Vec2{1, 0}});
    if (!settle() || noPixels.Read().outcome != HostOutcome::Succeeded || _session.history.UndoCount() != 0 ||
        !terminal())
        return failure(__LINE__);
    std::printf("PASS pixelmove-host.no-selected-source-region-preserves-history\n");
    return 0;
}
