#include "AccessibilityRoot.h"

#include <new>

#pragma comment(lib, "uiautomationcore.lib")

WindowAccessibilityRoot::WindowAccessibilityRoot(HWND window) noexcept : _window(window) {}

HRESULT WindowAccessibilityRoot::Create(HWND window, wil::com_ptr_nothrow<WindowAccessibilityRoot>& result) noexcept
{
    result.reset();
    if (window == nullptr)
    {
        return E_INVALIDARG;
    }
    WindowAccessibilityRoot* root = new (std::nothrow) WindowAccessibilityRoot(window);
    if (root == nullptr)
    {
        return E_OUTOFMEMORY;
    }
    result.attach(root);
    return S_OK;
}

void WindowAccessibilityRoot::SetChromeRoot(IRawElementProviderFragmentRoot* chrome) noexcept
{
    _chrome = chrome;
}

void WindowAccessibilityRoot::SetChromeHasFocus(bool focused) noexcept
{
    _chromeFocused = focused;
}

void WindowAccessibilityRoot::Disconnect() noexcept
{
    _window = nullptr;
    _chrome.reset();
}

HRESULT WindowAccessibilityRoot::QueryInterface(REFIID riid, void** object) noexcept
{
    if (object == nullptr)
    {
        return E_POINTER;
    }
    if (riid == __uuidof(IUnknown) || riid == __uuidof(IRawElementProviderSimple))
    {
        *object = static_cast<IRawElementProviderSimple*>(this);
    }
    else if (riid == __uuidof(IRawElementProviderFragment))
    {
        *object = static_cast<IRawElementProviderFragment*>(this);
    }
    else if (riid == __uuidof(IRawElementProviderFragmentRoot))
    {
        *object = static_cast<IRawElementProviderFragmentRoot*>(this);
    }
    else
    {
        *object = nullptr;
        return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
}

ULONG WindowAccessibilityRoot::AddRef() noexcept
{
    return static_cast<ULONG>(InterlockedIncrement(&_references));
}

ULONG WindowAccessibilityRoot::Release() noexcept
{
    const LONG remaining = InterlockedDecrement(&_references);
    if (remaining == 0)
    {
        delete this;
    }
    return static_cast<ULONG>(remaining);
}

HRESULT WindowAccessibilityRoot::get_ProviderOptions(ProviderOptions* options) noexcept
{
    if (options == nullptr)
    {
        return E_POINTER;
    }
    *options = ProviderOptions_ServerSideProvider | ProviderOptions_UseComThreading;
    return S_OK;
}

HRESULT WindowAccessibilityRoot::GetPatternProvider(PATTERNID, IUnknown** provider) noexcept
{
    if (provider == nullptr)
    {
        return E_POINTER;
    }
    *provider = nullptr;
    return S_OK;
}

HRESULT WindowAccessibilityRoot::GetPropertyValue(PROPERTYID property, VARIANT* value) noexcept
{
    if (value == nullptr)
    {
        return E_POINTER;
    }
    VariantInit(value);
    if (property == UIA_ProviderDescriptionPropertyId)
    {
        value->bstrVal = SysAllocString(L"RedPrism window root (DxUi chrome host)");
        if (value->bstrVal != nullptr)
        {
            value->vt = VT_BSTR;
        }
    }
    else if (property == UIA_IsKeyboardFocusablePropertyId)
    {
        value->vt = VT_BOOL;
        value->boolVal = VARIANT_TRUE;
    }
    return S_OK;
}

HRESULT WindowAccessibilityRoot::get_HostRawElementProvider(IRawElementProviderSimple** provider) noexcept
{
    if (provider == nullptr)
    {
        return E_POINTER;
    }
    *provider = nullptr;
    if (_window == nullptr)
    {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    return UiaHostProviderFromHwnd(_window, provider);
}

HRESULT WindowAccessibilityRoot::Navigate(NavigateDirection direction, IRawElementProviderFragment** result) noexcept
{
    if (result == nullptr)
    {
        return E_POINTER;
    }
    *result = nullptr;
    if (_window == nullptr)
    {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    if ((direction == NavigateDirection_FirstChild || direction == NavigateDirection_LastChild) && _chrome)
    {
        return _chrome->QueryInterface(IID_PPV_ARGS(result));
    }
    return S_OK;
}

HRESULT WindowAccessibilityRoot::GetRuntimeId(SAFEARRAY** runtimeId) noexcept
{
    if (runtimeId == nullptr)
    {
        return E_POINTER;
    }
    *runtimeId = nullptr; // the window's root: UI Automation derives it from the HWND
    return S_OK;
}

HRESULT WindowAccessibilityRoot::get_BoundingRectangle(UiaRect* rectangle) noexcept
{
    if (rectangle == nullptr)
    {
        return E_POINTER;
    }
    *rectangle = UiaRect{}; // the window's root: UI Automation takes the HWND rectangle
    return S_OK;
}

HRESULT WindowAccessibilityRoot::GetEmbeddedFragmentRoots(SAFEARRAY** roots) noexcept
{
    if (roots == nullptr)
    {
        return E_POINTER;
    }
    *roots = nullptr;
    return S_OK;
}

HRESULT WindowAccessibilityRoot::SetFocus() noexcept
{
    if (_window == nullptr)
    {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    (void)::SetFocus(_window);
    return S_OK;
}

HRESULT WindowAccessibilityRoot::get_FragmentRoot(IRawElementProviderFragmentRoot** root) noexcept
{
    if (root == nullptr)
    {
        return E_POINTER;
    }
    *root = static_cast<IRawElementProviderFragmentRoot*>(this);
    AddRef();
    return S_OK;
}

HRESULT WindowAccessibilityRoot::ElementProviderFromPoint(double x, double y,
                                                          IRawElementProviderFragment** result) noexcept
{
    if (result == nullptr)
    {
        return E_POINTER;
    }
    *result = nullptr;
    if (_window == nullptr)
    {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    if (_chrome)
    {
        // The chrome answers for its controls; null leaves the point to the window itself (the canvas hole).
        const HRESULT hit = _chrome->ElementProviderFromPoint(x, y, result);
        if (SUCCEEDED(hit) && *result != nullptr)
        {
            return S_OK;
        }
        *result = nullptr;
    }
    return S_OK;
}

HRESULT WindowAccessibilityRoot::GetFocus(IRawElementProviderFragment** result) noexcept
{
    if (result == nullptr)
    {
        return E_POINTER;
    }
    *result = nullptr;
    if (_window == nullptr)
    {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    if (_chrome && _chromeFocused)
    {
        const HRESULT focus = _chrome->GetFocus(result);
        if (FAILED(focus))
        {
            *result = nullptr;
        }
    }
    return S_OK;
}
