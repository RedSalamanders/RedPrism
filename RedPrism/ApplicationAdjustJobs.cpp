#include "Application.h"

#include "Adjust.h"
#include "ChromeHost.h"
#include "Runtime.Tests.ColorWorkTestGate.h"
#include "resource.h"

#include <array>
#include <atomic>
#include <cstdlib>
#include <memory>
#include <new>
#include <utility>

JobResult PassColorWorkTestGate(const std::shared_ptr<ColorWorkTestGate>& gate, ColorWorkTestKind kind,
                                JobContext& context) noexcept
{
    if (!gate || gate->kind != kind)
    {
        return {};
    }
    gate->visits.fetch_add(1);
    (void)SetEvent(gate->entered.get());
    const HANDLE handles[]{gate->release.get(), context.shutdownEvent};
    const DWORD waited = WaitForMultipleObjects(2, handles, FALSE, 10000);
    if (context.cancellation.IsCancelled() || waited == WAIT_OBJECT_0 + 1)
    {
        return {JobStatus::Cancelled};
    }
    return waited == WAIT_OBJECT_0 ? gate->releasedResult
                                   : JobResult{JobStatus::Failed, HRESULT_FROM_WIN32(ERROR_TIMEOUT)};
}

class Application::ColorPreviewJob final : public Job
{
  public:
    ColorPreviewJob(Application& owner, PreviewMailbox<ColorEditJobInput>::Launch launch)
        : _owner(owner), _stamp(launch.stamp), _input(std::move(launch.input)), _result{_input.token, _input.edit},
          _gate(owner._colorWorkTestGate)
    {
    }
    JobResult Run(JobContext& context) override
    {
        if (const auto gate = PassColorWorkTestGate(_gate, ColorWorkTestKind::Preview, context);
            gate.status != JobStatus::Finished)
        {
            return gate;
        }
        _result = RunColorEditPreviewJob(_input, context.executor, context.cancellation);
        if (_gate && _gate->kind == ColorWorkTestKind::Preview)
        {
            (void)SetEvent(_gate->computed.get());
        }
        return {};
    }
    void Completed(JobResult completion) noexcept override
    {
        _owner.CompleteColorPreview(_stamp, _result, completion);
    }

  private:
    Application& _owner; // UI completion only. Run reads immutable values, never the Application.
    PreviewStamp _stamp;
    const ColorEditJobInput _input;
    ColorEditPreviewResult _result;
    const std::shared_ptr<ColorWorkTestGate> _gate;
};

class Application::ColorHistogramJob final : public Job
{
  public:
    ColorHistogramJob(Application& owner, ColorEditJobInput input)
        : _owner(owner), _input(std::move(input)),
          _result{_input.token, _input.edit.openingSequence, _input.edit.original}, _gate(owner._colorWorkTestGate)
    {
    }
    JobResult Run(JobContext& context) override
    {
        if (const auto gate = PassColorWorkTestGate(_gate, ColorWorkTestKind::Histogram, context);
            gate.status != JobStatus::Finished)
        {
            return gate;
        }
        _result = RunColorEditHistogramJob(_input, context.executor, context.cancellation);
        if (_gate && _gate->kind == ColorWorkTestKind::Histogram)
        {
            (void)SetEvent(_gate->computed.get());
        }
        return {};
    }
    void Completed(JobResult completion) noexcept override
    {
        _owner.CompleteColorHistogram(_result, completion);
    }

  private:
    Application& _owner;
    const ColorEditJobInput _input;
    ColorEditHistogramResult _result;
    const std::shared_ptr<ColorWorkTestGate> _gate;
};

class Application::ColorFailureAction final : public UiAction
{
  public:
    explicit ColorFailureAction(Application& owner) noexcept
        : _owner(owner), _serial(owner._colorDocumentSerial), _opening(owner._colorOpening)
    {
    }
    void Run() noexcept override
    {
        const auto& edit = _owner._session.PendingColorEdit();
        if (!_owner._shutdownStarted && _owner._session.DocumentSerial() == _serial && edit &&
            edit->openingSequence == _opening)
        {
            _owner.ShowError(IDS_ERROR_FILTER);
        }
    }

