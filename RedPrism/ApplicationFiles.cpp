#include "Application.h"

#include "ChromeHost.h"
#include "FileDialogs.h"
#include "ImageCodecs.h"
#include "ProjectStore.h"
#include "Resources.h"
#include "resource.h"

#include <array>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <vector>

#include <commctrl.h>

// The file side of Application (C16, Specs/UI/UI_ApplicationChrome.md "Files" and "Unimplemented functions"): an
// image opened as a layer or as a new project, the save prompt before a document goes, Import Images, Close
// Project, the notice for a function that has no implementation yet, and the self-test that drives them.

namespace
{
constexpr int kTaskButtonFirst = 100;
constexpr int kTaskButtonSecond = 101;

// A menu caption as a plain name: no mnemonic, no accelerator column, no ellipsis.
[[nodiscard]] std::wstring CaptionName(UINT stringId)
{
    const std::wstring caption = LoadStringResource(nullptr, stringId);
    std::wstring name;
    name.reserve(caption.size());
    for (std::size_t index = 0; index < caption.size(); ++index)
    {
        const wchar_t ch = caption[index];
        if (ch == L'\t')
        {
            break;
        }
        if (ch == L'&')
        {
            if (index + 1 < caption.size() && caption[index + 1] == L'&')
            {
                name.push_back(L'&');
                ++index;
            }
            continue;
        }
        name.push_back(ch);
    }
    while (!name.empty() && (name.back() == L'.' || name.back() == static_cast<wchar_t>(0x2026) || name.back() == L' '))
    {
        name.pop_back();
    }
    return name;
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
    const std::wstring caption = LoadStringResource(nullptr, IDS_APP_TITLE);
    MessageBoxW(_window.get(), text.c_str(), caption.c_str(), MB_OK | MB_ICONINFORMATION);
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

bool Application::ConfirmDiscardChanges() noexcept
{
    if (!_session.Document().has_value() || !_session.history.IsModified())
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
    std::size_t next = 0;
    if (newProject || !_session.Document().has_value())
    {
        if (!ConfirmDiscardChanges())
        {
            return false;
        }
        CanvasDocument document;
        document.id = Guid::Generate();
        std::optional<Guid> active;
        const ProjectIoStatus status = ImportImageOnto(document, paths.front().c_str(), active);
        if (status != ProjectIoStatus::Ok)
        {
            ShowError(status == ProjectIoStatus::TooLarge ? IDS_ERROR_TOO_LARGE : IDS_ERROR_IMPORT_IMAGE);
            return false;
        }
        ReplaceDocument(std::move(document), active, std::wstring{});
        next = 1;
    }
    if (next >= paths.size())
    {
        return true;
    }
    std::optional<Guid> active = _session.ActiveLayerID();
    bool failed = false;
    bool tooLarge = false;
    _session.BeginEdit(paths.size() - next > 1 ? L"Import Images" : L"Import Image");
    for (; next < paths.size(); ++next)
    {
        const ProjectIoStatus status = ImportImageOnto(_session.Document().value(), paths[next].c_str(), active);
        if (status != ProjectIoStatus::Ok)
        {
            failed = true;
            tooLarge = tooLarge || status == ProjectIoStatus::TooLarge;
        }
    }
    _session.SelectLayer(active);
    _session.EndEdit();
    if (failed)
    {
        ShowError(tooLarge ? IDS_ERROR_TOO_LARGE : IDS_ERROR_IMPORT_IMAGE);
    }
    _viewport.followsFit = true;
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

    // A 6×3 opaque red PNG of the self-test's own.
    wchar_t temp[MAX_PATH]{};
    if (GetTempPathW(MAX_PATH, temp) == 0)
    {
        ReportFilesSelfTestFailure(L"The temporary directory is unavailable.\n");
        return 5;
    }
    const std::wstring fixture = std::wstring(temp) + L"RedPrism.selftest-image.png";
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
    cleanup();
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
    // Every function still awaiting an implementation raises the notice count by one and changes nothing.
    constexpr std::array<std::pair<int, const wchar_t*>, 2> kStubs{
        {{ChromeHost::kCommandRenameLayer, L"Rename Layer"}, {ChromeHost::kCommandMergeDown, L"Merge Down"}}};
    for (const auto& [command, name] : kStubs)
    {
        const int notices = _notImplementedCount;
        const std::optional<CanvasDocument> before = _session.Document();
        const int undoCount = _session.history.UndoCount();
        const EditorTool tool = _tool;
        OnCommand(command);
        if (_notImplementedCount != notices + 1 || _session.Document() != before ||
            _session.history.UndoCount() != undoCount || _tool != tool)
        {
            ReportFilesSelfTestFailure(std::format(L"{} is not implemented and did not say so, or changed something "
                                                   L"(notices {} -> {}).\n",
                                                   name, notices, _notImplementedCount));
            return 5;
        }
    }
    OnTool(EditorTool::Move);
    return 0;
}
