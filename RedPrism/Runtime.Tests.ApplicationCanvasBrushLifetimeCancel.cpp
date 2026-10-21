#include "Application.h"

#include "ChromeHost.h"
#include "Prompts.h"
#include "Runtime.Tests.LayerMetadataTestGate.h"

#include "../Tests/Support/Support.Tests.DocumentFixture.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <variant>
#include <vector>

int Application::RunCanvasBrushLifetimeCancelSelfTest() noexcept
{
    const auto originalViewport = _viewport;
    const auto originalPath = _documentPath;
    const auto originalSettings = _session.brushSettings;
    const auto originalMaskWhite = _session.maskPaintWhite;
    const auto originalMode = _session.brushMode;
    const auto originalTool = _tool;
    const bool originalSpacePan = _spacePan;
    const PointerDrag originalDrag = _drag;
    const auto originalForcedDiscardAnswer = _forcedDiscardAnswer;
    const bool originalEndSessionPending = _endSessionPending;
    const bool originalCloseConfirmed = _closeConfirmed;
    int checks = 0;
    const auto expect = [&checks](const char* scenario, const char* name, bool value) noexcept
    {
        std::fprintf(stderr, "%s canvas-brush-lifetime-cancel.%s.%s\n", value ? "PASS" : "FAIL", scenario, name);
        checks += value ? 1 : 0;
        return value;
    };
    const auto settle = [this]() noexcept
    { return SettleJobs(_jobs, 15000, DispatchMessageThunk, this) == SettleResult::Settled; };
    const auto cleanup = wil::scope_exit(
        [this, originalViewport, originalPath, originalSettings, originalMaskWhite, originalMode, originalTool,
         originalSpacePan, originalDrag, originalForcedDiscardAnswer, originalEndSessionPending, originalCloseConfirmed,
         &settle]() noexcept
        {
            if (_layerMetadataTestGate)
                (void)SetEvent(_layerMetadataTestGate->release.get());
            CancelHostContinuations();
            (void)CancelCanvasBrush();
            if (!settle())
                std::_Exit(5);
            _layerMetadataTestGate.reset();
            _opacityEdit.reset();
            _opacityRecorded.reset();
            _canvasBrushReceipt = {};
            _forcedDiscardAnswer = originalForcedDiscardAnswer;
            _viewport = originalViewport;
            _documentPath = originalPath;
            _session.brushSettings = originalSettings;
            _session.maskPaintWhite = originalMaskWhite;
            _session.brushMode = originalMode;
            _tool = originalTool;
            _spacePan = originalSpacePan;
            _drag = originalDrag;
            _endSessionPending = originalEndSessionPending;
            _closeConfirmed = originalCloseConfirmed;
        });

    enum class Lifecycle : std::uint8_t
    {
        New,
        CloseProject,
        CloseWindow,
        EndSession,
    };
    enum class StreamPhase : std::uint8_t
    {
        Held,
        Live,
    };
    enum class CancelRoute : std::uint8_t
    {
        DiscardPrompt,
        SavePicker,
    };
    struct Case final
    {
        const char* name;
        Lifecycle lifecycle;
        StreamPhase phase;
        CancelRoute route;
    };
    constexpr std::array<Case, 16> cases{{
        {"new-held-discard-prompt-cancel", Lifecycle::New, StreamPhase::Held, CancelRoute::DiscardPrompt},
        {"new-held-save-picker-cancel", Lifecycle::New, StreamPhase::Held, CancelRoute::SavePicker},
        {"new-live-discard-prompt-cancel", Lifecycle::New, StreamPhase::Live, CancelRoute::DiscardPrompt},
        {"new-live-save-picker-cancel", Lifecycle::New, StreamPhase::Live, CancelRoute::SavePicker},
        {"close-project-held-discard-prompt-cancel", Lifecycle::CloseProject, StreamPhase::Held,
         CancelRoute::DiscardPrompt},
        {"close-project-held-save-picker-cancel", Lifecycle::CloseProject, StreamPhase::Held, CancelRoute::SavePicker},
        {"close-project-live-discard-prompt-cancel", Lifecycle::CloseProject, StreamPhase::Live,
         CancelRoute::DiscardPrompt},
        {"close-project-live-save-picker-cancel", Lifecycle::CloseProject, StreamPhase::Live, CancelRoute::SavePicker},
        {"close-window-held-discard-prompt-cancel", Lifecycle::CloseWindow, StreamPhase::Held,
         CancelRoute::DiscardPrompt},
        {"close-window-held-save-picker-cancel", Lifecycle::CloseWindow, StreamPhase::Held, CancelRoute::SavePicker},
        {"close-window-live-discard-prompt-cancel", Lifecycle::CloseWindow, StreamPhase::Live,
         CancelRoute::DiscardPrompt},
        {"close-window-live-save-picker-cancel", Lifecycle::CloseWindow, StreamPhase::Live, CancelRoute::SavePicker},
        {"end-session-held-discard-prompt-cancel", Lifecycle::EndSession, StreamPhase::Held,
         CancelRoute::DiscardPrompt},
        {"end-session-held-save-picker-cancel", Lifecycle::EndSession, StreamPhase::Held, CancelRoute::SavePicker},
        {"end-session-live-discard-prompt-cancel", Lifecycle::EndSession, StreamPhase::Live,
         CancelRoute::DiscardPrompt},
        {"end-session-live-save-picker-cancel", Lifecycle::EndSession, StreamPhase::Live, CancelRoute::SavePicker},
    }};

    constexpr std::array<std::uint8_t, 4> sourcePixel{19, 71, 129, 255};
    constexpr std::array<std::uint8_t, 4> redPixel{0, 0, 255, 255};
    const auto findLayer = [](const CanvasDocument& document, const Guid& id) noexcept -> const ImageLayer*
    {
        for (const auto& layer : document.layers)
            if (layer.id == id)
                return &layer;
        return nullptr;
    };
    const auto pixelAt = [this](int x, int y) noexcept
    {
        std::array<std::uint8_t, 4> pixel{};
        const auto* layer = _session.ActiveLayer();
        if (layer && layer->asset)
            layer->asset->image.ReadRegion(x - static_cast<int>(std::lround(layer->transform.origin.x)),
                                           y - static_cast<int>(std::lround(layer->transform.origin.y)), 1, 1,
                                           pixel.data(), 4, 0);
        return pixel;
    };
    static constexpr auto pointer = [](Application& owner, UINT message, WPARAM modifiers, Vec2 point) noexcept
    {
        const auto hole = owner._chrome->HolePixels();
        const auto view = owner._viewport.ViewFromDocument(point, owner.DocumentSize());
        owner.OnPointer(message, modifiers,
                        MAKELPARAM(static_cast<int>(std::lround(hole.left + view.x)),
                                   static_cast<int>(std::lround(hole.top + view.y))));
    };
    struct GateState final
    {
        Application& owner;
        const Case& scenario;
        ContinuationReceipt lifecycleReceipt;
        std::uint64_t admittedDocumentSerial = 0;
        Guid admittedTarget{};
        std::uint64_t admittedStreamOpening = 0;
        std::optional<std::uint64_t> admittedCoreOpening;
        std::optional<std::uint64_t> replayedCoreOpening;
        bool visited = false;
        bool heldWorker = false;
        bool admissionKeptSampling = false;
        bool preflightPreservedAuthority = false;
        bool closeMessageConsumed = false;
        bool preflightDispatched = false;
    };
    static constexpr auto requestLifecycle = [](GateState& state) noexcept
    {
        auto& owner = state.owner;
        state.admittedDocumentSerial = owner._session.DocumentSerial();
        state.admittedTarget = owner._session.ActiveLayerID().value();
        state.admittedStreamOpening = owner._canvasBrushInput.Opening();
        state.admittedCoreOpening = owner._session.PendingBrushOpening();
        switch (state.scenario.lifecycle)
        {
        case Lifecycle::New:
            state.lifecycleReceipt = owner.QueueHostInput(HostNewInput{});
            break;
        case Lifecycle::CloseProject:
            state.lifecycleReceipt = owner.QueueHostInput(HostCloseProjectInput{});
            break;
        case Lifecycle::CloseWindow:
            state.lifecycleReceipt = owner.QueueHostInput(HostCloseWindowInput{});
            break;
        case Lifecycle::EndSession:
            owner._endSessionPending = true;
            ++owner._endSessionGeneration;
            state.lifecycleReceipt = owner.QueueHostInput(HostEndSessionInput{owner._endSessionGeneration, 0});
            break;
        }
        state.admissionKeptSampling = owner._canvasBrushInput.Sampling() &&
                                      owner._canvasBrushReceipt.Read().outcome == HostOutcome::Pending &&
                                      state.lifecycleReceipt.Read().outcome == HostOutcome::Pending &&
                                      owner._session.DocumentSerial() == state.admittedDocumentSerial &&
                                      owner._session.ActiveLayerID() == state.admittedTarget &&
                                      owner._canvasBrushInput.Opening() == state.admittedStreamOpening &&
                                      owner._session.PendingBrushOpening() == state.admittedCoreOpening;
    };
    const auto pump = [](void* opaque, void* native) noexcept
    {
        auto& state = *static_cast<GateState*>(opaque);
        auto& owner = state.owner;
        const auto& message = *static_cast<MSG*>(native);
        if (message.message != LayerMetadataTestGate::kMessage)
        {
            DispatchMessageThunk(&owner, native);
            return;
        }
        state.visited = true;
        const auto gate = owner._layerMetadataTestGate;
        state.heldWorker = owner._layerMetadataTicket && owner._historyWork.Busy() && gate->captures.load() == 1 &&
                           gate->reservationHeld.load() && owner._canvasBrushInput.Sampling() &&
                           owner._canvasBrushReceipt.Read().outcome == HostOutcome::Pending &&
                           !owner._session.PendingBrush() && owner._session.history.UndoCount() == 0;
        if (state.scenario.phase == StreamPhase::Held)
        {
            pointer(owner, WM_MOUSEMOVE, MK_LBUTTON, {12, 8});
            requestLifecycle(state);
            // This invocation is on the owner message pump, outside a scheduler callback. Lifecycle prompts and
            // brush cancellation must be observable while the opacity worker remains behind its event gate.
            owner.RunHostContinuation();
            state.preflightDispatched =
                owner._activeHostRequest.has_value() || state.lifecycleReceipt.Read().outcome != HostOutcome::Pending;
            state.preflightPreservedAuthority = owner._canvasBrushInput.Sampling() &&
                                                owner._canvasBrushReceipt.Read().outcome == HostOutcome::Pending &&
                                                owner._session.DocumentSerial() == state.admittedDocumentSerial &&
                                                owner._session.ActiveLayerID() == state.admittedTarget &&
                                                owner._canvasBrushInput.Opening() == state.admittedStreamOpening &&
                                                owner._session.PendingBrushOpening() == state.admittedCoreOpening;
        }
        gate->hold = false;
        (void)SetEvent(gate->release.get());
    };

    for (const auto& scenario : cases)
    {
        if (!settle())
            return 5;
        _layerMetadataTestGate.reset();
        _opacityEdit.reset();
        _opacityRecorded.reset();
        _canvasBrushReceipt = {};
        _session.ClearDocument();
        _session.CreateDocument(32, 24, true);
        _session.AddBlankLayer();
        const Guid target = _session.ActiveLayerID().value();
        std::vector<std::uint8_t> bytes(32 * 24 * 4);
        for (std::size_t offset = 0; offset < bytes.size(); offset += 4)
            std::copy(sourcePixel.begin(), sourcePixel.end(), bytes.begin() + static_cast<std::ptrdiff_t>(offset));
        const Raster source = Raster::Make(32, 24, RasterFormat::Bgra8888, bytes.data(), bytes.size());
        EditLayerRecordsForTest(_session,
                                [&](DocumentFixture& document)
                                {
                                    for (auto& layer : document.layers)
                                        if (layer.id == target)
                                            layer.asset =
                                                RasterAsset{source, Raster::MakeThumbnail(source), layer.name};
                                });
        _session.history.Reset();
        _session.history.MarkSaved();
        _tool = EditorTool::Brush;
        _spacePan = false;
        _drag = PointerDrag{};
        _session.maskPaintWhite = false;
        _session.brushMode = BrushToolMode::Paint;
        _session.brushSettings = BrushSettings{3, 1, 1, 0, 0, 1};
        _viewport.zoom = 1;
        _viewport.pan = {};
        _viewport.followsFit = false;
        _documentPath.clear();
        _forcedDiscardAnswer =
            scenario.route == CancelRoute::DiscardPrompt ? Prompts::Answer::Cancel : Prompts::Answer::First;
        DocumentChanged();
        OnLayerOpacity(.25, ChromeEditPhase::Preview);
        if (!settle() ||
            !expect(scenario.name, "typed-opacity-preview-is-live-before-down",
                    _opacityEdit && _session.ActiveLayer()->opacity == .25 && _session.history.UndoCount() == 0))
            return 5;

        _layerMetadataTestGate = std::make_shared<LayerMetadataTestGate>();
        _layerMetadataTestGate->release.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (!_layerMetadataTestGate->release)
            return 5;
        const std::uint64_t sourceSerial = _session.DocumentSerial();
        pointer(*this, WM_LBUTTONDOWN, MK_LBUTTON, {8, 8});
        const std::uint64_t streamOpening = _canvasBrushInput.Opening();
        const ContinuationReceipt brushReceipt = _canvasBrushReceipt;
        GateState state{*this, scenario};
        if (!expect(scenario.name, "down-is-sampling-without-core-brush-publication",
                    _canvasBrushInput.Sampling() && _canvasBrushInput.PendingCount() == 1 &&
                        brushReceipt.Read().outcome == HostOutcome::Pending && !_session.PendingBrush() &&
                        _session.history.UndoCount() == 0) ||
            SettleJobs(_jobs, 15000, pump, &state) != SettleResult::Settled ||
            !expect(scenario.name, "opacity-worker-reached-owned-event-gate", state.visited && state.heldWorker))
            return 5;

        if (scenario.phase == StreamPhase::Live)
        {
            if (!settle() || !_session.PendingBrushOpening() || !_canvasBrushInput.Sampling() ||
                _session.history.UndoCount() != 1 || _opacityEdit)
                return 5;
            pointer(*this, WM_MOUSEMOVE, MK_LBUTTON, {12, 8});
            requestLifecycle(state);
            _forcedDiscardAnswer =
                scenario.route == CancelRoute::DiscardPrompt ? Prompts::Answer::Cancel : Prompts::Answer::First;
            RunHostContinuation();
            state.preflightDispatched =
                _activeHostRequest.has_value() || state.lifecycleReceipt.Read().outcome != HostOutcome::Pending;
            state.preflightPreservedAuthority = _canvasBrushInput.Sampling() &&
                                                _canvasBrushReceipt.Read().outcome == HostOutcome::Pending &&
                                                _session.DocumentSerial() == state.admittedDocumentSerial &&
                                                _session.ActiveLayerID() == state.admittedTarget &&
                                                _canvasBrushInput.Opening() == state.admittedStreamOpening &&
                                                _session.PendingBrushOpening() == state.admittedCoreOpening;
        }
        if (!expect(scenario.name, "host-admission-preserves-active-stream-and-authority",
                    state.admissionKeptSampling && state.admittedDocumentSerial == sourceSerial &&
                        state.admittedTarget == target && state.admittedStreamOpening == streamOpening) ||
            !expect(scenario.name, "lifecycle-cancel-preflight-preserves-admitted-authority",
                    state.preflightDispatched && state.preflightPreservedAuthority))
            return 5;

        MSG closeMessage{};
        state.closeMessageConsumed = PeekMessageW(&closeMessage, _window.get(), WM_CLOSE, WM_CLOSE, PM_REMOVE) != FALSE;
        if (!expect(scenario.name, "cancel-route-does-not-post-window-close", !state.closeMessageConsumed))
            return 5;

        if (!settle())
            return 5;
        state.replayedCoreOpening = _session.PendingBrushOpening();
        while (PeekMessageW(&closeMessage, _window.get(), WM_CLOSE, WM_CLOSE, PM_REMOVE))
            state.closeMessageConsumed = true;
        // The acknowledged typed opacity remains in history while this original opening is live.
        // Inspect without acquiring another graph/control owner; the settling helper drops its test weak refs.
        const auto metadataBytes =
            _layerMetadataTestGate->accountingOutput.expired() ? 0 : _layerMetadataTestGate->accountingOutputBytes;
        const bool metadataSettled = MetadataAccountingSettledForTest();
        const auto lifecycleResult = state.lifecycleReceipt.Read();
        const auto* unchangedLayer = _session.Document() ? findLayer(_session.Document().value(), target) : nullptr;
        const auto* captured = _canvasBrushInput.CapturedContext(streamOpening);
        const bool originalStreamSurvived =
            brushReceipt.Read().outcome == HostOutcome::Pending && _canvasBrushInput.Sampling() &&
            _session.PendingBrush() && state.replayedCoreOpening &&
            _session.PendingBrushOpening() == state.replayedCoreOpening &&
            _canvasBrushInput.Opening() == streamOpening &&
            (state.admittedCoreOpening ? _session.PendingBrushOpening() == state.admittedCoreOpening
                                       : state.replayedCoreOpening.has_value()) &&
            captured && captured->settings == BrushSettings{3, 1, 1, 0, 0, 1};
        if (!expect(scenario.name, "cancelled-lifecycle-keeps-original-brush-pending-and-live",
                    lifecycleResult.outcome == HostOutcome::Cancelled &&
                        lifecycleResult.error == static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_CANCELLED)) &&
                        originalStreamSurvived && !state.closeMessageConsumed) ||
            !expect(scenario.name, "original-request-retains-budget-after-opacity-worker-retires",
                    _hostInputBudget.Used() != 0 && _session.PendingBrush() &&
                        _session.PendingBrush()->OpeningRetainedMemoryBytes() != 0 && metadataSettled &&
                        metadataBytes > 0 && _jobs.ReservedMemoryBytes() >= metadataBytes &&
                        _jobs.ReservedMemoryBytes() - metadataBytes ==
                            _session.PendingBrush()->OpeningRetainedMemoryBytes() &&
                        !_utilityTicket && !_utilityStamp && !_historyWork.Busy() && !_layerMetadataTicket &&
                        _layerMetadataTestGate->retired.load() && _layerMetadataTestGate->retiredOffOwner.load() &&
                        !_canvasLifetime) ||
            !expect(scenario.name, "source-and-history-stay-unchanged-before-original-up",
                    _session.DocumentSerial() == sourceSerial && _session.ActiveLayerID() == target && unchangedLayer &&
                        unchangedLayer->asset && unchangedLayer->asset->image == source &&
                        _session.history.UndoCount() == 1 && _session.history.UndoName() == L"Layer Opacity" &&
                        _session.ActiveLayer()->opacity == .25 && !_opacityEdit))
            return 5;

        pointer(*this, WM_MOUSEMOVE, MK_LBUTTON, {20, 8});
        pointer(*this, WM_LBUTTONUP, 0, {20, 8});
        if (!settle())
            return 5;
        if (!expect(scenario.name, "same-receipt-commits-original-stream-against-literal-pixels",
                    _session.DocumentSerial() == sourceSerial && _session.ActiveLayerID() == target &&
                        _session.history.UndoCount() == 2 && _session.history.UndoName() == L"Brush Stroke" &&
                        pixelAt(8, 8) == redPixel && pixelAt(12, 8) == redPixel && pixelAt(20, 8) == redPixel &&
                        pixelAt(8, 11) == sourcePixel && brushReceipt.Read().outcome == HostOutcome::Succeeded &&
                        _canvasBrushReceipt.Read() == brushReceipt.Read() && !_canvasBrushInput.Busy() &&
                        !_activeHostRequest && _hostRequests.Size() == 0 && _hostInputBudget.Used() == 0 &&
                        MetadataAccountingSettledForTest() && _jobs.ReservedMemoryBytes() == metadataBytes &&
                        !_canvasLifetime))
            return 5;
    }
    std::fprintf(stderr, "PASS canvas-brush-lifetime-cancel.summary checks=%d cases=%zu expected=160\n", checks,
                 cases.size());
    return checks == 160 ? 0 : 5;
}
