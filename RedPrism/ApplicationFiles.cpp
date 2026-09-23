#include "Application.h"

#include "Adjust.h"
#include "ChromeHost.h"
#include "FileDialogs.h"
#include "ImageCodecs.h"
#include "ProjectStore.h"
#include "Resources.h"
#include "Selection.h"
#include "SystemClipboard.h"
#include "resource.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <commctrl.h>

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

// Self-test diagnostics go to the debugger and, when the parent redirected it, to stderr (as Application.cpp does).
void ReportFilesSelfTestFailure(const std::wstring& message) noexcept
{
    OutputDebugStringW(message.c_str());
    HANDLE error = GetStdHandle(STD_ERROR_HANDLE);
    if (error == nullptr || error == INVALID_HANDLE_VALUE)
    {
        return;
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, message.c_str(), static_cast<int>(message.size()), nullptr, 0,
                                         nullptr, nullptr);
    if (size <= 0)
    {
        return;
    }
    std::string utf8(static_cast<std::size_t>(size), '\0');
    if (WideCharToMultiByte(CP_UTF8, 0, message.c_str(), static_cast<int>(message.size()), utf8.data(), size, nullptr,
                            nullptr) != size)
    {
        return;
    }
    DWORD written = 0;
    (void)WriteFile(error, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
}

// Another thread holding the clipboard open, as another app does while it reads or writes: a copy's write fails.
struct ClipboardHold
{
    wil::unique_event_nothrow opened;
    wil::unique_event_nothrow release;
    bool held = false;
};

DWORD WINAPI HoldClipboard(void* context) noexcept
{
    auto* hold = static_cast<ClipboardHold*>(context);
    // A clipboard manager may read the clipboard for a moment after each change: wait for it, boundedly.
    for (int attempt = 0; attempt < 100 && !hold->held; ++attempt)
    {
        hold->held = OpenClipboard(nullptr) != FALSE;
        if (!hold->held)
        {
            Sleep(10);
        }
    }
    hold->opened.SetEvent();
    (void)WaitForSingleObject(hold->release.get(), 10000);
    if (hold->held)
    {
        (void)CloseClipboard();
    }
    return 0;
}

// A solid opaque premultiplied BGRA raster for the self-tests.
[[nodiscard]] Raster SolidRaster(int width, int height, std::uint8_t blue, std::uint8_t green, std::uint8_t red)
{
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u, 0);
    for (std::size_t index = 0; index + 3 < bytes.size(); index += 4)
    {
        bytes[index] = blue;
        bytes[index + 1] = green;
        bytes[index + 2] = red;
        bytes[index + 3] = 255;
    }
    return Raster::Make(width, height, RasterFormat::Bgra8888, bytes.data(), bytes.size());
}
} // namespace

void Application::ShowNotImplemented(UINT captionId) noexcept
{
    ++_notImplementedCount;
    if (_quietUi || !_window)
    {
        return;
    }
    // Shown from the message loop: the command or click that raised it may still be inside a DxUi dispatch.
    PostMessageW(_window.get(), kNotImplementedMessage, static_cast<WPARAM>(captionId), 0);
}

void Application::ShowNotImplementedNotice(UINT captionId) noexcept
{
    if (_quietUi || !_window)
    {
        return;
    }
    const std::wstring text = FormatStringResource(nullptr, IDS_NOT_IMPLEMENTED, CaptionName(captionId));
    if (_chrome)
    {
        _chrome->ShowNotice(text);
    }
}

void Application::ShowUnavailable(UINT captionId) noexcept
{
    if (_quietUi || !_chrome)
    {
        return;
    }
    const std::wstring text = FormatStringResource(nullptr, IDS_CMD_UNAVAILABLE, CaptionName(captionId));
    _chrome->ShowNotice(text);
}

Application::ImageOpenChoice Application::PromptImageOpen(const std::wstring& firstName, std::size_t count) noexcept
{
    if (!_session.Document().has_value())
    {
        return ImageOpenChoice::NewProject;
    }
    if (_quietUi || !_window)
    {
        return _forcedImageOpen.value_or(ImageOpenChoice::AddLayer);
    }
    const std::wstring title = count > 1 ? FormatStringResource(nullptr, IDS_OPEN_IMAGES_TITLE, count)
                                         : FormatStringResource(nullptr, IDS_OPEN_IMAGE_TITLE, firstName);
    const int answer = AskThreeWay(_window.get(), title, LoadStringResource(nullptr, IDS_OPEN_IMAGE_TEXT),
                                   LoadStringResource(nullptr, IDS_OPEN_IMAGE_ADD_LAYER),
                                   LoadStringResource(nullptr, IDS_OPEN_IMAGE_NEW_PROJECT), false);
    if (answer == kTaskButtonFirst)
    {
        return ImageOpenChoice::AddLayer;
    }
    return answer == kTaskButtonSecond ? ImageOpenChoice::NewProject : ImageOpenChoice::Cancel;
}

