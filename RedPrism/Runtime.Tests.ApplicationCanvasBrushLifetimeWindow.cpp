#include "Application.h"
#include <cstddef>

#include "ChromeHost.h"
#include "ProjectStore.h"
#include "Runtime.Tests.LayerMetadataTestGate.h"
#include "Tests/Support/Support.Tests.DocumentFixture.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>
#include <string>
#include <vector>

int Application::RunCanvasBrushLifetimeWindowSelfTest() noexcept
{
    const auto viewport = _viewport;
    const auto path = _documentPath;
    const auto settings = _session.brushSettings;
    const auto maskWhite = _session.maskPaintWhite;
    const auto mode = _session.brushMode;
    const auto tool = _tool;
    const auto forced = _forcedDiscardAnswer;
    const auto errors = _errorsShown;
    const auto lastError = _lastErrorId;
    const bool pan = _spacePan;
    const bool originalEndSessionPending = _endSessionPending;
    const bool originalEndSessionPromptDeferred = _endSessionPromptDeferred;
    const auto originalEndSessionGeneration = _endSessionGeneration;
    const bool originalCloseConfirmed = _closeConfirmed;
    const bool originalCloseDeferred = _closeDeferred;

    struct Case final
    {
        const char* name;
        bool endSession;
        bool live;
        bool save;
        bool failWrite;
    };
    constexpr std::array cases{
        Case{"window-close-save-held", false, false, true, false},
        Case{"window-close-save-live", false, true, true, false},
        Case{"end-session-save-held", true, false, true, false},
        Case{"end-session-save-live", true, true, true, false},
        Case{"window-close-discard-held", false, false, false, false},
        Case{"window-close-discard-live", false, true, false, false},
        Case{"end-session-discard-held", true, false, false, false},
        Case{"end-session-discard-live", true, true, false, false},
        Case{"window-close-write-failure-held", false, false, true, true},
        Case{"window-close-write-failure-live", false, true, true, true},
        Case{"end-session-write-failure-held", true, false, true, true},
        Case{"end-session-write-failure-live", true, true, true, true},
    };

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4623 4626 5027)
    struct Pump final
    {
        Application& owner;
        unsigned int closePosts = 0;
        const ContinuationReceipt* stroke = nullptr;
        const ContinuationReceipt* dependent = nullptr;
        bool closeAfterAcknowledgement = false;
    };
#pragma warning(pop)

    Pump pump{*this};

    const auto dispatch = [](void* context, void* nativeMessage) noexcept
    {
        auto& state = *static_cast<Pump*>(context);
        auto& message = *static_cast<MSG*>(nativeMessage);
        if (message.message == WM_CLOSE && message.hwnd == state.owner._window.get())
        {
            ++state.closePosts;
            state.closeAfterAcknowledgement = state.stroke && state.dependent &&
                                              state.stroke->Read().outcome != HostOutcome::Pending &&
                                              state.dependent->Read().outcome == HostOutcome::Succeeded;
            return; // SettleJobs removed this harness close; keep the parent self-test window alive.
        }
        DispatchMessageThunk(&state.owner, nativeMessage);
    };
    const auto settle = [this, &dispatch, &pump]() noexcept
    { return SettleJobs(_jobs, 15000, dispatch, &pump) == SettleResult::Settled; };
    const auto drainClose = [this, &pump]() noexcept
    {
        MSG message{};
        while (_window && PeekMessageW(&message, _window.get(), WM_CLOSE, WM_CLOSE, PM_REMOVE) != FALSE)
        {
            ++pump.closePosts;
            pump.closeAfterAcknowledgement = pump.stroke && pump.dependent &&
                                             pump.stroke->Read().outcome != HostOutcome::Pending &&
                                             pump.dependent->Read().outcome == HostOutcome::Succeeded;
        }
    };

    const auto cleanup = wil::scope_exit(
        [this, &settle, &drainClose, &pump, viewport, &path, settings, maskWhite, mode, tool, forced, errors, lastError,
         pan, originalEndSessionPending, originalEndSessionPromptDeferred, originalCloseConfirmed,
         originalCloseDeferred]() noexcept
        {
            pump.stroke = nullptr;
            pump.dependent = nullptr;
            pump.closeAfterAcknowledgement = false;
            if (_layerMetadataTestGate)
                (void)SetEvent(_layerMetadataTestGate->release.get());
            (void)CancelCanvasBrush();
            CancelHostContinuations();
            if (!settle())
                std::_Exit(5);
            drainClose();
            _layerMetadataTestGate.reset();
            _opacityEdit.reset();
            _opacityRecorded.reset();
            _canvasBrushReceipt = {};
            if (_window)
                (void)ShutdownBlockReasonDestroy(_window.get());
            if (!settle())
                std::_Exit(5);
            drainClose();
            _endSessionPending = originalEndSessionPending;
            _endSessionPromptDeferred = originalEndSessionPromptDeferred;
            _closeConfirmed = originalCloseConfirmed;
            _closeDeferred = originalCloseDeferred;
            _viewport = viewport;
            _documentPath = path;
            _session.brushSettings = settings;
            _session.maskPaintWhite = maskWhite;
            _session.brushMode = mode;
            _tool = tool;
            _forcedDiscardAnswer = forced;
            _errorsShown = errors;
            _lastErrorId = lastError;
            _spacePan = pan;
            _drag = PointerDrag{};
        });

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4623 4626 5027)
    struct State final
    {
        Application& owner;
        Pump& pump;
        Case scenario;
        Guid target;
        std::uint64_t serial;
        std::uint64_t generationBefore = 0;
        std::uint64_t generation = 0;
        ContinuationReceipt stroke;
        ContinuationReceipt dependent;
        bool held = false;
        bool admission = false;
        bool decision = false;
        bool pendingUntilAck = false;
    };
