#include "Application.h"

#include "ChromeHost.h"
#include "ColorEditHistory.h"
#include "Runtime.Tests.ColorWorkTestGate.h"
#include "resource.h"

#include "../Tests/Support/Support.Tests.DocumentFixture.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>
#include <new>
#include <utility>

namespace
{
#pragma warning(push)
#pragma warning(disable : 4324)
struct ColorCommitPayload final : DeferredReleaseNode
{
    std::optional<ColorEditJobInput> input;
    ColorEditJobResult result;
    std::optional<CanvasDocument> source;
    DocumentHistory::StateView history;
    std::optional<PreparedColorEdit> publication;
    std::shared_ptr<ColorWorkTestGate> gate;
    DWORD ownerThread = 0;
    std::uint64_t generation = 0;
    ColorCommitPayload()
    {
        bytes = kDeferredReleaseThreshold;
        destroy = [](DeferredReleaseNode* node) noexcept
        { const std::unique_ptr<ColorCommitPayload> owned(static_cast<ColorCommitPayload*>(node)); };
    }
    ~ColorCommitPayload()
    {
        if (gate)
        {
            gate->payloadRetiredOffOwner.store(GetCurrentThreadId() != ownerThread);
            gate->payloadRetired.store(true);
        }
    }
};
#pragma warning(pop)

struct RetireColorCommitPayload final
{
    void operator()(ColorCommitPayload* payload) const noexcept
    {
        if (payload && !TryDeferRelease(*payload))
        {
            const std::unique_ptr<ColorCommitPayload> owned(payload);
        }
    }
};

} // namespace

// Production OK/write callers enter this engine through the owned continuation source. Admission is Pending;
// only validated apply/drop releases its shared history barrier and authorizes a dependent action.
class Application::ColorCommitJob final : public Job
{
  public:
    ColorCommitJob(Application& owner, HistoryWorkStamp stamp, std::uint64_t opening, std::uint64_t sequence,
                   std::uint64_t retirementBytes)
        : _owner(owner), _stamp(stamp), _opening(opening), _sequence(sequence), _retirementBytes(retirementBytes),
          _gate(owner._colorWorkTestGate), _ownerThread(GetCurrentThreadId())
    {
    }
    JobResult Run(JobContext& context) override
    {
        if (context.cancellation.IsCancelled())
            return {JobStatus::Cancelled};
        if (!_payload)
            return {}; // reach admitted owner capture through this same suspended/resumed record
        if (const auto gate = PassColorWorkTestGate(_gate, ColorWorkTestKind::Commit, context);
            gate.status != JobStatus::Finished)
        {
            return gate;
        }
        if (_gate && _gate->kind == ColorWorkTestKind::Commit && _gate->throwAllocationFailure)
        {
            throw std::bad_alloc{}; // harness-only: reaches the actual scheduler runner's translation boundary
        }
        _payload->result = RunColorEditJob(_payload->input.value(), context.executor, context.cancellation);
        if (_payload->result.status == ColorEditJobStatus::Ready)
        {
            const DocumentHistory::PrepareObserver observer{[](void* context, DocumentHistory::PrepareStage stage)
                                                            {
                                                                auto* gate = static_cast<ColorWorkTestGate*>(context);
                                                                if (gate && gate->historyFailure == stage)
                                                                {
                                                                    gate->historyFaultHits.fetch_add(1);
                                                                    throw std::bad_alloc{};
                                                                }
                                                            },
                                                            _gate.get()};
            _payload->publication = PrepareColorEditPublication(
                _payload->source, _payload->input->edit.layerID, _payload->generation, std::move(_payload->history),
                _payload->result, context.cancellation, _gate ? observer : DocumentHistory::PrepareObserver{});
            if (!_payload->publication)
                _payload->result.status =
                    context.cancellation.IsCancelled() ? ColorEditJobStatus::Cancelled : ColorEditJobStatus::Failed;
        }
        if (_gate && _gate->kind == ColorWorkTestKind::Commit)
        {
            _gate->unchangedCommit.store(_payload->result.status == ColorEditJobStatus::Unchanged);
            (void)SetEvent(_gate->computed.get());
        }
        return {};
    }
    bool DeferCompletion(JobResult result, JobTicket ticket) noexcept override
    {
        const auto& panel = _owner._session.PendingColorEdit();
        if (_payload || result.status != JobStatus::Finished || _owner._shutdownStarted ||
            !_owner._historyWork.IsCurrent(_stamp) || _owner._historyWork.CancelRequested(_stamp) || !panel ||
            panel->openingSequence != _opening || _owner._session.ColorEditSequence() != _sequence ||
            _owner._session.DocumentSerial() != _stamp.documentSerial)
            return false;
        try
        {
            auto owned = std::make_unique<ColorCommitPayload>();
            owned->bytes = (std::max)(_retirementBytes, kDeferredReleaseThreshold);
            owned->gate = _gate;
            owned->ownerThread = _ownerThread;
            _payload.reset(owned.release());
            if (_gate)
            {
                _gate->captureReservationHeld.store(_gate->reservationBytes != 0 &&
                                                    _owner._jobs.ReservedMemoryBytes() >= _gate->reservationBytes);
                if (_gate->throwCaptureFailure)
                    throw std::bad_alloc{};
            }
            _payload->input = _owner._session.PrepareColorEditCommit();
            if (!_payload->input)
            {
                _captureError = E_ABORT;
                return false;
            }
            _payload->result.token = _payload->input->token;
            _payload->result.kind = _payload->input->edit.kind;
            _payload->result.settings = _payload->input->edit.settings;
            _payload->source = _owner._session.Document();
            _payload->generation = _owner._session.Generation();
            _payload->history = _owner._session.history.CaptureState();
            if (_owner._jobs.Resume(ticket))
                return true;
            _captureError = E_UNEXPECTED;
        }
        catch (const std::bad_alloc&)
        {
            _captureError = E_OUTOFMEMORY; // owning noexcept capture boundary fails this whole operation
        }
        return false;
    }
    void Completed(JobResult completion) noexcept override
    {
        auto& result = _payload ? _payload->result : _failure;
        auto* publication = _payload && _payload->publication ? &_payload->publication.value() : nullptr;
        _owner.CompleteColorCommit(_stamp, _opening, result, completion, publication, _captureError);
    }

