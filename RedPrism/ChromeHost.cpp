#include "ChromeHost.h"

#include "CanvasOverlay.h"
#include "Resources.h"
#include "ThemePalette.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <new>

namespace
{
void AppendCommand(HMENU menu, int command, UINT stringId) noexcept
{
    const std::wstring caption = LoadStringResource(nullptr, stringId);
    (void)AppendMenuW(menu, MF_STRING, static_cast<UINT_PTR>(command), caption.c_str());
}

void AppendSeparator(HMENU menu) noexcept
{
    (void)AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
}

[[nodiscard]] wchar_t MenuMnemonic(const std::wstring& caption) noexcept
{
    return caption.empty() ? L'\0' : caption.front();
}
} // namespace

size_t ChromeHost::LayerTreeModel::GetVisibleItemCount() const noexcept
{
    return rows.size();
}

void ChromeHost::LayerTreeModel::GetVisibleItem(size_t visibleIndex, DxUi::TreeItemData& outItem) const
{
    outItem = {};
    if (visibleIndex >= rows.size())
    {
        return;
    }
    const HierarchyEntry& row = rows[visibleIndex];
    outItem.id = visibleIndex + 1;
    const std::wstring name = row.name.empty() ? row.id.ToString() : row.name;
    outItem.text = row.visible ? name : FormatStringResource(nullptr, IDS_LAYER_HIDDEN_SUFFIX, name);
    outItem.depth = static_cast<uint32_t>(row.depth);
    outItem.hasChildren = row.isGroup;
    outItem.expanded = row.isGroup && !row.collapsed;
    outItem.iconText = row.isGroup ? std::wstring(1, L'\xE8B7') : std::wstring{};
}

void ChromeHost::LayerTreeModel::OnTreeSelectionChanged(uint64_t itemId)
{
    if (itemId == 0 || itemId > rows.size() || onLayer == nullptr || !(*onLayer))
    {
        return;
    }
    (*onLayer)(rows[itemId - 1].id);
}

void ChromeHost::LayerTreeModel::OnTreeToggleExpanded(uint64_t itemId, bool expanded)
{
    if (itemId == 0 || itemId > rows.size() || onExpand == nullptr || !(*onExpand))
    {
        return;
    }
    (*onExpand)(rows[itemId - 1].id, expanded);
}

ChromeHost::~ChromeHost()
{
    Detach();
}

HRESULT ChromeHost::BindDevice(ID3D11Device* device) noexcept
{
    if (!device)
    {
        return E_INVALIDARG;
    }
    std::shared_ptr<DxUi::GraphicsDevice> graphics;
    const HRESULT result = DxUi::GraphicsDevice::Create(device, graphics);
    if (FAILED(result))
    {
        return result;
    }
    _graphics = std::move(graphics);
    return S_OK;
}

HRESULT ChromeHost::Attach(ID3D11Device* device, void* requestContext, void (*requestFrame)(void*) noexcept) noexcept
{
    Detach();
    HRESULT result = BindDevice(device);
    if (FAILED(result))
    {
        return result;
    }
    _requestContext = requestContext;
    _requestFrame = requestFrame;
    result = _view.Attach(_graphics, DxUi::EmbeddedCallbacks{_requestContext, _requestFrame});
    if (FAILED(result))
    {
        Detach();
        return result;
    }
    result = Build();
    if (FAILED(result))
    {
        Detach();
        return result;
    }
    _view.Controls().SetTheme(RedPrism::MakeRedPrismThemePalette());
    _attached = true;
    _view.SetVisible(true);
    return S_OK;
}

HRESULT ChromeHost::ReplaceDevice(ID3D11Device* device) noexcept
{
    if (!_attached)
    {
        return E_UNEXPECTED;
    }
    HRESULT result = BindDevice(device);
    if (FAILED(result))
    {
        return result;
    }
    result = _view.ReplaceDevice(_graphics);
    if (FAILED(result))
    {
        return result;
    }
    _view.MarkDirty();
    return S_OK;
}

void ChromeHost::Detach() noexcept
{
    _view.Detach();
    _menu = nullptr;
    _tabs = nullptr;
    _rail = nullptr;
    _header = nullptr;
    _layers = nullptr;
    _status = nullptr;
    _layersTitle = nullptr;
    _splitter = nullptr;
    _foregroundWell = nullptr;
    _backgroundWell = nullptr;
    _opacity = nullptr;
    _pickerCard = nullptr;
    _picker = nullptr;
    _overlay = nullptr;
    _transformHeader = nullptr;
    _transformTitle = nullptr;
    _autoSelect = nullptr;
    _showControls = nullptr;
    _lockRatio = nullptr;
    _transformX = nullptr;
    _transformY = nullptr;
    _transformWidth = nullptr;
    _transformHeight = nullptr;
    _transformScale = nullptr;
    _transformAngle = nullptr;
    _sampling = nullptr;
    _flipHorizontal = nullptr;
    _flipVertical = nullptr;
    _transformCancel = nullptr;
    _transformApply = nullptr;
    _toolButtons.fill(nullptr);
    for (unique_hmenu& menu : _menus)
    {
        menu.reset();
    }
    _graphics.reset();
    _requestContext = nullptr;
    _requestFrame = nullptr;
    _width = 0;
    _height = 0;
    _attached = false;
}

void ChromeHost::SetVisible(bool visible) noexcept
{
    _view.SetVisible(visible);
}

void ChromeHost::SetOwnerWindow(HWND window) noexcept
{
    _owner = window;
}

void ChromeHost::SetCommandHandler(std::function<void(int)> handler)
{
    _onCommand = std::move(handler);
}

void ChromeHost::SetToolHandler(std::function<void(EditorTool)> handler)
{
    _onTool = std::move(handler);
}

void ChromeHost::SetLayerHandler(std::function<void(Guid)> handler)
{
    _onLayer = std::move(handler);
}

void ChromeHost::SetLayerExpandHandler(std::function<void(Guid, bool)> handler)
{
    _onExpand = std::move(handler);
}

