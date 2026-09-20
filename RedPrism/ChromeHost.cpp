#include "ChromeHost.h"

#include "ThemePalette.h"

#include <new>

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
    _hello = nullptr;
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

HRESULT ChromeHost::Build() noexcept
{
    try
    {
        auto root = std::make_unique<DxUi::Panel>();
        _hello = root->AddChild<DxUi::Button>(L"Hello DxUi");
        _hello->SetPrimary(true);
        _hello->SetAccessibleName(L"Hello DxUi");
        _hello->SetAccessibleAutomationId(L"redprism.hello");
        _hello->SetOnClick([this] { ++_clicks; });
        _view.Controls().SetRoot(std::move(root));
        Arrange();
        return S_OK;
    }
    catch (const std::bad_alloc&)
    {
        _hello = nullptr;
        return E_OUTOFMEMORY;
    }
}

void ChromeHost::Arrange() noexcept
{
    if (!_hello)
    {
        return;
    }
    const float widthDip = (_dpi > 0.0f) ? (static_cast<float>(_width) * 96.0f / _dpi) : static_cast<float>(_width);
    const float heightDip = (_dpi > 0.0f) ? (static_cast<float>(_height) * 96.0f / _dpi) : static_cast<float>(_height);
    if (auto* root = _view.Controls().GetRoot())
    {
        root->SetBounds(D2D1::RectF(0.0f, 0.0f, widthDip, heightDip));
    }
    _hello->SetBounds(D2D1::RectF(16.0f, 16.0f, 176.0f, 52.0f));
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
    return _attached && _view.DispatchPointer(event);
}

bool ChromeHost::DispatchKey(UINT virtualKey, bool down, UINT modifiers) noexcept
{
    return _attached && _view.DispatchKey(virtualKey, down, modifiers);
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

UINT ChromeHost::ClickCount() const noexcept
{
    return _clicks;
}
