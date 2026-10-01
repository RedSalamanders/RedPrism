#include "Application.h"

#include "ChromeHost.h"
#include "RasterEditTestGate.h"
#include "resource.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <new>
#include <unordered_set>
#include <utility>

namespace
{
// A sparse frame can own hundreds of individually small tile allocations. Retire the complete captured/result
// graph together; per-Raster deferral alone would walk and free all those small owners on the UI thread.
#pragma warning(push)
#pragma warning(disable : 4324)
struct RasterEditPayload final : DeferredReleaseNode
{
    RasterEditInput input;
    std::optional<JobToken> token;
    RasterEditResult prepared;
    std::wstring name;
    std::shared_ptr<RasterEditTestGate> gate;
    DWORD ownerThread = 0;
    RasterEditPayload() noexcept
    {
        bytes = kDeferredReleaseThreshold; // allocation failure before worker accounting still defers the graph
        destroy = [](DeferredReleaseNode* node) noexcept
        { const std::unique_ptr<RasterEditPayload> owned(static_cast<RasterEditPayload*>(node)); };
    }
    ~RasterEditPayload()
    {
        if (gate)
        {
            gate->payloadRetiredOffOwner.store(GetCurrentThreadId() != ownerThread);
            gate->payloadRetired.store(true);
        }
    }

    void AccountRetainedGraph()
    {
        std::unordered_set<const void*> seen;
        std::uint64_t retained = sizeof(RasterEditPayload);
        const auto add = [&](std::uint64_t value) noexcept
        {
            retained = value > (std::numeric_limits<std::uint64_t>::max)() - retained
                           ? (std::numeric_limits<std::uint64_t>::max)()
                           : retained + value;
        };
        const auto raster = [&](const Raster& image)
        {
            image.ForEachStorage(
                [&](const Raster& storage)
                {
                    if (seen.insert(storage.Identity()).second)
                    {
                        // ByteSize observes published contiguous bytes only; never assembles a sparse input. The
                        // storage allowance covers its control block, mutex, buffers and allocation rounding.
                        add(PixelBuffer::RequiredBytes(storage.ByteSize()) + 256ull +
                            storage.Tiles().capacity() * sizeof(RasterTile));
                    }
                });
        };
        const auto layer = [&](const ImageLayer& value)
        {
            add(value.name.capacity() * sizeof(wchar_t));
            if (value.asset)
            {
                add(value.asset->name.capacity() * sizeof(wchar_t));
                raster(value.asset->image);
                raster(value.asset->thumbnail);
            }
            if (value.mask)
            {
                add(value.mask->asset.name.capacity() * sizeof(wchar_t));
                raster(value.mask->asset.image);
                raster(value.mask->asset.thumbnail);
            }
            raster(value.shapeImage);
        };
        layer(input.layer);
        if (prepared.layer)
            layer(prepared.layer.value());
        // The token's raster owners already occur in input.layer. Selection storage is shared, but a stale
        // document can release its last other owner, so include the held capacity in this retirement bound.
        if (input.selection)
            add(input.selection->loops.StorageBytes());
        add(name.capacity() * sizeof(wchar_t));
        bytes = retained; // conservative reachable capacity, including immutable source owners held by this job
    }
};
#pragma warning(pop)

struct RetireRasterEditPayload final
{
    void operator()(RasterEditPayload* payload) const noexcept
    {
        if (payload && !TryDeferRelease(*payload))
        {
            const std::unique_ptr<RasterEditPayload> owned(payload);
        }
    }
};
} // namespace

class Application::RasterEditJob final : public Job
{
  public:
    RasterEditJob(Application& owner, HistoryWorkStamp stamp, std::uint64_t generation, RasterEditKind kind,
                  FillSource fill, Guid target, bool mask, std::int64_t pixelLimit)
        : _owner(owner), _stamp(stamp), _generation(generation), _kind(kind), _fill(fill), _target(target), _mask(mask),
          _pixelLimit(pixelLimit), _gate(owner._rasterEditTestGate), _ownerThread(GetCurrentThreadId())
    {
    }

