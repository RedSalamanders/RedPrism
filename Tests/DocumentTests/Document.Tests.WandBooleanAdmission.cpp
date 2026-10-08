#include "Document.Tests.WandBooleanAdmission.h"

#include "WandLayerSelectionWork.h"

#include <cstdint>
#include <cstdio>
#include <limits>

namespace
{
int gFailures = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s wand-boolean-admission.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

WandLayerSelectionMemoryPlan CompletePlan() noexcept
{
    WandLayerSelectionMemoryPlan plan;
    plan.captureHistoryBytes = 1'024;
    plan.preparationBytes = 4'096;
    plan.retirementBytes = 2'048;
    plan.reservationBytes = 6'144;
    return plan;
}

void AdmissionModes()
{
    const auto plan = CompletePlan();
    std::uint64_t required = 99;
    Expect(EvaluateWandLayerSelectionAdmission(plan, SelectionMode::New, false, required) ==
                   WandLayerSelectionAdmission::Ready &&
               required == 6'144,
           "mode.new-retains-complete-plan-reservation");

    required = 99;
    Expect(EvaluateWandLayerSelectionAdmission(plan, SelectionMode::Add, false, required) ==
                   WandLayerSelectionAdmission::UnknownClipperWorkspace &&
               required == 0,
           "mode.add-refuses-before-capture-even-without-retained-selection");

    required = 99;
    Expect(EvaluateWandLayerSelectionAdmission(plan, SelectionMode::Add, true, required) ==
                   WandLayerSelectionAdmission::UnknownClipperWorkspace &&
               required == 0,
           "mode.add-refuses-boolean-against-retained-selection");

    required = 99;
    Expect(EvaluateWandLayerSelectionAdmission(plan, SelectionMode::Subtract, false, required) ==
                   WandLayerSelectionAdmission::Ready &&
               required == 6'144,
           "mode.subtract-without-selection-is-trace-only");

    required = 99;
    Expect(EvaluateWandLayerSelectionAdmission(plan, SelectionMode::Subtract, true, required) ==
                   WandLayerSelectionAdmission::UnknownClipperWorkspace &&
               required == 0,
           "mode.subtract-refuses-boolean-against-retained-selection");
}

void CheckedEnvelopeAndHistoryRefusal()
{
    constexpr auto maximum = (std::numeric_limits<std::uint64_t>::max)();
    std::uint64_t sum = 0;
    Expect(TryAddWandLayerSelectionBytes(4'096, 2'048, sum) && sum == 6'144,
           "envelope.literal-preparation-plus-retirement");
    Expect(!TryAddWandLayerSelectionBytes(maximum - 31, 32, sum), "envelope.overflow-is-refused");
    Expect(TryAddWandLayerSelectionBytes(maximum - 31, 31, sum) && sum == maximum,
           "envelope.exact-uint64-boundary-is-representable");

    auto plan = CompletePlan();
    plan.captureHistoryBytes = 0;
    std::uint64_t required = 99;
    Expect(EvaluateWandLayerSelectionAdmission(plan, SelectionMode::New, false, required) ==
                   WandLayerSelectionAdmission::IncompletePlan &&
               required == 0,
           "history.missing-capture-bound-refuses");

    plan = CompletePlan();
    plan.reservationBytes += 1;
    Expect(EvaluateWandLayerSelectionAdmission(plan, SelectionMode::New, false, required) ==
               WandLayerSelectionAdmission::IncompletePlan,
           "history-incomplete-reservation-refuses");

    plan = CompletePlan();
    plan.preparationBytes = maximum - 1;
    plan.retirementBytes = 2;
    plan.reservationBytes = maximum;
    Expect(EvaluateWandLayerSelectionAdmission(plan, SelectionMode::New, false, required) ==
                   WandLayerSelectionAdmission::Overflow &&
               required == 0,
           "history-overflow-before-capture-refuses");

    plan = CompletePlan();
    Expect(EvaluateWandLayerSelectionAdmission(plan, static_cast<SelectionMode>(255), false, required) ==
               WandLayerSelectionAdmission::InvalidMode,
           "mode-invalid-enum-refuses");
}
} // namespace

int RunWandBooleanAdmissionTests() noexcept
{
    gFailures = 0;
    AdmissionModes();
    CheckedEnvelopeAndHistoryRefusal();
    std::printf("wand-boolean-admission.summary failures=%d\n", gFailures);
    return gFailures;
}
