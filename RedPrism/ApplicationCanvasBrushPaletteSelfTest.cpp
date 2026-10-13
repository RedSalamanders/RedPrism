#include "Application.h"

#include "../Tests/Support/DocumentFixture.h"
#include "ChromeHost.h"
#include "LayerMetadataTestGate.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <utility>
#include <vector>

int Application::RunCanvasBrushPaletteSelfTest() noexcept
{
    constexpr int kWidth = 32;
    constexpr int kHeight = 24;
    constexpr std::array<std::uint8_t, 4> kSourcePixel{19, 71, 129, 255};
    const auto originalViewport = _viewport;
    const auto originalSettings = _session.brushSettings;
    const auto originalMaskWhite = _session.maskPaintWhite;
    const auto originalMode = _session.brushMode;
    const auto originalTool = _tool;
    int checks = 0;
    const auto expect = [&checks](const char* scenario, const char* name, bool condition) noexcept
    {
        std::fprintf(stderr, "%s canvas-brush-palette.%s.%s\n", condition ? "PASS" : "FAIL", scenario, name);
        checks += condition ? 1 : 0;
        return condition;
    };
    const auto settle = [this]() noexcept
    { return SettleJobs(_jobs, 15000, DispatchMessageThunk, this) == SettleResult::Settled; };
    const auto cleanup = wil::scope_exit(
        [this, originalViewport, originalSettings, originalMaskWhite, originalMode, originalTool, &settle]
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
            _session.brushSettings = originalSettings;
            _session.maskPaintWhite = originalMaskWhite;
            _session.brushMode = originalMode;
            _tool = originalTool;
            (void)SendMessageW(_window.get(), WM_ACTIVATEAPP, TRUE, 0);
        });

    enum class Mode
    {
        EraserSuccess,
        MaskSuccess,
        EraserFailure,
        MaskFailure,
    };
    struct State final
    {
        Application& owner;
        Mode mode;
        bool visited = false;
        bool held = false;
    };
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
        const auto* captured = owner._canvasBrushInput.CapturedContext(owner._canvasBrushInput.Opening());
        const bool eraser = state.mode == Mode::EraserSuccess || state.mode == Mode::EraserFailure;
        const auto* layer = owner._session.ActiveLayer();
        const auto* originalBytes = layer && layer->asset ? layer->asset->image.Data() : nullptr;
        bool noPixelChange = originalBytes && layer->asset->image.Width() == kWidth &&
                             layer->asset->image.Height() == kHeight && layer->asset->image.Stride() == kWidth * 4;
        if (noPixelChange)
        {
            for (int y = 0; y < kHeight && noPixelChange; ++y)
                for (int x = 0; x < kWidth; ++x)
                {
                    const auto offset = static_cast<std::size_t>(y) * layer->asset->image.Stride() + x * 4;
                    if (originalBytes[offset] != 19 || originalBytes[offset + 1] != 71 ||
                        originalBytes[offset + 2] != 129 || originalBytes[offset + 3] != 255)
                    {
                        noPixelChange = false;
                        break;
                    }
                }
        }
        bool noMaskChange = true;
        if (!eraser)
        {
            const auto* maskBytes = layer && layer->mask ? layer->mask->asset.image.Data() : nullptr;
            const auto* thumbnailBytes = layer && layer->mask ? layer->mask->asset.thumbnail.Data() : nullptr;
            noMaskChange = maskBytes && layer->mask->asset.image.Width() == kWidth &&
                           layer->mask->asset.image.Height() == kHeight && layer->mask->asset.thumbnail.HasStorage() &&
                           layer->mask->asset.thumbnail.Format() == RasterFormat::Gray8 && thumbnailBytes &&
                           layer->mask->asset.thumbnail.Width() == kWidth &&
                           layer->mask->asset.thumbnail.Height() == kHeight;
            if (noMaskChange)
            {
                for (int y = 0; y < kHeight && noMaskChange; ++y)
                    for (int x = 0; x < kWidth; ++x)
                        if (maskBytes[static_cast<std::size_t>(y) * layer->mask->asset.image.Stride() + x] != 255 ||
                            thumbnailBytes[static_cast<std::size_t>(y) * layer->mask->asset.thumbnail.Stride() + x] !=
                                255)
                        {
                            noMaskChange = false;
                            break;
                        }
            }
        }
        state.held = owner._layerMetadataTicket && owner._historyWork.Busy() && gate->captures.load() == 1 &&
                     gate->reservationHeld.load() && owner._session.ActiveLayer()->opacity == .25 &&
                     owner._session.history.UndoCount() == 0 && owner._session.PendingBrush() == nullptr &&
                     owner._canvasBrushReceipt.Read().outcome == HostOutcome::Pending && captured &&
                     captured->settings == BrushSettings{3, 1, 1, 0, 0, 1} && captured->mask == !eraser &&
                     captured->maskWhite == false &&
                     captured->tool == (eraser ? EditorTool::Eraser : EditorTool::Brush) &&
                     captured->kind == (eraser ? BrushKind::Erase : BrushKind::Paint) && noPixelChange && noMaskChange;

        gate->hold = false;
        const auto pointer = [&owner](UINT kind, WPARAM modifiers, Vec2 point)
        {
            const auto hole = owner._chrome->HolePixels();
            const auto view = owner._viewport.ViewFromDocument(point, owner.DocumentSize());
            owner.OnPointer(kind, modifiers,
                            MAKELPARAM(static_cast<int>(std::lround(hole.left + view.x)),
                                       static_cast<int>(std::lround(hole.top + view.y))));
        };
        // Change both the tip and the live palette only after the accepted Down captured its values.
        owner._session.brushSettings = BrushSettings{9, 1, 1, 0, 1, 0};
        owner._session.maskPaintWhite = true;
        pointer(WM_MOUSEMOVE, MK_LBUTTON, Vec2{12, 8});
        pointer(WM_LBUTTONUP, 0, Vec2{20, 8});
        (void)SetEvent(gate->release.get());
    };

    constexpr std::array cases{
        std::pair{"eraser-success", Mode::EraserSuccess}, std::pair{"mask-success", Mode::MaskSuccess},
        std::pair{"eraser-failure", Mode::EraserFailure}, std::pair{"mask-failure", Mode::MaskFailure}};
    for (const auto& [name, mode] : cases)
    {
        if (!settle())
            return 5;
        _session.ClearDocument();
        _session.CreateDocument(kWidth, kHeight, true);
        _session.AddBlankLayer();

        std::vector<std::uint8_t> sourceBytes(static_cast<std::size_t>(kWidth) * kHeight * kSourcePixel.size());
        for (std::size_t offset = 0; offset < sourceBytes.size(); offset += kSourcePixel.size())
            std::copy(kSourcePixel.begin(), kSourcePixel.end(), sourceBytes.begin() + offset);
        const Raster source =
            Raster::Make(kWidth, kHeight, RasterFormat::Bgra8888, sourceBytes.data(), sourceBytes.size());
        if (!source.HasStorage() || !_session.ActiveLayerID())
            return 5;
        const Guid layerId = _session.ActiveLayerID().value();
        EditLayerRecordsForTest(_session,
                                [&](DocumentFixture& document)
                                {
                                    for (auto& layer : document.layers)
                                        if (layer.id == layerId)
                                            layer.asset =
                                                RasterAsset{source, Raster::MakeThumbnail(source), layer.name};
                                });

        const bool mask = mode == Mode::MaskSuccess || mode == Mode::MaskFailure;
        Raster originalMask;
        Raster originalMaskThumbnail;
        if (mask)
        {
            // AddMask(false) shows the selected pixels. Select All therefore gives this 32x24 layer a white mask.
            // The session path also creates its required eager semantic Gray8 thumbnail.
            _session.SelectAll();
            _session.AddMask(false);
            const auto* layer = _session.ActiveLayer();
            if (!layer || !layer->mask || layer->mask->asset.image.Format() != RasterFormat::Gray8 ||
                !layer->mask->asset.thumbnail.HasStorage() ||
                layer->mask->asset.thumbnail.Format() != RasterFormat::Gray8 ||
                layer->mask->asset.image.Width() != kWidth || layer->mask->asset.image.Height() != kHeight ||
                layer->mask->asset.thumbnail.Width() > 96 || layer->mask->asset.thumbnail.Height() > 96 ||
                !_session.IsMaskSelected())
                return 5;
            originalMask = layer->mask->asset.image;
            originalMaskThumbnail = layer->mask->asset.thumbnail;
        }

        _session.history.Reset();
        _session.history.MarkSaved();
        _opacityEdit.reset();
        _opacityRecorded.reset();
        _tool = mask ? EditorTool::Brush : EditorTool::Eraser;
        _spacePan = false;
        _drag = PointerDrag{};
        _session.brushMode = BrushToolMode::Paint;
        _session.brushSettings = BrushSettings{3, 1, 1, 0, 0, 1};
        _session.maskPaintWhite = false;
        _layerMetadataTestGate.reset();
        _viewport.zoom = 1;
        _viewport.pan = {};
        _viewport.followsFit = false;
        DocumentChanged();

        const bool failed = mode == Mode::EraserFailure || mode == Mode::MaskFailure;
        OnLayerOpacity(.25, ChromeEditPhase::Preview);
        if (!settle() || !_opacityEdit || _session.ActiveLayer()->opacity != .25)
            return 5;
        if (!expect(name, "opacity-preview-acknowledges-without-closure-gate",
                    !_layerMetadataTestGate && _opacityEdit && _session.ActiveLayer()->opacity == .25 &&
                        _session.history.UndoCount() == 0))
            return 5;

        // The typed preview is complete. This gate belongs only to the closure started by physical Down.
        _layerMetadataTestGate = std::make_shared<LayerMetadataTestGate>();
        _layerMetadataTestGate->release.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (!_layerMetadataTestGate->release)
            return 5;
        if (failed)
            _layerMetadataTestGate->result = {JobStatus::Failed, E_OUTOFMEMORY};

        const auto clientAt = [this](Vec2 point)
        {
            const auto hole = _chrome->HolePixels();
            const auto view = _viewport.ViewFromDocument(point, DocumentSize());
            return MAKELPARAM(static_cast<int>(std::lround(hole.left + view.x)),
                              static_cast<int>(std::lround(hole.top + view.y)));
        };
        const LPARAM down = clientAt(Vec2{8, 8});
        OnPointer(WM_LBUTTONDOWN, MK_LBUTTON, down);
        if (!expect(name, "down-admits-without-pixels-or-history",
                    _canvasBrushInput.PendingCount() == 1 &&
                        _canvasBrushReceipt.Read().outcome == HostOutcome::Pending && !_session.PendingBrush() &&
                        _session.history.UndoCount() == 0 && _session.ActiveLayer()->opacity == .25))
            return 5;

        State state{*this, mode};
        if (SettleJobs(_jobs, 15000, pump, &state) != SettleResult::Settled ||
            !expect(name, "held-opacity-worker-and-captured-down-state", state.visited && state.held))
            return 5;

        const auto* layer = _session.ActiveLayer();
        if (!layer || !layer->asset)
            return 5;
        if (failed)
        {
            const auto* originalImage = source.Data();
            const auto* currentImage = layer->asset->image.Data();
            if (!expect(name, "failure-preserves-all-original-image-bytes",
                        originalImage && currentImage && layer->asset->image.Width() == kWidth &&
                            layer->asset->image.Height() == kHeight &&
                            layer->asset->image.Stride() == source.Stride() &&
                            std::equal(originalImage, originalImage + source.Stride() * kHeight, currentImage)))
                return 5;
            if (mask)
            {
                const auto* originalMaskBytes = originalMask.Data();
                const auto* maskBytes = layer->mask ? layer->mask->asset.image.Data() : nullptr;
                const auto* originalThumbnailBytes = originalMaskThumbnail.Data();
                const auto* thumbnailBytes = layer->mask ? layer->mask->asset.thumbnail.Data() : nullptr;
                bool white = maskBytes && originalMaskBytes && layer->mask->asset.image.Width() == kWidth &&
                             layer->mask->asset.image.Height() == kHeight &&
                             layer->mask->asset.image.Stride() == originalMask.Stride() && originalThumbnailBytes &&
                             thumbnailBytes && layer->mask->asset.thumbnail.Width() == originalMaskThumbnail.Width() &&
                             layer->mask->asset.thumbnail.Height() == originalMaskThumbnail.Height() &&
                             layer->mask->asset.thumbnail.Stride() == originalMaskThumbnail.Stride() &&
                             layer->mask->asset.thumbnail.HasStorage() &&
                             layer->mask->asset.thumbnail.Format() == RasterFormat::Gray8;
                if (white)
                    white =
                        std::equal(originalMaskBytes, originalMaskBytes + originalMask.Stride() * kHeight, maskBytes);
                if (white)
                    white = std::equal(originalThumbnailBytes,
                                       originalThumbnailBytes +
                                           originalMaskThumbnail.Stride() * originalMaskThumbnail.Height(),
                                       thumbnailBytes);
                if (!expect(name, "failure-preserves-original-white-mask-and-eager-thumbnail", white))
                    return 5;
            }
            if (!expect(name, "failure-adds-no-stroke-history",
                        _canvasBrushReceipt.Read().outcome == HostOutcome::Failed &&
                            _canvasBrushReceipt.Read().error == E_OUTOFMEMORY && _session.history.UndoCount() == 0 &&
                            !_canvasBrushInput.Busy() && !_session.PendingBrush()))
                return 5;
            if (!expect(name, "failure-keeps-typed-opacity-field",
                        _opacityEdit && _opacityEdit->start == 1 && _opacityEdit->shown == .25 &&
                            _session.ActiveLayer()->opacity == .25))
                return 5;
            continue;
        }

        if (!expect(
                name, "closure-and-stroke-commit-as-separate-history-entries",
                _session.history.UndoCount() == 2 && _session.history.UndoName() == (mask ? L"Paint Mask" : L"Erase") &&
                    _session.ActiveLayer()->opacity == .25 && !_opacityEdit && !_canvasBrushInput.Busy() &&
                    !_session.PendingBrush() && MetadataAccountingSettledForTest() && _hostInputBudget.Used() == 0 &&
                    _layerMetadataTestGate->retired.load() && _layerMetadataTestGate->retiredOffOwner.load()))
            return 5;

        std::array<std::uint8_t, 4> center{};
        std::array<std::uint8_t, 4> outside{};
        std::array<std::uint8_t, 4> endpoint{};
        const Raster& result = mask ? layer->mask->asset.image : layer->asset->image;
        const int channels = mask ? 1 : 4;
        std::array<std::uint8_t, 4> readCenter{};
        std::array<std::uint8_t, 4> readOutside{};
        std::array<std::uint8_t, 4> readEndpoint{};
        result.ReadRegion(8, 8, 1, 1, readCenter.data(), channels, 0);
        result.ReadRegion(8, 11, 1, 1, readOutside.data(), channels, 0);
        result.ReadRegion(20, 8, 1, 1, readEndpoint.data(), channels, 0);
        if (mask)
        {
            center[0] = readCenter[0];
            outside[0] = readOutside[0];
            endpoint[0] = readEndpoint[0];
        }
        else
        {
            center = readCenter;
            outside = readOutside;
            endpoint = readEndpoint;
        }
        const bool pixels = mask ? center[0] == 0 && outside[0] == 255 && endpoint[0] == 0
                                 : center == std::array<std::uint8_t, 4>{0, 0, 0, 0} && outside == kSourcePixel &&
                                       endpoint == std::array<std::uint8_t, 4>{0, 0, 0, 0};
        if (!expect(name, mask ? "captured-black-mask-tip-and-endpoint-only" : "captured-eraser-tip-and-endpoint-only",
                    pixels && _session.brushSettings == BrushSettings{9, 1, 1, 0, 1, 0} && _session.maskPaintWhite))
            return 5;
        if (mask)
        {
            const auto* sourceBytesNow = layer->asset->image.Data();
            if (!expect(name, "mask-stroke-preserves-original-image-bytes",
                        sourceBytesNow && layer->asset->image.Stride() == source.Stride() &&
                            std::equal(source.Data(), source.Data() + source.Stride() * kHeight, sourceBytesNow)))
                return 5;
            const auto& thumbnail = layer->mask->asset.thumbnail;
            std::array<std::uint8_t, 1> thumbCenter{};
            std::array<std::uint8_t, 1> thumbOutside{};
            thumbnail.ReadRegion(8, 8, 1, 1, thumbCenter.data(), 1, 0);
            thumbnail.ReadRegion(8, 11, 1, 1, thumbOutside.data(), 1, 0);
            if (!expect(name, "committed-mask-keeps-eager-semantic-thumbnail",
                        thumbnail.HasStorage() && thumbnail.Format() == RasterFormat::Gray8 &&
                            thumbnail.Width() == kWidth && thumbnail.Height() == kHeight && thumbCenter[0] == 0 &&
                            thumbOutside[0] == 255))
                return 5;
        }
    }
    std::fprintf(stderr, "PASS canvas-brush-palette.summary checks=%d cases=4\n", checks);
    return 0;
}