bool Application::HasUnsavedWork() const noexcept
{
    if (!_session.Document().has_value())
    {
        return false;
    }
    if (_session.history.IsModified())
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
    if (_brushDragging || _warpDragging || _session.PendingBrush() != nullptr || _session.PendingWarp() != nullptr ||
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

void Application::SettleForWrite() noexcept
{
    CloseOpacityEdit();
    _transformDrag.reset();
    _duplicateOnDrag = false;
    _session.CommitTransform();
    _session.ClearSnapGuides();
    FinishBrushStroke();
    FinishWarpStroke();
    CommitGradient();
    // An adjustment layer's panel previews inside the document: the file must not hold a value Cancel would undo.
    ApplyColorEdit();
    DocumentChanged();
}

bool Application::ConfirmDiscardChanges() noexcept
{
    // Nothing settles before the answer: Cancel leaves every panel, card, crop frame, draft and live stroke as it
    // was. Save settles what it writes; the caller settles the rest once the answer lets the document go. A pending
    // edit Save would record (a live stroke, a pending gradient, a changed transform, a typed opacity) counts as a
    // change, so a clean document with one still asks.
    if (!_session.Document().has_value() || !HasUnsavedWork())
    {
        return true;
    }
    if (_quietUi || !_window)
    {
        return _forcedDiscard.value_or(true);
    }
    const std::wstring title = FormatStringResource(nullptr, IDS_CONFIRM_SAVE_TITLE, FileTitle(_documentPath));
    const int answer = AskThreeWay(_window.get(), title, LoadStringResource(nullptr, IDS_CONFIRM_SAVE_TEXT),
                                   LoadStringResource(nullptr, IDS_CONFIRM_SAVE_SAVE),
                                   LoadStringResource(nullptr, IDS_CONFIRM_SAVE_DISCARD), true);
    if (answer == kTaskButtonFirst)
    {
        return SaveDocument(false);
    }
    return answer == kTaskButtonSecond;
}

void Application::OpenPath(const wchar_t* path) noexcept
{
    if (path == nullptr || path[0] == L'\0')
    {
        return;
    }
    OpenPaths(std::vector<std::wstring>{std::wstring(path)});
}

void Application::OpenPaths(const std::vector<std::wstring>& paths) noexcept
{
    // Projects first, each one replacing the document; then one question for every image in the batch.
    std::vector<std::wstring> images;
    for (const std::wstring& path : paths)
    {
        if (path.empty())
        {
            continue;
        }
        if (LooksLikeProjectPath(path.c_str()))
        {
            (void)OpenProjectPath(path.c_str());
        }
        else
        {
            images.push_back(path);
        }
    }
    if (images.empty())
    {
        return;
    }
    const ImageOpenChoice choice = PromptImageOpen(FileTitle(images.front()), images.size());
    if (choice == ImageOpenChoice::Cancel)
    {
        return;
    }
    (void)ImportImagePaths(images, choice == ImageOpenChoice::NewProject);
}

bool Application::OpenProjectPath(const wchar_t* path) noexcept
{
    if (!ConfirmDiscardChanges())
    {
        return false;
    }
    const ProjectLoadResult loaded = LoadProject(path);
    if (loaded.status != ProjectIoStatus::Ok || !loaded.document.width)
    {
        if (loaded.status == ProjectIoStatus::UnsupportedVersion && !_quietUi && _window)
        {
            const std::wstring text =
                FormatStringResource(nullptr, IDS_ERROR_UNSUPPORTED_VERSION, loaded.formatVersion);
            const std::wstring caption = LoadStringResource(nullptr, IDS_APP_TITLE);
            MessageBoxW(_window.get(), text.c_str(), caption.c_str(), MB_OK | MB_ICONERROR);
        }
        else
        {
            ShowError(LoadErrorStringId(loaded.status));
        }
        return false;
    }
    ReplaceDocument(loaded.document, loaded.activeLayerID, path);
    return true;
}

// A loaded or newly made document takes the session over: no outline in progress, pending edit or stroke carries
// over, the history starts afresh with the document marked saved, and the viewport fits it.
void Application::ReplaceDocument(CanvasDocument document, const std::optional<Guid>& active,
                                  std::wstring path) noexcept
{
    SettleInteractions(false);
    _session.history.Reset();
    _session.Document() = std::move(document);
    _session.SelectLayer(active);
    _session.CollapsedGroupIDs().clear();
    _session.history.MarkSaved();
    _documentPath = std::move(path);
    _viewport.followsFit = true;
    DocumentChanged();
}

// `newProject` (or no document): the first image becomes a document of its own size; every other image becomes a
// layer above the active one, all in one `Import Images` entry (`Import Image` for a single file).
bool Application::ImportImagePaths(const std::vector<std::wstring>& paths, bool newProject) noexcept
{
    if (paths.empty())
    {
        return false;
    }
    bool failed = false;
    bool tooLarge = false;
    const auto noteFailure = [&](ProjectIoStatus status) noexcept
    {
        failed = true;
        tooLarge = tooLarge || status == ProjectIoStatus::TooLarge;
    };
    std::size_t next = 0;
    if (newProject || !_session.Document().has_value())
    {
        if (!ConfirmDiscardChanges())
        {
            return false;
        }
        // The first image that loads determines the canvas (legacy importImages); one that fails before it is
        // reported with the batch and the next one is tried.
        std::optional<CanvasDocument> created;
        std::optional<Guid> active;
        for (; next < paths.size() && !created.has_value(); ++next)
        {
            CanvasDocument document;
            document.id = Guid::Generate();
            const ProjectIoStatus status = ImportImageOnto(document, paths[next].c_str(), active);
            if (status == ProjectIoStatus::Ok)
            {
                created = std::move(document);
            }
            else
            {
                noteFailure(status);
            }
        }
        if (!created.has_value())
        {
            ShowError(tooLarge ? IDS_ERROR_TOO_LARGE : IDS_ERROR_IMPORT_IMAGE);
            return false;
        }
        ReplaceDocument(std::move(created.value()), active, std::wstring{});
    }
    else
    {
        // Layers join the document as it stands: a pending edit settles first, and an open panel applies, so the
        // import never folds into a panel's entry that Cancel would take back.
        SettleInteractions(true);
    }
    if (next < paths.size())
    {
        std::optional<Guid> active = _session.ActiveLayerID();
        _session.BeginEdit(paths.size() - next > 1 ? L"Import Images" : L"Import Image");
        for (; next < paths.size(); ++next)
        {
            const ProjectIoStatus status = ImportImageOnto(_session.Document().value(), paths[next].c_str(), active);
            if (status != ProjectIoStatus::Ok)
            {
                noteFailure(status);
            }
        }
        _session.SelectLayer(active);
        _session.EndEdit();
        _viewport.followsFit = true;
    }
    if (failed)
    {
        ShowError(tooLarge ? IDS_ERROR_TOO_LARGE : IDS_ERROR_IMPORT_IMAGE);
    }
    DocumentChanged();
    return !failed;
}

void Application::ImportImages() noexcept
{
    if (!_window)
    {
        return;
    }
    std::vector<std::wstring> paths;
    if (!PickImagePaths(_window.get(), paths))
    {
        return;
    }
    (void)ImportImagePaths(paths, false);
}

void Application::CloseProject() noexcept
{
    if (!_session.Document().has_value() || !ConfirmDiscardChanges())
    {
        return;
    }
    SettleInteractions(false);
    _session.ClearDocument();
    _documentPath.clear();
    _viewport.followsFit = true;
    DocumentChanged();
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
        (void)PostMessageW(_window.get(), kEndSessionMessage, 0, 0);
    }
    if (std::exchange(_closeDeferred, false))
    {
        (void)PostMessageW(_window.get(), WM_CLOSE, 0, 0);
    }
}

bool Application::OnQueryEndSession(LPARAM reasons) noexcept
{
    if (_endSessionPending)
    {
        // The prompt for an earlier request has not been answered.
        return false;
    }
    // A critical end cannot wait; nothing unsaved (or a quiet run the self-test does not answer) has nothing to ask.
    if ((reasons & ENDSESSION_CRITICAL) != 0 || !_window || !HasUnsavedWork() ||
        (_quietUi && !_forcedDiscard.has_value()))
    {
        return true;
    }
    // Windows wants the answer now and shows the reason while it waits; the save prompt runs from the queue.
    const std::wstring reason = FormatStringResource(nullptr, IDS_SHUTDOWN_UNSAVED, FileTitle(_documentPath));
    (void)ShutdownBlockReasonCreate(_window.get(), reason.c_str());
    _endSessionPending = true;
    (void)PostMessageW(_window.get(), kEndSessionMessage, 0, 0);
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
    const bool proceed = ConfirmDiscardChanges();
    const bool ending = std::exchange(_endSessionPending, false);
    if (!_window)
    {
        return;
    }
    (void)ShutdownBlockReasonDestroy(_window.get());
    if (proceed && ending)
    {
        // Saved or discarded: the window closes without asking again and Windows goes on ending the session.
        _closeConfirmed = true;
        (void)PostMessageW(_window.get(), WM_CLOSE, 0, 0);
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
    // Legacy resignFirstResponder: every drag ends. A stroke or a warp keeps what it painted, as a release would; a
    // transform drag goes back to where it started (a drag-only edit drops, an Alt-drag's duplicate with it); marquee
    // and freehand outlines drop while a polygonal one stays; an outline move keeps its place; lifted pixels and a
    // shape draft drop; crop, gradient and eyedropper drags stop where they are; Space no longer pans.
    _spacePan = false;
    _canvasDrag = false;
    FinishBrushStroke();
    FinishWarpStroke();
    bool restored = false;
    if (_transformDrag.has_value())
    {
        const TransformDrag drag = _transformDrag.value();
        _transformDrag.reset();
        const std::optional<TransformEdit>& edit = _session.PendingTransformEdit();
        if (edit.has_value() && !edit->persistent)
        {
            _session.CancelTransform();
        }
        else if (edit.has_value() && edit->corners.has_value() && drag.originalCorners.has_value())
        {
            _session.PreviewCorners(drag.originalCorners.value());
        }
        else if (edit.has_value())
        {
            _session.PreviewTransform(drag.original);
        }
        restored = true;
    }
    _duplicateOnDrag = false;
    _session.ClearSnapGuides();
    if (_selectionDragStart.has_value() || _lassoDragging || _pixelDragStart.has_value() ||
        _session.IsMovingSelection() || _session.PendingLasso().has_value())
    {
        EndSelectionInteraction(true);
    }
    if (_gradientHandle.has_value())
    {
        EndGradientDrag();
    }
    if (_cropDrag.has_value())
    {
        EndCropDrag();
    }
    if (_shapeDragging || _session.PendingShape().has_value())
    {
        _shapeDragging = false;
        _session.CancelShape();
        restored = true;
    }
    EndColorSampling();
    if (_hueTargetStart.has_value())
    {
        _hueTargetStart.reset();
        _session.EndHueTargeting();
    }
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
    if (restored)
    {
        DocumentChanged();
    }
}

int Application::RunFilesSelfTest() noexcept
{
    if (!_chrome || !_window)
    {
        ReportFilesSelfTestFailure(L"The files self-test needs the chrome and the window.\n");
        return 5;
    }
    const auto layerCount = [&]() noexcept
    { return _session.Document().has_value() ? _session.Document().value().layers.size() : std::size_t{0}; };
    const auto documentWidth = [&]() noexcept
    { return _session.Document().has_value() ? _session.Document().value().width : 0; };

    // A 6×3 opaque red PNG of the self-test's own. Every scratch name carries the process id, so concurrent runs (the
    // review-fix worktrees, CI) never share or delete each other's files.
    wchar_t temp[MAX_PATH]{};
    if (GetTempPathW(MAX_PATH, temp) == 0)
    {
        ReportFilesSelfTestFailure(L"The temporary directory is unavailable.\n");
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
            ReportFilesSelfTestFailure(L"The self-test could not write its PNG fixture.\n");
            return 5;
        }
    }
    const auto cleanup = [&]() noexcept { (void)DeleteFileW(fixture.c_str()); };
    const auto removeFixture = wil::scope_exit([&]() noexcept { cleanup(); });

    // Add as Layer: the document keeps its size and gains one layer as one Import Image entry.
    OnCommand(ChromeHost::kCommandNew);
    const std::size_t layersBefore = layerCount();
    const int widthBefore = documentWidth();
    _forcedImageOpen = ImageOpenChoice::AddLayer;
    OpenPath(fixture.c_str());
    const ImageLayer* imported = _session.ActiveLayer();
    if (layerCount() != layersBefore + 1 || documentWidth() != widthBefore ||
        _session.history.UndoName() != L"Import Image" || imported == nullptr || !imported->asset.has_value() ||
        imported->asset.value().image.Width() != 6)
    {
        cleanup();
        ReportFilesSelfTestFailure(std::format(L"Opening an image as a layer did not add it ({} layers, width {}, "
                                               L"undo '{}').\n",
                                               layerCount(), documentWidth(), _session.history.UndoName()));
        return 5;
    }
    // New Project: an unmodified, untitled document of the image's size with nothing to undo.
    _forcedImageOpen = ImageOpenChoice::NewProject;
    OpenPath(fixture.c_str());
    _forcedImageOpen.reset();
    if (documentWidth() != 6 || (_session.Document().has_value() && _session.Document().value().height != 3) ||
        layerCount() != 1 || _session.history.IsModified() || _session.history.CanUndo() || !_documentPath.empty())
    {
        cleanup();
        ReportFilesSelfTestFailure(std::format(L"Opening an image as a new project did not replace the document "
                                               L"({} × {}, {} layers, modified {}).\n",
                                               documentWidth(),
                                               _session.Document().has_value() ? _session.Document().value().height : 0,
                                               layerCount(), _session.history.IsModified()));
        return 5;
    }
    // The save prompt: a modified document survives New under Cancel and is replaced under Don't Save.
    _session.AddBlankLayer();
    _forcedDiscard = false;
    OnCommand(ChromeHost::kCommandNew);
    const bool survived = documentWidth() == 6 && layerCount() == 2 && _session.history.IsModified();
    _forcedDiscard = true;
    OnCommand(ChromeHost::kCommandNew);
    _forcedDiscard.reset();
    if (!survived || documentWidth() != 1280 || layerCount() != 1 || _session.history.IsModified())
    {
        cleanup();
        ReportFilesSelfTestFailure(std::format(L"The save prompt's answers were not honored (survived {}, width {}, "
                                               L"{} layers).\n",
                                               survived, documentWidth(), layerCount()));
        return 5;
    }
    // Close Project empties the shell, a frame still renders, and New brings a document back.
    OnCommand(ChromeHost::kCommandCloseProject);
    const bool closed = !_session.Document().has_value() && _session.DisplayDocument() == nullptr &&
                        _session.LayerRows().empty() && !_session.history.IsModified();
    SyncChrome();
    const HRESULT emptyFrame = _renderer.Render(_chrome.get(), &_composition, &_viewport, _session.DisplayDocument());
    OnCommand(ChromeHost::kCommandNew);
    if (!closed || FAILED(emptyFrame) || !_session.Document().has_value() || layerCount() != 1)
    {
        cleanup();
        ReportFilesSelfTestFailure(std::format(L"Close Project did not empty the shell (closed {}, frame {:#x}, "
                                               L"{} layers after New).\n",
                                               closed, static_cast<unsigned long>(emptyFrame), layerCount()));
        return 5;
    }
    // Import Images: two files as one Import Images entry.
    if (!ImportImagePaths(std::vector<std::wstring>{fixture, fixture}, false) || layerCount() != 3 ||
        _session.history.UndoName() != L"Import Images")
    {
        cleanup();
        ReportFilesSelfTestFailure(std::format(L"Import Images did not add two layers as one entry ({} layers, "
                                               L"undo '{}').\n",
                                               layerCount(), _session.history.UndoName()));
        return 5;
    }
    // Move Layer Down / Up swap the active layer with its sibling as Reorder Layers entries.
    const Guid top = _session.ActiveLayerID().value();
    const auto indexOf = [&](const Guid& id) noexcept
    {
        const std::vector<ImageLayer>& layers = _session.Document().value().layers;
        for (std::size_t index = 0; index < layers.size(); ++index)
        {
            if (layers[index].id == id)
            {
                return static_cast<int>(index);
            }
        }
        return -1;
    };
    OnCommand(ChromeHost::kCommandMoveLayerDown);
    const bool movedDown = indexOf(top) == 1 && _session.history.UndoName() == L"Reorder Layers";
    OnCommand(ChromeHost::kCommandMoveLayerUp);
    if (!movedDown || indexOf(top) != 2 || _session.ActiveLayerID() != top)
    {
        ReportFilesSelfTestFailure(
            std::format(L"Move Layer Down / Up did not swap siblings (index {}).\n", indexOf(top)));
        return 5;
    }
    // Group Selected Layers wraps the active layer in a folder; undo restores.
    OnCommand(ChromeHost::kCommandGroupLayers);
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
        ReportFilesSelfTestFailure(L"Group Selected Layers did not wrap the active layer in a folder.\n");
        return 5;
    }
    _session.Undo();
    DocumentChanged();
    // The clipping toggle clips the middle layer to the one below and releases it again.
    const Guid base = _session.Document().value().layers[0].id;
    const Guid middle = _session.Document().value().layers[1].id;
    _session.SelectLayer(middle);
    OnCommand(ChromeHost::kCommandClipToggle);
    const bool clipped = _session.ActiveLayer() != nullptr && _session.ActiveLayer()->maskSourceID == base &&
                         _session.history.UndoName() == L"Create Clipping Mask";
    OnCommand(ChromeHost::kCommandClipToggle);
    if (!clipped || _session.ActiveLayer() == nullptr || _session.ActiveLayer()->maskSourceID.has_value() ||
        _session.history.UndoName() != L"Release Clipping Mask")
    {
        ReportFilesSelfTestFailure(L"Ctrl+Alt+G did not clip the layer to the one below and release it.\n");
        return 5;
    }
    // No exposed function awaits an implementation any more (Specs/UI/UI_ApplicationChrome.md lists none); the
    // notice path itself stays honest: raising it under quiet UI counts and changes nothing.
    {
        const int notices = _notImplementedCount;
        const std::optional<CanvasDocument> before = _session.Document();
        const int undoCount = _session.history.UndoCount();
        ShowNotImplemented(IDS_CMD_MERGE_DOWN);
        if (_notImplementedCount != notices + 1 || _session.Document() != before ||
            _session.history.UndoCount() != undoCount)
        {
            ReportFilesSelfTestFailure(L"The not-implemented notice changed the document or was not counted.\n");
            return 5;
        }
    }
    // New Project from a batch whose first image does not load: the next image that loads determines the canvas,
    // the rest join it, and the failure is reported once for the batch.
    {
        const std::wstring missing = scratch + L"missing.png";
        (void)DeleteFileW(missing.c_str());
        _forcedDiscard = true;
        const bool complete = ImportImagePaths(std::vector<std::wstring>{missing, fixture, fixture}, true);
        _forcedDiscard.reset();
        if (complete || documentWidth() != 6 || layerCount() != 2 || _session.history.UndoName() != L"Import Image")
        {
            ReportFilesSelfTestFailure(std::format(L"A new project from a batch whose first image fails did not open "
                                                   L"the next one ({} layers, width {}).\n",
                                                   layerCount(), documentWidth()));
            return 5;
        }
    }
    const auto insertRed = [&](int width, int height) -> Guid
    {
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u, 0);
        for (std::size_t index = 0; index + 3 < bytes.size(); index += 4)
        {
            bytes[index + 2] = 255;
            bytes[index + 3] = 255;
        }
        const Raster raster = Raster::Make(width, height, RasterFormat::Bgra8888, bytes.data(), bytes.size());
        ImageLayer layer = ImageLayer::Blank(L"Red", Vec2{static_cast<double>(width), static_cast<double>(height)});
        layer.asset = RasterAsset{raster, Raster::MakeThumbnail(raster), layer.name};
        _session.BeginEdit(L"Import Image");
        _session.Document().value().layers.push_back(layer);
        _session.EndEdit();
        _session.SelectLayer(layer.id);
        DocumentChanged();
        return layer.id;
    };
    // The save prompt settles nothing before its answer: under Cancel an open panel, the size card, the crop frame,
    // a shape draft and a pending transform stay as they were.
    {
        _forcedDiscard = true;
        OnCommand(ChromeHost::kCommandNew);
        const Guid red = insertRed(40, 30);
        _forcedDiscard = false;
        OnCommand(ChromeHost::kCommandLevels);
        const bool panelOpen = _session.HasColorEdit(FilterKind::Levels);
        OnCommand(ChromeHost::kCommandNew);
        const bool panelKept = _session.HasColorEdit(FilterKind::Levels) && layerCount() == 2;
        CancelColorEdit();
        OnCommand(ChromeHost::kCommandCanvasSize);
        OnCommand(ChromeHost::kCommandCloseProject);
        const bool cardKept = SizeCardOpen() && _session.Document().has_value();
        CancelSizeCard();
        OnTool(EditorTool::Crop);
        const std::optional<CropRect> frame = _session.CropFrame();
        OnCommand(ChromeHost::kCommandNew);
        const bool frameKept = frame.has_value() && _session.CropFrame() == frame;
        OnTool(EditorTool::Shape);
        _session.BeginShape(Vec2{10, 10});
        _session.DragShape(Vec2{50, 40}, false, false);
        OnCommand(ChromeHost::kCommandNew);
        const bool draftKept = _session.PendingShape().has_value();
        _session.CancelShape();
        OnTool(EditorTool::Move);
        _session.SelectLayer(red);
        OnCommand(ChromeHost::kCommandFreeTransform);
        OnTransformAction(TransformAction::SetX, 25.0);
        OnCommand(ChromeHost::kCommandNew);
        const bool transformKept =
            _session.PendingTransformEdit().has_value() && _session.PendingTransformEdit()->draft.origin.x == 25.0;
        OnTransformAction(TransformAction::Cancel, 0.0);
        _forcedDiscard.reset();
        if (!panelOpen || !panelKept || !cardKept || !frameKept || !draftKept || !transformKept)
        {
            ReportFilesSelfTestFailure(std::format(L"Cancel at the save prompt did not keep everything as it was "
                                                   L"(panel {} {}, size card {}, crop frame {}, shape draft {}, "
                                                   L"transform {}).\n",
                                                   panelOpen, panelKept, cardKept, frameKept, draftKept,
                                                   transformKept));
            return 5;
        }
    }
    // Save applies an open panel first (Decision D2): an adjustment layer's panel previews inside the document, so
    // the file holds the applied value and nothing is left for a Cancel to take back.
    {
        const std::wstring saved = scratch + L"save.prism";
        (void)DeleteFileW(saved.c_str());
        _forcedDiscard = true;
        OnCommand(ChromeHost::kCommandNew);
        _forcedDiscard.reset();
        (void)insertRed(40, 30);
        OnCommand(ChromeHost::kCommandNewLevelsAdjustment);
        const std::optional<Guid> adjustment = _session.AdjustmentEditingID();
        OnAdjustAction(AdjustAction::SetInputWhite, 128.0);
        _documentPath = saved;
        OnCommand(ChromeHost::kCommandSave);
        const bool applied = !_session.HasColorEdit() && !_session.history.IsModified() &&
                             _session.history.UndoName() == L"Edit Levels Adjustment";
        // What the panel's Cancel would do, were it still open.
        CancelColorEdit();
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
            ReportFilesSelfTestFailure(std::format(L"Save during an adjustment layer's panel did not apply it first "
                                                   L"(applied {}, file white {}, document white {}, modified {}).\n",
                                                   applied, fileWhite, documentWhite, _session.history.IsModified()));
            return 5;
        }
    }
    // A project opened from a folder named *.prism saves in place, as a folder.
    {
        const std::wstring exploded = scratch + L"exploded.prism";
        std::error_code error;
        std::filesystem::remove_all(exploded, error);
        const bool written =
            _session.Document().has_value() && SaveProject(exploded.c_str(), _session.Document().value(),
                                                           _session.ActiveLayerID(), false) == ProjectIoStatus::Ok;
        _forcedDiscard = true;
        const bool opened = written && OpenProjectPath(exploded.c_str());
        _forcedDiscard.reset();
        OnCommand(ChromeHost::kCommandNewLayer);
        const std::size_t layers = layerCount();
        const bool savedInPlace = SaveDocument(false);
        const DWORD attributes = GetFileAttributesW(exploded.c_str());
        const bool stillFolder = attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        const ProjectLoadResult reloaded = LoadProject(exploded.c_str());
        const bool complete = reloaded.status == ProjectIoStatus::Ok && reloaded.document.layers.size() == layers;
        std::filesystem::remove_all(exploded, error);
        _documentPath.clear();
        if (!written || !opened || !savedInPlace || !stillFolder || !complete || _session.history.IsModified())
        {
            ReportFilesSelfTestFailure(std::format(L"Saving a project opened from a folder did not save it in place "
                                                   L"(written {}, opened {}, saved {}, folder {}, reloaded {}).\n",
                                                   written, opened, savedInPlace, stillFolder, complete));
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
        ReportFilesSelfTestFailure(L"The clipboard self-test needs the chrome and the window.\n");
        return 5;
    }
    // The user's clipboard is set aside and comes back on every way out.
    SystemClipboard::SavedContents userClipboard;
    if (!SystemClipboard::SaveContents(_window.get(), userClipboard) || !SystemClipboard::Clear(_window.get()))
    {
        ReportFilesSelfTestFailure(L"The clipboard self-test could not set the user's clipboard aside.\n");
        return 5;
    }
    const auto restoreClipboard =
        wil::scope_exit([&]() noexcept { (void)SystemClipboard::RestoreContents(_window.get(), userClipboard); });
    _forcedDiscard = true;
    OnCommand(ChromeHost::kCommandNew);
    _forcedDiscard.reset();
    {
        const Raster raster = SolidRaster(100, 100, 0, 0, 255);
        ImageLayer layer = ImageLayer::Blank(L"Sheet", Vec2{100, 100});
        layer.asset = RasterAsset{raster, Raster::MakeThumbnail(raster), layer.name};
        _session.BeginEdit(L"Import Image");
        _session.Document().value().layers.push_back(layer);
        _session.EndEdit();
        _session.SelectLayer(layer.id);
        DocumentChanged();
    }
    // A copy that reaches the system clipboard, then another app's image there.
    _session.ApplySelection(SelectionGeometry::Rect(10, 10, 20, 10), SelectionMode::New, L"Select");
    OnCommand(ChromeHost::kCommandCopy);
    const Raster foreign = SolidRaster(3, 3, 0, 255, 0);
    if (!SystemClipboard::WriteImage(_window.get(), foreign))
    {
        ReportFilesSelfTestFailure(L"The clipboard self-test could not write the other app's image.\n");
        return 5;
    }
    // A newer copy that cannot reach the system clipboard: another thread holds it open.
    _session.ApplySelection(SelectionGeometry::Rect(10, 10, 30, 15), SelectionMode::New, L"Select");
    ClipboardHold hold;
    if (FAILED(hold.opened.create()) || FAILED(hold.release.create()))
    {
        ReportFilesSelfTestFailure(L"The clipboard self-test could not create its events.\n");
        return 5;
    }
    wil::unique_handle holder(CreateThread(nullptr, 0, HoldClipboard, &hold, 0, nullptr));
    if (!holder || WaitForSingleObject(hold.opened.get(), 5000) != WAIT_OBJECT_0 || !hold.held)
    {
        hold.release.SetEvent();
        if (holder)
        {
            (void)WaitForSingleObject(holder.get(), 15000);
        }
        ReportFilesSelfTestFailure(L"The clipboard self-test could not hold the clipboard open.\n");
        return 5;
    }
    OnCommand(ChromeHost::kCommandCopy);
    hold.release.SetEvent();
    (void)WaitForSingleObject(holder.get(), 15000);
    Raster still;
    const bool writeFailed = SystemClipboard::ReadImage(_window.get(), kMaxLayerPixels, still) && still.Width() == 3 &&
                             _session.Clipboard().has_value() && _session.Clipboard()->image.Width() == 30;
    // Paste takes the session's copy, newer than the other app's image the system clipboard still holds.
    OnCommand(ChromeHost::kCommandPaste);
    const ImageLayer* pasted = _session.ActiveLayer();
    const bool internal = pasted != nullptr && pasted->asset.has_value() && pasted->asset->image.Width() == 30 &&
                          pasted->asset->image.Height() == 15 && _session.history.UndoName() == L"Paste";
    if (!writeFailed || !internal)
    {
        ReportFilesSelfTestFailure(
            std::format(L"Paste after a copy that could not reach the system clipboard took an "
                        L"older image (write failed {}, pasted {} × {}).\n",
                        writeFailed, pasted && pasted->asset.has_value() ? pasted->asset->image.Width() : 0,
                        pasted && pasted->asset.has_value() ? pasted->asset->image.Height() : 0));
        return 5;
    }
    OnCommand(ChromeHost::kCommandDeselect);
    return 0;
}