    JobResult Run(JobContext& context) override
    {
        if (context.cancellation.IsCancelled())
            return {JobStatus::Cancelled};
        if (!_payload)
            return {}; // reach owner capture only after the complete reservation has been acquired
        _payload->AccountRetainedGraph();
        if (_gate && !_gate->afterPreparation)
        {
            const auto result = PassGate(context);
            if (result.status != JobStatus::Finished)
                return result;
        }
        if (_gate && _gate->throwAllocationFailure)
            throw std::bad_alloc{};
        // Recounting itself allocates bookkeeping on the worker. If it throws after preparation, keep the
        // aggregate eligible for deferral even when the opening source was tiny and the result grew large.
        _payload->bytes = (std::max)(_payload->bytes, kDeferredReleaseThreshold);
        _payload->input.progressContext = &context;
        _payload->input.progress = [](void* value, std::size_t completed, std::size_t total) noexcept
        {
            static_cast<JobContext*>(value)->ReportProgress(total ? static_cast<unsigned int>(completed * 100 / total)
                                                                  : 100);
        };
        const auto clearProgressBorrow = wil::scope_exit(
            [&]() noexcept
            {
                _payload->input.progress = nullptr;
                _payload->input.progressContext = nullptr;
            });
        _payload->prepared = RunRasterEdit(_payload->input, context.cancellation);
        _payload->AccountRetainedGraph();
        if (_gate && _gate->status)
            _payload->prepared.status = _gate->status.value();
        if (_gate && _gate->afterPreparation)
        {
            const auto result = PassGate(context);
            if (result.status != JobStatus::Finished)
                return result;
        }
        return context.cancellation.IsCancelled() || _payload->prepared.status == RasterEditStatus::Cancelled
                   ? JobResult{JobStatus::Cancelled}
                   : JobResult{};
    }

    bool DeferCompletion(JobResult result, JobTicket ticket) noexcept override
    {
        if (_payload || result.status != JobStatus::Finished || !Current() ||
            _owner._historyWork.CancelRequested(_stamp))
            return false;
        try
        {
            if (_gate)
            {
                _gate->captureReservationHeld =
                    _owner._jobs.ReservedMemoryBytes() == _gate->reservationBytes && _gate->reservationBytes != 0;
                if (_gate->throwCaptureFailure)
                    throw std::bad_alloc{};
            }
            const auto* layer = _owner._session.ActiveLayer();
            if (!layer || layer->id != _target || _owner._session.IsMaskSelected() != _mask)
            {
                _captureError = E_ABORT;
                return false;
            }
            _payload.reset(new RasterEditPayload);
            _payload->gate = _gate;
            _payload->ownerThread = _ownerThread;
            auto& input = _payload->input;
            input.layer = *layer;
            input.selection = _owner._session.Selection();
            input.canvasWidth = _owner._session.Document()->width;
            input.canvasHeight = _owner._session.Document()->height;
            input.pixelLimit = _pixelLimit;
            input.kind = _kind;
            input.mask = _mask;
            const auto argb =
                _fill == FillSource::Background ? _owner._session.BackgroundColor() : _owner._session.ForegroundColor();
            input.color = {static_cast<double>((argb >> 16) & 255) / 255, static_cast<double>((argb >> 8) & 255) / 255,
                           static_cast<double>(argb & 255) / 255, 1};
            if (_mask)
            {
                const bool white =
                    _fill == FillSource::Background ? !_owner._session.maskPaintWhite : _owner._session.maskPaintWhite;
                input.color = {white ? 1.0 : 0.0, white ? 1.0 : 0.0, white ? 1.0 : 0.0, 1};
            }
            _payload->token = _owner._session.CaptureJobToken(_target);
            _payload->name = _kind == RasterEditKind::Invert            ? (_mask ? L"Invert Mask" : L"Invert")
                             : _kind == RasterEditKind::Clear && !_mask ? L"Clear"
                                                                        : (_mask ? L"Fill Mask" : L"Fill");
            if (_payload->token && _owner._jobs.Resume(ticket))
                return true;
            _captureError = E_UNEXPECTED;
        }
        catch (const std::bad_alloc&)
        {
            _captureError = E_OUTOFMEMORY; // explicit noexcept capture boundary fails the whole edit
        }
        return false;
    }

