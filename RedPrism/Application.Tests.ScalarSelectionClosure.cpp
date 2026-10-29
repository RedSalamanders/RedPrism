#include "Application.h"

#include "Runtime.Tests.ApplicationProcessingTestProbe.h"
#include "Runtime.Tests.SelectionMoveCloseTestGate.h"
#include "SelectionMoveOpeningWork.h"
#include "SharedPublicationMemory.h"
#include "Tests/Support/Support.Tests.DocumentFixture.h"

#include <array>
#include <cstdio>
#include <memory>
#include <new>
#include <thread>

namespace
{
CanvasDocument ScalarClosureFixture(Guid& active)
{
    DocumentFixture fixture;
    fixture.id = Guid::Generate();
    fixture.width = fixture.height = 8;
    auto layer = ImageLayer::Blank(L"Scalar closure", Vec2{8, 8});
    active = layer.id;
    fixture.layers.push_back(std::move(layer));
    fixture.selection = DocumentSelection{false, SelectionLoops{SelectionLoop{{1, 2}, {3, 2}, {3, 4}, {1, 4}}}};
    return std::move(fixture).Seal();
}
bool LiteralClosurePoints(const std::optional<DocumentSelection>& selection, bool translated)
{
    if (!selection || selection->antialiased || selection->loops.size() != 1 || selection->loops[0].size() != 4)
        return false;
    const std::array<Vec2, 4> expected = translated ? std::array<Vec2, 4>{{{-1, 5}, {1, 5}, {1, 7}, {-1, 7}}}
                                                    : std::array<Vec2, 4>{{{1, 2}, {3, 2}, {3, 4}, {1, 4}}};
    for (std::size_t i = 0; i != expected.size(); ++i)
        if (selection->loops[0][i] != expected[i])
            return false;
    return true;
}
} // namespace

