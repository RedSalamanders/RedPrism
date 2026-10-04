#include "Application.h"

#include "../Tests/Support/DocumentFixture.h"
#include "DocumentDraft.h"

#include "AdjustmentOpeningWork.h"
#include "ChromeHost.h"
#include "DocumentReadbackJob.h"
#include "UtilityWorkTestGate.h"
#include "resource.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>
#include <new>
#include <unordered_set>
#include <utility>

namespace
{
#pragma warning(push)
#pragma warning(disable : 4324)
struct AdjustmentOpeningPayload final : DeferredReleaseNode
{
    std::optional<AdjustmentOpeningInput> input;
    std::optional<CanvasDocument> source;
    DocumentHistory::StateView history;
    std::optional<PreparedAdjustmentOpening> publication;
    CanvasDocument readbackDocument;
    PixelBuffer readbackPixels;
    Raster readbackImage;
    std::shared_ptr<UtilityWorkTestGate> gate;
    DWORD ownerThread = 0;
    AdjustmentOpeningPayload() noexcept
    {
        bytes = kDeferredReleaseThreshold;
        destroy = [](DeferredReleaseNode* node) noexcept
        { const std::unique_ptr<AdjustmentOpeningPayload> owned(static_cast<AdjustmentOpeningPayload*>(node)); };
    }
    ~AdjustmentOpeningPayload()
    {
        if (gate)
        {
            gate->retiredOffOwner.store(GetCurrentThreadId() != ownerThread);
            gate->retiredNodes.fetch_add(1);
            if (GetCurrentThreadId() == ownerThread)
                gate->retiredOwnerNodes.fetch_add(1);
        }
    }
};
#pragma warning(pop)
struct RetireAdjustmentOpening final
{
    void operator()(AdjustmentOpeningPayload* payload) const noexcept
    {
        if (payload && !TryDeferRelease(*payload))
        {
            const std::unique_ptr<AdjustmentOpeningPayload> owned(payload);
        }
    }
};
} // namespace

class Application::AdjustmentOpeningJob final : public DocumentReadbackJob
{
  public:
    AdjustmentOpeningJob(Application& owner, HistoryWorkStamp stamp, D3D11_BOX region,
                         std::uint64_t retirement) noexcept
        : DocumentReadbackJob(owner._jobs, owner._gpuWork, owner._composition, region), _owner(owner), _stamp(stamp),
          _generation(owner._session.Generation()), _sequence(owner._session.ColorEditSequence()),
          _retirement(retirement), _id(owner._session.AdjustmentEditingID().value()),
          _active(owner._session.ActiveLayerID()), _mask(owner._session.IsMaskSelected()),
          _crop(owner._session.CropFrame()), _gate(owner._utilityWorkTestGate), _ownerThread(GetCurrentThreadId())
    {
    }
    ~AdjustmentOpeningJob() override
    {
        if (_payload)
            TransferReadbackOwners(_payload->readbackDocument, _payload->readbackPixels, _payload->readbackImage);
        // GPU leases/targets finish on the owner in the base; all CPU owners move to this release aggregate.
    }