  private:
    Application& _owner;
    std::uint64_t _serial;
    std::uint64_t _opening;
};

void Application::QueueColorFailure() noexcept
{
    std::unique_ptr<UiAction> action{new (std::nothrow) ColorFailureAction(*this)};
    if (!action || !_jobs.PostUiAction(action))
    {
        // The bounded action queue may be full. The existing posted notice is also delivered outside the
        // completion callback; it does not open a modal or change focus from this call stack.
        _prompts->PostNotice(_window.get(), kNoticeMessage, IDS_ERROR_FILTER);
    }
}

void Application::ConfigureColorLookupPreview(ColorEditJobInput& input) const
{
    input.lookupPreview = FilterIsImageAdjustment(input.edit.kind);
    if (const auto id = _session.AdjustmentEditingID(); id)
    {
        const ImageLayer* layer = _session.ActiveLayer();
        if (layer && layer->id == *id && layer->adjustment)
        {
            input.liveAdjustment = layer->adjustment;
        }
    }
}

void Application::SyncColorLookupPreview() noexcept
{
    const auto& edit = _session.PendingColorEdit();
    if (edit && _session.AdjustmentEditingID() && _session.AdjustmentOriginal())
    {
        _composition.SetAdjustmentPreview(CanvasRenderer::AdjustmentPreview{
            edit->layerID, {}, {}, edit->preview ? edit->previewLookup : nullptr, _session.AdjustmentOriginal(), 1.0});
    }
    else if (edit && edit->preview && edit->previewLookup && !edit->previewLookupOnly)
    {
        _composition.SetAdjustmentPreview(CanvasRenderer::AdjustmentPreview{
            edit->layerID, edit->previewSource, edit->previewCoverage, edit->previewLookup, {}, edit->previewScale});
    }
    else
    {
        _composition.SetAdjustmentPreview(std::nullopt);
    }
}

std::int32_t Application::SubmitColorPreview(PreviewMailbox<ColorEditJobInput>::Launch launch) noexcept
{
    try
    {
        // Pending requests own settings captured during the active render. Pick up any compatible prepared
        // source that render just published, without changing the pending request's settings sequence.
        if (auto current = _session.PrepareColorEditPreview();
            current && current->token.sequence == launch.input.token.sequence &&
            current->edit.openingSequence == launch.input.edit.openingSequence &&
            current->token.documentSerial == launch.input.token.documentSerial)
        {
            ConfigureColorLookupPreview(*current);
            launch.input = std::move(*current);
        }
        const auto memory = ColorEditPreviewMemoryBytes(launch.input);
        if (!memory)
        {
            return E_INVALIDARG;
        }
        const auto serial = launch.input.token.documentSerial;
        std::unique_ptr<Job> job = std::make_unique<ColorPreviewJob>(*this, std::move(launch));
        JobTicket ticket;
        const auto status = _jobs.Submit(job, {JobClass::InteractivePreview, serial, *memory}, ticket);
        if (status >= 0)
        {
            _colorPreviewTicket = ticket;
        }
        return status;
    }
    catch (const std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }
}

std::int32_t Application::SubmitColorHistogram() noexcept
{
    try
    {
        auto input = _session.PrepareColorEditPreview();
        if (!input)
        {
            return E_INVALIDARG;
        }
        const auto memory = ColorEditHistogramMemoryBytes(*input);
        if (!memory)
        {
            return E_INVALIDARG;
        }
        const auto serial = input->token.documentSerial;
        std::unique_ptr<Job> job = std::make_unique<ColorHistogramJob>(*this, std::move(*input));
        JobTicket ticket;
        const auto status = _jobs.Submit(job, {JobClass::Background, serial, *memory}, ticket);
        if (status >= 0)
        {
            _colorHistogramTicket = ticket;
        }
        return status;
    }
    catch (const std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }
}