#pragma warning(pop)

    static constexpr auto pointer = [](Application& owner, UINT message, WPARAM modifiers, Vec2 point) noexcept
    {
        const auto hole = owner._chrome->HolePixels();
        const auto view = owner._viewport.ViewFromDocument(point, owner.DocumentSize());
        owner.OnPointer(message, modifiers,
                        MAKELPARAM(static_cast<int>(std::lround(hole.left + view.x)),
                                   static_cast<int>(std::lround(hole.top + view.y))));
    };
    static constexpr auto admit = [](State& state) noexcept
    {
        auto& owner = state.owner;
        const auto opening = owner._canvasBrushInput.Opening();
        const auto count = owner._canvasBrushInput.PendingCount();
        const auto core = owner._session.PendingBrushOpening();
        state.pump.stroke = &state.stroke;
        state.pump.dependent = &state.dependent;
        state.generationBefore = owner._endSessionGeneration;
        if (state.scenario.endSession)
        {
            if (owner._endSessionGeneration == std::numeric_limits<std::uint64_t>::max())
                return false;
            owner._endSessionPending = true;
            ++owner._endSessionGeneration; // Monotonic even after the self-test restores pending flags.
            state.dependent = owner.QueueHostInput(HostEndSessionInput{owner._endSessionGeneration, 0});
        }
        else
        {
            state.dependent = owner.QueueHostInput(HostCloseWindowInput{});
        }
        state.generation = owner._endSessionGeneration;
        state.admission = state.dependent.Read().outcome == HostOutcome::Pending &&
                          owner._canvasBrushInput.Sampling() && owner._canvasBrushInput.PendingCount() == count &&
                          owner._session.PendingBrushOpening() == core && owner._canvasBrushInput.Opening() == opening;
        owner.RunHostContinuation(); // Start the actual typed host flow while the prior brush owns its ordinal.
        state.decision = owner._canvasLifetime && owner._canvasLifetime->entry.documentSerial == state.serial &&
                         owner._canvasLifetime->brushOpening == opening && !owner._canvasLifetime->presenting &&
                         owner._canvasLifetime->save == state.scenario.save && !owner._canvasBrushInput.Sampling();
        state.pendingUntilAck =
            state.dependent.Read().outcome == HostOutcome::Pending && owner._session.DocumentSerial() == state.serial;
        // The accepted action owns the current prefix; later physical samples complete the same stream receipt.
        pointer(owner, WM_MOUSEMOVE, MK_LBUTTON, {20, 8});
        pointer(owner, WM_LBUTTONUP, 0, {20, 8});
        return true;
    };
    const auto gatePump = [](void* context, void* nativeMessage) noexcept
    {
        auto& state = *static_cast<State*>(context);
        auto& owner = state.owner;
        auto& message = *static_cast<MSG*>(nativeMessage);
        if (message.message == WM_CLOSE && message.hwnd == owner._window.get())
        {
            // Match SettleJobs' dispatcher: consume this harness close, never destroy the parent window.
            ++state.pump.closePosts;
            state.pump.closeAfterAcknowledgement = state.pump.stroke && state.pump.dependent &&
                                                   state.pump.stroke->Read().outcome != HostOutcome::Pending &&
                                                   state.pump.dependent->Read().outcome == HostOutcome::Succeeded;
            return;
        }
        if (message.message != LayerMetadataTestGate::kMessage)
        {
            DispatchMessageThunk(&owner, nativeMessage);
            return;
        }
        const auto gate = owner._layerMetadataTestGate;
        state.held = owner._layerMetadataTicket && owner._historyWork.Busy() && gate->captures.load() == 1 &&
                     gate->reservationHeld.load() && !owner._session.PendingBrushOpening();
        if (!state.scenario.live)
        {
            pointer(owner, WM_MOUSEMOVE, MK_LBUTTON, {12, 8});
            (void)admit(state);
        }
        gate->hold = false;
        (void)SetEvent(gate->release.get());
    };

    constexpr std::array<std::uint8_t, 4> sourcePixel{19, 71, 129, 255};
    constexpr std::array<std::uint8_t, 4> redPixel{0, 0, 255, 255};
    const auto sample = [](const Raster& raster, int x, int y) noexcept
    {
        std::array<std::uint8_t, 4> result{};
        raster.ReadRegion(x, y, 1, 1, result.data(), 4, 0);
        return result;
    };
    int controls = 0;
    int failures = 0;
    bool allPassed = true;
    const auto expect = [&controls, &failures](const char* scenario, const char* name, bool value) noexcept
    {
        ++controls;
        failures += value ? 0 : 1;
        std::fprintf(stderr, "%s canvas-brush-lifetime-window.%s.%s\n", value ? "PASS" : "FAIL", scenario, name);
        return value;
    };

    wchar_t temporary[MAX_PATH]{};
    if (!GetTempPathW(MAX_PATH, temporary))
        return 5;
    if (originalEndSessionGeneration > std::numeric_limits<std::uint64_t>::max() - 6)
        return 5;
    MSG queuedClose{};
    if (originalEndSessionPending || originalEndSessionPromptDeferred || originalCloseConfirmed ||
        originalCloseDeferred || PeekMessageW(&queuedClose, _window.get(), WM_CLOSE, WM_CLOSE, PM_NOREMOVE) != FALSE)
        return 5;
    int index = 0;
    for (const auto& scenario : cases)
    {
        if (!settle() || _closeDeferred || _closeConfirmed || _endSessionPending || _endSessionPromptDeferred)
            return 5;
        pump.closePosts = 0;
        pump.stroke = nullptr;
        pump.dependent = nullptr;
        pump.closeAfterAcknowledgement = false;
        _session.ClearDocument();
        _session.CreateDocument(32, 24, true);
        _session.AddBlankLayer();
        const Guid target = _session.ActiveLayerID().value();
        std::vector<std::uint8_t> bytes(32 * 24 * 4);
        for (std::size_t offset = 0; offset < bytes.size(); offset += 4)
            std::copy(sourcePixel.begin(), sourcePixel.end(), bytes.begin() + static_cast<std::ptrdiff_t>(offset));
        const auto source = Raster::Make(32, 24, RasterFormat::Bgra8888, bytes.data(), bytes.size());
        EditLayerRecordsForTest(_session,
                                [&](DocumentFixture& document)
                                {
                                    for (auto& layer : document.layers)
                                        if (layer.id == target)
                                            layer.asset = RasterAsset{source, source, L"Lifetime source"};
                                });
        _session.history.Reset();
        _session.history.MarkSaved();
        _opacityEdit.reset();
        _opacityRecorded.reset();
        _layerMetadataTestGate.reset();
        _tool = EditorTool::Brush;
        _spacePan = false;
        _drag = PointerDrag{};
        _session.brushMode = BrushToolMode::Paint;
        _session.brushSettings = BrushSettings{3, 1, 1, 0, 0, 1};
        _viewport.zoom = 1;
        _viewport.pan = {};
        _viewport.followsFit = false;
        const auto output = std::wstring(temporary) + L"RedPrism-canvas-lifetime-window-" +
                            std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(++index) +
                            (scenario.failWrite ? L"-missing/failed.prism" : L".prism");
        if (GetFileAttributesW(output.c_str()) != INVALID_FILE_ATTRIBUTES)
            return 5;
        const auto removeFile = wil::scope_exit([&output]() noexcept { (void)DeleteFileW(output.c_str()); });
        _documentPath = output;
        _forcedDiscardAnswer = scenario.save ? Prompts::Answer::First : Prompts::Answer::Second;
        DocumentChanged();
        OnLayerOpacity(.25, ChromeEditPhase::Preview);
        const bool preview =
            settle() && _opacityEdit && _session.ActiveLayer()->opacity == .25 && _session.history.UndoCount() == 0;

        _layerMetadataTestGate = std::make_shared<LayerMetadataTestGate>();
        _layerMetadataTestGate->release.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (!_layerMetadataTestGate->release)
            return 5;
        pointer(*this, WM_LBUTTONDOWN, MK_LBUTTON, {8, 8});
        State state{*this, pump, scenario, target, _session.DocumentSerial(), 0, 0, _canvasBrushReceipt};
        const bool held = SettleJobs(_jobs, 15000, gatePump, &state) == SettleResult::Settled && state.held;
        if (scenario.live)
        {
            pointer(*this, WM_MOUSEMOVE, MK_LBUTTON, {12, 8});
            if (!settle() || !_session.PendingBrushOpening())
                return 5;
            if (!admit(state))
                return 5;
        }
        if (state.dependent.Read().outcome == HostOutcome::Pending && !scenario.live && !state.admission)
            return 5;
        const bool receiptPumped = settle() && state.dependent.Read().outcome != HostOutcome::Pending;
        drainClose();
        const bool admission = state.admission;
        const bool decision = state.decision;
        const bool pendingUntilAck = state.pendingUntilAck;
        const bool originalReceiptAndOwners =
            receiptPumped &&
            state.stroke.Read().outcome == (scenario.save ? HostOutcome::Succeeded : HostOutcome::Cancelled) &&
            !_canvasLifetime && !_canvasBrushInput.Busy() && !_session.PendingBrushOpening() && !_activeHostRequest &&
            _hostRequests.Size() == 0 && _hostInputBudget.Used() == 0 && MetadataAccountingSettledForTest();

        bool exactSaved = GetFileAttributesW(output.c_str()) == INVALID_FILE_ATTRIBUTES;
        if (scenario.save && !scenario.failWrite)
        {
            const auto loaded = LoadProject(output.c_str());
            exactSaved = false;
            if (loaded.status == ProjectIoStatus::Ok)
                for (const auto& layer : loaded.document.layers)
                    if (layer.id == target && layer.asset)
                        exactSaved = layer.opacity == .25 && sample(layer.asset->image, 8, 8) == redPixel &&
                                     sample(layer.asset->image, 12, 8) == redPixel &&
                                     sample(layer.asset->image, 20, 8) == sourcePixel &&
                                     sample(layer.asset->image, 8, 11) == sourcePixel;
        }
        const auto dependentResult = state.dependent.Read();
        const bool generationAdvancedOnce = scenario.endSession ? state.generation == state.generationBefore + 1
                                                                : state.generation == state.generationBefore;
        const bool failedWrite = scenario.failWrite && dependentResult.outcome == HostOutcome::Failed &&
                                 FAILED(dependentResult.error) && pump.closePosts == 0 && !_closeConfirmed &&
                                 !_endSessionPending && generationAdvancedOnce && _session.history.UndoCount() == 2 &&
                                 _session.history.IsModified();
        const bool successfulClose = !scenario.failWrite && dependentResult.outcome == HostOutcome::Succeeded &&
                                     pump.closePosts == 1 && pump.closeAfterAcknowledgement && _closeConfirmed &&
                                     !_closeDeferred && state.stroke.Read().outcome != HostOutcome::Pending &&
                                     !_endSessionPending && generationAdvancedOnce;
        const bool routeResult = scenario.failWrite ? failedWrite : successfulClose;
        const bool sameDocument = _session.DocumentSerial() == state.serial && _session.Document().has_value() &&
                                  _session.Document()->width == 32 && _session.Document()->height == 24;

        const bool passedPreview = expect(scenario.name, "opacity-preview-is-literal-and-uncommitted", preview);
        const bool passedHeld = expect(scenario.name, "history-gate-held-before-brush-replay", held);
        const bool passedAdmission = expect(scenario.name, "typed-route-admission-preserves-stream", admission);
        const bool passedDecision = expect(scenario.name, "modal-decision-retains-exact-opening", decision);
        const bool passedPending = expect(scenario.name, "route-receipt-waits-for-brush-ack", pendingUntilAck);
        const bool passedOriginal =
            expect(scenario.name, "original-receipt-and-budget-retire", originalReceiptAndOwners);
        const bool passedPixels =
            expect(scenario.name, "saved-file-is-literal-acknowledged-prefix-or-absent", exactSaved);
        const bool passedRoute = expect(scenario.name, "typed-route-result-and-close-post-order", routeResult);
        const bool passedDocument = expect(scenario.name, "route-retains-original-document-serial", sameDocument);
        allPassed = passedPreview && passedHeld && passedAdmission && passedDecision && passedPending &&
                    passedOriginal && passedPixels && passedRoute && passedDocument && allPassed;

        _closeConfirmed = originalCloseConfirmed;
        _closeDeferred = originalCloseDeferred;
        _endSessionPending = originalEndSessionPending;
        _endSessionPromptDeferred = originalEndSessionPromptDeferred;
        _layerMetadataTestGate.reset();
        _opacityEdit.reset();
        _opacityRecorded.reset();
        pump.stroke = nullptr;
        pump.dependent = nullptr;
    }

    std::fprintf(stderr, "%s canvas-brush-lifetime-window.summary controls=%d failures=%d cases=%zu\n",
                 failures == 0 ? "PASS" : "FAIL", controls, failures, cases.size());
    return controls == 108 && failures == 0 && allPassed ? 0 : 5;
}
