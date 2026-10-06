#include "Application.h"

#include "../Tests/Support/DocumentFixture.h"

#include "Adjust.h"
#include "ChromeHost.h"
#include "FileDialogs.h"
#include "ImageCodecs.h"
#include "KeyboardState.h"
#include "ProjectSaveTestGate.h"
#include "ProjectStore.h"
#include "Prompts.h"
#include "Resources.h"
#include "Selection.h"
#include "SystemClipboard.h"
#include "resource.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <format>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <commctrl.h>
#include <shlobj.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

// The file side of Application (C16, Specs/UI/UI_ApplicationChrome.md "Files" and "Unimplemented functions"): an
// image opened as a layer or as a new project, the save prompt before a document goes, Import Images, Close
// Project, the notice for a function that has no implementation yet, and the self-test that drives them.

namespace
{
constexpr int kTaskButtonFirst = 100;
constexpr int kTaskButtonSecond = 101;

// A menu caption as a plain name: no mnemonic, no accelerator column, no ellipsis (Common/Resources.h).
[[nodiscard]] std::wstring CaptionName(UINT stringId)
{
    return PlainCommandName(LoadStringResource(nullptr, stringId));
}

// A two-answer question with Cancel; `first` is the default. Returns kTaskButtonFirst, kTaskButtonSecond or
// IDCANCEL. Without common controls v6 a message box asks the same question (Yes = first, No = second).
[[nodiscard]] int AskThreeWay(HWND owner, const std::wstring& title, const std::wstring& text,
                              const std::wstring& first, const std::wstring& second, bool warning) noexcept
{
    const std::wstring caption = LoadStringResource(nullptr, IDS_APP_TITLE);
    const TASKDIALOG_BUTTON buttons[] = {{kTaskButtonFirst, first.c_str()}, {kTaskButtonSecond, second.c_str()}};
    TASKDIALOGCONFIG config{};
    config.cbSize = sizeof(config);
    config.hwndParent = owner;
    config.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW;
    config.dwCommonButtons = TDCBF_CANCEL_BUTTON;
    config.pszWindowTitle = caption.c_str();
    config.pszMainIcon = warning ? TD_WARNING_ICON : nullptr;
    config.pszMainInstruction = title.c_str();
    config.pszContent = text.c_str();
    config.cButtons = static_cast<UINT>(std::size(buttons));
    config.pButtons = buttons;
    config.nDefaultButton = kTaskButtonFirst;
    int pressed = 0;
    if (SUCCEEDED(TaskDialogIndirect(&config, &pressed, nullptr, nullptr)))
    {
        return pressed == kTaskButtonFirst || pressed == kTaskButtonSecond ? pressed : IDCANCEL;
    }
    const std::wstring legend = FormatStringResource(nullptr, IDS_QUESTION_FALLBACK, first, second);
    const std::wstring fallback = title + L"\n\n" + text + L"\n\n" + legend;
    switch (MessageBoxW(owner, fallback.c_str(), caption.c_str(),
                        MB_YESNOCANCEL | (warning ? MB_ICONWARNING : MB_ICONQUESTION)))
    {
    case IDYES:
        return kTaskButtonFirst;
    case IDNO:
        return kTaskButtonSecond;
    default:
        return IDCANCEL;
    }
}

// The production Prompts implementation: every call shows real UI.
class RealPromptsImpl final : public Prompts
{
  public:
    bool ShowNotice(ChromeHost* chrome, std::wstring text) const noexcept override
    {
        if (chrome == nullptr)
        {
            return false;
        }
        chrome->ShowNotice(std::move(text));
        return true;
    }

    void PostNotice(HWND window, UINT message, UINT stringId, int formatValue) const noexcept override
    {
        if (window != nullptr)
        {
            (void)PostMessageW(window, message, static_cast<WPARAM>(stringId), static_cast<LPARAM>(formatValue));
        }
    }

    void ShowError(HWND window, UINT stringId) const noexcept override
    {
        if (window == nullptr)
        {
            return;
        }
        MessageBoxW(window, LoadStringResource(nullptr, stringId).c_str(),
                    LoadStringResource(nullptr, IDS_APP_TITLE).c_str(), MB_OK | MB_ICONERROR);
    }

    void ShowFormattedError(HWND window, const std::wstring& text) const noexcept override
    {
        if (window == nullptr)
        {
            return;
        }
        MessageBoxW(window, text.c_str(), LoadStringResource(nullptr, IDS_APP_TITLE).c_str(), MB_OK | MB_ICONERROR);
    }

    void ShowInfo(HWND window, const std::wstring& text, const std::wstring& caption) const noexcept override
    {
        if (window == nullptr)
        {
            return;
        }
        MessageBoxW(window, text.c_str(), caption.c_str(), MB_OK | MB_ICONINFORMATION);
    }

    [[nodiscard]] Answer AskThreeWay(HWND window, const std::wstring& title, const std::wstring& text,
                                     const std::wstring& first, const std::wstring& second, bool warning,
                                     Answer forced) const noexcept override
    {
        if (window == nullptr)
        {
            return forced;
        }
        switch (::AskThreeWay(window, title, text, first, second, warning))
        {
        case kTaskButtonFirst:
            return Answer::First;
        case kTaskButtonSecond:
            return Answer::Second;
        default:
            return Answer::Cancel;
        }
    }
};

// The quiet implementation (--self-test, --screenshot, --bench): never shows anything, always answers `forced`.
// Application itself still counts what each call meant (ShowError's `_errorsShown` and so on), unconditionally, as
// it always has; this class only ever suppresses the UI.
class QuietPromptsImpl final : public Prompts
{
  public:
    bool ShowNotice(ChromeHost*, std::wstring) const noexcept override
    {
        return false;
    }
    void PostNotice(HWND, UINT, UINT, int) const noexcept override {}
    void ShowError(HWND, UINT) const noexcept override {}
    void ShowFormattedError(HWND, const std::wstring&) const noexcept override {}
    void ShowInfo(HWND, const std::wstring&, const std::wstring&) const noexcept override {}
    [[nodiscard]] Answer AskThreeWay(HWND, const std::wstring&, const std::wstring&, const std::wstring&,
                                     const std::wstring&, bool, Answer forced) const noexcept override
    {
        return forced;
    }
};

// Another thread holding the clipboard open, as another app does while it reads or writes: a copy's write fails.
struct ClipboardHold
{
    wil::unique_event_nothrow opened;
    wil::unique_event_nothrow release;
    HWND window = nullptr; // borrowed while the holder thread waits; published by opened
    bool held = false;
    bool closed = false;
    DWORD released = WAIT_FAILED;
};

DWORD WINAPI HoldClipboard(void* context) noexcept
{
    auto* hold = static_cast<ClipboardHold*>(context);
    // A null opener can be reopened and closed by this process's asynchronous prefill reader. Use a distinct
    // window so the fixture models another window's exclusive hold throughout every product retry.
    wil::unique_hwnd window(CreateWindowExW(0, L"STATIC", L"RedPrism clipboard hold", 0, 0, 0, 0, 0, HWND_MESSAGE,
                                            nullptr, GetModuleHandleW(nullptr), nullptr));
    hold->window = window.get();
    // A clipboard manager may read the clipboard for a moment after each change: wait for it, boundedly.
    for (int attempt = 0; attempt < SystemClipboard::kHarnessAttempts && !hold->held; ++attempt)
    {
        hold->held = window && OpenClipboard(window.get()) != FALSE;
        if (!hold->held)
        {
            Sleep(10);
        }
    }
    hold->opened.SetEvent();
    hold->released = WaitForSingleObject(hold->release.get(), 10000);
    if (hold->held)
    {
        hold->closed = CloseClipboard() != FALSE;
    }
    return 0;
}

} // namespace

const Prompts& RealPrompts() noexcept
{
    static const RealPromptsImpl instance;
    return instance;
}

const Prompts& QuietPrompts() noexcept
{
    static const QuietPromptsImpl instance;
    return instance;
}

std::wstring FormatPostedNotice(UINT stringId, int formatValue) noexcept
{
    return stringId == IDS_ERROR_UNSUPPORTED_VERSION
               ? FormatStringResource(nullptr, IDS_ERROR_UNSUPPORTED_VERSION, formatValue)
               : LoadStringResource(nullptr, stringId);
}

void Application::ShowUnavailable(UINT captionId) noexcept
{
    const std::wstring text = FormatStringResource(nullptr, IDS_CMD_UNAVAILABLE, CaptionName(captionId));
    _prompts->ShowNotice(_chrome.get(), text);
}

Application::ImageOpenChoice Application::PromptImageOpen(const std::wstring& firstName, std::size_t count) noexcept
{
    if (!_session.Document().has_value())
    {
        return ImageOpenChoice::NewProject;
    }
    const std::wstring title = count > 1 ? FormatStringResource(nullptr, IDS_OPEN_IMAGES_TITLE, count)
                                         : FormatStringResource(nullptr, IDS_OPEN_IMAGE_TITLE, firstName);
    const Prompts::Answer forced = _forcedImageOpen == ImageOpenChoice::NewProject ? Prompts::Answer::Second
                                   : _forcedImageOpen == ImageOpenChoice::Cancel   ? Prompts::Answer::Cancel
                                                                                   : Prompts::Answer::First;
    switch (_prompts->AskThreeWay(_window.get(), title, LoadStringResource(nullptr, IDS_OPEN_IMAGE_TEXT),
                                  LoadStringResource(nullptr, IDS_OPEN_IMAGE_ADD_LAYER),
                                  LoadStringResource(nullptr, IDS_OPEN_IMAGE_NEW_PROJECT), false, forced))
    {
    case Prompts::Answer::First:
        return ImageOpenChoice::AddLayer;
    case Prompts::Answer::Second:
        return ImageOpenChoice::NewProject;
    default:
        return ImageOpenChoice::Cancel;
    }
}

bool Application::HasUnsavedWork() const noexcept
{
    if (!_session.Document().has_value())
    {
        return false;
    }
    if (_session.history.IsModified() || _session.HasColorEdit() ||
        (_canvasBrushInput.Busy() && _canvasBrushReceipt.Read().outcome == HostOutcome::Pending))
    {
        return true;
    }
    // Pending edits are not history entries yet, but Save records them (SettleForWrite).
    if (_opacityEdit.has_value() && _opacityEdit->shown != _opacityEdit->start &&
        std::any_of(_session.Document()->layers.begin(), _session.Document()->layers.end(), [&](const ImageLayer& layer)
                    { return layer.id == _opacityEdit->layer && layer.opacity == _opacityEdit->shown; }))
    {
        return true;
    }
    if (_session.PendingBrush() != nullptr || _session.PendingWarp() != nullptr ||
        _session.PendingGradient() != nullptr)
    {
        return true;
    }
    const std::optional<TransformEdit>& edit = _session.PendingTransformEdit();
    if (!edit.has_value())
    {
        return false;
    }
    if (edit->corners.has_value() || edit->group.has_value() || edit->floating.has_value() || edit->mask)
    {
        return true;
    }
    for (const ImageLayer& layer : _session.Document()->layers)
    {
        if (layer.id == edit->layerID)
        {
            return !(layer.transform == edit->draft);
        }
    }
    return true;
}

bool Application::SettleForWrite() noexcept
{
    // An adjustment layer's panel previews inside the document: the file must not hold a value Cancel would undo. A
    // live stroke or warp, a pending gradient and a pending transform record their entries (EditEnd::Write).
    const auto generation = _session.Generation();
    CloseOpacityEdit();
    EndCanvasEdits(EditEnd::Write);
    _session.ClearSnapGuides();
    if (_session.Generation() != generation)
        DocumentChanged();
    else
    {
        SyncChrome();
        _frameInvalidated = true; // cleared guides/chrome do not require recomposing settled pixels
    }
    return !_session.HasColorEdit();
}

