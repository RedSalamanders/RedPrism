// Opt-in committed display route. Normal startup remains on the existing path until pending edits,
// strokes, utilities, rows and complete resource admission have also been migrated and qualified.
#include "Application.h"
#include "PreparedRasterPreview.h"

HRESULT Application::InitializeRecordedCommandsOwner() noexcept
{
    if (_shutdownStarted || _jobs.InUiCallback())
        return E_UNEXPECTED;
    if (_recordedCommandsInitialized)
        return S_OK;
    RETURN_IF_FAILED(static_cast<HRESULT>(_jobs.EnableGpuRecordingLane()));
    RETURN_IF_FAILED(_recordedCommands.InitializeOwner(_jobs, _gpuWork, _ownerWork));
    _recordedCommandsInitialized = true;
    return S_OK;
}

bool Application::EnableCommittedDisplayForTest(bool allowHidden) noexcept
{
    return InitializePreparedDisplayOwner(allowHidden);
}

bool Application::InitializePreparedDisplayOwner(bool allowHidden) noexcept
{
    if (_shutdownStarted || _preparedDisplayAcquisitionStarted || _committedDisplayInitialized || _jobs.InUiCallback())
        return false;
    if (FAILED(InitializeRecordedCommandsOwner()))
        return false;
    // A partly acquired controller is closed on failure. Its one-shot state cannot be reinitialized;
    // the caller joins Application shutdown. The independent shared command route remains live.
    _preparedDisplayAcquisitionStarted = true;
    if (FAILED(_committedDisplay.InitializeOwner(_jobs, _gpuWork, _recordedCommands, _ownerWork, *this,
                                                 &_graphGpuBudget, false, false)))
    {
        return false;
    }
    if (FAILED(_pendingDisplay.InitializeOwner(_jobs, _gpuWork, _recordedCommands, _ownerWork, _pendingDisplayClient,
                                               &_graphGpuBudget, true, false)))
    {
        _committedDisplay.StopOwner();
        _committedDisplay.JoinedShutdownOwner(); // No graph was requested or captured.
        if (!_committedDisplay.CloseAfterWorkersOwner())
            std::terminate();
        return false;
    }
    _pendingDisplayInitialized = true;
    _committedDisplayInitialized = true;
    _committedDisplayAllowHiddenForTest = allowHidden;
    ReleaseSurfacesWhileUnseen();
    if (_rendererReady)
        BindCompositionDevice();
    _committedDisplay.RequestOwner();
    return true;
}

bool Application::DescribeCommittedDisplayOwner(CommittedDisplayDescriptor& descriptor) noexcept
{
    descriptor = {};
    if (_shutdownStarted || !_committedDisplayInitialized || !_rendererReady || !_gpuWorkInitialized ||
        FAILED(_gpuBindingError))
        return false;
    const auto authority = _session.CaptureDocumentReadAuthority();
    const auto& document = std::as_const(_session).Document();
    if (!authority || !document || !_renderer.Device() || !_renderer.Context())
        return false;
    descriptor.key = {authority.value(), _gpuWork.DeviceGeneration()};
    descriptor.document = &document.value();
    descriptor.device = _renderer.Device();
    descriptor.immediate = _renderer.Context();
    descriptor.software = _renderer.IsWarp();
    return true;
}

void Application::CommittedDisplayPublishedOwner(RecordedDisplayGraph* graph) noexcept
{
    _acknowledgedCommittedDisplay = graph;
    _committedDisplayFailure = S_OK;
    InvalidateFrame();
}

void Application::CommittedDisplayFailedOwner(CommittedDisplayKey, HRESULT failure) noexcept
{
    _committedDisplayFailure = failure;
    InvalidateFrame(); // Existing canvas-error notice runs outside scheduler/owner-pump callbacks.
}

bool Application::PendingDisplayClient::DescribeCommittedDisplayOwner(CommittedDisplayDescriptor& descriptor) noexcept
{
    descriptor = {};
    if (_owner._shutdownStarted || !_owner._pendingDisplayInitialized || !_owner._rendererReady ||
        !_owner._gpuWorkInitialized || FAILED(_owner._gpuBindingError))
        return false;
    PendingDisplaySource source;
    if (!_owner._session.DescribePendingDisplaySource(source))
        return false;
    // Accepted Cancel excludes useful preview immediately. Its original panel/source still belongs to the
    // selected closure until acknowledgement; admission refusal and another opening must not hide it.
    if (source.kind != PendingDisplaySource::Kind::Transform &&
        _owner._pendingCancelOpening == std::optional(source.opening) &&
        _owner._pendingCancelDocumentSerial == _owner._session.DocumentSerial() &&
        _owner._pendingCancelReceipt.Read().outcome == HostOutcome::Pending)
        return false;
    descriptor.key.document = source.authority;
    descriptor.key.deviceGeneration = _owner._gpuWork.DeviceGeneration();
    descriptor.key.purpose = source.kind == PendingDisplaySource::Kind::Transform   ? DisplaySourcePurpose::Transform
                             : source.kind == PendingDisplaySource::Kind::Color     ? DisplaySourcePurpose::Color
                             : source.kind == PendingDisplaySource::Kind::Gradient  ? DisplaySourcePurpose::Gradient
                             : source.kind == PendingDisplaySource::Kind::PixelMove ? DisplaySourcePurpose::PixelMove
                                                                                    : DisplaySourcePurpose::Adjustment;
    descriptor.key.opening = source.opening;
    descriptor.key.publication = source.publication;
    descriptor.key.sourceIdentity = source.sourceIdentity;
    descriptor.key.lookupIdentity = source.lookup && *source.lookup ? source.lookup->get() : nullptr;
    descriptor.key.rasterPreviewIdentity =
        source.rasterPreview && *source.rasterPreview ? source.rasterPreview->get() : nullptr;
    descriptor.document = source.document;
    descriptor.device = _owner._renderer.Device();
    descriptor.immediate = _owner._renderer.Context();
    descriptor.software = _owner._renderer.IsWarp();
    descriptor.pending = source;
    return true;
}

void Application::PendingDisplayClient::CommittedDisplayPublishedOwner(RecordedDisplayGraph*) noexcept
{
    _owner._pendingDisplayFailure = S_OK;
    _owner._pendingDisplayFailureKey.reset();
    _owner.InvalidateFrame();
}

void Application::PendingDisplayClient::CommittedDisplayFailedOwner(CommittedDisplayKey key, HRESULT failure) noexcept
{
    _owner._pendingDisplayFailure = failure;
    _owner._pendingDisplayFailureKey = key;
    _owner.InvalidateFrame();
}
