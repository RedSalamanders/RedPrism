#include "Document.Tests.SelectionOffsetCoreFaults.h"
#include "../Support/Support.Tests.DocumentFixture.h"
#include "Selection.h"
#include "SelectionOffsetWork.h"

#include <cstdio>
#include <new>
#include <stop_token>
#include <string>
#include <thread>

namespace
{
int gFailures = 0;
void Expect(bool value, const char* name) noexcept
{
    std::printf("%s selection-offset-core-fault.%s\n", value ? "PASS" : "FAIL", name);
    gFailures += value ? 0 : 1;
}
DocumentSession Session()
{
    DocumentSession session;
    DocumentFixture document;
    document.id = Guid::Generate();
    document.width = 100;
    document.height = 100;
    document.layers.push_back(ImageLayer::Blank(L"Offset core fault fixture", {100, 100}));
    const auto active = document.layers.front().id;
    session.ReplaceDocument(std::move(document).Seal(), active);
    return session;
}
DocumentSelection Rect(int x, int y, int width, int height)
{
    return {false, SelectionGeometry::Rect(x, y, width, height)};
}
struct State final
{
    std::optional<CanvasDocument> document;
    Guid revision;
    std::uint64_t generation;
    int undo;
    DocumentHistory::StateView history;
    std::wstring undoName, redoName;
    explicit State(const DocumentSession& session)
        : document(session.Document()), revision(session.history.CurrentRevision()), generation(session.Generation()),
          undo(session.history.UndoCount()), history(session.history.CaptureState()),
          undoName(session.history.UndoName()), redoName(session.history.RedoName())
    {
    }
    bool Same(const DocumentSession& session) const
    {
        return document == session.Document() && revision == session.history.CurrentRevision() &&
               generation == session.Generation() && undo == session.history.UndoCount() &&
               session.history.IsCurrent(history) &&
               session.history.CaptureState().FutureCount() == history.FutureCount() &&
               undoName == session.history.UndoName() && redoName == session.history.RedoName();
    }
};
} // namespace

int RunSelectionOffsetCoreFaultTests()
{
    gFailures = 0;
    {
        auto session = Session();
        for (int amount : {0, 501, -501})
        {
            char name[80]{};
            std::snprintf(name, sizeof(name), "invalid-amount-%d-refuses-plan-and-capture", amount);
            const State before(session);
            Expect(!session.PlanSelectionOffsetMemory(amount) && !session.PrepareSelectionOffsetInput(amount) &&
                       before.Same(session),
                   name);
        }
    }
    for (bool empty : {false, true})
    {
        auto session = Session();
        if (empty)
            session.SetSelection(DocumentSelection{false, {}}, L"Empty seed");
        session.SetSelection(Rect(40, 40, 20, 20), L"Redo seed");
        session.Undo();
        const State before(session);
        const auto plan = session.PlanSelectionOffsetMemory(5);
        auto input = session.PrepareSelectionOffsetInput(5);
        auto result = input ? DocumentSession::PrepareSelectionOffsetPublication(std::move(*input))
                            : std::optional<PreparedSelectionOffset>{};
        char name[80]{};
        std::snprintf(name, sizeof(name), "%s-plan-has-no-engine-output", empty ? "empty" : "absent");
        Expect(plan && !plan->hasSelection && plan->workspaceBytes == 0 && plan->geometryOutputBytes == 0, name);
        std::snprintf(name, sizeof(name), "%s-no-op-preserves-redo-generation-and-document",
                      empty ? "empty" : "absent");
        Expect(result && !result->ChangesSelection() && session.ApplyPreparedSelectionOffsetIfCurrent(*result) &&
                   before.Same(session) && session.history.CaptureState().FutureCount() == 1,
               name);
        std::snprintf(name, sizeof(name), "%s-publication-is-consumed-once", empty ? "empty" : "absent");
        Expect(result && !session.ApplyPreparedSelectionOffsetIfCurrent(*result) && before.Same(session), name);
    }
    {
        auto session = Session();
        session.SetSelection(Rect(40, 40, 20, 20), L"Seed");
        const State before(session);
        auto input = session.PrepareSelectionOffsetInput(5);
        std::stop_source stopped;
        stopped.request_stop();
        const auto result = input ? DocumentSession::PrepareSelectionOffsetPublication(std::move(*input),
                                                                                       CancelToken(stopped.get_token()))
                                  : std::optional<PreparedSelectionOffset>{};
        Expect(!result && before.Same(session), "cancel-before-worker-preserves-authority");
    }
    {
        auto session = Session();
        session.SetSelection(Rect(40, 40, 20, 20), L"Seed");
        auto input = session.PrepareSelectionOffsetInput(5);
        session.SetSelection(Rect(10, 10, 10, 10), L"Younger selection");
        const State younger(session);
        auto result = input ? DocumentSession::PrepareSelectionOffsetPublication(std::move(*input))
                            : std::optional<PreparedSelectionOffset>{};
        Expect(result && !session.IsPreparedSelectionOffsetCurrent(*result) &&
                   !session.ApplyPreparedSelectionOffsetIfCurrent(*result) && younger.Same(session),
               "stale-selection-refuses-publication-without-changing-younger-state");
    }
    {
        auto session = Session();
        session.SetSelection(Rect(40, 40, 20, 20), L"Seed");
        const State before(session);
        auto input = session.PrepareSelectionOffsetInput(-5);
        std::optional<PreparedSelectionOffset> prepared;
        std::thread worker(
            [&]()
            {
                if (input)
                    prepared = DocumentSession::PrepareSelectionOffsetPublication(std::move(*input));
            });
        worker.join();
        Expect(prepared && before.Same(session), "worker-preparation-does-not-mutate-owner-document");
        auto second = input ? DocumentSession::PrepareSelectionOffsetPublication(std::move(*input))
                            : std::optional<PreparedSelectionOffset>{};
        Expect(!second && before.Same(session), "moved-input-refuses-a-second-preparation");
        Expect(prepared && session.ApplyPreparedSelectionOffsetIfCurrent(*prepared) && session.Selection() &&
                   session.Selection()->Bounds() == SelectionBounds{45, 45, 55, 55} &&
                   session.history.UndoName() == L"Contract Selection",
               "worker-result-publishes-literal-contracted-selection");
    }
    {
        auto session = Session();
        session.SetSelection(Rect(40, 40, 20, 20), L"Seed");
        const State before(session);
        auto input = session.PrepareSelectionOffsetInput(5);
        SelectionOffsetObserver observer;
        observer.limits.workspaceBytes = 64;
        bool refused = false;
        try
        {
            refused =
                input && !DocumentSession::PrepareSelectionOffsetPublication(std::move(*input), {}, {}, {}, &observer);
        }
        catch (const std::bad_alloc&)
        {
            refused = true;
        }
        Expect(refused && before.Same(session), "actual-arena-refusal-preserves-complete-owner-state");
        auto fresh = session.PrepareSelectionOffsetInput(5);
        auto result = fresh ? DocumentSession::PrepareSelectionOffsetPublication(std::move(*fresh))
                            : std::optional<PreparedSelectionOffset>{};
        Expect(result && session.ApplyPreparedSelectionOffsetIfCurrent(*result) && session.Selection() &&
                   session.Selection()->Bounds() == SelectionBounds{35, 35, 65, 65},
               "fresh-command-after-refusal-succeeds");
    }
    return gFailures;
}
