#include "Application.h"

#include "ChromeHost.h"
#include "PreparedLayerRows.h"
#include "Resources.h"
#include "Runtime.Tests.LayerRowsTestGate.h"
#include "resource.h"

#include <exception>
#include <limits>
#include <memory>
#include <new>
#include <utility>

namespace
{
#pragma warning(push)
#pragma warning(disable : 4324)
struct RowsInput final : DeferredReleaseNode
{
    CanvasDocument source;
    std::vector<Guid> collapsed;
    std::wstring pattern;
    std::shared_ptr<PreparedLayerRows> rows;
    std::shared_ptr<const PreparedLayerRows> previousRows;
    std::shared_ptr<LayerRowsTestGate> gate;
    DWORD ownerThread = 0;
    RowsInput() noexcept
    {
        destroy = [](DeferredReleaseNode* node) noexcept
        {
            auto* input = static_cast<RowsInput*>(node);
            const auto witness = input->gate;
            const auto ownerThread = input->ownerThread;
            delete input;
            if (witness)
            {
                witness->retiredOffOwner.store(GetCurrentThreadId() != ownerThread);
                witness->retired.store(true, std::memory_order_release);
            }
        };
    }
};
#pragma warning(pop)
struct RetireRowsInput final
{
    void operator()(RowsInput* input) const noexcept
    {
        if (input && !TryDeferRelease(*input, DeferredReleasePolicy::AlwaysOnOwner))
            delete input;
    }
};
} // namespace

Application::LayerRowsKey Application::CurrentLayerRowsKey() const noexcept
{
    const auto& document = std::as_const(_session).Document();
    return {_session.DocumentSerial(), _session.Generation(), _session.CollapseRevision(),
            _session.CaptureDocumentReadAuthority(), document ? document->layers.Identity() : nullptr};
}

bool Application::LayerRowsSource::Start() noexcept
{
    if (_enabled || _attached || !_owner._ownerWork.Attach(*this))
        return false;
    _attached = true;
    _enabled = true;
    return true;
}

void Application::LayerRowsSource::StopNotifications() noexcept
{
    _enabled = false;
    _ready = false;
}

void Application::LayerRowsSource::CloseAfterWorkers() noexcept
{
    StopNotifications();
    if (_attached)
    {
        if (!_owner._ownerWork.Detach(*this))
            std::terminate();
        _attached = false;
    }
}

void Application::LayerRowsSource::Request() noexcept
{
    if (_enabled && !_owner._shutdownStarted && !_ready)
    {
        _ready = true;
        _owner._jobs.NotifyOwnerWork();
    }
}

bool Application::EnablePreparedLayerRows() noexcept
{
    if (_preparedLayerRowsEnabled || _shutdownStarted || !_chrome)
        return false;
    // Bounded localized input is loaded once before enabling, never once per row or settled sync.
    _layerRowsHiddenPattern = LoadStringResource(nullptr, IDS_LAYER_HIDDEN_SUFFIX);
    if (_layerRowsHiddenPattern.empty() || !_layerRowsSource.Start())
        return false;
    _preparedLayerRowsEnabled = true;
    SyncPreparedLayerRows();
    return true;
}

void Application::ClearPreparedLayerRows() noexcept
{
    _layerRowsRetryAvailable = false;
    if (_chrome && _preparedLayerRowsEnabled)
    {
        _chrome->SetPreparedLayers(nullptr, std::nullopt, false);
        _chrome->SetPreparedLayerRetry(false);
    }
    _preparedLayerRows.reset(); // The complete shared graph keeps its lease until final retirement.
    _layerRowsCacheTicket.reset();
    _layerRowsPublishedKey.reset();
}

