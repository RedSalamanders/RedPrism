#include "Application.h"

#include "../Tests/Support/DocumentFixture.h"

#include "AdjustmentClosureWork.h"
#include "ColorWorkTestGate.h"

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
struct AdjustmentClosurePayload final : DeferredReleaseNode
{
    std::optional<AdjustmentClosureInput> input;
    std::optional<CanvasDocument> source;
    DocumentHistory::StateView history;
    std::optional<PreparedAdjustmentClosure> publication;
    std::shared_ptr<ColorWorkTestGate> gate;
    DWORD ownerThread = 0;
    AdjustmentClosurePayload() noexcept
    {
        bytes = kDeferredReleaseThreshold;
        destroy = [](DeferredReleaseNode* node) noexcept
        { const std::unique_ptr<AdjustmentClosurePayload> owned(static_cast<AdjustmentClosurePayload*>(node)); };
    }
    ~AdjustmentClosurePayload()
    {
        if (gate)
        {
            gate->payloadRetiredOffOwner.store(GetCurrentThreadId() != ownerThread);
            gate->payloadRetired.store(true);
        }
    }
};
#pragma warning(pop)

struct RetireAdjustmentClosure final
{
    void operator()(AdjustmentClosurePayload* payload) const noexcept
    {
        if (payload && !TryDeferRelease(*payload))
        {
            const std::unique_ptr<AdjustmentClosurePayload> owned(payload);
        }
    }
};
} // namespace

class Application::AdjustmentClosureJob final : public Job
{
  public:
    AdjustmentClosureJob(Application& owner, HistoryWorkStamp stamp, bool commit, std::uint64_t retirement) noexcept
        : _owner(owner), _stamp(stamp), _commit(commit), _retirement(retirement),
          _opening(owner._session.PendingColorEdit()->openingSequence), _sequence(owner._session.ColorEditSequence()),
          _generation(owner._session.Generation()), _layer(owner._session.AdjustmentEditingID().value()),
          _gate(owner._colorWorkTestGate), _ownerThread(GetCurrentThreadId())
    {
    }
    JobResult Run(JobContext& context) override
    {
        if (context.cancellation.IsCancelled())
            return {JobStatus::Cancelled};
        if (!_payload)
            return {}; // this admitted record reaches owner capture before any document/settings copy
        if (const auto gate = PassColorWorkTestGate(_gate, ColorWorkTestKind::Commit, context);
            gate.status != JobStatus::Finished)
            return gate;
        if (_gate && _gate->kind == ColorWorkTestKind::Commit && _gate->throwAllocationFailure)
            throw std::bad_alloc{};
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
        _payload->publication = DocumentSession::PrepareAdjustmentClosurePublication(
            std::move(_payload->input.value()), _payload->source, std::move(_payload->history), context.cancellation,
            _gate ? observer : DocumentHistory::PrepareObserver{});
        if (_gate && _gate->kind == ColorWorkTestKind::Commit)
            (void)SetEvent(_gate->computed.get());
        return _payload->publication                ? JobResult{}
               : context.cancellation.IsCancelled() ? JobResult{JobStatus::Cancelled}
                                                    : JobResult{JobStatus::Failed, E_FAIL};
    }
    bool DeferCompletion(JobResult result, JobTicket ticket) noexcept override
    {
        if (_payload || result.status != JobStatus::Finished || !Current() ||
            _owner._historyWork.CancelRequested(_stamp))
            return false;
        try
        {
            auto owned = std::make_unique<AdjustmentClosurePayload>();
            owned->bytes = (std::max)(_retirement, kDeferredReleaseThreshold);
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
            _payload->input = _owner._session.PrepareAdjustmentClosureInput(_commit);
            if (!_payload->input)
            {
                _captureError = E_ABORT;
                return false;
            }
            _payload->source = _owner._session.Document();
            _payload->history = _owner._session.history.CaptureState();
            if (_owner._jobs.Resume(ticket))
                return true;
            _captureError = E_UNEXPECTED;
        }
        catch (const std::bad_alloc&)
        {
            _captureError = E_OUTOFMEMORY; // admitted capture fails the selected operation before publication
        }
        return false;
    }
    void Completed(JobResult result) noexcept override
    {
        auto* publication = _payload && _payload->publication ? &_payload->publication.value() : nullptr;
        _owner.CompleteAdjustmentClosure(_stamp, Current(), result, publication, _captureError);
    }

