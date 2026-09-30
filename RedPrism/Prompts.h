#pragma once

#include <cstdint>
#include <string>

#include <windows.h>

class ChromeHost;

// User prompts and notices (Specs/UI/UI_ApplicationChrome.md): one small interface so Application's call sites
// never branch on whether this is a quiet run. `RealPrompts()` shows real UI; `QuietPrompts()` is installed for
// --self-test, --screenshot and --bench and answers every question with the value the caller supplies instead of
// asking, so a quiet run takes the same path a real answer would without ever waiting on the user. Application
// itself counts what each call means for the self-test (ShowError's `_errorsShown`, ReportBudgetRefusal's
// `_budgetNotices`, and so on, unconditionally, as it always has); this interface only decides whether, and how,
// to put the prompt in front of the user. Both implementations are stateless singletons: Application just points
// `_prompts` at one or the other, in the same places it used to set `_quietUi`.
class Prompts
{
  public:
    virtual ~Prompts() = default;

    // A notice card over the canvas (ChromeHost::ShowNotice). Returns whether it actually showed (a quiet run
    // never does, and neither does a call before the chrome attaches), so the caller knows whether to invalidate
    // the frame.
    virtual bool ShowNotice(ChromeHost* chrome, std::wstring text) const noexcept = 0;
    // Posts `stringId` as `message` to `window` so the notice never lands inside a DxUi dispatch or a modal loop;
    // the window procedure delivers it back through ShowNotice once the post is handled.
    virtual void PostNotice(HWND window, UINT message, UINT stringId, int formatValue = 0) const noexcept = 0;
    // A blocking error dialog naming `stringId`.
    virtual void ShowError(HWND window, UINT stringId) const noexcept = 0;
    // A blocking error dialog already formatted as `text` (the unsupported-project-version message).
    virtual void ShowFormattedError(HWND window, const std::wstring& text) const noexcept = 0;
    // An informational dialog (Help > About).
    virtual void ShowInfo(HWND window, const std::wstring& text, const std::wstring& caption) const noexcept = 0;

    enum class Answer : std::uint8_t
    {
        First,
        Second,
        Cancel,
    };
    // A two-choice question with Cancel; `first` is the default button. Shared by the save prompt
    // (Application::AskDiscardChanges) and the image-open prompt (Application::PromptImageOpen), as the two
    // already shared the TaskDialog helper that answers it. A quiet run answers `forced` instead of asking.
    [[nodiscard]] virtual Answer AskThreeWay(HWND window, const std::wstring& title, const std::wstring& text,
                                             const std::wstring& first, const std::wstring& second, bool warning,
                                             Answer forced) const noexcept = 0;
};

[[nodiscard]] const Prompts& RealPrompts() noexcept;
[[nodiscard]] const Prompts& QuietPrompts() noexcept;
// Allocates only when the posted notice is handled, never during worker completion.
[[nodiscard]] std::wstring FormatPostedNotice(UINT stringId, int formatValue) noexcept;
