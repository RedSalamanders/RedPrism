#pragma once

#include "Jobs.h"

#include <atomic>
#include <memory>
#include <windows.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

enum class ColorWorkTestKind
{
    Preview,
    Histogram,
    Commit
};

// Harness-owned events travel with the submitted job. Production submissions have no gate and never wait here.
struct ColorWorkTestGate final
{
    wil::unique_handle entered{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    wil::unique_handle release{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    wil::unique_handle computed{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    ColorWorkTestKind kind = ColorWorkTestKind::Preview;
    JobResult releasedResult{}; // immutable after submission; harness can inject a runner failure
    bool throwAllocationFailure = false;
    std::atomic<bool> unchangedCommit{false};
    std::atomic<unsigned int> visits{0};
};

[[nodiscard]] JobResult PassColorWorkTestGate(const std::shared_ptr<ColorWorkTestGate>& gate, ColorWorkTestKind kind,
                                              JobContext& context) noexcept;
