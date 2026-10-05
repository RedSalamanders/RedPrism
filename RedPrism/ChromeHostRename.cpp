#include "ChromeHost.h"

#include "Resources.h"
#include "resource.h"

#include <algorithm>
#include <string>

// The Rename Layer card (C16, Specs/UI/UI_ApplicationChrome.md "Layers panel"): a text field over the canvas hole
// opened on the active layer's name (Layer › Rename Layer…, F2, or a double-click on its row); Enter or OK commits
// the trimmed name, Escape or Cancel leaves it.

namespace
{
constexpr float kCardWidthDip = 300.0f;
constexpr float kCardPaddingDip = 12.0f;
constexpr float kRowDip = 28.0f;
constexpr float kRowGapDip = 8.0f;
constexpr float kMarginDip = 8.0f;
constexpr float kButtonDip = 30.0f;
} // namespace

void ChromeHost::LayerTreeModel::OnTreeItemInvoked(uint64_t itemId)
{
    const auto visibleRows = RowsView();
    if (itemId == 0 || itemId > visibleRows.size())
    {
        return;
    }
    events.OnLayerInvoked(visibleRows[itemId - 1].id);
}

bool ChromeHost::RenameCardOpen() const noexcept
{
    return _renameCard != nullptr && _renameCard->IsVisible();
}

void ChromeHost::BuildRenameCard(DxUi::Panel& root)
{
    _renameCard = root.AddChild<DxUi::CardPanel>();
    _renameCard->SetCornerRadius(8.0f);
    _renameCard->SetVisible(false);
    _renameTitle = _renameCard->AddChild<DxUi::Label>(LoadStringResource(nullptr, IDS_RENAME_TITLE));
    _renameTitle->SetFontRole(DxUi::FontRole::BodyStrong);
    _renameField = _renameCard->AddChild<DxUi::TextField>();
    _renameField->SetAccessibleName(LoadStringResource(nullptr, IDS_RENAME_NAME));
    _renameField->SetPlaceholder(LoadStringResource(nullptr, IDS_RENAME_NAME));
    _renameField->SetOnSubmitted([this] { CloseRenameCard(true); });
    _renameCancel = MakeButton(*_renameCard, {.labelId = IDS_TRANSFORM_CANCEL}, [this] { CloseRenameCard(false); });
    _renameOk =
        MakeButton(*_renameCard, {.labelId = IDS_ADJUST_OK, .primary = true}, [this] { CloseRenameCard(true); });
}

void ChromeHost::ArrangeRenameCard(float right, float top) noexcept
{
    if (!_renameCard || !_renameCard->IsVisible())
    {
        return;
    }
    const float width = std::min(kCardWidthDip, std::max(200.0f, right - kRailWidthDip - 2.0f * kMarginDip));
    const float left = right - kMarginDip - width;
    const float innerLeft = left + kCardPaddingDip;
    const float innerRight = left + width - kCardPaddingDip;
    float y = top + kMarginDip + kCardPaddingDip;
    PlaceControl(_renameTitle, D2D1::RectF(innerLeft, y, innerRight, y + 24.0f));
    y += 24.0f + kRowGapDip;
    PlaceControl(_renameField, D2D1::RectF(innerLeft, y, innerRight, y + kRowDip));
    y += kRowDip + kRowGapDip;
    PlaceControl(_renameCancel, D2D1::RectF(innerLeft, y, innerLeft + 90.0f, y + kButtonDip));
    PlaceControl(_renameOk, D2D1::RectF(innerRight - 90.0f, y, innerRight, y + kButtonDip));
    y += kButtonDip + kCardPaddingDip;
    PlaceControl(_renameCard, D2D1::RectF(left, top + kMarginDip, left + width, y));
}

void ChromeHost::OpenRenameCard(const std::wstring& name) noexcept
{
    if (!_renameCard || !_renameField)
    {
        return;
    }
    _renameField->SetText(name);
    _renameField->SetSelectionRange(0, name.size());
    _renameCard->SetVisible(true);
    // Laid out before it takes the focus: a control without bounds cannot hold it.
    Arrange();
    PushCard(ModalCard::Rename, _renameCard, _renameField);
    _view.MarkDirty();
}

void ChromeHost::CloseRenameCard(bool commit) noexcept
{
    if (!RenameCardOpen())
    {
        return;
    }
    // The immediate callback copies/adopts this borrowed field text before PopCard can change focus or reuse it.
    const std::wstring_view name(_renameField->GetText());
    _events.OnRename(commit ? std::optional<std::wstring_view>(name) : std::nullopt);
    // The keyboard goes back where it was: the layers panel after a double-click on a row, the canvas after F2.
    PopCard(ModalCard::Rename);
    _renameCard->SetVisible(false);
    _view.MarkDirty();
}
