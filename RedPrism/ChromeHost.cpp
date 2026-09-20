#include "ChromeHost.h"

#include "ThemePalette.h"

#include <algorithm>
#include <iterator>
#include <new>

namespace
{
void AppendMenuString(HMENU menu, UINT id, const wchar_t* text) noexcept
{
    AppendMenuW(menu, MF_STRING, id, text);
}

[[nodiscard]] EditorTool ToolAt(size_t index) noexcept
{
    constexpr EditorTool kTools[] = {
        EditorTool::Move,       EditorTool::Marquee, EditorTool::Lasso,    EditorTool::Wand,
        EditorTool::Crop,       EditorTool::Brush,   EditorTool::Eraser,   EditorTool::Heal,
        EditorTool::Clone,      EditorTool::Smear,   EditorTool::Gradient, EditorTool::Shape,
        EditorTool::Eyedropper, EditorTool::Hand,    EditorTool::Zoom,     EditorTool::Idle};
    return index < std::size(kTools) ? kTools[index] : EditorTool::Move;
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
    outItem.text = row.name.empty() ? row.id.ToString() : row.name;
    if (!row.visible)
    {
        outItem.text += L" (hidden)";
    }
    outItem.depth = static_cast<uint32_t>(row.depth);
    outItem.hasChildren = row.isGroup;
    outItem.expanded = row.isGroup;
}

void ChromeHost::LayerTreeModel::OnTreeSelectionChanged(uint64_t itemId)
{
    if (itemId == 0 || itemId > rows.size() || onLayer == nullptr || !(*onLayer))
    {
        return;
    }
    (*onLayer)(rows[itemId - 1].id);
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
    _toolButtons.clear();
    _fileMenu.reset();
    _editMenu.reset();
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

void ChromeHost::BuildMenus() noexcept
{
    _fileMenu.reset(CreatePopupMenu());
    _editMenu.reset(CreatePopupMenu());
    if (_fileMenu)
    {
        AppendMenuString(_fileMenu.get(), static_cast<UINT>(kCommandNew), L"&New\tCtrl+N");
        AppendMenuString(_fileMenu.get(), static_cast<UINT>(kCommandOpen), L"&Open...\tCtrl+O");
        AppendMenuString(_fileMenu.get(), static_cast<UINT>(kCommandSave), L"&Save\tCtrl+S");
        AppendMenuString(_fileMenu.get(), static_cast<UINT>(kCommandSaveAs), L"Save &As...");
        AppendMenuString(_fileMenu.get(), static_cast<UINT>(kCommandExportPng), L"Export &PNG...");
        AppendMenuString(_fileMenu.get(), static_cast<UINT>(kCommandExportJpeg), L"Export &JPEG...");
    }
    if (_editMenu)
    {
        AppendMenuString(_editMenu.get(), static_cast<UINT>(kCommandUndo), L"&Undo\tCtrl+Z");
        AppendMenuString(_editMenu.get(), static_cast<UINT>(kCommandRedo), L"&Redo\tCtrl+Shift+Z");
    }
}

void ChromeHost::OpenMenu(size_t index, POINT screen, bool) noexcept
{
    HMENU menu = nullptr;
    if (index == 0)
    {
        menu = _fileMenu.get();
    }
    else if (index == 1)
    {
        menu = _editMenu.get();
    }
    if (!menu || !_owner)
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
    try
    {
        BuildMenus();
        auto root = std::make_unique<DxUi::Panel>();
        _menu = root->AddChild<DxUi::MenuBar>();
        _menu->SetItems({DxUi::MenuBarItem{L"File", L'F'}, DxUi::MenuBarItem{L"Edit", L'E'},
                         DxUi::MenuBarItem{L"Image", L'I'}, DxUi::MenuBarItem{L"Layer", L'L'},
                         DxUi::MenuBarItem{L"Select", L'S'}, DxUi::MenuBarItem{L"Filter", L't'},
                         DxUi::MenuBarItem{L"View", L'V'}, DxUi::MenuBarItem{L"Help", L'H'}});
        _menu->SetOnOpenItem([this](size_t index, POINT screen, bool keyboard) { OpenMenu(index, screen, keyboard); });
        _tabs = root->AddChild<DxUi::TabControl>();
        _tabs->AddTab<DxUi::Panel>(L"Untitled");
        _rail = root->AddChild<DxUi::Toolbar>();
        _toolButtons.clear();
        for (size_t index = 0; index < 16; ++index)
        {
            const EditorTool tool = ToolAt(index);
            std::wstring glyph(1, EditorToolKey(tool));
            DxUi::Toggle* toggle = _rail->AddToggleButton(EditorToolName(tool), std::move(glyph));
            toggle->SetChecked(tool == _tool);
            toggle->SetOnToggled(
                [this, tool](bool on)
                {
                    if (on)
                    {
                        SetTool(tool);
                        if (_onTool)
                        {
                            _onTool(tool);
                        }
                    }
                });
            _toolButtons.push_back(toggle);
        }
        _header = root->AddChild<DxUi::Label>(L"Move tool");
        _layersTitle = root->AddChild<DxUi::Label>(L"Layers");
        _layers = root->AddChild<DxUi::Tree>();
        _treeModel.onLayer = &_onLayer;
        _layers->SetModel(&_treeModel);
        _layers->SetDelegate(&_treeModel);
        _status = root->AddChild<DxUi::StatusStrip>();
        _status->SetSections({DxUi::StatusStrip::Section{L"100%", 80.0f},
                              DxUi::StatusStrip::Section{L"1280 × 800", 140.0f},
                              DxUi::StatusStrip::Section{L"Move (V)", 0.0f}});
        _view.Controls().SetRoot(std::move(root));
        Arrange();
        return S_OK;
    }
    catch (const std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }
}

void ChromeHost::Arrange() noexcept
{
    if (!_menu)
    {
        return;
    }
    const float widthDip = (_dpi > 0.0f) ? (static_cast<float>(_width) * 96.0f / _dpi) : static_cast<float>(_width);
    const float heightDip = (_dpi > 0.0f) ? (static_cast<float>(_height) * 96.0f / _dpi) : static_cast<float>(_height);
    if (auto* root = _view.Controls().GetRoot())
    {
        root->SetBounds(D2D1::RectF(0.0f, 0.0f, widthDip, heightDip));
    }
    _layersWidthDip = std::min(kLayersMaxDip, std::max(kLayersMinDip, _layersWidthDip));
    const float menuBottom = kMenuHeightDip;
    const float tabBottom = menuBottom + kTabHeightDip;
    const float headerBottom = tabBottom + kHeaderHeightDip;
    const float statusTop = heightDip - kStatusHeightDip;
    const float layersLeft = widthDip - _layersWidthDip;
    _menu->SetBounds(D2D1::RectF(0.0f, 0.0f, widthDip, menuBottom));
    if (_tabs)
    {
        _tabs->SetBounds(D2D1::RectF(0.0f, menuBottom, widthDip, tabBottom));
    }
    if (_rail)
    {
        _rail->SetBounds(D2D1::RectF(0.0f, tabBottom, kRailWidthDip, statusTop));
    }
    if (_header)
    {
        _header->SetBounds(D2D1::RectF(kRailWidthDip, tabBottom, layersLeft - kSplitterWidthDip, headerBottom));
    }
    if (_layersTitle)
    {
        _layersTitle->SetBounds(D2D1::RectF(layersLeft, tabBottom, widthDip, tabBottom + 24.0f));
    }
    if (_layers)
    {
        _layers->SetBounds(D2D1::RectF(layersLeft, tabBottom + 24.0f, widthDip, statusTop));
    }
    if (_status)
    {
        _status->SetBounds(D2D1::RectF(0.0f, statusTop, widthDip, heightDip));
    }
}

CanvasHole ChromeHost::HolePixels() const noexcept
{
    const float scale = _dpi / 96.0f;
    const float widthDip = (_dpi > 0.0f) ? (static_cast<float>(_width) * 96.0f / _dpi) : static_cast<float>(_width);
    const float heightDip = (_dpi > 0.0f) ? (static_cast<float>(_height) * 96.0f / _dpi) : static_cast<float>(_height);
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

bool ChromeHost::HitSplitter(float xDip, float yDip) const noexcept
{
    const float widthDip = (_dpi > 0.0f) ? (static_cast<float>(_width) * 96.0f / _dpi) : static_cast<float>(_width);
    const float heightDip = (_dpi > 0.0f) ? (static_cast<float>(_height) * 96.0f / _dpi) : static_cast<float>(_height);
    const float splitLeft = widthDip - _layersWidthDip - kSplitterWidthDip;
    const float top = kMenuHeightDip + kTabHeightDip;
    const float bottom = heightDip - kStatusHeightDip;
    return xDip >= splitLeft && xDip <= splitLeft + kSplitterWidthDip && yDip >= top && yDip <= bottom;
}

void ChromeHost::SetDocumentTitle(std::wstring title, bool dirty)
{
    if (_tabs && _tabs->GetTabCount() > 0)
    {
        if (dirty)
        {
            title += L" •";
        }
        _tabs->SetTabTitle(0, std::move(title));
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
        _header->SetText(EditorToolName(_tool));
    }
}

void ChromeHost::SetTool(EditorTool tool) noexcept
{
    _tool = tool;
    for (size_t index = 0; index < _toolButtons.size(); ++index)
    {
        if (_toolButtons[index])
        {
            _toolButtons[index]->SetChecked(ToolAt(index) == tool);
        }
    }
}

void ChromeHost::SetLayers(std::vector<HierarchyEntry> rows, const std::optional<Guid>& active)
{
    _treeModel.rows = std::move(rows);
    _treeModel.active = active;
    if (_layers)
    {
        _layers->NotifyDataChanged();
        if (active.has_value())
        {
            for (size_t index = 0; index < _treeModel.rows.size(); ++index)
            {
                if (_treeModel.rows[index].id == active.value())
                {
                    _layers->SetSelectedItemId(index + 1);
                    break;
                }
            }
        }
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
    const float xDip = (_dpi > 0.0f) ? (event.xPixels * 96.0f / _dpi) : event.xPixels;
    const float yDip = (_dpi > 0.0f) ? (event.yPixels * 96.0f / _dpi) : event.yPixels;
    if (event.action == DxUi::PointerAction::Down && HitSplitter(xDip, yDip))
    {
        _draggingSplitter = true;
        _dragStartX = xDip;
        _dragStartWidth = _layersWidthDip;
        return true;
    }
    if (_draggingSplitter)
    {
        if (event.action == DxUi::PointerAction::Move || event.action == DxUi::PointerAction::Down)
        {
            const float widthDip =
                (_dpi > 0.0f) ? (static_cast<float>(_width) * 96.0f / _dpi) : static_cast<float>(_width);
            _layersWidthDip = std::min(kLayersMaxDip, std::max(kLayersMinDip, _dragStartWidth - (xDip - _dragStartX)));
            (void)widthDip;
            Arrange();
            return true;
        }
        if (event.action == DxUi::PointerAction::Up || event.action == DxUi::PointerAction::Leave)
        {
            _draggingSplitter = false;
            return true;
        }
    }
    const CanvasHole hole = HolePixels();
    const bool inHole = event.xPixels >= hole.left && event.yPixels >= hole.top &&
                        event.xPixels < hole.left + hole.width && event.yPixels < hole.top + hole.height;
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
    return _view.DispatchKey(virtualKey, down, modifiers);
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