  private:
    [[nodiscard]] bool Current() const noexcept
    {
        return !_owner._shutdownStarted && _owner._utilityStamp == _stamp && _owner._historyWork.IsCurrent(_stamp) &&
               _owner._session.DocumentSerial() == _stamp.documentSerial &&
               _owner._session.Generation() == _generation && _owner._session.ColorEditSequence() == _sequence &&
               _owner._session.AdjustmentEditingID() == std::optional(_id) && !_owner._session.HasColorEdit() &&
               !_owner._session.AdjustmentOriginal() && _owner._session.ActiveLayerID() == _active &&
               _owner._session.IsMaskSelected() == _mask && _owner._session.CropFrame() == _crop;
    }
    HRESULT CaptureReadbackDocument(CanvasDocument& document) noexcept override
    {
        if (!Current() || _owner._historyWork.CancelRequested(_stamp))
            return E_ABORT;
        try
        {
            _payload.reset(new AdjustmentOpeningPayload);
            _payload->bytes = (std::max)(_retirement, kDeferredReleaseThreshold);
            _payload->gate = _gate;
            _payload->ownerThread = _ownerThread;
            if (_gate)
            {
                _gate->probeReservationHeld = _gate->probeReservationBytes != 0 &&
                                              _owner._jobs.ReservedMemoryBytes() >= _gate->probeReservationBytes;
                if (_gate->throwCaptureFailure)
                    throw std::bad_alloc{};
            }
            _payload->input = _owner._session.PrepareAdjustmentOpeningInput();
            if (!_payload->input)
                return E_ABORT;
            document = _owner._session.Document().value();
            _payload->history = _owner._session.history.CaptureState();
            if (_gate)
            {
                _gate->captured.store(true);
                _gate->readbackCaptures.fetch_add(1);
            }
            return S_OK;
        }
        catch (const std::bad_alloc&)
        {
            return E_OUTOFMEMORY; // admitted construction fails before any session/history publication
        }
    }
    JobResult PrepareReadbackSnapshot(CanvasDocument& document, JobContext& context) override
    {
        if (!_payload || !_payload->input)
            return {JobStatus::Failed, E_UNEXPECTED};
        if (context.cancellation.IsCancelled())
            return {JobStatus::Cancelled};
        if (_payload->source)
            return {}; // bounded device restart reuses this same immutable source and completed hidden snapshot
        _payload->source.emplace(std::move(document));
        if (_gate && _gate->throwProbeAllocationFailure)
            throw std::bad_alloc{};
        const auto& source = _payload->source.value();
        auto draft = CanvasDocumentDraft::Clone(source, source.layers.size(), context.cancellation);
        if (!draft)
            return context.cancellation.IsCancelled() ? JobResult{JobStatus::Cancelled}
                                                      : JobResult{JobStatus::Failed, E_INVALIDARG};
        // Preserve every record for live mask sources/folder ancestry. Only visibility differs for sampling.
        std::unordered_set<Guid> hidden;
        bool above = false;
        for (const Guid& id : RenderLayerIds(source))
        {
            if (context.cancellation.IsCancelled())
                return {JobStatus::Cancelled};
            above = above || id == _id;
            if (above)
                hidden.insert(id);
        }
        for (auto& layer : draft.value().EditLayers())
        {
            if (context.cancellation.IsCancelled())
                return {JobStatus::Cancelled};
            if (!layer.isGroup && (layer.id == _id || hidden.contains(layer.id)))
                layer.visible = false;
        }
        auto sealed = draft.value().Seal(context.cancellation);
        if (!sealed)
            return context.cancellation.IsCancelled() ? JobResult{JobStatus::Cancelled}
                                                      : JobResult{JobStatus::Failed, E_INVALIDARG};
        document = std::move(sealed.value());
        return {};
    }
    HRESULT PrepareReadbackDevice(HRESULT reason) noexcept override
    {
        return _owner.PrepareUtilityDevice(reason);
    }
    bool ProcessReadbackDetached() const noexcept override
    {
        return true; // final mapped bytes are acknowledged and utility surfaces released before history Begin
    }
    JobResult ProcessReadback(Raster& image, JobContext& context) override
    {
        if (!_payload || !_payload->input || !_payload->source)
            return {JobStatus::Failed, E_UNEXPECTED};
        const DocumentHistory::PrepareObserver observer{[](void* opaque, DocumentHistory::PrepareStage stage)
                                                        {
                                                            auto* gate = static_cast<UtilityWorkTestGate*>(opaque);
                                                            if (gate && gate->throwHistoryStage == stage)
                                                                throw std::bad_alloc{};
                                                        },
                                                        _gate.get()};
        _payload->publication = DocumentSession::PrepareAdjustmentOpeningPublication(
            std::move(_payload->input.value()), _payload->source, std::move(image), std::move(_payload->history),
            context.cancellation, _gate ? observer : DocumentHistory::PrepareObserver{});
        // image is empty: publication/failure owns its copied bytes on the worker, never a local owner callback.
        if (context.cancellation.IsCancelled())
            return {JobStatus::Cancelled};
        if (!_payload->publication)
            return {JobStatus::Failed, E_FAIL};
        if (_gate && _gate->visits.fetch_add(1) == 0)
        {
            if (!PostThreadMessageW(_ownerThread, UtilityWorkTestGate::kMessage, 0, 0))
                return {JobStatus::Failed, HRESULT_FROM_WIN32(GetLastError())};
            const HANDLE handles[]{_gate->release.get(), context.shutdownEvent};
            const auto waited = WaitForMultipleObjects(2, handles, FALSE, 10000);
            if (context.cancellation.IsCancelled() || waited == WAIT_OBJECT_0 + 1)
                return {JobStatus::Cancelled};
            return waited == WAIT_OBJECT_0 ? _gate->result
                                           : JobResult{JobStatus::Failed, HRESULT_FROM_WIN32(ERROR_TIMEOUT)};
        }
        return {};
    }
    void ReadbackCompleted(JobResult result, Raster, std::uint64_t) noexcept override
    {
        if (_owner._utilityStamp != _stamp || !_owner._historyWork.IsCurrent(_stamp))
            return;
        HostOutcome outcome =
            !Current() ? HostOutcome::Stale
            : _owner._historyWork.CancelRequested(_stamp) || result.status == JobStatus::Cancelled
                ? HostOutcome::Cancelled
            : result.status == JobStatus::Failed ? HostOutcome::Failed
            : _payload && _payload->publication &&
                    _owner._session.ApplyPreparedAdjustmentOpeningIfCurrent(_payload->publication.value())
                ? HostOutcome::Succeeded
                : HostOutcome::Stale;
        const auto error = outcome == HostOutcome::Failed      ? (result.error < 0 ? result.error : E_FAIL)
                           : outcome == HostOutcome::Succeeded ? S_OK
                                                               : E_ABORT;
        if (outcome != HostOutcome::Succeeded && _owner._session.AdjustmentEditingID() == std::optional(_id) &&
            !_owner._session.HasColorEdit())
            _owner._session.SetAdjustmentEditingID(std::nullopt);
        _owner._utilityTicket.reset();
        _owner._utilityStamp.reset();
        (void)_owner._historyWork.Finish(_stamp);
        _owner.ResumeHostHistory(_stamp, outcome, error);
        _owner.DocumentChanged();
        if (outcome == HostOutcome::Failed)
            _owner._prompts->PostNotice(_owner._window.get(), kNoticeMessage, IDS_ERROR_FLATTEN);
    }
    Application& _owner; // Current/capture/device/completion only; worker stages never reach the live owner
    const HistoryWorkStamp _stamp;
    const std::uint64_t _generation, _sequence, _retirement;
    const Guid _id;
    const std::optional<Guid> _active;
    const bool _mask;
    const std::optional<CropRect> _crop;
    const std::shared_ptr<UtilityWorkTestGate> _gate;
    const DWORD _ownerThread;
    std::unique_ptr<AdjustmentOpeningPayload, RetireAdjustmentOpening> _payload;
};

