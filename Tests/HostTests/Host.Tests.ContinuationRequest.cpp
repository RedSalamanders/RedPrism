#include "../../RedPrism/ContinuationRequests.h"

#include "Host.Tests.ContinuationRequest.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <new>
#include <span>
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

void TestBorrowedInputCopies()
{
    HostInputBudget budget;
    std::array<std::wstring, 3> source{L"C:\\photos\\été.arw", L"C:\\photos\\δεύτερο.cr3", L"C:\\photos\\第三.raf"};
    std::array<std::wstring_view, 3> views{source[0], source[1], source[2]};
    const Vec2 dropPoint{19.25, -7.5};
    auto copied = HostRequest::TryCopyPaths(views, HostPathAction::ImagesAsLayers, dropPoint, budget);
    const auto copiedCharge = copied ? HostInputStorageBytes(copied->Input()) : std::nullopt;
    source[0] = L"changed-0";
    source[1] = L"changed-1";
    source[2] = L"changed-2";
    const auto* paths = copied ? std::get_if<HostPathsInput>(&copied->Input()) : nullptr;
    Expect(paths &&
               paths->paths == std::vector<std::wstring>{L"C:\\photos\\été.arw", L"C:\\photos\\δεύτερο.cr3",
                                                         L"C:\\photos\\第三.raf"} &&
               paths->action == HostPathAction::ImagesAsLayers && paths->dropPoint == dropPoint,
           "borrowed-path-copy-preserves-unicode-order-and-drop-point-after-source-edit");
    Expect(copied && copiedCharge && budget.Used() == copiedCharge.value(),
           "borrowed-path-copy-charges-actual-owned-capacities");
    copied.reset();
    Expect(budget.Used() == 0, "borrowed-path-copy-releases-charge-on-destruction");

    HostInputBudget writeBudget;
    std::wstring output = L"D:\\exports\\résultat.png";
    auto copiedWrite = HostRequest::TryCopyWrite(HostWriteFormat::Png, true, std::wstring_view(output), writeBudget);
    output.assign(L"reused source");
    const auto* write = copiedWrite ? std::get_if<HostWriteInput>(&copiedWrite->Input()) : nullptr;
    Expect(write && write->format == HostWriteFormat::Png && write->saveAs &&
               write->path == std::optional<std::wstring>(L"D:\\exports\\résultat.png"),
           "borrowed-write-copy-owns-selected-unicode-path");
    Expect(copiedWrite && HostInputStorageBytes(copiedWrite->Input()) == writeBudget.Used(),
           "borrowed-write-copy-charges-actual-capacity");

    HostInputBudget renameBudget;
    Guid id;
    id.bytes[0] = 73;
    std::wstring name = L"名前 — couche élève";
    auto copiedRename = HostRequest::TryCopyRename(id, std::wstring_view(name), renameBudget);
    name.clear();
    const auto* rename = copiedRename ? std::get_if<HostLayerRenameInput>(&copiedRename->Input()) : nullptr;
    Expect(rename && rename->id.bytes[0] == 73 && rename->name == std::optional<std::wstring>(L"名前 — couche élève"),
           "borrowed-rename-copy-owns-unicode-name-and-target");
    Expect(copiedRename && HostInputStorageBytes(copiedRename->Input()) == renameBudget.Used(),
           "borrowed-rename-copy-charges-actual-capacity");

    HostInputBudget optionalBudget;
    auto absentPath = HostRequest::TryCopyWrite(HostWriteFormat::Project, false, std::nullopt, optionalBudget);
    auto emptyPath = HostRequest::TryCopyWrite(HostWriteFormat::Project, false, std::wstring_view{}, optionalBudget);
    auto absentName = HostRequest::TryCopyRename(Guid{}, std::nullopt, optionalBudget);
    auto emptyName = HostRequest::TryCopyRename(Guid{}, std::wstring_view{}, optionalBudget);
    const auto* absentWrite = absentPath ? std::get_if<HostWriteInput>(&absentPath->Input()) : nullptr;
    const auto* emptyWrite = emptyPath ? std::get_if<HostWriteInput>(&emptyPath->Input()) : nullptr;
    const auto* absentRename = absentName ? std::get_if<HostLayerRenameInput>(&absentName->Input()) : nullptr;
    const auto* emptyRename = emptyName ? std::get_if<HostLayerRenameInput>(&emptyName->Input()) : nullptr;
    Expect(absentWrite && !absentWrite->path && emptyWrite && emptyWrite->path && emptyWrite->path->empty() &&
               absentRename && !absentRename->name && emptyRename && emptyRename->name && emptyRename->name->empty(),
           "borrowed-copy-preserves-absent-versus-empty-optional-values");

    HostInputBudget longNameBudget;
    const std::wstring longName(HostInputBudget::kMaxPathUnits + 1, L'n');
    auto longRename = HostRequest::TryCopyRename(Guid{}, std::wstring_view(longName), longNameBudget);
    Expect(longRename && std::get<HostLayerRenameInput>(longRename->Input()).name->size() == longName.size() &&
               longNameBudget.Used() == HostInputStorageBytes(longRename->Input()),
           "borrowed-rename-copy-is-bounded-by-byte-budget-not-path-unit-limit");
}