    void Completed(JobResult result) noexcept override
    {
        const bool current =
            Current() && (!_payload || !_payload->token || _owner._session.CanApplyJobResult(*_payload->token, 0));
        HostOutcome outcome = !current || _captureError == E_ABORT ? HostOutcome::Stale
                              : result.status == JobStatus::Cancelled || _owner._historyWork.CancelRequested(_stamp)
                                  ? HostOutcome::Cancelled
                              : result.status == JobStatus::Failed || FAILED(_captureError) ? HostOutcome::Failed
                                                                                            : HostOutcome::Succeeded;
        std::int32_t error = FAILED(_captureError) ? _captureError : result.error;
        bool changed = false;
        if (outcome == HostOutcome::Succeeded)
        {
            const auto status = _payload ? _payload->prepared.status : RasterEditStatus::Invalid;
            switch (status)
            {
            case RasterEditStatus::Ready:
                changed = _payload->prepared.layer && _payload->token &&
                          _owner._session.ApplyRasterEditIfCurrent(std::move(*_payload->prepared.layer), _mask,
                                                                   std::move(_payload->name), *_payload->token);
                if (!changed)
                {
                    outcome = HostOutcome::Stale;
                    error = E_ABORT;
                }
                break;
            case RasterEditStatus::Unchanged:
                break;
            case RasterEditStatus::Cancelled:
                outcome = HostOutcome::Cancelled;
                error = E_ABORT;
                break;
            default:
                outcome = HostOutcome::Failed;
                error = status == RasterEditStatus::TooLarge ? HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE)
                        : status == RasterEditStatus::Memory ? E_OUTOFMEMORY
                                                             : E_INVALIDARG;
                break;
            }
        }
        if (outcome == HostOutcome::Failed)
        {
            ++_owner._errorsShown;
            _owner._lastErrorId =
                error == HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE) ? IDS_ERROR_TOO_LARGE : IDS_ERROR_PIXEL_EDIT;
            _owner._prompts->PostNotice(_owner._window.get(), kNoticeMessage, _owner._lastErrorId);
        }
        if (outcome == HostOutcome::Cancelled || outcome == HostOutcome::Stale)
            error = E_ABORT;
        _owner._rasterEditTicket.reset();
        _owner._rasterEditStamp.reset();
        if (changed)
            _owner.DocumentChanged();
        else
        {
            _owner.SyncChrome();
            _owner._frameInvalidated = true;
        }
        (void)_owner._historyWork.Finish(_stamp);
        _owner.ResumeHostHistory(_stamp, outcome, error);
    }

  private:
    bool Current() const noexcept
    {
        return _owner._rasterEditStamp == _stamp && _owner._historyWork.IsCurrent(_stamp) &&
               _owner._activeHostRequest && _owner._activeHostRequest->ordinal == _stamp.ordinal &&
               _owner._activeHostRequest->documentSerial == _stamp.documentSerial &&
               _owner._session.DocumentSerial() == _stamp.documentSerial && _owner._session.Generation() == _generation;
    }
    JobResult PassGate(JobContext& context) noexcept
    {
        _gate->visits.fetch_add(1);
        if (!PostThreadMessageW(_ownerThread, RasterEditTestGate::kMessage, 0, 0))
            return {JobStatus::Failed, HRESULT_FROM_WIN32(GetLastError())};
        const HANDLE handles[]{_gate->release.get(), context.shutdownEvent};
        const auto waited = WaitForMultipleObjects(2, handles, FALSE, 10000);
        if (context.cancellation.IsCancelled() || waited == WAIT_OBJECT_0 + 1)
            return {JobStatus::Cancelled};
        return waited == WAIT_OBJECT_0 ? _gate->result
                                       : JobResult{JobStatus::Failed, HRESULT_FROM_WIN32(ERROR_TIMEOUT)};
    }
    Application& _owner; // owner callbacks only; Run reads the detached payload and immutable gate
    const HistoryWorkStamp _stamp;
    const std::uint64_t _generation;
    const RasterEditKind _kind;
    const FillSource _fill;
    const Guid _target;
    const bool _mask;
    const std::int64_t _pixelLimit;
    const std::shared_ptr<RasterEditTestGate> _gate;
    const DWORD _ownerThread;
    std::unique_ptr<RasterEditPayload, RetireRasterEditPayload> _payload;
    HRESULT _captureError = S_OK;
};

