#include "Application.h"

#include "ChromeHost.h"
#include "ColorWorkTestGate.h"
#include "RasterEditTestGate.h"
#include "resource.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>

int Application::RunRasterEditSelfTest() noexcept
{
    enum class Mode
    {
        Success,
        Cancel,
        LateCancel,
        Failure,
        Allocation,
        Admission,
        Capture,
        HistoryBefore,
        HistoryAfter,
        HistoryEntry,
        HistoryStack,
        HistoryTrim,
        Invalid,
        Memory,
        TooLarge,
        Unchanged,
        StaleImage,
        StaleTransform,
        StaleMask,
        StaleSelection,
        StaleGeneration,
        Restore,
        Replace,
        Undo,
        LargeRelease
    };
    struct State final
    {
        Application& owner;
        Mode mode;
        ContinuationReceipt receipt;
        Guid revision;
        Raster source;
        bool visited = false;
        bool held = false;
        bool cancelRetained = false;
    };
    const auto dispatch = [](void* context, void* native) noexcept
    {
        auto& state = *static_cast<State*>(context);
        auto& owner = state.owner;
        auto& message = *static_cast<MSG*>(native);
        if (message.message != RasterEditTestGate::kMessage)
        {
            owner.DispatchQueuedMessage(message);
            return;
        }
        state.visited = true;
        state.held = owner._activeHostRequest && owner._rasterEditTicket && owner._rasterEditStamp &&
                     owner._historyWork.Busy() && owner._hostCommitStamp == owner._rasterEditStamp &&
                     owner._jobs.ReservedMemoryBytes() == owner._rasterEditTestGate->reservationBytes &&
                     owner._rasterEditTestGate->captureReservationHeld &&
                     state.receipt.Read().outcome == HostOutcome::Pending &&
                     owner._session.history.CurrentRevision() == state.revision &&
                     owner._session.ActiveLayer()->asset->image == state.source;
        // View commands run while the accepted mutation still owns history. They neither retire its request nor
        // Present the hidden harness. Later palette input cannot change the already captured fill color.
        owner.OnCommand(ChromeHost::kCommandZoomIn);
        owner._session.SetForegroundColor(0xFF0000FF);
        owner._session.SetBackgroundColor(0xFF00FF00);
        if (state.mode == Mode::Cancel || state.mode == Mode::LargeRelease)
        {
            owner.CancelHostContinuations();
            state.cancelRetained = state.receipt.Read().outcome == HostOutcome::Pending && owner._rasterEditTicket &&
                                   owner._historyWork.Busy() && owner._jobs.ReservedMemoryBytes() != 0;
        }
        else if (state.mode == Mode::LateCancel)
        {
            // Only the owner barrier is canceled; the worker returns Finished with prepared bytes. Publication
            // must still be suppressed, and ownership must survive until that completion is acknowledged.
            (void)owner._historyWork.RequestCancel();
            state.cancelRetained = state.receipt.Read().outcome == HostOutcome::Pending && owner._rasterEditTicket;
        }
        else if (state.mode == Mode::StaleImage)
            owner._session.EditForTest([](CanvasDocument& document)
                                       { document.layers.back().asset->image = SolidRaster(4, 2, 0xFF0000FF); });
        else if (state.mode == Mode::StaleTransform)
            owner._session.EditForTest([](CanvasDocument& document)
                                       { document.layers.back().transform.origin.x += 1; });
        else if (state.mode == Mode::StaleMask)
            owner._session.EditForTest([](CanvasDocument& document) { document.layers.back().mask->linked = false; });
        else if (state.mode == Mode::StaleSelection)
            owner._session.Deselect();
        else if (state.mode == Mode::StaleGeneration)
            owner._session.SetOpacity(0.5);
        else if (state.mode == Mode::Restore)
        {
            owner._session.Undo();
            owner._session.Redo();
        }
        else if (state.mode == Mode::Replace)
            owner._session.CreateDocument(3, 2);
        (void)SetEvent(owner._rasterEditTestGate->release.get());
    };
    const auto cleanup = wil::scope_exit(
        [&]() noexcept
        {
            if (_rasterEditTestGate)
                (void)SetEvent(_rasterEditTestGate->release.get());
            if (SettleJobs(_jobs, 15000, DispatchMessageThunk, this) != SettleResult::Settled)
            {
                SelfTestFailure(L"Raster edit cleanup could not settle borrowed harness state.\n");
                std::_Exit(5);
            }
            _rasterEditTestGate.reset();
        });
    const std::array commands{ChromeHost::kCommandFillForeground, ChromeHost::kCommandFillBackground,
                              ChromeHost::kCommandClearPixels, ChromeHost::kCommandDeleteKey,
                              ChromeHost::kCommandInvert};
    for (const bool mask : {false, true})
    {
        for (const auto command : commands)
        {
            for (const bool after : {false, true})
            {
                for (const auto mode :
                     {Mode::Success,         Mode::Cancel,         Mode::LateCancel,   Mode::Failure,
                      Mode::Allocation,      Mode::Admission,      Mode::Capture,      Mode::Invalid,
                      Mode::HistoryBefore,   Mode::HistoryAfter,   Mode::HistoryEntry, Mode::HistoryStack,
                      Mode::HistoryTrim,     Mode::Memory,         Mode::TooLarge,     Mode::Unchanged,
                      Mode::StaleImage,      Mode::StaleTransform, Mode::StaleMask,    Mode::StaleSelection,
                      Mode::StaleGeneration, Mode::Restore,        Mode::Replace,      Mode::Undo,
                      Mode::LargeRelease})
                {
                    // Failure injections precede preparation; the large aggregate-release control needs only one
                    // full-size canceled result. All five production commands still get both ordinary gate stages.
                    const bool historyFailure = mode >= Mode::HistoryBefore && mode <= Mode::HistoryTrim;
                    if ((after && mode == Mode::Allocation) ||
                        (historyFailure && (mask || after || command != ChromeHost::kCommandInvert)) ||
                        (mode == Mode::LargeRelease && (mask || !after || command != ChromeHost::kCommandInvert)))
                        continue;
                    const bool large = mode == Mode::LargeRelease;
                    const int width = large ? 2048 : 4;
                    const int height = large ? 2048 : 2;
                    _session.CreateDocument(width, height);
                    const auto target =
                        InsertPaintedLayer(_session, L"Raster source", width, height, {0, 0}, 0, 0xFFFF0000);
                    _session.EditForTest(
                        [&](CanvasDocument& document)
                        {
                            const std::uint8_t reveal = 255;
                            const auto image = Raster::Make(1, 1, RasterFormat::Gray8, &reveal, 1);
                            document.layers.back().mask = LayerMask{RasterAsset{image, image, L"Raster mask"}};
                            document.selection =
                                DocumentSelection{false, SelectionLoops{{{0, 0}, {1, 0}, {1, 1}, {0, 1}}}};
                            if (large)
                                document.selection.reset();
                        });
                    _session.SelectLayer(target);
                    if (mask)
                        _session.SelectLayerTarget(target, true);
                    _session.SetForegroundColor(0xFF0000FF);
                    _session.SetBackgroundColor(0xFFFFFFFF);
                    _session.maskPaintWhite = false;
                    _session.history.Reset(); // independent baseline; the 100-entry cap has its own legacy tests
                    if (mode == Mode::Restore)
                        _session.SetOpacity(0.5); // an actual restorable entry, then Undo/Redo returns to these pixels
                    _session.history.MarkSaved();
                    DocumentChanged();
                    const auto revision = _session.history.CurrentRevision();
                    const auto count = _session.history.UndoCount();
                    const auto source = _session.ActiveLayer()->asset->image;
                    const auto presents = _renderer.PresentCount();
                    _rasterEditTestGate = std::make_shared<RasterEditTestGate>();
                    if (!_rasterEditTestGate->release)
                        return 5;
                    _rasterEditTestGate->afterPreparation = after;
                    _rasterEditTestGate->throwAllocationFailure = mode == Mode::Allocation;
                    _rasterEditTestGate->throwAdmissionFailure = mode == Mode::Admission;
                    _rasterEditTestGate->throwCaptureFailure = mode == Mode::Capture;
                    if (historyFailure)
                    {
                        constexpr std::array stages{
                            DocumentHistory::PrepareStage::BeforeSnapshot, DocumentHistory::PrepareStage::AfterSnapshot,
                            DocumentHistory::PrepareStage::Entry, DocumentHistory::PrepareStage::StackGrowth,
                            DocumentHistory::PrepareStage::TrimIndex};
                        _rasterEditTestGate->historyFailure =
                            stages[static_cast<std::size_t>(mode) - static_cast<std::size_t>(Mode::HistoryBefore)];
                    }
                    if (mode == Mode::Failure)
                        _rasterEditTestGate->result = {JobStatus::Failed, E_ACCESSDENIED};
                    if (mode == Mode::Invalid)
                        _rasterEditTestGate->status = RasterEditStatus::Invalid;
                    if (mode == Mode::Memory)
                        _rasterEditTestGate->status = RasterEditStatus::Memory;
                    if (mode == Mode::TooLarge)
                        _rasterEditTestGate->status = RasterEditStatus::TooLarge;
                    if (mode == Mode::Unchanged)
                        _rasterEditTestGate->status = RasterEditStatus::Unchanged;
                    State state{*this, mode, QueueHostInput(HostCommandInput{command}), revision, source};
                    std::optional<ContinuationReceipt> undo;
                    if (mode == Mode::Undo)
                        undo = QueueHostInput(HostCommandInput{ChromeHost::kCommandUndo});
                    const auto settled = SettleJobs(_jobs, 15000, dispatch, &state);
                    const auto actual = state.receipt.Read();
                    const auto expected = mode == Mode::Cancel || mode == Mode::LateCancel || large
                                              ? HostOutcome::Cancelled
                                          : mode >= Mode::StaleImage && mode <= Mode::Replace ? HostOutcome::Stale
                                          : mode >= Mode::Failure && mode <= Mode::TooLarge   ? HostOutcome::Failed
                                                                                              : HostOutcome::Succeeded;
                    const bool noGate = mode == Mode::Admission || mode == Mode::Capture;
                    bool correct = settled == SettleResult::Settled && actual.outcome == expected &&
                                   (noGate ? !state.visited : state.visited && state.held) && !_historyWork.Busy() &&
                                   !_rasterEditStamp && !_rasterEditTicket && !_activeHostRequest &&
                                   _hostRequests.Size() == 0 && !_hostInputBudget.Used() &&
                                   _jobs.ReservedMemoryBytes() == 0 && _jobs.ReleasingMemoryBytes() == 0 &&
                                   _renderer.PresentCount() == presents;
                    if (mode == Mode::Cancel || mode == Mode::LateCancel || large)
                        correct = correct && state.cancelRetained && actual.error == E_ABORT;
                    if (mode == Mode::Allocation || mode == Mode::Admission || mode == Mode::Capture ||
                        mode == Mode::Memory || historyFailure)
                        correct = correct && actual.error == E_OUTOFMEMORY;
                    if (historyFailure)
                        correct = correct && _rasterEditTestGate->historyFaultHits.load() == 1 &&
                                  _rasterEditTestGate->payloadRetired.load() &&
                                  _rasterEditTestGate->payloadRetiredOffOwner.load();
                    if (mode == Mode::Failure)
                        correct = correct && actual.error == E_ACCESSDENIED;
                    if (mode == Mode::Invalid)
                        correct = correct && actual.error == E_INVALIDARG;
                    if (mode == Mode::TooLarge)
                        correct = correct && actual.error == HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
                    if (large)
                        correct = correct && _rasterEditTestGate->payloadRetired.load() &&
                                  _rasterEditTestGate->payloadRetiredOffOwner.load();
                    if (mode == Mode::Success)
                    {
                        const auto* layer = _session.ActiveLayer();
                        std::array<std::uint8_t, 4> pixel{};
                        std::array<std::uint8_t, 4> outside{};
                        if (mask)
                        {
                            layer->mask->asset.image.ReadRegion(0, 0, 1, 1, pixel.data(), 1, 255);
                            layer->mask->asset.image.ReadRegion(1, 0, 1, 1, outside.data(), 1, 255);
                            correct = correct &&
                                      pixel[0] == (command == ChromeHost::kCommandFillBackground ||
                                                           command == ChromeHost::kCommandClearPixels ||
                                                           command == ChromeHost::kCommandDeleteKey
                                                       ? 255
                                                       : 0) &&
                                      outside[0] == 255;
                        }
                        else
                        {
                            layer->asset->image.ReadRegion(0, 0, 1, 1, pixel.data(), 4, 0);
                            layer->asset->image.ReadRegion(1, 0, 1, 1, outside.data(), 4, 0);
                            const auto expectedPixel = command == ChromeHost::kCommandFillForeground
                                                           ? std::array<std::uint8_t, 4>{255, 0, 0, 255}
                                                       : command == ChromeHost::kCommandFillBackground
                                                           ? std::array<std::uint8_t, 4>{255, 255, 255, 255}
                                                       : command == ChromeHost::kCommandInvert
                                                           ? std::array<std::uint8_t, 4>{255, 255, 0, 255}
                                                           : std::array<std::uint8_t, 4>{0, 0, 0, 0};
                            correct = correct && pixel == expectedPixel &&
                                      outside == std::array<std::uint8_t, 4>{0, 0, 255, 255};
                        }
                        const auto expectedName = command == ChromeHost::kCommandInvert
                                                      ? (mask ? L"Invert Mask" : L"Invert")
                                                  : (command == ChromeHost::kCommandClearPixels ||
                                                     command == ChromeHost::kCommandDeleteKey) &&
                                                          !mask
                                                      ? L"Clear"
                                                      : (mask ? L"Fill Mask" : L"Fill");
                        correct = correct && _session.history.UndoCount() == count + 1 &&
                                  _session.history.UndoName() == expectedName;
                        _session.Undo();
                        correct = correct && _session.ActiveLayer()->asset->image == source;
                        _session.Redo();
                        correct = correct && _session.history.UndoName() == expectedName;
                    }
                    else if (mode == Mode::Undo)
                        correct = correct && undo->Read().outcome == HostOutcome::Succeeded &&
                                  _session.ActiveLayer()->asset->image == source &&
                                  _session.history.UndoCount() == count;
                    else if (mode == Mode::Replace)
                        correct = correct && _session.Document()->width == 3;
                    else if (expected != HostOutcome::Stale)
                        correct = correct && _session.history.CurrentRevision() == revision &&
                                  _session.ActiveLayer()->asset->image == source &&
                                  _session.history.UndoCount() == count;
                    if (!correct)
                    {
                        std::fprintf(stderr,
                                     "Raster edit command=%d mask=%u after=%u mode=%u visited=%u held=%u outcome=%u "
                                     "expected=%u error=%08x\n",
                                     command, mask ? 1U : 0U, after ? 1U : 0U, static_cast<unsigned int>(mode),
                                     state.visited ? 1U : 0U, state.held ? 1U : 0U,
                                     static_cast<unsigned int>(actual.outcome), static_cast<unsigned int>(expected),
                                     static_cast<unsigned int>(actual.error));
                        std::fprintf(stderr,
                                     "Terminal count=%d expectedCount=%d sourceSame=%u undoOutcome=%u idle=%u host=%u "
                                     "charge=%llu releasing=%llu presents=%llu/%llu\n",
                                     _session.history.UndoCount(), count,
                                     _session.ActiveLayer() && _session.ActiveLayer()->asset &&
                                             _session.ActiveLayer()->asset->image == source
                                         ? 1U
                                         : 0U,
                                     undo ? static_cast<unsigned int>(undo->Read().outcome) : 99U,
                                     _jobs.IsIdle() ? 1U : 0U, _activeHostRequest ? 1U : 0U,
                                     static_cast<unsigned long long>(_jobs.ReservedMemoryBytes()),
                                     static_cast<unsigned long long>(_jobs.ReleasingMemoryBytes()),
                                     static_cast<unsigned long long>(_renderer.PresentCount()),
                                     static_cast<unsigned long long>(presents));
                        SelfTestFailure(L"Raster edit host ordering, bytes or terminal ownership failed.\n");
                        return 5;
                    }
                    std::printf("PASS rasteredit-host.command-%d.mask-%u.after-%u.mode-%u\n", command, mask ? 1U : 0U,
                                after ? 1U : 0U, static_cast<unsigned int>(mode));
                    _rasterEditTestGate.reset();
                }
            }
        }
    }
    // The no-selection Delete is still the metadata action; Clear without selection records nothing.
    for (const auto command : {ChromeHost::kCommandDeleteKey, ChromeHost::kCommandClearPixels})
    {
        _session.CreateDocument(4, 2);
        const auto target = InsertPaintedLayer(_session, L"Delete source", 4, 2, {0, 0}, 0, 0xFFFF0000);
        _session.history.Reset();
        const auto count = _session.history.UndoCount();
        const auto receipt = QueueHostInput(HostCommandInput{command});
        if (SettleJobs(_jobs, 15000, DispatchMessageThunk, this) != SettleResult::Settled ||
            receipt.Read().outcome != HostOutcome::Succeeded || _rasterEditTicket ||
            (command == ChromeHost::kCommandDeleteKey
                 ? IndexOf(_session.Document()->layers, target).has_value() || _session.history.UndoCount() != count + 1
                 : !IndexOf(_session.Document()->layers, target).has_value() || _session.history.UndoCount() != count))
            return 5;
        std::printf("PASS rasteredit-host.no-selection-command-%d\n", command);
    }
    return 0;
}