void Application::SyncPreparedLayerRows() noexcept
{
    if (!_preparedLayerRowsEnabled || !_chrome || _shutdownStarted)
        return;
    const auto current = CurrentLayerRowsKey();
    if (_layerRowsRequestedKey && _layerRowsRequestedKey.value() != current)
        _layerRowsRetryAvailable = false;
    const bool same = _layerRowsPublishedKey && _layerRowsPublishedKey.value() == current;
    // Keep same-document labels visible during a refresh, but an obsolete hierarchy cannot accept a drag or
    // selection. Document replacement clears the borrowed model before any younger interaction can see it.
    const bool sameDocument =
        _layerRowsPublishedKey && _layerRowsPublishedKey->documentSerial == current.documentSerial;
    _chrome->SetPreparedLayers(sameDocument ? _preparedLayerRows : nullptr, _session.ActiveLayerID(),
                               _session.IsMaskSelected());
    _chrome->SetPreparedLayerInteraction(same);
    _chrome->SetPreparedLayerRetry(_layerRowsRetryAvailable && !same && !_layerRowsTicket);
    if (!same && (!_layerRowsRequestedKey || _layerRowsRequestedKey.value() != current))
    {
        _layerRowsRequestedKey = current;
        if (_layerRowsTicket)
            (void)_jobs.Cancel(_layerRowsTicket.value());
        else
            _layerRowsSource.Request();
    }
}

void Application::ReportLayerRowsFailure() noexcept
{
    if (_shutdownStarted || !_preparedLayerRowsEnabled)
        return;
    _layerRowsRetryAvailable = true;
    SyncPreparedLayerRows();
    _prompts->PostNotice(_window.get(), kNoticeMessage, IDS_ERROR_LAYER_ROWS);
    InvalidateFrame();
    _layoutDirty = true;
}

void Application::RetryPreparedLayerRows() noexcept
{
    if (_shutdownStarted || !_preparedLayerRowsEnabled || !_layerRowsRetryAvailable || _layerRowsTicket)
        return;
    _layerRowsRetryAvailable = false;
    SyncPreparedLayerRows();
    _layerRowsSource.Request(); // One new ticket for explicit input; unchanged-key memory events never rearm it.
    InvalidateFrame();
    _layoutDirty = true;
}

class Application::LayerRowsJob final : public Job
{
  public:
    LayerRowsJob(Application& owner, LayerRowsKey key, LayerRowsAdmission admission,
                 std::uint64_t ownershipAllowance) noexcept
        : _owner(owner), _key(key), _admission(admission), _ownershipAllowance(ownershipAllowance),
          _gate(owner._layerRowsTestGate), _ownerThread(GetCurrentThreadId())
    {
    }

    // Owner-only binding immediately after accepted Submit, including cancellation before the first Run.
    // Workers never read this field; completion hooks cannot execute before the owner returns to the pump.
    void BindOwnerTicket(JobTicket ticket, std::uint64_t firstItemId) noexcept
    {
        _ticket = ticket;
        _firstItemId = firstItemId;
    }

    JobResult Run(JobContext& context) override
    {
        if (context.cancellation.IsCancelled())
            return {JobStatus::Cancelled};
        if (!_input)
        {
            // The complete original reservation precedes this first worker. Construct its source/row
            // holder here; the successful acknowledgement only captures into an existing allocation.
            if (_gate && _gate->throwAllocate)
                throw std::bad_alloc{};
            auto input = std::make_unique<RowsInput>();
            input->bytes = _admission.Bytes() + sizeof(RowsInput);
            input->gate = _gate;
            input->ownerThread = _ownerThread;
            if (_gate)
            {
                _gate->allocatedOffOwner.store(GetCurrentThreadId() != _ownerThread);
                _gate->allocationReserved.store(context.reservedMemoryBytes >=
                                                (_admission.Bytes() + sizeof(RowsInput)));
                _gate->allocated.store(true);
            }
            _input.reset(input.release());
            return {};
        }
        if (!_captured)
            return {JobStatus::Failed, E_UNEXPECTED};
        if (_gate)
        {
            _gate->preparations.fetch_add(1);
            if (_gate->throwPrepare)
                throw std::bad_alloc{};
            if (!_gate->holdAfterPrepare)
            {
                const auto held = HoldForTest(context);
                if (held.status != JobStatus::Finished)
                    return held;
            }
        }
        auto rows = LayerRowsWork::Prepare(std::move(_input->source), std::move(_input->collapsed),
                                           std::move(_input->pattern), _admission, context.cancellation);
        _input->rows = PreparedLayerRows::Create(std::move(rows), _admission.Bytes() + _ownershipAllowance,
                                                 _key.documentSerial, _firstItemId, _input->previousRows.get());
        if (_gate && _gate->holdAfterPrepare)
        {
            const auto held = HoldForTest(context);
            if (held.status != JobStatus::Finished)
                return held;
        }
        return context.cancellation.IsCancelled() ? JobResult{JobStatus::Cancelled}
               : _input->rows                     ? JobResult{}
                                                  : JobResult{JobStatus::Failed, E_FAIL};
    }

