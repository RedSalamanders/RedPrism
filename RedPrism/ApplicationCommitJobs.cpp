#include "Application.h"

#include "ChromeHost.h"
#include "ColorWorkTestGate.h"
#include "resource.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <memory>
#include <new>
#include <utility>

// This engine is qualified before activation by ApplyColorEdit. The production OK/write callers switch to it
// together with owned continuations in P1.1c.3c; acceptance can never be mistaken for a completed operation.
class Application::ColorCommitJob final : public Job
{
  public:
    ColorCommitJob(Application& owner, HistoryWorkStamp stamp, ColorEditJobInput input)
        : _owner(owner), _stamp(stamp), _input(std::move(input)), _gate(owner._colorWorkTestGate)
    {
        // Preserve request identity even if the runner fails before producing a result.
        _result.token = _input.token;
        _result.kind = _input.edit.kind;
        _result.settings = _input.edit.settings;
    }
    JobResult Run(JobContext& context) override
    {
        if (const auto gate = PassColorWorkTestGate(_gate, ColorWorkTestKind::Commit, context);
            gate.status != JobStatus::Finished)
        {
            return gate;
        }
        if (_gate && _gate->kind == ColorWorkTestKind::Commit && _gate->throwAllocationFailure)
        {
            throw std::bad_alloc{}; // harness-only: reaches the actual scheduler runner's translation boundary
        }
        _result = RunColorEditJob(_input, context.executor, context.cancellation);
        if (_gate && _gate->kind == ColorWorkTestKind::Commit)
        {
            _gate->unchangedCommit.store(_result.status == ColorEditJobStatus::Unchanged);
            (void)SetEvent(_gate->computed.get());
        }
        return {};
    }
    void Completed(JobResult completion) noexcept override
    {
        _owner.CompleteColorCommit(_stamp, _input.edit.openingSequence, _result, completion);
    }

  private:
    Application& _owner; // UI completion only. Run cannot access the mutable owner.
    const HistoryWorkStamp _stamp;
    const ColorEditJobInput _input;
    ColorEditJobResult _result;
    const std::shared_ptr<ColorWorkTestGate> _gate;
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
        // Adjustment-layer OK settles live metadata/history. It does not run a destructive image kernel.
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
        _session.CommitColorEdit();
        (void)_historyWork.Finish(_colorCommitStamp.value());
        _colorCommitStamp.reset();
        _colorCommitOutcome = _session.HasColorEdit() ? HostOutcome::Failed : HostOutcome::Succeeded;
        _colorCommitError = _colorCommitOutcome == HostOutcome::Failed ? E_FAIL : 0;
        DocumentChanged();
        return _colorCommitOutcome;
    }
    std::int32_t status = E_INVALIDARG;
    try
    {
        auto input = _session.PrepareColorEditCommit();
        const auto memory = input ? ColorEditJobMemoryBytes(input.value()) : std::nullopt;
        if (input && memory)
        {
            const auto serial = input->token.documentSerial;
            std::unique_ptr<Job> job =
                std::make_unique<ColorCommitJob>(*this, _colorCommitStamp.value(), std::move(input.value()));
            JobTicket ticket;
            status = _jobs.Submit(job, {JobClass::Commit, serial, memory.value()}, ticket);
            if (status >= 0)
            {
                _colorCommitTicket = ticket;
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
        _colorPreviewTicket.reset();
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
            (void)_jobs.Cancel(_colorCommitTicket.value());
        }
        // A late successful worker still drops. Keep the barrier until its UI completion acknowledges that drop.
    }
}

