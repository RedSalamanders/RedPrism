#pragma once

#include <cstdint>

// Pure timing policy for the host's one-shot busy-indicator timer. The host owns
// timer arming, frame requests, and visibility wiring.
[[nodiscard]] constexpr bool BusyIndicatorVisible(std::uint64_t startQpc, std::uint64_t nowQpc,
                                                  std::uint64_t frequency) noexcept
{
    if (frequency == 0 || nowQpc < startQpc)
    {
        return false;
    }

    const std::uint64_t elapsed = nowQpc - startQpc;
    const std::uint64_t threshold = frequency / 4 + (frequency % 4 == 0 ? 0 : 1);
    return elapsed >= threshold;
}

struct EndSessionWorkState final
{
    bool saving = false;
    bool exportWorker = false;
    bool exportGpu = false;
    bool clipboardWrite = false;
    bool critical = false;
};

struct EndSessionWorkPolicy final
{
    bool blockQuery;
    bool waitForWorkerWrites;
    bool cancelGpuExport;
    bool cancelClipboardWrite;
};

// Pure policy only: this does not perform an end-session wait, cancel work, or
// wire WM_QUERYENDSESSION / WM_ENDSESSION. The host performs any worker wait
// later, bounded by the end-session timeout.
[[nodiscard]] constexpr EndSessionWorkPolicy GetEndSessionWorkPolicy(EndSessionWorkState state) noexcept
{
    return {(state.saving || state.exportWorker) && !state.critical, state.saving || state.exportWorker,
            state.exportGpu, state.clipboardWrite};
}
