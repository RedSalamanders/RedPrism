#include "Application.h"

#include "ChromeHost.h"
#include "Resources.h"
#include "resource.h"

#include <cstdint>
#include <format>
#include <string>
#include <unordered_set>
#include <vector>

// The Layer menu's rename and merge (C16, Specs/UI/UI_ApplicationChrome.md "Layers panel"): the Rename Layer card
// and Ctrl+E, which composites the planned layers through the canvas composition and installs the result.

namespace
{
// Self-test diagnostics go to the debugger and, when the parent redirected it, to stderr (as Application.cpp does).
void ReportLayerEditsSelfTestFailure(const std::wstring& message) noexcept
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

// A solid premultiplied BGRA raster for the self-test.
[[nodiscard]] Raster SolidRaster(int width, int height, std::uint32_t argb)
{
    const std::uint8_t alpha = static_cast<std::uint8_t>(argb >> 24);
    const auto premultiply = [alpha](std::uint32_t channel)
    { return static_cast<std::uint8_t>((channel & 0xFFu) * alpha / 255u); };
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(width) * 4u * static_cast<std::size_t>(height));
    for (std::size_t index = 0; index + 3 < bytes.size(); index += 4)
    {
        bytes[index] = premultiply(argb);
        bytes[index + 1] = premultiply(argb >> 8);
        bytes[index + 2] = premultiply(argb >> 16);
        bytes[index + 3] = alpha;
    }
    return Raster::Make(width, height, RasterFormat::Bgra8888, bytes.data(), bytes.size());
}
} // namespace

void Application::RenameActiveLayer() noexcept
{
    const ImageLayer* active = _session.ActiveLayer();
    if (active == nullptr || !_chrome)
    {
        return;
    }
    _chrome->OpenRenameCard(active->name);
    _frameInvalidated = true;
}

void Application::OnLayerInvoked(Guid id) noexcept
{
    // A double-click on a row renames that layer (legacy: the inline editor of the layer list).
    _session.SelectLayer(id);
    SyncChrome();
    RenameActiveLayer();
}

void Application::OnRename(std::optional<std::wstring> name) noexcept
{
    if (name.has_value())
    {
        if (const ImageLayer* active = _session.ActiveLayer(); active != nullptr)
        {
            _session.RenameLayer(active->id, name.value());
        }
    }
    DocumentChanged();
}

void Application::MergeLayersCommand() noexcept
{
    const std::optional<DocumentSession::MergePlan> plan = _session.PlanMerge();
    if (!plan.has_value() || !_session.Document().has_value())
    {
        return;
    }
    // Only the merged layers, cut loose from anything outside the merge, composited as the canvas shows them.
    CanvasDocument flat = _session.Document().value();
    flat.layers.clear();
    flat.selection.reset();
    const std::unordered_set<Guid> kept(plan->ids.begin(), plan->ids.end());
    for (const ImageLayer& layer : _session.Document().value().layers)
    {
        if (!kept.contains(layer.id))
        {
            continue;
        }
        ImageLayer copy = layer;
        if (copy.parentID.has_value() && !kept.contains(copy.parentID.value()))
        {
            copy.parentID.reset();
        }
        if (copy.maskSourceID.has_value() && !kept.contains(copy.maskSourceID.value()))
        {
            copy.maskSourceID.reset();
        }
        flat.layers.push_back(std::move(copy));
    }
    BindCompositionDevice();
    _composition.InvalidateDocument();
    bool composed = SUCCEEDED(_composition.Compose(flat));
    std::vector<std::uint8_t> bgra;
    UINT width = 0;
    UINT height = 0;
    composed = composed && SUCCEEDED(_composition.Readback(bgra, width, height)) && width != 0 && height != 0;
    // The next frame composes the real document again.
    _composition.InvalidateDocument();
    if (!composed)
    {
        ShowError(IDS_ERROR_FLATTEN);
        return;
    }
    const Raster composite = Raster::Make(static_cast<int>(width), static_cast<int>(height), RasterFormat::Bgra8888,
                                          bgra.data(), bgra.size());
    if (!composite.HasStorage() || !_session.MergeLayers(plan.value(), composite))
    {
        ShowError(IDS_ERROR_FLATTEN);
        return;
    }
    DocumentChanged();
}

