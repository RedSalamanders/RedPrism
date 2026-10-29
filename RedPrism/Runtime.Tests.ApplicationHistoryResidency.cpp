#include "Application.h"

#include "Tests/Support/Support.Tests.DocumentFixture.h"

#include "ChromeHost.h"
#include "Runtime.Tests.HistoryResidencyTestGate.h"
#include "Runtime.Tests.LayerMetadataTestGate.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <memory>
#include <utility>

namespace
{
// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4626 5027)
class PublishLiteralCache final : public Job
{
  public:
    PublishLiteralCache(Raster source, bool& completed) noexcept : _source(std::move(source)), _completed(completed) {}
    JobResult Run(JobContext&) override
    {
        const auto* pixels = _source.Data();
        constexpr std::array<std::uint8_t, 8> expected{10, 20, 30, 255, 1, 1, 1, 1};
        return pixels && _source.ByteSize() == expected.size() && std::equal(expected.begin(), expected.end(), pixels)
                   ? JobResult{}
                   : JobResult{JobStatus::Failed, E_FAIL};
    }
    void Completed(JobResult result) noexcept override
    {
        _completed = result.status == JobStatus::Finished;
    }

  private:
    const Raster _source; // Literal eight-byte fixture only; no large owner destruction on this diagnostic lane.
    bool& _completed;
};
#pragma warning(pop)

class OccupyResidencySlot final : public Job
{
  public:
    JobResult Run(JobContext&) override
    {
        return {};
    }
    void Completed(JobResult) noexcept override {}
};
} // namespace