std::int32_t Application::OfferColorPreview() noexcept
{
    try
    {
        auto input = _session.PrepareColorEditPreview();
        if (input)
        {
            ConfigureColorLookupPreview(*input);
        }
        return input && _colorPreviews.Offer(*input).accepted ? S_OK : E_INVALIDARG;
    }
    catch (const std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }
}

void Application::LaunchColorPreview() noexcept
{
    if (auto launch = _colorPreviews.TakeNext(); launch)
    {
        const auto stamp = launch->stamp;
        if (SubmitColorPreview(std::move(*launch)) < 0)
        {
            (void)_colorPreviews.Finish(stamp, false);
            QueueColorFailure();
        }
    }
}

void Application::CancelColorJobs() noexcept
{
    CancelColorCommit();
    (void)_colorPreviews.Close();
    _colorMailboxOpen = false;
    if (_colorPreviewTicket)
    {
        (void)_jobs.Cancel(*_colorPreviewTicket);
    }
    if (_colorHistogramTicket)
    {
        (void)_jobs.Cancel(*_colorHistogramTicket);
    }
    _colorPreviewTicket.reset();
    _colorHistogramTicket.reset();
    _colorOpening = 0;
    _colorSeenSequence = 0;
    _colorHistogramAttempted = false;
}

void Application::RefreshColorJobs() noexcept
{
    // An admitted commit owns a fixed settings snapshot until its apply/drop. Preview preparation must not
    // relaunch beneath it. A document replacement requests cancellation, but cannot release its history barrier.
    if (ColorCommitBusy())
    {
        if (_colorCommitStamp->documentSerial != _session.DocumentSerial())
        {
            CancelColorCommit();
        }
        return;
    }
    const auto& edit = _session.PendingColorEdit();
    if (_shutdownStarted || !edit || !edit->deferred)
    {
        if (_colorOpening != 0)
        {
            CancelColorJobs();
        }
        return;
    }
    if (_colorDocumentSerial != _session.DocumentSerial() || _colorOpening != edit->openingSequence)
    {
        CancelColorJobs();
        _colorDocumentSerial = _session.DocumentSerial();
        _colorOpening = edit->openingSequence;
    }
    if (!_colorHistogramAttempted && edit->kind == FilterKind::Levels && !edit->histogramReady)
    {
        _colorHistogramAttempted = true;
        if (SubmitColorHistogram() < 0)
        {
            QueueColorFailure();
        }
    }
    const auto sequence = _session.ColorEditSequence();
    if (sequence == _colorSeenSequence)
    {
        return;
    }
    _colorSeenSequence = sequence;
    const bool pixels = _session.AdjustmentEditingID().has_value() ||
                        (edit->preview && !ColorEditChangesNothing(edit->kind, edit->settings) &&
                         Adjust::BlurMargin(edit->kind, edit->settings) <= edit->grownMargin);
    if (!pixels)
    {
        (void)_colorPreviews.Close();
        _colorMailboxOpen = false;
        if (_colorPreviewTicket)
        {
            (void)_jobs.Cancel(*_colorPreviewTicket);
        }
        _colorPreviewTicket.reset();
        return;
    }
    if (!_colorMailboxOpen)
    {
        _colorMailboxOpen = _colorPreviews.Begin();
    }
    if (!_colorMailboxOpen || OfferColorPreview() < 0)
    {
        QueueColorFailure();
        return;
    }
    LaunchColorPreview();
}

void Application::CompleteColorPreview(PreviewStamp stamp, ColorEditPreviewResult& result,
                                       JobResult completion) noexcept
{
    if (!_colorPreviews.CanPublish(stamp))
    {
        return;
    }
    _colorPreviewTicket.reset();
    if (completion.status != JobStatus::Finished)
    {
        result.status =
            completion.status == JobStatus::Cancelled ? ColorEditJobStatus::Cancelled : ColorEditJobStatus::Failed;
    }
    const bool applied = _session.ApplyColorEditPreview(result);
    (void)_colorPreviews.Finish(stamp, applied);
    if (result.status == ColorEditJobStatus::Failed && result.token.sequence == _session.ColorEditSequence())
    {
        QueueColorFailure();
    }
    LaunchColorPreview();
    if (applied || result.token.sequence == _session.ColorEditSequence())
    {
        DocumentChanged();
    }
}