int Application::RunLayerEditsSelfTest() noexcept
{
    if (!_chrome || !_window)
    {
        ReportLayerEditsSelfTestFailure(L"The layer edits self-test needs the chrome and the window.\n");
        return 5;
    }
    _forcedDiscard = true;
    OnCommand(ChromeHost::kCommandNew);
    _forcedDiscard.reset();
    // A user sees frames between opening the card and typing: the chrome lays out and renders in between.
    const auto settle = [&]() noexcept
    {
        (void)_chrome->AdvanceAnimation(GetTickCount64());
        SyncChrome();
        (void)_renderer.Render(_chrome.get(), &_composition, &_viewport, _session.DisplayDocument());
    };
    // Rename: the card opens on the active layer's name, typing replaces it, Enter commits one Rename Layer entry.
    OnCommand(ChromeHost::kCommandRenameLayer);
    settle();
    if (!_chrome->RenameCardOpen())
    {
        ReportLayerEditsSelfTestFailure(L"Rename Layer did not open its card.\n");
        return 5;
    }
    bool typed = true;
    for (const wchar_t character : std::wstring(L"Hero"))
    {
        typed = _chrome->DispatchCharacter(character, 0) && typed;
    }
    const bool entered = _chrome->DispatchKey(VK_RETURN, true, 0);
    (void)_chrome->DispatchKey(VK_RETURN, false, 0);
    settle();
    const ImageLayer* renamed = _session.ActiveLayer();
    if (_chrome->RenameCardOpen() || renamed == nullptr || renamed->name != L"Hero" ||
        _session.history.UndoName() != L"Rename Layer")
    {
        ReportLayerEditsSelfTestFailure(std::format(L"Typing a name and Enter did not rename the layer (open {}, "
                                                    L"typed {}, entered {}, name '{}', undo '{}').\n",
                                                    _chrome->RenameCardOpen(), typed, entered,
                                                    renamed ? renamed->name : L"", _session.history.UndoName()));
        return 5;
    }
    // Escape leaves the name alone.
    OnCommand(ChromeHost::kCommandRenameLayer);
    settle();
    (void)_chrome->DispatchCharacter(L'x', 0);
    (void)_chrome->DispatchKey(VK_ESCAPE, true, 0);
    (void)_chrome->DispatchKey(VK_ESCAPE, false, 0);
    settle();
    if (_chrome->RenameCardOpen() || _session.ActiveLayer() == nullptr || _session.ActiveLayer()->name != L"Hero")
    {
        ReportLayerEditsSelfTestFailure(L"Escape did not leave the layer's name alone.\n");
        return 5;
    }
    // Merge Down: a blue layer above a red one becomes one layer named after the lower one, covering both.
    const auto insert = [&](const wchar_t* name, int width, int height, Vec2 origin, std::uint32_t argb)
    {
        ImageLayer layer = ImageLayer::Blank(name, Vec2{static_cast<double>(width), static_cast<double>(height)});
        const Raster raster = SolidRaster(width, height, argb);
        layer.asset = RasterAsset{raster, Raster::MakeThumbnail(raster), name};
        layer.transform.origin = origin;
        _session.BeginEdit(L"Import Image");
        _session.Document().value().layers.push_back(layer);
        _session.EndEdit();
        _session.SelectLayer(layer.id);
        return layer.id;
    };
    insert(L"Red", 6, 3, Vec2{0, 0}, 0xFFFF0000u);
    insert(L"Blue", 4, 4, Vec2{10, 10}, 0xFF0000FFu);
    DocumentChanged();
    const std::optional<DocumentSession::MergePlan> plan = _session.PlanMerge();
    if (!plan.has_value() || plan->action != L"Merge Down" || plan->name != L"Red")
    {
        ReportLayerEditsSelfTestFailure(L"The merge plan for the top layer is not Merge Down onto Red.\n");
        return 5;
    }
    OnCommand(ChromeHost::kCommandMergeDown);
    const ImageLayer* merged = _session.ActiveLayer();
    const Raster* pixels = merged != nullptr && merged->asset.has_value() ? &merged->asset->image : nullptr;
    const auto pixelAt = [&](int x, int y, int channel) -> int
    {
        if (pixels == nullptr || pixels->Data() == nullptr || x >= pixels->Width() || y >= pixels->Height())
        {
            return -1;
        }
        return pixels->Data()[static_cast<std::size_t>(y) * pixels->Stride() + static_cast<std::size_t>(x) * 4u +
                              static_cast<std::size_t>(channel)];
    };
    if (_session.Document().value().layers.size() != 2 || merged == nullptr || merged->name != L"Red" ||
        _session.history.UndoName() != L"Merge Down" || pixels == nullptr || pixels->Width() != 14 ||
        pixels->Height() != 14 || merged->transform.origin != Vec2{0, 0} || pixelAt(0, 0, 2) != 255 ||
        pixelAt(0, 0, 3) != 255 || pixelAt(13, 13, 0) != 255 || pixelAt(13, 13, 3) != 255 || pixelAt(8, 8, 3) != 0)
    {
        ReportLayerEditsSelfTestFailure(std::format(L"Merge Down did not composite the two layers into one ({} layers, "
                                                    L"name '{}', undo '{}', {} × {}).\n",
                                                    _session.Document().value().layers.size(),
                                                    merged ? merged->name : L"", _session.history.UndoName(),
                                                    pixels ? pixels->Width() : 0, pixels ? pixels->Height() : 0));
        return 5;
    }
    _session.Undo();
    DocumentChanged();
    if (_session.Document().value().layers.size() != 3)
    {
        ReportLayerEditsSelfTestFailure(L"Undo did not restore the merged layers.\n");
        return 5;
    }
    return 0;
}
