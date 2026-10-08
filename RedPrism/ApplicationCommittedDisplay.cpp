// Opt-in committed display route. Normal startup remains on the existing path until pending edits,
// strokes, utilities, rows and complete resource admission have also been migrated and qualified.
#include "Application.h"

bool Application::EnableCommittedDisplayForTest(bool allowHidden) noexcept
{
    if (_shutdownStarted || _committedDisplayInitialized || _jobs.InUiCallback())
        return false;
    if (FAILED(_jobs.EnableGpuRecordingLane()))
        return false;
    if (FAILED(_recordedCommands.InitializeOwner(_jobs, _gpuWork, _ownerWork)))
        return false;
    if (FAILED(
            _committedDisplay.InitializeOwner(_jobs, _gpuWork, _recordedCommands, _ownerWork, *this, &_graphGpuBudget)))
    {
        _recordedCommands.StopOwner();
        _recordedCommands.AcknowledgeJoinedShutdownOwner(); // No source/producer was ever attached.
        if (!_recordedCommands.CloseOwner())
            std::terminate();
        return false;
    }
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
    _frameInvalidated = true;
}

void Application::CommittedDisplayFailedOwner(CommittedDisplayKey, HRESULT failure) noexcept
{
    _committedDisplayFailure = failure;
    _frameInvalidated = true; // Existing canvas-error notice runs outside scheduler/owner-pump callbacks.
}
