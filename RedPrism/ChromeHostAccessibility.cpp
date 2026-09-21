#include "ChromeHost.h"

#include "AccessibilityRoot.h"
#include "AdjustControls.h"

#include <memory>
#include <new>

#include <DxUi/EmbeddedAccessibility.h>

// The chrome's side of DxUi's accessibility site: the window root is the parent and fragment root of the embedded
// tree, a focus request goes to the HWND, and a UIA action asks for a frame the way pointer input does.
class ChromeAccessibilitySite final : public DxUi::EmbeddedAccessibilitySite
{
  public:
    ChromeAccessibilitySite(WindowAccessibilityRoot* root, HWND window, void* context,
                            void (*requestFrame)(void*) noexcept) noexcept
        : _root(root), _window(window), _context(context), _requestFrame(requestFrame)
    {
    }

    HRESULT Navigate(NavigateDirection direction, IRawElementProviderFragment** result) noexcept override
    {
        if (result == nullptr)
        {
            return E_POINTER;
        }
        *result = nullptr;
        if (direction == NavigateDirection_Parent && _root)
        {
            return _root->QueryInterface(IID_PPV_ARGS(result));
        }
        return S_OK;
    }

    HRESULT FragmentRoot(IRawElementProviderFragmentRoot** result) noexcept override
    {
        if (result == nullptr)
        {
            return E_POINTER;
        }
        *result = nullptr;
        if (!_root)
        {
            return UIA_E_ELEMENTNOTAVAILABLE;
        }
        return _root->QueryInterface(IID_PPV_ARGS(result));
    }

    HRESULT RequestFocus() noexcept override
    {
        if (_window == nullptr)
        {
            return UIA_E_ELEMENTNOTAVAILABLE;
        }
        (void)::SetFocus(_window);
        return S_OK;
    }

    void ActionCompleted() noexcept override
    {
        if (_requestFrame != nullptr)
        {
            _requestFrame(_context);
        }
    }

    void Disconnect() noexcept
    {
        _root.reset();
        _window = nullptr;
        _requestFrame = nullptr;
    }

  private:
    wil::com_ptr_nothrow<WindowAccessibilityRoot> _root;
    HWND _window = nullptr;
    void* _context = nullptr;
    void (*_requestFrame)(void*) noexcept = nullptr;
};

namespace
{
constexpr std::uint64_t kChromeAccessibilityRuntimeId = 1;

[[nodiscard]] const wchar_t* InteractiveKind(const DxUi::Control& control) noexcept
{
    if (dynamic_cast<const DxUi::Button*>(&control))
    {
        return L"Button";
    }
    if (dynamic_cast<const DxUi::Checkbox*>(&control))
    {
        return L"Checkbox";
    }
    if (dynamic_cast<const DxUi::ComboBox*>(&control))
    {
        return L"ComboBox";
    }
    if (dynamic_cast<const DxUi::NumericStepper*>(&control))
    {
        return L"NumericStepper";
    }
    if (dynamic_cast<const DxUi::Slider*>(&control))
    {
        return L"Slider";
    }
    if (dynamic_cast<const DxUi::ColorSwatch*>(&control))
    {
        return L"ColorSwatch";
    }
    if (dynamic_cast<const DxUi::ColorPicker*>(&control))
    {
        return L"ColorPicker";
    }
    if (dynamic_cast<const DxUi::Tree*>(&control))
    {
        return L"Tree";
    }
    if (dynamic_cast<const DxUi::TabControl*>(&control))
    {
        return L"TabControl";
    }
    if (dynamic_cast<const DxUi::Splitter*>(&control))
    {
        return L"Splitter";
    }
    if (dynamic_cast<const DxUi::MenuBar*>(&control))
    {
        return L"MenuBar";
    }
    if (dynamic_cast<const DxUi::Toolbar*>(&control))
    {
        return L"Toolbar";
    }
    if (dynamic_cast<const CurveEditor*>(&control))
    {
        return L"CurveEditor";
    }
    if (dynamic_cast<const LevelsBar*>(&control))
    {
        return L"LevelsBar";
    }
    if (dynamic_cast<const SpectrumEditor*>(&control))
    {
        return L"SpectrumEditor";
    }
    if (dynamic_cast<const GradientRamp*>(&control))
    {
        return L"GradientRamp";
    }
    return nullptr;
}

// A button or a checkbox is named by its caption; everything else interactive needs an explicit accessible name.
[[nodiscard]] bool HasAccessibleName(const DxUi::Control& control) noexcept
{
    if (!control.GetAccessibleName().empty())
    {
        return true;
    }
    if (const auto* button = dynamic_cast<const DxUi::Button*>(&control))
    {
        return !button->GetText().empty();
    }
    if (const auto* checkbox = dynamic_cast<const DxUi::Checkbox*>(&control))
    {
        return !checkbox->GetText().empty();
    }
    return false;
}

void CollectUnnamed(const DxUi::Control& control, const std::wstring& context, std::vector<std::wstring>& out)
{
    const wchar_t* kind = InteractiveKind(control);
    if (kind != nullptr && !HasAccessibleName(control))
    {
        out.push_back(std::wstring{kind} + L" under " + context);
    }
    const std::wstring here = !control.GetAccessibleName().empty() ? std::wstring{control.GetAccessibleName()}
                              : kind != nullptr                    ? std::wstring{kind}
                                                                   : context;
    if (const auto* panel = dynamic_cast<const DxUi::Panel*>(&control))
    {
        for (const std::unique_ptr<DxUi::Control>& child : panel->GetChildren())
        {
            if (child)
            {
                CollectUnnamed(*child, here, out);
            }
        }
    }
}
} // namespace