HRESULT Application::StartAdjustmentOpening() noexcept
{
    if (_utilityTicket || !_session.Document() || !_session.AdjustmentEditingID() || _session.HasColorEdit() ||
        _session.AdjustmentOriginal())
        return HRESULT_FROM_WIN32(ERROR_INVALID_STATE);
    const auto ordinal = _activeHostRequest ? std::optional(_activeHostRequest->ordinal) : _historyWork.NextOrdinal();
    if (!ordinal)
        return HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA);
    _utilityStamp = _historyWork.Begin(_session.DocumentSerial(), ordinal.value(), HistoryWorkKind::Commit);
    if (!_utilityStamp && _hostFinishedStamp && _hostFinishedStamp->ordinal == ordinal.value())
        _utilityStamp =
            _historyWork.Continue(_hostFinishedStamp.value(), _session.DocumentSerial(), HistoryWorkKind::Commit);
    if (!_utilityStamp)
        return E_UNEXPECTED;
    HRESULT result = E_INVALIDARG;
    try
    {
        const D3D11_BOX region{
            0, 0, 0, static_cast<UINT>(_session.Document()->width), static_cast<UINT>(_session.Document()->height), 1};
        const auto pixels = DocumentReadbackJob::RequiredMemory(region);
        const auto history = _session.history.EditMemoryBytes(_session.Document());
        auto retirement = _session.ImageImportRetirementMemoryBytes();
        std::uint64_t memory = sizeof(AdjustmentOpeningJob) + sizeof(AdjustmentOpeningPayload) + 64 * 1024;
        const auto maximum = (std::numeric_limits<std::uint64_t>::max)();
        const auto add = [&](std::uint64_t count, std::uint64_t size) noexcept
        {
            if ((size && count > maximum / size) || count * size > maximum - memory)
                return false;
            memory += count * size;
            return true;
        };
        // Includes private source/sampling records, render-order/set storage, panel/original curves, Begin and
        // complete copied readback. Sharing is conservatively overcounted until H2/H3 cache these traversals.
        if (pixels && history && retirement && add(2, pixels) && add(2, history) &&
            add(_session.Document()->layers.size(), 4 * sizeof(Guid) + 1024) && memory <= (maximum - retirement) / 2)
        {
            retirement += memory * 2;
            if (_utilityWorkTestGate)
                _utilityWorkTestGate->probeReservationBytes = memory;
            std::unique_ptr<Job> job =
                std::make_unique<AdjustmentOpeningJob>(*this, _utilityStamp.value(), region, retirement);
            JobTicket ticket;
            result =
                static_cast<HRESULT>(_jobs.Submit(job, {JobClass::Commit, _session.DocumentSerial(), memory}, ticket));
            if (SUCCEEDED(result))
                _utilityTicket = ticket;
        }
    }
    catch (const std::bad_alloc&)
    {
        result = E_OUTOFMEMORY; // owning admission boundary: fails this entire request before live publication
    }
    if (FAILED(result))
    {
        (void)_historyWork.Finish(_utilityStamp.value());
        _hostFinishedStamp = _utilityStamp;
        _utilityStamp.reset();
        return result;
    }
    if (_activeHostRequest)
    {
        _hostCommitStamp = _utilityStamp;
        _hostAfterPrerequisite = HostStage::Complete;
    }
    return S_OK;
}

