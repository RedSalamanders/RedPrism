#include "Application.h"

#include "ChromeHost.h"
#include "Resources.h"
#include "resource.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <string>
#include <unordered_set>
#include <utility>
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
    // The card renames the layer it opened for, whichever layer is active when it commits.
    _renameLayer = active->id;
    _chrome->OpenRenameCard(active->name);
    _frameInvalidated = true;
}

void Application::OnLayerInvoked(Guid id) noexcept
{
    // A double-click on a row renames that layer (legacy: the inline editor of the layer list).
    CloseOpacityEdit();
    _session.SelectLayer(id);
    SyncChrome();
    RenameActiveLayer();
}

void Application::OnRename(std::optional<std::wstring> name) noexcept
{
    const std::optional<Guid> layer = std::exchange(_renameLayer, std::nullopt);
    if (!layer.has_value())
    {
        // The host closed the card (its layer or the document went away): nothing to rename.
        return;
    }
    if (name.has_value())
    {
        _session.RenameLayer(layer.value(), name.value());
    }
    DocumentChanged();
}

void Application::CloseRenameCard() noexcept
{
    _renameLayer.reset();
    if (_chrome && _chrome->RenameCardOpen())
    {
        // The card reports a cancel, which OnRename ignores now that no layer is waiting for a name.
        _chrome->CloseRenameCard(false);
    }
}

ImageLayer* Application::OpacityEditLayer() noexcept
{
    // The typed edit's layer while it still shows the previewed value; anything else changed it, and the edit is
    // over.
    if (!_opacityEdit.has_value() || !_session.Document().has_value())
    {
        _opacityEdit.reset();
        return nullptr;
    }
    for (ImageLayer& layer : _session.Document().value().layers)
    {
        if (layer.id == _opacityEdit->layer)
        {
            if (layer.isGroup || layer.opacity != _opacityEdit->shown)
            {
                break;
            }
            return &layer;
        }
    }
    _opacityEdit.reset();
    return nullptr;
}

void Application::CloseOpacityEdit() noexcept
{
    ImageLayer* layer = OpacityEditLayer();
    if (layer == nullptr)
    {
        return;
    }
    const OpacityEdit edit = _opacityEdit.value();
    _opacityEdit.reset();
    if (edit.shown == edit.start)
    {
        return;
    }
    // History still holds the start value: the previewed one becomes its own entry before anything else records.
    // The field keeps its text, so its Enter or focus loss may still commit this value.
    layer->opacity = edit.start;
    _session.BeginEdit(L"Layer Opacity");
    layer->opacity = edit.shown;
    _session.EndEdit();
    _opacityRecorded = edit.shown;
    DocumentChanged();
}

