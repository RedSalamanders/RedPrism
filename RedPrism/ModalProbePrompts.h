#pragma once

#include "Prompts.h"

#include <utility>

// Test-only Prompts facade for a synchronous modal-question probe. The callback context is borrowed only while
// AskThreeWay is running. The callback receiver and delegate must outlive this facade call, and all jobs must settle
// before destruction. Restore Application::_prompts before destroying this facade.
// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4626)
class ModalProbePrompts final : public Prompts
{
  public:
    using Callback = void (*)(void*) noexcept;

    ModalProbePrompts(const Prompts& delegate, void* context, Callback callback) noexcept
        : _delegate(delegate), _context(context), _callback(callback)
    {
    }

    ~ModalProbePrompts() override = default;

    bool ShowNotice(ChromeHost* chrome, std::wstring text) const noexcept override
    {
        return _delegate.ShowNotice(chrome, std::move(text));
    }

    void PostNotice(HWND window, UINT message, UINT stringId, int formatValue = 0) const noexcept override
    {
        _delegate.PostNotice(window, message, stringId, formatValue);
    }

    void ShowError(HWND window, UINT stringId) const noexcept override
    {
        _delegate.ShowError(window, stringId);
    }

    void ShowFormattedError(HWND window, const std::wstring& text) const noexcept override
    {
        _delegate.ShowFormattedError(window, text);
    }

    void ShowInfo(HWND window, const std::wstring& text, const std::wstring& caption) const noexcept override
    {
        _delegate.ShowInfo(window, text, caption);
    }

    [[nodiscard]] Answer AskThreeWay(HWND window, const std::wstring& title, const std::wstring& text,
                                     const std::wstring& first, const std::wstring& second, bool warning,
                                     Answer forced) const noexcept override
    {
        if (_callback != nullptr)
        {
            _callback(_context);
        }
        return _delegate.AskThreeWay(window, title, text, first, second, warning, forced);
    }

  private:
    const Prompts& _delegate;
    void* _context;
    Callback _callback;
};
#pragma warning(pop)