Prompts::Answer Application::AskDiscardChanges() noexcept
{
    if (!_session.Document() || !HasUnsavedWork())
    {
        return Prompts::Answer::Second;
    }
    const auto forced = _forcedDiscardAnswer.value_or(_forcedDiscard.value_or(true) ? Prompts::Answer::Second
                                                                                    : Prompts::Answer::Cancel);
    return _prompts->AskThreeWay(
        _window.get(), FormatStringResource(nullptr, IDS_CONFIRM_SAVE_TITLE, FileTitle(_documentPath)),
        LoadStringResource(nullptr, IDS_CONFIRM_SAVE_TEXT), LoadStringResource(nullptr, IDS_CONFIRM_SAVE_SAVE),
        LoadStringResource(nullptr, IDS_CONFIRM_SAVE_DISCARD), true, forced);
}

HRESULT Application::SelectHostOutput(HostWriteFormat format, bool saveAs) noexcept
{
    if (!_activeHostRequest || !_session.Document())
    {
        return E_UNEXPECTED;
    }
    return SelectHostOutput(_activeHostRequest->input, format, saveAs);
}

HRESULT Application::SelectHostOutput(HostRequest& request, HostWriteFormat format, bool saveAs) noexcept
{
    if (!_session.Document())
        return E_UNEXPECTED;
    try
    {
        if (const auto* write = std::get_if<HostWriteInput>(&request.Input()); write && write->path)
        {
            return request.TrySelectOutput(write->path.value(), _hostInputBudget)
                       ? S_OK
                       : HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA);
        }
        if (format == HostWriteFormat::Project && !saveAs && !_documentPath.empty())
        {
            return request.TrySelectOutput(_documentPath, _hostInputBudget)
                       ? S_OK
                       : HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA);
        }
        if (_quietUi || !_window)
        {
            return HRESULT_FROM_WIN32(ERROR_CANCELLED);
        }
        const auto strings = LoadFilterStrings();
        const COMDLG_FILTERSPEC filters[] = {{format == HostWriteFormat::Project ? strings.project.c_str()
                                              : format == HostWriteFormat::Jpeg  ? strings.jpeg.c_str()
                                                                                 : strings.png.c_str(),
                                              format == HostWriteFormat::Project ? L"*.prism"
                                              : format == HostWriteFormat::Jpeg  ? L"*.jpg;*.jpeg"
                                                                                 : L"*.png"},
                                             {strings.exploded.c_str(), L"*.*"}};
        const auto name = LoadStringResource(nullptr, format == HostWriteFormat::Project ? IDS_DEFAULT_PROJECT_NAME
                                                      : format == HostWriteFormat::Jpeg  ? IDS_DEFAULT_EXPORT_JPEG
                                                                                         : IDS_DEFAULT_EXPORT_PNG);
        return PickOutputForRequest(_window.get(), name.c_str(),
                                    format == HostWriteFormat::Project ? L"prism"
                                    : format == HostWriteFormat::Jpeg  ? L"jpg"
                                                                       : L"png",
                                    filters, format == HostWriteFormat::Project ? 2u : 1u, request, _hostInputBudget);
    }
    catch (const std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }
}

HRESULT Application::WriteHostOutput(HostWriteFormat format) noexcept
{
    if (!_activeHostRequest || !_session.Document() || _session.HasColorEdit())
    {
        return E_UNEXPECTED;
    }
    _hostExecuting = true;
    const auto leave = wil::scope_exit([this] { _hostExecuting = false; });
    if (!SettleForWrite() || !_session.Document())
    {
        return E_FAIL;
    }
    if (format == HostWriteFormat::Project)
        return StartProjectSave();
    return StartExportReadback(format);
}

bool Application::ExecuteHostInput() noexcept
{
    if (!_activeHostRequest)
    {
        return false;
    }
    _hostExecuting = true;
    const auto leave = wil::scope_exit([this] { _hostExecuting = false; });
    const auto& input = _activeHostRequest->input.Input();
    return std::visit(
        [this](const auto& value) noexcept -> bool
        {
            using Input = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<Input, HostPanelOkInput> || std::is_same_v<Input, HostPanelCancelInput>)
            {
                return !_session.HasColorEdit();
            }
            else if constexpr (std::is_same_v<Input, HostGradientApplyInput>)
            {
                return _session.PendingGradient() == nullptr;
            }
            else if constexpr (std::is_same_v<Input, HostPixelMoveApplyInput>)
            {
                return _session.PendingPixelMove() == nullptr;
            }
            else if constexpr (std::is_same_v<Input, HostTransformFinishInput>)
            {
                return !_session.PendingTransformEdit();
            }
            else if constexpr (std::is_same_v<Input, HostDuplicateBeginInput>)
            {
                if (!_duplicateGesture || _duplicateGesture->serial != value.gesture ||
                    _duplicateGesture->documentSerial != _session.DocumentSerial() ||
                    _session.ActiveLayerID() != value.layer || _session.PendingTransformEdit())
                {
                    _hostExecutionError = E_ABORT;
                    return false;
                }
                _hostExecutionError = StartTransformWork(TransformWorkKind::OpenDuplicate);
                if (FAILED(_hostExecutionError))
                {
                    ++_errorsShown;
                    _lastErrorId = _hostExecutionError == HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE)
                                       ? IDS_ERROR_TOO_LARGE
                                       : IDS_ERROR_PIXEL_EDIT;
                    _prompts->PostNotice(_window.get(), kNoticeMessage, _lastErrorId);
                }
                return SUCCEEDED(_hostExecutionError);
            }
            else if constexpr (std::is_same_v<Input, HostDuplicateFinishInput>)
            {
                return !_session.PendingTransformEdit();
            }
            else if constexpr (std::is_same_v<Input, HostSelectionNudgeInput>)
            {
                _hostExecutionError = StartSelectionNudge(value.dx, value.dy);
                if (FAILED(_hostExecutionError))
                    _prompts->PostNotice(_window.get(), kNoticeMessage, IDS_ERROR_SELECTION_EDIT);
            }
            else if constexpr (std::is_same_v<Input, HostPixelNudgeInput>)
            {
                ExecutePixelNudge(value.offset);
            }
            else if constexpr (std::is_same_v<Input, HostLayerNudgeInput>)
            {
                ExecuteLayerNudge(value.offset);
            }
            else if constexpr (std::is_same_v<Input, HostPathsInput>)
            {
                if (_hostPathIndex < value.paths.size())
                {
                    const auto result = StartProjectOpen(value.paths[_hostPathIndex]);
                    if (FAILED(result))
                    {
                        _hostBatchFailed = true;
                        if (!FAILED(_hostBatchError))
                            _hostBatchError = result;
                        ++_errorsShown;
                        _lastErrorId = IDS_ERROR_OPEN_PROJECT;
                        _prompts->PostNotice(_window.get(), kNoticeMessage, _lastErrorId);
                    }
                    return true; // preserve the document on refusal and continue later project paths
                }
                std::array<std::wstring_view, HostInputBudget::kMaxPaths> paths{};
                std::size_t count = 0;
                for (std::size_t index = 0; index < value.paths.size(); ++index)
                {
                    const auto& path = value.paths[index];
                    if (!path.empty() && (value.action != HostPathAction::OpenPaths || !HostPathIsProject(index)))
                    {
                        paths[count++] = path;
                    }
                }
                _hostExecutionError =
                    StartImageImport(std::span<const std::wstring_view>(paths.data(), count), _hostImportNewProject,
                                     _hostSawProject ? std::nullopt : value.dropPoint);
                if (FAILED(_hostExecutionError))
                {
                    ++_errorsShown;
                    _lastErrorId = IDS_ERROR_IMPORT_IMAGE;
                    _prompts->PostNotice(_window.get(), kNoticeMessage, _lastErrorId);
                }
                return SUCCEEDED(_hostExecutionError);
            }
            else if constexpr (std::is_same_v<Input, HostNewInput>)
            {
                NewDocument();
            }
            else if constexpr (std::is_same_v<Input, HostCloseProjectInput>)
            {
                CloseProject();
            }
            else if constexpr (std::is_same_v<Input, HostCloseWindowInput> ||
                               std::is_same_v<Input, HostEndSessionInput>)
            {
                if constexpr (std::is_same_v<Input, HostEndSessionInput>)
                {
                    if (!_endSessionPending || value.generation != _endSessionGeneration)
                        return false;
                    _endSessionPending = false;
                    if (_window)
                        (void)ShutdownBlockReasonDestroy(_window.get());
                }
                _closeConfirmed = true;
                if (_window)
                    (void)PostMessageW(_window.get(), WM_CLOSE, 0, 0);
            }
            else if constexpr (std::is_same_v<Input, HostSizeApplyInput>)
            {
                if (value.invocation != _sizeInvocation || !SizeCardOpen())
                    return false;
                _sizeDialog = value.draft;
                ApplySizeCard(value.extensionArgb);
            }
            else if constexpr (std::is_same_v<Input, HostCommandInput>)
            {
                OnCommand(value.command);
            }
            else if constexpr (std::is_same_v<Input, HostWandLayerNewInput>)
            {
                _hostExecutionError = value.settings.sampleAllLayers ? StartWandCompositeSelection(value)
                                                                     : StartWandLayerSelection(value);
                if (FAILED(_hostExecutionError))
                    _prompts->PostNotice(_window.get(), kNoticeMessage, IDS_ERROR_WAND_MEMORY);
            }
            else if constexpr (std::is_same_v<Input, HostSelectionMoveCloseInput>)
            {
                _hostAfterPrerequisite = HostStage::Complete;
                _hostExecutionError = StartSelectionMoveClose(value.opening, value.cancel);
                if (FAILED(_hostExecutionError))
                    _prompts->PostNotice(_window.get(), kNoticeMessage, IDS_ERROR_SELECTION_EDIT);
            }
            else if constexpr (std::is_same_v<Input, HostSelectionMoveClickInput>)
            {
                _hostAfterPrerequisite = HostStage::SelectionMoveFollowup;
                _hostExecutionError = StartSelectionMoveClose(value.opening, false);
                if (FAILED(_hostExecutionError))
                    _prompts->PostNotice(_window.get(), kNoticeMessage, IDS_ERROR_SELECTION_EDIT);
            }
            else if constexpr (std::is_same_v<Input, HostSelectionMoveHeaderInput>)
            {
                _hostAfterPrerequisite = HostStage::SelectionHeaderFollowup;
                _hostExecutionError = StartSelectionMoveClose(value.opening, false);
                if (FAILED(_hostExecutionError))
                    _prompts->PostNotice(_window.get(), kNoticeMessage, IDS_ERROR_SELECTION_EDIT);
            }
            else if constexpr (std::is_same_v<Input, HostToolInput>)
            {
                OnTool(value.tool);
            }
            else if constexpr (std::is_same_v<Input, HostShapeFinishInput>)
            {
                _hostExecutionError = StartShape(value.draft, value.foregroundArgb);
                if (FAILED(_hostExecutionError))
                {
                    ++_errorsShown;
                    _lastErrorId = _hostExecutionError == HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE)
                                       ? IDS_ERROR_TOO_LARGE
                                       : IDS_ERROR_PIXEL_EDIT;
                    _prompts->PostNotice(_window.get(), kNoticeMessage, _lastErrorId);
                }
            }
            else if constexpr (std::is_same_v<Input, HostLayerSelectInput>)
            {
                OnLayerSelected(value.id);
            }
            else if constexpr (std::is_same_v<Input, HostLayerTargetInput>)
            {
                TargetLayer(value.id, value.mask, value.toggleMask);
            }
            else if constexpr (std::is_same_v<Input, HostLayerInvokeInput>)
            {
                OnLayerInvoked(value.id);
            }
            else if constexpr (std::is_same_v<Input, HostLayerReorderInput>)
            {
                OnLayerReordered(value.source, value.target, value.parent, value.place);
            }
            else if constexpr (std::is_same_v<Input, HostLayerRenameInput>)
            {
                if (value.name)
                    _hostExecutionError = StartLayerMetadata(LayerMetadataKind::Rename, value.id, value.name.value());
                if (FAILED(_hostExecutionError))
                    _prompts->PostNotice(_window.get(), kNoticeMessage, IDS_ERROR_PIXEL_EDIT);
            }
            else if constexpr (std::is_same_v<Input, HostLayerOpacityInput>)
            {
                if (_session.ActiveLayerID() != value.id)
                    return false;
                ExecuteLayerOpacityInput(value);
            }
            else
            {
                return false;
            }
            if constexpr (!std::is_same_v<Input, HostPanelOkInput> && !std::is_same_v<Input, HostPanelCancelInput> &&
                          !std::is_same_v<Input, HostGradientApplyInput> &&
                          !std::is_same_v<Input, HostPixelMoveApplyInput> && !std::is_same_v<Input, HostPathsInput> &&
                          !std::is_same_v<Input, HostDuplicateBeginInput> &&
                          !std::is_same_v<Input, HostDuplicateFinishInput> &&
                          !std::is_same_v<Input, HostTransformFinishInput> && !std::is_same_v<Input, HostWriteInput> &&
                          !std::is_same_v<Input, HostCanvasBrushInput>)
            {
                return true;
            }
        },
        input);
}

