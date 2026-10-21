#include "Application.h"

#include <algorithm>

#include "ChromeHost.h"
#include "Prompts.h"

#include <array>
#include <cstdio>
#include <vector>

int Application::RunSelectionNewCanvasLiteralSelfTest() noexcept
{
    // Independent literal host-state expectations; existing lifecycle and selection numeric goldens stay unchanged.
    const CanvasViewport originalViewport = _viewport;
    const EditorTool originalTool = _tool;
    const std::wstring originalPath = _documentPath;
    const std::optional<Prompts::Answer> originalForcedDiscardAnswer = _forcedDiscardAnswer;
    const std::uint32_t originalForeground = _session.ForegroundColor();
    const std::uint32_t originalBackground = _session.BackgroundColor();

    const auto fullSettleJobs15000 = [this]() noexcept
    {
        return SettleJobs(
            _jobs, 15000,
            [](void* context, void* nativeMessage) noexcept
            {
                // PumpMessages lends a mutable MSG for dispatch; the normal host dispatcher owns its lifetime.
                MSG* message = static_cast<MSG*>(nativeMessage);
                static_cast<Application*>(context)->DispatchQueuedMessage(*message);
            },
            this);
    };
    const auto cleanup = wil::scope_exit(
        [this, originalViewport, originalTool, &originalPath, originalForcedDiscardAnswer, originalForeground,
         originalBackground, &fullSettleJobs15000]() noexcept
        {
            _forcedDiscardAnswer = originalForcedDiscardAnswer;
            CancelHostContinuations();
            CancelSizeCard(); // Also requests cancellation of pending New Canvas prefill/lifecycle work.
            if (fullSettleJobs15000() != SettleResult::Settled)
            {
                SelfTestFailure(L"Selection New Canvas literal cleanup did not acknowledge its worker.");
            }
            if (_session.IsMovingSelection())
            {
                DropDrag<OutlineDrag>();
                _session.CancelSelectionMove();
            }
            _viewport = originalViewport;
            _tool = originalTool;
            _session.SetForegroundColor(originalForeground);
            _session.SetBackgroundColor(originalBackground);
            _documentPath = originalPath;
            SyncChrome();
        });

    struct Mode final
    {
        const char* name;
        Prompts::Answer answer;
    };
    constexpr std::array<Mode, 2> modes{{
        {"discard", Prompts::Answer::Second},
        {"cancel", Prompts::Answer::Cancel},
    }};

    int assertions = 0;
    int failures = 0;
    const auto expect = [&assertions, &failures](const char* mode, const char* check, bool condition) noexcept
    {
        ++assertions;
        std::fprintf(stderr, "%s selection-newcanvas-literal.%s.%s\n", condition ? "PASS" : "FAIL", mode, check);
        failures += condition ? 0 : 1;
    };
    const auto selectionMatches = [this](double left, double top, double right, double bottom) noexcept
    {
        const std::optional<DocumentSelection> selection = _session.Selection();
        if (!selection || selection.value().antialiased || selection.value().loops.size() != 1 ||
            selection.value().loops[0].size() != 4)
        {
            return false;
        }
        const SelectionLoop& loop = selection.value().loops[0];
        return loop[0].x == left && loop[0].y == top && loop[1].x == right && loop[1].y == top && loop[2].x == right &&
               loop[2].y == bottom && loop[3].x == left && loop[3].y == bottom;
    };

    for (const Mode& mode : modes)
    {
        _forcedDiscardAnswer = mode.answer;
        _session.ClearDocument();
        _tool = EditorTool::Marquee;
        _session.CreateDocument(40, 40, false);
        _session.AddBlankLayer();
        _session.SetSelection(DocumentSelection{false, SelectionLoops{SelectionLoop{Vec2{10, 10}, Vec2{30, 10},
                                                                                    Vec2{30, 30}, Vec2{10, 30}}}},
                              L"New Canvas literal source selection");
        _session.history.Reset();
        _session.history.MarkSaved();
        _session.SetOpacity(0.5); // A real edit makes discard confirmation part of the route.
        DocumentChanged();

        const bool sourceFixture = _session.Document().has_value() && _session.Document().value().width == 40 &&
                                   _session.Document().value().height == 40 &&
                                   _session.Document().value().layers.size() == 1 &&
                                   _session.ActiveLayerID().has_value() && _session.history.IsModified() &&
                                   _session.history.UndoCount() == 1 && selectionMatches(10, 10, 30, 30);
        expect(mode.name, "modified-40x40-source-fixture", sourceFixture);
        if (!sourceFixture)
        {
            break;
        }

        const auto& originalLayerRecords = _session.Document().value().layers;
        const std::vector<ImageLayer> originalLayers(originalLayerRecords.begin(), originalLayerRecords.end());
        const Guid originalActiveLayer = _session.ActiveLayerID().value();
        const int originalUndoCount = _session.history.UndoCount();
        const bool originalCanRedo = _session.history.CanRedo();
        const std::wstring originalUndoName = _session.history.UndoName();

        const bool moveOpened = _session.BeginSelectionMove();
        _session.MoveSelection(5, 7);
        _drag = OutlineDrag{Vec2{20, 20}, Vec2{5, 7}};
        const auto originalOpening = _session.SelectionMoveOpening();
        expect(mode.name, "live-outline-is-15-17-through-35-37",
               moveOpened && _session.IsMovingSelection() && originalOpening.has_value() &&
                   selectionMatches(15, 17, 35, 37));
        if (!moveOpened || !_session.IsMovingSelection() || !originalOpening)
        {
            break;
        }

        OpenSizeCard(SizeCardKind::NewCanvas);
        const bool cardOpened = SizeCardOpen();
        expect(mode.name, "new-canvas-card-opened", cardOpened);
        if (!cardOpened)
        {
            break;
        }
        // New Canvas begins an asynchronous clipboard metadata prefill. Editing width cancels it before state is
        // captured or Apply is admitted, so no late prefill can overwrite the two literal dimensions.
        OnSizeAction(SizeAction::SetWidth, 20.0);
        OnSizeAction(SizeAction::SetHeight, 30.0);
        const bool prefillDrained = fullSettleJobs15000() == SettleResult::Settled;
        expect(mode.name, "cancelled-prefill-drained", prefillDrained);
        if (!prefillDrained)
        {
            break;
        }
        expect(mode.name, "edited-card-keeps-live-outline",
               SizeCardOpen() && _session.IsMovingSelection() && selectionMatches(15, 17, 35, 37));

        const Guid originalRevision = _session.history.CurrentRevision();
        const std::uint64_t originalGeneration = _session.Generation();
        const DocumentHistory::StateView originalHistory = _session.history.CaptureState();

        OnSizeAction(SizeAction::Apply, 0.0); // Exercise the real New Canvas Apply route.
        if (mode.answer == Prompts::Answer::Second)
        {
            // Admission alone must leave the source intact until the confirmed replacement's worker publishes.
            expect(mode.name, "discard-apply-admitted-before-worker-publication",
                   _session.Document().has_value() && _session.Document().value().width == 40 &&
                       _session.Document().value().height == 40 && SizeCardOpen() && _session.IsMovingSelection() &&
                       selectionMatches(15, 17, 35, 37));
        }
        const bool settled = fullSettleJobs15000() == SettleResult::Settled;
        expect(mode.name, "real-apply-settled", settled);
        if (!settled)
        {
            break;
        }

        const CanvasDocument* result = _session.Document() ? &_session.Document().value() : nullptr;
        if (mode.answer == Prompts::Answer::Second)
        {
            expect(mode.name, "literal-new-20x30-document",
                   result != nullptr && result->width == 20 && result->height == 30);
            expect(mode.name, "replacement-has-no-selection-or-live-move",
                   !_session.Selection().has_value() && !_session.IsMovingSelection() &&
                       !_session.SelectionMoveOpening().has_value());
            expect(mode.name, "replacement-starts-with-new-canvas-history",
                   result != nullptr && result->layers.size() == 1 && _session.history.UndoCount() == 1 &&
                       _session.history.UndoName() == L"New Canvas" && !_session.history.CanRedo() &&
                       !_session.history.IsModified());
            expect(mode.name, "replacement-closes-card-and-clears-path",
                   !SizeCardOpen() && _documentPath.empty() && std::holds_alternative<std::monostate>(_drag));
        }
        else
        {
            expect(mode.name, "cancel-keeps-original-document-and-layers",
                   result != nullptr && result->width == 40 && result->height == 40 &&
                       std::ranges::equal(result->layers, originalLayers) && _session.ActiveLayerID().has_value() &&
                       _session.ActiveLayerID().value() == originalActiveLayer);
            expect(mode.name, "cancel-restores-literal-live-outline-and-opening",
                   SizeCardOpen() && _session.IsMovingSelection() &&
                       _session.SelectionMoveOpening() == originalOpening && selectionMatches(15, 17, 35, 37) &&
                       std::holds_alternative<OutlineDrag>(_drag) &&
                       std::get<OutlineDrag>(_drag).start == Vec2{20, 20} &&
                       std::get<OutlineDrag>(_drag).offset == Vec2{5, 7});
            expect(
                mode.name, "cancel-preserves-exact-history-revision-and-generation",
                _session.history.IsCurrent(originalHistory) && _session.history.CurrentRevision() == originalRevision &&
                    _session.Generation() == originalGeneration && _session.history.UndoCount() == originalUndoCount &&
                    _session.history.CanRedo() == originalCanRedo && _session.history.UndoName() == originalUndoName &&
                    _session.history.IsModified());
        }
    }

    std::fprintf(stderr, "SUMMARY selection-newcanvas-literal assertions=%d failures=%d modes=%zu\n", assertions,
                 failures, modes.size());
    return failures == 0 ? 0 : 5;
}
