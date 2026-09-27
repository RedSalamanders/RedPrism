#include "ChromeHost.h"
#include "LucideToolButton.h"
#include "PaletteControl.h"

#include "Resources.h"
#include "ThemePalette.h"

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <iterator>
#include <new>
#include <string_view>

namespace
{
[[nodiscard]] std::wstring KeyNameFromScan(UINT scanCode, bool extended)
{
    const LPARAM param =
        (static_cast<LPARAM>(scanCode & 0xFFu) << 16) | (extended ? (static_cast<LPARAM>(1) << 24) : 0);
    wchar_t name[64]{};
    const int length = GetKeyNameTextW(static_cast<LONG>(param), name, static_cast<int>(std::size(name)));
    if (length <= 0)
    {
        return {};
    }
    return std::wstring(name, static_cast<std::size_t>(length));
}

// The US legend of a position key, from the table: a key name the layout does not give.
[[nodiscard]] UINT PositionLegendId(KeyPosition position) noexcept
{
    switch (position)
    {
    case KeyPosition::NumberRowMinus:
        return IDS_KEY_NUMBER_ROW_MINUS;
    case KeyPosition::NumberRowPlus:
        return IDS_KEY_NUMBER_ROW_PLUS;
    case KeyPosition::BracketLeft:
        return IDS_KEY_BRACKET_LEFT;
    case KeyPosition::BracketRight:
    case KeyPosition::None:
        break;
    }
    return IDS_KEY_BRACKET_RIGHT;
}

// The name the active keyboard layout gives a key: a letter or digit is that character (a letter chord follows the
// key the layout labels with it); a position key and a named key use GetKeyNameTextW with the set-1 scan code
// (extended for the navigation keys, so Delete is not "Num Del"), which also tells numpad plus and minus from the
// number row. When the layout has no name, the table's stands in: the position key's legend, F13, "Key E7".
[[nodiscard]] std::wstring LayoutKeyName(const KeyChord& chord)
{
    if (chord.position != KeyPosition::None)
    {
        std::wstring name = KeyNameFromScan(KeyPositionScanCode(chord.position), false);
        return name.empty() ? LoadStringResource(nullptr, PositionLegendId(chord.position)) : name;
    }
    const UINT key = chord.virtualKey;
    if ((key >= '0' && key <= '9') || (key >= 'A' && key <= 'Z'))
    {
        return std::wstring(1, static_cast<wchar_t>(key));
    }
    const UINT scanCode = MapVirtualKeyW(key, MAPVK_VK_TO_VSC_EX);
    if ((scanCode & 0xFFu) != 0)
    {
        const bool extended = (scanCode & 0xFF00u) != 0 || IsExtendedNavigationKey(chord.virtualKey);
        std::wstring name = KeyNameFromScan(scanCode & 0xFFu, extended);
        if (!name.empty())
        {
            return name;
        }
    }
    if (key >= VirtualKey::kF1 && key <= VirtualKey::kF24)
    {
        return FormatStringResource(nullptr, IDS_KEY_FUNCTION, key - VirtualKey::kF1 + 1u);
    }
    return FormatStringResource(nullptr, IDS_KEY_VIRTUAL, key);
}

// A menu row with its STRINGTABLE name; ChromeHost::RefreshMenuChords then adds the effective chord.
void AppendCommand(HMENU menu, int command, UINT stringId)
{
    const std::wstring caption{CaptionWithoutChord(LoadStringResource(nullptr, stringId))};
    (void)AppendMenuW(menu, MF_STRING, static_cast<UINT_PTR>(command), caption.c_str());
}

[[nodiscard]] std::wstring MenuCaptionFrom(HMENU menu, int command)
{
    if (menu == nullptr)
    {
        return {};
    }
    wchar_t text[512]{};
    const int length =
        GetMenuStringW(menu, static_cast<UINT>(command), text, static_cast<int>(std::size(text)), MF_BYCOMMAND);
    if (length > 0)
    {
        return std::wstring(text, static_cast<std::size_t>(length));
    }
    const int count = GetMenuItemCount(menu);
    for (int index = 0; index < count; ++index)
    {
        if (HMENU child = GetSubMenu(menu, index))
        {
            std::wstring nested = MenuCaptionFrom(child, command);
            if (!nested.empty())
            {
                return nested;
            }
        }
    }
    return {};
}

void AppendSeparator(HMENU menu) noexcept
{
    (void)AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
}

[[nodiscard]] wchar_t MenuMnemonic(const std::wstring& caption) noexcept
{
    return caption.empty() ? L'\0' : caption.front();
}

[[nodiscard]] ChromeEditPhase EditPhase(DxUi::NumericStepperChangePhase phase) noexcept
{
    return phase == DxUi::NumericStepperChangePhase::Preview  ? ChromeEditPhase::Preview
           : phase == DxUi::NumericStepperChangePhase::Commit ? ChromeEditPhase::Commit
                                                              : ChromeEditPhase::Cancel;
}

[[nodiscard]] ChromeEditPhase EditPhase(DxUi::ColorPickerChangePhase phase) noexcept
{
    return phase == DxUi::ColorPickerChangePhase::Preview  ? ChromeEditPhase::Preview
           : phase == DxUi::ColorPickerChangePhase::Commit ? ChromeEditPhase::Commit
                                                           : ChromeEditPhase::Cancel;
}

[[nodiscard]] bool IsPaletteTarget(PickerTarget target) noexcept
{
    return target == PickerTarget::Foreground || target == PickerTarget::Background;
}

// Whether `control` is `panel` or lies inside it (the logical tree DxUi walks for focus).
[[nodiscard]] bool Contains(const DxUi::Control* panel, const DxUi::Control* control) noexcept
{
    if (panel == nullptr || control == nullptr)
    {
        return false;
    }
    if (panel == control)
    {
        return true;
    }
    for (std::size_t index = 0; index < panel->GetLogicalChildCount(); ++index)
    {
        if (Contains(panel->GetLogicalChild(index), control))
        {
            return true;
        }
    }
    return false;
}
} // namespace

void ChromeHost::LayerTreeModel::SetRows(std::vector<HierarchyEntry> newRows, const std::optional<Guid>& newActive,
                                         bool newMaskTargeted)
{
    rows = std::move(newRows);
    active = newActive;
    maskTargeted = newMaskTargeted;
    if (maskBadge.empty())
    {
        maskBadge = LoadStringResource(nullptr, IDS_LAYER_MASK_BADGE);
        maskOffBadge = LoadStringResource(nullptr, IDS_LAYER_MASK_OFF_BADGE);
    }
    displayText.resize(rows.size());
    badgeText.resize(rows.size());
    badgeTone.resize(rows.size());
    for (std::size_t index = 0; index < rows.size(); ++index)
    {
        const HierarchyEntry& row = rows[index];
        const std::wstring name = row.name.empty() ? row.id.ToString() : row.name;
        displayText[index] = row.visible ? name : FormatStringResource(nullptr, IDS_LAYER_HIDDEN_SUFFIX, name);
        // The mask a row shows, as the legacy mask thumbnail did: the target in the accent, a disabled one crossed out.
        const bool targeted = row.hasMask && maskTargeted && active == std::optional<Guid>(row.id);
        badgeText[index] = !row.hasMask ? nullptr : row.maskEnabled ? &maskBadge : &maskOffBadge;
        badgeTone[index] = targeted           ? DxUi::AdornmentTone::Accent
                           : !row.maskEnabled ? DxUi::AdornmentTone::Error
                                              : DxUi::AdornmentTone::Info;
    }
}

size_t ChromeHost::LayerTreeModel::GetVisibleItemCount() const noexcept
{
    return rows.size();
}

void ChromeHost::LayerTreeModel::GetVisibleItem(size_t visibleIndex, DxUi::TreeItemData& outItem) const
{
    if (visibleIndex >= rows.size())
    {
        outItem = {};
        return;
    }
    // DxUi's Tree::Paint declares one TreeItemData outside its per-row loop and reuses it for every visible row
    // (Tree::Paint, DxUi.Tree.cpp): assigning into each field, rather than resetting the whole struct first,
    // keeps outItem.text's existing heap buffer instead of dropping it every row of every paint.
    const HierarchyEntry& row = rows[visibleIndex];
    outItem.id = visibleIndex + 1;
    outItem.parentId.reset();
    outItem.text = displayText[visibleIndex];
    outItem.iconText = row.isGroup ? std::wstring(1, useFluentIcons ? L'\xE8B7' : L'\u25A1') : std::wstring{};
    if (badgeText[visibleIndex] != nullptr)
    {
        outItem.badgeText = *badgeText[visibleIndex];
    }
    else
    {
        outItem.badgeText.clear();
    }
    outItem.tooltipText.clear();
    outItem.depth = static_cast<uint32_t>(row.depth);
    outItem.hasChildren = row.isGroup;
    outItem.expanded = row.isGroup && !row.collapsed;
    outItem.badgeTone = badgeTone[visibleIndex];
}

void ChromeHost::LayerTreeModel::OnTreeReorder(const DxUi::TreeDrop& drop)
{
    if (drop.sourceId == 0 || drop.targetId == 0 || drop.sourceId > rows.size() || drop.targetId > rows.size())
    {
        return;
    }
    const Guid source = rows[drop.sourceId - 1].id;
    const Guid target = rows[drop.targetId - 1].id;
    std::optional<Guid> parent;
    if (drop.place != DxUi::TreeDropPlace::Inside)
    {
        const int depth = rows[drop.targetId - 1].depth;
        if (depth > 0)
        {
            for (std::size_t index = drop.targetId - 1; index > 0;)
            {
                --index;
                if (rows[index].depth == depth - 1)
                {
                    parent = rows[index].id;
                    break;
                }
            }
        }
    }
    events.OnLayerReordered(source, target, parent, drop.place);
}

std::size_t ChromeHost::ShortcutListModel::GetVisibleItemCount() const noexcept
{
    return rows.size();
}

void ChromeHost::ShortcutListModel::GetVisibleItem(std::size_t visibleIndex, DxUi::TreeItemData& outItem) const
{
    if (visibleIndex >= rows.size())
    {
        outItem = {};
        return;
    }
    // As the layers tree (RF-506): DxUi's Tree::Paint reuses one TreeItemData for every visible row, so assigning
    // each field keeps the item's string buffers instead of dropping and reallocating two strings per row per paint.
    const Row& row = rows[visibleIndex];
    outItem.id = static_cast<uint64_t>(row.command);
    outItem.parentId.reset();
    outItem.text = row.name;
    outItem.iconText.clear();
    outItem.badgeText = row.chord;
    outItem.tooltipText.clear();
    outItem.depth = 0;
    outItem.hasChildren = false;
    outItem.expanded = false;
    outItem.badgeTone = DxUi::AdornmentTone::Accent;
}

std::optional<std::size_t> ChromeHost::ShortcutListModel::FindVisibleItemById(uint64_t itemId) const noexcept
{
    for (std::size_t index = 0; index < rows.size(); ++index)
    {
        if (static_cast<uint64_t>(rows[index].command) == itemId)
        {
            return index;
        }
    }
    return std::nullopt;
}

void ChromeHost::ShortcutListModel::OnTreeItemInvoked(uint64_t itemId)
{
    if (!FindVisibleItemById(itemId).has_value())
    {
        return;
    }
    host.LaunchShortcut(static_cast<int>(itemId));
}

void ChromeHost::LayerTreeModel::OnTreeSelectionChanged(uint64_t itemId)
{
    if (itemId == 0 || itemId > rows.size())
    {
        return;
    }
    events.OnLayerSelected(rows[itemId - 1].id);
}

void ChromeHost::LayerTreeModel::OnTreeToggleExpanded(uint64_t itemId, bool expanded)
{
    if (itemId == 0 || itemId > rows.size())
    {
        return;
    }
    events.OnLayerExpanded(rows[itemId - 1].id, expanded);
}

ChromeHost::ChromeHost(ChromeEvents& events) noexcept : _events(events), _shortcutModel(*this), _treeModel(events) {}

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
    // The rail's cached icon documents belong to the old device: release them while its Direct2D factory is still
    // held (by _graphics and the view), never after (LucideToolButton.h, RF-1110).
    ReleaseLucideIcons();
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
    // Before the view and _graphics let go of the Direct2D factory: the icon caches must not outlive the device
    // they were made on, or the static destructors at exit release their last documents (LucideToolButton.h).
    ReleaseLucideIcons();
    // DxUi::ControlHost::Detach (reached through _view.Detach(), EmbeddedHost::Detach -> _host.Detach()) frees the
    // whole control tree (_root.reset()): every raw pointer this class holds into it would dangle from here on,
    // not read as null, so a null-only check (no `_attached` guard, as most of ChromeHost's own accessors use)
    // would read freed memory. Attach() rebuilds the tree from scratch, reassigning all of them before anything
    // reads one again.
    ResetControlPointers();
    _view.Detach();
    for (unique_hmenu& menu : _menus)
    {
        menu.reset();
    }
    _graphics.reset();
    _requestContext = nullptr;
    _requestFrame = nullptr;
    _cardCount = 0;
    _attached = false;
}

void ChromeHost::ResetControlPointers() noexcept
{
    _cards = {};
    _menu = nullptr;
    _tabs = nullptr;
    _rail = nullptr;
    _moreTools = nullptr;
    _moreCard = nullptr;
    _overflowButtons.fill(nullptr);
    _header = nullptr;
    _layers = nullptr;
    _status = nullptr;
    _jobProgress = nullptr;
    _deferredJobProgress.reset();
    _layersTitle = nullptr;
    _splitter = nullptr;
    _railPalette = nullptr;
    _opacity = nullptr;
    _pickerCard = nullptr;
    _picker = nullptr;
    _noticeCard = nullptr;
    _noticeLabel = nullptr;
    _noticeClose = nullptr;
    _shortcutsCard = nullptr;
    _shortcutsTitle = nullptr;
    _shortcutsSearch = nullptr;
    _shortcutsTree = nullptr;
    _shortcutsClose = nullptr;
    _menuReturnFocus = nullptr;
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
    _selectionHeader = nullptr;
    _selectionTitle = nullptr;
    _marqueeShape = nullptr;
    _lassoShape = nullptr;
    _selectionMode = nullptr;
    _wandTolerance = nullptr;
    _wandSampleSize = nullptr;
    _wandSample = nullptr;
    _wandContiguous = nullptr;
    _selectionAntialias = nullptr;
    _expandButton = nullptr;
    _expandAmount = nullptr;
    _contractButton = nullptr;
    _contractAmount = nullptr;
    _selectionEmpty = nullptr;
    _deselectButton = nullptr;
    _brushHeader = nullptr;
    _brushTitle = nullptr;
    _brushMode = nullptr;
    _brushSize = nullptr;
    _brushHardness = nullptr;
    _brushOpacity = nullptr;
    _brushMaskPaint = nullptr;
    _brushColorLabel = nullptr;
    _brushColor = nullptr;
    _brushMaskNote = nullptr;
    _brushHealMode = nullptr;
    _brushCloneAligned = nullptr;
    _brushCloneSample = nullptr;
    _brushSmearMode = nullptr;
    _brushSourceNote = nullptr;
    _gradientHeader = nullptr;
    _gradientTitle = nullptr;
    _gradientShape = nullptr;
    _gradientStart = nullptr;
    _gradientEnd = nullptr;
    _gradientStyle = nullptr;
    _gradientReverse = nullptr;
    _gradientOpacity = nullptr;
    _gradientMaskNote = nullptr;
    _gradientCancel = nullptr;
    _gradientApply = nullptr;
    _shapeHeader = nullptr;
    _cropHeader = nullptr;
    _cropTitle = nullptr;
    _cropRatioLabel = nullptr;
    _cropRatio = nullptr;
    _cropSize = nullptr;
    _cropCancel = nullptr;
    _cropApply = nullptr;
    _shapeTitle = nullptr;
    _shapeKind = nullptr;
    _shapeRadius = nullptr;
    _shapeFillLabel = nullptr;
    _shapeFill = nullptr;
    _toolButtons.fill(nullptr);
    _renameCard = nullptr;
    _renameTitle = nullptr;
    _renameField = nullptr;
    _renameCancel = nullptr;
    _renameOk = nullptr;
    _adjustCard = nullptr;
    _adjustTitle = nullptr;
    _adjustChannel = nullptr;
    _curveEditor = nullptr;
    _curveCaption = nullptr;
    _curveRemove = nullptr;
    _curveReset = nullptr;
    _levelsInput = nullptr;
    _levelsOutput = nullptr;
    _levelsBlack = nullptr;
    _levelsGamma = nullptr;
    _levelsWhite = nullptr;
    _levelsOutBlack = nullptr;
    _levelsOutWhite = nullptr;
    _levelsSample.fill(nullptr);
    _levelsAuto.fill(nullptr);
    _hueRange = nullptr;
    _hueSample.fill(nullptr);
    _hueTargeting = nullptr;
    _spectrum = nullptr;
    _hueInvert = nullptr;
    _hueColorize = nullptr;
    _gradientRamp = nullptr;
    _mapShadows = nullptr;
    _mapShadowsLabel = nullptr;
    _mapHighlights = nullptr;
    _mapHighlightsLabel = nullptr;
    _mapReverse = nullptr;
    _noiseDistribution = nullptr;
    _noiseMono = nullptr;
    _lensHint = nullptr;
    _adjustRows = {};
    _adjustPreview = nullptr;
    _fillHint = nullptr;
    _fillNoSource = nullptr;
    _adjustNote = nullptr;
    _adjustCancel = nullptr;
    _adjustApply = nullptr;
    _sizeCard = nullptr;
    _sizeTitle = nullptr;
    _sizeCurrent = nullptr;
    _sizeBytes = nullptr;
    _sizeUnit = nullptr;
    _sizeWidth = nullptr;
    _sizeHeight = nullptr;
    _sizeRelative = nullptr;
    _sizeLocked = nullptr;
    _sizeResult = nullptr;
    _sizeAnchorLabel = nullptr;
    _sizeAnchors.fill(nullptr);
    _sizeAnchorName = nullptr;
    _sizeAnchorHint = nullptr;
    _sizeExtension = nullptr;
    _sizeExtensionSwatch = nullptr;
    _sizeResolution = nullptr;
    _sizePpi = nullptr;
    _sizeResample = nullptr;
    _sizeSampling = nullptr;
    _sizeNote = nullptr;
    _sizeOpen = nullptr;
    _sizeImport = nullptr;
    _sizeCancel = nullptr;
    _sizeApply = nullptr;
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

void ChromeHost::BuildMenus() noexcept
{
    // A new tree: nothing points into the previous one.
    _menuKeyboard = false;
    _menuReturnFocus = nullptr;
    _recordedMenu.reset();
    for (unique_hmenu& menu : _menus)
    {
        menu.reset(CreatePopupMenu());
    }
    // Rows carry their names here; RefreshDisplayedChords adds each effective chord through CommandMenuCaption.
    if (HMENU file = _menus[static_cast<std::size_t>(MenuIndex::File)].get())
    {
        AppendCommand(file, kCommandNew, IDS_CMD_NEW);
        AppendCommand(file, kCommandNewCanvas, IDS_CMD_NEW_CANVAS);
        AppendCommand(file, kCommandOpen, IDS_CMD_OPEN);
        AppendCommand(file, kCommandImportImages, IDS_CMD_IMPORT_IMAGES);
        AppendSeparator(file);
        AppendCommand(file, kCommandSave, IDS_CMD_SAVE);
        AppendCommand(file, kCommandSaveAs, IDS_CMD_SAVE_AS);
        AppendSeparator(file);
        AppendCommand(file, kCommandExportPng, IDS_CMD_EXPORT_PNG);
        AppendCommand(file, kCommandExportJpeg, IDS_CMD_EXPORT_JPEG);
        AppendSeparator(file);
        AppendCommand(file, kCommandCloseProject, IDS_CMD_CLOSE_PROJECT);
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
        AppendCommand(image, kCommandCanvasSize, IDS_CMD_CANVAS_SIZE);
        AppendCommand(image, kCommandImageSize, IDS_CMD_IMAGE_SIZE);
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
        AppendCommand(layer, kCommandGroupLayers, IDS_CMD_GROUP_LAYERS);
        AppendCommand(layer, kCommandMoveOutOfGroup, IDS_CMD_MOVE_OUT_OF_GROUP);
        AppendCommand(layer, kCommandRenameLayer, IDS_CMD_RENAME_LAYER);
        AppendCommand(layer, kCommandDeleteLayer, IDS_CMD_DELETE_LAYER);
        AppendCommand(layer, kCommandToggleLayerVisibility, IDS_CMD_TOGGLE_LAYER_VISIBILITY);
        AppendCommand(layer, kCommandMoveLayerUp, IDS_CMD_MOVE_LAYER_UP);
        AppendCommand(layer, kCommandMoveLayerDown, IDS_CMD_MOVE_LAYER_DOWN);
        AppendSeparator(layer);
        AppendCommand(layer, kCommandClipToggle, IDS_CMD_CLIP_CREATE);
        AppendCommand(layer, kCommandMergeDown, IDS_CMD_MERGE_DOWN);
        AppendSeparator(layer);
        AppendCommand(layer, kCommandAddMask, IDS_CMD_ADD_MASK);
        AppendCommand(layer, kCommandAddBlackMask, IDS_CMD_ADD_BLACK_MASK);
        AppendCommand(layer, kCommandTargetLayerMask, IDS_CMD_TARGET_MASK);
        AppendCommand(layer, kCommandToggleMaskLink, IDS_CMD_UNLINK_MASK);
        AppendCommand(layer, kCommandToggleLayerMask, IDS_CMD_DISABLE_MASK);
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
        AppendCommand(help, kCommandKeyboardShortcuts, IDS_CMD_KEYBOARD_SHORTCUTS);
        AppendCommand(help, kCommandAbout, IDS_CMD_ABOUT);
    }
}

void ChromeHost::OpenMenu(size_t index, POINT screen, bool keyboard) noexcept
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
    if (_menusRecordOnly)
    {
        // Quiet runs: a native flyout runs a modal loop nothing would close.
        _recordedMenu = index;
        if (keyboard)
        {
            LeaveMenuKeyboard();
        }
        return;
    }
    const std::optional<int> command =
        DxUi::ShowNativeHMenuContextMenu(_owner, MenuAnchorScreen(index, screen), menu, _palette);
    if (keyboard)
    {
        // A menu opened from the keyboard hands the focus back before its command runs (a command that opens a card
        // then takes it from there).
        LeaveMenuKeyboard();
    }
    if (command.has_value())
    {
        RunCommand(command.value());
    }
}