void Application::OpenPath(const wchar_t* path) noexcept
{
    if (path == nullptr || path[0] == L'\0')
    {
        return;
    }
    const auto view = HostBoundedPathView(path);
    if (!view)
    {
        ReportHostAdmission(ContinuationReceipt::Failure(HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA)));
        return;
    }
    const std::array<std::wstring_view, 1> paths{view.value()};
    ReportHostAdmission(QueueHostPaths(paths, HostPathAction::OpenPaths));
}

void Application::OpenPaths(const std::vector<std::wstring>& paths, std::optional<Vec2> dropPoint) noexcept
{
    if (paths.size() > HostInputBudget::kMaxPaths)
    {
        ReportHostAdmission(ContinuationReceipt::Failure(HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA)));
        return;
    }
    std::array<std::wstring_view, HostInputBudget::kMaxPaths> views{};
    std::size_t count = 0;
    for (const std::wstring& path : paths)
    {
        views[count++] = path;
    }
    ReportHostAdmission(
        QueueHostPaths(std::span<const std::wstring_view>(views.data(), count), HostPathAction::OpenPaths, dropPoint));
}

// A loaded or newly made document takes the session over: no outline in progress, pending edit or stroke carries
// over, the history starts afresh with the document marked saved, and the viewport fits it. The session replaces its
// whole state (DocumentSession::ReplaceDocument), so a mask target or a selection of layers never survives into the
// new document, not even when the reopened active layer has the same id and a mask of its own.
void Application::FinishPreparedDocumentReplacement() noexcept
{
    CancelTransformPreviewJobs();
    RetireTransformRestore();
    _duplicateGesture.reset();
    // Model/history publication has already moved every discarded draft into its CPU retirement aggregate.
    // Drop only host UI state here: committing an old drag or opacity edit against reused layer ids is invalid.
    if (_opacityEdit)
        _opacityRecorded = _opacityEdit->shown;
    _opacityEdit.reset();
    // A field's late Enter/focus-loss commit must still acknowledge the value closed on the old document.
    _sizeDialog.kind = SizeCardKind::None;
    CloseRenameCard();
    _drag = PointerDrag{};
    _canvasPanLast.reset();
    _composition.EndStroke();
    if (_window && GetCapture() == _window.get())
    {
        _releasingCapture = true;
        ReleaseCapture();
        _releasingCapture = false;
    }
}

void Application::ReplaceDocument(CanvasDocument document, const std::optional<Guid>& active,
                                  std::wstring path) noexcept
{
    SettleInteractions(false);
    _canvasPanLast.reset();
    _session.ReplaceDocument(std::move(document), active);
    _documentPath = std::move(path);
    _viewport.followsFit = true;
    DocumentChanged();
}

// `newProject` (or no document): the first image becomes a document of its own size; every other image becomes a
// layer above the active one, all in one `Import Images` entry (`Import Image` for a single file), centered on
// `center` when the images join the document it was taken on (legacy drainImports: the images of a batch that
// starts the document center on its canvas, whatever the drop point).
void Application::ImportImages() noexcept
{
    // A quiet run never waits on a file dialog (Application::OpenDocument).
    if (_quietUi || !_window)
    {
        return;
    }
    std::optional<HostRequest> request;
    const auto result = PickImageRequest(_window.get(), _hostInputBudget, request);
    if (FAILED(result))
    {
        if (result != HRESULT_FROM_WIN32(ERROR_CANCELLED))
            ReportHostAdmission(ContinuationReceipt::Failure(result));
        return;
    }
    ReportHostAdmission(QueueHostRequest(std::move(request.value())));
}

void Application::CloseProject() noexcept
{
    if (!_hostExecuting)
    {
        ReportHostAdmission(QueueHostInput(HostCloseProjectInput{}));
        return;
    }
    if (!_session.Document().has_value())
    {
        return;
    }
    _hostExecutionError = StartDocumentLifecycle(std::nullopt);
    if (FAILED(_hostExecutionError))
    {
        ++_errorsShown;
        _lastErrorId = IDS_ERROR_DOCUMENT_LIFECYCLE;
        _prompts->PostNotice(_window.get(), kNoticeMessage, _lastErrorId);
    }
}

// ── Close, session end, capture and focus loss (Specs/UI/UI_Windowing.md) ─────────────────────────────────────────

bool Application::CloseMustWait() const noexcept
{
    // A window procedure on the stack below this one means a modal loop (a dialog, a menu, a message box, a caption
    // drag) runs inside a message whose code, chrome dispatch included, still has to return.
    return _messageDepth > 1 || (_chrome && _chrome->InDispatch());
}

void Application::ReleaseDeferredClose() noexcept
{
    if ((!_closeDeferred && !_endSessionPromptDeferred) || CloseMustWait())
    {
        return;
    }
    if (!_window)
    {
        _closeDeferred = false;
        _endSessionPromptDeferred = false;
        return;
    }
    // Posted, not run here: the queue delivers them once this message has returned.
    if (std::exchange(_endSessionPromptDeferred, false))
    {
        (void)PostMessageW(_window.get(), kEndSessionMessage, static_cast<WPARAM>(_endSessionGeneration), 0);
    }
    if (std::exchange(_closeDeferred, false))
    {
        (void)PostMessageW(_window.get(), WM_CLOSE, 0, 0);
    }
}

bool Application::OnQueryEndSession(LPARAM reasons) noexcept
{
    if ((reasons & ENDSESSION_CRITICAL) != 0)
    {
        return true;
    }
    if (_endSessionPending)
    {
        // The prompt for an earlier request has not been answered.
        return false;
    }
    // A critical end cannot wait; nothing unsaved (or a quiet run the self-test does not answer) has nothing to ask.
    if (!_window || !HasUnsavedWork() || (_quietUi && !_forcedDiscard.has_value()))
    {
        return true;
    }
    // Windows wants the answer now and shows the reason while it waits; the save prompt runs from the queue.
    const std::wstring reason = FormatStringResource(nullptr, IDS_SHUTDOWN_UNSAVED, FileTitle(_documentPath));
    (void)ShutdownBlockReasonCreate(_window.get(), reason.c_str());
    _endSessionPending = true;
    if (_endSessionGeneration == UINT64_MAX)
    {
        _endSessionPending = false;
        (void)ShutdownBlockReasonDestroy(_window.get());
        return false;
    }
    ++_endSessionGeneration;
    (void)PostMessageW(_window.get(), kEndSessionMessage, static_cast<WPARAM>(_endSessionGeneration), 0);
    return false;
}

void Application::OnEndSession(bool ending) noexcept
{
    if (_endSessionPending && _window)
    {
        (void)ShutdownBlockReasonDestroy(_window.get());
    }
    // Cancelled (another app refused, or the user did): a prompt still waiting or showing no longer closes.
    _endSessionPending = false;
    _endSessionPromptDeferred = false;
    if (ending)
    {
        // The process may end as soon as this returns: the settings are saved as a close saves them.
        SaveSettings();
    }
}

void Application::RunEndSessionPrompt() noexcept
{
    if (!_endSessionPending || !_window)
    {
        return;
    }
    if (CloseMustWait())
    {
        _endSessionPromptDeferred = true;
        return;
    }
    const ContinuationReceipt receipt = QueueHostInput(HostEndSessionInput{_endSessionGeneration, 0});
    ReportHostAdmission(receipt);
    if (receipt.Read().outcome == HostOutcome::Failed)
    {
        _endSessionPending = false;
        (void)ShutdownBlockReasonDestroy(_window.get());
    }
}

bool Application::HonorsPointerLoss() const noexcept
{
    // A hidden harness window's capture follows no real button: Windows may take it back whenever the user's mouse
    // moves, and says so from another thread. Only the self-test's own sends (this thread) stand for a loss there.
    return !_quietUi || InSendMessage() == FALSE;
}

void Application::EndPointerInteractions() noexcept
{
    _canvasPanLast.reset();
    // Our close preflight's modal loop may release capture. Its Cancel answer must leave this edit live.
    if (_canvasLifetime && _canvasLifetime->presenting && _canvasBrushInput.Busy())
        return;
    // Legacy resignFirstResponder: every drag ends (EndPointerDrag). A stroke or a warp keeps what it painted, as a
    // release would; a transform drag goes back to where it started (a drag-only edit drops, an Alt-drag's duplicate
    // with it); marquee and freehand outlines drop while a polygonal one stays; an outline move keeps its place;
    // lifted pixels and a shape draft drop; crop, gradient and eyedropper drags stop where they are; Space no longer
    // pans.
    _spacePan = false;
    if (_canvasBrushInput.Busy())
        LoseCanvasBrush();
    else
        EndPointerDrag(PointerEnd::Lost);
    _session.ClearSnapGuides();
    if (_brushHover.has_value())
    {
        _brushHover.reset();
        SyncBrushCursor();
    }
    if (_chrome && !_chrome->InDispatch())
    {
        // DxUi's press ends too (a splitter drag restores its start), and the leave clears its press and hover.
        DxUi::PointerEvent cancel{};
        cancel.action = DxUi::PointerAction::Cancel;
        (void)_chrome->DispatchPointer(cancel);
        DxUi::PointerEvent leave{};
        leave.action = DxUi::PointerAction::Leave;
        (void)_chrome->DispatchPointer(leave);
        _frameInvalidated = true;
    }
    if (_window && GetCapture() == _window.get())
    {
        _releasingCapture = true;
        (void)ReleaseCapture();
        _releasingCapture = false;
    }
}

