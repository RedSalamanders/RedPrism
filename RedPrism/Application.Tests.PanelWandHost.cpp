#include "Application.h"

#include "../Tests/Support/DocumentFixture.h"
#include "ChromeHost.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <utility>

namespace
{
DocumentSelection PanelWandRect(double x, double width)
{
    return {false, {SelectionLoop{{x, 1}, {x + width, 1}, {x + width, 2}, {x, 2}}}};
}
bool PanelWandLiteral(const std::optional<DocumentSelection>& selected, double x, double width)
{
    if (!selected || selected->antialiased || selected->loops.size() != 1)
        return false;
    const std::array expected{Vec2{x, 1}, Vec2{x + width, 1}, Vec2{x + width, 2}, Vec2{x, 2}};
    const auto& points = selected->loops.Get().front();
    SelectionLoop corners;
    for (std::size_t i = 0; i < points.size(); ++i)
    {
        const auto& before = points[(i + points.size() - 1) % points.size()];
        const auto& point = points[i];
        const auto& after = points[(i + 1) % points.size()];
        const Vec2 d{point.x - before.x, point.y - before.y};
        const Vec2 n{after.x - point.x, after.y - point.y};
        if (d.x * n.y - d.y * n.x != 0 || d.x * n.x + d.y * n.y <= 0)
            corners.push_back(point);
    }
    if (corners.size() != expected.size())
        return false;
    for (std::size_t start = 0; start < expected.size(); ++start)
    {
        bool equal = true;
        for (std::size_t i = 0; i < expected.size(); ++i)
            equal = equal && corners[(start + i) % expected.size()] == expected[i];
        if (equal)
            return true;
    }
    return false;
}
bool PanelWandSemanticPanel(const ColorEdit& before, const ColorEdit& after) noexcept
{
    // Derived preview/histogram caches may refresh. Original editing semantics must remain exact.
    return before.kind == after.kind && before.layerID == after.layerID && before.original == after.original &&
           before.transform == after.transform && before.selection == after.selection &&
           before.settings == after.settings && before.openingSequence == after.openingSequence &&
           before.seed == after.seed && before.levelsSample == after.levelsSample &&
           before.hueSample == after.hueSample && before.targeting == after.targeting &&
           before.targetDrag.has_value() == after.targetDrag.has_value();
}
} // namespace

