#include "Application.h"

#include "ChromeHost.h"
#include "Resources.h"
#include "Selection.h"
#include "resource.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <new>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

// The Layer menu's rename and merge (C16, Specs/UI/UI_ApplicationChrome.md "Layers panel"): the Rename Layer card
// and Ctrl+E, which composites the planned layers through the canvas composition and installs the result.

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
    if (!_hostExecuting && (_historyWork.Busy() || _session.HasColorEdit() || _session.PendingGradient() ||
                            _opacityEdit || _activeHostRequest || _hostRequests.Size()))
    {
        ReportHostAdmission(QueueHostInput(HostLayerInvokeInput{id}));
        return;
    }
    // A double-click on a row renames that layer (legacy: the inline editor of the layer list).
    CloseOpacityEdit();
    _session.SelectLayer(id);
    SyncChrome();
    RenameActiveLayer();
}

void Application::OnRename(std::optional<std::wstring_view> name) noexcept
{
    const std::optional<Guid> layer = std::exchange(_renameLayer, std::nullopt);
    if (!layer.has_value())
    {
        // The host closed the card (its layer or the document went away): nothing to rename.
        return;
    }
    if (name.has_value())
    {
        try
        {
            auto request = HostRequest::TryCopyRename(layer.value(), name, _hostInputBudget);
            ReportHostAdmission(request ? QueueHostRequest(std::move(request.value()))
                                        : ContinuationReceipt::Failure(HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA)));
        }
        catch (const std::bad_alloc&)
        {
            ReportHostAdmission(ContinuationReceipt::Failure(E_OUTOFMEMORY));
        }
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

const ImageLayer* Application::OpacityEditLayer() noexcept
{
    // The typed edit's layer while it still shows the previewed value; anything else changed it, and the edit is
    // over.
    if (!_opacityEdit.has_value() || !_session.Document().has_value())
    {
        _opacityEdit.reset();
        return nullptr;
    }
    for (const ImageLayer& layer : _session.Document().value().layers)
    {
        if (layer.id == _opacityEdit->layer)
        {
            if (layer.isGroup || layer.opacity != _opacityEdit->shown ||
                _opacityEdit->documentSerial != _session.DocumentSerial())
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
    if (!_hostExecuting && (_historyWork.Busy() || _activeHostRequest || _hostRequests.Size()))
    {
        return; // the selected continuation settles it after the admitted predecessor finishes
    }
    const ImageLayer* layer = OpacityEditLayer();
    if (layer == nullptr)
    {
        return;
    }
    const OpacityEdit edit = _opacityEdit.value();
    if (edit.shown == edit.start)
    {
        _opacityEdit.reset();
        return;
    }
    if (!_hostExecuting)
        return; // Changed edits require the selected continuation's acknowledged worker closure.
    _opacityEdit.reset();
    // History still holds the start value: the previewed one becomes its own entry before anything else records.
    // The field keeps its text, so its Enter or focus loss may still commit this value.
    if (!_session.PreviewLayerOpacity(edit.layer, edit.start))
    {
        return;
    }
    _session.BeginEdit(L"Layer Opacity");
    (void)_session.PreviewLayerOpacity(edit.layer, edit.shown);
    _session.EndEdit();
    _opacityRecorded = edit.shown;
    DocumentChanged();
}

bool Application::NewerOpacityInputPending(Guid target, OpacityInputOrder::Stamp order) const noexcept
{
    if (!order.stream)
        return false;
    for (std::size_t i = 0; i < _hostRequests.Size(); ++i)
    {
        const auto* entry = _hostRequests.PendingAt(i);
        const auto* input = std::get_if<HostLayerOpacityInput>(&entry->input.Input());
        // Another accepted input owns a barrier; a later field stream cannot erase its predecessor.
        if (!input || entry->documentSerial != _session.DocumentSerial() || input->id != target ||
            input->order.stream != order.stream)
            return false;
        if (input->order.sequence > order.sequence)
            return true;
    }
    return false;
}

void Application::AcknowledgeOpacityMetadata(Guid target, double value, OpacityMetadataContext context) noexcept
{
    if (!_opacityEdit || _opacityEdit->opening != context.opening || _opacityEdit->layer != target ||
        _opacityEdit->documentSerial != _session.DocumentSerial())
        return;
    if (context.action == OpacityAcknowledgement::Preview)
        _opacityEdit->shown = value;
    else
    {
        if (context.action == OpacityAcknowledgement::Close)
            _opacityRecorded = value;
        _opacityEdit.reset();
    }
}

HRESULT Application::StartOpacityClosure() noexcept
{
    if (!OpacityEditLayer())
        return S_OK;
    const auto edit = _opacityEdit.value();
    const auto previousResume = _hostAfterPrerequisite;
    const auto result = StartLayerMetadata(LayerMetadataKind::SetOpacity, edit.layer, {}, edit.shown,
                                           LayerBlendMode::Normal, OpacityMetadataIntent::TypedCommit, edit.start,
                                           OpacityMetadataContext{edit.opening, {}, OpacityAcknowledgement::Close});
    _hostAfterPrerequisite = previousResume;
    return result;
}

void Application::OnLayerOpacity(double opacity, ChromeEditPhase phase) noexcept
{
    // The disabled stepper must not settle another panel implicitly.
    if (!std::isfinite(opacity) || (_session.HasColorEdit() && phase != ChromeEditPhase::Cancel))
        return;
    if (const auto id = _session.ActiveLayerID())
        ReportHostAdmission(QueueHostInput(HostLayerOpacityInput{id.value(), opacity, phase}));
}

void Application::ExecuteLayerOpacityInput(const HostLayerOpacityInput& request) noexcept
{
    const double opacity = request.opacity;
    const auto phase = request.phase;
    if (!_activeHostRequest)
    {
        _hostExecutionError = E_UNEXPECTED;
        return;
    }
    const auto* input = std::get_if<HostLayerOpacityInput>(&_activeHostRequest->input.Input());
    if (!input || input->order.sequence == 0)
    {
        _hostExecutionError = E_INVALIDARG;
        return;
    }
    const double value = std::clamp(opacity, 0.0, 1.0);
    const auto recorded = std::exchange(_opacityRecorded, std::nullopt);
    const ImageLayer* layer = OpacityEditLayer();
    if (phase == ChromeEditPhase::Preview && !layer)
    {
        layer = _session.ActiveLayer();
        if (!layer || layer->isGroup)
            return;
        if (_opacityOpening == UINT64_MAX)
        {
            _hostExecutionError = HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
            return;
        }
        _opacityEdit =
            OpacityEdit{layer->id, layer->opacity, layer->opacity, ++_opacityOpening, _session.DocumentSerial()};
    }
    if (layer)
    {
        const auto edit = _opacityEdit.value();
        const bool commit = phase == ChromeEditPhase::Commit;
        const bool cancel = phase == ChromeEditPhase::Cancel;
        const double final = cancel ? edit.start : value;
        _hostExecutionError =
            StartLayerMetadata(LayerMetadataKind::SetOpacity, edit.layer, {}, final, LayerBlendMode::Normal,
                               commit ? OpacityMetadataIntent::TypedCommit : OpacityMetadataIntent::Preview,
                               commit ? std::optional(edit.start) : std::nullopt,
                               OpacityMetadataContext{edit.opening, input->order,
                                                      cancel   ? OpacityAcknowledgement::Cancel
                                                      : commit ? OpacityAcknowledgement::Commit
                                                               : OpacityAcknowledgement::Preview});
    }
    else if (phase == ChromeEditPhase::Commit && recorded != value)
    {
        if (const auto* active = _session.ActiveLayer(); active && !active->isGroup)
            _hostExecutionError = StartLayerMetadata(LayerMetadataKind::SetOpacity, active->id, {}, value);
    }
    if (FAILED(_hostExecutionError))
        _prompts->PostNotice(_window.get(), kNoticeMessage, IDS_ERROR_LAYER_METADATA);
}

void Application::MergeLayersCommand() noexcept
{
    // The merge composes what the canvas shows: a pending transform applies and a pending gradient commits first,
    // each as its own entry (Decision D3), before the plan is made and its layers composed. The session would commit
    // the gradient only inside MergeLayers, after the composite was read without it.
    (void)EndCanvasEdit(CanvasEdit::Transform, EditEnd::Enter);
    CommitGradient();
    _hostExecutionError = StartMergeReadback();
    if (FAILED(_hostExecutionError))
        _prompts->PostNotice(_window.get(), kNoticeMessage, IDS_ERROR_FLATTEN);
}

int Application::RunLayerEditsSelfTest() noexcept
{
    if (!_chrome || !_window)
    {
        SelfTestFailure(L"The layer edits self-test needs the chrome and the window.\n");
        return 5;
    }
    _forcedDiscard = true;
    OnCommand(ChromeHost::kCommandNew);
    if (SettleJobs(_jobs, 10000, DispatchMessageThunk, this) != SettleResult::Settled)
        return 5;
    _forcedDiscard.reset();
    // A user sees frames between opening the card and typing: the chrome lays out and renders in between.
    const auto settle = [&]() noexcept
    {
        if (SettleJobs(_jobs, 10000, DispatchMessageThunk, this) != SettleResult::Settled)
        {
            SelfTestFailure(L"The layer harness could not settle admitted work.\n");
            std::_Exit(5);
        }
        (void)_chrome->AdvanceAnimation(GetTickCount64());
        SyncChrome();
        (void)_renderer.Render(_chrome.get(), &_composition, &_viewport, _session.DisplayDocument());
    };
    // Rename: the card opens on the active layer's name, typing replaces it, Enter commits one Rename Layer entry.
    OnCommand(ChromeHost::kCommandRenameLayer);
    settle();
    if (!_chrome->RenameCardOpen())
    {
        SelfTestFailure(L"Rename Layer did not open its card.\n");
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
        SelfTestFailure(std::format(L"Typing a name and Enter did not rename the layer (open {}, "
                                    L"typed {}, entered {}, name '{}', undo '{}').\n",
                                    _chrome->RenameCardOpen(), typed, entered, renamed ? renamed->name : L"",
                                    _session.history.UndoName()));
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
        SelfTestFailure(L"Escape did not leave the layer's name alone.\n");
        return 5;
    }
    // Merge Down: a blue layer above a red one becomes one layer named after the lower one, covering both.
    // A painted layer added on top (InsertPaintedLayer), without the DocumentChanged the caller does once per pair.
    const auto insert = [&](const wchar_t* name, int width, int height, Vec2 origin, std::uint32_t argb)
    { return InsertPaintedLayer(_session, name, width, height, origin, 0.0, argb); };
    insert(L"Red", 6, 3, Vec2{0, 0}, 0xFFFF0000u);
    insert(L"Blue", 4, 4, Vec2{10, 10}, 0xFF0000FFu);
    DocumentChanged();
    const std::optional<DocumentSession::MergePlan> plan = _session.PlanMerge();
    if (!plan.has_value() || plan->action != L"Merge Down" || plan->name != L"Red")
    {
        SelfTestFailure(L"The merge plan for the top layer is not Merge Down onto Red.\n");
        return 5;
    }
    OnCommand(ChromeHost::kCommandMergeDown);
    settle(); // observe the asynchronous result before inspecting committed pixels
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
        SelfTestFailure(std::format(L"Merge Down did not composite the two layers into one ({} layers, "
                                    L"name '{}', undo '{}', {} × {}).\n",
                                    _session.Document().value().layers.size(), merged ? merged->name : L"",
                                    _session.history.UndoName(), pixels ? pixels->Width() : 0,
                                    pixels ? pixels->Height() : 0));
        return 5;
    }
    _session.Undo();
    DocumentChanged();
    if (_session.Document().value().layers.size() != 3)
    {
        SelfTestFailure(L"Undo did not restore the merged layers.\n");
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
        SelfTestFailure(std::format(L"The Rename card did not rename the layer it opened for (opened "
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
    if (SettleJobs(_jobs, 10000, DispatchMessageThunk, this) != SettleResult::Settled)
        return 5;
    _forcedDiscard.reset();
    settle();
    if (!openedOnNewLayer || !closedWithLayer || !openedAgain || _chrome->RenameCardOpen())
    {
        SelfTestFailure(std::format(L"The Rename card outlived its layer or its document (opened {}, "
                                    L"closed by Undo {}, reopened {}, open after New {}).\n",
                                    openedOnNewLayer, closedWithLayer, openedAgain, _chrome->RenameCardOpen()));
        return 5;
    }
    // The opacity stepper's typed edit: no path leaves history open or bakes a preview into another entry.
    const std::optional<Guid> activeId = _session.ActiveLayerID();
    if (!activeId.has_value())
    {
        SelfTestFailure(L"The new document has no active layer for the opacity checks.\n");
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
    settle();
    const bool previewed = opacityOf(target) == 0.1;
    OnLayerOpacity(1.0, ChromeEditPhase::Preview);
    OnCommand(ChromeHost::kCommandNewLayer);
    settle();
    if (!previewed || !_session.history.CanUndo() || _session.history.UndoName() != L"New Blank Layer" ||
        _session.history.UndoCount() != entries + 1 || opacityOf(target) != 1.0)
    {
        SelfTestFailure(std::format(L"A typed opacity that ended at its start value left history open "
                                    L"(can undo {}, undo '{}', {} entries, expected {}).\n",
                                    _session.history.CanUndo(), _session.history.UndoName(),
                                    _session.history.UndoCount(), entries + 1));
        return 5;
    }
    OnCommand(ChromeHost::kCommandUndo);
    settle();
    // A command runs before the field sees Enter (Ctrl+G): the typed value records its own entry first, the folder
    // its own, and the field's late commit of the same value adds nothing to the folder now active.
    _session.SelectLayer(target);
    SyncChrome();
    entries = _session.history.UndoCount();
    OnLayerOpacity(0.4, ChromeEditPhase::Preview);
    OnCommand(ChromeHost::kCommandNewFolder);
    OnLayerOpacity(0.4, ChromeEditPhase::Commit);
    settle();
    const bool folderLast = _session.history.UndoName() == L"New Folder" && _session.history.CanUndo() &&
                            _session.history.UndoCount() == entries + 2 && opacityOf(target) == 0.4;
    OnCommand(ChromeHost::kCommandUndo);
    settle();
    const bool opacityBelow = _session.history.UndoName() == L"Layer Opacity" && opacityOf(target) == 0.4;
    OnCommand(ChromeHost::kCommandUndo);
    settle();
    if (!folderLast || !opacityBelow || opacityOf(target) != 1.0)
    {
        SelfTestFailure(std::format(L"A command during a typed opacity did not record the opacity as "
                                    L"its own entry (folder last {}, opacity below {}, opacity {}).\n",
                                    folderLast, opacityBelow, opacityOf(target)));
        return 5;
    }
    // A new document closes the edit with the old history; the field's late commit changes nothing in the new one.
    OnLayerOpacity(0.3, ChromeEditPhase::Preview);
    _forcedDiscard = true;
    OnCommand(ChromeHost::kCommandNew);
    if (SettleJobs(_jobs, 10000, DispatchMessageThunk, this) != SettleResult::Settled)
        return 5;
    _forcedDiscard.reset();
    OnLayerOpacity(0.3, ChromeEditPhase::Commit);
    settle();
    const ImageLayer* fresh = _session.ActiveLayer();
    if (fresh == nullptr || fresh->opacity != 1.0 || _session.history.IsModified() ||
        _session.history.UndoName() != L"New Canvas")
    {
        SelfTestFailure(std::format(L"A typed opacity carried over into the new document (opacity {}, "
                                    L"modified {}, undo '{}').\n",
                                    fresh != nullptr ? fresh->opacity : -1.0, _session.history.IsModified(),
                                    _session.history.UndoName()));
        return 5;
    }
    // Escape restores the start value without an entry; a commit records one entry from before the first preview.
    const Guid freshId = fresh->id;
    entries = _session.history.UndoCount();
    OnLayerOpacity(0.6, ChromeEditPhase::Preview);
    OnLayerOpacity(1.0, ChromeEditPhase::Cancel);
    settle();
    const bool cancelled =
        opacityOf(freshId) == 1.0 && _session.history.UndoCount() == entries && !_session.history.IsModified();
    OnLayerOpacity(0.2, ChromeEditPhase::Preview);
    OnLayerOpacity(0.25, ChromeEditPhase::Preview);
    OnLayerOpacity(0.25, ChromeEditPhase::Commit);
    settle();
    const bool committed = opacityOf(freshId) == 0.25 && _session.history.UndoCount() == entries + 1 &&
                           _session.history.UndoName() == L"Layer Opacity";
    OnCommand(ChromeHost::kCommandUndo);
    settle();
    if (!cancelled || !committed || opacityOf(freshId) != 1.0)
    {
        SelfTestFailure(std::format(L"The opacity stepper's Escape or commit did not record as one "
                                    L"entry (cancelled {}, committed {}, opacity {}).\n",
                                    cancelled, committed, opacityOf(freshId)));
        return 5;
    }
    // Merge composes what the canvas shows (RF-1104): a gradient pending on the upper layer commits first as its own
    // entry, and the merged layer holds it instead of the blue it covered.
    {
        _forcedDiscard = true;
        OnCommand(ChromeHost::kCommandNew);
        if (SettleJobs(_jobs, 10000, DispatchMessageThunk, this) != SettleResult::Settled)
            return 5;
        _forcedDiscard.reset();
        insert(L"Base", 40, 20, Vec2{0, 0}, 0xFFFF0000u);
        insert(L"Top", 40, 20, Vec2{0, 0}, 0xFF0000FFu);
        DocumentChanged();
        OnTool(EditorTool::Gradient);
        _session.SetForegroundColor(0xFF000000u);
        _session.SetBackgroundColor(0xFFFFFFFFu);
        _session.gradientSettings = GradientSettings{};
        BeginGradientDrag(_viewport.ViewFromDocument(Vec2{0, 10}, DocumentSize()));
        DragGradient(_viewport.ViewFromDocument(Vec2{40, 10}, DocumentSize()), 0);
        EndGradientDrag();
        const bool pending = _session.PendingGradient() != nullptr;
        OnCommand(ChromeHost::kCommandMergeDown);
        settle();
        const ImageLayer* result = _session.ActiveLayer();
        std::array<std::uint8_t, 4> start{};
        if (result != nullptr && result->asset.has_value())
        {
            const Vec2 pixel =
                PixelToDocument(result->transform, result->asset->image.Width(), result->asset->image.Height())
                    .Inverted()
                    .Apply(Vec2{1.5, 10.5});
            result->asset->image.ReadRegion(static_cast<int>(std::floor(pixel.x)),
                                            static_cast<int>(std::floor(pixel.y)), 1, 1, start.data(), 4, 0);
        }
        const std::wstring mergeEntry = _session.history.UndoName();
        _session.Undo();
        const std::wstring gradientEntry = _session.history.UndoName();
        DocumentChanged();
        OnTool(EditorTool::Move);
        if (!pending || mergeEntry != L"Merge Down" || gradientEntry != L"Gradient" || start[0] > 40 || start[2] > 40 ||
            start[3] != 255)
        {
            SelfTestFailure(std::format(L"Merge Down under a pending gradient did not merge it (pending "
                                        L"{}, entries '{}' over '{}', merged start {} {} {} {}).\n",
                                        pending, mergeEntry, gradientEntry, start[0], start[1], start[2], start[3]));
            return 5;
        }
    }
    // A layer's mask stays reachable once another layer was chosen (RF-907, legacy NativeLayerList): the Layer menu's
    // Target Layer Mask switches the target and back and is grayed without a mask; Unlink and Disable Layer Mask
    // record their entries and retitle; the row's Mask badge shows the target in the accent and a disabled mask as
    // `Mask off`; a press on another row's badge chooses that layer and targets its mask (Shift turning the mask on
    // again), and a press on the row's name targets its pixels.
    {
        _forcedDiscard = true;
        OnCommand(ChromeHost::kCommandNew);
        if (SettleJobs(_jobs, 10000, DispatchMessageThunk, this) != SettleResult::Settled)
            return 5;
        _forcedDiscard.reset();
        const Guid masked = insert(L"Masked", 40, 20, Vec2{0, 0}, 0xFFFF0000u);
        OnCommand(ChromeHost::kCommandAddMask);
        settle();
        const Guid other = insert(L"Other", 40, 20, Vec2{0, 40}, 0xFF0000FFu);
        DocumentChanged();
        settle();
        const auto maskOf = [&]() -> const LayerMask*
        {
            for (const ImageLayer& layer : _session.Document().value().layers)
            {
                if (layer.id == masked && layer.mask.has_value())
                {
                    return &layer.mask.value();
                }
            }
            return nullptr;
        };
        const auto captionIs = [&](int command, UINT stringId)
        {
            return PlainCommandName(_chrome->MenuItemCaption(command)) ==
                   PlainCommandName(LoadStringResource(nullptr, stringId));
        };
        const auto rowOf = [&](const Guid& id)
        {
            const std::vector<HierarchyEntry> rows = _session.LayerRows();
            std::size_t index = 0;
            while (index < rows.size() && rows[index].id != id)
            {
                ++index;
            }
            return index;
        };
        const bool grayed = _chrome->MenuItemGrayedForTest(ChromeHost::kCommandTargetLayerMask) &&
                            _chrome->MenuItemGrayedForTest(ChromeHost::kCommandToggleMaskLink) &&
                            _chrome->MenuItemGrayedForTest(ChromeHost::kCommandToggleLayerMask);
        OnLayerSelected(masked);
        const bool pixelsFirst = !_session.IsMaskSelected() &&
                                 !_chrome->MenuItemGrayedForTest(ChromeHost::kCommandTargetLayerMask) &&
                                 captionIs(ChromeHost::kCommandTargetLayerMask, IDS_CMD_TARGET_MASK);
        OnCommand(ChromeHost::kCommandTargetLayerMask);
        settle();
        const DxUi::TreeItemData targetedRow = _chrome->LayerRowItemForTest(rowOf(masked));
        const bool targeted = _session.IsMaskSelected() && _session.ActiveLayerID() == std::optional<Guid>(masked) &&
                              captionIs(ChromeHost::kCommandTargetLayerMask, IDS_CMD_TARGET_PIXELS) &&
                              targetedRow.badgeText == LoadStringResource(nullptr, IDS_LAYER_MASK_BADGE) &&
                              targetedRow.badgeTone == DxUi::AdornmentTone::Accent;
        OnCommand(ChromeHost::kCommandTargetLayerMask);
        const bool back = !_session.IsMaskSelected() &&
                          _chrome->LayerRowItemForTest(rowOf(masked)).badgeTone == DxUi::AdornmentTone::Info;
        OnCommand(ChromeHost::kCommandToggleMaskLink);
        const bool unlinked = maskOf() != nullptr && !maskOf()->linked &&
                              _session.history.UndoName() == L"Unlink Layer Mask" &&
                              captionIs(ChromeHost::kCommandToggleMaskLink, IDS_CMD_LINK_MASK);
        OnCommand(ChromeHost::kCommandToggleLayerMask);
        settle();
        const DxUi::TreeItemData offRow = _chrome->LayerRowItemForTest(rowOf(masked));
        const bool disabled = maskOf() != nullptr && !maskOf()->enabled &&
                              _session.history.UndoName() == L"Disable Layer Mask" &&
                              captionIs(ChromeHost::kCommandToggleLayerMask, IDS_CMD_ENABLE_MASK) &&
                              offRow.badgeText == LoadStringResource(nullptr, IDS_LAYER_MASK_OFF_BADGE) &&
                              offRow.badgeTone == DxUi::AdornmentTone::Error;
        // The badge takes a press of its own: Shift turns the mask on again; the row's name then targets the pixels.
        const float scale = static_cast<float>(GetDpiForWindow(_window.get())) / 96.0f;
        const auto press = [&](float xDip, float yDip, WPARAM keys)
        {
            const LPARAM at =
                MAKELPARAM(static_cast<int>(std::lround(xDip * scale)), static_cast<int>(std::lround(yDip * scale)));
            OnPointer(WM_MOUSEMOVE, keys, at);
            OnPointer(WM_LBUTTONDOWN, MK_LBUTTON | keys, at);
            OnPointer(WM_LBUTTONUP, keys, at);
            settle();
        };
        OnLayerSelected(other);
        settle();
        const D2D1_RECT_F badge = _chrome->LayerMaskBadgeBoundsDip(rowOf(masked));
        press((badge.left + badge.right) / 2.0f, (badge.top + badge.bottom) / 2.0f, MK_SHIFT);
        const bool shiftBadge = _session.ActiveLayerID() == std::optional<Guid>(masked) && _session.IsMaskSelected() &&
                                maskOf() != nullptr && maskOf()->enabled &&
                                _session.history.UndoName() == L"Enable Layer Mask" && !_chrome->RenameCardOpen();
        const D2D1_RECT_F row = _chrome->LayerRowBoundsDip(rowOf(masked));
        press(row.left + 24.0f, (row.top + row.bottom) / 2.0f, 0);
        const bool name = _session.ActiveLayerID() == std::optional<Guid>(masked) && !_session.IsMaskSelected() &&
                          !_chrome->RenameCardOpen();
        OnLayerSelected(other);
        settle();
        press((badge.left + badge.right) / 2.0f, (badge.top + badge.bottom) / 2.0f, 0);
        const bool plainBadge = _session.ActiveLayerID() == std::optional<Guid>(masked) && _session.IsMaskSelected() &&
                                maskOf() != nullptr && maskOf()->enabled;
        if (!grayed || !pixelsFirst || !targeted || !back || !unlinked || !disabled || badge.right <= badge.left ||
            !shiftBadge || !name || !plainBadge)
        {
            SelfTestFailure(std::format(
                L"A layer's mask could not be targeted again (grayed {}, pixels first {}, targeted {}, back {}, "
                L"unlinked {}, disabled {}, badge {} {}, Shift on badge {}, name {}, badge {}).\n",
                grayed, pixelsFirst, targeted, back, unlinked, disabled, badge.left, badge.right, shiftBadge, name,
                plainBadge));
            return 5;
        }
    }
    return 0;
}
