#include "ChromeHost.h"

#include "Resources.h"
#include "resource.h"

#include <algorithm>
#include <array>
#include <string>

// The Crop tool's header (C16, Specs/UI/UI_ApplicationChrome.md "Crop tool"): the ratio choice, the frame's size,
// Cancel and Apply Crop; the frame itself is the transform box plus a shade over everything outside it.

void ChromeHost::BuildCropHeader(DxUi::Panel& root)
{
    _cropHeader = root.AddChild<DxUi::Panel>();
    DxUi::Panel& header = *_cropHeader;
    _cropTitle = header.AddChild<DxUi::Label>(LoadStringResource(nullptr, IDS_TOOL_CROP));
    _cropTitle->SetFontRole(DxUi::FontRole::BodyStrong);
    _cropRatioLabel = header.AddChild<DxUi::Label>(LoadStringResource(nullptr, IDS_CROP_RATIO));
    _cropRatio = MakeCombo(header,
                           NamedItems({{L"free", IDS_CROP_FREE},
                                       {L"original", IDS_CROP_ORIGINAL},
                                       {L"1:1", IDS_CROP_1_1},
                                       {L"4:3", IDS_CROP_4_3},
                                       {L"16:9", IDS_CROP_16_9}}),
                           {.nameId = IDS_CROP_RATIO, .tipId = IDS_CROP_RATIO_TIP}, CropAction::SetRatio);
    _cropSize = header.AddChild<DxUi::Label>(std::wstring{});
    _cropCancel = MakeButton(header, {.labelId = IDS_TRANSFORM_CANCEL}, CropAction::Cancel);
    _cropApply = MakeButton(header, {.labelId = IDS_CROP_APPLY, .primary = true}, CropAction::Apply);
    header.SetVisible(_tool == EditorTool::Crop);
}

void ChromeHost::ArrangeCropHeader(float left, float top, float right, float bottom) noexcept
{
    // Cancel and Apply Crop keep the right edge; the rest flows from the left and hides when the room runs out.
    const std::array pinned{HeaderSlot{_cropApply, 100.0f}, HeaderSlot{_cropCancel, 80.0f}};
    const std::array flow{HeaderSlot{_cropTitle, 50.0f}, HeaderSlot{_cropRatioLabel, 40.0f},
                          HeaderSlot{_cropRatio, 110.0f}, HeaderSlot{_cropSize, 120.0f}};
    ArrangeHeaderRow(_cropHeader, D2D1::RectF(left, top, right, bottom), pinned, flow, false);
}

void ChromeHost::SetCropInspector(const CropInspectorState& state) noexcept
{
    // The header writes only a state it has not shown (RF-1001, `_resync`).
    if (!_cropHeader || (!TakeResync(kResyncCrop) && state == _cropInspector))
    {
        return;
    }
    _cropInspector = state;
    ++_syncCounters.headers;
    const auto hold = HoldReports();
    _cropRatio->SetSelectedIndex(static_cast<size_t>(std::clamp(state.ratio, 0, 4)));
    _cropRatio->SetEnabled(state.enabled);
    const std::wstring size =
        state.hasFrame ? FormatStringResource(nullptr, IDS_CROP_SIZE, state.width, state.height) : std::wstring{};
    if (_cropSize->GetText() != size)
    {
        _cropSize->SetText(size);
    }
    _cropCancel->SetEnabled(state.enabled && state.hasFrame);
    _cropApply->SetEnabled(state.enabled && state.hasFrame);
}

void ChromeHost::SetCropShade(std::optional<D2D1_RECT_F> framePixels) noexcept
{
    if (!_overlay)
    {
        return;
    }
    if (framePixels.has_value())
    {
        framePixels = PixelToDip(*framePixels);
    }
    if (_overlay->SetCropShade(framePixels))
    {
        ++_syncCounters.overlays;
    }
}