int Application::RunPanelWandHostSelfTest() noexcept
{
    const auto settled = [this]() noexcept
    { return SettleJobs(_jobs, 15000, DispatchMessageThunk, this) == SettleResult::Settled; };
    if (!_chrome || !_hostSourceRegistered || !settled() || !_session.Document() || _session.HasColorEdit())
        return 5;
    const auto original = std::make_unique<CanvasDocument>(_session.Document().value());
    const auto originalActive = _session.ActiveLayerID();
    const auto originalPath = _documentPath;
    const auto originalTool = _tool;
    const auto originalViewport = _viewport;
    const auto originalWand = _session.wandSettings;
    const auto originalMode = _session.selectionModeChoice;
    const auto originalAa = _session.selectionAntialiased;
    const auto cleanup = wil::scope_exit(
        [&]() noexcept
        {
            CancelHostContinuations();
            if (!settled())
                std::_Exit(5);
            CancelColorEditForTest();
            ReplaceDocument(*original, originalActive, originalPath);
            _tool = originalTool;
            _viewport = originalViewport;
            _session.wandSettings = originalWand;
            _session.selectionModeChoice = originalMode;
            _session.selectionAntialiased = originalAa;
            SyncChrome();
        });
    int assertions = 0;
    const auto check =
        [&assertions](bool value, const char* panel, bool composite, const char* action, const char* fact) noexcept
    {
        ++assertions;
        std::printf("%s panel-wand-host.%s.%s.%s.%s\n", value ? "PASS" : "FAIL", panel,
                    composite ? "composite" : "layer", action, fact);
        return value;
    };
    // Literal 4x4 input: black except one opaque red pixel at (1,1). Both the placed layer
    // and committed composite must select precisely that one pixel with tolerance zero.
    constexpr std::array<std::uint8_t, 64> pixels{0, 0, 0, 255, 0, 0, 0,   255, 0, 0, 0, 255, 0, 0, 0, 255,
                                                  0, 0, 0, 255, 0, 0, 255, 255, 0, 0, 0, 255, 0, 0, 0, 255,
                                                  0, 0, 0, 255, 0, 0, 0,   255, 0, 0, 0, 255, 0, 0, 0, 255,
                                                  0, 0, 0, 255, 0, 0, 0,   255, 0, 0, 0, 255, 0, 0, 0, 255};
    const auto raster = Raster::Make(4, 4, RasterFormat::Bgra8888, pixels.data(), pixels.size());
    const std::array kinds{FilterKind::Levels, FilterKind::HueSaturation, FilterKind::Exposure};
    const std::array names{"levels", "hsv", "exposure"};
    const std::array commands{ChromeHost::kCommandLevels, ChromeHost::kCommandHueSaturation,
                              ChromeHost::kCommandExposure};
    for (std::size_t panelIndex = 0; panelIndex < kinds.size(); ++panelIndex)
        for (const bool composite : {false, true})
            for (int action = 0; action < 5; ++action)
            {
                CancelColorEditForTest();
                if (!settled())
                    return 5;
                DocumentFixture fixture;
                fixture.id = Guid::Generate();
                fixture.width = 4;
                fixture.height = 4;
                auto layer = ImageLayer::Blank(L"Panel Wand literal source", {4, 4});
                const auto active = layer.id;
                layer.asset = RasterAsset{raster, {}, layer.name};
                layer.transform.sampling = LayerSampling::Nearest;
                fixture.layers.push_back(std::move(layer));
                ReplaceDocument(std::move(fixture).Seal(), active, {});
                const auto mode = action == 1   ? SelectionMode::Add
                                  : action == 2 ? SelectionMode::Subtract
                                                : SelectionMode::New;
                if (action == 1)
                    _session.SetSelection(PanelWandRect(0, 1), L"Seed panel Add");
                if (action == 2)
                    _session.SetSelection(PanelWandRect(0, 2), L"Seed panel Subtract");
                if (action == 3)
                    _session.SetSelection(PanelWandRect(0, 3), L"Seed zero-offset panel Wand");
                _session.history.Reset();
                _session.history.MarkSaved();
                _tool = EditorTool::Wand;
                _viewport.zoom = 1;
                _viewport.followsFit = false;
                _session.selectionModeChoice = mode;
                _session.selectionAntialiased = false;
                _session.wandSettings = {};
                _session.wandSettings.tolerance = 0;
                _session.wandSettings.sampleSize = WandSampleSize::Point;
                _session.wandSettings.contiguous = true;
                _session.wandSettings.sampleAllLayers = composite;
                if (!settled() || !BeginColorEdit(commands[panelIndex]) || !settled())
                    return 5;
                if (action == 4)
                    _session.history.Begin(L"Panel Wand outer transaction", _session.Document(),
                                           _session.ActiveLayerID());
                const auto before = std::make_unique<ColorEdit>(_session.PendingColorEdit().value());
                const auto selection = _session.Selection();
                const auto* actionName = action == 0   ? "new"
                                         : action == 1 ? "add"
                                         : action == 2 ? "subtract"
                                         : action == 3 ? "zero-offset"
                                                       : "nested";
                if (before->kind != kinds[panelIndex])
                    return 5;
                const auto view = _viewport.ViewFromDocument({1.2, 1.2}, DocumentSize());
                DxUi::PointerEvent event;
                event.action = DxUi::PointerAction::Down;
                PressCanvas(event, WM_LBUTTONDOWN, view, POINT{42, 42});
                if (action == 3)
                {
                    if (!_session.IsMovingSelection())
                        return 5;
                    SelectionRelease(view);
                }
                const auto* input = _hostRequests.Size() ? _hostRequests.PendingAt(_hostRequests.Size() - 1) : nullptr;
                const auto receipt = input ? input->input.Receipt() : ContinuationReceipt::Failure(E_UNEXPECTED);
                if (!check(input && receipt.Read().outcome == HostOutcome::Pending && ColorControlsFrozen() &&
                               _session.Selection() == selection && _session.PendingColorEdit() &&
                               PanelWandSemanticPanel(*before, _session.PendingColorEdit().value()),
                           names[panelIndex], composite, actionName,
                           "physical-admission-preserves-panel-and-selection"))
                    return 5;
                const bool didSettle = settled();
                const auto observed = receipt.Read();
                const bool originalCondition = didSettle && observed.outcome == HostOutcome::Succeeded &&
                                               !_session.IsMovingSelection() && !_wandLayerHostTicket &&
                                               !_historyWork.Busy();
                if (!originalCondition)
                    std::printf("INFO panel-wand-diagnostic panel=%s composite=%u action=%s settled=%u outcome=%u "
                                "error=%08X moving=%u wand=%u history-busy=%u idle=%u reserved=%llu releasing=%llu "
                                "generation=%llu color-sequence=%llu host-stage=%u queued=%zu\n",
                                names[panelIndex], static_cast<unsigned int>(composite), actionName,
                                static_cast<unsigned int>(didSettle), static_cast<unsigned int>(observed.outcome),
                                static_cast<unsigned int>(observed.error),
                                static_cast<unsigned int>(_session.IsMovingSelection()),
                                static_cast<unsigned int>(_wandLayerHostTicket.has_value()),
                                static_cast<unsigned int>(_historyWork.Busy()),
                                static_cast<unsigned int>(_jobs.IsIdle()),
                                static_cast<unsigned long long>(_jobs.ReservedMemoryBytes()),
                                static_cast<unsigned long long>(_jobs.ReleasingMemoryBytes()),
                                static_cast<unsigned long long>(_session.Generation()),
                                static_cast<unsigned long long>(_session.ColorEditSequence()),
                                static_cast<unsigned int>(_hostStage), _hostRequests.Size());
                if (!check(originalCondition, names[panelIndex], composite, actionName,
                           "original-request-joins-and-finishes"))
                    return 5;
                const double x = action == 1 || action == 2 ? 0 : 1;
                const double width = action == 1 ? 2 : 1;
                if (!check(PanelWandLiteral(_session.Selection(), x, width), names[panelIndex], composite, actionName,
                           "literal-selection"))
                    return 5;
                if (!check(_session.PendingColorEdit() &&
                               PanelWandSemanticPanel(*before, _session.PendingColorEdit().value()) &&
                               _session.ActiveLayer()->asset->image == raster &&
                               (action == 4
                                    ? _session.history.CaptureState().Depth() == 1 && _session.history.UndoCount() == 0
                                    : _session.history.CaptureState().Depth() == 0 &&
                                          _session.history.UndoCount() == 1 &&
                                          _session.history.UndoName() == L"Magic Wand"),
                           names[panelIndex], composite, actionName, "panel-source-and-history-preserved"))
                    return 5;
                if (action == 4)
                {
                    _session.history.End(_session.Document(), _session.ActiveLayerID());
                    if (!check(_session.history.CaptureState().Depth() == 0 && _session.history.UndoCount() == 1 &&
                                   _session.history.UndoName() == L"Panel Wand outer transaction",
                               names[panelIndex], composite, actionName, "original-outer-title-published"))
                        return 5;
                }
            }
    std::printf("PASS panel-wand-host (%d assertions)\n", assertions);
    return 0;
}
