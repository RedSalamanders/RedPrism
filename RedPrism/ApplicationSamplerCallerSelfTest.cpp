#include "Application.h"

#include "../Tests/Support/DocumentFixture.h"
#include "CanvasViewport.h"
#include "ChromeHost.h"
#include "KeyboardState.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <wil/resource.h>

int Application::RunSamplerCallerSelfTest() noexcept
{
    if (!_chrome || !_window || !_session.Document() || !_samplerState || !_hostSourceRegistered ||
        _chrome->ColorPickerOpen())
    {
        SelfTestFailure(L"The sampler caller self-test requires an initialized, idle host and a closed picker.\n");
        return 5;
    }
    if (SettleJobs(_jobs, 10000, DispatchMessageThunk, this) != SettleResult::Settled)
    {
        SelfTestFailure(L"Earlier work did not settle before sampler caller qualification.\n");
        return 5;
    }

    const CanvasDocument originalDocument = _session.Document().value();
    const std::optional<Guid> originalActive = _session.ActiveLayerID();
    const std::wstring originalPath = _documentPath;
    const CanvasViewport originalViewport = _viewport;
    const std::uint32_t originalForeground = _session.ForegroundColor();
    const std::uint32_t originalBackground = _session.BackgroundColor();
    const BrushSettings originalBrushSettings = _session.brushSettings;
    const GradientSettings originalGradientSettings = _session.gradientSettings;
    const ShapeKind originalShapeKind = _session.shapeKind;
    const double originalShapeRadius = _session.shapeCornerRadius;
    const BrushToolMode originalBrushMode = _session.brushMode;
    const bool originalMaskWhite = _session.maskPaintWhite;
    const EditorTool originalTool = _tool;
    const bool originalSpacePan = _spacePan;
    const KeyboardState::Detail::Synthetic originalSyntheticKeyboard = KeyboardState::Detail::g_synthetic;
    std::array<BYTE, 256> originalKeyboard{};
    if (!KeyboardState::Read(originalKeyboard))
    {
        SelfTestFailure(L"The sampler caller self-test could not preserve the current keyboard state.\n");
        return 5;
    }
    bool fixtureInstalled = false;

    const auto settle = [this]() noexcept
    { return SettleJobs(_jobs, 10000, DispatchMessageThunk, this) == SettleResult::Settled; };
    const auto cleanup = wil::scope_exit(
        [this, &settle, &fixtureInstalled, &originalDocument, &originalActive, &originalPath, originalViewport,
         originalForeground, originalBackground, originalBrushSettings, originalGradientSettings, originalShapeKind,
         originalShapeRadius, originalBrushMode, originalMaskWhite, originalTool, originalSpacePan,
         originalSyntheticKeyboard, &originalKeyboard]() noexcept
        {
            if (_chrome && _chrome->ColorPickerOpen())
                _chrome->CloseColorPicker(false);
            (void)CancelCanvasBrush();
            CancelHostContinuations();
            CancelGradientJobs();
            if (_window && GetCapture() == _window.get())
            {
                _releasingCapture = true;
                ReleaseCapture();
                _releasingCapture = false;
            }
            if (!settle())
                std::_Exit(5);
            CancelGradient();
            _session.CancelShape();
            _session.CancelBrush();
            _composition.EndStroke();
            if (fixtureInstalled)
                ReplaceDocument(originalDocument, originalActive, originalPath);
            _session.SetForegroundColor(originalForeground);
            _session.SetBackgroundColor(originalBackground);
            _session.brushSettings = originalBrushSettings;
            _session.gradientSettings = originalGradientSettings;
            _session.shapeKind = originalShapeKind;
            _session.shapeCornerRadius = originalShapeRadius;
            _session.brushMode = originalBrushMode;
            _session.maskPaintWhite = originalMaskWhite;
            _viewport = originalViewport;
            _tool = originalTool;
            _spacePan = originalSpacePan;
            if (_chrome)
                _chrome->SetTool(originalTool);
            SyncView();
            SyncChrome();
            KeyboardState::Detail::g_synthetic = originalSyntheticKeyboard;
            KeyboardState::Write(originalKeyboard);
        });

    const auto check = [](bool condition, const wchar_t* message, const char* control) noexcept
    {
        if (!condition)
        {
            SelfTestFailure(message);
            std::printf("FAIL %s\n", control);
            return false;
        }
        std::printf("PASS %s\n", control);
        return true;
    };

    const auto samplerSlotsClear = [this]() noexcept
    {
        return std::all_of(_samplerState->openings.begin(), _samplerState->openings.end(),
                           [](const SamplerOpening& opening)
                           { return opening.opening == 0 && !opening.jobAlive && opening.consumers == 0; });
    };
    if (!check(!_canvasBrushInput.Busy() && !_activeHostRequest && !_hostRequests.Size() && !_historyWork.Busy() &&
                   !_samplerHostTicket && _samplerHostJob == nullptr && samplerSlotsClear() &&
                   !_session.PendingBrush() && !_session.PendingGradient() && !_session.PendingShape(),
               L"The sampler caller self-test did not begin with an idle input graph.\n", "sampler-caller.starts-idle"))
        return 5;

    KeyboardState::UseSynthetic(true);
    constexpr int kWidth = 64;
    constexpr int kHeight = 32;
    constexpr std::uint32_t kBlue = 0xFF3366CCu;
    constexpr std::uint32_t kMagenta = 0xFFFF00FFu;
    constexpr std::uint32_t kBackground = 0xFF203040u;
    auto bgra = std::make_unique<std::vector<std::uint8_t>>(
        static_cast<std::size_t>(kWidth) * static_cast<std::size_t>(kHeight) * 4u, std::uint8_t{0});
    for (int y = 0; y < kHeight; ++y)
    {
        for (int x = 0; x < kWidth; ++x)
        {
            const std::size_t pixel =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(kWidth) + static_cast<std::size_t>(x)) * 4u;
            if (x < 16)
            {
                (*bgra)[pixel] = 0xCC;
                (*bgra)[pixel + 1] = 0x66;
                (*bgra)[pixel + 2] = 0x33;
                (*bgra)[pixel + 3] = 0xFF;
            }
            else if (x < 32)
            {
                (*bgra)[pixel] = 0x20;
                (*bgra)[pixel + 1] = 0x40;
                (*bgra)[pixel + 2] = 0x80;
                (*bgra)[pixel + 3] = 0xFF;
            }
            else
            {
                (*bgra)[pixel] = 0xA5;
                (*bgra)[pixel + 1] = 0x5A;
                (*bgra)[pixel + 2] = 0xC3;
                (*bgra)[pixel + 3] = 0x00;
            }
        }
    }
    for (int y = 0; y < kHeight; ++y)
    {
        const std::size_t partialAlphaPixel =
            (static_cast<std::size_t>(y) * static_cast<std::size_t>(kWidth) + 60u) * 4u;
        (*bgra)[partialAlphaPixel] = 31;
        (*bgra)[partialAlphaPixel + 1] = 63;
        (*bgra)[partialAlphaPixel + 2] = 127;
        (*bgra)[partialAlphaPixel + 3] = 128;

        const std::size_t aboveAlphaPixel = (static_cast<std::size_t>(y) * static_cast<std::size_t>(kWidth) + 62u) * 4u;
        (*bgra)[aboveAlphaPixel] = 31;
        (*bgra)[aboveAlphaPixel + 1] = 63;
        (*bgra)[aboveAlphaPixel + 2] = 127;
        (*bgra)[aboveAlphaPixel + 3] = 16;
    }
    const Raster source = Raster::Make(kWidth, kHeight, RasterFormat::Bgra8888, bgra->data(), bgra->size());
    if (!source.HasStorage())
    {
        SelfTestFailure(L"The sampler caller literal BGRA source could not be constructed.\n");
        return 5;
    }

    const auto installFixture = [this, &source, &fixtureInstalled, kWidth, kHeight, kBlue, kBackground]() noexcept
    {
        DocumentFixture fixture;
        fixture.id = Guid::Generate();
        fixture.width = kWidth;
        fixture.height = kHeight;
        ImageLayer layer = ImageLayer::Blank(L"Sampler caller literal source", Vec2{kWidth, kHeight});
        const Guid active = layer.id;
        layer.asset = RasterAsset{source, {}, layer.name};
        fixture.layers.push_back(std::move(layer));
        ReplaceDocument(std::move(fixture).Seal(), active, {});
        fixtureInstalled = true;
        _session.history.Reset();
        _session.history.MarkSaved();
        _session.SetForegroundColor(kBlue);
        _session.SetBackgroundColor(kBackground);
        _session.brushSettings = BrushSettings{6.0, 1.0, 0.0, 0.0, 0.0, 1.0};
        _session.gradientSettings =
            GradientSettings{GradientShape::Radial, GradientStyle::ForegroundToBackground, false, 0.375};
        _session.shapeKind = ShapeKind::Rectangle;
        _session.shapeCornerRadius = 0.0;
        _session.maskPaintWhite = false;
        _session.brushMode = BrushToolMode::Paint;
        _spacePan = false;
        _viewport.followsFit = false;
        _viewport.zoom = 1.0;
        _viewport.pan = {};
        _viewport.SetZoom(1.0, _viewport.Center(), DocumentSize());
        SyncView();
        SyncChrome();
    };
    const auto clientAt = [this](Vec2 documentPoint) noexcept
    {
        const CanvasHole hole = _chrome->HolePixels();
        const Vec2 view = _viewport.ViewFromDocument(documentPoint, DocumentSize());
        return MAKELPARAM(static_cast<int>(std::lround(hole.left + view.x)),
                          static_cast<int>(std::lround(hole.top + view.y)));
    };
    const auto pointer = [this, &clientAt](UINT message, Vec2 documentPoint) noexcept
    {
        const WPARAM modifiers = message == WM_LBUTTONDOWN || message == WM_MOUSEMOVE ? MK_LBUTTON : 0;
        OnPointer(message, modifiers, clientAt(documentPoint));
    };
    const auto pixelAt = [this](int x, int y) noexcept
    {
        std::array<std::uint8_t, 4> pixel{};
        const ImageLayer* layer = _session.ActiveLayer();
        if (layer && layer->asset)
        {
            layer->asset->image.ReadRegion(x - static_cast<int>(std::lround(layer->transform.origin.x)),
                                           y - static_cast<int>(std::lround(layer->transform.origin.y)), 1, 1,
                                           pixel.data(), 4, 0);
        }
        return pixel;
    };
    const auto hostReceipts = [this](EditorTool tool)
    {
        struct Pair final
        {
            std::optional<ContinuationReceipt> tool;
            std::optional<ContinuationReceipt> canvas;
        } result;
        for (std::size_t i = 0; i < _hostRequests.Size(); ++i)
        {
            const auto* pending = _hostRequests.PendingAt(i);
            if (!pending)
                continue;
            const auto& input = pending->input.Input();
            if (const auto* selectedTool = std::get_if<HostToolInput>(&input);
                selectedTool && selectedTool->tool == tool)
                result.tool = pending->input.Receipt();
            if (const auto* canvas = std::get_if<HostCanvasBrushInput>(&input);
                canvas && canvas->kind == HostCanvasInputKind::Brush)
                result.canvas = pending->input.Receipt();
        }
        return result;
    };

    const auto runCaller = [this, &installFixture, &pointer, &check, &hostReceipts, &pixelAt, &samplerSlotsClear,
                            settle, kBlue, kMagenta,
                            kBackground](EditorTool tool, Vec2 down, Vec2 up, const char* controlPrefix) noexcept
    {
        if (!settle())
            return false;
        installFixture();
        OnTool(EditorTool::Eyedropper);
        _session.brushSettings = BrushSettings{6.0, 1.0, 0.0, 0.0, 0.0, 1.0};
        _session.gradientSettings =
            GradientSettings{GradientShape::Radial, GradientStyle::ForegroundToBackground, false, 0.375};
        _session.shapeKind = ShapeKind::Rectangle;
        _session.shapeCornerRadius = 0.0;
        const Guid fixtureRevision = _session.history.CurrentRevision();
        const int fixtureUndo = _session.history.UndoCount();
        const std::uint64_t fixtureGeneration = _session.Generation();

        pointer(WM_LBUTTONDOWN, Vec2{0, 15}); // Opaque blue opening point.
        const auto* sampling = std::get_if<ColorSampling>(&_drag);
        const bool scalarOpening = sampling && sampling->opening.has_value() && !sampling->composite.has_value() &&
                                   !_samplerHostTicket && _samplerHostJob == nullptr;
        const std::uint64_t sampleOpening = scalarOpening ? sampling->opening.value() : 0;
        const auto* openingBeforeRelease = scalarOpening ? FindSamplerOpening(sampleOpening) : nullptr;
        const bool samplingStarted = scalarOpening && openingBeforeRelease &&
                                     openingBeforeRelease->points.PendingCount() == 1 &&
                                     openingBeforeRelease->receipt.Read().outcome == HostOutcome::Pending;
        pointer(WM_MOUSEMOVE, Vec2{20, 15}); // Opaque brown middle point.
        pointer(WM_MOUSEMOVE, Vec2{40, 15}); // The transparent tail point is accepted but carries no color.
        pointer(WM_LBUTTONUP, Vec2{60, 15}); // Release position must not become another sample.
        if (!check(samplingStarted && !std::holds_alternative<ColorSampling>(_drag) &&
                       openingBeforeRelease->points.PendingCount() == 3,
                   L"The eyedropper did not retain its scalar prefix or sampled its release coordinate.\n",
                   "sampler-caller.eyedropper.blue-brown-transparent-prefix-no-release-sample"))
            return false;

        OnTool(tool);                  // Queued behind the physical sampler Up.
        pointer(WM_LBUTTONDOWN, down); // Actual consumer press before any owner pump.
        const std::uint64_t canvasOpening = _canvasBrushInput.Opening();
        const auto* captured = _canvasBrushInput.CapturedContext(canvasOpening);
        const auto receipts = hostReceipts(tool);
        const ContinuationReceipt canvasReceipt = _canvasBrushReceipt;
        const ContinuationReceipt samplerReceipt = openingBeforeRelease->receipt;
        const bool eventSettingsCaptured =
            captured &&
            (tool == EditorTool::Brush ? captured->settings == BrushSettings{6.0, 1.0, 0.0, 0.0, 0.0, 1.0}
             : tool == EditorTool::Gradient
                 ? captured->gradient == GradientSettings{GradientShape::Radial, GradientStyle::ForegroundToBackground,
                                                          false, 0.375} &&
                       captured->background == kBackground
                 : captured->shape == ShapeKind::Rectangle && captured->cornerRadius == 0.0);
        const bool contextCaptured = captured && captured->tool == tool && captured->sampledColor.has_value() &&
                                     captured->sampledColor->opening == sampleOpening &&
                                     captured->sampledColor->fallbackArgb == kBlue && !captured->mask &&
                                     eventSettingsCaptured;
        if (!check(contextCaptured && receipts.tool.has_value() && receipts.canvas.has_value() &&
                       samplerReceipt.Read().outcome == HostOutcome::Pending &&
                       receipts.tool->Read().outcome == HostOutcome::Pending &&
                       canvasReceipt.Read().outcome == HostOutcome::Pending && !_activeHostRequest &&
                       _samplerHostJob == nullptr && !_samplerHostTicket,
                   L"The physical consumer did not capture the selected tool and exact pending sample dependency.\n",
                   "sampler-caller.consumer-captures-tool-and-sample-before-ack"))
            return false;

        const CanvasBrushContext capturedValues = captured ? *captured : CanvasBrushContext{};
        pointer(WM_LBUTTONUP, up);
        const auto* stillPendingSample = FindSamplerOpening(sampleOpening);
        if (!check(stillPendingSample && stillPendingSample->points.PendingCount() == 3 &&
                       _session.history.CurrentRevision() == fixtureRevision &&
                       _session.history.UndoCount() == fixtureUndo && _session.Generation() == fixtureGeneration,
                   L"The consumer release changed the sampler prefix or committed before settlement.\n",
                   "sampler-caller.release-keeps-three-point-prefix-and-document-read-only"))
            return false;

        OnPickerColor(PickerTarget::Foreground, ChromeEditPhase::Commit, kMagenta);
        if (!check(_session.ForegroundColor() == kMagenta && capturedValues.sampledColor.has_value() &&
                       capturedValues.sampledColor->fallbackArgb == kBlue &&
                       _session.history.CurrentRevision() == fixtureRevision &&
                       _session.history.UndoCount() == fixtureUndo && _session.Generation() == fixtureGeneration,
                   L"The manual color write did not follow consumer capture without editing the document.\n",
                   "sampler-caller.manual-magenta-ordering-keeps-captured-dependency"))
            return false;

        if (!settle())
        {
            SelfTestFailure(L"A selected sampler caller graph did not settle.\n");
            return false;
        }
        if (!check(samplerReceipt.Read().outcome == HostOutcome::Succeeded &&
                       receipts.tool->Read().outcome == HostOutcome::Succeeded &&
                       canvasReceipt.Read().outcome == HostOutcome::Succeeded &&
                       _session.ForegroundColor() == kMagenta && !_canvasBrushInput.Busy() && !_activeHostRequest &&
                       !_hostRequests.Size() && !_historyWork.Busy() && !_samplerHostTicket &&
                       _samplerHostJob == nullptr && _jobs.ReservedMemoryBytes() == 0 &&
                       _composition.UtilitySurfaceBytes() == 0 && samplerSlotsClear(),
                   L"Sampler, tool, and canvas receipts did not acknowledge cleanly or manual publication was "
                   L"overwritten.\n",
                   "sampler-caller.ordered-receipts-settle-and-suppress-sampler-publication"))
            return false;

        if (tool == EditorTool::Brush)
        {
            const auto center = pixelAt(40, 16);
            if (!check(center == std::array<std::uint8_t, 4>{0x20, 0x40, 0x80, 0xFF} &&
                           _session.history.UndoCount() == fixtureUndo + 1 &&
                           _session.history.UndoName() == L"Brush Stroke",
                       L"The deferred brush did not use the opaque middle sample for one committed stroke.\n",
                       "sampler-caller.brush-uses-older-brown-sample-one-history-entry"))
                return false;
        }
        else if (tool == EditorTool::Shape)
        {
            const auto center = pixelAt(45, 15);
            if (!check(center == std::array<std::uint8_t, 4>{0x20, 0x40, 0x80, 0xFF} &&
                           _session.history.UndoCount() == fixtureUndo + 1 &&
                           _session.history.UndoName() == L"Rectangle",
                       L"The deferred shape did not use the opaque middle sample for one committed rectangle.\n",
                       "sampler-caller.shape-uses-older-brown-sample-one-history-entry"))
                return false;
        }
        else if (tool == EditorTool::Gradient)
        {
            const GradientRequest request = CaptureGradientRequest();
            const bool exactRequest =
                _session.PendingGradient() && _session.PendingGradient()->capturedInputs.has_value() &&
                request.shape == GradientShape::Radial && request.opacity == 0.375 && request.start == down &&
                request.end == up &&
                request.color == std::array<double, 4>{128.0 / 255.0, 64.0 / 255.0, 32.0 / 255.0, 1.0} &&
                request.endColor == std::array<double, 4>{32.0 / 255.0, 48.0 / 255.0, 64.0 / 255.0, 1.0};
            if (!check(exactRequest,
                       L"The deferred gradient request lost its sampled first color or captured settings.\n",
                       "sampler-caller.gradient-request-has-literal-brown-and-event-settings"))
                return false;
            const int beforeCommitUndo = _session.history.UndoCount();
            CommitGradient();
            std::optional<ContinuationReceipt> commitReceipt;
            for (std::size_t i = 0; i < _hostRequests.Size(); ++i)
            {
                const auto* pending = _hostRequests.PendingAt(i);
                if (pending && std::holds_alternative<HostGradientApplyInput>(pending->input.Input()))
                    commitReceipt = pending->input.Receipt();
            }
            if (!check(
                    commitReceipt.has_value() && settle() && commitReceipt->Read().outcome == HostOutcome::Succeeded &&
                        _session.PendingGradient() == nullptr && _session.history.UndoCount() == beforeCommitUndo + 1 &&
                        _session.history.UndoName() == L"Gradient",
                    L"Explicit deferred gradient commit did not settle to exactly one history entry.\n",
                    "sampler-caller.gradient-explicit-commit-one-history-entry"))
                return false;
        }
        else
        {
            return false;
        }

        const bool graphEmpty = !_canvasBrushInput.Busy() && !_activeHostRequest && !_hostRequests.Size() &&
                                !_historyWork.Busy() && !_samplerHostTicket && _samplerHostJob == nullptr &&
                                !_gradientPreviewTicket && !_gradientCommitTicket && samplerSlotsClear() &&
                                _jobs.ReservedMemoryBytes() == 0 && _composition.UtilitySurfaceBytes() == 0;
        if (!check(graphEmpty, L"The settled caller retained sampler, canvas, or utility work.\n", controlPrefix))
            return false;
        return true;
    };

    if (!runCaller(EditorTool::Brush, Vec2{40, 16}, Vec2{40, 16}, "sampler-caller.brush.no-pending-graph"))
        return 5;
    if (!runCaller(EditorTool::Gradient, Vec2{40, 10}, Vec2{50, 20}, "sampler-caller.gradient.no-pending-graph"))
        return 5;
    if (!runCaller(EditorTool::Shape, Vec2{40, 10}, Vec2{50, 20}, "sampler-caller.shape.no-pending-graph"))
        return 5;

    const auto setSyntheticAlt = [](bool held) noexcept
    {
        std::array<BYTE, 256> keys{};
        if (held)
            keys[static_cast<std::size_t>(VK_MENU)] = 0x80;
        KeyboardState::Write(keys);
    };
    const auto runAltSampler = [this, &installFixture, &pointer, &check, &settle, &samplerSlotsClear, &setSyntheticAlt,
                                kBlue](EditorTool tool, const char* inputControl, const char* settledControl) noexcept
    {
        if (!settle())
            return false;
        installFixture();
        OnTool(tool);
        const Guid fixtureRevision = _session.history.CurrentRevision();
        const int fixtureUndo = _session.history.UndoCount();
        const std::uint64_t fixtureGeneration = _session.Generation();
        const auto sourceAuthority = _session.CaptureDocumentReadAuthority();

        setSyntheticAlt(true);
        const auto releaseAlt = wil::scope_exit([&setSyntheticAlt]() noexcept { setSyntheticAlt(false); });
        pointer(WM_LBUTTONDOWN, Vec2{0, 15}); // Opaque blue.
        const auto* sampling = std::get_if<ColorSampling>(&_drag);
        const std::uint64_t opening = sampling && sampling->opening ? sampling->opening.value() : 0;
        const auto* pending = opening != 0 ? FindSamplerOpening(opening) : nullptr;
        const ContinuationReceipt receipt = pending ? pending->receipt : ContinuationReceipt{};
        const bool scalarInput = KeyboardState::Held(VK_MENU) && _tool == tool && sampling &&
                                 sampling->opening.has_value() && !sampling->composite.has_value() && pending &&
                                 pending->points.PendingCount() == 1 &&
                                 receipt.Read().outcome == ContinuationOutcome::Pending && !_samplerHostTicket &&
                                 _samplerHostJob == nullptr && !_canvasBrushInput.Busy();
        pointer(WM_MOUSEMOVE, Vec2{20, 15}); // Opaque brown.
        pointer(WM_MOUSEMOVE, Vec2{40, 15}); // Transparent tail.
        pointer(WM_LBUTTONUP, Vec2{60, 15}); // Release coordinate is not sampled.
        setSyntheticAlt(false);
        const bool altReleased = !KeyboardState::Held(VK_MENU);
        const auto* ended = opening != 0 ? FindSamplerOpening(opening) : nullptr;
        if (!check(scalarInput && altReleased && ended && ended->points.PendingCount() == 3 &&
                       receipt.Read().outcome == ContinuationOutcome::Pending &&
                       std::holds_alternative<std::monostate>(_drag) && _session.ForegroundColor() == kBlue &&
                       _session.history.CurrentRevision() == fixtureRevision &&
                       _session.history.UndoCount() == fixtureUndo && _session.Generation() == fixtureGeneration &&
                       !_session.PendingBrush() && !_session.PendingGradient() && !_session.PendingShape() &&
                       !_samplerHostTicket && _samplerHostJob == nullptr && !_canvasBrushInput.Busy(),
                   L"Alt sampling ran inline, retained its drag after Up, or changed the document before pumping.\n",
                   inputControl))
            return false;

        if (!settle())
        {
            SelfTestFailure(L"An Alt sampler caller did not settle its accepted prefix.\n");
            return false;
        }
        if (!check(
                receipt.Read().outcome == ContinuationOutcome::Succeeded && _session.ForegroundColor() == 0xFF804020u &&
                    _session.history.CurrentRevision() == fixtureRevision &&
                    _session.history.UndoCount() == fixtureUndo && _session.Generation() == fixtureGeneration &&
                    sourceAuthority.has_value() && _session.IsDocumentReadCurrent(sourceAuthority.value()) &&
                    !_session.PendingBrush() && !_session.PendingGradient() && !_session.PendingShape() &&
                    !_canvasBrushInput.Busy() && !_activeHostRequest && !_hostRequests.Size() && !_historyWork.Busy() &&
                    !_samplerHostTicket && !_samplerHostStamp && _samplerHostJob == nullptr &&
                    !_gradientPreviewTicket && !_gradientCommitTicket && _jobs.ReservedMemoryBytes() == 0 &&
                    _composition.UtilitySurfaceBytes() == 0 && samplerSlotsClear() &&
                    FindSamplerOpening(opening) == nullptr,
                L"Alt sampling failed to publish brown cleanly or left document/tool work behind.\n", settledControl))
            return false;
        return true;
    };

    if (!runAltSampler(EditorTool::Brush, "sampler-caller.alt-brush.scalar-prefix-ends-before-pump",
                       "sampler-caller.alt-brush.opaque-middle-publishes-without-document-edit"))
        return 5;
    if (!runAltSampler(EditorTool::Heal, "sampler-caller.alt-heal.scalar-prefix-ends-before-pump",
                       "sampler-caller.alt-heal.opaque-middle-publishes-without-document-edit"))
        return 5;
    if (!runAltSampler(EditorTool::Gradient, "sampler-caller.alt-gradient.scalar-prefix-ends-before-pump",
                       "sampler-caller.alt-gradient.opaque-middle-publishes-without-document-edit"))
        return 5;

    const auto runAlphaSample = [this, &installFixture, &pointer, &check, &settle,
                                 &samplerSlotsClear](int sampleX, std::uint32_t expectedArgb, const char* inputControl,
                                                     const char* settledControl) noexcept
    {
        if (!settle())
            return false;
        installFixture();
        OnTool(EditorTool::Eyedropper);
        const Guid fixtureRevision = _session.history.CurrentRevision();
        const int fixtureUndo = _session.history.UndoCount();
        const std::uint64_t fixtureGeneration = _session.Generation();

        pointer(WM_LBUTTONDOWN, Vec2{static_cast<double>(sampleX), 15.0});
        const auto* sampling = std::get_if<ColorSampling>(&_drag);
        const std::uint64_t opening = sampling && sampling->opening ? sampling->opening.value() : 0;
        const auto* pending = opening != 0 ? FindSamplerOpening(opening) : nullptr;
        const ContinuationReceipt receipt = pending ? pending->receipt : ContinuationReceipt{};
        const bool oneScalarPoint = sampling && sampling->opening.has_value() && !sampling->composite.has_value() &&
                                    pending && pending->points.PendingCount() == 1 &&
                                    receipt.Read().outcome == ContinuationOutcome::Pending && !_samplerHostTicket &&
                                    _samplerHostJob == nullptr;
        pointer(WM_LBUTTONUP, Vec2{0.0, 15.0}); // The opaque blue release coordinate must not be sampled.
        const auto* sealed = opening != 0 ? FindSamplerOpening(opening) : nullptr;
        if (!check(oneScalarPoint && sealed && sealed->points.PendingCount() == 1 &&
                       !std::holds_alternative<ColorSampling>(_drag) &&
                       _session.history.CurrentRevision() == fixtureRevision &&
                       _session.history.UndoCount() == fixtureUndo && _session.Generation() == fixtureGeneration,
                   L"The alpha sample did not seal exactly its Down point or sampled the blue release coordinate.\n",
                   inputControl))
            return false;

        if (!settle())
        {
            SelfTestFailure(L"An alpha sampler opening did not settle.\n");
            return false;
        }
        return check(
            receipt.Read().outcome == ContinuationOutcome::Succeeded && _session.ForegroundColor() == expectedArgb &&
                _session.history.CurrentRevision() == fixtureRevision && _session.history.UndoCount() == fixtureUndo &&
                _session.Generation() == fixtureGeneration && !_samplerHostTicket && !_samplerHostStamp &&
                _samplerHostJob == nullptr && !_activeHostRequest && !_hostRequests.Size() && !_historyWork.Busy() &&
                _jobs.ReservedMemoryBytes() == 0 && _composition.UtilitySurfaceBytes() == 0 && samplerSlotsClear() &&
                FindSamplerOpening(opening) == nullptr,
            L"The alpha sample produced the wrong straight color, changed document history, or left sampler work.\n",
            settledControl);
    };
    if (!runAlphaSample(60, 0xFFFD7E3Eu, "sampler-caller.alpha-half.down-only-no-release-sample",
                        "sampler-caller.alpha-half.straight-color-and-retirement"))
        return 5;
    if (!runAlphaSample(62, 0xFFFFFFFFu, "sampler-caller.alpha-low.down-only-no-release-sample",
                        "sampler-caller.alpha-low.clamped-straight-color-and-retirement"))
        return 5;

    installFixture();
    _session.SetForegroundColor(kMagenta);
    OnTool(EditorTool::Brush);
    _session.SetForegroundColor(kMagenta);
    _session.brushSettings = BrushSettings{6.0, 1.0, 1.0, 0.0, 1.0, 1.0};
    auto unusedDependency = CaptureSamplerColorDependency();
    if (unusedDependency)
        ReleaseSamplerColorDependency(unusedDependency.value());
    if (!check(!unusedDependency, L"A new press after manual input unexpectedly borrowed an old sampler prefix.\n",
               "sampler-caller.younger-press-has-no-old-sample-dependency"))
        return 5;
    pointer(WM_LBUTTONDOWN, Vec2{55, 16});
    pointer(WM_LBUTTONUP, Vec2{55, 16});
    if (!check(pixelAt(55, 16) == std::array<std::uint8_t, 4>{0xFF, 0x00, 0xFF, 0xFF} &&
                   _session.ForegroundColor() == kMagenta,
               L"A brush press after the manual write did not use its magenta live palette color.\n",
               "sampler-caller.younger-press-paints-captured-magenta"))
        return 5;

    enum class Terminal : std::uint8_t
    {
        CapturedLeave,
        CapturelessLeave,
        CaptureChanged,
        CancelMode,
        Escape
    };
    struct TerminalCase final
    {
        Terminal terminal;
        const char* immediate;
        const char* retired;
    };
    constexpr std::array<TerminalCase, 5> terminals{{
        {Terminal::CapturedLeave, "sampler-caller.terminal.captured-leave-then-up-seals-only-prefix",
         "sampler-caller.terminal.captured-leave-prefix-acknowledged"},
        {Terminal::CapturelessLeave, "sampler-caller.terminal.captureless-leave-seals-only-prefix",
         "sampler-caller.terminal.captureless-leave-prefix-acknowledged"},
        {Terminal::CaptureChanged, "sampler-caller.terminal.capture-change-seals-only-prefix",
         "sampler-caller.terminal.capture-change-prefix-acknowledged"},
        {Terminal::CancelMode, "sampler-caller.terminal.cancel-mode-seals-only-prefix",
         "sampler-caller.terminal.cancel-mode-prefix-acknowledged"},
        {Terminal::Escape, "sampler-caller.terminal.escape-cancels-and-releases-capture",
         "sampler-caller.terminal.escape-acknowledges-without-late-publication"},
    }};
    for (const auto& test : terminals)
    {
        if (!settle())
            return 5;
        installFixture();
        OnTool(EditorTool::Eyedropper);
        _chrome->SetCanvasFocused(true);
        const auto sourceAuthority = _session.CaptureDocumentReadAuthority();
        const auto revision = _session.history.CurrentRevision();
        const auto undo = _session.history.UndoCount();
        pointer(WM_LBUTTONDOWN, Vec2{0, 15});
        pointer(WM_MOUSEMOVE, Vec2{20, 15});
        pointer(WM_MOUSEMOVE, Vec2{40, 15});
        const auto* sampling = std::get_if<ColorSampling>(&_drag);
        const auto opening = sampling && sampling->opening ? sampling->opening.value() : 0;
        const auto* pending = FindSamplerOpening(opening);
        if (!pending || pending->points.PendingCount() != 3 || GetCapture() != _window.get())
        {
            SelfTestFailure(L"The physical sampler terminal fixture did not retain its captured prefix.\n");
            return 5;
        }
        const ContinuationReceipt receipt = pending->receipt;
        if (test.terminal == Terminal::CapturedLeave)
        {
            (void)SendMessageW(_window.get(), WM_MOUSELEAVE, 0, 0);
            pointer(WM_MOUSEMOVE, Vec2{20, 15}); // Capture-held leave must still accept a real subsequent move.
            if (!check(GetCapture() == _window.get() && std::holds_alternative<ColorSampling>(_drag) &&
                           pending->points.PendingCount() == 4 && receipt.Read().outcome == HostOutcome::Pending,
                       L"Captured pointer leave incorrectly ended the live sampler gesture.\n",
                       "sampler-caller.terminal.captured-leave-remains-live"))
                return 5;
            pointer(WM_LBUTTONUP, Vec2{0, 15}); // An opaque release location must not replace the brown prefix.
        }
        else if (test.terminal == Terminal::CapturelessLeave)
        {
            _releasingCapture = true;
            (void)ReleaseCapture();
            _releasingCapture = false;
            (void)SendMessageW(_window.get(), WM_MOUSELEAVE, 0, 0);
        }
        else if (test.terminal == Terminal::CaptureChanged)
        {
            (void)ReleaseCapture(); // Delivers the real WM_CAPTURECHANGED through this HWND's window procedure.
        }
        else if (test.terminal == Terminal::CancelMode)
            (void)SendMessageW(_window.get(), WM_CANCELMODE, 0, 0);
        else if (!HandleAccelerator(VK_ESCAPE, 0))
        {
            SelfTestFailure(L"The keyboard route did not take Escape for a physical sampler gesture.\n");
            return 5;
        }
        const bool cancelled = test.terminal == Terminal::Escape;
        const auto* ended = FindSamplerOpening(opening);
        if (!check(std::holds_alternative<std::monostate>(_drag) && GetCapture() != _window.get() &&
                       (cancelled || (ended && ended->points.PendingCount() ==
                                                   (test.terminal == Terminal::CapturedLeave ? 4u : 3u))) &&
                       _session.ForegroundColor() == kBlue && sourceAuthority &&
                       _session.IsDocumentReadCurrent(sourceAuthority.value()) &&
                       _session.history.CurrentRevision() == revision && _session.history.UndoCount() == undo,
                   L"A sampler terminal kept capture, invented a point or mutated the document inline.\n",
                   test.immediate))
            return 5;
        if (!check(
                settle() && receipt.Read().outcome == (cancelled ? HostOutcome::Cancelled : HostOutcome::Succeeded) &&
                    _session.ForegroundColor() == (cancelled ? kBlue : 0xFF804020u) &&
                    _session.IsDocumentReadCurrent(sourceAuthority.value()) &&
                    _session.history.CurrentRevision() == revision && _session.history.UndoCount() == undo &&
                    !FindSamplerOpening(opening) && samplerSlotsClear() && !_samplerHostTicket && !_samplerHostStamp &&
                    !_samplerHostJob && !_activeHostRequest && !_hostRequests.Size() && !_historyWork.Busy() &&
                    _jobs.ReservedMemoryBytes() == 0 && _composition.UtilitySurfaceBytes() == 0,
                L"A physical sampler terminal changed its outcome/prefix or retained graph resources.\n", test.retired))
            return 5;
    }

    std::printf("PASS sampler-caller.fixture-restore-scheduled\n");
    return 0;
}
