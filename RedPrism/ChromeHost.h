#pragma once

#include "Document.h"
#include "EditorTool.h"

#include <d3d11.h>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <windows.h>

#include <DxUi/DxUi.h>
#include <DxUi/Embedded.h>
#include <DxUi/NativeMenuInterop.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

using unique_hmenu = wil::unique_any<HMENU, decltype(&::DestroyMenu), ::DestroyMenu>;

struct CanvasHole
{
    float left = 0;
    float top = 0;
    float width = 0;
    float height = 0;
};

class ChromeHost final
{
  public:
    static constexpr float kMenuHeightDip = 30.0f;
    static constexpr float kTabHeightDip = 32.0f;
    static constexpr float kHeaderHeightDip = 36.0f;
    static constexpr float kStatusHeightDip = 24.0f;
    static constexpr float kRailWidthDip = 48.0f;
    static constexpr float kSplitterWidthDip = 6.0f;
    static constexpr float kLayersMinDip = 160.0f;
    static constexpr float kLayersMaxDip = 480.0f;
    static constexpr float kLayersDefaultDip = 240.0f;

    static constexpr int kCommandNew = 100;
    static constexpr int kCommandOpen = 101;
    static constexpr int kCommandSave = 102;
    static constexpr int kCommandSaveAs = 103;
    static constexpr int kCommandExportPng = 104;
    static constexpr int kCommandExportJpeg = 105;
    static constexpr int kCommandUndo = 200;
    static constexpr int kCommandRedo = 201;

    ChromeHost() = default;
    ~ChromeHost();

    ChromeHost(const ChromeHost&) = delete;
    ChromeHost& operator=(const ChromeHost&) = delete;

    HRESULT Attach(ID3D11Device* device, void* requestContext, void (*requestFrame)(void*) noexcept) noexcept;
    HRESULT ReplaceDevice(ID3D11Device* device) noexcept;
    void Detach() noexcept;
    void SetVisible(bool visible) noexcept;
    void SetOwnerWindow(HWND window) noexcept;
    void SetCommandHandler(std::function<void(int)> handler);
    void SetToolHandler(std::function<void(EditorTool)> handler);
    void SetLayerHandler(std::function<void(Guid)> handler);
    void SetDocumentTitle(std::wstring title, bool dirty);
    void SetStatus(std::wstring zoom, std::wstring size, std::wstring tool);
    void SetTool(EditorTool tool) noexcept;
    void SetLayers(std::vector<HierarchyEntry> rows, const std::optional<Guid>& active);
    HRESULT Prepare(UINT widthPixels, UINT heightPixels, float dpi) noexcept;
    HRESULT Composite(ID3D11DeviceContext* context, const D3D11_VIEWPORT& viewport) noexcept;
    bool DispatchPointer(const DxUi::PointerEvent& event) noexcept;
    bool DispatchKey(UINT virtualKey, bool down, UINT modifiers = 0) noexcept;
    bool DispatchCharacter(wchar_t character, UINT modifiers = 0) noexcept;
    bool AdvanceAnimation(uint64_t nowTickMs) noexcept;
    [[nodiscard]] bool NeedsPreparation() const noexcept;
    [[nodiscard]] bool NeedsAnimation() const noexcept;
    [[nodiscard]] DxUi::EmbeddedStatistics Statistics() const noexcept;
    [[nodiscard]] CanvasHole HolePixels() const noexcept;
    [[nodiscard]] float LayersWidthDip() const noexcept
    {
        return _layersWidthDip;
    }
    [[nodiscard]] EditorTool Tool() const noexcept
    {
        return _tool;
    }
    [[nodiscard]] bool CanvasFocused() const noexcept
    {
        return _canvasFocused;
    }
    void SetCanvasFocused(bool focused) noexcept
    {
        _canvasFocused = focused;
    }

  private:
    class LayerTreeModel final : public DxUi::IDxTreeModel, public DxUi::IDxTreeDelegate
    {
      public:
        std::vector<HierarchyEntry> rows;
        std::optional<Guid> active;
        std::function<void(Guid)>* onLayer = nullptr;
        [[nodiscard]] size_t GetVisibleItemCount() const noexcept override;
        void GetVisibleItem(size_t visibleIndex, DxUi::TreeItemData& outItem) const override;
        void OnTreeSelectionChanged(uint64_t itemId) override;
    };

    HRESULT BindDevice(ID3D11Device* device) noexcept;
    HRESULT Build() noexcept;
    void Arrange() noexcept;
    void BuildMenus() noexcept;
    void OpenMenu(size_t index, POINT screen, bool keyboard) noexcept;
    [[nodiscard]] bool HitSplitter(float xDip, float yDip) const noexcept;

    DxUi::EmbeddedHost _view;
    std::shared_ptr<DxUi::GraphicsDevice> _graphics;
    void* _requestContext = nullptr;
    void (*_requestFrame)(void*) noexcept = nullptr;
    HWND _owner = nullptr;
    unique_hmenu _fileMenu;
    unique_hmenu _editMenu;
    DxUi::MenuBar* _menu = nullptr;
    DxUi::TabControl* _tabs = nullptr;
    DxUi::Toolbar* _rail = nullptr;
    DxUi::Label* _header = nullptr;
    DxUi::Tree* _layers = nullptr;
    DxUi::StatusStrip* _status = nullptr;
    DxUi::Label* _layersTitle = nullptr;
    std::vector<DxUi::Toggle*> _toolButtons;
    LayerTreeModel _treeModel;
    std::function<void(int)> _onCommand;
    std::function<void(EditorTool)> _onTool;
    std::function<void(Guid)> _onLayer;
    UINT _width = 0;
    UINT _height = 0;
    float _dpi = 96.0f;
    float _layersWidthDip = kLayersDefaultDip;
    bool _draggingSplitter = false;
    float _dragStartX = 0;
    float _dragStartWidth = kLayersDefaultDip;
    EditorTool _tool = EditorTool::Move;
    bool _canvasFocused = true;
    bool _attached = false;
};