void Application::CompleteColorCommit(HistoryWorkStamp stamp, std::uint64_t opening, ColorEditJobResult& result,
                                      JobResult completion) noexcept
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
    else if (samePanel)
    {
        if (completion.status != JobStatus::Finished)
        {
            result.status = ColorEditJobStatus::Failed;
        }
        const bool validTarget = _session.CanApplyJobResult(result.token, result.token.sequence);
        const bool changed = _session.ApplyColorEditResult(result);
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
    _colorCommitError = outcome == HostOutcome::Failed ? (completion.error < 0 ? completion.error : E_FAIL)
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
    CancelColorEdit();
    if (!settled())
    {
        return fail(L"The ordered color commit fixture could not settle earlier work.\n");
    }
    constexpr std::array<std::uint8_t, 16> pixels{0,   0,   0,   255, 64,  64,  64,  255,
                                                  128, 128, 128, 255, 255, 255, 255, 255};
    const Raster source = Raster::Make(4, 1, RasterFormat::Bgra8888, pixels.data(), pixels.size());
    CanvasDocument document;
    document.id = Guid::Generate();
    document.width = 4;
    document.height = 1;
    ImageLayer layer = ImageLayer::Blank(L"Ordered color work", Vec2{4, 1});
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
            CancelColorEdit();
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
        WaitForSingleObject(_colorWorkTestGate->entered.get(), 10000) != WAIT_OBJECT_0)
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
        WaitForSingleObject(_colorWorkTestGate->entered.get(), 10000) != WAIT_OBJECT_0)
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
    CancelColorEdit();
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
    CancelColorEdit();
    if (!gate() || !BeginColorEdit(ChromeHost::kCommandLevels) || start() != HostOutcome::Pending ||
        WaitForSingleObject(_colorWorkTestGate->entered.get(), 10000) != WAIT_OBJECT_0)
    {
        return fail(L"The identity/stale commit fixture did not start.\n");
    }
    _session.EditForTest([&](CanvasDocument& value) { value.layers.front().transform.origin.x += 1; });
    (void)SetEvent(_colorWorkTestGate->release.get());
    if (!settled() || _colorCommitOutcome != HostOutcome::Stale || _historyWork.Busy() || _session.HasColorEdit() ||
        _session.ActiveLayer()->asset->image != source || _session.history.UndoCount() != cancelledHistory ||
        !_colorWorkTestGate->unchangedCommit.load())
    {
        return fail(L"A stale identity result was mistaken for a successful settlement.\n");
    }
    _session.EditForTest([](CanvasDocument& value) { value.layers.front().transform.origin.x = 0; });
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
    CancelColorEdit();
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
    CancelColorEdit();
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
    CancelColorEdit();
    _session.Deselect();
    if (!gate() || !BeginColorEdit(ChromeHost::kCommandLevels))
    {
        return fail(L"The document replacement commit fixture did not open.\n");
    }
    OnAdjustAction(AdjustAction::SetInputGamma, 2);
    if (start() != HostOutcome::Pending ||
        WaitForSingleObject(_colorWorkTestGate->entered.get(), 10000) != WAIT_OBJECT_0)
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
    reopened.layers.front().asset = RasterAsset{replacement, replacement, reopened.layers.front().name};
    ReplaceDocument(std::move(reopened), active, {});
    if (!settled() || _colorCommitOutcome != HostOutcome::Cancelled || _historyWork.Busy() ||
        _session.DocumentSerial() == oldSerial || _session.ActiveLayer()->asset->image != replacement ||
        _session.history.UndoCount() != 0 || _session.HasColorEdit())
    {
        return fail(L"An old completed commit crossed a document replacement with reused persisted IDs.\n");
    }
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
    if (start() != HostOutcome::Succeeded || _historyWork.Busy() || ColorCommitBusy() || _session.HasColorEdit() ||
        _session.history.UndoCount() != metadataHistory + 1 ||
        _session.ActiveLayer()->adjustment->levels.ranges[0].gamma != 2 ||
        _session.Document()->layers.front().asset->image != replacement)
    {
        return fail(L"Adjustment metadata settlement ran destructive work or did not record exactly one edit.\n");
    }
    _session.SetAdjustmentEditingID(adjustmentId);
    if (!_session.BeginAdjustmentEditing(adjustmentId, replacement, true))
    {
        return fail(L"The missing adjustment target fixture did not open.\n");
    }
    _session.EditForTest(
        [&](CanvasDocument& value)
        { std::erase_if(value.layers, [&](const ImageLayer& item) { return item.id == adjustmentId; }); });
    const int staleMetadataHistory = _session.history.UndoCount();
    if (start() != HostOutcome::Stale || _historyWork.Busy() || ColorCommitBusy() || !_session.HasColorEdit() ||
        _session.history.UndoCount() != staleMetadataHistory || _session.Document()->layers.size() != 1 ||
        _session.Document()->layers.front().asset->image != replacement)
    {
        return fail(L"A missing adjustment layer was reported as successful metadata settlement.\n");
    }
    return 0;
}
