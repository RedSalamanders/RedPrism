#include "Application.h"

#include "CanvasSize.h"
#include "ChromeHost.h"
#include "Resources.h"
#include "SystemClipboard.h"
#include "resource.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <vector>

// The size cards (C16, Specs/UI/UI_ApplicationChrome.md "Size cards", Specs/Document/Document_Tools.md "Canvas
// size, image size and crop"): the Application owns the draft the card edits, applies it through the resizers as
// one history entry, and drives the cards in the self-test.

namespace
{
[[nodiscard]] CanvasExtensionColor ColorOf(uint32_t argb) noexcept
{
    return CanvasExtensionColor{((argb >> 16) & 0xFFu) / 255.0, ((argb >> 8) & 0xFFu) / 255.0, (argb & 0xFFu) / 255.0};
}

} // namespace

bool Application::SizeCardOpen() const noexcept
{
    return _sizeDialog.kind != SizeCardKind::None;
}

void Application::OpenSizeCard(SizeCardKind kind) noexcept
{
    if (kind == SizeCardKind::None)
    {
        return;
    }
    const bool hasDocument = _session.Document().has_value();
    if (kind != SizeCardKind::NewCanvas && !hasDocument)
    {
        return;
    }
    if (!_hostExecuting && _session.HasColorEdit())
    {
        const int command = kind == SizeCardKind::NewCanvas    ? ChromeHost::kCommandNewCanvas
                            : kind == SizeCardKind::CanvasSize ? ChromeHost::kCommandCanvasSize
                                                               : ChromeHost::kCommandImageSize;
        ReportHostAdmission(QueueHostInput(HostCommandInput{command}));
        return;
    }
    if (_sizeInvocation == UINT64_MAX)
    {
        ReportBudgetRefusal();
        return;
    }
    ++_sizeInvocation;
    _sizeDialog.kind = kind;
    if (kind == SizeCardKind::NewCanvas)
    {
        // An image on the clipboard suggests its size, as the Mac sheet does.
        Raster clipboard;
        if (_window && SystemClipboard::HasImage() &&
            SystemClipboard::ReadImage(_window.get(), kMaxLayerPixels, clipboard) && clipboard.HasStorage() &&
            clipboard.Width() >= 1 && clipboard.Width() <= kMaxCanvasSide && clipboard.Height() >= 1 &&
            clipboard.Height() <= kMaxCanvasSide)
        {
            _sizeDialog.newWidth = static_cast<double>(clipboard.Width());
            _sizeDialog.newHeight = static_cast<double>(clipboard.Height());
        }
    }
    else
    {
        const CanvasDocument& document = _session.Document().value();
        _sizeDialog.canvas = CanvasSizeDraft(document.width, document.height, document.resolution);
        _sizeDialog.anchor = 4;
        _sizeDialog.extension = 0;
        _sizeDialog.image = ImageSizeDraft(document.width, document.height, document.resolution);
    }
    SyncChrome();
    _frameInvalidated = true;
}

void Application::CancelSizeCard() noexcept
{
    if (_sizingTicket)
    {
        (void)_historyWork.RequestCancel();
        (void)_jobs.Cancel(_sizingTicket.value());
    }
    if (!SizeCardOpen())
    {
        return;
    }
    _sizeDialog.kind = SizeCardKind::None;
    SyncChrome();
    _frameInvalidated = true;
}