  private:
    Application& _owner; // UI completion only. Run cannot access the mutable owner.
    const HistoryWorkStamp _stamp;
    const std::uint64_t _opening;
    const std::uint64_t _sequence;
    const std::uint64_t _retirementBytes;
    ColorEditJobResult _failure; // bounded preallocated failure header; no source snapshot before admission
    const std::shared_ptr<ColorWorkTestGate> _gate;
    const DWORD _ownerThread;
    std::int32_t _captureError = 0;
    std::unique_ptr<ColorCommitPayload, RetireColorCommitPayload> _payload;
};

Application::HostOutcome Application::StartColorCommit(std::uint64_t ordinal) noexcept
{
    if (ColorCommitBusy())
    {
        return _colorCommitStamp->ordinal == ordinal ? HostOutcome::Pending : HostOutcome::Failed;
    }
    if (!_session.HasColorEdit())
    {
        _colorCommitError = 0;
        return HostOutcome::Succeeded;
    }
    // The caller selects the oldest accepted input before snapshotting. An unrelated barrier is not admission.
    _colorCommitStamp = _historyWork.Begin(_session.DocumentSerial(), ordinal, HistoryWorkKind::Commit);
    if (!_colorCommitStamp)
    {
        _colorCommitOutcome = HostOutcome::Failed;
        _colorCommitError = E_UNEXPECTED;
        return HostOutcome::Failed;
    }
    _colorCommitError = 0;
    DropDrag<HueTargetDrag>();
    if (_session.AdjustmentEditingID())
    {
        // Adjustment metadata and history are prepared on the admitted worker; this target check preserves
        // immediate stale classification without constructing the input or a history snapshot.
        const auto& document = _session.Document();
        const auto& original = _session.AdjustmentOriginal();
        const Guid id = _session.AdjustmentEditingID().value();
        const bool validTarget =
            document && original &&
            std::any_of(document->layers.begin(), document->layers.end(), [&](const ImageLayer& layer)
                        { return layer.id == id && layer.adjustment && layer.adjustment->kind == original->kind; });
        if (!validTarget)
        {
            (void)_historyWork.Finish(_colorCommitStamp.value());
            _colorCommitStamp.reset();
            _colorCommitOutcome = HostOutcome::Stale;
            _colorCommitError = E_ABORT;
            DocumentChanged();
            return _colorCommitOutcome;
        }
        return StartAdjustmentClosure(ordinal, true);
    }
    std::int32_t status = E_INVALIDARG;
    try
    {
        const auto& edit = _session.PendingColorEdit();
        const auto preparation =
            edit ? ColorEditCommitMemoryBytes(edit.value(), _session.ColorEditSequence()) : std::nullopt;
        const auto history = _session.history.EditMemoryBytes(_session.Document());
        const auto held = _session.history.RetirementMemoryBytes(_session.Document());
        const auto panel = edit ? ColorEditHeldMemoryBytes(edit.value()) : 0;
        std::uint64_t memory = preparation.value_or(0);
        std::uint64_t retirement = held;
        const auto add = [](std::uint64_t& value, std::uint64_t more) noexcept
        {
            if (more > (std::numeric_limits<std::uint64_t>::max)() - value)
                return false;
            value += more;
            return true;
        };
        bool valid = preparation && history && held && panel && add(memory, history) &&
                     add(memory, sizeof(ColorCommitJob) + sizeof(ColorCommitPayload)) && add(retirement, panel);
        for (const auto& channel : _session.filterSettings.curves.channels)
        {
            const auto capacity = static_cast<std::uint64_t>(channel.capacity());
            valid = valid && capacity <= (std::numeric_limits<std::uint64_t>::max)() / sizeof(CurvePoint) &&
                    add(retirement, capacity * sizeof(CurvePoint));
        }
        valid = valid && memory <= (std::numeric_limits<std::uint64_t>::max)() / 2 && add(retirement, memory * 2);
        if (valid)
        {
            const auto serial = _session.DocumentSerial();
            if (_colorWorkTestGate)
                _colorWorkTestGate->reservationBytes = memory;
            std::unique_ptr<Job> job = std::make_unique<ColorCommitJob>(
                *this, _colorCommitStamp.value(), edit->openingSequence, _session.ColorEditSequence(), retirement);
            JobTicket ticket;
            status = _jobs.Submit(job, {JobClass::Commit, serial, memory}, ticket);
            if (status >= 0)
            {
                _colorCommitTicket = ticket;
                if (_uiBenchmark)
                    NoteUiCancellationCommitAdmitted(ticket, edit.value().openingSequence);
            }
        }
    }
    catch (const std::bad_alloc&)
    {
        // Owning construction/admission boundary. The caller fails this entire request; no job was admitted.
        status = E_OUTOFMEMORY;
    }
    if (status < 0)
    {
        (void)_historyWork.Finish(_colorCommitStamp.value());
        _colorCommitStamp.reset();
        _colorCommitOutcome = HostOutcome::Failed;
        _colorCommitError = status;
        return _colorCommitOutcome;
    }
    _colorCommitOutcome = HostOutcome::Pending;
    (void)_colorPreviews.Close();
    _colorMailboxOpen = false;
    if (_colorPreviewTicket)
    {
        (void)_jobs.Cancel(_colorPreviewTicket.value());
        // The cancelled preview ticket is retained until its exact acknowledgement.
    }
    // UI synchronization is outside the failure boundary. An admitted job keeps its barrier through apply/drop.
    // The opening histogram remains independent and may finish without changing settings.
    DocumentChanged();
    return HostOutcome::Pending;
}

