#pragma once

#include "Document.h"
#include "SharedPublicationMemory.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include <d2d1.h>

// Immutable worker-prepared numeric display transport. This prepares numeric points only; no factory, sink, GPU or
// document mutation. Callers must admit source, output, control and retirement before invoking Prepare on a worker.
struct WandLayerSelectionMemoryPlan;

class PreparedSelectionOutline final
{
  public:
    using Loop = std::vector<D2D1_POINT_2F>;
    using Loops = std::vector<Loop>;
    struct Observer final
    {
        void* context = nullptr;
        void (*checkpoint)(void*, std::uint64_t) noexcept = nullptr;
    };

    [[nodiscard]] static std::optional<std::uint64_t> OutputMemory(const SelectionLoops& source) noexcept;
    [[nodiscard]] static std::optional<std::uint64_t> OutputMemory(std::uint64_t loops, std::uint64_t points) noexcept;
    // Original Wand trace/boolean bounds cover headers/points. Add the publication node/control allowance.
    [[nodiscard]] static std::optional<std::uint64_t> AdmissionMemory(
        const WandLayerSelectionMemoryPlan& plan) noexcept;
    [[nodiscard]] bool HasStorage() const noexcept
    {
        return static_cast<bool>(_storage);
    }
    // Output ownership must be nonempty (an owning null alias is valid). Binding the original ticket is the host's job.
    [[nodiscard]] static std::optional<PreparedSelectionOutline> Prepare(
        SelectionLoops source, std::shared_ptr<const SharedPublicationMemory> outputMemory,
        CancelToken cancellation = {}, Observer observer = {});

    [[nodiscard]] const Loops& Points() const noexcept;
    [[nodiscard]] const void* SourceIdentity() const noexcept;
    [[nodiscard]] Vec2 Anchor() const noexcept;
    [[nodiscard]] std::uint64_t PointCount() const noexcept;
    [[nodiscard]] std::uint64_t DeclaredOutputBytes() const noexcept;
    [[nodiscard]] std::weak_ptr<const void> WeakStorageForTest() const noexcept;

  private:
    struct Storage;
    std::shared_ptr<const Storage> _storage;
};