void TestBoundedPathViews()
{
    const auto absent = HostBoundedPathView(nullptr);
    std::wstring empty;
    const auto emptyView = HostBoundedPathView(empty.c_str());
    std::wstring unicode = L"D:\\Images\\été\\第三.png";
    const auto unicodeView = HostBoundedPathView(unicode.c_str());
    Expect(!absent && emptyView && emptyView->empty() && unicodeView && unicodeView.value() == unicode,
           "bounded-path-view-preserves-null-empty-and-unicode-pointer-inputs");
    HostInputBudget copyBudget;
    const std::array<std::wstring_view, 1> borrowed{unicodeView.value()};
    auto copied = HostRequest::TryCopyPaths(borrowed, HostPathAction::OpenPaths, std::nullopt, copyBudget);
    unicode.assign(L"source reused after bounded view copy");
    Expect(copied && std::get<HostPathsInput>(copied->Input()).paths.front() == L"D:\\Images\\été\\第三.png",
           "bounded-pointer-view-feeds-owned-copy-before-source-reuse");

    const std::wstring exact(HostInputBudget::kMaxPathUnits, L'x');
    const std::wstring tooLong(HostInputBudget::kMaxPathUnits + 1, L'y');
    const auto exactView = HostBoundedPathView(exact.c_str());
    const auto tooLongView = HostBoundedPathView(tooLong.c_str());
    Expect(exactView && exactView->size() == HostInputBudget::kMaxPathUnits && exactView.value() == exact &&
               !tooLongView,
           "bounded-path-view-accepts-32767-and-rejects-32768-units");
}

