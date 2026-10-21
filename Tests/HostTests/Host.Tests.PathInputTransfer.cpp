#include "Host.Tests.PathInputTransfer.h"

#include "../../RedPrism/ContinuationRequests.h"

#include <array>
#include <cstddef>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace
{
constexpr std::array<std::string_view, 26> kExpectedCases{
    "budgeted-path-request-created",
    "path-request-has-three-values",
    "reserved-capacity-destination-refused-before-take",
    "take-keeps-vector-and-string-storage-addresses",
    "take-preserves-ordered-paths-action-and-drop-point",
    "take-retains-original-reservation-and-pending-receipt",
    "repeat-take-and-occupied-destination-refuse-without-mutation",
    "restore-swaps-payload-back-to-empty-source",
    "restore-retains-original-vector-and-string-allocations",
    "restore-without-detach-refuses-without-mutation",
    "original-receipt-observes-completion-after-path-restore",
    "wrong-variant-refuses-both-transfers",
    "terminal-receipt-refuses-take-and-preserves-literal-paths",
    "terminal-receipt-refuses-restore-and-preserves-payload",
    "empty-batch-detaches-with-zero-byte-fallback-reservation",
    "empty-batch-detached-flag-prevents-repeat-and-allows-restore",
    "move-construction-request-created",
    "move-construction-transfers-detached-state-receipt-and-charge",
    "move-assignment-old-request-created",
    "move-assignment-retires-old-then-transfers-detached-state-and-charge",
    "cleanup-request-created",
    "detached-payload-does-not-take-owner-reservation",
    "destroyed-detached-payload-does-not-release-owner-charge",
    "success-publication-keeps-charge-until-request-owner-destruction",
    "owner-destruction-returns-charge-after-detached-payload-lifetime",
    "all-registered-cases-ran",
};
std::size_t gCheckCount = 0;

void Expect(bool condition, const char* name, int& failures) noexcept
{
    const bool inOrder = gCheckCount < kExpectedCases.size() && kExpectedCases[gCheckCount] == name;
    ++gCheckCount;
    if (!inOrder)
    {
        const std::string_view expected =
            gCheckCount <= kExpectedCases.size() ? kExpectedCases[gCheckCount - 1] : std::string_view("<end>");
        std::printf("FAIL path-input-transfer.assertion-order expected=%.*s actual=%s\n",
                    static_cast<int>(expected.size()), expected.data(), name);
    }
    condition = condition && inOrder;
    std::printf("%s path-input-transfer.%s\n", condition ? "PASS" : "FAIL", name);
    if (!condition)
    {
        ++failures;
    }
}

using Paths = std::vector<std::wstring>;

void TestDetachRestorePreservesBatchAndStorage(int& failures)
{
    HostInputBudget budget;
    HostRequestInput input =
        HostPathsInput{{L"C:\\photos\\été-01-nef-long-name.raw", L"", L"D:\\archive\\three-ordered-image-files.tiff"},
                       HostPathAction::ImagesAsProject,
                       Vec2{31.25, -8.5}};
    auto created = HostRequest::TryCreate(input, budget);
    Expect(created.has_value(), "budgeted-path-request-created", failures);
    if (!created)
    {
        return;
    }
    HostRequest request = std::move(created.value());
    const auto* const owned = std::get_if<HostPathsInput>(&request.Input());
    Expect(owned && owned->paths.size() == 3, "path-request-has-three-values", failures);
    if (!owned || owned->paths.size() != 3)
    {
        return;
    }
    const auto receipt = request.Receipt();
    const std::size_t charge = budget.Used();
    const std::size_t reserved = request.ReservedInputBytes();
    const auto* const vectorStorage = owned->paths.data();
    const auto* const firstText = owned->paths[0].data();
    const auto* const lastText = owned->paths[2].data();
    Paths payload;
    Paths reservedDestination;
    reservedDestination.reserve(4);
    const auto* const reservedStorage = reservedDestination.data();
    Expect(reservedDestination.empty() && reservedDestination.capacity() > 0 &&
               !request.TryTakePaths(reservedDestination) && reservedDestination.empty() &&
               reservedDestination.data() == reservedStorage && request.ReservedInputBytes() == reserved &&
               budget.Used() == charge && receipt.Read().outcome == ContinuationOutcome::Pending,
           "reserved-capacity-destination-refused-before-take", failures);
    const bool detached = request.TryTakePaths(payload);
    const auto* const moved = std::get_if<HostPathsInput>(&request.Input());
    Expect(detached && moved && moved->paths.empty() && moved->paths.capacity() == 0 && payload.size() == 3 &&
               payload.data() == vectorStorage && payload[0].data() == firstText && payload[2].data() == lastText,
           "take-keeps-vector-and-string-storage-addresses", failures);
    Expect(payload == Paths{L"C:\\photos\\été-01-nef-long-name.raw", L"",
                            L"D:\\archive\\three-ordered-image-files.tiff"} &&
               moved && moved->action == HostPathAction::ImagesAsProject && moved->dropPoint == Vec2{31.25, -8.5},
           "take-preserves-ordered-paths-action-and-drop-point", failures);
    Expect(request.ReservedInputBytes() == reserved && budget.Used() == charge &&
               receipt.Read().outcome == ContinuationOutcome::Pending,
           "take-retains-original-reservation-and-pending-receipt", failures);

    Paths occupied;
    occupied.reserve(1);
    occupied.emplace_back(L"keep-capacity-and-value");
    const auto* const occupiedStorage = occupied.data();
    Paths secondPayload;
    Expect(!request.TryTakePaths(occupied) && occupied.data() == occupiedStorage &&
               occupied == Paths{L"keep-capacity-and-value"} && !request.TryTakePaths(secondPayload) &&
               secondPayload.empty() && secondPayload.capacity() == 0,
           "repeat-take-and-occupied-destination-refuse-without-mutation", failures);
    Expect(request.TryRestorePaths(payload) && payload.empty() && payload.capacity() == 0,
           "restore-swaps-payload-back-to-empty-source", failures);
    const auto* const restored = std::get_if<HostPathsInput>(&request.Input());
    Expect(restored && restored->paths.data() == vectorStorage && restored->paths[0].data() == firstText &&
               restored->paths[2].data() == lastText &&
               restored->paths ==
                   Paths{L"C:\\photos\\été-01-nef-long-name.raw", L"", L"D:\\archive\\three-ordered-image-files.tiff"},
           "restore-retains-original-vector-and-string-allocations", failures);
    Paths replacement{L"replacement"};
    const auto* const replacementStorage = replacement.data();
    Expect(!request.TryRestorePaths(replacement) && replacement.data() == replacementStorage &&
               replacement == Paths{L"replacement"},
           "restore-without-detach-refuses-without-mutation", failures);
    Expect(request.Finish(ContinuationOutcome::Succeeded) && receipt.Read().outcome == ContinuationOutcome::Succeeded,
           "original-receipt-observes-completion-after-path-restore", failures);
}

void TestRefusalsAndEmptyBatch(int& failures)
{
    HostRequestInput wrongInput = HostPanelOkInput{4};
    HostRequest wrong = HostRequest::Create(std::move(wrongInput));
    Paths empty;
    Expect(!wrong.TryTakePaths(empty) && !wrong.TryRestorePaths(empty), "wrong-variant-refuses-both-transfers",
           failures);

    HostRequestInput terminalInput = HostPathsInput{{L"terminal-path"}, HostPathAction::OpenPaths, std::nullopt};
    HostRequest terminal = HostRequest::Create(std::move(terminalInput));
    const auto terminalReceipt = terminal.Receipt();
    const auto* const terminalLiteral = std::get_if<HostPathsInput>(&terminal.Input());
    Paths terminalPayload;
    Expect(terminal.Finish(ContinuationOutcome::Succeeded) && !terminal.TryTakePaths(terminalPayload) &&
               terminalLiteral && terminalLiteral->paths == Paths{L"terminal-path"} && terminalPayload.empty() &&
               terminalPayload.capacity() == 0 && terminalReceipt.Read().outcome == ContinuationOutcome::Succeeded,
           "terminal-receipt-refuses-take-and-preserves-literal-paths", failures);

    HostRequestInput restoreTerminalInput =
        HostPathsInput{{L"terminal-restore-path"}, HostPathAction::OpenPaths, std::nullopt};
    HostRequest restoreTerminal = HostRequest::Create(std::move(restoreTerminalInput));
    Paths terminalRestorePayload;
    const bool detachedForTerminalRestore = restoreTerminal.TryTakePaths(terminalRestorePayload);
    Expect(detachedForTerminalRestore && restoreTerminal.Finish(ContinuationOutcome::Succeeded) &&
               !restoreTerminal.TryRestorePaths(terminalRestorePayload) &&
               terminalRestorePayload == Paths{L"terminal-restore-path"},
           "terminal-receipt-refuses-restore-and-preserves-payload", failures);

    HostRequestInput emptyInput = HostPathsInput{{}, HostPathAction::ImportImages, Vec2{0.5, 2.0}};
    HostRequest emptyRequest = HostRequest::Create(std::move(emptyInput));
    const auto receipt = emptyRequest.Receipt();
    Expect(emptyRequest.ReservedInputBytes() == 0 && emptyRequest.TryTakePaths(empty) && empty.empty() &&
               emptyRequest.Receipt().Read().outcome == ContinuationOutcome::Pending,
           "empty-batch-detaches-with-zero-byte-fallback-reservation", failures);
    Expect(!emptyRequest.TryTakePaths(empty) && emptyRequest.TryRestorePaths(empty) &&
               emptyRequest.ReservedInputBytes() == 0 && receipt.Read().outcome == ContinuationOutcome::Pending,
           "empty-batch-detached-flag-prevents-repeat-and-allows-restore", failures);
}

void TestMoveConstructionAndAssignmentWhileDetached(int& failures)
{
    HostInputBudget budget;
    HostRequestInput input =
        HostPathsInput{{L"move-construction-path-with-storage"}, HostPathAction::OpenPaths, std::nullopt};
    auto created = HostRequest::TryCreate(input, budget);
    Expect(created.has_value(), "move-construction-request-created", failures);
    if (!created)
    {
        return;
    }
    HostRequest source = std::move(created.value());
    Paths payload;
    const auto receipt = source.Receipt();
    const std::size_t charge = budget.Used();
    const std::size_t reserved = source.ReservedInputBytes();
    const bool detached = source.TryTakePaths(payload);
    HostRequest moved(std::move(source));
    Expect(detached && moved.TryRestorePaths(payload) && payload.empty() &&
               moved.Receipt().Read().outcome == ContinuationOutcome::Pending &&
               receipt.Read().outcome == ContinuationOutcome::Pending && moved.ReservedInputBytes() == reserved &&
               budget.Used() == charge,
           "move-construction-transfers-detached-state-receipt-and-charge", failures);

    HostRequestInput oldInput = HostPathsInput{{L"old"}, HostPathAction::OpenPaths, std::nullopt};
    auto oldCreated = HostRequest::TryCreate(oldInput, budget);
    Expect(oldCreated.has_value(), "move-assignment-old-request-created", failures);
    if (!oldCreated)
    {
        return;
    }
    HostRequest target = std::move(oldCreated.value());
    const auto oldReceipt = target.Receipt();
    const bool detachedAgain = moved.TryTakePaths(payload);
    const auto movedReceipt = moved.Receipt();
    const std::size_t transferredCharge = moved.ReservedInputBytes();
    target = std::move(moved);
    Expect(detachedAgain && oldReceipt.Read().outcome == ContinuationOutcome::Cancelled &&
               target.TryRestorePaths(payload) && target.Receipt().Read().outcome == ContinuationOutcome::Pending &&
               movedReceipt.Read().outcome == ContinuationOutcome::Pending &&
               target.ReservedInputBytes() == transferredCharge && budget.Used() == charge,
           "move-assignment-retires-old-then-transfers-detached-state-and-charge", failures);
}

void TestDetachedPayloadScopeKeepsOwnerCharge(int& failures)
{
    HostInputBudget budget;
    HostRequestInput input =
        HostPathsInput{{L"owner-charge-outlives-detached-vector"}, HostPathAction::OpenPaths, std::nullopt};
    auto created = HostRequest::TryCreate(input, budget);
    Expect(created.has_value(), "cleanup-request-created", failures);
    if (!created)
    {
        return;
    }
    HostRequest request = std::move(created.value());
    const auto receipt = request.Receipt();
    const std::size_t charge = budget.Used();
    {
        Paths detachedPayload;
        const bool detached = request.TryTakePaths(detachedPayload);
        Expect(detached && !detachedPayload.empty() && budget.Used() == charge &&
                   request.ReservedInputBytes() == charge && receipt.Read().outcome == ContinuationOutcome::Pending,
               "detached-payload-does-not-take-owner-reservation", failures);
    }
    Expect(budget.Used() == charge && receipt.Read().outcome == ContinuationOutcome::Pending,
           "destroyed-detached-payload-does-not-release-owner-charge", failures);
    Expect(request.Finish(ContinuationOutcome::Succeeded) && budget.Used() == charge,
           "success-publication-keeps-charge-until-request-owner-destruction", failures);
    {
        HostRequest released = std::move(request);
    }
    Expect(budget.Used() == 0 && receipt.Read().outcome == ContinuationOutcome::Succeeded,
           "owner-destruction-returns-charge-after-detached-payload-lifetime", failures);
}
} // namespace

int RunPathInputTransferTests()
{
    gCheckCount = 0;
    int failures = 0;
    TestDetachRestorePreservesBatchAndStorage(failures);
    TestRefusalsAndEmptyBatch(failures);
    TestMoveConstructionAndAssignmentWhileDetached(failures);
    TestDetachedPayloadScopeKeepsOwnerCharge(failures);
    Expect(gCheckCount + 1 == kExpectedCases.size(), "all-registered-cases-ran", failures);
    if (gCheckCount != kExpectedCases.size())
    {
        std::printf("FAIL path-input-transfer.assertion-count expected=%zu actual=%zu\n", kExpectedCases.size(),
                    gCheckCount);
        ++failures;
    }
    std::printf("path-input-transfer: %zu checks, %d failures\n", gCheckCount, failures);
    return failures;
}