HRESULT ChromeHost::Build() noexcept
{
    // New controls show nothing of the Application's yet: every region writes on its first push.
    _resync = kResyncAll;
    _menuTargets = {};
    _documentTitle.clear();
    _transformInspector = {};
    _cropInspector = {};
    ++_displayRevision;
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
            DxUi::MenuBarItem item;
            item.text = std::move(caption);
            item.mnemonic = MenuMnemonic(item.text);
            item.rightJustified = id == IDS_MENU_HELP;
            items.push_back(std::move(item));
        }
        _menu->SetItems(std::move(items));
        _menu->SetOnOpenItem([this](size_t index, POINT screen, bool keyboard) { OpenMenu(index, screen, keyboard); });
        _tabs = root->AddChild<DxUi::TabControl>();
        _tabs->SetAccessibleName(LoadStringResource(nullptr, IDS_A11Y_PROJECT_TABS));
        _tabs->AddTab<DxUi::Panel>(LoadStringResource(nullptr, IDS_UNTITLED));
        _rail = root->AddChild<DxUi::Toolbar>();
        _rail->SetAccessibleName(LoadStringResource(nullptr, IDS_A11Y_TOOLS));
        const bool fluentIcons = _view.Controls().HasFluentIconFont();
        _treeModel.useFluentIcons = fluentIcons;
        for (std::size_t index = 0; index < kEditorToolCount; ++index)
        {
            const EditorTool tool = EditorToolAt(index);
            const std::wstring name = LoadStringResource(nullptr, EditorToolNameId(tool));
            const std::wstring tooltip = WithChordHint(name, {SelectToolCommand(tool)});
            // An icon button whose pressed look, held as a latch, marks the active tool (DxUi paints a Toggle as a
            // switch, glyph or not). The latch is written only between presses: SyncToolVisuals defers while a
            // press is in flight, because a pressed-look write reaching the tree cancels a press held across a
            // frame.
            DxUi::Button* button = _rail->AddChild<LucideToolButton>(tooltip, tool, fluentIcons);
            button->SetAccessibleName(name);
            button->SetOnClick([this, tool] { ChooseTool(tool); });
            _toolButtons[index] = button;
        }
        const std::wstring moreName = LoadStringResource(nullptr, IDS_MORE_TOOLS);
        _moreTools = _rail->AddChild<LucideIconButton>(moreName, IDR_LUCIDE_ELLIPSIS_VERTICAL);
        _moreTools->SetAccessibleName(moreName);
        _moreTools->SetVisible(false);
        _moreTools->SetOnClick(
            [this]
            {
                if (_moreToolsOpen)
                {
                    CloseMoreTools();
                }
                else
                {
                    OpenMoreTools();
                }
            });
        _moreCard = root->AddChild<DxUi::CardPanel>();
        _moreCard->SetCornerRadius(8.0f);
        _moreCard->SetVisible(false);
        _moreCard->SetAccessibleName(moreName);
        for (std::size_t index = 0; index < kEditorToolCount; ++index)
        {
            const EditorTool tool = EditorToolAt(index);
            const std::wstring name = LoadStringResource(nullptr, EditorToolNameId(tool));
            const std::wstring tooltip = WithChordHint(name, {SelectToolCommand(tool)});
            DxUi::Button* overflow = _moreCard->AddChild<LucideToolButton>(tooltip, tool, fluentIcons);
            overflow->SetAccessibleName(name);
            overflow->SetVisible(false);
            overflow->SetOnClick([this, tool] { ChooseTool(tool); });
            _overflowButtons[index] = overflow;
        }
        // One palette control: overlapping wells, a swap hot zone, and a black-and-white reset hot zone.
        _railPalette = _rail->AddChild<PaletteControl>(
            LoadStringResource(nullptr, IDS_PALETTE_FOREGROUND), LoadStringResource(nullptr, IDS_PALETTE_BACKGROUND),
            LoadStringResource(nullptr, IDS_PALETTE_SWAP), LoadStringResource(nullptr, IDS_PALETTE_RESET));
        _railPalette->SetHandlers([this] { OpenColorPicker(PickerTarget::Foreground, _foregroundArgb); },
                                  [this] { OpenColorPicker(PickerTarget::Background, _backgroundArgb); }, [this]
                                  { RunCommand(kCommandSwapPalette); }, [this] { RunCommand(kCommandResetPalette); });
        _railPalette->SetForeground(_foregroundArgb);
        _railPalette->SetBackground(_backgroundArgb);
        _header = root->AddChild<DxUi::Label>(LoadStringResource(nullptr, EditorToolNameId(_tool)));
        BuildTransformHeader(*root);
        BuildSelectionHeader(*root);
        BuildBrushHeader(*root);
        BuildGradientHeader(*root);
        BuildShapeHeader(*root);
        BuildCropHeader(*root);
        _header->SetVisible(GenericHeaderWanted(_tool));
        _layersTitle = root->AddChild<DxUi::Label>(LoadStringResource(nullptr, IDS_LAYERS_TITLE));
        _opacity = MakeStepper(*root,
                               {.labelId = IDS_LAYER_OPACITY,
                                .unitId = IDS_UNIT_PERCENT,
                                .unitWidth = 14.0f,
                                .maximum = 100.0,
                                .value = 100.0},
                               [this](DxUi::NumericStepperChange change)
                               { _events.OnLayerOpacity(change.value / 100.0, EditPhase(change.phase)); });
        _opacity->SetEnabled(false);
        _layers = root->AddChild<DxUi::Tree>();
        _layers->SetAccessibleName(LoadStringResource(nullptr, IDS_A11Y_LAYERS));
        _layers->SetModel(&_treeModel);
        _layers->SetDelegate(&_treeModel);
        _layers->SetReorderEnabled(true);
        _status = root->AddChild<DxUi::StatusStrip>();
        _status->SetSections({DxUi::StatusStrip::Section{std::wstring{}, kStatusZoomSectionWidthDip},
                              DxUi::StatusStrip::Section{std::wstring{}, kStatusDocumentSectionWidthDip},
                              DxUi::StatusStrip::Section{std::wstring{}, 0.0f},
                              DxUi::StatusStrip::Section{std::wstring{}, kJobProgressSectionWidthDip}});
        // StatusStrip has text sections only. This sibling occupies the reserved fourth section and paints above it.
        _jobProgress = root->AddChild<DxUi::ProgressBar>();
        _jobProgress->SetAccessibleName(LoadStringResource(nullptr, IDS_A11Y_JOB_PROGRESS));
        _jobProgress->SetTrackHeightDip(kJobProgressTrackHeightDip);
        _jobProgress->SetVisible(false);
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
        BuildSizeCard(*root);
        BuildRenameCard(*root);
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
        _noticeCard = root->AddChild<DxUi::CardPanel>();
        _noticeCard->SetCornerRadius(8.0f);
        _noticeCard->SetVisible(false);
        _noticeLabel = _noticeCard->AddChild<DxUi::Label>(std::wstring{});
        _noticeLabel->SetFontRole(DxUi::FontRole::Body);
        _noticeClose = MakeButton(*_noticeCard, {.labelId = IDS_PICKER_OK, .primary = true}, [this] { CloseNotice(); });
        _shortcutsCard = root->AddChild<DxUi::CardPanel>();
        _shortcutsCard->SetCornerRadius(8.0f);
        _shortcutsCard->SetVisible(false);
        _shortcutsTitle = _shortcutsCard->AddChild<DxUi::Label>(LoadStringResource(nullptr, IDS_SHORTCUTS_TITLE));
        _shortcutsTitle->SetFontRole(DxUi::FontRole::BodyStrong);
        _shortcutsSearch = _shortcutsCard->AddChild<DxUi::TextField>();
        _shortcutsSearch->SetAccessibleName(LoadStringResource(nullptr, IDS_SHORTCUTS_SEARCH));
        _shortcutsSearch->SetPlaceholder(LoadStringResource(nullptr, IDS_SHORTCUTS_SEARCH));
        _shortcutsSearch->SetOnTextChanged(
            [this](std::wstring_view text)
            {
                _shortcutModel.query = text;
                ApplyShortcutFilter();
            });
        // Enter in the field runs the selected (or first) row; Up and Down move the selection from the field.
        _shortcutsSearch->SetOnSubmitted([this] { RunShortcutFromSearch(); });
        _shortcutsSearch->SetOnPreviewKeyDown(
            [this](DxUi::ControlHost&, UINT virtualKey, UINT modifiers)
            {
                if ((modifiers & (MK_CONTROL | MK_SHIFT)) != 0 || (virtualKey != VK_DOWN && virtualKey != VK_UP))
                {
                    return false;
                }
                return StepShortcutSelection(virtualKey == VK_DOWN ? 1 : -1);
            });
        _shortcutsTree = _shortcutsCard->AddChild<DxUi::Tree>();
        _shortcutsTree->SetAccessibleName(LoadStringResource(nullptr, IDS_SHORTCUTS_TITLE));
        _shortcutsTree->SetModel(&_shortcutModel);
        _shortcutsTree->SetDelegate(&_shortcutModel);
        _shortcutsClose =
            MakeButton(*_shortcutsCard, {.labelId = IDS_PICKER_OK, .primary = true}, [this] { CloseShortcuts(); });
        _view.Controls().SetRoot(std::move(root));
        RefreshDisplayedChords();
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
    // The glyph stays centered in the 48 DIP rail. The hit and hover fill that width, so the padding to the
    // left of the icon is the same button.
    return D2D1::RectF(0.0f, top, kRailWidthDip, top + kToolButtonDip);
}

int ChromeHost::VisibleToolButtonCount() const noexcept
{
    int count = 0;
    for (DxUi::Button* button : _toolButtons)
    {
        if (button != nullptr && button->IsVisible())
        {
            ++count;
        }
    }
    return count;
}

int ChromeHost::RailSlotsThatFit(float wellTop) const noexcept
{
    int fit = 0;
    const float limit = wellTop - kToolGapDip;
    for (std::size_t index = 0; index < kEditorToolCount; ++index)
    {
        if (ToolButtonBoundsDip(EditorToolAt(index)).bottom <= limit)
        {
            ++fit;
        }
        else
        {
            break;
        }
    }
    return fit;
}

bool ChromeHost::MoreToolsOpen() const noexcept
{
    return _moreToolsOpen && _moreCard != nullptr && _moreCard->IsVisible();
}

D2D1_RECT_F ChromeHost::MoreToolsBoundsDip() const noexcept
{
    return _moreTools != nullptr && _moreTools->IsVisible() ? _moreTools->GetBounds() : D2D1::RectF();
}

D2D1_RECT_F ChromeHost::OverflowToolBoundsDip(EditorTool tool) const noexcept
{
    const std::size_t index = EditorToolIndex(tool);
    DxUi::Button* button = index < kEditorToolCount ? _overflowButtons[index] : nullptr;
    if (button == nullptr || !MoreToolsOpen() || !button->IsVisible())
    {
        return D2D1::RectF();
    }
    return button->GetBounds();
}

void ChromeHost::ChooseTool(EditorTool tool) noexcept
{
    CloseMoreTools();
    RunCommand(SelectToolCommand(tool));
}

void ChromeHost::OpenMoreTools() noexcept
{
    if (_moreCard == nullptr || _moreToolStart >= static_cast<int>(kEditorToolCount))
    {
        return;
    }
    _moreToolsOpen = true;
    _accessibleStructureChanged = true;
    ArrangeMoreTools();
    _view.MarkDirty();
    if (_requestFrame != nullptr)
    {
        _requestFrame(_requestContext);
    }
    if (_pointerPressed)
    {
        _toolVisualsDirty = true;
    }
    else
    {
        SyncToolVisuals();
    }
}

void ChromeHost::CloseMoreTools() noexcept
{
    if (!_moreToolsOpen && (_moreCard == nullptr || !_moreCard->IsVisible()))
    {
        return;
    }
    _moreToolsOpen = false;
    _accessibleStructureChanged = true;
    ArrangeMoreTools();
    _view.MarkDirty();
    if (_requestFrame != nullptr)
    {
        _requestFrame(_requestContext);
    }
    if (_pointerPressed)
    {
        _toolVisualsDirty = true;
    }
    else
    {
        SyncToolVisuals();
    }
}

void ChromeHost::ArrangeMoreTools() noexcept
{
    if (_moreCard == nullptr)
    {
        return;
    }
    const bool show = _moreToolsOpen && _moreToolStart < static_cast<int>(kEditorToolCount);
    if (!show)
    {
        ShowControl(_moreCard, false);
        for (DxUi::Button* button : _overflowButtons)
        {
            ShowControl(button, false);
        }
        return;
    }
    const int count = static_cast<int>(kEditorToolCount) - _moreToolStart;
    constexpr float pad = 4.0f;
    const float width = kRailWidthDip + pad * 2.0f;
    const float height = pad * 2.0f + static_cast<float>(count) * kToolButtonDip +
                         static_cast<float>(std::max(0, count - 1)) * kToolGapDip;
    const float minTop = kMenuHeightDip + kTabHeightDip + pad;
    const float maxBottom = HeightDip() - kStatusHeightDip - pad;
    float top = _moreTools != nullptr ? _moreTools->GetBounds().top : minTop;
    if (top + height > maxBottom)
    {
        top = std::max(minTop, maxBottom - height);
    }
    if (top < minTop)
    {
        top = minTop;
    }
    const float left = kRailWidthDip + pad;
    PlaceControl(_moreCard, D2D1::RectF(left, top, left + width, top + height));
    ShowControl(_moreCard, true);
    float y = top + pad;
    for (std::size_t index = 0; index < kEditorToolCount; ++index)
    {
        DxUi::Button* button = _overflowButtons[index];
        const bool listed = static_cast<int>(index) >= _moreToolStart;
        if (listed && button != nullptr)
        {
            PlaceControl(button, D2D1::RectF(left + pad, y, left + pad + kRailWidthDip, y + kToolButtonDip));
            y += kToolButtonDip + kToolGapDip;
        }
        ShowControl(button, listed);
    }
}

