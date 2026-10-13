#include "Application.h"

#include "../Tests/Support/DocumentFixture.h"
#include "ChromeHost.h"
#include "LayerMetadataTestGate.h"
#include "ModalProbePrompts.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

int Application::RunCanvasBrushLifetimeAuthoritySelfTest() noexcept
{
    const auto viewport = _viewport;
    const auto path = _documentPath;
    const auto settings = _session.brushSettings;
    const auto mode = _session.brushMode;
    const auto maskWhite = _session.maskPaintWhite;
    const auto tool = _tool;
    const auto forced = _forcedDiscardAnswer;
    const auto pendingEnd = _endSessionPending;
    const auto deferredEnd = _endSessionPromptDeferred;
    const auto pan = _spacePan;
    const auto settle = [this]() noexcept
    { return SettleJobs(_jobs, 15000, DispatchMessageThunk, this) == SettleResult::Settled; };
    const auto cleanup = wil::scope_exit(
        [&]() noexcept
        {
            if (_layerMetadataTestGate)
                (void)SetEvent(_layerMetadataTestGate->release.get());
            (void)CancelCanvasBrush();
            CancelHostContinuations();
            if (!settle())
                std::_Exit(5);
            _session.CancelBrush();
            _composition.EndStroke();
            _previewSource.reset();
            _drag = PointerDrag{};
            _layerMetadataTestGate.reset();
            _opacityEdit.reset();
            _opacityRecorded.reset();
            _canvasBrushReceipt = {};
            _viewport = viewport;
            _documentPath = path;
            _session.brushSettings = settings;
            _session.brushMode = mode;
            _session.maskPaintWhite = maskWhite;
            _tool = tool;
            _spacePan = pan;
            _forcedDiscardAnswer = forced;
            _endSessionPending = pendingEnd;
            _endSessionPromptDeferred = deferredEnd;
        });
    enum class Fault : std::uint8_t
    {
        Document,
        Core,
        EndCancelled,
        EndGeneration,
        OwnerCancelled
    };
    struct Case
    {
        const char* name;
        Fault fault;
        bool live;
        bool save;
    };
    constexpr std::array cases{Case{"document-held-save", Fault::Document, false, true},
                               Case{"document-held-discard", Fault::Document, false, false},
                               Case{"document-live-save", Fault::Document, true, true},
                               Case{"document-live-discard", Fault::Document, true, false},
                               Case{"core-held-save", Fault::Core, false, true},
                               Case{"core-held-discard", Fault::Core, false, false},
                               Case{"core-live-save", Fault::Core, true, true},
                               Case{"core-live-discard", Fault::Core, true, false},
                               Case{"end-cancelled-held-save", Fault::EndCancelled, false, true},
                               Case{"end-cancelled-held-discard", Fault::EndCancelled, false, false},
                               Case{"end-cancelled-live-save", Fault::EndCancelled, true, true},
                               Case{"end-cancelled-live-discard", Fault::EndCancelled, true, false},
                               Case{"end-generation-held-save", Fault::EndGeneration, false, true},
                               Case{"end-generation-held-discard", Fault::EndGeneration, false, false},
                               Case{"end-generation-live-save", Fault::EndGeneration, true, true},
                               Case{"end-generation-live-discard", Fault::EndGeneration, true, false},
                               Case{"owner-cancelled-held-save", Fault::OwnerCancelled, false, true},
                               Case{"owner-cancelled-held-discard", Fault::OwnerCancelled, false, false},
                               Case{"owner-cancelled-live-save", Fault::OwnerCancelled, true, true},
                               Case{"owner-cancelled-live-discard", Fault::OwnerCancelled, true, false}};
    int checks = 0;
    const auto expect = [&checks](const char* scenario, const char* name, bool value) noexcept
    {
        std::fprintf(stderr, "%s canvas-brush-lifetime-authority.%s.%s\n", value ? "PASS" : "FAIL", scenario, name);
        checks += value ? 1 : 0;
        return value;
    };
    static constexpr auto pointer = [](Application& owner, UINT message, WPARAM modifiers, Vec2 point) noexcept
    {
        const auto hole = owner._chrome->HolePixels();
        const auto view = owner._viewport.ViewFromDocument(point, owner.DocumentSize());
        owner.OnPointer(message, modifiers,
                        MAKELPARAM(static_cast<int>(std::lround(hole.left + view.x)),
                                   static_cast<int>(std::lround(hole.top + view.y))));
    };
    struct State
    {
        Application& owner;
        const Case& scenario;
        ContinuationReceipt dependent;
        bool held = false;
        bool admission = false;
        bool visited = false;
        bool borrowedOwner = false;
        bool authorityChanged = false;
        bool refusedBeforeSettlement = false;
        std::uint64_t serial = 0;
        std::uint64_t generation = 0;
        std::optional<std::uint64_t> core;
        const BrushStroke* replacement = nullptr;
        int history = 0;
    };
    static constexpr auto modal = [](void* opaque) noexcept
    {
        auto& state = *static_cast<State*>(opaque);
        auto& owner = state.owner;
        state.visited = true;
        const auto ordinal = owner._canvasLifetime ? owner._canvasLifetime->entry.ordinal : 0;
        const auto oldSerial = owner._session.DocumentSerial();
        const auto oldCore = owner._session.PendingBrushOpening();
        switch (state.scenario.fault)
        {
        case Fault::Document:
        {
            // Reused persisted IDs deliberately do not restore session authority.
            const CanvasDocument replacement = owner._session.Document().value();
            const auto active = owner._session.ActiveLayerID();
            owner._session.ReplaceDocument(replacement, active);
            state.authorityChanged = owner._session.DocumentSerial() != oldSerial;
            break;
        }
        case Fault::Core:
            owner._session.CancelBrush();
            state.authorityChanged = owner._session.BeginBrush({24, 18}, BrushKind::Paint, nullptr,
                                                               BrushSettings{3, 1, 0, 1, 0, 1}, false) &&
                                     owner._session.PendingBrushOpening() != oldCore;
            state.core = owner._session.PendingBrushOpening();
            state.replacement = owner._session.PendingBrush();
            break;
        case Fault::EndCancelled:
            owner.OnEndSession(false);
            state.authorityChanged = !owner._endSessionPending;
            break;
        case Fault::EndGeneration:
            ++owner._endSessionGeneration;
            owner._endSessionPending = true;
            state.authorityChanged = owner._endSessionGeneration != state.generation;
            break;
        case Fault::OwnerCancelled:
            owner.CancelHostContinuations();
            state.authorityChanged =
                owner._canvasLifetime &&
                owner._canvasLifetime->interrupted == ContinuationReceipt::Result{HostOutcome::Cancelled, E_ABORT};
            break;
        }
        state.serial = owner._session.DocumentSerial();
        state.generation = owner._endSessionGeneration;
        state.history = owner._session.history.UndoCount();
        owner.RunHostContinuation(); // Presenter ownership must survive a nested action after authority loss.
        state.borrowedOwner = ordinal != 0 && owner._canvasLifetime && owner._canvasLifetime->presenting &&
                              owner._canvasLifetime->entry.ordinal == ordinal &&
                              state.dependent.Read().outcome == HostOutcome::Pending;
    };
    static constexpr auto admit = [](State& state) noexcept
    {
        auto& owner = state.owner;
        const auto opening = owner._canvasBrushInput.Opening();
        const auto core = owner._session.PendingBrushOpening();
        const auto count = owner._canvasBrushInput.PendingCount();
        const bool ending = state.scenario.fault == Fault::EndCancelled || state.scenario.fault == Fault::EndGeneration;
        if (ending)
        {
            ++owner._endSessionGeneration;
            owner._endSessionPending = true;
        }
        state.generation = owner._endSessionGeneration;
        state.dependent = ending ? owner.QueueHostInput(HostEndSessionInput{state.generation, 0})
                                 : owner.QueueHostInput(HostCloseProjectInput{});
        state.admission = state.dependent.Read().outcome == HostOutcome::Pending &&
                          owner._canvasBrushInput.Sampling() && owner._canvasBrushInput.Opening() == opening &&
                          owner._session.PendingBrushOpening() == core &&
                          owner._canvasBrushInput.PendingCount() == count;
        // Observe preflight's refusal before the same UI source may acknowledge and retire an old stream.
        owner.RunCanvasLifetimePreflight();
        state.refusedBeforeSettlement =
            !owner._canvasLifetime && (state.scenario.fault == Fault::OwnerCancelled ||
                                       (owner._canvasBrushInput.Sampling() && owner._canvasBrushCancelledOpening == 0));
    };
    const auto pump = [](void* opaque, void* native) noexcept
    {
        auto& state = *static_cast<State*>(opaque);
        auto& owner = state.owner;
        if (static_cast<MSG*>(native)->message != LayerMetadataTestGate::kMessage)
        {
            DispatchMessageThunk(&owner, native);
            return;
        }
        const auto gate = owner._layerMetadataTestGate;
        state.held = owner._layerMetadataTicket && owner._historyWork.Busy() && gate->captures.load() == 1 &&
                     gate->reservationHeld.load() && !owner._session.PendingBrushOpening();
        if (!state.scenario.live)
        {
            pointer(owner, WM_MOUSEMOVE, MK_LBUTTON, {12, 8});
            admit(state);
        }
        gate->hold = false;
        (void)SetEvent(gate->release.get());
    };
    constexpr std::array<std::uint8_t, 4> sourcePixel{19, 71, 129, 255};
    constexpr std::array<std::uint8_t, 4> redPixel{0, 0, 255, 255};
    constexpr std::array<std::uint8_t, 4> greenPixel{0, 255, 0, 255};
    const auto sample = [this](int x, int y) noexcept
    {
        std::array<std::uint8_t, 4> result{};
        if (const auto* layer = _session.ActiveLayer(); layer && layer->asset)
            layer->asset->image.ReadRegion(x, y, 1, 1, result.data(), 4, 0);
        return result;
    };
    wchar_t temporary[MAX_PATH]{};
    if (!GetTempPathW(MAX_PATH, temporary))
        return 5;
    int index = 0;
    for (const auto& scenario : cases)
    {
        if (!settle())
            return 5;
        _session.ClearDocument();
        _session.CreateDocument(32, 24, true);
        _session.AddBlankLayer();
        const Guid target = _session.ActiveLayerID().value();
        std::vector<std::uint8_t> bytes(32 * 24 * 4);
        for (std::size_t offset = 0; offset < bytes.size(); offset += 4)
            std::copy(sourcePixel.begin(), sourcePixel.end(), bytes.begin() + offset);
        const auto source = Raster::Make(32, 24, RasterFormat::Bgra8888, bytes.data(), bytes.size());
        EditLayerRecordsForTest(_session,
                                [&](DocumentFixture& document)
                                {
                                    for (auto& layer : document.layers)
                                        if (layer.id == target)
                                            layer.asset = RasterAsset{source, source, L"Authority source"};
                                });
        _session.history.Reset();
        _session.history.MarkSaved();
        _opacityEdit.reset();
        _opacityRecorded.reset();
        _layerMetadataTestGate.reset();
        _endSessionPending = false;
        _endSessionPromptDeferred = false;
        _tool = EditorTool::Brush;
        _spacePan = false;
        _drag = PointerDrag{};
        _session.brushMode = BrushToolMode::Paint;
        _session.maskPaintWhite = false;
        _session.brushSettings = BrushSettings{3, 1, 1, 0, 0, 1};
        _viewport.zoom = 1;
        _viewport.pan = {};
        _viewport.followsFit = false;
        const auto output = std::wstring(temporary) + L"RedPrism-canvas-authority-" +
                            std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(++index) + L".prism";
        if (GetFileAttributesW(output.c_str()) != INVALID_FILE_ATTRIBUTES)
            return 5;
        const auto removeFile = wil::scope_exit([&output]() noexcept { (void)DeleteFileW(output.c_str()); });
        _documentPath = output;
        _forcedDiscardAnswer = scenario.save ? Prompts::Answer::First : Prompts::Answer::Second;
        State state{*this, scenario};
        ModalProbePrompts presenter(QuietPrompts(), &state, modal);
        const Prompts* previous = _prompts;
        _prompts = &presenter;
        const auto restorePrompts = wil::scope_exit([this, previous]() noexcept { _prompts = previous; });
        DocumentChanged();
        // Retain separate original-ticket observations for the preview source and typed closure.
        // Document replacement may keep that preview root even when the later closure becomes stale.
        _layerMetadataTestGate = std::make_shared<LayerMetadataTestGate>();
        _layerMetadataTestGate->hold = false;
        const auto previewGate = _layerMetadataTestGate;
        OnLayerOpacity(.25, ChromeEditPhase::Preview);
        if (!settle() ||
            !expect(scenario.name, "preview-before-closure",
                    _opacityEdit && _session.ActiveLayer()->opacity == .25 && _session.history.UndoCount() == 0))
            return 5;
        _layerMetadataTestGate = std::make_shared<LayerMetadataTestGate>();
        _layerMetadataTestGate->release.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (!_layerMetadataTestGate->release)
            return 5;
        pointer(*this, WM_LBUTTONDOWN, MK_LBUTTON, {8, 8});
        const auto stroke = _canvasBrushReceipt;
        if (SettleJobs(_jobs, 15000, pump, &state) != SettleResult::Settled ||
            !expect(scenario.name, "held-before-brush-replay", state.held))
            return 5;
        if (scenario.live)
        {
            pointer(*this, WM_MOUSEMOVE, MK_LBUTTON, {12, 8});
            if (!settle() || !_session.PendingBrushOpening())
                return 5;
            admit(state);
        }
        if (!expect(scenario.name, "admission-preserves-original-stream", state.admission) ||
            !expect(scenario.name, "modal-borrow-survives-authority-change",
                    state.visited && state.borrowedOwner && state.authorityChanged) ||
            !settle())
            return 5;
        const bool cancelled = scenario.fault == Fault::OwnerCancelled;
        if (!expect(
                scenario.name, "lost-decision-cannot-execute-or-select-output",
                state.refusedBeforeSettlement && !_canvasLifetime &&
                    state.dependent.Read() ==
                        ContinuationReceipt::Result{cancelled ? HostOutcome::Cancelled : HostOutcome::Stale, E_ABORT} &&
                    _session.DocumentSerial() == state.serial &&
                    GetFileAttributesW(output.c_str()) == INVALID_FILE_ATTRIBUTES))
            return 5;
        const bool original = scenario.fault == Fault::EndCancelled || scenario.fault == Fault::EndGeneration;
        const bool authority = original ? _canvasBrushInput.Sampling() && stroke.Read().outcome == HostOutcome::Pending
                               : scenario.fault == Fault::Core ? _session.PendingBrushOpening() == state.core &&
                                                                     _session.PendingBrush() == state.replacement
                                                               : !_session.PendingBrushOpening();
        const auto* liveStroke = _session.PendingBrush();
        const auto liveCharge = liveStroke ? liveStroke->OpeningRetainedMemoryBytes() : 0;
        // Non-owning observations distinguish real surviving roots from test-only expired weak controls.
        const auto metadataGate = _layerMetadataTestGate;
        const auto metadataBytes = metadataGate->accountingOutput.expired() ? 0 : metadataGate->accountingOutputBytes;
        const auto previewBytes = previewGate->accountingOutput.expired() ? 0 : previewGate->accountingOutputBytes;
        const bool metadataSettled = MetadataAccountingSettledForTest();
        _layerMetadataTestGate = previewGate;
        const bool previewSettled = MetadataAccountingSettledForTest();
        _layerMetadataTestGate = metadataGate;
        if (!expect(scenario.name, "retirement-preserves-current-authority", authority) ||
            !expect(scenario.name, "only-original-owner-retains-input-budget",
                    metadataSettled && previewSettled && _jobs.ReservedMemoryBytes() >= metadataBytes &&
                        _jobs.ReservedMemoryBytes() - metadataBytes >= previewBytes &&
                        _jobs.ReservedMemoryBytes() - metadataBytes - previewBytes == liveCharge && !_utilityTicket &&
                        !_utilityStamp && !_historyWork.Busy() &&
                        (original
                             ? liveStroke && liveCharge != 0 && _hostInputBudget.Used() != 0
                             : liveCharge == 0 && _hostInputBudget.Used() == 0 && !_canvasBrushInput.Busy() &&
                                   stroke.Read().outcome == (cancelled ? HostOutcome::Cancelled : HostOutcome::Stale))))
            return 5;
        if (!expect(scenario.name, "newer-end-generation-is-not-cleared",
                    scenario.fault == Fault::EndGeneration
                        ? _endSessionPending && _endSessionGeneration == state.generation
                        : !_endSessionPending))
            return 5;
        const auto before = _session.history.UndoCount();
        if (original)
        {
            pointer(*this, WM_MOUSEMOVE, MK_LBUTTON, {20, 8});
            pointer(*this, WM_LBUTTONUP, 0, {20, 8});
            if (!settle())
                return 5;
        }
        else if (!cancelled)
        {
            if (scenario.fault == Fault::Document &&
                !_session.BeginBrush({24, 18}, BrushKind::Paint, nullptr, BrushSettings{3, 1, 0, 1, 0, 1}, false))
                return 5;
            _session.FinishBrush();
            _composition.EndStroke();
            _previewSource.reset();
            _drag = PointerDrag{};
            // Reference FinishBrush can destroy accounted ancestors and enqueue memory acknowledgements.
            // Observe retirement only after the same owner pump has consumed those real destruction events.
            if (!settle())
                return 5;
        }
        const bool literal = original    ? stroke.Read().outcome == HostOutcome::Succeeded &&
                                               _session.history.UndoCount() == before + 1 && sample(8, 8) == redPixel &&
                                               sample(12, 8) == redPixel && sample(20, 8) == redPixel &&
                                               sample(24, 18) == sourcePixel
                             : cancelled ? _session.history.UndoCount() == before && sample(8, 8) == sourcePixel &&
                                               sample(12, 8) == sourcePixel && sample(24, 18) == sourcePixel
                                         : _session.history.UndoCount() == before + 1 && sample(24, 18) == greenPixel &&
                                               sample(8, 8) == sourcePixel && sample(12, 8) == sourcePixel &&
                                               sample(20, 8) == sourcePixel;
        const bool accountingSettled = MetadataAccountingSettledForTest();
        const bool allRetired = !_canvasLifetime && !_canvasBrushInput.Busy() && !_session.PendingBrushOpening() &&
                                !_activeHostRequest && _hostRequests.Size() == 0 && _hostInputBudget.Used() == 0 &&
                                accountingSettled && _jobs.ReservedMemoryBytes() >= metadataBytes &&
                                _jobs.ReservedMemoryBytes() - metadataBytes == previewBytes;
        if (!allRetired)
            std::fprintf(stderr,
                         "DIAGNOSTIC canvas-brush-lifetime-authority.%s reserved=%llu releasing=%llu metadata=%llu "
                         "preview=%llu idle=%u accounting=%u input=%llu host=%zu core=%u stream=%u\n",
                         scenario.name, static_cast<unsigned long long>(_jobs.ReservedMemoryBytes()),
                         static_cast<unsigned long long>(_jobs.ReleasingMemoryBytes()),
                         static_cast<unsigned long long>(metadataBytes), static_cast<unsigned long long>(previewBytes),
                         static_cast<unsigned int>(_jobs.IsIdle()), static_cast<unsigned int>(accountingSettled),
                         static_cast<unsigned long long>(_hostInputBudget.Used()), _hostRequests.Size(),
                         static_cast<unsigned int>(_session.PendingBrushOpening().has_value()),
                         static_cast<unsigned int>(_canvasBrushInput.Busy()));
        if (!expect(scenario.name, "only-current-authority-commits-literal-pixels", literal) ||
            !expect(scenario.name, "all-input-and-worker-reservations-retire", allRetired))
            return 5;
        OnEndSession(false);
    }
    std::fprintf(stderr, "PASS canvas-brush-lifetime-authority.summary checks=%d cases=%zu expected=200\n", checks,
                 cases.size());
    return checks == 200 ? 0 : 5;
}
