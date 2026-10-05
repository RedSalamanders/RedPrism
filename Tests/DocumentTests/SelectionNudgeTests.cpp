#include "SelectionNudgeTests.h"

#include "../Support/DocumentFixture.h"

#include "Document.h"
#include "Executor.h"
#include "SelectionNudgeWork.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{
int gFailures = 0;
int gAssertions = 0;

void Expect(bool condition, const char* name) noexcept
{
    ++gAssertions;
    std::printf("%s selection-nudge.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

DocumentSession Session()
{
    DocumentSession session;
    session.CreateDocument(40, 40, true);
    session.AddBlankLayer();
    session.SetSelection(
        DocumentSelection{true, {SelectionLoop{Vec2{10, 10}, Vec2{30, 10}, Vec2{30, 30}, Vec2{10, 30}}}},
        L"Seed Selection");
    return session;
}

bool LiteralSelection(const DocumentSession& session, bool antialiased,
                      const std::vector<SelectionLoop>& expected) noexcept
{
    if (!session.Document() || !session.Document().value().selection ||
        session.Document().value().selection.value().antialiased != antialiased ||
        session.Document().value().selection.value().loops.size() != expected.size())
        return false;
    for (std::size_t i = 0; i < expected.size(); ++i)
        if (session.Document().value().selection.value().loops[i] != expected[i])
            return false;
    return true;
}

bool Nudge(DocumentSession& session, int dx, int dy)
{
    auto input = session.PrepareSelectionNudgeInput(dx, dy);
    auto prepared = input ? DocumentSession::PrepareSelectionNudgePublication(std::move(input.value())) : std::nullopt;
    return prepared && session.ApplyPreparedSelectionNudgeIfCurrent(prepared.value());
}

void LiteralArrowGolden()
{
    auto session = Session();
    const int initialUndoCount = session.history.UndoCount();
    Expect(Nudge(session, 1, 0) && Nudge(session, 0, -10), "literal-two-arrow-nudges-publish");
    Expect(LiteralSelection(session, true, {SelectionLoop{Vec2{11, 0}, Vec2{31, 0}, Vec2{31, 20}, Vec2{11, 20}}}),
           "literal-swift-arrow-golden-aa-and-vertices");
    Expect(session.history.UndoCount() == initialUndoCount + 2 && session.history.UndoName() == L"Move Selection",
           "two-nudges-create-two-move-selection-entries");
    session.Undo();
    Expect(LiteralSelection(session, true, {SelectionLoop{Vec2{11, 10}, Vec2{31, 10}, Vec2{31, 30}, Vec2{11, 30}}}) &&
               session.history.RedoName() == L"Move Selection",
           "first-undo-restores-first-nudge-state");
    session.Undo();
    Expect(LiteralSelection(session, true, {SelectionLoop{Vec2{10, 10}, Vec2{30, 10}, Vec2{30, 30}, Vec2{10, 30}}}),
           "second-undo-restores-source-outline");
    session.Redo();
    Expect(LiteralSelection(session, true, {SelectionLoop{Vec2{11, 10}, Vec2{31, 10}, Vec2{31, 30}, Vec2{11, 30}}}),
           "first-redo-restores-first-nudge-state");
    session.Redo();
    Expect(LiteralSelection(session, true, {SelectionLoop{Vec2{11, 0}, Vec2{31, 0}, Vec2{31, 20}, Vec2{11, 20}}}),
           "second-redo-restores-literal-final-outline");
}

void ShapeAndAdmission()
{
    static_assert(!std::is_copy_constructible_v<SelectionNudgeInput>);
    static_assert(std::is_nothrow_move_constructible_v<SelectionNudgeInput>);
    static_assert(!std::is_copy_constructible_v<PreparedSelectionNudge>);
    static_assert(std::is_nothrow_move_constructible_v<PreparedSelectionNudge>);
    {
        auto session = Session();
        session.SetSelection(DocumentSelection{false,
                                               {SelectionLoop{Vec2{10, 10}, Vec2{30, 10}, Vec2{30, 30}, Vec2{10, 30}},
                                                SelectionLoop{Vec2{15, 15}, Vec2{15, 25}, Vec2{25, 25}, Vec2{25, 15}}}},
                             L"Non-AA Selection With Hole");
        Expect(Nudge(session, 2, -3), "hole-outline-nudge-publishes");
        Expect(LiteralSelection(session, false,
                                {SelectionLoop{Vec2{12, 7}, Vec2{32, 7}, Vec2{32, 27}, Vec2{12, 27}},
                                 SelectionLoop{Vec2{17, 12}, Vec2{17, 22}, Vec2{27, 22}, Vec2{27, 12}}}),
               "hole-vertices-and-aa-flag-are-preserved-literally");
    }
    {
        auto session = Session();
        Expect(Nudge(session, -25, 0) &&
                   LiteralSelection(session, true,
                                    {SelectionLoop{Vec2{-15, 10}, Vec2{5, 10}, Vec2{5, 30}, Vec2{-15, 30}}}) &&
                   Nudge(session, 25, 0) &&
                   LiteralSelection(session, true,
                                    {SelectionLoop{Vec2{10, 10}, Vec2{30, 10}, Vec2{30, 30}, Vec2{10, 30}}}),
               "off-canvas-then-back-retains-full-shape");
    }
    {
        DocumentSession none;
        Expect(!none.PrepareSelectionNudgeInput(1, 0), "absent-document-refused");
        none.CreateDocument(40, 40, true);
        Expect(none.Document().has_value() && !none.Selection() && !none.PrepareSelectionNudgeInput(1, 0),
               "absent-selection-on-real-document-refused");
        auto empty = Session();
        empty.SetSelection(DocumentSelection{true, {}}, L"Explicit Empty");
        Expect(!empty.PrepareSelectionNudgeInput(1, 0), "explicit-empty-selection-refused");
    }
    {
        auto session = Session();
        session.BeginLasso(Vec2{2, 2}, SelectionMode::New);
        Expect(session.PendingLasso().has_value() && !session.PrepareSelectionNudgeInput(1, 0),
               "pending-outline-refuses-live-nudge");
    }
    {
        auto session = Session();
        static_cast<void>(session.BeginSelectionMove());
        Expect(session.IsMovingSelection() && !session.PrepareSelectionNudgeInput(1, 0),
               "active-outline-move-refuses-nudge-sealing");
        session.CancelSelectionMove();
    }
    {
        auto session = Session();
        auto captured = session.PrepareSelectionNudgeInput(1, 0);
        Expect(captured.has_value(), "move-only-input-captured");
        if (captured)
        {
            SelectionNudgeInput moved(std::move(captured.value()));
            Expect(!DocumentSession::PrepareSelectionNudgePublication(std::move(captured.value())) &&
                       LiteralSelection(session, true,
                                        {SelectionLoop{Vec2{10, 10}, Vec2{30, 10}, Vec2{30, 30}, Vec2{10, 30}}}),
                   "moved-from-input-refused-without-owner-mutation");
            auto prepared = DocumentSession::PrepareSelectionNudgePublication(std::move(moved));
            if (prepared)
            {
                PreparedSelectionNudge movedPrepared(std::move(prepared.value()));
                Expect(!session.IsPreparedSelectionNudgeCurrent(prepared.value()) &&
                           session.IsPreparedSelectionNudgeCurrent(movedPrepared) &&
                           session.ApplyPreparedSelectionNudgeIfCurrent(movedPrepared),
                       "moved-prepared-publication-retains-owner-authority");
            }
            else
                Expect(false, "moved-input-publication-prepares");
        }
    }
    Expect(Session().SelectionNudgeMemoryBytes() > 0, "memory-estimate-admits-sealed-selection");
}

template <typename Mutate> void StaleAfterPreparation(const char* name, Mutate&& mutate)
{
    auto session = Session();
    auto input = session.PrepareSelectionNudgeInput(1, 0);
    auto prepared = input ? DocumentSession::PrepareSelectionNudgePublication(std::move(input.value())) : std::nullopt;
    char label[96]{};
    std::snprintf(label, sizeof(label), "%s-initially-current", name);
    Expect(prepared && session.IsPreparedSelectionNudgeCurrent(prepared.value()), label);
    if (!prepared)
        return;
    mutate(session);
    std::snprintf(label, sizeof(label), "%s-stale-publication-refused", name);
    Expect(!session.IsPreparedSelectionNudgeCurrent(prepared.value()) &&
               !session.ApplyPreparedSelectionNudgeIfCurrent(prepared.value()),
           label);
}

void StalenessAndTransactions()
{
    StaleAfterPreparation("active-layer-change-stales",
                          [](DocumentSession& session)
                          {
                              const auto active = session.ActiveLayerID();
                              const auto generation = session.Generation();
                              const auto history = session.history.CaptureState();
                              for (const auto& layer : session.Document().value().layers)
                                  if (active != std::optional(layer.id))
                                  {
                                      session.SelectLayer(layer.id);
                                      break;
                                  }
                              Expect(session.ActiveLayerID() != active && session.Generation() == generation &&
                                         session.history.IsCurrent(history),
                                     "active-only-stale-guard-reached");
                          });
    StaleAfterPreparation(
        "selection-change-stales",
        [](DocumentSession& session)
        {
            session.SetSelection(
                DocumentSelection{true, {SelectionLoop{Vec2{1, 1}, Vec2{4, 1}, Vec2{4, 4}, Vec2{1, 4}}}},
                L"Replacement Selection");
        });
    StaleAfterPreparation("history-epoch-change-stales",
                          [](DocumentSession& session) { session.BeginEdit(L"Open competing history edit"); });
    StaleAfterPreparation("document-replacement-stales",
                          [](DocumentSession& session) { session.CreateDocument(40, 40, true); });
    {
        auto owner = Session();
        auto input = owner.PrepareSelectionNudgeInput(1, 0);
        auto prepared =
            input ? DocumentSession::PrepareSelectionNudgePublication(std::move(input.value())) : std::nullopt;
        auto foreign = Session();
        Expect(prepared && !foreign.IsPreparedSelectionNudgeCurrent(prepared.value()) &&
                   !foreign.ApplyPreparedSelectionNudgeIfCurrent(prepared.value()) &&
                   owner.IsPreparedSelectionNudgeCurrent(prepared.value()),
               "foreign-session-authority-refused");
    }
    {
        auto session = Session();
        session.BeginEdit(L"Outer Nudge Transaction");
        const auto revision = session.history.CurrentRevision();
        Expect(Nudge(session, 1, 0) && session.history.CaptureState().Depth() == 1 &&
                   session.history.CurrentRevision() == revision,
               "nested-nudge-keeps-outer-transaction-open");
        session.EndEdit();
        Expect(session.history.CaptureState().Depth() == 0 && session.history.UndoName() == L"Outer Nudge Transaction",
               "nested-nudge-commits-under-outer-name");
        session.Undo();
        Expect(LiteralSelection(session, true, {SelectionLoop{Vec2{10, 10}, Vec2{30, 10}, Vec2{30, 30}, Vec2{10, 30}}}),
               "outer-undo-restores-source-selection");
    }
}

void ZeroOffsetAcknowledgement()
{
    auto session = Session();
    Expect(session.BeginSelectionMove(), "zero-offset-opening-counter-fixture-reached");
    const auto opening = session.SelectionMoveOpening();
    session.EndSelectionMove();
    session.BeginEdit(L"Redo Fixture");
    session.EditForTest([](CanvasDocument& document) { document.resolution = 144; });
    session.EndEdit();
    session.Undo();
    const auto beforeHistory = session.history.CaptureState();
    const auto beforeRevision = session.history.CurrentRevision();
    const int beforeUndoCount = session.history.UndoCount();
    const std::wstring beforeUndoName = session.history.UndoName();
    const std::wstring beforeRedoName = session.history.RedoName();
    const auto beforeGeneration = session.Generation();
    auto staleInput = session.PrepareSelectionNudgeInput(1, 0);
    auto input = session.PrepareSelectionNudgeInput(0, 0);
    auto prepared = input ? DocumentSession::PrepareSelectionNudgePublication(std::move(input.value())) : std::nullopt;
    Expect(prepared && session.ApplyPreparedSelectionNudgeIfCurrent(prepared.value()), "zero-offset-is-acknowledged");
    Expect(!session.history.IsCurrent(beforeHistory) && session.history.CurrentRevision() == beforeRevision &&
               session.history.UndoCount() == beforeUndoCount && session.history.UndoName() == beforeUndoName &&
               session.history.RedoName() == beforeRedoName && session.history.CanRedo(),
           "zero-offset-preserves-redo-history-and-title");
    Expect(session.Generation() == beforeGeneration + 2 && staleInput.has_value() && ([&]
               {
                   auto stalePrepared = DocumentSession::PrepareSelectionNudgePublication(std::move(staleInput.value()));
                   return stalePrepared && !session.IsPreparedSelectionNudgeCurrent(*stalePrepared);
               }()),
           "zero-offset-advances-two-generations-and-nudge-opening");
    Expect(opening && session.BeginSelectionMove() &&
               session.SelectionMoveOpening() == std::optional(opening.value() + 2),
           "zero-offset-advances-exactly-one-outline-opening");
    session.CancelSelectionMove();
    session.Redo();
    Expect(session.Document().value().resolution == 144 && session.history.UndoName() == L"Redo Fixture",
           "zero-offset-retained-redo-restores-literal-resolution");
}

void GeometryCancellationBoundaries()
{
    for (const auto target : {SelectionNudgeStage::Begin, SelectionNudgeStage::Complete})
    {
        auto session = Session();
        const auto before = session.Document();
        const auto history = session.history.CaptureState();
        const auto generation = session.Generation();
        auto input = session.PrepareSelectionNudgeInput(1, 0);
        struct Stop final
        {
            CancelSource cancellation;
            SelectionNudgeStage target;
            unsigned hits = 0;
        } stop{{}, target};
        const SelectionNudgeObserver observer{&stop, [](void* opaque, SelectionNudgeStage stage, std::uint64_t) noexcept
                                              {
                                                  auto& state = *static_cast<Stop*>(opaque);
                                                  if (stage == state.target)
                                                  {
                                                      ++state.hits;
                                                      static_cast<void>(state.cancellation.Cancel());
                                                  }
                                              }};
        auto result = input ? DocumentSession::PrepareSelectionNudgePublication(
                                  std::move(input.value()), stop.cancellation.Token(), {}, &observer)
                            : std::nullopt;
        const auto label = std::format("cancel-{}-reached-preserves-complete-owner-state",
                                       target == SelectionNudgeStage::Begin ? "begin" : "complete");
        Expect(!result && stop.hits == 1 && session.Document() == before && session.history.IsCurrent(history) &&
                   session.Generation() == generation,
               label.c_str());
    }
}

void CancellationAndCheckpoints()
{
    struct Checkpoints final
    {
        CancelSource* cancel = nullptr;
        std::uint64_t previous = 0;
        std::uint64_t largestGap = 0;
        unsigned begin = 0;
        unsigned translate = 0;
        unsigned complete = 0;
        bool cancelOnTranslate = false;
    };
    const auto observe = [](void* opaque, SelectionNudgeStage stage, std::uint64_t work) noexcept
    {
        auto& state = *static_cast<Checkpoints*>(opaque);
        if (stage == SelectionNudgeStage::Begin)
            ++state.begin;
        else if (stage == SelectionNudgeStage::Translate)
            ++state.translate;
        else
            ++state.complete;
        state.largestGap = (std::max)(state.largestGap, work - state.previous);
        state.previous = work;
        if (state.cancelOnTranslate && stage == SelectionNudgeStage::Translate)
            static_cast<void>(state.cancel->Cancel());
    };
    {
        auto session = Session();
        CancelSource cancellation;
        static_cast<void>(cancellation.Cancel());
        auto input = session.PrepareSelectionNudgeInput(1, 0);
        Checkpoints points{&cancellation};
        const SelectionNudgeObserver observer{&points, observe};
        auto prepared = input ? DocumentSession::PrepareSelectionNudgePublication(std::move(input.value()),
                                                                                  cancellation.Token(), {}, &observer)
                              : std::nullopt;
        Expect(!prepared && points.begin == 0 && points.translate == 0 && points.complete == 0,
               "pre-cancelled-input-stops-before-geometry");
    }
    {
        auto session = Session();
        CancelSource cancellation;
        auto input = session.PrepareSelectionNudgeInput(1, 0);
        Checkpoints points{&cancellation, 0, 0, 0, 0, 0, true};
        const SelectionNudgeObserver observer{&points, observe};
        auto prepared = input ? DocumentSession::PrepareSelectionNudgePublication(std::move(input.value()),
                                                                                  cancellation.Token(), {}, &observer)
                              : std::nullopt;
        Expect(!prepared && cancellation.Token().IsCancelled() && points.begin == 1 && points.translate == 1 &&
                   points.complete == 0,
               "translation-checkpoint-cancels-before-publication");
    }
    {
        auto session = Session();
        std::vector<SelectionLoop> loops(3000);
        SelectionLoop manyPoints;
        manyPoints.reserve(2101);
        for (int i = 0; i < 2101; ++i)
            manyPoints.push_back(Vec2{static_cast<double>(i % 40), static_cast<double>(i / 40)});
        loops.push_back(std::move(manyPoints));
        session.SetSelection(DocumentSelection{true, std::move(loops)}, L"Checkpoint Geometry");
        CancelSource cancellation;
        auto input = session.PrepareSelectionNudgeInput(1, 0);
        Checkpoints points{&cancellation};
        const SelectionNudgeObserver observer{&points, observe};
        auto prepared = input ? DocumentSession::PrepareSelectionNudgePublication(std::move(input.value()),
                                                                                  cancellation.Token(), {}, &observer)
                              : std::nullopt;
        Expect(prepared && points.begin == 1 && points.translate > 2 && points.complete == 1,
               "large-geometry-checkpoints-cover-begin-translate-complete");
        Expect(points.largestGap <= 1024, "empty-loops-and-points-checkpoint-gap-at-most-1024");
    }
}
void CapacityAndCachedGeometry()
{
    auto session = Session();
    std::vector<SelectionLoop> loops;
    loops.reserve(4096); // The source retains spare capacity; the translated output needs only three loops.
    loops.push_back(SelectionLoop{Vec2{10, 10}, Vec2{30, 10}, Vec2{30, 30}, Vec2{10, 30}});
    loops.emplace_back();
    loops.push_back(SelectionLoop{Vec2{15, 15}, Vec2{15, 25}, Vec2{25, 25}, Vec2{25, 15}});
    session.SetSelection(DocumentSelection{false, std::move(loops)}, L"Capacity Source");
    const auto beforeBytes = session.Selection().value().loops.StorageBytes();
    const auto historyBytes = session.history.EditMemoryBytes(session.Document());
    const auto reserved = session.SelectionNudgeMemoryBytes();
    std::vector<SelectionLoop> outerCapacity;
    outerCapacity.reserve(3);
    SelectionLoop pointCapacity;
    pointCapacity.reserve(4);
    // The admission bound depends on the pinned v145 STL, as Core_PerformanceAndResources requires.
    // This is a toolset qualification gate, not a portable std::vector guarantee.
    Expect(outerCapacity.capacity() == 3 && pointCapacity.capacity() == 4,
           "capacity-pinned-reserve-does-not-grow-past-request");
    Expect(Nudge(session, 2, -3), "capacity-translated-output-publishes");
    const auto& output = session.Selection().value().loops;
    const auto bounds = output.Bounds();
    Expect(output.size() == 3 && output.PointCount() == 8 && bounds && bounds.value().minX == 12 &&
               bounds.value().minY == 7 && bounds.value().maxX == 32 && bounds.value().maxY == 27,
           "cache-literal-point-count-and-bounds-with-hole-and-empty-loop");
    Expect(output.StorageBytes() == sizeof(std::vector<SelectionLoop>) + 3 * sizeof(SelectionLoop) + 8 * sizeof(Vec2) &&
               output.StorageBytes() < beforeBytes && reserved > historyBytes &&
               reserved - historyBytes >= 2 * output.StorageBytes(),
           "capacity-reservation-covers-observed-output-overlap-without-source-spare-capacity-copy");
}

void HistoryPreparationFailures()
{
    using Stage = DocumentHistory::PrepareStage;
    constexpr std::array stages{Stage::BeforeSnapshot, Stage::AfterSnapshot, Stage::Entry, Stage::StackGrowth,
                                Stage::TrimIndex};
    for (const auto stage : stages)
        for (const bool allocation : {false, true})
        {
            auto session = Session();
            const auto before = session.Document();
            const auto history = session.history.CaptureState();
            const auto generation = session.Generation();
            auto input = session.PrepareSelectionNudgeInput(1, 0);
            struct Failure final
            {
                Stage stage;
                bool allocation;
                unsigned hits = 0;
                CancelSource cancellation;
            } failure{stage, allocation};
            const DocumentHistory::PrepareObserver observer{[](void* opaque, Stage current)
                                                            {
                                                                auto& state = *static_cast<Failure*>(opaque);
                                                                if (current != state.stage)
                                                                    return;
                                                                ++state.hits;
                                                                if (state.allocation)
                                                                    throw std::bad_alloc{};
                                                                static_cast<void>(state.cancellation.Cancel());
                                                            },
                                                            &failure};
            std::optional<PreparedSelectionNudge> result;
            bool propagated = false;
            try
            {
                if (input)
                    result = DocumentSession::PrepareSelectionNudgePublication(std::move(input.value()),
                                                                               failure.cancellation.Token(), observer);
            }
            catch (const std::bad_alloc&)
            {
                propagated = true;
            }
            const auto label = std::format("history-{}-{}-reached-no-partial-owner-publication",
                                           static_cast<unsigned>(stage), allocation ? "bad-alloc" : "cancel");
            Expect(!result && failure.hits == 1 && propagated == allocation && session.Document() == before &&
                       session.history.IsCurrent(history) && session.Generation() == generation,
                   label.c_str());
        }
}

void WorkerPreparationAndDisposal()
{
    enum class Mode
    {
        Success,
        Stale,
        LocalCancel,
        ShutdownCancel
    };
    for (const auto mode : {Mode::Success, Mode::Stale, Mode::LocalCancel, Mode::ShutdownCancel})
    {
        const char* name = mode == Mode::Success       ? "success"
                           : mode == Mode::Stale       ? "stale"
                           : mode == Mode::LocalCancel ? "local-cancel"
                                                       : "shutdown-cancel";
        const auto expect = [&](bool value, const char* suffix)
        {
            const auto label = std::format("worker-{}-{}", name, suffix);
            Expect(value, label.c_str());
        };
        auto session = Session();
        auto before = session.Document();
        auto history = session.history.CaptureState();
        const auto generation = session.Generation();
        auto input = session.PrepareSelectionNudgeInput(1, 0);
        expect(input.has_value(), "sealed-input");
        if (!input)
            continue;
        struct Trial final
        {
            std::mutex mutex;
            std::condition_variable condition;
            bool entered = false;
            bool release = false;
            bool completed = false;
            std::thread::id worker;
            std::thread::id disposed;
            std::optional<PreparedSelectionNudge> result;
        } trial;
        const SelectionNudgeObserver observer{&trial,
                                              [](void* opaque, SelectionNudgeStage stage, std::uint64_t) noexcept
                                              {
                                                  if (stage != SelectionNudgeStage::Translate)
                                                      return;
                                                  auto& state = *static_cast<Trial*>(opaque);
                                                  std::unique_lock lock(state.mutex);
                                                  if (state.entered)
                                                      return;
                                                  state.entered = true;
                                                  state.condition.notify_all();
                                                  state.condition.wait(lock, [&] { return state.release; });
                                              }};
        CancelSource local;
        CancelSource shutdown;
        const auto cancellation = CancelToken{local.Token().LocalToken(), shutdown.Token().LocalToken()};
        const auto owner = std::this_thread::get_id();
        std::jthread worker(
            [&trial, captured = std::move(input.value()), cancellation, observer]() mutable
            {
                trial.worker = std::this_thread::get_id();
                auto result =
                    DocumentSession::PrepareSelectionNudgePublication(std::move(captured), cancellation, {}, &observer);
                {
                    std::lock_guard lock(trial.mutex);
                    trial.result = std::move(result);
                    trial.completed = true;
                }
                trial.condition.notify_all();
            });
        bool reached = false;
        {
            std::unique_lock lock(trial.mutex);
            reached = trial.condition.wait_for(lock, std::chrono::seconds(5), [&] { return trial.entered; });
        }
        expect(reached && session.Document() == before && session.history.IsCurrent(history) &&
                   session.Generation() == generation,
               "held-worker-owner-unchanged");
        if (mode == Mode::LocalCancel)
            static_cast<void>(local.Cancel());
        if (mode == Mode::ShutdownCancel)
            static_cast<void>(shutdown.Cancel());
        {
            std::lock_guard lock(trial.mutex);
            trial.release = true;
        }
        trial.condition.notify_all();
        bool completed = false;
        {
            std::unique_lock lock(trial.mutex);
            completed = trial.condition.wait_for(lock, std::chrono::seconds(5), [&] { return trial.completed; });
        }
        expect(completed, "completion-within-failure-bound");
        if (!completed)
        {
            std::fflush(stdout);
            std::abort(); // A hung producer cannot safely be detached while it borrows the fixture.
        }
        worker.join();
        expect(trial.worker != owner, "preparation-off-owner");
        if (mode == Mode::LocalCancel || mode == Mode::ShutdownCancel)
        {
            expect(!trial.result && session.Document() == before && session.history.IsCurrent(history) &&
                       session.Generation() == generation,
                   "cancel-preserves-complete-owner-state");
            continue;
        }
        if (mode == Mode::Stale)
        {
            const auto active = session.ActiveLayerID();
            for (const auto& layer : session.Document().value().layers)
                if (active != std::optional(layer.id))
                {
                    session.SelectLayer(layer.id);
                    break;
                }
            expect(session.ActiveLayerID() != active && trial.result &&
                       !session.ApplyPreparedSelectionNudgeIfCurrent(trial.result.value()) &&
                       session.Document() == before && session.history.IsCurrent(history) &&
                       session.Generation() == generation,
                   "owner-refuses-stale-result-without-publication");
        }
        else
            expect(trial.result && session.ApplyPreparedSelectionNudgeIfCurrent(trial.result.value()) &&
                       LiteralSelection(session, true,
                                        {SelectionLoop{Vec2{11, 10}, Vec2{31, 10}, Vec2{31, 30}, Vec2{11, 30}}}),
                   "owner-applies-literal-result");
        if (!trial.result)
            continue;
        struct Retire final
        {
            std::optional<PreparedSelectionNudge> result;
            std::thread::id* disposed;
            Retire(PreparedSelectionNudge value, std::thread::id& thread) : result(std::move(value)), disposed(&thread)
            {
            }
            ~Retire()
            {
                result.reset(); // Dispose the actual prepared input/history/old-display/document aggregate here.
                *disposed = std::this_thread::get_id();
            }
        };
        auto retired = std::make_unique<Retire>(std::move(trial.result.value()), trial.disposed);
        trial.result.reset();
        session.ClearDocument();
        before.reset();
        history = DocumentHistory::StateView{};
        // This observes the complete result aggregate. It does not claim every shared raster's final release.
        std::jthread dispose([value = std::move(retired)]() mutable { value.reset(); });
        dispose.join();
        expect(trial.disposed != owner, "actual-prepared-aggregate-disposed-off-owner");
    }
}
void LateResidencyKeepsOriginalNudgeAndZeroOffset()
{
    using Validation = DocumentHistory::ResidencyValidation;
    auto session = Session();
    const std::uint64_t outlineBytes = session.Selection().value().loops.StorageBytes();
    constexpr std::array<std::uint8_t, 4> pixel{51, 52, 53, 255};
    const Raster leaf = Raster::Make(1, 1, RasterFormat::Bgra8888, pixel.data(), pixel.size());
    const Raster sparse = Raster::MakeSparse(2, 1, RasterFormat::Bgra8888, {}, 0, 0, {{0, 0, leaf}}, 1);
    auto historical = DocumentFixture::Copy(session.Document().value());
    historical.layers.front().asset = RasterAsset{sparse, {}, L"Historical nudge pixel"};
    session.history = DocumentHistory(100, 8 + outlineBytes);
    auto baseline = DocumentHistory::PrepareEdit(session.history.CaptureState(), std::move(historical).Seal(),
                                                 session.ActiveLayerID(), session.Document(), session.ActiveLayerID(),
                                                 L"Saved sparse history");
    if (!baseline || !session.history.Publish(baseline.value()))
    {
        Expect(false, "residency-literal-baseline");
        return;
    }
    baseline.reset();
    session.history.MarkSaved();
    const auto before = session.Document();
    const auto revision = session.history.CurrentRevision();
    const auto generation = session.Generation();
    const auto history = session.history.CaptureState();
    auto input = session.PrepareSelectionNudgeInput(1, 0);
    auto prepared = input ? DocumentSession::PrepareSelectionNudgePublication(std::move(input.value())) : std::nullopt;
    if (!prepared)
    {
        Expect(false, "residency-prepare-nudge");
        return;
    }
    const auto* pixels = sparse.Data();
    DocumentHistory::ResidencyPermit permit;
    Expect(pixels && pixels[0] == 51 && pixels[1] == 52 && pixels[2] == 53 && pixels[3] == 255 && pixels[4] == 1 &&
               sparse.ByteSize() == 8 && session.history.RetainedBytes(before) == 12 &&
               prepared.value().TryAcquireHistoryResidency(permit) == Validation::Refresh,
           "residency-literal-cache-growth-requests-worker-refresh");
    CancelSource stop;
    (void)stop.Cancel();
    Expect(!prepared.value().RefreshHistoryResidency(stop.Token()) && session.Document() == before &&
               session.Generation() == generation && session.history.IsCurrent(history) &&
               session.history.CurrentRevision() == revision && !session.history.IsModified(),
           "residency-cancelled-refresh-preserves-original-outline-opening-and-history");
    bool fault = false;
    const DocumentHistory::PrepareObserver observer{[](void*, DocumentHistory::PrepareStage stage)
                                                    {
                                                        if (stage == DocumentHistory::PrepareStage::StorageIndexClone)
                                                            throw std::bad_alloc{};
                                                    },
                                                    nullptr};
    try
    {
        (void)prepared.value().RefreshHistoryResidency({}, observer);
    }
    catch (const std::bad_alloc&)
    {
        fault = true; // Explicit injected construction failure; the worker runner owns production translation.
    }
    Expect(fault && session.Document() == before && session.Generation() == generation &&
               session.history.IsCurrent(history) && !session.history.IsModified() &&
               prepared.value().TryAcquireHistoryResidency(permit) == Validation::Refresh,
           "residency-refresh-allocation-failure-retains-sealed-nudge-and-owner-authority");
    PreparedSelectionNudge moved(std::move(prepared.value()));
    Expect(prepared.value().TryAcquireHistoryResidency(permit) == Validation::Invalid &&
               !prepared.value().RefreshHistoryResidency(),
           "residency-moved-source-refuses-refresh-and-permit");
    const bool refreshed = moved.RefreshHistoryResidency();
    const auto ready = moved.TryAcquireHistoryResidency(permit);
    const bool applied = refreshed && ready == Validation::Ready && session.ApplyPreparedSelectionNudgeIfCurrent(moved);
    permit.Reset();
    Expect(
        applied &&
            LiteralSelection(session, true, {SelectionLoop{Vec2{11, 10}, Vec2{31, 10}, Vec2{31, 30}, Vec2{11, 30}}}) &&
            session.Generation() == generation + 2 && session.history.CurrentRevision() != revision &&
            session.history.IsModified() && session.history.UndoCount() == 1 &&
            session.history.UndoName() == L"Move Selection" &&
            session.history.RetainedBytes(session.Document()) == outlineBytes,
        "residency-after-outline-witness-refreshes-one-literal-nudge-and-trims-oldest-cache");
    Expect(applied && moved.TryAcquireHistoryResidency(permit) == Validation::Invalid &&
               !moved.RefreshHistoryResidency() && !session.ApplyPreparedSelectionNudgeIfCurrent(moved),
           "residency-applied-nudge-refuses-republication-and-refresh");

    auto zeroSession = Session();
    const auto zeroDocument = zeroSession.Document();
    const auto zeroRevision = zeroSession.history.CurrentRevision();
    const auto zeroGeneration = zeroSession.Generation();
    auto zeroInput = zeroSession.PrepareSelectionNudgeInput(0, 0);
    auto zero =
        zeroInput ? DocumentSession::PrepareSelectionNudgePublication(std::move(zeroInput.value())) : std::nullopt;
    DocumentHistory::ResidencyPermit zeroPermit;
    const bool zeroRefreshed = zero && zero.value().RefreshHistoryResidency();
    const auto zeroReady = zero ? zero.value().TryAcquireHistoryResidency(zeroPermit) : Validation::Invalid;
    const bool zeroApplied = zeroRefreshed && zeroReady == Validation::Ready &&
                             zeroSession.ApplyPreparedSelectionNudgeIfCurrent(zero.value());
    zeroPermit.Reset();
    Expect(zeroApplied && zeroSession.Document() == zeroDocument &&
               zeroSession.history.CurrentRevision() == zeroRevision && zeroSession.Generation() == zeroGeneration + 2,
           "residency-zero-offset-refresh-uses-original-witness-and-keeps-existing-acknowledgement");
    Expect(zeroApplied && zero.value().TryAcquireHistoryResidency(zeroPermit) == Validation::Invalid &&
               !zero.value().RefreshHistoryResidency(),
           "residency-zero-offset-applied-output-is-single-use");
}
} // namespace

int RunSelectionNudgeTests()
{
    gFailures = 0;
    gAssertions = 0;
    LiteralArrowGolden();
    ShapeAndAdmission();
    StalenessAndTransactions();
    ZeroOffsetAcknowledgement();
    CancellationAndCheckpoints();
    GeometryCancellationBoundaries();
    CapacityAndCachedGeometry();
    HistoryPreparationFailures();
    WorkerPreparationAndDisposal();
    LateResidencyKeepsOriginalNudgeAndZeroOffset();
    std::printf("%d selection-nudge assertions; %d failed.\n", gAssertions, gFailures);
    return gFailures;
}