void Application::ApplySizeCard() noexcept
{
    if (!SizeCardOpen())
    {
        return;
    }
    if (!_hostExecuting)
    {
        ReportHostAdmission(QueueHostInput(HostSizeApplyInput{_sizeDialog, _sizeInvocation}));
        return;
    }
    const SizeCardKind kind = _sizeDialog.kind;
    if (kind == SizeCardKind::NewCanvas)
    {
        const int width = static_cast<int>(std::round(_sizeDialog.newWidth));
        const int height = static_cast<int>(std::round(_sizeDialog.newHeight));
        if (!std::isfinite(_sizeDialog.newWidth) || !std::isfinite(_sizeDialog.newHeight) || width < 1 ||
            width > kMaxCanvasSide || height < 1 || height > kMaxCanvasSide)
        {
            return;
        }
        _sizeDialog.kind = SizeCardKind::None;
        SettleInteractions(false);
        _session.ClearDocument();
        _session.CreateDocument(width, height, true);
        _session.history.MarkSaved();
        _documentPath.clear();
        _viewport.followsFit = true;
        DocumentChanged();
        return;
    }
    if (!_session.Document().has_value())
    {
        CancelSizeCard();
        return;
    }
    if ((kind == SizeCardKind::CanvasSize && !_sizeDialog.canvas.Valid()) ||
        (kind == SizeCardKind::ImageSize && !_sizeDialog.image.Valid()))
    {
        return;
    }
    // A pending gradient or transform commits first, so the resize starts from the document that holds it (the
    // settle closes the card; the draft stays in _sizeDialog).
    SettleInteractions(false);
    if (!_session.Document().has_value())
    {
        return;
    }
    std::variant<CanvasSizeOptions, ImageSizeOptions> work;
    if (kind == SizeCardKind::CanvasSize)
    {
        const CanvasSizeDraft& draft = _sizeDialog.canvas;
        CanvasSizeOptions options;
        options.width = draft.RoundedWidth();
        options.height = draft.RoundedHeight();
        options.anchor = _sizeDialog.anchor;
        switch (_sizeDialog.extension)
        {
        case 1:
            options.fill = ColorOf(_session.ForegroundColor());
            break;
        case 2:
            options.fill = ColorOf(_session.BackgroundColor());
            break;
        case 3:
            options.fill = CanvasExtensionColor{0, 0, 0};
            break;
        case 4:
            options.fill = CanvasExtensionColor{1, 1, 1};
            break;
        case 5:
            options.fill = ColorOf(_sizeDialog.customArgb);
            break;
        default:
            break;
        }
        work = options;
    }
    else
    {
        const ImageSizeDraft& draft = _sizeDialog.image;
        ImageSizeOptions options;
        options.width = draft.resample ? draft.RoundedWidth() : _session.Document().value().width;
        options.height = draft.resample ? draft.RoundedHeight() : _session.Document().value().height;
        options.resolution = draft.resolution;
        options.sampling = draft.sampling;
        work = options;
    }
    _sizeDialog.kind = SizeCardKind::None;
    _hostExecutionError = StartSizing(std::move(work));
    if (FAILED(_hostExecutionError))
        _prompts->PostNotice(_window.get(), kNoticeMessage, IDS_ERROR_RESIZE);
    SyncChrome();
    _frameInvalidated = true;
}