void ChromeHost::SetPaletteHandler(std::function<void(uint32_t, uint32_t)> handler)
{
    _onPalette = std::move(handler);
}

void ChromeHost::SetOpacityHandler(std::function<void(double, ChromeEditPhase)> handler)
{
    _onOpacity = std::move(handler);
}

void ChromeHost::SetLayoutHandler(std::function<void()> handler)
{
    _onLayout = std::move(handler);
}

void ChromeHost::SetTransformHandler(std::function<void(TransformAction, double)> handler)
{
    _onTransform = std::move(handler);
}

void ChromeHost::BuildMenus() noexcept
{
    for (unique_hmenu& menu : _menus)
    {
        menu.reset(CreatePopupMenu());
    }
    if (HMENU file = _menus[static_cast<std::size_t>(MenuIndex::File)].get())
    {
        AppendCommand(file, kCommandNew, IDS_CMD_NEW);
        AppendCommand(file, kCommandOpen, IDS_CMD_OPEN);
        AppendCommand(file, kCommandSave, IDS_CMD_SAVE);
        AppendCommand(file, kCommandSaveAs, IDS_CMD_SAVE_AS);
        AppendSeparator(file);
        AppendCommand(file, kCommandExportPng, IDS_CMD_EXPORT_PNG);
        AppendCommand(file, kCommandExportJpeg, IDS_CMD_EXPORT_JPEG);
        AppendSeparator(file);
        AppendCommand(file, kCommandExit, IDS_CMD_EXIT);
    }
    if (HMENU edit = _menus[static_cast<std::size_t>(MenuIndex::Edit)].get())
    {
        AppendCommand(edit, kCommandUndo, IDS_CMD_UNDO);
        AppendCommand(edit, kCommandRedo, IDS_CMD_REDO);
        AppendSeparator(edit);
        AppendCommand(edit, kCommandFreeTransform, IDS_CMD_FREE_TRANSFORM);
    }
    if (HMENU image = _menus[static_cast<std::size_t>(MenuIndex::Image)].get())
    {
        AppendCommand(image, kCommandFlipCanvasHorizontal, IDS_CMD_FLIP_CANVAS_HORIZONTAL);
        AppendCommand(image, kCommandFlipCanvasVertical, IDS_CMD_FLIP_CANVAS_VERTICAL);
    }
    if (HMENU layer = _menus[static_cast<std::size_t>(MenuIndex::Layer)].get())
    {
        AppendCommand(layer, kCommandNewLayer, IDS_CMD_NEW_LAYER);
        AppendCommand(layer, kCommandNewFolder, IDS_CMD_NEW_FOLDER);
        AppendSeparator(layer);
        AppendCommand(layer, kCommandDeleteLayer, IDS_CMD_DELETE_LAYER);
        AppendCommand(layer, kCommandToggleLayerVisibility, IDS_CMD_TOGGLE_LAYER_VISIBILITY);
        AppendCommand(layer, kCommandMoveOutOfGroup, IDS_CMD_MOVE_OUT_OF_GROUP);
        AppendSeparator(layer);
        AppendCommand(layer, kCommandFlipLayerHorizontal, IDS_CMD_FLIP_LAYER_HORIZONTAL);
        AppendCommand(layer, kCommandFlipLayerVertical, IDS_CMD_FLIP_LAYER_VERTICAL);
    }
    if (HMENU view = _menus[static_cast<std::size_t>(MenuIndex::View)].get())
    {
        AppendCommand(view, kCommandZoomFit, IDS_CMD_ZOOM_FIT);
        AppendCommand(view, kCommandZoomActual, IDS_CMD_ZOOM_ACTUAL);
        AppendSeparator(view);
        AppendCommand(view, kCommandZoomIn, IDS_CMD_ZOOM_IN);
        AppendCommand(view, kCommandZoomOut, IDS_CMD_ZOOM_OUT);
    }
    if (HMENU help = _menus[static_cast<std::size_t>(MenuIndex::Help)].get())
    {
        AppendCommand(help, kCommandAbout, IDS_CMD_ABOUT);
    }
}

void ChromeHost::OpenMenu(size_t index, POINT screen, bool) noexcept
{
    if (index >= _menus.size() || !_owner)
    {
        return;
    }
    HMENU menu = _menus[index].get();
    if (!menu || GetMenuItemCount(menu) <= 0)
    {
        return;
    }
    const std::optional<int> command =
        DxUi::ShowNativeHMenuContextMenu(_owner, screen, menu, RedPrism::MakeRedPrismThemePalette());
    if (command.has_value() && _onCommand)
    {
        _onCommand(command.value());
    }
}

