#include "Application.h"
#include <cstddef>

#include "ChromeHost.h"
#include "Runtime.Tests.LayerMetadataTestGate.h"
#include "Tests/Support/Support.Tests.DocumentFixture.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>

namespace
{
constexpr int kMaskPaletteExtensionWidth = 32;
constexpr int kMaskPaletteExtensionHeight = 24;
constexpr std::array<std::uint8_t, 4> kMaskPaletteExtensionSourcePixel{19, 71, 129, 255};
constexpr BrushSettings kMaskPaletteExtensionDownTip{3, 1, 1, 0, 0, 1};
constexpr BrushSettings kMaskPaletteExtensionLiveTip{9, 1, 1, 0, 1, 0};

bool MaskPaletteExtensionSamePixels(const Raster& actual, const Raster& expected) noexcept
{
    if (!actual.HasStorage() || !expected.HasStorage() || actual.Format() != expected.Format() ||
        actual.Width() != kMaskPaletteExtensionWidth || actual.Height() != kMaskPaletteExtensionHeight ||
        expected.Width() != kMaskPaletteExtensionWidth || expected.Height() != kMaskPaletteExtensionHeight)
        return false;
    const int channels = actual.Channels();
    if (channels != 1 && channels != 4)
        return false;
    std::array<std::uint8_t, kMaskPaletteExtensionWidth * kMaskPaletteExtensionHeight * 4> actualBytes{};
    std::array<std::uint8_t, kMaskPaletteExtensionWidth * kMaskPaletteExtensionHeight * 4> expectedBytes{};
    const int stride = kMaskPaletteExtensionWidth * channels;
    actual.ReadRegion(0, 0, kMaskPaletteExtensionWidth, kMaskPaletteExtensionHeight, actualBytes.data(), stride, 0);
    expected.ReadRegion(0, 0, kMaskPaletteExtensionWidth, kMaskPaletteExtensionHeight, expectedBytes.data(), stride, 0);
    return std::equal(actualBytes.begin(), actualBytes.begin() + stride * kMaskPaletteExtensionHeight,
                      expectedBytes.begin());
}

bool MaskPaletteExtensionSameOriginal(const Raster& actual, const Raster& expected) noexcept
{
    return actual.Identity() == expected.Identity() && MaskPaletteExtensionSamePixels(actual, expected);
}

bool MaskPaletteExtensionLiteralImage(const Raster& image) noexcept
{
    if (!image.HasStorage() || image.Format() != RasterFormat::Bgra8888 ||
        image.Width() != kMaskPaletteExtensionWidth || image.Height() != kMaskPaletteExtensionHeight)
        return false;
    std::array<std::uint8_t, kMaskPaletteExtensionWidth * kMaskPaletteExtensionHeight * 4> bytes{};
    image.ReadRegion(0, 0, kMaskPaletteExtensionWidth, kMaskPaletteExtensionHeight, bytes.data(),
                     kMaskPaletteExtensionWidth * 4, 0);
    for (std::size_t offset = 0; offset < bytes.size(); offset += 4)
        if (!std::equal(kMaskPaletteExtensionSourcePixel.begin(), kMaskPaletteExtensionSourcePixel.end(),
                        bytes.begin() + static_cast<std::ptrdiff_t>(offset)))
            return false;
    return true;
}

bool MaskPaletteExtensionBlackMask(const Raster& mask) noexcept
{
    if (!mask.HasStorage() || mask.Format() != RasterFormat::Gray8 || mask.Width() != kMaskPaletteExtensionWidth ||
        mask.Height() != kMaskPaletteExtensionHeight)
        return false;
    std::array<std::uint8_t, kMaskPaletteExtensionWidth * kMaskPaletteExtensionHeight> bytes{};
    mask.ReadRegion(0, 0, kMaskPaletteExtensionWidth, kMaskPaletteExtensionHeight, bytes.data(),
                    kMaskPaletteExtensionWidth, 173);
    return std::all_of(bytes.begin(), bytes.end(), [](std::uint8_t value) noexcept { return value == 0; });
}

bool MaskPaletteExtensionGray(const Raster& mask, int x, int y, std::uint8_t expected) noexcept
{
    if (!mask.HasStorage() || mask.Format() != RasterFormat::Gray8 || mask.Width() != kMaskPaletteExtensionWidth ||
        mask.Height() != kMaskPaletteExtensionHeight)
        return false;
    std::uint8_t actual = 173;
    mask.ReadRegion(x, y, 1, 1, &actual, 1, 173);
    if (actual != expected)
        std::fprintf(stderr, "canvas-brush-mask-palette.observed x=%d y=%d actual=%u expected=%u\n", x, y,
                     static_cast<unsigned int>(actual), static_cast<unsigned int>(expected));
    return actual == expected;
}
} // namespace