int Application::RunFilesSelfTest() noexcept
{
    if (!_chrome || !_window)
    {
        SelfTestFailure(L"The files self-test needs the chrome and the window.\n");
        return 5;
    }
    const auto layerCount = [&]() noexcept
    { return _session.Document().has_value() ? _session.Document().value().layers.size() : std::size_t{0}; };
    const auto documentWidth = [&]() noexcept
    { return _session.Document().has_value() ? _session.Document().value().width : 0; };
    const auto settle = [&]() noexcept
    {
        if (SettleJobs(_jobs, 10000, DispatchMessageThunk, this) != SettleResult::Settled)
        {
            SelfTestFailure(L"The files self-test could not settle admitted work.\n");
            return false;
        }
        return true;
    };
    const auto command = [&](int id) noexcept
    {
        OnCommand(id);
        return settle();
    };
    const auto openPath = [&](const wchar_t* path) noexcept
    {
        OpenPath(path);
        return settle();
    };
    const auto queuePaths = [&](const std::vector<std::wstring>& paths,
                                HostPathAction action) -> std::optional<ContinuationReceipt::Result>
    {
        std::array<std::wstring_view, HostInputBudget::kMaxPaths> views{};
        if (paths.size() > views.size())
        {
            return ContinuationReceipt::Result{HostOutcome::Failed, HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA)};
        }
        std::size_t count = 0;
        for (const std::wstring& path : paths)
        {
            views[count++] = path;
        }
        const ContinuationReceipt receipt =
            QueueHostPaths(std::span<const std::wstring_view>(views.data(), count), action);
        if (!settle())
        {
            return std::nullopt;
        }
        return receipt.Read();
    };
    const auto queueWrite = [&](HostWriteFormat format) noexcept
    {
        const ContinuationReceipt receipt = QueueHostWrite(format, false);
        if (!settle())
        {
            return std::optional<ContinuationReceipt::Result>{};
        }
        return std::optional<ContinuationReceipt::Result>{receipt.Read()};
    };

    // A 6×3 opaque red PNG of the self-test's own. Every scratch name carries the process id, so concurrent runs (the
    // review-fix worktrees, CI) never share or delete each other's files.
    wchar_t temp[MAX_PATH]{};
    if (GetTempPathW(MAX_PATH, temp) == 0)
    {
        SelfTestFailure(L"The temporary directory is unavailable.\n");
        return 5;
    }
    const std::wstring scratch =
        std::wstring(temp) + L"RedPrism.selftest-" + std::to_wstring(GetCurrentProcessId()) + L"-";
    const std::wstring fixture = scratch + L"image.png";
    {
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(6 * 3 * 4), 0);
        for (std::size_t index = 0; index + 3 < bytes.size(); index += 4)
        {
            bytes[index + 2] = 255;
            bytes[index + 3] = 255;
        }
        const Raster raster = Raster::Make(6, 3, RasterFormat::Bgra8888, bytes.data(), bytes.size());
        if (EncodePngFile(raster, 72.0, fixture.c_str()) != ImageCodecStatus::Ok)
        {
            SelfTestFailure(L"The self-test could not write its PNG fixture.\n");
            return 5;
        }
    }
    const auto cleanup = [&]() noexcept { (void)DeleteFileW(fixture.c_str()); };
    const auto removeFixture = wil::scope_exit([&]() noexcept { cleanup(); });

    // Add as Layer: the document keeps its size and gains one layer as one Import Image entry.
    if (!command(ChromeHost::kCommandNew))
        return 5;
    const std::size_t layersBefore = layerCount();
    const int widthBefore = documentWidth();
    _forcedImageOpen = ImageOpenChoice::AddLayer;
    if (!openPath(fixture.c_str()))
    {
        _forcedImageOpen.reset();
        cleanup();
        return 5;
    }
    const ImageLayer* imported = _session.ActiveLayer();
    if (layerCount() != layersBefore + 1 || documentWidth() != widthBefore ||
        _session.history.UndoName() != L"Import Image" || imported == nullptr || !imported->asset.has_value() ||
        imported->asset.value().image.Width() != 6)
    {
        cleanup();
        SelfTestFailure(std::format(L"Opening an image as a layer did not add it ({} layers, width {}, "
                                    L"undo '{}').\n",
                                    layerCount(), documentWidth(), _session.history.UndoName()));
        return 5;
    }
    // New Project: an unmodified, untitled document of the image's size with nothing to undo.
    _forcedImageOpen = ImageOpenChoice::NewProject;
    const bool openedAsProject = openPath(fixture.c_str());
    _forcedImageOpen.reset();
    if (!openedAsProject)
    {
        cleanup();
        return 5;
    }
    if (documentWidth() != 6 || (_session.Document().has_value() && _session.Document().value().height != 3) ||
        layerCount() != 1 || _session.history.IsModified() || _session.history.CanUndo() || !_documentPath.empty())
    {
        cleanup();
        SelfTestFailure(std::format(L"Opening an image as a new project did not replace the document "
                                    L"({} × {}, {} layers, modified {}).\n",
                                    documentWidth(),
                                    _session.Document().has_value() ? _session.Document().value().height : 0,
                                    layerCount(), _session.history.IsModified()));
        return 5;
    }
    // The save prompt: a modified document survives New under Cancel and is replaced under Don't Save.
    _session.AddBlankLayer();
    _forcedDiscard = false;
    if (!command(ChromeHost::kCommandNew))
        return 5;
    const bool survived = documentWidth() == 6 && layerCount() == 2 && _session.history.IsModified();
    _forcedDiscard = true;
    if (!command(ChromeHost::kCommandNew))
        return 5;
    _forcedDiscard.reset();
    if (!survived || documentWidth() != 1280 || layerCount() != 1 || _session.history.IsModified())
    {
        cleanup();
        SelfTestFailure(std::format(L"The save prompt's answers were not honored (survived {}, width {}, "
                                    L"{} layers).\n",
                                    survived, documentWidth(), layerCount()));
        return 5;
    }
    // Close Project empties the shell, a frame still renders, and New brings a document back.
    if (!command(ChromeHost::kCommandCloseProject))
        return 5;
    const bool closed = !_session.Document().has_value() && _session.DisplayDocument() == nullptr &&
                        _session.LayerRows().empty() && !_session.history.IsModified();
    SyncChrome();
    const HRESULT emptyFrame = _renderer.Render(_chrome.get(), &_composition, &_viewport, _session.DisplayDocument());
    if (!command(ChromeHost::kCommandNew))
        return 5;
    if (!closed || FAILED(emptyFrame) || !_session.Document().has_value() || layerCount() != 1)
    {
        cleanup();
        SelfTestFailure(std::format(L"Close Project did not empty the shell (closed {}, frame {:#x}, "
                                    L"{} layers after New).\n",
                                    closed, static_cast<unsigned long>(emptyFrame), layerCount()));
        return 5;
    }
    // Import Images: two files as one Import Images entry.
    const std::optional<ContinuationReceipt::Result> importedImages =
        queuePaths(std::vector<std::wstring>{fixture, fixture}, HostPathAction::ImagesAsLayers);
    if (!importedImages.has_value() || importedImages.value().outcome != HostOutcome::Succeeded || layerCount() != 3 ||
        _session.history.UndoName() != L"Import Images")
    {
        cleanup();
        SelfTestFailure(std::format(L"Import Images did not add two layers as one entry ({} layers, "
                                    L"undo '{}').\n",
                                    layerCount(), _session.history.UndoName()));
        return 5;
    }
    // Move Layer Down / Up swap the active layer with its sibling as Reorder Layers entries.
    const Guid top = _session.ActiveLayerID().value();
    const auto indexOf = [&](const Guid& id) noexcept
    {
        const auto& layers = _session.Document().value().layers;
        for (std::size_t index = 0; index < layers.size(); ++index)
        {
            if (layers[index].id == id)
            {
                return static_cast<int>(index);
            }
        }
        return -1;
    };
    if (!command(ChromeHost::kCommandMoveLayerDown))
        return 5;
    const bool movedDown = indexOf(top) == 1 && _session.history.UndoName() == L"Reorder Layers";
    if (!command(ChromeHost::kCommandMoveLayerUp))
        return 5;
    if (!movedDown || indexOf(top) != 2 || _session.ActiveLayerID() != top)
    {
        SelfTestFailure(std::format(L"Move Layer Down / Up did not swap siblings (index {}).\n", indexOf(top)));
        return 5;
    }
    // Group Selected Layers wraps the active layer in a folder; undo restores.
    if (!command(ChromeHost::kCommandGroupLayers))
        return 5;
    const ImageLayer* folder = _session.ActiveLayer();
    const ImageLayer* wrapped = nullptr;
    for (const ImageLayer& layer : _session.Document().value().layers)
    {
        if (layer.id == top)
        {
            wrapped = &layer;
        }
    }
    if (folder == nullptr || !folder->isGroup || wrapped == nullptr || wrapped->parentID != folder->id ||
        _session.history.UndoName() != L"Group Layers")
    {
        SelfTestFailure(L"Group Selected Layers did not wrap the active layer in a folder.\n");
        return 5;
    }
    _session.Undo();
    DocumentChanged();
    // The clipping toggle clips the middle layer to the one below and releases it again.
    const Guid base = _session.Document().value().layers[0].id;
    const Guid middle = _session.Document().value().layers[1].id;
    _session.SelectLayer(middle);
    if (!command(ChromeHost::kCommandClipToggle))
        return 5;
    const bool clipped = _session.ActiveLayer() != nullptr && _session.ActiveLayer()->maskSourceID == base &&
                         _session.history.UndoName() == L"Create Clipping Mask";
    if (!command(ChromeHost::kCommandClipToggle))
        return 5;
    if (!clipped || _session.ActiveLayer() == nullptr || _session.ActiveLayer()->maskSourceID.has_value() ||
        _session.history.UndoName() != L"Release Clipping Mask")
    {
        SelfTestFailure(L"Ctrl+Alt+G did not clip the layer to the one below and release it.\n");
        return 5;
    }
    // No exposed function awaits an implementation any more (Specs/UI/UI_ApplicationChrome.md lists none). A budget
    // refusal raises the same posted-notice card path (RF-905): quiet UI counts it and changes nothing.
    {
        const int notices = _budgetNotices;
        const std::optional<CanvasDocument> before = _session.Document();
        const int undoCount = _session.history.UndoCount();
        ReportBudgetRefusal();
        if (_budgetNotices != notices + 1 || _session.Document() != before || _session.history.UndoCount() != undoCount)
        {
            SelfTestFailure(L"The posted notice changed the document or was not counted.\n");
            return 5;
        }
    }
    // New Project from a batch whose first image does not load: the next image that loads determines the canvas,
    // the rest join it, and the failure is reported once for the batch.
    {
        const std::wstring missing = scratch + L"missing.png";
        (void)DeleteFileW(missing.c_str());
        _forcedDiscard = true;
        const std::optional<ContinuationReceipt::Result> complete =
            queuePaths(std::vector<std::wstring>{missing, fixture, fixture}, HostPathAction::ImagesAsProject);
        _forcedDiscard.reset();
        if (!complete.has_value() || complete.value().outcome != HostOutcome::Failed || documentWidth() != 6 ||
            layerCount() != 2 || _session.history.UndoName() != L"Import Image")
        {
            SelfTestFailure(std::format(L"A new project from a batch whose first image fails did not open "
                                        L"the next one ({} layers, width {}).\n",
                                        layerCount(), documentWidth()));
            return 5;
        }
    }
    // An opaque red layer named "Red", added and made active (as InsertPaintedLayer, plus the DocumentChanged the
    // self-test needs after each one).
    const auto insertRed = [&](int width, int height) -> Guid
    {
        const Guid id = InsertPaintedLayer(_session, L"Red", width, height, Vec2{0, 0}, 0.0, 0xFFFF0000u);
        DocumentChanged();
        return id;
    };
    // The save prompt settles nothing before its answer: under Cancel an open panel, the size card, the crop frame,
    // a shape draft and a pending transform stay as they were.
    {
        _forcedDiscard = true;
        if (!command(ChromeHost::kCommandNew))
            return 5;
        const Guid red = insertRed(40, 30);
        _forcedDiscard = false;
        if (!command(ChromeHost::kCommandLevels))
            return 5;
        const bool panelOpen = _session.HasColorEdit(FilterKind::Levels);
        if (!command(ChromeHost::kCommandNew))
            return 5;
        const bool panelKept = _session.HasColorEdit(FilterKind::Levels) && layerCount() == 2;
        CancelColorEditForTest();
        if (!command(ChromeHost::kCommandCanvasSize))
            return 5;
        if (!command(ChromeHost::kCommandCloseProject))
            return 5;
        const bool cardKept = SizeCardOpen() && _session.Document().has_value();
        CancelSizeCard();
        OnTool(EditorTool::Crop);
        const std::optional<CropRect> frame = _session.CropFrame();
        if (!command(ChromeHost::kCommandNew))
            return 5;
        const bool frameKept = frame.has_value() && _session.CropFrame() == frame;
        OnTool(EditorTool::Shape);
        _session.BeginShape(Vec2{10, 10});
        _session.DragShape(Vec2{50, 40}, false, false);
        if (!command(ChromeHost::kCommandNew))
            return 5;
        const bool draftKept = _session.PendingShape().has_value();
        _session.CancelShape();
        OnTool(EditorTool::Move);
        _session.SelectLayer(red);
        if (!command(ChromeHost::kCommandFreeTransform))
            return 5;
        OnTransformAction(TransformAction::SetX, 25.0);
        if (!command(ChromeHost::kCommandNew))
            return 5;
        const bool transformKept =
            _session.PendingTransformEdit().has_value() && _session.PendingTransformEdit()->draft.origin.x == 25.0;
        OnTransformAction(TransformAction::Cancel, 0.0);
        _forcedDiscard.reset();
        if (!panelOpen || !panelKept || !cardKept || !frameKept || !draftKept || !transformKept)
        {
            SelfTestFailure(std::format(L"Cancel at the save prompt did not keep everything as it was "
                                        L"(panel {} {}, size card {}, crop frame {}, shape draft {}, "
                                        L"transform {}).\n",
                                        panelOpen, panelKept, cardKept, frameKept, draftKept, transformKept));
            return 5;
        }
    }
    // Save applies an open panel first (Decision D2): an adjustment layer's panel previews inside the document, so
    // the file holds the applied value and nothing is left for a Cancel to take back.
    {
        const std::wstring saved = scratch + L"save.prism";
        (void)DeleteFileW(saved.c_str());
        _forcedDiscard = true;
        if (!command(ChromeHost::kCommandNew))
            return 5;
        _forcedDiscard.reset();
        (void)insertRed(40, 30);
        if (!command(ChromeHost::kCommandNewLevelsAdjustment))
            return 5;
        const std::optional<Guid> adjustment = _session.AdjustmentEditingID();
        OnAdjustAction(AdjustAction::SetInputWhite, 128.0);
        _documentPath = saved;
        if (!command(ChromeHost::kCommandSave))
            return 5;
        const bool applied = !_session.HasColorEdit() && !_session.history.IsModified() &&
                             _session.history.UndoName() == L"Edit Levels Adjustment";
        // What the panel's Cancel would do, were it still open.
        CancelColorEditForTest();
        const auto whiteOf = [&](const CanvasDocument& document) -> double
        {
            for (const ImageLayer& layer : document.layers)
            {
                if (adjustment.has_value() && layer.id == adjustment.value() && layer.adjustment.has_value())
                {
                    return Adjust::LevelsCurrent(layer.adjustment->levels).white;
                }
            }
            return -1.0;
        };
        const ProjectLoadResult loaded = LoadProject(saved.c_str());
        const double fileWhite = loaded.status == ProjectIoStatus::Ok ? whiteOf(loaded.document) : -1.0;
        const double documentWhite = _session.Document().has_value() ? whiteOf(_session.Document().value()) : -1.0;
        (void)DeleteFileW(saved.c_str());
        _documentPath.clear();
        if (!adjustment.has_value() || !applied || fileWhite != 128.0 || documentWhite != 128.0 ||
            _session.history.IsModified())
        {
            SelfTestFailure(std::format(L"Save during an adjustment layer's panel did not apply it first "
                                        L"(applied {}, file white {}, document white {}, modified {}).\n",
                                        applied, fileWhite, documentWhite, _session.history.IsModified()));
            return 5;
        }
    }
    // A changed Levels panel is unsaved work even when its document revision is clean; Cancel leaves the live panel,
    // preview settings, document and saved history revision untouched.
    {
        const std::wstring pathBefore = _documentPath;
        const std::optional<bool> forcedDiscardBefore = _forcedDiscard;
        _forcedDiscard = true;
        if (!command(ChromeHost::kCommandNew))
            return 5;
        _forcedDiscard = false;
        (void)insertRed(12, 8);
        _session.history.MarkSaved();
        const std::optional<CanvasDocument> documentBefore = _session.Document();
        const Guid revisionBefore = _session.history.CurrentRevision();
        const int undoCountBefore = _session.history.UndoCount();
        const bool cleanBefore = !_session.history.IsModified();
        if (!command(ChromeHost::kCommandLevels))
            return 5;
        OnAdjustAction(AdjustAction::SetOutputBlack, 24.0);
        const bool changedPanel =
            _session.PendingColorEdit().has_value() && _session.PendingColorEdit()->kind == FilterKind::Levels &&
            Adjust::LevelsCurrent(_session.PendingColorEdit()->settings.levels).outputBlack == 24.0;
        const bool unsaved = HasUnsavedWork();
        const bool cancelKept = !_forcedDiscard.value() && AskDiscardChanges() == Prompts::Answer::Cancel;
        const bool panelKept = _session.HasColorEdit(FilterKind::Levels) && _session.PendingColorEdit().has_value() &&
                               Adjust::LevelsCurrent(_session.PendingColorEdit()->settings.levels).outputBlack == 24.0;
        const bool documentKept = _session.Document() == documentBefore;
        const bool historyKept = !_session.history.IsModified() &&
                                 _session.history.CurrentRevision() == revisionBefore &&
                                 _session.history.UndoCount() == undoCountBefore;
        CancelColorEditForTest();
        _documentPath = pathBefore;
        _forcedDiscard = forcedDiscardBefore;
        if (!cleanBefore || !changedPanel || !unsaved || !cancelKept || !panelKept || !documentKept || !historyKept)
        {
            SelfTestFailure(std::format(L"A changed Levels panel on a clean document was not preserved by Cancel "
                                        L"(clean {}, changed {}, unsaved {}, cancel {}, panel {}, document {}, "
                                        L"history {}).\n",
                                        cleanBefore, changedPanel, unsaved, cancelKept, panelKept, documentKept,
                                        historyKept));
            return 5;
        }
    }
    // Content-Aware Fill has no donors when the selection covers every opaque pixel. Failed settling must leave the
    // panel and document/history intact and must not create the exact scratch project path.
    {
        const std::wstring pathBefore = _documentPath;
        const std::wstring fillPath = scratch + L"fill-no-donors.prism";
        const std::optional<bool> forcedDiscardBefore = _forcedDiscard;
        (void)DeleteFileW(fillPath.c_str());
        _forcedDiscard = true;
        if (!command(ChromeHost::kCommandNew))
            return 5;
        _forcedDiscard.reset();
        (void)insertRed(4, 4);
        _session.ApplySelection(SelectionGeometry::Rect(0, 0, 4, 4), SelectionMode::New, L"Select");
        _session.history.MarkSaved();
        const bool cleanBefore = !_session.history.IsModified();
        const std::optional<CanvasDocument> documentBefore = _session.Document();
        const Guid revisionBefore = _session.history.CurrentRevision();
        const int undoCountBefore = _session.history.UndoCount();
        if (!command(ChromeHost::kCommandContentAwareFill))
            return 5;
        const bool failedPreview = settle() && _session.PendingColorEdit().has_value() &&
                                   _session.PendingColorEdit()->kind == FilterKind::ContentAwareFill &&
                                   _session.PendingColorEdit()->previewFailed;
        _documentPath = fillPath;
        const std::optional<ContinuationReceipt::Result> saved = queueWrite(HostWriteFormat::Project);
        const DWORD attributes = GetFileAttributesW(fillPath.c_str());
        const DWORD attributesError = attributes == INVALID_FILE_ATTRIBUTES ? GetLastError() : ERROR_SUCCESS;
        const bool noFile = attributes == INVALID_FILE_ATTRIBUTES &&
                            (attributesError == ERROR_FILE_NOT_FOUND || attributesError == ERROR_PATH_NOT_FOUND);
        const bool panelKept = _session.PendingColorEdit().has_value() &&
                               _session.PendingColorEdit()->kind == FilterKind::ContentAwareFill &&
                               _session.PendingColorEdit()->previewFailed;
        const bool documentKept = _session.Document() == documentBefore;
        const bool historyKept = !_session.history.IsModified() &&
                                 _session.history.CurrentRevision() == revisionBefore &&
                                 _session.history.UndoCount() == undoCountBefore;
        CancelColorEditForTest();
        (void)DeleteFileW(fillPath.c_str());
        _documentPath = pathBefore;
        _forcedDiscard = forcedDiscardBefore;
        if (!cleanBefore || !failedPreview || !saved.has_value() || saved.value().outcome != HostOutcome::Failed ||
            !noFile || !panelKept || !documentKept || !historyKept)
        {
            SelfTestFailure(std::format(L"Saving with donorless Content-Aware Fill did not fail without changing "
                                        L"state (clean {}, preview failed {}, saved {}, no file {}, panel {}, "
                                        L"document {}, history {}).\n",
                                        cleanBefore, failedPreview,
                                        saved.has_value() && saved.value().outcome == HostOutcome::Succeeded, noFile,
                                        panelKept, documentKept, historyKept));
            return 5;
        }
    }
    // A project opened from a folder named *.prism saves in place, as a folder.
    {
        const std::wstring exploded = scratch + L"exploded.prism";
        std::error_code error;
        std::filesystem::remove_all(exploded, error);
        struct SaveTrace
        {
            struct Observation
            {
                ProjectIoStage stage;
                unsigned int value;
            };
            std::array<Observation, 32> observations{};
            std::size_t count = 0;
            bool truncated = false;
        } trace;
        const ProjectIoObserver observer{[](void* context, ProjectIoStage stage, unsigned int value) noexcept
                                         {
                                             auto& recorded = *static_cast<SaveTrace*>(context);
                                             if (recorded.count < recorded.observations.size())
                                                 recorded.observations[recorded.count++] = {stage, value};
                                             else
                                                 recorded.truncated = true;
                                         },
                                         &trace};
        const DocumentStatus validation =
            _session.Document().has_value() ? ValidateDocument(_session.Document().value()) : DocumentStatus::Invalid;
        const ProjectIoStatus writtenStatus = _session.Document().has_value()
                                                  ? SaveProject(exploded.c_str(), _session.Document().value(),
                                                                _session.ActiveLayerID(), false, {}, observer)
                                                  : ProjectIoStatus::Invalid;
        const bool written = writtenStatus == ProjectIoStatus::Ok;
        if (!written)
        {
            std::wstring stages;
            for (std::size_t index = 0; index < trace.count; ++index)
                stages += std::format(L" {}:{}", static_cast<int>(trace.observations[index].stage),
                                      trace.observations[index].value);
            SelfTestFailure(std::format(L"Writing the initial folder-project fixture failed (status {}, "
                                        L"validation {}, prior cleanup {}, trace truncated {}, stages [{}], "
                                        L"path '{}').\n",
                                        static_cast<int>(writtenStatus), static_cast<int>(validation), error.value(),
                                        trace.truncated, stages, exploded));
            return 5;
        }
        _forcedDiscard = true;
        const std::optional<ContinuationReceipt::Result> opened =
            written
                ? queuePaths(std::vector<std::wstring>{exploded}, HostPathAction::OpenPaths)
                : std::optional<ContinuationReceipt::Result>{ContinuationReceipt::Result{HostOutcome::Failed, E_ABORT}};
        _forcedDiscard.reset();
        if (!command(ChromeHost::kCommandNewLayer))
            return 5;
        const std::size_t layers = layerCount();
        const auto asynchronousTrace = std::make_shared<ProjectSaveTestGate>();
        asynchronousTrace->traceOnly = true;
        _projectSaveTestGate = asynchronousTrace;
        const auto clearSaveTrace = wil::scope_exit([&]() noexcept { _projectSaveTestGate.reset(); });
        const std::optional<ContinuationReceipt::Result> savedInPlace = queueWrite(HostWriteFormat::Project);
        const DWORD attributes = GetFileAttributesW(exploded.c_str());
        const bool stillFolder = attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        const ProjectLoadResult reloaded = LoadProject(exploded.c_str());
        const bool complete = reloaded.status == ProjectIoStatus::Ok && reloaded.document.layers.size() == layers;
        std::filesystem::remove_all(exploded, error);
        _documentPath.clear();
        if (!written || !opened.has_value() || opened.value().outcome != HostOutcome::Succeeded ||
            !savedInPlace.has_value() || savedInPlace.value().outcome != HostOutcome::Succeeded || !stillFolder ||
            !complete || _session.history.IsModified())
        {
            std::wstring asynchronousStages;
            // A receipt exists only after SettleJobs acknowledged the worker and established visibility of trace.
            if (savedInPlace)
                for (std::size_t index = 0; index < asynchronousTrace->observationCount; ++index)
                    asynchronousStages +=
                        std::format(L" {}:{}", static_cast<int>(asynchronousTrace->observations[index].stage),
                                    asynchronousTrace->observations[index].value);
            SelfTestFailure(std::format(
                L"Saving a project opened from a folder did not save it in place "
                L"(written {}, opened {}, saved {}, folder {}, reloaded {}, receipt {}, "
                L"error {:#x}, save status {}, reload status {}, trace truncated {}, stages [{}]).\n",
                written, opened.has_value() && opened.value().outcome == HostOutcome::Succeeded,
                savedInPlace.has_value() && savedInPlace.value().outcome == HostOutcome::Succeeded, stillFolder,
                complete, savedInPlace ? static_cast<int>(savedInPlace->outcome) : -1,
                savedInPlace ? static_cast<std::uint32_t>(savedInPlace->error) : 0,
                savedInPlace ? static_cast<int>(asynchronousTrace->saveStatus) : -1, static_cast<int>(reloaded.status),
                savedInPlace && asynchronousTrace->traceTruncated, asynchronousStages));
            return 5;
        }
    }
    // A reopened project starts on its active layer's image (RF-1101): a mask targeted in the document it replaces
    // never carries over, not even when the reopened active layer is that same layer with a mask of its own.
    {
        const std::wstring reopened = scratch + L"reopen.prism";
        (void)DeleteFileW(reopened.c_str());
        _forcedDiscard = true;
        if (!command(ChromeHost::kCommandNew))
            return 5;
        _forcedDiscard.reset();
        const Guid masked = insertRed(60, 40);
        if (!command(ChromeHost::kCommandAddMask))
            return 5;
        const bool targeted = _session.IsMaskSelected() && _session.ActiveLayerID() == masked;
        const bool written = targeted && SaveProject(reopened.c_str(), _session.Document().value(),
                                                     _session.ActiveLayerID(), true) == ProjectIoStatus::Ok;
        _forcedDiscard = true;
        const std::optional<ContinuationReceipt::Result> opened =
            written
                ? queuePaths(std::vector<std::wstring>{reopened}, HostPathAction::OpenPaths)
                : std::optional<ContinuationReceipt::Result>{ContinuationReceipt::Result{HostOutcome::Failed, E_ABORT}};
        _forcedDiscard.reset();
        (void)DeleteFileW(reopened.c_str());
        _documentPath.clear();
        const bool imageTargeted = opened.has_value() && opened.value().outcome == HostOutcome::Succeeded &&
                                   _session.ActiveLayerID() == masked && _session.ActiveLayer() != nullptr &&
                                   _session.ActiveLayer()->mask.has_value() && !_session.IsMaskSelected();
        OnTool(EditorTool::Brush);
        _session.brushSettings = BrushSettings{8, 1, 1, 0, 0, 1};
        BeginBrushStroke(_viewport.ViewFromDocument(Vec2{30, 20}, DocumentSize()), 0);
        FinishBrushStroke();
        const std::wstring painted = _session.history.UndoName();
        OnTool(EditorTool::Move);
        if (!targeted || !written || !opened.has_value() || opened.value().outcome != HostOutcome::Succeeded ||
            !imageTargeted || painted != L"Brush Stroke")
        {
            SelfTestFailure(std::format(L"Reopening a project kept the mask target of the document it "
                                        L"replaced (targeted {}, written {}, opened {}, image targeted {}, "
                                        L"stroke '{}').\n",
                                        targeted, written,
                                        opened.has_value() && opened.value().outcome == HostOutcome::Succeeded,
                                        imageTargeted, painted));
            return 5;
        }
    }
    // A file dropped on the canvas joins as a layer centered on the drop point (RF-1106, legacy ContentView.onDrop);
    // dropped on the chrome it centers on the canvas. The drop goes through the window procedure with a real HDROP.
    {
        _forcedDiscard = true;
        if (!command(ChromeHost::kCommandNew))
            return 5;
        _forcedDiscard.reset();
        const auto dropAt = [&](POINT client) -> std::optional<Vec2>
        {
            const std::size_t bytes = sizeof(DROPFILES) + (fixture.size() + 2) * sizeof(wchar_t);
            wil::unique_hglobal memory(GlobalAlloc(GHND, bytes)); // zero-filled: the list ends with two nulls
            auto* files = memory ? static_cast<DROPFILES*>(GlobalLock(memory.get())) : nullptr;
            if (files == nullptr)
            {
                return std::nullopt;
            }
            files->pFiles = sizeof(DROPFILES);
            files->pt = client;
            files->fNC = FALSE;
            files->fWide = TRUE;
            std::copy(fixture.begin(), fixture.end(),
                      reinterpret_cast<wchar_t*>(reinterpret_cast<std::uint8_t*>(files) + sizeof(DROPFILES)));
            (void)GlobalUnlock(memory.get());
            const std::size_t before = layerCount();
            _forcedImageOpen = ImageOpenChoice::AddLayer;
            // The window procedure's DragFinish frees the handle.
            (void)SendMessageW(_window.get(), WM_DROPFILES, reinterpret_cast<WPARAM>(memory.release()), 0);
            if (!settle())
                return std::nullopt;
            _forcedImageOpen.reset();
            const ImageLayer* layer = _session.ActiveLayer();
            if (layerCount() != before + 1 || layer == nullptr || _session.history.UndoName() != L"Import Image")
            {
                return std::nullopt;
            }
            return layer->transform.origin;
        };
        const CanvasHole hole = _chrome->HolePixels();
        const Vec2 view = _viewport.ViewFromDocument(Vec2{200.0, 150.0}, DocumentSize());
        const POINT onCanvas{static_cast<LONG>(std::lround(hole.left + view.x)),
                             static_cast<LONG>(std::lround(hole.top + view.y))};
        const Vec2 landed = _viewport.DocumentFromView(
            Vec2{static_cast<double>(onCanvas.x) - hole.left, static_cast<double>(onCanvas.y) - hole.top},
            DocumentSize());
        const std::optional<Vec2> atPoint = dropAt(onCanvas);
        const float scale = static_cast<float>(GetDpiForWindow(_window.get())) / 96.0f;
        const D2D1_RECT_F rail = _chrome->ToolButtonBoundsDip(EditorTool::Move);
        const POINT onRail{static_cast<LONG>(std::lround((rail.left + rail.right) / 2.0f * scale)),
                           static_cast<LONG>(std::lround((rail.top + rail.bottom) / 2.0f * scale))};
        const std::optional<Vec2> atMiddle = dropAt(onRail);
        const Vec2 expectedPoint{std::floor(landed.x - 3.0), std::floor(landed.y - 1.5)};
        const Vec2 expectedMiddle{std::floor(documentWidth() / 2.0 - 3.0),
                                  std::floor(_session.Document().value().height / 2.0 - 1.5)};
        if (atPoint != expectedPoint || atMiddle != expectedMiddle)
        {
            SelfTestFailure(std::format(
                L"A dropped image did not center on the drop point, or on the canvas off it (at the point {},{} for "
                L"{},{}; off the canvas {},{} for {},{}).\n",
                atPoint.has_value() ? atPoint->x : -1.0, atPoint.has_value() ? atPoint->y : -1.0, expectedPoint.x,
                expectedPoint.y, atMiddle.has_value() ? atMiddle->x : -1.0, atMiddle.has_value() ? atMiddle->y : -1.0,
                expectedMiddle.x, expectedMiddle.y));
            return 5;
        }
    }
    OnTool(EditorTool::Move);
    return 0;
}

