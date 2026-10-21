#include "Application.h"
#include "ChromeHost.h"

#include "Runtime.Tests.WandLayerHostTestGate.h"
#include "SharedPublicationMemory.h"
#include "WandLayerSelectionWork.h"
#include "resource.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <new>
#include <utility>

namespace
{
#pragma warning(push)
#pragma warning(disable : 4324)
struct WandLayerHostPayload final : DeferredReleaseNode
{
    // Fields precede every source/output graph: their actual/control destruction precedes the leases.
    std::shared_ptr<SharedPublicationMemory> temporary;
    std::shared_ptr<SharedPublicationMemory> output;
    std::optional<WandLayerSelectionInput> input;
    std::optional<PreparedWandLayerSelection> publication;
    PreparedSelectionOutline outline;
    std::uint64_t outlineBytes = 0;
    std::vector<CanvasOverlay::OutlineLoop> retiredReferenceOutline;
    WandFailure failure = WandFailure::None;
    std::shared_ptr<WandLayerHostTestGate> gate;
    DWORD ownerThread = 0;
    WandLayerHostPayload() noexcept
    {
        bytes = kDeferredReleaseThreshold;
        destroy = [](DeferredReleaseNode* node) noexcept
        { const std::unique_ptr<WandLayerHostPayload> owned(static_cast<WandLayerHostPayload*>(node)); };
    }
    ~WandLayerHostPayload()
    {
        if (gate)
        {
            gate->retiredOffOwner.store(GetCurrentThreadId() != ownerThread);
            gate->retired.store(true);
        }
    }
};
#pragma warning(pop)
struct RetireWandLayerHostPayload final
{
    void operator()(WandLayerHostPayload* payload) const noexcept
    {
        if (payload && !TryDeferRelease(*payload))
        {
            const std::unique_ptr<WandLayerHostPayload> owned(payload);
        }
    }
};
} // namespace

bool Application::PanelWandBusyOwner() const noexcept
{
    const auto preservesPanel = [](const HostRequestInput& input) noexcept
    {
        if (const auto* wand = std::get_if<HostWandLayerNewInput>(&input))
            return wand->panelOpening.has_value();
        if (const auto* click = std::get_if<HostSelectionMoveClickInput>(&input))
            return click->wand && click->wand->panelOpening.has_value();
        return false;
    };
    if (_activeHostRequest && preservesPanel(_activeHostRequest->input.Input()))
        return true;
    // The existing fixed-capacity input ring is the complete accepted intent owner. This bounded
    // query allocates nothing and stays true through original close/readback/publication retirement.
    for (std::size_t index = 0; index < _hostRequests.Size(); ++index)
    {
        const auto* entry = _hostRequests.PendingAt(index);
        if (entry && preservesPanel(entry->input.Input()))
            return true;
    }
    return false;
}

