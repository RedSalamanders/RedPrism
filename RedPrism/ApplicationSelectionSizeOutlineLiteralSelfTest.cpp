#include "Application.h"

#include "ChromeHost.h"

#include <array>
#include <cstdio>

int Application::RunSelectionSizeOutlineLiteralSelfTest() noexcept
{
    // Literal size/history expectations follow legacy CanvasSizeTests, ImageSizeTests and SelectionTests.
    // The cancellation predecessor restores the opening square (10..30), before resizing drops selection.
    const auto originalViewport = _viewport;
    const auto restoreViewport = wil::scope_exit(
        [this, originalViewport]() noexcept
        {
            if (!_jobs.IsIdle())
            {
                CancelHostContinuations();
                if (SettleJobs(_jobs, 15000, DispatchMessageThunk, this) != SettleResult::Settled)
                    SelfTestFailure(L"Size outline literal cleanup did not acknowledge its worker.");
            }
            _viewport = originalViewport;
            CancelSizeCard();
        });
    struct Mode final
    {
        const char* name;
        SizeCardKind kind;
        bool nested;
        int width;
        int height;
        double resolution;
        const wchar_t* historyName;
        const wchar_t* outerName;
    };
    constexpr std::array<Mode, 4> modes{{
        {"canvas-ordinary", SizeCardKind::CanvasSize, false, 50, 40, 72.0, L"Canvas Size", L"Outer Canvas Size"},
        {"canvas-nested", SizeCardKind::CanvasSize, true, 50, 40, 72.0, L"Canvas Size", L"Outer Canvas Size"},
        {"image-ordinary", SizeCardKind::ImageSize, false, 80, 80, 144.0, L"Image Size", L"Outer Image Size"},
        {"image-nested", SizeCardKind::ImageSize, true, 80, 80, 144.0, L"Image Size", L"Outer Image Size"},
    }};

    int assertions = 0;
    int failures = 0;
    const auto expect = [&assertions, &failures](const char* mode, const char* check, bool condition) noexcept
    {
        ++assertions;
        if (condition)
        {
            std::fprintf(stderr, "PASS selection-size-outline-literal.%s.%s\n", mode, check);
            return;
        }
        ++failures;
        std::fprintf(stderr, "FAIL selection-size-outline-literal.%s.%s\n", mode, check);
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

    for (const Mode& mode : modes)
    {
        _session.ClearDocument();
        _session.CreateDocument(40, 40, false);
        _session.AddBlankLayer();
        _session.AddBlankLayer();
        _session.SetSelection(DocumentSelection{false, SelectionLoops{SelectionLoop{Vec2{10, 10}, Vec2{30, 10},
                                                                                    Vec2{30, 30}, Vec2{10, 30}}}},
                              L"Fixture Selection");
        _session.history.Reset();
        _session.history.MarkSaved();
        DocumentChanged();

        const bool fixture = _session.Document().has_value() && _session.Document().value().width == 40 &&
                             _session.Document().value().height == 40 && _session.ActiveLayerID().has_value() &&
                             selectionMatches(10, 10, 30, 30);
        expect(mode.name, "fixture", fixture);
        if (!fixture)
            continue;

        OpenSizeCard(mode.kind);
        const bool opened = SizeCardOpen();
        expect(mode.name, "card-open", opened);
        if (!opened)
            continue;

        if (mode.nested)
            _session.history.Begin(mode.outerName, _session.Document(), _session.ActiveLayerID());

        if (mode.kind == SizeCardKind::CanvasSize)
        {
            OnSizeAction(SizeAction::SetLocked, 0.0);
            OnSizeAction(SizeAction::SetWidth, 50.0);
            OnSizeAction(SizeAction::SetHeight, 40.0);
            OnSizeAction(SizeAction::SetAnchor, 0.0);
            OnSizeAction(SizeAction::SetExtension, 0.0);
        }
        else
        {
            OnSizeAction(SizeAction::SetWidth, 80.0);
            OnSizeAction(SizeAction::SetHeight, 80.0);
            OnSizeAction(SizeAction::SetResolution, 144.0);
            OnSizeAction(SizeAction::SetSampling, 0.0); // LayerSampling::Nearest.
            OnSizeAction(SizeAction::SetResample, 1.0);
        }

        const bool moveOpened = _session.BeginSelectionMove();
        _session.MoveSelection(5.0, 7.0);
        expect(mode.name, "live-outline-before-apply",
               moveOpened && _session.IsMovingSelection() && selectionMatches(15, 17, 35, 37));

        ApplySizeCard();
        const bool settled = SettleJobs(_jobs, 15000, DispatchMessageThunk, this) == SettleResult::Settled;
        expect(mode.name, "settled", settled);
        if (!settled)
            return 5;

        const CanvasDocument* resized = _session.Document() ? &_session.Document().value() : nullptr;
        expect(mode.name, "literal-resized-document",
               resized != nullptr && resized->width == mode.width && resized->height == mode.height &&
                   resized->resolution == mode.resolution);
        expect(mode.name, "selection-and-outline-dropped",
               !_session.Selection().has_value() && !_session.IsMovingSelection());

        if (mode.nested)
        {
            expect(mode.name, "outer-depth-retained",
                   _session.history.CaptureState().Depth() == 1 && _session.history.UndoCount() == 0 &&
                       resized != nullptr && resized->width == mode.width && resized->height == mode.height);
            _session.history.End(_session.Document(), _session.ActiveLayerID());
            expect(mode.name, "outer-name-after-end",
                   _session.history.CaptureState().Depth() == 0 && _session.history.UndoCount() == 1 &&
                       _session.history.UndoName() == mode.outerName && !_session.history.CanRedo());
        }
        else
        {
            expect(mode.name, "single-size-entry",
                   _session.history.CaptureState().Depth() == 0 && _session.history.UndoCount() == 1 &&
                       _session.history.UndoName() == mode.historyName && !_session.history.CanRedo());
        }

        _session.Undo();
        const CanvasDocument* undone = _session.Document() ? &_session.Document().value() : nullptr;
        expect(mode.name, "undo-literal-original-document",
               undone != nullptr && undone->width == 40 && undone->height == 40 && undone->resolution == 72.0 &&
                   selectionMatches(10, 10, 30, 30) && !_session.IsMovingSelection());
        expect(mode.name, "undo-history-state",
               _session.history.UndoCount() == 0 && _session.history.CanRedo() &&
                   _session.history.RedoName() == (mode.nested ? mode.outerName : mode.historyName));

        _session.Redo();
        const CanvasDocument* redone = _session.Document() ? &_session.Document().value() : nullptr;
        expect(mode.name, "redo-literal-resized-document",
               redone != nullptr && redone->width == mode.width && redone->height == mode.height &&
                   redone->resolution == mode.resolution && !_session.Selection().has_value() &&
                   !_session.IsMovingSelection());
        expect(mode.name, "redo-history-state",
               _session.history.UndoCount() == 1 &&
                   _session.history.UndoName() == (mode.nested ? mode.outerName : mode.historyName) &&
                   !_session.history.CanRedo());
    }

    std::fprintf(stderr, "SUMMARY selection-size-outline-literal assertions=%d failures=%d modes=%zu\n", assertions,
                 failures, modes.size());
    return failures == 0 ? 0 : 5;
}