int Application::RunClipboardSelfTest() noexcept
{
    if (!_chrome || !_window)
    {
        SelfTestFailure(L"The clipboard self-test needs the chrome and the window.\n");
        return 5;
    }
    // The user's clipboard is set aside and comes back on every way out.
    SystemClipboard::SavedContents userClipboard;
    if (!SystemClipboard::SaveContents(_window.get(), userClipboard) || !SystemClipboard::Clear(_window.get()))
    {
        SelfTestFailure(L"The clipboard self-test could not set the user's clipboard aside.\n");
        return 5;
    }
    const auto restoreClipboard =
        wil::scope_exit([&]() noexcept { (void)SystemClipboard::RestoreContents(_window.get(), userClipboard); });
    const auto clipboardCommand = [&](int command) noexcept
    {
        OnCommand(command);
        if (SettleJobs(_jobs, 15000, DispatchMessageThunk, this) != SettleResult::Settled)
        {
            SelfTestFailure(L"The clipboard operation did not acknowledge its asynchronous work.\n");
            std::_Exit(5); // retain borrowed harness owners after a failed join
        }
    };
    _forcedDiscard = true;
    OnCommand(ChromeHost::kCommandNew);
    if (SettleJobs(_jobs, 10000, DispatchMessageThunk, this) != SettleResult::Settled)
    {
        SelfTestFailure(L"The New command did not settle in the harness.\n");
        std::_Exit(5);
    }
    _forcedDiscard.reset();
    {
        const Raster raster = SolidRaster(100, 100, 0xFFFF0000u);
        ImageLayer layer = ImageLayer::Blank(L"Sheet", Vec2{100, 100});
        layer.asset = RasterAsset{raster, Raster::MakeThumbnail(raster), layer.name};
        _session.BeginEdit(L"Import Image");
        EditLayerRecordsForTest(_session, [&](DocumentFixture& document) { document.layers.push_back(layer); });
        _session.EndEdit();
        _session.SelectLayer(layer.id);
        DocumentChanged();
    }
    // A copy that reaches the system clipboard, then another app's image there.
    _session.ApplySelection(SelectionGeometry::Rect(10, 10, 20, 10), SelectionMode::New, L"Select");
    clipboardCommand(ChromeHost::kCommandCopy);
    const Raster foreign = SolidRaster(3, 3, 0xFF00FF00u);
    // Other applications' clipboard listeners are still reading the copy: the test's own writes and reads wait them
    // out (SystemClipboard.h); the product's copy and paste keep their short wait.
    if (!SystemClipboard::WriteImage(_window.get(), foreign, SystemClipboard::kHarnessAttempts))
    {
        SelfTestFailure(L"The clipboard self-test could not write the other app's image.\n");
        return 5;
    }
    // A newer copy that cannot reach the system clipboard: another thread holds it open.
    _session.ApplySelection(SelectionGeometry::Rect(10, 10, 30, 15), SelectionMode::New, L"Select");
    ClipboardHold hold;
    if (FAILED(hold.opened.create()) || FAILED(hold.release.create()))
    {
        SelfTestFailure(L"The clipboard self-test could not create its events.\n");
        return 5;
    }
    wil::unique_handle holder(CreateThread(nullptr, 0, HoldClipboard, &hold, 0, nullptr));
    if (!holder || WaitForSingleObject(hold.opened.get(), 5000) != WAIT_OBJECT_0 || !hold.held ||
        GetOpenClipboardWindow() != hold.window)
    {
        hold.release.SetEvent();
        if (holder)
        {
            (void)WaitForSingleObject(holder.get(), 15000);
        }
        SelfTestFailure(L"The clipboard self-test could not hold the clipboard open.\n");
        return 5;
    }
    clipboardCommand(ChromeHost::kCommandCopy);
    hold.release.SetEvent();
    (void)WaitForSingleObject(holder.get(), 15000);
    Raster still;
    const bool systemRead =
        SystemClipboard::ReadImage(_window.get(), kMaxLayerPixels, still, SystemClipboard::kHarnessAttempts);
    const DWORD systemReadError = GetLastError();
    const bool writeFailed = hold.released == WAIT_OBJECT_0 && hold.closed && systemRead && still.Width() == 3 &&
                             _session.Clipboard().has_value() && _session.Clipboard()->image.Width() == 30;
    // Paste takes the session's copy, newer than the other app's image the system clipboard still holds.
    clipboardCommand(ChromeHost::kCommandPaste);
    const ImageLayer* pasted = _session.ActiveLayer();
    const bool internal = pasted != nullptr && pasted->asset.has_value() && pasted->asset->image.Width() == 30 &&
                          pasted->asset->image.Height() == 15 && _session.history.UndoName() == L"Paste";
    if (!writeFailed || !internal)
    {
        SelfTestFailure(std::format(L"Paste after a copy that could not reach the system clipboard took an "
                                    L"older image (write failed {}, OS read {} {} × {}, native error {:#x}, "
                                    L"holder release {:#x}, closed {}, OS publication {}, pasted {} × {}).\n",
                                    writeFailed, systemRead, still.Width(), still.Height(), systemReadError,
                                    hold.released, hold.closed, _clipboardWriteSucceeded,
                                    pasted && pasted->asset.has_value() ? pasted->asset->image.Width() : 0,
                                    pasted && pasted->asset.has_value() ? pasted->asset->image.Height() : 0));
        return 5;
    }
    // Copy and Copy Merged take the pixels the canvas shows (RF-1104, Decision D3): a pending gradient commits first
    // as its own entry and its preview ends with it; the merged copy holds the gradient, not the layers beneath it.
    {
        clipboardCommand(ChromeHost::kCommandDeselect);
        OnCommand(ChromeHost::kCommandNewLayer);
        OnTool(EditorTool::Gradient);
        _session.SetForegroundColor(0xFF000000u);
        _session.SetBackgroundColor(0xFFFFFFFFu);
        _session.gradientSettings = GradientSettings{};
        const auto dragLine = [&]()
        {
            BeginGradientDrag(_viewport.ViewFromDocument(Vec2{0, 50}, DocumentSize()));
            DragGradient(_viewport.ViewFromDocument(Vec2{100, 50}, DocumentSize()), 0);
            EndGradientDrag();
            return SettleJobs(_jobs, 10000, DispatchMessageThunk, this) == SettleResult::Settled &&
                   _session.PendingGradient() != nullptr && _composition.HasStroke();
        };
        const int before = _session.history.UndoCount();
        const bool copyPending = dragLine();
        clipboardCommand(ChromeHost::kCommandCopy);
        const bool copyApplied = SettleJobs(_jobs, 10000, DispatchMessageThunk, this) == SettleResult::Settled &&
                                 _session.PendingGradient() == nullptr && !_composition.HasStroke() &&
                                 _session.history.UndoCount() == before + 1 &&
                                 _session.history.UndoName() == L"Gradient";
        // A white start this time: the merged copy shows it only when the pending line was applied first.
        _session.SetForegroundColor(0xFFFFFFFFu);
        const bool mergedPending = dragLine();
        OnCommand(ChromeHost::kCommandCopyMerged);
        if (SettleJobs(_jobs, 10000, DispatchMessageThunk, this) != SettleResult::Settled)
        {
            SelfTestFailure(std::format(L"Clipboard compatibility merged settlement failed: busy {}, ticket {}, "
                                        L"reserved {}, releasing {}.\n",
                                        _historyWork.Busy(), _clipboardTicket.has_value(), _jobs.ReservedMemoryBytes(),
                                        _jobs.ReleasingMemoryBytes()));
            return 5;
        }
        std::array<std::uint8_t, 4> start{};
        if (_session.Clipboard().has_value())
        {
            _session.Clipboard()->image.ReadRegion(2, 50, 1, 1, start.data(), 4, 0);
        }
        const bool mergedApplied = _session.PendingGradient() == nullptr && !_composition.HasStroke() &&
                                   _session.history.UndoCount() == before + 2 && start[0] > 200 && start[1] > 200 &&
                                   start[2] > 200 && start[3] == 255;
        OnTool(EditorTool::Move);
        if (!copyPending || !copyApplied || !mergedPending || !mergedApplied)
        {
            SelfTestFailure(std::format(L"Copy or Copy Merged under a pending gradient did not apply it "
                                        L"first (copy {} {}, merged {} {}, merged start {} {} {} {}).\n",
                                        copyPending, copyApplied, mergedPending, mergedApplied, start[0], start[1],
                                        start[2], start[3]));
            return 5;
        }
    }
    clipboardCommand(ChromeHost::kCommandDeselect);
    return 0;
}