void Application::CancelColorCommit() noexcept
{
    if (_colorCommitStamp)
    {
        (void)_historyWork.RequestCancel();
        if (_colorCommitTicket)
        {
            const auto original = _colorCommitTicket.value();
            const auto begin = _uiBenchmark ? BeginUiCancellationStop(original) : -1;
            const bool accepted = _jobs.Cancel(original);
            if (_uiBenchmark)
                NoteUiCancellationToken(original, accepted, begin);
        }
        // A late successful worker still drops. Keep the barrier until its UI completion acknowledges that drop.
    }
}

void Application::CompleteColorCommit(HistoryWorkStamp stamp, std::uint64_t opening, ColorEditJobResult& result,
                                      JobResult completion, PreparedColorEdit* publication,
                                      std::int32_t captureError) noexcept
{
    if (!_historyWork.IsCurrent(stamp) || _colorCommitStamp != stamp)
    {
        return;
    }
    HostOutcome outcome = HostOutcome::Stale;
    const auto& edit = _session.PendingColorEdit();
    const bool samePanel = !_shutdownStarted && edit && edit->openingSequence == opening &&
                           result.token.documentSerial == _session.DocumentSerial() &&
                           result.token.sequence == _session.ColorEditSequence();
    if (_historyWork.CancelRequested(stamp) || completion.status == JobStatus::Cancelled)
    {
        outcome = HostOutcome::Cancelled;
    }
    else if (captureError < 0 && !_shutdownStarted && edit && edit->openingSequence == opening)
    {
        // Failed capture has not produced a complete input/result. Fail this request locally and retain the
        // unchanged panel; never allocate an artificial result or partially publish history to report it.
        outcome = captureError == E_ABORT ? HostOutcome::Stale : HostOutcome::Failed;
    }
    else if (samePanel)
    {
        if (completion.status != JobStatus::Finished)
        {
            result.status = ColorEditJobStatus::Failed;
        }
        const bool validTarget = _session.CanApplyJobResult(result.token, result.token.sequence) &&
                                 (result.status != ColorEditJobStatus::Ready ||
                                  (publication && _session.IsPreparedColorEditCurrent(*publication)));
        const bool changed = result.status == ColorEditJobStatus::Ready
                                 ? (publication && _session.ApplyPreparedColorEditResult(*publication))
                                 : _session.ApplyColorEditResult(result);
        outcome = !validTarget ? HostOutcome::Stale
                  : changed || (result.status == ColorEditJobStatus::Unchanged && !_session.HasColorEdit())
                      ? HostOutcome::Succeeded
                  : result.status == ColorEditJobStatus::Cancelled ? HostOutcome::Cancelled
                                                                   : HostOutcome::Failed;
    }
    // Result validation, apply/drop and the history transaction precede release of the shared ordering barrier.
    (void)_historyWork.Finish(stamp);
    _colorCommitStamp.reset();
    _colorCommitTicket.reset();
    _colorCommitOutcome = outcome;
    _colorCommitError = outcome == HostOutcome::Failed ? (captureError < 0       ? captureError
                                                          : completion.error < 0 ? completion.error
                                                                                 : E_FAIL)
                        : outcome == HostOutcome::Cancelled || outcome == HostOutcome::Stale ? E_ABORT
                                                                                             : 0;
    ResumeHostHistory(stamp, outcome, _colorCommitError);
    DocumentChanged();
    if (outcome == HostOutcome::Failed)
    {
        if (_session.TakeBudgetRefusal())
        {
            ReportBudgetRefusal();
        }
        else
        {
            QueueColorFailure();
        }
    }
}

