#include "Application.h"

#include "../Tests/Support/DocumentFixture.h"

#include "ChromeHost.h"
#include "GradientWorkTestGate.h"

#include <array>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>

int Application::RunGradientWorkSelfTest() noexcept
{
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
        StaleMetadata,
        StaleGeneration,
        StaleSelection,
        StaleTarget,
        Restore,
        Replace,
        Opening,
        UndoPending,
        Coalesce,
        PaletteRefresh,
        Mask,
    };
    struct State final
    {
        Application& owner;
        Mode mode;
        std::shared_ptr<GradientWorkTestGate> gate;
        ContinuationReceipt receipt;
        Guid revision;
        Raster source;
        std::uint64_t opening = 0;
        bool visited = false;
        bool held = false;
        bool retained = false;
        bool changedPalette = false;
        bool firstPreviewShown = false;
    };
    const auto dispatch = [](void* context, void* native) noexcept
    {
        auto& state = *static_cast<State*>(context);
        auto& owner = state.owner;
        auto& message = *static_cast<MSG*>(native);
        if (message.message != GradientWorkTestGate::kMessage)
        {
            owner.DispatchQueuedMessage(message);
            return;
        }

        state.visited = true;
        const bool committing = state.gate->kind == GradientWorkTestKind::Commit;
        const bool reservationMatches =
            state.gate->captureReservationHeld && owner._jobs.ReservedMemoryBytes() == state.gate->reservationBytes;
        state.held =
            reservationMatches && state.gate->visits.load() != 0 &&
            (committing ? owner._gradientCommitTicket && owner._gradientCommitStamp && owner._historyWork.Busy() &&
                              owner._hostCommitStamp == owner._gradientCommitStamp
                        : owner._gradientPreviewTicket.has_value()) &&
            (!committing || (owner._activeHostRequest && state.receipt.Read().outcome == HostOutcome::Pending &&
                             owner._session.history.CurrentRevision() == state.revision &&
                             owner._session.ActiveLayer()->asset->image == state.source));

        // A view-only command must remain usable while a commit owns the selected host ordinal.
        const auto presents = owner._renderer.PresentCount();
        owner.OnCommand(ChromeHost::kCommandZoomIn);
        state.held = state.held && owner._renderer.PresentCount() == presents;

        switch (state.mode)
        {
        case Mode::Cancel:
            owner.CancelGradient();
            state.retained = (!committing || state.receipt.Read().outcome == HostOutcome::Pending) &&
                             (owner._gradientPreviewTicket || owner._gradientCommitTicket);
            break;
        case Mode::LateCancel:
            (void)owner._historyWork.RequestCancel();
            state.retained =
                state.receipt.Read().outcome == HostOutcome::Pending && owner._gradientCommitTicket.has_value();
            break;
        case Mode::StaleGeneration:
            if (committing)
                owner._session.SetOpacity(0.5);
            else
                owner._session.EditForTest([](CanvasDocument&) {});
            break;
        case Mode::StaleSelection:
            owner._session.SelectAll();
            owner._session.Deselect();
            break;
        case Mode::StaleTarget:
            EditLayerRecordsForTest(owner._session,
                                    [&](DocumentFixture& document)
                                    {
                                        for (auto& layer : document.layers)
                                            if (layer.id == owner._session.ActiveLayerID())
                                                layer.transform.origin.x += 1;
                                    });
            break;
        case Mode::Restore:
            owner._session.Undo();
            owner._session.Redo();
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
        case Mode::StaleMetadata:
            owner._session.SetBackgroundColor(0xFF00FF00u);
            break;
        case Mode::Opening:
            ++owner._gradientPreviewOpening;
            break;
        case Mode::UndoPending:
            owner.OnCommand(ChromeHost::kCommandUndo);
            break;
        case Mode::Coalesce:
            // The active worker is held after preparation. Move the endpoint once; the mailbox must retain only
            // this latest request and publish it after the active preview is acknowledged.
            if (state.gate->visits.load() == 1)
            {
                owner.BeginGradientDrag(owner._viewport.ViewFromDocument(Vec2{3.5, 0.5}, owner.DocumentSize()));
                owner.DragGradient(owner._viewport.ViewFromDocument(Vec2{2.5, 0.5}, owner.DocumentSize()), 0);
                owner.EndGradientDrag();
            }
            else if (state.gate->visits.load() == 2)
            {
                const auto* pending = owner._session.PendingGradient();
                if (pending && pending->raster)
                {
                    const auto shown = pending->raster->PaintSnapshot();
                    std::array<std::uint8_t, 16> bytes{};
                    if (shown)
                        shown->image.ReadRegion(0, 0, 4, 1, bytes.data(), 16, 0);
                    state.firstPreviewShown =
                        shown && pending->publishedSequence < pending->sequence &&
                        bytes == std::array<std::uint8_t, 16>{0,   0,   0,   255, 85,  85,  85,  255,
                                                              170, 170, 170, 255, 255, 255, 255, 255};
                }
            }
            break;
        case Mode::PaletteRefresh:
            if (state.gate->visits.load() == 1)
            {
                owner.OnCommand(ChromeHost::kCommandSwapPalette);
                owner.OnCommand(ChromeHost::kCommandResetPalette);
            }
            break;
        case Mode::Success:
        case Mode::Failure:
        case Mode::Allocation:
        case Mode::Admission:
        case Mode::Capture:
        case Mode::Invalid:
        case Mode::Memory:
        case Mode::TooLarge:
        case Mode::Unchanged:
        case Mode::Mask:
            break;
        }

        if (committing && state.mode == Mode::Success)
        {
            // Production callbacks preserve the frozen commit; forced session mutation is StaleMetadata.
            owner.OnPickerColor(PickerTarget::Foreground, ChromeEditPhase::Commit, 0xFFFF0000u);
            state.changedPalette = owner._session.ForegroundColor() == 0xFF000000u;
        }
        (void)SetEvent(state.gate->release.get());
    };
    const auto cleanup = wil::scope_exit(
        [&]() noexcept
        {
            if (_gradientWorkTestGate)
                (void)SetEvent(_gradientWorkTestGate->release.get());
            if (SettleJobs(_jobs, 15000, DispatchMessageThunk, this) != SettleResult::Settled)
            {
                SelfTestFailure(L"Gradient cleanup could not settle borrowed harness state.\n");
                std::_Exit(5);
            }
            _gradientWorkTestGate.reset();
            CancelGradient();
            CancelGradientJobs();
        });

    const auto terminal = [&]() noexcept
    {
        return !_gradientPreviewTicket && !_gradientCommitTicket && !_gradientCommitStamp && !_historyWork.Busy() &&
               !_activeHostRequest && _hostRequests.Size() == 0 && !_hostInputBudget.Used() &&
               _jobs.ReservedMemoryBytes() == 0 && _jobs.ReleasingMemoryBytes() == 0;
    };
    const auto startLine = [&](bool mask, bool restorable, bool longAssetNames = false) noexcept
    {
        _session.CreateDocument(4, 1);
        const Guid layer = InsertPaintedLayer(_session, L"Gradient source", 4, 1, Vec2{0, 0}, 0.0, 0xFF000000u);
        (void)InsertPaintedLayer(_session, L"Other gradient target", 4, 1, Vec2{0, 0}, 0.0, 0xFF000000u);
        _session.SelectLayer(layer);
        if (mask)
        {
            EditLayerRecordsForTest(_session,
                                    [&](DocumentFixture& document)
                                    {
                                        const std::array<std::uint8_t, 4> zero{};
                                        const Raster image = Raster::Make(4, 1, RasterFormat::Gray8, zero.data(), 4);
                                        for (ImageLayer& candidate : document.layers)
                                        {
                                            if (candidate.id == layer)
                                            {
                                                candidate.mask = LayerMask{RasterAsset{image, image, L"Gradient mask"}};
                                                break;
                                            }
                                        }
                                    });
            _session.SelectLayerTarget(layer, true);
            _session.maskPaintWhite = false;
        }
        else
            _session.SelectLayerTarget(layer, false);
        std::wstring imageAssetName;
        std::wstring maskAssetName;
        constexpr std::size_t kAssetNameUnits = 1'048'576;
        if (longAssetNames)
        {
            imageAssetName.assign(kAssetNameUnits, L'P');
            maskAssetName.assign(kAssetNameUnits, L'M');
            EditLayerRecordsForTest(_session,
                                    [&](DocumentFixture& document)
                                    {
                                        for (ImageLayer& candidate : document.layers)
                                        {
                                            if (candidate.id == layer)
                                            {
                                                candidate.asset->name = imageAssetName;
                                                if (candidate.mask)
                                                    candidate.mask->asset.name = maskAssetName;
                                                break;
                                            }
                                        }
                                    });
        }
        _session.SetForegroundColor(0xFF000000u);
        _session.SetBackgroundColor(0xFFFFFFFFu);
        _session.gradientSettings = GradientSettings{};
        _session.gradientSettings.style = GradientStyle::ForegroundToBackground;
        _session.gradientSettings.shape = GradientShape::Linear;
        _session.gradientSettings.opacity = 1;
        _session.history.Reset();
        if (restorable)
            _session.SetOpacity(0.5);
        _session.history.MarkSaved();
        DocumentChanged();
        BeginGradientDrag(_viewport.ViewFromDocument(Vec2{0.5, 0.5}, DocumentSize()));
        DragGradient(_viewport.ViewFromDocument(Vec2{3.5, 0.5}, DocumentSize()), 0);
        EndGradientDrag();
        return _session.PendingGradient() != nullptr;
    };
    const auto setGate = [&](GradientWorkTestKind kind, Mode mode, bool after) noexcept
    {
        _gradientWorkTestGate = std::make_shared<GradientWorkTestGate>();
        _gradientWorkTestGate->kind = kind;
        _gradientWorkTestGate->afterPreparation = after;
        _gradientWorkTestGate->throwAllocationFailure = mode == Mode::Allocation;
        _gradientWorkTestGate->throwAdmissionFailure = mode == Mode::Admission;
        _gradientWorkTestGate->throwCaptureFailure = mode == Mode::Capture;
        if (mode == Mode::Failure)
            _gradientWorkTestGate->result = {JobStatus::Failed, E_ACCESSDENIED};
        if (mode == Mode::Invalid)
            _gradientWorkTestGate->status = RasterEditStatus::Invalid;
        if (mode == Mode::Memory)
            _gradientWorkTestGate->status = RasterEditStatus::Memory;
        if (mode == Mode::TooLarge)
            _gradientWorkTestGate->status = RasterEditStatus::TooLarge;
        if (mode == Mode::Unchanged)
            _gradientWorkTestGate->status = RasterEditStatus::Unchanged;
        return static_cast<bool>(_gradientWorkTestGate->release);
    };

    for (const Mode mode : {Mode::Success, Mode::Cancel, Mode::LateCancel, Mode::Failure, Mode::Allocation,
                            Mode::Admission, Mode::Capture, Mode::Invalid, Mode::Memory, Mode::TooLarge,
                            Mode::Unchanged, Mode::StaleMetadata, Mode::StaleGeneration, Mode::StaleSelection,
                            Mode::StaleTarget, Mode::Restore, Mode::Replace, Mode::Opening, Mode::UndoPending})
    {
        const bool mask = false;
        CancelGradient();
        CancelGradientJobs();
        if (SettleJobs(_jobs, 15000, DispatchMessageThunk, this) != SettleResult::Settled ||
            !setGate(GradientWorkTestKind::Preview, Mode::Success, true) ||
            !startLine(mask, mode == Mode::Restore, mode == Mode::Success))
            return 5;
        const auto source = _session.ActiveLayer()->asset->image;
        auto revision = _session.history.CurrentRevision();
        int count = _session.history.UndoCount();
        const auto presents = _renderer.PresentCount();

        // Each line first exercises the real interactive preview lane, then applies it through the typed host
        // continuation so the commit uses the caller's selected ordinal and opening identity.
        const Mode previewMode = mode == Mode::UndoPending ? Mode::UndoPending : Mode::Success;
        State preview{*this, previewMode, _gradientWorkTestGate, {}, revision, source, _gradientPreviewOpening};
        if (SettleJobs(_jobs, 15000, dispatch, &preview) != SettleResult::Settled || !preview.visited ||
            !preview.held || _session.history.UndoCount() != count || _session.history.CurrentRevision() != revision)
            return 5;
        _gradientWorkTestGate.reset();

        if (mode == Mode::UndoPending)
        {
            if (_session.PendingGradient() != nullptr || !terminal() || _session.history.UndoCount() != count ||
                _session.history.CurrentRevision() != revision)
                return 5;
            std::printf("PASS gradient-host.undo-pending\n");
            continue;
        }

        const bool afterPreparation = mode != Mode::Cancel && mode != Mode::Allocation;
        if (!setGate(GradientWorkTestKind::Commit, mode, afterPreparation))
            return 5;
        const std::uint64_t opening = _gradientPreviewOpening;
        State state{*this, mode, _gradientWorkTestGate, {}, revision, source, opening};
        const std::uint64_t requestedOpening = mode == Mode::Opening ? opening + 1 : opening;
        state.receipt = QueueHostInput(HostGradientApplyInput{requestedOpening});
        const SettleResult settled = SettleJobs(_jobs, 15000, dispatch, &state);
        const auto result = state.receipt.Read();
        HostOutcome expected = HostOutcome::Succeeded;
        if (mode == Mode::Cancel || mode == Mode::LateCancel)
            expected = HostOutcome::Cancelled;
        else if (mode == Mode::StaleGeneration || mode == Mode::StaleSelection || mode == Mode::StaleTarget ||
                 mode == Mode::Restore || mode == Mode::Replace || mode == Mode::Opening || mode == Mode::StaleMetadata)
            expected = HostOutcome::Stale;
        else if (mode == Mode::Failure || mode == Mode::Allocation || mode == Mode::Admission ||
                 mode == Mode::Capture || mode == Mode::Invalid || mode == Mode::Memory || mode == Mode::TooLarge)
            expected = HostOutcome::Failed;

        bool correct = settled == SettleResult::Settled && result.outcome == expected && terminal() &&
                       _renderer.PresentCount() == presents &&
                       (mode == Mode::Admission || mode == Mode::Capture ? !state.visited
                        : mode == Mode::Opening                          ? !state.visited
                                                                         : state.visited && state.held);
        if (mode == Mode::Cancel || mode == Mode::LateCancel)
            correct = correct && state.retained && result.error == E_ABORT;
        if (mode == Mode::Failure)
            correct = correct && result.error == E_ACCESSDENIED;
        if (mode == Mode::Allocation || mode == Mode::Admission || mode == Mode::Capture)
            correct = correct && result.error == E_OUTOFMEMORY;
        if (mode == Mode::Invalid)
            correct = correct && result.error == E_INVALIDARG;
        if (mode == Mode::Memory)
            correct = correct && result.error == E_OUTOFMEMORY;
        if (mode == Mode::TooLarge)
            correct = correct && result.error == static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE));

        if (mode == Mode::Success)
        {
            std::array<std::uint8_t, 16> pixels{};
            _session.ActiveLayer()->asset->image.ReadRegion(0, 0, 4, 1, pixels.data(), 16, 0);
            constexpr std::array<std::uint8_t, 4> expectedGray{0, 85, 170, 255};
            for (std::size_t index = 0; index < expectedGray.size(); ++index)
            {
                const auto offset = index * 4;
                correct = correct && pixels[offset] == expectedGray[index] &&
                          pixels[offset + 1] == expectedGray[index] && pixels[offset + 2] == expectedGray[index] &&
                          pixels[offset + 3] == 255;
            }
            correct = correct && state.changedPalette && _session.history.UndoCount() == count + 1 &&
                      _session.history.UndoName() == L"Gradient";
            const auto& gate = *_gradientWorkTestGate;
            const bool rootOnlyCaptured = gate.rootOnlyCapturedOnOwner;
            const bool targetCopiedOnWorker =
                gate.targetCapturesOnOwner.load() == 0 && gate.targetCapturesOnWorker.load() == 1;
            const bool fullMetadataCaptured =
                gate.capturedImageNameUnits.load() == 1'048'576 && gate.capturedMaskNameUnits.load() == 0;
            std::printf("%s gradient-host.capture.root-only-owner.image-commit\n", rootOnlyCaptured ? "PASS" : "FAIL");
            std::printf("%s gradient-host.capture.target-worker-only.image-commit\n",
                        targetCopiedOnWorker ? "PASS" : "FAIL");
            std::printf("%s gradient-host.capture.full-image-name.image-commit\n",
                        fullMetadataCaptured ? "PASS" : "FAIL");
            correct = correct && rootOnlyCaptured && targetCopiedOnWorker && fullMetadataCaptured;
            _session.Undo();
            std::array<std::uint8_t, 16> restored{};
            _session.ActiveLayer()->asset->image.ReadRegion(0, 0, 4, 1, restored.data(), 16, 0);
            correct = correct && _session.history.UndoCount() == count &&
                      restored == std::array<std::uint8_t, 16>{0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255};
        }
        else if (mode == Mode::Unchanged)
            correct = correct && !_session.PendingGradient() && _session.history.UndoCount() == count &&
                      _session.history.CurrentRevision() == revision && _session.ActiveLayer()->asset->image == source;
        else if (expected == HostOutcome::Stale)
            correct = correct && result.error == E_ABORT;
        else
            correct = correct && _session.history.UndoCount() == count &&
                      _session.history.CurrentRevision() == revision && _session.ActiveLayer()->asset->image == source;

        if (!correct)
        {
            std::fprintf(stderr,
                         "gradient-host mode=%u outcome=%u expected=%u error=%08x visited=%u held=%u terminal=%u\n",
                         static_cast<unsigned int>(mode), static_cast<unsigned int>(result.outcome),
                         static_cast<unsigned int>(expected), static_cast<unsigned int>(result.error),
                         state.visited ? 1U : 0U, state.held ? 1U : 0U, terminal() ? 1U : 0U);
            SelfTestFailure(L"Gradient worker outcome, numeric result, or terminal ownership failed.\n");
            return 5;
        }
        std::printf("PASS gradient-host.mode-%u\n", static_cast<unsigned int>(mode));
        _gradientWorkTestGate.reset();
    }

    // Preview errors/cancellation also reach the actual producer and its owner acknowledgement; they must never
    // turn a private stroke into history or release a canceled ticket early.
    for (const Mode mode : {Mode::Cancel, Mode::Failure, Mode::Admission, Mode::Capture, Mode::Allocation,
                            Mode::Invalid, Mode::Memory, Mode::TooLarge, Mode::StaleTarget, Mode::StaleGeneration})
    {
        CancelGradient();
        if (SettleJobs(_jobs, 15000, DispatchMessageThunk, this) != SettleResult::Settled ||
            !setGate(GradientWorkTestKind::Preview, mode, mode != Mode::Allocation) || !startLine(false, false))
            return 5;
        const auto revision = _session.history.CurrentRevision();
        const auto source = _session.ActiveLayer()->asset->image;
        State state{*this, mode, _gradientWorkTestGate, {}, revision, source, _gradientPreviewOpening};
        if (SettleJobs(_jobs, 15000, dispatch, &state) != SettleResult::Settled || !terminal() ||
            _session.history.UndoCount() != 0 || _session.history.CurrentRevision() != revision ||
            _session.ActiveLayer()->asset->image != source ||
            (mode == Mode::Admission || mode == Mode::Capture ? state.visited : !state.visited || !state.held) ||
            (mode == Mode::Cancel && !state.retained) ||
            (_session.PendingGradient() && _session.PendingGradient()->raster))
        {
            SelfTestFailure(L"A refused/canceled/stale gradient preview published pixels or lost ownership.\n");
            return 5;
        }
        std::printf("PASS gradient-host.preview-refusal-%u\n", static_cast<unsigned int>(mode));
        _gradientWorkTestGate.reset();
    }

    // The mask lane keeps its Gray8 target and records a single named history entry with exact endpoint bytes.
    CancelGradient();
    CancelGradientJobs();
    if (SettleJobs(_jobs, 15000, DispatchMessageThunk, this) != SettleResult::Settled ||
        !setGate(GradientWorkTestKind::Preview, Mode::Mask, true) || !startLine(true, false, true))
        return 5;
    State maskPreview{*this,
                      Mode::Mask,
                      _gradientWorkTestGate,
                      {},
                      _session.history.CurrentRevision(),
                      _session.ActiveLayer()->asset->image,
                      _gradientPreviewOpening};
    if (SettleJobs(_jobs, 15000, dispatch, &maskPreview) != SettleResult::Settled || !maskPreview.visited ||
        !maskPreview.held)
        return 5;
    _gradientWorkTestGate.reset();
    if (!setGate(GradientWorkTestKind::Commit, Mode::Mask, true))
        return 5;
    State maskCommit{*this,
                     Mode::Mask,
                     _gradientWorkTestGate,
                     {},
                     _session.history.CurrentRevision(),
                     _session.ActiveLayer()->asset->image,
                     _gradientPreviewOpening};
    maskCommit.receipt = QueueHostInput(HostGradientApplyInput{_gradientPreviewOpening});
    if (SettleJobs(_jobs, 15000, dispatch, &maskCommit) != SettleResult::Settled ||
        maskCommit.receipt.Read().outcome != HostOutcome::Succeeded || !maskCommit.visited || !maskCommit.held ||
        !terminal() || !_session.ActiveLayer()->mask || _session.history.UndoName() != L"Gradient Mask")
        return 5;
    const auto& maskGate = *_gradientWorkTestGate;
    const bool maskRootOnlyCaptured = maskGate.rootOnlyCapturedOnOwner;
    const bool maskTargetCopiedOnWorker =
        maskGate.targetCapturesOnOwner.load() == 0 && maskGate.targetCapturesOnWorker.load() == 1;
    const bool maskFullMetadataCaptured =
        maskGate.capturedImageNameUnits.load() == 1'048'576 && maskGate.capturedMaskNameUnits.load() == 1'048'576;
    std::printf("%s gradient-host.capture.root-only-owner.mask-commit\n", maskRootOnlyCaptured ? "PASS" : "FAIL");
    std::printf("%s gradient-host.capture.target-worker-only.mask-commit\n",
                maskTargetCopiedOnWorker ? "PASS" : "FAIL");
    std::printf("%s gradient-host.capture.full-image-and-mask-names.mask-commit\n",
                maskFullMetadataCaptured ? "PASS" : "FAIL");
    if (!maskRootOnlyCaptured || !maskTargetCopiedOnWorker || !maskFullMetadataCaptured)
    {
        SelfTestFailure(L"Gradient mask target capture did not remain worker-owned or preserve full metadata.\n");
        return 5;
    }
    std::array<std::uint8_t, 4> maskPixels{};
    _session.ActiveLayer()->mask->asset.image.ReadRegion(0, 0, 4, 1, maskPixels.data(), 4, 255);
    if (maskPixels != std::array<std::uint8_t, 4>{0, 85, 170, 255})
        return 5;
    std::printf("PASS gradient-host.gray8-mask\n");
    _gradientWorkTestGate.reset();

    // Swap and reset only refresh the pending request; the latest settings sequence is the one published.
    CancelGradient();
    CancelGradientJobs();
    if (SettleJobs(_jobs, 15000, DispatchMessageThunk, this) != SettleResult::Settled ||
        !setGate(GradientWorkTestKind::Preview, Mode::PaletteRefresh, true) || !startLine(false, false))
        return 5;
    const int paletteHistory = _session.history.UndoCount();
    State paletteRefresh{*this,
                         Mode::PaletteRefresh,
                         _gradientWorkTestGate,
                         {},
                         _session.history.CurrentRevision(),
                         _session.ActiveLayer()->asset->image,
                         _gradientPreviewOpening};
    if (SettleJobs(_jobs, 15000, dispatch, &paletteRefresh) != SettleResult::Settled || !paletteRefresh.visited ||
        !paletteRefresh.held || _gradientWorkTestGate->visits.load() < 2 || !terminal() ||
        _session.PendingGradient() == nullptr || _gradientSeenSequence != _session.PendingGradient()->sequence ||
        _session.history.UndoCount() != paletteHistory)
        return 5;
    std::printf("PASS gradient-host.palette-refresh-only\n");
    _gradientWorkTestGate.reset();

    // A canceled gradient prerequisite prevents a dependent invert from running under its selected ordinal.
    CancelGradient();
    CancelGradientJobs();
    if (SettleJobs(_jobs, 15000, DispatchMessageThunk, this) != SettleResult::Settled ||
        !setGate(GradientWorkTestKind::Preview, Mode::Success, true) || !startLine(false, false))
        return 5;
    const auto dependentSource = _session.ActiveLayer()->asset->image;
    const auto dependentRevision = _session.history.CurrentRevision();
    const int dependentHistory = _session.history.UndoCount();
    State dependentPreview{*this,           Mode::Success,          _gradientWorkTestGate, {}, dependentRevision,
                           dependentSource, _gradientPreviewOpening};
    if (SettleJobs(_jobs, 15000, dispatch, &dependentPreview) != SettleResult::Settled || !dependentPreview.visited ||
        !dependentPreview.held)
        return 5;
    _gradientWorkTestGate.reset();
    if (!setGate(GradientWorkTestKind::Commit, Mode::Cancel, false))
        return 5;
    State dependent{*this,           Mode::Cancel,           _gradientWorkTestGate, {}, dependentRevision,
                    dependentSource, _gradientPreviewOpening};
    dependent.receipt = QueueHostInput(HostCommandInput{ChromeHost::kCommandInvert});
    if (SettleJobs(_jobs, 15000, dispatch, &dependent) != SettleResult::Settled ||
        dependent.receipt.Read().outcome != HostOutcome::Cancelled || !dependent.retained || !terminal() ||
        _session.history.UndoCount() != dependentHistory || _session.history.CurrentRevision() != dependentRevision ||
        _session.ActiveLayer()->asset->image != dependentSource)
        return 5;
    std::printf("PASS gradient-host.dependent-invert-cancel\n");
    _gradientWorkTestGate.reset();

    // A captured preview keeps its first immutable input while one changed endpoint is coalesced behind it.
    CancelGradient();
    CancelGradientJobs();
    if (SettleJobs(_jobs, 15000, DispatchMessageThunk, this) != SettleResult::Settled ||
        !setGate(GradientWorkTestKind::Preview, Mode::Coalesce, true) || !startLine(false, false))
        return 5;
    State coalesced{*this,
                    Mode::Coalesce,
                    _gradientWorkTestGate,
                    {},
                    _session.history.CurrentRevision(),
                    _session.ActiveLayer()->asset->image,
                    _gradientPreviewOpening};
    const auto coalescedSettled = SettleJobs(_jobs, 15000, dispatch, &coalesced);
    if (coalescedSettled != SettleResult::Settled || !coalesced.visited || !coalesced.held ||
        !coalesced.firstPreviewShown || _gradientWorkTestGate->visits.load() != 2 || !terminal() ||
        _session.history.UndoCount() != 0 || !_session.PendingGradient() ||
        _session.PendingGradient()->publishedSequence != _session.PendingGradient()->sequence)
    {
        const auto* pending = _session.PendingGradient();
        std::fprintf(stderr,
                     "gradient-host.coalesced settled=%u visited=%u held=%u first-shown=%u visits=%u terminal=%u "
                     "undo=%d pending=%u published=%llu sequence=%llu\n",
                     static_cast<unsigned int>(coalescedSettled), coalesced.visited ? 1U : 0U, coalesced.held ? 1U : 0U,
                     coalesced.firstPreviewShown ? 1U : 0U, _gradientWorkTestGate->visits.load(), terminal() ? 1U : 0U,
                     _session.history.UndoCount(), pending ? 1U : 0U,
                     static_cast<unsigned long long>(pending ? pending->publishedSequence : 0),
                     static_cast<unsigned long long>(pending ? pending->sequence : 0));
        SelfTestFailure(L"The coalesced gradient preview lost its held intermediate publication or ownership.\n");
        return 5;
    }
    const auto latest = _session.PendingGradient()->raster->PaintSnapshot();
    std::array<std::uint8_t, 16> latestBytes{};
    if (latest)
        latest->image.ReadRegion(0, 0, 4, 1, latestBytes.data(), 16, 0);
    if (!latest || latestBytes != std::array<std::uint8_t, 16>{0, 0, 0, 255, 128, 128, 128, 255, 255, 255, 255, 255,
                                                               255, 255, 255, 255})
    {
        std::fprintf(stderr, "gradient-host.coalesced latest=%u bytes=", latest ? 1U : 0U);
        for (const auto byte : latestBytes)
            std::fprintf(stderr, "%u,", static_cast<unsigned int>(byte));
        std::fprintf(stderr, "\n");
        SelfTestFailure(L"The coalesced gradient preview did not match the independent latest endpoint bytes.\n");
        return 5;
    }
    std::printf("PASS gradient-host.latest-preview\n");
    _gradientWorkTestGate.reset();
    return 0;
}