// Production Close job only. The fixture seeds its scalar draft with worker-prepared Begin;
// it does not claim physical-press admission, hit/branch replay or ordinary S5 activation.
int Application::RunStandaloneScalarSelectionClosureSelfTest(HINSTANCE instance) noexcept
{
    enum class Mode
    {
        End,
        Cancel,
        NoOp,
        WorkerFailure,
        WorkerCancel,
        CaptureFailure,
        HistoryFailure,
        Stale,
        MemoryRefusal,
        Close
    };
    constexpr std::array modes{Mode::End,           Mode::Cancel,         Mode::NoOp,           Mode::WorkerFailure,
                               Mode::WorkerCancel,  Mode::CaptureFailure, Mode::HistoryFailure, Mode::Stale,
                               Mode::MemoryRefusal, Mode::Close};
    constexpr std::array names{
        "end",   "cancel", "noop", "worker-failure", "worker-cancel", "capture-failure", "history-failure",
        "stale", "memory", "close"};
    unsigned assertions = 0, failures = 0;
    const auto expect = [&](bool condition, const char* mode, const char* fact) noexcept
    {
        ++assertions;
        failures += condition ? 0u : 1u;
        std::printf("%s scalar-selection-closure.%s.%s\n", condition ? "PASS" : "FAIL", mode, fact);
        return condition;
    };
    auto summary = wil::scope_exit(
        [&]() noexcept
        { std::printf("SUMMARY scalar-selection-closure assertions=%u failures=%u\n", assertions, failures); });
    bool refuseMemory = false;
    ApplicationProcessingTestProbe processing;
    processing.workerCount = 2;
    processing.memory = {[](void* raw, MemorySnapshot& out) noexcept
                         {
                             const auto free = *static_cast<bool*>(raw) ? 0ULL : 8ULL * 1024 * 1024 * 1024;
                             out = {free, free};
                             return true;
                         },
                         &refuseMemory};
    auto owner = std::unique_ptr<Application>{new (std::nothrow) Application(instance, true, nullptr, 0)};
    if (!owner)
        return 5;
    const auto cleanup = wil::scope_exit(
        [&]() noexcept
        {
            if (owner->_selectionMoveCloseTestGate && owner->_selectionMoveCloseTestGate->release)
                (void)SetEvent(owner->_selectionMoveCloseTestGate->release.get());
            owner->Shutdown();
            owner->_processingTestProbe = nullptr;
            owner.reset();
            while (PumpMessages().quit)
            {
            }
        });
    owner->_quietUi = true;
    owner->_prompts = &QuietPrompts();
    owner->_processingTestProbe = &processing;
    if (!expect(SUCCEEDED(owner->InitializeProcessing()) && SUCCEEDED(owner->RegisterWindowClass()) &&
                    SUCCEEDED(owner->CreateMainWindow(false)) && SUCCEEDED(owner->InitializeRenderer()) &&
                    SUCCEEDED(owner->InitializeChrome()),
                "fixture", "real-two-worker-warp-host"))
        return 5;
    const auto settle = [&]() noexcept
    { return SettleJobs(owner->_jobs, 15000, DispatchMessageThunk, owner.get()) == SettleResult::Settled; };
    for (std::size_t caseIndex = 0; caseIndex != modes.size(); ++caseIndex)
    {
        const auto mode = modes[caseIndex];
        const auto name = names[caseIndex];
        Guid active;
        owner->_drag = PointerDrag{};
        auto fixture = ScalarClosureFixture(active);
        owner->_session.ReplaceDocument(std::move(fixture), active);
        owner->DocumentChanged();
        if (!settle())
            return 5;
        auto input = owner->_session.PrepareDeferredSelectionMoveOpeningInput();
        auto prepared = std::make_unique<std::optional<PreparedSelectionMoveOpening>>();
        if (!input)
            return 5;
        std::jthread prepareWorker(
            [&]()
            {
                auto value = DocumentSession::PrepareDeferredSelectionMoveOpening(std::move(input.value()));
                if (value)
                    prepared->emplace(std::move(value.value()));
            });
        prepareWorker.join();
        if (!expect(prepared->has_value() &&
                        owner->_session.ApplyPreparedDeferredSelectionMoveOpeningIfCurrent(prepared->value()) &&
                        owner->_session.IsDeferredSelectionMove(),
                    name, "worker-prepared-opening-publishes-scalar-mode"))
            return 5;
        std::jthread retireOpening([owned = std::move(prepared)]() mutable { owned.reset(); });
        retireOpening.join();
        const auto original = owner->_session.Selection()->loops.Identity();
        const auto opening = owner->_session.SelectionMoveOpening();
        const auto undoBefore = owner->_session.history.UndoCount();
        if (mode != Mode::NoOp)
            owner->_session.MoveSelection(-2, 3);
        if (!expect(owner->_session.Selection()->loops.Identity() == original &&
                        LiteralClosurePoints(owner->_session.Selection(), false),
                    name, "draft-keeps-original-geometry-unmaterialized"))
            return 5;
        auto gate = std::make_shared<SelectionMoveCloseTestGate>();
        gate->release.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (!gate->release)
            return 5;
        owner->_selectionMoveCloseTestGate = gate;
        if (mode == Mode::WorkerFailure)
            gate->result = {JobStatus::Failed, E_FAIL};
        gate->throwCaptureFailure = mode == Mode::CaptureFailure;
        if (mode == Mode::HistoryFailure)
            gate->throwHistoryStage = DocumentHistory::PrepareStage::AfterSnapshot;
        refuseMemory = mode == Mode::MemoryRefusal;
        const auto receipt = owner->QueueSelectionMoveClose(mode == Mode::Cancel);
        struct State final
        {
            Application* owner;
            Mode mode;
            ContinuationReceipt receipt;
            std::shared_ptr<SelectionMoveCloseTestGate> gate;
            bool visited = false, held = false;
            std::optional<JobTicket> ticket;
        } state{owner.get(), mode, receipt, gate};
        const auto pump = [](void* opaque, void* native) noexcept
        {
            auto& s = *static_cast<State*>(opaque);
            auto& message = *static_cast<MSG*>(native);
            if (message.message != SelectionMoveCloseTestGate::kMessage)
                return DispatchMessageThunk(s.owner, native);
            s.visited = true;
            s.ticket = s.owner->_selectionMoveCloseTicket;
            const auto memory = s.gate->geometryMemory.lock();
            s.held = s.ticket && s.owner->_historyWork.Busy() && s.receipt.Read().outcome == HostOutcome::Pending &&
                     s.gate->captures.load() == 1 && s.gate->payloadAllocatedOffOwner.load() &&
                     s.gate->reservationHeld.load() && memory && !memory->IsBound() && s.gate->reservationBytes != 0 &&
                     s.owner->_jobs.ReservedMemoryBytes() >= s.gate->reservationBytes;
            if (s.mode == Mode::WorkerCancel && s.ticket)
                (void)s.owner->_jobs.Cancel(s.ticket.value());
            if (s.mode == Mode::Stale)
                s.owner->_session.RenameLayer(s.owner->_session.ActiveLayerID().value(), L"Changed while held");
            (void)SetEvent(s.gate->release.get());
            if (s.mode == Mode::Close)
                s.owner->Shutdown();
            return;
        };
        const auto settled = SettleJobs(owner->_jobs, 15000, pump, &state);
        refuseMemory = false;
        if (mode == Mode::MemoryRefusal)
        {
            if (!expect(receipt.Read().outcome == HostOutcome::Failed && !state.visited &&
                            !owner->_selectionMoveCloseTicket && owner->_session.IsDeferredSelectionMove() &&
                            owner->_session.SelectionMoveOpening() == opening &&
                            owner->_session.history.UndoCount() == undoBefore && gate->captures.load() == 0,
                        name, "admission-refusal-keeps-draft-before-capture-no-fallback"))
                return 5;
            gate->hold = false;
            const auto retry = owner->QueueSelectionMoveClose(false);
            if (!expect(settle() && retry.Read().outcome == HostOutcome::Succeeded &&
                            LiteralClosurePoints(owner->_session.Selection(), true),
                        name, "explicit-retry-publishes-same-literal-offset"))
                return 5;
        }
        else
        {
            const bool prepareFault = mode == Mode::CaptureFailure || mode == Mode::HistoryFailure;
            if (prepareFault)
            {
                if (!expect(!state.visited && gate->payloadAllocatedOffOwner.load() && gate->reservationHeld.load() &&
                                gate->captures.load() == (mode == Mode::CaptureFailure ? 0u : 1u),
                            name, "allocation-fault-has-original-admission-without-publication"))
                    return 5;
            }
            else if (!expect(state.visited && state.held, name,
                             "original-ticket-charge-and-unbound-output-held-until-worker-ack"))
                return 5;
            if (mode == Mode::Close)
            {
                if (!expect(owner->_shutdownStarted && gate->retired.load() && gate->retiredOffOwner.load() &&
                                !owner->_session.Document(),
                            name, "joined-shutdown-retires-unpublished-geometry-off-owner"))
                    return 5;
                gate->geometryMemory.reset();
                break;
            }
            if (!expect(settled == SettleResult::Settled && gate->retired.load() && gate->retiredOffOwner.load(), name,
                        "producer-and-payload-retirement-acknowledged-off-owner"))
                return 5;
            if (mode == Mode::WorkerFailure || mode == Mode::WorkerCancel || mode == Mode::Stale || prepareFault)
            {
                const auto expected = (mode == Mode::WorkerFailure || prepareFault) ? HostOutcome::Failed
                                      : mode == Mode::WorkerCancel                  ? HostOutcome::Cancelled
                                                                                    : HostOutcome::Stale;
                if (!expect(receipt.Read().outcome == expected && owner->_session.IsDeferredSelectionMove() &&
                                owner->_session.SelectionMoveOpening() == opening &&
                                owner->_session.Selection()->loops.Identity() == original &&
                                owner->_session.history.UndoCount() == undoBefore,
                            name, "failed-cancelled-or-stale-result-keeps-original-draft-and-opening"))
                    return 5;
                owner->_session.CancelSelectionMove(); // Fixture cleanup only; production retry remains explicit.
            }
            else
            {
                const bool changed = mode == Mode::End;
                if (!expect(receipt.Read().outcome == HostOutcome::Succeeded && !owner->_session.IsMovingSelection() &&
                                LiteralClosurePoints(owner->_session.Selection(), changed) &&
                                owner->_session.history.UndoCount() == undoBefore + (changed ? 1 : 0),
                            name, "atomic-closure-has-literal-geometry-and-exact-history-count"))
                    return 5;
                if (changed)
                {
                    const auto memory = gate->geometryMemory.lock();
                    if (!expect(memory && memory->IsBound() && state.ticket &&
                                    memory->BoundTo(owner->_jobs, *state.ticket) &&
                                    memory->Bytes() == gate->reservationBytes,
                                name, "published-geometry-retains-exact-original-ticket-reservation"))
                        return 5;
                    owner->_session.Undo();
                    const bool undoLiteral = LiteralClosurePoints(owner->_session.Selection(), false);
                    owner->_session.Redo();
                    if (!expect(undoLiteral && LiteralClosurePoints(owner->_session.Selection(), true), name,
                                "undo-redo-retain-exact-original-and-translated-vertices"))
                        return 5;
                }
            }
        }
        auto weakGeometry = owner->_session.Selection()->loops.WeakStorageForTest();
        owner->_session.ClearDocument();
        owner->DocumentChanged();
        if (!settle())
            return 5;
        if (mode == Mode::End || mode == Mode::MemoryRefusal)
        {
            auto memory = gate->geometryMemory.lock();
            if (!expect(weakGeometry.expired() && memory && memory->IsBound() &&
                            owner->_jobs.ReservedMemoryBytes() >= gate->reservationBytes,
                        name, "final-geometry-weak-control-retains-charge-after-data-retirement"))
                return 5;
            memory.reset();
        }
        std::jthread weakWorker([held = std::move(weakGeometry)]() mutable { held.reset(); });
        weakWorker.join();
        if (!expect(gate->geometryMemory.expired(), name, "last-geometry-control-releases-semantic-accounting-owner"))
            return 5;
        gate->geometryMemory.reset();
        owner->_selectionMoveCloseTestGate.reset();
        if (!expect(settle() && owner->_jobs.ReservedMemoryBytes() == 0 && owner->_jobs.ReleasingMemoryBytes() == 0,
                    name, "final-control-refund-is-consumed-before-next-case"))
            return 5;
    }
    return failures == 0 ? 0 : 5;
}