int Application::RunSessionSelfTest() noexcept
{
    if (!_chrome || !_window)
    {
        ReportFilesSelfTestFailure(L"The session self-test needs the chrome and the window.\n");
        return 5;
    }
    const float scale = static_cast<float>(GetDpiForWindow(_window.get())) / 96.0f;
    const LPARAM elsewhere = reinterpret_cast<LPARAM>(GetDesktopWindow());
    // A close that arrives inside a chrome callback (a modal loop a menu command or a card button runs) waits until
    // the dispatch has unwound, then runs from the queue: the chrome is never destroyed under its own stack.
    {
        bool aliveInside = false;
        bool deferredInside = false;
        _chrome->SetCommandHandler(
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
            _chrome->SetCommandHandler([this](int command) { OnCommand(command); });
        }
        if (!aliveInside || !deferredInside || !reposted || !_window || !_chrome || _closeDeferred)
        {
            ReportFilesSelfTestFailure(std::format(L"A close inside a chrome callback did not wait for the dispatch to "
                                                   L"unwind (alive {}, deferred {}, posted after {}).\n",
                                                   aliveInside, deferredInside, reposted));
            return 5;
        }
    }
    // A document with a painted layer for the pointer checks.
    _forcedDiscard = true;
    OnCommand(ChromeHost::kCommandNew);
    _forcedDiscard.reset();
    {
        const Raster raster = SolidRaster(200, 150, 0xB0, 0x6F, 0x2F);
        ImageLayer layer = ImageLayer::Blank(L"Painted", Vec2{200, 150});
        layer.asset = RasterAsset{raster, Raster::MakeThumbnail(raster), layer.name};
        layer.transform.origin = Vec2{300, 200};
        _session.BeginEdit(L"Import Image");
        _session.Document().value().layers.push_back(layer);
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
        _session.brushSettings = BrushSettings{20, 1, 1, 0, 0, 1, false};
        pointer(WM_LBUTTONDOWN, 0, 320.0, 250.0);
        pointer(WM_MOUSEMOVE, MK_LBUTTON, 380.0, 250.0);
        const bool stroking = _brushDragging;
        const int before = _session.history.UndoCount();
        (void)SendMessageW(_window.get(), WM_CAPTURECHANGED, 0, elsewhere);
        const bool committed = !_brushDragging && _session.PendingBrush() == nullptr &&
                               _session.history.UndoCount() == before + 1 &&
                               _session.history.UndoName() == L"Brush Stroke";
        pointer(WM_MOUSEMOVE, MK_LBUTTON, 450.0, 250.0);
        pointer(WM_LBUTTONUP, 0, 450.0, 250.0);
        if (!stroking || !committed || _session.PendingBrush() != nullptr || _session.history.UndoCount() != before + 1)
        {
            ReportFilesSelfTestFailure(
                std::format(L"Losing the capture mid-stroke did not end the stroke (stroking {}, "
                            L"committed {}, entries {} for {}).\n",
                            stroking, committed, _session.history.UndoCount(), before + 1));
            return 5;
        }
    }
    // A stroke whose capture went without a word still ends when the pointer leaves the window.
    {
        pointer(WM_LBUTTONDOWN, 0, 320.0, 300.0);
        pointer(WM_MOUSEMOVE, MK_LBUTTON, 360.0, 300.0);
        const bool stroking = _brushDragging;
        const int before = _session.history.UndoCount();
        _releasingCapture = true;
        (void)ReleaseCapture();
        _releasingCapture = false;
        (void)SendMessageW(_window.get(), WM_MOUSELEAVE, 0, 0);
        if (!stroking || _brushDragging || _session.history.UndoCount() != before + 1)
        {
            ReportFilesSelfTestFailure(L"Leaving the window did not end a stroke that no longer held the capture.\n");
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
        const bool dragging = _transformDrag.has_value() && _session.PendingTransformEdit().has_value() &&
                              _session.PendingTransformEdit()->draft.origin != start.origin;
        (void)SendMessageW(_window.get(), WM_CAPTURECHANGED, 0, elsewhere);
        pointer(WM_MOUSEMOVE, MK_LBUTTON, center.x + 80.0, center.y + 50.0);
        pointer(WM_LBUTTONUP, 0, center.x + 80.0, center.y + 50.0);
        layer = _session.ActiveLayer();
        if (layer == nullptr || !dragging || _transformDrag.has_value() ||
            _session.PendingTransformEdit().has_value() || !(layer->transform == start) ||
            _session.history.UndoCount() != before)
        {
            ReportFilesSelfTestFailure(L"Losing the capture mid-drag did not put the layer back where it started.\n");
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
            ReportFilesSelfTestFailure(std::format(L"Losing the capture did not cancel the splitter drag (pressed {}, "
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
            ReportFilesSelfTestFailure(L"Space panning outlived the focus or the app's activation.\n");
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
        const bool haveKeys = GetKeyboardState(keys.data()) != FALSE;
        std::array<BYTE, 256> altKeys = keys;
        altKeys[VK_MENU] |= 0x80;
        altKeys[VK_LMENU] |= 0x80;
        (void)SetKeyboardState(altKeys.data());
        pointer(WM_LBUTTONDOWN, 0, middle.x, middle.y);
        const bool armed = _duplicateOnDrag;
        (void)SetKeyboardState(keys.data());
        OnCommand(ChromeHost::kCommandUndo);
        pointer(WM_MOUSEMOVE, MK_LBUTTON, middle.x + 30.0, middle.y + 20.0);
        pointer(WM_LBUTTONUP, 0, middle.x + 30.0, middle.y + 20.0);
        if (!haveKeys || !armed || _session.Document().value().layers.size() != layersBefore ||
            _session.history.UndoName() == L"Duplicate Layer")
        {
            ReportFilesSelfTestFailure(std::format(L"A drag after Alt-press and Undo still duplicated the layer (armed "
                                                   L"{}, {} layers for {}).\n",
                                                   armed, _session.Document().value().layers.size(), layersBefore));
            return 5;
        }
    }
    // Session end: a modified document refuses with a block reason and asks through the posted prompt; Cancel keeps
    // the window, Don't Save closes it without asking again, a critical end is never refused, and the session's end
    // saves the settings.
    {
        _session.AddBlankLayer();
        DocumentChanged();
        MSG message{};
        _forcedDiscard = false;
        const LRESULT refused = SendMessageW(_window.get(), WM_QUERYENDSESSION, 0, ENDSESSION_LOGOFF);
        std::array<wchar_t, 256> reason{};
        DWORD length = static_cast<DWORD>(reason.size());
        const std::wstring expected = FormatStringResource(nullptr, IDS_SHUTDOWN_UNSAVED, FileTitle(_documentPath));
        const bool reasonShown = ShutdownBlockReasonQuery(_window.get(), reason.data(), &length) != FALSE &&
                                 std::wstring(reason.data()) == expected;
        const bool promptQueued =
            PeekMessageW(&message, _window.get(), kEndSessionMessage, kEndSessionMessage, PM_REMOVE) != FALSE;
        if (promptQueued)
        {
            (void)DispatchMessageW(&message);
        }
        length = static_cast<DWORD>(reason.size());
        const bool keptOpen = _window && !_endSessionPending && _session.history.IsModified() &&
                              PeekMessageW(&message, _window.get(), WM_CLOSE, WM_CLOSE, PM_NOREMOVE) == FALSE &&
                              ShutdownBlockReasonQuery(_window.get(), reason.data(), &length) == FALSE;
        const LRESULT critical = SendMessageW(_window.get(), WM_QUERYENDSESSION, 0, ENDSESSION_CRITICAL);
        _forcedDiscard = true;
        const LRESULT refusedAgain = SendMessageW(_window.get(), WM_QUERYENDSESSION, 0, 0);
        const bool promptAgain =
            PeekMessageW(&message, _window.get(), kEndSessionMessage, kEndSessionMessage, PM_REMOVE) != FALSE;
        if (promptAgain)
        {
            (void)DispatchMessageW(&message);
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
        if (refused != FALSE || !reasonShown || !promptQueued || !keptOpen || critical == FALSE ||
            refusedAgain != FALSE || !promptAgain || !closePosted || !settingsSaved)
        {
            ReportFilesSelfTestFailure(std::format(
                L"The session end did not ask for the unsaved document (refused {}, reason {} '{}', prompt {}, kept "
                L"open {}, critical allowed {}, refused again {}, prompt again {}, close posted {}, settings "
                L"saved {}).\n",
                refused == FALSE, reasonShown, std::wstring(reason.data()), promptQueued, keptOpen, critical != FALSE,
                refusedAgain == FALSE, promptAgain, closePosted, settingsSaved));
            return 5;
        }
    }
    OnTool(EditorTool::Move);
    return 0;
}