int Application::RunSessionSelfTest() noexcept
{
    if (!_chrome || !_window)
    {
        SelfTestFailure(L"The session self-test needs the chrome and the window.\n");
        return 5;
    }
    SyncChrome();
    (void)_chrome->Prepare(_renderer.Width(), _renderer.Height(), ChromeDpi());
    const float scale = static_cast<float>(GetDpiForWindow(_window.get())) / 96.0f;
    const LPARAM elsewhere = reinterpret_cast<LPARAM>(GetDesktopWindow());
    // A close that arrives inside a chrome callback (a modal loop a menu command or a card button runs) waits until
    // the dispatch has unwound, then runs from the queue: the chrome is never destroyed under its own stack.
    {
        bool aliveInside = false;
        bool deferredInside = false;
        _chrome->SetCommandProbeForTest(
            [this, &aliveInside, &deferredInside](int)
            {
                (void)SendMessageW(_window.get(), WM_CLOSE, 0, 0);
                aliveInside = _window && _chrome;
                deferredInside = _closeDeferred;
            });
        const D2D1_RECT_F button = _chrome->ToolButtonBoundsDip(EditorTool::Move);
        const LPARAM at = MAKELPARAM(static_cast<int>(std::lround((button.left + button.right) / 2.0f * scale)),
                                     static_cast<int>(std::lround((button.top + button.bottom) / 2.0f * scale)));
        (void)SendMessageW(_window.get(), WM_MOUSEMOVE, 0, at);
        (void)SendMessageW(_window.get(), WM_LBUTTONDOWN, MK_LBUTTON, at);
        (void)SendMessageW(_window.get(), WM_LBUTTONUP, 0, at);
        MSG message{};
        const bool reposted = _window && PeekMessageW(&message, _window.get(), WM_CLOSE, WM_CLOSE, PM_REMOVE) != FALSE;
        if (_chrome)
        {
            _chrome->SetCommandProbeForTest({});
        }
        if (!aliveInside || !deferredInside || !reposted || !_window || !_chrome || _closeDeferred)
        {
            SelfTestFailure(std::format(L"A close inside a chrome callback did not wait for the dispatch to "
                                        L"unwind (alive {}, deferred {}, posted after {}).\n",
                                        aliveInside, deferredInside, reposted));
            return 5;
        }
    }
    // A document with a painted layer for the pointer checks.
    _forcedDiscard = true;
    OnCommand(ChromeHost::kCommandNew);
    if (SettleJobs(_jobs, 10000, DispatchMessageThunk, this) != SettleResult::Settled)
    {
        SelfTestFailure(L"The New command did not settle in the harness.\n");
        std::_Exit(5);
    }
    _forcedDiscard.reset();
    {
        const Raster raster = SolidRaster(200, 150, 0xFF2F6FB0u);
        ImageLayer layer = ImageLayer::Blank(L"Painted", Vec2{200, 150});
        layer.asset = RasterAsset{raster, Raster::MakeThumbnail(raster), layer.name};
        layer.transform.origin = Vec2{300, 200};
        _session.BeginEdit(L"Import Image");
        EditLayerRecordsForTest(_session, [&](DocumentFixture& document) { document.layers.push_back(layer); });
        _session.EndEdit();
        _session.SelectLayer(layer.id);
    }
    _viewport.SetZoom(1.0, _viewport.Center(), DocumentSize());
    DocumentChanged();
    const CanvasHole hole = _chrome->HolePixels();
    const auto pointer = [&](UINT message, WPARAM wParam, double x, double y)
    {
        const Vec2 view = _viewport.ViewFromDocument(Vec2{x, y}, DocumentSize());
        OnPointer(message, wParam,
                  MAKELPARAM(static_cast<int>(std::lround(hole.left + view.x)),
                             static_cast<int>(std::lround(hole.top + view.y))));
    };
    // Capture lost mid-stroke (a dialog took the pointer): the stroke keeps what it painted, as a release would, and
    // the pointer moving on afterwards paints nothing.
    {
        OnTool(EditorTool::Brush);
        _chrome->SetCanvasFocused(true);
        _session.brushSettings = BrushSettings{20, 1, 1, 0, 0, 1};
        pointer(WM_LBUTTONDOWN, 0, 320.0, 250.0);
        pointer(WM_MOUSEMOVE, MK_LBUTTON, 380.0, 250.0);
        const bool stroking = _session.PendingBrush() != nullptr;
        const int before = _session.history.UndoCount();
        (void)SendMessageW(_window.get(), WM_CAPTURECHANGED, 0, elsewhere);
        const bool committed = _session.PendingBrush() == nullptr && _session.history.UndoCount() == before + 1 &&
                               _session.history.UndoName() == L"Brush Stroke";
        pointer(WM_MOUSEMOVE, MK_LBUTTON, 450.0, 250.0);
        pointer(WM_LBUTTONUP, 0, 450.0, 250.0);
        if (!stroking || !committed || _session.PendingBrush() != nullptr || _session.history.UndoCount() != before + 1)
        {
            SelfTestFailure(std::format(L"Losing the capture mid-stroke did not end the stroke (stroking {}, "
                                        L"committed {}, entries {} for {}).\n",
                                        stroking, committed, _session.history.UndoCount(), before + 1));
            return 5;
        }
    }
    // A stroke whose capture went without a word still ends when the pointer leaves the window.
    {
        pointer(WM_LBUTTONDOWN, 0, 320.0, 300.0);
        pointer(WM_MOUSEMOVE, MK_LBUTTON, 360.0, 300.0);
        const bool stroking = _session.PendingBrush() != nullptr;
        const int before = _session.history.UndoCount();
        _releasingCapture = true;
        (void)ReleaseCapture();
        _releasingCapture = false;
        (void)SendMessageW(_window.get(), WM_MOUSELEAVE, 0, 0);
        if (!stroking || _session.PendingBrush() != nullptr || _session.history.UndoCount() != before + 1)
        {
            SelfTestFailure(L"Leaving the window did not end a stroke that no longer held the capture.\n");
            return 5;
        }
    }
    // A drag the canvas took keeps the pointer over the chrome until its release (RF-904): a stroke dragged onto the
    // layers panel follows the pointer there instead of the panel's hover, and its release there commits it.
    {
        SyncChrome();
        const D2D1_RECT_F row = _chrome->LayerRowBoundsDip(0);
        const POINT over{static_cast<LONG>(std::lround((row.left + row.right) / 2.0f * scale)),
                         static_cast<LONG>(std::lround((row.top + row.bottom) / 2.0f * scale))};
        const Vec2 underPointer = _viewport.DocumentFromView(
            Vec2{static_cast<double>(over.x) - hole.left, static_cast<double>(over.y) - hole.top}, DocumentSize());
        pointer(WM_LBUTTONDOWN, 0, 320.0, 350.0);
        const int before = _session.history.UndoCount();
        OnPointer(WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(over.x, over.y));
        const bool followed = _session.PendingBrush() != nullptr && _brushHover.has_value() &&
                              std::fabs(_brushHover->x - underPointer.x) < 1e-6 &&
                              std::fabs(_brushHover->y - underPointer.y) < 1e-6;
        OnPointer(WM_LBUTTONUP, 0, MAKELPARAM(over.x, over.y));
        if (row.bottom <= row.top || !followed || _session.PendingBrush() != nullptr ||
            _session.history.UndoCount() != before + 1 || _session.history.UndoName() != L"Brush Stroke")
        {
            SelfTestFailure(std::format(L"A stroke dragged over the layers panel did not keep the pointer "
                                        L"(followed {}, entries {} for {}).\n",
                                        followed, _session.history.UndoCount(), before + 1));
            return 5;
        }
    }
    // Capture lost mid-drag with the Move tool: the layer goes back to where the drag started, with no entry.
    {
        OnTool(EditorTool::Move);
        const ImageLayer* layer = _session.ActiveLayer();
        const LayerTransform start = layer != nullptr ? layer->transform : LayerTransform{};
        const Vec2 center = start.Center();
        const int before = _session.history.UndoCount();
        pointer(WM_LBUTTONDOWN, 0, center.x, center.y);
        pointer(WM_MOUSEMOVE, MK_LBUTTON, center.x + 40.0, center.y + 25.0);
        const bool dragging = std::holds_alternative<MoveToolDrag>(_drag) &&
                              _session.PendingTransformEdit().has_value() &&
                              _session.PendingTransformEdit()->draft.origin != start.origin;
        (void)SendMessageW(_window.get(), WM_CAPTURECHANGED, 0, elsewhere);
        pointer(WM_MOUSEMOVE, MK_LBUTTON, center.x + 80.0, center.y + 50.0);
        pointer(WM_LBUTTONUP, 0, center.x + 80.0, center.y + 50.0);
        if (SettleJobs(_jobs, 10000, DispatchMessageThunk, this) != SettleResult::Settled)
        {
            SelfTestFailure(L"The capture-loss cancellation did not acknowledge its transform worker.\n");
            return 5;
        }
        layer = _session.ActiveLayer();
        if (layer == nullptr || !dragging || std::holds_alternative<MoveToolDrag>(_drag) ||
            _session.PendingTransformEdit().has_value() || !(layer->transform == start) ||
            _session.history.UndoCount() != before)
        {
            SelfTestFailure(L"Losing the capture mid-drag did not put the layer back where it started.\n");
            return 5;
        }
    }
    // DxUi hears the cancel too: a splitter drag in progress goes back to its start.
    {
        SyncChrome();
        const float widthBefore = _chrome->LayersWidthDip();
        const D2D1_RECT_F separator = _chrome->SplitterBoundsDip();
        DxUi::PointerEvent press{};
        press.action = DxUi::PointerAction::Down;
        press.xPixels = (separator.left + separator.right) * 0.5f * scale;
        press.yPixels = (separator.top + separator.bottom) * 0.5f * scale;
        const int hit = _chrome->HitKindForTest(press.xPixels, press.yPixels);
        const bool pressed = _chrome->DispatchPointer(press);
        press.action = DxUi::PointerAction::Move;
        press.xPixels -= 40.0f * scale;
        (void)_chrome->DispatchPointer(press);
        const bool moved = std::fabs(_chrome->LayersWidthDip() - (widthBefore + 40.0f)) <= 0.5f;
        (void)SendMessageW(_window.get(), WM_CAPTURECHANGED, 0, elsewhere);
        const bool restored = std::fabs(_chrome->LayersWidthDip() - widthBefore) <= 0.5f;
        press.action = DxUi::PointerAction::Up;
        (void)_chrome->DispatchPointer(press);
        _layoutDirty = false;
        SyncChrome();
        if (!pressed || !moved || !restored)
        {
            SelfTestFailure(std::format(L"Losing the capture did not cancel the splitter drag (pressed {}, "
                                        L"moved {}, restored {}, hit {} at {:.1f},{:.1f}).\n",
                                        pressed, moved, restored, hit, press.xPixels, press.yPixels));
            return 5;
        }
    }
    // Space held for panning ends with the focus and with the app's deactivation (its key-up goes elsewhere).
    {
        _spacePan = true;
        (void)SendMessageW(_window.get(), WM_KILLFOCUS, 0, 0);
        const bool focusEnds = !_spacePan;
        _spacePan = true;
        (void)SendMessageW(_window.get(), WM_ACTIVATEAPP, FALSE, 0);
        if (!focusEnds || _spacePan)
        {
            _spacePan = false;
            SelfTestFailure(L"Space panning outlived the focus or the app's activation.\n");
            return 5;
        }
    }
    // An Alt-press, Undo, then a drag: Undo dropped the pending transform and the drag with it, so nothing is
    // duplicated.
    {
        const ImageLayer* layer = _session.ActiveLayer();
        const Vec2 middle = layer != nullptr ? layer->transform.Center() : Vec2{};
        const std::size_t layersBefore = _session.Document().value().layers.size();
        std::array<BYTE, 256> keys{};
        const bool haveKeys = KeyboardState::Read(keys);
        std::array<BYTE, 256> altKeys = keys;
        altKeys[VK_MENU] |= 0x80;
        altKeys[VK_LMENU] |= 0x80;
        KeyboardState::Write(altKeys);
        pointer(WM_LBUTTONDOWN, 0, middle.x, middle.y);
        const MoveToolDrag* altPress = std::get_if<MoveToolDrag>(&_drag);
        const bool armed = altPress != nullptr && altPress->duplicateOnMove;
        KeyboardState::Write(keys);
        OnCommand(ChromeHost::kCommandUndo);
        pointer(WM_MOUSEMOVE, MK_LBUTTON, middle.x + 30.0, middle.y + 20.0);
        pointer(WM_LBUTTONUP, 0, middle.x + 30.0, middle.y + 20.0);
        if (SettleJobs(_jobs, 10000, DispatchMessageThunk, this) != SettleResult::Settled)
        {
            SelfTestFailure(L"The Alt-press/Undo cancellation did not acknowledge before the next session scenario.\n");
            return 5;
        }
        if (!haveKeys || !armed || _session.Document().value().layers.size() != layersBefore ||
            _session.history.UndoName() == L"Duplicate Layer")
        {
            SelfTestFailure(std::format(L"A drag after Alt-press and Undo still duplicated the layer (armed "
                                        L"{}, {} layers for {}).\n",
                                        armed, _session.Document().value().layers.size(), layersBefore));
            return 5;
        }
    }
    // Session end: an older cancelled generation cannot enqueue a continuation or consume the current block reason;
    // typed queue-full admission releases its reason and preserves the document. Cancel keeps the window; Don't Save
    // closes it without asking again. A critical end is never refused, and the session's end saves the settings.
    {
        _session.AddBlankLayer();
        DocumentChanged();
        MSG oldMessage{};
        MSG message{};
        _forcedDiscard = false;
        const LRESULT refused = SendMessageW(_window.get(), WM_QUERYENDSESSION, 0, ENDSESSION_LOGOFF);
        const std::uint64_t oldGeneration = _endSessionGeneration;
        std::array<wchar_t, 256> reason{};
        DWORD length = static_cast<DWORD>(reason.size());
        const std::wstring expected = FormatStringResource(nullptr, IDS_SHUTDOWN_UNSAVED, FileTitle(_documentPath));
        const bool reasonShown = ShutdownBlockReasonQuery(_window.get(), reason.data(), &length) != FALSE &&
                                 std::wstring(reason.data()) == expected;
        const bool earlierOrdinaryPending = _endSessionPending && _endSessionGeneration == oldGeneration;
        const LRESULT critical = SendMessageW(_window.get(), WM_QUERYENDSESSION, 0, ENDSESSION_CRITICAL);
        const bool oldPromptQueued =
            PeekMessageW(&oldMessage, _window.get(), kEndSessionMessage, kEndSessionMessage, PM_REMOVE) != FALSE;
        const bool oldMessageTagged = oldPromptQueued && oldMessage.wParam == static_cast<WPARAM>(oldGeneration);
        (void)SendMessageW(_window.get(), WM_ENDSESSION, FALSE, ENDSESSION_LOGOFF);
        length = static_cast<DWORD>(reason.size());
        const bool oldReasonRemoved = ShutdownBlockReasonQuery(_window.get(), reason.data(), &length) == FALSE;
        const LRESULT refusedAgain = SendMessageW(_window.get(), WM_QUERYENDSESSION, 0, 0);
        const std::uint64_t currentGeneration = _endSessionGeneration;
        const bool currentPromptQueued =
            PeekMessageW(&message, _window.get(), kEndSessionMessage, kEndSessionMessage, PM_REMOVE) != FALSE;
        if (oldPromptQueued)
        {
            (void)DispatchMessageW(&oldMessage);
        }
        length = static_cast<DWORD>(reason.size());
        const bool staleMessageIgnored =
            _endSessionPending && currentGeneration != oldGeneration && _endSessionGeneration == currentGeneration &&
            _hostRequests.Size() == 0 && !_activeHostRequest &&
            ShutdownBlockReasonQuery(_window.get(), reason.data(), &length) != FALSE &&
            std::wstring(reason.data()) == expected &&
            PeekMessageW(&message, _window.get(), WM_CLOSE, WM_CLOSE, PM_NOREMOVE) == FALSE;
        if (currentPromptQueued)
        {
            (void)DispatchMessageW(&message);
            (void)RunUiActions(_jobs);
        }
        length = static_cast<DWORD>(reason.size());
        const bool keptOpen = _window && !_endSessionPending && _session.history.IsModified() &&
                              PeekMessageW(&message, _window.get(), WM_CLOSE, WM_CLOSE, PM_NOREMOVE) == FALSE &&
                              ShutdownBlockReasonQuery(_window.get(), reason.data(), &length) == FALSE;

        bool queueFilled = _hostRequests.Size() == 0 && !_activeHostRequest;
        for (std::size_t i = 0; queueFilled && i < HostRequestQueue::kCapacity; ++i)
        {
            const ContinuationReceipt queued = QueueHostInput(HostCloseWindowInput{});
            queueFilled = queued.Read().outcome == HostOutcome::Pending;
        }
        const std::size_t fullQueueSize = _hostRequests.Size();
        const LRESULT refusedAtCapacity = SendMessageW(_window.get(), WM_QUERYENDSESSION, 0, ENDSESSION_LOGOFF);
        const bool fullPromptQueued =
            PeekMessageW(&message, _window.get(), kEndSessionMessage, kEndSessionMessage, PM_REMOVE) != FALSE;
        if (fullPromptQueued)
        {
            (void)DispatchMessageW(&message);
        }
        length = static_cast<DWORD>(reason.size());
        const bool fullAdmissionReleasedBlock =
            !_endSessionPending && ShutdownBlockReasonQuery(_window.get(), reason.data(), &length) == FALSE;
        const bool queueFullPreservedDocument =
            _session.history.IsModified() && _session.Document().has_value() && _hostRequests.Size() == fullQueueSize;
        (void)_hostRequests.Clear(RetireHostContinuation);
        const bool drainedQueue =
            RunUiActions(_jobs) && _hostRequests.Size() == 0 && !_activeHostRequest && _hostInputBudget.Used() == 0;

        _forcedDiscard = true;
        const LRESULT refusedAfterCapacity = SendMessageW(_window.get(), WM_QUERYENDSESSION, 0, 0);
        const bool promptAgain =
            PeekMessageW(&message, _window.get(), kEndSessionMessage, kEndSessionMessage, PM_REMOVE) != FALSE;
        if (promptAgain)
        {
            (void)DispatchMessageW(&message);
            (void)RunUiActions(_jobs);
        }
        const bool closePosted =
            PeekMessageW(&message, _window.get(), WM_CLOSE, WM_CLOSE, PM_REMOVE) != FALSE && _closeConfirmed;
        _closeConfirmed = false;
        _forcedDiscard.reset();
        std::error_code error;
        const std::filesystem::path scratch =
            std::filesystem::temp_directory_path(error) /
            (L"RedPrism.SelfTest.endsession-" + std::to_wstring(GetCurrentProcessId()));
        std::filesystem::remove_all(scratch, error);
        _settingsPath = (scratch / L"settings.json").wstring();
        (void)SendMessageW(_window.get(), WM_ENDSESSION, TRUE, ENDSESSION_LOGOFF);
        const bool settingsSaved = std::filesystem::exists(_settingsPath, error);
        _settingsPath.clear();
        std::filesystem::remove_all(scratch, error);
        if (refused != FALSE || !reasonShown || !earlierOrdinaryPending || critical == FALSE || !oldMessageTagged ||
            !oldReasonRemoved || refusedAgain != FALSE || !currentPromptQueued || !staleMessageIgnored || !keptOpen ||
            !queueFilled || refusedAtCapacity != FALSE || !fullPromptQueued || !fullAdmissionReleasedBlock ||
            !queueFullPreservedDocument || !drainedQueue || refusedAfterCapacity != FALSE || !promptAgain ||
            !closePosted || !settingsSaved)
        {
            SelfTestFailure(std::format(
                L"Session-end generation or admission behavior failed (refused {}, reason {} '{}', earlier pending "
                L"{}, critical {}, old tagged {}, old reason removed {}, second refused {}, new prompt {}, stale "
                L"ignored {}, Cancel kept "
                L"open {}, queue filled {}, capacity refused {}, capacity prompt {}, failed admission released block "
                L"{}, document preserved {}, queue drained {}, final refused {}, final prompt {}, close posted {}, "
                L"settings saved {}).\n",
                refused == FALSE, reasonShown, std::wstring(reason.data()), earlierOrdinaryPending, critical != FALSE,
                oldMessageTagged, oldReasonRemoved, refusedAgain == FALSE, currentPromptQueued, staleMessageIgnored,
                keptOpen, queueFilled, refusedAtCapacity == FALSE, fullPromptQueued, fullAdmissionReleasedBlock,
                queueFullPreservedDocument, drainedQueue, refusedAfterCapacity == FALSE, promptAgain, closePosted,
                settingsSaved));
            return 5;
        }
    }
    OnTool(EditorTool::Move);
    return 0;
}