int Application::RunColorCommitSelfTest() noexcept
{
    const auto fail = [](const wchar_t* message) noexcept
    {
        SelfTestFailure(message);
        return 5;
    };
    const auto settled = [this]() noexcept
    { return SettleJobs(_jobs, 10000, DispatchMessageThunk, this) == SettleResult::Settled; };
    CancelColorEditForTest();
    if (!settled())
    {
        return fail(L"The ordered color commit fixture could not settle earlier work.\n");
    }
    constexpr std::array<std::uint8_t, 16> pixels{0,   0,   0,   255, 64,  64,  64,  255,
                                                  128, 128, 128, 255, 255, 255, 255, 255};
    const Raster source = Raster::Make(4, 1, RasterFormat::Bgra8888, pixels.data(), pixels.size());
    DocumentFixture documentFixture;
    documentFixture.id = Guid::Generate();
    documentFixture.width = 4;
    documentFixture.height = 1;
    ImageLayer layer = ImageLayer::Blank(L"Ordered color work", Vec2{4, 1});
    const Guid active = layer.id;
    layer.asset = RasterAsset{source, source, layer.name};
    documentFixture.layers.push_back(std::move(layer));
    ReplaceDocument(std::move(documentFixture).Seal(), active, {});
    const auto cleanup = wil::scope_exit(
        [&]() noexcept
        {
            if (_colorWorkTestGate)
            {
                (void)SetEvent(_colorWorkTestGate->release.get());
            }
            CancelColorEditForTest();
            _colorWorkTestGate.reset();
            if (!settled())
            {
                SelfTestFailure(L"Ordered color jobs did not join at their harness failure boundary.\n");
                std::_Exit(5);
            }
        });
    const auto gate = [this]()
    {
        _colorWorkTestGate = std::make_shared<ColorWorkTestGate>();
        _colorWorkTestGate->kind = ColorWorkTestKind::Commit;
        return _colorWorkTestGate->entered && _colorWorkTestGate->release && _colorWorkTestGate->computed;
    };
    const auto start = [this]() noexcept
    {
        const auto ordinal = _historyWork.NextOrdinal();
        return ordinal ? StartColorCommit(ordinal.value()) : HostOutcome::Failed;
    };
    if (!gate() || !BeginColorEdit(ChromeHost::kCommandLevels))
    {
        return fail(L"The ordered Levels commit did not open.\n");
    }
    OnAdjustAction(AdjustAction::SetInputGamma, 2);
    if (start() != HostOutcome::Pending ||
        !WaitForColorWorkTestEvent(_jobs, _colorWorkTestGate->entered.get(), DispatchMessageThunk, this))
    {
        return fail(L"The immutable color commit did not reach its worker gate.\n");
    }
    const auto stamp = _colorCommitStamp.value();
    const int beforeHistory = _session.history.UndoCount();
    const auto beforeSequence = _session.ColorEditSequence();
    OnAdjustAction(AdjustAction::SetInputGamma, 3);
    OnAdjustAction(AdjustAction::Apply, 0);
    _viewport.followsFit = false;
    _viewport.zoom = 1;
    OnCommand(ChromeHost::kCommandZoomIn);
    const auto anotherOrdinal = _historyWork.NextOrdinal();
    if (!_historyWork.Busy() || !ColorCommitBusy() || _session.ActiveLayer()->asset->image != source ||
        _session.history.UndoCount() != beforeHistory || _session.ColorEditSequence() != beforeSequence ||
        _session.PendingColorEdit()->settings.levels.ranges[0].gamma != 2 || _viewport.zoom <= 1 || !anotherOrdinal ||
        StartColorCommit(anotherOrdinal.value()) != HostOutcome::Failed || !_historyWork.IsCurrent(stamp))
    {
        return fail(
            L"A gated commit mutated history, admitted a competing ordinal, or blocked/froze the wrong input.\n");
    }
    (void)SetEvent(_colorWorkTestGate->release.get());
    if (!settled() || _colorCommitOutcome != HostOutcome::Succeeded || ColorCommitBusy() || _historyWork.Busy() ||
        _session.HasColorEdit() || _session.history.UndoCount() != beforeHistory + 1)
    {
        return fail(L"The ordered color result did not apply exactly once before releasing its barrier.\n");
    }
    std::array<std::uint8_t, 16> actual{};
    const Raster& result = _session.ActiveLayer()->asset->image;
    result.ReadRegion(0, 0, 4, 1, actual.data(), 16, 0);
    constexpr std::array<std::uint8_t, 16> expected{0,   0,   0,   255, 128, 128, 128, 255,
                                                    181, 181, 181, 255, 255, 255, 255, 255};
    if (actual != expected)
    {
        return fail(L"The committed Gamma 2 pixels differed from the independent square-root reference.\n");
    }
    _session.Undo();
    DocumentChanged();
    if (!gate() || !BeginColorEdit(ChromeHost::kCommandLevels))
    {
        return fail(L"The completed-before-Cancel commit fixture did not open.\n");
    }
    OnAdjustAction(AdjustAction::SetInputGamma, 2);
    if (start() != HostOutcome::Pending ||
        !WaitForColorWorkTestEvent(_jobs, _colorWorkTestGate->entered.get(), DispatchMessageThunk, this))
    {
        return fail(L"The late successful commit did not start.\n");
    }
    (void)SetEvent(_colorWorkTestGate->release.get());
    if (WaitForSingleObject(_colorWorkTestGate->computed.get(), 10000) != WAIT_OBJECT_0)
    {
        return fail(L"The old commit did not compute before the Cancel/reopen race.\n");
    }
    const auto oldOpening = _session.PendingColorEdit()->openingSequence;
    const int cancelledHistory = _session.history.UndoCount();
    // Direct engine lifetime-invalidating reference fixture. Production Cancel/reopen has its own selected
    // receipt tests, including computed-before-acknowledgement OK, in RunColorCancellationSelfTest.
    CancelColorCommit();
    _session.CancelColorEdit();
    DocumentChanged();
    if (!_historyWork.Busy())
    {
        return fail(L"Cancel released the barrier before the old result dropped.\n");
    }
    _session.BeginColorEdit(FilterKind::Levels, true);
    DocumentChanged();
    if (!settled() || _colorCommitOutcome != HostOutcome::Cancelled || _historyWork.Busy() ||
        !_session.HasColorEdit() || _session.PendingColorEdit()->openingSequence == oldOpening ||
        _session.ActiveLayer()->asset->image != source || _session.history.UndoCount() != cancelledHistory)
    {
        return fail(L"Late successful commit pixels crossed a cancelled panel lifetime.\n");
    }
    CancelColorEditForTest();
    if (!gate() || !BeginColorEdit(ChromeHost::kCommandLevels) || start() != HostOutcome::Pending ||
        !WaitForColorWorkTestEvent(_jobs, _colorWorkTestGate->entered.get(), DispatchMessageThunk, this))
    {
        return fail(L"The identity/stale commit fixture did not start.\n");
    }
    EditLayerRecordsForTest(_session, [](DocumentFixture& fixture) { fixture.layers.front().transform.origin.x += 1; });
    (void)SetEvent(_colorWorkTestGate->release.get());
    if (!settled() || _colorCommitOutcome != HostOutcome::Stale || _historyWork.Busy() || _session.HasColorEdit() ||
        _session.ActiveLayer()->asset->image != source || _session.history.UndoCount() != cancelledHistory ||
        !_colorWorkTestGate->unchangedCommit.load())
    {
        return fail(L"A stale identity result was mistaken for a successful settlement.\n");
    }
    EditLayerRecordsForTest(_session, [](DocumentFixture& fixture) { fixture.layers.front().transform.origin.x = 0; });
    if (!gate() || !BeginColorEdit(ChromeHost::kCommandLevels) || start() != HostOutcome::Pending)
    {
        return fail(L"The current identity commit fixture did not start.\n");
    }
    (void)SetEvent(_colorWorkTestGate->release.get());
    if (!settled() || _colorCommitOutcome != HostOutcome::Succeeded || _session.HasColorEdit() ||
        _session.history.UndoCount() != cancelledHistory)
    {
        return fail(L"A valid unchanged commit did not settle without a history entry.\n");
    }
    if (!gate() || !BeginColorEdit(ChromeHost::kCommandLevels))
    {
        return fail(L"The failed runner commit fixture did not open.\n");
    }
    _colorWorkTestGate->releasedResult = {JobStatus::Failed, E_OUTOFMEMORY};
    OnAdjustAction(AdjustAction::SetInputGamma, 2);
    if (start() != HostOutcome::Pending)
    {
        return fail(L"The failed runner commit fixture did not start.\n");
    }
    (void)SetEvent(_colorWorkTestGate->release.get());
    if (!settled() || _colorCommitOutcome != HostOutcome::Failed || _colorCommitError != E_OUTOFMEMORY ||
        _historyWork.Busy() || !_session.HasColorEdit() || !_session.PendingColorEdit()->commitFailed ||
        _session.ActiveLayer()->asset->image != source || _session.history.UndoCount() != cancelledHistory)
    {
        return fail(L"A failed runner result lost its request identity or did not retain the panel safely.\n");
    }
    CancelColorEditForTest();
    if (!gate() || !BeginColorEdit(ChromeHost::kCommandLevels))
    {
        return fail(L"The throwing runner commit fixture did not open.\n");
    }
    _colorWorkTestGate->throwAllocationFailure = true;
    OnAdjustAction(AdjustAction::SetInputGamma, 2);
    if (start() != HostOutcome::Pending)
    {
        return fail(L"The throwing runner commit fixture did not start.\n");
    }
    (void)SetEvent(_colorWorkTestGate->release.get());
    if (!settled() || _colorCommitOutcome != HostOutcome::Failed || _colorCommitError != E_OUTOFMEMORY ||
        _historyWork.Busy() || !_session.HasColorEdit() || !_session.PendingColorEdit()->commitFailed ||
        _session.ActiveLayer()->asset->image != source || _session.history.UndoCount() != cancelledHistory)
    {
        return fail(L"The scheduler's actual bad_alloc translation did not retain the commit request safely.\n");
    }
    CancelColorEditForTest();
    _session.SelectAll();
    const int failedHistory = _session.history.UndoCount();
    if (!gate() || !BeginColorEdit(ChromeHost::kCommandContentAwareFill) || start() != HostOutcome::Pending)
    {
        return fail(L"The actual donorless fill commit fixture did not start.\n");
    }
    (void)SetEvent(_colorWorkTestGate->release.get());
    if (!settled() || _colorCommitOutcome != HostOutcome::Failed || _historyWork.Busy() || !_session.HasColorEdit() ||
        !_session.PendingColorEdit()->previewFailed || _session.ActiveLayer()->asset->image != source ||
        _session.history.UndoCount() != failedHistory)
    {
        return fail(L"A failed actual fill committed pixels, lost its panel, or retained the history barrier.\n");
    }
    CancelColorEditForTest();
    _session.Deselect();
    if (!gate() || !BeginColorEdit(ChromeHost::kCommandLevels))
    {
        return fail(L"The document replacement commit fixture did not open.\n");
    }
    OnAdjustAction(AdjustAction::SetInputGamma, 2);
    if (start() != HostOutcome::Pending ||
        !WaitForColorWorkTestEvent(_jobs, _colorWorkTestGate->entered.get(), DispatchMessageThunk, this))
    {
        return fail(L"The document replacement commit fixture did not start.\n");
    }
    (void)SetEvent(_colorWorkTestGate->release.get());
    if (WaitForSingleObject(_colorWorkTestGate->computed.get(), 10000) != WAIT_OBJECT_0)
    {
        return fail(L"The old document result did not compute before replacement.\n");
    }
    const auto oldSerial = _session.DocumentSerial();
    const Raster replacement = SolidRaster(4, 1, 0xFF112233u);
    CanvasDocument reopened = _session.Document().value(); // Deliberately reuse its persisted document/layer IDs.
    reopened.selection.reset();
    EditDocumentFixture(
        reopened, [&](DocumentFixture& fixture)
        { fixture.layers.front().asset = RasterAsset{replacement, replacement, fixture.layers.front().name}; });
    ReplaceDocument(std::move(reopened), active, {});
    if (!settled() || _colorCommitOutcome != HostOutcome::Cancelled || _historyWork.Busy() ||
        _session.DocumentSerial() == oldSerial || _session.ActiveLayer()->asset->image != replacement ||
        _session.history.UndoCount() != 0 || _session.HasColorEdit())
    {
        return fail(L"An old completed commit crossed a document replacement with reused persisted IDs.\n");
    }

    // Every history allocation boundary fails inside the real worker runner before owner publication. A seeded
    // redo entry and complete literal source survive; release must include the captured history/panel graph.
    _session.SetOpacity(0.75);
    _session.Undo();
    using HistoryStage = DocumentHistory::PrepareStage;
    constexpr std::array stages{HistoryStage::BeforeSnapshot, HistoryStage::AfterSnapshot, HistoryStage::Entry,
                                HistoryStage::StackGrowth, HistoryStage::TrimIndex};
    for (const auto stage : stages)
    {
        if (!gate() || !BeginColorEdit(ChromeHost::kCommandLevels))
            return fail(L"A prepared color history fault fixture could not open its panel.\n");
        OnAdjustAction(AdjustAction::SetInputGamma, 2);
        if (!settled())
            return fail(L"A prepared color history fault fixture could not settle its opening preview.\n");
        const auto revision = _session.history.CurrentRevision();
        const auto historyCount = _session.history.UndoCount();
        const auto redoName = _session.history.RedoName();
        const auto sourceImage = _session.ActiveLayer()->asset->image;
        _colorWorkTestGate->historyFailure = stage;
        if (start() != HostOutcome::Pending)
            return fail(L"A prepared color history fault fixture was not admitted.\n");
        (void)SetEvent(_colorWorkTestGate->release.get());
        if (!settled() || _colorCommitOutcome != HostOutcome::Failed || _colorCommitError != E_OUTOFMEMORY ||
            _historyWork.Busy() || !_session.HasColorEdit() || !_session.PendingColorEdit()->commitFailed ||
            _session.ActiveLayer()->asset->image != sourceImage || _session.history.CurrentRevision() != revision ||
            _session.history.UndoCount() != historyCount || !_session.history.CanRedo() ||
            _session.history.RedoName() != redoName || _colorWorkTestGate->historyFaultHits.load() != 1 ||
            !_colorWorkTestGate->captureReservationHeld.load() || !_colorWorkTestGate->payloadRetired.load() ||
            !_colorWorkTestGate->payloadRetiredOffOwner.load() || _jobs.ReservedMemoryBytes() != 0)
            return fail(L"A color history allocation failure changed pixels/history or lost its release owner.\n");
        std::fprintf(stderr, "PASS color-history-host.stage=%u\n", static_cast<unsigned int>(stage));
        CancelColorEditForTest();
    }
    if (!gate() || !BeginColorEdit(ChromeHost::kCommandLevels))
        return fail(L"The admitted color capture refusal fixture could not open its panel.\n");
    OnAdjustAction(AdjustAction::SetInputGamma, 2);
    if (!settled())
        return fail(L"The admitted color capture refusal fixture could not settle its preview.\n");
    const auto captureRevision = _session.history.CurrentRevision();
    const auto captureSequence = _session.ColorEditSequence();
    const auto captureImage = _session.ActiveLayer()->asset->image;
    _colorWorkTestGate->throwCaptureFailure = true;
    if (start() != HostOutcome::Pending || !settled() || _colorCommitOutcome != HostOutcome::Failed ||
        _colorCommitError != E_OUTOFMEMORY || _historyWork.Busy() || !_session.HasColorEdit() ||
        _session.PendingColorEdit()->commitFailed || _session.ColorEditSequence() != captureSequence ||
        _session.ActiveLayer()->asset->image != captureImage || _session.history.CurrentRevision() != captureRevision ||
        !_session.history.CanRedo() || !_colorWorkTestGate->captureReservationHeld.load() ||
        !_colorWorkTestGate->payloadRetired.load() || !_colorWorkTestGate->payloadRetiredOffOwner.load() ||
        _jobs.ReservedMemoryBytes() != 0)
        return fail(L"Admitted color capture refusal did not retain the unchanged panel/history and owners.\n");
    std::fprintf(stderr, "PASS color-history-host.capture-refusal\n");
    CancelColorEditForTest();

    if (!gate() || !BeginColorEdit(ChromeHost::kCommandLevels))
        return fail(L"The stale color history epoch fixture could not open its panel.\n");
    OnAdjustAction(AdjustAction::SetInputGamma, 2);
    if (!settled() || start() != HostOutcome::Pending ||
        !WaitForColorWorkTestEvent(_jobs, _colorWorkTestGate->entered.get(), DispatchMessageThunk, this))
        return fail(L"The stale color history epoch fixture did not reach its admitted worker.\n");
    const auto staleRevision = _session.history.CurrentRevision();
    const auto staleImage = _session.ActiveLayer()->asset->image;
    _session.history.Begin(L"Epoch only", _session.Document(), _session.ActiveLayerID());
    _session.history.End(_session.Document(), _session.ActiveLayerID());
    (void)SetEvent(_colorWorkTestGate->release.get());
    if (!settled() || _colorCommitOutcome != HostOutcome::Stale || _historyWork.Busy() || !_session.HasColorEdit() ||
        _session.PendingColorEdit()->commitFailed || _session.ActiveLayer()->asset->image != staleImage ||
        _session.history.CurrentRevision() != staleRevision || !_session.history.CanRedo() ||
        !_colorWorkTestGate->payloadRetiredOffOwner.load())
        return fail(L"An old prepared color publication crossed a changed history epoch.\n");
    std::fprintf(stderr, "PASS color-history-host.stale-history-epoch\n");
    CancelColorEditForTest();
    if (!gate())
        return fail(L"The adjustment metadata fixture could not create its worker gate.\n");
    _session.AddAdjustment(AdjustmentKind::Levels);
    const Guid adjustmentId = _session.ActiveLayerID().value();
    _session.SetAdjustmentEditingID(adjustmentId);
    if (!_session.BeginAdjustmentEditing(adjustmentId, replacement, true))
    {
        return fail(L"The adjustment metadata settlement fixture did not open.\n");
    }
    DocumentChanged();
    OnAdjustAction(AdjustAction::SetInputGamma, 2);
    const int metadataHistory = _session.history.UndoCount();
    if (start() != HostOutcome::Pending || !_historyWork.Busy() || !ColorCommitBusy() || !_session.HasColorEdit())
        return fail(L"Adjustment metadata closure was not admitted before its owner capture.\n");
    (void)SetEvent(_colorWorkTestGate->release.get());
    if (!settled() || _colorCommitOutcome != HostOutcome::Succeeded || _historyWork.Busy() || ColorCommitBusy() ||
        _session.HasColorEdit() || _session.history.UndoCount() != metadataHistory + 1 ||
        _session.ActiveLayer()->adjustment->levels.ranges[0].gamma != 2 ||
        _session.Document()->layers.front().asset->image != replacement ||
        !_colorWorkTestGate->captureReservationHeld.load() || !_colorWorkTestGate->payloadRetiredOffOwner.load() ||
        _jobs.ReservedMemoryBytes() != 0)
    {
        return fail(L"Adjustment metadata settlement ran destructive work or did not record exactly one edit.\n");
    }
    _session.SetAdjustmentEditingID(adjustmentId);
    if (!_session.BeginAdjustmentEditing(adjustmentId, replacement, true))
    {
        return fail(L"The missing adjustment target fixture did not open.\n");
    }
    EditLayerRecordsForTest(
        _session, [&](DocumentFixture& fixture)
        { std::erase_if(fixture.layers, [&](const ImageLayer& item) { return item.id == adjustmentId; }); });
    const int staleMetadataHistory = _session.history.UndoCount();
    if (start() != HostOutcome::Stale || _historyWork.Busy() || ColorCommitBusy() || !_session.HasColorEdit() ||
        _session.history.UndoCount() != staleMetadataHistory || _session.Document()->layers.size() != 1 ||
        _session.Document()->layers.front().asset->image != replacement)
    {
        return fail(L"A missing adjustment layer was reported as successful metadata settlement.\n");
    }
    return 0;
}