HRESULT ChromeHost::Build() noexcept
{
    // DxUi construction allocates; std::bad_alloc is the only exception these calls raise and the host reports it as
    // a startup failure (exit code 1) instead of terminating inside chrome construction.
    try
    {
        BuildMenus();
        auto root = std::make_unique<DxUi::Panel>();
        _menu = root->AddChild<DxUi::MenuBar>();
        std::vector<DxUi::MenuBarItem> items;
        constexpr std::array<UINT, static_cast<std::size_t>(MenuIndex::Count)> kMenuIds{
            IDS_MENU_FILE,   IDS_MENU_EDIT,   IDS_MENU_IMAGE, IDS_MENU_LAYER,
            IDS_MENU_SELECT, IDS_MENU_FILTER, IDS_MENU_VIEW,  IDS_MENU_HELP};
        for (const UINT id : kMenuIds)
        {
            std::wstring caption = LoadStringResource(nullptr, id);
            const wchar_t mnemonic = MenuMnemonic(caption);
            items.push_back(DxUi::MenuBarItem{std::move(caption), mnemonic});
        }
        _menu->SetItems(std::move(items));
        _menu->SetOnOpenItem([this](size_t index, POINT screen, bool keyboard) { OpenMenu(index, screen, keyboard); });
        _tabs = root->AddChild<DxUi::TabControl>();
        _tabs->AddTab<DxUi::Panel>(LoadStringResource(nullptr, IDS_UNTITLED));
        _rail = root->AddChild<DxUi::Toolbar>();
        for (std::size_t index = 0; index < kEditorToolCount; ++index)
        {
            const EditorTool tool = EditorToolAt(index);
            const std::wstring name = LoadStringResource(nullptr, EditorToolNameId(tool));
            const std::wstring tooltip =
                FormatStringResource(nullptr, IDS_TOOL_TOOLTIP, name, std::wstring(1, EditorToolKey(tool)));
            DxUi::Button* button = _rail->AddButton(tooltip, std::wstring(1, EditorToolGlyph(tool)));
            button->SetAccessibleName(name);
            button->SetOnClick(
                [this, tool]
                {
                    SetTool(tool);
                    if (_onTool)
                    {
                        _onTool(tool);
                    }
                });
            _toolButtons[index] = button;
        }
        // Palette wells sit at the bottom of the rail: background behind, foreground in front (legacy layout).
        _backgroundWell = _rail->AddChild<DxUi::ColorSwatch>(_backgroundArgb);
        _backgroundWell->SetAccessibleName(LoadStringResource(nullptr, IDS_PALETTE_BACKGROUND));
        _backgroundWell->SetTooltipText(LoadStringResource(nullptr, IDS_PALETTE_BACKGROUND));
        _backgroundWell->SetOnClick([this] { OpenColorPicker(true); });
        _foregroundWell = _rail->AddChild<DxUi::ColorSwatch>(_foregroundArgb);
        _foregroundWell->SetAccessibleName(LoadStringResource(nullptr, IDS_PALETTE_FOREGROUND));
        _foregroundWell->SetTooltipText(LoadStringResource(nullptr, IDS_PALETTE_FOREGROUND));
        _foregroundWell->SetOnClick([this] { OpenColorPicker(false); });
        _header = root->AddChild<DxUi::Label>(LoadStringResource(nullptr, EditorToolNameId(_tool)));
        BuildTransformHeader(*root);
        _layersTitle = root->AddChild<DxUi::Label>(LoadStringResource(nullptr, IDS_LAYERS_TITLE));
        _opacity = root->AddChild<DxUi::NumericStepper>();
        _opacity->SetMinimum(0.0);
        _opacity->SetMaximum(100.0);
        _opacity->SetStep(1.0);
        _opacity->SetLargeStep(10.0);
        _opacity->SetDecimals(0);
        _opacity->SetLabel(LoadStringResource(nullptr, IDS_LAYER_OPACITY), 0.0f); // accessible name, not painted
        _opacity->SetUnit(LoadStringResource(nullptr, IDS_UNIT_PERCENT), 14.0f);
        _opacity->SetValue(100.0);
        _opacity->SetEnabled(false);
        _opacity->SetOnChange(
            [this](DxUi::NumericStepperChange change)
            {
                if (!_onOpacity)
                {
                    return;
                }
                const ChromeEditPhase phase =
                    change.phase == DxUi::NumericStepperChangePhase::Preview  ? ChromeEditPhase::Preview
                    : change.phase == DxUi::NumericStepperChangePhase::Commit ? ChromeEditPhase::Commit
                                                                              : ChromeEditPhase::Cancel;
                _onOpacity(change.value / 100.0, phase);
            });
        _layers = root->AddChild<DxUi::Tree>();
        _treeModel.onLayer = &_onLayer;
        _treeModel.onExpand = &_onExpand;
        _layers->SetModel(&_treeModel);
        _layers->SetDelegate(&_treeModel);
        _status = root->AddChild<DxUi::StatusStrip>();
        _status->SetSections({DxUi::StatusStrip::Section{std::wstring{}, 80.0f},
                              DxUi::StatusStrip::Section{std::wstring{}, 140.0f},
                              DxUi::StatusStrip::Section{std::wstring{}, 0.0f}});
        // The overlay draws the transform box over the hole and is never hit.
        _overlay = root->AddChild<CanvasOverlay>();
        // The splitter spans canvas and layers; only its separator (plus slop) is hittable, so it goes above them.
        _splitter = root->AddChild<DxUi::Splitter>();
        _splitter->SetMinimumFirstPane(kCanvasMinDip);
        _splitter->SetMinimumSecondPane(kLayersMinDip);
        _splitter->SetOnChange([this](DxUi::SplitterChange change) { OnSplitterChange(change); });
        // The picker card floats over the canvas hole; chrome composites over the canvas with premultiplied alpha.
        _pickerCard = root->AddChild<DxUi::CardPanel>();
        _pickerCard->SetCornerRadius(8.0f);
        _pickerCard->SetVisible(false);
        _picker = _pickerCard->AddChild<DxUi::ColorPicker>();
        DxUi::ColorPicker::Labels labels;
        labels.newColor = LoadStringResource(nullptr, IDS_PICKER_NEW);
        labels.currentColor = LoadStringResource(nullptr, IDS_PICKER_CURRENT);
        labels.hex = LoadStringResource(nullptr, IDS_PICKER_HEX);
        labels.red = LoadStringResource(nullptr, IDS_PICKER_RED);
        labels.green = LoadStringResource(nullptr, IDS_PICKER_GREEN);
        labels.blue = LoadStringResource(nullptr, IDS_PICKER_BLUE);
        labels.ok = LoadStringResource(nullptr, IDS_PICKER_OK);
        labels.cancel = LoadStringResource(nullptr, IDS_PICKER_CANCEL);
        _picker->SetLabels(std::move(labels));
        _picker->SetOnChange([this](DxUi::ColorPickerChange change) { OnPickerChange(change); });
        _view.Controls().SetRoot(std::move(root));
        SyncToolVisuals();
        Arrange();
        return S_OK;
    }
    catch (const std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }
}

float ChromeHost::WidthDip() const noexcept
{
    return (_dpi > 0.0f) ? (static_cast<float>(_width) * 96.0f / _dpi) : static_cast<float>(_width);
}

float ChromeHost::HeightDip() const noexcept
{
    return (_dpi > 0.0f) ? (static_cast<float>(_height) * 96.0f / _dpi) : static_cast<float>(_height);
}