void ChromeHost::PlaceControl(DxUi::Control* control, const D2D1_RECT_F& bounds) noexcept
{
    if (control == nullptr)
    {
        return;
    }
    const D2D1_RECT_F current = control->GetBounds();
    if (current.left != bounds.left || current.top != bounds.top || current.right != bounds.right ||
        current.bottom != bounds.bottom)
    {
        control->SetBounds(bounds);
    }
}

void ChromeHost::SetPressedVisualIfChanged(DxUi::Button* button, bool& current, bool pressed) noexcept
{
    if (button == nullptr)
    {
        return;
    }
    // DxUi clears the button's own latch on mouse-up and mouse-leave (Button::OnMouseUp, Button::OnMouseLeave),
    // so the cached `current` bool can still say "pressed" after the control itself reverted (the pointer moved
    // on to the canvas while the eyedropper stayed armed, or a re-clicked anchor lost its selection look).
    // Compare the button's real state, not just the last value this function wrote.
    if (button->DebugIsPressed() != pressed)
    {
        button->SetPressedVisual(pressed);
    }
    current = pressed;
}

void ChromeHost::ShowControl(DxUi::Control* control, bool visible) noexcept
{
    if (control != nullptr && control->IsVisible() != visible)
    {
        control->SetVisible(visible);
        _accessibleStructureChanged = true;
    }
}

void ChromeHost::ArrangeHeaderRow(DxUi::Panel* header, const D2D1_RECT_F& bounds, std::span<const HeaderSlot> pinned,
                                  std::span<const HeaderSlot> flow, bool keepTitleRoom) noexcept
{
    if (header == nullptr)
    {
        return;
    }
    PlaceControl(header, bounds);
    constexpr float kRowDip = 28.0f;
    constexpr float kTitleRoomDip = 80.0f;
    const float rowTop = bounds.top + (bounds.bottom - bounds.top - kRowDip) / 2.0f;
    const float rowBottom = rowTop + kRowDip;
    float rightEdge = bounds.right - 4.0f;
    for (const HeaderSlot& slot : pinned)
    {
        if (slot.control == nullptr)
        {
            continue;
        }
        const bool fits = slot.wanted && (!keepTitleRoom || rightEdge - slot.width > bounds.left + kTitleRoomDip);
        ShowControl(slot.control, fits);
        if (fits)
        {
            PlaceControl(slot.control,
                         D2D1::RectF(rightEdge - slot.width, rowTop + slot.inset, rightEdge, rowBottom - slot.inset));
            rightEdge -= slot.width + slot.gap;
        }
    }
    float cursor = bounds.left + 4.0f;
    bool room = true;
    for (const HeaderSlot& slot : flow)
    {
        if (slot.control == nullptr)
        {
            continue;
        }
        const bool fits = slot.wanted && room && cursor + slot.width <= rightEdge;
        ShowControl(slot.control, fits);
        if (fits)
        {
            PlaceControl(slot.control,
                         D2D1::RectF(cursor, rowTop + slot.inset, cursor + slot.width, rowBottom - slot.inset));
            cursor += slot.width + slot.gap;
        }
        else if (slot.wanted)
        {
            room = false;
        }
    }
}

std::vector<DxUi::ComboBox::Item> ChromeHost::NamedItems(std::initializer_list<std::pair<const wchar_t*, UINT>> items)
{
    std::vector<DxUi::ComboBox::Item> entries;
    entries.reserve(items.size());
    for (const auto& [value, id] : items)
    {
        entries.push_back(DxUi::ComboBox::Item{value, LoadStringResource(nullptr, id)});
    }
    return entries;
}

std::vector<DxUi::ComboBox::Item> ChromeHost::NumberedItems(std::span<const UINT> ids, std::size_t first)
{
    std::vector<DxUi::ComboBox::Item> entries;
    entries.reserve(ids.size());
    for (const UINT id : ids)
    {
        entries.push_back(DxUi::ComboBox::Item{std::to_wstring(first++), LoadStringResource(nullptr, id)});
    }
    return entries;
}

struct PaletteLayout
{
    D2D1_RECT_F block{};
    D2D1_RECT_F foreground{};
    D2D1_RECT_F background{};
    D2D1_RECT_F swap{};
    D2D1_RECT_F reset{};
};

[[nodiscard]] PaletteLayout PaletteLayoutAt(float blockTop) noexcept
{
    PaletteLayout layout;
    layout.block = D2D1::RectF(0.0f, blockTop, ChromeHost::kRailWidthDip, blockTop + ChromeHost::kWellBlockDip);
    // 20 DIP squares overlap by 6, so the foreground sits in front and most of the background still shows.
    // The step leaves a 14 DIP dark notch at each corner. Swap fills the top-right notch. Reset fills the
    // bottom-left one. Neither covers a swatch, so the light glyph stays visible.
    constexpr float swatch = 20.0f;
    constexpr float overlap = 6.0f;
    constexpr float step = swatch - overlap;
    const float left = 7.0f;
    const float foregroundTop = blockTop + 7.0f;
    layout.foreground = D2D1::RectF(left, foregroundTop, left + swatch, foregroundTop + swatch);
    layout.background =
        D2D1::RectF(left + step, foregroundTop + step, left + step + swatch, foregroundTop + step + swatch);
    layout.swap =
        D2D1::RectF(layout.foreground.right, layout.foreground.top, layout.background.right, layout.background.top);
    layout.reset =
        D2D1::RectF(layout.foreground.left, layout.foreground.bottom, layout.background.left, layout.background.bottom);
    return layout;
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
        PlaceControl(root, D2D1::RectF(0.0f, 0.0f, widthDip, heightDip));
    }
    const float menuBottom = kMenuHeightDip;
    const float tabBottom = menuBottom + kTabHeightDip;
    const float headerBottom = tabBottom + kHeaderHeightDip;
    const float statusTop = heightDip - kStatusHeightDip;
    const float extent = std::max(0.0f, widthDip - kRailWidthDip);
    if (_splitter)
    {
        // The splitter owns the clamp: canvas minimum, layers minimum and maximum, and the persisted preference.
        PlaceControl(_splitter, D2D1::RectF(kRailWidthDip, tabBottom, widthDip, statusTop));
        // Unlike SetPosition, DxUi's minimum setters RequestInvalidate unconditionally, even when the value does
        // not change (Splitter::SetMinimumFirstPane/SetMinimumSecondPane); written every Prepare, that keeps
        // EmbeddedHost from ever taking its settled-tree early-out. Write only on a real change.
        const float minimumFirst = std::max(kCanvasMinDip, extent - kLayersMaxDip - kSplitterWidthDip);
        if (_splitter->GetMinimumFirstPane() != minimumFirst)
        {
            _splitter->SetMinimumFirstPane(minimumFirst);
        }
        if (_splitter->GetMinimumSecondPane() != kLayersMinDip)
        {
            _splitter->SetMinimumSecondPane(kLayersMinDip);
        }
        _splitter->SetPosition(extent - _preferredLayersWidthDip - kSplitterWidthDip);
        _layersWidthDip = std::max(0.0f, extent - _splitter->GetPosition() - kSplitterWidthDip);
    }
    else
    {
        _layersWidthDip = std::min(kLayersMaxDip, std::max(kLayersMinDip, _preferredLayersWidthDip));
    }
    const float layersLeft = widthDip - _layersWidthDip;
    PlaceControl(_menu, D2D1::RectF(0.0f, 0.0f, widthDip, menuBottom));
    if (_tabs)
    {
        PlaceControl(_tabs, D2D1::RectF(0.0f, menuBottom, widthDip, tabBottom));
    }
    if (_rail)
    {
        PlaceControl(_rail, D2D1::RectF(0.0f, tabBottom, kRailWidthDip, statusTop));
        const float wellTop = std::max(tabBottom, statusTop - kWellBlockDip);
        const PaletteLayout palette = PaletteLayoutAt(wellTop);
        if (_railPalette)
        {
            PlaceControl(_railPalette, palette.block);
            PlaceControl(_railPalette->BackgroundZone(), palette.background);
            PlaceControl(_railPalette->ForegroundZone(), palette.foreground);
            PlaceControl(_railPalette->ResetZone(), palette.reset);
            PlaceControl(_railPalette->SwapZone(), palette.swap);
        }
        const int fit = RailSlotsThatFit(wellTop);
        const bool overflow = fit < static_cast<int>(kEditorToolCount);
        const int shown = overflow ? std::max(0, fit - 1) : static_cast<int>(kEditorToolCount);
        _moreToolStart = shown;
        for (std::size_t index = 0; index < kEditorToolCount; ++index)
        {
            if (DxUi::Button* button = _toolButtons[index])
            {
                PlaceControl(button, ToolButtonBoundsDip(EditorToolAt(index)));
                ShowControl(button, static_cast<int>(index) < shown);
            }
        }
        if (_moreTools)
        {
            const bool showMore = shown < static_cast<int>(kEditorToolCount) && fit >= 1;
            if (showMore)
            {
                PlaceControl(_moreTools, ToolButtonBoundsDip(EditorToolAt(static_cast<std::size_t>(shown))));
            }
            ShowControl(_moreTools, showMore);
            if (!showMore && _moreToolsOpen)
            {
                CloseMoreTools();
            }
        }
        ArrangeMoreTools();
    }
    if (_header)
    {
        PlaceControl(_header,
                     D2D1::RectF(kRailWidthDip + 8.0f, tabBottom, layersLeft - kSplitterWidthDip, headerBottom));
    }
    ArrangeTransformHeader(kRailWidthDip + 8.0f, tabBottom, layersLeft - kSplitterWidthDip, headerBottom);
    ArrangeSelectionHeader(kRailWidthDip + 8.0f, tabBottom, layersLeft - kSplitterWidthDip, headerBottom);
    ArrangeBrushHeader(kRailWidthDip + 8.0f, tabBottom, layersLeft - kSplitterWidthDip, headerBottom);
    ArrangeGradientHeader(kRailWidthDip + 8.0f, tabBottom, layersLeft - kSplitterWidthDip, headerBottom);
    ArrangeShapeHeader(kRailWidthDip + 8.0f, tabBottom, layersLeft - kSplitterWidthDip, headerBottom);
    ArrangeCropHeader(kRailWidthDip + 8.0f, tabBottom, layersLeft - kSplitterWidthDip, headerBottom);
    if (_overlay)
    {
        PlaceControl(_overlay, D2D1::RectF(kRailWidthDip, headerBottom, layersLeft - kSplitterWidthDip, statusTop));
    }
    if (_layersTitle)
    {
        const float titleRight = std::max(layersLeft + 4.0f, widthDip - kOpacityWidthDip - 8.0f);
        PlaceControl(_layersTitle, D2D1::RectF(layersLeft + 4.0f, tabBottom, titleRight, tabBottom + kLayersTitleDip));
    }
    if (_opacity)
    {
        PlaceControl(_opacity, D2D1::RectF(widthDip - 4.0f - kOpacityWidthDip, tabBottom + 2.0f, widthDip - 4.0f,
                                           tabBottom + kLayersTitleDip - 2.0f));
    }
    if (_layers)
    {
        PlaceControl(_layers, D2D1::RectF(layersLeft, tabBottom + kLayersTitleDip, widthDip, statusTop));
    }
    if (_status)
    {
        PlaceControl(_status, D2D1::RectF(0.0f, statusTop, widthDip, heightDip));
    }
    if (_jobProgress)
    {
        const float fixedBeforeProgress = kStatusZoomSectionWidthDip + kStatusDocumentSectionWidthDip;
        const float sectionLeft = std::max(fixedBeforeProgress, widthDip - kJobProgressSectionWidthDip);
        const float top = statusTop + (kStatusHeightDip - kJobProgressTrackHeightDip) * 0.5f;
        PlaceControl(_jobProgress,
                     D2D1::RectF(sectionLeft + kJobProgressInsetDip, top,
                                 std::max(sectionLeft + kJobProgressInsetDip, widthDip - kJobProgressInsetDip),
                                 top + kJobProgressTrackHeightDip));
    }
    ArrangeAdjustPanel(layersLeft - kSplitterWidthDip, headerBottom, statusTop);
    ArrangeSizeCard(layersLeft - kSplitterWidthDip, headerBottom, statusTop);
    ArrangeRenameCard(layersLeft - kSplitterWidthDip, headerBottom);
    ArrangePicker();
    ArrangeNotice();
    ArrangeShortcuts();
}