int Application::RunCanvasBrushMaskPaletteExtensionSelfTest() noexcept
{
    constexpr int kExpectedChecks = 68;
    const auto originalViewport = _viewport;
    const auto originalBrushSettings = _session.brushSettings;
    const auto originalMaskPaintWhite = _session.maskPaintWhite;
    const auto originalBrushMode = _session.brushMode;
    const auto originalTool = _tool;
    const auto originalSpacePan = _spacePan;
    const auto originalDrag = _drag;
    int checks = 0;
    const auto expect = [&checks](const char* scenario, const char* name, bool condition) noexcept
    {
        std::fprintf(stderr, "%s canvas-brush-mask-palette.%s.%s\n", condition ? "PASS" : "FAIL", scenario, name);
        checks += condition ? 1 : 0;
        return condition;
    };
    const auto settle = [this]() noexcept
    { return SettleJobs(_jobs, 15000, DispatchMessageThunk, this) == SettleResult::Settled; };
    const auto cleanup = wil::scope_exit(
        [this, originalViewport, originalBrushSettings, originalMaskPaintWhite, originalBrushMode, originalTool,
         originalSpacePan, originalDrag, &settle]
        {
            if (_layerMetadataTestGate)
                (void)SetEvent(_layerMetadataTestGate->release.get());
            if (_canvasBrushInput.Sampling())
                LoseCanvasBrush();
            if (!settle())
                std::_Exit(5);
            _layerMetadataTestGate.reset();
            _opacityEdit.reset();
            _opacityRecorded.reset();
            _canvasBrushReceipt = {};
            _viewport = originalViewport;
            (void)SendMessageW(_window.get(), WM_ACTIVATEAPP, TRUE, 0);
            // Restore settings only after accepted work has acknowledged the gate and finished using its capture.
            _session.brushSettings = originalBrushSettings;
            _session.maskPaintWhite = originalMaskPaintWhite;
            _session.brushMode = originalBrushMode;
            _tool = originalTool;
            _spacePan = originalSpacePan;
            _drag = originalDrag;
        });

    struct Case final
    {
        const char* name;
        bool eraser;
        bool failure;
    };
    constexpr std::array cases{Case{"white-paint-success", false, false}, Case{"mask-eraser-success", true, false},
                               Case{"white-paint-failure", false, true}, Case{"mask-eraser-failure", true, true}};
// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4623 4626 5027)
    struct State final
    {
        Application& owner;
        const Case& scenario;
        Guid target;
        Guid historyRevision;
        const Raster& image;
        const Raster& mask;
        const Raster& thumbnail;
        bool visited = false;
        bool held = false;
        bool captured = false;
        bool terminalAndUnchanged = false;
    };
#pragma warning(pop)

    const auto pump = [](void* opaque, void* native) noexcept
    {
        auto& state = *static_cast<State*>(opaque);
        auto& owner = state.owner;
        auto& message = *static_cast<MSG*>(native);
        if (message.message != LayerMetadataTestGate::kMessage)
        {
            DispatchMessageThunk(&owner, native);
            return;
        }
        state.visited = true;
        const auto gate = owner._layerMetadataTestGate;
        state.held = gate && owner._layerMetadataTicket && owner._historyWork.Busy() && gate->captures.load() == 1 &&
                     gate->reservationHeld.load() && owner._canvasBrushReceipt.Read().outcome == HostOutcome::Pending &&
                     owner._session.PendingBrush() == nullptr;
        const auto unchanged = [&owner, &state]() noexcept
        {
            const auto* layer = owner._session.ActiveLayer();
            return layer && layer->id == state.target && layer->asset && layer->mask &&
                   owner._session.IsMaskSelected() && layer->opacity == .25 &&
                   owner._session.history.UndoCount() == 0 &&
                   owner._session.history.CurrentRevision() == state.historyRevision &&
                   !owner._session.PendingBrush() &&
                   MaskPaletteExtensionSameOriginal(layer->asset->image, state.image) &&
                   MaskPaletteExtensionSameOriginal(layer->mask->asset.image, state.mask) &&
                   MaskPaletteExtensionSameOriginal(layer->mask->asset.thumbnail, state.thumbnail);
        };
        const auto capturedDown = [&owner, &state]() noexcept
        {
            const auto* captured = owner._canvasBrushInput.CapturedContext(owner._canvasBrushInput.Opening());
            return captured && captured->documentSerial == owner._session.DocumentSerial() &&
                   captured->target == state.target && captured->mask && captured->maskWhite &&
                   captured->settings == kMaskPaletteExtensionDownTip &&
                   captured->tool == (state.scenario.eraser ? EditorTool::Eraser : EditorTool::Brush) &&
                   captured->kind == (state.scenario.eraser ? BrushKind::Erase : BrushKind::Paint);
        };
        const bool beforeUnchanged = unchanged();
        state.captured = capturedDown();
        gate->hold = false;

        // Swap the live mask foreground to black only after Down has captured white and diameter three.
        owner._session.SwapPaletteColors();
        owner._session.brushSettings = kMaskPaletteExtensionLiveTip;
        const auto pointer = [&owner](UINT kind, WPARAM modifiers, Vec2 point)
        {
            const auto hole = owner._chrome->HolePixels();
            const auto view = owner._viewport.ViewFromDocument(point, owner.DocumentSize());
            owner.OnPointer(kind, modifiers,
                            MAKELPARAM(static_cast<int>(std::lround(hole.left + view.x)),
                                       static_cast<int>(std::lround(hole.top + view.y))));
        };
        pointer(WM_MOUSEMOVE, MK_LBUTTON, Vec2{12, 8});
        pointer(WM_LBUTTONUP, 0, Vec2{20, 8});
        state.captured = state.captured && capturedDown();
        state.terminalAndUnchanged = beforeUnchanged && unchanged() && owner._canvasBrushInput.PendingCount() == 3 &&
                                     !owner._canvasBrushInput.Sampling() &&
                                     owner._canvasBrushReceipt.Read().outcome == HostOutcome::Pending;
        (void)SetEvent(gate->release.get());
    };

    for (const auto& scenario : cases)
    {
        if (!settle())
            return 5;
        _layerMetadataTestGate.reset();
        _opacityEdit.reset();
        _opacityRecorded.reset();
        _session.ClearDocument();
        _session.CreateDocument(kMaskPaletteExtensionWidth, kMaskPaletteExtensionHeight, true);
        _session.AddBlankLayer();
        if (!_session.ActiveLayerID())
            return 5;
        const Guid target = _session.ActiveLayerID().value();
        std::array<std::uint8_t, kMaskPaletteExtensionWidth * kMaskPaletteExtensionHeight * 4> bytes{};
        for (std::size_t offset = 0; offset < bytes.size(); offset += 4)
            std::copy(kMaskPaletteExtensionSourcePixel.begin(), kMaskPaletteExtensionSourcePixel.end(),
                      bytes.begin() + static_cast<std::ptrdiff_t>(offset));
        const Raster image = Raster::Make(kMaskPaletteExtensionWidth, kMaskPaletteExtensionHeight,
                                          RasterFormat::Bgra8888, bytes.data(), bytes.size());
        EditLayerRecordsForTest(_session,
                                [&](DocumentFixture& document)
                                {
                                    for (auto& layer : document.layers)
                                        if (layer.id == target)
                                            layer.asset = RasterAsset{image, Raster::MakeThumbnail(image), layer.name};
                                });
        // The selection covers the whole layer: AddMask(true) uses 255 - coverage, producing 32x24 black.
        // Unlike a no-selection 1x1 mask, this retains spatial center/end/outside controls and an eager thumbnail.
        _session.SelectAll();
        _session.AddMask(true);
        const auto* fixture = _session.ActiveLayer();
        if (!expect(scenario.name, "fixture-literal-image-and-black-mask",
                    fixture && fixture->asset && fixture->mask && _session.IsMaskSelected() && !_session.Selection() &&
                        MaskPaletteExtensionLiteralImage(fixture->asset->image) &&
                        MaskPaletteExtensionBlackMask(fixture->mask->asset.image) &&
                        MaskPaletteExtensionBlackMask(fixture->mask->asset.thumbnail)))
            return 5;
        if (!fixture || !fixture->mask)
            return 5;
        const Raster mask = fixture->mask->asset.image;
        const Raster thumbnail = fixture->mask->asset.thumbnail;
        _session.history.Reset();
        _session.history.MarkSaved();
        const Guid historyRevision = _session.history.CurrentRevision();
        _tool = scenario.eraser ? EditorTool::Eraser : EditorTool::Brush;
        _spacePan = false;
        _drag = PointerDrag{};
        _session.brushMode = BrushToolMode::Paint;
        _session.brushSettings = kMaskPaletteExtensionDownTip;
        _session.maskPaintWhite = true;
        _viewport.zoom = 1;
        _viewport.pan = {};
        _viewport.followsFit = false;
        DocumentChanged();

        OnLayerOpacity(.25, ChromeEditPhase::Preview);
        if (!settle())
            return 5;
        if (!expect(scenario.name, "opacity-preview-acknowledged-before-gate",
                    !_layerMetadataTestGate && _opacityEdit && _opacityEdit->start == 1 && _opacityEdit->shown == .25 &&
                        _session.ActiveLayer()->opacity == .25 && _session.history.UndoCount() == 0 &&
                        !_layerMetadataTicket))
            return 5;
        _layerMetadataTestGate = std::make_shared<LayerMetadataTestGate>();
        _layerMetadataTestGate->release.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (!_layerMetadataTestGate->release)
            return 5;
        if (scenario.failure)
            _layerMetadataTestGate->result = {JobStatus::Failed, E_OUTOFMEMORY};
        const auto hole = _chrome->HolePixels();
        const auto downView = _viewport.ViewFromDocument(Vec2{8, 8}, DocumentSize());
        OnPointer(WM_LBUTTONDOWN, MK_LBUTTON,
                  MAKELPARAM(static_cast<int>(std::lround(hole.left + downView.x)),
                             static_cast<int>(std::lround(hole.top + downView.y))));
        const auto* afterDown = _session.ActiveLayer();
        if (!expect(scenario.name, "physical-down-admitted-without-mutation",
                    _canvasBrushInput.PendingCount() == 1 && _canvasBrushInput.Sampling() &&
                        _canvasBrushReceipt.Read().outcome == HostOutcome::Pending && !_session.PendingBrush() &&
                        _session.history.UndoCount() == 0 && afterDown && afterDown->asset && afterDown->mask &&
                        afterDown->opacity == .25 && MaskPaletteExtensionSameOriginal(afterDown->asset->image, image) &&
                        MaskPaletteExtensionSameOriginal(afterDown->mask->asset.image, mask) &&
                        MaskPaletteExtensionSameOriginal(afterDown->mask->asset.thumbnail, thumbnail)))
            return 5;
        State state{*this, scenario, target, historyRevision, image, mask, thumbnail};
        if (SettleJobs(_jobs, 15000, pump, &state) != SettleResult::Settled)
            return 5;
        if (!expect(scenario.name, "older-opacity-worker-held-before-replay", state.visited && state.held) ||
            !expect(scenario.name, "down-captures-mask-palette-tip-target", state.captured) ||
            !expect(scenario.name, "held-move-up-retained-without-model-mutation", state.terminalAndUnchanged) ||
            !expect(scenario.name, "live-palette-swapped-and-tip-changed",
                    !_session.maskPaintWhite && _session.brushSettings == kMaskPaletteExtensionLiveTip))
            return 5;

        const auto* layer = _session.ActiveLayer();
        if (!expect(scenario.name, "image-raster-remains-exactly-unchanged",
                    layer && layer->asset && MaskPaletteExtensionSameOriginal(layer->asset->image, image)))
            return 5;
        if (!expect(scenario.name, "budgets-and-worker-owners-released",
                    MetadataAccountingSettledForTest() && _hostInputBudget.Used() == 0 &&
                        _layerMetadataTestGate->retired.load() && _layerMetadataTestGate->retiredOffOwner.load() &&
                        !_layerMetadataTicket && !_historyWork.Busy() && !_activeHostRequest &&
                        _hostRequests.Size() == 0 && !_canvasBrushInput.Busy() && !_session.PendingBrush() &&
                        !_composition.HasStroke()))
            return 5;
        if (!layer || !layer->mask)
            return 5;

        if (scenario.failure)
        {
            if (!expect(scenario.name, "failed-terminal-receipt-is-out-of-memory",
                        _canvasBrushReceipt.Read().outcome == HostOutcome::Failed &&
                            _canvasBrushReceipt.Read().error == E_OUTOFMEMORY) ||
                !expect(scenario.name, "failure-preserves-complete-original-mask",
                        MaskPaletteExtensionSameOriginal(layer->mask->asset.image, mask)) ||
                !expect(scenario.name, "failure-preserves-eager-mask-thumbnail",
                        MaskPaletteExtensionSameOriginal(layer->mask->asset.thumbnail, thumbnail)) ||
                !expect(scenario.name, "failed-mask-center-remains-black",
                        MaskPaletteExtensionGray(layer->mask->asset.image, 8, 8, 0)) ||
                !expect(scenario.name, "failed-mask-endpoint-remains-black",
                        MaskPaletteExtensionGray(layer->mask->asset.image, 20, 8, 0)) ||
                !expect(scenario.name, "failed-mask-outside-remains-black",
                        MaskPaletteExtensionGray(layer->mask->asset.image, 8, 11, 0)) ||
                !expect(scenario.name, "failure-preserves-original-history",
                        _session.history.UndoCount() == 0 && !_session.history.CanRedo() &&
                            _session.history.CurrentRevision() == historyRevision) ||
                !expect(scenario.name, "failure-retains-typed-opacity-field",
                        _opacityEdit && _opacityEdit->layer == target &&
                            _opacityEdit->documentSerial == _session.DocumentSerial() && _opacityEdit->start == 1 &&
                            _opacityEdit->shown == .25 && layer->opacity == .25))
                return 5;
            continue;
        }

        // A mask is never alpha-erased. Document_Tools and legacy EditorSession+Brush.swift define palette paint
        // even for Erase, and name its history entry Paint Mask. Both modes captured white at physical Down.
        if (!expect(scenario.name, "successful-terminal-receipt",
                    _canvasBrushReceipt.Read().outcome == HostOutcome::Succeeded &&
                        _canvasBrushReceipt.Read().error == S_OK) ||
            !expect(scenario.name, "mask-stroke-history-follows-separate-opacity-entry",
                    _session.history.UndoCount() == 2 && _session.history.UndoName() == L"Paint Mask" &&
                        !_opacityEdit && layer->opacity == .25) ||
            !expect(scenario.name, "committed-mask-center-is-captured-white",
                    MaskPaletteExtensionGray(layer->mask->asset.image, 8, 8, 255)) ||
            !expect(scenario.name, "committed-mask-endpoint-is-captured-white",
                    MaskPaletteExtensionGray(layer->mask->asset.image, 20, 8, 255)) ||
            !expect(scenario.name, "committed-mask-outside-small-tip-remains-black",
                    MaskPaletteExtensionGray(layer->mask->asset.image, 8, 11, 0)) ||
            !expect(scenario.name, "committed-eager-thumbnail-matches-mask-literals",
                    MaskPaletteExtensionSamePixels(layer->mask->asset.thumbnail, layer->mask->asset.image) &&
                        MaskPaletteExtensionGray(layer->mask->asset.thumbnail, 8, 8, 255) &&
                        MaskPaletteExtensionGray(layer->mask->asset.thumbnail, 20, 8, 255) &&
                        MaskPaletteExtensionGray(layer->mask->asset.thumbnail, 8, 11, 0)))
            return 5;
        OnCommand(ChromeHost::kCommandUndo);
        if (!settle())
            return 5;
        layer = _session.ActiveLayer();
        if (!expect(scenario.name, "first-undo-restores-mask-and-keeps-opacity",
                    layer && layer->asset && layer->mask && layer->opacity == .25 && _session.IsMaskSelected() &&
                        _session.history.UndoCount() == 1 && _session.history.UndoName() == L"Layer Opacity" &&
                        MaskPaletteExtensionSameOriginal(layer->asset->image, image) &&
                        MaskPaletteExtensionSameOriginal(layer->mask->asset.image, mask) &&
                        MaskPaletteExtensionSameOriginal(layer->mask->asset.thumbnail, thumbnail)))
            return 5;
        OnCommand(ChromeHost::kCommandUndo);
        if (!settle())
            return 5;
        layer = _session.ActiveLayer();
        if (!expect(scenario.name, "second-undo-restores-original-opacity",
                    layer && layer->asset && layer->mask && layer->opacity == 1 && _session.history.UndoCount() == 0 &&
                        MaskPaletteExtensionSameOriginal(layer->asset->image, image) &&
                        MaskPaletteExtensionSameOriginal(layer->mask->asset.image, mask) &&
                        MaskPaletteExtensionSameOriginal(layer->mask->asset.thumbnail, thumbnail)))
            return 5;
    }
    std::fprintf(stderr, "%s canvas-brush-mask-palette.summary checks=%d cases=4 expected-checks=68\n",
                 checks == kExpectedChecks ? "PASS" : "FAIL", checks);
    return checks == kExpectedChecks ? 0 : 5;
}