D2D1_RECT_F ChromeHost::ToolButtonBoundsDip(EditorTool tool) const noexcept
{
    const float inset = (kRailWidthDip - kToolButtonDip) / 2.0f;
    const float top = kMenuHeightDip + kTabHeightDip + inset +
                      static_cast<float>(EditorToolIndex(tool)) * (kToolButtonDip + kToolGapDip);
    return D2D1::RectF(inset, top, inset + kToolButtonDip, top + kToolButtonDip);
}

void ChromeHost::Arrange() noexcept
{
    if (!_menu)
    {
        return;
    }
    const float widthDip = WidthDip();
    const float heightDip = HeightDip();
    if (auto* root = _view.Controls().GetRoot())
    {
        root->SetBounds(D2D1::RectF(0.0f, 0.0f, widthDip, heightDip));
    }
    const float menuBottom = kMenuHeightDip;
    const float tabBottom = menuBottom + kTabHeightDip;
    const float headerBottom = tabBottom + kHeaderHeightDip;
    const float statusTop = heightDip - kStatusHeightDip;
    const float extent = std::max(0.0f, widthDip - kRailWidthDip);
    if (_splitter)
    {
        // The splitter owns the clamp: canvas minimum, layers minimum and maximum, and the persisted preference.
        _splitter->SetBounds(D2D1::RectF(kRailWidthDip, tabBottom, widthDip, statusTop));
        _splitter->SetMinimumFirstPane(std::max(kCanvasMinDip, extent - kLayersMaxDip - kSplitterWidthDip));
        _splitter->SetMinimumSecondPane(kLayersMinDip);
        _splitter->SetPosition(extent - _preferredLayersWidthDip - kSplitterWidthDip);
        _layersWidthDip = std::max(0.0f, extent - _splitter->GetPosition() - kSplitterWidthDip);
    }
    else
    {
        _layersWidthDip = std::min(kLayersMaxDip, std::max(kLayersMinDip, _preferredLayersWidthDip));
    }
    const float layersLeft = widthDip - _layersWidthDip;
    _menu->SetBounds(D2D1::RectF(0.0f, 0.0f, widthDip, menuBottom));
    if (_tabs)
    {
        _tabs->SetBounds(D2D1::RectF(0.0f, menuBottom, widthDip, tabBottom));
    }
    if (_rail)
    {
        _rail->SetBounds(D2D1::RectF(0.0f, tabBottom, kRailWidthDip, statusTop));
        const float wellTop = std::max(tabBottom, statusTop - kWellBlockDip);
        for (std::size_t index = 0; index < kEditorToolCount; ++index)
        {
            if (DxUi::Button* button = _toolButtons[index])
            {
                const D2D1_RECT_F bounds = ToolButtonBoundsDip(EditorToolAt(index));
                button->SetBounds(bounds);
                button->SetVisible(bounds.bottom <= wellTop - kToolGapDip);
            }
        }
        if (_backgroundWell)
        {
            _backgroundWell->SetBounds(PaletteWellBoundsDip(true));
        }
        if (_foregroundWell)
        {
            _foregroundWell->SetBounds(PaletteWellBoundsDip(false));
        }
    }
    if (_header)
    {
        _header->SetBounds(D2D1::RectF(kRailWidthDip + 8.0f, tabBottom, layersLeft - kSplitterWidthDip, headerBottom));
    }
    ArrangeTransformHeader(kRailWidthDip + 8.0f, tabBottom, layersLeft - kSplitterWidthDip, headerBottom);
    if (_overlay)
    {
        _overlay->SetBounds(D2D1::RectF(kRailWidthDip, headerBottom, layersLeft - kSplitterWidthDip, statusTop));
    }
    if (_layersTitle)
    {
        const float titleRight = std::max(layersLeft + 4.0f, widthDip - kOpacityWidthDip - 8.0f);
        _layersTitle->SetBounds(D2D1::RectF(layersLeft + 4.0f, tabBottom, titleRight, tabBottom + kLayersTitleDip));
    }
    if (_opacity)
    {
        _opacity->SetBounds(D2D1::RectF(widthDip - 4.0f - kOpacityWidthDip, tabBottom + 2.0f, widthDip - 4.0f,
                                        tabBottom + kLayersTitleDip - 2.0f));
    }
    if (_layers)
    {
        _layers->SetBounds(D2D1::RectF(layersLeft, tabBottom + kLayersTitleDip, widthDip, statusTop));
    }
    if (_status)
    {
        _status->SetBounds(D2D1::RectF(0.0f, statusTop, widthDip, heightDip));
    }
    ArrangePicker();
}

void ChromeHost::ArrangePicker() noexcept
{
    if (!_pickerCard || !_picker)
    {
        return;
    }
    const float left = kRailWidthDip + kPickerMarginDip;
    const float top = kMenuHeightDip + kTabHeightDip + kHeaderHeightDip + kPickerMarginDip;
    const float width = DxUi::ColorPicker::kDefaultWidthDip + 2.0f * kPickerMarginDip;
    const float height = DxUi::ColorPicker::kDefaultHeightDip + 2.0f * kPickerMarginDip;
    _pickerCard->SetBounds(D2D1::RectF(left, top, left + width, top + height));
    _picker->SetBounds(D2D1::RectF(left + kPickerMarginDip, top + kPickerMarginDip,
                                   left + kPickerMarginDip + DxUi::ColorPicker::kDefaultWidthDip,
                                   top + kPickerMarginDip + DxUi::ColorPicker::kDefaultHeightDip));
}

D2D1_RECT_F ChromeHost::SplitterBoundsDip() const noexcept
{
    return _splitter ? _splitter->GetSeparatorBounds() : D2D1::RectF();
}

D2D1_RECT_F ChromeHost::PaletteWellBoundsDip(bool background) const noexcept
{
    const float statusTop = HeightDip() - kStatusHeightDip;
    const float wellTop = std::max(kMenuHeightDip + kTabHeightDip, statusTop - kWellBlockDip);
    const float inset = (kRailWidthDip - kWellDip - kWellOffsetDip) / 2.0f;
    const float left = inset + (background ? kWellOffsetDip : 0.0f);
    const float top = wellTop + inset + (background ? kWellOffsetDip : 0.0f);
    return D2D1::RectF(left, top, left + kWellDip, top + kWellDip);
}

