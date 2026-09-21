#pragma once

#include <windows.h>

#include <objbase.h> // `interface`, which WIN32_LEAN_AND_MEAN keeps out of windows.h and UIAutomationCore.h needs
#include <oleacc.h>

#include <UIAutomation.h>
#include <wil/com.h>

// The UIA root of the main window (Specs/UI/UI_ApplicationChrome.md, Accessibility): the HWND's own provider with
// the DxUi chrome's fragment root as its one child, so screen readers walk the menus, rail, headers, layers and
// panels. Everything runs on the UI thread (server-side provider with COM threading); Disconnect retires it.
class WindowAccessibilityRoot final : public IRawElementProviderSimple,
                                      public IRawElementProviderFragment,
                                      public IRawElementProviderFragmentRoot
{
  public:
    static HRESULT Create(HWND window, wil::com_ptr_nothrow<WindowAccessibilityRoot>& result) noexcept;

    // The chrome's fragment root (null until DxUi attached its accessibility).
    void SetChromeRoot(IRawElementProviderFragmentRoot* chrome) noexcept;
    // Whether keyboard focus is in the chrome (the canvas otherwise): GetFocus answers from the chrome only then.
    void SetChromeHasFocus(bool focused) noexcept;
    // Drops the window and the chrome; UIA calls afterwards report the element as unavailable.
    void Disconnect() noexcept;

    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) noexcept override;
    ULONG STDMETHODCALLTYPE AddRef() noexcept override;
    ULONG STDMETHODCALLTYPE Release() noexcept override;
    // IRawElementProviderSimple
    HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions* options) noexcept override;
    HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID pattern, IUnknown** provider) noexcept override;
    HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID property, VARIANT* value) noexcept override;
    HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(IRawElementProviderSimple** provider) noexcept override;
    // IRawElementProviderFragment
    HRESULT STDMETHODCALLTYPE Navigate(NavigateDirection direction,
                                       IRawElementProviderFragment** result) noexcept override;
    HRESULT STDMETHODCALLTYPE GetRuntimeId(SAFEARRAY** runtimeId) noexcept override;
    HRESULT STDMETHODCALLTYPE get_BoundingRectangle(UiaRect* rectangle) noexcept override;
    HRESULT STDMETHODCALLTYPE GetEmbeddedFragmentRoots(SAFEARRAY** roots) noexcept override;
    HRESULT STDMETHODCALLTYPE SetFocus() noexcept override;
    HRESULT STDMETHODCALLTYPE get_FragmentRoot(IRawElementProviderFragmentRoot** root) noexcept override;
    // IRawElementProviderFragmentRoot
    HRESULT STDMETHODCALLTYPE ElementProviderFromPoint(double x, double y,
                                                       IRawElementProviderFragment** result) noexcept override;
    HRESULT STDMETHODCALLTYPE GetFocus(IRawElementProviderFragment** result) noexcept override;

  private:
    explicit WindowAccessibilityRoot(HWND window) noexcept;
    ~WindowAccessibilityRoot() = default;

    LONG _references = 1;
    HWND _window = nullptr;
    wil::com_ptr_nothrow<IRawElementProviderFragmentRoot> _chrome;
    bool _chromeFocused = false;
};
