#include "Application.h"
#include "ChromeHost.h"
#include "LassoFinishGeometry.h"
#include "Runtime.Tests.ApplicationProcessingTestProbe.h"
#include "Runtime.Tests.LassoWorkTestGate.h"
#include "Tests/Support/Support.Tests.DocumentFixture.h"
#include "Tests/Support/Support.Tests.HistoryWorkFixture.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <new>
#include <utility>

namespace
{
CanvasDocument LassoHostInputFixture()
{
    DocumentFixture fixture;
    fixture.width = 64;
    fixture.height = 64;
    fixture.layers.push_back(ImageLayer::Blank(L"Lasso host input", Vec2{64, 64}));
    return std::move(fixture).Seal();
}

double LoopArea(const SelectionLoop& loop) noexcept
{
    double twiceArea = 0.0;
    for (std::size_t index = 0; index < loop.size(); ++index)
    {
        const Vec2 first = loop[index];
        const Vec2 second = loop[(index + 1) % loop.size()];
        twiceArea += first.x * second.y - second.x * first.y;
    }
    return std::fabs(twiceArea) * 0.5;
}

bool HasVertex(const SelectionLoop& loop, Vec2 expected) noexcept
{
    for (const Vec2 point : loop)
        if (point == expected)
            return true;
    return false;
}
} // namespace