D2D1_RECT_F ChromeHost::ColorPickerBoundsDip() const noexcept
{
    return ColorPickerOpen() ? _pickerCard->GetBounds() : D2D1::RectF();
}

bool ChromeHost::ColorPickerOpen() const noexcept
{
    return _pickerCard != nullptr && _picker != nullptr && _pickerCard->IsVisible();
}

void ChromeHost::OnSplitterChange(DxUi::SplitterChange change) noexcept
{
    // No re-layout here: SetBounds inside a captured drag bumps the embedded interaction revision, and the next
    // pointer event would cancel the drag. The next Prepare (every frame) arranges the panes from the new width.
    const float extent = std::max(0.0f, WidthDip() - kRailWidthDip);
    _layersWidthDip = std::max(0.0f, extent - change.position - kSplitterWidthDip);
    _preferredLayersWidthDip = _layersWidthDip;
    _view.MarkDirty();
    if (_onLayout)
    {
        _onLayout();
    }
}

void ChromeHost::OnPickerChange(DxUi::ColorPickerChange change) noexcept
{
    switch (change.phase)
    {
    case DxUi::ColorPickerChangePhase::Preview:
        if (DxUi::ColorSwatch* well = _pickerBackground ? _backgroundWell : _foregroundWell)
        {
            well->SetSwatchValue(change.argb);
        }
        _view.MarkDirty();
        break;
    case DxUi::ColorPickerChangePhase::Commit:
        FinishPicker(true, change.argb);
        break;
    case DxUi::ColorPickerChangePhase::Cancel:
    default:
        FinishPicker(false, change.argb);
        break;
    }
}

void ChromeHost::FinishPicker(bool commit, uint32_t argb) noexcept
{
    uint32_t& target = _pickerBackground ? _backgroundArgb : _foregroundArgb;
    if (commit)
    {
        target = argb | 0xFF000000u;
    }
    DxUi::ColorSwatch* well = _pickerBackground ? _backgroundWell : _foregroundWell;
    if (well)
    {
        well->SetSwatchValue(target);
    }
    if (_pickerCard)
    {
        _pickerCard->SetVisible(false);
    }
    _view.Controls().SetFocusControl(well);
    _view.MarkDirty();
    if (commit && _onPalette)
    {
        _onPalette(_foregroundArgb, _backgroundArgb);
    }
}

void ChromeHost::OpenColorPicker(bool background) noexcept
{
    if (!_picker || !_pickerCard)
    {
        return;
    }
    _pickerBackground = background;
    _picker->SetColor(background ? _backgroundArgb : _foregroundArgb);
    ArrangePicker();
    _pickerCard->SetVisible(true);
    _view.Controls().SetFocusControl(_picker);
    _canvasFocused = false;
    _view.MarkDirty();
}

void ChromeHost::CloseColorPicker(bool commit) noexcept
{
    if (!ColorPickerOpen())
    {
        return;
    }
    if (commit)
    {
        _picker->Commit(_view.Controls());
    }
    else
    {
        _picker->Cancel(_view.Controls());
    }
}

void ChromeHost::SampleColor(uint32_t argb) noexcept
{
    if (ColorPickerOpen())
    {
        _picker->SampleColor(_view.Controls(), argb);
    }
}

bool ChromeHost::RequestLayerOpacity(double percent) noexcept
{
    return _opacity != nullptr && _opacity->RequestValue(_view.Controls(), percent);
}

void ChromeHost::SetPalette(uint32_t foregroundArgb, uint32_t backgroundArgb) noexcept
{
    _foregroundArgb = foregroundArgb | 0xFF000000u;
    _backgroundArgb = backgroundArgb | 0xFF000000u;
    const bool open = ColorPickerOpen();
    if (_foregroundWell && !(open && !_pickerBackground))
    {
        _foregroundWell->SetSwatchValue(_foregroundArgb);
    }
    if (_backgroundWell && !(open && _pickerBackground))
    {
        _backgroundWell->SetSwatchValue(_backgroundArgb);
    }
}

void ChromeHost::SetActiveLayerOpacity(std::optional<double> opacity) noexcept
{
    if (!_opacity)
    {
        return;
    }
    if (!opacity.has_value() || !std::isfinite(opacity.value()))
    {
        _opacity->SetEnabled(false);
        if (!_opacity->IsEditing())
        {
            _opacity->SetValue(100.0);
        }
        return;
    }
    _opacity->SetEnabled(true);
    if (!_opacity->IsEditing())
    {
        _opacity->SetValue(std::round(std::min(1.0, std::max(0.0, opacity.value())) * 100.0));
    }
}

void ChromeHost::SetLayersWidth(float widthDip) noexcept
{
    _preferredLayersWidthDip = std::isfinite(widthDip) ? widthDip : kLayersDefaultDip;
    if (_attached)
    {
        Arrange();
        _view.MarkDirty();
    }
}

CanvasHole ChromeHost::HolePixels() const noexcept
{
    const float scale = _dpi / 96.0f;
    const float widthDip = WidthDip();
    const float heightDip = HeightDip();
    const float left = kRailWidthDip * scale;
    const float top = (kMenuHeightDip + kTabHeightDip + kHeaderHeightDip) * scale;
    const float right = (widthDip - _layersWidthDip - kSplitterWidthDip) * scale;
    const float bottom = (heightDip - kStatusHeightDip) * scale;
    CanvasHole hole;
    hole.left = left;
    hole.top = top;
    hole.width = std::max(0.0f, right - left);
    hole.height = std::max(0.0f, bottom - top);
    return hole;
}

void ChromeHost::SetDocumentTitle(std::wstring title, bool dirty)
{
    if (_tabs && _tabs->GetTabCount() > 0)
    {
        _tabs->SetTabTitle(0, dirty ? FormatStringResource(nullptr, IDS_DIRTY_MARK, title) : std::move(title));
    }
}

