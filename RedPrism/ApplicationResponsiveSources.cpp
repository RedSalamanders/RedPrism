#include "Application.h"

#include "ApplicationProcessingTestProbe.h"

// One cold-start owner path for the already qualified prepared display, rows and stroke engines.
// Run currently reaches this only through the explicit startup fixture. Ordinary activation follows
// complete consumer/admission/lifetime and cost qualification; diagnostic direct/prepared routes stay explicit.
bool Application::InitializeResponsiveSourcesOwner(bool allowHidden) noexcept
{
    if (_shutdownStarted || _jobs.InUiCallback() || !_rendererReady || !_gpuWorkInitialized || !_chrome ||
        !_brushEngine.Ready() || _preparedLayerRowsEnabled || _committedDisplayInitialized ||
        _pendingDisplayInitialized || _asyncStrokeFinishEnabled || _asyncStrokeSamplesEnabled || _asyncWarpLiveEnabled)
        return false;
    const auto reached = [this](ApplicationResponsiveSourceStage stage) noexcept
    {
        if (_shutdownStarted)
            return false;
        const auto result = !_processingTestProbe || !_processingTestProbe->afterResponsiveSourceStage
                                ? S_OK
                                : static_cast<HRESULT>(_processingTestProbe->afterResponsiveSourceStage(
                                      _processingTestProbe->context, stage));
        return SUCCEEDED(result) && !_shutdownStarted;
    };
    if (!reached(ApplicationResponsiveSourceStage::BeforeAcquisition))
        return false;
    if (!EnablePreparedLayerRows() || !reached(ApplicationResponsiveSourceStage::LayerRows))
        return false;
    if (!InitializePreparedDisplayOwner(allowHidden) || !reached(ApplicationResponsiveSourceStage::Display))
        return false;
    if (!_strokeFinishSource.Start() || !reached(ApplicationResponsiveSourceStage::Stroke))
        return false;
    if (!reached(ApplicationResponsiveSourceStage::ReadyToEnable))
        return false;
    // The input loop and startup-path dispatch follow successful startup. Any acquired-prefix failure uses joined
    // Application::Shutdown; no partial engine activation or synchronous pixel fallback is admitted.
    _asyncStrokeFinishEnabled = true;
    _asyncStrokeSamplesEnabled = true;
    _asyncWarpLiveEnabled = true;
    return true;
}
