#pragma once

#include <cstdint>

enum class UiTimingKind : std::uint8_t;

// TraceLogging provider: RedPrism.Pipeline
// Stable provider GUID: {7B5D9C3E-42F1-4B76-9A2D-61E8C4F037A5}.
// Initialize and close on the single owner thread. All producers must be joined before ClosePipelineTrace;
// initialization/close must not race any producer call.
[[nodiscard]] std::int32_t InitializePipelineTrace() noexcept;
void ClosePipelineTrace() noexcept;

// Call at the actual destruction point. UiFree is emitted only for at least 1 MiB and on the initialization thread.
void TracePipelineFree(std::uint64_t bytes) noexcept;

// Optional memory accounting event. The event text is payload data; the ETW event name is always "Memory".
void TracePipelineMemory(const char* event, std::uint64_t requested, std::uint64_t reserved,
                         std::uint64_t releasing) noexcept;

// Owner-thread ETW timing: no clock reads while this keyword is disabled. Message/dispatch/drain events emit
// only above 4 ms; result apply, UI actions and frames emit for every operation while the keyword is enabled.
[[nodiscard]] bool PipelineUiTimingEnabled() noexcept;
void TracePipelineUi(UiTimingKind kind, std::uint64_t durationUs, std::uint32_t detail) noexcept;
