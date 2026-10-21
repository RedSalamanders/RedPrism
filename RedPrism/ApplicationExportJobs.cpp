#include "Application.h"

#include "ChromeHost.h"
#include "DocumentReadbackJob.h"
#include "ExportWork.h"
#include "ImageCodecs.h"
#include "ProjectStore.h"
#include "Runtime.Tests.ExportWorkTestGate.h"
#include "resource.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <utility>

class Application::ExportReadbackJob final : public DocumentReadbackJob
{
  public:
    ExportReadbackJob(Application& owner, HistoryWorkStamp stamp, JobToken token, CanvasDocument document,
                      HostWriteFormat format, JpegExportOptions options, std::wstring path)
        : DocumentReadbackJob(
              owner._jobs, owner._gpuWork, owner._composition, std::move(document),
              {0, 0, 0, static_cast<UINT>(token.canvasWidth), static_cast<UINT>(token.canvasHeight), 1}),
          _owner(owner), _stamp(stamp), _token(std::move(token)), _format(format), _options(options),
          _path(std::move(path)), _gate(owner._exportWorkTestGate), _ownerThread(GetCurrentThreadId())
    {
    }

  private:
    HRESULT PrepareReadbackDevice(HRESULT reason) noexcept override
    {
        return _owner.PrepareUtilityDevice(reason);
    }
    // The owner acknowledges the final mapping and releases utility surfaces before the encoding stage.
    // Once every tile belongs to this immutable CPU result, a new device cannot invalidate or repeat encoding.
    bool ProcessReadbackDetached() const noexcept override
    {
        return true;
    }
    JobResult ProcessReadback(Raster& image, JobContext& context) override
    {
        if (_gate && !_gate->afterPublication && _gate->visits.load() == 0)
        {
            const auto result = PassGate(context);
            if (result.status != JobStatus::Finished)
                return result;
        }
        if (_gate && _gate->throwAllocationFailure)
            throw std::bad_alloc{};
        if (context.cancellation.IsCancelled())
            return {JobStatus::Cancelled};
        if (_gate)
            _gate->encodeAttempts.fetch_add(1);
        _status = _format == HostWriteFormat::Jpeg
                      ? EncodeJpegFile(image, _options, _path.c_str(), context.cancellation)
                      : EncodePngFile(image, _options.resolution, _path.c_str(), context.cancellation);
        _published = _status == ImageCodecStatus::Ok;
        // Encoding has returned and no future tile reads this raster. Release large CPU output on its worker.
        image = {};
        if (_gate && _gate->afterPublication)
        {
            const auto result = PassGate(context);
            if (result.status != JobStatus::Finished)
                return result;
        }
        return _status == ImageCodecStatus::Cancelled ? JobResult{JobStatus::Cancelled} : JobResult{};
    }
    void ReadbackCompleted(JobResult result, Raster, std::uint64_t) noexcept override
    {
        const bool current = _owner._exportStamp == _stamp && _owner._historyWork.IsCurrent(_stamp) &&
                             _owner._activeHostRequest && _owner._activeHostRequest->ordinal == _stamp.ordinal &&
                             _owner._session.CanApplyJobResult(_token, _token.sequence);
        HostOutcome outcome = result.status == JobStatus::Cancelled ? HostOutcome::Cancelled
                              : result.status == JobStatus::Failed  ? HostOutcome::Failed
                                                                    : HostOutcome::Succeeded;
        std::int32_t error = result.error;
        if (!current)
        {
            outcome = HostOutcome::Stale;
            error = E_ABORT;
        }
        else if (_owner._historyWork.CancelRequested(_stamp) || result.status == JobStatus::Cancelled)
        {
            outcome = HostOutcome::Cancelled;
            error = E_ABORT;
        }
        else if (_published)
        {
            outcome = HostOutcome::Succeeded;
            error = S_OK;
        }
        else if (outcome == HostOutcome::Succeeded)
        {
            outcome = _status == ImageCodecStatus::Cancelled ? HostOutcome::Cancelled : HostOutcome::Failed;
            error = _status == ImageCodecStatus::TooLarge ? HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE)
                    : outcome == HostOutcome::Cancelled   ? E_ABORT
                                                          : E_FAIL;
        }
        if (outcome == HostOutcome::Failed)
        {
            const auto stringId = error == HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE) ? IDS_ERROR_TOO_LARGE
                                  : _format == HostWriteFormat::Jpeg                ? IDS_ERROR_EXPORT_JPEG
                                                                                    : IDS_ERROR_EXPORT_PNG;
            ++_owner._errorsShown;
            _owner._lastErrorId = stringId;
            _owner._prompts->PostNotice(_owner._window.get(), kNoticeMessage, stringId);
        }
        _owner._exportTicket.reset();
        _owner._exportStamp.reset();
        (void)_owner._historyWork.Finish(_stamp);
        _owner.SyncChrome();
        _owner._frameInvalidated = true; // exports never alter the document/path/saved revision
        _owner.ResumeHostHistory(_stamp, outcome, error);
    }
    JobResult PassGate(JobContext& context) noexcept
    {
        _gate->quality.store(_options.quality);
        _gate->resolution.store(_options.resolution);
        _gate->visits.fetch_add(1);
        if (!PostThreadMessageW(_ownerThread, ExportWorkTestGate::kMessage, 0, 0))
            return {JobStatus::Failed, HRESULT_FROM_WIN32(GetLastError())};
        const HANDLE handles[]{_gate->release.get(), context.shutdownEvent};
        const auto waited = WaitForMultipleObjects(2, handles, FALSE, 10000);
        if (context.cancellation.IsCancelled() || waited == WAIT_OBJECT_0 + 1)
            return {JobStatus::Cancelled};
        return waited == WAIT_OBJECT_0 ? _gate->result
                                       : JobResult{JobStatus::Failed, HRESULT_FROM_WIN32(ERROR_TIMEOUT)};
    }
    Application& _owner; // owner device/completion stages only
    const HistoryWorkStamp _stamp;
    const JobToken _token;
    const HostWriteFormat _format;
    const JpegExportOptions _options;
    const std::wstring _path;
    const std::shared_ptr<ExportWorkTestGate> _gate;
    const DWORD _ownerThread;
    ImageCodecStatus _status = ImageCodecStatus::Encode;
    bool _published = false; // published stage ownership synchronizes this worker write with owner reads
};