void ChromeHost::SetStatus(std::wstring zoom, std::wstring size, std::wstring tool)
{
    if (_status && _status->GetSectionCount() >= 3)
    {
        _status->SetSectionText(0, std::move(zoom));
        _status->SetSectionText(1, std::move(size));
        _status->SetSectionText(2, std::move(tool));
    }
    if (_header)
    {
        _header->SetText(LoadStringResource(nullptr, EditorToolNameId(_tool)));
    }
}

void ChromeHost::SetUndoRedoNames(const std::wstring& undoName, const std::wstring& redoName, bool canUndo,
                                  bool canRedo)
{
    HMENU edit = _menus[static_cast<std::size_t>(MenuIndex::Edit)].get();
    if (!edit)
    {
        return;
    }
    const std::wstring undo = undoName.empty() ? LoadStringResource(nullptr, IDS_CMD_UNDO)
                                               : FormatStringResource(nullptr, IDS_CMD_UNDO_NAMED, undoName);
    const std::wstring redo = redoName.empty() ? LoadStringResource(nullptr, IDS_CMD_REDO)
                                               : FormatStringResource(nullptr, IDS_CMD_REDO_NAMED, redoName);
    (void)ModifyMenuW(edit, static_cast<UINT>(kCommandUndo),
                      MF_BYCOMMAND | MF_STRING | (canUndo ? MF_ENABLED : MF_GRAYED),
                      static_cast<UINT_PTR>(kCommandUndo), undo.c_str());
    (void)ModifyMenuW(edit, static_cast<UINT>(kCommandRedo),
                      MF_BYCOMMAND | MF_STRING | (canRedo ? MF_ENABLED : MF_GRAYED),
                      static_cast<UINT_PTR>(kCommandRedo), redo.c_str());
}

void ChromeHost::SyncToolVisuals() noexcept
{
    for (std::size_t index = 0; index < kEditorToolCount; ++index)
    {
        if (DxUi::Button* button = _toolButtons[index])
        {
            button->SetPressedVisual(EditorToolAt(index) == _tool);
        }
    }
}

void ChromeHost::SetTool(EditorTool tool) noexcept
{
    _tool = tool;
    SyncToolVisuals();
    if (_transformHeader)
    {
        _transformHeader->SetVisible(tool == EditorTool::Move);
    }
    if (_header)
    {
        _header->SetVisible(tool != EditorTool::Move);
    }
    if (_header)
    {
        _header->SetText(LoadStringResource(nullptr, EditorToolNameId(_tool)));
    }
}

void ChromeHost::SetLayers(std::vector<HierarchyEntry> rows, const std::optional<Guid>& active)
{
    _treeModel.rows = std::move(rows);
    _treeModel.active = active;
    if (_layers)
    {
        _layers->NotifyDataChanged();
        std::optional<uint64_t> selected;
        if (active.has_value())
        {
            for (size_t index = 0; index < _treeModel.rows.size(); ++index)
            {
                if (_treeModel.rows[index].id == active.value())
                {
                    selected = index + 1;
                    break;
                }
            }
        }
        _layers->SetSelectedItemId(selected);
    }
}

HRESULT ChromeHost::Prepare(UINT widthPixels, UINT heightPixels, float dpi) noexcept
{
    if (!_attached)
    {
        return E_UNEXPECTED;
    }
    _width = widthPixels;
    _height = heightPixels;
    _dpi = dpi > 0.0f ? dpi : 96.0f;
    SyncToolVisuals();
    Arrange();
    return _view.Prepare(widthPixels, heightPixels, _dpi);
}

HRESULT ChromeHost::Composite(ID3D11DeviceContext* context, const D3D11_VIEWPORT& viewport) noexcept
{
    if (!_attached)
    {
        return E_UNEXPECTED;
    }
    return _view.Composite(context, viewport);
}

bool ChromeHost::DispatchPointer(const DxUi::PointerEvent& event) noexcept
{
    if (!_attached)
    {
        return false;
    }
    const CanvasHole hole = HolePixels();
    bool inHole = event.xPixels >= hole.left && event.yPixels >= hole.top && event.xPixels < hole.left + hole.width &&
                  event.yPixels < hole.top + hole.height;
    if (inHole && ColorPickerOpen())
    {
        // The picker card floats over the hole; pointer input on it belongs to chrome, not the canvas.
        const float scale = _dpi > 0.0f ? _dpi / 96.0f : 1.0f;
        const D2D1_RECT_F card = _pickerCard->GetBounds();
        inHole = !(event.xPixels >= card.left * scale && event.xPixels < card.right * scale &&
                   event.yPixels >= card.top * scale && event.yPixels < card.bottom * scale);
    }
    if (inHole && event.action == DxUi::PointerAction::Down)
    {
        _canvasFocused = true;
        return false;
    }
    if (!inHole && event.action == DxUi::PointerAction::Down)
    {
        _canvasFocused = false;
    }
    return _view.DispatchPointer(event);
}

bool ChromeHost::DispatchKey(UINT virtualKey, bool down, UINT modifiers) noexcept
{
    if (!_attached)
    {
        return false;
    }
    if (down && virtualKey == VK_ESCAPE && _canvasFocused)
    {
        _canvasFocused = false;
        return true;
    }
    if (_view.DispatchKey(virtualKey, down, modifiers))
    {
        return true;
    }
    if (down && virtualKey == VK_ESCAPE && ColorPickerOpen())
    {
        // Focus sat on a picker child that did not consume Escape: the picker still closes without committing.
        CloseColorPicker(false);
        return true;
    }
    return false;
}

bool ChromeHost::DispatchCharacter(wchar_t character, UINT modifiers) noexcept
{
    return _attached && _view.DispatchCharacter(character, modifiers);
}

bool ChromeHost::AdvanceAnimation(uint64_t nowTickMs) noexcept
{
    return _attached && _view.AdvanceAnimation(nowTickMs);
}

bool ChromeHost::NeedsPreparation() const noexcept
{
    return _attached && _view.NeedsPreparation();
}

