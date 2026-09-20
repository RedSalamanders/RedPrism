#pragma once

#include <d3d11.h>
#include <memory>
#include <windows.h>

#include <DxUi/Embedded.h>

class ChromeHost final
{
  public:
    ChromeHost() = default;
    ~ChromeHost();

    ChromeHost(const ChromeHost&) = delete;
    ChromeHost& operator=(const ChromeHost&) = delete;
    ChromeHost(ChromeHost&&) = delete;
    ChromeHost& operator=(ChromeHost&&) = delete;

    HRESULT Attach(ID3D11Device* device, void* requestContext, void (*requestFrame)(void*) noexcept) noexcept;
    HRESULT ReplaceDevice(ID3D11Device* device) noexcept;
    void Detach() noexcept;
    void SetVisible(bool visible) noexcept;
    HRESULT Prepare(UINT widthPixels, UINT heightPixels, float dpi) noexcept;
    HRESULT Composite(ID3D11DeviceContext* context, const D3D11_VIEWPORT& viewport) noexcept;
    bool DispatchPointer(const DxUi::PointerEvent& event) noexcept;
    bool DispatchKey(UINT virtualKey, bool down, UINT modifiers = 0) noexcept;
    bool DispatchCharacter(wchar_t character, UINT modifiers = 0) noexcept;
    bool AdvanceAnimation(uint64_t nowTickMs) noexcept;
    [[nodiscard]] bool NeedsPreparation() const noexcept;
    [[nodiscard]] bool NeedsAnimation() const noexcept;
    [[nodiscard]] DxUi::EmbeddedStatistics Statistics() const noexcept;
    [[nodiscard]] UINT ClickCount() const noexcept;

  private:
    HRESULT BindDevice(ID3D11Device* device) noexcept;
    HRESULT Build() noexcept;
    void Arrange() noexcept;

    DxUi::EmbeddedHost _view;
    std::shared_ptr<DxUi::GraphicsDevice> _graphics;
    void* _requestContext = nullptr;
    void (*_requestFrame)(void*) noexcept = nullptr;
    DxUi::Button* _hello = nullptr;
    UINT _width = 0;
    UINT _height = 0;
    float _dpi = 96.0f;
    UINT _clicks = 0;
    bool _attached = false;
};
