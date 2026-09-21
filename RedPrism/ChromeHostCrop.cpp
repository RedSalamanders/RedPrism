#include "ChromeHost.h"

#include "Resources.h"
#include "resource.h"

#include <algorithm>
#include <array>
#include <string>

// The Crop tool's header (C16, Specs/UI/UI_ApplicationChrome.md "Crop tool"): the ratio choice, the frame's size,
// Cancel and Apply Crop; the frame itself is the transform box plus a shade over everything outside it.

void ChromeHost::SetCropHandler(std::function<void(CropAction, double)> handler)
{
    _onCrop = std::move(handler);
}

void ChromeHost::BuildCropHeader(DxUi::Panel& root)
{
    _cropHeader = root.AddChild<DxUi::Panel>();
    _cropTitle = _cropHeader->AddChild<DxUi::Label>(LoadStringResource(nullptr, IDS_TOOL_CROP));
    _cropTitle->SetFontRole(DxUi::FontRole::BodyStrong);
    _cropRatioLabel = _cropHeader->AddChild<DxUi::Label>(LoadStringResource(nullptr, IDS_CROP_RATIO));
    _cropRatio = _cropHeader->AddChild<DxUi::ComboBox>();
    _cropRatio->SetItems({DxUi::ComboBox::Item{L"free", LoadStringResource(nullptr, IDS_CROP_FREE)},
                          DxUi::ComboBox::Item{L"original", LoadStringResource(nullptr, IDS_CROP_ORIGINAL)},
                          DxUi::ComboBox::Item{L"1:1", LoadStringResource(nullptr, IDS_CROP_1_1)},
                          DxUi::ComboBox::Item{L"4:3", LoadStringResource(nullptr, IDS_CROP_4_3)},
                          DxUi::ComboBox::Item{L"16:9", LoadStringResource(nullptr, IDS_CROP_16_9)}});
    _cropRatio->SetSelectedIndex(0);
    _cropRatio->SetAccessibleName(LoadStringResource(nullptr, IDS_CROP_RATIO));
    _cropRatio->SetTooltipText(LoadStringResource(nullptr, IDS_CROP_RATIO_TIP));
    _cropRatio->SetOnSelectionChanged(
        [this](size_t index)
        {
            if (!_syncingCrop && _onCrop)
            {
                _onCrop(CropAction::SetRatio, static_cast<double>(index));
            }
        });
    _cropSize = _cropHeader->AddChild<DxUi::Label>(std::wstring{});
    _cropCancel = _cropHeader->AddChild<DxUi::Button>(LoadStringResource(nullptr, IDS_TRANSFORM_CANCEL));
    _cropCancel->SetOnClick(
        [this]
        {
            if (_onCrop)
            {
                _onCrop(CropAction::Cancel, 0.0);
            }
        });
    _cropApply = _cropHeader->AddChild<DxUi::Button>(LoadStringResource(nullptr, IDS_CROP_APPLY));
    _cropApply->SetPrimary(true);
    _cropApply->SetOnClick(
        [this]
        {
            if (_onCrop)
            {
                _onCrop(CropAction::Apply, 0.0);
            }
        });
    _cropHeader->SetVisible(_tool == EditorTool::Crop);
}

void ChromeHost::ArrangeCropHeader(float left, float top, float right, float bottom) noexcept
{
    if (!_cropHeader)
    {
        return;
    }
    PlaceControl(_cropHeader, D2D1::RectF(left, top, right, bottom));
    constexpr float kRowDip = 28.0f;
    constexpr float kGapDip = 6.0f;
    const float rowTop = top + (bottom - top - kRowDip) / 2.0f;
    const float rowBottom = rowTop + kRowDip;
    // Cancel and Apply Crop keep the right edge; the rest flows from the left and hides when the room runs out.
    constexpr float kApplyDip = 100.0f;
    constexpr float kCancelDip = 80.0f;
    float rightEdge = right - 4.0f;
    PlaceControl(_cropApply, D2D1::RectF(rightEdge - kApplyDip, rowTop, rightEdge, rowBottom));
    rightEdge -= kApplyDip + kGapDip;
    PlaceControl(_cropCancel, D2D1::RectF(rightEdge - kCancelDip, rowTop, rightEdge, rowBottom));
    rightEdge -= kCancelDip + kGapDip;
    struct Slot
    {
        DxUi::Control* control;
        float width;
    };
    const std::array<Slot, 4> slots{Slot{_cropTitle, 50.0f}, Slot{_cropRatioLabel, 40.0f}, Slot{_cropRatio, 110.0f},
                                    Slot{_cropSize, 120.0f}};
    float cursor = left + 4.0f;
    bool room = true;
    for (const Slot& slot : slots)
    {
        if (slot.control == nullptr)
        {
            continue;
        }
        const bool fits = room && cursor + slot.width <= rightEdge;
        ShowControl(slot.control, fits);
        if (fits)
        {
            PlaceControl(slot.control, D2D1::RectF(cursor, rowTop, cursor + slot.width, rowBottom));
            cursor += slot.width + kGapDip;
        }
        else
        {
            room = false;
        }
    }
}

void ChromeHost::SetCropInspector(const CropInspectorState& state) noexcept
{
    _cropInspector = state;
    if (!_cropHeader)
    {
        return;
    }
    _syncingCrop = true;
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
    _syncingCrop = false;
}

void ChromeHost::SetCropShade(std::optional<D2D1_RECT_F> framePixels) noexcept
{
    if (!_overlay)
    {
        return;
    }
    if (framePixels.has_value())
    {
        const float toDip = _dpi > 0.0f ? 96.0f / _dpi : 1.0f;
        framePixels->left *= toDip;
        framePixels->top *= toDip;
        framePixels->right *= toDip;
        framePixels->bottom *= toDip;
    }
    _overlay->SetCropShade(framePixels);
}