bool ChromeHost::NeedsAnimation() const noexcept
{
    return _attached && _view.NeedsAnimation();
}

DxUi::EmbeddedStatistics ChromeHost::Statistics() const noexcept
{
    return _view.GetStatistics();
}

// ── Move tool header (transform inspector) and canvas overlay ────────────

void ChromeHost::BuildTransformHeader(DxUi::Panel& root)
{
    _transformHeader = root.AddChild<DxUi::Panel>();
    _transformTitle = _transformHeader->AddChild<DxUi::Label>(LoadStringResource(nullptr, IDS_TRANSFORM_TITLE));
    _transformTitle->SetFontRole(DxUi::FontRole::BodyStrong);
    const auto stepper = [this](UINT labelId, float labelWidth, UINT unitId, float unitWidth, double minimum,
                                double maximum, std::uint8_t decimals, TransformAction action)
    {
        DxUi::NumericStepper* control = _transformHeader->AddChild<DxUi::NumericStepper>();
        control->SetLabel(LoadStringResource(nullptr, labelId), labelWidth);
        if (unitId != 0)
        {
            control->SetUnit(LoadStringResource(nullptr, unitId), unitWidth);
        }
        control->SetMinimum(minimum);
        control->SetMaximum(maximum);
        control->SetStep(1.0);
        control->SetLargeStep(10.0);
        control->SetDecimals(decimals);
        control->SetOnChange(
            [this, action](DxUi::NumericStepperChange change)
            {
                if (!_syncingInspector && _onTransform)
                {
                    _onTransform(action, change.value);
                }
            });
        return control;
    };
    _transformX = stepper(IDS_TRANSFORM_X, 14.0f, 0, 0.0f, -1.0e6, 1.0e6, 0, TransformAction::SetX);
    _transformY = stepper(IDS_TRANSFORM_Y, 14.0f, 0, 0.0f, -1.0e6, 1.0e6, 0, TransformAction::SetY);
    _transformWidth = stepper(IDS_TRANSFORM_WIDTH, 14.0f, 0, 0.0f, 1.0, 300000.0, 0, TransformAction::SetWidth);
    _transformHeight = stepper(IDS_TRANSFORM_HEIGHT, 14.0f, 0, 0.0f, 1.0, 300000.0, 0, TransformAction::SetHeight);
    _lockRatio = _transformHeader->AddChild<DxUi::Checkbox>(LoadStringResource(nullptr, IDS_TRANSFORM_LOCK));
    _lockRatio->SetTooltipText(LoadStringResource(nullptr, IDS_TRANSFORM_LOCK_RATIO));
    _lockRatio->SetChecked(true);
    _lockRatio->SetOnToggled(
        [this](bool checked)
        {
            if (!_syncingInspector && _onTransform)
            {
                _onTransform(TransformAction::LockRatio, checked ? 1.0 : 0.0);
            }
        });
    _transformScale =
        stepper(IDS_TRANSFORM_SCALE, 38.0f, IDS_UNIT_PERCENT, 14.0f, 1.0, 10000.0, 1, TransformAction::SetScale);
    _transformScale->SetTooltipText(LoadStringResource(nullptr, IDS_TRANSFORM_SCALE_TIP));
    _transformAngle =
        stepper(IDS_TRANSFORM_ANGLE, 40.0f, IDS_UNIT_DEGREES, 12.0f, -360.0, 360.0, 0, TransformAction::SetRotation);
    _flipHorizontal =
        _transformHeader->AddChild<DxUi::Button>(LoadStringResource(nullptr, IDS_TRANSFORM_FLIP_HORIZONTAL));
    _flipHorizontal->SetOnClick(
        [this]
        {
            if (_onTransform)
            {
                _onTransform(TransformAction::FlipHorizontal, 0.0);
            }
        });
    _flipVertical = _transformHeader->AddChild<DxUi::Button>(LoadStringResource(nullptr, IDS_TRANSFORM_FLIP_VERTICAL));
    _flipVertical->SetOnClick(
        [this]
        {
            if (_onTransform)
            {
                _onTransform(TransformAction::FlipVertical, 0.0);
            }
        });
    _sampling = _transformHeader->AddChild<DxUi::ComboBox>();
    _sampling->SetItems({DxUi::ComboBox::Item{L"nearest", LoadStringResource(nullptr, IDS_TRANSFORM_SAMPLING_NEAREST)},
                         DxUi::ComboBox::Item{L"smooth", LoadStringResource(nullptr, IDS_TRANSFORM_SAMPLING_SMOOTH)},
                         DxUi::ComboBox::Item{L"high", LoadStringResource(nullptr, IDS_TRANSFORM_SAMPLING_HIGH)}});
    _sampling->SetSelectedIndex(2);
    _sampling->SetOnSelectionChanged(
        [this](size_t index)
        {
            if (!_syncingInspector && _onTransform)
            {
                _onTransform(TransformAction::SetSampling, static_cast<double>(index));
            }
        });
    _autoSelect = _transformHeader->AddChild<DxUi::Checkbox>(LoadStringResource(nullptr, IDS_TRANSFORM_AUTO_SELECT));
    _autoSelect->SetTooltipText(LoadStringResource(nullptr, IDS_TRANSFORM_AUTO_SELECT_TIP));
    _autoSelect->SetOnToggled(
        [this](bool checked)
        {
            if (!_syncingInspector && _onTransform)
            {
                _onTransform(TransformAction::AutoSelect, checked ? 1.0 : 0.0);
            }
        });
    _showControls =
        _transformHeader->AddChild<DxUi::Checkbox>(LoadStringResource(nullptr, IDS_TRANSFORM_SHOW_CONTROLS));
    _showControls->SetTooltipText(LoadStringResource(nullptr, IDS_TRANSFORM_SHOW_CONTROLS_TIP));
    _showControls->SetChecked(true);
    _showControls->SetOnToggled(
        [this](bool checked)
        {
            if (!_syncingInspector && _onTransform)
            {
                _onTransform(TransformAction::ShowControls, checked ? 1.0 : 0.0);
            }
        });
    _transformCancel = _transformHeader->AddChild<DxUi::Button>(LoadStringResource(nullptr, IDS_TRANSFORM_CANCEL));
    _transformCancel->SetOnClick(
        [this]
        {
            if (_onTransform)
            {
                _onTransform(TransformAction::Cancel, 0.0);
            }
        });
    _transformApply = _transformHeader->AddChild<DxUi::Button>(LoadStringResource(nullptr, IDS_TRANSFORM_APPLY));
    _transformApply->SetPrimary(true);
    _transformApply->SetOnClick(
        [this]
        {
            if (_onTransform)
            {
                _onTransform(TransformAction::Apply, 0.0);
            }
        });
    _transformHeader->SetVisible(_tool == EditorTool::Move);
    if (_header)
    {
        _header->SetVisible(_tool != EditorTool::Move);
    }
}

