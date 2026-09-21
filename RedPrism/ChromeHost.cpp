#include "ChromeHost.h"

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
    _view.Controls().SetTheme(_palette);
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
    DetachAccessibility(); // DxUi drops UIA on a device swap; the next Prepare attaches again
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
    DetachAccessibility();
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
    _adjustCard = nullptr;
    _curveEditor = nullptr;
    _levelsInput = nullptr;
    _levelsOutput = nullptr;
    _spectrum = nullptr;
    _gradientRamp = nullptr;
    _adjustRows = {};
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
    if (!visible)
    {
        DetachAccessibility(); // DxUi drops UIA on hide; the next Prepare attaches again
    }
    _view.SetVisible(visible);
}

void ChromeHost::SetOwnerWindow(HWND window) noexcept
{
    _owner = window;
}

void ChromeHost::SetTheme(const DxUi::ThemePalette& palette) noexcept
{
    _palette = palette;
    if (_attached)
    {
        _view.Controls().SetTheme(_palette);
        _view.MarkDirty();
    }
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

void ChromeHost::SetSelectionHandler(std::function<void(SelectionAction, double)> handler)
{
    _onSelection = std::move(handler);
}

void ChromeHost::SetBrushHandler(std::function<void(BrushAction, double)> handler)
{
    _onBrush = std::move(handler);
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
        AppendCommand(edit, kCommandCut, IDS_CMD_CUT);
        AppendCommand(edit, kCommandCopy, IDS_CMD_COPY);
        AppendCommand(edit, kCommandCopyMerged, IDS_CMD_COPY_MERGED);
        AppendCommand(edit, kCommandPaste, IDS_CMD_PASTE);
        AppendSeparator(edit);
        AppendCommand(edit, kCommandFillForeground, IDS_CMD_FILL_FOREGROUND);
        AppendCommand(edit, kCommandFillBackground, IDS_CMD_FILL_BACKGROUND);
        AppendCommand(edit, kCommandClearPixels, IDS_CMD_CLEAR_PIXELS);
        AppendCommand(edit, kCommandContentAwareFill, IDS_CMD_CONTENT_AWARE_FILL);
        AppendSeparator(edit);
        AppendCommand(edit, kCommandInvert, IDS_CMD_INVERT);
        AppendSeparator(edit);
        AppendCommand(edit, kCommandFreeTransform, IDS_CMD_FREE_TRANSFORM);
    }
    if (HMENU image = _menus[static_cast<std::size_t>(MenuIndex::Image)].get())
    {
        AppendCommand(image, kCommandCurves, IDS_CMD_CURVES);
        AppendCommand(image, kCommandLevels, IDS_CMD_LEVELS);
        AppendCommand(image, kCommandHueSaturation, IDS_CMD_HUE_SATURATION);
        AppendCommand(image, kCommandExposure, IDS_CMD_EXPOSURE);
        AppendCommand(image, kCommandGradientMap, IDS_CMD_GRADIENT_MAP);
        AppendCommand(image, kCommandGrain, IDS_CMD_GRAIN);
        AppendSeparator(image);
        AppendCommand(image, kCommandFlipCanvasHorizontal, IDS_CMD_FLIP_CANVAS_HORIZONTAL);
        AppendCommand(image, kCommandFlipCanvasVertical, IDS_CMD_FLIP_CANVAS_VERTICAL);
    }
    if (HMENU layer = _menus[static_cast<std::size_t>(MenuIndex::Layer)].get())
    {
        AppendCommand(layer, kCommandNewLayer, IDS_CMD_NEW_LAYER);
        AppendCommand(layer, kCommandNewFolder, IDS_CMD_NEW_FOLDER);
        AppendCommand(layer, kCommandLayerViaCopy, IDS_CMD_LAYER_VIA_COPY);
        if (HMENU adjustments = CreatePopupMenu())
        {
            // The submenu belongs to the Layer menu once appended; DestroyMenu on the parent frees it.
            AppendCommand(adjustments, kCommandNewLevelsAdjustment, IDS_CMD_ADJUST_LEVELS);
            AppendCommand(adjustments, kCommandNewCurvesAdjustment, IDS_CMD_ADJUST_CURVES);
            AppendCommand(adjustments, kCommandNewHueSaturationAdjustment, IDS_CMD_ADJUST_HSV);
            AppendCommand(adjustments, kCommandNewExposureAdjustment, IDS_CMD_ADJUST_EXPOSURE);
            AppendCommand(adjustments, kCommandNewGradientMapAdjustment, IDS_CMD_ADJUST_GRADIENT_MAP);
            AppendCommand(adjustments, kCommandNewGrainAdjustment, IDS_CMD_ADJUST_GRAIN);
            const std::wstring caption = LoadStringResource(nullptr, IDS_CMD_NEW_ADJUSTMENT);
            (void)AppendMenuW(layer, MF_POPUP | MF_STRING, reinterpret_cast<UINT_PTR>(adjustments), caption.c_str());
        }
        AppendCommand(layer, kCommandEditAdjustment, IDS_CMD_EDIT_ADJUSTMENT);
        AppendSeparator(layer);
        AppendCommand(layer, kCommandDeleteLayer, IDS_CMD_DELETE_LAYER);
        AppendCommand(layer, kCommandToggleLayerVisibility, IDS_CMD_TOGGLE_LAYER_VISIBILITY);
        AppendCommand(layer, kCommandMoveOutOfGroup, IDS_CMD_MOVE_OUT_OF_GROUP);
        AppendSeparator(layer);
        AppendCommand(layer, kCommandAddMask, IDS_CMD_ADD_MASK);
        AppendCommand(layer, kCommandAddBlackMask, IDS_CMD_ADD_BLACK_MASK);
        AppendSeparator(layer);
        AppendCommand(layer, kCommandFlipLayerHorizontal, IDS_CMD_FLIP_LAYER_HORIZONTAL);
        AppendCommand(layer, kCommandFlipLayerVertical, IDS_CMD_FLIP_LAYER_VERTICAL);
    }
    if (HMENU select = _menus[static_cast<std::size_t>(MenuIndex::Select)].get())
    {
        AppendCommand(select, kCommandSelectAll, IDS_CMD_SELECT_ALL);
        AppendCommand(select, kCommandDeselect, IDS_CMD_DESELECT);
        AppendCommand(select, kCommandInverse, IDS_CMD_INVERSE);
        AppendSeparator(select);
        AppendCommand(select, kCommandLoadLayerSelection, IDS_CMD_LOAD_LAYER_SELECTION);
        AppendCommand(select, kCommandLoadMaskSelection, IDS_CMD_LOAD_MASK_SELECTION);
    }
    if (HMENU filter = _menus[static_cast<std::size_t>(MenuIndex::Filter)].get())
    {
        AppendCommand(filter, kCommandGaussianBlur, IDS_CMD_GAUSSIAN_BLUR);
        AppendCommand(filter, kCommandMotionBlur, IDS_CMD_MOTION_BLUR);
        AppendCommand(filter, kCommandAddNoise, IDS_CMD_ADD_NOISE);
        AppendCommand(filter, kCommandLensCorrection, IDS_CMD_LENS_CORRECTION);
    }
    if (HMENU view = _menus[static_cast<std::size_t>(MenuIndex::View)].get())
    {
        AppendCommand(view, kCommandZoomFit, IDS_CMD_ZOOM_FIT);
        AppendCommand(view, kCommandZoomActual, IDS_CMD_ZOOM_ACTUAL);
        AppendSeparator(view);
        AppendCommand(view, kCommandZoomIn, IDS_CMD_ZOOM_IN);
        AppendCommand(view, kCommandZoomOut, IDS_CMD_ZOOM_OUT);
        AppendSeparator(view);
        AppendCommand(view, kCommandSampleRing, IDS_CMD_SAMPLE_RING);
        (void)CheckMenuItem(view, static_cast<UINT>(kCommandSampleRing), MF_BYCOMMAND | MF_CHECKED);
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
        DxUi::ShowNativeHMenuContextMenu(_owner, MenuAnchorScreen(index, screen), menu, _palette);
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
        _menu->SetAccessibleName(LoadStringResource(nullptr, IDS_A11Y_MENU_BAR));
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
        _tabs->SetAccessibleName(LoadStringResource(nullptr, IDS_A11Y_PROJECT_TABS));
        _tabs->AddTab<DxUi::Panel>(LoadStringResource(nullptr, IDS_UNTITLED));
        _rail = root->AddChild<DxUi::Toolbar>();
        _rail->SetAccessibleName(LoadStringResource(nullptr, IDS_A11Y_TOOLS));
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
        BuildSelectionHeader(*root);
        BuildBrushHeader(*root);
        BuildGradientHeader(*root);
        BuildShapeHeader(*root);
        _layersTitle = root->AddChild<DxUi::Label>(LoadStringResource(nullptr, IDS_LAYERS_TITLE));
        _opacity = root->AddChild<DxUi::NumericStepper>();
        _opacity->SetAccessibleName(LoadStringResource(nullptr, IDS_LAYER_OPACITY));
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
        _layers->SetAccessibleName(LoadStringResource(nullptr, IDS_A11Y_LAYERS));
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
        _splitter->SetAccessibleName(LoadStringResource(nullptr, IDS_A11Y_LAYERS_SPLITTER));
        _splitter->SetMinimumFirstPane(kCanvasMinDip);
        _splitter->SetMinimumSecondPane(kLayersMinDip);
        _splitter->SetOnChange([this](DxUi::SplitterChange change) { OnSplitterChange(change); });
        // The adjustment panel and the picker card float over the canvas hole; chrome composites over the canvas
        // with premultiplied alpha. The picker comes last so it opens above the panel.
        BuildAdjustPanel(*root);
        _pickerCard = root->AddChild<DxUi::CardPanel>();
        _pickerCard->SetCornerRadius(8.0f);
        _pickerCard->SetVisible(false);
        _picker = _pickerCard->AddChild<DxUi::ColorPicker>();
        _picker->SetAccessibleName(LoadStringResource(nullptr, IDS_A11Y_COLOR_PICKER));
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
        _picker->RedField().SetAccessibleName(LoadStringResource(nullptr, IDS_ADJUST_RED));
        _picker->GreenField().SetAccessibleName(LoadStringResource(nullptr, IDS_ADJUST_GREEN));
        _picker->BlueField().SetAccessibleName(LoadStringResource(nullptr, IDS_ADJUST_BLUE));
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
    ArrangeSelectionHeader(kRailWidthDip + 8.0f, tabBottom, layersLeft - kSplitterWidthDip, headerBottom);
    ArrangeBrushHeader(kRailWidthDip + 8.0f, tabBottom, layersLeft - kSplitterWidthDip, headerBottom);
    ArrangeGradientHeader(kRailWidthDip + 8.0f, tabBottom, layersLeft - kSplitterWidthDip, headerBottom);
    ArrangeShapeHeader(kRailWidthDip + 8.0f, tabBottom, layersLeft - kSplitterWidthDip, headerBottom);
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
    ArrangeAdjustPanel(layersLeft - kSplitterWidthDip, headerBottom, statusTop);
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
    if (_pickerForAdjust)
    {
        // A Gradient Map end: the panel follows the working color; OK keeps it, Cancel restores it.
        const ChromeEditPhase phase = change.phase == DxUi::ColorPickerChangePhase::Preview  ? ChromeEditPhase::Preview
                                      : change.phase == DxUi::ColorPickerChangePhase::Commit ? ChromeEditPhase::Commit
                                                                                             : ChromeEditPhase::Cancel;
        if (phase != ChromeEditPhase::Preview)
        {
            _pickerForAdjust = false;
            if (_pickerCard)
            {
                _pickerCard->SetVisible(false);
            }
        }
        _view.MarkDirty();
        if (_onAdjustColor)
        {
            _onAdjustColor(phase, change.argb | 0xFF000000u, _pickerHighlights);
        }
        return;
    }
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

bool ChromeHost::IsCanvasPoint(float xPixels, float yPixels) const noexcept
{
    if (!_attached)
    {
        return false;
    }
    const CanvasHole hole = HolePixels();
    if (xPixels < hole.left || yPixels < hole.top || xPixels >= hole.left + hole.width ||
        yPixels >= hole.top + hole.height)
    {
        return false;
    }
    // The picker card and the adjustment panel float over the hole: on them the pointer belongs to the chrome.
    const float scale = _dpi > 0.0f ? _dpi / 96.0f : 1.0f;
    const auto covers = [&](const D2D1_RECT_F& card) noexcept
    {
        return xPixels >= card.left * scale && xPixels < card.right * scale && yPixels >= card.top * scale &&
               yPixels < card.bottom * scale;
    };
    if (ColorPickerOpen() && covers(_pickerCard->GetBounds()))
    {
        return false;
    }
    if (AdjustPanelOpen() && covers(_adjustCard->GetBounds()))
    {
        return false;
    }
    return true;
}

bool ChromeHost::ApplyChromeCursor(float xPixels, float yPixels) noexcept
{
    if (!_attached)
    {
        return false;
    }
    const float scale = _dpi > 0.0f ? _dpi / 96.0f : 1.0f;
    LPCWSTR cursor = IDC_ARROW;
    if (_splitter != nullptr && _splitter->IsVisible())
    {
        // The splitter is the one chrome control with a cursor of its own: its separator plus DxUi's hit slop, the
        // band its own hit test uses for a drag.
        const D2D1_RECT_F bounds = _splitter->GetBounds();
        const float separatorLeft = bounds.left + _splitter->GetPosition() - DxUi::Splitter::kHitSlopDip;
        const float separatorRight =
            bounds.left + _splitter->GetPosition() + _splitter->GetThickness() + DxUi::Splitter::kHitSlopDip;
        const float xDip = xPixels / scale;
        const float yDip = yPixels / scale;
        if (xDip >= separatorLeft && xDip < separatorRight && yDip >= bounds.top && yDip < bounds.bottom)
        {
            cursor = IDC_SIZEWE;
        }
    }
    SetCursor(LoadCursorW(nullptr, cursor));
    return true;
}

POINT ChromeHost::MenuAnchorScreen(size_t index, POINT callbackPoint) const noexcept
{
    POINT anchor = callbackPoint;
    RECT item{};
    if (_attached && _menu != nullptr && _menu->TryGetItemScreenRect(_view.Controls(), index, item))
    {
        anchor.x = item.left;
        anchor.y = item.bottom;
    }
    POINT clientOrigin{};
    if (!_attached || _owner == nullptr || !ClientToScreen(_owner, &clientOrigin))
    {
        return anchor;
    }
    // The embedded view owns no window: DxUi's "screen" points are relative to whatever origin it assumed for the
    // view, so the difference to the owner's client origin puts them on the screen.
    const POINT assumed = _view.Controls().DipPointToScreenPoint(D2D1::Point2F(0.0f, 0.0f));
    anchor.x += clientOrigin.x - assumed.x;
    anchor.y += clientOrigin.y - assumed.y;
    return anchor;
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

void ChromeHost::SetEditTargets(bool masksTargeted, bool transformsSelection)
{
    const auto rename = [](HMENU menu, int command, UINT stringId)
    {
        if (!menu)
        {
            return;
        }
        const std::wstring caption = LoadStringResource(nullptr, stringId);
        (void)ModifyMenuW(menu, static_cast<UINT>(command), MF_BYCOMMAND | MF_STRING, static_cast<UINT_PTR>(command),
                          caption.c_str());
    };
    HMENU edit = _menus[static_cast<std::size_t>(MenuIndex::Edit)].get();
    HMENU layer = _menus[static_cast<std::size_t>(MenuIndex::Layer)].get();
    rename(edit, kCommandInvert, masksTargeted ? IDS_CMD_INVERT_MASK : IDS_CMD_INVERT);
    rename(edit, kCommandFreeTransform, transformsSelection ? IDS_CMD_TRANSFORM_SELECTION : IDS_CMD_FREE_TRANSFORM);
    rename(layer, kCommandDeleteLayer, masksTargeted ? IDS_CMD_DELETE_LAYER_MASK : IDS_CMD_DELETE_LAYER);
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
    if (_selectionHeader)
    {
        _selectionHeader->SetVisible(EditorToolIsSelection(tool));
    }
    if (_brushHeader)
    {
        _brushHeader->SetVisible(EditorToolIsBrush(tool));
    }
    if (_gradientHeader)
    {
        _gradientHeader->SetVisible(tool == EditorTool::Gradient);
    }
    if (_shapeHeader)
    {
        _shapeHeader->SetVisible(tool == EditorTool::Shape);
    }
    if (_header)
    {
        _header->SetVisible(GenericHeaderWanted(tool));
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
    const HRESULT prepared = _view.Prepare(widthPixels, heightPixels, _dpi);
    if (SUCCEEDED(prepared) && _owner != nullptr && !_accessibilitySite)
    {
        // DxUi attaches UI Automation lazily, after preparation, and drops it on hide or a device swap: (re)arm here.
        AttachAccessibility();
    }
    return prepared;
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
    const bool inHole = IsCanvasPoint(event.xPixels, event.yPixels);
    if (inHole && event.action == DxUi::PointerAction::Down)
    {
        _canvasFocused = true;
        UpdateAccessibilityPlacement();
        return false;
    }
    if (!inHole && event.action == DxUi::PointerAction::Down)
    {
        _canvasFocused = false;
        UpdateAccessibilityPlacement();
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
        UpdateAccessibilityPlacement();
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
        control->SetAccessibleName(LoadStringResource(nullptr, labelId));
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
    _sampling->SetAccessibleName(LoadStringResource(nullptr, IDS_A11Y_SAMPLING));
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

void ChromeHost::BuildSelectionHeader(DxUi::Panel& root)
{
    _selectionHeader = root.AddChild<DxUi::Panel>();
    _selectionTitle = _selectionHeader->AddChild<DxUi::Label>(LoadStringResource(nullptr, IDS_TOOL_MARQUEE));
    _selectionTitle->SetFontRole(DxUi::FontRole::BodyStrong);
    const auto combo =
        [this](std::initializer_list<std::pair<const wchar_t*, UINT>> items, UINT tipId, SelectionAction action)
    {
        DxUi::ComboBox* control = _selectionHeader->AddChild<DxUi::ComboBox>();
        std::vector<DxUi::ComboBox::Item> entries;
        for (const auto& [value, id] : items)
        {
            entries.push_back(DxUi::ComboBox::Item{value, LoadStringResource(nullptr, id)});
        }
        control->SetItems(std::move(entries));
        control->SetSelectedIndex(0);
        control->SetTooltipText(LoadStringResource(nullptr, tipId));
        control->SetAccessibleName(LoadStringResource(nullptr, tipId));
        control->SetOnSelectionChanged(
            [this, action](size_t index)
            {
                if (!_syncingSelection && _onSelection)
                {
                    _onSelection(action, static_cast<double>(index));
                }
            });
        return control;
    };
    const auto checkbox = [this](UINT labelId, UINT tipId, bool checked, SelectionAction action)
    {
        DxUi::Checkbox* control = _selectionHeader->AddChild<DxUi::Checkbox>(LoadStringResource(nullptr, labelId));
        control->SetTooltipText(LoadStringResource(nullptr, tipId));
        control->SetAccessibleName(LoadStringResource(nullptr, tipId));
        control->SetChecked(checked);
        control->SetOnToggled(
            [this, action](bool value)
            {
                if (!_syncingSelection && _onSelection)
                {
                    _onSelection(action, value ? 1.0 : 0.0);
                }
            });
        return control;
    };
    const auto stepper = [this](UINT labelId, float labelWidth, UINT unitId, float unitWidth, double minimum,
                                double maximum, double value, SelectionAction action)
    {
        DxUi::NumericStepper* control = _selectionHeader->AddChild<DxUi::NumericStepper>();
        control->SetLabel(LoadStringResource(nullptr, labelId), labelWidth);
        control->SetAccessibleName(LoadStringResource(nullptr, labelId));
        if (unitId != 0)
        {
            control->SetUnit(LoadStringResource(nullptr, unitId), unitWidth);
        }
        control->SetMinimum(minimum);
        control->SetMaximum(maximum);
        control->SetStep(1.0);
        control->SetLargeStep(10.0);
        control->SetDecimals(0);
        control->SetValue(value);
        control->SetOnChange(
            [this, action](DxUi::NumericStepperChange change)
            {
                if (!_syncingSelection && _onSelection)
                {
                    _onSelection(action, change.value);
                }
            });
        return control;
    };
    const auto button = [this](UINT labelId, SelectionAction action)
    {
        DxUi::Button* control = _selectionHeader->AddChild<DxUi::Button>(LoadStringResource(nullptr, labelId));
        control->SetOnClick(
            [this, action]
            {
                if (_onSelection)
                {
                    _onSelection(action, 0.0);
                }
            });
        return control;
    };
    _marqueeShape = combo({{L"rectangle", IDS_SELECTION_RECTANGLE}, {L"ellipse", IDS_SELECTION_ELLIPSE}},
                          IDS_SELECTION_SHAPE_TIP, SelectionAction::SetMarqueeKind);
    _lassoShape = combo({{L"freehand", IDS_SELECTION_FREEHAND}, {L"polygonal", IDS_SELECTION_POLYGONAL}},
                        IDS_SELECTION_LASSO_TIP, SelectionAction::SetLassoKind);
    _selectionMode = combo({{L"new", IDS_SELECTION_MODE_NEW},
                            {L"add", IDS_SELECTION_MODE_ADD},
                            {L"subtract", IDS_SELECTION_MODE_SUBTRACT}},
                           IDS_SELECTION_MODE_TIP, SelectionAction::SetMode);
    _wandTolerance = stepper(IDS_WAND_TOLERANCE, 60.0f, 0, 0.0f, 0.0, 255.0, 32.0, SelectionAction::SetTolerance);
    _wandTolerance->SetTooltipText(LoadStringResource(nullptr, IDS_WAND_TOLERANCE_TIP));
    _wandSampleSize = combo(
        {{L"point", IDS_WAND_POINT_SAMPLE}, {L"three", IDS_WAND_THREE_BY_THREE}, {L"five", IDS_WAND_FIVE_BY_FIVE}},
        IDS_WAND_SAMPLE_SIZE_TIP, SelectionAction::SetSampleSize);
    _wandSample = combo({{L"layer", IDS_WAND_THIS_LAYER}, {L"all", IDS_WAND_ALL_LAYERS}}, IDS_WAND_SAMPLE_TIP,
                        SelectionAction::SetSampleAllLayers);
    _wandContiguous = checkbox(IDS_WAND_CONTIGUOUS, IDS_WAND_CONTIGUOUS_TIP, true, SelectionAction::SetContiguous);
    _selectionAntialias =
        checkbox(IDS_SELECTION_ANTIALIAS, IDS_SELECTION_ANTIALIAS_TIP, true, SelectionAction::SetAntialias);
    _expandButton = button(IDS_SELECTION_EXPAND, SelectionAction::Expand);
    _expandButton->SetTooltipText(
        FormatStringResource(nullptr, IDS_SELECTION_MODIFY_TIP, LoadStringResource(nullptr, IDS_SELECTION_EXPAND)));
    _expandAmount =
        stepper(IDS_SELECTION_EXPAND, 0.0f, IDS_UNIT_PIXELS, 18.0f, 1.0,
                static_cast<double>(DocumentSession::kSelectionModifyLimit), 1.0, SelectionAction::SetExpandAmount);
    _contractButton = button(IDS_SELECTION_CONTRACT, SelectionAction::Contract);
    _contractButton->SetTooltipText(
        FormatStringResource(nullptr, IDS_SELECTION_MODIFY_TIP, LoadStringResource(nullptr, IDS_SELECTION_CONTRACT)));
    _contractAmount =
        stepper(IDS_SELECTION_CONTRACT, 0.0f, IDS_UNIT_PIXELS, 18.0f, 1.0,
                static_cast<double>(DocumentSession::kSelectionModifyLimit), 1.0, SelectionAction::SetContractAmount);
    _selectionEmpty = _selectionHeader->AddChild<DxUi::Label>(LoadStringResource(nullptr, IDS_SELECTION_EMPTY));
    _deselectButton = button(IDS_SELECTION_DESELECT, SelectionAction::Deselect);
    _selectionHeader->SetVisible(EditorToolIsSelection(_tool));
    if (_header)
    {
        _header->SetVisible(_tool != EditorTool::Move && !EditorToolIsSelection(_tool));
    }
}

void ChromeHost::ArrangeSelectionHeader(float left, float top, float right, float bottom) noexcept
{
    if (!_selectionHeader)
    {
        return;
    }
    _selectionHeader->SetBounds(D2D1::RectF(left, top, right, bottom));
    constexpr float kRowDip = 28.0f;
    constexpr float kGapDip = 6.0f;
    const float rowTop = top + (bottom - top - kRowDip) / 2.0f;
    const float rowBottom = rowTop + kRowDip;
    const SelectionInspectorState& state = _selectionInspector;
    // Deselect and the empty note pin to the right; the rest runs left to right and stops when out of room.
    float rightEdge = right - 4.0f;
    struct Pinned
    {
        DxUi::Control* control;
        float width;
        bool wanted;
    };
    const std::array<Pinned, 2> pinned{Pinned{_deselectButton, 80.0f, state.hasSelection},
                                       Pinned{_selectionEmpty, 110.0f, state.hasSelection && state.isEmpty}};
    for (const Pinned& item : pinned)
    {
        if (item.control == nullptr)
        {
            continue;
        }
        const bool fits = item.wanted && rightEdge - item.width > left + 80.0f;
        item.control->SetVisible(fits);
        if (fits)
        {
            item.control->SetBounds(D2D1::RectF(rightEdge - item.width, rowTop, rightEdge, rowBottom));
            rightEdge -= item.width + kGapDip;
        }
    }
    const bool marquee = state.tool == EditorTool::Marquee;
    const bool lasso = state.tool == EditorTool::Lasso;
    const bool wand = state.tool == EditorTool::Wand;
    struct Slot
    {
        DxUi::Control* control;
        float width;
        bool wanted;
    };
    const std::array<Slot, 13> slots{Slot{_selectionTitle, 92.0f, true},
                                     Slot{_marqueeShape, 118.0f, marquee},
                                     Slot{_lassoShape, 118.0f, lasso},
                                     Slot{_selectionMode, 106.0f, true},
                                     Slot{_wandTolerance, 150.0f, wand},
                                     Slot{_wandSampleSize, 150.0f, wand},
                                     Slot{_wandSample, 118.0f, wand},
                                     Slot{_wandContiguous, 104.0f, wand},
                                     Slot{_selectionAntialias, 94.0f, state.antialiasVisible},
                                     Slot{_expandButton, 70.0f, true},
                                     Slot{_expandAmount, 90.0f, true},
                                     Slot{_contractButton, 78.0f, true},
                                     Slot{_contractAmount, 90.0f, true}};
    float cursor = left + 4.0f;
    bool room = true;
    for (const Slot& slot : slots)
    {
        if (slot.control == nullptr)
        {
            continue;
        }
        const bool fits = slot.wanted && room && cursor + slot.width <= rightEdge;
        slot.control->SetVisible(fits);
        if (fits)
        {
            slot.control->SetBounds(D2D1::RectF(cursor, rowTop, cursor + slot.width, rowBottom));
            cursor += slot.width + kGapDip;
        }
        else if (slot.wanted)
        {
            room = false;
        }
    }
}

void ChromeHost::SetSelectionInspector(const SelectionInspectorState& state) noexcept
{
    _selectionInspector = state;
    if (!_selectionHeader)
    {
        return;
    }
    // Values only: the next Prepare arranges the slots the state wants (never SetBounds from a chrome callback).
    _syncingSelection = true;
    _selectionTitle->SetText(LoadStringResource(nullptr, EditorToolNameId(state.tool)));
    _marqueeShape->SetSelectedIndex(static_cast<size_t>(std::clamp(state.marqueeKind, 0, 1)));
    _lassoShape->SetSelectedIndex(static_cast<size_t>(std::clamp(state.lassoKind, 0, 1)));
    _selectionMode->SetSelectedIndex(static_cast<size_t>(std::clamp(state.mode, 0, 2)));
    const auto sync = [](DxUi::NumericStepper* stepper, double value, bool enabled)
    {
        stepper->SetEnabled(enabled);
        if (!stepper->IsEditing())
        {
            stepper->SetValue(value);
        }
    };
    sync(_wandTolerance, state.tolerance, state.enabled);
    _wandSampleSize->SetSelectedIndex(static_cast<size_t>(std::clamp(state.sampleSize, 0, 2)));
    _wandSample->SetSelectedIndex(state.sampleAllLayers ? 1u : 0u);
    _wandContiguous->SetChecked(state.contiguous);
    _selectionAntialias->SetChecked(state.antialiased);
    for (DxUi::Control* control :
         {static_cast<DxUi::Control*>(_marqueeShape), static_cast<DxUi::Control*>(_lassoShape),
          static_cast<DxUi::Control*>(_selectionMode), static_cast<DxUi::Control*>(_wandSampleSize),
          static_cast<DxUi::Control*>(_wandSample), static_cast<DxUi::Control*>(_wandContiguous),
          static_cast<DxUi::Control*>(_selectionAntialias)})
    {
        control->SetEnabled(state.enabled);
    }
    sync(_expandAmount, state.expandAmount, state.canModify);
    sync(_contractAmount, state.contractAmount, state.canModify);
    _expandButton->SetEnabled(state.canModify);
    _contractButton->SetEnabled(state.canModify);
    _deselectButton->SetEnabled(state.enabled && state.hasSelection);
    _syncingSelection = false;
}

void ChromeHost::SetSelectionOutline(std::vector<std::vector<D2D1_POINT_2F>> loops) noexcept
{
    if (!_overlay)
    {
        return;
    }
    const float toDip = _dpi > 0.0f ? 96.0f / _dpi : 1.0f;
    for (std::vector<D2D1_POINT_2F>& loop : loops)
    {
        for (D2D1_POINT_2F& point : loop)
        {
            point.x *= toDip;
            point.y *= toDip;
        }
    }
    _overlay->SetSelectionOutline(std::move(loops));
}

void ChromeHost::SetLassoDraft(std::optional<CanvasOverlay::Draft> draft) noexcept
{
    if (!_overlay)
    {
        return;
    }
    if (draft.has_value())
    {
        const float toDip = _dpi > 0.0f ? 96.0f / _dpi : 1.0f;
        for (D2D1_POINT_2F& point : draft->points)
        {
            point.x *= toDip;
            point.y *= toDip;
        }
        if (draft->cursor.has_value())
        {
            draft->cursor->x *= toDip;
            draft->cursor->y *= toDip;
        }
    }
    _overlay->SetDraft(std::move(draft));
}

void ChromeHost::BuildBrushHeader(DxUi::Panel& root)
{
    _brushHeader = root.AddChild<DxUi::Panel>();
    _brushTitle = _brushHeader->AddChild<DxUi::Label>(LoadStringResource(nullptr, IDS_TOOL_BRUSH));
    _brushTitle->SetFontRole(DxUi::FontRole::BodyStrong);
    const auto combo =
        [this](std::initializer_list<std::pair<const wchar_t*, UINT>> items, UINT tipId, BrushAction action)
    {
        DxUi::ComboBox* control = _brushHeader->AddChild<DxUi::ComboBox>();
        std::vector<DxUi::ComboBox::Item> entries;
        for (const auto& [value, id] : items)
        {
            entries.push_back(DxUi::ComboBox::Item{value, LoadStringResource(nullptr, id)});
        }
        control->SetItems(std::move(entries));
        control->SetSelectedIndex(0);
        control->SetTooltipText(LoadStringResource(nullptr, tipId));
        control->SetAccessibleName(LoadStringResource(nullptr, tipId));
        control->SetOnSelectionChanged(
            [this, action](size_t index)
            {
                if (!_syncingBrush && _onBrush)
                {
                    _onBrush(action, static_cast<double>(index));
                }
            });
        return control;
    };
    const auto stepper = [this](UINT labelId, float labelWidth, UINT unitId, float unitWidth, double minimum,
                                double maximum, double value, UINT tipId, BrushAction action)
    {
        DxUi::NumericStepper* control = _brushHeader->AddChild<DxUi::NumericStepper>();
        control->SetLabel(LoadStringResource(nullptr, labelId), labelWidth);
        control->SetAccessibleName(LoadStringResource(nullptr, labelId));
        control->SetUnit(LoadStringResource(nullptr, unitId), unitWidth);
        control->SetMinimum(minimum);
        control->SetMaximum(maximum);
        control->SetStep(1.0);
        control->SetLargeStep(10.0);
        control->SetDecimals(0);
        control->SetValue(value);
        control->SetTooltipText(LoadStringResource(nullptr, tipId));
        control->SetAccessibleName(LoadStringResource(nullptr, tipId));
        control->SetOnChange(
            [this, action](DxUi::NumericStepperChange change)
            {
                if (!_syncingBrush && _onBrush)
                {
                    _onBrush(action, change.value);
                }
            });
        return control;
    };
    _brushMode = combo({{L"paint", IDS_BRUSH_MODE_PAINT}, {L"erase", IDS_BRUSH_MODE_ERASE}}, IDS_BRUSH_MODE_TIP,
                       BrushAction::SetMode);
    _brushSize = stepper(IDS_BRUSH_SIZE, 34.0f, IDS_UNIT_PIXELS, 18.0f, 1.0, 2000.0, 40.0, IDS_BRUSH_SIZE_TIP,
                         BrushAction::SetSize);
    _brushHardness = stepper(IDS_BRUSH_HARDNESS, 62.0f, IDS_UNIT_PERCENT, 14.0f, 0.0, 100.0, 100.0,
                             IDS_BRUSH_HARDNESS_TIP, BrushAction::SetHardness);
    _brushOpacity = stepper(IDS_BRUSH_OPACITY, 54.0f, IDS_UNIT_PERCENT, 14.0f, 1.0, 100.0, 100.0, IDS_BRUSH_OPACITY_TIP,
                            BrushAction::SetOpacity);
    _brushMaskPaint = combo({{L"black", IDS_BRUSH_MASK_BLACK}, {L"white", IDS_BRUSH_MASK_WHITE}}, IDS_BRUSH_MASK_TIP,
                            BrushAction::SetMaskPaint);
    _brushHealMode = combo({{L"content-aware", IDS_HEAL_CONTENT_AWARE},
                            {L"create-texture", IDS_HEAL_CREATE_TEXTURE},
                            {L"proximity-match", IDS_HEAL_PROXIMITY_MATCH}},
                           IDS_HEAL_TYPE_TIP, BrushAction::SetHealMode);
    _brushCloneAligned = _brushHeader->AddChild<DxUi::Checkbox>(LoadStringResource(nullptr, IDS_CLONE_ALIGNED));
    _brushCloneAligned->SetTooltipText(LoadStringResource(nullptr, IDS_CLONE_ALIGNED_TIP));
    _brushCloneAligned->SetChecked(true);
    _brushCloneAligned->SetOnToggled(
        [this](bool value)
        {
            if (!_syncingBrush && _onBrush)
            {
                _onBrush(BrushAction::SetCloneAligned, value ? 1.0 : 0.0);
            }
        });
    _brushCloneSample = combo({{L"layer", IDS_CLONE_THIS_LAYER}, {L"all", IDS_CLONE_ALL_LAYERS}}, IDS_CLONE_SAMPLE_TIP,
                              BrushAction::SetCloneSample);
    _brushSmearMode = combo({{L"liquify", IDS_SMEAR_LIQUIFY}, {L"blur", IDS_SMEAR_BLUR}, {L"smudge", IDS_SMEAR_SMUDGE}},
                            IDS_SMEAR_MODE_TIP, BrushAction::SetSmearMode);
    _brushSourceNote = _brushHeader->AddChild<DxUi::Label>(LoadStringResource(nullptr, IDS_CLONE_SOURCE_NOTE));
    _brushColorLabel = _brushHeader->AddChild<DxUi::Label>(LoadStringResource(nullptr, IDS_BRUSH_COLOR));
    // The same foreground color and picker as the rail's well.
    _brushColor = _brushHeader->AddChild<DxUi::ColorSwatch>(_foregroundArgb);
    _brushColor->SetAccessibleName(LoadStringResource(nullptr, IDS_PALETTE_FOREGROUND));
    _brushColor->SetTooltipText(LoadStringResource(nullptr, IDS_PALETTE_FOREGROUND));
    _brushColor->SetOnClick(
        [this]
        {
            if (_onBrush)
            {
                _onBrush(BrushAction::OpenColor, 0.0);
            }
        });
    _brushMaskNote = _brushHeader->AddChild<DxUi::Label>(LoadStringResource(nullptr, IDS_BRUSH_MASK_NOTE));
    _brushHeader->SetVisible(EditorToolIsBrush(_tool));
    if (_header)
    {
        _header->SetVisible(GenericHeaderWanted(_tool));
    }
}

void ChromeHost::ArrangeBrushHeader(float left, float top, float right, float bottom) noexcept
{
    if (!_brushHeader)
    {
        return;
    }
    _brushHeader->SetBounds(D2D1::RectF(left, top, right, bottom));
    constexpr float kRowDip = 28.0f;
    constexpr float kGapDip = 6.0f;
    const float rowTop = top + (bottom - top - kRowDip) / 2.0f;
    const float rowBottom = rowTop + kRowDip;
    const BrushInspectorState& state = _brushInspector;
    float rightEdge = right - 4.0f;
    if (_brushMaskNote)
    {
        const bool fits = state.maskSelected && rightEdge - 50.0f > left + 80.0f;
        _brushMaskNote->SetVisible(fits);
        if (fits)
        {
            _brushMaskNote->SetBounds(D2D1::RectF(rightEdge - 50.0f, rowTop, rightEdge, rowBottom));
            rightEdge -= 50.0f + kGapDip;
        }
    }
    if (_brushSourceNote)
    {
        const bool fits = state.tool == EditorTool::Clone && !state.cloneHasSource && rightEdge - 170.0f > left + 80.0f;
        _brushSourceNote->SetVisible(fits);
        if (fits)
        {
            _brushSourceNote->SetBounds(D2D1::RectF(rightEdge - 170.0f, rowTop, rightEdge, rowBottom));
            rightEdge -= 170.0f + kGapDip;
        }
    }
    struct Slot
    {
        DxUi::Control* control;
        float width;
        bool wanted;
    };
    const bool colorWanted = !state.maskSelected && state.tool != EditorTool::Clone && state.tool != EditorTool::Smear;
    const std::array<Slot, 12> slots{Slot{_brushTitle, 92.0f, true},
                                     Slot{_brushMode, 104.0f, state.tool == EditorTool::Brush},
                                     Slot{_brushSmearMode, 112.0f, state.tool == EditorTool::Smear},
                                     Slot{_brushHealMode, 150.0f, state.tool == EditorTool::Heal},
                                     Slot{_brushCloneAligned, 84.0f, state.tool == EditorTool::Clone},
                                     Slot{_brushCloneSample, 118.0f, state.tool == EditorTool::Clone},
                                     Slot{_brushSize, 118.0f, true},
                                     Slot{_brushHardness, 150.0f, true},
                                     Slot{_brushOpacity, 142.0f, true},
                                     Slot{_brushMaskPaint, 150.0f, state.maskSelected},
                                     Slot{_brushColorLabel, 40.0f, colorWanted},
                                     Slot{_brushColor, 34.0f, colorWanted}};
    float cursor = left + 4.0f;
    bool room = true;
    for (const Slot& slot : slots)
    {
        if (slot.control == nullptr)
        {
            continue;
        }
        const bool fits = slot.wanted && room && cursor + slot.width <= rightEdge;
        slot.control->SetVisible(fits);
        if (fits)
        {
            const float inset = slot.control == _brushColor ? 4.0f : 0.0f;
            slot.control->SetBounds(D2D1::RectF(cursor, rowTop + inset, cursor + slot.width, rowBottom - inset));
            cursor += slot.width + kGapDip;
        }
        else if (slot.wanted)
        {
            room = false;
        }
    }
}

void ChromeHost::SetBrushInspector(const BrushInspectorState& state) noexcept
{
    _brushInspector = state;
    if (!_brushHeader)
    {
        return;
    }
    // Values only; the next Prepare arranges the slots the state wants.
    _syncingBrush = true;
    _brushTitle->SetText(LoadStringResource(nullptr, EditorToolNameId(state.tool)));
    _brushMode->SetSelectedIndex(static_cast<size_t>(std::clamp(state.mode, 0, 1)));
    const auto sync = [](DxUi::NumericStepper* stepper, double value, bool enabled)
    {
        stepper->SetEnabled(enabled);
        if (!stepper->IsEditing())
        {
            stepper->SetValue(value);
        }
    };
    sync(_brushSize, state.size, state.enabled);
    sync(_brushHardness, state.hardness, state.enabled);
    sync(_brushOpacity, state.opacity, state.enabled);
    _brushMaskPaint->SetSelectedIndex(state.maskPaintWhite ? 1u : 0u);
    _brushMode->SetEnabled(state.enabled);
    _brushMaskPaint->SetEnabled(state.enabled);
    _brushHealMode->SetSelectedIndex(static_cast<size_t>(std::clamp(state.healMode, 0, 2)));
    _brushHealMode->SetEnabled(state.enabled);
    _brushCloneAligned->SetChecked(state.cloneAligned);
    _brushCloneAligned->SetEnabled(state.enabled);
    _brushCloneSample->SetSelectedIndex(state.cloneAllLayers ? 1u : 0u);
    _brushCloneSample->SetEnabled(state.enabled);
    _brushSmearMode->SetSelectedIndex(static_cast<size_t>(std::clamp(state.smearMode, 0, 2)));
    _brushSmearMode->SetEnabled(state.enabled);
    // Smear calls its opacity Strength.
    _brushOpacity->SetLabel(
        LoadStringResource(nullptr, state.tool == EditorTool::Smear ? IDS_BRUSH_STRENGTH : IDS_BRUSH_OPACITY), 54.0f);
    _brushColor->SetSwatchValue(state.foregroundArgb);
    _brushColor->SetEnabled(state.enabled);
    _syncingBrush = false;
}

void ChromeHost::SetBrushCursor(std::optional<CanvasOverlay::BrushCursor> cursor) noexcept
{
    if (!_overlay)
    {
        return;
    }
    if (cursor.has_value())
    {
        const float toDip = _dpi > 0.0f ? 96.0f / _dpi : 1.0f;
        cursor->center.x *= toDip;
        cursor->center.y *= toDip;
        cursor->radius *= toDip;
    }
    _overlay->SetBrushCursor(cursor);
}

bool ChromeHost::GenericHeaderWanted(EditorTool tool) noexcept
{
    return tool != EditorTool::Move && !EditorToolIsSelection(tool) && !EditorToolIsBrush(tool) &&
           tool != EditorTool::Gradient && tool != EditorTool::Shape;
}

void ChromeHost::SetGradientHandler(std::function<void(GradientAction, double)> handler)
{
    _onGradient = std::move(handler);
}

void ChromeHost::SetShapeHandler(std::function<void(ShapeAction, double)> handler)
{
    _onShape = std::move(handler);
}

void ChromeHost::BuildGradientHeader(DxUi::Panel& root)
{
    _gradientHeader = root.AddChild<DxUi::Panel>();
    _gradientTitle = _gradientHeader->AddChild<DxUi::Label>(LoadStringResource(nullptr, IDS_TOOL_GRADIENT));
    _gradientTitle->SetFontRole(DxUi::FontRole::BodyStrong);
    const auto combo =
        [this](std::initializer_list<std::pair<const wchar_t*, UINT>> items, UINT tipId, GradientAction action)
    {
        DxUi::ComboBox* control = _gradientHeader->AddChild<DxUi::ComboBox>();
        std::vector<DxUi::ComboBox::Item> entries;
        for (const auto& [value, id] : items)
        {
            entries.push_back(DxUi::ComboBox::Item{value, LoadStringResource(nullptr, id)});
        }
        control->SetItems(std::move(entries));
        control->SetSelectedIndex(0);
        control->SetTooltipText(LoadStringResource(nullptr, tipId));
        control->SetAccessibleName(LoadStringResource(nullptr, tipId));
        control->SetOnSelectionChanged(
            [this, action](size_t index)
            {
                if (!_syncingGradient && _onGradient)
                {
                    _onGradient(action, static_cast<double>(index));
                }
            });
        return control;
    };
    _gradientShape = combo({{L"linear", IDS_GRADIENT_LINEAR}, {L"radial", IDS_GRADIENT_RADIAL}}, IDS_GRADIENT_SHAPE_TIP,
                           GradientAction::SetShape);
    // The two end colors as swatches (the transparent end shows the checker); clicking opens the foreground picker.
    for (DxUi::ColorSwatch** swatch : {&_gradientStart, &_gradientEnd})
    {
        *swatch = _gradientHeader->AddChild<DxUi::ColorSwatch>(_foregroundArgb);
        (*swatch)->SetTooltipText(LoadStringResource(nullptr, IDS_GRADIENT_COLORS_TIP));
        (*swatch)->SetAccessibleName(
            LoadStringResource(nullptr, swatch == &_gradientStart ? IDS_A11Y_GRADIENT_START : IDS_A11Y_GRADIENT_END));
        (*swatch)->SetOnClick(
            [this]
            {
                if (_onGradient)
                {
                    _onGradient(GradientAction::OpenColor, 0.0);
                }
            });
    }
    _gradientStyle = combo(
        {{L"foreground-background", IDS_GRADIENT_FG_BG}, {L"foreground-transparent", IDS_GRADIENT_FG_TRANSPARENT}},
        IDS_GRADIENT_COLORS_TIP, GradientAction::SetStyle);
    _gradientReverse = _gradientHeader->AddChild<DxUi::Checkbox>(LoadStringResource(nullptr, IDS_GRADIENT_REVERSE));
    _gradientReverse->SetOnToggled(
        [this](bool value)
        {
            if (!_syncingGradient && _onGradient)
            {
                _onGradient(GradientAction::SetReversed, value ? 1.0 : 0.0);
            }
        });
    _gradientOpacity = _gradientHeader->AddChild<DxUi::NumericStepper>();
    _gradientOpacity->SetLabel(LoadStringResource(nullptr, IDS_BRUSH_OPACITY), 54.0f);
    _gradientOpacity->SetAccessibleName(LoadStringResource(nullptr, IDS_BRUSH_OPACITY));
    _gradientOpacity->SetUnit(LoadStringResource(nullptr, IDS_UNIT_PERCENT), 14.0f);
    _gradientOpacity->SetMinimum(1.0);
    _gradientOpacity->SetMaximum(100.0);
    _gradientOpacity->SetStep(1.0);
    _gradientOpacity->SetLargeStep(10.0);
    _gradientOpacity->SetDecimals(0);
    _gradientOpacity->SetValue(100.0);
    _gradientOpacity->SetTooltipText(LoadStringResource(nullptr, IDS_GRADIENT_OPACITY_TIP));
    _gradientOpacity->SetOnChange(
        [this](DxUi::NumericStepperChange change)
        {
            if (!_syncingGradient && _onGradient)
            {
                _onGradient(GradientAction::SetOpacity, change.value);
            }
        });
    _gradientMaskNote = _gradientHeader->AddChild<DxUi::Label>(LoadStringResource(nullptr, IDS_BRUSH_MASK_NOTE));
    const auto button = [this](UINT labelId, GradientAction action)
    {
        DxUi::Button* control = _gradientHeader->AddChild<DxUi::Button>(LoadStringResource(nullptr, labelId));
        control->SetOnClick(
            [this, action]
            {
                if (_onGradient)
                {
                    _onGradient(action, 0.0);
                }
            });
        return control;
    };
    _gradientCancel = button(IDS_TRANSFORM_CANCEL, GradientAction::Cancel);
    _gradientApply = button(IDS_TRANSFORM_APPLY, GradientAction::Apply);
    _gradientHeader->SetVisible(_tool == EditorTool::Gradient);
}

void ChromeHost::ArrangeGradientHeader(float left, float top, float right, float bottom) noexcept
{
    if (!_gradientHeader)
    {
        return;
    }
    _gradientHeader->SetBounds(D2D1::RectF(left, top, right, bottom));
    constexpr float kRowDip = 28.0f;
    constexpr float kGapDip = 6.0f;
    const float rowTop = top + (bottom - top - kRowDip) / 2.0f;
    const float rowBottom = rowTop + kRowDip;
    const GradientInspectorState& state = _gradientInspector;
    float rightEdge = right - 4.0f;
    struct Pinned
    {
        DxUi::Control* control;
        float width;
        bool wanted;
    };
    const std::array<Pinned, 3> pinned{Pinned{_gradientApply, 72.0f, state.pending},
                                       Pinned{_gradientCancel, 72.0f, state.pending},
                                       Pinned{_gradientMaskNote, 50.0f, state.maskSelected}};
    for (const Pinned& item : pinned)
    {
        if (item.control == nullptr)
        {
            continue;
        }
        const bool fits = item.wanted && rightEdge - item.width > left + 80.0f;
        item.control->SetVisible(fits);
        if (fits)
        {
            item.control->SetBounds(D2D1::RectF(rightEdge - item.width, rowTop, rightEdge, rowBottom));
            rightEdge -= item.width + kGapDip;
        }
    }
    struct Slot
    {
        DxUi::Control* control;
        float width;
        bool inset;
    };
    const std::array<Slot, 7> slots{Slot{_gradientTitle, 78.0f, false},   Slot{_gradientShape, 104.0f, false},
                                    Slot{_gradientStart, 30.0f, true},    Slot{_gradientEnd, 30.0f, true},
                                    Slot{_gradientStyle, 200.0f, false},  Slot{_gradientReverse, 88.0f, false},
                                    Slot{_gradientOpacity, 142.0f, false}};
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
            const float inset = slot.inset ? 4.0f : 0.0f;
            slot.control->SetBounds(D2D1::RectF(cursor, rowTop + inset, cursor + slot.width, rowBottom - inset));
            cursor += slot.width + (slot.control == _gradientStart ? 2.0f : kGapDip);
        }
        else
        {
            room = false;
        }
    }
}

void ChromeHost::SetGradientInspector(const GradientInspectorState& state) noexcept
{
    _gradientInspector = state;
    if (!_gradientHeader)
    {
        return;
    }
    // Values only; the next Prepare arranges the slots the state wants.
    _syncingGradient = true;
    _gradientShape->SetSelectedIndex(static_cast<size_t>(std::clamp(state.shape, 0, 1)));
    _gradientShape->SetEnabled(state.enabled);
    _gradientStyle->SetSelectedIndex(static_cast<size_t>(std::clamp(state.style, 0, 1)));
    _gradientStyle->SetEnabled(state.enabled);
    _gradientReverse->SetChecked(state.reversed);
    _gradientReverse->SetEnabled(state.enabled);
    _gradientOpacity->SetEnabled(state.enabled);
    if (!_gradientOpacity->IsEditing())
    {
        _gradientOpacity->SetValue(state.opacity);
    }
    _gradientStart->SetSwatchValue(state.startArgb);
    _gradientEnd->SetSwatchValue(state.endArgb);
    _gradientStart->SetEnabled(state.enabled);
    _gradientEnd->SetEnabled(state.enabled);
    _gradientCancel->SetEnabled(state.pending);
    _gradientApply->SetEnabled(state.pending);
    _syncingGradient = false;
}

void ChromeHost::BuildShapeHeader(DxUi::Panel& root)
{
    _shapeHeader = root.AddChild<DxUi::Panel>();
    _shapeTitle = _shapeHeader->AddChild<DxUi::Label>(LoadStringResource(nullptr, IDS_TOOL_SHAPE));
    _shapeTitle->SetFontRole(DxUi::FontRole::BodyStrong);
    _shapeKind = _shapeHeader->AddChild<DxUi::ComboBox>();
    _shapeKind->SetItems({DxUi::ComboBox::Item{L"rectangle", LoadStringResource(nullptr, IDS_SHAPE_RECTANGLE)},
                          DxUi::ComboBox::Item{L"ellipse", LoadStringResource(nullptr, IDS_SHAPE_ELLIPSE)}});
    _shapeKind->SetSelectedIndex(0);
    _shapeKind->SetTooltipText(LoadStringResource(nullptr, IDS_SHAPE_KIND_TIP));
    _shapeKind->SetAccessibleName(LoadStringResource(nullptr, IDS_SHAPE_KIND_TIP));
    _shapeKind->SetOnSelectionChanged(
        [this](size_t index)
        {
            if (!_syncingShape && _onShape)
            {
                _onShape(ShapeAction::SetKind, static_cast<double>(index));
            }
        });
    _shapeRadius = _shapeHeader->AddChild<DxUi::NumericStepper>();
    _shapeRadius->SetLabel(LoadStringResource(nullptr, IDS_SHAPE_RADIUS), 50.0f);
    _shapeRadius->SetAccessibleName(LoadStringResource(nullptr, IDS_SHAPE_RADIUS));
    _shapeRadius->SetUnit(LoadStringResource(nullptr, IDS_UNIT_PIXELS), 18.0f);
    _shapeRadius->SetMinimum(0.0);
    _shapeRadius->SetMaximum(5000.0);
    _shapeRadius->SetStep(1.0);
    _shapeRadius->SetLargeStep(10.0);
    _shapeRadius->SetDecimals(0);
    _shapeRadius->SetValue(0.0);
    _shapeRadius->SetTooltipText(LoadStringResource(nullptr, IDS_SHAPE_RADIUS_TIP));
    _shapeRadius->SetOnChange(
        [this](DxUi::NumericStepperChange change)
        {
            if (!_syncingShape && _onShape)
            {
                _onShape(ShapeAction::SetRadius, change.value);
            }
        });
    _shapeFillLabel = _shapeHeader->AddChild<DxUi::Label>(LoadStringResource(nullptr, IDS_SHAPE_FILL));
    _shapeFill = _shapeHeader->AddChild<DxUi::ColorSwatch>(_foregroundArgb);
    _shapeFill->SetAccessibleName(LoadStringResource(nullptr, IDS_PALETTE_FOREGROUND));
    _shapeFill->SetTooltipText(LoadStringResource(nullptr, IDS_SHAPE_FILL_TIP));
    _shapeFill->SetOnClick(
        [this]
        {
            if (_onShape)
            {
                _onShape(ShapeAction::OpenColor, 0.0);
            }
        });
    _shapeHeader->SetVisible(_tool == EditorTool::Shape);
}

void ChromeHost::ArrangeShapeHeader(float left, float top, float right, float bottom) noexcept
{
    if (!_shapeHeader)
    {
        return;
    }
    _shapeHeader->SetBounds(D2D1::RectF(left, top, right, bottom));
    constexpr float kRowDip = 28.0f;
    constexpr float kGapDip = 6.0f;
    const float rowTop = top + (bottom - top - kRowDip) / 2.0f;
    const float rowBottom = rowTop + kRowDip;
    const float rightEdge = right - 4.0f;
    struct Slot
    {
        DxUi::Control* control;
        float width;
        bool wanted;
    };
    const std::array<Slot, 5> slots{Slot{_shapeTitle, 60.0f, true}, Slot{_shapeKind, 118.0f, true},
                                    Slot{_shapeRadius, 140.0f, _shapeInspector.kind == 0},
                                    Slot{_shapeFillLabel, 30.0f, true}, Slot{_shapeFill, 34.0f, true}};
    float cursor = left + 4.0f;
    bool room = true;
    for (const Slot& slot : slots)
    {
        if (slot.control == nullptr)
        {
            continue;
        }
        const bool fits = slot.wanted && room && cursor + slot.width <= rightEdge;
        slot.control->SetVisible(fits);
        if (fits)
        {
            const float inset = slot.control == _shapeFill ? 4.0f : 0.0f;
            slot.control->SetBounds(D2D1::RectF(cursor, rowTop + inset, cursor + slot.width, rowBottom - inset));
            cursor += slot.width + kGapDip;
        }
        else if (slot.wanted)
        {
            room = false;
        }
    }
}

void ChromeHost::SetShapeInspector(const ShapeInspectorState& state) noexcept
{
    _shapeInspector = state;
    if (!_shapeHeader)
    {
        return;
    }
    _syncingShape = true;
    _shapeKind->SetSelectedIndex(static_cast<size_t>(std::clamp(state.kind, 0, 1)));
    _shapeKind->SetEnabled(state.enabled);
    _shapeRadius->SetEnabled(state.enabled);
    if (!_shapeRadius->IsEditing())
    {
        _shapeRadius->SetValue(state.radius);
    }
    _shapeFill->SetSwatchValue(state.foregroundArgb);
    _shapeFill->SetEnabled(state.enabled);
    _syncingShape = false;
}

void ChromeHost::SetGradientLine(std::optional<std::pair<D2D1_POINT_2F, D2D1_POINT_2F>> line) noexcept
{
    if (!_overlay)
    {
        return;
    }
    if (line.has_value())
    {
        const float toDip = _dpi > 0.0f ? 96.0f / _dpi : 1.0f;
        line->first.x *= toDip;
        line->first.y *= toDip;
        line->second.x *= toDip;
        line->second.y *= toDip;
    }
    _overlay->SetGradientLine(line);
}

void ChromeHost::SetShapeDraft(std::optional<CanvasOverlay::ShapeDraftOverlay> draft) noexcept
{
    if (!_overlay)
    {
        return;
    }
    if (draft.has_value())
    {
        const float toDip = _dpi > 0.0f ? 96.0f / _dpi : 1.0f;
        draft->rect.left *= toDip;
        draft->rect.top *= toDip;
        draft->rect.right *= toDip;
        draft->rect.bottom *= toDip;
        draft->cornerRadius *= toDip;
    }
    _overlay->SetShapeDraft(draft);
}

void ChromeHost::SetSampleRing(std::optional<CanvasOverlay::SampleRing> ring) noexcept
{
    if (!_overlay)
    {
        return;
    }
    if (ring.has_value())
    {
        const float toDip = _dpi > 0.0f ? 96.0f / _dpi : 1.0f;
        ring->center.x *= toDip;
        ring->center.y *= toDip;
    }
    _overlay->SetSampleRing(ring);
}

void ChromeHost::SetCloneSourceMark(std::optional<D2D1_POINT_2F> point) noexcept
{
    if (!_overlay)
    {
        return;
    }
    if (point.has_value())
    {
        const float toDip = _dpi > 0.0f ? 96.0f / _dpi : 1.0f;
        point->x *= toDip;
        point->y *= toDip;
    }
    _overlay->SetCloneSource(point);
}

void ChromeHost::SetPickerColor(uint32_t argb) noexcept
{
    if (!_picker || !_pickerCard || !_pickerCard->IsVisible())
    {
        return;
    }
    _picker->SetCurrentColor(argb | 0xFF000000u);
    if (DxUi::ColorSwatch* well = _pickerBackground ? _backgroundWell : _foregroundWell)
    {
        well->SetSwatchValue(argb | 0xFF000000u);
    }
    _view.MarkDirty();
}

uint32_t ChromeHost::PickerColor() const noexcept
{
    return _picker != nullptr ? _picker->GetCurrentColor() : _foregroundArgb;
}

void ChromeHost::SetCommandChecked(int command, bool checked) noexcept
{
    for (const unique_hmenu& menu : _menus)
    {
        if (menu)
        {
            (void)CheckMenuItem(menu.get(), static_cast<UINT>(command),
                                MF_BYCOMMAND | (checked ? MF_CHECKED : MF_UNCHECKED));
        }
    }
}