void TestBorrowedInputPreflightAndFailures()
{
    HostInputBudget budget;
    std::vector<std::wstring> maximumBatch;
    maximumBatch.reserve(HostInputBudget::kMaxPaths + 1);
    maximumBatch.resize(HostInputBudget::kMaxPaths, L"x");
    std::vector<std::wstring_view> maximumViews;
    maximumViews.reserve(maximumBatch.size());
    for (const auto& path : maximumBatch)
    {
        maximumViews.emplace_back(path);
    }
    auto acceptedBatch = HostRequest::TryCopyPaths(maximumViews, HostPathAction::OpenPaths, std::nullopt, budget);
    Expect(acceptedBatch && std::get<HostPathsInput>(acceptedBatch->Input()).paths.size() == 256 && budget.Used() > 0,
           "borrowed-path-preflight-accepts-256-paths");
    acceptedBatch.reset();
    Expect(budget.Used() == 0, "maximum-borrowed-batch-releases-actual-charge");

    maximumBatch.emplace_back(L"257th path");
    maximumViews.emplace_back(maximumBatch.back());
    const std::size_t priorUsed = budget.Used();
    bool overCountThrew = false;
    std::optional<HostRequest> overCount;
    try
    {
        overCount = HostRequest::TryCopyPaths(maximumViews, HostPathAction::OpenPaths, std::nullopt, budget,
                                              HostCopyFailureForTest::Input);
    }
    catch (const std::bad_alloc&)
    {
        overCountThrew = true;
    }
    Expect(!overCount && !overCountThrew && budget.Used() == priorUsed && maximumBatch.back() == L"257th path",
           "257-path-preflight-refuses-before-copy-even-with-input-fault-enabled");

    const std::wstring exactPath(HostInputBudget::kMaxPathUnits, L'p');
    const std::wstring tooLongPath(HostInputBudget::kMaxPathUnits + 1, L'q');
    const std::array<std::wstring_view, 1> exactView{exactPath};
    const std::array<std::wstring_view, 1> tooLongView{tooLongPath};
    auto acceptedLength = HostRequest::TryCopyPaths(exactView, HostPathAction::OpenPaths, std::nullopt, budget);
    bool overLengthThrew = false;
    std::optional<HostRequest> overLength;
    try
    {
        overLength = HostRequest::TryCopyPaths(tooLongView, HostPathAction::OpenPaths, std::nullopt, budget,
                                               HostCopyFailureForTest::Input);
    }
    catch (const std::bad_alloc&)
    {
        overLengthThrew = true;
    }
    Expect(acceptedLength && std::get<HostPathsInput>(acceptedLength->Input()).paths.front().size() == 32767 &&
               !overLength && !overLengthThrew && budget.Used() == HostInputStorageBytes(acceptedLength->Input()),
           "borrowed-path-preflight-accepts-32767-and-refuses-32768-before-copy");
    acceptedLength.reset();
    Expect(budget.Used() == 0, "path-length-boundary-charge-released");

    std::vector<std::wstring> largeBatch(33, std::wstring(HostInputBudget::kMaxPathUnits, L'a'));
    std::vector<std::wstring_view> largeViews;
    largeViews.reserve(largeBatch.size());
    for (const auto& path : largeBatch)
    {
        largeViews.emplace_back(path);
    }
    bool aggregateThrew = false;
    std::optional<HostRequest> aggregate;
    try
    {
        aggregate = HostRequest::TryCopyPaths(largeViews, HostPathAction::OpenPaths, std::nullopt, budget,
                                              HostCopyFailureForTest::Input);
    }
    catch (const std::bad_alloc&)
    {
        aggregateThrew = true;
    }
    Expect(!aggregate && !aggregateThrew && budget.Used() == 0 && largeBatch.front().size() == 32767,
           "aggregate-over-budget-preflight-refuses-before-copy-even-with-input-fault-enabled");

    auto fullBudget = budget.TryReserve(HostInputBudget::kStorageLimit);
    const std::array<std::wstring_view, 1> smallView{L"still borrowed"};
    bool quotaThrew = false;
    std::optional<HostRequest> quotaRefused;
    try
    {
        quotaRefused = HostRequest::TryCopyPaths(smallView, HostPathAction::OpenPaths, std::nullopt, budget,
                                                 HostCopyFailureForTest::Input);
    }
    catch (const std::bad_alloc&)
    {
        quotaThrew = true;
    }
    Expect(fullBudget && !quotaRefused && !quotaThrew && budget.Used() == HostInputBudget::kStorageLimit,
           "quota-refusal-precedes-copy-and-does-not-trigger-input-fault");
    fullBudget.reset();

    HostInputBudget failureBudget;
    auto preceding = failureBudget.TryReserve(128);
    const std::array<std::wstring, 2> faultSources{L"first path remains", L"second path remains"};
    const std::array<std::wstring_view, 2> faultViews{faultSources[0], faultSources[1]};
    bool inputFaultThrown = false;
    try
    {
        auto failed = HostRequest::TryCopyPaths(faultViews, HostPathAction::OpenPaths, std::nullopt, failureBudget,
                                                HostCopyFailureForTest::Input);
        (void)failed;
    }
    catch (const std::bad_alloc&)
    {
        inputFaultThrown = true;
    }
    Expect(preceding && inputFaultThrown && failureBudget.Used() == 128 && faultSources[0] == L"first path remains" &&
               faultSources[1] == L"second path remains",
           "partial-path-copy-fault-releases-new-charge-and-preserves-borrowed-sources");

    bool receiptFaultThrown = false;
    try
    {
        auto failed = HostRequest::TryCopyPaths(faultViews, HostPathAction::OpenPaths, std::nullopt, failureBudget,
                                                HostCopyFailureForTest::Receipt);
        (void)failed;
    }
    catch (const std::bad_alloc&)
    {
        receiptFaultThrown = true;
    }
    Expect(receiptFaultThrown && failureBudget.Used() == 128 && faultSources[0] == L"first path remains" &&
               faultSources[1] == L"second path remains",
           "receipt-copy-fault-releases-owned-input-charge-and-preserves-borrowed-sources");

    const std::wstring borrowedWrite = L"C:\\exports\\selected.prism";
    bool writeInputFaultThrown = false;
    try
    {
        auto failed = HostRequest::TryCopyWrite(HostWriteFormat::Project, true, std::wstring_view(borrowedWrite),
                                                failureBudget, HostCopyFailureForTest::Input);
        (void)failed;
    }
    catch (const std::bad_alloc&)
    {
        writeInputFaultThrown = true;
    }
    Expect(writeInputFaultThrown && failureBudget.Used() == 128 && borrowedWrite == L"C:\\exports\\selected.prism",
           "scalar-string-copy-fault-releases-charge-and-preserves-borrowed-write-path");

    std::wstring borrowedName = L"borrowed name";
    bool renameReceiptFaultThrown = false;
    try
    {
        auto failed = HostRequest::TryCopyRename(Guid{}, std::wstring_view(borrowedName), failureBudget,
                                                 HostCopyFailureForTest::Receipt);
        (void)failed;
    }
    catch (const std::bad_alloc&)
    {
        renameReceiptFaultThrown = true;
    }
    Expect(renameReceiptFaultThrown && failureBudget.Used() == 128 && borrowedName == L"borrowed name",
           "rename-receipt-fault-releases-charge-and-preserves-borrowed-name");
}

