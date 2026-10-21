#include "Document.Tests.HistoryPreparationFault.h"

#include "../Support/Support.Tests.DocumentFixture.h"
#include "Document.h"

#include <array>
#include <cstdio>
#include <new>
#include <optional>
#include <string>
#include <utility>

namespace
{
int gFailures = 0;

enum class Operation
{
    Edit,
    Begin,
    End,
    Undo,
    Redo,
};

enum class FailureMode
{
    BadAlloc,
    Cancellation,
};

struct CheckpointFailure final
{
    DocumentHistory::PrepareStage target{};
    FailureMode mode{};
    CancelSource* cancellation = nullptr;
    int targetHits = 0;
    int checkpointCalls = 0;
};

void FailAtCheckpoint(void* opaque, DocumentHistory::PrepareStage stage)
{
    auto& failure = *static_cast<CheckpointFailure*>(opaque);
    ++failure.checkpointCalls;
    if (stage != failure.target)
        return;

    ++failure.targetHits;
    if (failure.mode == FailureMode::BadAlloc)
        throw std::bad_alloc{};
    (void)failure.cancellation->Cancel();
}

const char* OperationName(Operation operation) noexcept
{
    switch (operation)
    {
    case Operation::Edit:
        return "edit";
    case Operation::Begin:
        return "begin";
    case Operation::End:
        return "end";
    case Operation::Undo:
        return "undo";
    case Operation::Redo:
        return "redo";
    }
    return "invalid-operation";
}

const char* StageName(DocumentHistory::PrepareStage stage) noexcept
{
    using Stage = DocumentHistory::PrepareStage;
    switch (stage)
    {
    case Stage::BeforeSnapshot:
        return "before-snapshot";
    case Stage::AfterSnapshot:
        return "after-snapshot";
    case Stage::Entry:
        return "entry";
    case Stage::StackGrowth:
        return "stack-growth";
    case Stage::TrimIndex:
        return "trim-index";
    case Stage::PendingSnapshot:
        return "pending-snapshot";
    case Stage::PendingRecord:
        return "pending-record";
    case Stage::RestoreSnapshot:
        return "restore-snapshot";
    }
    return "invalid-stage";
}

const char* FailureName(FailureMode mode) noexcept
{
    return mode == FailureMode::BadAlloc ? "bad-alloc" : "cancellation";
}

void Expect(bool condition, Operation operation, DocumentHistory::PrepareStage stage, FailureMode mode,
            const char* fact) noexcept
{
    std::printf("%s history-preparation-fault.%s.%s.%s.%s\n", condition ? "PASS" : "FAIL", OperationName(operation),
                StageName(stage), FailureName(mode), fact);
    if (!condition)
        ++gFailures;
}

CanvasDocument MakePixelDocument(std::uint8_t blue)
{
    DocumentFixture document;
    ImageLayer layer = ImageLayer::Blank(L"History fault fixture", Vec2{1, 1});
    const std::array<std::uint8_t, 4> pixels{blue, 0x35, 0x79, 0xff};
    const Raster raster = Raster::Make(1, 1, RasterFormat::Bgra8888, pixels.data(), pixels.size());
    layer.asset = RasterAsset{raster, raster, L"History fault fixture"};
    document.layers.push_back(std::move(layer));
    return std::move(document).Seal();
}

CanvasDocument WithPixel(const CanvasDocument& source, std::uint8_t blue)
{
    DocumentFixture result = DocumentFixture::Copy(source);
    const std::array<std::uint8_t, 4> pixels{blue, 0x35, 0x79, 0xff};
    const Raster raster = Raster::Make(1, 1, RasterFormat::Bgra8888, pixels.data(), pixels.size());
    result.layers.front().asset = RasterAsset{raster, raster, L"History fault fixture"};
    return std::move(result).Seal();
}

bool HasPixel(const CanvasDocument& document, std::uint8_t blue) noexcept
{
    if (document.layers.size() != 1 || !document.layers.front().asset)
        return false;
    const Raster& raster = document.layers.front().asset->image;
    const std::uint8_t* pixels = raster.Data();
    return raster.Width() == 1 && raster.Height() == 1 && pixels != nullptr && pixels[0] == blue && pixels[1] == 0x35 &&
           pixels[2] == 0x79 && pixels[3] == 0xff;
}

struct Fixture final
{
    DocumentHistory history;
    CanvasDocument original = MakePixelDocument(0x11);
    CanvasDocument changed = WithPixel(original, 0x22);
    std::optional<CanvasDocument> live{original};
    std::optional<Guid> active{original.layers.front().id};

    void SeedCommittedEdit()
    {
        history.Begin(L"Fixture redo entry", live, active);
        live = changed;
        history.End(live, active);
    }

    void SeedRedoEntry()
    {
        SeedCommittedEdit();
        const auto snapshot = history.Undo();
        if (snapshot)
        {
            live = snapshot->document;
            active = snapshot->activeLayerID;
        }
    }

