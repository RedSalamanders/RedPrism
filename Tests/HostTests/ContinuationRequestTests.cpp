#include "../../RedPrism/ContinuationRequests.h"

#include "ContinuationRequestTests.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace
{
using Queue = ContinuationQueue<HostRequest>;
int gFailures = 0;

static_assert(!std::is_copy_constructible_v<HostRequest> && !std::is_copy_assignable_v<HostRequest>);
static_assert(std::is_nothrow_move_constructible_v<HostRequest> && std::is_nothrow_move_assignable_v<HostRequest>);

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s continuation-request.%s\n", condition ? "PASS" : "FAIL", name);
    if (!condition)
    {
        ++gFailures;
    }
}

void Retire(Queue::Entry&& entry, ContinuationRetirement retirement) noexcept
{
    entry.input.Retire(retirement);
}

void TestOwnedPathsAndWriteParameters()
{
    Queue queue;
    std::vector<std::wstring> paths{L"C:\\photos\\été.arw", L"C:\\photos\\second.cr3", L"C:\\photos\\third.raf"};
    Vec2 point{123.5, -42.25};
    HostRequest batch = HostRequest::Create(HostPathsInput{paths, HostPathAction::ImagesAsLayers, point});
    const auto batchReceipt = batch.Receipt();
    Expect(queue.Enqueue(51, 1, ContinuationKind::Import, batch, Retire) == ContinuationAdmission::Queued,
           "whole-path-batch-admitted-as-one-entry");
    paths[0] = L"changed.nef";
    paths.clear();
    point = {};
    const auto blocked = queue.TakeReady(51, true, Retire);
    Expect(!blocked.ready && queue.Size() == 1 && batchReceipt.Read().outcome == ContinuationOutcome::Pending,
           "busy-batch-keeps-owned-input-and-pending-receipt");
    auto taken = queue.TakeReady(51, false, Retire);
    const auto* input = taken.ready ? std::get_if<HostPathsInput>(&taken.ready.value().input.input) : nullptr;
    Expect(input &&
               input->paths == std::vector<std::wstring>{L"C:\\photos\\été.arw", L"C:\\photos\\second.cr3",
                                                         L"C:\\photos\\third.raf"} &&
               input->action == HostPathAction::ImagesAsLayers && input->dropPoint == Vec2{123.5, -42.25},
           "dequeue-uses-original-unicode-path-order-and-drop-point");
    if (!taken.ready)
    {
        return;
    }
    Expect(taken.ready.value().input.Finish(ContinuationOutcome::Succeeded), "path-owner-completes-selected-input");
    taken.ready.reset();
    Expect(batchReceipt.Read().outcome == ContinuationOutcome::Succeeded,
           "path-batch-receipt-survives-active-input-release");

    std::wstring selectedPath = L"C:\\exports\\été.png";
    HostRequest write = HostRequest::Create(HostWriteInput{HostWriteFormat::Png, true, selectedPath});
    const auto writeReceipt = write.Receipt();
    Expect(queue.Enqueue(51, 2, ContinuationKind::Command, write, Retire) == ContinuationAdmission::Queued,
           "write-arguments-admitted");
    selectedPath.clear();
    auto writeTaken = queue.TakeReady(51, false, Retire);
    const auto* output =
        writeTaken.ready ? std::get_if<HostWriteInput>(&writeTaken.ready.value().input.input) : nullptr;
    Expect(output && output->format == HostWriteFormat::Png && output->saveAs &&
               output->path == L"C:\\exports\\été.png",
           "write-keeps-format-save-as-and-selected-path");
    if (writeTaken.ready)
    {
        Expect(writeTaken.ready.value().input.Finish(ContinuationOutcome::Failed, -7), "write-owner-publishes-failure");
    }
    writeTaken.ready.reset();
    Expect(writeReceipt.Read() == ContinuationReceipt::Result{ContinuationOutcome::Failed, -7},
           "failed-write-receipt-keeps-its-error-after-input-release");
}