void Application::OnLayerOpacity(double opacity, ChromeEditPhase phase) noexcept
{
    if (!std::isfinite(opacity))
    {
        return;
    }
    const double value = std::clamp(opacity, 0.0, 1.0);
    const std::optional<double> recorded = std::exchange(_opacityRecorded, std::nullopt);
    ImageLayer* layer = OpacityEditLayer();
    switch (phase)
    {
    case ChromeEditPhase::Preview:
        // Typing previews on the layer the edit started on, outside history; the first preview remembers the value
        // history holds. No transaction stays open, so an edit that ends without a word (the value typed back to
        // where it started) leaves history as it was.
        if (layer == nullptr)
        {
            layer = _session.ActiveLayer();
            if (layer == nullptr || layer->isGroup)
            {
                return;
            }
            _opacityEdit = OpacityEdit{layer->id, layer->opacity, layer->opacity};
        }
        layer->opacity = value;
        _opacityEdit->shown = value;
        break;
    case ChromeEditPhase::Commit:
        if (layer != nullptr)
        {
            // One entry from the value before the first preview to the committed one (none when they are equal).
            const OpacityEdit edit = _opacityEdit.value();
            _opacityEdit.reset();
            layer->opacity = edit.start;
            _session.BeginEdit(L"Layer Opacity");
            layer->opacity = value;
            _session.EndEdit();
        }
        else if (recorded != value)
        {
            // A button, an arrow key or RequestValue: the active layer, one entry.
            _session.SetOpacity(value);
        }
        break;
    case ChromeEditPhase::Cancel:
    default:
        // Escape restores the value history holds; an edit that already closed keeps its entry.
        if (layer != nullptr)
        {
            layer->opacity = _opacityEdit->start;
        }
        _opacityEdit.reset();
        break;
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
    // A full-resolution readback of `flat` (tiled past the texture limit, never the display proxy or a preview); the
    // next frame composes the real document again.
    std::vector<std::uint8_t> bgra;
    UINT width = 0;
    UINT height = 0;
    const bool composed =
        SUCCEEDED(_composition.ComposeReadback(flat, bgra, width, height)) && width != 0 && height != 0;
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
    const auto layerNamed = [&](const wchar_t* name) -> Guid
    {
        for (const ImageLayer& layer : _session.Document().value().layers)
        {
            if (layer.name == name)
            {
                return layer.id;
            }
        }
        return Guid{};
    };
    const auto nameOf = [&](const Guid& id) -> std::wstring
    {
        for (const ImageLayer& layer : _session.Document().value().layers)
        {
            if (layer.id == id)
            {
                return layer.name;
            }
        }
        return {};
    };
    // The Rename card renames the layer it opened for: another row selected while it is open keeps its name.
    const Guid redId = layerNamed(L"Red");
    const Guid blueId = layerNamed(L"Blue");
    _session.SelectLayer(redId);
    SyncChrome();
    OnCommand(ChromeHost::kCommandRenameLayer);
    settle();
    _session.SelectLayer(blueId);
    SyncChrome();
    for (const wchar_t character : std::wstring(L"Kept"))
    {
        (void)_chrome->DispatchCharacter(character, 0);
    }
    (void)_chrome->DispatchKey(VK_RETURN, true, 0);
    (void)_chrome->DispatchKey(VK_RETURN, false, 0);
    settle();
    if (_chrome->RenameCardOpen() || nameOf(redId) != L"Kept" || nameOf(blueId) != L"Blue" ||
        _session.history.UndoName() != L"Rename Layer")
    {
        ReportLayerEditsSelfTestFailure(std::format(L"The Rename card did not rename the layer it opened for (opened "
                                                    L"on Red, committed with Blue active: '{}', '{}').\n",
                                                    nameOf(redId), nameOf(blueId)));
        return 5;
    }
    // It closes once its layer is gone, and when the document is replaced.
    OnCommand(ChromeHost::kCommandNewLayer);
    OnCommand(ChromeHost::kCommandRenameLayer);
    settle();
    const bool openedOnNewLayer = _chrome->RenameCardOpen();
    OnCommand(ChromeHost::kCommandUndo);
    settle();
    const bool closedWithLayer = !_chrome->RenameCardOpen();
    OnCommand(ChromeHost::kCommandRenameLayer);
    settle();
    const bool openedAgain = _chrome->RenameCardOpen();
    _forcedDiscard = true;
    OnCommand(ChromeHost::kCommandNew);
    _forcedDiscard.reset();
    settle();
    if (!openedOnNewLayer || !closedWithLayer || !openedAgain || _chrome->RenameCardOpen())
    {
        ReportLayerEditsSelfTestFailure(std::format(L"The Rename card outlived its layer or its document (opened {}, "
                                                    L"closed by Undo {}, reopened {}, open after New {}).\n",
                                                    openedOnNewLayer, closedWithLayer, openedAgain,
                                                    _chrome->RenameCardOpen()));
        return 5;
    }
    // The opacity stepper's typed edit: no path leaves history open or bakes a preview into another entry.
    const std::optional<Guid> activeId = _session.ActiveLayerID();
    if (!activeId.has_value())
    {
        ReportLayerEditsSelfTestFailure(L"The new document has no active layer for the opacity checks.\n");
        return 5;
    }
    const Guid target = activeId.value();
    const auto opacityOf = [&](const Guid& id) -> double
    {
        for (const ImageLayer& layer : _session.Document().value().layers)
        {
            if (layer.id == id)
            {
                return layer.opacity;
            }
        }
        return -1.0;
    };
    // Typed back to its start value ("100" → "10" → "100", Enter): the field sends no commit at all.
    int entries = _session.history.UndoCount();
    OnLayerOpacity(0.1, ChromeEditPhase::Preview);
    const bool previewed = opacityOf(target) == 0.1;
    OnLayerOpacity(1.0, ChromeEditPhase::Preview);
    OnCommand(ChromeHost::kCommandNewLayer);
    if (!previewed || !_session.history.CanUndo() || _session.history.UndoName() != L"New Blank Layer" ||
        _session.history.UndoCount() != entries + 1 || opacityOf(target) != 1.0)
    {
        ReportLayerEditsSelfTestFailure(std::format(L"A typed opacity that ended at its start value left history open "
                                                    L"(can undo {}, undo '{}', {} entries, expected {}).\n",
                                                    _session.history.CanUndo(), _session.history.UndoName(),
                                                    _session.history.UndoCount(), entries + 1));
        return 5;
    }
    OnCommand(ChromeHost::kCommandUndo);
    // A command runs before the field sees Enter (Ctrl+G): the typed value records its own entry first, the folder
    // its own, and the field's late commit of the same value adds nothing to the folder now active.
    _session.SelectLayer(target);
    SyncChrome();
    entries = _session.history.UndoCount();
    OnLayerOpacity(0.4, ChromeEditPhase::Preview);
    OnCommand(ChromeHost::kCommandNewFolder);
    OnLayerOpacity(0.4, ChromeEditPhase::Commit);
    const bool folderLast = _session.history.UndoName() == L"New Folder" && _session.history.CanUndo() &&
                            _session.history.UndoCount() == entries + 2 && opacityOf(target) == 0.4;
    OnCommand(ChromeHost::kCommandUndo);
    const bool opacityBelow = _session.history.UndoName() == L"Layer Opacity" && opacityOf(target) == 0.4;
    OnCommand(ChromeHost::kCommandUndo);
    if (!folderLast || !opacityBelow || opacityOf(target) != 1.0)
    {
        ReportLayerEditsSelfTestFailure(std::format(L"A command during a typed opacity did not record the opacity as "
                                                    L"its own entry (folder last {}, opacity below {}, opacity {}).\n",
                                                    folderLast, opacityBelow, opacityOf(target)));
        return 5;
    }
    // A new document closes the edit with the old history; the field's late commit changes nothing in the new one.
    OnLayerOpacity(0.3, ChromeEditPhase::Preview);
    _forcedDiscard = true;
    OnCommand(ChromeHost::kCommandNew);
    _forcedDiscard.reset();
    OnLayerOpacity(0.3, ChromeEditPhase::Commit);
    const ImageLayer* fresh = _session.ActiveLayer();
    if (fresh == nullptr || fresh->opacity != 1.0 || _session.history.IsModified() ||
        _session.history.UndoName() != L"New Canvas")
    {
        ReportLayerEditsSelfTestFailure(std::format(L"A typed opacity carried over into the new document (opacity {}, "
                                                    L"modified {}, undo '{}').\n",
                                                    fresh != nullptr ? fresh->opacity : -1.0,
                                                    _session.history.IsModified(), _session.history.UndoName()));
        return 5;
    }
    // Escape restores the start value without an entry; a commit records one entry from before the first preview.
    const Guid freshId = fresh->id;
    entries = _session.history.UndoCount();
    OnLayerOpacity(0.6, ChromeEditPhase::Preview);
    OnLayerOpacity(1.0, ChromeEditPhase::Cancel);
    const bool cancelled =
        opacityOf(freshId) == 1.0 && _session.history.UndoCount() == entries && !_session.history.IsModified();
    OnLayerOpacity(0.2, ChromeEditPhase::Preview);
    OnLayerOpacity(0.25, ChromeEditPhase::Preview);
    OnLayerOpacity(0.25, ChromeEditPhase::Commit);
    const bool committed = opacityOf(freshId) == 0.25 && _session.history.UndoCount() == entries + 1 &&
                           _session.history.UndoName() == L"Layer Opacity";
    OnCommand(ChromeHost::kCommandUndo);
    if (!cancelled || !committed || opacityOf(freshId) != 1.0)
    {
        ReportLayerEditsSelfTestFailure(std::format(L"The opacity stepper's Escape or commit did not record as one "
                                                    L"entry (cancelled {}, committed {}, opacity {}).\n",
                                                    cancelled, committed, opacityOf(freshId)));
        return 5;
    }
    return 0;
}