HRESULT Application::StartExportReadback(HostWriteFormat format) noexcept
{
    if (!_activeHostRequest || !_session.Document() || _session.HasColorEdit() || _exportTicket ||
        (format != HostWriteFormat::Png && format != HostWriteFormat::Jpeg))
        return E_UNEXPECTED;
    const auto path = _activeHostRequest->input.SelectedOutputPath();
    if (!path)
        return E_UNEXPECTED;
    std::array<wchar_t, 32768> resolved{};
    const DWORD units = GetFullPathNameW(path->data(), static_cast<DWORD>(resolved.size()), resolved.data(), nullptr);
    if (units == 0)
        return HRESULT_FROM_WIN32(GetLastError());
    if (units >= resolved.size() || units > kMaxImageOutputPathUnits)
        return HRESULT_FROM_WIN32(ERROR_FILENAME_EXCED_RANGE);
    const auto ordinal = _activeHostRequest->ordinal;
    _exportStamp = _historyWork.Begin(_session.DocumentSerial(), ordinal, HistoryWorkKind::Commit);
    if (!_exportStamp && _hostFinishedStamp && _hostFinishedStamp->ordinal == ordinal)
        _exportStamp =
            _historyWork.Continue(_hostFinishedStamp.value(), _session.DocumentSerial(), HistoryWorkKind::Commit);
    if (!_exportStamp)
        return E_UNEXPECTED;
    HRESULT result = E_INVALIDARG;
    try
    {
        if (_exportWorkTestGate && _exportWorkTestGate->throwAdmissionFailure)
            throw std::bad_alloc{};
        auto token = _session.CaptureJobToken();
        const auto memory = ExportReadbackMemoryBytes(_session.Document().value(), units);
        if (static_cast<std::int64_t>(_session.Document()->width) * _session.Document()->height > kMaxLayerPixels)
            result = HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
        if (token && memory != 0 && memory <= (std::numeric_limits<std::uint64_t>::max)() - sizeof(ExportReadbackJob))
        {
            CanvasDocument snapshot = _session.Document().value();
            snapshot.selection.reset(); // export pixels come from the committed whole document, never selection
            std::unique_ptr<Job> job = std::make_unique<ExportReadbackJob>(
                *this, _exportStamp.value(), std::move(token.value()), std::move(snapshot), format,
                JpegExportOptions{_settings.jpegQuality, _session.Document()->resolution},
                std::wstring(resolved.data(), units));
            JobTicket ticket;
            result = static_cast<HRESULT>(_jobs.Submit(
                job, {JobClass::Io, _session.DocumentSerial(), memory + sizeof(ExportReadbackJob)}, ticket));
            if (SUCCEEDED(result))
                _exportTicket = ticket;
        }
    }
    catch (const std::bad_alloc&)
    {
        result = E_OUTOFMEMORY;
    }
    if (FAILED(result))
    {
        (void)_historyWork.Finish(_exportStamp.value());
        _hostFinishedStamp = _exportStamp;
        _exportStamp.reset();
        return result;
    }
    _hostCommitStamp = _exportStamp;
    _hostWriteInFlight = true;
    return S_OK;
}

