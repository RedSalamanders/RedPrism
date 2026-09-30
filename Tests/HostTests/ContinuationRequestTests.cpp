#include "../../RedPrism/ContinuationRequests.h"

#include "ContinuationRequestTests.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <limits>
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
    const auto* input = taken.ready ? std::get_if<HostPathsInput>(&taken.ready.value().input.Input()) : nullptr;
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
        writeTaken.ready ? std::get_if<HostWriteInput>(&writeTaken.ready.value().input.Input()) : nullptr;
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
    const auto* input = taken.ready ? std::get_if<HostSizeApplyInput>(&taken.ready.value().input.Input()) : nullptr;
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
    const auto* layer =
        renamed.ready ? std::get_if<HostLayerRenameInput>(&renamed.ready.value().input.Input()) : nullptr;
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
    const auto* end = taken.ready ? std::get_if<HostEndSessionInput>(&taken.ready.value().input.Input()) : nullptr;
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
               std::get_if<HostToolInput>(&first.Input()) &&
               std::get<HostToolInput>(first.Input()).tool == EditorTool::Brush &&
               !second.Finish(ContinuationOutcome::Succeeded),
           "move-overwrite-retires-old-observer-and-transfers-new-publication");
    Expect(first.Finish(ContinuationOutcome::Succeeded) &&
               secondReceipt.Read().outcome == ContinuationOutcome::Succeeded,
           "moved-tool-owner-publishes-to-its-original-observer");
}