void Application::ExecuteRasterEditCommand(RasterEditKind kind, FillSource fill) noexcept
{
    _hostExecutionError = StartRasterEdit(kind, fill);
    if (FAILED(_hostExecutionError))
    {
        ++_errorsShown;
        _lastErrorId = _hostExecutionError == HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE) ? IDS_ERROR_TOO_LARGE
                                                                                       : IDS_ERROR_PIXEL_EDIT;
        _prompts->PostNotice(_window.get(), kNoticeMessage, _lastErrorId);
    }
}

HRESULT Application::StartRasterEdit(RasterEditKind kind, FillSource fill) noexcept
{
    if (!_activeHostRequest || _rasterEditTicket)
        return E_UNEXPECTED;
    if (!_session.Document())
        return S_FALSE;
    // Preserve legacy settlement while A4/A6 move these prerequisites to workers. This is an explicitly measured
    // remaining caller, not proof that pending gradient/distortion routes have passed G1.
    if (kind == RasterEditKind::Invert)
        (void)EndCanvasEdit(CanvasEdit::Transform, EditEnd::Enter);
    CommitGradient();
    const auto* layer = _session.ActiveLayer();
    if (!layer || (kind == RasterEditKind::Invert ? !_session.CanInvert() : !_session.CanPaint()) ||
        (kind == RasterEditKind::Clear && (!_session.Selection() || (!_session.IsMaskSelected() && !layer->asset))))
        return S_FALSE;
    const auto pixelLimit = _session.RasterEditPixelLimit(*layer, _session.IsMaskSelected());
    const auto memory = RasterEditMemoryBytes(*layer, _session.Selection(), _session.Document()->width,
                                              _session.Document()->height, pixelLimit, _session.IsMaskSelected());
    const auto historyMemory = _session.history.EditMemoryBytes(_session.Document());
    if (memory == 0 || historyMemory == 0 ||
        historyMemory > (std::numeric_limits<std::uint64_t>::max)() - sizeof(RasterEditJob) ||
        memory > (std::numeric_limits<std::uint64_t>::max)() - historyMemory - sizeof(RasterEditJob))
        return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
    const auto ordinal = _activeHostRequest->ordinal;
    _rasterEditStamp = _historyWork.Begin(_session.DocumentSerial(), ordinal, HistoryWorkKind::Commit);
    if (!_rasterEditStamp && _hostFinishedStamp && _hostFinishedStamp->ordinal == ordinal)
        _rasterEditStamp =
            _historyWork.Continue(*_hostFinishedStamp, _session.DocumentSerial(), HistoryWorkKind::Commit);
    if (!_rasterEditStamp)
        return E_UNEXPECTED;
    HRESULT result = E_OUTOFMEMORY;
    try
    {
        if (_rasterEditTestGate && _rasterEditTestGate->throwAdmissionFailure)
            throw std::bad_alloc{};
        std::unique_ptr<Job> job =
            std::make_unique<RasterEditJob>(*this, *_rasterEditStamp, _session.Generation(), kind, fill, layer->id,
                                            _session.IsMaskSelected(), pixelLimit);
        JobTicket ticket;
        result = static_cast<HRESULT>(_jobs.Submit(
            job, {JobClass::Commit, _session.DocumentSerial(), memory + historyMemory + sizeof(RasterEditJob)},
            ticket));
        if (SUCCEEDED(result))
        {
            _rasterEditTicket = ticket;
            if (_rasterEditTestGate)
                _rasterEditTestGate->reservationBytes = memory + historyMemory + sizeof(RasterEditJob);
        }
    }
    catch (const std::bad_alloc&)
    {
        // Complete request admission refusal. No target capture or pixel work has occurred.
    }
    if (FAILED(result))
    {
        (void)_historyWork.Finish(*_rasterEditStamp);
        _hostFinishedStamp = _rasterEditStamp;
        _rasterEditStamp.reset();
        return result;
    }
    _hostCommitStamp = _rasterEditStamp;
    _hostAfterPrerequisite = HostStage::Complete;
    return S_OK;
}