void ChromeHost::AttachAccessibility() noexcept
{
    DetachAccessibility();
    if (_owner == nullptr || !_attached)
    {
        return;
    }
    wil::com_ptr_nothrow<WindowAccessibilityRoot> root;
    _accessibilityStatus = WindowAccessibilityRoot::Create(_owner, root);
    if (FAILED(_accessibilityStatus))
    {
        return;
    }
    std::shared_ptr<ChromeAccessibilitySite> site{
        new (std::nothrow) ChromeAccessibilitySite(root.get(), _owner, _requestContext, _requestFrame)};
    if (!site)
    {
        _accessibilityStatus = E_OUTOFMEMORY;
        return;
    }
    _accessibilityStatus = _view.AttachAccessibility(site, kChromeAccessibilityRuntimeId, AccessibilityPlacement());
    if (FAILED(_accessibilityStatus))
    {
        site->Disconnect();
        return;
    }
    wil::com_ptr_nothrow<IRawElementProviderFragmentRoot> chrome;
    _accessibilityStatus = _view.GetAccessibilityProvider(chrome.put());
    if (SUCCEEDED(_accessibilityStatus) && chrome)
    {
        root->SetChromeRoot(chrome.get());
    }
    root->SetChromeHasFocus(!_canvasFocused && GetFocus() == _owner);
    _accessibilityRoot = std::move(root);
    _accessibilitySite = std::move(site);
}

void ChromeHost::DetachAccessibility() noexcept
{
    if (!_accessibilityRoot && !_accessibilitySite)
    {
        return;
    }
    if (_owner != nullptr && _accessibilityRoot)
    {
        // Clients holding the old root learn it is gone; the window no longer answers WM_GETOBJECT with it.
        (void)UiaReturnRawElementProvider(_owner, 0, 0, nullptr);
    }
    _view.DisconnectAccessibility();
    if (_accessibilitySite)
    {
        _accessibilitySite->Disconnect();
    }
    if (_accessibilityRoot)
    {
        _accessibilityRoot->Disconnect();
        (void)UiaDisconnectProvider(static_cast<IRawElementProviderSimple*>(_accessibilityRoot.get()));
    }
    _accessibilityRoot.reset();
    _accessibilitySite.reset();
}

LRESULT ChromeHost::HandleGetObject(WPARAM wParam, LPARAM lParam) noexcept
{
    if (!_accessibilityRoot || _owner == nullptr || static_cast<LONG>(lParam) != UiaRootObjectId)
    {
        return 0;
    }
    return UiaReturnRawElementProvider(_owner, wParam, lParam,
                                       static_cast<IRawElementProviderSimple*>(_accessibilityRoot.get()));
}

DxUi::EmbeddedAccessibilityPlacement ChromeHost::AccessibilityPlacement() const noexcept
{
    DxUi::EmbeddedAccessibilityPlacement placement{};
    RECT client{};
    POINT origin{};
    if (_owner != nullptr && GetClientRect(_owner, &client) && ClientToScreen(_owner, &origin))
    {
        placement.viewport.left = origin.x;
        placement.viewport.top = origin.y;
        placement.viewport.width = client.right - client.left;
        placement.viewport.height = client.bottom - client.top;
    }
    placement.hasKeyboardFocus = _owner != nullptr && !_canvasFocused && GetFocus() == _owner;
    return placement;
}

void ChromeHost::UpdateAccessibilityPlacement() noexcept
{
    if (!_accessibilitySite || !_attached)
    {
        return;
    }
    const DxUi::EmbeddedAccessibilityPlacement placement = AccessibilityPlacement();
    (void)_view.UpdateAccessibility(placement);
    if (_accessibilityRoot)
    {
        _accessibilityRoot->SetChromeHasFocus(placement.hasKeyboardFocus);
    }
}

IRawElementProviderFragmentRoot* ChromeHost::AccessibilityRoot() const noexcept
{
    return _accessibilityRoot ? static_cast<IRawElementProviderFragmentRoot*>(_accessibilityRoot.get()) : nullptr;
}

std::vector<std::wstring> ChromeHost::UnnamedInteractiveControls() const
{
    std::vector<std::wstring> gaps;
    if (!_attached)
    {
        return gaps;
    }
    if (const DxUi::Control* root = _view.Controls().GetRoot())
    {
        CollectUnnamed(*root, L"root", gaps);
    }
    return gaps;
}