void TestInputBudgetBoundaries()
{
    Expect(HostInputBudget::kStorageLimit > 0 && HostInputBudget::kMaxPaths > 0 && HostInputBudget::kMaxPathUnits > 0 &&
               HostInputBudget::kStorageLimit == 1024 * 1024 && HostInputBudget::kMaxPaths == 256 &&
               HostInputBudget::kMaxPathUnits == 32767,
           "budget-limits-are-positive-and-match-host-admission-contract");

    HostInputBudget exactBudget;
    HostRequestInput exactInput = HostPanelOkInput{1};
    const auto exactBytes = HostInputStorageBytes(exactInput);
    if (!exactBytes || exactBytes.value() > HostInputBudget::kStorageLimit)
    {
        Expect(false, "request-admitted-at-exact-one-mib-budget-boundary");
        return;
    }
    auto filler = exactBudget.TryReserve(HostInputBudget::kStorageLimit - exactBytes.value());
    auto exact = HostRequest::TryCreate(exactInput, exactBudget);
    Expect(filler && exact && exactBudget.Used() == HostInputBudget::kStorageLimit,
           "request-admitted-at-exact-one-mib-budget-boundary");
    const auto overLimit = exactBudget.TryReserve((std::numeric_limits<std::size_t>::max)());
    Expect(!overLimit && exactBudget.Used() == HostInputBudget::kStorageLimit,
           "size-max-reservation-refuses-without-wrapping-budget");
    HostRequestInput quotaInput = HostNewInput{};
    const auto refusedAtQuota = HostRequest::TryCreate(quotaInput, exactBudget);
    const auto immediateFailure = ContinuationReceipt::Failure(-1);
    Expect(!refusedAtQuota && std::holds_alternative<HostNewInput>(quotaInput) &&
               immediateFailure.Read() == ContinuationReceipt::Result{ContinuationOutcome::Failed, -1},
           "quota-refusal-preserves-input-and-caller-can-return-immediate-failure");
    exact.reset();
    filler.reset();
    Expect(exactBudget.Used() == 0, "exact-boundary-reservations-return-to-zero-after-quota-refusal");

    HostInputBudget failedBudget;
    auto precedingReservation = failedBudget.TryReserve(128);
    HostRequestInput failedInput = HostPathsInput{{L"unchanged-after-receipt-allocation-failure"}};
    bool allocationFailed = false;
    try
    {
        auto refused = HostRequest::TryCreate(failedInput, failedBudget, true);
        (void)refused;
    }
    catch (const std::bad_alloc&)
    {
        allocationFailed = true;
    }
    Expect(precedingReservation && allocationFailed && failedBudget.Used() == 128 &&
               std::get<HostPathsInput>(failedInput).paths ==
                   std::vector<std::wstring>{L"unchanged-after-receipt-allocation-failure"},
           "receipt-allocation-failure-returns-only-its-ticket-and-preserves-input");

    HostInputBudget budget;
    HostRequestInput largeString = HostWriteInput{HostWriteFormat::Png, false, std::wstring{}};
    auto& write = std::get<HostWriteInput>(largeString);
    write.path.value().reserve(HostInputBudget::kStorageLimit / sizeof(wchar_t) + 1);
    write.path.value().push_back(L'x');
    const auto stringCapacity = write.path.value().capacity();
    const auto refusedString = HostRequest::TryCreate(largeString, budget);

    HostRequestInput largeVector = HostPathsInput{};
    auto& paths = std::get<HostPathsInput>(largeVector).paths;
    paths.reserve(HostInputBudget::kStorageLimit / sizeof(std::wstring) + 1);
    paths.emplace_back(L"x");
    const auto vectorCapacity = paths.capacity();
    const auto refusedVector = HostRequest::TryCreate(largeVector, budget);
    Expect(!refusedString && !refusedVector && budget.Used() == 0 &&
               std::get<HostWriteInput>(largeString).path.value() == L"x" &&
               std::get<HostWriteInput>(largeString).path.value().capacity() == stringCapacity &&
               std::get<HostPathsInput>(largeVector).paths.size() == 1 &&
               std::get<HostPathsInput>(largeVector).paths.front() == L"x" &&
               std::get<HostPathsInput>(largeVector).paths.capacity() == vectorCapacity,
           "retained-large-string-and-vector-refuse-with-original-input-intact");

    HostRequestInput tooManyPaths = HostPathsInput{};
    auto& excessivePaths = std::get<HostPathsInput>(tooManyPaths).paths;
    excessivePaths.resize(HostInputBudget::kMaxPaths + 1);
    excessivePaths.back() = L"keep-this";
    const auto refusedCount = HostRequest::TryCreate(tooManyPaths, budget);
    Expect(!refusedCount && budget.Used() == 0 && excessivePaths.size() == 257 && excessivePaths.back() == L"keep-this",
           "257-path-batch-refuses-with-payload-preserved");

    HostRequestInput longPath = HostPathsInput{};
    std::get<HostPathsInput>(longPath).paths.emplace_back(HostInputBudget::kMaxPathUnits + 1, L'p');
    const auto refusedLength = HostRequest::TryCreate(longPath, budget);
    Expect(!refusedLength && budget.Used() == 0 && std::get<HostPathsInput>(longPath).paths.front().size() == 32768,
           "32768-unit-path-refuses-with-payload-preserved");

    HostRequestInput maximumBatch = HostPathsInput{};
    auto& acceptedPaths = std::get<HostPathsInput>(maximumBatch).paths;
    acceptedPaths.resize(HostInputBudget::kMaxPaths, L"x");
    auto accepted = HostRequest::TryCreate(maximumBatch, budget);
    const bool acceptedWithinBudget = accepted && budget.Used() > 0 && budget.Used() <= HostInputBudget::kStorageLimit;
    accepted.reset();
    Expect(acceptedWithinBudget && budget.Used() == 0, "256-small-path-batch-admits-and-releases-its-charge");

    HostRequestInput optionalInputs = HostWriteInput{HostWriteFormat::Project, false, std::nullopt};
    auto writeWithoutPath = HostRequest::TryCreate(optionalInputs, budget);
    HostRequestInput optionalName = HostLayerRenameInput{Guid{}, std::nullopt};
    auto renameWithoutName = HostRequest::TryCreate(optionalName, budget);
    Expect(writeWithoutPath && renameWithoutName && budget.Used() > 0 &&
               !std::get<HostWriteInput>(writeWithoutPath->Input()).path &&
               !std::get<HostLayerRenameInput>(renameWithoutName->Input()).name,
           "absent-optional-write-path-and-rename-name-are-admitted");
}