void Application::CompleteColorHistogram(ColorEditHistogramResult& result, JobResult completion) noexcept
{
    if (result.token.documentSerial != _colorDocumentSerial || result.openingSequence != _colorOpening)
    {
        return;
    }
    _colorHistogramTicket.reset();
    if (completion.status != JobStatus::Finished)
    {
        result.status =
            completion.status == JobStatus::Cancelled ? ColorEditJobStatus::Cancelled : ColorEditJobStatus::Failed;
    }
    if (_session.ApplyColorEditHistogram(result))
    {
        SyncChrome();
        _frameInvalidated = true;
    }
    else if (result.status == ColorEditJobStatus::Failed)
    {
        QueueColorFailure();
    }
}

int Application::RunColorJobsSelfTest() noexcept
{
    const auto fail = [](const wchar_t* message) noexcept
    {
        SelfTestFailure(message);
        return 5;
    };
    CancelColorEditForTest();
    if (SettleJobs(_jobs, 10000, DispatchMessageThunk, this) != SettleResult::Settled)
    {
        return fail(L"Color job fixture could not settle preceding work.\n");
    }
    constexpr std::array<std::uint8_t, 16> pixels{0,   0,   0,   255, 64,  64,  64,  255,
                                                  128, 128, 128, 255, 255, 255, 255, 255};
    const Raster source = Raster::Make(4, 1, RasterFormat::Bgra8888, pixels.data(), pixels.size());
    CanvasDocument document;
    document.id = Guid::Generate();
    document.width = 4;
    document.height = 1;
    ImageLayer layer = ImageLayer::Blank(L"Color jobs", Vec2{4, 1});
    const Guid active = layer.id;
    layer.asset = RasterAsset{source, source, layer.name};
    document.layers.push_back(std::move(layer));
    ReplaceDocument(std::move(document), active, {});

    const auto cleanup = wil::scope_exit(
        [&]() noexcept
        {
            if (_colorWorkTestGate)
            {
                (void)SetEvent(_colorWorkTestGate->release.get());
            }
            CancelColorEditForTest();
            _colorWorkTestGate.reset();
            if (SettleJobs(_jobs, 10000, DispatchMessageThunk, this) != SettleResult::Settled)
            {
                SelfTestFailure(L"Color jobs did not settle at the harness failure boundary.\n");
                std::_Exit(5);
            }
        });
    const auto newGate = [&](bool histogram)
    {
        _colorWorkTestGate = std::make_shared<ColorWorkTestGate>();
        _colorWorkTestGate->kind = histogram ? ColorWorkTestKind::Histogram : ColorWorkTestKind::Preview;
        return _colorWorkTestGate->entered && _colorWorkTestGate->release && _colorWorkTestGate->computed;
    };
    if (!newGate(true) || !BeginColorEdit(ChromeHost::kCommandLevels) ||
        WaitForSingleObject(_colorWorkTestGate->entered.get(), 10000) != WAIT_OBJECT_0)
    {
        return fail(L"The production Levels histogram did not reach its worker gate.\n");
    }
    OnAdjustAction(AdjustAction::SetInputGamma, 2);
    _viewport.followsFit = false;
    _viewport.zoom = 1;
    constexpr UINT gateMessage = WM_APP + 0x7D0;
    struct PumpEvidence final
    {
        Application* owner;
        bool beforeHistogram = false;
        bool zoomed = false;
    } evidence{this};
    const auto dispatch = [](void* context, void* nativeMessage) noexcept
    {
        auto& seen = *static_cast<PumpEvidence*>(context);
        auto& message = *static_cast<MSG*>(nativeMessage);
        Application& app = *seen.owner;
        if (!message.hwnd && message.message == gateMessage)
        {
            seen.beforeHistogram = app._session.HasColorEdit() && !app._session.PendingColorEdit()->histogramReady;
            app.OnCommand(ChromeHost::kCommandZoomIn);
            seen.zoomed = app._viewport.zoom > 1;
            (void)SetEvent(app._colorWorkTestGate->release.get());
        }
        else
        {
            app.DispatchQueuedMessage(message);
        }
    };
    if (!PostThreadMessageW(GetCurrentThreadId(), gateMessage, 0, 0) ||
        SettleJobs(_jobs, 10000, dispatch, &evidence) != SettleResult::Settled || !evidence.beforeHistogram ||
        !evidence.zoomed || !_session.PendingColorEdit()->histogramReady ||
        _session.PendingColorEdit()->histogram[0][64] != 1 ||
        _session.PendingColorEdit()->settings.levels.ranges[0].gamma != 2)
    {
        return fail(L"Messages/view input or the opening histogram failed while real color work was gated.\n");
    }
    CancelColorEditForTest();

    if (!newGate(false) || !BeginColorEdit(ChromeHost::kCommandLevels))
    {
        return fail(L"The coalesced preview fixture could not open.\n");
    }
    OnAdjustAction(AdjustAction::SetInputGamma, 2);
    if (WaitForSingleObject(_colorWorkTestGate->entered.get(), 10000) != WAIT_OBJECT_0)
    {
        return fail(L"The production preview did not reach its worker gate.\n");
    }
    for (int tick = 0; tick < 12; ++tick)
    {
        OnAdjustAction(AdjustAction::SetInputGamma, 1.1 + tick * 0.1);
    }
    OnAdjustAction(AdjustAction::SetInputGamma, 3);
    if (_colorWorkTestGate->visits.load() != 1 || !_colorPreviews.HasPending())
    {
        return fail(L"Settings ticks started overlapping preview work instead of keeping the latest pending input.\n");
    }
    (void)SetEvent(_colorWorkTestGate->release.get());
    if (SettleJobs(_jobs, 10000, DispatchMessageThunk, this) != SettleResult::Settled ||
        _colorWorkTestGate->visits.load() != 2 || _colorPreviews.HasPending() ||
        !_session.PendingColorEdit()->previewLookup || _session.PendingColorEdit()->previewRaster.HasStorage() ||
        _session.PendingColorEdit()->previewSequence != _session.ColorEditSequence() ||
        _session.PendingColorEdit()->settings.levels.ranges[0].gamma != 3)
    {
        return fail(L"The preview mailbox did not finish with exactly the latest request.\n");
    }
    CancelColorEditForTest();

    if (!newGate(false) || !BeginColorEdit(ChromeHost::kCommandLevels))
    {
        return fail(L"The late completion fixture could not open.\n");
    }
    OnAdjustAction(AdjustAction::SetInputGamma, 2);
    if (WaitForSingleObject(_colorWorkTestGate->entered.get(), 10000) != WAIT_OBJECT_0)
    {
        return fail(L"The late completion fixture did not start.\n");
    }
    (void)SetEvent(_colorWorkTestGate->release.get());
    if (WaitForSingleObject(_colorWorkTestGate->computed.get(), 10000) != WAIT_OBJECT_0)
    {
        return fail(L"The old preview did not finish its pixels before the Cancel/reopen race.\n");
    }
    CancelColorEditForTest(); // Its successful pixels exist, but no completion has been drained yet.
    if (!BeginColorEdit(ChromeHost::kCommandLevels) ||
        SettleJobs(_jobs, 10000, DispatchMessageThunk, this) != SettleResult::Settled ||
        _session.PendingColorEdit()->previewRaster.HasStorage() || _session.PendingColorEdit()->previewLookup ||
        !_session.PendingColorEdit()->histogramReady || _session.ActiveLayer()->asset->image != source)
    {
        return fail(L"A completed old preview crossed the cancelled panel lifetime.\n");
    }
    CancelColorEditForTest();
    if (SettleJobs(_jobs, 10000, DispatchMessageThunk, this) != SettleResult::Settled)
    {
        return fail(L"The live adjustment fixture could not retire earlier work.\n");
    }
    _session.AddAdjustment(AdjustmentKind::Levels);
    const auto adjustmentId = _session.AdjustmentEditingID();
    LayerAdjustment original;
    original.kind = AdjustmentKind::Levels;
    original.levels.ranges[0].gamma = 1.25;
    if (!adjustmentId || !_session.UpdateAdjustment(*adjustmentId, original) ||
        !_session.BeginAdjustmentEditing(*adjustmentId, source, true) || !newGate(false))
    {
        return fail(L"The live adjustment lookup fixture did not open.\n");
    }
    DocumentChanged();
    if (WaitForSingleObject(_colorWorkTestGate->entered.get(), 10000) != WAIT_OBJECT_0)
    {
        return fail(L"The live adjustment did not build its lookup on a worker.\n");
    }
    const auto matchesAdjustment = [&](const LayerAdjustment& adjustment)
    {
        const CanvasDocument* display = _session.DisplayDocument();
        std::vector<std::uint8_t> shown;
        UINT width = 0;
        UINT height = 0;
        const Raster expected = Adjust::ApplyAdjustment(adjustment, source);
        if (!display || FAILED(_composition.Compose(*display)) || FAILED(_composition.Readback(shown, width, height)) ||
            width != 4 || height != 1 || !expected.HasStorage() || shown.size() != 16)
            return false;
        const auto* bytes = expected.Data();
        for (std::size_t index = 0; index < shown.size(); ++index)
        {
            if (std::abs(static_cast<int>(shown[index]) - bytes[index]) > (index % 4 == 3 ? 0 : 1))
                return false;
        }
        return true;
    };
    OnAdjustAction(AdjustAction::SetInputGamma, 3);
    if (!matchesAdjustment(original) || !_colorPreviews.HasPending())
    {
        return fail(L"Pending live adjustment work changed the canvas before its immutable lookup completed.\n");
    }
    (void)SetEvent(_colorWorkTestGate->release.get());
    if (SettleJobs(_jobs, 10000, DispatchMessageThunk, this) != SettleResult::Settled ||
        !_session.PendingColorEdit()->previewLookup || !_session.PendingColorEdit()->previewLookupOnly ||
        _session.PendingColorEdit()->previewRaster.HasStorage() ||
        _session.PendingColorEdit()->previewSequence != _session.ColorEditSequence() ||
        _session.PendingColorEdit()->previewLookup->adjustment.levels.ranges[0].gamma != 3 ||
        !matchesAdjustment(_session.PendingColorEdit()->previewLookup->adjustment))
    {
        return fail(L"The live adjustment mailbox did not publish the latest lookup and matching GPU pixels.\n");
    }
    std::printf("PASS color-jobs.live-worker-lookup-and-latest-gpu-pixels\n");
    if (!newGate(false))
        return fail(L"The live preview-off gate could not be created.\n");
    _session.UpdateColorEdit(_session.PendingColorEdit()->settings, false);
    DocumentChanged();
    if (WaitForSingleObject(_colorWorkTestGate->entered.get(), 10000) != WAIT_OBJECT_0 || !matchesAdjustment(original))
    {
        return fail(L"Live preview off waited for a worker before restoring the opening adjustment.\n");
    }
    CancelColorEditForTest();
    (void)SetEvent(_colorWorkTestGate->release.get());
    if (SettleJobs(_jobs, 10000, DispatchMessageThunk, this) != SettleResult::Settled || _session.HasColorEdit() ||
        !matchesAdjustment(original))
    {
        return fail(L"A late lookup crossed live adjustment Cancel.\n");
    }
    std::printf("PASS color-jobs.live-preview-off-and-cancel-in-flight\n");
    _session.SelectLayer(active); // leave the image target expected by the following host fixtures
    DocumentChanged();
    return 0;
}