  private:
    // Owner stages only. Run never reaches the mutable owner through this function.
    [[nodiscard]] bool Current() const noexcept
    {
        const auto& panel = _owner._session.PendingColorEdit();
        return !_owner._shutdownStarted && _owner._colorCommitStamp == _stamp &&
               _owner._historyWork.IsCurrent(_stamp) && _owner._session.DocumentSerial() == _stamp.documentSerial &&
               _owner._session.Generation() == _generation && _owner._session.ColorEditSequence() == _sequence &&
               _owner._session.AdjustmentEditingID() == std::optional(_layer) && panel &&
               panel->openingSequence == _opening;
    }
    Application& _owner;
    const HistoryWorkStamp _stamp;
    const bool _commit;
    const std::uint64_t _retirement, _opening, _sequence, _generation;
    const Guid _layer;
    const std::shared_ptr<ColorWorkTestGate> _gate;
    const DWORD _ownerThread;
    std::int32_t _captureError = 0;
    std::unique_ptr<AdjustmentClosurePayload, RetireAdjustmentClosure> _payload;
};

Application::HostOutcome Application::StartAdjustmentClosure(std::uint64_t ordinal, bool commit) noexcept
{
    std::int32_t status = E_INVALIDARG;
    try
    {
        const auto& panel = _session.PendingColorEdit();
        const auto& original = _session.AdjustmentOriginal();
        std::uint64_t memory = _session.history.EditMemoryBytes(_session.Document());
        // This complete session bound includes display, panel and original curves, including an original
        // curve larger than the currently previewed replacement. H2/H3 will cache the admission traversal.
        std::uint64_t retirement = _session.ImageImportRetirementMemoryBytes();
        const auto maximum = (std::numeric_limits<std::uint64_t>::max)();
        const auto add = [&](std::uint64_t& value, std::uint64_t count, std::uint64_t size) noexcept
        {
            if ((size && count > maximum / size) || count * size > maximum - value)
                return false;
            value += count * size;
            return true;
        };
        bool valid = _colorCommitStamp && _colorCommitStamp->ordinal == ordinal && panel && original &&
                     _session.AdjustmentEditingID() && memory && retirement &&
                     add(memory, 1,
                         sizeof(AdjustmentClosureJob) + sizeof(AdjustmentClosurePayload) +
                             4 * sizeof(AdjustmentClosureInput) + 4 * sizeof(PreparedAdjustmentClosure));
        if (panel)
            for (const auto& channel : panel->settings.curves.channels)
                valid = valid && add(memory, channel.capacity(), 4 * sizeof(CurvePoint));
        if (original)
            for (const auto& channel : original->curves.channels)
                valid = valid && add(memory, channel.capacity(), 8 * sizeof(CurvePoint));
        valid = valid && add(retirement, 2, memory);
        if (valid)
        {
            if (_colorWorkTestGate)
                _colorWorkTestGate->reservationBytes = memory;
            std::unique_ptr<Job> job =
                std::make_unique<AdjustmentClosureJob>(*this, _colorCommitStamp.value(), commit, retirement);
            JobTicket ticket;
            status = _jobs.Submit(job, {JobClass::Commit, _session.DocumentSerial(), memory}, ticket);
            if (status >= 0)
                _colorCommitTicket = ticket;
        }
    }
    catch (const std::bad_alloc&)
    {
        status = E_OUTOFMEMORY; // no job admitted; no document/history/panel mutation occurred
    }
    if (status < 0)
    {
        if (_colorCommitStamp)
            (void)_historyWork.Finish(_colorCommitStamp.value());
        _colorCommitStamp.reset();
        _colorCommitOutcome = HostOutcome::Failed;
        _colorCommitError = status;
        return HostOutcome::Failed;
    }
    _colorCommitOutcome = HostOutcome::Pending;
    _colorCommitError = 0;
    (void)_colorPreviews.Close();
    _colorMailboxOpen = false;
    if (_colorPreviewTicket)
    {
        (void)_jobs.Cancel(_colorPreviewTicket.value());
        // The cancelled preview ticket is retained until its exact acknowledgement.
    }
    DocumentChanged();
    return HostOutcome::Pending;
}

