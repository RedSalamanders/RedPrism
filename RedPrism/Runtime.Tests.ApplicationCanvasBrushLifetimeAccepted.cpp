#include "Application.h"

#include "../Tests/Support/Support.Tests.DocumentFixture.h"
#include "ChromeHost.h"
#include "ProjectStore.h"
#include "Runtime.Tests.LayerMetadataTestGate.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

int Application::RunCanvasBrushLifetimeAcceptedSelfTest() noexcept
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
            _layerMetadataTestGate.reset();
            _opacityEdit.reset();
            _opacityRecorded.reset();
            _canvasBrushReceipt = {};
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
    struct Case
    {
        const char* name;
        bool live;
        bool newDocument;
        bool save;
        bool failWrite;
    };
    constexpr std::array cases{Case{"close-save-held", false, false, true, false},
                               Case{"close-save-live", true, false, true, false},
                               Case{"new-save-held", false, true, true, false},
                               Case{"new-save-live", true, true, true, false},
                               Case{"close-discard-held", false, false, false, false},
                               Case{"close-discard-live", true, false, false, false},
                               Case{"new-discard-held", false, true, false, false},
                               Case{"new-discard-live", true, true, false, false},
                               Case{"close-write-failure-held", false, false, true, true},
                               Case{"close-write-failure-live", true, false, true, true}};
    int checks = 0;
    const auto expect = [&checks](const char* scenario, const char* name, bool value) noexcept
    {
        std::fprintf(stderr, "%s canvas-brush-lifetime-accepted.%s.%s\n", value ? "PASS" : "FAIL", scenario, name);
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
        Case scenario;
        Guid target;
        std::uint64_t serial;
        ContinuationReceipt dependent;
        ContinuationReceipt younger;
        bool held = false;
        bool admission = false;
        bool decision = false;
        bool ordered = false;
    };
    static constexpr auto admit = [](State& state) noexcept
    {
        auto& owner = state.owner;
        const auto opening = owner._canvasBrushInput.Opening();
        const auto count = owner._canvasBrushInput.PendingCount();
        const auto core = owner._session.PendingBrushOpening();
        state.dependent = state.scenario.newDocument ? owner.QueueHostInput(HostNewInput{})
                                                     : owner.QueueHostInput(HostCloseProjectInput{});
        state.admission = state.dependent.Read().outcome == HostOutcome::Pending &&
                          owner._canvasBrushInput.Sampling() && owner._canvasBrushInput.PendingCount() == count &&
                          owner._session.PendingBrushOpening() == core;
        owner.RunHostContinuation(); // Explicit nonnested owner seam while the older gate may still be held.
        state.decision = owner._canvasLifetime && owner._canvasLifetime->entry.documentSerial == state.serial &&
                         owner._canvasLifetime->brushOpening == opening && !owner._canvasLifetime->presenting &&
                         owner._canvasLifetime->save == state.scenario.save && !owner._canvasBrushInput.Sampling();
        if (!state.scenario.failWrite)
            state.younger = owner.QueueHostInput(HostLayerSelectInput{state.target});
        state.ordered = owner._session.DocumentSerial() == state.serial &&
                        state.dependent.Read().outcome == HostOutcome::Pending &&
                        (state.scenario.failWrite || state.younger.Read().outcome == HostOutcome::Pending);
        // Accepted preflight ends/cancels the prefix. Physical samples after the decision are not in the save.
        pointer(owner, WM_MOUSEMOVE, MK_LBUTTON, {20, 8});
        pointer(owner, WM_LBUTTONUP, 0, {20, 8});
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
    const auto sample = [](const Raster& raster, int x, int y) noexcept
    {
        std::array<std::uint8_t, 4> result{};
        raster.ReadRegion(x, y, 1, 1, result.data(), 4, 0);
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
        const auto output = std::wstring(temporary) + L"RedPrism-canvas-lifetime-" +
                            std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(++index) +
                            (scenario.failWrite ? L"-missing/failed.prism" : L".prism");
        if (GetFileAttributesW(output.c_str()) != INVALID_FILE_ATTRIBUTES)
            return 5;
        const auto removeFile = wil::scope_exit([&output]() noexcept { (void)DeleteFileW(output.c_str()); });
        _documentPath = output;
        _forcedDiscardAnswer = scenario.save ? Prompts::Answer::First : Prompts::Answer::Second;
        DocumentChanged();
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
        const auto serial = _session.DocumentSerial();
        State state{*this, scenario, target, serial};
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
        if (!expect(scenario.name, "admission-preserves-live-stream", state.admission) ||
            !expect(scenario.name, "decision-owns-exact-stream-and-ordinal", state.decision) ||
            !expect(scenario.name, "younger-input-waits-for-original-receipt", state.ordered) || !settle())
            return 5;
        if (!expect(scenario.name, "all-owners-and-reservations-retire",
                    !_canvasLifetime && !_canvasBrushInput.Busy() && !_session.PendingBrushOpening() &&
                        !_activeHostRequest && _hostRequests.Size() == 0 && _hostInputBudget.Used() == 0 &&
                        MetadataAccountingSettledForTest() &&
                        stroke.Read().outcome == (scenario.save ? HostOutcome::Succeeded : HostOutcome::Cancelled)))
            return 5;
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
        if (!expect(scenario.name, "saved-file-holds-only-acknowledged-prefix", exactSaved))
            return 5;
        const bool action = scenario.failWrite
                                ? state.dependent.Read().outcome == HostOutcome::Failed &&
                                      FAILED(state.dependent.Read().error) && _session.DocumentSerial() == serial &&
                                      _session.history.UndoCount() == 2 && _session.history.IsModified()
                                : state.dependent.Read().outcome == HostOutcome::Succeeded &&
                                      state.younger.Read().outcome == HostOutcome::Stale &&
                                      _session.DocumentSerial() != serial &&
                                      (scenario.newDocument ? _session.Document().has_value() : !_session.Document());
        if (!expect(scenario.name, "dependent-action-publishes-only-after-successful-save", action))
            return 5;
    }
    std::fprintf(stderr, "PASS canvas-brush-lifetime-accepted.summary checks=%d cases=%zu\n", checks, cases.size());
    return checks == 80 ? 0 : 5;
}
