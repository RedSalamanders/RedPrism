#include "Application.h"

#include "ChromeHost.h"
#include "Runtime.Tests.ImageImportTestGate.h"
#include "Tests/Support/Support.Tests.DocumentFixture.h"
#include "resource.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <utility>

int Application::RunImageImportResidencySelfTest(std::span<const std::wstring_view> paths) noexcept
{
    if (paths.size() != 2)
        return 5;
    const auto originalBudget = _session.history.RetainedByteLimit();
    const auto originalDiscard = _forcedDiscardAnswer;
    const auto cleanup = wil::scope_exit(
        [&]() noexcept
        {
            if (_imageImportTestGate)
                (void)SetEvent(_imageImportTestGate->release.get());
            CancelHostContinuations();
            if (SettleJobs(_jobs, 25000, DispatchMessageThunk, this) != SettleResult::Settled)
                std::_Exit(5);
            _imageImportTestGate.reset();
            _forcedDiscardAnswer = originalDiscard;
            _session.history = DocumentHistory{kHistoryEntryLimit, originalBudget};
        });
    enum class Target
    {
        Existing,
        ReplacementMultiple,
        ReplacementSingle
    };
    enum class Mode
    {
        Success,
        RefreshFailure,
        RefreshCancel
    };
// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4623 4626 5027)
    struct Held final
    {
        Application& owner;
        ContinuationReceipt receipt;
        std::optional<CanvasDocument> source;
        Guid revision;
        std::uint64_t serial;
        bool visited = false, unchanged = false;
        std::optional<JobTicket> ticket;
    };
#pragma warning(pop)

    const auto dispatch = [](void* context, void* native) noexcept
    {
        auto& held = *static_cast<Held*>(context);
        auto& owner = held.owner;
        auto& message = *static_cast<MSG*>(native);
        if (message.message != ImageImportTestGate::kMessage)
        {
            owner.DispatchQueuedMessage(message);
            return;
        }
        held.visited = true;
        held.ticket = owner._imageImportTicket;
        held.unchanged = held.ticket && owner._imageImportStamp && owner._activeHostRequest &&
                         owner._historyWork.Busy() && held.receipt.Read().outcome == HostOutcome::Pending &&
                         owner._session.Document() == held.source && owner._session.DocumentSerial() == held.serial &&
                         owner._session.history.CurrentRevision() == held.revision &&
                         owner._jobs.ReservedMemoryBytes() >= owner._imageImportTestGate->preparationReservationBytes;
        owner.OnCommand(ChromeHost::kCommandZoomActual);
        owner._composition.ReleaseDocumentSurfaces();
        held.unchanged = held.unchanged && owner._composition.CanvasSurfaceBytes() == 0;
        (void)SetEvent(owner._imageImportTestGate->release.get());
    };
    for (const auto target : {Target::Existing, Target::ReplacementMultiple, Target::ReplacementSingle})
        for (const auto mode : {Mode::Success, Mode::RefreshFailure, Mode::RefreshCancel})
        {
            if (SettleJobs(_jobs, 25000, DispatchMessageThunk, this) != SettleResult::Settled)
                return 5;
            _session.history = DocumentHistory{kHistoryEntryLimit, originalBudget};
            _session.ClearDocument();
            _session.CreateDocument(4, 1);
            (void)InsertPaintedLayer(_session, L"Import residency source", 4, 1, {}, 0, 0xFF404040);
            constexpr std::array<std::uint8_t, 4> leafBytes{7, 11, 13, 255};
            const auto leaf = Raster::Make(1, 1, RasterFormat::Bgra8888, leafBytes.data(), leafBytes.size());
            const auto sparse =
                Raster::MakeSparse(4, 1, RasterFormat::Bgra8888, {}, 0, 0, {RasterTile{0, 0, leaf}}, 23);
            EditLayerRecordsForTest(_session,
                                    [&](DocumentFixture& fixture)
                                    {
                                        fixture.layers.back().asset->image = sparse;
                                        fixture.layers.back().asset->thumbnail = {};
                                    });
            // Two imported 2x1 BGRA rasters occupy 16 bytes when Undo retains them. The older sparse source
            // grows to 20 bytes, so this fixed cap trims that older entry and still permits the import's Redo.
            _session.history = DocumentHistory{kHistoryEntryLimit, 16};
            _session.history.Begin(L"Older sparse import source", _session.Document(), _session.ActiveLayerID());
            constexpr std::array<std::uint8_t, 4> currentBytes{31, 37, 41, 255};
            const auto current = Raster::Make(1, 1, RasterFormat::Bgra8888, currentBytes.data(), currentBytes.size());
            EditLayerRecordsForTest(_session,
                                    [&](DocumentFixture& fixture) { fixture.layers.back().asset->image = current; });
            _session.history.End(_session.Document(), _session.ActiveLayerID());
            _session.history.MarkSaved();
            _documentPath = L"import-residency-source.prism";
            DocumentChanged();
            if (SettleJobs(_jobs, 25000, DispatchMessageThunk, this) != SettleResult::Settled)
                return 5;
            const auto source = _session.Document();
            const auto revision = _session.history.CurrentRevision();
            const auto serial = _session.DocumentSerial();
            const auto generation = _session.Generation();
            const auto errors = _errorsShown;
            const auto presents = _renderer.PresentCount();
            unsigned int failed = 0;
            const auto expect = [&](bool condition, const char* label) noexcept
            {
                std::fprintf(stderr, "%s image-import-residency.%u.%u.%s\n", condition ? "PASS" : "FAIL",
                             static_cast<unsigned int>(target), static_cast<unsigned int>(mode), label);
                failed += condition ? 0U : 1U;
            };
            expect(!sparse.IsMaterialized() && sparse.ByteSize() == 0 && leaf.ByteSize() == 4 &&
                       _session.history.UndoCount() == 1 && _session.history.RetainedBytes(source) == 4 &&
                       _session.history.RetainedByteLimit() == 16,
                   "literal-four-byte-leaf-baseline");
            if (failed)
                return 5;
            _imageImportTestGate = std::make_shared<ImageImportTestGate>();
            const auto gate = _imageImportTestGate;
            if (!gate->release)
                return 5;
            gate->phase = ImageImportTestPhase::Prepare;
            gate->afterPreparation = true;
            gate->lateResidencySource = sparse;
            gate->throwResidencyRefresh = mode == Mode::RefreshFailure;
            gate->cancelResidencyRefresh = mode == Mode::RefreshCancel;
            _forcedDiscardAnswer = Prompts::Answer::Second;
            const bool replacement = target != Target::Existing;
            const auto selectedPaths = target == Target::ReplacementSingle ? paths.first(1) : paths;
            const auto receipt = QueueHostPaths(
                selectedPaths, replacement ? HostPathAction::ImagesAsProject : HostPathAction::ImagesAsLayers,
                Vec2{12, 14});
            Held held{*this, receipt, source, revision, serial};
            const auto settled = SettleJobs(_jobs, 25000, dispatch, &held);
            const auto result = receipt.Read();
            // A single-image replacement publishes only a saved baseline. PrepareEdit's no-op path samples
            // no resident trim decision, so unrelated cache growth must not invent a refresh or its faults.
            const bool needsRefresh = target != Target::ReplacementSingle;
            const bool refreshFailed = needsRefresh && mode == Mode::RefreshFailure;
            const bool refreshCancelled = needsRefresh && mode == Mode::RefreshCancel;
            const auto expected = !needsRefresh || mode == Mode::Success ? HostOutcome::Succeeded
                                  : mode == Mode::RefreshCancel          ? HostOutcome::Cancelled
                                                                         : HostOutcome::Failed;
            expect(settled == SettleResult::Settled && result.outcome == expected, "receipt-after-acknowledgement");
            expect(held.visited && held.unchanged, "held-original-intent-and-view-input");
            expect(gate->captures.load() == 1 && gate->preparations.load() == 1 && gate->residentGrowths.load() == 1 &&
                       gate->residencyRefreshes.load() == (needsRefresh ? 1U : 0U) && gate->captureReservationHeld &&
                       gate->refreshReservationHeld.load() == needsRefresh && held.ticket == gate->capturedTicket &&
                       gate->refreshTicketMatches.load() == needsRefresh,
                   "one-capture-decode-and-same-reservation-refresh");
            if (!needsRefresh)
                expect(gate->residencyRefreshes.load() == 0 && !gate->refreshReservationHeld.load() &&
                           !gate->refreshTicketMatches.load(),
                       "single-replacement-no-resident-trim-or-refresh");
            expect(sparse.IsMaterialized() && sparse.ByteSize() == 16 && leaf.ByteSize() == 4 &&
                       sparse.ByteSize() + leaf.ByteSize() == 20,
                   "literal-twenty-byte-late-growth");
            expect(!_imageImportTicket && !_imageImportStamp && !_activeHostRequest && !_historyWork.Busy() &&
                       _jobs.ReservedMemoryBytes() == 0 && _hostInputBudget.Used() == 0,
                   "ticket-input-barrier-and-reservation-retired");
            expect(gate->retired.load() && gate->retiredOffOwner.load(), "payload-retires-off-owner");
            expect(_renderer.PresentCount() == presents, "hidden-owner-never-presents");
            expect(_errorsShown == errors + (refreshFailed ? 1 : 0), "one-current-failure-notice");
            if (!refreshFailed && !refreshCancelled)
            {
                expect(result.error == S_OK && _session.Document() &&
                           _session.Document().value().layers.size() ==
                               (replacement ? selectedPaths.size() : source.value().layers.size() + 2) &&
                           _session.ActiveLayer() && _session.ActiveLayer()->asset &&
                           _session.ActiveLayer()->asset->image.Width() == 2 &&
                           _session.ActiveLayer()->asset->image.Data()[0] == 191,
                       "original-imported-pixels-publish-once");
                expect(_session.history.UndoCount() == (target == Target::ReplacementSingle ? 0 : 1) &&
                           !_session.history.CanRedo(),
                       "trim-preserves-original-import-entry");
                expect(replacement
                           ? _session.DocumentSerial() != serial && _documentPath.empty()
                           : _session.DocumentSerial() == serial && _documentPath == L"import-residency-source.prism",
                       "original-replacement-or-insertion-authority");
                expect(_session.history.IsModified() == (target != Target::ReplacementSingle),
                       "saved-baseline-and-import-revision-truth");
                if (target != Target::ReplacementSingle)
                {
                    _session.Undo();
                    expect(!_session.history.IsModified() &&
                               (replacement ? _session.Document().value().layers.size() == 1
                                            : _session.Document() == source) &&
                               _session.history.RedoName() == (replacement ? L"Import Image" : L"Import Images"),
                           "undo-restores-original-saved-intent");
                    _session.Redo();
                    expect(_session.history.IsModified() && _session.history.UndoCount() == 1,
                           "redo-restores-original-import-lineage");
                }
            }
            else
            {
                expect(result.error ==
                           static_cast<std::int32_t>(mode == Mode::RefreshFailure ? E_OUTOFMEMORY : E_ABORT),
                       "exact-refresh-failure-or-cancellation");
                expect(_session.Document() == source && _session.DocumentSerial() == serial &&
                           _session.Generation() == generation && _session.history.CurrentRevision() == revision &&
                           _session.history.UndoCount() == 1 && !_session.history.CanRedo() &&
                           !_session.history.IsModified() && _documentPath == L"import-residency-source.prism",
                       "refusal-preserves-live-source-and-saved-history");
                if (mode == Mode::RefreshFailure)
                    expect(_lastErrorId == IDS_ERROR_IMPORT_IMAGE, "localized-import-error");
            }
            _imageImportTestGate.reset();
            if (failed)
                return 5;
        }
    return 0;
}