void Application::OnSizeAction(SizeAction action, double value) noexcept
{
    if (!SizeCardOpen() || !std::isfinite(value))
    {
        return;
    }
    switch (action)
    {
    case SizeAction::Cancel:
        CancelSizeCard();
        return;
    case SizeAction::Apply:
        ApplySizeCard();
        return;
    case SizeAction::OpenProject:
        CancelSizeCard();
        OpenDocument();
        return;
    case SizeAction::ImportImage:
        CancelSizeCard();
        ImportImages();
        return;
    default:
        break;
    }
    switch (_sizeDialog.kind)
    {
    case SizeCardKind::NewCanvas:
        if (action == SizeAction::SetWidth)
        {
            _sizeDialog.newWidth = value;
        }
        else if (action == SizeAction::SetHeight)
        {
            _sizeDialog.newHeight = value;
        }
        break;
    case SizeCardKind::CanvasSize:
    {
        CanvasSizeDraft& draft = _sizeDialog.canvas;
        switch (action)
        {
        case SizeAction::SetWidth:
            draft.Set(value, true);
            break;
        case SizeAction::SetHeight:
            draft.Set(value, false);
            break;
        case SizeAction::SetUnit:
            draft.unit = static_cast<CanvasUnit>(std::clamp(static_cast<int>(value), 0, 3));
            break;
        case SizeAction::SetRelative:
            draft.relative = value != 0.0;
            break;
        case SizeAction::SetLocked:
            draft.locked = value != 0.0;
            if (draft.locked)
            {
                // Locking settles the height on the width, as the Mac sheet does.
                draft.Set(draft.Displayed(true), true);
            }
            break;
        case SizeAction::SetAnchor:
            _sizeDialog.anchor = std::clamp(static_cast<int>(value), 0, 8);
            break;
        case SizeAction::SetExtension:
            _sizeDialog.extension = std::clamp(static_cast<int>(value), 0, 5);
            if (_sizeDialog.extension == 5 && _chrome)
            {
                _sizeDialog.customBefore = _sizeDialog.customArgb;
                _chrome->OpenColorPicker(PickerTarget::SizeExtension, _sizeDialog.customArgb);
            }
            break;
        default:
            break;
        }
        break;
    }
    case SizeCardKind::ImageSize:
    {
        ImageSizeDraft& draft = _sizeDialog.image;
        switch (action)
        {
        case SizeAction::SetWidth:
            draft.Set(value, true);
            break;
        case SizeAction::SetHeight:
            draft.Set(value, false);
            break;
        case SizeAction::SetUnit:
            draft.unit = static_cast<CanvasUnit>(std::clamp(static_cast<int>(value), 0, 3));
            break;
        case SizeAction::SetLocked:
            draft.locked = value != 0.0;
            break;
        case SizeAction::SetResolution:
            draft.SetResolution(value);
            break;
        case SizeAction::SetResample:
            draft.SetResample(value != 0.0);
            break;
        case SizeAction::SetSampling:
            draft.sampling = static_cast<LayerSampling>(std::clamp(static_cast<int>(value), 0, 2));
            break;
        default:
            break;
        }
        break;
    }
    case SizeCardKind::None:
    default:
        break;
    }
    SyncChrome();
    _frameInvalidated = true;
}

void Application::OnSizeColor(ChromeEditPhase phase, uint32_t argb) noexcept
{
    if (_sizeDialog.kind != SizeCardKind::CanvasSize)
    {
        return;
    }
    // Cancel restores the color the picker opened on; Preview and OK take the working color.
    _sizeDialog.customArgb = (phase == ChromeEditPhase::Cancel ? _sizeDialog.customBefore : argb) | 0xFF000000u;
    SyncChrome();
    _frameInvalidated = true;
}

void Application::SyncSizeCard() noexcept
{
    if (!_chrome)
    {
        return;
    }
    SizeCardState state;
    state.kind = _sizeDialog.kind;
    switch (_sizeDialog.kind)
    {
    case SizeCardKind::NewCanvas:
        state.width = _sizeDialog.newWidth;
        state.height = _sizeDialog.newHeight;
        state.valid = std::isfinite(_sizeDialog.newWidth) && std::isfinite(_sizeDialog.newHeight) &&
                      std::round(_sizeDialog.newWidth) >= 1 && std::round(_sizeDialog.newWidth) <= kMaxCanvasSide &&
                      std::round(_sizeDialog.newHeight) >= 1 && std::round(_sizeDialog.newHeight) <= kMaxCanvasSide;
        state.resultWidth = static_cast<int>(std::round(_sizeDialog.newWidth));
        state.resultHeight = static_cast<int>(std::round(_sizeDialog.newHeight));
        break;
    case SizeCardKind::CanvasSize:
    {
        const CanvasSizeDraft& draft = _sizeDialog.canvas;
        state.originalWidth = draft.originalWidth;
        state.originalHeight = draft.originalHeight;
        state.width = draft.Displayed(true);
        state.height = draft.Displayed(false);
        state.unit = static_cast<int>(draft.unit);
        state.relative = draft.relative;
        state.locked = draft.locked;
        state.anchor = _sizeDialog.anchor;
        state.extension = _sizeDialog.extension;
        state.extensionArgb = _sizeDialog.customArgb;
        state.valid = draft.Valid();
        state.resultWidth = state.valid ? draft.RoundedWidth() : 0;
        state.resultHeight = state.valid ? draft.RoundedHeight() : 0;
        break;
    }
    case SizeCardKind::ImageSize:
    {
        const ImageSizeDraft& draft = _sizeDialog.image;
        state.originalWidth = draft.originalWidth;
        state.originalHeight = draft.originalHeight;
        state.width = draft.Displayed(true);
        state.height = draft.Displayed(false);
        state.unit = static_cast<int>(draft.unit);
        state.locked = draft.locked;
        state.resolution = draft.resolution;
        state.resample = draft.resample;
        state.sampling = static_cast<int>(draft.sampling);
        state.valid = draft.Valid();
        state.resultWidth = state.valid ? draft.RoundedWidth() : 0;
        state.resultHeight = state.valid ? draft.RoundedHeight() : 0;
        break;
    }
    case SizeCardKind::None:
    default:
        break;
    }
    _chrome->SetSizeCard(state);
}