void TestSelectedOutputLifetimeAndReplacement()
{
    HostInputBudget budget;
    auto created = HostRequest::TryCopyWrite(HostWriteFormat::Project, false, std::nullopt, budget);
    if (!created)
    {
        Expect(false, "budgeted-write-request-created-for-output-selection");
        return;
    }
    HostRequest request = std::move(created.value());
    const std::size_t inputCharge = budget.Used();
    std::wstring source = L"C:\\exports\\été.prism";
    Expect(request.TrySelectOutput(source, budget) &&
               request.SelectedOutputPath() == std::optional<std::wstring_view>(source),
           "pending-budgeted-request-selects-and-owns-output-path");
    const std::size_t selectedCharge = budget.Used();
    source.assign(L"caller changed output");
    Expect(request.SelectedOutputPath() == std::optional<std::wstring_view>(L"C:\\exports\\été.prism") &&
               selectedCharge > inputCharge,
           "selected-output-is-independent-of-later-caller-mutation");

    HostInputBudget otherBudget;
    const std::wstring alternate = L"C:\\exports\\alternate.prism";
    Expect(!request.TrySelectOutput(alternate, otherBudget) && budget.Used() == selectedCharge &&
               request.SelectedOutputPath() == std::optional<std::wstring_view>(L"C:\\exports\\été.prism"),
           "output-selection-rejects-a-different-budget-and-retains-old-path");

    auto filler = budget.TryReserve(HostInputBudget::kStorageLimit - selectedCharge);
    const std::wstring refusedReplacement = L"C:\\exports\\refused.prism";
    Expect(filler && !request.TrySelectOutput(refusedReplacement, budget) &&
               budget.Used() == HostInputBudget::kStorageLimit &&
               request.SelectedOutputPath() == std::optional<std::wstring_view>(L"C:\\exports\\été.prism"),
           "output-replacement-quota-refusal-preserves-old-path-and-charge");
    filler.reset();

    bool outputFaultThrown = false;
    try
    {
        (void)request.TrySelectOutput(alternate, budget, true);
    }
    catch (const std::bad_alloc&)
    {
        outputFaultThrown = true;
    }
    Expect(outputFaultThrown && budget.Used() == selectedCharge &&
               request.SelectedOutputPath() == std::optional<std::wstring_view>(L"C:\\exports\\été.prism"),
           "output-copy-allocation-fault-preserves-old-path-and-charge");

    const std::wstring oversizedOutput(HostInputBudget::kMaxPathUnits + 1, L'p');
    bool oversizedOutputThrew = false;
    bool oversizedOutputSelected = false;
    try
    {
        oversizedOutputSelected = request.TrySelectOutput(oversizedOutput, budget, true);
    }
    catch (const std::bad_alloc&)
    {
        oversizedOutputThrew = true;
    }
    Expect(!oversizedOutputSelected && !oversizedOutputThrew && budget.Used() == selectedCharge &&
               request.SelectedOutputPath() == std::optional<std::wstring_view>(L"C:\\exports\\été.prism"),
           "oversized-output-refuses-before-copy-and-keeps-original-output-charge");

    HostInputBudget probeBudget;
    auto probe = HostRequest::TryCopyWrite(HostWriteFormat::Project, false, std::nullopt, probeBudget);
    const std::wstring oldOutput(4096, L'o');
    const std::wstring newOutput(6000, L'n');
    const bool probeReady = probe && probe->TrySelectOutput(oldOutput, probeBudget);
    const std::size_t oldOutputCharge =
        probeReady ? probeBudget.Used() - HostInputStorageBytes(probe->Input()).value() : 0;
    auto newProbe = HostRequest::TryCopyWrite(HostWriteFormat::Project, false, std::nullopt, probeBudget);
    const bool newProbeReady = probeReady && newProbe && newProbe->TrySelectOutput(newOutput, probeBudget);
    const std::size_t newOutputCharge = newProbeReady
                                            ? probeBudget.Used() - HostInputStorageBytes(probe->Input()).value() -
                                                  oldOutputCharge - HostInputStorageBytes(newProbe->Input()).value()
                                            : 0;
    HostInputBudget peakBudget;
    auto peakCreated = HostRequest::TryCopyWrite(HostWriteFormat::Project, false, std::nullopt, peakBudget);
    bool peakRefused = false;
    std::size_t peakCharge = 0;
    std::size_t peakAfterReplacementCharge = 0;
    bool replacementFitsOnlyAfterOldRelease = false;
    std::size_t priorPeakCharge = 0;
    if (peakCreated && oldOutputCharge > 0 && newOutputCharge > oldOutputCharge &&
        newOutputCharge - oldOutputCharge < HostInputBudget::kStorageLimit)
    {
        HostRequest peakRequest = std::move(peakCreated.value());
        const std::size_t peakInputCharge = peakBudget.Used();
        const bool selectedOld = peakRequest.TrySelectOutput(oldOutput, peakBudget);
        priorPeakCharge = peakBudget.Used();
        const std::size_t freeDuringReplacement = newOutputCharge - oldOutputCharge;
        auto peakFiller =
            peakBudget.TryReserve(HostInputBudget::kStorageLimit - priorPeakCharge - freeDuringReplacement);
        peakCharge = peakBudget.Used();
        replacementFitsOnlyAfterOldRelease =
            HostInputBudget::kStorageLimit - peakCharge < newOutputCharge &&
            HostInputBudget::kStorageLimit - peakCharge + oldOutputCharge >= newOutputCharge;
        peakRefused = selectedOld && peakFiller && peakInputCharge + oldOutputCharge == priorPeakCharge &&
                      replacementFitsOnlyAfterOldRelease && !peakRequest.TrySelectOutput(newOutput, peakBudget);
        peakAfterReplacementCharge = peakBudget.Used();
        peakRefused = peakRefused && peakAfterReplacementCharge == peakCharge &&
                      peakRequest.SelectedOutputPath() == std::optional<std::wstring_view>(oldOutput);
    }
    Expect(probeReady && newProbeReady && newOutputCharge > oldOutputCharge && peakRefused &&
               peakAfterReplacementCharge == peakCharge && replacementFitsOnlyAfterOldRelease,
           "output-replacement-reserves-old-and-new-at-peak-before-releasing-old");

    HostRequestInput unbudgetedInput = HostWriteInput{HostWriteFormat::Project, false, std::nullopt};
    HostRequest unbudgeted = HostRequest::Create(std::move(unbudgetedInput));
    HostInputBudget unbudgetedBudget;
    const auto unbudgetedReceipt = unbudgeted.Receipt();
    Expect(!unbudgeted.TrySelectOutput(alternate, unbudgetedBudget) && !unbudgeted.SelectedOutputPath() &&
               unbudgetedReceipt.Read().outcome == ContinuationOutcome::Pending,
           "pending-bare-model-without-output-cannot-select-budgeted-output");

    HostInputBudget replacementReferenceBudget;
    auto replacementReference =
        HostRequest::TryCopyWrite(HostWriteFormat::Project, false, std::nullopt, replacementReferenceBudget);
    const bool referenceSelected =
        replacementReference && replacementReference.value().TrySelectOutput(alternate, replacementReferenceBudget);
    Expect(referenceSelected && request.TrySelectOutput(alternate, budget) &&
               request.SelectedOutputPath() == std::optional<std::wstring_view>(alternate) &&
               budget.Used() == replacementReferenceBudget.Used(),
           "successful-output-replacement-releases-old-charge-and-matches-fresh-owner");
    Expect(request.Finish(ContinuationOutcome::Succeeded) && !request.TrySelectOutput(alternate, budget) &&
               request.SelectedOutputPath() == std::optional<std::wstring_view>(alternate) &&
               budget.Used() == replacementReferenceBudget.Used(),
           "terminal-request-cannot-replace-selected-output");

    request = std::move(unbudgeted);
    Expect(budget.Used() == 0 && request.SelectedOutputPath() == std::nullopt,
           "move-overwrite-releases-input-and-output-reservations");

    HostInputBudget moveBudget;
    auto oldCreated = HostRequest::TryCopyWrite(HostWriteFormat::Project, false, std::nullopt, moveBudget);
    auto newCreated = HostRequest::TryCopyWrite(HostWriteFormat::Jpeg, true, std::nullopt, moveBudget);
    if (!oldCreated || !newCreated)
    {
        Expect(false, "budgeted-move-overwrite-requests-created");
        return;
    }
    HostRequest oldRequest = std::move(oldCreated.value());
    HostRequest newRequest = std::move(newCreated.value());
    const auto oldReceipt = oldRequest.Receipt();
    const auto newReceipt = newRequest.Receipt();
    const bool oldSelected = oldRequest.TrySelectOutput(oldOutput, moveBudget);
    const bool newSelected = newRequest.TrySelectOutput(newOutput, moveBudget);
    const std::size_t newRequestCharge = HostInputStorageBytes(newRequest.Input()).value() + newOutputCharge;
    oldRequest = std::move(newRequest);
    Expect(oldSelected && newSelected && oldReceipt.Read().outcome == ContinuationOutcome::Cancelled &&
               newReceipt.Read().outcome == ContinuationOutcome::Pending && moveBudget.Used() == newRequestCharge &&
               oldRequest.SelectedOutputPath() == std::optional<std::wstring_view>(newOutput),
           "move-overwrite-cancels-observer-before-transferring-input-and-output-charges");
    Expect(oldRequest.Finish(ContinuationOutcome::Succeeded), "moved-budgeted-output-publishes-success");
    oldRequest = HostRequest::Create(HostRequestInput{HostNewInput{}});
    Expect(moveBudget.Used() == 0 && newReceipt.Read().outcome == ContinuationOutcome::Succeeded,
           "move-overwrite-destroys-transferred-budgeted-output-after-publication");
}

