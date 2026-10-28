#include "Application.h"

#include "RecordedDocumentReadbackJob.h"
#include "Runtime.Tests.ColorWorkTestGate.h"
#include "SharedPublicationMemory.h"

#include <algorithm>
#include <exception>
#include <limits>
#include <memory>
#include <new>
#include <utility>

namespace
{
#pragma warning(push)
#pragma warning(disable : 4324)
// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626)
struct HueReadbackPayload final : DeferredReleaseNode
{
    // Retain the original complete reservation after all document/pixel owners and their weak controls.
    std::shared_ptr<SharedPublicationMemory> memory;
    CanvasDocument document;
    PixelBuffer pixels;
    Raster image;
    Raster completedImage;

    HueReadbackPayload() noexcept
    {
        bytes = kDeferredReleaseThreshold;
        destroy = [](DeferredReleaseNode* node) noexcept
        { const std::unique_ptr<HueReadbackPayload> owner(static_cast<HueReadbackPayload*>(node)); };
    }
};
#pragma warning(pop)

#pragma warning(pop)
struct RetireHueReadback final
{
    void operator()(HueReadbackPayload* payload) const noexcept
    {
        if (payload && !TryDeferRelease(*payload, DeferredReleasePolicy::AlwaysOnOwner))
            std::terminate();
    }
};
bool AddHueBytes(std::uint64_t& total, std::uint64_t extra) noexcept
{
    if (extra > (std::numeric_limits<std::uint64_t>::max)() - total)
        return false;
    total += extra;
    return true;
}
} // namespace

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626)
class Application::HueReadbackHostJob final : public RecordedDocumentReadbackJob
{
  public:
    HueReadbackHostJob(Application& owner, HistoryWorkStamp stamp, std::uint64_t opening, D3D11_BOX region,
                       UtilityReadbackAdmission::Envelope envelope, GpuResidencyReservation reservation,
                       std::shared_ptr<ColorWorkTestGate> gate) noexcept
        : RecordedDocumentReadbackJob(owner._jobs, owner._gpuWork, owner._recordedCommands, owner._utilityReadback,
                                      owner._graphGpuBudget, envelope, std::move(reservation), region,
                                      owner._renderer.IsWarp()),
          _owner(owner), _stamp(stamp), _opening(opening), _retirement(envelope.cpuBytes), _gate(std::move(gate))
    {
    }
    ~HueReadbackHostJob() override
    {
        if (_payload)
            TransferReadbackOwners(_payload->document, _payload->pixels, _payload->image, _payload->memory);
    }

  private:
    bool Current() const noexcept
    {
        return !_owner._shutdownStarted && _owner._hueReadbackStamp == _stamp &&
               _owner._historyWork.IsCurrent(_stamp) && _owner.HueInputCurrentOwner(_opening);
    }
    HRESULT CaptureReadbackDocument(CanvasDocument& document) noexcept override
    {
        if (!Current() || _owner._historyWork.CancelRequested(_stamp))
            return E_ABORT;
        if (_owner._hueCapturesForTest)
            ++*_owner._hueCapturesForTest;
        document = _owner._session.Document().value(); // Committed source, never the panel preview.
        return S_OK;
    }
    JobResult PrepareReadbackSnapshot(CanvasDocument&, JobContext&) override
    {
        if (!_payload)
        {
            std::unique_ptr<HueReadbackPayload, RetireHueReadback> payload(
                std::make_unique<HueReadbackPayload>().release());
            payload->bytes = (std::max)(_retirement, kDeferredReleaseThreshold);
            _payload = std::move(payload);
        }
        return {};
    }
    HRESULT PrepareReadbackDevice(HRESULT reason) noexcept override
    {
        return _owner.PrepareUtilityDevice(reason);
    }
    bool ProcessReadbackDetached() const noexcept override
    {
        return true;
    }
    JobResult ProcessReadback(Raster& image, JobContext& context) override
    {
        if (context.cancellation.IsCancelled())
            return {JobStatus::Cancelled};
        if (!image.HasStorage() || image.Width() != 1 || image.Height() != 1 ||
            image.Format() != RasterFormat::Bgra8888)
            return {JobStatus::Failed, E_UNEXPECTED};
        if (_gate)
        {
            _gate->captureReservationHeld.store(context.reservedMemoryBytes >= _retirement);
            _gate->reservationBytes = context.reservedMemoryBytes;
            const auto held = PassColorWorkTestGate(_gate, ColorWorkTestKind::Preview, context);
            if (held.status != JobStatus::Finished)
                return held;
        }
        _hue = DocumentSession::SampledHue(image, {0, 0}); // Unchanged legacy straight-color formula.
        return {};
    }
    void ResetReadback() noexcept override
    {
        _hue.reset();
    }
    void ReadbackCompleted(JobResult result, Raster image, std::uint64_t) noexcept override
    {
        if (_payload)
            _payload->completedImage = std::move(image);
        if (!Current())
            result = {JobStatus::Cancelled, E_ABORT};
        else if (_owner._historyWork.CancelRequested(_stamp))
            result = {JobStatus::Cancelled, E_ABORT};
        _owner.CompleteHueReadbackOwner(_opening, _stamp, result, _hue);
    }

