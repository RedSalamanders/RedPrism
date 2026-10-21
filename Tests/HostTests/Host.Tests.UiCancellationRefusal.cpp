#include "Host.Tests.UiCancellationRefusal.h"

#include "../../RedPrism/UiCancellationRefusal.h"

#include <array>
#include <cstdio>

bool RunUiCancellationRefusalTests() noexcept
{
    unsigned int checks = 0;
    unsigned int failures = 0;
    struct Profile final
    {
        int width;
        int height;
        const char* name;
        UiCancellationRefusalKind expectedBlur;
    };
    constexpr std::array profiles{
        Profile{64, 48, "smoke", UiCancellationRefusalKind::None},
        Profile{3840, 2160, "4k", UiCancellationRefusalKind::None},
        Profile{8192, 5464, "45mp", UiCancellationRefusalKind::SettingsPixelBudget},
        Profile{10000, 10000, "100mp", UiCancellationRefusalKind::OpeningPixelBudget},
    };
    constexpr std::array names{"levels", "hsv",         "exposure",   "curves", "gradient-map",
                               "grain",  "gaussian250", "motion2000", "noise",  "lens"};
    for (const auto& profile : profiles)
    {
        for (std::size_t index = 0; index < names.size(); ++index)
        {
            const auto expected = index == 6 || index == 7 ? profile.expectedBlur : UiCancellationRefusalKind::None;
            ++checks;
            const bool value = ExpectedUiCancellationRefusal(profile.width, profile.height, index) == expected;
            failures += value ? 0u : 1u;
            std::printf("%s ui-cancellation-refusal.profile.%s.%s\n", value ? "PASS" : "FAIL", profile.name,
                        names[index]);
        }
    }
    struct Refusal final
    {
        UiCancellationRefusalKind kind;
        const char* name;
        bool pending;
    };
    constexpr std::array refusals{
        Refusal{UiCancellationRefusalKind::OpeningPixelBudget, "opening", false},
        Refusal{UiCancellationRefusalKind::SettingsPixelBudget, "settings", true},
    };
    for (const auto& refusal : refusals)
    {
        UiCancellationRefusalFacts facts{refusal.kind,   1, true, true, true, false, false, refusal.pending,
                                         refusal.pending};
        const auto check = [&](bool value, const char* name) noexcept
        {
            ++checks;
            failures += value ? 0u : 1u;
            std::printf("%s ui-cancellation-refusal.%s.%s\n", value ? "PASS" : "FAIL", refusal.name, name);
        };
        check(IsUiCancellationBudgetRefusal(facts), "original-notice-and-unchanged-settled-source");
        auto changed = facts;
        changed.ownersSettled = false;
        check(!IsUiCancellationBudgetRefusal(changed), "unjoined-owner-refused");
        changed = facts;
        changed.sourceUnchanged = false;
        check(!IsUiCancellationBudgetRefusal(changed), "changed-source-refused");
        changed = facts;
        changed.historyUnchanged = false;
        check(!IsUiCancellationBudgetRefusal(changed), "changed-history-refused");
        changed = facts;
        changed.hasProcessingOwner = true;
        check(!IsUiCancellationBudgetRefusal(changed), "admitted-processing-refused");
        changed = facts;
        changed.nativeTimer = true;
        check(!IsUiCancellationBudgetRefusal(changed), "armed-timer-refused");
        changed = facts;
        changed.reportedBudgetNotices = 0;
        check(!IsUiCancellationBudgetRefusal(changed), "missing-budget-notice-refused");
        changed = facts;
        changed.reportedBudgetNotices = 2;
        check(!IsUiCancellationBudgetRefusal(changed), "multiple-budget-notices-refused");
        changed = facts;
        changed.kind = UiCancellationRefusalKind::None;
        check(!IsUiCancellationBudgetRefusal(changed), "undeclared-refusal-refused");
        changed = facts;
        changed.kind = static_cast<UiCancellationRefusalKind>(255);
        check(!IsUiCancellationBudgetRefusal(changed), "unknown-kind-refused");
        changed = facts;
        changed.pendingEdit = !changed.pendingEdit;
        check(!IsUiCancellationBudgetRefusal(changed), "wrong-panel-state-refused");
        changed = facts;
        changed.requestedGridRefused = !changed.requestedGridRefused;
        check(!IsUiCancellationBudgetRefusal(changed), "wrong-grid-state-refused");
    }
    constexpr unsigned int kExpectedChecks = 64;
    std::printf("%s ui-cancellation-refusal.summary controls=%u failed=%u\n",
                failures == 0 && checks == kExpectedChecks ? "PASS" : "FAIL", checks, failures);
    std::fflush(stdout);
    return failures == 0 && checks == kExpectedChecks;
}