void TestSelectedOutputQueueLifetime()
{
    HostInputBudget budget;
    Queue queue;
    auto created = HostRequest::TryCopyWrite(HostWriteFormat::Jpeg, true, std::nullopt, budget);
    if (!created)
    {
        Expect(false, "budgeted-export-request-created");
        return;
    }
    HostRequest request = std::move(created.value());
    const std::wstring path = L"C:\\exports\\final.jpg";
    if (!request.TrySelectOutput(path, budget))
    {
        Expect(false, "export-output-selected-before-queue");
        return;
    }
    const auto receipt = request.Receipt();
    const std::size_t charge = budget.Used();
    Expect(queue.Enqueue(211, 1, ContinuationKind::Command, request, Retire) == ContinuationAdmission::Queued &&
               budget.Used() == charge,
           "queued-selected-output-retains-combined-charge");
    auto taken = queue.TakeReady(211, false, Retire);
    const auto selected = taken.ready ? taken.ready->input.SelectedOutputPath() : std::nullopt;
    const bool active = taken.ready && selected == std::optional<std::wstring_view>(path) && budget.Used() == charge &&
                        receipt.Read().outcome == ContinuationOutcome::Pending;
    const bool finished = taken.ready && taken.ready->input.Finish(ContinuationOutcome::Failed, -19) &&
                          budget.Used() == charge &&
                          receipt.Read() == ContinuationReceipt::Result{ContinuationOutcome::Failed, -19};
    taken.ready.reset();
    Expect(active && finished && budget.Used() == 0,
           "active-output-charge-survives-publication-until-request-destruction");
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
    TestBorrowedInputCopies();
    TestBoundedPathViews();
    TestBorrowedInputPreflightAndFailures();
    TestSelectedOutputLifetimeAndReplacement();
    TestSelectedOutputQueueLifetime();
    return gFailures;
}