void ChromeHost::ArrangeTransformHeader(float left, float top, float right, float bottom) noexcept
{
    if (!_transformHeader)
    {
        return;
    }
    _transformHeader->SetBounds(D2D1::RectF(left, top, right, bottom));
    constexpr float kRowDip = 28.0f;
    constexpr float kGapDip = 6.0f;
    constexpr float kButtonDip = 64.0f;
    const float rowTop = top + (bottom - top - kRowDip) / 2.0f;
    const float rowBottom = rowTop + kRowDip;
    // Apply and Cancel pin to the right; the rest runs left to right in priority order and stops when out of room.
    float rightEdge = right - 4.0f;
    const bool pinned = right - left > 3.0f * kButtonDip;
    for (DxUi::Button* button : {_transformApply, _transformCancel})
    {
        if (button == nullptr)
        {
            continue;
        }
        button->SetVisible(pinned);
        if (pinned)
        {
            button->SetBounds(D2D1::RectF(rightEdge - kButtonDip, rowTop, rightEdge, rowBottom));
            rightEdge -= kButtonDip + kGapDip;
        }
    }
    struct Slot
    {
        DxUi::Control* control;
        float width;
    };
    const std::array<Slot, 13> slots{
        Slot{_transformTitle, 80.0f},  Slot{_transformX, 92.0f},      Slot{_transformY, 92.0f},
        Slot{_transformWidth, 92.0f},  Slot{_transformHeight, 92.0f}, Slot{_lockRatio, 60.0f},
        Slot{_transformScale, 136.0f}, Slot{_transformAngle, 116.0f}, Slot{_flipHorizontal, 64.0f},
        Slot{_flipVertical, 64.0f},    Slot{_sampling, 130.0f},       Slot{_autoSelect, 104.0f},
        Slot{_showControls, 122.0f}};
    float cursor = left + 4.0f;
    bool room = true;
    for (const Slot& slot : slots)
    {
        if (slot.control == nullptr)
        {
            continue;
        }
        const bool fits = room && cursor + slot.width <= rightEdge;
        slot.control->SetVisible(fits);
        if (fits)
        {
            slot.control->SetBounds(D2D1::RectF(cursor, rowTop, cursor + slot.width, rowBottom));
            cursor += slot.width + kGapDip;
        }
        else
        {
            room = false;
        }
    }
}

void ChromeHost::SetTransformInspector(const TransformInspectorState& state) noexcept
{
    _inspector = state;
    if (!_transformHeader)
    {
        return;
    }
    _syncingInspector = true;
    _transformTitle->SetText(
        LoadStringResource(nullptr, state.targetsMask ? IDS_TRANSFORM_MASK_TITLE : IDS_TRANSFORM_TITLE));
    const bool numbers = state.enabled && !state.distorted;
    const auto sync = [&](DxUi::NumericStepper* stepper, double value)
    {
        stepper->SetEnabled(numbers);
        if (!stepper->IsEditing())
        {
            stepper->SetValue(value);
        }
    };
    sync(_transformX, state.x);
    sync(_transformY, state.y);
    sync(_transformWidth, state.width);
    sync(_transformHeight, state.height);
    sync(_transformScale, state.scale);
    sync(_transformAngle, state.rotation);
    _lockRatio->SetEnabled(numbers);
    _lockRatio->SetChecked(state.lockRatio);
    _sampling->SetEnabled(numbers);
    _sampling->SetSelectedIndex(static_cast<size_t>(std::clamp(state.sampling, 0, 2)));
    _flipHorizontal->SetEnabled(numbers);
    _flipVertical->SetEnabled(numbers);
    _autoSelect->SetChecked(state.autoSelect);
    _showControls->SetChecked(state.showControls);
    _transformCancel->SetEnabled(state.editing);
    _transformApply->SetEnabled(state.editing);
    _syncingInspector = false;
}

void ChromeHost::SetTransformOverlay(const std::optional<TransformOverlayGeometry>& geometry,
                                     const std::vector<float>& guideXs, const std::vector<float>& guideYs) noexcept
{
    if (!_overlay)
    {
        return;
    }
    const float toDip = _dpi > 0.0f ? 96.0f / _dpi : 1.0f;
    std::optional<CanvasOverlay::Geometry> converted;
    if (geometry.has_value())
    {
        CanvasOverlay::Geometry result;
        for (std::size_t index = 0; index < 8; ++index)
        {
            result.handles[index] = D2D1::Point2F(static_cast<float>(geometry->handles[index].x) * toDip,
                                                  static_cast<float>(geometry->handles[index].y) * toDip);
        }
        result.rotation = D2D1::Point2F(static_cast<float>(geometry->rotationHandle.x) * toDip,
                                        static_cast<float>(geometry->rotationHandle.y) * toDip);
        result.showsRotation = geometry->showsRotation;
        converted = result;
    }
    std::vector<float> xs;
    std::vector<float> ys;
    for (const float x : guideXs)
    {
        xs.push_back(x * toDip);
    }
    for (const float y : guideYs)
    {
        ys.push_back(y * toDip);
    }
    _overlay->SetGeometry(converted);
    _overlay->SetGuides(std::move(xs), std::move(ys));
}
