#include "Application.h"

#include "ChromeHost.h"
#include "Prompts.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <variant>
#include <vector>

int Application::RunSelectionCloseProjectLiteralSelfTest() noexcept
{
    const CanvasViewport originalViewport = _viewport;
    const EditorTool originalTool = _tool;
    const std::wstring originalPath = _documentPath;
    const std::optional<Prompts::Answer> originalForcedDiscardAnswer = _forcedDiscardAnswer;
    const std::uint32_t originalForeground = _session.ForegroundColor();
    const std::uint32_t originalBackground = _session.BackgroundColor();

    const auto settle = [this]() noexcept
    {
        return SettleJobs(
                   _jobs, 15000,
                   [](void* context, void* nativeMessage) noexcept
                   {
                       MSG* message = static_cast<MSG*>(nativeMessage);
                       static_cast<Application*>(context)->DispatchQueuedMessage(*message);
                   },
                   this) == SettleResult::Settled;
    };
    const auto cleanup = wil::scope_exit(
        [this, originalViewport, originalTool, &originalPath, originalForcedDiscardAnswer, originalForeground,
         originalBackground, &settle]() noexcept
        {
            _forcedDiscardAnswer = originalForcedDiscardAnswer;
            CancelHostContinuations();
            if (!settle())
            {
                SelfTestFailure(L"Selection Close Project literal cleanup did not acknowledge its worker.");
                std::_Exit(5);
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
        std::fprintf(stderr, "%s selection-closeproject-literal.%s.%s\n", condition ? "PASS" : "FAIL", mode, check);
        failures += condition ? 0 : 1;
    };
    const auto selectionMatches = [this](double left, double top, double right, double bottom) noexcept
    {
        const std::optional<DocumentSelection> selection = _session.Selection();
        if (!selection || selection.value().antialiased || selection.value().loops.size() != 1 ||
            selection.value().loops[0].size() != 4)
            return false;
        const SelectionLoop& loop = selection.value().loops[0];
        return loop[0].x == left && loop[0].y == top && loop[1].x == right && loop[1].y == top && loop[2].x == right &&
               loop[2].y == bottom && loop[3].x == left && loop[3].y == bottom;
    };

    for (const Mode& mode : modes)
    {
        CancelSizeCard();
        if (!settle())
        {
            expect(mode.name, "preceding-card-jobs-drained", false);
            break;
        }
        _forcedDiscardAnswer = mode.answer;
        _session.ClearDocument();
        _documentPath = L"Z:\\selection-closeproject-literal\\source.prism";
        _tool = EditorTool::Marquee;
        _session.CreateDocument(40, 40, false);
        _session.AddBlankLayer();
        _session.SetSelection(DocumentSelection{false, SelectionLoops{SelectionLoop{Vec2{10, 10}, Vec2{30, 10},
                                                                                    Vec2{30, 30}, Vec2{10, 30}}}},
                              L"Close Project literal source selection");
        _session.history.Reset();
        _session.history.MarkSaved();
        _session.SetOpacity(0.5);
        DocumentChanged();

        const bool sourceFixture = _session.Document().has_value() && _session.Document().value().width == 40 &&
                                   _session.Document().value().height == 40 &&
                                   _session.Document().value().layers.size() == 1 &&
                                   _session.ActiveLayerID().has_value() && _session.history.IsModified() &&
                                   _session.history.UndoCount() == 1 && selectionMatches(10, 10, 30, 30);
        expect(mode.name, "modified-40x40-source-fixture", sourceFixture);
        if (!sourceFixture)
            break;

        const bool moveOpened = _session.BeginSelectionMove();
        _session.MoveSelection(5, 7);
        _drag = OutlineDrag{Vec2{20, 20}, Vec2{5, 7}};
        const auto sourceOpening = _session.SelectionMoveOpening();
        const bool liveOutline = moveOpened && _session.IsMovingSelection() && sourceOpening.has_value() &&
                                 selectionMatches(15, 17, 35, 37) && std::holds_alternative<OutlineDrag>(_drag) &&
                                 std::get<OutlineDrag>(_drag).start == Vec2{20, 20} &&
                                 std::get<OutlineDrag>(_drag).offset == Vec2{5, 7};
        expect(mode.name, "live-outline-moved-5-7-with-local-drag", liveOutline);
        if (!liveOutline)
            break;

        const std::vector<ImageLayer> sourceLayers = _session.Document().value().layers;
        const Guid sourceActiveLayer = _session.ActiveLayerID().value();
        const Guid sourceRevision = _session.history.CurrentRevision();
        const std::uint64_t sourceGeneration = _session.Generation();
        const DocumentHistory::StateView sourceHistory = _session.history.CaptureState();
        const int sourceUndoCount = _session.history.UndoCount();
        const bool sourceCanRedo = _session.history.CanRedo();
        const std::wstring sourceUndoName = _session.history.UndoName();
        const std::wstring sourcePath = _documentPath;

        CloseProject();
        const bool admittedPending = (_hostRequests.Size() != 0 || _activeHostRequest.has_value()) &&
                                     _session.Document().has_value() && _session.Document().value().width == 40 &&
                                     _session.Document().value().height == 40 && _session.IsMovingSelection() &&
                                     selectionMatches(15, 17, 35, 37) && _session.history.IsCurrent(sourceHistory) &&
                                     _documentPath == sourcePath;
        expect(mode.name, "close-admitted-before-publication", admittedPending);

        const bool settled = settle();
        expect(mode.name, "real-close-project-message-pump-settled", settled);
        if (!settled)
            break;

        if (mode.answer == Prompts::Answer::Second)
        {
            expect(mode.name, "discard-closes-document-and-selection-move",
                   !_session.Document().has_value() && !_session.Selection().has_value() &&
                       !_session.IsMovingSelection() && !_session.SelectionMoveOpening().has_value() &&
                       std::holds_alternative<std::monostate>(_drag));
            expect(mode.name, "discard-resets-history-to-empty-saved-lifecycle-state",
                   !_session.history.CanUndo() && !_session.history.CanRedo() && !_session.history.IsModified() &&
                       _session.history.UndoCount() == 0 && _session.history.UndoName().empty() &&
                       _session.history.RedoName().empty());
            expect(mode.name, "discard-clears-active-layer-and-path",
                   !_session.ActiveLayerID().has_value() && _documentPath.empty());
        }
        else
        {
            const CanvasDocument* result = _session.Document() ? &_session.Document().value() : nullptr;
            expect(mode.name, "cancel-preserves-document-and-exact-layers",
                   result != nullptr && result->width == 40 && result->height == 40 && result->layers == sourceLayers &&
                       _session.ActiveLayerID().has_value() && _session.ActiveLayerID().value() == sourceActiveLayer);
            expect(mode.name, "cancel-preserves-live-moved-outline-and-opening",
                   _session.IsMovingSelection() && _session.SelectionMoveOpening() == sourceOpening &&
                       selectionMatches(15, 17, 35, 37) && std::holds_alternative<OutlineDrag>(_drag) &&
                       std::get<OutlineDrag>(_drag).start == Vec2{20, 20} &&
                       std::get<OutlineDrag>(_drag).offset == Vec2{5, 7});
            expect(mode.name, "cancel-preserves-exact-history-revision-generation-and-path",
                   _session.history.IsCurrent(sourceHistory) && _session.history.CurrentRevision() == sourceRevision &&
                       _session.Generation() == sourceGeneration && _session.history.UndoCount() == sourceUndoCount &&
                       _session.history.CanRedo() == sourceCanRedo && _session.history.UndoName() == sourceUndoName &&
                       _session.history.IsModified() && _documentPath == sourcePath);
        }
    }

    std::fprintf(stderr, "SUMMARY selection-closeproject-literal assertions=%d failures=%d modes=%zu\n", assertions,
                 failures, modes.size());
    return failures == 0 ? 0 : 5;
}
