#pragma once

#include <cstddef>
#include <cstdint>

enum class UiCancellationRefusalKind : std::uint8_t
{
    None,
    OpeningPixelBudget,
    SettingsPixelBudget,
};

// These are the declared P1 benchmark contexts, not a replacement for document admission. The actual
// caller must also report exactly one D7 notice, preserve source/history, and admit no Commit processing.
[[nodiscard]] inline UiCancellationRefusalKind ExpectedUiCancellationRefusal(int width, int height,
                                                                             std::size_t operation) noexcept
{
    if (operation != 6 && operation != 7)
        return UiCancellationRefusalKind::None;
    if (width == 10000 && height == 10000)
        return UiCancellationRefusalKind::OpeningPixelBudget;
    if (width == 8192 && height == 5464)
        return UiCancellationRefusalKind::SettingsPixelBudget;
    return UiCancellationRefusalKind::None;
}

struct UiCancellationRefusalFacts final
{
    UiCancellationRefusalKind kind = UiCancellationRefusalKind::None;
    std::uint64_t reportedBudgetNotices = 0;
    bool ownersSettled = false;
    bool sourceUnchanged = false;
    bool historyUnchanged = false;
    bool hasProcessingOwner = false;
    bool nativeTimer = false;
    bool pendingEdit = false;
    bool requestedGridRefused = false;
};

[[nodiscard]] inline bool IsUiCancellationBudgetRefusal(const UiCancellationRefusalFacts& facts) noexcept
{
    if (facts.reportedBudgetNotices != 1 || !facts.ownersSettled || !facts.sourceUnchanged || !facts.historyUnchanged ||
        facts.hasProcessingOwner || facts.nativeTimer)
        return false;
    switch (facts.kind)
    {
    case UiCancellationRefusalKind::OpeningPixelBudget:
        return !facts.pendingEdit && !facts.requestedGridRefused;
    case UiCancellationRefusalKind::SettingsPixelBudget:
        return facts.pendingEdit && facts.requestedGridRefused;
    default:
        return false;
    }
}