    Application& _owner;
    const HistoryWorkStamp _stamp;
    const std::uint64_t _opening;
    const std::uint64_t _retirement;
    std::optional<double> _hue;
    const std::shared_ptr<ColorWorkTestGate> _gate;
    std::unique_ptr<HueReadbackPayload, RetireHueReadback> _payload;
};
#pragma warning(pop)

HRESULT Application::StartHueReadbackOwner(std::uint64_t opening) noexcept
{
    const auto* captured = _hueInput.CapturedContext(opening);
    if (_shutdownStarted || !_hueReadbackEnabled || !_activeHostRequest || !captured || _hueReadbackTicket ||
        !HueInputCurrentOwner(opening))
        return E_ABORT;
    const auto x = static_cast<unsigned int>(captured->documentPoint.x);
    const auto y = static_cast<unsigned int>(captured->documentPoint.y);
    const D3D11_BOX region{x, y, 0, x + 1, y + 1, 1};
    auto memory = RecordedDocumentReadbackJob::RequiredMemory(region);
    const auto metadata = sizeof(HueReadbackHostJob) + sizeof(HueReadbackPayload) + sizeof(HostHueInput) +
                          SharedPublicationMemory::OwnershipAllowance() + 8192ull;
    if (!memory || !AddHueBytes(memory, metadata))
        return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
    UtilityReadbackAdmission::Envelope envelope;
    GpuResidencyReservation reservation;
    RETURN_IF_FAILED(PrepareRecordedUtilityAdmission(_session.Document().value(), memory, envelope, reservation));
    _hueReadbackStamp =
        _historyWork.Begin(_session.DocumentSerial(), _activeHostRequest->ordinal, HistoryWorkKind::Selection);
    if (!_hueReadbackStamp)
        return E_UNEXPECTED;
    HRESULT error = E_OUTOFMEMORY;
    try
    {
        std::unique_ptr<Job> job = std::make_unique<HueReadbackHostJob>(
            *this, _hueReadbackStamp.value(), opening, region, envelope, std::move(reservation), _hueReadbackTestGate);
        JobTicket ticket;
        error = static_cast<HRESULT>(_jobs.Submit(job,
                                                  {.kind = JobClass::Selection,
                                                   .serialKey = _session.DocumentSerial(),
                                                   .memoryBytes = envelope.cpuBytes,
                                                   .deferredRetirement = true,
                                                   .allowMemoryWaiting = true,
                                                   .reserveRetainedMemory = true},
                                                  ticket));
        if (SUCCEEDED(error))
            _hueReadbackTicket = ticket;
    }
    catch (const std::bad_alloc&)
    {
        // Descriptor-only construction refusal; no source root, pixels or graphics resources captured.
    }
    if (FAILED(error))
    {
        (void)_historyWork.Finish(_hueReadbackStamp.value());
        _hueReadbackStamp.reset();
        return error;
    }
    _hostCommitStamp = _hueReadbackStamp;
    _hostAfterPrerequisite = HostStage::Complete;
    return S_OK;
}
void Application::CancelHueReadbackOwner() noexcept
{
    if (_hueReadbackTicket)
        (void)_jobs.Cancel(_hueReadbackTicket.value());
}