int Application::RunExportReadbackSelfTest() noexcept
{
    enum class Mode
    {
        Png,
        Jpeg,
        Ordered,
        CancelBefore,
        CancelPublished,
        Failure,
        Allocation,
        Stale,
        RebindBefore,
        RebindPublished,
        Options,
        Admission,
        PixelBudget,
        PathBudget,
        LaterTileBusy
    };
    struct State final
    {
        Application& owner;
        Mode mode;
        ContinuationReceipt receipt;
        Guid revision;
        std::wstring output;
        double quality;
        bool visited = false, held = false, cancelPending = false;
        HRESULT rebound = E_PENDING;
    };
    const auto dispatch = [](void* context, void* native) noexcept
    {
        auto& state = *static_cast<State*>(context);
        auto& owner = state.owner;
        auto& message = *static_cast<MSG*>(native);
        if (message.message != ExportWorkTestGate::kMessage)
        {
            owner.DispatchQueuedMessage(message);
            return;
        }
        state.visited = true;
        state.held = owner._exportTicket && owner._exportStamp && owner._historyWork.Busy() &&
                     owner._hostWriteInFlight && owner._activeHostRequest && owner._gpuWork.LentCount() == 0 &&
                     owner._composition.UtilitySurfaceBytes() == 0 &&
                     state.receipt.Read().outcome == HostOutcome::Pending && owner._jobs.ReservedMemoryBytes() != 0 &&
                     owner._session.history.CurrentRevision() == state.revision && owner._session.history.IsModified();
        const bool published = owner._exportWorkTestGate->afterPublication;
        state.held = state.held && ((GetFileAttributesW(state.output.c_str()) != INVALID_FILE_ATTRIBUTES) == published);
        owner.OnCommand(ChromeHost::kCommandZoomActual);
        state.held = state.held && owner._session.history.CurrentRevision() == state.revision;
        if (state.mode == Mode::CancelBefore || state.mode == Mode::CancelPublished)
        {
            owner.CancelHostContinuations();
            state.cancelPending =
                owner._activeHostRequest && owner._exportTicket && state.receipt.Read().outcome == HostOutcome::Pending;
        }
        else if (state.mode == Mode::Stale)
            owner._session.CreateDocument(3, 2); // bypass normal ordering to prove stale lineage isolation
        else if (state.mode == Mode::RebindBefore || state.mode == Mode::RebindPublished)
        {
            state.rebound = owner._renderer.RecoverDeviceForTest();
            owner.BindCompositionDevice();
        }
        else if (state.mode == Mode::Options)
        {
            state.held = state.held && owner._exportWorkTestGate->quality.load() == state.quality &&
                         owner._exportWorkTestGate->resolution.load() == 72;
            owner._settings.jpegQuality = 0.05; // queued writer must retain the already captured options
        }
        owner._composition.ReleaseDocumentSurfaces();
        state.held = state.held && owner._composition.CanvasSurfaceBytes() == 0;
        (void)SetEvent(owner._exportWorkTestGate->release.get());
    };
    std::array<wchar_t, 32768> temporary{};
    const DWORD units = GetTempPathW(static_cast<DWORD>(temporary.size()), temporary.data());
    if (units == 0 || units >= temporary.size())
        return 5;
    const std::filesystem::path root =
        std::filesystem::path(temporary.data()) / (L"RedPrism.ExportHost." + Guid::Generate().ToString());
    if (!CreateDirectoryW(root.c_str(), nullptr))
        return 5;
    const auto previousQuality = _settings.jpegQuality;
    const auto previousTextureLimit = _composition.TextureLimit();
    const auto cleanup = wil::scope_exit(
        [&]() noexcept
        {
            if (_exportWorkTestGate)
                (void)SetEvent(_exportWorkTestGate->release.get());
            CancelHostContinuations();
            if (SettleJobs(_jobs, 15000, DispatchMessageThunk, this) != SettleResult::Settled)
                std::_Exit(5);
            _exportWorkTestGate.reset();
            _settings.jpegQuality = previousQuality;
            _composition.SetTextureLimitForTest(previousTextureLimit);
            _gpuWork.SetMapBusyForTest(0);
            std::error_code error;
            const auto target = std::filesystem::weakly_canonical(root, error);
            const auto base = std::filesystem::weakly_canonical(std::filesystem::path(temporary.data()), error);
            if (!error && target.parent_path() == base)
                std::filesystem::remove_all(target, error);
        });
    for (const auto mode : {Mode::Png, Mode::Jpeg, Mode::Ordered, Mode::CancelBefore, Mode::CancelPublished,
                            Mode::Failure, Mode::Allocation, Mode::Stale, Mode::RebindBefore, Mode::RebindPublished,
                            Mode::Options, Mode::Admission, Mode::PixelBudget, Mode::PathBudget, Mode::LaterTileBusy})
    {
        _session.CreateDocument(mode == Mode::PixelBudget     ? 10001
                                : mode == Mode::LaterTileBusy ? 17
                                                              : 2,
                                mode == Mode::PixelBudget ? 10000 : 1);
        _composition.SetTextureLimitForTest(mode == Mode::LaterTileBusy ? 16 : previousTextureLimit);
        _gpuWork.SetMapBusyForTest(mode == Mode::LaterTileBusy ? 2 : 0, mode == Mode::LaterTileBusy ? 1 : 0);
        const auto id = InsertPaintedLayer(_session, L"Export snapshot", 2, 1, {}, 0, 0xFF404040);
        _session.history.MarkSaved();
        _session.InvertPixels(); // independent captured literal is opaque 191/255
        const auto revision = _session.history.CurrentRevision();
        const auto historyCount = _session.history.UndoCount();
        const auto errors = _errorsShown;
        const auto presents = _renderer.PresentCount();
        _settings.jpegQuality = 0.8;
        _exportWorkTestGate = std::make_shared<ExportWorkTestGate>();
        if (!_exportWorkTestGate->release || id == Guid{})
            return 5;
        _exportWorkTestGate->afterPublication = mode == Mode::CancelPublished || mode == Mode::RebindPublished;
        if (mode == Mode::Failure)
            _exportWorkTestGate->result = {JobStatus::Failed, E_ACCESSDENIED};
        _exportWorkTestGate->throwAllocationFailure = mode == Mode::Allocation;
        _exportWorkTestGate->throwAdmissionFailure = mode == Mode::Admission;
        const bool jpeg = mode == Mode::Jpeg || mode == Mode::Options;
        const auto format = jpeg ? HostWriteFormat::Jpeg : HostWriteFormat::Png;
        const std::wstring output =
            mode == Mode::PathBudget
                ? L"C:\\" + std::wstring(32726, L'a')
                : (root / (std::to_wstring(static_cast<unsigned int>(mode)) + (jpeg ? L".jpg" : L".png"))).wstring();
        _documentPath = L"before-export.prism";
        DocumentChanged();
        std::wstring callerPath = output;
        const auto receipt = QueueHostInput(HostWriteInput{format, false, callerPath});
        callerPath = L"changed-after-admission.png";
        std::optional<ContinuationReceipt> younger;
        if (mode == Mode::Ordered)
            younger = QueueHostInput(HostCommandInput{ChromeHost::kCommandInvert});
        State state{*this, mode, receipt, revision, output, _settings.jpegQuality};
        const auto settled = SettleJobs(_jobs, 20000, dispatch, &state);
        const auto actual = receipt.Read();
        const bool cancelled = mode == Mode::CancelBefore || mode == Mode::CancelPublished;
        const bool admission = mode == Mode::Admission || mode == Mode::PixelBudget || mode == Mode::PathBudget;
        const bool failed =
            mode == Mode::Failure || mode == Mode::Allocation || admission || mode == Mode::LaterTileBusy;
        const auto expected = mode == Mode::Stale ? HostOutcome::Stale
                              : cancelled         ? HostOutcome::Cancelled
                              : failed            ? HostOutcome::Failed
                                                  : HostOutcome::Succeeded;
        bool correct =
            settled == SettleResult::Settled &&
            (admission || mode == Mode::LaterTileBusy ? !state.visited && _exportWorkTestGate->visits.load() == 0 &&
                                                            _exportWorkTestGate->encodeAttempts.load() == 0
                                                      : state.visited && state.held) &&
            actual.outcome == expected && !_activeHostRequest && !_hostWriteInFlight && !_exportTicket &&
            !_exportStamp && !_historyWork.Busy() && _jobs.ReservedMemoryBytes() == 0 && _hostInputBudget.Used() == 0 &&
            _gpuWork.LentCount() == 0 && _gpuWork.InFlightCount() == 0 && !_gpuWork.HasPending() &&
            _composition.UtilitySurfaceBytes() == 0 && _renderer.PresentCount() == presents &&
            _documentPath == L"before-export.prism" && _session.Document();
        if (cancelled)
            correct = correct && state.cancelPending;
        const bool published = mode != Mode::CancelBefore && !failed;
        if (published)
        {
            DecodedImage decoded;
            correct = correct && ImportImageFile(output.c_str(), kMaxLayerPixels, decoded) == ImageCodecStatus::Ok &&
                      decoded.image.Width() == 2 && decoded.image.Height() == 1 && decoded.image.HasStorage();
            if (decoded.image.HasStorage())
            {
                const auto* bytes = decoded.image.Data();
                for (std::size_t pixel = 0; pixel < 2; ++pixel)
                {
                    for (std::size_t channel = 0; channel < 3; ++channel)
                        correct =
                            correct && std::abs(static_cast<int>(bytes[pixel * 4 + channel]) - 191) <= (jpeg ? 3 : 0);
                    correct = correct && bytes[pixel * 4 + 3] == 255;
                }
            }
        }
        else
            correct = correct && GetFileAttributesW(output.c_str()) == INVALID_FILE_ATTRIBUTES;
        if (mode == Mode::Ordered)
            correct = correct && younger->Read().outcome == HostOutcome::Succeeded && _session.history.IsModified() &&
                      _session.history.UndoCount() == historyCount + 1 &&
                      _session.ActiveLayer()->asset->image.Data()[0] == 64;
        else if (mode == Mode::Stale)
            correct = correct && _session.Document()->width == 3 && _session.Document()->height == 2;
        else
            correct = correct && _session.history.CurrentRevision() == revision && _session.history.IsModified() &&
                      _session.history.UndoCount() == historyCount;
        if (failed)
            correct = correct && _errorsShown == errors + 1 &&
                      _lastErrorId ==
                          static_cast<UINT>(mode == Mode::PixelBudget ? IDS_ERROR_TOO_LARGE : IDS_ERROR_EXPORT_PNG) &&
                      actual.error == (mode == Mode::Failure         ? E_ACCESSDENIED
                                       : mode == Mode::PixelBudget   ? HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE)
                                       : mode == Mode::PathBudget    ? HRESULT_FROM_WIN32(ERROR_FILENAME_EXCED_RANGE)
                                       : mode == Mode::LaterTileBusy ? DXGI_ERROR_WAS_STILL_DRAWING
                                                                     : E_OUTOFMEMORY);
        else
            correct = correct && _errorsShown == errors;
        if (mode == Mode::RebindBefore || mode == Mode::RebindPublished)
            correct = correct && SUCCEEDED(state.rebound) && _exportWorkTestGate->visits.load() == 1 &&
                      _exportWorkTestGate->encodeAttempts.load() == 1;
        std::fprintf(stderr, "%s export-host.case=%u\n", correct ? "PASS" : "FAIL", static_cast<unsigned int>(mode));
        if (!correct)
            return 5;
    }
    return 0;
}
