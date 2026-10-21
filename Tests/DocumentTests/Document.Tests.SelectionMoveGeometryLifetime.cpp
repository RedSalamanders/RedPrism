#include "Document.Tests.SelectionMoveGeometryLifetime.h"

#include "../Support/Support.Tests.DocumentFixture.h"
#include "DeferredRelease.h"
#include "SelectionMoveWork.h"
#include "SelectionNudgeWork.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <memory>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

namespace
{
int gFailures = 0;
int gAssertions = 0;
void Expect(bool value, const char* label)
{
    ++gAssertions;
    gFailures += value ? 0 : 1;
    std::printf("%s selection-move-lifetime.%s\n", value ? "PASS" : "FAIL", label);
}
struct Trace final
{
    std::atomic<unsigned> destroyed{0};
    std::thread::id thread;
};
struct Lifetime final
{
    Trace* trace;
    explicit Lifetime(Trace& value) noexcept : trace(&value) {}
    ~Lifetime()
    {
        trace->thread = std::this_thread::get_id();
        trace->destroyed.fetch_add(1, std::memory_order_release);
    }
};
struct Sink final
{
    DeferredReleaseNode* pending = nullptr;
    unsigned count = 0;
    static void Enqueue(void* opaque, DeferredReleaseNode& node) noexcept
    {
        auto& self = *static_cast<Sink*>(opaque);
        ++self.count;
        if (self.pending)
            std::terminate();
        self.pending = &node;
    }
};
SelectionLoops Source()
{
    return SelectionLoops{SelectionLoop{{1, 2}, {3, 2}, {3, 4}, {1, 4}}};
}
struct StopAtComplete final
{
    CancelSource* source;
    bool visited = false;
    static void Checkpoint(void* opaque, SelectionNudgeStage stage, std::uint64_t) noexcept
    {
        auto& self = *static_cast<StopAtComplete*>(opaque);
        if (stage == SelectionNudgeStage::Complete)
        {
            self.visited = true;
            (void)self.source->Cancel();
        }
    }
};
void GeometryAndWeakControls()
{
    const auto source = Source();
    const auto original = source.Identity();
    Trace trace;
    std::shared_ptr<const void> lifetime = std::make_shared<Lifetime>(trace);
    std::weak_ptr<const void> weakLifetime = lifetime;
    Sink sink;
    const bool installed = InstallDeferredReleaseSink(&Sink::Enqueue, &sink);
    Expect(installed, "owner-sink-installs");
    if (!installed)
        return;
    auto output = TranslateSelectionLoopsForWork(source, -2, 3, {}, {}, lifetime);
    Expect(output && output->Get() == std::vector<SelectionLoop>{SelectionLoop{{-1, 5}, {1, 5}, {1, 7}, {-1, 7}}},
           "owned-translation-preserves-four-literal-vertices");
    if (!output)
    {
        (void)ClearDeferredReleaseSink(&sink);
        return;
    }
    Expect(output->PointCount() == 4 && output->Bounds() == std::optional(SelectionBounds{-1, 5, 1, 7}) &&
               source.Identity() == original && source[0][0] == Vec2{1, 2},
           "owned-translation-caches-metadata-without-changing-source");
    const auto required = output->StorageBytes() + SelectionLoops::WorkOwnershipAllowance(1);
    auto weakGeometry = output->WeakStorageForTest();
    auto reader = output.value();
    lifetime.reset();
    output.reset();
    Expect(!weakLifetime.expired() && sink.count == 0 && reader[0][0] == Vec2{-1, 5},
           "copied-reader-retains-original-lifetime-and-points");
    reader = SelectionLoops{};
    Expect(sink.count == 1 && sink.pending && weakGeometry.expired() && !weakLifetime.expired() &&
               trace.destroyed.load() == 0,
           "last-owner-reader-defers-node-and-retains-lifetime");
    Expect(sink.pending && sink.pending->bytes >= required,
           "deferred-node-declares-cached-capacity-and-ownership-controls");
    const bool cleared = ClearDeferredReleaseSink(&sink);
    Expect(cleared, "owner-sink-clears-before-worker-retirement");
    auto* retired = std::exchange(sink.pending, nullptr);
    if (!retired)
        return;
    std::thread geometryWorker([retired] { retired->destroy(retired); });
    geometryWorker.join();
    Expect(weakGeometry.expired() && !weakLifetime.expired() && trace.destroyed.load() == 0,
           "final-weak-control-retains-lifetime-after-worker-frees-geometry");
    std::thread::id finalWorker;
    std::thread controlWorker(
        [held = std::move(weakGeometry), &finalWorker]() mutable
        {
            finalWorker = std::this_thread::get_id();
            held.reset();
        });
    controlWorker.join();
    Expect(weakLifetime.expired() && trace.destroyed.load() == 1 && trace.thread == finalWorker &&
               trace.thread != std::this_thread::get_id(),
           "last-weak-worker-releases-original-lifetime-once-off-owner");
}
void NoOutputAndReferenceControls()
{
    const auto source = Source();
    auto lifetime = std::make_shared<int>(7);
    auto noChange = TranslateSelectionLoopsForWork(source, 0, 0, {}, {}, lifetime);
    Expect(noChange && noChange->Identity() == source.Identity() && lifetime.use_count() == 1,
           "zero-offset-shares-source-without-new-lifetime-owner");
    CancelSource stopped;
    (void)stopped.Cancel();
    Expect(!TranslateSelectionLoopsForWork(source, 1, 1, stopped.Token(), {}, lifetime) && lifetime.use_count() == 1,
           "pre-cancelled-output-retains-no-new-lifetime-owner");
    CancelSource cancelled;
    StopAtComplete stop{&cancelled};
    Expect(!TranslateSelectionLoopsForWork(source, 1, 1, cancelled.Token(), {&stop, &StopAtComplete::Checkpoint},
                                           lifetime) &&
               stop.visited && lifetime.use_count() == 1,
           "complete-cancellation-discards-unpublished-geometry-and-lifetime");
    Sink sink;
    const bool installed = InstallDeferredReleaseSink(&Sink::Enqueue, &sink);
    Expect(installed, "reference-control-owner-sink-installs");
    if (!installed)
        return;
    auto reference = TranslateSelectionLoopsForWork(source, -2, 3);
    Expect(reference && reference->Get() == std::vector<SelectionLoop>{SelectionLoop{{-1, 5}, {1, 5}, {1, 7}, {-1, 7}}},
           "empty-lifetime-keeps-literal-reference-output");
    reference.reset();
    Expect(sink.count == 0 && !sink.pending && ClearDeferredReleaseSink(&sink),
           "empty-lifetime-preserves-existing-reference-retirement");
}
void PreparedClosureCarriesLifetime()
{
    Trace trace;
    auto session = std::make_unique<DocumentSession>();
    DocumentFixture fixture;
    fixture.id = Guid::Generate();
    fixture.width = 8;
    fixture.height = 8;
    fixture.selection = DocumentSelection{false, Source()};
    auto layer = ImageLayer::Blank(L"Move lifetime", Vec2{8, 8});
    const auto active = layer.id;
    fixture.layers.push_back(std::move(layer));
    session->ReplaceDocument(std::move(fixture).Seal(), active);
    const bool began = session->BeginDeferredSelectionMove();
    Expect(began, "prepared-closure-fixture-begins");
    if (!began)
        return;
    session->MoveSelection(-2, 3);
    auto input = std::unique_ptr<std::optional<SelectionMoveClosureInput>>(
        new std::optional<SelectionMoveClosureInput>(session->PrepareSelectionMoveClosureInput(false)));
    Expect(input->has_value(), "prepared-closure-capture-keeps-original-input");
    if (!*input)
        return;
    std::shared_ptr<const void> lifetime = std::make_shared<Lifetime>(trace);
    std::weak_ptr<const void> weakLifetime = lifetime;
    auto prepared = std::unique_ptr<std::optional<PreparedSelectionMoveClosure>>(
        new std::optional<PreparedSelectionMoveClosure>(DocumentSession::PrepareSelectionMoveClosurePublication(
            std::move(input->value()), {}, {}, nullptr, lifetime)));
    Expect(*prepared && session->ApplyPreparedSelectionMoveClosureIfCurrent(prepared->value()) &&
               session->Selection()->loops[0][0] == Vec2{-1, 5} && !session->IsMovingSelection(),
           "prepared-closure-publishes-owned-literal-translation");
    if (!*prepared || !session->Selection())
        return;
    auto weakGeometry = session->Selection()->loops.WeakStorageForTest();
    lifetime.reset();
    Expect(!weakLifetime.expired() && trace.destroyed.load() == 0,
           "published-document-and-history-retain-original-lifetime");
    std::thread aggregateWorker(
        [ownedSession = std::move(session), ownedPrepared = std::move(prepared)]() mutable
        {
            ownedPrepared.reset();
            ownedSession.reset();
        });
    aggregateWorker.join();
    Expect(weakGeometry.expired() && !weakLifetime.expired() && trace.destroyed.load() == 0,
           "retired-closure-document-history-leave-only-charged-weak-control");
    std::thread::id workerID;
    std::thread weakWorker(
        [held = std::move(weakGeometry), &workerID]() mutable
        {
            workerID = std::this_thread::get_id();
            held.reset();
        });
    weakWorker.join();
    Expect(weakLifetime.expired() && trace.destroyed.load() == 1 && trace.thread == workerID,
           "published-closure-last-weak-retires-lifetime-off-owner");
}
} // namespace

int RunSelectionMoveGeometryLifetimeTests()
{
    gFailures = 0;
    gAssertions = 0;
    GeometryAndWeakControls();
    NoOutputAndReferenceControls();
    PreparedClosureCarriesLifetime();
    std::printf("SUMMARY selection-move-lifetime assertions=%d failures=%d\n", gAssertions, gFailures);
    return gFailures;
}