void TestInputBudgetPublicationLifetime()
{
    HostInputBudget budget;
    Queue queue;
    HostRequestInput queuedInput = HostPathsInput{{L"queued-path"}};
    auto queued = HostRequest::TryCreate(queuedInput, budget);
    if (!queued)
    {
        Expect(false, "queued-request-created-with-budget-ticket");
        return;
    }
    const auto queuedCharge = budget.Used();
    const auto queuedReceipt = queued->Receipt();
    Expect(queuedCharge > 0 &&
               queue.Enqueue(101, 1, ContinuationKind::Import, queued.value(), Retire) ==
                   ContinuationAdmission::Queued &&
               budget.Used() == queuedCharge,
           "queue-transfer-keeps-request-charge");
    queued.reset(); // moved-from owner's destructor must not retire the queue's ticket
    Expect(queuedReceipt.Read().outcome == ContinuationOutcome::Pending && budget.Used() == queuedCharge,
           "moved-from-request-destruction-leaves-queued-receipt-pending");
    bool chargeHeldThroughRetirement = false;
    const auto observeCancellation = [&](Queue::Entry&& entry, ContinuationRetirement retirement) noexcept
    {
        entry.input.Retire(retirement);
        const auto* retainedPaths = std::get_if<HostPathsInput>(&entry.input.Input());
        chargeHeldThroughRetirement =
            budget.Used() == queuedCharge && queuedReceipt.Read().outcome == ContinuationOutcome::Cancelled &&
            retainedPaths && retainedPaths->paths.size() == 1 && retainedPaths->paths.front() == L"queued-path";
    };
    Expect(queue.Clear(observeCancellation) == 1 && chargeHeldThroughRetirement && budget.Used() == 0,
           "queue-clear-publishes-cancellation-and-releases-charge");

    HostRequestInput staleInput = HostPathsInput{{L"stale-path"}};
    auto stale = HostRequest::TryCreate(staleInput, budget);
    if (!stale)
    {
        Expect(false, "stale-request-construction");
        return;
    }
    const auto staleReceipt = stale->Receipt();
    const auto staleCharge = budget.Used();
    const auto staleAdmission = queue.Enqueue(102, 2, ContinuationKind::Import, stale.value(), Retire);
    stale.reset();
    const auto staleTaken = queue.TakeReady(103, false, Retire);
    Expect(staleAdmission == ContinuationAdmission::Queued && staleTaken.stale == 1 &&
               staleReceipt.Read().outcome == ContinuationOutcome::Stale && budget.Used() == 0 && staleCharge > 0,
           "stale-retirement-releases-queued-charge-after-publication");

    HostRequestInput activeInput = HostPathsInput{{L"active-path"}};
    auto activeRequest = HostRequest::TryCreate(activeInput, budget);
    if (!activeRequest)
    {
        Expect(false, "active-request-construction");
        return;
    }
    const auto activeReceipt = activeRequest->Receipt();
    const auto activeCharge = budget.Used();
    const auto activeAdmission = queue.Enqueue(104, 3, ContinuationKind::Import, activeRequest.value(), Retire);
    activeRequest.reset();
    auto active = queue.TakeReady(104, false, Retire);
    const bool retainedActive = activeAdmission == ContinuationAdmission::Queued && active.ready &&
                                budget.Used() == activeCharge &&
                                activeReceipt.Read().outcome == ContinuationOutcome::Pending;
    bool completedActive = false;
    if (active.ready)
    {
        completedActive = active.ready->input.Finish(ContinuationOutcome::Succeeded) && budget.Used() == activeCharge &&
                          activeReceipt.Read().outcome == ContinuationOutcome::Succeeded;
    }
    active.ready.reset();
    Expect(retainedActive && completedActive && budget.Used() == 0,
           "take-finish-and-destroy-active-request-retains-then-releases-charge");

    HostRequestInput firstInput = HostPathsInput{{std::wstring(200, L'o')}};
    auto first = HostRequest::TryCreate(firstInput, budget);
    if (!first)
    {
        Expect(false, "move-source-request-construction");
        return;
    }
    const auto firstReceipt = first->Receipt();
    HostRequestInput secondInput = HostPathsInput{{L"new-path"}};
    auto second = HostRequest::TryCreate(secondInput, budget);
    if (!second)
    {
        Expect(false, "move-replacement-request-construction");
        return;
    }
    const auto secondReceipt = second->Receipt();
    const auto bothCharges = budget.Used();
    const auto replacementCharge = HostInputStorageBytes(second->Input());
    first.value() = std::move(second.value());
    second.reset();
    const bool movedOwnership = bothCharges > 0 && replacementCharge &&
                                firstReceipt.Read().outcome == ContinuationOutcome::Cancelled &&
                                secondReceipt.Read().outcome == ContinuationOutcome::Pending &&
                                bothCharges > replacementCharge.value() && budget.Used() == replacementCharge.value();
    const bool movedFromStayedEmpty =
        first->Finish(ContinuationOutcome::Succeeded) && secondReceipt.Read().outcome == ContinuationOutcome::Succeeded;
    first.reset();
    Expect(movedOwnership && movedFromStayedEmpty && budget.Used() == 0,
           "move-assignment-and-moved-from-destruction-transfer-ticket-and-release-charge");
}
} // namespace

int RunContinuationRequestTests()
{
    gFailures = 0;
    TestOwnedPathsAndWriteParameters();
    TestCopiedSizeDraftAndLayerParameters();
    TestRetirementAndMoveOwnership();
    TestInputBudgetBoundaries();
    TestInputBudgetPublicationLifetime();
    return gFailures;
}
