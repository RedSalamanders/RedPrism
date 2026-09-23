#pragma once

#include <cstdint>

enum class HostFrameAction
{
    WaitForMessage,
    ProbeOcclusion,
    Render,
};

struct HostFrameState final
{
    bool windowVisible = false;
    bool displayPoweredOn = false;
    bool rendererSuspended = true;
    bool rendererOccluded = false;
    bool occlusionStatusChanged = false;
    bool frameInvalidated = true;
    bool chromeAnimating = false;
    // The DXGI occlusion-status message is registered. Without it no message ever says the window is visible
    // again, so an occluded window probes (Present TEST) on each wake instead of waiting for one (RF-410).
    bool occlusionMessages = true;
    // `--screenshot`: the capture needs presented frames whatever the window's visibility, the display's power or
    // the occlusion state says (RF-409).
    bool capturing = false;
};

[[nodiscard]] constexpr HostFrameAction SelectHostFrameAction(const HostFrameState& state) noexcept
{
    const bool visible = state.windowVisible || state.capturing;
    const bool displayOn = state.displayPoweredOn || state.capturing;
    const bool suspended = state.rendererSuspended && !state.capturing;
    const bool occluded = state.rendererOccluded && !state.capturing;
    if (!visible || !displayOn || suspended)
    {
        return HostFrameAction::WaitForMessage;
    }
    if (occluded)
    {
        return state.occlusionStatusChanged || !state.occlusionMessages ? HostFrameAction::ProbeOcclusion
                                                                        : HostFrameAction::WaitForMessage;
    }
    if (!state.frameInvalidated && !state.chromeAnimating)
    {
        return HostFrameAction::WaitForMessage;
    }
    return HostFrameAction::Render;
}

// Marching ants (UI_ApplicationChrome.md, Selection tools): the dash moves one DIP every 120 ms through its 8 DIP
// cycle, as the legacy EditorCanvas timer did. The frame loop wakes from a timed wait at the next step; nothing
// polls, and nothing wakes while no outline shows.
inline constexpr std::uint64_t kMarchingAntsStepMilliseconds = 120;
inline constexpr std::uint64_t kMarchingAntsCycleSteps = 8;

[[nodiscard]] constexpr float MarchingAntsPhase(std::uint64_t tickMilliseconds) noexcept
{
    return static_cast<float>((tickMilliseconds / kMarchingAntsStepMilliseconds) % kMarchingAntsCycleSteps);
}

// Milliseconds from `tickMilliseconds` to the next step of the dash (1 to 120).
[[nodiscard]] constexpr std::uint32_t MillisecondsToNextAntStep(std::uint64_t tickMilliseconds) noexcept
{
    return static_cast<std::uint32_t>(kMarchingAntsStepMilliseconds - tickMilliseconds % kMarchingAntsStepMilliseconds);
}