int Application::RunSizeSelfTest() noexcept
{
    if (!_chrome || !_window)
    {
        SelfTestFailure(L"The size self-test needs the chrome and the window.\n");
        return 5;
    }
    const auto document = [&]() -> const CanvasDocument* { return _session.DisplayDocument(); };
    const auto settle = [&]() noexcept
    {
        if (SettleJobs(_jobs, 10000, DispatchMessageThunk, this) != SettleResult::Settled)
        {
            SelfTestFailure(L"The size self-test could not settle admitted work.\n");
            return false;
        }
        return true;
    };
    const auto command = [&](int id) noexcept
    {
        OnCommand(id);
        return settle();
    };
    const auto sizeAction = [&](SizeAction action, double value) noexcept
    {
        OnSizeAction(action, value);
        return settle();
    };
    const auto key = [&](unsigned int virtualKey) noexcept
    {
        const bool handled = HandleAccelerator(virtualKey, 0);
        return settle() && handled;
    };
    // A fresh default document with a small red layer at the top-left corner.
    _forcedDiscard = true;
    if (!command(ChromeHost::kCommandNew))
        return 5;
    _forcedDiscard.reset();
    {
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(6 * 3 * 4), 0);
        for (std::size_t index = 0; index + 3 < bytes.size(); index += 4)
        {
            bytes[index + 2] = 255;
            bytes[index + 3] = 255;
        }
        const Raster raster = Raster::Make(6, 3, RasterFormat::Bgra8888, bytes.data(), bytes.size());
        ImageLayer layer = ImageLayer::Blank(L"Red", Vec2{6, 3});
        layer.asset = RasterAsset{raster, Raster::MakeThumbnail(raster), layer.name};
        _session.BeginEdit(L"Import Image");
        _session.EditForTest([&](CanvasDocument& document) { document.layers.push_back(layer); });
        _session.EndEdit();
        _session.SelectLayer(layer.id);
        DocumentChanged();
    }
    const Guid red = _session.ActiveLayerID().value();
    // Canvas Size: half the size in percent with the ratio locked, anchored top-left, applied with Enter.
    if (!command(ChromeHost::kCommandCanvasSize))
        return 5;
    if (!SizeCardOpen() || !_chrome->SizeCardOpen())
    {
        SelfTestFailure(L"Canvas Size did not open its card.\n");
        return 5;
    }
    if (!sizeAction(SizeAction::SetUnit, 1.0))
        return 5;
    if (!sizeAction(SizeAction::SetLocked, 1.0))
        return 5;
    if (!sizeAction(SizeAction::SetWidth, 50.0))
        return 5;
    if (!sizeAction(SizeAction::SetAnchor, 0.0))
        return 5;
    if (!sizeAction(SizeAction::SetExtension, 4.0))
        return 5;
    _chrome->SetCanvasFocused(true);
    if (!key(VK_RETURN) || SizeCardOpen() || document() == nullptr || document()->width != 640 ||
        document()->height != 400 || _session.history.UndoName() != L"Canvas Size" || document()->layers.size() != 2 ||
        _session.ActiveLayerID() != red || _session.ActiveLayer()->transform.origin != Vec2{0, 0})
    {
        SelfTestFailure(std::format(L"Canvas Size did not shrink the canvas to 640 × 400 around the "
                                    L"top-left anchor ({} × {}, undo '{}', {} layers).\n",
                                    document() ? document()->width : 0, document() ? document()->height : 0,
                                    _session.history.UndoName(), document() ? document()->layers.size() : 0));
        return 5;
    }
    // Growing with a white extension adds the bottom layer with the old canvas cut out.
    if (!command(ChromeHost::kCommandCanvasSize))
        return 5;
    if (!sizeAction(SizeAction::SetRelative, 1.0))
        return 5;
    if (!sizeAction(SizeAction::SetWidth, 10.0))
        return 5;
    if (!sizeAction(SizeAction::SetHeight, 10.0))
        return 5;
    if (!sizeAction(SizeAction::SetAnchor, 8.0))
        return 5;
    if (!sizeAction(SizeAction::SetExtension, 4.0))
        return 5;
    if (!sizeAction(SizeAction::Apply, 0.0))
        return 5;
    const ImageLayer* extension = document() && !document()->layers.empty() ? &document()->layers.front() : nullptr;
    if (document() == nullptr || document()->width != 650 || document()->height != 410 || extension == nullptr ||
        extension->name != L"Canvas Extension" || !extension->asset.has_value() ||
        extension->asset->image.Width() != 650 || _session.ActiveLayerID() != red ||
        _session.ActiveLayer()->transform.origin != Vec2{10, 10})
    {
        SelfTestFailure(L"Growing the canvas with a white extension did not add the extension layer and "
                        L"move the content by the bottom-right anchor.\n");
        return 5;
    }
    // Escape drops a pending edit.
    if (!command(ChromeHost::kCommandCanvasSize))
        return 5;
    if (!sizeAction(SizeAction::SetWidth, 100.0))
        return 5;
    _chrome->SetCanvasFocused(true);
    if (!key(VK_ESCAPE) || SizeCardOpen() || document() == nullptr || document()->width != 650)
    {
        SelfTestFailure(L"Escape did not cancel the Canvas Size card.\n");
        return 5;
    }
    // Image Size: 325 × 205 at 300 ppi with nearest sampling; the red layer's pixels scale with the canvas.
    if (!command(ChromeHost::kCommandImageSize))
        return 5;
    if (!sizeAction(SizeAction::SetWidth, 325.0))
        return 5;
    if (!sizeAction(SizeAction::SetResolution, 300.0))
        return 5;
    if (!sizeAction(SizeAction::SetSampling, 0.0))
        return 5;
    if (!sizeAction(SizeAction::Apply, 0.0))
        return 5;
    const ImageLayer* scaled = nullptr;
    if (document() != nullptr)
    {
        for (const ImageLayer& layer : document()->layers)
        {
            if (layer.id == red)
            {
                scaled = &layer;
            }
        }
    }
    if (document() == nullptr || document()->width != 325 || document()->height != 205 ||
        document()->resolution != 300.0 || _session.history.UndoName() != L"Image Size" || scaled == nullptr ||
        !scaled->asset.has_value() || scaled->asset->image.Width() != 3 || scaled->asset->image.Height() != 2 ||
        scaled->transform.origin != Vec2{5, 5} || scaled->asset->image.Data() == nullptr ||
        scaled->asset->image.Data()[2] != 255 || scaled->asset->image.Data()[3] != 255)
    {
        SelfTestFailure(std::format(L"Image Size did not resample the document to 325 × 205 at 300 ppi "
                                    L"({} × {} at {}, undo '{}').\n",
                                    document() ? document()->width : 0, document() ? document()->height : 0,
                                    document() ? document()->resolution : 0.0, _session.history.UndoName()));
        return 5;
    }
    _session.Undo();
    _session.Undo();
    _session.Undo();
    DocumentChanged();
    if (document() == nullptr || document()->width != 1280 || document()->height != 800 ||
        document()->layers.size() != 2)
    {
        SelfTestFailure(L"Undo did not restore the document through the three size entries.\n");
        return 5;
    }
    // Without resampling Image Size lists Inches and Centimeters only, and a pick reports the unit, not its place in
    // the list (RF-502): Down on the focused combo picks Centimeters and the draft stores Centimeters.
    {
        if (!command(ChromeHost::kCommandImageSize))
            return 5;
        if (!sizeAction(SizeAction::SetResample, 0.0))
            return 5;
        const bool inches = _sizeDialog.image.unit == CanvasUnit::Inches;
        (void)_chrome->AdvanceAnimation(GetTickCount64());
        SyncChrome();
        (void)_renderer.Render(_chrome.get(), &_composition, &_viewport, _session.DisplayDocument());
        const bool focused = _chrome->FocusSizeUnitForTest();
        const bool picked = _chrome->DispatchKey(VK_DOWN, true, 0);
        (void)_chrome->DispatchKey(VK_DOWN, false, 0);
        const CanvasUnit unit = _sizeDialog.image.unit;
        CancelSizeCard();
        _chrome->SetCanvasFocused(true);
        if (!inches || !focused || !picked || unit != CanvasUnit::Centimeters)
        {
            SelfTestFailure(std::format(L"Image Size's unit pick without resampling did not store the unit "
                                        L"picked (inches first {}, focused {}, picked {}, unit {}).\n",
                                        inches, focused, picked, static_cast<int>(unit)));
            return 5;
        }
    }
    // New Canvas: a fresh unmodified document of the typed size.
    if (!command(ChromeHost::kCommandNewCanvas))
        return 5;
    if (!sizeAction(SizeAction::SetWidth, 300.0))
        return 5;
    if (!sizeAction(SizeAction::SetHeight, 200.0))
        return 5;
    if (!sizeAction(SizeAction::Apply, 0.0))
        return 5;
    if (SizeCardOpen() || document() == nullptr || document()->width != 300 || document()->height != 200 ||
        document()->layers.size() != 1 || _session.history.IsModified())
    {
        SelfTestFailure(L"New Canvas did not create a 300 × 200 document with one blank layer.\n");
        return 5;
    }
    // A pending gradient commits before Canvas Size resizes: the resized canvas keeps it, one entry below.
    OnTool(EditorTool::Gradient);
    _session.SetForegroundColor(0xFF000000u);
    _session.SetBackgroundColor(0xFFFFFFFFu);
    _session.gradientSettings = GradientSettings{};
    _session.gradientSettings.style = GradientStyle::ForegroundToBackground;
    const std::optional<Guid> plate = _session.ActiveLayerID();
    if (!plate.has_value() || !_session.BeginGradient(Vec2{10, 100}))
    {
        SelfTestFailure(L"The blank layer did not take a gradient.\n");
        return 5;
    }
    _session.MoveGradient(std::nullopt, Vec2{290, 100});
    _session.EndGradientDrag();
    SyncChrome();
    const bool pending = _session.PendingGradient() != nullptr;
    if (!command(ChromeHost::kCommandCanvasSize))
        return 5;
    if (!sizeAction(SizeAction::SetRelative, 1.0))
        return 5;
    if (!sizeAction(SizeAction::SetWidth, 20.0))
        return 5;
    if (!sizeAction(SizeAction::SetHeight, 20.0))
        return 5;
    if (!sizeAction(SizeAction::Apply, 0.0))
        return 5;
    std::array<std::uint8_t, 4> center{};
    bool painted = false;
    if (document() != nullptr)
    {
        for (const ImageLayer& layer : document()->layers)
        {
            if (layer.id == plate.value() && layer.asset.has_value() && layer.asset->image.HasStorage())
            {
                layer.asset->image.ReadRegion(layer.asset->image.Width() / 2, layer.asset->image.Height() / 2, 1, 1,
                                              center.data(), 4, 0);
                painted = center[3] == 255;
            }
        }
    }
    const bool resized = document() != nullptr && document()->width == 320 && document()->height == 220 &&
                         _session.history.UndoName() == L"Canvas Size";
    _session.Undo();
    const bool gradientBelow = _session.history.UndoName() == L"Gradient";
    DocumentChanged();
    OnTool(EditorTool::Move);
    if (!pending || !resized || !painted || !gradientBelow)
    {
        SelfTestFailure(std::format(L"Canvas Size over a pending gradient did not keep it (pending {}, "
                                    L"resized {}, painted {}, gradient entry {}).\n",
                                    pending, resized, painted, gradientBelow));
        return 5;
    }
    return 0;
}
