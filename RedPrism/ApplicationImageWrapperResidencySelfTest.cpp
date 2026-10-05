#include "Application.h"

#include "../Tests/Support/DocumentFixture.h"
#include "ChromeHost.h"
#include "LayerCopyWorkTestGate.h"
#include "MaskWorkTestGate.h"
#include "ShapeWorkTestGate.h"
#include "resource.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <type_traits>

int Application::RunImageWrapperResidencySelfTest(unsigned int kind) noexcept
{
    if (kind > 2)
        return 5;
    const auto originalBudget = _session.history.RetainedByteLimit();
    const auto cleanup = wil::scope_exit(
        [&]() noexcept
        {
            if (_maskWorkTestGate)
                (void)SetEvent(_maskWorkTestGate->release.get());
            if (_layerCopyWorkTestGate)
                (void)SetEvent(_layerCopyWorkTestGate->release.get());
            if (_shapeWorkTestGate)
                (void)SetEvent(_shapeWorkTestGate->release.get());
            CancelHostContinuations();
            if (SettleJobs(_jobs, 25000, DispatchMessageThunk, this) != SettleResult::Settled)
                std::_Exit(5);
            _maskWorkTestGate.reset();
            _layerCopyWorkTestGate.reset();
            _shapeWorkTestGate.reset();
            _session.history = DocumentHistory{kHistoryEntryLimit, originalBudget};
        });
    for (const bool alternate : {false, true})
        for (const unsigned int mode : {0U, 1U, 2U})
        {
            if (kind == 2 && alternate)
                continue;
            if (SettleJobs(_jobs, 25000, DispatchMessageThunk, this) != SettleResult::Settled)
                return 5;
            _session.history = DocumentHistory{kHistoryEntryLimit, originalBudget};
            _session.ClearDocument();
            _session.CreateDocument(2, 1);
            (void)InsertPaintedLayer(_session, L"Wrapper residency source", 2, 1, {}, 0, 0xFF404040);
            constexpr std::array<std::uint8_t, 4> leafBytes{7, 11, 13, 255};
            constexpr std::array<std::uint8_t, 4> sourceBytes{31, 37, 41, 255};
            const auto leaf = Raster::Make(1, 1, RasterFormat::Bgra8888, leafBytes.data(), leafBytes.size());
            const auto sparse =
                Raster::MakeSparse(2, 1, RasterFormat::Bgra8888, {}, 0, 0, {RasterTile{0, 0, leaf}}, 23);
            const auto image = Raster::Make(1, 1, RasterFormat::Bgra8888, sourceBytes.data(), sourceBytes.size());
            EditLayerRecordsForTest(
                _session,
                [&](DocumentFixture& fixture)
                {
                    fixture.layers.back().asset->image = sparse;
                    fixture.layers.back().asset->thumbnail = {};
                    if (kind == 1 && alternate)
                        fixture.selection = DocumentSelection{
                            false, SelectionLoops{SelectionLoop{Vec2{0, 0}, Vec2{2, 0}, Vec2{2, 1}, Vec2{0, 1}}}};
                });
            const auto selectionBytes = _session.Selection() ? _session.Selection()->loops.StorageBytes() : 0;
            _session.history = DocumentHistory{kHistoryEntryLimit, selectionBytes + 8};
            _session.history.Begin(L"Older sparse wrapper source", _session.Document(), _session.ActiveLayerID());
            EditLayerRecordsForTest(_session,
                                    [&](DocumentFixture& fixture) { fixture.layers.back().asset->image = image; });
            _session.history.End(_session.Document(), _session.ActiveLayerID());
            _session.history.MarkSaved();
            DocumentChanged();
            if (SettleJobs(_jobs, 25000, DispatchMessageThunk, this) != SettleResult::Settled)
                return 5;
            const auto source = _session.Document();
            const auto revision = _session.history.CurrentRevision();
            const auto generation = _session.Generation();
            const auto serial = _session.DocumentSerial();
            const auto errors = _errorsShown;
            const auto presents = _renderer.PresentCount();
            unsigned int failures = 0;
            const auto expect = [&](bool condition, const char* label) noexcept
            {
                std::fprintf(stderr, "%s image-wrapper-residency.%u.%u.%u.%s\n", condition ? "PASS" : "FAIL", kind,
                             alternate ? 1U : 0U, mode, label);
                failures += condition ? 0U : 1U;
            };
            expect(sparse.ByteSize() == 0 && !sparse.IsMaterialized() && leaf.ByteSize() == 4 &&
                       _session.history.UndoCount() == 1 && _session.history.RetainedBytes(source) == 4,
                   "literal-retained-four-byte-leaf");
            if (failures)
                return 5;
            const auto run = [&]<class Gate>(std::shared_ptr<Gate> gate) noexcept
            {
                if (!gate->release)
                    return false;
                gate->afterPreparation = true;
                gate->lateResidencySource = sparse;
                gate->throwResidencyRefresh = mode == 1;
                gate->cancelResidencyRefresh = mode == 2;
                ContinuationReceipt receipt;
                if constexpr (std::is_same_v<Gate, MaskWorkTestGate>)
                {
                    _maskWorkTestGate = gate;
                    receipt = QueueHostInput(
                        HostCommandInput{alternate ? ChromeHost::kCommandAddBlackMask : ChromeHost::kCommandAddMask});
                }
                else if constexpr (std::is_same_v<Gate, LayerCopyWorkTestGate>)
                {
                    _layerCopyWorkTestGate = gate;
                    receipt = QueueHostInput(HostCommandInput{ChromeHost::kCommandLayerViaCopy});
                }
                else
                {
                    _shapeWorkTestGate = gate;
                    const ShapeDraft draft{ShapeKind::Rectangle, {0, 0}, {0, 0, 1, 1}, 0};
                    receipt = QueueHostInput(HostShapeFinishInput{draft, 0xFF6B4F35});
                }
                struct Held final
                {
                    Application& owner;
                    Gate& gate;
                    ContinuationReceipt receipt;
                    std::optional<CanvasDocument> source;
                    Guid revision;
                    std::uint64_t serial;
                    bool visited = false, unchanged = false;
                } held{*this, *gate, receipt, source, revision, serial};
                const auto dispatch = [](void* context, void* native) noexcept
                {
                    auto& state = *static_cast<Held*>(context);
                    auto& owner = state.owner;
                    auto& message = *static_cast<MSG*>(native);
                    if (message.message != Gate::kMessage)
                    {
                        owner.DispatchQueuedMessage(message);
                        return;
                    }
                    state.visited = true;
                    state.unchanged = owner._activeHostRequest && owner._historyWork.Busy() &&
                                      state.receipt.Read().outcome == HostOutcome::Pending &&
                                      owner._session.Document() == state.source &&
                                      owner._session.DocumentSerial() == state.serial &&
                                      owner._session.history.CurrentRevision() == state.revision &&
                                      state.gate.reservationBytes != 0 &&
                                      owner._jobs.ReservedMemoryBytes() >= state.gate.reservationBytes;
                    owner.OnCommand(ChromeHost::kCommandZoomActual);
                    owner._composition.ReleaseDocumentSurfaces();
                    state.unchanged = state.unchanged && owner._composition.CanvasSurfaceBytes() == 0;
                    (void)SetEvent(state.gate.release.get());
                };
                const auto settled = SettleJobs(_jobs, 25000, dispatch, &held);
                const auto result = receipt.Read();
                const auto expected = mode == 0   ? HostOutcome::Succeeded
                                      : mode == 2 ? HostOutcome::Cancelled
                                                  : HostOutcome::Failed;
                expect(settled == SettleResult::Settled && result.outcome == expected,
                       "receipt-after-final-acknowledgement");
                expect(held.visited && held.unchanged, "original-held-document-and-view-input");
                expect(gate->captures.load() == 1 && gate->preparations.load() == 1 &&
                           gate->residentGrowths.load() == 1 && gate->residencyRefreshes.load() == 1 &&
                           gate->captureReservationHeld && gate->refreshReservationHeld.load() &&
                           gate->capturedTicket && gate->refreshTicketMatches.load(),
                       "one-capture-pixel-preparation-and-exact-ticket-refresh");
                expect(sparse.ByteSize() == 8 && leaf.ByteSize() == 4 && sparse.IsMaterialized(),
                       "literal-twelve-byte-late-growth");
                expect(!_maskTicket && !_maskStamp && !_layerCopyTicket && !_layerCopyStamp && !_shapeTicket &&
                           !_shapeStamp && !_activeHostRequest && !_historyWork.Busy() &&
                           _jobs.ReservedMemoryBytes() == 0 && _hostInputBudget.Used() == 0 && !_shapeCommitPreview,
                       "source-ticket-input-preview-barrier-and-reservation-retired");
                expect(gate->retired.load() && gate->retiredOffOwner.load(), "payload-retires-off-owner");
                expect(_renderer.PresentCount() == presents, "hidden-owner-never-presents");
                expect(_errorsShown == errors + (mode == 1 ? 1 : 0), "one-current-failure-notice");
                if (mode == 0)
                {
                    expect(result.error == S_OK && _session.DocumentSerial() == serial &&
                               _session.history.UndoCount() == 1 && _session.history.IsModified() &&
                               !_session.history.CanRedo(),
                           "original-entry-and-saved-lineage-publish-once");
                    const auto* layer = _session.ActiveLayer();
                    if constexpr (std::is_same_v<Gate, MaskWorkTestGate>)
                    {
                        const auto value = alternate ? 0 : 255;
                        expect(layer && layer->mask && layer->mask->asset.image.Data()[0] == value &&
                                   layer->mask->asset.thumbnail.HasStorage() &&
                                   layer->mask->asset.thumbnail.Data()[0] == value && layer->asset->image == image,
                               "literal-mask-and-eager-semantic-thumbnail");
                    }
                    else
                    {
                        std::array<std::uint8_t, 4> actual{};
                        if (layer && layer->asset)
                            layer->asset->image.ReadRegion(0, 0, 1, 1, actual.data(), 4, 0);
                        expect(layer && layer->asset && _session.Document().value().layers.size() == 2 &&
                                   actual ==
                                       (kind == 1 ? sourceBytes : std::array<std::uint8_t, 4>{53, 79, 107, 255}) &&
                                   _session.Document().value().layers.front().asset->image == image,
                               "literal-new-layer-pixels-and-held-source");
                    }
                    _session.Undo();
                    expect(_session.Document() == source && !_session.history.IsModified(),
                           "undo-restores-original-saved-intent");
                    _session.Redo();
                    expect(_session.history.UndoCount() == 1 && _session.history.IsModified(),
                           "redo-restores-original-intent-revision");
                }
                else
                {
                    expect(result.error == static_cast<std::int32_t>(mode == 1 ? E_OUTOFMEMORY : E_ABORT),
                           "exact-refresh-allocation-or-cancel-error");
                    expect(_session.Document() == source && _session.DocumentSerial() == serial &&
                               _session.Generation() == generation && _session.history.CurrentRevision() == revision &&
                               _session.history.UndoCount() == 1 && !_session.history.IsModified() &&
                               !_session.history.CanRedo(),
                           "failed-refresh-preserves-live-intent-and-saved-history");
                    if (mode == 1)
                        expect(_lastErrorId == IDS_ERROR_PIXEL_EDIT, "localized-pixel-error");
                }
                return failures == 0;
            };
            const bool correct = kind == 0   ? run(std::make_shared<MaskWorkTestGate>())
                                 : kind == 1 ? run(std::make_shared<LayerCopyWorkTestGate>())
                                             : run(std::make_shared<ShapeWorkTestGate>());
            _maskWorkTestGate.reset();
            _layerCopyWorkTestGate.reset();
            _shapeWorkTestGate.reset();
            if (!correct)
                return 5;
        }
    return 0;
}
