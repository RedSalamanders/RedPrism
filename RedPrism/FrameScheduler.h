#pragma once

#include <cstdint>

enum class HostFrameAction
{
    WaitForMessage,
    Render,
    DrainCompletions,
};

struct HostFrameState final
{
    bool windowVisible     = false;
    bool windowMinimized   = false;
    bool windowCloaked     = false;
    bool displayPoweredOn  = false;
    bool systemSuspended   = false;
    bool rendererSuspended = true;
    bool frameInvalidated  = true;
    bool chromeAnimating   = false;
    // `--screenshot` uses its owned capture window and requests frames even when desktop presentation is unavailable (RF-409).
    bool capturing          = false;
    bool completionsPending = false;
};

[[nodiscard]] constexpr bool CanRenderHostFrame(const HostFrameState& state) noexcept
{
    return state.capturing || (state.windowVisible && ! state.windowMinimized && ! state.windowCloaked && state.displayPoweredOn && ! state.systemSuspended &&
                               ! state.rendererSuspended);
}

[[nodiscard]] constexpr HostFrameAction SelectHostFrameAction(const HostFrameState& state) noexcept
{
    if (state.completionsPending)
    {
        return HostFrameAction::DrainCompletions;
    }

    if (! CanRenderHostFrame(state) || (! state.frameInvalidated && ! state.chromeAnimating))
    {
        return HostFrameAction::WaitForMessage;
    }
    return HostFrameAction::Render;
}

// Marching ants (UI_ApplicationChrome.md, Selection tools): the dash moves one DIP every 120 ms through its 8 DIP
// cycle, as the legacy EditorCanvas timer did. The frame loop wakes from a timed wait at the next step; nothing
// polls, and nothing wakes while no outline shows.
inline constexpr std::uint64_t kMarchingAntsStepMilliseconds = 120;
inline constexpr std::uint64_t kMarchingAntsCycleSteps       = 8;

[[nodiscard]] constexpr float MarchingAntsPhase(std::uint64_t tickMilliseconds) noexcept
{
    return static_cast<float>((tickMilliseconds / kMarchingAntsStepMilliseconds) % kMarchingAntsCycleSteps);
}

// Milliseconds from `tickMilliseconds` to the next step of the dash (1 to 120).
[[nodiscard]] constexpr std::uint32_t MillisecondsToNextAntStep(std::uint64_t tickMilliseconds) noexcept
{
    return static_cast<std::uint32_t>(kMarchingAntsStepMilliseconds - tickMilliseconds % kMarchingAntsStepMilliseconds);
}
