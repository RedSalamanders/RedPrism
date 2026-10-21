#include "Application.h"

#include "ChromeHost.h"

#include <array>
#include <cstdio>

int Application::RunSelectionNudgeSelfTest() noexcept
{
    const bool originalShift = _acceleratorShift;
    const EditorTool originalTool = _tool;
    const CanvasViewport originalViewport = _viewport;
    const auto restore = wil::scope_exit(
        [this, originalShift, originalTool, originalViewport]() noexcept
        {
            _acceleratorShift = originalShift;
            _tool = originalTool;
            _viewport = originalViewport;
            _selectionNudgeTestGate.reset();
        });

    struct NudgeCase final
    {
        const char* name;
        int command;
        bool shift;
        double left;
        double top;
        double right;
        double bottom;
    };
    constexpr std::array<NudgeCase, 8> cases{{
        {"left-1", ChromeHost::kCommandNudgeSelectionLeft, false, 9, 10, 29, 30},
        {"left-10", ChromeHost::kCommandNudgeSelectionLeft, true, 0, 10, 20, 30},
        {"right-1", ChromeHost::kCommandNudgeSelectionRight, false, 11, 10, 31, 30},
        {"right-10", ChromeHost::kCommandNudgeSelectionRight, true, 20, 10, 40, 30},
        {"up-1", ChromeHost::kCommandNudgeSelectionUp, false, 10, 9, 30, 29},
        {"up-10", ChromeHost::kCommandNudgeSelectionUp, true, 10, 0, 30, 20},
        {"down-1", ChromeHost::kCommandNudgeSelectionDown, false, 10, 11, 30, 31},
        {"down-10", ChromeHost::kCommandNudgeSelectionDown, true, 10, 20, 30, 40},
    }};

    int assertions = 0;
    int failures = 0;
    int modes = 0;
    bool stop = false;
    const auto expect = [&assertions, &failures](const char* mode, const char* check, bool condition) noexcept
    {
        ++assertions;
        if (condition)
        {
            std::fprintf(stderr, "PASS selection-nudge-host.%s.%s\n", mode, check);
            return;
        }
        ++failures;
        std::fprintf(stderr, "FAIL selection-nudge-host.%s.%s\n", mode, check);
    };
    const auto selectionMatches = [this](double left, double top, double right, double bottom) noexcept
    {
        if (!_session.Selection().has_value())
            return false;
        const DocumentSelection& selection = _session.Selection().value();
        if (selection.antialiased || selection.loops.size() != 1 || selection.loops[0].size() != 4)
            return false;
        const SelectionLoop& loop = selection.loops[0];
        return loop[0].x == left && loop[0].y == top && loop[1].x == right && loop[1].y == top && loop[2].x == right &&
               loop[2].y == bottom && loop[3].x == left && loop[3].y == bottom;
    };
    const auto prepareDocument = [this, &selectionMatches](bool withSelection) noexcept
    {
        _session.ClearDocument();
        _session.CreateDocument(40, 40, false);
        _session.AddBlankLayer();
        _session.AddBlankLayer();
        if (withSelection)
        {
            _session.SetSelection(DocumentSelection{false, SelectionLoops{SelectionLoop{Vec2{10, 10}, Vec2{30, 10},
                                                                                        Vec2{30, 30}, Vec2{10, 30}}}},
                                  L"Fixture Selection");
        }
        _session.history.Reset();
        _session.history.MarkSaved();
        _tool = EditorTool::Move;
        DocumentChanged();
        return _session.Document().has_value() && _session.Document().value().width == 40 &&
               _session.Document().value().height == 40 && _session.Document().value().layers.size() == 2 &&
               _session.ActiveLayerID().has_value() && (!withSelection || selectionMatches(10, 10, 30, 30));
    };

    _selectionNudgeTestGate.reset();
    for (const NudgeCase& testCase : cases)
    {
        ++modes;
        if (!prepareDocument(true))
        {
            expect(testCase.name, "fixture", false);
            stop = true;
            break;
        }
        const Guid activeLayer = _session.ActiveLayerID().value();
        const auto document = _session.Document();
        _acceleratorShift = testCase.shift;
        OnCommand(testCase.command);
        _acceleratorShift = !testCase.shift;
        const bool settled = SettleJobs(_jobs, 15000, DispatchMessageThunk, this) == SettleResult::Settled;
        expect(testCase.name, "settled", settled);
        if (!settled)
        {
            stop = true;
            break;
        }

        expect(testCase.name, "literal-output",
               selectionMatches(testCase.left, testCase.top, testCase.right, testCase.bottom));
        expect(testCase.name, "single-move-entry",
               _session.history.UndoCount() == 1 && _session.history.UndoName() == L"Move Selection" &&
                   !_session.history.CanRedo());
        expect(testCase.name, "retained-targets",
               _session.Document().has_value() && _session.Document().value().layers.size() == 2 &&
                   _session.Document().value().layers == document.value().layers &&
                   _session.ActiveLayerID().has_value() && _session.ActiveLayerID().value() == activeLayer &&
                   _session.Selection().has_value() && !_session.Selection().value().antialiased);

        _session.Undo();
        expect(testCase.name, "undo-literal-origin", selectionMatches(10, 10, 30, 30));
        expect(testCase.name, "undo-history",
               _session.history.UndoCount() == 0 && _session.history.CanRedo() &&
                   _session.history.RedoName() == L"Move Selection");
        _session.Redo();
        expect(testCase.name, "redo-literal-output",
               selectionMatches(testCase.left, testCase.top, testCase.right, testCase.bottom));
        expect(testCase.name, "redo-history",
               _session.history.UndoCount() == 1 && _session.history.UndoName() == L"Move Selection" &&
                   !_session.history.CanRedo());
        expect(testCase.name, "redo-retained-targets",
               _session.Document().has_value() && _session.Document().value().layers.size() == 2 &&
                   _session.Document().value().layers == document.value().layers &&
                   _session.ActiveLayerID().has_value() && _session.ActiveLayerID().value() == activeLayer &&
                   _session.Selection().has_value() && !_session.Selection().value().antialiased);
    }

    if (!stop)
    {
        ++modes;
        const bool fixture = prepareDocument(true);
        expect("nested", "fixture", fixture);
        if (fixture)
        {
            _session.history.Begin(L"Outer nudge fixture", _session.Document(), _session.ActiveLayerID());
            _acceleratorShift = false;
            OnCommand(ChromeHost::kCommandNudgeSelectionRight);
            _acceleratorShift = true;
            const auto settled = SettleJobs(_jobs, 15000, DispatchMessageThunk, this);
            expect("nested", "worker-keeps-outer-depth",
                   settled == SettleResult::Settled && _session.history.CaptureState().Depth() == 1 &&
                       selectionMatches(11, 10, 31, 30) && _session.history.UndoCount() == 0);
            _session.history.End(_session.Document(), _session.ActiveLayerID());
            expect("nested", "literal-outer-name",
                   _session.history.UndoName() == L"Outer nudge fixture" && _session.history.UndoCount() == 1);
            _session.Undo();
            expect("nested", "literal-origin-undo", selectionMatches(10, 10, 30, 30));
        }
    }

    if (!stop)
    {
        ++modes;
        const bool fixture = prepareDocument(false);
        expect("no-selection", "fixture", fixture);
        if (!fixture)
        {
            stop = true;
        }
        else
        {
            _acceleratorShift = true;
            OnCommand(ChromeHost::kCommandNudgeSelectionRight);
            _acceleratorShift = originalShift;
            const bool settled = SettleJobs(_jobs, 15000, DispatchMessageThunk, this) == SettleResult::Settled;
            expect("no-selection", "settled", settled);
            expect("no-selection", "old-policy-no-op",
                   settled && !_session.Selection().has_value() && _session.history.UndoCount() == 0 &&
                       !_selectionNudgeTicket.has_value() && !_selectionNudgeStamp.has_value());
        }
    }

    if (!stop)
    {
        ++modes;
        const bool fixture = prepareDocument(true);
        expect("live-outline", "fixture", fixture);
        if (fixture)
        {
            const bool opened = _session.BeginSelectionMove();
            _session.MoveSelection(1, 0);
            const bool draftReady = opened && _session.IsMovingSelection() && selectionMatches(11, 10, 31, 30);
            expect("live-outline", "draft", draftReady);
            _acceleratorShift = true;
            OnCommand(ChromeHost::kCommandNudgeSelectionDown);
            _acceleratorShift = originalShift;
            const bool settled = SettleJobs(_jobs, 15000, DispatchMessageThunk, this) == SettleResult::Settled;
            expect("live-outline", "settled", settled);
            expect("live-outline", "old-policy-no-op",
                   settled && _session.IsMovingSelection() && selectionMatches(11, 10, 31, 30) &&
                       _session.history.UndoCount() == 0 && !_selectionNudgeTicket.has_value() &&
                       !_selectionNudgeStamp.has_value());
            _session.CancelSelectionMove();
            expect("live-outline", "cancel-restores-origin",
                   !_session.IsMovingSelection() && selectionMatches(10, 10, 30, 30) &&
                       _session.history.UndoCount() == 0);
        }
    }

    std::fprintf(stderr, "SUMMARY selection-nudge-host assertions=%d failures=%d modes=%d\n", assertions, failures,
                 modes);
    return failures == 0 && modes == 11 ? 0 : 5;
}