void ChromeHost::ArrangePicker() noexcept
{
    if (!_pickerCard || !_picker)
    {
        return;
    }
    const float width = DxUi::ColorPicker::kDefaultWidthDip + 2.0f * kPickerMarginDip;
    const float height = DxUi::ColorPicker::kDefaultHeightDip + 2.0f * kPickerMarginDip;
    const float left = kRailWidthDip + kPickerMarginDip;
    const D2D1_RECT_F foreground = PaletteWellBoundsDip(false);
    const float minTop = kMenuHeightDip + kTabHeightDip + kPickerMarginDip;
    const float maxBottom = HeightDip() - kStatusHeightDip - kPickerMarginDip;
    float top = foreground.top - kPickerMarginDip - height;
    if (top < minTop)
    {
        top = minTop;
    }
    if (top + height > maxBottom)
    {
        top = std::max(minTop, maxBottom - height);
    }
    PlaceControl(_pickerCard, D2D1::RectF(left, top, left + width, top + height));
    PlaceControl(_picker, D2D1::RectF(left + kPickerMarginDip, top + kPickerMarginDip,
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
    const PaletteLayout layout = PaletteLayoutAt(wellTop);
    return background ? layout.background : layout.foreground;
}

D2D1_RECT_F ChromeHost::PaletteHotBoundsDip(bool reset) const noexcept
{
    const float statusTop = HeightDip() - kStatusHeightDip;
    const float wellTop = std::max(kMenuHeightDip + kTabHeightDip, statusTop - kWellBlockDip);
    const PaletteLayout layout = PaletteLayoutAt(wellTop);
    return reset ? layout.reset : layout.swap;
}

D2D1_RECT_F ChromeHost::ColorPickerBoundsDip() const noexcept
{
    return ColorPickerOpen() ? _pickerCard->GetBounds() : D2D1::RectF();
}

bool ChromeHost::ColorPickerOpen() const noexcept
{
    return _pickerCard != nullptr && _picker != nullptr && _pickerCard->IsVisible();
}

D2D1_RECT_F ChromeHost::LayerRowBoundsDip(std::size_t index) const noexcept
{
    if (_layers == nullptr)
    {
        return D2D1::RectF();
    }
    const std::optional<D2D1_RECT_F> row = _layers->GetVisibleItemHitRect(index);
    return row.value_or(D2D1::RectF());
}

D2D1_RECT_F ChromeHost::LayerMaskBadgeBoundsDip(std::size_t index) const noexcept
{
    if (!_attached || _layers == nullptr || index >= _treeModel.rows.size())
    {
        return D2D1::RectF();
    }
    const DxUi::TreeItemLayoutMetrics metrics = _layers->GetItemLayoutMetrics(_view.Controls(), index);
    return metrics.hasBadge ? metrics.badgeRect : D2D1::RectF();
}

D2D1_RECT_F ChromeHost::FlipHorizontalBoundsDipForTest() const noexcept
{
    const bool shown = _attached && _transformHeader != nullptr && _transformHeader->IsVisible() &&
                       _flipHorizontal != nullptr && _flipHorizontal->IsVisible();
    return shown ? _flipHorizontal->GetBounds() : D2D1::RectF();
}

std::optional<ChromeHost::LayerTargetPress> ChromeHost::LayerTargetAt(float xPixels, float yPixels) const
{
    if (!_attached || _layers == nullptr || !_layers->IsVisible())
    {
        return std::nullopt;
    }
    const D2D1_POINT_2F point = PixelToDip(D2D1::Point2F(xPixels, yPixels));
    const auto inside = [&point](const D2D1_RECT_F& bounds) noexcept
    { return point.x >= bounds.left && point.x < bounds.right && point.y >= bounds.top && point.y < bounds.bottom; };
    // A floating card over the panel takes the press itself.
    for (std::size_t index = 0; index < _cardCount; ++index)
    {
        if (_cards[index].panel != nullptr && inside(_cards[index].panel->GetBounds()))
        {
            return std::nullopt;
        }
    }
    const std::optional<std::size_t> row = _layers->FindVisibleItemAtPoint(point);
    if (!row.has_value() || row.value() >= _treeModel.rows.size() || _treeModel.badgeText[row.value()] == nullptr)
    {
        return std::nullopt;
    }
    const Guid id = _treeModel.rows[row.value()].id;
    const DxUi::TreeItemLayoutMetrics metrics = _layers->GetItemLayoutMetrics(_view.Controls(), row.value());
    if (metrics.hasBadge && inside(metrics.badgeRect))
    {
        return LayerTargetPress{id, true};
    }
    if (_treeModel.maskTargeted && _treeModel.active == std::optional<Guid>(id))
    {
        return LayerTargetPress{id, false};
    }
    return std::nullopt;
}

DxUi::TreeItemData ChromeHost::LayerRowItemForTest(std::size_t index) const
{
    DxUi::TreeItemData item;
    _treeModel.GetVisibleItem(index, item);
    return item;
}

bool ChromeHost::MenuItemGrayedForTest(int command) const noexcept
{
    for (const unique_hmenu& menu : _menus)
    {
        const UINT state =
            menu ? GetMenuState(menu.get(), static_cast<UINT>(command), MF_BYCOMMAND) : static_cast<UINT>(-1);
        if (state != static_cast<UINT>(-1))
        {
            return (state & (MF_GRAYED | MF_DISABLED)) != 0;
        }
    }
    return false;
}

int ChromeHost::HitKindForTest(float xPixels, float yPixels) noexcept
{
    if (!_attached)
    {
        return 0;
    }
    const float scale = _dpi > 0.0f ? _dpi / 96.0f : 1.0f;
    const DxUi::Control* hit = _view.Controls().DebugHitTestControl(D2D1::Point2F(xPixels / scale, yPixels / scale));
    if (hit == nullptr)
    {
        return 0;
    }
    if (hit == _layers)
    {
        return 1;
    }
    if (hit == _splitter)
    {
        return 2;
    }
    if (hit == _shortcutsCard || hit == _shortcutsTree)
    {
        return 3;
    }
    if (hit == _noticeCard || hit == _noticeLabel || hit == _noticeClose)
    {
        return 4;
    }
    if (hit == _adjustCard)
    {
        return 5;
    }
    if (hit == _pickerCard || hit == _picker)
    {
        return 6;
    }
    if (hit == _overlay)
    {
        return 7;
    }
    return 8;
}

void ChromeHost::ArrangeNotice() noexcept
{
    if (_noticeCard == nullptr || !_noticeCard->IsVisible())
    {
        return;
    }
    const float width = std::min(420.0f, std::max(240.0f, WidthDip() - kRailWidthDip - 32.0f));
    const float left = kRailWidthDip + 16.0f;
    const float top = kMenuHeightDip + kTabHeightDip + kHeaderHeightDip + 16.0f;
    const float labelBottom = top + 12.0f + 48.0f;
    PlaceControl(_noticeCard, D2D1::RectF(left, top, left + width, labelBottom + 46.0f));
    PlaceControl(_noticeLabel, D2D1::RectF(left + 12.0f, top + 12.0f, left + width - 12.0f, labelBottom));
    PlaceControl(_noticeClose,
                 D2D1::RectF(left + width - 102.0f, labelBottom + 4.0f, left + width - 12.0f, labelBottom + 34.0f));
}

void ChromeHost::ArrangeShortcuts() noexcept
{
    if (_shortcutsCard == nullptr || !_shortcutsCard->IsVisible())
    {
        return;
    }
    const float right = WidthDip() - LayersWidthDip() - kSplitterWidthDip;
    const float bottom = HeightDip() - kStatusHeightDip - 8.0f;
    const float left = kRailWidthDip + 12.0f;
    const float top = kMenuHeightDip + kTabHeightDip + 8.0f;
    const float width = std::min(440.0f, std::max(220.0f, right - left));
    const float height = std::max(160.0f, bottom - top);
    PlaceControl(_shortcutsCard, D2D1::RectF(left, top, left + width, top + height));
    PlaceControl(_shortcutsTitle, D2D1::RectF(left + 12.0f, top + 10.0f, left + width - 12.0f, top + 32.0f));
    if (_shortcutsSearch != nullptr)
    {
        PlaceControl(_shortcutsSearch, D2D1::RectF(left + 12.0f, top + 36.0f, left + width - 12.0f, top + 64.0f));
    }
    PlaceControl(_shortcutsTree, D2D1::RectF(left + 8.0f, top + 70.0f, left + width - 8.0f, top + height - 46.0f));
    PlaceControl(_shortcutsClose,
                 D2D1::RectF(left + width - 102.0f, top + height - 40.0f, left + width - 12.0f, top + height - 10.0f));
}

void ChromeHost::ShowNotice(std::wstring text) noexcept
{
    if (_noticeCard == nullptr || _noticeLabel == nullptr)
    {
        return;
    }
    CloseShortcuts();
    _noticeLabel->SetText(std::move(text));
    _noticeCard->SetVisible(true);
    _noticeLabel->SetVisible(true);
    ShowControl(_noticeClose, true);
    ArrangeNotice();
    PushCard(ModalCard::Notice, _noticeCard, _noticeClose);
    _view.MarkDirty();
}

void ChromeHost::CloseNotice() noexcept
{
    if (!NoticeOpen())
    {
        return;
    }
    PopCard(ModalCard::Notice);
    _noticeCard->SetVisible(false);
    _view.MarkDirty();
}

bool ChromeHost::EscapeFloatingCard() noexcept
{
    if (MoreToolsOpen())
    {
        CloseMoreTools();
        return true;
    }
    for (std::size_t index = _cardCount; index-- > 0;)
    {
        if (_cards[index].card == ModalCard::Notice)
        {
            CloseNotice();
            return true;
        }
        if (_cards[index].card == ModalCard::Shortcuts)
        {
            CloseShortcuts();
            return true;
        }
    }
    return false;
}

void ChromeHost::PushCard(ModalCard card, DxUi::Control* panel, DxUi::Control* home) noexcept
{
    _accessibleStructureChanged = true; // a card joins the tree
    DxUi::ControlHost& controls = _view.Controls();
    OpenCard entry{card, panel, home, _canvasFocused ? nullptr : controls.GetFocusControl(), _canvasFocused};
    // A card opened again moves to the top and keeps the keyboard it first opened on.
    if (const std::optional<OpenCard> earlier = TakeCard(card); earlier.has_value())
    {
        entry.keyboard = earlier.value().keyboard;
        entry.canvas = earlier.value().canvas;
    }
    _cards[_cardCount++] = entry; // one entry per card: the array holds them all
    if (home != nullptr)
    {
        controls.SetFocusControl(home);
        _canvasFocused = false;
        _accessibilityPlacementDirty = true;
    }
}

std::optional<ChromeHost::OpenCard> ChromeHost::TakeCard(ModalCard card) noexcept
{
    const auto end = _cards.begin() + static_cast<std::ptrdiff_t>(_cardCount);
    const auto found = std::find_if(_cards.begin(), end, [card](const OpenCard& open) { return open.card == card; });
    if (found == end)
    {
        return std::nullopt;
    }
    const OpenCard entry = *found;
    std::copy(found + 1, end, found);
    --_cardCount;
    return entry;
}

void ChromeHost::PopCard(ModalCard card) noexcept
{
    const std::optional<OpenCard> taken = TakeCard(card);
    if (!taken.has_value())
    {
        return;
    }
    _accessibleStructureChanged = true; // a card leaves the tree
    const OpenCard& entry = taken.value();
    DxUi::ControlHost& controls = _view.Controls();
    DxUi::Control* const focus = controls.GetFocusControl();
    if (_canvasFocused || (focus != nullptr && !Contains(entry.panel, focus)))
    {
        return; // the card did not hold the keyboard: it stays where it is
    }
    if (!entry.canvas)
    {
        for (DxUi::Control* target : {entry.keyboard != _menu ? entry.keyboard : nullptr,
                                      _cardCount > 0 ? _cards[_cardCount - 1].home : nullptr})
        {
            if (target == nullptr)
            {
                continue;
            }
            controls.SetFocusControl(target);
            if (controls.GetFocusControl() == target)
            {
                _accessibilityPlacementDirty = true;
                return;
            }
        }
    }
    controls.SetFocusControl(nullptr);
    _canvasFocused = true;
    _accessibilityPlacementDirty = true;
}

const ChromeHost::OpenCard* ChromeHost::KeyboardCard() const noexcept
{
    if (_cardCount == 0)
    {
        return nullptr;
    }
    const DxUi::Control* const focus = _view.Controls().GetFocusControl();
    for (std::size_t index = _cardCount; focus != nullptr && index-- > 0;)
    {
        if (Contains(_cards[index].panel, focus))
        {
            return &_cards[index];
        }
    }
    return &_cards[_cardCount - 1];
}

bool ChromeHost::CardKey(bool enter) noexcept
{
    const OpenCard* const entry = KeyboardCard();
    if (entry == nullptr)
    {
        return false;
    }
    switch (entry->card)
    {
    case ModalCard::Notice:
        CloseNotice();
        return true;
    case ModalCard::Shortcuts:
        // Enter runs a row only from the search field or the list, which take it themselves.
        if (enter)
        {
            return false;
        }
        CloseShortcuts();
        return true;
    case ModalCard::Rename:
        CloseRenameCard(enter);
        return true;
    case ModalCard::Size:
        Report(enter ? SizeAction::Apply : SizeAction::Cancel, 0.0);
        return true;
    case ModalCard::Adjust:
        Report(enter ? AdjustAction::Apply : AdjustAction::Cancel, 0.0);
        return true;
    case ModalCard::Picker:
        CloseColorPicker(enter);
        return true;
    case ModalCard::Count:
        break;
    }
    return false;
}

bool ChromeHost::NoticeOpen() const noexcept
{
    return _noticeCard != nullptr && _noticeCard->IsVisible();
}

D2D1_RECT_F ChromeHost::NoticeBoundsDip() const noexcept
{
    return NoticeOpen() ? _noticeCard->GetBounds() : D2D1::RectF();
}

void ChromeHost::ApplyShortcutFilter() noexcept
{
    _shortcutModel.rows.clear();
    for (const ShortcutListModel::Row& row : _shortcutModel.all)
    {
        if (_shortcutModel.query.empty())
        {
            _shortcutModel.rows.push_back(row);
            continue;
        }
        const auto fold = [](wchar_t character)
        { return static_cast<wchar_t>(towlower(static_cast<wint_t>(character))); };
        bool found = false;
        if (row.name.size() >= _shortcutModel.query.size())
        {
            for (std::size_t start = 0; start + _shortcutModel.query.size() <= row.name.size() && !found; ++start)
            {
                found = true;
                for (std::size_t index = 0; index < _shortcutModel.query.size(); ++index)
                {
                    if (fold(row.name[start + index]) != fold(_shortcutModel.query[index]))
                    {
                        found = false;
                        break;
                    }
                }
            }
        }
        if (found)
        {
            _shortcutModel.rows.push_back(row);
        }
    }
    if (_shortcutsTree != nullptr)
    {
        _shortcutsTree->NotifyDataChanged();
    }
}

const CommandBindings& ChromeHost::Bindings() const noexcept
{
    return _bindings != nullptr ? *_bindings : CommandBindings::Defaults();
}

void ChromeHost::SetCommandBindings(const CommandBindings* bindings) noexcept
{
    _bindings = bindings;
    RefreshDisplayedChords();
}

std::wstring ChromeHost::ChordDisplayText(const KeyChord& chord)
{
    if (chord.IsEmpty())
    {
        return {};
    }
    // Decision D9: modifier names from the STRINGTABLE (a translated table spells them its way), key names from
    // the active keyboard layout; Ctrl, Alt, Shift in that order.
    std::wstring text = LayoutKeyName(chord);
    const auto prefix = [&text](UINT modifierId)
    { text = FormatStringResource(nullptr, IDS_KEY_JOIN, LoadStringResource(nullptr, modifierId), text); };
    if (chord.shift)
    {
        prefix(IDS_KEY_SHIFT);
    }
    if (chord.alt)
    {
        prefix(IDS_KEY_ALT);
    }
    if (chord.ctrl)
    {
        prefix(IDS_KEY_CTRL);
    }
    return text;
}

std::wstring ChromeHost::CommandChordText(int command) const
{
    const std::span<const CommandCatalogEntry> catalog = CommandCatalog();
    if (_chordTexts.size() == catalog.size())
    {
        for (std::size_t index = 0; index < catalog.size(); ++index)
        {
            if (catalog[index].command == command)
            {
                return _chordTexts[index];
            }
        }
        return {};
    }
    const std::optional<KeyChord> chord = Bindings().ChordFor(command);
    return chord.has_value() ? ChordDisplayText(chord.value()) : std::wstring{};
}

std::wstring ChromeHost::WithChordHint(std::wstring text, std::initializer_list<int> commands) const
{
    std::wstring chords;
    for (const int command : commands)
    {
        const std::wstring chord = CommandChordText(command);
        if (!chord.empty())
        {
            chords = chords.empty() ? chord : FormatStringResource(nullptr, IDS_KEY_LIST, chords, chord);
        }
    }
    return chords.empty() ? text : FormatStringResource(nullptr, IDS_TOOL_TOOLTIP, text, chords);
}

std::wstring ChromeHost::CommandMenuCaption(UINT stringId, int command) const
{
    return CommandMenuCaption(LoadStringResource(nullptr, stringId), command);
}

std::wstring ChromeHost::CommandMenuCaption(std::wstring caption, int command) const
{
    caption.resize(CaptionWithoutChord(caption).size());
    const std::wstring chord = CommandChordText(command);
    if (!chord.empty())
    {
        caption.push_back(L'\t');
        caption += chord;
    }
    return caption;
}

void ChromeHost::SetMenuCaption(HMENU menu, int command, const std::wstring& caption, UINT stateFlags) noexcept
{
    if (menu == nullptr)
    {
        return;
    }
    const UINT state = GetMenuState(menu, static_cast<UINT>(command), MF_BYCOMMAND);
    if (state == static_cast<UINT>(-1))
    {
        return;
    }
    wchar_t current[512]{};
    const int length =
        GetMenuStringW(menu, static_cast<UINT>(command), current, static_cast<int>(std::size(current)), MF_BYCOMMAND);
    const bool sameText = length >= 0 && std::wstring_view(current, static_cast<std::size_t>(length)) == caption;
    const bool sameGray = ((state & (MF_GRAYED | MF_DISABLED)) != 0) == ((stateFlags & MF_GRAYED) != 0);
    const bool sameCheck = ((state & MF_CHECKED) != 0) == ((stateFlags & MF_CHECKED) != 0);
    if (sameText && sameGray && sameCheck)
    {
        return;
    }
    (void)ModifyMenuW(menu, static_cast<UINT>(command), MF_BYCOMMAND | MF_STRING | stateFlags,
                      static_cast<UINT_PTR>(command), caption.c_str());
}

void ChromeHost::RefreshMenuChords() noexcept
{
    const auto walk = [this](const auto& self, HMENU menu) -> void
    {
        const int count = menu != nullptr ? GetMenuItemCount(menu) : 0;
        for (int index = 0; index < count; ++index)
        {
            if (HMENU child = GetSubMenu(menu, index))
            {
                self(self, child);
                continue;
            }
            const UINT command = GetMenuItemID(menu, index);
            if (command == static_cast<UINT>(-1) || command == 0)
            {
                continue;
            }
            wchar_t text[512]{};
            const int length = GetMenuStringW(menu, command, text, static_cast<int>(std::size(text)), MF_BYCOMMAND);
            if (length <= 0)
            {
                continue;
            }
            const UINT state = GetMenuState(menu, command, MF_BYCOMMAND);
            const UINT flags = ((state & (MF_GRAYED | MF_DISABLED)) != 0 ? MF_GRAYED : 0u) |
                               ((state & MF_CHECKED) != 0 ? MF_CHECKED : 0u);
            SetMenuCaption(
                menu, static_cast<int>(command),
                CommandMenuCaption(std::wstring(text, static_cast<std::size_t>(length)), static_cast<int>(command)),
                flags);
        }
    };
    for (const unique_hmenu& menu : _menus)
    {
        walk(walk, menu.get());
    }
}

void ChromeHost::RefreshShortcutHints() noexcept
{
    for (std::size_t index = 0; index < kEditorToolCount; ++index)
    {
        const EditorTool tool = EditorToolAt(index);
        const std::wstring tooltip =
            WithChordHint(LoadStringResource(nullptr, EditorToolNameId(tool)), {SelectToolCommand(tool)});
        if (_toolButtons[index] != nullptr)
        {
            _toolButtons[index]->SetTooltipText(tooltip);
        }
        if (_overflowButtons[index] != nullptr)
        {
            _overflowButtons[index]->SetTooltipText(tooltip);
        }
    }
    if (_railPalette != nullptr)
    {
        if (DxUi::Control* swap = _railPalette->SwapZone(); swap != nullptr)
        {
            swap->SetTooltipText(WithChordHint(LoadStringResource(nullptr, IDS_PALETTE_SWAP), {kCommandSwapPalette}));
        }
        if (DxUi::Control* reset = _railPalette->ResetZone(); reset != nullptr)
        {
            reset->SetTooltipText(
                WithChordHint(LoadStringResource(nullptr, IDS_PALETTE_RESET), {kCommandResetPalette}));
        }
    }
    if (_brushMode != nullptr)
    {
        _brushMode->SetTooltipText(
            WithChordHint(LoadStringResource(nullptr, IDS_BRUSH_MODE_TIP),
                          {SelectToolCommand(EditorTool::Brush), SelectToolCommand(EditorTool::Eraser)}));
    }
    if (_brushSize != nullptr)
    {
        _brushSize->SetTooltipText(WithChordHint(LoadStringResource(nullptr, IDS_BRUSH_SIZE_TIP),
                                                 {kCommandBrushSmaller, kCommandBrushLarger}));
    }
    if (_brushHardness != nullptr)
    {
        _brushHardness->SetTooltipText(WithChordHint(LoadStringResource(nullptr, IDS_BRUSH_HARDNESS_TIP),
                                                     {kCommandBrushSofter, kCommandBrushHarder}));
    }
}

void ChromeHost::LaunchShortcut(int command) noexcept
{
    CloseShortcuts();
    if (command != kCommandKeyboardShortcuts)
    {
        RunCommand(command);
    }
}

void ChromeHost::RunShortcutFromSearch() noexcept
{
    if (!ShortcutsOpen() || _shortcutModel.rows.empty())
    {
        return;
    }
    int command = _shortcutModel.rows.front().command;
    if (_shortcutsTree != nullptr)
    {
        if (const std::optional<uint64_t> selected = _shortcutsTree->GetSelectedItemId();
            selected.has_value() && _shortcutModel.FindVisibleItemById(selected.value()).has_value())
        {
            command = static_cast<int>(selected.value());
        }
    }
    LaunchShortcut(command);
}

bool ChromeHost::StepShortcutSelection(int delta) noexcept
{
    if (!ShortcutsOpen() || _shortcutsTree == nullptr || _shortcutModel.rows.empty())
    {
        return false;
    }
    std::optional<std::size_t> current;
    if (const std::optional<uint64_t> selected = _shortcutsTree->GetSelectedItemId(); selected.has_value())
    {
        current = _shortcutModel.FindVisibleItemById(selected.value());
    }
    std::size_t next = 0;
    if (current.has_value())
    {
        next = delta < 0 ? (current.value() == 0 ? 0 : current.value() - 1)
                         : std::min(current.value() + 1, _shortcutModel.rows.size() - 1);
    }
    (void)_shortcutsTree->RequestSelectVisibleItem(next);
    _view.MarkDirty();
    return true;
}

bool ChromeHost::SelectFirstShortcutRow(UINT virtualKey) noexcept
{
    if ((virtualKey != VK_RETURN && virtualKey != VK_SPACE) || !ShortcutsOpen() || _shortcutsTree == nullptr ||
        _view.Controls().GetFocusControl() != _shortcutsTree || _shortcutModel.rows.empty())
    {
        return false;
    }
    if (const std::optional<uint64_t> selected = _shortcutsTree->GetSelectedItemId();
        selected.has_value() && _shortcutModel.FindVisibleItemById(selected.value()).has_value())
    {
        return false;
    }
    (void)_shortcutsTree->RequestSelectVisibleItem(0);
    _view.MarkDirty();
    return true;
}

void ChromeHost::BuildShortcutRows() noexcept
{
    _shortcutModel.all.clear();
    for (const CommandCatalogEntry& entry : CommandCatalog())
    {
        _shortcutModel.all.push_back(
            ShortcutListModel::Row{PlainCommandName(LoadStringResource(nullptr, entry.captionId)),
                                   CommandChordText(entry.command), entry.command});
    }
    ApplyShortcutFilter();
}

void ChromeHost::OpenShortcuts() noexcept
{
    if (_shortcutsCard == nullptr || _shortcutsTree == nullptr)
    {
        return;
    }
    CloseNotice();
    // A fresh list: no query, no selection.
    _shortcutModel.query.clear();
    if (_shortcutsSearch != nullptr)
    {
        _shortcutsSearch->SetText(std::wstring{});
    }
    _shortcutsTree->SetSelectedItemId(std::nullopt);
    BuildShortcutRows();
    _shortcutsCard->SetVisible(true);
    if (_shortcutsTitle != nullptr)
    {
        _shortcutsTitle->SetVisible(true);
    }
    _shortcutsTree->SetVisible(true);
    ShowControl(_shortcutsClose, true);
    ShowControl(_shortcutsSearch, true);
    // Laid out before the search field takes the focus.
    ArrangeShortcuts();
    PushCard(ModalCard::Shortcuts, _shortcutsCard, _shortcutsSearch);
    _view.MarkDirty();
}

void ChromeHost::RefreshDisplayedChords() noexcept
{
    const std::span<const CommandCatalogEntry> catalog = CommandCatalog();
    _chordTexts.clear();
    _chordTexts.reserve(catalog.size());
    for (const CommandCatalogEntry& entry : catalog)
    {
        const std::optional<KeyChord> chord = Bindings().ChordFor(entry.command);
        _chordTexts.push_back(chord.has_value() ? ChordDisplayText(chord.value()) : std::wstring{});
    }
    // Text built from the old chords (the status bar's tool key) is stale; the dynamic menu rows rebuild too.
    ++_displayRevision;
    _resync |= kResyncUndoRedo | kResyncEditTargets | kResyncMaskCommands;
    RefreshMenuChords();
    RefreshShortcutHints();
    if (ShortcutsOpen())
    {
        BuildShortcutRows();
    }
    if (_attached)
    {
        _view.MarkDirty();
    }
}

std::wstring ChromeHost::ShortcutChordAt(std::size_t index) const
{
    if (index >= _shortcutModel.rows.size())
    {
        return {};
    }
    return _shortcutModel.rows[index].chord;
}

std::wstring ChromeHost::MenuItemCaption(int command) const
{
    for (const unique_hmenu& menu : _menus)
    {
        if (!menu)
        {
            continue;
        }
        std::wstring caption = MenuCaptionFrom(menu.get(), command);
        if (!caption.empty())
        {
            return caption;
        }
    }
    return {};
}

void ChromeHost::CloseShortcuts() noexcept
{
    if (!ShortcutsOpen())
    {
        return;
    }
    // When the list held the keyboard it goes back to the card underneath (the Rename card's field, the color
    // picker) or the canvas (PopCard).
    PopCard(ModalCard::Shortcuts);
    _shortcutsCard->SetVisible(false);
    _view.MarkDirty();
}

bool ChromeHost::ShortcutsOpen() const noexcept
{
    return _shortcutsCard != nullptr && _shortcutsCard->IsVisible();
}

bool ChromeHost::TextInputFocused() const noexcept
{
    return _attached && !_canvasFocused && _view.Controls().HasActiveTextInput();
}

ChromeHost::KeyboardFocusMark ChromeHost::KeyboardFocus() const noexcept
{
    return KeyboardFocusMark{_canvasFocused, _attached ? _view.Controls().GetFocusControl() : nullptr};
}

bool ChromeHost::NoteKeyFocusMove(const KeyboardFocusMark& before, bool navigation) noexcept
{
    const KeyboardFocusMark after = KeyboardFocus();
    if (navigation && !after.canvas && after.control != nullptr && after.control != before.control)
    {
        _focusFromKeyboard = true;
    }
    return !(after == before);
}

bool ChromeHost::FocusFromKeyboard() const noexcept
{
    return _focusFromKeyboard && _attached && !_canvasFocused && _view.Controls().GetFocusControl() != nullptr;
}

bool ChromeHost::FocusTakesEnterAndEscape() const noexcept
{
    if (!_attached || _canvasFocused)
    {
        return false;
    }
    const DxUi::Control* const focus = _view.Controls().GetFocusControl();
    if (focus == nullptr)
    {
        return false;
    }
    if (FocusFromKeyboard() || _view.Controls().HasActiveTextInput())
    {
        return true;
    }
    if (const auto* const combo = dynamic_cast<const DxUi::ComboBox*>(focus); combo != nullptr && combo->IsPopupOpen())
    {
        return true; // Enter chooses from the open list, Escape closes it
    }
    for (std::size_t index = 0; index < _cardCount; ++index)
    {
        if (_cards[index].home != nullptr && Contains(_cards[index].panel, focus))
        {
            return true;
        }
    }
    return false;
}

bool ChromeHost::MenuKeyboardActive() const noexcept
{
    // Never behind the canvas: once the canvas has the keyboard, keys are the canvas's.
    return _menuKeyboard && !_canvasFocused && _attached && _menu != nullptr &&
           _view.Controls().GetFocusControl() == _menu;
}

void ChromeHost::ToggleMenuKeyboard() noexcept
{
    if (MenuKeyboardActive())
    {
        LeaveMenuKeyboard();
        return;
    }
    if (!_attached || _menu == nullptr)
    {
        return;
    }
    DxUi::ControlHost& controls = _view.Controls();
    _menuReturnFocus = controls.GetFocusControl() != _menu ? controls.GetFocusControl() : nullptr;
    _menuReturnCanvas = _canvasFocused;
    // The bar highlights its first item when it takes the focus (MenuBar::OnFocusChanged).
    controls.SetFocusControl(_menu);
    _menuKeyboard = controls.GetFocusControl() == _menu;
    if (_menuKeyboard)
    {
        _canvasFocused = false;
        _accessibilityPlacementDirty = true;
    }
    else
    {
        _menuReturnFocus = nullptr;
    }
    _view.MarkDirty();
}

void ChromeHost::LeaveMenuKeyboard() noexcept
{
    if (!_menuKeyboard)
    {
        return;
    }
    _menuKeyboard = false;
    DxUi::Control* const target = _menuReturnFocus;
    _menuReturnFocus = nullptr;
    if (!_attached || _menu == nullptr)
    {
        return;
    }
    DxUi::ControlHost& controls = _view.Controls();
    if (controls.GetFocusControl() != _menu && controls.GetFocusControl() != nullptr)
    {
        return; // a card that opened took the keyboard: it keeps it
    }
    // The bar holds DxUi's focus, or DxUi dropped it (a pruned control): the keyboard goes back where it was.
    controls.SetFocusControl(target);
    if (controls.GetFocusControl() != target)
    {
        controls.SetFocusControl(nullptr);
    }
    _canvasFocused = _menuReturnCanvas;
    _accessibilityPlacementDirty = true;
    _view.MarkDirty();
}

bool ChromeHost::OpenMenuByMnemonic(wchar_t mnemonic) noexcept
{
    if (!_attached || _menu == nullptr || mnemonic < L' ')
    {
        return false;
    }
    if (!MenuKeyboardActive())
    {
        DxUi::Control* const focus = _view.Controls().GetFocusControl();
        _menuReturnFocus = focus != _menu ? focus : nullptr;
        _menuReturnCanvas = _canvasFocused;
    }
    _menuKeyboard = true;
    // The bar takes the focus and opens the item's flyout (OpenMenu, from the keyboard), which hands the focus
    // back before the chosen command runs.
    _inDispatch = true;
    const bool opened = _menu->ActivateMnemonic(_view.Controls(), mnemonic);
    _inDispatch = false;
    if (!opened)
    {
        LeaveMenuKeyboard();
    }
    _view.MarkDirty();
    return opened;
}

void ChromeHost::SetMenusRecordOnlyForTest(bool recordOnly) noexcept
{
    _menusRecordOnly = recordOnly;
    _recordedMenu.reset();
}

void ChromeHost::SetCommandProbeForTest(std::function<void(int)> probe) noexcept
{
    _commandProbe = std::move(probe);
}

void ChromeHost::RunCommand(int command) noexcept
{
    if (_commandProbe)
    {
        _commandProbe(command);
        return;
    }
    _events.OnCommand(command);
}

std::optional<std::size_t> ChromeHost::TakeRecordedMenuForTest() noexcept
{
    const std::optional<std::size_t> recorded = _recordedMenu;
    _recordedMenu.reset();
    return recorded;
}

std::size_t ChromeHost::ShortcutRowCountForTest() const noexcept
{
    return _shortcutModel.rows.size();
}

int ChromeHost::ShortcutCommandAtForTest(std::size_t index) const noexcept
{
    return index < _shortcutModel.rows.size() ? _shortcutModel.rows[index].command : 0;
}

int ChromeHost::SelectedShortcutForTest() const noexcept
{
    if (_shortcutsTree == nullptr)
    {
        return 0;
    }
    const std::optional<uint64_t> selected = _shortcutsTree->GetSelectedItemId();
    return selected.has_value() && _shortcutModel.FindVisibleItemById(selected.value()).has_value()
               ? static_cast<int>(selected.value())
               : 0;
}

void ChromeHost::SelectShortcutForTest(int command) noexcept
{
    if (_shortcutsTree != nullptr)
    {
        _shortcutsTree->SetSelectedItemId(static_cast<uint64_t>(command));
    }
}

void ChromeHost::FocusShortcutListForTest() noexcept
{
    if (ShortcutsOpen() && _shortcutsTree != nullptr)
    {
        _view.Controls().SetFocusControl(_shortcutsTree);
        _canvasFocused = false;
    }
}

std::wstring ChromeHost::ShortcutSearchTextForTest() const
{
    return _shortcutsSearch != nullptr ? std::wstring(_shortcutsSearch->GetText()) : std::wstring{};
}

std::wstring ChromeHost::FocusedControlNameForTest() const
{
    const DxUi::Control* focus = _attached ? _view.Controls().GetFocusControl() : nullptr;
    return focus != nullptr ? std::wstring(focus->GetAccessibleName()) : std::wstring{};
}

bool ChromeHost::ShortcutRowsKeepBuffersForTest() const
{
    const std::vector<ShortcutListModel::Row>& rows = _shortcutModel.rows;
    if (rows.size() < 2)
    {
        return false;
    }
    const auto byName = [](const ShortcutListModel::Row& left, const ShortcutListModel::Row& right)
    { return left.name.size() < right.name.size(); };
    const auto longest = static_cast<std::size_t>(std::max_element(rows.begin(), rows.end(), byName) - rows.begin());
    const auto shortest = static_cast<std::size_t>(std::min_element(rows.begin(), rows.end(), byName) - rows.begin());
    DxUi::TreeItemData item;
    _shortcutModel.GetVisibleItem(longest, item);
    const std::size_t capacity = item.text.capacity();
    _shortcutModel.GetVisibleItem(shortest, item);
    return item.text == rows[shortest].name && item.text.capacity() >= capacity &&
           capacity >= rows[longest].name.size();
}

void ChromeHost::DropChromeFocusForTest() noexcept
{
    if (_attached)
    {
        _view.Controls().SetFocusControl(nullptr);
    }
}

std::wstring ChromeHost::FocusedTextForTest() const
{
    const auto* field = dynamic_cast<const DxUi::TextField*>(_attached ? _view.Controls().GetFocusControl() : nullptr);
    return field != nullptr ? std::wstring(field->GetText()) : std::wstring{};
}

std::wstring ChromeHost::ToolTooltipForTest(EditorTool tool) const
{
    const DxUi::Button* button = _toolButtons[EditorToolIndex(tool)];
    return button != nullptr ? std::wstring(button->GetTooltipText()) : std::wstring{};
}

std::wstring ChromeHost::PaletteSwapTooltipForTest() const
{
    const DxUi::Control* swap = _railPalette != nullptr ? _railPalette->SwapZone() : nullptr;
    return swap != nullptr ? std::wstring(swap->GetTooltipText()) : std::wstring{};
}

void ChromeHost::OnSplitterChange(DxUi::SplitterChange change) noexcept
{
    // No re-layout here: SetBounds inside a captured drag bumps the embedded interaction revision, and the next
    // pointer event would cancel the drag. The next Prepare (every frame) arranges the panes from the new width.
    const float extent = std::max(0.0f, WidthDip() - kRailWidthDip);
    _layersWidthDip = std::max(0.0f, extent - change.position - kSplitterWidthDip);
    _preferredLayersWidthDip = _layersWidthDip;
    _view.MarkDirty();
    _events.OnChromeLayout();
}

void ChromeHost::OnPickerChange(DxUi::ColorPickerChange change) noexcept
{
    // The target follows the working color (a palette well here, a Gradient Map end or the Canvas Size swatch in
    // the host); OK keeps it, Cancel restores it.
    const PickerTarget target = _pickerTarget;
    const ChromeEditPhase phase = EditPhase(change.phase);
    const bool palette = IsPaletteTarget(target);
    if (palette)
    {
        uint32_t& well = target == PickerTarget::Background ? _backgroundArgb : _foregroundArgb;
        if (phase == ChromeEditPhase::Commit)
        {
            well = change.argb | 0xFF000000u;
        }
        ShowPaletteWell(target, phase == ChromeEditPhase::Preview ? change.argb : well);
    }
    if (phase != ChromeEditPhase::Preview)
    {
        ClosePickerCard();
    }
    _view.MarkDirty();
    _events.OnPickerColor(target, phase, change.argb | 0xFF000000u);
}

void ChromeHost::ClosePickerCard() noexcept
{
    if (!ColorPickerOpen())
    {
        return;
    }
    // The keyboard goes back where it was: the well or swatch that opened the picker.
    PopCard(ModalCard::Picker);
    _pickerCard->SetVisible(false);
}

void ChromeHost::ShowPaletteWell(PickerTarget target, uint32_t argb) noexcept
{
    if (_railPalette == nullptr)
    {
        return;
    }
    if (target == PickerTarget::Background)
    {
        _railPalette->SetBackground(argb);
    }
    else if (target == PickerTarget::Foreground)
    {
        _railPalette->SetForeground(argb);
    }
}

void ChromeHost::OpenColorPicker(PickerTarget target, uint32_t argb) noexcept
{
    // A targeted mask paints black or white, never a picked color (legacy openColorPicker, RF-1113).
    if (!_picker || !_pickerCard || (_maskPaintWhite.has_value() && IsPaletteTarget(target)))
    {
        return;
    }
    _pickerTarget = target;
    _picker->SetColor(argb | 0xFF000000u);
    ArrangePicker();
    _pickerCard->SetVisible(true);
    PushCard(ModalCard::Picker, _pickerCard, _picker);
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

void ChromeHost::SetPalette(uint32_t foregroundArgb, uint32_t backgroundArgb,
                            std::optional<bool> maskPaintWhite) noexcept
{
    _foregroundArgb = foregroundArgb | 0xFF000000u;
    _backgroundArgb = backgroundArgb | 0xFF000000u;
    _maskPaintWhite = maskPaintWhite;
    if (maskPaintWhite.has_value())
    {
        // RF-1113 (legacy ColorPalette.swift paletteColor): a targeted mask's wells are black and white, the
        // foreground the color it paints; a picker open on a well closes unchanged (legacy closeColorPicker(commit:
        // false) once isMaskSelected).
        if (ColorPickerOpen() && IsPaletteTarget(_pickerTarget))
        {
            CloseColorPicker(false);
        }
        ShowPaletteWell(PickerTarget::Foreground, maskPaintWhite.value() ? 0xFFFFFFFFu : 0xFF000000u);
        ShowPaletteWell(PickerTarget::Background, maskPaintWhite.value() ? 0xFF000000u : 0xFFFFFFFFu);
        return;
    }
    // A well the open picker edits keeps showing the working color.
    const bool open = ColorPickerOpen();
    if (!(open && _pickerTarget == PickerTarget::Foreground))
    {
        ShowPaletteWell(PickerTarget::Foreground, _foregroundArgb);
    }
    if (!(open && _pickerTarget == PickerTarget::Background))
    {
        ShowPaletteWell(PickerTarget::Background, _backgroundArgb);
    }
}

uint32_t ChromeHost::ForegroundWellForTest() const noexcept
{
    return _railPalette != nullptr ? _railPalette->ForegroundSwatch() : 0u;
}

uint32_t ChromeHost::BackgroundWellForTest() const noexcept
{
    return _railPalette != nullptr ? _railPalette->BackgroundSwatch() : 0u;
}

void ChromeHost::SetActiveLayerOpacity(std::optional<double> opacity) noexcept
{
    if (!_opacity)
    {
        return;
    }
    // A stepper value write invalidates even when it is the value shown: compare with what the stepper holds (the
    // user may have typed another one) before writing.
    const auto show = [this](double percent) noexcept
    {
        if (!_opacity->IsEditing() && _opacity->GetValue() != percent)
        {
            _opacity->SetValue(percent);
        }
    };
    if (!opacity.has_value() || !std::isfinite(opacity.value()))
    {
        _opacity->SetEnabled(false);
        show(100.0);
        return;
    }
    _opacity->SetEnabled(true);
    show(std::round(std::min(1.0, std::max(0.0, opacity.value())) * 100.0));
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
    if (SizeCardOpen() && covers(_sizeCard->GetBounds()))
    {
        return false;
    }
    if (RenameCardOpen() && covers(_renameCard->GetBounds()))
    {
        return false;
    }
    if (NoticeOpen() && covers(_noticeCard->GetBounds()))
    {
        return false;
    }
    if (ShortcutsOpen() && covers(_shortcutsCard->GetBounds()))
    {
        return false;
    }
    if (MoreToolsOpen() && covers(_moreCard->GetBounds()))
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
    if (!_tabs || _tabs->GetTabCount() == 0 ||
        (!TakeResync(kResyncTitle) && dirty == _documentDirty && title == _documentTitle))
    {
        return;
    }
    _documentDirty = dirty;
    _documentTitle = title;
    _tabs->SetTabTitle(0, dirty ? FormatStringResource(nullptr, IDS_DIRTY_MARK, title) : std::move(title));
    _accessibleStructureChanged = true; // the tab's name
    ++_syncCounters.texts;
}

void ChromeHost::SetStatus(std::wstring zoom, std::wstring size, std::wstring tool)
{
    if (!_status || _status->GetSectionCount() < 3)
    {
        return;
    }
    // StatusStrip writes invalidate even when the text is the same.
    const auto section = [this](std::size_t index, std::wstring& text)
    {
        if (_status->GetSectionText(index) != text)
        {
            _status->SetSectionText(index, std::move(text));
            ++_syncCounters.texts;
        }
    };
    section(0, zoom);
    section(1, size);
    section(2, tool);
}

void ChromeHost::SetJobProgress(bool visible, std::optional<unsigned> percent) noexcept
{
    if (_jobProgress == nullptr)
    {
        return;
    }
    if (_pointerPressed && _jobProgress->IsVisible() != visible)
    {
        // The pinned EmbeddedHost cancels an ordinary held button when a sibling's visibility changes its
        // interaction revision. Keep that press intact; replay only the latest requested visibility on release.
        _deferredJobProgress = DeferredJobProgress{visible, percent};
        return;
    }
    _deferredJobProgress.reset();
    if (!visible)
    {
        // Quick/background jobs and hidden progress reports must not dirty the whole retained chrome surface.
        ShowControl(_jobProgress, false);
        return;
    }
    const bool indeterminate = !percent.has_value();
    if (_jobProgress->IsIndeterminate() != indeterminate)
    {
        _jobProgress->SetIndeterminate(indeterminate);
    }
    if (percent.has_value())
    {
        const double value = static_cast<double>(std::clamp(percent.value(), 0u, 100u));
        if (_jobProgress->GetValue() != value)
        {
            _jobProgress->SetValue(value);
        }
    }
    ShowControl(_jobProgress, visible);
}

bool ChromeHost::JobProgressVisible() const noexcept
{
    return _jobProgress != nullptr && _jobProgress->IsVisible();
}

bool ChromeHost::JobProgressIndeterminate() const noexcept
{
    return _jobProgress != nullptr && _jobProgress->IsIndeterminate();
}

std::optional<unsigned> ChromeHost::JobProgressPercent() const noexcept
{
    if (_jobProgress == nullptr || _jobProgress->IsIndeterminate())
    {
        return std::nullopt;
    }
    return static_cast<unsigned int>(_jobProgress->GetValue());
}

D2D1_RECT_F ChromeHost::JobProgressBoundsDip() const noexcept
{
    return _jobProgress != nullptr ? _jobProgress->GetBounds() : D2D1_RECT_F{};
}

void ChromeHost::SetUndoRedoNames(const std::wstring& undoName, const std::wstring& redoName, bool canUndo,
                                  bool canRedo)
{
    HMENU edit = _menus[static_cast<std::size_t>(MenuIndex::Edit)].get();
    if (!edit)
    {
        return;
    }
    MenuTargets& shown = _menuTargets;
    if (!TakeResync(kResyncUndoRedo) && canUndo == shown.canUndo && canRedo == shown.canRedo &&
        undoName == shown.undoName && redoName == shown.redoName)
    {
        return;
    }
    shown.undoName = undoName;
    shown.redoName = redoName;
    shown.canUndo = canUndo;
    shown.canRedo = canRedo;
    ++_syncCounters.menus;
    // The name from the STRINGTABLE, the chord from the effective table (a replaced Undo shows its replacement).
    const std::wstring undo =
        CommandMenuCaption(undoName.empty() ? LoadStringResource(nullptr, IDS_CMD_UNDO)
                                            : FormatStringResource(nullptr, IDS_CMD_UNDO_NAMED, undoName),
                           kCommandUndo);
    const std::wstring redo =
        CommandMenuCaption(redoName.empty() ? LoadStringResource(nullptr, IDS_CMD_REDO)
                                            : FormatStringResource(nullptr, IDS_CMD_REDO_NAMED, redoName),
                           kCommandRedo);
    SetMenuCaption(edit, kCommandUndo, undo, canUndo ? MF_ENABLED : MF_GRAYED);
    SetMenuCaption(edit, kCommandRedo, redo, canRedo ? MF_ENABLED : MF_GRAYED);
}

void ChromeHost::SetEditTargets(bool masksTargeted, bool transformsSelection, bool clipped, int merge)
{
    MenuTargets& shown = _menuTargets;
    if (!TakeResync(kResyncEditTargets) && masksTargeted == shown.masksTargeted &&
        transformsSelection == shown.transformsSelection && clipped == shown.clipped && merge == shown.merge)
    {
        return;
    }
    shown.masksTargeted = masksTargeted;
    shown.transformsSelection = transformsSelection;
    shown.clipped = clipped;
    shown.merge = merge;
    ++_syncCounters.menus;
    const auto rename = [this](HMENU menu, int command, UINT stringId)
    { SetMenuCaption(menu, command, CommandMenuCaption(stringId, command), MF_ENABLED); };
    HMENU edit = _menus[static_cast<std::size_t>(MenuIndex::Edit)].get();
    HMENU layer = _menus[static_cast<std::size_t>(MenuIndex::Layer)].get();
    rename(edit, kCommandInvert, masksTargeted ? IDS_CMD_INVERT_MASK : IDS_CMD_INVERT);
    rename(edit, kCommandFreeTransform, transformsSelection ? IDS_CMD_TRANSFORM_SELECTION : IDS_CMD_FREE_TRANSFORM);
    rename(layer, kCommandDeleteLayer, masksTargeted ? IDS_CMD_DELETE_LAYER_MASK : IDS_CMD_DELETE_LAYER);
    rename(layer, kCommandClipToggle, clipped ? IDS_CMD_CLIP_RELEASE : IDS_CMD_CLIP_CREATE);
    rename(layer, kCommandMergeDown,
           merge == 1 ? IDS_CMD_MERGE_GROUP : (merge == 2 ? IDS_CMD_MERGE_LAYERS : IDS_CMD_MERGE_DOWN));
}

void ChromeHost::SetMaskCommands(bool present, bool targeted, bool linked, bool enabled)
{
    MenuTargets& shown = _menuTargets;
    if (!TakeResync(kResyncMaskCommands) && present == shown.maskPresent && targeted == shown.maskTargeted &&
        linked == shown.maskLinked && enabled == shown.maskEnabled)
    {
        return;
    }
    shown.maskPresent = present;
    shown.maskTargeted = targeted;
    shown.maskLinked = linked;
    shown.maskEnabled = enabled;
    ++_syncCounters.menus;
    HMENU layer = _menus[static_cast<std::size_t>(MenuIndex::Layer)].get();
    const UINT state = present ? MF_ENABLED : MF_GRAYED;
    const auto caption = [&](int command, UINT stringId)
    { SetMenuCaption(layer, command, CommandMenuCaption(stringId, command), state); };
    caption(kCommandTargetLayerMask, targeted ? IDS_CMD_TARGET_PIXELS : IDS_CMD_TARGET_MASK);
    caption(kCommandToggleMaskLink, linked ? IDS_CMD_UNLINK_MASK : IDS_CMD_LINK_MASK);
    caption(kCommandToggleLayerMask, enabled ? IDS_CMD_DISABLE_MASK : IDS_CMD_ENABLE_MASK);
}

void ChromeHost::SyncToolVisuals() noexcept
{
    // Never during a press: a pressed-look write reaching the tree cancels a press held across a frame. Between
    // presses every button's latch is made to match the tool; the button's own pressed state is the reference,
    // because DxUi clears it on release (the clicked active button would otherwise lose its latch).
    if (_pointerPressed)
    {
        _toolVisualsDirty = true;
        return;
    }
    _toolVisualsDirty = false;
    for (std::size_t index = 0; index < kEditorToolCount; ++index)
    {
        DxUi::Button* button = _toolButtons[index];
        const bool latched = EditorToolAt(index) == _tool;
        if (button != nullptr && button->DebugIsPressed() != latched)
        {
            button->SetPressedVisual(latched);
        }
        if (DxUi::Button* overflow = _overflowButtons[index];
            overflow != nullptr && overflow->DebugIsPressed() != latched)
        {
            overflow->SetPressedVisual(latched);
        }
    }
    if (_moreTools != nullptr)
    {
        const bool latched = _moreToolsOpen || (_moreToolStart < static_cast<int>(kEditorToolCount) &&
                                                EditorToolIndex(_tool) >= static_cast<std::size_t>(_moreToolStart));
        if (_moreTools->DebugIsPressed() != latched)
        {
            _moreTools->SetPressedVisual(latched);
        }
    }
}

void ChromeHost::SetTool(EditorTool tool) noexcept
{
    if (!TakeResync(kResyncTool) && tool == _tool)
    {
        return; // the latch follows on its own (Prepare, the release of a press)
    }
    _tool = tool;
    _accessibleStructureChanged = true; // another header
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
    if (_cropHeader)
    {
        _cropHeader->SetVisible(tool == EditorTool::Crop);
    }
    if (_header)
    {
        _header->SetVisible(GenericHeaderWanted(tool));
        _header->SetText(LoadStringResource(nullptr, EditorToolNameId(_tool)));
    }
}

void ChromeHost::SetLayers(std::vector<HierarchyEntry> rows, const std::optional<Guid>& active, bool maskTargeted)
{
    // The same rows, target and mask write nothing (RF-1001): a rebuild re-measures every row's text and repaints.
    const auto sameRow = [](const HierarchyEntry& left, const HierarchyEntry& right) noexcept
    {
        return left.id == right.id && left.depth == right.depth && left.visible == right.visible &&
               left.isGroup == right.isGroup && left.collapsed == right.collapsed && left.hasMask == right.hasMask &&
               left.maskEnabled == right.maskEnabled && left.name == right.name;
    };
    if (!TakeResync(kResyncLayers) && active == _treeModel.active && maskTargeted == _treeModel.maskTargeted &&
        std::equal(rows.begin(), rows.end(), _treeModel.rows.begin(), _treeModel.rows.end(), sameRow))
    {
        return;
    }
    ++_syncCounters.layerRows;
    _accessibleStructureChanged = true;
    // RF-506: also rebuilds each row's display text and Mask badge once, not per paint.
    _treeModel.SetRows(std::move(rows), active, maskTargeted);
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

void ChromeHost::Layout(UINT widthPixels, UINT heightPixels, float dpi) noexcept
{
    if (!_attached)
    {
        return;
    }
    _width = widthPixels;
    _height = heightPixels;
    _dpi = dpi > 0.0f ? dpi : 96.0f;
    Arrange();
}

HRESULT ChromeHost::Prepare(UINT widthPixels, UINT heightPixels, float dpi) noexcept
{
    if (!_attached)
    {
        return E_UNEXPECTED;
    }
    SyncToolVisuals();
    Layout(widthPixels, heightPixels, dpi);
    const HRESULT prepared = _view.Prepare(widthPixels, heightPixels, _dpi);
    // DxUi attaches UI Automation lazily, after preparation, and drops it on hide or a device swap: (re)arm here.
    // Its accessibility calls cancel a press in progress, so both wait for the pointer to be up.
    if (SUCCEEDED(prepared) && _owner != nullptr && !_accessibilitySite && !_pointerPressed && !_inDispatch)
    {
        AttachAccessibility();
    }
    else if (SUCCEEDED(prepared) && !_inDispatch && !_pointerPressed && AccessibilityBehind() &&
             (_accessibilityRequested || _accessibleStructureChanged || AccessibilityListened()))
    {
        // DxUi republishes its UI Automation snapshot only when told after a preparation, walking every control. A
        // listening client hears every change, and the controls a panel, a card or a header shows join the tree with
        // the frame that lays them out. Anything else (the overlay, values, the status bar under a moving pointer)
        // waits until a client asks for the tree (PublishAccessibilityForClient, RF-1001).
        FlushAccessibilityPlacement();
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
    if (event.action == DxUi::PointerAction::Down)
    {
        // A press anywhere, the canvas included, takes the keyboard back from the menu bar (a tapped Alt or F10),
        // and a control it focuses was not reached with the keyboard: Space pans rather than pressing it.
        LeaveMenuKeyboard();
        _focusFromKeyboard = false;
    }
    if (event.action == DxUi::PointerAction::Down && MoreToolsOpen())
    {
        const float scale = _dpi > 0.0f ? _dpi / 96.0f : 1.0f;
        const float x = event.xPixels / scale;
        const float y = event.yPixels / scale;
        const auto inside = [](const D2D1_RECT_F& bounds, float px, float py)
        { return px >= bounds.left && px < bounds.right && py >= bounds.top && py < bounds.bottom; };
        const bool inCard = _moreCard != nullptr && inside(_moreCard->GetBounds(), x, y);
        const bool inButton = _moreTools != nullptr && inside(_moreTools->GetBounds(), x, y);
        if (!inCard && !inButton)
        {
            CloseMoreTools();
            return true;
        }
    }
    const bool inHole = IsCanvasPoint(event.xPixels, event.yPixels);
    if (inHole && event.action == DxUi::PointerAction::Down)
    {
        _canvasFocused = true;
        _accessibilityPlacementDirty = true;
        return false;
    }
    if (!inHole && event.action == DxUi::PointerAction::Down)
    {
        _canvasFocused = false;
        _accessibilityPlacementDirty = true;
        ReleaseFocusForPress(event);
    }
    if (event.action == DxUi::PointerAction::Down)
    {
        _pointerPressed = true;
    }
    _inDispatch = true;
    const bool handled = _view.DispatchPointer(event);
    _inDispatch = false;
    if (event.action == DxUi::PointerAction::Up || event.action == DxUi::PointerAction::Leave)
    {
        // The press is over once DxUi has handled the release (a click in it may have switched the tool): the
        // rail latch deferred during the press applies now.
        _pointerPressed = false;
        if (_deferredJobProgress)
        {
            const auto pending = *_deferredJobProgress;
            SetJobProgress(pending.visible, pending.percent);
        }
        if (_toolVisualsDirty)
        {
            SyncToolVisuals();
        }
    }
    return handled;
}

bool ChromeHost::DispatchKey(UINT virtualKey, bool down, UINT modifiers) noexcept
{
    if (!_attached)
    {
        return false;
    }
    if (down && MenuKeyboardActive() && (virtualKey == VK_ESCAPE || virtualKey == VK_F10))
    {
        LeaveMenuKeyboard();
        return true;
    }
    if (down && _canvasFocused)
    {
        // A key the canvas did not use goes nowhere: the chrome control that held the focus before the canvas took
        // it (a stepper, a card's field) must not type it. Escape and Tab move the keyboard to the chrome.
        if (virtualKey == VK_ESCAPE)
        {
            _canvasFocused = false;
            _accessibilityPlacementDirty = true;
            return true;
        }
        if (virtualKey != VK_TAB)
        {
            return false;
        }
        _canvasFocused = false;
        _accessibilityPlacementDirty = true;
        _view.Controls().SetFocusControl(nullptr);
    }
    if (down && SelectFirstShortcutRow(virtualKey))
    {
        return true;
    }
    _inDispatch = true;
    const bool handledKey = _view.DispatchKey(virtualKey, down, modifiers);
    _inDispatch = false;
    if (handledKey)
    {
        return true;
    }
    // An Enter or Escape the focused control left (a picker child, a card's checkbox, the Rename field's Escape) is
    // the OK or Cancel of the card holding the focus, else of the top card.
    if (down && (virtualKey == VK_RETURN || virtualKey == VK_ESCAPE) && (modifiers & MK_CONTROL) == 0)
    {
        return CardKey(virtualKey == VK_RETURN);
    }
    return false;
}

// A press moves DxUi's focus to the pressed control, and a layout pass while that move is pending cancels the
// press: the release then goes nowhere and a rail button never activates (the WARP self-test holds a press across
// frames). So the focus leaves before the press and the press moves it from nothing. A text edit commits when the
// press lands outside its control, as a click elsewhere commits it anyway; inside, it continues.
void ChromeHost::ReleaseFocusForPress(const DxUi::PointerEvent& event) noexcept
{
    DxUi::ControlHost& controls = _view.Controls();
    DxUi::Control* focused = controls.GetFocusControl();
    if (focused == nullptr)
    {
        return;
    }
    if (controls.HasActiveTextInput())
    {
        const float scale = _dpi > 0.0f ? _dpi / 96.0f : 1.0f;
        const D2D1_RECT_F bounds = focused->GetBounds();
        const float x = event.xPixels / scale;
        const float y = event.yPixels / scale;
        if (x >= bounds.left && x < bounds.right && y >= bounds.top && y < bounds.bottom)
        {
            return;
        }
        controls.CommitFocusedTextInput();
    }
    controls.SetFocusControl(nullptr);
}

void ChromeHost::FocusLayerOpacityForTest() noexcept
{
    // The stepper is a panel, which DxUi never focuses: a click on the value focuses its text field.
    if (_attached && _opacity != nullptr)
    {
        _view.Controls().SetFocusControl(&_opacity->Field());
        _canvasFocused = false;
        _accessibilityPlacementDirty = true;
    }
}

void ChromeHost::FocusLayersForTest() noexcept
{
    if (_attached && _layers != nullptr)
    {
        _view.Controls().SetFocusControl(_layers);
        _canvasFocused = false;
        _accessibilityPlacementDirty = true;
    }
}

bool ChromeHost::ForceTooltipForTest() noexcept
{
    if (!_attached)
    {
        return false;
    }
    (void)_view.Controls().DebugAdvanceTooltipDelayForTest();
    return _view.Controls().HasTooltip();
}

bool ChromeHost::DispatchCharacter(wchar_t character, UINT modifiers) noexcept
{
    // The canvas takes no text, and a chrome control that held the focus before it must not either.
    if (!_attached || _canvasFocused)
    {
        return false;
    }
    if (MenuKeyboardActive())
    {
        // A letter opens its menu; a control character (the Enter or Escape that just acted) does nothing.
        return character >= L' ' ? OpenMenuByMnemonic(character) : true;
    }
    _inDispatch = true;
    const bool handled = _view.DispatchCharacter(character, modifiers);
    _inDispatch = false;
    return handled;
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
    DxUi::Panel& header = *_transformHeader;
    _transformTitle = header.AddChild<DxUi::Label>(LoadStringResource(nullptr, IDS_TRANSFORM_TITLE));
    _transformTitle->SetFontRole(DxUi::FontRole::BodyStrong);
    _transformX =
        MakeStepper(header, {.labelId = IDS_TRANSFORM_X, .labelWidth = 14.0f, .minimum = -1.0e6, .maximum = 1.0e6},
                    TransformAction::SetX);
    _transformY =
        MakeStepper(header, {.labelId = IDS_TRANSFORM_Y, .labelWidth = 14.0f, .minimum = -1.0e6, .maximum = 1.0e6},
                    TransformAction::SetY);
    _transformWidth =
        MakeStepper(header, {.labelId = IDS_TRANSFORM_WIDTH, .labelWidth = 14.0f, .minimum = 1.0, .maximum = 300000.0},
                    TransformAction::SetWidth);
    _transformHeight =
        MakeStepper(header, {.labelId = IDS_TRANSFORM_HEIGHT, .labelWidth = 14.0f, .minimum = 1.0, .maximum = 300000.0},
                    TransformAction::SetHeight);
    _lockRatio = MakeCheck(header, {.labelId = IDS_TRANSFORM_LOCK, .tipId = IDS_TRANSFORM_LOCK_RATIO, .checked = true},
                           TransformAction::LockRatio);
    _transformScale = MakeStepper(header,
                                  {.labelId = IDS_TRANSFORM_SCALE,
                                   .labelWidth = 38.0f,
                                   .unitId = IDS_UNIT_PERCENT,
                                   .unitWidth = 14.0f,
                                   .minimum = 1.0,
                                   .maximum = 10000.0,
                                   .decimals = 1,
                                   .tipId = IDS_TRANSFORM_SCALE_TIP},
                                  TransformAction::SetScale);
    _transformAngle = MakeStepper(header,
                                  {.labelId = IDS_TRANSFORM_ANGLE,
                                   .labelWidth = 40.0f,
                                   .unitId = IDS_UNIT_DEGREES,
                                   .unitWidth = 12.0f,
                                   .minimum = -360.0,
                                   .maximum = 360.0},
                                  TransformAction::SetRotation);
    _flipHorizontal = MakeButton(header, {.labelId = IDS_TRANSFORM_FLIP_HORIZONTAL}, TransformAction::FlipHorizontal);
    _flipVertical = MakeButton(header, {.labelId = IDS_TRANSFORM_FLIP_VERTICAL}, TransformAction::FlipVertical);
    _sampling = MakeCombo(header,
                          NamedItems({{L"nearest", IDS_TRANSFORM_SAMPLING_NEAREST},
                                      {L"smooth", IDS_TRANSFORM_SAMPLING_SMOOTH},
                                      {L"high", IDS_TRANSFORM_SAMPLING_HIGH}}),
                          {.nameId = IDS_A11Y_SAMPLING, .selected = 2}, TransformAction::SetSampling);
    _autoSelect = MakeCheck(header, {.labelId = IDS_TRANSFORM_AUTO_SELECT, .tipId = IDS_TRANSFORM_AUTO_SELECT_TIP},
                            TransformAction::AutoSelect);
    _showControls = MakeCheck(
        header, {.labelId = IDS_TRANSFORM_SHOW_CONTROLS, .tipId = IDS_TRANSFORM_SHOW_CONTROLS_TIP, .checked = true},
        TransformAction::ShowControls);
    _transformCancel = MakeButton(header, {.labelId = IDS_TRANSFORM_CANCEL}, TransformAction::Cancel);
    _transformApply = MakeButton(header, {.labelId = IDS_TRANSFORM_APPLY, .primary = true}, TransformAction::Apply);
    header.SetVisible(_tool == EditorTool::Move);
}

void ChromeHost::ArrangeTransformHeader(float left, float top, float right, float bottom) noexcept
{
    // Apply and Cancel pin to the right while the header holds three buttons; the rest runs left to right in priority
    // order and stops when out of room.
    constexpr float kButtonDip = 64.0f;
    const bool pinned = right - left > 3.0f * kButtonDip;
    const std::array pinnedSlots{HeaderSlot{_transformApply, kButtonDip, pinned},
                                 HeaderSlot{_transformCancel, kButtonDip, pinned}};
    const std::array flow{
        HeaderSlot{_transformTitle, 80.0f},  HeaderSlot{_transformX, 92.0f},      HeaderSlot{_transformY, 92.0f},
        HeaderSlot{_transformWidth, 92.0f},  HeaderSlot{_transformHeight, 92.0f}, HeaderSlot{_lockRatio, 60.0f},
        HeaderSlot{_transformScale, 136.0f}, HeaderSlot{_transformAngle, 116.0f}, HeaderSlot{_flipHorizontal, 64.0f},
        HeaderSlot{_flipVertical, 64.0f},    HeaderSlot{_sampling, 130.0f},       HeaderSlot{_autoSelect, 104.0f},
        HeaderSlot{_showControls, 122.0f}};
    ArrangeHeaderRow(_transformHeader, D2D1::RectF(left, top, right, bottom), pinnedSlots, flow, false);
}

void ChromeHost::SyncStepper(DxUi::NumericStepper* stepper, double value, bool enabled) noexcept
{
    stepper->SetEnabled(enabled);
    if (!stepper->IsEditing())
    {
        stepper->SetValue(value);
    }
}

void ChromeHost::SetTransformInspector(const TransformInspectorState& state) noexcept
{
    // A header writes only a state it has not shown (RF-1001, `_resync`).
    if (!_transformHeader || (!TakeResync(kResyncTransform) && state == _transformInspector))
    {
        return;
    }
    if (state.targetsMask != _transformInspector.targetsMask)
    {
        _accessibleStructureChanged = true; // the title names the target
    }
    _transformInspector = state;
    ++_syncCounters.headers;
    const auto hold = HoldReports();
    _transformTitle->SetText(
        LoadStringResource(nullptr, state.targetsMask ? IDS_TRANSFORM_MASK_TITLE : IDS_TRANSFORM_TITLE));
    const bool numbers = state.enabled && !state.distorted;
    SyncStepper(_transformX, state.x, numbers);
    SyncStepper(_transformY, state.y, numbers);
    SyncStepper(_transformWidth, state.width, numbers);
    SyncStepper(_transformHeight, state.height, numbers);
    SyncStepper(_transformScale, state.scale, numbers);
    SyncStepper(_transformAngle, state.rotation, numbers);
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
}

void ChromeHost::SetTransformOverlay(const std::optional<TransformOverlayGeometry>& geometry,
                                     const std::vector<float>& guideXs, const std::vector<float>& guideYs) noexcept
{
    if (!_overlay)
    {
        return;
    }
    std::optional<CanvasOverlay::Geometry> converted;
    if (geometry.has_value())
    {
        CanvasOverlay::Geometry result;
        for (std::size_t index = 0; index < 8; ++index)
        {
            result.handles[index] = PixelToDip(D2D1::Point2F(static_cast<float>(geometry->handles[index].x),
                                                             static_cast<float>(geometry->handles[index].y)));
        }
        result.rotation = PixelToDip(D2D1::Point2F(static_cast<float>(geometry->rotationHandle.x),
                                                   static_cast<float>(geometry->rotationHandle.y)));
        result.showsRotation = geometry->showsRotation;
        converted = result;
    }
    // The DIP guides go through buffers kept between calls: a drag's steady guides allocate nothing.
    _guideXsDip.clear();
    _guideYsDip.clear();
    for (const float x : guideXs)
    {
        _guideXsDip.push_back(PixelToDip(x));
    }
    for (const float y : guideYs)
    {
        _guideYsDip.push_back(PixelToDip(y));
    }
    const bool moved = _overlay->SetGeometry(converted);
    if (_overlay->SetGuides(_guideXsDip, _guideYsDip) || moved)
    {
        ++_syncCounters.overlays;
    }
}

void ChromeHost::BuildSelectionHeader(DxUi::Panel& root)
{
    _selectionHeader = root.AddChild<DxUi::Panel>();
    DxUi::Panel& header = *_selectionHeader;
    _selectionTitle = header.AddChild<DxUi::Label>(LoadStringResource(nullptr, IDS_TOOL_MARQUEE));
    _selectionTitle->SetFontRole(DxUi::FontRole::BodyStrong);
    // The combos and checkboxes are named by their tooltips.
    _marqueeShape =
        MakeCombo(header, NamedItems({{L"rectangle", IDS_SELECTION_RECTANGLE}, {L"ellipse", IDS_SELECTION_ELLIPSE}}),
                  {.tipId = IDS_SELECTION_SHAPE_TIP}, SelectionAction::SetMarqueeKind);
    _lassoShape =
        MakeCombo(header, NamedItems({{L"freehand", IDS_SELECTION_FREEHAND}, {L"polygonal", IDS_SELECTION_POLYGONAL}}),
                  {.tipId = IDS_SELECTION_LASSO_TIP}, SelectionAction::SetLassoKind);
    _selectionMode = MakeCombo(header,
                               NamedItems({{L"new", IDS_SELECTION_MODE_NEW},
                                           {L"add", IDS_SELECTION_MODE_ADD},
                                           {L"subtract", IDS_SELECTION_MODE_SUBTRACT}}),
                               {.tipId = IDS_SELECTION_MODE_TIP}, SelectionAction::SetMode);
    _wandTolerance = MakeStepper(header,
                                 {.labelId = IDS_WAND_TOLERANCE,
                                  .labelWidth = 60.0f,
                                  .minimum = 0.0,
                                  .maximum = 255.0,
                                  .value = 32.0,
                                  .tipId = IDS_WAND_TOLERANCE_TIP},
                                 SelectionAction::SetTolerance);
    _wandSampleSize = MakeCombo(
        header,
        NamedItems(
            {{L"point", IDS_WAND_POINT_SAMPLE}, {L"three", IDS_WAND_THREE_BY_THREE}, {L"five", IDS_WAND_FIVE_BY_FIVE}}),
        {.tipId = IDS_WAND_SAMPLE_SIZE_TIP}, SelectionAction::SetSampleSize);
    _wandSample = MakeCombo(header, NamedItems({{L"layer", IDS_WAND_THIS_LAYER}, {L"all", IDS_WAND_ALL_LAYERS}}),
                            {.tipId = IDS_WAND_SAMPLE_TIP}, SelectionAction::SetSampleAllLayers);
    _wandContiguous = MakeCheck(header,
                                {.labelId = IDS_WAND_CONTIGUOUS,
                                 .tipId = IDS_WAND_CONTIGUOUS_TIP,
                                 .nameId = IDS_WAND_CONTIGUOUS_TIP,
                                 .checked = true},
                                SelectionAction::SetContiguous);
    _selectionAntialias = MakeCheck(header,
                                    {.labelId = IDS_SELECTION_ANTIALIAS,
                                     .tipId = IDS_SELECTION_ANTIALIAS_TIP,
                                     .nameId = IDS_SELECTION_ANTIALIAS_TIP,
                                     .checked = true},
                                    SelectionAction::SetAntialias);
    // Expand and Contract: the button steps the outline by the amount in the stepper beside it, named like it.
    const auto amount = [](UINT labelId)
    {
        return StepperSpec{.labelId = labelId,
                           .unitId = IDS_UNIT_PIXELS,
                           .unitWidth = 18.0f,
                           .minimum = 1.0,
                           .maximum = static_cast<double>(DocumentSession::kSelectionModifyLimit),
                           .value = 1.0};
    };
    const auto modifyTip = [](UINT labelId)
    { return FormatStringResource(nullptr, IDS_SELECTION_MODIFY_TIP, LoadStringResource(nullptr, labelId)); };
    _expandButton = MakeButton(header, {.labelId = IDS_SELECTION_EXPAND}, SelectionAction::Expand);
    _expandButton->SetTooltipText(modifyTip(IDS_SELECTION_EXPAND));
    _expandAmount = MakeStepper(header, amount(IDS_SELECTION_EXPAND), SelectionAction::SetExpandAmount);
    _contractButton = MakeButton(header, {.labelId = IDS_SELECTION_CONTRACT}, SelectionAction::Contract);
    _contractButton->SetTooltipText(modifyTip(IDS_SELECTION_CONTRACT));
    _contractAmount = MakeStepper(header, amount(IDS_SELECTION_CONTRACT), SelectionAction::SetContractAmount);
    _selectionEmpty = header.AddChild<DxUi::Label>(LoadStringResource(nullptr, IDS_SELECTION_EMPTY));
    _deselectButton = MakeButton(header, {.labelId = IDS_SELECTION_DESELECT}, SelectionAction::Deselect);
    header.SetVisible(EditorToolIsSelection(_tool));
}

void ChromeHost::ArrangeSelectionHeader(float left, float top, float right, float bottom) noexcept
{
    // Deselect and the empty note pin to the right; the rest runs left to right and stops when out of room.
    const SelectionInspectorState& state = _selectionInspector;
    const bool marquee = state.tool == EditorTool::Marquee;
    const bool lasso = state.tool == EditorTool::Lasso;
    const bool wand = state.tool == EditorTool::Wand;
    const std::array pinned{HeaderSlot{_deselectButton, 80.0f, state.hasSelection},
                            HeaderSlot{_selectionEmpty, 110.0f, state.hasSelection && state.isEmpty}};
    const std::array flow{HeaderSlot{_selectionTitle, 92.0f},
                          HeaderSlot{_marqueeShape, 118.0f, marquee},
                          HeaderSlot{_lassoShape, 118.0f, lasso},
                          HeaderSlot{_selectionMode, 106.0f},
                          HeaderSlot{_wandTolerance, 150.0f, wand},
                          HeaderSlot{_wandSampleSize, 150.0f, wand},
                          HeaderSlot{_wandSample, 118.0f, wand},
                          HeaderSlot{_wandContiguous, 104.0f, wand},
                          HeaderSlot{_selectionAntialias, 94.0f, state.antialiasVisible},
                          HeaderSlot{_expandButton, 70.0f},
                          HeaderSlot{_expandAmount, 90.0f},
                          HeaderSlot{_contractButton, 78.0f},
                          HeaderSlot{_contractAmount, 90.0f}};
    ArrangeHeaderRow(_selectionHeader, D2D1::RectF(left, top, right, bottom), pinned, flow, true);
}

void ChromeHost::SetSelectionInspector(const SelectionInspectorState& state) noexcept
{
    if (!_selectionHeader || (!TakeResync(kResyncSelection) && state == _selectionInspector))
    {
        _selectionInspector = state;
        return;
    }
    _selectionInspector = state;
    ++_syncCounters.headers;
    // Values only: the next Prepare arranges the slots the state wants (never SetBounds from a chrome callback).
    const auto hold = HoldReports();
    _selectionTitle->SetText(LoadStringResource(nullptr, EditorToolNameId(state.tool)));
    _marqueeShape->SetSelectedIndex(static_cast<size_t>(std::clamp(state.marqueeKind, 0, 1)));
    _lassoShape->SetSelectedIndex(static_cast<size_t>(std::clamp(state.lassoKind, 0, 1)));
    _selectionMode->SetSelectedIndex(static_cast<size_t>(std::clamp(state.mode, 0, 2)));
    SyncStepper(_wandTolerance, state.tolerance, state.enabled);
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
    SyncStepper(_expandAmount, state.expandAmount, state.canModify);
    SyncStepper(_contractAmount, state.contractAmount, state.canModify);
    _expandButton->SetEnabled(state.canModify);
    _contractButton->SetEnabled(state.canModify);
    _deselectButton->SetEnabled(state.enabled && state.hasSelection);
}

void ChromeHost::SetSelectionOutline(std::vector<std::vector<D2D1_POINT_2F>> loops) noexcept
{
    if (!_overlay)
    {
        return;
    }
    ++_syncCounters.overlays; // the Application pushes an outline only when it or the view mapping changed
    for (std::vector<D2D1_POINT_2F>& loop : loops)
    {
        for (D2D1_POINT_2F& point : loop)
        {
            point = PixelToDip(point);
        }
    }
    _overlay->SetSelectionOutline(std::move(loops));
}

void ChromeHost::SetSelectionAntPhase(float phase) noexcept
{
    if (_overlay)
    {
        _overlay->SetAntPhase(phase);
    }
}

float ChromeHost::SelectionAntPhase() const noexcept
{
    return _overlay != nullptr ? _overlay->AntPhase() : 0.0f;
}

void ChromeHost::SetLassoDraft(std::optional<CanvasOverlay::Draft> draft) noexcept
{
    if (!_overlay)
    {
        return;
    }
    if (draft.has_value())
    {
        for (D2D1_POINT_2F& point : draft->points)
        {
            point = PixelToDip(point);
        }
        if (draft->cursor.has_value())
        {
            draft->cursor = PixelToDip(*draft->cursor);
        }
    }
    if (_overlay->SetDraft(std::move(draft)))
    {
        ++_syncCounters.overlays;
    }
}

void ChromeHost::BuildBrushHeader(DxUi::Panel& root)
{
    _brushHeader = root.AddChild<DxUi::Panel>();
    DxUi::Panel& header = *_brushHeader;
    _brushTitle = header.AddChild<DxUi::Label>(LoadStringResource(nullptr, IDS_TOOL_BRUSH));
    _brushTitle->SetFontRole(DxUi::FontRole::BodyStrong);
    // The combos are named by their tooltips; the steppers by their labels (the tooltips name their keys).
    _brushMode = MakeCombo(header, NamedItems({{L"paint", IDS_BRUSH_MODE_PAINT}, {L"erase", IDS_BRUSH_MODE_ERASE}}),
                           {.tipId = IDS_BRUSH_MODE_TIP}, BrushAction::SetMode);
    _brushSize = MakeStepper(header,
                             {.labelId = IDS_BRUSH_SIZE,
                              .labelWidth = 34.0f,
                              .unitId = IDS_UNIT_PIXELS,
                              .unitWidth = 18.0f,
                              .minimum = 1.0,
                              .maximum = 2000.0,
                              .value = 40.0,
                              .tipId = IDS_BRUSH_SIZE_TIP},
                             BrushAction::SetSize);
    _brushHardness = MakeStepper(header,
                                 {.labelId = IDS_BRUSH_HARDNESS,
                                  .labelWidth = 62.0f,
                                  .unitId = IDS_UNIT_PERCENT,
                                  .unitWidth = 14.0f,
                                  .minimum = 0.0,
                                  .maximum = 100.0,
                                  .value = 100.0,
                                  .tipId = IDS_BRUSH_HARDNESS_TIP},
                                 BrushAction::SetHardness);
    _brushOpacity = MakeStepper(header,
                                {.labelId = IDS_BRUSH_OPACITY,
                                 .labelWidth = 54.0f,
                                 .unitId = IDS_UNIT_PERCENT,
                                 .unitWidth = 14.0f,
                                 .minimum = 1.0,
                                 .maximum = 100.0,
                                 .value = 100.0,
                                 .tipId = IDS_BRUSH_OPACITY_TIP},
                                BrushAction::SetOpacity);
    _brushMaskPaint =
        MakeCombo(header, NamedItems({{L"black", IDS_BRUSH_MASK_BLACK}, {L"white", IDS_BRUSH_MASK_WHITE}}),
                  {.tipId = IDS_BRUSH_MASK_TIP}, BrushAction::SetMaskPaint);
    _brushHealMode = MakeCombo(header,
                               NamedItems({{L"content-aware", IDS_HEAL_CONTENT_AWARE},
                                           {L"create-texture", IDS_HEAL_CREATE_TEXTURE},
                                           {L"proximity-match", IDS_HEAL_PROXIMITY_MATCH}}),
                               {.tipId = IDS_HEAL_TYPE_TIP}, BrushAction::SetHealMode);
    _brushCloneAligned =
        MakeCheck(header, {.labelId = IDS_CLONE_ALIGNED, .tipId = IDS_CLONE_ALIGNED_TIP, .checked = true},
                  BrushAction::SetCloneAligned);
    _brushCloneSample =
        MakeCombo(header, NamedItems({{L"layer", IDS_CLONE_THIS_LAYER}, {L"all", IDS_CLONE_ALL_LAYERS}}),
                  {.tipId = IDS_CLONE_SAMPLE_TIP}, BrushAction::SetCloneSample);
    _brushSmearMode = MakeCombo(
        header, NamedItems({{L"liquify", IDS_SMEAR_LIQUIFY}, {L"blur", IDS_SMEAR_BLUR}, {L"smudge", IDS_SMEAR_SMUDGE}}),
        {.tipId = IDS_SMEAR_MODE_TIP}, BrushAction::SetSmearMode);
    _brushSourceNote = header.AddChild<DxUi::Label>(LoadStringResource(nullptr, IDS_CLONE_SOURCE_NOTE));
    _brushColorLabel = header.AddChild<DxUi::Label>(LoadStringResource(nullptr, IDS_BRUSH_COLOR));
    // The same foreground color and picker as the rail's well.
    _brushColor = header.AddChild<DxUi::ColorSwatch>(_foregroundArgb);
    _brushColor->SetAccessibleName(LoadStringResource(nullptr, IDS_PALETTE_FOREGROUND));
    _brushColor->SetTooltipText(LoadStringResource(nullptr, IDS_PALETTE_FOREGROUND));
    _brushColor->SetOnClick([this] { Report(BrushAction::OpenColor, 0.0); });
    _brushMaskNote = header.AddChild<DxUi::Label>(LoadStringResource(nullptr, IDS_BRUSH_MASK_NOTE));
    header.SetVisible(EditorToolIsBrush(_tool));
}

void ChromeHost::ArrangeBrushHeader(float left, float top, float right, float bottom) noexcept
{
    const BrushInspectorState& state = _brushInspector;
    const bool clone = state.tool == EditorTool::Clone;
    const bool colorWanted = !state.maskSelected && !clone && state.tool != EditorTool::Smear;
    const std::array pinned{HeaderSlot{_brushMaskNote, 50.0f, state.maskSelected},
                            HeaderSlot{_brushSourceNote, 170.0f, clone && !state.cloneHasSource}};
    const std::array flow{HeaderSlot{_brushTitle, 92.0f},
                          HeaderSlot{_brushMode, 104.0f, state.tool == EditorTool::Brush},
                          HeaderSlot{_brushSmearMode, 112.0f, state.tool == EditorTool::Smear},
                          HeaderSlot{_brushHealMode, 150.0f, state.tool == EditorTool::Heal},
                          HeaderSlot{_brushCloneAligned, 84.0f, clone},
                          HeaderSlot{_brushCloneSample, 118.0f, clone},
                          HeaderSlot{_brushSize, 118.0f},
                          HeaderSlot{_brushHardness, 150.0f},
                          HeaderSlot{_brushOpacity, 142.0f},
                          HeaderSlot{_brushMaskPaint, 150.0f, state.maskSelected},
                          HeaderSlot{_brushColorLabel, 40.0f, colorWanted},
                          HeaderSlot{_brushColor, 34.0f, colorWanted, 4.0f}};
    ArrangeHeaderRow(_brushHeader, D2D1::RectF(left, top, right, bottom), pinned, flow, true);
}

void ChromeHost::SetBrushInspector(const BrushInspectorState& state) noexcept
{
    if (!_brushHeader || (!TakeResync(kResyncBrush) && state == _brushInspector))
    {
        _brushInspector = state;
        return;
    }
    _brushInspector = state;
    ++_syncCounters.headers;
    // Values only; the next Prepare arranges the slots the state wants.
    const auto hold = HoldReports();
    _brushTitle->SetText(LoadStringResource(nullptr, EditorToolNameId(state.tool)));
    _brushMode->SetSelectedIndex(static_cast<size_t>(std::clamp(state.mode, 0, 1)));
    SyncStepper(_brushSize, state.size, state.enabled);
    SyncStepper(_brushHardness, state.hardness, state.enabled);
    SyncStepper(_brushOpacity, state.opacity, state.enabled);
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
}

void ChromeHost::SetBrushCursor(std::optional<CanvasOverlay::BrushCursor> cursor) noexcept
{
    if (!_overlay)
    {
        return;
    }
    if (cursor.has_value())
    {
        cursor->center = PixelToDip(cursor->center);
        cursor->radius = PixelToDip(cursor->radius);
    }
    if (_overlay->SetBrushCursor(cursor))
    {
        ++_syncCounters.overlays;
    }
}

bool ChromeHost::GenericHeaderWanted(EditorTool tool) noexcept
{
    return tool != EditorTool::Move && !EditorToolIsSelection(tool) && !EditorToolIsBrush(tool) &&
           tool != EditorTool::Gradient && tool != EditorTool::Shape && tool != EditorTool::Crop;
}

void ChromeHost::BuildGradientHeader(DxUi::Panel& root)
{
    _gradientHeader = root.AddChild<DxUi::Panel>();
    DxUi::Panel& header = *_gradientHeader;
    _gradientTitle = header.AddChild<DxUi::Label>(LoadStringResource(nullptr, IDS_TOOL_GRADIENT));
    _gradientTitle->SetFontRole(DxUi::FontRole::BodyStrong);
    _gradientShape = MakeCombo(header, NamedItems({{L"linear", IDS_GRADIENT_LINEAR}, {L"radial", IDS_GRADIENT_RADIAL}}),
                               {.tipId = IDS_GRADIENT_SHAPE_TIP}, GradientAction::SetShape);
    // The two end colors as swatches (the transparent end shows the checker); clicking opens the foreground picker.
    for (DxUi::ColorSwatch** swatch : {&_gradientStart, &_gradientEnd})
    {
        *swatch = header.AddChild<DxUi::ColorSwatch>(_foregroundArgb);
        (*swatch)->SetTooltipText(LoadStringResource(nullptr, IDS_GRADIENT_COLORS_TIP));
        (*swatch)->SetAccessibleName(
            LoadStringResource(nullptr, swatch == &_gradientStart ? IDS_A11Y_GRADIENT_START : IDS_A11Y_GRADIENT_END));
        (*swatch)->SetOnClick([this] { Report(GradientAction::OpenColor, 0.0); });
    }
    _gradientStyle = MakeCombo(header,
                               NamedItems({{L"foreground-background", IDS_GRADIENT_FG_BG},
                                           {L"foreground-transparent", IDS_GRADIENT_FG_TRANSPARENT}}),
                               {.tipId = IDS_GRADIENT_COLORS_TIP}, GradientAction::SetStyle);
    _gradientReverse = MakeCheck(header, {.labelId = IDS_GRADIENT_REVERSE}, GradientAction::SetReversed);
    _gradientOpacity = MakeStepper(header,
                                   {.labelId = IDS_BRUSH_OPACITY,
                                    .labelWidth = 54.0f,
                                    .unitId = IDS_UNIT_PERCENT,
                                    .unitWidth = 14.0f,
                                    .minimum = 1.0,
                                    .maximum = 100.0,
                                    .value = 100.0,
                                    .tipId = IDS_GRADIENT_OPACITY_TIP},
                                   GradientAction::SetOpacity);
    _gradientMaskNote = header.AddChild<DxUi::Label>(LoadStringResource(nullptr, IDS_BRUSH_MASK_NOTE));
    _gradientCancel = MakeButton(header, {.labelId = IDS_TRANSFORM_CANCEL}, GradientAction::Cancel);
    _gradientApply = MakeButton(header, {.labelId = IDS_TRANSFORM_APPLY}, GradientAction::Apply);
    header.SetVisible(_tool == EditorTool::Gradient);
}

void ChromeHost::ArrangeGradientHeader(float left, float top, float right, float bottom) noexcept
{
    const GradientInspectorState& state = _gradientInspector;
    const std::array pinned{HeaderSlot{_gradientApply, 72.0f, state.pending},
                            HeaderSlot{_gradientCancel, 72.0f, state.pending},
                            HeaderSlot{_gradientMaskNote, 50.0f, state.maskSelected}};
    // The two end swatches sit 2 DIP apart.
    const std::array flow{HeaderSlot{_gradientTitle, 78.0f},
                          HeaderSlot{_gradientShape, 104.0f},
                          HeaderSlot{_gradientStart, 30.0f, true, 4.0f, 2.0f},
                          HeaderSlot{_gradientEnd, 30.0f, true, 4.0f},
                          HeaderSlot{_gradientStyle, 200.0f},
                          HeaderSlot{_gradientReverse, 88.0f},
                          HeaderSlot{_gradientOpacity, 142.0f}};
    ArrangeHeaderRow(_gradientHeader, D2D1::RectF(left, top, right, bottom), pinned, flow, true);
}

void ChromeHost::SetGradientInspector(const GradientInspectorState& state) noexcept
{
    if (!_gradientHeader || (!TakeResync(kResyncGradient) && state == _gradientInspector))
    {
        _gradientInspector = state;
        return;
    }
    _gradientInspector = state;
    ++_syncCounters.headers;
    // Values only; the next Prepare arranges the slots the state wants.
    const auto hold = HoldReports();
    _gradientShape->SetSelectedIndex(static_cast<size_t>(std::clamp(state.shape, 0, 1)));
    _gradientShape->SetEnabled(state.enabled);
    _gradientStyle->SetSelectedIndex(static_cast<size_t>(std::clamp(state.style, 0, 1)));
    _gradientStyle->SetEnabled(state.enabled);
    _gradientReverse->SetChecked(state.reversed);
    _gradientReverse->SetEnabled(state.enabled);
    SyncStepper(_gradientOpacity, state.opacity, state.enabled);
    _gradientStart->SetSwatchValue(state.startArgb);
    _gradientEnd->SetSwatchValue(state.endArgb);
    _gradientStart->SetEnabled(state.enabled);
    _gradientEnd->SetEnabled(state.enabled);
    _gradientCancel->SetEnabled(state.pending);
    _gradientApply->SetEnabled(state.pending);
}

void ChromeHost::BuildShapeHeader(DxUi::Panel& root)
{
    _shapeHeader = root.AddChild<DxUi::Panel>();
    DxUi::Panel& header = *_shapeHeader;
    _shapeTitle = header.AddChild<DxUi::Label>(LoadStringResource(nullptr, IDS_TOOL_SHAPE));
    _shapeTitle->SetFontRole(DxUi::FontRole::BodyStrong);
    _shapeKind = MakeCombo(header, NamedItems({{L"rectangle", IDS_SHAPE_RECTANGLE}, {L"ellipse", IDS_SHAPE_ELLIPSE}}),
                           {.tipId = IDS_SHAPE_KIND_TIP}, ShapeAction::SetKind);
    _shapeRadius = MakeStepper(header,
                               {.labelId = IDS_SHAPE_RADIUS,
                                .labelWidth = 50.0f,
                                .unitId = IDS_UNIT_PIXELS,
                                .unitWidth = 18.0f,
                                .minimum = 0.0,
                                .maximum = 5000.0,
                                .value = 0.0,
                                .tipId = IDS_SHAPE_RADIUS_TIP},
                               ShapeAction::SetRadius);
    _shapeFillLabel = header.AddChild<DxUi::Label>(LoadStringResource(nullptr, IDS_SHAPE_FILL));
    _shapeFill = header.AddChild<DxUi::ColorSwatch>(_foregroundArgb);
    _shapeFill->SetAccessibleName(LoadStringResource(nullptr, IDS_PALETTE_FOREGROUND));
    _shapeFill->SetTooltipText(LoadStringResource(nullptr, IDS_SHAPE_FILL_TIP));
    _shapeFill->SetOnClick([this] { Report(ShapeAction::OpenColor, 0.0); });
    header.SetVisible(_tool == EditorTool::Shape);
}

void ChromeHost::ArrangeShapeHeader(float left, float top, float right, float bottom) noexcept
{
    // The corner radius is a rectangle's.
    const std::array flow{HeaderSlot{_shapeTitle, 60.0f}, HeaderSlot{_shapeKind, 118.0f},
                          HeaderSlot{_shapeRadius, 140.0f, _shapeInspector.kind == 0},
                          HeaderSlot{_shapeFillLabel, 30.0f}, HeaderSlot{_shapeFill, 34.0f, true, 4.0f}};
    ArrangeHeaderRow(_shapeHeader, D2D1::RectF(left, top, right, bottom), {}, flow, true);
}

void ChromeHost::SetShapeInspector(const ShapeInspectorState& state) noexcept
{
    if (!_shapeHeader || (!TakeResync(kResyncShape) && state == _shapeInspector))
    {
        _shapeInspector = state;
        return;
    }
    _shapeInspector = state;
    ++_syncCounters.headers;
    const auto hold = HoldReports();
    _shapeKind->SetSelectedIndex(static_cast<size_t>(std::clamp(state.kind, 0, 1)));
    _shapeKind->SetEnabled(state.enabled);
    SyncStepper(_shapeRadius, state.radius, state.enabled);
    _shapeFill->SetSwatchValue(state.foregroundArgb);
    _shapeFill->SetEnabled(state.enabled);
}

void ChromeHost::SetGradientLine(std::optional<std::pair<D2D1_POINT_2F, D2D1_POINT_2F>> line) noexcept
{
    if (!_overlay)
    {
        return;
    }
    if (line.has_value())
    {
        line->first = PixelToDip(line->first);
        line->second = PixelToDip(line->second);
    }
    if (_overlay->SetGradientLine(line))
    {
        ++_syncCounters.overlays;
    }
}

void ChromeHost::SetShapeDraft(std::optional<CanvasOverlay::ShapeDraftOverlay> draft) noexcept
{
    if (!_overlay)
    {
        return;
    }
    if (draft.has_value())
    {
        draft->rect = PixelToDip(draft->rect);
        draft->cornerRadius = PixelToDip(draft->cornerRadius);
    }
    if (_overlay->SetShapeDraft(draft))
    {
        ++_syncCounters.overlays;
    }
}

void ChromeHost::SetSampleRing(std::optional<CanvasOverlay::SampleRing> ring) noexcept
{
    if (!_overlay)
    {
        return;
    }
    if (ring.has_value())
    {
        ring->center = PixelToDip(ring->center);
    }
    if (_overlay->SetSampleRing(ring))
    {
        ++_syncCounters.overlays;
    }
}

void ChromeHost::SetCloneSourceMark(std::optional<D2D1_POINT_2F> point) noexcept
{
    if (!_overlay)
    {
        return;
    }
    if (point.has_value())
    {
        point = PixelToDip(*point);
    }
    if (_overlay->SetCloneSource(point))
    {
        ++_syncCounters.overlays;
    }
}

std::optional<D2D1_POINT_2F> ChromeHost::CloneSourceMarkDip() const noexcept
{
    return _overlay != nullptr ? _overlay->CloneSource() : std::nullopt;
}

void ChromeHost::SetPickerColor(uint32_t argb) noexcept
{
    if (!_picker || !_pickerCard || !_pickerCard->IsVisible())
    {
        return;
    }
    _picker->SetCurrentColor(argb | 0xFF000000u);
    ShowPaletteWell(_pickerTarget, argb | 0xFF000000u);
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