    bool DeferCompletion(JobResult, JobTicket ticket) noexcept override
    {
        _ticket = ticket;
        if (_captured || !_input || !Current())
            return false;
        try
        {
            if (_gate)
            {
                _gate->captureReserved.store(_owner._jobs.ReservedMemoryBytes() >= _gate->reservationBytes);
                if (_gate->throwCapture)
                    throw std::bad_alloc{};
                _gate->captures.fetch_add(1);
            }
            _input->source = std::as_const(_owner._session).Document().value();
            // This small shared capture preserves its existing original charge. The worker copies only IDs,
            // then retires this reference with RowsInput; the new cache never owns a chain of old epochs.
            _input->previousRows = _owner._preparedLayerRows;
            // The collapse set is explicitly read-only and has at most 10,000 Guid values (160 KB). Its one
            // admitted capture is the only bounded owner traversal; no rows/names/pixels/history are copied.
            const auto& collapsed = std::as_const(_owner._session).CollapsedGroupIDs();
            _input->collapsed.reserve(collapsed.size());
            for (const auto& id : collapsed)
                _input->collapsed.push_back(id);
            _input->pattern = _owner._layerRowsHiddenPattern;
            _captured = true;
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

    void Completed(JobResult result) noexcept override
    {
        const bool ownsTicket = _owner._layerRowsTicket && _owner._layerRowsTicket.value() == _ticket;
        if (ownsTicket)
            _owner._layerRowsTicket.reset();
        if (result.status == JobStatus::Finished && SUCCEEDED(_captureError) && Current() && _input && _input->rows)
        {
            if (SUCCEEDED(_input->rows->BindMemory(_owner._jobs, _ticket)))
            {
                if (!_input->rows->SealForPublication())
                    std::terminate();
                auto previous = std::move(_owner._preparedLayerRows);
                _owner._preparedLayerRows = std::move(_input->rows);
                _owner._layerRowsCacheTicket = _ticket;
                _owner._layerRowsPublishedKey = _key;
                if (_gate)
                    _gate->publications.fetch_add(1);
                _input->bytes = sizeof(RowsInput) + 64 * 1024; // Source/rows moved to the separately charged cache.
                _owner.SyncPreparedLayerRows(); // Swap borrowing before any old rows lose their final owner.
                previous.reset();
                // Old snapshot readers keep the old original reservation until its complete graph retires.
                _owner.InvalidateFrame();
            }
        }
        if (ownsTicket && Current() &&
            (!_owner._layerRowsPublishedKey || _owner._layerRowsPublishedKey.value() != _key) &&
            result.status != JobStatus::Cancelled)
            _owner.ReportLayerRowsFailure();
        // A changed key schedules exactly one newest request after this old record acknowledges. Hard current
        // failures remain dormant until a new explicit key/event; no self-rearm or memory-pressure polling.
        if (ownsTicket && !_owner._shutdownStarted && _owner._preparedLayerRowsEnabled &&
            _owner._layerRowsRequestedKey && _owner._layerRowsRequestedKey.value() != _key)
            _owner._layerRowsSource.Request();
    }

  private:
    [[nodiscard]] JobResult HoldForTest(JobContext& context) noexcept
    {
        if (!_gate || !_gate->hold)
            return {};
        if (!PostThreadMessageW(_ownerThread, LayerRowsTestGate::kMessage, 0, 0))
            return {JobStatus::Failed, HRESULT_FROM_WIN32(GetLastError())};
        const HANDLE waits[]{_gate->release.get(), context.shutdownEvent};
        const auto waited = WaitForMultipleObjects(2, waits, FALSE, 15000);
        if (context.cancellation.IsCancelled() || waited == WAIT_OBJECT_0 + 1)
            return {JobStatus::Cancelled};
        return waited == WAIT_OBJECT_0 ? JobResult{} : JobResult{JobStatus::Failed, HRESULT_FROM_WIN32(ERROR_TIMEOUT)};
    }
    [[nodiscard]] bool Current() const noexcept
    {
        return !_owner._shutdownStarted && _owner._preparedLayerRowsEnabled && _key.collapseRevision != 0 &&
               _owner.CurrentLayerRowsKey() == _key;
    }
    Application& _owner;
    const LayerRowsKey _key;
    const LayerRowsAdmission _admission;
    const std::uint64_t _ownershipAllowance;
    std::unique_ptr<RowsInput, RetireRowsInput> _input;
    JobTicket _ticket;
    std::uint64_t _firstItemId = 0;
    bool _captured = false;
    HRESULT _captureError = S_OK;
    const std::shared_ptr<LayerRowsTestGate> _gate;
    const DWORD _ownerThread;
};

void Application::LayerRowsSource::Pump() noexcept
{
    if (!_enabled || !_ready || _owner._shutdownStarted)
        return;
    _ready = false;
    if (_owner._layerRowsTicket || !_owner._preparedLayerRowsEnabled)
        return;
    const auto key = _owner.CurrentLayerRowsKey();
    const auto& document = std::as_const(_owner._session).Document();
    if (!document)
    {
        _owner.ClearPreparedLayerRows();
        _owner._layerRowsPublishedKey = key;
        _owner._layerRowsRequestedKey = key;
        _owner.SyncPreparedLayerRows();
        return;
    }
    if (key.collapseRevision == 0)
    {
        _owner.ReportLayerRowsFailure();
        return;
    }
    const auto admission = LayerRowsAdmission::Estimate(
        document.value(), std::as_const(_owner._session).CollapsedGroupIDs().size(), _owner._layerRowsHiddenPattern);
    const auto layerCount = document->layers.size();
    const auto ownershipAllowance = PreparedLayerRows::OwnershipAllowance(layerCount);
    const auto overhead = sizeof(LayerRowsJob) + sizeof(RowsInput) + ownershipAllowance + 64 * 1024;
    if (!admission.IsValid() || ownershipAllowance == 0 ||
        _owner._nextLayerRowItemId < PreparedLayerRows::FirstItemIdentity ||
        layerCount > (std::numeric_limits<std::uint64_t>::max)() - _owner._nextLayerRowItemId ||
        admission.Bytes() > (std::numeric_limits<std::uint64_t>::max)() - overhead)
    {
        _owner.ReportLayerRowsFailure();
        return;
    }
    try
    {
        auto concrete = std::make_unique<LayerRowsJob>(_owner, key, admission, ownershipAllowance);
        auto* submitted = concrete.get();
        std::unique_ptr<Job> job = std::move(concrete);
        JobTicket ticket;
        const auto submittedResult = _owner._jobs.Submit(
            job, {JobClass::Background, key.documentSerial, admission.Bytes() + overhead, false, false}, ticket);
        if (SUCCEEDED(submittedResult))
        {
            submitted->BindOwnerTicket(ticket, _owner._nextLayerRowItemId);
            _owner._nextLayerRowItemId += layerCount; // Accepted ranges stay consumed after cancellation/failure.
            _owner._layerRowsTicket = ticket;
            _owner._layerRowsRequestedKey = key;
        }
        else
            _owner.ReportLayerRowsFailure();
    }
    catch (const std::bad_alloc&)
    {
        _owner.ReportLayerRowsFailure();
    }
}