int Application::RunStandaloneLassoHostInputSelfTest(HINSTANCE instance) noexcept
{
    unsigned int checks = 0;
    unsigned int failures = 0;
    const auto expect = [&](bool okay, const char* label) noexcept
    {
        std::printf("%s application-lasso-host-input.%s\n", okay ? "PASS" : "FAIL", label);
        ++checks;
        failures += okay ? 0u : 1u;
        return okay;
    };

    bool refuseMemory = false;
    ApplicationProcessingTestProbe processingProbe;
    processingProbe.memory = {[](void* raw, MemorySnapshot& output) noexcept
                              {
                                  const bool refuse = *static_cast<bool*>(raw);
                                  output = {refuse ? 0 : 8ULL * 1024 * 1024 * 1024,
                                            refuse ? 0 : 8ULL * 1024 * 1024 * 1024};
                                  return true;
                              },
                              &refuseMemory};
    std::unique_ptr<Application> owner;
    auto cleanup = wil::scope_exit(
        [&]() noexcept
        {
            if (owner)
            {
                owner->Shutdown();
                owner.reset();
                while (PumpMessages().quit)
                {
                }
            }
        });
    auto summary = wil::scope_exit(
        [&]() noexcept
        { std::printf("SUMMARY application-lasso-host-input checks=%u failures=%u\n", checks, failures); });

    const auto settle = [&]() noexcept
    { return SettleJobs(owner->_jobs, 30000, DispatchMessageThunk, owner.get()) == SettleResult::Settled; };

    owner = std::unique_ptr<Application>{new (std::nothrow) Application(instance, true, L"", 0, nullptr, nullptr)};
    if (!expect(bool(owner), "real-application-owner-allocated"))
        return 5;
    owner->_processingTestProbe = &processingProbe;
    owner->_quietUi = true;
    owner->_prompts = &QuietPrompts();
    if (!expect(SUCCEEDED(owner->InitializeProcessing()) && SUCCEEDED(owner->RegisterWindowClass()) &&
                    SUCCEEDED(owner->CreateMainWindow(false)) && SUCCEEDED(owner->InitializeRenderer()) &&
                    SUCCEEDED(owner->InitializeChrome()),
                "real-hidden-warp-window-renderer-chrome-and-scheduler"))
        return 5;
    if (!expect(owner->EnableCommittedDisplayForTest() && owner->EnableSelectionOutlineForTest() &&
                    owner->EnableLassoDraftDisplayForTest(),
                "real-committed-selection-and-lasso-display-owners-enabled"))
        return 5;

    auto fixture = LassoHostInputFixture();
    const Guid target = fixture.layers[0].id;
    owner->_session.ReplaceDocument(std::move(fixture), target);
    owner->DocumentChanged();
    if (!expect(settle() && owner->_acknowledgedCommittedDisplay && owner->_session.Document() &&
                    owner->_session.Document()->width == 64 && owner->_session.Document()->height == 64 &&
                    owner->_session.Document()->layers.size() == 1 &&
                    !owner->_session.Document()->selection.has_value(),
                "one-blank-64x64-document-display-settles"))
        return 5;
    owner->RenderFrame();

    for (unsigned int operation = 0; operation < 4; ++operation)
    {
        for (unsigned int commandFirst = 0; commandFirst < 2; ++commandFirst)
        {
            owner->_tool = operation == 3 ? EditorTool::Marquee : EditorTool::Lasso;
            owner->_session.lassoKind = LassoKind::Polygonal;
            owner->_session.marqueeKind = LassoKind::Rectangle;
            const auto revision = owner->_session.history.CurrentRevision();
            const auto history = owner->_session.history.UndoCount();
            if (!expect(owner->QueueLassoOpeningOwner({8, 8}, SelectionMode::New, operation == 3) && settle(),
                        "sample-command-order-opening-acquired"))
                return 5;
            const auto opening = owner->_lassoInputOpening;
            if (operation != 3)
            {
                owner->RecordLassoInputOwner(
                    owner->_lassoInput.Point(opening, {48, 8}, owner->NextLassoSampleOrdinalOwner()));
                owner->RecordLassoInputOwner(
                    owner->_lassoInput.Point(opening, {48, 48}, owner->NextLassoSampleOrdinalOwner()));
                if (!settle())
                    return 5;
            }
            const auto beforeCount = owner->_session.PendingLasso()->PointCount();
            ContinuationReceipt command;
            if (commandFirst)
                command = owner->QueueHostInput(HostToolInput{EditorTool::Move});
            const auto ordinal = owner->NextLassoSampleOrdinalOwner();
            const auto admitted = operation == 0   ? owner->_lassoInput.Point(opening, {8, 48}, ordinal)
                                  : operation == 1 ? owner->_lassoInput.Cursor(opening, Vec2{20, 32}, ordinal)
                                  : operation == 2
                                      ? owner->_lassoInput.Backspace(opening, ordinal)
                                      : owner->_lassoInput.Marquee(opening, {48, 32}, false, false, ordinal);
            owner->RecordLassoInputOwner(admitted);
            if (!commandFirst)
                command = owner->QueueHostInput(HostToolInput{EditorTool::Move});
            const auto* queued = owner->_hostRequests.Front();
            if (!expect(admitted == HostLassoInput::Admission::Recorded && ordinal != 0 && queued &&
                            (commandFirst ? queued->ordinal < ordinal : ordinal < queued->ordinal) &&
                            command.Read().outcome == HostOutcome::Pending,
                        "sample-command-order-literal-admission-before-dispatch"))
                return 5;
            owner->RunHostContinuation();
            const auto& draft = owner->_session.PendingLasso();
            const auto bounds = [&]() noexcept
            {
                const auto display = owner->_session.CaptureBoundedLassoDisplayForWork();
                return display ? display->display.points.Bounds() : std::optional<SelectionBounds>{};
            }();
            const bool applied =
                draft &&
                (operation == 0   ? draft->PointCount() == 4
                 : operation == 1 ? draft->PointCount() == 3 && draft->cursor == std::optional<Vec2>(Vec2{20, 32})
                 : operation == 2 ? draft->PointCount() == 2
                                  : draft->PointCount() == 4 && bounds && bounds->maxX == 48 && bounds->maxY == 32);
            const bool untouched = !draft || (draft->PointCount() == beforeCount && !draft->cursor);
            if (!expect(commandFirst ? !applied && untouched
                                     : applied && command.Read().outcome == HostOutcome::Pending,
                        commandFirst ? "older-command-wins-before-younger-point-cursor-backspace-or-marquee"
                                     : "older-point-cursor-backspace-or-marquee-replays-before-younger-command"))
                return 5;
            if (!expect(settle() && command.Read().outcome == HostOutcome::Succeeded &&
                            owner->_tool == EditorTool::Move && !owner->_lassoInput.Busy() &&
                            !owner->_session.PendingLasso() && owner->_session.history.CurrentRevision() == revision &&
                            owner->_session.history.UndoCount() == history,
                        "both-order-tool-acknowledgement-cancels-only-exact-draft-without-history"))
                return 5;
        }
    }

    for (unsigned int marquee = 0; marquee < 2; ++marquee)
    {
        for (unsigned int phase = 0; phase < 3; ++phase)
        {
            auto reset = LassoHostInputFixture();
            const auto active = reset.layers[0].id;
            owner->_session.ReplaceDocument(std::move(reset), active);
            owner->DocumentChanged();
            if (!settle())
                return 5;
            owner->_tool = marquee ? EditorTool::Marquee : EditorTool::Lasso;
            owner->_session.lassoKind = LassoKind::Polygonal;
            owner->_session.marqueeKind = LassoKind::Rectangle;
            const auto revision = owner->_session.history.CurrentRevision();
            const auto count = owner->_session.history.UndoCount();
            const auto before = marquee ? owner->_session.marqueeKind : owner->_session.lassoKind;
            const auto after = marquee ? LassoKind::Ellipse : LassoKind::Freehand;
            const bool queued = owner->QueueLassoOpeningOwner({8, 8}, SelectionMode::New, marquee != 0);
            const auto opening = owner->_lassoInputOpening;
            bool literal;
            if (marquee)
                literal =
                    owner->_lassoInput.Marquee(opening, {48, 32}, false, false, owner->NextLassoSampleOrdinalOwner()) ==
                    HostLassoInput::Admission::Recorded;
            else
                literal = owner->_lassoInput.Point(opening, {48, 8}, owner->NextLassoSampleOrdinalOwner()) ==
                              HostLassoInput::Admission::Recorded &&
                          owner->_lassoInput.Point(opening, {48, 48}, owner->NextLassoSampleOrdinalOwner()) ==
                              HostLassoInput::Admission::Recorded;
            if (!expect(queued && literal && owner->_lassoOpeningReceipt.Read().outcome == HostOutcome::Pending,
                        "kind-toggle-original-opening-and-literal-samples-admitted"))
                return 5;
            if (phase != 0 && !settle())
                return 5;
            if (phase == 2 && !owner->QueueLassoFinishOwner(false))
                return 5;
            const auto finish = owner->_lassoFinishReceipt;
            owner->OnCommand(marquee ? ChromeHost::kCommandToggleMarqueeKind : ChromeHost::kCommandToggleLassoKind);
            const auto current = marquee ? owner->_session.marqueeKind : owner->_session.lassoKind;
            if (!expect(phase == 1 ? !owner->_lassoInput.Busy() && !owner->_session.PendingLasso() && current == after
                                   : owner->_lassoInput.Busy() && current == before && owner->_hostRequests.Size() != 0,
                        phase == 1 ? "idle-kind-toggle-cancels-paired-descriptor-and-draft-immediately"
                                   : "queued-kind-toggle-cannot-overtake-original-opening-or-selected-finish"))
                return 5;
            if (!settle())
                return 5;
            const auto selection = owner->_session.Selection();
            const auto changedKind = marquee ? owner->_session.marqueeKind : owner->_session.lassoKind;
            const bool history =
                phase == 2
                    ? finish.Read().outcome == HostOutcome::Succeeded && selection && selection->loops.size() == 1 &&
                          std::fabs(LoopArea(selection->loops[0]) - (marquee ? 960.0 : 800.0)) <= 1e-9 &&
                          owner->_session.history.UndoCount() == count + 1
                    : owner->_session.history.CurrentRevision() == revision &&
                          owner->_session.history.UndoCount() == count && !selection;
            if (!(owner->_lassoOpeningReceipt.Read().outcome == HostOutcome::Succeeded && !owner->_lassoInput.Busy() &&
                  !owner->_session.PendingLasso() && changedKind == after && history))
                std::fprintf(
                    stderr,
                    "KIND DIAG marquee=%u phase=%u opening_outcome=%u opening_error=0x%08lx busy=%d draft=%d kind=%u "
                    "expected=%u history_ok=%d undo=%d before=%d revision_equal=%d selection=%d host_remaining=%zu\n",
                    marquee, phase, static_cast<unsigned int>(owner->_lassoOpeningReceipt.Read().outcome),
                    static_cast<unsigned long>(owner->_lassoOpeningReceipt.Read().error), owner->_lassoInput.Busy(),
                    owner->_session.PendingLasso().has_value(), static_cast<unsigned int>(changedKind),
                    static_cast<unsigned int>(after), history, owner->_session.history.UndoCount(), count,
                    owner->_session.history.CurrentRevision() == revision, selection.has_value(),
                    owner->_hostRequests.Size());
            if (!expect(owner->_lassoOpeningReceipt.Read().outcome == HostOutcome::Succeeded &&
                            !owner->_lassoInput.Busy() && !owner->_session.PendingLasso() && changedKind == after &&
                            history,
                        "kind-toggle-acknowledges-original-owners-with-exact-history-and-literal-finish"))
                return 5;
            const bool reopened = owner->QueueLassoOpeningOwner({12, 12}, SelectionMode::New, marquee != 0) && settle();
            const auto* context = owner->_lassoInput.CapturedContext(owner->_lassoInput.Opening());
            const bool changedOpening = context && context->kind == after && owner->_lassoInput.Opening() > opening &&
                                        owner->_session.PendingLasso() && owner->_session.PendingLasso()->kind == after;
            owner->CancelLassoInputOwner(owner->_lassoInput.Opening(), HostOutcome::Cancelled);
            owner->SelectionChanged();
            if (!expect(reopened && changedOpening && settle() && !owner->_lassoInput.Busy() &&
                            !owner->_session.PendingLasso() &&
                            owner->_session.history.UndoCount() == count + (phase == 2 ? 1 : 0),
                        "kind-toggle-next-opening-uses-new-kind-and-retires-without-orphan-or-extra-history"))
                return 5;
        }
    }

    // The same literal triangle must survive both hard-edge and antialiased selection publication.
    for (unsigned int aa = 0; aa < 2; ++aa)
    {
        owner->_session.lassoKind = LassoKind::Freehand;
        owner->_session.selectionAntialiased = aa != 0;
        const auto historyBefore = owner->_session.history.UndoCount();
        if (!expect(owner->QueueLassoOpeningOwner({8, 8}, SelectionMode::New, false),
                    "triangle-opening-request-admitted"))
            return 5;
        const std::uint64_t opening = owner->_lassoInputOpening;
        const auto p1 = owner->_lassoInput.Point(opening, {48, 8});
        const auto p2 = owner->_lassoInput.Point(opening, {48, 48});
        if (!expect(p1 == HostLassoInput::Admission::Recorded && p2 == HostLassoInput::Admission::Recorded &&
                        owner->QueueLassoFinishOwner(aa != 0),
                    "triangle-points-and-finish-recorded-before-opening-settles"))
            return 5;
        if (!expect(!owner->_session.PendingLasso() && owner->_lassoAcquiredOpening == 0 &&
                        owner->_lassoOpeningReceipt.Read().outcome == HostOutcome::Pending,
                    "opening-does-not-publish-or-finish-before-acquisition"))
            return 5;
        if (!expect(settle(), "triangle-opening-replay-and-finish-acknowledged"))
            return 5;
        const auto selection = owner->_session.Selection();
        if (!expect(selection && selection->antialiased == (aa != 0) && selection->loops.size() == 1 &&
                        selection->loops[0].size() == 3 && HasVertex(selection->loops[0], {8, 8}) &&
                        HasVertex(selection->loops[0], {48, 8}) && HasVertex(selection->loops[0], {48, 48}) &&
                        std::fabs(LoopArea(selection->loops[0]) - 800.0) <= 1e-9,
                    aa ? "aa-triangle-literal-vertices-and-area-800"
                       : "hard-edge-triangle-literal-vertices-and-area-800"))
            return 5;
        if (!expect(owner->_session.history.UndoCount() == historyBefore + 1 &&
                        owner->_session.history.UndoName() == L"Lasso" && !owner->_session.PendingLasso() &&
                        owner->_lassoOpeningReceipt.Read().outcome == HostOutcome::Succeeded &&
                        owner->_lassoFinishReceipt.Read().outcome == HostOutcome::Succeeded,
                    "triangle-commits-one-lasso-entry-and-both-distinct-receipts-succeed"))
            return 5;
    }

    {
        // FIFO: four vertices, a display-only cursor, Backspace, then a replacement fourth vertex.
        owner->_session.lassoKind = LassoKind::Polygonal;
        const auto polygonHistoryBefore = owner->_session.history.UndoCount();
        if (!expect(owner->QueueLassoOpeningOwner({8, 8}, SelectionMode::New, false),
                    "polygon-opening-request-admitted"))
            return 5;
        const std::uint64_t polygonOpening = owner->_lassoInputOpening;
        const auto p1 = owner->_lassoInput.Point(polygonOpening, {48, 8});
        const auto p2 = owner->_lassoInput.Point(polygonOpening, {48, 48});
        const auto p3 = owner->_lassoInput.Point(polygonOpening, {8, 48});
        const auto cursor = owner->_lassoInput.Cursor(polygonOpening, Vec2{20, 32});
        const auto pop = owner->_lassoInput.Backspace(polygonOpening);
        const auto replacement = owner->_lassoInput.Point(polygonOpening, {8, 40});
        if (!expect(p1 == HostLassoInput::Admission::Recorded && p2 == HostLassoInput::Admission::Recorded &&
                        p3 == HostLassoInput::Admission::Recorded && cursor == HostLassoInput::Admission::Recorded &&
                        pop == HostLassoInput::Admission::Recorded &&
                        replacement == HostLassoInput::Admission::Recorded,
                    "polygon-points-cursor-backspace-and-replacement-recorded-in-fifo-before-acquisition"))
            return 5;
        if (!expect(!owner->_session.PendingLasso() && owner->_lassoAcquiredOpening == 0 &&
                        owner->_lassoOpeningReceipt.Read().outcome == HostOutcome::Pending,
                    "polygon-input-is-unpublished-before-opening-acquisition"))
            return 5;
        if (!expect(settle(), "polygon-opening-and-buffered-edits-acknowledged"))
            return 5;
        const auto* livePolygon = owner->_session.PendingLasso() ? &owner->_session.PendingLasso().value() : nullptr;
        if (!expect(livePolygon && livePolygon->IsBounded() && livePolygon->points.empty() &&
                        livePolygon->PointCount() == 4 && livePolygon->cursor == std::optional<Vec2>(Vec2{20, 32}) &&
                        !owner->_lassoFinishTicket && owner->_session.history.UndoCount() == polygonHistoryBefore &&
                        owner->QueueLassoFinishOwner(true),
                    "acquisition-keeps-four-point-bounded-draft-open-without-vector-or-implicit-finish"))
            return 5;
        if (!expect(settle(), "polygon-explicit-finish-acknowledged"))
            return 5;
        const auto polygon = owner->_session.Selection();
        if (!expect(polygon && polygon->loops.size() == 1 && polygon->loops[0].size() == 4 &&
                        HasVertex(polygon->loops[0], {8, 8}) && HasVertex(polygon->loops[0], {48, 8}) &&
                        HasVertex(polygon->loops[0], {48, 48}) && HasVertex(polygon->loops[0], {8, 40}) &&
                        !HasVertex(polygon->loops[0], {8, 48}) && !HasVertex(polygon->loops[0], {20, 32}) &&
                        std::fabs(LoopArea(polygon->loops[0]) - 1440.0) <= 1e-9,
                    "polygon-has-replacement-vertices-not-cursor-and-independent-area-1440"))
            return 5;
        if (!expect(owner->_session.history.UndoCount() == polygonHistoryBefore + 1 &&
                        owner->_session.history.UndoName() == L"Polygonal Lasso" &&
                        owner->_lassoOpeningReceipt.Read().outcome == HostOutcome::Succeeded &&
                        owner->_lassoFinishReceipt.Read().outcome == HostOutcome::Succeeded &&
                        !owner->_session.PendingLasso(),
                    "polygon-commits-one-entry-with-two-success-receipts-and-no-draft"))
            return 5;
    }
    {
        // With anchor (24,24) and drag (32,28), square + from-center yields [16,16]..[32,32].
        owner->_session.marqueeKind = LassoKind::Rectangle;
        owner->_session.selectionAntialiased = false;
        const auto marqueeHistoryBefore = owner->_session.history.UndoCount();
        if (!expect(owner->QueueLassoOpeningOwner({24, 24}, SelectionMode::New, true),
                    "rectangle-marquee-opening-request-admitted"))
            return 5;
        const std::uint64_t marqueeOpening = owner->_lassoInputOpening;
        const auto marquee = owner->_lassoInput.Marquee(marqueeOpening, {32, 28}, true, true);
        if (!expect(marquee == HostLassoInput::Admission::Recorded && owner->QueueLassoFinishOwner(false) &&
                        !owner->_session.PendingLasso() &&
                        owner->_lassoOpeningReceipt.Read().outcome == HostOutcome::Pending,
                    "square-from-center-marquee-recorded-before-opening-acquisition"))
            return 5;
        if (!expect(settle(), "rectangle-marquee-opening-and-finish-acknowledged"))
            return 5;
        const auto marqueeSelection = owner->_session.Selection();
        const auto marqueeBounds = marqueeSelection ? marqueeSelection->Bounds() : std::nullopt;
        if (!expect(marqueeSelection && !marqueeSelection->antialiased && marqueeBounds &&
                        marqueeBounds->minX == 16.0 && marqueeBounds->minY == 16.0 && marqueeBounds->maxX == 32.0 &&
                        marqueeBounds->maxY == 32.0 && marqueeSelection->loops.size() == 1 &&
                        std::fabs(LoopArea(marqueeSelection->loops[0]) - 256.0) <= 1e-9,
                    "square-from-center-marquee-has-exact-whole-pixel-bounds"))
            return 5;
        if (!expect(owner->_session.history.UndoCount() == marqueeHistoryBefore + 1 &&
                        owner->_session.history.UndoName() == L"Rectangular Marquee" &&
                        owner->_lassoOpeningReceipt.Read().outcome == HostOutcome::Succeeded &&
                        owner->_lassoFinishReceipt.Read().outcome == HostOutcome::Succeeded &&
                        !owner->_session.PendingLasso(),
                    "marquee-commits-one-named-history-entry-and-no-draft"))
            return 5;
    }
    {
        // The physical terminal precedes the younger Deselect even when no opening worker has joined.
        owner->_session.lassoKind = LassoKind::Freehand;
        owner->_session.selectionAntialiased = false;
        const auto count = owner->_session.history.UndoCount();
        if (!expect(owner->QueueLassoOpeningOwner({8, 8}, SelectionMode::New, false), "fifo-opening-request-admitted"))
            return 5;
        const auto opening = owner->_lassoInputOpening;
        if (!expect(owner->_lassoInput.Point(opening, {48, 8}) == HostLassoInput::Admission::Recorded &&
                        owner->_lassoInput.Point(opening, {48, 48}) == HostLassoInput::Admission::Recorded &&
                        owner->QueueLassoFinishOwner(false),
                    "fifo-physical-terminal-admitted-before-younger-command"))
            return 5;
        const auto finish = owner->_lassoFinishReceipt;
        const auto younger = owner->QueueHostInput(HostCommandInput{ChromeHost::kCommandDeselect});
        if (!expect(finish.Read().outcome == HostOutcome::Pending && younger.Read().outcome == HostOutcome::Pending &&
                        settle(),
                    "fifo-end-and-younger-command-acknowledged"))
            return 5;
        if (!expect(finish.Read().outcome == HostOutcome::Succeeded &&
                        younger.Read().outcome == HostOutcome::Succeeded && !owner->_session.Selection() &&
                        owner->_session.history.UndoCount() == count + 2 &&
                        owner->_session.history.UndoName() == L"Deselect",
                    "fifo-end-publishes-one-lasso-before-younger-deselect"))
            return 5;
    }
    {
        owner->_tool = EditorTool::Lasso;
        owner->_session.lassoKind = LassoKind::Freehand;
        owner->_session.selectionModeChoice = SelectionMode::New;
        owner->_session.selectionAntialiased = false;
        owner->_viewport.zoom = 1.0;
        const auto count = owner->_session.history.UndoCount();
        const auto view = [&](Vec2 pixel) noexcept
        { return owner->_viewport.ViewFromDocument(pixel, owner->DocumentSize()); };
        owner->_lastPressTick = 0;
        owner->SelectionPress(view({8, 8}), 0, POINT{100, 100});
        const bool first = owner->SelectionPointerMove(view({48, 8}), MK_LBUTTON);
        const bool second = owner->SelectionPointerMove(view({48, 48}), MK_LBUTTON);
        owner->SelectionRelease(view({48, 48}));
        if (!expect(first && second && owner->_lassoInput.Busy() && owner->_lassoFinishQueued &&
                        !owner->_session.PendingLasso(),
                    "physical-freehand-handlers-buffer-before-owner-acquisition"))
            return 5;
        if (!expect(settle(), "physical-freehand-handlers-opening-replay-and-terminal-acknowledged"))
            return 5;
        const auto selection = owner->_session.Selection();
        if (!expect(selection && !selection->antialiased && selection->loops.size() == 1 &&
                        selection->loops[0].size() == 3 && HasVertex(selection->loops[0], {8, 8}) &&
                        HasVertex(selection->loops[0], {48, 8}) && HasVertex(selection->loops[0], {48, 48}) &&
                        std::fabs(LoopArea(selection->loops[0]) - 800.0) < 1e-9 &&
                        owner->_session.history.UndoCount() == count + 1 &&
                        owner->_session.history.UndoName() == L"Lasso",
                    "physical-freehand-handlers-publish-literal-triangle-and-one-history-entry"))
            return 5;
    }
    for (unsigned int nearEarly = 0; nearEarly < 2; ++nearEarly)
    {
        owner->_tool = EditorTool::Lasso;
        owner->_session.lassoKind = LassoKind::Polygonal;
        owner->_session.selectionModeChoice = SelectionMode::New;
        owner->_session.selectionAntialiased = false;
        owner->_viewport.zoom = 1.0;
        const auto count = owner->_session.history.UndoCount();
        const auto view = [&](Vec2 pixel) noexcept
        { return owner->_viewport.ViewFromDocument(pixel, owner->DocumentSize()); };
        const auto press = [&](Vec2 point) noexcept
        {
            // These cases isolate the proximity/three-corner rule from the separate system double-click rule.
            owner->_lastPressTick = 0;
            owner->SelectionPress(view(point), 0, POINT{100, 100});
            owner->SelectionRelease(view(point));
        };
        const Vec2 first = nearEarly ? Vec2{4, 60} : Vec2{12, 44};
        const Vec2 second = nearEarly ? Vec2{8, 60} : Vec2{44, 44};
        const Vec2 third = nearEarly ? Vec2{40, 20} : Vec2{44, 12};
        const Vec2 fourth = nearEarly ? Vec2{4, 20} : Vec2{12, 12};
        press(first);
        press(second);
        press(third);
        press(fourth);
        press(first);
        if (!expect(owner->_lassoInput.Busy() && !owner->_session.PendingLasso(),
                    "physical-polygon-close-input-remains-unpublished-before-acquisition"))
            return 5;
        if (!expect(settle(), "physical-polygon-corners-and-proximity-barriers-acknowledged-in-order"))
            return 5;
        const auto selection = owner->_session.Selection();
        if (!expect(selection && selection->loops.size() == 1 && selection->loops[0].size() == 4 &&
                        HasVertex(selection->loops[0], first) && HasVertex(selection->loops[0], second) &&
                        HasVertex(selection->loops[0], third) && HasVertex(selection->loops[0], fourth) &&
                        std::fabs(LoopArea(selection->loops[0]) - (nearEarly ? 800.0 : 1024.0)) < 1e-9,
                    nearEarly ? "early-near-first-click-keeps-two-corners-and-final-close-area-800"
                              : "pending-four-corner-proximity-close-publishes-square-area-1024"))
            return 5;
        if (!expect(owner->_session.history.UndoCount() == count + 1 &&
                        owner->_session.history.UndoName() == L"Polygonal Lasso" &&
                        owner->_lassoFinishReceipt.Read().outcome == HostOutcome::Succeeded &&
                        !owner->_lassoInput.Busy() && !owner->_session.PendingLasso(),
                    "physical-polygon-conditional-close-publishes-one-entry-and-retires-gesture"))
            return 5;
    }
    {
        // Explicit cancellation owns the exact queued opening outcome before any worker capture.
        const auto cancelHistoryBefore = owner->_session.history.UndoCount();
        const auto cancelRevisionBefore = owner->_session.history.CurrentRevision();
        const auto selectionBeforeCancel = owner->_session.Selection();
        if (!expect(owner->QueueLassoOpeningOwner({8, 8}, SelectionMode::New, false),
                    "cancel-case-opening-request-admitted"))
            return 5;
        const std::uint64_t cancelledOpening = owner->_lassoInputOpening;
        const auto cancelledReceipt = owner->_lassoOpeningReceipt;
        owner->CancelLassoInputOwner(cancelledOpening, HostOutcome::Cancelled);
        if (!expect(!owner->_lassoInput.Busy() && !owner->_session.PendingLasso() &&
                        owner->_lassoOpeningReceipt.Read().outcome == HostOutcome::Pending,
                    "immediate-cancel-drops-unacquired-draft-before-worker"))
            return 5;
        if (!expect(settle(), "cancelled-opening-retires-through-real-host-continuation-pump"))
            return 5;
        if (!expect(cancelledReceipt.Read().outcome == HostOutcome::Cancelled &&
                        owner->_session.history.UndoCount() == cancelHistoryBefore &&
                        owner->_session.history.CurrentRevision() == cancelRevisionBefore &&
                        owner->_session.Selection() == selectionBeforeCancel && !owner->_session.PendingLasso(),
                    "cancel-receipt-is-exactly-cancelled-with-zero-history-or-document-mutation"))
            return 5;
    }
    // Insert this member-scope block in RunStandaloneLassoHostInputSelfTest before its queued-shutdown case.
    // It uses the existing owner, expect, and settle locals; it intentionally introduces no retained selection copies.
    {
        owner->_tool = EditorTool::Lasso;
        owner->_session.lassoKind = LassoKind::Polygonal;
        owner->_session.selectionModeChoice = SelectionMode::New;
        owner->_session.selectionAntialiased = false;
        owner->_viewport.zoom = 1.0;
        owner->_session.SelectAll(); // Independent 64x64 full-canvas selection reference.
        owner->DocumentChanged();
        const auto newDoubleHistory = owner->_session.history.UndoCount();
        const auto newDoubleRevision = owner->_session.history.CurrentRevision();
        const auto view = [&](Vec2 point) noexcept
        { return owner->_viewport.ViewFromDocument(point, owner->DocumentSize()); };
        const POINT sameClient{100, 100};
        owner->_lastPressTick = 0;
        owner->SelectionPress(view({-8, -8}), 0, sameClient);
        owner->SelectionRelease(view({-8, -8}));
        owner->_lastPressTick = GetTickCount64();
        owner->_lastPressPoint = sameClient;
        owner->SelectionPress(view({-7, -8}), 0, sameClient); // Physical system double-click closes a one-point draft.
        owner->SelectionRelease(view({-7, -8}));
        if (!expect(owner->_lassoInput.Busy() && settle() &&
                        owner->_lassoFinishReceipt.Read().outcome == HostOutcome::Succeeded &&
                        !owner->_session.Selection() && !owner->_session.PendingLasso() &&
                        owner->_session.history.UndoCount() == newDoubleHistory + 1 &&
                        owner->_session.history.CurrentRevision() != newDoubleRevision &&
                        owner->_session.history.UndoName() == L"Deselect",
                    "short-new-double-click-deselects-existing-selection-once"))
            return 5;
    }
    {
        owner->_session.SelectAll();
        owner->DocumentChanged();
        owner->_session.selectionModeChoice = SelectionMode::Add;
        const auto addHistory = owner->_session.history.UndoCount();
        const auto addRevision = owner->_session.history.CurrentRevision();
        const auto view = [&](Vec2 point) noexcept
        { return owner->_viewport.ViewFromDocument(point, owner->DocumentSize()); };
        const POINT sameClient{100, 100};
        owner->_lastPressTick = 0;
        owner->SelectionPress(view({-8, -8}), 0, sameClient);
        owner->SelectionRelease(view({-8, -8}));
        owner->_lastPressTick = GetTickCount64();
        owner->_lastPressPoint = sameClient;
        owner->SelectionPress(view({-7, -8}), 0, sameClient);
        owner->SelectionRelease(view({-7, -8}));
        if (!expect(owner->_lassoInput.Busy() && settle() &&
                        owner->_lassoFinishReceipt.Read().outcome == HostOutcome::Succeeded &&
                        owner->_session.Selection().has_value() && owner->_session.Selection()->loops.size() == 1 &&
                        HasVertex(owner->_session.Selection()->loops[0], {0, 0}) &&
                        HasVertex(owner->_session.Selection()->loops[0], {64, 64}) &&
                        owner->_session.history.UndoCount() == addHistory &&
                        owner->_session.history.CurrentRevision() == addRevision,
                    "short-add-double-click-keeps-literal-full-canvas-selection-and-revision"))
            return 5;
    }
    {
        owner->_session.selectionModeChoice = SelectionMode::New;
        const auto enterHistory = owner->_session.history.UndoCount();
        const auto enterRevision = owner->_session.history.CurrentRevision();
        const auto view = [&](Vec2 point) noexcept
        { return owner->_viewport.ViewFromDocument(point, owner->DocumentSize()); };
        const POINT sameClient{100, 100};
        owner->_lastPressTick = 0;
        owner->SelectionPress(view({-8, -8}), 0, sameClient);
        owner->SelectionRelease(view({-8, -8}));
        if (!expect(owner->_lassoInput.Busy(), "short-enter-case-physical-opening-is-live"))
            return 5;
        owner->_chrome->SetCanvasFocused(true);
        owner->RenderFrame(); // Publish the current DxUi canvas layout before real WM_KEYDOWN routing.
        const bool enterHandled = owner->HandleAccelerator(VK_RETURN, 0);
        if (!expect(enterHandled && settle() && owner->_lassoFinishReceipt.Read().outcome == HostOutcome::Succeeded &&
                        !owner->_session.Selection() && !owner->_session.PendingLasso() &&
                        owner->_session.history.UndoCount() == enterHistory + 1 &&
                        owner->_session.history.CurrentRevision() != enterRevision &&
                        owner->_session.history.UndoName() == L"Deselect",
                    "real-enter-finishes-short-new-draft-as-one-deselect"))
            return 5;
    }
    {
        owner->_session.lassoKind = LassoKind::Polygonal;
        owner->_session.selectionModeChoice = SelectionMode::New;
        const auto cancelHistory = owner->_session.history.UndoCount();
        const auto cancelRevision = owner->_session.history.CurrentRevision();
        const auto view = [&](Vec2 point) noexcept
        { return owner->_viewport.ViewFromDocument(point, owner->DocumentSize()); };
        const POINT sameClient{100, 100};
        const auto press = [&](Vec2 point) noexcept
        {
            owner->_lastPressTick = 0;
            owner->SelectionPress(view(point), 0, sameClient);
            owner->SelectionRelease(view(point));
        };
        press({8, 8});
        press({48, 8});
        press({48, 48});
        press({8, 48});
        if (!expect(settle() && owner->_session.PendingLasso().has_value() &&
                        owner->_session.PendingLasso()->PointCount() == 4 && owner->_lassoInput.Busy(),
                    "four-literal-corners-acquired-as-live-polygon"))
            return 5;
        owner->_chrome->SetCanvasFocused(true);
        owner->RenderFrame();
        const bool backHandled = owner->HandleAccelerator(VK_BACK, 0);
        const bool deleteHandled = owner->HandleAccelerator(VK_DELETE, 0);
        if (!expect(backHandled && deleteHandled && settle() && owner->_session.PendingLasso().has_value() &&
                        owner->_session.PendingLasso()->PointCount() == 2,
                    "real-backspace-and-delete-remove-two-corners-through-acknowledged-input"))
            return 5;
        owner->_chrome->SetCanvasFocused(true);
        owner->RenderFrame();
        const bool escapeHandled = owner->HandleAccelerator(VK_ESCAPE, 0);
        if (!expect(escapeHandled && settle() && !owner->_session.PendingLasso() && !owner->_lassoInput.Busy() &&
                        owner->_session.history.UndoCount() == cancelHistory &&
                        owner->_session.history.CurrentRevision() == cancelRevision && !owner->_session.Selection(),
                    "real-escape-retires-edited-draft-with-zero-history-or-document-mutation"))
            return 5;
    }

    for (unsigned int fault = 0; fault < 4; ++fault)
    {
        auto gate = std::make_shared<LassoWorkTestGate>();
        if (!expect(bool(gate->release), "fault-gate-acquires-original-owned-event"))
            return 5;
        processingProbe.lassoWork = gate;
        owner->_session.lassoKind = LassoKind::Freehand;
        const auto selection = owner->_session.Selection();
        const auto revision = owner->_session.history.CurrentRevision();
        const auto generation = owner->_session.Generation();
        const auto count = owner->_session.history.UndoCount();
        const bool openingFault = fault < 2;
        if (openingFault)
            gate->failAt =
                fault == 0 ? LassoWorkTestGate::Stage::OpeningAllocated : LassoWorkTestGate::Stage::OpeningPrepared;
        if (!expect(owner->QueueLassoOpeningOwner({60, 4}, SelectionMode::New, false),
                    "fault-original-opening-descriptor-admitted"))
            return 5;
        ContinuationReceipt result = owner->_lassoOpeningReceipt;
        if (!openingFault)
        {
            if (!expect(settle() && owner->_session.PendingLasso() && owner->_session.PendingLasso()->IsBounded(),
                        "fault-finish-source-acquired-from-original-worker"))
                return 5;
            gate->failAt =
                fault == 2 ? LassoWorkTestGate::Stage::FinishAllocated : LassoWorkTestGate::Stage::FinishPrepared;
            const auto opening = owner->_lassoInputOpening;
            const bool recorded = owner->_lassoInput.Point(opening, {44, 4}) == HostLassoInput::Admission::Recorded &&
                                  owner->_lassoInput.Point(opening, {44, 20}) == HostLassoInput::Admission::Recorded;
            if (!expect(recorded && owner->QueueLassoFinishOwner(false), "fault-original-finish-terminal-admitted"))
                return 5;
            result = owner->_lassoFinishReceipt;
        }
        if (!expect(settle() && result.Read().outcome == HostOutcome::Failed && result.Read().error == E_OUTOFMEMORY,
                    "reached-partial-allocation-fault-preserves-exact-original-runner-error"))
            return 5;
        if (!expect(owner->_session.Selection() == selection && owner->_session.history.CurrentRevision() == revision &&
                        owner->_session.Generation() == generation && owner->_session.history.UndoCount() == count &&
                        !owner->_lassoInput.Busy() && !owner->_session.PendingLasso(),
                    "reached-partial-allocation-fault-publishes-no-document-history-or-draft"))
            return 5;
        const auto captures = gate->openingCaptures.load();
        const auto finishCaptures = gate->finishCaptures.load();
        if (!expect(captures == (fault == 0 ? 0u : 1u) && finishCaptures == (fault == 3 ? 1u : 0u) &&
                        gate->retired.load() && gate->retiredOffOwner.load() && gate->openingMemory.expired() &&
                        gate->finishMemory.expired() && gate->finishOutputMemory.expired(),
                    "reached-partial-fault-capture-order-and-all-original-memory-retire-off-owner"))
            return 5;
        processingProbe.lassoWork.reset();
    }
    for (unsigned int finishStage = 0; finishStage < 2; ++finishStage)
    {
        auto gate = std::make_shared<LassoWorkTestGate>();
        if (!expect(bool(gate->release), "held-original-job-owns-shutdown-aware-event"))
            return 5;
        processingProbe.lassoWork = gate;
        owner->_session.lassoKind = LassoKind::Freehand;
        const auto selection = owner->_session.Selection();
        const auto revision = owner->_session.history.CurrentRevision();
        const auto generation = owner->_session.Generation();
        const auto count = owner->_session.history.UndoCount();
        if (!finishStage)
            gate->holdAt = LassoWorkTestGate::Stage::OpeningPrepared;
        if (!expect(owner->QueueLassoOpeningOwner({60, 4}, SelectionMode::New, false),
                    "held-original-opening-descriptor-admitted"))
            return 5;
        ContinuationReceipt result = owner->_lassoOpeningReceipt;
        if (finishStage)
        {
            if (!expect(settle() && owner->_session.PendingLasso(), "held-finish-original-arena-acquired"))
                return 5;
            gate->holdAt = LassoWorkTestGate::Stage::FinishPrepared;
            const auto opening = owner->_lassoInputOpening;
            const bool recorded = owner->_lassoInput.Point(opening, {44, 4}) == HostLassoInput::Admission::Recorded &&
                                  owner->_lassoInput.Point(opening, {44, 20}) == HostLassoInput::Admission::Recorded;
            if (!expect(recorded && owner->QueueLassoFinishOwner(false), "held-original-finish-terminal-admitted"))
                return 5;
            result = owner->_lassoFinishReceipt;
        }
        struct DispatchControl final
        {
            Application* owner;
            LassoWorkTestGate* gate;
            ContinuationReceipt receipt;
            bool reached = false;
            bool pendingAfterCancel = false;
        } dispatch{owner.get(), gate.get(), result};
        const auto callback = [](void* raw, void* native) noexcept
        {
            auto& control = *static_cast<DispatchControl*>(raw);
            const auto& message = *static_cast<MSG*>(native);
            if (!message.hwnd && message.message == LassoWorkTestGate::kMessage)
            {
                control.reached = true;
                control.owner->CancelHostContinuations();
                control.pendingAfterCancel = control.receipt.Read().outcome == HostOutcome::Pending;
                (void)SetEvent(control.gate->release.get());
                return;
            }
            DispatchMessageThunk(control.owner, native);
        };
        if (!expect(SettleJobs(owner->_jobs, 30000, callback, &dispatch) == SettleResult::Settled && dispatch.reached &&
                        dispatch.pendingAfterCancel && gate->visits.load() == 1,
                    "actual-owner-cancel-keeps-original-receipt-pending-through-worker-acknowledgement"))
            return 5;
        if (!expect(result.Read().outcome == HostOutcome::Cancelled && result.Read().error == E_ABORT &&
                        owner->_session.Selection() == selection &&
                        owner->_session.history.CurrentRevision() == revision &&
                        owner->_session.Generation() == generation && owner->_session.history.UndoCount() == count &&
                        !owner->_lassoInput.Busy() && !owner->_session.PendingLasso(),
                    "actual-held-original-cancel-publishes-no-document-history-or-draft"))
            return 5;
        if (!expect(gate->retired.load() && gate->retiredOffOwner.load() && gate->openingMemory.expired() &&
                        gate->finishMemory.expired() && gate->finishOutputMemory.expired(),
                    "held-original-cancel-retires-all-memory-after-creator-and-output-join"))
            return 5;
        processingProbe.lassoWork.reset();
    }
    {
        const auto selection = owner->_session.Selection();
        const auto revision = owner->_session.history.CurrentRevision();
        const auto generation = owner->_session.Generation();
        auto gate = std::make_shared<LassoWorkTestGate>();
        processingProbe.lassoWork = gate;
        refuseMemory = true;
        const bool queued = owner->QueueLassoOpeningOwner({60, 4}, SelectionMode::New, false);
        const auto result = owner->_lassoOpeningReceipt;
        if (!expect(queued && settle() && result.Read().outcome == HostOutcome::Failed,
                    "whole-opening-admission-refuses-before-worker-source-capture"))
            return 5;
        refuseMemory = false;
        if (!expect(settle() && owner->_session.Selection() == selection &&
                        owner->_session.history.CurrentRevision() == revision &&
                        owner->_session.Generation() == generation && !owner->_lassoInput.Busy() &&
                        !owner->_session.PendingLasso(),
                    "whole-opening-memory-refusal-preserves-document-and-no-reference-fallback"))
            return 5;
        if (!expect(gate->openingCaptures.load() == 0 && gate->finishCaptures.load() == 0 && !gate->retired.load() &&
                        gate->openingMemory.expired(),
                    "whole-memory-refusal-creates-no-worker-payload-or-arena"))
            return 5;
        processingProbe.lassoWork.reset();
    }
    owner->_session.Deselect();
    owner->SelectionChanged();
    if (!settle())
        return 5;
    for (unsigned int terminalSelected = 0; terminalSelected < 2; ++terminalSelected)
    {
        owner->_tool = EditorTool::Lasso;
        owner->_session.lassoKind = LassoKind::Polygonal;
        const auto revision = owner->_session.history.CurrentRevision();
        const auto count = owner->_session.history.UndoCount();
        if (!expect(owner->QueueLassoOpeningOwner({8, 8}, SelectionMode::New, false) && settle(),
                    "ordinal-boundary-original-opening-acquired"))
            return 5;
        const auto opening = owner->_lassoInputOpening;
        if (terminalSelected)
        {
            owner->RecordLassoInputOwner(
                owner->_lassoInput.Point(opening, {48, 8}, owner->NextLassoSampleOrdinalOwner()));
            owner->RecordLassoInputOwner(
                owner->_lassoInput.Point(opening, {48, 48}, owner->NextLassoSampleOrdinalOwner()));
            if (!settle() || !owner->QueueLassoFinishOwner(false))
                return 5;
        }
        const auto finish = owner->_lassoFinishReceipt;
        const auto originalNext = HistoryWorkFixture::NextValue(owner->_historyWork);
        if (!expect(HistoryWorkFixture::SetNext(owner->_historyWork, (std::numeric_limits<std::uint64_t>::max)()) &&
                        !owner->_historyWork.NextOrdinal(),
                    "actual-shared-clock-at-uint64-max-refuses-without-wrap"))
            return 5;
        const auto view = owner->_viewport.ViewFromDocument({20, 32}, owner->DocumentSize());
        (void)owner->SelectionPointerMove(view, 0);
        owner->RecordLassoInputOwner(owner->_lassoInput.Backspace(opening, owner->NextLassoSampleOrdinalOwner()));
        owner->RecordLassoPolygonPressOwner({8, 8}, true, true, true);
        if (!expect(terminalSelected ? owner->_lassoInput.Busy() && owner->_session.PendingLasso() &&
                                           finish.Read().outcome == HostOutcome::Pending &&
                                           owner->_session.history.CurrentRevision() == revision
                                     : !owner->_lassoInput.Busy() && !owner->_session.PendingLasso() &&
                                           owner->_session.history.CurrentRevision() == revision,
                    terminalSelected ? "closed-terminal-ignores-late-move-backspace-and-press-at-clock-exhaustion"
                                     : "live-stream-clock-exhaustion-drops-only-exact-draft-before-late-sample"))
            return 5;
        const bool settled = settle();
        const auto selection = owner->_session.Selection();
        const bool result =
            terminalSelected
                ? finish.Read().outcome == HostOutcome::Succeeded && selection && selection->loops.size() == 1 &&
                      selection->loops[0].size() == 3 && std::fabs(LoopArea(selection->loops[0]) - 800) <= 1e-9 &&
                      owner->_session.history.UndoCount() == count + 1
                : owner->_session.history.CurrentRevision() == revision && owner->_session.history.UndoCount() == count;
        if (!expect(settled && result && !owner->_lassoInput.Busy() && !owner->_session.PendingLasso() &&
                        HistoryWorkFixture::SetNext(owner->_historyWork, originalNext),
                    "clock-boundary-settles-original-terminal-or-live-refusal-with-exact-history"))
            return 5;
    }
    // Leave the original opening queued; real shutdown must retire its receipt and all reservations.
    if (!expect(owner->QueueLassoOpeningOwner({8, 8}, SelectionMode::New, false),
                "shutdown-case-original-opening-request-admitted"))
        return 5;
    const auto shutdownReceipt = owner->_lassoOpeningReceipt;
    owner->Shutdown();
    if (!expect(owner->_shutdownComplete && shutdownReceipt.Read().outcome == HostOutcome::Cancelled &&
                    !owner->_lassoInput.Busy() && !owner->_session.PendingLasso() &&
                    owner->_jobs.ReservedMemoryBytes() == 0 && owner->_graphGpuBudget.ReservedOwner() == 0,
                "shutdown-cancels-queued-opening-joins-and-refunds-to-zero-with-no-draft"))
        return 5;

    (void)cleanup.release();
    (void)summary.release();
    owner.reset();
    while (PumpMessages().quit)
    {
    }
    std::printf("SUMMARY application-lasso-host-input checks=%u failures=%u\n", checks, failures);
    return failures ? 5 : 0;
}