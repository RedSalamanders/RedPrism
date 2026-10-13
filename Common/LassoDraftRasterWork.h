#pragma once

#include "BoundedLassoGesture.h"
#include "SelectionOutlineRasterWork.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

// Worker-owned preparation only. The immutable result contains BGRA premultiplied bytes and source/view
// metadata, never a D2D, WIC or GPU owner. Caller admits complete original source/output/temporary lifetime.
class PreparedLassoDraftRaster final
{
  public:
    using GridKey = PreparedSelectionOutlineRaster::GridKey;
    struct MemoryPlan final
    {
        std::uint64_t outputBytes = 0;
        std::uint64_t temporaryBytes = 0;
        std::uint64_t totalBytes = 0;
        // Explicit planning headroom, not a proved upper bound for D2D's private allocation/cancellation.
        std::uint64_t opaqueD2DAllowanceBytes = 0;
    };
    struct Observer final
    {
        void* context = nullptr;
        void (*checkpoint)(void*, std::uint64_t) noexcept = nullptr;
    };
    [[nodiscard]] static std::int32_t PlanMemory(const BoundedLassoGesture::Display& source, const GridKey& grid,
                                                 MemoryPlan& output) noexcept;
    [[nodiscard]] static std::int32_t Prepare(BoundedLassoGesture::Display source, GridKey grid,
                                              std::shared_ptr<const SharedPublicationMemory> outputMemory,
                                              CancelToken cancellation, Observer observer,
                                              PreparedLassoDraftRaster& output);
    [[nodiscard]] bool HasStorage() const noexcept
    {
        return bool(_storage);
    }
    [[nodiscard]] const BoundedLassoGesture::Display* Source() const noexcept;
    [[nodiscard]] const GridKey& Key() const noexcept;
    [[nodiscard]] std::span<const std::uint8_t> Pixels() const noexcept;
    [[nodiscard]] std::uint64_t DeclaredOutputBytes() const noexcept;
    [[nodiscard]] std::weak_ptr<const void> WeakStorageForTest() const noexcept;

  private:
    struct Storage;
    std::shared_ptr<const Storage> _storage;
};