void Application::CompleteAdjustmentClosure(HistoryWorkStamp stamp, bool current, JobResult completion,
                                            PreparedAdjustmentClosure* publication, std::int32_t captureError) noexcept
{
    if (_colorCommitStamp != stamp || !_historyWork.IsCurrent(stamp))
        return;
    HostOutcome outcome =
        !current                                                                           ? HostOutcome::Stale
        : _historyWork.CancelRequested(stamp) || completion.status == JobStatus::Cancelled ? HostOutcome::Cancelled
        : captureError == E_ABORT                                                          ? HostOutcome::Stale
        : captureError < 0 || completion.status == JobStatus::Failed || !publication       ? HostOutcome::Failed
        : _session.ApplyPreparedAdjustmentClosureIfCurrent(*publication)                   ? HostOutcome::Succeeded
                                                                                           : HostOutcome::Stale;
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
        QueueColorFailure();
}

int Application::RunAdjustmentClosureSelfTest() noexcept
{
    const auto fail = [](const wchar_t* message) noexcept
    {
        SelfTestFailure(message);
        return 5;
    };
    const auto settle = [this]() noexcept
    { return SettleJobs(_jobs, 10000, DispatchMessageThunk, this) == SettleResult::Settled; };
    CancelColorEdit();
    if (!settle())
        return fail(L"Earlier work did not settle before adjustment closure qualification.\n");
    const auto cleanup = wil::scope_exit(
        [&]() noexcept
        {
            if (_colorWorkTestGate)
                (void)SetEvent(_colorWorkTestGate->release.get());
            CancelColorCommit();
            if (!settle())
                std::_Exit(5);
            _colorWorkTestGate.reset();
            _session.CancelColorEdit(); // reference fixture teardown after every admitted job has acknowledged
        });
    constexpr std::array<std::uint8_t, 16> bytes{0, 0, 0, 255, 64, 64, 64, 255, 128, 128, 128, 255, 255, 255, 255, 255};
    const Raster image = Raster::Make(4, 1, RasterFormat::Bgra8888, bytes.data(), bytes.size());
    enum class Mode
    {
        ChangedOn,
        ChangedOff,
        Noop,
        Cancel,
        Failure,
        Capture,
        Epoch,
        LateCancel,
        ImplicitDefaults
    };
    constexpr std::array kinds{AdjustmentKind::Hsv,      AdjustmentKind::Levels,      AdjustmentKind::Curves,
                               AdjustmentKind::Exposure, AdjustmentKind::GradientMap, AdjustmentKind::Grain};
    using Stage = DocumentHistory::PrepareStage;
    constexpr std::array stages{Stage::AdjustmentSnapshot, Stage::AfterSnapshot, Stage::Entry, Stage::StackGrowth,
                                Stage::TrimIndex};
    for (const auto kind : kinds)
    {
        for (const auto mode : {Mode::ChangedOn, Mode::ChangedOff, Mode::Noop, Mode::Cancel, Mode::Failure,
                                Mode::Capture, Mode::Epoch, Mode::LateCancel, Mode::ImplicitDefaults})
        {
            if (mode == Mode::ImplicitDefaults && kind != AdjustmentKind::Hsv && kind != AdjustmentKind::Exposure)
                continue;
            const auto attempts = mode == Mode::Failure ? stages.size() : std::size_t{1};
            for (std::size_t attempt = 0; attempt < attempts; ++attempt)
            {
                _session.CancelColorEdit(); // no worker is live at this fixture construction boundary
                DocumentFixture document;
                document.id = Guid::Generate();
                document.width = 4;
                document.height = 1;
                ImageLayer base = ImageLayer::Blank(L"Literal source", Vec2{4, 1});
                base.asset = RasterAsset{image, {}, base.name};
                const Guid baseID = base.id;
                document.layers.push_back(std::move(base));
                ReplaceDocument(std::move(document).Seal(), baseID, {});
                _session.AddAdjustment(kind);
                const Guid id = _session.ActiveLayerID().value();
                LayerAdjustment original = _session.ActiveLayer()->adjustment.value();
                if (mode != Mode::ImplicitDefaults)
                {
                    // True unchanged metadata uses explicit optional settings. Opening old/default HSV or
                    // Exposure promotes absent settings; that stored-record change retains its legacy entry.
                    if (kind == AdjustmentKind::Hsv)
                        original.hsvSettings = HueSaturationSettings{};
                    if (kind == AdjustmentKind::Exposure)
                        original.exposureSettings = ExposureSettings{};
                    _session.UpdateAdjustment(id, original);
                }
                _session.SetOpacity(0.75);
                _session.Undo(); // real redo must survive all refused and unchanged publications
                _session.SetAdjustmentEditingID(id);
                if (!_session.BeginAdjustmentEditing(id, image, true))
                    return fail(L"An adjustment closure fixture could not open its reference panel.\n");
                FilterSettings settings = _session.PendingColorEdit()->settings;
                LayerAdjustment expected = original;
                if (mode == Mode::ImplicitDefaults)
                {
                    if (kind == AdjustmentKind::Hsv)
                        expected.hsvSettings = HueSaturationSettings{};
                    else
                        expected.exposureSettings = ExposureSettings{};
                }
                else if (mode != Mode::Noop)
                {
                    switch (kind)
                    {
                    case AdjustmentKind::Hsv:
                        settings.hsv.adjustments[0].hue = 17;
                        expected.hsvSettings = settings.hsv;
                        break;
                    case AdjustmentKind::Levels:
                        settings.levels.ranges[0].gamma = 2;
                        expected.levels.ranges[0].gamma = 2;
                        break;
                    case AdjustmentKind::Curves:
                        settings.curves.channels[0] = {{0, 0}, {96, 120}, {255, 255}};
                        expected.curves.channels[0] = {{0, 0}, {96, 120}, {255, 255}};
                        break;
                    case AdjustmentKind::Exposure:
                        settings.exposure = {0.25, 0.05, 1.2};
                        expected.exposureSettings = ExposureSettings{0.25, 0.05, 1.2};
                        break;
                    case AdjustmentKind::GradientMap:
                        settings.gradientMap = {{0.1, 0.2, 0.3}, {0.8, 0.7, 0.6}, true};
                        expected.gradientMapSettings = GradientMapSettings{{0.1, 0.2, 0.3}, {0.8, 0.7, 0.6}, true};
                        break;
                    case AdjustmentKind::Grain:
                        settings.grain.amount = 23;
                        expected.grainSettings = settings.grain;
                        break;
                    }
                }
                _session.UpdateColorEdit(settings, mode != Mode::ChangedOff);
                DocumentChanged();
                if (!settle())
                    return fail(L"An adjustment closure fixture did not settle its preview.\n");
                _colorWorkTestGate = std::make_shared<ColorWorkTestGate>();
                const auto gate = _colorWorkTestGate;
                gate->kind = ColorWorkTestKind::Commit;
                if (!gate->entered || !gate->release || !gate->computed)
                    return fail(L"An adjustment closure fixture could not create its gate.\n");
                gate->throwCaptureFailure = mode == Mode::Capture;
                if (mode == Mode::Failure)
                    gate->historyFailure = stages[attempt];
                const auto revision = _session.history.CurrentRevision();
                const auto historyCount = _session.history.UndoCount();
                const auto sequence = _session.ColorEditSequence();
                const auto generation = _session.Generation();
                const auto sourceDocument = _session.Document();
                const auto presents = _renderer.PresentCount();
                const auto ordinal = _historyWork.NextOrdinal();
                if (!ordinal)
                    return fail(L"An adjustment closure fixture exhausted its ordering ordinal.\n");
                HostOutcome admitted = HostOutcome::Failed;
                if (mode == Mode::Cancel)
                {
                    _colorCommitStamp =
                        _historyWork.Begin(_session.DocumentSerial(), ordinal.value(), HistoryWorkKind::Commit);
                    admitted = StartAdjustmentClosure(ordinal.value(), false);
                }
                else
                    admitted = StartColorCommit(ordinal.value());
                if (admitted != HostOutcome::Pending || !_historyWork.Busy() || !ColorCommitBusy() ||
                    !_session.HasColorEdit() || _session.ColorEditSequence() != sequence ||
                    _session.Document() != sourceDocument || _session.history.CurrentRevision() != revision)
                    return fail(L"Adjustment closure admission changed live state or did not hold its barrier.\n");
                if (mode != Mode::Capture)
                {
                    if (!WaitForColorWorkTestEvent(_jobs, gate->entered.get(), DispatchMessageThunk, this) ||
                        !gate->captureReservationHeld.load() || _session.Document() != sourceDocument ||
                        _session.history.CurrentRevision() != revision || !_session.HasColorEdit())
                        return fail(L"Adjustment closure captured without its reservation or published before "
                                    L"acknowledgement.\n");
                    if (mode == Mode::Epoch)
                    {
                        _session.history.Begin(L"Epoch only", _session.Document(), _session.ActiveLayerID());
                        _session.history.End(_session.Document(), _session.ActiveLayerID());
                    }
                    (void)SetEvent(gate->release.get());
                    if (mode == Mode::LateCancel)
                    {
                        if (WaitForSingleObject(gate->computed.get(), 10000) != WAIT_OBJECT_0)
                            return fail(L"Late-cancel adjustment closure did not compute its candidate.\n");
                        CancelColorCommit(); // completed worker bytes still cannot publish after this request
                    }
                }
                if (!settle())
                    return fail(L"An adjustment closure fixture did not acknowledge its terminal result.\n");
                const bool succeeded = mode == Mode::ChangedOn || mode == Mode::ChangedOff || mode == Mode::Noop ||
                                       mode == Mode::Cancel || mode == Mode::ImplicitDefaults;
                const auto outcome = succeeded                  ? HostOutcome::Succeeded
                                     : mode == Mode::Epoch      ? HostOutcome::Stale
                                     : mode == Mode::LateCancel ? HostOutcome::Cancelled
                                                                : HostOutcome::Failed;
                const auto expectedHistory =
                    historyCount +
                    (mode == Mode::ChangedOn || mode == Mode::ChangedOff || mode == Mode::ImplicitDefaults ? 1 : 0);
                if (_colorCommitOutcome != outcome || _historyWork.Busy() || ColorCommitBusy() ||
                    _session.HasColorEdit() == succeeded || _session.history.UndoCount() != expectedHistory ||
                    _session.Document()->layers.front().asset->image != image || !gate->captureReservationHeld.load() ||
                    !gate->payloadRetired.load() || !gate->payloadRetiredOffOwner.load() ||
                    _jobs.ReservedMemoryBytes() != 0 || _renderer.PresentCount() != presents)
                    return fail(L"Adjustment closure violated outcome, history, source bytes, hidden rendering or "
                                L"retirement.\n");
                if (succeeded)
                {
                    const auto& actual = _session.ActiveLayer()->adjustment.value();
                    const auto& wanted = mode == Mode::Cancel ? original : expected;
                    const auto expectedGeneration = generation + 1;
                    if (actual != wanted || _session.Generation() != expectedGeneration ||
                        _session.ColorEditSequence() != sequence + 1 ||
                        ((mode == Mode::Noop || mode == Mode::Cancel) &&
                         (_session.history.CurrentRevision() != revision || !_session.history.CanRedo())))
                        return fail(L"Adjustment closure changed parameter, generation, no-op or redo semantics.\n");
                }
                else if (_session.Document() != sourceDocument || _session.Generation() != generation ||
                         _session.ColorEditSequence() != sequence || _session.history.CurrentRevision() != revision ||
                         _session.history.CaptureState().FutureCount() != 1 ||
                         _session.history.RedoName() != L"Layer Opacity" ||
                         ((mode == Mode::Failure || mode == Mode::Capture) && _colorCommitError != E_OUTOFMEMORY) ||
                         (mode == Mode::Failure && gate->historyFaultHits.load() != 1))
                    return fail(
                        L"A refused adjustment closure lost its unchanged panel, document, redo or original error.\n");
                std::fprintf(stderr, "PASS adjustment-closure-host kind=%u mode=%u attempt=%zu\n",
                             static_cast<unsigned int>(kind), static_cast<unsigned int>(mode), attempt);
            }
        }
    }
    return 0;
}