int Application::RunAdjustmentOpeningSelfTest() noexcept
{
    enum class Mode
    {
        Ordered,
        Younger,
        Cancel,
        Failure,
        Stale,
        Rebind,
        Capture,
        Sampling,
        AdjustmentSnapshot,
        PendingSnapshot,
        PendingRecord,
        Epoch
    };
    struct State final
    {
        Application& owner;
        Mode mode;
        ContinuationReceipt opening;
        std::optional<ContinuationReceipt> younger;
        bool visited = false, held = false;
        HRESULT rebound = E_PENDING;
    };
    const auto dispatch = [](void* opaque, void* native) noexcept
    {
        auto& state = *static_cast<State*>(opaque);
        auto& owner = state.owner;
        const auto& message = *static_cast<MSG*>(native);
        if (message.message != UtilityWorkTestGate::kMessage)
        {
            owner.DispatchQueuedMessage(*static_cast<MSG*>(native));
            return;
        }
        state.visited = true;
        state.held = owner._historyWork.Busy() && !owner._session.HasColorEdit() &&
                     owner._session.history.CaptureState().Depth() == 0 && owner._gpuWork.LentCount() == 0 &&
                     owner._composition.UtilitySurfaceBytes() == 0 &&
                     state.opening.Read().outcome == HostOutcome::Pending &&
                     (!state.younger || state.younger->Read().outcome == HostOutcome::Pending);
        if (state.mode == Mode::Cancel)
            owner.CancelHostContinuations();
        else if (state.mode == Mode::Stale)
            owner._session.SetOpacity(0.5); // deliberately invalidate the captured whole document
        else if (state.mode == Mode::Epoch)
        {
            owner._session.history.Begin(L"Epoch only", owner._session.Document(), owner._session.ActiveLayerID());
            owner._session.history.End(owner._session.Document(), owner._session.ActiveLayerID());
        }
        else if (state.mode == Mode::Rebind)
        {
            state.rebound = owner._renderer.RecoverDeviceForTest();
            owner.BindCompositionDevice();
        }
        (void)SetEvent(owner._utilityWorkTestGate->release.get());
    };
    const auto fail = [](const wchar_t* message) noexcept
    {
        SelfTestFailure(message);
        return 5;
    };
    const auto settle = [this]() noexcept
    { return SettleJobs(_jobs, 15000, DispatchMessageThunk, this) == SettleResult::Settled; };
    CancelColorEdit();
    if (!settle())
        return fail(L"Earlier work did not settle before adjustment opening qualification.\n");
    const auto cleanup = wil::scope_exit(
        [&]() noexcept
        {
            if (_utilityWorkTestGate)
                (void)SetEvent(_utilityWorkTestGate->release.get());
            CancelHostContinuations();
            if (!settle())
                std::_Exit(5);
            _utilityWorkTestGate.reset();
            _session.CancelColorEdit(); // bounded fixture teardown after worker acknowledgement
        });
    constexpr std::array<std::uint8_t, 16> bytes{0, 0, 0, 255, 64, 64, 64, 255, 128, 128, 128, 255, 255, 255, 255, 255};
    const Raster image = Raster::Make(4, 1, RasterFormat::Bgra8888, bytes.data(), bytes.size());
    constexpr std::array kinds{AdjustmentKind::Hsv,      AdjustmentKind::Levels,      AdjustmentKind::Curves,
                               AdjustmentKind::Exposure, AdjustmentKind::GradientMap, AdjustmentKind::Grain};
    for (const auto kind : kinds)
    {
        for (const auto mode :
             {Mode::Ordered, Mode::Younger, Mode::Cancel, Mode::Failure, Mode::Stale, Mode::Rebind, Mode::Capture,
              Mode::Sampling, Mode::AdjustmentSnapshot, Mode::PendingSnapshot, Mode::PendingRecord, Mode::Epoch})
        {
            _session.CancelColorEdit();
            DocumentFixture document;
            document.id = Guid::Generate();
            document.width = 4;
            document.height = 1;
            ImageLayer base = ImageLayer::Blank(L"Literal opening source", Vec2{4, 1});
            base.asset = RasterAsset{image, {}, base.name};
            ImageLayer adjustment = ImageLayer::Blank(L"Opening adjustment", Vec2{4, 1});
            LayerAdjustment parameters;
            parameters.kind = kind;
            parameters.hsvSettings = HueSaturationSettings{};
            parameters.exposureSettings = ExposureSettings{};
            parameters.gradientMapSettings = GradientMapSettings{};
            parameters.grainSettings = GrainSettings{0, 1, 50, 7};
            adjustment.adjustment = parameters;
            const Guid id = adjustment.id;
            ImageLayer above = ImageLayer::Blank(L"Above stays visible in live records", Vec2{4, 1});
            above.asset = RasterAsset{SolidRaster(4, 1, 0xFFFF0000u), {}, above.name};
            document.layers.push_back(std::move(base));
            document.layers.push_back(std::move(adjustment));
            document.layers.push_back(std::move(above));
            ReplaceDocument(std::move(document).Seal(), id, {});
            _session.SetOpacity(0.75);
            _session.Undo();
            const auto revision = _session.history.CurrentRevision();
            const auto before = _session.Document();
            const auto generation = _session.Generation();
            const auto sequence = _session.ColorEditSequence();
            const auto presents = _renderer.PresentCount();
            _utilityWorkTestGate = std::make_shared<UtilityWorkTestGate>();
            const auto gate = _utilityWorkTestGate;
            gate->release.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
            if (!gate->release)
                return fail(L"Adjustment opening could not create its private gate.\n");
            gate->throwCaptureFailure = mode == Mode::Capture;
            gate->throwProbeAllocationFailure = mode == Mode::Sampling;
            if (mode == Mode::Failure)
                gate->result = {JobStatus::Failed, E_ACCESSDENIED};
            using Stage = DocumentHistory::PrepareStage;
            if (mode == Mode::AdjustmentSnapshot)
                gate->throwHistoryStage = Stage::AdjustmentSnapshot;
            if (mode == Mode::PendingSnapshot)
                gate->throwHistoryStage = Stage::PendingSnapshot;
            if (mode == Mode::PendingRecord)
                gate->throwHistoryStage = Stage::PendingRecord;
            State state{*this, mode, QueueHostInput(HostCommandInput{ChromeHost::kCommandEditAdjustment})};
            if (mode == Mode::Younger)
                state.younger = QueueHostInput(HostLayerRenameInput{id, L"Queued after opening"});
            if (SettleJobs(_jobs, 15000, dispatch, &state) != SettleResult::Settled)
                return fail(L"An adjustment opening did not acknowledge its admitted graph.\n");
            const bool constructionFailure = mode == Mode::Capture || mode == Mode::Sampling ||
                                             mode == Mode::AdjustmentSnapshot || mode == Mode::PendingSnapshot ||
                                             mode == Mode::PendingRecord;
            const bool success = mode == Mode::Ordered || mode == Mode::Rebind || mode == Mode::Younger;
            const auto expected = success                                      ? HostOutcome::Succeeded
                                  : mode == Mode::Cancel                       ? HostOutcome::Cancelled
                                  : mode == Mode::Stale || mode == Mode::Epoch ? HostOutcome::Stale
                                                                               : HostOutcome::Failed;
            const auto outcome = state.opening.Read();
            if (outcome.outcome != expected || (constructionFailure ? state.visited : !state.visited || !state.held) ||
                !gate->probeReservationHeld || !gate->retiredOffOwner.load() || gate->retiredNodes.load() != 1 ||
                gate->retiredOwnerNodes.load() != 0 || _historyWork.Busy() || _utilityTicket ||
                _gpuWork.LentCount() != 0 || _gpuWork.InFlightCount() != 0 || _gpuWork.HasPending() ||
                _composition.UtilitySurfaceBytes() != 0 || _jobs.ReservedMemoryBytes() != 0 ||
                _renderer.PresentCount() != presents || _session.HasColorEdit() != (success && mode != Mode::Younger))
                return fail(L"Adjustment opening violated terminal lineage, hidden rendering, GPU acknowledgement or "
                            L"retirement.\n");
            if (mode == Mode::Younger)
            {
                if (!state.younger || state.younger->Read().outcome != HostOutcome::Succeeded ||
                    _session.ActiveLayer()->name != L"Queued after opening" ||
                    _session.ActiveLayer()->adjustment != std::optional(parameters) ||
                    _session.history.CaptureState().Depth() != 0 || _session.history.UndoCount() != 1 ||
                    _session.Document()->layers.front().asset->image != image ||
                    !_session.Document()->layers.back().visible)
                    return fail(L"A younger adjustment action ran before opening/closure acknowledgement or changed "
                                L"source visibility.\n");
            }
            else if (success)
            {
                const auto& panel = _session.PendingColorEdit().value();
                std::array<std::uint8_t, bytes.size()> actual{};
                panel.original.ReadRegion(0, 0, 4, 1, actual.data(), 16, 0);
                if (_session.Document() != before || _session.Generation() != generation ||
                    _session.history.CurrentRevision() != revision ||
                    _session.history.CaptureState().FutureCount() != 1 ||
                    _session.history.RedoName() != L"Layer Opacity" || _session.history.CaptureState().Depth() != 1 ||
                    !panel.deferred || panel.layerID != id || panel.openingSequence != sequence + 1 ||
                    panel.original.Width() != 4 || panel.original.Height() != 1 ||
                    panel.original.Format() != RasterFormat::Bgra8888 || actual != bytes ||
                    _session.AdjustmentOriginal() != std::optional(parameters) ||
                    (mode == Mode::Rebind && (FAILED(state.rebound) || gate->visits.load() != 1)))
                    return fail(L"Adjustment opening changed live visibility, source bytes, baseline or named "
                                L"transaction semantics.\n");
            }
            else if (_session.ColorEditSequence() != sequence || _session.history.CaptureState().Depth() != 0 ||
                     _session.AdjustmentOriginal() || _session.AdjustmentEditingID() ||
                     (mode != Mode::Stale &&
                      (_session.Document() != before || _session.Generation() != generation ||
                       _session.history.CurrentRevision() != revision || !_session.history.CanRedo())) ||
                     (constructionFailure && outcome.error != E_OUTOFMEMORY) ||
                     (mode == Mode::Failure && outcome.error != E_ACCESSDENIED))
                return fail(L"Refused adjustment opening changed source/history, installed a partial panel or lost its "
                            L"error.\n");
            std::fprintf(stderr, "PASS adjustment-opening-host kind=%u mode=%u\n", static_cast<unsigned int>(kind),
                         static_cast<unsigned int>(mode));
        }
    }
    return 0;
}
