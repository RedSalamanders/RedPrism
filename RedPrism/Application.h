#pragma once

#include "Renderer.h"

#include <memory>
#include <string>
#include <windows.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

class ChromeHost;

class Application final
{
  public:
    Application(HINSTANCE instance, bool forceWarp, const wchar_t* screenshotPath) noexcept;
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;
    Application(Application&&) = delete;
    Application& operator=(Application&&) = delete;

    int Run(int showCommand) noexcept;
    int RunSelfTest() noexcept;

  private:
    static constexpr wchar_t kWindowClassName[] = L"RedPrism.Window";
    static constexpr UINT_PTR kScreenshotTimerId = 1;
    static LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) noexcept;
    static void RequestFrameThunk(void* context) noexcept;

    HRESULT RegisterWindowClass() noexcept;
    HRESULT CreateMainWindow(bool visible) noexcept;
    HRESULT RegisterDisplayPowerNotification(HWND window) noexcept;
    HRESULT InitializeRenderer() noexcept;
    HRESULT InitializeChrome() noexcept;
    void CloseMainWindow() noexcept;
    bool WaitUntilMessage() noexcept;
    bool WaitForFrameLatency() noexcept;
    void CheckDeviceAdapter() noexcept;
    void OnPointer(UINT message, WPARAM wParam, LPARAM lParam) noexcept;
    LRESULT HandleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam) noexcept;
    LRESULT OnSize(HWND window, UINT width, UINT height) noexcept;
    LRESULT OnDpiChanged(HWND window, const RECT* suggestedBounds) noexcept;

    HINSTANCE _instance = nullptr;
    wil::unique_hwnd _window;
    wil::unique_hpowernotify _displayPowerNotification;
    Renderer _renderer;
    std::unique_ptr<ChromeHost> _chrome;
    std::wstring _screenshotPath;
    bool _forceWarp = false;
    bool _classRegistered = false;
    bool _rendererReady = false;
    bool _windowVisible = false;
    bool _displayPoweredOn = true;
    bool _occlusionStatusChanged = false;
    bool _frameInvalidated = true;
    bool _mouseTracked = false;
    bool _screenshotArmed = false;
    HRESULT _runtimeFailure = S_OK;
    HRESULT _screenshotResult = S_OK;
};
