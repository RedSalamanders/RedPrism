#pragma once

#include "Document.h"
#include "SharedPublicationMemory.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

// Immutable worker-prepared CPU coverage. This type has no host, UI, GPU or D2D ownership.
// Prepare must run on an already COM-initialized worker with its source captured by value and its
// complete admission held by the caller. The final argument is unchanged on every failing HRESULT.
class PreparedSelectionOutlineRaster final
{
  public:
    static constexpr std::size_t PlaneCount = 9;
    static constexpr std::uint32_t MaximumSide = 16384;
    static constexpr std::uint64_t MaximumPixels = 64ull * 1024ull * 1024ull;

    struct GridKey final
    {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        double documentScalePixel = 0.0;
        // Pixel position of the cached source minimum bound. This remains double until target setup.
        double originPixelX = 0.0;
        double originPixelY = 0.0;
        float dpi = 96.0f;
        bool clipped = false;
        double clipLeft = 0.0;
        double clipTop = 0.0;
        double clipRight = 0.0;
        double clipBottom = 0.0;
        [[nodiscard]] bool operator==(const GridKey&) const noexcept = default;
    };

    struct MemoryPlan final
    {
        std::uint64_t outputBytes = 0;
        std::uint64_t temporaryBytes = 0;
        std::uint64_t totalBytes = 0;
        // Informational component included in temporaryBytes. It is planning headroom, not a proven
        // upper bound for Direct2D's private geometry/tessellation/driver allocations.
        std::uint64_t opaqueD2DAllowanceBytes = 0;
    };

    struct Observer final
    {
        void* context = nullptr;
        void (*checkpoint)(void*, std::uint64_t) noexcept = nullptr;
    };

    [[nodiscard]] static std::int32_t PlanMemory(const GridKey& grid, std::uint64_t loopCount, std::uint64_t pointCount,
                                                 MemoryPlan& output) noexcept;

    // Source retains its existing accounting owner. The caller supplies an owning, nonempty
    // SharedPublicationMemory with a sufficient immutable local allocation limit, created under the
    // original complete reservation. Output buffer/data and actual shared controls are charged to it.
    // A sealed or insufficient owner refuses before a result escapes. The worker runner
    // owns std::bad_alloc translation; this routine deliberately does not catch allocation exceptions.
    [[nodiscard]] static std::int32_t Prepare(SelectionLoops source, GridKey grid,
                                              std::shared_ptr<const SharedPublicationMemory> outputMemory,
                                              CancelToken cancellation, Observer observer,
                                              PreparedSelectionOutlineRaster& output);

    [[nodiscard]] bool HasStorage() const noexcept
    {
        return static_cast<bool>(_storage);
    }
    [[nodiscard]] const GridKey& Key() const noexcept;
    [[nodiscard]] const void* SourceIdentity() const noexcept;
    [[nodiscard]] const SelectionLoops& Source() const noexcept;
    [[nodiscard]] std::span<const std::uint8_t> Plane(std::size_t index) const noexcept;
    [[nodiscard]] std::uint64_t DeclaredOutputBytes() const noexcept;
    [[nodiscard]] std::weak_ptr<const void> WeakStorageForTest() const noexcept;

  private:
    struct Storage;
    std::shared_ptr<const Storage> _storage;
};
