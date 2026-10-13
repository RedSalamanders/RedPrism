#include "Application.h"

#include "../Tests/Support/DocumentFixture.h"
#include "ChromeHost.h"
#include "LayerMetadataTestGate.h"
#include "ProjectStore.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <variant>
#include <vector>

int Application::RunCanvasBrushBoundarySelfTest() noexcept
{
    const auto originalViewport = _viewport;
    const auto originalPath = _documentPath;
    const auto originalSettings = _session.brushSettings;
    const auto originalMaskWhite = _session.maskPaintWhite;
    const auto originalMode = _session.brushMode;
    const auto originalTool = _tool;
    int checks = 0;
    const auto expect = [&checks](const char* scenario, const char* name, bool value) noexcept
    {
        std::fprintf(stderr, "%s canvas-brush-boundary.%s.%s\n", value ? "PASS" : "FAIL", scenario, name);
        checks += value ? 1 : 0;
        return value;
    };
    const auto settle = [this]() noexcept
    { return SettleJobs(_jobs, 15000, DispatchMessageThunk, this) == SettleResult::Settled; };
    const auto cleanup = wil::scope_exit(
        [this, originalViewport, originalPath, originalSettings, originalMaskWhite, originalMode, originalTool,
         &settle]() noexcept
        {
            if (_layerMetadataTestGate)
                (void)SetEvent(_layerMetadataTestGate->release.get());
            (void)CancelCanvasBrush();
            if (!settle())
                std::_Exit(5);
            _layerMetadataTestGate.reset();
            _opacityEdit.reset();
            _opacityRecorded.reset();
            _canvasBrushReceipt = {};
            _viewport = originalViewport;
            _documentPath = originalPath;
            _session.brushSettings = originalSettings;
            _session.maskPaintWhite = originalMaskWhite;
            _session.brushMode = originalMode;
            _tool = originalTool;
        });
    enum class Action
    {
        Tool,
        Layer,
        Mask,
        Save
    };
    enum class Phase
    {
        Held,
        Live,
        Refused
    };
    struct Case
    {
        const char* name;
        Action action;
        Phase phase;
    };
    constexpr std::array cases{
        Case{"tool-held", Action::Tool, Phase::Held},       Case{"tool-live", Action::Tool, Phase::Live},
        Case{"tool-refused", Action::Tool, Phase::Refused}, Case{"layer-held", Action::Layer, Phase::Held},
        Case{"layer-live", Action::Layer, Phase::Live},     Case{"layer-refused", Action::Layer, Phase::Refused},
        Case{"mask-held", Action::Mask, Phase::Held},       Case{"mask-live", Action::Mask, Phase::Live},
        Case{"mask-refused", Action::Mask, Phase::Refused}, Case{"save-held", Action::Save, Phase::Held},
        Case{"save-live", Action::Save, Phase::Live},       Case{"save-refused", Action::Save, Phase::Refused}};
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
        Case scenario;
        Guid source;
        Guid other;
        std::wstring output;
        std::uint64_t opening;
        ContinuationReceipt dependent;
        bool visited = false;
        bool held = false;
        bool admissionCorrect = false;
        bool lateInputCorrect = false;
    };
    static constexpr auto admit = [](State& state) noexcept
    {
        auto& owner = state.owner;
        const bool refused = state.scenario.phase == Phase::Refused;
        const auto oldCount = owner._canvasBrushInput.PendingCount();
        const auto oldCore = owner._session.PendingBrushOpening();
        owner._hostAdmissionAllocationFailureForTest = refused;
        switch (state.scenario.action)
        {
        case Action::Tool:
            state.dependent = owner.QueueHostInput(HostToolInput{EditorTool::Move});
            break;
        case Action::Layer:
            state.dependent = owner.QueueHostInput(HostLayerSelectInput{state.other});
            break;
        case Action::Mask:
            state.dependent = owner.QueueHostInput(HostLayerTargetInput{state.source, true, false});
            break;
        case Action::Save:
            state.dependent = owner.QueueHostInput(HostWriteInput{HostWriteFormat::Project, false, state.output});
            break;
        }
        owner._hostAdmissionAllocationFailureForTest = false;
        state.admissionCorrect =
            owner._canvasBrushInput.Opening() == state.opening &&
            owner._canvasBrushReceipt.Read().outcome == HostOutcome::Pending &&
            owner._session.PendingBrushOpening() == oldCore && owner._session.ActiveLayerID() == state.source &&
            !owner._session.IsMaskSelected() && owner._tool == EditorTool::Brush &&
            (refused
                 ? state.dependent.Read().outcome == HostOutcome::Failed && owner._canvasBrushInput.Sampling() &&
                       owner._canvasBrushInput.PendingCount() == oldCount
                 : state.dependent.Read().outcome == HostOutcome::Pending && !owner._canvasBrushInput.Sampling() &&
                       owner._canvasBrushInput.PendingCount() == oldCount + 1 && GetCapture() != owner._window.get());
        const auto terminalCount = owner._canvasBrushInput.PendingCount();
        pointer(owner, WM_MOUSEMOVE, MK_LBUTTON, {20, 8});
        pointer(owner, WM_LBUTTONUP, 0, {20, 8});
        state.lateInputCorrect = !owner._canvasBrushInput.Sampling() && owner._canvasBrushInput.Busy() &&
                                 owner._canvasBrushInput.PendingCount() == terminalCount + (refused ? 2 : 0) &&
                                 owner._canvasBrushInput.Opening() == state.opening &&
                                 owner._canvasBrushReceipt.Read().outcome == HostOutcome::Pending;
    };
    const auto pump = [](void* opaque, void* native) noexcept
    {
        auto& state = *static_cast<State*>(opaque);
        auto& owner = state.owner;
        const auto& message = *static_cast<MSG*>(native);
        if (message.message != LayerMetadataTestGate::kMessage)
        {
            DispatchMessageThunk(&owner, native);
            return;
        }
        state.visited = true;
        const auto gate = owner._layerMetadataTestGate;
        state.held = owner._layerMetadataTicket && owner._historyWork.Busy() && gate->captures.load() == 1 &&
                     gate->reservationHeld.load() && !owner._session.PendingBrush() &&
                     owner._canvasBrushInput.Sampling() && owner._session.history.UndoCount() == 0;
        if (state.scenario.phase != Phase::Live)
        {
            pointer(owner, WM_MOUSEMOVE, MK_LBUTTON, {12, 8});
            admit(state);
        }
        gate->hold = false;
        (void)SetEvent(gate->release.get());
    };
    constexpr std::array<std::uint8_t, 4> sourcePixel{19, 71, 129, 255};
    constexpr std::array<std::uint8_t, 4> redPixel{0, 0, 255, 255};
    const auto findLayer = [](const CanvasDocument& document, const Guid& id) noexcept -> const ImageLayer*
    {
        for (const auto& layer : document.layers)
            if (layer.id == id)
                return &layer;
        return nullptr;
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
        const Guid sourceId = _session.ActiveLayerID().value();
        std::vector<std::uint8_t> bytes(32 * 24 * 4);
        for (std::size_t offset = 0; offset < bytes.size(); offset += 4)
            std::copy(sourcePixel.begin(), sourcePixel.end(), bytes.begin() + offset);
        const Raster source = Raster::Make(32, 24, RasterFormat::Bgra8888, bytes.data(), bytes.size());
        EditLayerRecordsForTest(_session,
                                [&](DocumentFixture& document)
                                {
                                    for (auto& layer : document.layers)
                                        if (layer.id == sourceId)
                                            layer.asset =
                                                RasterAsset{source, Raster::MakeThumbnail(source), layer.name};
                                });
        _session.SelectAll();
        _session.AddMask(false);
        _session.Deselect();
        _session.SelectLayerTarget(sourceId, false);
        _session.AddBlankLayer();
        const Guid otherId = _session.ActiveLayerID().value();
        _session.SelectLayerTarget(sourceId, false);
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
        _documentPath.clear();
        DocumentChanged();
        OnLayerOpacity(.25, ChromeEditPhase::Preview);
        if (!settle() ||
            !expect(scenario.name, "preview-acknowledges-before-gated-closure",
                    _opacityEdit && _session.ActiveLayer()->opacity == .25 && _session.history.UndoCount() == 0))
            return 5;
        _layerMetadataTestGate = std::make_shared<LayerMetadataTestGate>();
        _layerMetadataTestGate->release.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (!_layerMetadataTestGate->release)
            return 5;
        const auto output = std::wstring(temporary) + L"RedPrism-canvas-boundary-" +
                            std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(++index) + L".prism";
        if (GetFileAttributesW(output.c_str()) != INVALID_FILE_ATTRIBUTES)
            return 5;
        const auto removeFile = wil::scope_exit([&output]() noexcept { (void)DeleteFileW(output.c_str()); });
        pointer(*this, WM_LBUTTONDOWN, MK_LBUTTON, {8, 8});
        const auto strokeReceipt = _canvasBrushReceipt;
        State state{*this, scenario, sourceId, otherId, output, _canvasBrushInput.Opening()};
        if (!expect(scenario.name, "physical-down-is-owned-before-worker-ack",
                    _canvasBrushInput.Sampling() && !_session.PendingBrush() &&
                        strokeReceipt.Read().outcome == HostOutcome::Pending) ||
            SettleJobs(_jobs, 15000, pump, &state) != SettleResult::Settled ||
            !expect(scenario.name, "older-opacity-closure-held-without-brush-mutation", state.visited && state.held))
            return 5;
        if (scenario.phase == Phase::Live)
        {
            if (!_session.PendingBrushOpening() || !_canvasBrushInput.Sampling() || _session.history.UndoCount() != 1)
                return 5;
            pointer(*this, WM_MOUSEMOVE, MK_LBUTTON, {12, 8});
            if (!settle())
                return 5;
            admit(state);
        }
        if (!expect(scenario.name, "admission-alone-controls-prefix-seal-and-capture", state.admissionCorrect) ||
            !expect(scenario.name, "later-physical-events-respect-admitted-boundary", state.lateInputCorrect) ||
            !settle())
            return 5;
        const bool refused = scenario.phase == Phase::Refused;
        if (!expect(scenario.name, "stroke-and-dependent-receipts-retire-without-budget",
                    strokeReceipt.Read().outcome == HostOutcome::Succeeded &&
                        state.dependent.Read().outcome == (refused ? HostOutcome::Failed : HostOutcome::Succeeded) &&
                        !_canvasBrushInput.Busy() && !_session.PendingBrushOpening() && !_activeHostRequest &&
                        _hostRequests.Size() == 0 && _hostInputBudget.Used() == 0 &&
                        _layerMetadataTestGate->originalAccountingBound &&
                        _layerMetadataTestGate->accountingOutputBytes > 0 && MetadataAccountingSettledForTest() &&
                        !_opacityEdit && _session.history.UndoCount() == 2))
            return 5;
        const auto* layer = findLayer(_session.Document().value(), sourceId);
        const auto sample = [](const Raster& raster, int x, int y)
        {
            std::array<std::uint8_t, 4> pixel{};
            raster.ReadRegion(x, y, 1, 1, pixel.data(), 4, 0);
            return pixel;
        };
        if (!expect(scenario.name, "literal-pixels-belong-to-accepted-prefix-only",
                    layer && layer->asset && sample(layer->asset->image, 8, 8) == redPixel &&
                        sample(layer->asset->image, 12, 8) == redPixel &&
                        sample(layer->asset->image, 20, 8) == (refused ? redPixel : sourcePixel) &&
                        sample(layer->asset->image, 8, 11) == sourcePixel && layer->opacity == .25))
            return 5;
        bool dependentCorrect =
            refused ? _tool == EditorTool::Brush && _session.ActiveLayerID() == sourceId &&
                          !_session.IsMaskSelected() && GetFileAttributesW(output.c_str()) == INVALID_FILE_ATTRIBUTES
            : scenario.action == Action::Tool  ? _tool == EditorTool::Move
            : scenario.action == Action::Layer ? _session.ActiveLayerID() == otherId
            : scenario.action == Action::Mask  ? _session.ActiveLayerID() == sourceId && _session.IsMaskSelected()
                                               : _documentPath == output && !_session.history.IsModified();
        if (!refused && scenario.action == Action::Save)
        {
            const auto loaded = LoadProject(output.c_str());
            const auto* saved = loaded.status == ProjectIoStatus::Ok ? findLayer(loaded.document, sourceId) : nullptr;
            dependentCorrect = dependentCorrect && saved && saved->asset && saved->opacity == .25 &&
                               sample(saved->asset->image, 8, 8) == redPixel &&
                               sample(saved->asset->image, 12, 8) == redPixel &&
                               sample(saved->asset->image, 20, 8) == sourcePixel;
        }
        if (!expect(scenario.name, "dependent-action-observes-acknowledged-prefix", dependentCorrect))
            return 5;
    }
    std::fprintf(stderr, "PASS canvas-brush-boundary.summary checks=%d cases=%zu\n", checks, cases.size());
    return checks == 96 ? 0 : 5;
}