    void SeedOpenChangedTransaction()
    {
        history.Begin(L"Fixture open transaction", live, active);
        live = changed;
    }
};

bool SameHistoryState(const DocumentHistory& history, const DocumentHistory::StateView& before)
{
    const DocumentHistory::StateView after = history.CaptureState();
    return history.CurrentRevision() == before.Revision() && history.UndoCount() == before.PastCount() &&
           history.UndoName() == before.UndoName() && history.RedoName() == before.RedoName() &&
           after.Revision() == before.Revision() && after.Depth() == before.Depth() &&
           after.PastCount() == before.PastCount() && after.FutureCount() == before.FutureCount() &&
           after.UndoName() == before.UndoName() && after.RedoName() == before.RedoName();
}

void RunFailureCase(Operation operation, DocumentHistory::PrepareStage stage, FailureMode mode)
{
    Fixture fixture;
    switch (operation)
    {
    case Operation::Edit:
        fixture.SeedRedoEntry();
        break;
    case Operation::Begin:
        break;
    case Operation::End:
        fixture.SeedOpenChangedTransaction();
        break;
    case Operation::Undo:
        fixture.SeedCommittedEdit();
        break;
    case Operation::Redo:
        fixture.SeedRedoEntry();
        break;
    }

    const DocumentHistory::StateView stateBefore = fixture.history.CaptureState();
    const auto liveBefore = fixture.live;
    const auto changedBefore = fixture.changed;

    CancelSource cancellation;
    CheckpointFailure checkpoint{stage, mode, &cancellation};
    const DocumentHistory::PrepareObserver observer{&FailAtCheckpoint, &checkpoint};
    std::optional<DocumentHistory::Publication> publication;
    bool caughtBadAlloc = false;
    try
    {
        switch (operation)
        {
        case Operation::Edit:
        {
            const CanvasDocument editResult = WithPixel(fixture.original, 0x33);
            publication =
                DocumentHistory::PrepareEdit(fixture.history.CaptureState(), fixture.live, fixture.active, editResult,
                                             fixture.active, L"Faulted edit", cancellation.Token(), observer);
            break;
        }
        case Operation::Begin:
            publication = DocumentHistory::PrepareBegin(fixture.history.CaptureState(), fixture.live, fixture.active,
                                                        L"Faulted transaction", cancellation.Token(), observer);
            break;
        case Operation::End:
            publication = DocumentHistory::PrepareEnd(fixture.history.CaptureState(), fixture.live, fixture.active,
                                                      cancellation.Token(), observer);
            break;
        case Operation::Undo:
            publication =
                DocumentHistory::PrepareRestore(fixture.history.CaptureState(), false, cancellation.Token(), observer);
            break;
        case Operation::Redo:
            publication =
                DocumentHistory::PrepareRestore(fixture.history.CaptureState(), true, cancellation.Token(), observer);
            break;
        }
    }
    catch (const std::bad_alloc&)
    {
        caughtBadAlloc = true;
    }

    const bool didFailAsRequested = mode == FailureMode::BadAlloc
                                        ? caughtBadAlloc && !publication
                                        : !caughtBadAlloc && !publication && cancellation.Token().IsCancelled();
    Expect(didFailAsRequested, operation, stage, mode, "requested-failure-observed");
    Expect(checkpoint.targetHits == 1 && checkpoint.checkpointCalls >= 1, operation, stage, mode,
           "requested-stage-reached-once");
    Expect(SameHistoryState(fixture.history, stateBefore), operation, stage, mode,
           "revision-names-counts-and-depth-unchanged");
    Expect(fixture.live == liveBefore && fixture.changed == changedBefore && HasPixel(fixture.original, 0x11),
           operation, stage, mode, "fixture-documents-and-original-pixels-unchanged");
    const std::uint8_t expectedLivePixel = operation == Operation::End || operation == Operation::Undo ? 0x22 : 0x11;
    Expect(fixture.live && HasPixel(*fixture.live, expectedLivePixel), operation, stage, mode,
           "live-source-pixels-match-literal");

    if (operation == Operation::Edit)
    {
        Expect(stateBefore.FutureCount() == 1 && stateBefore.RedoName() == L"Fixture redo entry" && fixture.live &&
                   HasPixel(*fixture.live, 0x11),
               operation, stage, mode, "redo-entry-and-source-pixel-retained");
    }
    if (operation == Operation::End)
    {
        Expect(stateBefore.Depth() == 1 && fixture.live && HasPixel(*fixture.live, 0x22), operation, stage, mode,
               "open-transaction-and-live-pixel-retained");
    }
}

template <std::size_t N>
void RunOperationCases(Operation operation, const std::array<DocumentHistory::PrepareStage, N>& stages)
{
    for (const auto stage : stages)
    {
        RunFailureCase(operation, stage, FailureMode::BadAlloc);
        RunFailureCase(operation, stage, FailureMode::Cancellation);
    }
}
} // namespace

int RunHistoryPreparationFaultTests()
{
    gFailures = 0;
    using Stage = DocumentHistory::PrepareStage;
    RunOperationCases(Operation::Edit, std::array{Stage::BeforeSnapshot, Stage::AfterSnapshot, Stage::Entry,
                                                  Stage::StackGrowth, Stage::TrimIndex});
    RunOperationCases(Operation::Begin, std::array{Stage::PendingSnapshot, Stage::PendingRecord});
    RunOperationCases(Operation::End,
                      std::array{Stage::AfterSnapshot, Stage::Entry, Stage::StackGrowth, Stage::TrimIndex});
    RunOperationCases(Operation::Undo, std::array{Stage::RestoreSnapshot, Stage::StackGrowth, Stage::TrimIndex});
    RunOperationCases(Operation::Redo, std::array{Stage::RestoreSnapshot, Stage::StackGrowth, Stage::TrimIndex});
    return gFailures;
}