int Application::RunHistoryResidencySelfTest() noexcept
{
    enum class Mode : unsigned int
    {
        Success,
        Ordered,
        Cancel,
        CaptureFailure,
        WorkerFailure,
        Stale,
    };
    constexpr std::array names{"success", "ordered", "cancel", "capture-failure", "worker-failure", "stale"};
    int checks = 0;
    const auto expect = [&checks](const char* mode, const char* name, bool condition) noexcept
    {
        ++checks;
        std::printf("%s history-residency-host.%s.%s\n", condition ? "PASS" : "FAIL", mode, name);
        return condition;
    };
    constexpr std::array<std::uint8_t, 4> livePixel{90, 80, 70, 255};
    constexpr std::array<std::uint8_t, 4> historicalPixel{10, 20, 30, 255};
    const Raster live = Raster::Make(1, 1, RasterFormat::Bgra8888, livePixel.data(), livePixel.size());
    const Raster leaf = Raster::Make(1, 1, RasterFormat::Bgra8888, historicalPixel.data(), historicalPixel.size());
    for (unsigned int index = 0; index < names.size(); ++index)
    {
        const auto mode = static_cast<Mode>(index);
        const char* name = names[index];
        if (SettleJobs(_jobs, 15000, DispatchMessageThunk, this) != SettleResult::Settled || _historyWork.Busy())
            return 5;
        _historyResidencySource.CloseAfterWorkers();
        // Only Ordered admits a metadata producer. Observe its original durable output separately from
        // the maintenance/cache jobs; a live published graph must remain charged after those jobs retire.
        _layerMetadataTestGate.reset();
        if (mode == Mode::Ordered)
        {
            _layerMetadataTestGate = std::make_shared<LayerMetadataTestGate>();
            _layerMetadataTestGate->hold = false;
        }
        DocumentFixture fixture;
        fixture.width = 2;
        fixture.height = 1;
        ImageLayer layer = ImageLayer::Blank(L"Current layer", {2, 1});
        const Guid target = layer.id;
        layer.asset = RasterAsset{live, {}, L"Current pixels"};
        fixture.layers.push_back(std::move(layer));
        _session.ReplaceDocument(std::move(fixture).Seal(), target);
        const Raster sparse = Raster::MakeSparse(2, 1, RasterFormat::Bgra8888, {}, 0, 0, {{0, 0, leaf}}, 1);
        auto historical = DocumentFixture::Copy(_session.Document().value());
        historical.layers.front().asset = RasterAsset{sparse, {}, L"Historical pixels"};
        _session.history = DocumentHistory(100, 8);
        auto baseline = DocumentHistory::PrepareEdit(_session.history.CaptureState(), std::move(historical).Seal(),
                                                     target, _session.Document(), target, L"Original saved edit");
        if (!baseline || !_session.history.Publish(baseline.value()))
            return 5;
        baseline.reset();
        _session.history.MarkSaved();
        const auto original = _session.Document();
        const Guid revision = _session.history.CurrentRevision();
        const auto generation = _session.Generation();
        const auto gate = std::make_shared<HistoryResidencyTestGate>();
        gate->release.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        gate->hold = mode != Mode::CaptureFailure && mode != Mode::WorkerFailure;
        gate->throwCaptureFailure = mode == Mode::CaptureFailure;
        gate->throwPrepareFailure = mode == Mode::WorkerFailure;
        _historyResidencyTestGate = gate;
        if (!gate->release || !_historyResidencySource.Start())
            return 5;
        const auto cleanup = wil::scope_exit(
            [this, gate]() noexcept
            {
                _historyResidencySource.StopNotifications();
                (void)SetEvent(gate->release.get());
                if (_historyResidencyTicket)
                    (void)_jobs.Cancel(_historyResidencyTicket.value());
                (void)SettleJobs(_jobs, 15000, DispatchMessageThunk, this);
                _historyResidencySource.CloseAfterWorkers();
                _historyResidencyTestGate.reset();
            });
        if (!expect(name, "fresh-index-does-not-request-maintenance",
                    !_session.history.NeedsResidencyMaintenance() && _session.history.RetainedBytes(original) == 4 &&
                        !_session.history.IsModified()))
            return 5;
// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4623 4626 5027)
        struct State final
        {
            Application& owner;
            Mode mode;
            std::optional<CanvasDocument> original, younger;
            Guid revision, youngerRevision;
            ContinuationReceipt dependent;
            std::uint64_t generation = 0;
            bool visited = false, held = false;
        }
#pragma warning(pop)
        state{*this, mode, original, {}, revision, {}, {}, generation};
        const auto dispatch = [](void* raw, void* native) noexcept
        {
            auto& state = *static_cast<State*>(raw);
            auto& owner = state.owner;
            const auto& message = *static_cast<MSG*>(native);
            if (message.message != HistoryResidencyTestGate::kMessage)
            {
                DispatchMessageThunk(&owner, native);
                return;
            }
            const auto gate = owner._historyResidencyTestGate;
            state.visited = true;
            state.held =
                gate && gate->captures.load() == 1 && gate->preparations.load() == 1 && gate->reservationHeld.load() &&
                owner._historyResidencyTicket && owner._historyResidencyStamp && owner._historyWork.Busy() &&
                owner._session.Document() == state.original &&
                owner._session.history.CurrentRevision() == state.revision &&
                owner._session.Generation() == state.generation && !owner._session.history.IsModified() &&
                owner._session.history.UndoCount() == 1 && owner._jobs.ReservedMemoryBytes() >= gate->reservationBytes;
            if (state.mode == Mode::Ordered)
                state.dependent = owner.QueueHostInput(HostCommandInput{ChromeHost::kCommandToggleLayerVisibility});
            if (state.mode == Mode::Cancel && owner._historyResidencyTicket)
                (void)owner._jobs.Cancel(owner._historyResidencyTicket.value());
            if (state.mode == Mode::Stale)
            {
                // Independent authority mutation while the real maintenance worker is held. The candidate must
                // refuse publication rather than replacing this newer fixture/document/history lineage.
                owner._session.RenameLayer(owner._session.ActiveLayerID().value(), L"Younger direct edit");
                state.younger = owner._session.Document();
                state.youngerRevision = owner._session.history.CurrentRevision();
            }
            if (gate)
                (void)SetEvent(gate->release.get());
        };
        bool cacheCompleted = false;
        std::unique_ptr<Job> cache = std::make_unique<PublishLiteralCache>(sparse, cacheCompleted);
        JobTicket cacheTicket;
        if (!expect(name, "cache-worker-admitted",
                    SUCCEEDED(static_cast<HRESULT>(
                        _jobs.Submit(cache, {JobClass::InteractivePreview, 0, 65536}, cacheTicket)))))
            return 5;
        const bool settled = SettleJobs(_jobs, 15000, dispatch, &state) == SettleResult::Settled;
        if (!expect(name, "actual-source-and-worker-settle",
                    settled && cacheCompleted && sparse.ByteSize() == 8 && !_historyResidencyTicket &&
                        !_historyResidencyStamp && !_historyWork.Busy() &&
                        (mode == Mode::Ordered ? _layerMetadataTestGate->originalAccountingBound &&
                                                     _layerMetadataTestGate->accountingOutputBytes > 0 &&
                                                     MetadataAccountingSettledForTest()
                                               : _jobs.ReservedMemoryBytes() == 0)))
            return 5;
        if (!expect(name, "admitted-capture-and-off-owner-retirement",
                    gate->captures.load() == 1 && gate->reservationHeld.load() && gate->retired.load() &&
                        gate->retiredOffOwner.load() &&
                        gate->preparations.load() == (mode == Mode::CaptureFailure ? 0u : 1u)))
            return 5;
        if (!expect(name, "held-publication-does-not-mutate-live-state",
                    gate->hold ? state.visited && state.held : !state.visited))
            return 5;
        const bool originalPreserved = _session.Document() == original &&
                                       _session.history.CurrentRevision() == revision &&
                                       _session.Generation() == generation && !_session.history.IsModified();
        if (mode == Mode::Success)
        {
            if (!expect(name, "trim-preserves-document-user-revision-and-saved-marker",
                        gate->published.load() && originalPreserved && _session.history.UndoCount() == 0 &&
                            _session.history.RetainedBytes(original) == 0 &&
                            !_session.history.NeedsResidencyMaintenance()))
                return 5;
        }
        else if (mode == Mode::Ordered)
        {
            if (!expect(name, "younger-user-edit-follows-maintenance-once",
                        gate->published.load() && state.dependent.Read().outcome == HostOutcome::Succeeded &&
                            _session.Document() && !_session.Document()->layers.front().visible &&
                            _session.history.CurrentRevision() != revision && _session.history.IsModified() &&
                            _session.history.UndoCount() == 1 && _session.history.UndoName() == L"Hide Layer" &&
                            _session.history.RetainedBytes(_session.Document()) == 0))
                return 5;
        }
        else if (mode == Mode::Stale)
        {
            if (!expect(name, "stale-maintenance-cannot-overwrite-younger-document-or-revision",
                        gate->stale.load() && !gate->published.load() && state.younger &&
                            _session.Document() == state.younger &&
                            _session.history.CurrentRevision() == state.youngerRevision &&
                            _session.Document()->layers.front().name == L"Younger direct edit"))
                return 5;
        }
        else if (!expect(name, "failed-or-cancelled-maintenance-preserves-original-history",
                         !gate->published.load() && originalPreserved && _session.history.UndoCount() == 1 &&
                             _session.history.UndoName() == L"Original saved edit" &&
                             (mode == Mode::Cancel ? gate->cancelled.load() : gate->failed.load())))
            return 5;
    }

    _layerMetadataTestGate.reset();
    const auto installFixture = [this, &live, &leaf](Raster& sparse) noexcept
    {
        DocumentFixture fixture;
        fixture.width = 2;
        fixture.height = 1;
        ImageLayer layer = ImageLayer::Blank(L"Current layer", {2, 1});
        const Guid target = layer.id;
        layer.asset = RasterAsset{live, {}, L"Current pixels"};
        fixture.layers.push_back(std::move(layer));
        _session.ReplaceDocument(std::move(fixture).Seal(), target);
        sparse = Raster::MakeSparse(2, 1, RasterFormat::Bgra8888, {}, 0, 0, {{0, 0, leaf}}, 1);
        auto historical = DocumentFixture::Copy(_session.Document().value());
        historical.layers.front().asset = RasterAsset{sparse, {}, L"Historical pixels"};
        _session.history = DocumentHistory(100, 8);
        auto baseline = DocumentHistory::PrepareEdit(_session.history.CaptureState(), std::move(historical).Seal(),
                                                     target, _session.Document(), target, L"Original saved edit");
        if (!baseline || !_session.history.Publish(baseline.value()))
            return false;
        _session.history.MarkSaved();
        return true;
    };

    if (SettleJobs(_jobs, 15000, DispatchMessageThunk, this) != SettleResult::Settled || _historyWork.Busy())
        return 5;
    _historyResidencySource.CloseAfterWorkers();
    Raster deferredSparse;
    if (!installFixture(deferredSparse))
        return 5;
    const auto deferredOriginal = _session.Document();
    const Guid deferredRevision = _session.history.CurrentRevision();
    const auto deferredGeneration = _session.Generation();
    const auto deferredGate = std::make_shared<HistoryResidencyTestGate>();
    deferredGate->release.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!deferredGate->release)
        return 5;
    _historyResidencyTestGate = deferredGate;
    if (!_historyResidencySource.Start())
        return 5;
    auto deferredCleanup = wil::scope_exit(
        [this, deferredGate]() noexcept
        {
            _historyResidencySource.StopNotifications();
            (void)SetEvent(deferredGate->release.get());
            if (_historyResidencyTicket)
                (void)_jobs.Cancel(_historyResidencyTicket.value());
            (void)SettleJobs(_jobs, 15000, DispatchMessageThunk, this);
            _historyResidencySource.CloseAfterWorkers();
            _historyResidencyTestGate.reset();
        });
    _session.BeginEdit(L"Unchanged residency transaction");
    bool deferredCacheCompleted = false;
    std::unique_ptr<Job> deferredCache = std::make_unique<PublishLiteralCache>(deferredSparse, deferredCacheCompleted);
    JobTicket deferredCacheTicket;
    if (!expect("transaction-deferred", "cache-growth-producer-admitted",
                SUCCEEDED(static_cast<HRESULT>(
                    _jobs.Submit(deferredCache, {JobClass::InteractivePreview, 0, 65536}, deferredCacheTicket)))))
        return 5;
    const bool transactionSettled = SettleJobs(_jobs, 15000, DispatchMessageThunk, this) == SettleResult::Settled;
    if (!expect("transaction-deferred", "open-transaction-defers-before-capture-or-admission",
                transactionSettled && deferredCacheCompleted && deferredSparse.ByteSize() == 8 &&
                    deferredGate->captures.load() == 0 && deferredGate->preparations.load() == 0 &&
                    !_historyResidencyTicket && !_historyResidencyStamp && !_historyWork.Busy() &&
                    !_historyResidencySource.HasPending() && _session.history.CaptureState().Depth() == 1 &&
                    _session.Document() == deferredOriginal && _session.history.CurrentRevision() == deferredRevision &&
                    _session.Generation() == deferredGeneration && !_session.history.IsModified() &&
                    _session.history.UndoCount() == 1 && _session.history.RetainedBytes(deferredOriginal) == 12))
        return 5;
    _session.EndEdit();
    const auto deferredFinishedGeneration = _session.Generation();
    DocumentChanged(); // Existing owner finisher wake rearms the deferred notice without creating a user edit.
// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4623 4626 5027)
    struct DeferredState final
    {
        Application& owner;
        std::optional<CanvasDocument> original;
        Guid revision;
        std::uint64_t generation;
        bool reached = false;
        bool held = false;
    }
#pragma warning(pop)
    deferredState{*this, deferredOriginal, deferredRevision, deferredFinishedGeneration};
    const auto deferredDispatch = [](void* raw, void* native) noexcept
    {
        auto& state = *static_cast<DeferredState*>(raw);
        auto& owner = state.owner;
        const auto& message = *static_cast<MSG*>(native);
        if (message.message != HistoryResidencyTestGate::kMessage)
        {
            DispatchMessageThunk(&owner, native);
            return;
        }
        const auto gate = owner._historyResidencyTestGate;
        state.reached = true;
        state.held = gate && gate->captures.load() == 1 && gate->preparations.load() == 1 &&
                     gate->reservationHeld.load() && owner._historyResidencyTicket && owner._historyResidencyStamp &&
                     owner._historyWork.Busy() && owner._session.Document() == state.original &&
                     owner._session.history.CurrentRevision() == state.revision &&
                     owner._session.Generation() == state.generation && !owner._session.history.IsModified() &&
                     owner._session.history.UndoCount() == 1;
        if (gate)
            (void)SetEvent(gate->release.get());
    };
    const bool deferredMaintenanceSettled =
        SettleJobs(_jobs, 15000, deferredDispatch, &deferredState) == SettleResult::Settled;
    if (!expect("transaction-deferred", "finisher-wake-admits-one-maintenance-and-trims-literal-two-pixels",
                deferredMaintenanceSettled && deferredState.reached && deferredState.held &&
                    deferredGate->captures.load() == 1 && deferredGate->preparations.load() == 1 &&
                    deferredGate->published.load() && deferredSparse.ByteSize() == 8 && deferredOriginal &&
                    deferredOriginal->layers.front().asset.value().image == live &&
                    deferredOriginal->layers.front().asset.value().image.Data()[0] == 90 &&
                    deferredOriginal->layers.front().asset.value().image.Data()[1] == 80 &&
                    deferredOriginal->layers.front().asset.value().image.Data()[2] == 70 &&
                    deferredOriginal->layers.front().asset.value().image.Data()[3] == 255 &&
                    _session.Document() == deferredOriginal && _session.history.CurrentRevision() == deferredRevision &&
                    _session.Generation() == deferredFinishedGeneration && !_session.history.IsModified() &&
                    _session.history.UndoCount() == 0 && _session.history.RetainedBytes(deferredOriginal) == 0 &&
                    !_session.history.NeedsResidencyMaintenance() && !_historyResidencyTicket &&
                    !_historyResidencyStamp && !_historyWork.Busy()))
        return 5;
    deferredCleanup.release();
    _historyResidencySource.StopNotifications();
    _historyResidencySource.CloseAfterWorkers();
    _historyResidencyTestGate.reset();

    if (SettleJobs(_jobs, 15000, DispatchMessageThunk, this) != SettleResult::Settled || _historyWork.Busy())
        return 5;
    Raster refusalSparse;
    if (!installFixture(refusalSparse))
        return 5;
    const auto refusalOriginal = _session.Document();
    const Guid refusalRevision = _session.history.CurrentRevision();
    const auto refusalGate = std::make_shared<HistoryResidencyTestGate>();
    refusalGate->release.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!refusalGate->release)
        return 5;
    _historyResidencyTestGate = refusalGate;
    if (!_historyResidencySource.Start())
        return 5;
    auto refusalCleanup = wil::scope_exit(
        [this, refusalGate]() noexcept
        {
            _historyResidencySource.StopNotifications();
            (void)SetEvent(refusalGate->release.get());
            (void)SettleJobs(_jobs, 15000, DispatchMessageThunk, this);
            _historyResidencySource.CloseAfterWorkers();
            _historyResidencyTestGate.reset();
        });
    _session.BeginEdit(L"Unchanged capacity transaction");
    bool refusalCacheCompleted = false;
    std::unique_ptr<Job> refusalCache = std::make_unique<PublishLiteralCache>(refusalSparse, refusalCacheCompleted);
    JobTicket refusalCacheTicket;
    if (FAILED(static_cast<HRESULT>(
            _jobs.Submit(refusalCache, {JobClass::InteractivePreview, 0, 65536}, refusalCacheTicket))) ||
        SettleJobs(_jobs, 15000, DispatchMessageThunk, this) != SettleResult::Settled || !refusalCacheCompleted ||
        refusalGate->captures.load() != 0 || _historyResidencySource.HasPending())
        return 5;
    _session.EndEdit();
    const auto refusalGeneration = _session.Generation();
    std::unique_ptr<Job> held[JobScheduler::kCapacity];
    JobTicket heldTickets[JobScheduler::kCapacity];
    for (std::size_t index = 0; index < JobScheduler::kCapacity; ++index)
    {
        held[index] = std::make_unique<OccupyResidencySlot>();
        if (FAILED(static_cast<HRESULT>(_jobs.Submit(held[index], {JobClass::Background, 0, 0}, heldTickets[index]))))
            return 5;
    }
    std::unique_ptr<Job> overflow = std::make_unique<OccupyResidencySlot>();
    JobTicket overflowTicket;
    const bool full =
        FAILED(static_cast<HRESULT>(_jobs.Submit(overflow, {JobClass::Background, 0, 0}, overflowTicket)));
    _historyResidencySource.RetryDeferred();
    _historyResidencySource.Pump();
    if (!expect("capacity-refusal", "128-live-records-refuse-maintenance-before-capture",
                full && overflow && refusalSparse.ByteSize() == 8 && refusalGate->captures.load() == 0 &&
                    refusalGate->preparations.load() == 0 && !_historyResidencyTicket && !_historyResidencyStamp &&
                    !_historyWork.Busy() && _session.Document() == refusalOriginal &&
                    _session.history.CurrentRevision() == refusalRevision &&
                    _session.Generation() == refusalGeneration && !_session.history.IsModified() &&
                    _session.history.UndoCount() == 1 && _session.history.RetainedBytes(refusalOriginal) == 12))
        return 5;
    (void)SetEvent(refusalGate->release.get());
    if (!expect("capacity-refusal", "capacity-records-retire-after-owner-drain",
                SettleJobs(_jobs, 15000, DispatchMessageThunk, this) == SettleResult::Settled && _jobs.IsIdle() &&
                    _jobs.ReservedMemoryBytes() == 0))
        return 5;
    refusalCleanup.release();
    _historyResidencySource.StopNotifications();
    _historyResidencySource.CloseAfterWorkers();
    _historyResidencyTestGate.reset();

    _session.history = DocumentHistory(100, HistoryLimitForPhysicalMemory(0));
    std::fprintf(stderr, "PASS history-residency-host.summary checks=%d cases=%zu controls=2\n", checks, names.size());
    return 0;
}
