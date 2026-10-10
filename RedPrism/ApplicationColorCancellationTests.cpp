#include "Application.h"

#include "../Tests/Support/DocumentFixture.h"
#include "ChromeHost.h"
#include "ColorWorkTestGate.h"
#include "ProjectStore.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <utility>

void Application::CancelColorEditForTest() noexcept
{
    // Fixture cleanup acknowledges the real cancellation route before reuse. A preceding fault-injection gate
    // belongs to its old job; it must not inject that same failure into the independent cleanup operation.
    auto previousGate = std::exchange(_colorWorkTestGate, {});
    if (previousGate)
        (void)SetEvent(previousGate->release.get());
    CancelColorEdit();
    if (SettleJobs(_jobs, 10000, DispatchMessageThunk, this) != SettleResult::Settled)
    {
        SelfTestFailure(L"The panel cancellation harness did not acknowledge cleanup.\n");
        std::_Exit(5);
    }
    _colorWorkTestGate = std::move(previousGate);
}

int Application::RunColorCancellationSelfTest() noexcept
{
    const auto fail = [](const wchar_t* text) noexcept
    {
        SelfTestFailure(text);
        return 5;
    };
    const auto settle = [this]() noexcept
    { return SettleJobs(_jobs, 10000, DispatchMessageThunk, this) == SettleResult::Settled; };
    CancelColorEdit();
    if (!settle())
        return fail(L"Earlier work did not settle before production panel cancellation.\n");
    const auto cleanup = wil::scope_exit(
        [&]() noexcept
        {
            if (_colorWorkTestGate)
                (void)SetEvent(_colorWorkTestGate->release.get());
            CancelHostContinuations();
            CancelColorCommit();
            if (!settle())
                std::_Exit(5);
            _colorWorkTestGate.reset();
            _session.CancelColorEdit(); // reference teardown only after every admitted owner acknowledges
            CancelSizeCard();
        });
    constexpr std::array<std::uint8_t, 16> bytes{0, 0, 0, 255, 64, 64, 64, 255, 128, 128, 128, 255, 255, 255, 255, 255};
    const Raster source = Raster::Make(4, 1, RasterFormat::Bgra8888, bytes.data(), bytes.size());
    constexpr std::array adjustmentKinds{AdjustmentKind::Hsv,      AdjustmentKind::Levels,      AdjustmentKind::Curves,
                                         AdjustmentKind::Exposure, AdjustmentKind::GradientMap, AdjustmentKind::Grain};
    constexpr std::array destructiveKinds{FilterKind::Levels, FilterKind::GaussianBlur, FilterKind::ContentAwareFill};
    std::optional<CanvasDocument> original;
    const auto prepare = [&](std::size_t kind) -> bool
    {
        _colorWorkTestGate.reset();
        _session.CancelColorEdit(); // no worker exists at this independent fixture construction boundary
        CancelSizeCard();
        DocumentFixture document;
        document.id = Guid::Generate();
        document.width = 4;
        document.height = 1;
        document.selection = DocumentSelection{false, {{Vec2{1, 0}, Vec2{2, 0}, Vec2{2, 1}, Vec2{1, 1}}}};
        ImageLayer layer = ImageLayer::Blank(L"Cancellation source", Vec2{4, 1});
        const Guid id = layer.id;
        layer.asset = RasterAsset{source, {}, layer.name};
        document.layers.push_back(std::move(layer));
        ReplaceDocument(std::move(document).Seal(), id, {});
        if (kind < adjustmentKinds.size())
            _session.AddAdjustment(adjustmentKinds[kind]);
        _session.SetOpacity(0.75);
        _session.Undo();
        original = _session.Document();
        if (kind < adjustmentKinds.size())
        {
            const Guid adjustmentID = _session.ActiveLayerID().value();
            _session.SetAdjustmentEditingID(adjustmentID);
            if (!_session.BeginAdjustmentEditing(adjustmentID, source, true))
                return false;
        }
        else
            _session.BeginColorEdit(destructiveKinds[kind - adjustmentKinds.size()], true);
        if (!_session.PendingColorEdit())
            return false;
        auto settings = _session.PendingColorEdit()->settings;
        settings.levels.ranges[0].gamma = 2;
        settings.hsv.adjustments[0].hue = 17;
        settings.curves.channels[0] = {{0, 0}, {96, 120}, {255, 255}};
        settings.exposure = {0.25, 0.05, 1.2};
        settings.gradientMap = {{0.1, 0.2, 0.3}, {0.8, 0.7, 0.6}, true};
        settings.grain.amount = 23;
        _session.UpdateColorEdit(settings, true);
        DocumentChanged();
        return settle();
    };
    enum class Mode
    {
        Cancel,
        CaptureFailure,
        WorkerFailure,
        Epoch,
        AdmissionFailure
    };
    for (std::size_t kind = 0; kind < adjustmentKinds.size() + destructiveKinds.size(); ++kind)
    {
        for (const auto mode :
             {Mode::Cancel, Mode::CaptureFailure, Mode::WorkerFailure, Mode::Epoch, Mode::AdmissionFailure})
        {
            if (!prepare(kind))
                return fail(L"A production Cancel fixture could not prepare its panel.\n");
            if (!_chrome || !_chrome->AdjustPanelOpen())
                return fail(L"The cancellation feedback fixture did not expose its original card.\n");
            const bool metadata = kind < adjustmentKinds.size();
            const auto before = _session.Document();
            const auto generation = _session.Generation();
            const auto sequence = _session.ColorEditSequence();
            const auto revision = _session.history.CurrentRevision();
            const auto history = _session.history.CaptureState();
            const auto templateSettings = _session.filterSettings;
            const auto opening = _session.PendingColorEdit()->openingSequence;
            const auto presents = _renderer.PresentCount();
            std::weak_ptr<const AdjustmentLookup> lookup = _session.PendingColorEdit()->previewLookup;
            const bool hasLookup = !lookup.expired();
            _colorWorkTestGate = std::make_shared<ColorWorkTestGate>();
            const auto gate = _colorWorkTestGate;
            gate->kind = ColorWorkTestKind::Commit;
            gate->throwCaptureFailure = mode == Mode::CaptureFailure;
            gate->throwAllocationFailure = mode == Mode::WorkerFailure;
            if (!gate->entered || !gate->computed || !gate->release)
                return fail(L"The production Cancel event gate could not initialize.\n");
            _hostAdmissionAllocationFailureForTest = mode == Mode::AdmissionFailure;
            ContinuationReceipt receipt;
            if (mode == Mode::Cancel)
            {
                if (kind % 2 == 0)
                    OnAdjustAction(AdjustAction::Cancel, 0);
                else if (!EndCanvasEdit(CanvasEdit::Panel, EditEnd::Escape))
                    return fail(L"Escape did not admit the panel's selected Cancel route.\n");
                receipt = _pendingCancelReceipt;
            }
            else
                receipt = QueuePanelCancel(opening);
            _hostAdmissionAllocationFailureForTest = false;
            const bool refusedAdmission = mode == Mode::AdmissionFailure;
            if (_chrome->AdjustPanelOpen() != refusedAdmission ||
                _chrome->AdjustInspectorForTest().filter.has_value() != refusedAdmission ||
                _session.Document() != before || !_session.HasColorEdit())
                return fail(
                    L"Cancel feedback did not hide only its accepted matching card while retaining the source.\n");
            std::fprintf(stderr, "PASS color-cancel-feedback.admission kind=%zu mode=%u\n", kind,
                         static_cast<unsigned int>(mode));

            if (mode == Mode::AdmissionFailure)
            {
                if (receipt.Read() != ContinuationReceipt::Result{HostOutcome::Failed, E_OUTOFMEMORY} ||
                    !_session.HasColorEdit() || ColorControlsFrozen() || _session.Document() != before ||
                    !_session.history.IsCurrent(history) || _jobs.ReservedMemoryBytes() != 0)
                    return fail(L"Refused Cancel admission changed/froze a live panel or lost its original error.\n");
            }
            else
            {
                if (receipt.Read().outcome != HostOutcome::Pending || !ColorControlsFrozen() ||
                    !_session.HasColorEdit() || _session.Document() != before || _session.Generation() != generation)
                    return fail(L"Cancel admission closed/mutated the panel before acknowledgement.\n");
                const auto repeated = QueuePanelCancel(opening);
                if (repeated.Read().outcome != HostOutcome::Pending || receipt.Read().outcome != HostOutcome::Pending ||
                    _hostRequests.Size() != 1)
                    return fail(L"Repeated queued Cancel did not coalesce without retiring itself.\n");
                if (mode != Mode::CaptureFailure)
                {
                    if (!WaitForColorWorkTestEvent(_jobs, gate->entered.get(), DispatchMessageThunk, this) ||
                        !gate->captureReservationHeld.load() || !_historyWork.Busy() || !_session.HasColorEdit() ||
                        _session.Document() != before || receipt.Read().outcome != HostOutcome::Pending)
                        return fail(L"Cancel did not hold its admitted owner/barrier until its worker gate.\n");
                    const auto activeRepeat = QueuePanelCancel(opening);
                    if (activeRepeat.Read().outcome != HostOutcome::Pending || _hostRequests.Size() != 0 ||
                        _historyWork.CancelRequested(_colorCommitStamp.value()))
                        return fail(L"Repeated active Cancel canceled its own worker or queued a duplicate.\n");
                    if (mode == Mode::Epoch)
                    {
                        _session.history.Begin(L"Cancel epoch only", _session.Document(), _session.ActiveLayerID());
                        _session.history.End(_session.Document(), _session.ActiveLayerID());
                    }
                    (void)SetEvent(gate->release.get());
                }
                if (!settle())
                    return fail(L"Production Cancel failed to acknowledge within its harness bound.\n");
                const auto expected = mode == Mode::Cancel  ? HostOutcome::Succeeded
                                      : mode == Mode::Epoch ? HostOutcome::Stale
                                                            : HostOutcome::Failed;
                if (receipt.Read().outcome != expected || repeated.Read() != receipt.Read() || _historyWork.Busy() ||
                    ColorCommitBusy() || ColorControlsFrozen() || !gate->captureReservationHeld.load() ||
                    !gate->payloadRetired.load() || !gate->payloadRetiredOffOwner.load() ||
                    _jobs.ReservedMemoryBytes() != 0 || _renderer.PresentCount() != presents ||
                    _session.history.CurrentRevision() != revision || _session.filterSettings != templateSettings)
                    return fail(
                        L"Cancel violated terminal receipts, error, history, hidden render or owner retirement.\n");
                if (mode == Mode::Cancel)
                {
                    if (_session.HasColorEdit() || _session.Document() != original ||
                        _session.Generation() != generation + (metadata ? 1 : 0) ||
                        _session.ColorEditSequence() != sequence + 1 || _session.history.CaptureState().Depth() != 0 ||
                        _session.history.CaptureState().FutureCount() != history.FutureCount() || !lookup.expired())
                        return fail(L"Successful Cancel changed source/template/redo or retained its lookup owner.\n");
                }
                else if (!_session.HasColorEdit() || _session.Document() != before ||
                         _session.Generation() != generation || _session.ColorEditSequence() != sequence ||
                         _session.history.CaptureState().Depth() != history.Depth() ||
                         _session.history.CaptureState().FutureCount() != history.FutureCount() ||
                         (hasLookup && lookup.expired()) ||
                         (expected == HostOutcome::Failed && receipt.Read().error != E_OUTOFMEMORY))
                    return fail(L"Refused Cancel destroyed or changed the live panel, redo or source error.\n");
            }
            const bool panelRemains = mode != Mode::Cancel;
            if (_chrome->AdjustPanelOpen() != panelRemains ||
                _chrome->AdjustInspectorForTest().filter.has_value() != panelRemains ||
                _chrome->AdjustInspectorForTest().busy || _session.HasColorEdit() != panelRemains)
                return fail(L"Terminal Cancel feedback did not restore its refused/stale card or keep successful close "
                            L"hidden.\n");
            std::fprintf(stderr, "PASS color-cancel-feedback.terminal kind=%zu mode=%u\n", kind,
                         static_cast<unsigned int>(mode));
            std::fprintf(stderr, "PASS color-cancel-host kind=%zu mode=%u\n", kind, static_cast<unsigned int>(mode));
        }
    }
    // A real selected OK remains the active owner until its canceled result acknowledges; two Cancel clicks
    // retain one younger request, and a subsequently admitted rename cannot execute before that close.
    for (const bool metadata : {false, true})
    {
        for (const bool computedBeforeCancel : {false, true})
        {
            if (!prepare(metadata ? std::size_t{1} : adjustmentKinds.size()))
                return fail(L"The held-OK Cancel ordering fixture could not open.\n");
            const auto document = original;
            const auto revision = _session.history.CurrentRevision();
            const auto opening = _session.PendingColorEdit()->openingSequence;
            _colorWorkTestGate = std::make_shared<ColorWorkTestGate>();
            const auto gate = _colorWorkTestGate;
            gate->kind = ColorWorkTestKind::Commit;
            const auto ok = QueuePanelCommit(opening);
            if (!WaitForColorWorkTestEvent(_jobs, gate->entered.get(), DispatchMessageThunk, this))
                return fail(L"Selected OK did not reach the Cancel ordering gate.\n");
            if (computedBeforeCancel)
            {
                (void)SetEvent(gate->release.get());
                if (WaitForSingleObject(gate->computed.get(), 10000) != WAIT_OBJECT_0)
                    return fail(L"The late Cancel fixture did not compute OK before its acknowledgement.\n");
            }
            const auto cancel = QueuePanelCancel(opening);
            const auto repeat = QueuePanelCancel(opening);
            const Guid id = _session.ActiveLayerID().value();
            const auto rename = QueueHostInput(HostLayerRenameInput{id, L"After acknowledged Cancel"});
            if (ok.Read().outcome != HostOutcome::Pending || cancel.Read().outcome != HostOutcome::Pending ||
                repeat.Read().outcome != HostOutcome::Pending || rename.Read().outcome != HostOutcome::Pending ||
                _hostRequests.Size() != 2 || !_historyWork.Busy() || !_session.HasColorEdit())
                return fail(
                    L"Cancel released a held OK early or failed to retain one ordered close and younger rename.\n");
            (void)SetEvent(gate->release.get());
            if (!settle() || ok.Read().outcome != HostOutcome::Cancelled ||
                cancel.Read().outcome != HostOutcome::Succeeded || repeat.Read() != cancel.Read() ||
                rename.Read().outcome != HostOutcome::Succeeded || _session.HasColorEdit() ||
                _session.ActiveLayer()->name != L"After acknowledged Cancel" ||
                _session.Document()->layers.front().asset->image != source || _jobs.ReservedMemoryBytes() != 0 ||
                _historyWork.Busy() || _session.history.CurrentRevision() == revision)
                return fail(
                    L"Held OK pixels escaped cancellation or a younger rename crossed close acknowledgement.\n");
            _session.Undo(); // independent expected entry: only the younger rename, never the canceled OK
            if (_session.Document() != document)
                return fail(L"Undo of the younger rename revealed an unexpected canceled panel history entry.\n");
            std::fprintf(stderr, "PASS color-cancel-host held-ok metadata=%u computed=%u\n",
                         static_cast<unsigned int>(metadata), static_cast<unsigned int>(computedBeforeCancel));
        }
    }
    enum class Younger
    {
        Save,
        Tool,
        SizeCard,
        CloseProject
    };
    std::array<wchar_t, MAX_PATH + 1> temporary{};
    const DWORD temporaryLength = GetTempPathW(static_cast<DWORD>(temporary.size()), temporary.data());
    if (!temporaryLength || temporaryLength >= temporary.size())
        return fail(L"The Cancel writer fixture could not obtain a bounded temporary directory.\n");
    const std::wstring output =
        std::wstring(temporary.data()) + L"RedPrism-panel-cancel-" + std::to_wstring(GetCurrentProcessId()) + L".prism";
    const auto deleteOutput = wil::scope_exit([&]() noexcept { (void)DeleteFileW(output.c_str()); });
    for (const bool metadata : {false, true})
    {
        for (const auto younger : {Younger::Save, Younger::Tool, Younger::SizeCard, Younger::CloseProject})
        {
            if (!prepare(metadata ? std::size_t{1} : adjustmentKinds.size()))
                return fail(L"A younger-action Cancel fixture could not open.\n");
            _session.history.MarkSaved();
            const auto expectedDocument = original;
            const auto opening = _session.PendingColorEdit()->openingSequence;
            _tool = EditorTool::Move;
            _colorWorkTestGate = std::make_shared<ColorWorkTestGate>();
            const auto gate = _colorWorkTestGate;
            gate->kind = ColorWorkTestKind::Commit;
            const auto cancel = QueuePanelCancel(opening);
            if (!WaitForColorWorkTestEvent(_jobs, gate->entered.get(), DispatchMessageThunk, this))
                return fail(L"Cancel did not reach the younger-action ownership gate.\n");
            HostRequestInput input = HostToolInput{EditorTool::Hand};
            if (younger == Younger::Save)
                input = HostWriteInput{HostWriteFormat::Project, false, output};
            else if (younger == Younger::SizeCard)
                input = HostCommandInput{ChromeHost::kCommandImageSize};
            else if (younger == Younger::CloseProject)
                input = HostCloseProjectInput{};
            const auto receipt = QueueHostInput(std::move(input));
            if (receipt.Read().outcome != HostOutcome::Pending || cancel.Read().outcome != HostOutcome::Pending ||
                !_session.HasColorEdit() || SizeCardOpen() || _tool != EditorTool::Move || _hostRequests.Size() != 1)
                return fail(L"A younger write/tool/card/close crossed pending Cancel acknowledgement.\n");
            (void)SetEvent(gate->release.get());
            if (!settle() || cancel.Read().outcome != HostOutcome::Succeeded ||
                receipt.Read().outcome != HostOutcome::Succeeded || _session.HasColorEdit() || _historyWork.Busy() ||
                _jobs.ReservedMemoryBytes() != 0)
                return fail(L"The selected Cancel did not complete before its younger action.\n");
            if (younger == Younger::Save)
            {
                const auto saved = LoadProject(output.c_str());
                std::array<std::uint8_t, 16> savedBytes{};
                if (saved.status == ProjectIoStatus::Ok && !saved.document.layers.empty() &&
                    saved.document.layers.front().asset)
                    saved.document.layers.front().asset->image.ReadRegion(0, 0, 4, 1, savedBytes.data(), 16, 0);
                if (saved.status != ProjectIoStatus::Ok ||
                    saved.document.layers.size() != expectedDocument->layers.size() ||
                    saved.document.layers.back().adjustment != expectedDocument->layers.back().adjustment ||
                    saved.document.selection || savedBytes != bytes)
                    return fail(L"The younger save captured canceled preview metadata or pixels.\n");
            }
            else if (younger == Younger::Tool && _tool != EditorTool::Hand)
                return fail(L"The younger tool did not execute after Cancel acknowledgement.\n");
            else if (younger == Younger::SizeCard && (!SizeCardOpen() || _sizeDialog.kind != SizeCardKind::ImageSize ||
                                                      _sizeDialog.image.width != 4 || _sizeDialog.image.height != 1))
                return fail(L"The younger size card did not acquire settled document dimensions.\n");
            else if (younger == Younger::CloseProject && _session.Document())
                return fail(L"The younger Close Project did not execute after Cancel acknowledgement.\n");
            std::fprintf(stderr, "PASS color-cancel-host younger=%u metadata=%u\n", static_cast<unsigned int>(younger),
                         static_cast<unsigned int>(metadata));
        }
    }
    enum class SelectedDismissal
    {
        SizeCard,
        New,
        CloseProject
    };
    for (const bool metadata : {false, true})
    {
        for (const auto action : {SelectedDismissal::SizeCard, SelectedDismissal::New, SelectedDismissal::CloseProject})
        {
            for (const bool failCapture : {false, true})
            {
                if (!prepare(metadata ? std::size_t{1} : adjustmentKinds.size()))
                    return fail(L"A selected dismissal prerequisite fixture could not open.\n");
                const auto before = _session.Document();
                const auto wanted = original;
                const auto revision = _session.history.CurrentRevision();
                const auto priorAnswer = std::exchange(_forcedDiscardAnswer, Prompts::Answer::Second);
                const auto restoreAnswer = wil::scope_exit([&]() noexcept { _forcedDiscardAnswer = priorAnswer; });
                _colorWorkTestGate = std::make_shared<ColorWorkTestGate>();
                const auto gate = _colorWorkTestGate;
                gate->kind = ColorWorkTestKind::Commit;
                gate->throwCaptureFailure = failCapture;
                HostRequestInput input = HostCommandInput{ChromeHost::kCommandImageSize};
                if (action == SelectedDismissal::New)
                    input = HostNewInput{};
                else if (action == SelectedDismissal::CloseProject)
                    input = HostCloseProjectInput{};
                const auto receipt = QueueHostInput(std::move(input));
                if (!failCapture)
                {
                    if (!WaitForColorWorkTestEvent(_jobs, gate->entered.get(), DispatchMessageThunk, this) ||
                        receipt.Read().outcome != HostOutcome::Pending || !_session.HasColorEdit() ||
                        _session.Document() != before || SizeCardOpen() || !_historyWork.Busy())
                        return fail(L"A selected card/reset bypassed its same-ordinal cancellation prerequisite.\n");
                    (void)SetEvent(gate->release.get());
                }
                if (!settle() || _historyWork.Busy() || _jobs.ReservedMemoryBytes() != 0 ||
                    !gate->captureReservationHeld.load() || !gate->payloadRetiredOffOwner.load())
                    return fail(L"A selected dismissal prerequisite lost its acknowledged owner or reservation.\n");
                if (failCapture)
                {
                    if (receipt.Read() != ContinuationReceipt::Result{HostOutcome::Failed, E_OUTOFMEMORY} ||
                        !_session.HasColorEdit() || _session.Document() != before || SizeCardOpen() ||
                        _session.history.CurrentRevision() != revision)
                        return fail(
                            L"A failed Cancel prerequisite executed its dependent card/reset or lost the draft.\n");
                }
                else if (receipt.Read().outcome != HostOutcome::Succeeded || _session.HasColorEdit() ||
                         (action == SelectedDismissal::SizeCard && (!SizeCardOpen() || _session.Document() != wanted ||
                                                                    _session.history.CurrentRevision() != revision)) ||
                         (action == SelectedDismissal::New &&
                          (!_session.Document() || _session.Document()->id == wanted->id ||
                           _session.history.IsModified())) ||
                         (action == SelectedDismissal::CloseProject && _session.Document()))
                    return fail(
                        L"The selected card/reset did not execute only after its successful Cancel prerequisite.\n");
                std::fprintf(stderr, "PASS color-cancel-host selected=%u metadata=%u capture-failure=%u\n",
                             static_cast<unsigned int>(action), static_cast<unsigned int>(metadata),
                             static_cast<unsigned int>(failCapture));
            }
        }
    }
    if (!prepare(adjustmentKinds.size()))
        return fail(L"The old-opening cancellation fixture could not open.\n");
    const auto oldOpening = _session.PendingColorEdit()->openingSequence;
    CancelColorEdit();
    if (!settle() || !prepare(adjustmentKinds.size()))
        return fail(L"The old-opening fixture could not acknowledge Cancel and reopen.\n");
    const auto newOpening = _session.PendingColorEdit()->openingSequence;
    const auto newDocument = _session.Document();
    const auto stale = QueueHostInput(HostPanelCancelInput{oldOpening});
    if (!settle() || stale.Read().outcome != HostOutcome::Stale || !_session.HasColorEdit() ||
        _session.PendingColorEdit()->openingSequence != newOpening || newOpening == oldOpening ||
        _session.Document() != newDocument || ColorControlsFrozen())
        return fail(L"A stale cancellation closed or froze a reopened panel.\n");
    std::fprintf(stderr, "PASS color-cancel-host stale-opening\n");

    // Immediate discarded feedback is checked before the held predecessor can acknowledge.
    for (const bool heldOk : {false, true})
    {
        if (!prepare(adjustmentKinds.size()))
            return fail(L"The discarded Cancel feedback fixture could not open.\n");
        const auto before = _session.Document();
        const auto history = _session.history.CaptureState();
        const auto revision = _session.history.CurrentRevision();
        const auto generation = _session.Generation();
        const auto sequence = _session.ColorEditSequence();
        const auto opening = _session.PendingColorEdit()->openingSequence;
        ContinuationReceipt predecessor;
        std::optional<HistoryWorkStamp> originalStamp;
        std::shared_ptr<ColorWorkTestGate> gate;
        if (heldOk)
        {
            gate = std::make_shared<ColorWorkTestGate>();
            gate->kind = ColorWorkTestKind::Commit;
            _colorWorkTestGate = gate;
            predecessor = QueuePanelCommit(opening);
            if (!WaitForColorWorkTestEvent(_jobs, gate->entered.get(), DispatchMessageThunk, this) ||
                predecessor.Read().outcome != HostOutcome::Pending || !_colorCommitStamp || !_historyWork.Busy())
                return fail(L"The discarded feedback predecessor did not hold its original owner.\n");
            originalStamp = _colorCommitStamp;
        }
        const auto discarded = QueuePanelCancel(opening);
        if (discarded.Read().outcome != HostOutcome::Pending || _hostRequests.Size() != 1 || !_session.HasColorEdit() ||
            !ColorCancelPending() || _chrome->AdjustPanelOpen())
            return fail(L"Accepted queued Cancel did not hide only its retained current panel.\n");
        std::fprintf(stderr, "PASS color-cancel-feedback.queued-accepted held=%u\n", static_cast<unsigned int>(heldOk));
        CancelHostContinuations();
        if (discarded.Read().outcome != HostOutcome::Cancelled || _hostRequests.Size() != 0 || ColorCancelPending() ||
            !_chrome->AdjustPanelOpen() || !_session.HasColorEdit() || _session.Document() != before ||
            !_session.history.IsCurrent(history) || ColorControlsFrozen() != heldOk ||
            _chrome->AdjustInspectorForTest().busy != heldOk ||
            (heldOk && (_colorCommitStamp != originalStamp || !_historyWork.Busy() ||
                        !_historyWork.CancelRequested(originalStamp.value()) ||
                        predecessor.Read().outcome != HostOutcome::Pending)))
            return fail(L"Discarded Cancel did not restore the card before its original held owner acknowledged.\n");
        std::fprintf(stderr, "PASS color-cancel-feedback.queued-discarded-before-ack held=%u\n",
                     static_cast<unsigned int>(heldOk));
        if (gate)
            (void)SetEvent(gate->release.get());
        if (!settle() || (heldOk && predecessor.Read().outcome != HostOutcome::Cancelled) ||
            discarded.Read().outcome != HostOutcome::Cancelled || _colorCommitStamp || _historyWork.Busy() ||
            !_session.HasColorEdit() || _session.PendingColorEdit()->openingSequence != opening ||
            _session.Document() != before || _session.Generation() != generation ||
            _session.ColorEditSequence() != sequence || !_session.history.IsCurrent(history) ||
            _session.history.CurrentRevision() != revision || !_chrome->AdjustPanelOpen() ||
            !_chrome->AdjustInspectorForTest().filter || ColorControlsFrozen())
            return fail(L"Discarded Cancel changed its retained panel or history after original acknowledgement.\n");
        std::fprintf(stderr, "PASS color-cancel-feedback.queued-discarded-after-ack held=%u\n",
                     static_cast<unsigned int>(heldOk));
    }
    {
        const auto before = _session.Document();
        const auto history = _session.history.CaptureState();
        const auto currentOpening = _session.PendingColorEdit()->openingSequence;
        const auto settings = _session.PendingColorEdit()->settings;
        // Use the full production admission/feedback path, not QueueHostInput's unmarked test route above.
        const auto unrelated = QueuePanelCancel(oldOpening);
        if (unrelated.Read().outcome != HostOutcome::Pending || ColorCancelPending() || ColorControlsFrozen() ||
            !_chrome->AdjustPanelOpen() || _session.PendingColorEdit()->openingSequence != currentOpening ||
            _session.PendingColorEdit()->settings != settings || _session.Document() != before)
            return fail(L"An unrelated admitted opening hid or changed the current card.\n");
        std::fprintf(stderr, "PASS color-cancel-feedback.stale-opening-admission\n");
        if (!settle() || unrelated.Read().outcome != HostOutcome::Stale || !_session.HasColorEdit() ||
            _session.PendingColorEdit()->openingSequence != currentOpening ||
            _session.PendingColorEdit()->settings != settings || _session.Document() != before ||
            !_session.history.IsCurrent(history) || !_chrome->AdjustPanelOpen() || ColorControlsFrozen())
            return fail(L"An unrelated opening's terminal publication changed the current card or history.\n");
        std::fprintf(stderr, "PASS color-cancel-feedback.stale-opening-terminal\n");
    }
    {
        if (!prepare(adjustmentKinds.size()))
            return fail(L"The old-document feedback fixture could not open.\n");
        const auto oldSerial = _session.DocumentSerial();
        const auto opening = _session.PendingColorEdit()->openingSequence;
        auto gate = std::make_shared<ColorWorkTestGate>();
        gate->kind = ColorWorkTestKind::Commit;
        _colorWorkTestGate = gate;
        const auto oldCancel = QueuePanelCancel(opening);
        if (!WaitForColorWorkTestEvent(_jobs, gate->entered.get(), DispatchMessageThunk, this) || !_colorCommitStamp)
            return fail(L"The old-document cancellation did not retain its original worker.\n");
        // Independent lifetime-invalidating reference fixture; ordinary replacements remain ordered.
        DocumentFixture replacement;
        replacement.id = Guid::Generate();
        replacement.width = 4;
        replacement.height = 1;
        ImageLayer replacementLayer = ImageLayer::Blank(L"Current feedback source", Vec2{4, 1});
        const Guid replacementActive = replacementLayer.id;
        replacementLayer.asset = RasterAsset{source, {}, replacementLayer.name};
        replacement.layers.push_back(std::move(replacementLayer));
        _session.ReplaceDocument(std::move(replacement).Seal(), replacementActive);
        _session.BeginColorEdit(FilterKind::Exposure, true);
        DocumentChanged();
        if (!_session.PendingColorEdit())
            return fail(L"The replacement reference fixture could not open its current panel.\n");
        const auto before = _session.Document();
        const auto history = _session.history.CaptureState();
        const auto currentOpening = _session.PendingColorEdit()->openingSequence;
        const auto settings = _session.PendingColorEdit()->settings;
        if (_session.DocumentSerial() == oldSerial || oldCancel.Read().outcome != HostOutcome::Pending ||
            ColorCancelPending() || !_chrome->AdjustPanelOpen() || !_historyWork.Busy() || !_session.HasColorEdit())
            return fail(
                L"An old-document pending cancellation hid the current card or released its original barrier.\n");
        std::fprintf(stderr, "PASS color-cancel-feedback.stale-document-before-ack\n");
        (void)SetEvent(gate->release.get());
        if (!settle() || oldCancel.Read().outcome != HostOutcome::Stale || !_session.HasColorEdit() ||
            _session.PendingColorEdit()->openingSequence != currentOpening ||
            _session.PendingColorEdit()->settings != settings || _session.Document() != before ||
            !_session.history.IsCurrent(history) || !_chrome->AdjustPanelOpen() || ColorControlsFrozen() ||
            _historyWork.Busy())
            return fail(L"An old-document acknowledgement changed the current panel/source/history.\n");
        std::fprintf(stderr, "PASS color-cancel-feedback.stale-document-after-ack\n");
    }
    return 0;
}