class Application::WandLayerHostJob final : public Job
{
  public:
    WandLayerHostJob(Application& owner, HistoryWorkStamp stamp, HostWandLayerNewInput request,
                     std::uint64_t retirement, std::uint64_t outlineLimit, LayerSelectionSource source) noexcept
        : _owner(owner), _stamp(stamp), _request(request), _retirement(retirement), _outlineLimit(outlineLimit),
          _source(source), _authority(owner._session.CaptureHistoryRestoreAuthority()),
          _panelAuthority(request.panelOpening ? owner._session.CapturePanelWandAuthority() : std::nullopt),
          _gate(owner._wandLayerHostTestGate), _outputGate(owner._wandOutputLifetimeTestGate),
          _ownerThread(GetCurrentThreadId()), _trace(owner._traceWandForTest)
    {
    }
    JobResult Run(JobContext& context) override
    {
        if (context.cancellation.IsCancelled())
            return {JobStatus::Cancelled};
        if (!_payload)
        {
            auto payload = std::make_unique<WandLayerHostPayload>();
            payload->temporary = SharedPublicationMemory::CreateForWork();
            payload->output = SharedPublicationMemory::CreateForWork();
            payload->bytes = (std::max)(_retirement, kDeferredReleaseThreshold);
            payload->gate = _gate;
            payload->ownerThread = _ownerThread;
            _payload.reset(payload.release());
            return {}; // Complete admission and worker allocation precede immutable owner capture.
        }
        if (!_payload->input)
            return {JobStatus::Failed, E_UNEXPECTED};
        DocumentHistory::PrepareObserver observer;
        if (_gate && _gate->throwHistoryStage)
            observer = {[](void* opaque, DocumentHistory::PrepareStage stage)
                        {
                            auto& gate = *static_cast<WandLayerHostTestGate*>(opaque);
                            if (gate.throwHistoryStage == stage)
                            {
                                gate.historyFaultHits.fetch_add(1);
                                throw std::bad_alloc{};
                            }
                        },
                        _gate.get()};
        _payload->publication = DocumentSession::PrepareWandLayerSelectionPublication(
            std::move(_payload->input.value()), &_payload->failure, context.cancellation, observer, nullptr,
            _payload->output);
        if (_trace)
            std::printf("WAND WORKER publication=%d failure=%u cancelled=%d\n", _payload->publication.has_value(),
                        static_cast<unsigned int>(_payload->failure), context.cancellation.IsCancelled());
        if (context.cancellation.IsCancelled() || _payload->failure == WandFailure::Cancelled)
            return {JobStatus::Cancelled};
        if (!_payload->publication)
            return {JobStatus::Failed, _payload->failure == WandFailure::TooDetailed
                                           ? HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW)
                                           : E_OUTOFMEMORY};
        if (_payload->publication->ChangesSelection())
        {
            const auto* selected = _payload->publication->SelectionForDisplayForWork();
            SelectionLoops source = selected ? selected->loops : SelectionLoops{};
            const auto declared = PreparedSelectionOutline::OutputMemory(source);
            if (!declared || declared.value() > _outlineLimit)
                return {JobStatus::Failed, E_OUTOFMEMORY}; // Refuse before allocating outside original admission.
            auto outline = PreparedSelectionOutline::Prepare(std::move(source), _payload->output, context.cancellation);
            if (!outline)
                return context.cancellation.IsCancelled() ? JobResult{JobStatus::Cancelled}
                                                          : JobResult{JobStatus::Failed, E_OUTOFMEMORY};
            if (_gate)
            {
                _gate->outlinePreparedOffOwner.store(GetCurrentThreadId() != _ownerThread);
                const auto& points = outline->Points();
                _gate->preparedPointBuffer.store(!points.empty() && !points[0].empty() ? points[0].data() : nullptr);
            }
            _payload->outlineBytes = outline->DeclaredOutputBytes();
            _payload->outline = std::move(outline.value());
        }
        if (_gate && _gate->hold)
        {
            _gate->visits.fetch_add(1);
            if (!PostThreadMessageW(_ownerThread, WandLayerHostTestGate::kMessage, 0, 0))
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
    bool DeferCompletion(JobResult result, JobTicket ticket) noexcept override
    {
        if (result.status != JobStatus::Finished || !_payload || _captureAttempted || !Current() ||
            _owner._historyWork.CancelRequested(_stamp))
            return false;
        try
        {
            _captureAttempted = true;
            if (_gate)
            {
                _gate->reservationHeld.store(_gate->reservationBytes != 0 &&
                                             _owner._jobs.ReservedMemoryBytes() >= _gate->reservationBytes);
                if (_gate->throwCaptureFailure)
                    throw std::bad_alloc{};
            }
            _payload->input =
                (_source != LayerSelectionSource::ColorMatch
                     ? _owner._session.PrepareLoadedLayerSelectionInput(_source == LayerSelectionSource::MaskDark)
                 : _request.panelOpening ? _owner._session.PreparePanelWandLayerSelectionInput(
                                               _request.point, _request.settings, _request.antialiased, _request.mode)
                                         : _owner._session.PrepareWandLayerSelectionInput(
                                               _request.point, _request.settings, _request.antialiased, _request.mode));
            if (_trace)
                std::printf("WAND CAPTURE input=%d point=%.3f,%.3f generation=%llu\n", _payload->input.has_value(),
                            _request.point.x, _request.point.y,
                            static_cast<unsigned long long>(_owner._session.Generation()));
            if (!_payload->input)
            {
                _captureError = E_ABORT;
                return false;
            }
            if (_gate)
                _gate->captures.fetch_add(1);
            if (_owner._jobs.Resume(ticket))
                return true;
            _captureError = E_UNEXPECTED;
        }
        catch (const std::bad_alloc&)
        {
            _captureError = E_OUTOFMEMORY;
        }
        return false;
    }
    bool DeferRetirement(JobResult, JobTicket original) noexcept override
    {
        _ticket = original;
        return false;
    }
    void RetainJoinedRetirement(JobScheduler&, JobTicket original) noexcept override
    {
        _ticket = original;
        BindRetirement(original);
    }
    void Completed(JobResult result) noexcept override
    {
        // Original terminal authority binds every retiring allocation before publishing any document.
        BindRetirement(_ticket);
        HostOutcome outcome = result.status == JobStatus::Cancelled ? HostOutcome::Cancelled
                              : result.status == JobStatus::Failed  ? HostOutcome::Failed
                                                                    : HostOutcome::Succeeded;
        std::int32_t error = result.error;
        const bool current = Current();
        if (!current)
        {
            outcome = HostOutcome::Stale;
            error = E_ABORT;
        }
        else if (_owner._historyWork.CancelRequested(_stamp))
        {
            outcome = HostOutcome::Cancelled;
            error = E_ABORT;
        }
        else if (_captureError && outcome == HostOutcome::Succeeded)
        {
            outcome = HostOutcome::Failed;
            error = _captureError;
        }
        else if (outcome == HostOutcome::Succeeded &&
                 (!_payload || !_payload->publication ||
                  !_owner._session.ApplyPreparedWandLayerSelectionIfCurrent(_payload->publication.value())))
        {
            outcome = HostOutcome::Stale;
            error = E_ABORT;
        }
        if (outcome == HostOutcome::Succeeded && _payload && _payload->outline.HasStorage())
            (void)_owner.AdoptPreparedSelectionOutlineOwner(std::move(_payload->outline),
                                                            _payload->retiredReferenceOutline);
        if (_trace)
            std::printf("WAND COMPLETE status=%u current=%d outcome=%u error=%08X capture_error=%08X\n",
                        static_cast<unsigned int>(result.status), current, static_cast<unsigned int>(outcome),
                        static_cast<std::uint32_t>(error), static_cast<std::uint32_t>(_captureError));
        if (_outputGate && outcome == HostOutcome::Succeeded && _payload && _payload->publication &&
            _payload->publication->OutputAllocationBytes() != 0)
        {
            const auto bytes = _payload->output->Bytes();
            _outputGate->allOutputsBoundToOriginal =
                _outputGate->allOutputsBoundToOriginal && _payload->output->BoundTo(_owner._jobs, _ticket) &&
                bytes == _payload->publication->OutputAllocationBytes() + _payload->outlineBytes;
            _outputGate->publishedOutputBytes += bytes;
            ++_outputGate->publishedOutputs;
        }
        if (outcome == HostOutcome::Failed)
        {
            const auto message = _payload && _payload->failure == WandFailure::TooDetailed
                                     ? (_source == LayerSelectionSource::ColorMatch ? IDS_ERROR_WAND_DETAILED
                                                                                    : IDS_ERROR_SELECTION_DETAILED)
                                     : IDS_ERROR_WAND_MEMORY;
            if (_source != LayerSelectionSource::ColorMatch)
            {
                ++_owner._errorsShown;
                _owner._lastErrorId = message;
            }
            _owner._prompts->PostNotice(_owner._window.get(), kNoticeMessage, message);
        }
        _owner._wandLayerHostTicket.reset();
        _owner._wandLayerHostStamp.reset();
        (void)_owner._historyWork.Finish(_stamp);
        _owner.SelectionChanged();
        _owner.ResumeHostHistory(_stamp, outcome, error);
    }

  private:
    void BindRetirement(JobTicket original) noexcept
    {
        if (!_payload || _payload->temporary->IsBound())
            return;
        const auto output =
            _payload->publication ? _payload->publication->OutputAllocationBytes() + _payload->outlineBytes : 0;
        const auto result = output ? _payload->output->BindPersistentOutputForRetirement(_owner._jobs, original, output,
                                                                                         *_payload->temporary)
                                   : _payload->temporary->BindForRetirement(_owner._jobs, original);
        if (result < 0)
            std::terminate(); // Failure here would refund allocations still readable by a live graph.
    }
    [[nodiscard]] bool Current() const noexcept
    {
        return !_owner._shutdownStarted && _owner._wandLayerHostStamp == _stamp &&
               _owner._historyWork.IsCurrent(_stamp) &&
               (_request.panelOpening
                    ? _panelAuthority && _owner._session.IsPanelWandOpeningCurrent(_request.panelOpening.value()) &&
                          _owner._session.IsPanelWandAuthorityCurrent(_panelAuthority.value())
                    : _owner._session.CanApplyHistoryRestore(_authority));
    }
    Application& _owner;
    const HistoryWorkStamp _stamp;
    const HostWandLayerNewInput _request;
    const std::uint64_t _retirement;
    const std::uint64_t _outlineLimit;
    const LayerSelectionSource _source;
    const HistoryRestoreAuthority _authority;
    const std::optional<PanelWandAuthority> _panelAuthority;
    const std::shared_ptr<WandLayerHostTestGate> _gate;
    const std::shared_ptr<WandLayerHostTestGate> _outputGate;
    const DWORD _ownerThread;
    const bool _trace;
    std::unique_ptr<WandLayerHostPayload, RetireWandLayerHostPayload> _payload;
    JobTicket _ticket;
    std::int32_t _captureError = 0;
    bool _captureAttempted = false;
};

HRESULT Application::StartLoadedLayerSelection(bool mask) noexcept
{
    const auto* target = _session.ActiveLayer();
    if (!target || (mask ? !target->mask || !target->mask->asset.image.HasStorage()
                         : target->isGroup || !target->asset || !target->asset->image.HasStorage()))
        return S_OK; // Missing/blank sources preserve the exact reference no-op.
    return StartLayerSelection(HostWandLayerNewInput{{}, {}, _session.selectionAntialiased},
                               mask ? LayerSelectionSource::MaskDark : LayerSelectionSource::LayerAlpha);
}

HRESULT Application::StartWandLayerSelection(const HostWandLayerNewInput& input) noexcept
{
    return StartLayerSelection(input, LayerSelectionSource::ColorMatch);
}

HRESULT Application::StartLayerSelection(const HostWandLayerNewInput& input, LayerSelectionSource source) noexcept
{
    if (_wandLayerHostTicket ||
        !(input.panelOpening ? _session.IsPanelWandOpeningCurrent(input.panelOpening.value()) &&
                                   _session.CapturePanelWandAuthority().has_value()
                             : _session.CanApplyHistoryRestore(_session.CaptureHistoryRestoreAuthority())))
        return HRESULT_FROM_WIN32(ERROR_INVALID_STATE);
    const auto& document = _session.Document();
    if (!document || !std::isfinite(input.point.x) || !std::isfinite(input.point.y))
        return S_OK;
    if (input.settings.sampleAllLayers || input.settings.sampleSize > WandSampleSize::FiveByFive)
        return E_INVALIDARG;
    if (input.point.x < 0 || input.point.y < 0 || input.point.x >= document->width || input.point.y >= document->height)
        return S_OK; // The direct reference ignores outside clicks; a valid in-canvas no-match deselects.
    const auto plan =
        (source != LayerSelectionSource::ColorMatch
             ? _session.PlanLoadedLayerSelectionMemory(source == LayerSelectionSource::MaskDark)
         : input.panelOpening ? _session.PlanPanelWandLayerSelectionMemory(input.point, input.settings, input.mode)
                              : _session.PlanWandLayerSelectionMemory(input.point, input.settings, input.mode));
    if (!plan)
        return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
    const std::uint64_t metadata =
        sizeof(WandLayerHostJob) + sizeof(WandLayerHostPayload) + 2 * SharedPublicationMemory::OwnershipAllowance();
    const auto maximum = (std::numeric_limits<std::uint64_t>::max)();
    if (metadata > maximum - plan->reservationBytes || metadata > maximum - plan->retirementBytes)
        return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
    std::uint64_t declared = 0;
    if (EvaluateWandLayerSelectionAdmission(*plan, input.mode, document->selection.has_value(), declared) !=
        WandLayerSelectionAdmission::Ready)
        return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
    const auto outline = PreparedSelectionOutline::AdmissionMemory(plan.value());
    const auto legacy = _chrome ? _chrome->LegacySelectionOutlineMemoryOwner() : std::optional<std::uint64_t>{0};
    if (!outline || !legacy || outline.value() > maximum - legacy.value())
        return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
    const auto additional = outline.value() + legacy.value();
    if (additional > maximum - (declared + metadata) || additional > maximum - (plan->retirementBytes + metadata))
        return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
    const auto memory = declared + metadata + additional;
    const auto retirement = plan->retirementBytes + metadata + additional;
    const auto ordinal = _activeHostRequest ? std::optional(_activeHostRequest->ordinal) : _historyWork.NextOrdinal();
    if (!ordinal)
        return HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA);
    _wandLayerHostStamp = _historyWork.Begin(_session.DocumentSerial(), ordinal.value(), HistoryWorkKind::Selection);
    if (!_wandLayerHostStamp && _hostFinishedStamp && _hostFinishedStamp->ordinal == ordinal.value())
        _wandLayerHostStamp =
            _historyWork.Continue(_hostFinishedStamp.value(), _session.DocumentSerial(), HistoryWorkKind::Selection);
    if (!_wandLayerHostStamp)
        return E_UNEXPECTED;
    HRESULT result;
    try
    {
        if (_wandLayerHostTestGate)
            _wandLayerHostTestGate->reservationBytes = memory;
        std::unique_ptr<Job> job = std::make_unique<WandLayerHostJob>(*this, _wandLayerHostStamp.value(), input,
                                                                      retirement, outline.value(), source);
        JobTicket ticket;
        result = static_cast<HRESULT>(_jobs.Submit(job,
                                                   {.kind = JobClass::Selection,
                                                    .serialKey = _session.DocumentSerial(),
                                                    .memoryBytes = memory,
                                                    .deferredRetirement = true,
                                                    .reserveRetainedMemory = true},
                                                   ticket));
        if (SUCCEEDED(result))
            _wandLayerHostTicket = ticket;
    }
    catch (const std::bad_alloc&)
    {
        result = E_OUTOFMEMORY;
    }
    if (FAILED(result))
    {
        (void)_historyWork.Finish(_wandLayerHostStamp.value());
        _hostFinishedStamp = _wandLayerHostStamp;
        _wandLayerHostStamp.reset();
        return result;
    }
    if (_activeHostRequest)
    {
        _hostCommitStamp = _wandLayerHostStamp;
        _hostAfterPrerequisite = HostStage::Complete;
    }
    return S_OK;
}