void TestCopiedSizeDraftAndLayerParameters()
{
    Queue queue;
    HostSizeDraft draft;
    draft.kind = static_cast<SizeCardKind>(1); // New Canvas, without a DxUi/Windows header dependency
    draft.canvas.originalWidth = 640;
    draft.canvas.originalHeight = 480;
    draft.canvas.width = 513;
    draft.canvas.height = 271;
    draft.canvas.resolution = 180;
    draft.canvas.unit = CanvasUnit::Inches;
    draft.canvas.relative = true;
    draft.canvas.locked = true;
    draft.anchor = 8;
    draft.extension = 5;
    draft.customArgb = 0xFF123456u;
    draft.customBefore = 0xFF654321u;
    draft.image.width = 17;
    draft.image.height = 19;
    draft.image.originalWidth = 53;
    draft.image.originalHeight = 89;
    draft.image.resolution = 240;
    draft.image.locked = false;
    draft.image.unit = CanvasUnit::Centimeters;
    draft.image.sampling = LayerSampling::Nearest;
    draft.image.resample = false;
    draft.newWidth = 2048;
    draft.newHeight = 1536;
    HostRequest size = HostRequest::Create(HostSizeApplyInput{draft, 71});
    const auto sizeReceipt = size.Receipt();
    Expect(queue.Enqueue(61, 1, ContinuationKind::Command, size, Retire) == ContinuationAdmission::Queued,
           "complete-size-draft-admitted");
    draft = {};
    auto taken = queue.TakeReady(61, false, Retire);
    const auto* input = taken.ready ? std::get_if<HostSizeApplyInput>(&taken.ready.value().input.input) : nullptr;
    Expect(input && input->invocation == 71 && static_cast<std::uint8_t>(input->draft.kind) == 1 &&
               input->draft.canvas.originalWidth == 640 && input->draft.canvas.originalHeight == 480 &&
               input->draft.canvas.width == 513 && input->draft.canvas.height == 271 &&
               input->draft.canvas.resolution == 180 && input->draft.canvas.unit == CanvasUnit::Inches &&
               input->draft.canvas.relative && input->draft.canvas.locked && input->draft.anchor == 8 &&
               input->draft.extension == 5 && input->draft.customArgb == 0xFF123456u &&
               input->draft.customBefore == 0xFF654321u && input->draft.image.width == 17 &&
               input->draft.image.height == 19 && input->draft.image.originalWidth == 53 &&
               input->draft.image.originalHeight == 89 && input->draft.image.resolution == 240 &&
               !input->draft.image.resample && !input->draft.image.locked &&
               input->draft.image.unit == CanvasUnit::Centimeters &&
               input->draft.image.sampling == LayerSampling::Nearest && input->draft.newWidth == 2048 &&
               input->draft.newHeight == 1536,
           "later-card-edits-cannot-change-the-accepted-size-draft");
    taken.ready.reset();
    Expect(sizeReceipt.Read().outcome == ContinuationOutcome::Cancelled,
           "abandoned-size-owner-cancels-observer-before-draft-release");

    Guid id;
    id.bytes[0] = 17;
    std::wstring name = L"Première couche";
    HostRequest rename = HostRequest::Create(HostLayerRenameInput{id, name});
    const auto receipt = rename.Receipt();
    Expect(queue.Enqueue(61, 2, ContinuationKind::Command, rename, Retire) == ContinuationAdmission::Queued,
           "rename-captures-layer-and-owned-name");
    id.bytes[0] = 29;
    name.clear();
    auto renamed = queue.TakeReady(61, false, Retire);
    const auto* layer = renamed.ready ? std::get_if<HostLayerRenameInput>(&renamed.ready.value().input.input) : nullptr;
    Expect(layer && layer->id.bytes[0] == 17 && layer->name == L"Première couche",
           "renamed-input-keeps-original-target-and-unicode-name");
    renamed.ready.reset();
    Expect(receipt.Read().outcome == ContinuationOutcome::Cancelled, "abandoned-rename-receipt-cancels");
}

void TestRetirementAndMoveOwnership()
{
    Queue queue;
    HostRequest old = HostRequest::Create(HostPanelOkInput{11});
    const auto oldReceipt = old.Receipt();
    Expect(queue.Enqueue(81, 1, ContinuationKind::Command, old, Retire) == ContinuationAdmission::Queued,
           "old-panel-input-admitted");
    HostRequest current = HostRequest::Create(HostEndSessionInput{99, 0x80000000LL});
    const auto currentReceipt = current.Receipt();
    Expect(queue.Enqueue(82, 2, ContinuationKind::Command, current, Retire) == ContinuationAdmission::Queued,
           "session-end-input-admitted");
    auto taken = queue.TakeReady(82, false, Retire);
    const auto* end = taken.ready ? std::get_if<HostEndSessionInput>(&taken.ready.value().input.input) : nullptr;
    Expect(taken.stale == 1 && oldReceipt.Read().outcome == ContinuationOutcome::Stale && end &&
               end->generation == 99 && end->reasons == 0x80000000LL &&
               currentReceipt.Read().outcome == ContinuationOutcome::Pending,
           "replaced-document-input-is-stale-while-current-session-end-stays-pending");
    if (!taken.ready)
    {
        return;
    }
    taken.ready.value().input.Retire(ContinuationRetirement::Cancelled);
    Expect(!taken.ready.value().input.Finish(ContinuationOutcome::Succeeded) &&
               currentReceipt.Read().outcome == ContinuationOutcome::Cancelled,
           "cancelled-session-end-cannot-be-republished-as-success");

    HostRequest first = HostRequest::Create(HostCommandInput{123});
    const auto firstReceipt = first.Receipt();
    HostRequest second = HostRequest::Create(HostToolInput{EditorTool::Brush});
    const auto secondReceipt = second.Receipt();
    first = std::move(second);
    Expect(firstReceipt.Read().outcome == ContinuationOutcome::Cancelled &&
               secondReceipt.Read().outcome == ContinuationOutcome::Pending &&
               std::get_if<HostToolInput>(&first.input) &&
               std::get<HostToolInput>(first.input).tool == EditorTool::Brush &&
               !second.Finish(ContinuationOutcome::Succeeded),
           "move-overwrite-retires-old-observer-and-transfers-new-publication");
    Expect(first.Finish(ContinuationOutcome::Succeeded) &&
               secondReceipt.Read().outcome == ContinuationOutcome::Succeeded,
           "moved-tool-owner-publishes-to-its-original-observer");
}
} // namespace

int RunContinuationRequestTests()
{
    gFailures = 0;
    TestOwnedPathsAndWriteParameters();
    TestCopiedSizeDraftAndLayerParameters();
    TestRetirementAndMoveOwnership();
    return gFailures;
}
